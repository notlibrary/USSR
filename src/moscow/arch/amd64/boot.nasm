; boot.nasm - the Moscow boot loader for amd64 PCs (BIOS), in two stages.
;
;     nasm -f bin boot.nasm -o boot.bin
;
; Stage 1 is the MBR: the BIOS loads it at 0x7c00 and it reads stage 2
; (the next STAGE2_SECTORS sectors) to 0x7e00 with the extended disk
; read. Stage 2 asks the BIOS for what only it knows (A20, the e820
; memory map), builds identity page tables for the first 4 GiB and
; switches from real mode straight to long mode. Everything after that
; is 64-bit: an ATA PIO driver reads the file table and every file it
; lists (the NASM USSR runtime first, then moscow.ru and russia.ru) to
; memory from 1 MiB up, and stage 2 jumps to the runtime with the boot
; information block's address in rdi.
;
; Progress goes to the screen through the BIOS and to the first serial
; port, so a headless QEMU shows where a boot stopped.
%include "layout.inc"

        bits 16
        org 0x7c00

; --- stage 1 -----------------------------------------------------------

stage1:
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

        align 8
gdt:
        dq 0
        dq 0x00af9a000000ffff           ; 0x08: 64-bit code
        dq 0x00cf92000000ffff           ; 0x10: data
gdtr:
        dw gdtr - gdt - 1
        dd gdt

msg_hello:   db "Moscow boot: stage 2, entering long mode", 13, 10, 0
msg_a20:     db "Moscow boot: A20 stays off", 13, 10, 0

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

        ; the file table
        mov eax, TOC_LBA
        mov ecx, 1
        mov rdi, TOC_BUFFER
        call ata_read
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
        call ata_read
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

; ATA PIO, primary master, LBA28: eax = first sector, ecx = sectors,
; rdi = destination. Carry set on a device error.
ata_read:
        push rbx
        push rdx
.chunk:
        test ecx, ecx
        jz .ok
        mov ebx, ecx
        cmp ebx, 128
        jbe .count
        mov ebx, 128
.count:
        push rax
        mov edx, 0x1f7
.idle:
        in al, dx
        test al, 0x80                   ; BSY
        jnz .idle
        pop rax
        push rax
        mov edx, eax
        shr edx, 24
        and dl, 0x0f
        or dl, 0xe0                     ; master, LBA
        mov al, dl
        mov dx, 0x1f6
        out dx, al
        mov dx, 0x1f2
        mov al, bl
        out dx, al
        pop rax
        push rax
        mov dx, 0x1f3
        out dx, al
        shr eax, 8
        mov dx, 0x1f4
        out dx, al
        shr eax, 8
        mov dx, 0x1f5
        out dx, al
        mov dx, 0x1f7
        mov al, 0x20                    ; READ SECTORS
        out dx, al
        push rcx
        mov r8d, ebx
.sector:
        mov dx, 0x3f6                   ; 400 ns
        in al, dx
        in al, dx
        in al, dx
        in al, dx
        mov dx, 0x1f7
.wait:
        in al, dx
        test al, 0x80
        jnz .wait
        test al, 0x21                   ; ERR, DF
        jnz .error
        test al, 0x08                   ; DRQ
        jz .wait
        mov dx, 0x1f0
        mov ecx, 256
        rep insw
        dec r8d
        jnz .sector
        pop rcx
        pop rax
        add eax, ebx
        sub ecx, ebx
        jmp .chunk
.error:
        pop rcx
        pop rax
        pop rdx
        pop rbx
        stc
        ret
.ok:
        pop rdx
        pop rbx
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
msg_long:    db "Moscow boot: long mode, reading the files (ATA)", 13, 10, 0
msg_loaded:  db "Moscow boot: files loaded from 1 MiB, starting the runtime", 13, 10, 0
msg_disk:    db "Moscow boot: ATA read error", 13, 10, 0
msg_toc:     db "Moscow boot: no file table (MOSCOWFS)", 13, 10, 0

        times (1 + STAGE2_SECTORS) * 512 - ($ - $$) db 0
