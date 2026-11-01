# Belarus

Belarus is the USSR file system: one B+ tree of paths on a disk, a disk
image, or a partition of either. It is written in USSR (`belarus.su`),
as a library and a command line, and the same code runs under `ussr` on
disk image files and under the [Moscow](../moscow/README.md) kernel on
ATA disks: every sector goes through `bread`, `bwrite` and `bcopy`.

```bash
ussr src/stalin/stalin.su mkimg disk.img 64M
ussr src/stalin/stalin.su init disk.img mbr
ussr src/stalin/stalin.su add disk.img --type belarus
ussr src/belarus/belarus.su mkfs disk.img --part 1 --label ussr
ussr src/belarus/belarus.su put disk.img README.md /doc/README.md
ussr src/belarus/belarus.su write disk.img /etc/hostname moscow
ussr src/belarus/belarus.su tree disk.img /
ussr src/belarus/belarus.su fsck disk.img
```

```
d          0  /
d          0  /doc
-       9570  /doc/README.md
d          0  /etc
-          6  /etc/hostname
2 directories, 2 files, 9576 bytes; 1 tree nodes, 1 leaves (0 empty)
```

## Commands

| Command | Does |
|---------|------|
| `belarus mkfs DEV [--label L] [--part N]` | a new file system on the whole DEV, or in partition N |
| `belarus info DEV` | label, UUID, size, free space, where the tree's root is |
| `belarus ls DEV [PATH]` | a directory |
| `belarus tree DEV [PATH]` | everything under PATH |
| `belarus cat DEV PATH` | a file's text |
| `belarus put DEV FILE PATH` | a host file in (its directories are made) |
| `belarus putlist DEV LIST [PREFIX]` | the host files listed in LIST, one a line, under PREFIX |
| `belarus get DEV PATH FILE` | a file out to the host |
| `belarus write DEV PATH TEXT` | TEXT as the file PATH |
| `belarus mkdir DEV PATH` | a directory and its parents |
| `belarus rm DEV PATH` | a file or an empty directory |
| `belarus fsck DEV` | checks the tree, the directories and the free space |

DEV is a disk or a disk image. Belarus uses the first Belarus file system
it finds on it (the whole disk, or a partition of an MBR or GPT table);
`DEV@N` names partition N. Stalin knows the partition type `belarus`
(MBR type `7F`, GPT `B3E1A2C5-5553-4252-8E4C-42454C415255`) and shows a
Belarus file system and its label in its FS column.

## On the disk

Blocks are 4096 bytes, counted from the start of the partition:

| Blocks | Hold |
|--------|------|
| 0 | the superblock, `BELARUS1` |
| 1 .. 16 | the free-space map, `BLFREE01` |
| the rest | tree nodes (`BLNODE01`, a block each) and file data, in extents of whole blocks |

A metadata block starts with a 24-byte ASCII header - the magic, then
the length of the text and its CRC-32 in 8 hex digits each - and then
the text: a value in UNO, padded with spaces to a whole sector. A hex
viewer shows the whole file system's structure as text.

The tree maps every path to a record `[type, size, mtime, extents]`:
type `d` or `f`, extents a list of `[first block, blocks]`. Paths sort
byte by byte, so the entries of a directory are one range of keys
(`/bin/` up to `/bin0`, `0` being the byte after `/`). The leaves hold
the records and are linked in key order; a directory listing or a tree
walk is a range scan along that chain. An internal node maps separator
keys to child blocks, its first child under `""`. A node splits when its
text no longer fits its block, the separator going up to the parent, up
to a new root. Deleting is lazy: an emptied leaf stays in the tree (as
in PostgreSQL's B-trees) until the tree is rebuilt.

## In memory

A mounted file system, its nodes, its node cache and its free-space map
are all `btree` containers, the ordered map USSR has had since this file
system needed one (`src/btree.c`; `help(_): "data"` lists the `bt*`
commands). An internal node finds the child for a key with `btfloor`;
the free-space map keeps free extents by first block, so `btfloor` and
`btget` find the neighbours a freed extent joins. Nothing is written
until `bl_sync`: the changed nodes, the free-space map, the superblock
last.

File data never becomes USSR values when it does not have to: `put`
copies a host file into its extents with `bcopy` byte for byte, and
`bl_copy_tree` copies a whole tree from one Belarus file system to
another extent by extent. The installer copies a system that way.

## As a library

`$include "../belarus/belarus.su"` gives `bl_open(fs): "disk.img"`,
`bl_mount`, `bl_mkfs`, `bl_read_text`, `bl_read_bytes`, `bl_write_text`,
`bl_write_bytes`, `bl_write_from`, `bl_list`, `bl_walk`, `bl_mkdirs`,
`bl_remove`, `bl_copy_tree`, `bl_fsck` and `bl_sync`. The Moscow kernel
mounts its root file system with them and serves `ls`, `cat`,
`readfile`, `writefile` and the other file commands from it;
`tests/belarus_test.su` checks them on two images.
