; layout.inc - where things are, on the disk image and in memory, for
; boot.nasm, image.nasm and ussr_rt.nasm.
;
; The image (512-byte sectors):
;   LBA 0                    stage 1, the MBR boot sector
;   LBA 1 .. STAGE2_SECTORS  stage 2: A20, memory map, files, long mode
;   LBA TOC_LBA              the file table ("MOSCOWFS")
;   LBA TOC_LBA + 1 ..       the files, each starting on a sector: the
;                            runtime (always first), then the programs
;
; Memory after stage 2 (identity mapped, 2 MiB pages, the first GiB):
;   0x01000 .. 0x03fff       page tables (PML4, PDPT, PD)
;   0x05000                  boot information for the runtime (BI_*)
;   0x06000                  the file table, as read from the disk
;   0x07c00                  stage 1, then stage 2 from 0x07e00
;   0x10000 .. LOAD_LIMIT    the files: the runtime at RT_BASE, the
;                            others after it, each on a 512-byte boundary
;   0x100000 .. 0x1fffff     runtime variables and its stack (top 2 MiB)
;   0x200000 ..              the runtime's heap, up to the end of RAM

STAGE2_SECTORS  equ 15
TOC_LBA         equ 1 + STAGE2_SECTORS
TOC_MAX         equ 15                  ; entries in the table sector
TOC_ENTRY_SIZE  equ 32                  ; name[16] lba size sectors pad

PAGE_PML4       equ 0x1000
PAGE_PDPT       equ 0x2000
PAGE_PD         equ 0x3000
TOC_BUFFER      equ 0x6000
RT_BASE         equ 0x10000
LOAD_LIMIT      equ 0x9f000

; Boot information block at BOOTINFO.
BOOTINFO        equ 0x5000
BI_MAGIC        equ 0                   ; 'MOSC'
BI_NFILES       equ 4                   ; u32
BI_NE820        equ 8                   ; u32
BI_DRIVE        equ 12                  ; u32, the BIOS boot drive
BI_FILES        equ 16                  ; BIF_SIZE each, TOC_MAX of them
BIF_NAME        equ 0                   ; 16 bytes, NUL padded
BIF_ADDR        equ 16                  ; u64
BIF_SIZE_FIELD  equ 24                  ; u64, bytes
BIF_SIZE        equ 32
BI_E820         equ 0x400               ; 24-byte BIOS entries
E820_MAX        equ 64

RT_DATA         equ 0x100000            ; runtime variables
RT_STACK_TOP    equ 0x200000
HEAP_BASE       equ 0x200000
MAP_LIMIT       equ 0x40000000          ; identity-mapped: 1 GiB

COM1            equ 0x3f8
