# Changelog

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
