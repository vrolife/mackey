## Build

```bash
sudo apt install libxcb-keysyms1-dev

(
    mkdir $(uname -p)
    cd $(uname -p)
    wget wget https://github.com/frida/frida/releases/download/16.0.18/frida-gum-devkit-16.0.18-linux-x86_64-musl.tar.xz
    tar xvf frida-gum-devkit-16.0.18-linux-x86_64-musl.tar.xz
)

cmake -S . -B cmake-build
cmake --build cmake-build
```

## Install

see install.sh
