#!/bin/sh
# Builds the Moscow boot disk, build/moscow/moscow.img, from the
# repository root (or anywhere: paths are taken from this script):
#
#     sh src/moscow/build.sh [ussr]
#
# Steps: boot.nasm -> boot.bin, ussr_rt.nasm -> ussr_rt.bin, moscow.su
# and russia.su -> .ru with `ussr -c`, then image.nasm puts them together.
# Needs nasm (NASM=path to override) and a ussr binary (first argument,
# else ./ussr or ./ussr.exe at the root).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
arch=$here/arch/amd64
out=$root/build/moscow

USSR=${1:-}
if [ -z "$USSR" ]; then
    if [ -x "$root/ussr" ]; then USSR=$root/ussr; else USSR=$root/ussr.exe; fi
fi
NASM=${NASM:-nasm}
if ! command -v "$NASM" >/dev/null 2>&1; then
    for n in "$LOCALAPPDATA/bin/NASM/nasm.exe" "/c/Program Files/NASM/nasm.exe"; do
        [ -x "$n" ] && NASM=$n && break
    done
fi

mkdir -p "$out"
"$NASM" -f bin -i "$arch/" "$arch/boot.nasm" -o "$out/boot.bin"
"$NASM" -f bin -i "$arch/" "$arch/ussr_rt.nasm" -o "$out/ussr_rt.bin"
# no startup file may change what gets compiled
(cd "$here" && USSR_KIEV= "$USSR" -c moscow.su "$out/moscow.ru")
(cd "$here/../russia" && USSR_KIEV= "$USSR" -c russia.su "$out/russia.ru")
"$NASM" -f bin -i "$out/" -i "$arch/" "$arch/image.nasm" -o "$out/moscow.img"
echo "build/moscow/moscow.img: $(wc -c < "$out/moscow.img") bytes"
