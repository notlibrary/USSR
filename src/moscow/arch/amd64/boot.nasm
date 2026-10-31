; boot.nasm - the Moscow boot loader for amd64 PCs (BIOS), in two stages.
;
;     nasm -f bin boot.nasm -o boot.bin
;
; Stage 1 is the MBR: the BIOS loads it at 0x7c00 and it reads stage 2
; (the next STAGE2_SECTORS sectors) to 0x7e00 with the extended disk
; read. Stage 2 enables A20, records the BIOS memory map, reads the file
; table and every file it lists (the NASM USSR runtime first, then
; moscow.ru and russia.ru), builds identity page tables for the first
; GiB, switches straight from real mode to long mode and jumps to the
; runtime with the boot information block's address in rdi.
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

stage2:
        mov si, msg_hello
        call bios_print

        call enable_a20
        jc .a20_fail
        call read_memory_map

        ; the file table
        mov word [dap_count], 1
        mov word [dap_offset], TOC_BUFFER
        mov word [dap_segment], 0
        mov dword [dap_lba], TOC_LBA
        mov dword [dap_lba + 4], 0
        call disk_read
        jc .toc_fail
        cmp dword [TOC_BUFFER], 'MOSC'
        jne .toc_fail
        cmp dword [TOC_BUFFER + 4], 'OWFS'
        jne .toc_fail
        mov eax, [TOC_BUFFER + 8]
        test eax, eax
        jz .toc_fail
        cmp eax, TOC_MAX
        ja .toc_fail
        mov [file_count], ax

        ; boot information header
        mov dword [BOOTINFO + BI_MAGIC], 'MOSC'
        mov [BOOTINFO + BI_NFILES], eax
        movzx eax, byte [boot_drive]
        mov [BOOTINFO + BI_DRIVE], eax

        ; every file, one after another from RT_BASE
        mov dword [load_addr], RT_BASE
        mov word [file_index], 0
.file:
        mov ax, [file_index]
        cmp ax, [file_count]
        jae .files_done
        mov si, ax
        shl si, 5                       ; TOC_ENTRY_SIZE
        add si, TOC_BUFFER + 16
        mov di, ax
        shl di, 5                       ; BIF_SIZE
        add di, BOOTINFO + BI_FILES
        ; name
        push si
        push di
        mov cx, 16
        rep movsb
        pop di
        pop si
        mov eax, [load_addr]
        mov [di + BIF_ADDR], eax
        mov dword [di + BIF_ADDR + 4], 0
        mov eax, [si + 20]              ; size in bytes
        mov [di + BIF_SIZE_FIELD], eax
        mov dword [di + BIF_SIZE_FIELD + 4], 0
        mov eax, [si + 16]              ; first sector
        mov [cur_lba], eax
        mov eax, [si + 24]              ; sectors
        mov [cur_left], eax
        ; does it fit below the EBDA?
        shl eax, 9
        add eax, [load_addr]
        cmp eax, LOAD_LIMIT
        ja .too_big
.chunk:
        mov eax, [cur_left]
        test eax, eax
        jz .file_done
        cmp eax, 64                     ; 32 KiB a read
        jbe .count_ok
        mov eax, 64
.count_ok:
        mov [dap_count], ax
        mov ebx, [load_addr]
        mov edx, ebx
        shr edx, 4
        mov [dap_segment], dx
        and bx, 0x000f
        mov [dap_offset], bx
        mov edx, [cur_lba]
        mov [dap_lba], edx
        mov dword [dap_lba + 4], 0
        push eax
        call disk_read
        pop eax
        jc .read_fail
        add [cur_lba], eax
        sub [cur_left], eax
        shl eax, 9
        add [load_addr], eax
        jmp .chunk
.file_done:
        inc word [file_index]
        jmp .file

.files_done:
        mov si, msg_loaded
        call bios_print

        ; identity map the first GiB with 2 MiB pages
        cld
        xor ax, ax
        mov es, ax
        mov di, PAGE_PML4
        mov cx, 3 * 4096 / 4
        xor eax, eax
        rep stosd
        mov dword [PAGE_PML4], PAGE_PDPT | 3
        mov dword [PAGE_PDPT], PAGE_PD | 3
        mov di, PAGE_PD
        mov eax, 0x83                   ; present, writable, 2 MiB
        xor edx, edx
        mov cx, 512
.pd:
        mov [di], eax
        mov [di + 4], edx
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
.toc_fail:
        mov si, msg_toc
        jmp stage1_fail
.read_fail:
        mov si, msg_read2
        jmp stage1_fail
.too_big:
        mov si, msg_big
        jmp stage1_fail

; Reads with the packet at dap; carry set on failure (after 3 tries).
disk_read:
        mov byte [tries], 3
.again:
        mov si, dap
        mov ah, 0x42
        mov dl, [boot_drive]
        int 0x13
        jnc .ok
        xor ah, ah                      ; reset, then try again
        mov dl, [boot_drive]
        int 0x13
        dec byte [tries]
        jnz .again
        stc
.ok:
        ret

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

file_count:  dw 0
file_index:  dw 0
load_addr:   dd 0
cur_lba:     dd 0
cur_left:    dd 0
tries:       db 0

msg_hello:   db "Moscow boot: stage 2", 13, 10, 0
msg_loaded:  db "Moscow boot: files loaded, entering long mode", 13, 10, 0
msg_a20:     db "Moscow boot: A20 stays off", 13, 10, 0
msg_toc:     db "Moscow boot: no file table (MOSCOWFS)", 13, 10, 0
msg_read2:   db "Moscow boot: disk read error", 13, 10, 0
msg_big:     db "Moscow boot: the files do not fit below 0x9f000", 13, 10, 0

        bits 64
long_entry:
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        xor ax, ax
        mov fs, ax
        mov gs, ax
        mov rsp, RT_STACK_TOP
        mov edi, BOOTINFO
        mov eax, RT_BASE
        jmp rax

        times (1 + STAGE2_SECTORS) * 512 - ($ - $$) db 0
