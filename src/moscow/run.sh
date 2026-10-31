#!/bin/sh
# Boots build/moscow/moscow.img in QEMU: the VGA screen in a window and
# the first serial port on this terminal; type into either.
#
#     sh src/moscow/build.sh && sh src/moscow/run.sh [qemu options]
#
# exit, then the kernel halts and QEMU quits (isa-debug-exit).
here=$(cd "$(dirname "$0")" && pwd)
img=$here/../../build/moscow/moscow.img
QEMU=${QEMU:-qemu-system-x86_64}
if ! command -v "$QEMU" >/dev/null 2>&1 && [ -x "/c/Program Files/qemu/qemu-system-x86_64.exe" ]; then
    QEMU="/c/Program Files/qemu/qemu-system-x86_64.exe"
fi
exec "$QEMU" -drive format=raw,file="$img" -m 128 -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=4 -serial stdio "$@"
