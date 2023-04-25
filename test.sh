LIB=$PWD/cmake-build/libgtkmac.so
cp $LIB $HOME/.config/gtk-3.0/

flatpak run --command=/app/chromium/chrome --env=LD_PRELOAD=$HOME/.config/gtk-3.0/libgtkmac.so org.chromium.Chromium
