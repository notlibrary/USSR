#!/bin/sh
# Builds the Moscow disks from the repository root (or anywhere: paths
# are taken from this script):
#
#     sh src/moscow/build.sh [ussr]
#
# build/moscow/moscow.img  the boot image alone: boot.nasm -> boot.bin,
#                          ussr_rt.nasm -> ussr_rt.bin, moscow.su and
#                          russia.su -> .ru with `ussr -c`, put together
#                          by image.nasm
# build/moscow/ussr.img    the install disk: an MBR disk whose first 4 MiB
#                          hold the boot image and whose one partition is
#                          a Belarus file system with the programs (/bin),
#                          the boot files (/boot), /etc and the whole
#                          project (/src/ussr, the files git tracks)
#
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
# Git for Windows rewrites arguments that look like paths ("/bin" becomes
# a folder of Git's): ussr's arguments go through as they are (run_ussr),
# with host paths in Windows form (hp)
run_ussr() {
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' USSR_KIEV= "$USSR" "$@"
}
hp() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

mkdir -p "$out"
"$NASM" -f bin -i "$arch/" "$arch/boot.nasm" -o "$out/boot.bin"
"$NASM" -f bin -i "$arch/" "$arch/ussr_rt.nasm" -o "$out/ussr_rt.bin"
# no startup file may change what gets compiled
(cd "$here" && run_ussr -c moscow.su "$(hp "$out/moscow.ru")")
(cd "$here" && run_ussr -c install.su "$(hp "$out/install.ru")")
(cd "$here/../russia" && run_ussr -c russia.su "$(hp "$out/russia.ru")")
(cd "$here/../stalin" && run_ussr -c stalin.su "$(hp "$out/stalin.ru")")
(cd "$here/../belarus" && run_ussr -c belarus.su "$(hp "$out/belarus.ru")")
"$NASM" -f bin -i "$out/" -i "$arch/" "$arch/image.nasm" -o "$out/moscow.img"
echo "build/moscow/moscow.img: $(wc -c < "$out/moscow.img") bytes"

# the install disk
boot=$(wc -c < "$out/moscow.img")
if [ "$boot" -gt $((4 * 1048576)) ]; then
    echo "build.sh: the boot image is over 4 MiB, the room in front of the partition" >&2
    exit 1
fi
img=$out/ussr.img
rm -f "$img"
stalin() { (cd "$root" && run_ussr src/stalin/stalin.su "$@" >/dev/null); }
belarus() { (cd "$root" && run_ussr src/belarus/belarus.su "$@"); }
STALIN_SEED=1917
export STALIN_SEED
stalin mkimg "$(hp "$img")" 64M
stalin init "$(hp "$img")" mbr
stalin add "$(hp "$img")" --start 4M --type belarus --boot on
# the boot image in front of the partition: the MBR's boot code (its
# first 440 bytes; the partition table stays), then sectors 1 ..
dd if="$out/moscow.img" of="$img" bs=440 count=1 conv=notrunc 2>/dev/null
dd if="$out/moscow.img" of="$img" bs=512 skip=1 seek=1 count=$((boot / 512 - 1)) conv=notrunc 2>/dev/null
dev=$(hp "$img")
belarus mkfs "$dev" --part 1 --label ussr-install
for f in ussr_rt.bin moscow.ru russia.ru; do
    belarus put "$dev" "$(hp "$out/$f")" "/boot/$f"
done
for f in install stalin belarus; do
    belarus put "$dev" "$(hp "$out/$f.ru")" "/bin/$f.ru"
done
belarus write "$dev" /etc/hostname "moscow-install"
belarus write "$dev" /etc/motd "USSR install disk: install(_): puts USSR on a disk of its own; help(_): lists the commands."
# the project: the files git tracks (every file in the tree when this is
# no git checkout), never build products - objects, binaries, packages,
# disk images - whichever way they got there
list=$out/files.txt
if git -C "$root" ls-files >"$list.all" 2>/dev/null && [ -s "$list.all" ]; then :; else
    (cd "$root" && find . \( -path ./build -o -path ./.git -o -path ./.claude \
        -o -path ./autom4te.cache \) -prune -o -type f -print | sed 's|^\./||') >"$list.all"
fi
grep -viE '\.(obj|o|a|ural|exe|dll|so|lib|pdb|ilk|exp|res|msi|wixpdb|img|qcow2|iso|ru|tmp|log)$' \
    "$list.all" >"$list"
rm -f "$list.all"
belarus putlist "$dev" "$(hp "$list")" /src/ussr
belarus fsck "$dev"
echo "build/moscow/ussr.img: $(wc -c < "$img") bytes"
