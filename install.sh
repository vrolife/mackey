#!/bin/sh

if test $(id -u) -ne 0; then
    echo "Danger !!!"
    exit 1
fi

LIB="$PWD/cmake-build/libmackey.so"
DST="/home/$SUDO_USER/.config/gtk-3.0/mackey"

mkdir -p "$DST"

cp $LIB $DST

llvm-strip -x $DST/libmackey.so

chown root:root $DST $DST/libmackey.so
chmod 755 $DST
chmod 644 $DST/libmackey.so

flatpak override --filesystem=xdg-config/gtk-3.0:ro --env=LD_PRELOAD=$DST/libmackey.so org.chromium.Chromium
