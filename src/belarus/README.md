# Belarus

Belarus is the USSR file system: inodes whose blocks are found through a
B+ tree each, on a disk, a disk image, or a partition of either. It is written in USSR (`belarus.su`),
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
d         23  /
d         18  /doc
-      10363  /doc/README.md
d         17  /etc
-          6  /etc/hostname
3 directories, 2 files, 10369 bytes; 5 inodes, 0 block map nodes, map depth up to 1
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
| 0 | the superblock, `BELARUS1`: size, label, UUID, the root directory's inode |
| 1 .. 16 | the free-space map, `BLFREE01` |
| the rest | inodes (`BLINODE1`), block map nodes (`BLBMAP01`) and the data of files and directories |

A metadata block starts with a 24-byte ASCII header - the magic, then
the length of the text and its CRC-32 in 8 hex digits each - and then
the text: a value in UNO, padded with spaces to a whole sector. A hex
viewer shows the whole file system's structure as text.

### Files: inodes and block maps

Every file and directory is an inode, a block of its own whose number
is the inode's number. It holds the type, size and modification time and
the root of the file's **block map**: a B+ tree from the index of a block
of the file (0, 1, 2, ...) to the LBA of the disk sector that block
starts at. Reading or writing block *i* of a file looks *i* up in that
tree (`bl_bmap`); reading a whole file walks the leaves, which are linked
in index order, and reads each run of consecutive LBAs at once.

```
inode 19   [`f`,880800,1793535166,`I`,0,[[0,236],[64,237],[128,238]]]
node  236  [`L`,237,[[0,160],[1,168],[2,176], ... ,[63,664]]]
node  237  [`L`,238,[[64,672], ... ,[127,1176]]]
node  238  [`L`,0,[[128,1184],[129,1192], ... ,[215,1880]]]
```

A node holds up to 128 entries. The root starts in the inode as a leaf;
when it fills up, its entries move down into a node of their own and the
inode keeps one entry pointing at it (ext4's inodes grow their extent
trees the same way); from then on a full node splits in two and the
separator goes up to its parent. Small files never leave the inode;
`russia.ru` (216 blocks, above) has a map two levels deep with three
leaves: type, size, time, the root's kind, then its entries.

### Directories

Directories are not in a B+ tree: a directory is a file whose data is
its entries, `[name, inode]` pairs in UNO sorted by name. A path is
looked up a name at a time from the root directory's inode, which the
superblock names. In memory a directory is a `btree` from name to inode,
cached while the file system is mounted and written back when it
changed.

## In memory

A mounted file system, its nodes, its directories, its node cache and
its free-space map are all `btree` containers, the ordered map USSR has
had since this file system needed one (`src/btree.c`; `help(_): "data"`
lists the `bt*` commands). A block map's internal node finds the child
for an index with `btfloor`; the free-space map keeps free extents by
first block, so `btfloor` and `btget` find the neighbours a freed extent
joins. Nothing is written until `bl_sync`: the changed directories, the
changed nodes, the free-space map, the superblock last.

File data never becomes USSR values when it does not have to: `put`
copies a host file into its blocks with `bcopy` byte for byte, and
`bl_copy_tree` copies a whole tree from one Belarus file system to
another run by run. The installer copies a system that way.

`fsck` walks every inode from the root, checks that each block map maps
blocks 0 .. n - 1 for the file's size, that no block is used twice or
both used and free, and that no block is lost.

## As a library

`$include "../belarus/belarus.su"` gives `bl_open(fs): "disk.img"`,
`bl_mount`, `bl_mkfs`, `bl_read_text`, `bl_read_bytes`, `bl_write_text`,
`bl_write_bytes`, `bl_write_from`, `bl_list`, `bl_walk`, `bl_mkdirs`,
`bl_bmap` (the LBA of a block of a file), `bl_runs`,
`bl_remove`, `bl_copy_tree`, `bl_fsck` and `bl_sync`. The Moscow kernel
mounts its root file system with them and serves `ls`, `cat`,
`readfile`, `writefile` and the other file commands from it;
`tests/belarus_test.su` checks them on two images.
