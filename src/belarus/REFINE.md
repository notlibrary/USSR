# Belarus — refinement ideas

**Today:** the USSR file system, a library and a command line in
`belarus.su`.
- Inodes are 4096-byte blocks, and each file finds its blocks through a
  B+ tree block map. Directories are UNO lists of `[name, inode]`.
- Metadata blocks are readable text, a 24-byte header plus UNO, with a
  CRC-32.
- It runs on images under `ussr` and on ATA disks under Moscow, through
  `bread`, `bwrite` and `bcopy`.
- Commands: `mkfs info ls tree cat put putlist get write mkdir rm fsck`.
  The library adds `bl_rename`, `bl_pread`, `bl_pwrite` and
  `bl_copy_tree`.

**Refine pass 1 (0.2.0):** [x] done, [~] partly done; see CHANGELOG.md.

## Files and directories
- [x] `mv` and `cp` on the command line (the library can already rename and
  copy trees)
- [x] `rm -r`: a directory and everything under it
- [x] `touch`: an empty file, or a new modification time
- [x] `append PATH TEXT`, `truncate PATH SIZE`
- [x] `head`, `tail` and `hexdump` of a file, through `bl_pread` (no whole-file
  read)
- [x] Symbolic links: `ln -s TARGET PATH`, `readlink`, followed by `cat`
- Hard links and a link count in the inode
- Extended attributes (`user.*` key/value pairs) in the inode
- Permissions and an owner (mode bits, uid, gid) for Moscow's users
- File holes: sparse files whose unwritten blocks are not mapped

## Host exchange
- [x] `putdir HOSTDIR PATH` and `getdir PATH HOSTDIR`: whole directory trees
  in and out
- [x] `diff PATH HOSTFILE`: the first byte that differs
- [x] `sha256sum` and `crc32` of a file inside the file system
- Export and import as a tar archive
- [x] `clone DEV DEV2`: one file system copied into another

## Inspecting
- [x] `stat PATH`: type, size, time, inode, blocks, runs, map nodes and depth
- [x] `du PATH` and `df`: space per directory, and used and free blocks with
  counts of files and directories
- [x] `find PATH` with `--name GLOB`, `--type f|d|l` and `--size +N/-N`
- [x] `grep PATTERN PATH`: regular expressions over the files under a path
- [x] `frag`: the most fragmented files, and the share of files in one run
- [x] `ls -l` with modification times, and `--json` for `ls`, `stat`, `info`
  and `df`
- [x] Dump any metadata block as its UNO text (`belarus block DEV N`)

## Robustness
- [~] `fsck --repair`: give lost blocks back to the free map and drop entries
  whose inode does not read — partly: lost blocks only; entries whose inode does not read are not dropped yet
- A write-ahead journal, so a crash between directory and superblock
  writes leaves a consistent tree
- [x] `--dry-run`: run a command and write nothing
- Read-only mounts (`--read-only`, and Moscow mounting a damaged file
  system read-only)
- A backup superblock at the end of the file system
- Fuzz tests: corrupt metadata blocks must give errors, not crashes

## Size and speed
- [x] Grow a file system when its partition grows (`belarus grow DEV`)
- A defragmenter: rewrite fragmented files into single runs
- Extent leaves: one map entry for a run of blocks, not one per block
- Small files stored inline in the inode
- Directory B+ trees for directories with thousands of entries
- A node cache size limit for large trees under Moscow's memory
- Delayed allocation: blocks chosen at sync time, so files land in one
  run

## Interoperability
- [x] `relabel DEV LABEL` and a new UUID (`belarus uuid DEV`)
- [x] Repeatable images: `BELARUS_TIME` fixes every modification time (for
  reproducible builds and tests)
- A FUSE driver (`belarus mount DEV DIR`) on Linux
- A read-only Belarus reader in C for bootloaders
- Stalin's `mkfs DEV N belarus`, and `fsinfo` reading Belarus space

## Moscow
- `mkfs` from the Moscow shell, and `fsck` at boot
- `df`, `du` and `find` as Moscow shell commands over the root file system
