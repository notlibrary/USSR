# Changelog

## 0.2.0
- Refine pass 1, in `more.su`: `verify` (overlaps, alignment, CRCs, the
  backup GPT's place, the protective MBR, the array at LBA 2 and 128
  entries, an EFI system partition with FAT or a BIOS boot partition),
  `backup` and `restore` in `sfdisk --dump` text, `sort`, `newuuids`,
  `mbr2gpt` and `gpt2mbr`.
- Images: `grow` (the backup GPT moved to the new end), `shrink`, `copy`
  with a checksum check, `compare` sector by sector.
- Partition data: `dump`, `load`, `zero [--random]`; whole extents move
  through `bcopy`, not byte vectors.
- Layouts: `layout DEV uefi-linux|bios-gpt|windows|moscow` (smaller EFI
  partitions on disks under 4 GiB) and `apply DEV FILE`.
- `mkfs DEV N fat32 [LABEL]`, `fsinfo` (used and free space in FAT32 and
  ext2/3/4), `eltorito` (an ISO's boot catalog), `explain` (every MBR and
  GPT header field), `hexdump`.
- `undo`: each writing command keeps the sectors it replaced beside an
  image.
- Options: `--json` for `show` and `verify`, `--end POS`, `--size 50%`,
  named `--attrs`, `--random`.
- Probes: LUKS, LVM PV, mdraid, ReFS, F2FS, HFS+, APFS, ISO 9660, UDF, ZFS.
- 24 more type names: ChromeOS, Android, Solaris, VMware, Ceph, Apple, LUKS
  and the Linux discoverable partitions (root by architecture, /usr, /srv,
  /var, /var/tmp).
- Tests: `tests/stalin_more_test.su`, including corrupt headers and arrays
  and random first sectors.

## 0.1.0
- First version: lists storage devices (HDD, SSD, flash) and edits GPT and
  MBR partition tables on raw disks and disk images through the ussr
  builtins `bread`, `bwrite`, `bsize` and `bdevices`.
- Commands: `list show mkimg init add rm edit wipe types`; options `--type
  --start --size --name --boot --uuid --attrs --num --sector --dry-run
  --yes`.
- GPT with both headers and arrays CRC-checked and rewritten, recovery from
  the backup, UTF-16 names, attributes, 512 and 4096-byte sectors. MBR with
  CHS, the boot flag and extended partitions' logical ones (read only).
- Recognises ext2/3/4, NTFS, UFS1, UFS2/FFS2 (also in a BSD disklabel),
  FAT, exFAT, XFS, Btrfs and swap.
