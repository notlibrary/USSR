# Stalin — refinement ideas

**Today:** a partition editor through the builtins `bread`, `bwrite`,
`bsize` and `bdevices`.
- It lists devices (HDD/SSD/flash), and reads, creates and edits GPT
  (both headers, CRCs, recovery from the backup, UTF-16 names,
  attributes) and MBR (CHS, the boot flag, logical partitions read-only).
- It recognises ext2/3/4, NTFS, UFS1/2, a BSD disklabel, FAT, exFAT, XFS,
  Btrfs and swap.
- Commands: `list show mkimg init add rm edit wipe types`, with
  `--dry-run` and `--yes`.

## Table operations
- Writing logical partitions inside an MBR extended partition (today
  read-only)
- Convert MBR to GPT and GPT to MBR (when the layout fits), keeping the
  partitions
- Hybrid MBR for old BIOS boot of GPT disks
- Sort partition entries by start, and renumber
- Back up and restore tables: `stalin backup DEV file`, `stalin restore`
  (sfdisk dump format compatible)
- `stalin verify DEV`: overlaps, alignment, CRCs, header/backup
  mismatch, protective MBR
- Repair: rebuild a lost primary or backup GPT, and move the backup GPT
  to the end after an image grew
- Resize the disk image (`stalin grow img 40M`) and relocate the backup
  header
- Alignment checks against the device's physical sector and optimal I/O
  size
- APM (Apple partition map) and BSD disklabel editing
- GPT partition-name editing in bulk, and GUID regeneration for cloned
  disks (`stalin newuuids`)

## File systems
- Read more labels and UUIDs: F2FS, ZFS, HFS+, APFS, ISO 9660, UDF,
  LUKS, LVM PV, mdraid, ReFS, Belarus (the project's own FS has a probe
  hook)
- Show used/free space inside recognised file systems
- Creating simple file systems: FAT32 and exFAT `mkfs` in USSR, and
  Belarus for Moscow
- Resize FAT/ext4 together with the partition (or at least warn about
  data loss)
- Move partition data when a partition moves (block copy with
  progress), not only the table entry
- Secure wipe (zero or random) of a partition, with a progress bar

## Devices
- More device information: serial, firmware, rotation rate, TRIM
  support, SMART health summary
- Hot-plug refresh, and names for removable media
- macOS (`/dev/diskN`) and BSD device names
- Loop device setup for images on Linux
- Read-only safety mode by default for raw disks, with a lock against
  mounted partitions (refuse to write to a mounted disk)

## Interface
- A TUI (Mbl-style raw terminal) with a partition map bar, arrow
  selection and dialogs
- A Petersburg GUI with a graphical disk map, used from Kremlin
- `--json` output for scripting
- Undo inside a session before commit (stage changes, then `write`)
- Scripting: `stalin apply DEV layout.uno` to create a whole layout
  from a description

## Moscow
- Use in the Moscow installer: create the ISO's GPT and partitions
  from a USSR script
- Boot sector writers (a protective MBR boot code stub, an EFI system
  partition setup)

## Tests
- Compare against `sfdisk --dump` across a corpus of real-world tables
- Fuzz tables (corrupt headers and entries) for crash-free reading
- 4Kn and 512e images through every command

## Images and formats
- Sparse image files, and reading and writing VHD, VHDX, VMDK and QCOW2
  containers
- ISO hybrid images: show the El Torito boot catalog and the embedded
  MBR/GPT
- Clone a disk or a partition to an image and back (`stalin copy`),
  with a hash check
- Compare two disks or images sector by sector
- A partition extracted to a file and written back (`dump`, `load`)
- Shrink an image to the end of its last partition

## Layout knowledge
- More type names: ChromeOS, Android, Solaris, VMware, Ceph, the Linux
  Discoverable Partitions GUIDs (root by architecture, `/home`,
  `/srv`), with the automount attribute bits named
- Named GPT attribute bits (`--attrs required,no-automount`) in place
  of a number
- Percent sizes (`--size 50%`) and `--end POS`
- Templates: `stalin layout DEV uefi-linux` (EFI + root + swap),
  `bios-gpt`, `windows`, `moscow`
- Boot checks: is there an EFI system partition with a FAT file
  system, is a BIOS boot partition present for GRUB on GPT
- Warn on tables that some firmware rejects (entries array not at LBA
  2, fewer than 128 entries)

## Safety and audit
- A journal of every write with the old sectors saved, and `stalin
  undo` to put them back
- Confirm by typing the device's model or size for raw disks, in
  addition to `--yes`
- Detect that a disk is the one the system runs from, and refuse
- Explain mode: print what each field of the MBR and GPT headers
  means, for teaching (`stalin explain DEV`)
- A hexdump of any sector with fields annotated
