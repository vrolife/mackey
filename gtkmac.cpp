#include <assert.h>
#include <dlfcn.h>

#include <string>
#include <map>
#include <mutex>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "frida-gum.h"

// #define LOG printf
#define LOG(...)

struct _AccelHook {
    GObject parent;
};

typedef struct _AccelHook AccelHook;

static void accel_hook_iface_init (gpointer g_iface, gpointer iface_data);

#define ACCEL_HOOK_LISTENER (accel_hook_get_type())
G_DECLARE_FINAL_TYPE(AccelHook, accel_hook, ACCELHOOK, LISTENER, GObject)
G_DEFINE_TYPE_EXTENDED(AccelHook, accel_hook, G_TYPE_OBJECT, 0, 
    G_IMPLEMENT_INTERFACE(GUM_TYPE_INVOCATION_LISTENER, accel_hook_iface_init))

static GumInterceptor* interceptor;
static GumInvocationListener* listener;

static void* XNextEvent_ptr;
static void* xcb_wait_for_event_ptr;
static void* xcb_poll_for_event_ptr;

__attribute__((constructor))
static void load() 
{
    gum_init_embedded();
    interceptor = gum_interceptor_obtain();
    listener = reinterpret_cast<GumInvocationListener*>(g_object_new(ACCEL_HOOK_LISTENER, nullptr));
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
accel_hook_on_enter (GumInvocationListener * listener, GumInvocationContext * ic)
{
    AccelHook* self = ACCELHOOK_LISTENER (listener);
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
accel_hook_on_leave (GumInvocationListener * listener,
                           GumInvocationContext * ic)
{
    AccelHook* self = ACCELHOOK_LISTENER (listener);
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
accel_hook_class_init (AccelHookClass * klass)
{
}

static void
accel_hook_iface_init (gpointer g_iface,
                             gpointer iface_data)
{
  auto* iface = reinterpret_cast<GumInvocationListenerInterface*>(g_iface);

  iface->on_enter = accel_hook_on_enter;
  iface->on_leave = accel_hook_on_leave;
}

static void
accel_hook_init (AccelHook * self)
{
}
