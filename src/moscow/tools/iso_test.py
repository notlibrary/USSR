#!/usr/bin/env python3
"""Reads an ISO 9660 image's Joliet tree with no ISO library and compares it with
a folder: every file there, under its full long name, with the same bytes. Then
its El Torito boot record: a no-emulation boot file with a boot sector, that the
BIOS loads 16 sectors of, and a boot information table saying where it lies.

    python src/moscow/tools/iso_test.py build/moscow/ussr.iso build/moscow/iso

It checks what a Joliet reader relies on: a primary volume descriptor, a
supplementary one with a UCS-2 escape sequence, and names longer than plain ISO
9660 can hold. Exits nonzero on any difference.
"""
import hashlib
import os
import sys

SECTOR = 2048
UCS2_ESCAPES = (b"%/@", b"%/C", b"%/E")  # levels 1, 2 and 3
# what the build adds that is not in the folder
BOOT_FILES = ("boot/ussr.img", "boot/boot.catalog")


def descriptors(f):
    """-> the primary volume descriptor and the Joliet one."""
    primary = joliet = None
    sector = 16
    while True:
        f.seek(sector * SECTOR)
        d = f.read(SECTOR)
        if len(d) < SECTOR or d[1:6] != b"CD001":
            raise SystemExit("sector %d is not a volume descriptor" % sector)
        if d[0] == 255:
            break
        if d[0] == 1:
            primary = d
        elif d[0] == 2 and d[88:91] in UCS2_ESCAPES:
            joliet = d
        sector += 1
    return primary, joliet


def walk(f, extent, length, path, files):
    """The directory at extent: its files into files, path -> (extent, size)."""
    f.seek(extent * SECTOR)
    data = f.read(length)
    at = 0
    while at < len(data):
        size = data[at]
        if size == 0:
            at = (at // SECTOR + 1) * SECTOR  # records do not cross a sector
            continue
        rec = data[at:at + size]
        at += size
        child = int.from_bytes(rec[2:6], "little")
        bytes_ = int.from_bytes(rec[10:14], "little")
        flags = rec[25]
        raw = rec[33:33 + rec[32]]
        if raw in (b"\0", b"\1"):
            continue
        name = raw.decode("utf-16-be")
        if flags & 2:
            walk(f, child, bytes_, path + name + "/", files)
        else:
            name = name.split(";")[0]
            if path + name in files:
                raise SystemExit("two records for %s" % (path + name))
            # a file over 4 GiB would be in several records: none here
            if flags & 0x80:
                raise SystemExit("%s has several extents" % (path + name))
            files[path + name] = (child, bytes_)


def boot_record(f):
    """Checks the El Torito boot: a list of what is wrong with it."""
    sector = 16
    catalog = None
    while True:
        f.seek(sector * SECTOR)
        d = f.read(SECTOR)
        if len(d) < SECTOR or d[1:6] != b"CD001" or d[0] == 255:
            break
        if d[0] == 0 and d[7:30] == b"EL TORITO SPECIFICATION":
            catalog = int.from_bytes(d[71:75], "little")
        sector += 1
    if catalog is None:
        return ["no El Torito boot record"]
    f.seek(catalog * SECTOR)
    c = f.read(64)
    wrong = []
    if c[0] != 1 or c[30:32] != b"\x55\xaa":
        wrong.append("the boot catalog has no validation entry")
    if sum(int.from_bytes(c[i:i + 2], "little") for i in range(0, 32, 2)) & 0xffff:
        wrong.append("the boot catalog's checksum is wrong")
    if c[32] != 0x88:
        wrong.append("the initial entry is not bootable")
    if c[33] != 0:
        wrong.append("the boot is not no-emulation")
    if int.from_bytes(c[38:40], "little") != 16:
        wrong.append("the BIOS does not load 16 sectors")
    lba = int.from_bytes(c[40:44], "little")
    f.seek(lba * SECTOR)
    head = f.read(512)
    if head[510:512] != b"\x55\xaa":
        wrong.append("the boot file has no boot sector")
    if int.from_bytes(head[12:16], "little") != lba:
        wrong.append("the boot information table does not say where the file is")
    return wrong


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    iso, tree = sys.argv[1:]
    with open(iso, "rb") as f:
        primary, joliet = descriptors(f)
        if primary is None:
            print("FAIL no primary volume descriptor")
            return 1
        if joliet is None:
            print("FAIL no Joliet descriptor")
            return 1
        root = joliet[156:190]
        files = {}
        walk(f, int.from_bytes(root[2:6], "little"), int.from_bytes(root[10:14], "little"), "", files)
        want = []
        for folder, _, names in os.walk(tree):
            for name in names:
                full = os.path.join(folder, name)
                want.append(os.path.relpath(full, tree).replace(os.sep, "/"))
        failed = 0
        for path in want:
            if path not in files:
                print("FAIL missing in the image: %s" % path)
                failed += 1
                continue
            extent, size = files[path]
            with open(os.path.join(tree, path), "rb") as local:
                bytes_ = local.read()
            f.seek(extent * SECTOR)
            if size != len(bytes_) or sha(f.read(size)) != sha(bytes_):
                print("FAIL different bytes: %s" % path)
                failed += 1
        for why in boot_record(f):
            print("FAIL %s" % why)
            failed += 1
    extra = set(files) - set(want) - set(BOOT_FILES)
    for path in sorted(extra):
        print("FAIL not in the folder: %s" % path)
        failed += 1
    longest = max((p.split("/")[-1] for p in files), key=len, default="")
    if len(longest) <= 31:
        print("FAIL no long name in it to show Joliet works: the longest is %s" % longest)
        failed += 1
    label = joliet[40:72].decode("utf-16-be", "replace").strip()
    print("%s: volume %s, %d files, the longest name %d characters: %s"
          % ("FAIL" if failed else "PASS", label, len(files), len(longest), longest))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
