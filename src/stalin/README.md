# Stalin

The sysadmin's partition editor, written in USSR. It lists the machine's
storage devices, reads, creates and edits GPT and MBR partition tables on
disks and disk images, and recognises the file systems inside the
partitions.

```bash
ussr src/stalin/stalin.su                       # the storage devices
ussr src/stalin/stalin.su mkimg disk.img 20M    # a blank 20 MiB disk image
ussr src/stalin/stalin.su init disk.img gpt
ussr src/stalin/stalin.su add disk.img --size 4M --type ext4 --name root
ussr src/stalin/stalin.su add disk.img --size 4M --type ntfs --name windows
ussr src/stalin/stalin.su add disk.img --type ffs2 --name openbsd
mkfs.ext4 -L rootfs -E offset=1048576 disk.img 4M  # Linux: ext4 in partition 1
ussr src/stalin/stalin.su show disk.img
```

```
Disk disk.img: 20 MiB, 40960 sectors of 512 bytes
Table: GPT, disk GUID 8C71882E-E691-4F9B-9BDA-89861E5BA52F, usable sectors 34..40926, 128 entries
#  START    END  SECTORS    SIZE  TYPE   FS    LABEL   NAME
1   2048  10239     8192   4 MiB  linux  ext4  rootfs  root
2  10240  18431     8192   4 MiB  ntfs                 windows
3  18432  40926    22495  11 MiB  ffs2                 openbsd
```

The FS and LABEL columns come from the partitions' own headers: Stalin
edits tables, it does not make file systems.

## Devices

With no command (or `list`) Stalin prints every storage device with its
kind (HDD, SSD, flash, virtual), bus, size, sector size, model and the
partition table on it:

```
DEVICE              KIND  BUS        SIZE  SECTOR  TABLE  MODEL
\\.\PhysicalDrive0  SSD   SATA  111.8 GiB     512  ?      TS120GSSD220S
\\.\PhysicalDrive1  HDD   SATA  298.1 GiB     512  ?      ST9320328CS
```

`TABLE ?` means no read access: raw disks are read and written only with
administrator (root) rights. Windows names them `\\.\PhysicalDrive0`,
Linux `/dev/sda`. Anything else Stalin is given is a disk image file.

## Commands

| Command | Does |
|---------|------|
| `stalin [list]` | storage devices: HDD, SSD, flash |
| `stalin show DEV` (or `stalin DEV`) | the table, file systems and free space |
| `stalin mkimg FILE SIZE` | a blank disk image (`--yes` replaces a file) |
| `stalin init DEV gpt\|mbr` | a new, empty partition table |
| `stalin add DEV [options]` | a partition; default: the first aligned free space, all of it |
| `stalin rm DEV N` | removes partition N (an MBR extended one with its logical ones) |
| `stalin edit DEV N [options]` | changes type, name, flags, start or size of partition N |
| `stalin wipe DEV` | erases both GPTs and the MBR partition area |
| `stalin types` | the partition type names |

Options:

- `--type T` a name from `stalin types` (`linux`, `ext4`, `ntfs`, `fat32`,
  `efi`, `swap`, `ufs`, `freebsd`, `ffs2`, `openbsd`, `netbsd`, ...), an MBR
  code (`83`, `0xa5`) or a GPT GUID;
- `--start POS` and `--size SIZE` in sectors (`2048s`) or bytes with K, M,
  G, T (`1M`, `1.5G`); a bare number is MiB; `--size max` takes the rest.
  New partitions start on 1 MiB boundaries unless `--start` says otherwise;
- `--name NAME`, `--uuid GUID`, `--attrs N` for GPT; `--boot on|off` is the
  MBR active flag (one partition at a time) or the GPT legacy BIOS bootable
  attribute;
- `--num N` the number `add` gives the partition;
- `--sector N` the sector size of an image: 512 (default), 1024, 2048, 4096;
- `--dry-run` prints the sectors a command would write and writes none;
- `--yes` is required to write to a raw disk.

Moving or resizing a partition changes the table only: the data stays where
it was.

## What it reads and writes

GPT: protective MBR, primary and backup headers and partition arrays, all
CRC-32 checked. A damaged primary is read from the backup, with a warning,
and any change writes both again. Names are UTF-16.

MBR: four primary entries with CHS and LBA, the disk signature and the boot
code left as they are. Extended partitions (05, 0F, 85) are followed down
their EBR chain and the logical partitions shown; Stalin does not create or
edit logical partitions.

File systems, recognised by their own headers: ext2, ext3, ext4 (label and
size from the superblock), NTFS (serial), UFS1 and UFS2 (FFS2 on an
OpenBSD or NetBSD partition), also inside a BSD disklabel, FAT12/16/32,
exFAT, XFS, Btrfs and Linux swap. Stalin does not make file systems.

Every byte goes through the ussr builtins `bread` and `bwrite`; `bsize` and
`bdevices` find the disks (see `docs/API.md`).
