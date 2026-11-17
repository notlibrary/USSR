# Changelog

## 0.2.0
- Refine pass 1, in `more.su`: `mv`, `cp`, `rm -r`, `touch`, `append`,
  `truncate`, `head`, `tail`, `hexdump`.
- Symbolic links: `ln -s`, `readlink`. `cat`, `head`, `tail` and `ls`
  follow them, and `cp` and `clone` keep them.
- Host trees: `putdir`, `getdir`, `diff` against a host file, and
  `sha256sum` / `crc32` of a file inside.
- `clone DEV DEV2 [FROM [TO]]`.
- Inspecting: `stat`, `du`, `df`, `find` (`--name GLOB`, `--type`,
  `--size`), `grep`, `frag`, `block N` (a metadata block's UNO text).
- `fsck --repair` gives lost blocks back to the free map. Also
  `relabel`, `uuid`, and `grow` (fills a larger partition or image).
- Options: `-l` (ls with UTC times), `--json` (ls, stat, info, df),
  `--dry-run`. `BELARUS_TIME` fixes every time stamp.
- Packaging: `box.uno` (so Ural can install it), `REFINE.md` and this
  changelog.
- Tests: `tests/belarus_more_test.su`.

## 0.1.0
- The USSR file system: inodes with B+ tree block maps, UNO directories,
  readable metadata blocks with CRC-32. Commands `mkfs info ls tree cat put
  putlist get write mkdir rm fsck`, on images under ussr and on ATA disks
  under Moscow.
