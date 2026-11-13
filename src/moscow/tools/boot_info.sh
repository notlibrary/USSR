#!/bin/sh
# boot_info.sh ISO - writes El Torito's boot information table into the
# boot file of ISO, as mkisofs -boot-info-table does: bytes 8 .. 23 of the
# file hold the primary volume descriptor's LBA (16), the file's LBA, its
# length and the 32-bit sum of its words from byte 64 on. moscow.mk does it
# itself because xorriso (libisofs) cannot for a boot file as big as
# ussr.img ("Boot image to large to buffer"). Stage 1 of boot.nasm reads
# the LBA and the length (ELTORITO_TABLE) to find the disk image on the CD.
set -e
iso=$1
[ -f "$iso" ] || { echo "usage: boot_info.sh ISO" >&2; exit 2; }

u32() { od -An -tu4 -j "$2" -N4 "$1" | tr -d ' '; }
u8() { od -An -tu1 -j "$2" -N1 "$1" | tr -d ' '; }

# the boot record volume descriptor, from sector 16 to the terminator
sector=16
catalog=
while :; do
    off=$((sector * 2048))
    type=$(u8 "$iso" "$off")
    [ -n "$type" ] && [ "$type" != 255 ] || break
    if [ "$type" = 0 ] && [ "$(dd if="$iso" bs=1 skip=$((off + 7)) count=23 2>/dev/null)" = "EL TORITO SPECIFICATION" ]; then
        catalog=$(u32 "$iso" $((off + 71)))
    fi
    sector=$((sector + 1))
done
[ -n "$catalog" ] || { echo "boot_info.sh: no El Torito boot record in $iso" >&2; exit 1; }
# the initial entry: the boot file's LBA at 8 .. 11; its length is the
# size of the file the El Torito image was made from, as xorriso reports
lba=$(u32 "$iso" $((catalog * 2048 + 32 + 8)))
length=$(xorriso -indev "$iso" -find /boot/ussr.img -exec report_lba -- 2>/dev/null |
    awk -F, '/ussr\.img/ { gsub(/ /, "", $4); print $4; exit }')
[ -n "$length" ] || { echo "boot_info.sh: no /boot/ussr.img in $iso" >&2; exit 1; }
start=$((lba * 2048))
# the sum of the 32-bit words from byte 64 to the end, modulo 2^32
sum=$(dd if="$iso" bs=2048 skip="$lba" count=$(((length + 2047) / 2048)) 2>/dev/null |
    head -c "$length" | tail -c +65 | od -An -tu4 -v |
    awk '{ for (i = 1; i <= NF; i++) s = (s + $i) % 4294967296 } END { printf "%d\n", s }')

le32() {
    v=$1
    printf "\\$(printf %o $((v & 255)))\\$(printf %o $(((v >> 8) & 255)))"
    printf "\\$(printf %o $(((v >> 16) & 255)))\\$(printf %o $(((v >> 24) & 255)))"
}
{ le32 16; le32 "$lba"; le32 "$length"; le32 "$sum"; } |
    dd of="$iso" bs=1 seek=$((start + 8)) conv=notrunc 2>/dev/null
echo "boot_info.sh: /boot/ussr.img at LBA $lba, $length bytes"
