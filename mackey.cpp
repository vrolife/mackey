#include <assert.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>

#include <string>
#include <map>
#include <mutex>
#include <filesystem>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "frida-gum.h"

// #define LOG printf
#define LOG(...)

namespace fs = std::filesystem;

struct _MacKey {
    GObject parent;
};

typedef struct _MacKey MacKey;

static void mackey_iface_init (gpointer g_iface, gpointer iface_data);

#define MACKEY_LISTENER (mackey_get_type())
G_DECLARE_FINAL_TYPE(MacKey, mackey, ACCELHOOK, LISTENER, GObject)
G_DEFINE_TYPE_EXTENDED(MacKey, mackey, G_TYPE_OBJECT, 0, 
    G_IMPLEMENT_INTERFACE(GUM_TYPE_INVOCATION_LISTENER, mackey_iface_init))

static GumInterceptor* interceptor;
static GumInvocationListener* listener;

static void* XNextEvent_ptr;
static void* xcb_wait_for_event_ptr;
static void* xcb_poll_for_event_ptr;

void remove_chrome_ntp_promo() 
{
    FILE* fp = fopen("/proc/self/maps", "rb");
    if (fp == nullptr) {
        return;
    }

    char line[BUFSIZ];

    while (fgets(line, sizeof(line), fp) != nullptr) 
    {
        uintptr_t begin { 0 };
        uintptr_t end { 0 };
        char r { 0 };
        char w { 0 };
        char x { 0 };
        char p { 0 };
        uintptr_t offset { 0 };
        int major { 0 };
        int minor { 0 };
        unsigned long inode { 0 };
        char name[BUFSIZ];

        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %c%c%c%c %" SCNxPTR " %x:%x %lu %s", &begin, &end, &r, &w, &x, &p, &offset, &major, &minor, &inode, name) > 10) 
        {
            fs::path path{name};
            if (path.filename() == "chrome")
            {
                auto* p = memmem(reinterpret_cast<void*>(begin), end - begin, "/async/newtab_promos", 20);
                if (p) {
                    void* page = reinterpret_cast<void*>(reinterpret_cast<size_t>(p) & (~0xFFF));
                    size_t size = sysconf(_SC_PAGE_SIZE) * 2;
                    if (mprotect(page, size, PROT_READ | PROT_WRITE) != -1) {
                        memcpy(p, "/fxck", 6);
                        mprotect(page, size, PROT_READ);
                    }
                }
            }
        }
    }

    fclose(fp);
}

__attribute__((constructor))
static void load() 
{
    gum_init_embedded();
    interceptor = gum_interceptor_obtain();
    listener = reinterpret_cast<GumInvocationListener*>(g_object_new(MACKEY_LISTENER, nullptr));
    gum_interceptor_begin_transaction (interceptor);
    
    {
        void* x11 = dlopen("libX11.so", RTLD_LAZY);
        XNextEvent_ptr = dlsym(x11, "XNextEvent");

        LOG("XNextEvent %p\n", XNextEvent_ptr);

        if (XNextEvent_ptr) {
            gum_interceptor_attach(interceptor, 
                XNextEvent_ptr,
                listener, 
                XNextEvent_ptr);
        }
    }
    
    {
        void* xcb = dlopen("libxcb.so", RTLD_LAZY);
        xcb_wait_for_event_ptr = dlsym(xcb, "xcb_wait_for_event");

        LOG("xcb_wait_for_event %p\n", xcb_wait_for_event_ptr);

        if (xcb_wait_for_event_ptr) {
            gum_interceptor_attach(interceptor, 
                xcb_wait_for_event_ptr,
                listener, 
                xcb_wait_for_event_ptr);
        }

        xcb_poll_for_event_ptr = dlsym(xcb, "xcb_poll_for_event");

        LOG("xcb_poll_for_event %p\n", xcb_poll_for_event_ptr);

        if (xcb_poll_for_event_ptr) {
            gum_interceptor_attach(interceptor, 
                xcb_poll_for_event_ptr,
                listener, 
                xcb_poll_for_event_ptr);
        }
    }

    gum_interceptor_end_transaction (interceptor);

    // disable chrome ad
    remove_chrome_ntp_promo();
}

static std::mutex syms_lock;
static xcb_key_symbols_t* syms{nullptr};
static xcb_connection_t* conn{nullptr};

static void init_syms() {
    std::lock_guard<std::mutex> lock(syms_lock);
    if (syms != nullptr) {
        return;
    }

    conn = xcb_connect(NULL, NULL);
    assert(conn);
    syms = xcb_key_symbols_alloc(conn);
}

static void translate_xcb_key_press_pre(xcb_connection_t* conn)
{
    if (syms == nullptr) {
        init_syms();
    }
}

static void translate_xcb_key_press_post(xcb_connection_t* conn, xcb_key_press_event_t* key)
{
    // LOG("%p %hhu %d\n", conn, key->detail, key->state);

    auto keysym = xcb_key_press_lookup_keysym(syms, key, 0);

    LOG("key %d state %d\n", keysym, key->state);

#define MOD_SHIFT XCB_MOD_MASK_SHIFT
#define MOD_LOCK XCB_MOD_MASK_LOCK
#define MOD_CTRL XCB_MOD_MASK_CONTROL
#define MOD_ALT XCB_MOD_MASK_1
#define MOD_SUPER XCB_MOD_MASK_4

    // swap SUPER CTRL
    // if (key->state & MOD_SUPER) {
    //     key->state = (key->state & ~MOD_SUPER) | MOD_CTRL;
    // }

    switch(key->state) {
        case MOD_SUPER:
            switch(keysym) {
                // case XK_z:
                // case XK_x:
                // case XK_c:
                // case XK_v:
                case XK_l:
                case XK_r:
                case XK_t:
                case XK_w:
                case XK_q:
                case XK_f:
                    key->state = MOD_CTRL;
                    break;
            }
            break;
        case (MOD_SUPER | MOD_SHIFT):
            switch(keysym) {
                case XK_t:
                    key->state = MOD_CTRL | MOD_SHIFT;
                    break;
            }
            break;
    }
}


static void
mackey_on_enter (GumInvocationListener * listener, GumInvocationContext * ic)
{
    MacKey* self = ACCELHOOK_LISTENER (listener);
    void* data = GUM_IC_GET_FUNC_DATA (ic, void*);
    if (data == xcb_wait_for_event_ptr) 
    {
        auto *conn = reinterpret_cast<xcb_connection_t*>(gum_invocation_context_get_nth_argument(ic, 0));
        translate_xcb_key_press_pre(conn);

    } else if (data == xcb_poll_for_event_ptr) 
    {
        auto *conn = reinterpret_cast<xcb_connection_t*>(gum_invocation_context_get_nth_argument(ic, 0));
        translate_xcb_key_press_pre(conn);
    }
}

static void
mackey_on_leave (GumInvocationListener * listener,
                           GumInvocationContext * ic)
{
    MacKey* self = ACCELHOOK_LISTENER (listener);
    void* data = GUM_IC_GET_FUNC_DATA (ic, void*);
    if (data == XNextEvent_ptr)
    {
        auto* event = reinterpret_cast<XEvent*>(gum_invocation_context_get_nth_argument(ic, 1));
        if (event and event->type == KeyPress) {
            LOG("XNextEvent: KeyPress\n");
        }

    } else if (data == xcb_wait_for_event_ptr) 
    {
        auto *conn = reinterpret_cast<xcb_connection_t*>(gum_invocation_context_get_nth_argument(ic, 0));
        auto *event = reinterpret_cast<xcb_generic_event_t*>(gum_invocation_context_get_return_value(ic));
        if (event and event->response_type == XCB_KEY_PRESS) {
            auto* key = reinterpret_cast<xcb_key_press_event_t*>(event);
            LOG("leave xcb_wait_for_event: XCB_KEY_PRESS\n");
            translate_xcb_key_press_post(conn, key);
        }

    } else if (data == xcb_poll_for_event_ptr) 
    {
        auto *conn = reinterpret_cast<xcb_connection_t*>(gum_invocation_context_get_nth_argument(ic, 0));
        auto *event = reinterpret_cast<xcb_generic_event_t*>(gum_invocation_context_get_return_value(ic));
        if (event and event->response_type == XCB_KEY_PRESS) {
            auto* key = reinterpret_cast<xcb_key_press_event_t*>(event);
            LOG("leave xcb_poll_for_event: XCB_KEY_PRESS\n");
            translate_xcb_key_press_post(conn, key);
        }
    }
}

static void
mackey_class_init (MacKeyClass * klass)
{
}

static void
mackey_iface_init (gpointer g_iface,
                             gpointer iface_data)
{
  auto* iface = reinterpret_cast<GumInvocationListenerInterface*>(g_iface);

  iface->on_enter = mackey_on_enter;
  iface->on_leave = mackey_on_leave;
}

static void
mackey_init (MacKey * self)
{
}
