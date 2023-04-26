#!/bin/sh

if test $(id -u) -ne 0; then
    echo "Danger !!!"
    exit 1
fi

LIB=$PWD/cmake-build/libmackey.so
DST=/home/$SUDO_USER/.config/gtk-3.0/libmackey.so

cp $LIB $DST

llvm-strip -x $DST

chown root:root $DST
chmod 755 $DST

flatpak override --filesystem=xdg-config/gtk-3.0:ro --env=LD_PRELOAD=$DST org.chromium.Chromium
