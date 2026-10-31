; layout.inc - where things are, on the disk image and in memory, for
; boot.nasm, image.nasm and ussr_rt.nasm.
;
; The image (512-byte sectors):
;   LBA 0                    stage 1, the MBR boot sector
;   LBA 1 .. STAGE2_SECTORS  stage 2: A20, memory map, long mode, loader
;   LBA TOC_LBA              the file table ("MOSCOWFS")
;   LBA TOC_LBA + 1 ..       the files, each starting on a sector: the
;                            runtime (always first), then the programs
;
; Stage 2 uses the BIOS only for A20 and the memory map, then switches to
; long mode and reads the file table and the files with its own ATA PIO
; driver (primary master, LBA28): files go from 1 MiB up, as large as
; they are, with no real-mode window in the way.
;
; Memory (identity mapped, 2 MiB pages, the first 4 GiB):
;   0x00500                  boot information for the runtime (BI_*)
;   0x01000 .. 0x06fff       page tables (PML4, PDPT, four PDs)
;   0x07000                  the file table, as read from the disk
;   0x07c00                  stage 1, then stage 2 from 0x07e00
;   0x10000 .. 0x7ffff       runtime variables, IDT and stack (RT_DATA)
;   0x100000 ..              the files: the runtime at RT_BASE, the others
;                            after it, each on a 4 KiB boundary
;   after the files          the runtime's heap, from the next 2 MiB to
;                            the end of RAM

STAGE2_SECTORS  equ 15
TOC_LBA         equ 1 + STAGE2_SECTORS
TOC_MAX         equ 15                  ; entries in the table sector
TOC_ENTRY_SIZE  equ 32                  ; name[16] lba size sectors pad

BOOTINFO        equ 0x500
PAGE_PML4       equ 0x1000
PAGE_PDPT       equ 0x2000
PAGE_PD         equ 0x3000              ; four of them, 0x3000 .. 0x6fff
TOC_BUFFER      equ 0x7000
RT_BASE         equ 0x100000
FILE_ALIGN      equ 0x1000

; Boot information block at BOOTINFO.
BI_MAGIC        equ 0                   ; 'MOSC'
BI_NFILES       equ 4                   ; u32
BI_NE820        equ 8                   ; u32
BI_DRIVE        equ 12                  ; u32, the BIOS boot drive
BI_FILES        equ 16                  ; BIF_SIZE each, TOC_MAX of them
BIF_NAME        equ 0                   ; 16 bytes, NUL padded
BIF_ADDR        equ 16                  ; u64
BIF_SIZE_FIELD  equ 24                  ; u64, bytes
BIF_SIZE        equ 32
BI_FILES_END    equ 0x200               ; u64: first byte after the files
BI_E820         equ 0x208               ; 24-byte BIOS entries
E820_MAX        equ 64

RT_DATA         equ 0x10000             ; runtime variables
RT_STACK_TOP    equ 0x80000
MAP_LIMIT       equ 0x100000000         ; identity-mapped: 4 GiB

COM1            equ 0x3f8
