# Stalin test plan

Automated: `ussr tests/stalin_test.su` (also through russia.su in
`tests/run_all.sh`, checked against `tests/expected/stalin_test.su.txt`).
With `STALIN_SEED` set it makes a 20 MiB image in `tests/stalin-tmp.img`
and removes it at the end. It checks:

- `types`; `mkimg` and refusing to replace an image without `--yes`; `show`
  of an image with no table;
- GPT: `init`, four `add`s (`--size`, `--type` by alias, `--name`, `--boot`)
  and one that finds no room; ext4, NTFS and two UFS2 headers stamped with
  `bwrite` and recognised with their labels; `edit` of type and a UTF-8
  name; growing into a neighbour refused; `rm`; moving with `--start
  --size` and `--attrs`; `add --num --uuid`; `rm --dry-run`;
- mistakes: missing partition, bad number, unknown type, bad `--boot`, bad
  size, a start inside a partition, a used `--num`, a bad GUID, an unknown
  option and command, a missing image, an unknown table kind;
- a zeroed primary GPT header read from the backup with a warning, and
  repaired by the next `edit`;
- MBR: `init`, NTFS (active), FreeBSD, OpenBSD and extended partitions, the
  fifth `add` refused, names refused; UFS1, NTFS, a BSD disklabel holding
  FFS2, and an EBR chain with ext4 and FAT32 logical partitions stamped and
  recognised; moving the active flag; logical partitions read only;
  removing the extended partition with its logical ones; a raw type code;
- `wipe`; a 4096-byte-sector image; a bad `--sector`; `--yes` guarding raw
  disks; `bdevices` returning a vector.

By hand, on Linux (util-linux) against images Stalin wrote:
`sfdisk --verify` (GPT CRCs, MBR), `sfdisk --dump` (types, names,
attributes, unique GUIDs), `fdisk -b 4096 -l` for 4K sectors,
`mkfs.ext4 -E offset=...` into a partition and `blkid -p -O` to compare
the file system Stalin reports; and Stalin reading tables sfdisk wrote,
extended partitions included. `stalin` with no arguments on Windows lists
the physical drives with the right HDD/SSD kinds.
