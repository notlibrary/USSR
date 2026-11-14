; boot.nasm - the Moscow boot loader for amd64 PCs (BIOS), in two stages.
;
;     nasm -f bin boot.nasm -o boot.bin
;
; Stage 1 is the MBR: the BIOS loads it at 0x7c00 and it reads stage 2
; (the next STAGE2_SECTORS sectors) to 0x7e00 with the extended disk
; read. Stage 2 asks the BIOS for what only it knows (A20, the e820
; memory map) and, from a hard disk, for the file table and every file
; it lists (the NASM USSR runtime first, then moscow.ru and russia.ru):
; extended reads, 64 sectors at a time, to a buffer under 1 MiB, copied
; to their place from 1 MiB up in unreal mode. So the disk can be on
; whatever the BIOS boots from - IDE, SATA (AHCI), anything else - and the
; kernel's drivers take over after. Then stage 2 builds identity page
; tables for the first 4 GiB, switches from real mode straight to long
; mode and jumps to the runtime with the boot information block's
; address in rdi.
;
; From a CD: the ISO's El Torito boot file is the whole disk image, no emulation.
; The BIOS has loaded stage 1 and stage 2 together and put the image's place on
; the CD in the boot information table (bytes 8 .. 63 of the file), so stage 1
; goes straight to stage 2, which reads the rest over ATAPI: it looks through the
; four IDE positions for the drive where the file table is. Stage 1 asks the BIOS
; whether this is such a boot (int 13h, ax = 4b01): the table cannot say, as an
; install from the CD copies it onto a hard disk.
;
; Progress goes to the screen through the BIOS and to the first serial
; port, so a headless QEMU shows where a boot stopped.
%include "layout.inc"

        bits 16
        org 0x7c00

; --- stage 1 -----------------------------------------------------------

stage1:
        jmp short stage1_start
        nop
        times 64 - ($ - $$) db 0        ; El Torito's boot information table
stage1_start:
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7c00
        jmp 0:.flush                    ; some BIOSes start at 07c0:0000
.flush:
        sti
        cld
        mov [boot_drive], dl

        ; a no-emulation CD: the BIOS loaded stage 2 already
        mov si, TOC_BUFFER
        mov byte [si], 0x13
        mov ax, 0x4b01
        int 0x13
        jc .hard_disk
        test byte [si + 1], 0x0f        ; media type 0: no emulation
        jnz .hard_disk
        mov dl, [boot_drive]
        cmp [si + 2], dl                ; it is this drive
        jne .hard_disk
        mov byte [boot_cd], 1
        jmp 0:stage2
.hard_disk:
        mov dl, [boot_drive]
        mov ah, 0x41                    ; extended disk services?
        mov bx, 0x55aa
        int 0x13
        jc .no_ext
        cmp bx, 0xaa55
        jne .no_ext

        mov word [dap_count], STAGE2_SECTORS
        mov word [dap_offset], 0x7e00
        mov word [dap_segment], 0
        mov dword [dap_lba], 1
        mov dword [dap_lba + 4], 0
        mov si, dap
        mov ah, 0x42
        mov dl, [boot_drive]
        int 0x13
        jc .read_error
        jmp 0:stage2

.no_ext:
        mov si, msg_no_ext
        jmp stage1_fail
.read_error:
        mov si, msg_read1
stage1_fail:
        call bios_print
.stop:
        cli
        hlt
        jmp .stop

; si: NUL-terminated text, to the screen (BIOS teletype) and COM1.
bios_print:
        push ax
        push bx
        push dx
.next:
        lodsb
        test al, al
        jz .done
        mov ah, 0x0e
        mov bx, 0x0007
        int 0x10
        mov dx, COM1
        out dx, al
        jmp .next
.done:
        pop dx
        pop bx
        pop ax
        ret

        align 4
dap:                                    ; disk address packet (int 13h ah=42h)
        db 16, 0
dap_count:   dw 0
dap_offset:  dw 0
dap_segment: dw 0
dap_lba:     dq 0
boot_drive:  db 0
boot_cd:     db 0

msg_no_ext:  db "Moscow: no BIOS extended disk reads", 13, 10, 0
msg_read1:   db "Moscow: cannot read stage 2", 13, 10, 0

        times 446 - ($ - $$) db 0
        times 64 db 0                   ; no partition table
        dw 0xaa55

; --- stage 2 (0x7e00) ----------------------------------------------------
; Real mode only for what needs the BIOS (A20, the e820 memory map); then
; long mode, and the files are read with the ATA driver below.

stage2:
        mov si, msg_hello
        call bios_print

        call enable_a20
        jc .a20_fail
        call read_memory_map
        movzx eax, byte [boot_drive]
        mov [BOOTINFO + BI_DRIVE], eax

        ; from a hard disk the BIOS reads the files
        cmp byte [boot_cd], 0
        jne .page_tables
        call bios_load
        jc stage1_fail
        mov byte [bios_loaded], 1
.page_tables:

        ; identity map the first 4 GiB with 2 MiB pages
        cld
        xor ax, ax
        mov es, ax
        mov di, PAGE_PML4
        mov cx, 6 * 4096 / 4
        xor eax, eax
        rep stosd
        mov dword [PAGE_PML4], PAGE_PDPT | 3
        mov di, PAGE_PDPT
        mov eax, PAGE_PD | 3
        mov cx, 4
.pdpt:
        mov [di], eax
        add eax, 0x1000
        add di, 8
        loop .pdpt
        mov di, PAGE_PD
        mov eax, 0x83                   ; present, writable, 2 MiB
        mov cx, 2048
.pd:
        mov [di], eax
        add eax, 0x200000
        add di, 8
        loop .pd

        ; real mode straight to long mode: PAE, the tables, EFER.LME,
        ; then protection and paging together
        cli
        mov al, 0xff                    ; no PIC interrupts until the runtime
        out 0xa1, al
        out 0x21, al
        lgdt [gdtr]
        mov eax, 0x20 | 0x80            ; PAE, PGE
        mov cr4, eax
        mov eax, PAGE_PML4
        mov cr3, eax
        mov ecx, 0xc0000080
        rdmsr
        or eax, 0x100
        wrmsr
        mov eax, cr0
        or eax, 0x80000001
        mov cr0, eax
        jmp 0x08:long_entry

.a20_fail:
        mov si, msg_a20
        jmp stage1_fail

; A20 through the BIOS, then the fast gate; carry set when it stays off.
enable_a20:
        call a20_on
        jnc .done
        mov ax, 0x2401
        int 0x15
        call a20_on
        jnc .done
        in al, 0x92
        test al, 2
        jnz .check
        or al, 2
        and al, 0xfe
        out 0x92, al
.check:
        call a20_on
.done:
        ret

; carry clear when A20 is on: 0000:0500 and ffff:0510 are different bytes
a20_on:
        push ds
        push es
        xor ax, ax
        mov ds, ax
        mov ax, 0xffff
        mov es, ax
        mov al, [ds:0x0500]
        push ax
        mov al, [es:0x0510]
        push ax
        mov byte [ds:0x0500], 0x00
        mov byte [es:0x0510], 0xff
        cmp byte [ds:0x0500], 0xff
        pop ax
        mov [es:0x0510], al
        pop ax
        mov [ds:0x0500], al
        pop es
        pop ds
        je .off
        clc
        ret
.off:
        stc
        ret

; BIOS e820 map into BOOTINFO + BI_E820.
read_memory_map:
        xor ax, ax
        mov es, ax
        mov di, BOOTINFO + BI_E820
        xor ebx, ebx
        xor bp, bp
.next:
        mov eax, 0xe820
        mov ecx, 24
        mov edx, 0x534d4150             ; 'SMAP'
        mov dword [di + 20], 1          ; ACPI 3 "valid" bit, for 24-byte replies
        int 0x15
        jc .done
        cmp eax, 0x534d4150
        jne .done
        jcxz .skip
        inc bp
        add di, 24
        cmp bp, E820_MAX
        jae .done
.skip:
        test ebx, ebx
        jnz .next
.done:
        movzx eax, bp
        mov [BOOTINFO + BI_NE820], eax
        ret

; --- from a hard disk: the files through the BIOS

BOUNCE_SEG      equ 0x2000              ; 0x20000, under 1 MiB, in one 64 KiB
BOUNCE_SECTORS  equ 64

; The file table to TOC_BUFFER, every file it lists from RT_BASE up (each
; on a FILE_ALIGN boundary), and the boot information for them, as the
; long-mode loader does from a CD. CF set and si the reason on an error.
bios_load:
        mov dword [dap_lba], TOC_LBA
        mov dword [dap_lba + 4], 0
        mov word [dap_count], 1
        mov word [dap_offset], TOC_BUFFER
        mov word [dap_segment], 0
        call bios_read
        jc .read_fail
        cmp dword [TOC_BUFFER], 'MOSC'
        jne .toc_fail
        cmp dword [TOC_BUFFER + 4], 'OWFS'
        jne .toc_fail
        mov ebx, [TOC_BUFFER + 8]
        test ebx, ebx
        jz .toc_fail
        cmp ebx, TOC_MAX
        ja .toc_fail
        mov dword [BOOTINFO + BI_MAGIC], 'MOSC'
        mov [BOOTINFO + BI_NFILES], ebx
        mov dword [load_at], RT_BASE
        mov si, TOC_BUFFER + 16         ; table entry
        mov di, BOOTINFO + BI_FILES     ; boot information entry
.file:
        test ebx, ebx
        jz .done
        mov eax, [si]
        mov [di + BIF_NAME], eax
        mov eax, [si + 4]
        mov [di + BIF_NAME + 4], eax
        mov eax, [si + 8]
        mov [di + BIF_NAME + 8], eax
        mov eax, [si + 12]
        mov [di + BIF_NAME + 12], eax
        mov eax, [load_at]
        mov [di + BIF_ADDR], eax
        mov dword [di + BIF_ADDR + 4], 0
        mov eax, [si + 20]              ; bytes
        mov [di + BIF_SIZE_FIELD], eax
        mov dword [di + BIF_SIZE_FIELD + 4], 0
        mov eax, [si + 16]              ; first sector
        mov ecx, [si + 24]              ; sectors
        push si
        push di
        call bios_read_high
        pop di
        pop si
        jc .read_fail
        mov eax, [load_at]
        add eax, FILE_ALIGN - 1
        and eax, -FILE_ALIGN
        mov [load_at], eax
        add si, TOC_ENTRY_SIZE
        add di, BIF_SIZE
        dec ebx
        jmp .file
.done:
        mov eax, [load_at]
        mov [BOOTINFO + BI_FILES_END], eax
        mov dword [BOOTINFO + BI_FILES_END + 4], 0
        clc
        ret
.read_fail:
        mov si, msg_bios_read
        stc
        ret
.toc_fail:
        mov si, msg_toc16
        stc
        ret

; dap as it is -> int 13h extended read from the boot drive; CF set on an
; error. Keeps the others.
bios_read:
        pushad
        mov si, dap
        mov ah, 0x42
        mov dl, [boot_drive]
        int 0x13
        popad
        ret

; eax = first sector, ecx = sectors -> read through the bounce buffer to
; load_at and on (load_at moves past them). CF set on an error.
bios_read_high:
.chunk:
        test ecx, ecx
        jz .ok
        mov edx, ecx
        cmp edx, BOUNCE_SECTORS
        jbe .count
        mov edx, BOUNCE_SECTORS
.count:
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        mov [dap_count], dx
        mov word [dap_offset], 0
        mov word [dap_segment], BOUNCE_SEG
        call bios_read
        jc .out
        push ecx
        mov ecx, edx
        shl ecx, 9
        call copy_high
        pop ecx
        add eax, edx
        sub ecx, edx
        jmp .chunk
.ok:
        clc
.out:
        ret

; ecx = bytes (a multiple of 4) from the bounce buffer to load_at, which
; moves past them. Unreal mode: protected mode just long enough to give
; ds and es a 4 GiB limit, then real mode again with it. Keeps the others.
copy_high:
        push ds
        push es
        pushad
        cli
        lgdt [gdtr]
        mov eax, cr0
        or al, 1
        mov cr0, eax
        jmp short .protected
.protected:
        mov bx, 0x10
        mov ds, bx
        mov es, bx
        and al, 0xfe
        mov cr0, eax
        jmp short .real
.real:
        xor bx, bx
        mov ds, bx
        mov es, bx
        sti
        mov esi, BOUNCE_SEG * 16
        mov edi, [load_at]
        add [load_at], ecx
        shr ecx, 2
        cld
        a32 rep movsd
        popad
        pop es
        pop ds
        ret

        align 8
gdt:
        dq 0
        dq 0x00af9a000000ffff           ; 0x08: 64-bit code
        dq 0x00cf92000000ffff           ; 0x10: data
gdtr:
        dw gdtr - gdt - 1
        dd gdt
load_at:     dd 0                       ; bios_load: where the next file goes
bios_loaded: db 0                       ; the files are in memory already

msg_hello:   db "Moscow boot: stage 2, entering long mode", 13, 10, 0
msg_a20:     db "Moscow boot: A20 stays off", 13, 10, 0
msg_bios_read: db "Moscow boot: disk read error (BIOS)", 13, 10, 0
msg_toc16:   db "Moscow boot: no file table (MOSCOWFS)", 13, 10, 0

; --- long mode ------------------------------------------------------------

        bits 64
        default abs
long_entry:
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        xor ax, ax
        mov fs, ax
        mov gs, ax
        mov rsp, 0x7c00
        cld

        mov rsi, msg_long
        call puts64

        xor eax, eax
        mov [BOOTINFO + BI_CD_DRIVE], rax
        mov [BOOTINFO + BI_CD_BASE], rax
        mov [BOOTINFO + BI_CD_BYTES], rax
        cmp byte [boot_cd], 0
        je .read_table
        call cd_find
        jc .disk_fail
        mov eax, [cd_index]
        inc eax
        mov [BOOTINFO + BI_CD_DRIVE], rax
        mov eax, [ELTORITO_TABLE + 4]
        mov [BOOTINFO + BI_CD_BASE], rax
        mov eax, [ELTORITO_TABLE + 8]
        mov [BOOTINFO + BI_CD_BYTES], rax
.read_table:
        cmp byte [bios_loaded], 0
        jne .start_runtime
        ; the file table, from the CD
        mov eax, TOC_LBA
        mov ecx, 1
        mov rdi, TOC_BUFFER
        call read_sectors
        jc .disk_fail
        mov rax, 'MOSCOWFS'
        cmp [TOC_BUFFER], rax
        jne .toc_fail
        mov ebx, [TOC_BUFFER + 8]
        test ebx, ebx
        jz .toc_fail
        cmp ebx, TOC_MAX
        ja .toc_fail
        mov dword [BOOTINFO + BI_MAGIC], 'MOSC'
        mov [BOOTINFO + BI_NFILES], ebx

        ; every file, one after another from RT_BASE
        mov r12, RT_BASE                ; where the next file goes
        mov r13, TOC_BUFFER + 16        ; table entry
        mov r14, BOOTINFO + BI_FILES    ; boot information entry
.file:
        test ebx, ebx
        jz .files_done
        mov rax, [r13]
        mov [r14 + BIF_NAME], rax
        mov rax, [r13 + 8]
        mov [r14 + BIF_NAME + 8], rax
        mov [r14 + BIF_ADDR], r12
        mov eax, [r13 + 20]             ; bytes
        mov [r14 + BIF_SIZE_FIELD], rax
        mov eax, [r13 + 16]             ; first sector
        mov ecx, [r13 + 24]             ; sectors
        mov rdi, r12
        push rcx
        call read_sectors
        pop rcx
        jc .disk_fail
        shl rcx, 9
        add r12, rcx
        add r12, FILE_ALIGN - 1
        and r12, -FILE_ALIGN
        add r13, TOC_ENTRY_SIZE
        add r14, BIF_SIZE
        dec ebx
        jmp .file
.files_done:
        mov [BOOTINFO + BI_FILES_END], r12
.start_runtime:
        mov rsi, msg_loaded
        call puts64
        mov rsp, RT_STACK_TOP
        mov edi, BOOTINFO
        mov rax, RT_BASE
        jmp rax

.disk_fail:
        mov rsi, msg_disk
        jmp fail64
.toc_fail:
        mov rsi, msg_toc
fail64:
        call puts64
.stop:
        cli
        hlt
        jmp .stop

; eax = first 512-byte sector of the image, ecx = sectors, rdi = destination,
; from the CD, a 2048-byte block at a time. Carry set on a device error.
read_sectors:
cd_read:
        test ecx, ecx
        jz .ok
        mov edx, eax
        shr edx, 2
        add edx, [ELTORITO_TABLE + 4]
        cmp edx, [cd_cached]
        je .have
        push rax
        push rcx
        mov eax, edx
        call cd_block
        pop rcx
        pop rax
        jc .out
.have:
        push rax
        push rcx
        and eax, 3
        shl eax, 9
        lea rsi, [rax + CD_BUFFER]
        mov ecx, 512
        rep movsb
        pop rcx
        pop rax
        inc eax
        dec ecx
        jmp cd_read
.ok:
        clc
.out:
        ret

; The drive with this image on it, from the four IDE positions: CF set when no
; drive has it. Sets cd_port, cd_select and cd_index.
cd_find:
        xor ebx, ebx
.try:
        cmp ebx, 4
        jae .none
        mov [cd_index], ebx
        mov ax, 0x1f0
        cmp ebx, 2
        jb .channel
        mov ax, 0x170
.channel:
        mov [cd_port], ax
        mov al, 0xa0
        test ebx, 1
        jz .master
        mov al, 0xb0
.master:
        mov [cd_select], al
        mov dword [cd_cached], -1
        mov eax, TOC_LBA
        mov ecx, 1
        mov rdi, TOC_BUFFER
        call cd_read
        jc .next
        mov rax, 'MOSCOWFS'
        cmp [TOC_BUFFER], rax
        je .found
.next:
        inc ebx
        jmp .try
.found:
        clc
        ret
.none:
        stc
        ret

; eax = a 2048-byte block of the CD -> into CD_BUFFER, cd_cached set. CF set on an
; error. A drive answers UNIT ATTENTION to the first command after a disc comes:
; so a few tries. Keeps rdi, rbx.
cd_block:
        push rdi
        push r9
        push r10
        mov r10d, eax
        mov r9d, 8
.attempt:
        movzx edx, word [cd_port]
        add dx, 6
        mov al, [cd_select]
        out dx, al
        inc dx                          ; the status, 400 ns for the select
        in al, dx
        in al, dx
        in al, dx
        in al, dx
        cmp al, 0xff                    ; nothing there
        je .fail
        mov ecx, 2000000
.idle:
        in al, dx
        test al, 0x80
        jz .idle_done
        dec ecx
        jnz .idle
        jmp .fail
.idle_done:
        movzx edx, word [cd_port]
        inc dx
        xor eax, eax
        out dx, al                      ; features: PIO
        inc dx
        out dx, al                      ; sector count
        add dx, 2
        out dx, al                      ; bytes a transfer, low
        inc dx
        mov al, 0x08
        out dx, al                      ; and high: 2048
        add dx, 2
        mov al, 0xa0                    ; PACKET
        out dx, al
        call cd_wait_drq
        jc .retry
        ; READ (12), one block
        sub rsp, 16
        mov byte [rsp], 0xa8
        mov byte [rsp + 1], 0
        mov eax, r10d
        bswap eax
        mov [rsp + 2], eax
        mov dword [rsp + 6], 0x01000000
        mov word [rsp + 10], 0
        mov rsi, rsp
        movzx edx, word [cd_port]
        mov ecx, 6
        rep outsw
        add rsp, 16
        call cd_wait_drq
        jc .retry
        mov rdi, CD_BUFFER
        movzx edx, word [cd_port]
        mov ecx, 1024
        rep insw
        mov [cd_cached], r10d
        clc
        jmp .out
.retry:
        dec r9d
        jnz .attempt
.fail:
        stc
.out:
        pop r10
        pop r9
        pop rdi
        ret

; Waits for DRQ on the CD's channel; CF set on ERR or a timeout
cd_wait_drq:
        movzx edx, word [cd_port]
        add dx, 7
        mov ecx, 2000000
.poll:
        in al, dx
        test al, 0x80
        jnz .next
        test al, 0x01
        jnz .bad
        test al, 0x08
        jnz .good
.next:
        dec ecx
        jnz .poll
.bad:
        stc
        ret
.good:
        clc
        ret

; rsi = NUL-terminated text -> COM1 and the top of the screen
puts64:
        push rax
        push rdx
        push rdi
.next:
        lodsb
        test al, al
        jz .done
        push rax
        mov dx, COM1 + 5
.tx:
        in al, dx
        test al, 0x20
        jz .tx
        pop rax
        mov dx, COM1
        out dx, al
        cmp al, 32
        jb .next
        mov rdi, [screen_pos]
        mov ah, 0x07
        mov [rdi], ax
        add qword [screen_pos], 2
        jmp .next
.done:
        add qword [screen_pos], 160     ; the next message on a new line
        and qword [screen_pos], -1
        mov rax, [screen_pos]
        sub rax, 0xb8000
        xor edx, edx
        mov edi, 160
        div rdi
        imul rax, rax, 160
        add rax, 0xb8000
        mov [screen_pos], rax
        pop rdi
        pop rdx
        pop rax
        ret

        align 8
screen_pos:  dq 0xb8000 + 160 * 20
cd_index:    dd 0
cd_cached:   dd -1
cd_port:     dw 0
cd_select:   db 0
msg_long:    db "Moscow boot: long mode", 13, 10, 0
msg_loaded:  db "Moscow boot: files loaded from 1 MiB, starting the runtime", 13, 10, 0
msg_disk:    db "Moscow boot: disk read error", 13, 10, 0
msg_toc:     db "Moscow boot: no file table (MOSCOWFS)", 13, 10, 0

        times (1 + STAGE2_SECTORS) * 512 - ($ - $$) db 0
