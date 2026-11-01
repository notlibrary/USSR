#!/bin/sh
# Boots build/moscow/ussr.img, the install disk, in QEMU: the VGA screen
# in a window and the first serial port on this terminal; type into
# either. With a disk image as the first argument it boots that instead
# (an installed disk, or moscow.img, the boot image alone); --hdb FILE
# adds FILE as the second disk, /dev/hdb, the one to install onto (a raw
# image, or a .qcow2 virtual hard disk).
#
#     sh src/moscow/build.sh && sh src/moscow/run.sh [disk.img] [--hdb FILE] [qemu options]
#     qemu-img create -f qcow2 hdd.qcow2 128M && sh src/moscow/run.sh --hdb hdd.qcow2
#
# Ctrl-D ends the shell, then the kernel halts and QEMU quits (isa-debug-exit).
here=$(cd "$(dirname "$0")" && pwd)
img=$here/../../build/moscow/ussr.img
case "$1" in
    ""|-*) ;;
    *) img=$1; shift ;;
esac
hdb=
if [ "$1" = --hdb ]; then
    fmt=raw
    case "$2" in *.qcow2) fmt=qcow2 ;; esac
    hdb="-drive format=$fmt,index=1,media=disk,file=$2"
    shift 2
fi
QEMU=${QEMU:-qemu-system-x86_64}
if ! command -v "$QEMU" >/dev/null 2>&1 && [ -x "/c/Program Files/qemu/qemu-system-x86_64.exe" ]; then
    QEMU="/c/Program Files/qemu/qemu-system-x86_64.exe"
fi
# $hdb is unquoted on purpose: it is two words or none
exec "$QEMU" -drive format=raw,index=0,media=disk,file="$img" $hdb -m 2048 -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=4 -serial stdio "$@"
