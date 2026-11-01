; ata.inc - disks for the Moscow runtime (ussr_rt.nasm includes it): an
; ATA PIO driver (LBA28, so up to 128 GiB a disk) for the four IDE
; positions, and over it the block builtins ussr has for disks and disk
; images: bread, bwrite, bsize, bcopy and bdevices. The disks are named
; as Linux names them: /dev/hda (primary master), /dev/hdb (primary
; slave), /dev/hdc and /dev/hdd (secondary). QEMU gives -drive index=N
; the N-th of them.
;
; Offsets and lengths are in bytes underneath: lba * sector with any
; sector size, so bread(v): dev byte count 1 reads bytes anywhere, and a
; write that covers a disk sector only in part reads that sector first.
; The driver polls (the drives' interrupts are off) and flushes the
; drive's cache after every write.

ATA_DRIVES      equ 4
ATA_CHUNK       equ 64                  ; sectors per transfer (32 KiB)
ATA_TIMEOUT     equ 20000000
; a drive record
ATAD_PRESENT    equ 0
ATAD_SECTORS    equ 8
ATAD_MODEL      equ 16                  ; 40 bytes and a NUL
ATAD_SIZE       equ 64

; drive index in edi -> dx: its base port, al: the select bits
ata_ports:
        mov dx, 0x1f0
        cmp edi, 2
        jb .channel
        mov dx, 0x170
.channel:
        mov al, 0xe0
        test edi, 1
        jz .master
        or al, 0x10
.master:
        ret

; dx = base port -> waits until the drive is not busy; CF set on a
; timeout or an error. Keeps the others but ecx.
ata_wait_ready:
        push rdx
        add dx, 7
        mov ecx, ATA_TIMEOUT
.poll:
        in al, dx
        test al, 0x80
        jz .ready
        dec ecx
        jnz .poll
        stc
        jmp .out
.ready:
        test al, 0x21                   ; ERR, DF
        jz .ok
        stc
        jmp .out
.ok:
        clc
.out:
        pop rdx
        ret

; dx = base port -> waits for DRQ; CF set on a timeout or an error
ata_wait_drq:
        push rdx
        add dx, 7
        mov ecx, ATA_TIMEOUT
.poll:
        in al, dx
        test al, 0x80
        jnz .busy
        test al, 0x21
        jnz .error
        test al, 0x08
        jnz .ok
.busy:
        dec ecx
        jnz .poll
.error:
        stc
        pop rdx
        ret
.ok:
        clc
        pop rdx
        ret

; Finds the drives (IDENTIFY) and turns their interrupts off.
ata_init:
        push rbx
        push rdi
        mov dx, 0x3f6
        mov al, 2
        out dx, al
        mov dx, 0x376
        out dx, al
        xor edi, edi
.drive:
        cmp edi, ATA_DRIVES
        jae .done
        mov rbx, rdi
        shl rbx, 6
        add rbx, ata_drive
        mov qword [rbx + ATAD_PRESENT], 0
        call ata_ports
        push rdx
        add dx, 6
        out dx, al
        ; 400 ns for the select to settle: four status reads
        add dx, 1
        in al, dx
        in al, dx
        in al, dx
        in al, dx
        cmp al, 0xff                    ; nothing on this channel
        pop rdx
        je .next
        push rdx
        add dx, 2                       ; count and LBA to 0
        xor eax, eax
        out dx, al
        inc dx
        out dx, al
        inc dx
        out dx, al
        inc dx
        out dx, al
        add dx, 2
        mov al, 0xec                    ; IDENTIFY
        out dx, al
        in al, dx
        pop rdx
        test al, al
        jz .next
        call ata_wait_ready
        jc .next
        push rdx
        add dx, 4                       ; ATAPI and SATA say so here
        in al, dx
        mov ah, al
        inc dx
        in al, dx
        or al, ah
        pop rdx
        jnz .next
        call ata_wait_drq
        jc .next
        push rdi
        mov rdi, ata_sector
        mov ecx, 256
        rep insw
        pop rdi
        mov eax, [ata_sector + 120]     ; words 60-61: LBA28 sectors
        test eax, eax
        jz .next
        mov [rbx + ATAD_SECTORS], rax
        ; the model, words 27-46, two bytes a word the other way round
        xor ecx, ecx
.model:
        mov ax, [ata_sector + 54 + rcx]
        xchg al, ah
        mov [rbx + ATAD_MODEL + rcx], ax
        add ecx, 2
        cmp ecx, 40
        jb .model
        mov byte [rbx + ATAD_MODEL + 40], 0
        mov ecx, 40
.trim:
        test ecx, ecx
        jz .trimmed
        cmp byte [rbx + ATAD_MODEL + rcx - 1], ' '
        ja .trimmed
        dec ecx
        mov byte [rbx + ATAD_MODEL + rcx], 0
        jmp .trim
.trimmed:
        mov qword [rbx + ATAD_PRESENT], 1
.next:
        inc edi
        jmp .drive
.done:
        pop rdi
        pop rbx
        ret

; edi = drive, rax = first sector, ecx = sectors (1 .. 256), rsi =
; buffer, r8d = 1 to write -> CF set on an error. Keeps rbx, rbp, r12 ..
; r15.
ata_transfer:
        push rbx
        push r9
        push r10
        mov r9, rax
        mov r10d, ecx
        mov rbx, rsi
        lea rax, [r9 + r10]
        cmp rax, 0x10000000             ; LBA28
        ja .fail
        call ata_ports                  ; dx: base, al: the drive bits
        mov cl, al
        mov rax, r9
        shr rax, 24
        and al, 0x0f                    ; LBA bits 24-27
        or al, cl
        push rdx
        add dx, 6
        out dx, al
        pop rdx
        call ata_wait_ready
        jc .fail
        push rdx
        add dx, 2
        mov al, r10b                    ; 256 is 0
        out dx, al
        inc dx
        mov rax, r9
        out dx, al
        inc dx
        shr rax, 8
        out dx, al
        inc dx
        shr rax, 8
        out dx, al
        add dx, 2
        mov al, 0x20                    ; READ SECTORS
        test r8d, r8d
        jz .command
        mov al, 0x30                    ; WRITE SECTORS
.command:
        out dx, al
        pop rdx
.sector:
        call ata_wait_drq
        jc .fail
        mov ecx, 256
        test r8d, r8d
        jnz .write
        push rdi
        mov rdi, rbx
        rep insw
        pop rdi
        jmp .moved
.write:
        mov rsi, rbx
        rep outsw
.moved:
        add rbx, 512
        dec r10d
        jnz .sector
        test r8d, r8d
        jz .ok
        call ata_wait_ready
        jc .fail
        push rdx
        add dx, 7
        mov al, 0xe7                    ; FLUSH CACHE
        out dx, al
        pop rdx
        call ata_wait_ready
        jc .fail
.ok:
        clc
        jmp .out
.fail:
        stc
.out:
        pop r10
        pop r9
        pop rbx
        ret

; rsi = string value (a device name) -> eax: the drive 0 .. 3, or -1
; when it is not a present disk. Keeps the others.
ata_drive_of:
        push rsi
        mov rsi, [rsi + VAL_DATA]
        cmp qword [rsi + STR_LEN], 8
        jne .none
        cmp dword [rsi + STR_BYTES], '/dev'
        jne .none
        cmp word [rsi + STR_BYTES + 4], '/h'
        jne .none
        cmp byte [rsi + STR_BYTES + 6], 'd'
        jne .none
        movzx eax, byte [rsi + STR_BYTES + 7]
        sub eax, 'a'
        cmp eax, ATA_DRIVES
        jae .none
        push rax
        shl rax, 6
        cmp qword [ata_drive + rax + ATAD_PRESENT], 0
        pop rax
        je .none
        pop rsi
        ret
.none:
        mov eax, -1
        pop rsi
        ret

; edi = drive, rax = byte offset, rcx = bytes (<= ATA_CHUNK * 512 - 512),
; rsi = destination -> CF set on an error or past the end of the disk
dev_read:
        push rbx
        push r8
        push r12
        push r13
        push r14
        mov r12, rax
        mov r13, rcx
        mov r14, rsi
        mov rbx, rdi
        shl rbx, 6
        lea rax, [r12 + r13 + 511]
        shr rax, 9
        cmp rax, [ata_drive + rbx + ATAD_SECTORS]
        ja .fail
        test r13, r13
        jz .ok
        mov rax, r12
        shr rax, 9                      ; first sector
        lea rcx, [r12 + r13 + 511]
        shr rcx, 9
        sub rcx, rax                    ; sectors
        mov rsi, ata_buffer
        xor r8d, r8d
        call ata_transfer
        jc .fail
        mov rsi, r12
        and esi, 511
        add rsi, ata_buffer
        mov rcx, r13
        push rdi
        mov rdi, r14
        rep movsb
        pop rdi
.ok:
        clc
        jmp .out
.fail:
        stc
.out:
        pop r14
        pop r13
        pop r12
        pop r8
        pop rbx
        ret

; edi = drive, rax = byte offset, rcx = bytes (<= ATA_CHUNK * 512 -
; 512), rsi = source -> CF set on an error; sectors the bytes cover in
; part are read first
dev_write:
        push rbx
        push r8
        push r12
        push r13
        push r14
        push r15
        mov r12, rax
        mov r13, rcx
        mov r14, rsi
        mov rbx, rdi
        shl rbx, 6
        lea rax, [r12 + r13 + 511]
        shr rax, 9
        cmp rax, [ata_drive + rbx + ATAD_SECTORS]
        ja .fail
        test r13, r13
        jz .ok
        mov r15, r12
        shr r15, 9                      ; first sector
        lea rcx, [r12 + r13 + 511]
        shr rcx, 9
        sub rcx, r15                    ; sectors
        mov rax, r12
        or rax, r13
        test eax, 511
        jz .whole
        ; read what the bytes do not cover
        push rcx
        mov rax, r15
        mov rsi, ata_buffer
        xor r8d, r8d
        call ata_transfer
        pop rcx
        jc .fail
.whole:
        push rcx
        push rdi
        mov rsi, r14
        mov rdi, r12
        and edi, 511
        add rdi, ata_buffer
        mov rcx, r13
        rep movsb
        pop rdi
        pop rcx
        mov rax, r15
        mov rsi, ata_buffer
        mov r8d, 1
        call ata_transfer
        jc .fail
.ok:
        clc
        jmp .out
.fail:
        stc
.out:
        pop r15
        pop r14
        pop r13
        pop r12
        pop r8
        pop rbx
        ret

DEV_PIECE       equ (ATA_CHUNK - 1) * 512

; The block numbers of bread/bwrite/bcopy from argument index r8 on:
; [lba, count, sector] into blk_args (count 1 and sector 512 unless
; given), for esi arguments at rdi. CF set when one is not an integer.
blk_numbers:
        mov qword [blk_args + 8], 1
        mov qword [blk_args + 16], 512
        xor ecx, ecx
.next:
        lea eax, [r8d + ecx]
        cmp eax, esi
        jae .done
        cmp ecx, 3
        jae .done
        shl eax, 4
        cmp qword [rdi + rax], T_INT
        jne .bad
        mov rax, [rdi + rax + 8]
        mov [blk_args + rcx * 8], rax
        inc ecx
        jmp .next
.done:
        mov rax, [blk_args]
        or rax, [blk_args + 8]
        js .bad
        mov rax, [blk_args + 16]
        test rax, rax
        jle .bad
        cmp rax, 65536
        ja .bad
        clc
        ret
.bad:
        stc
        ret

; bread(v): dev lba [count [sector]] -> the bytes, or null
bi_bread:
        cmp esi, 2
        jb .usage
        cmp esi, 4
        ja .usage
        cmp qword [rdi], T_STR
        jne .usage
        push rdx
        mov r8d, 1
        call blk_numbers
        jc .bad_numbers
        mov rsi, rdi
        call ata_drive_of
        cmp eax, -1
        je .null
        push rbx
        push r12
        push r13
        push r14
        push r15
        mov ebx, eax
        mov rax, [blk_args + 8]
        imul rax, [blk_args + 16]
        cmp rax, 1 << 24
        ja .null_pop
        mov r13, rax                    ; bytes
        mov rax, [blk_args]
        imul rax, [blk_args + 16]
        mov r12, rax                    ; offset
        ; the vector, its items all at once
        call vec_new
        mov r14, rax
        lea rdi, [r13 * 8 + 8]
        shl rdi, 1
        call alloc
        mov [r14 + VEC_ITEMS], rax
        mov [r14 + VEC_CAP], r13
        mov r15, rax                    ; the next item
.piece:
        test r13, r13
        jz .read
        mov rcx, r13
        cmp rcx, DEV_PIECE
        jbe .size
        mov rcx, DEV_PIECE
.size:
        push rcx
        mov edi, ebx
        mov rax, r12
        mov rsi, ata_stage
        call dev_read
        pop rcx
        jc .null_pop
        add r12, rcx
        sub r13, rcx
        add [r14 + VEC_COUNT], rcx
        xor eax, eax
.item:
        mov qword [r15], T_INT
        movzx edx, byte [ata_stage + rax]
        mov [r15 + 8], rdx
        add r15, 16
        inc rax
        cmp rax, rcx
        jb .item
        jmp .piece
.read:
        mov rax, r14
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rdx
        jmp bi_vec_out
.null_pop:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
.null:
        pop rdx
        xor eax, eax
        ret
.bad_numbers:
        pop rdx
        mov eax, e_bread_numbers
        jmp bi_error
.usage:
        mov eax, e_bread
        jmp bi_error

; bwrite(n): dev lba bytes [sector] -> bytes written, or -1
bi_bwrite:
        cmp esi, 3
        jb .usage
        cmp esi, 4
        ja .usage
        cmp qword [rdi], T_STR
        jne .usage
        cmp qword [rdi + 32], T_VEC
        jne .usage
        push rdx
        mov qword [blk_args + 16], 512
        cmp qword [rdi + 16], T_INT
        jne .bad_numbers
        mov rax, [rdi + 24]
        mov [blk_args], rax
        test rax, rax
        js .bad_numbers
        cmp esi, 4
        jb .sector_ok
        cmp qword [rdi + 48], T_INT
        jne .bad_numbers
        mov rax, [rdi + 56]
        test rax, rax
        jle .bad_numbers
        cmp rax, 65536
        ja .bad_numbers
        mov [blk_args + 16], rax
.sector_ok:
        push rbx
        push r12
        push r13
        push r14
        push r15
        mov r14, [rdi + 40]             ; the vector
        mov r13, [r14 + VEC_COUNT]
        mov rax, r13
        xor edx, edx
        div qword [blk_args + 16]
        test rdx, rdx
        jnz .not_whole
        test r13, r13
        jz .not_whole
        mov rsi, rdi
        call ata_drive_of
        cmp eax, -1
        je .failed
        mov ebx, eax
        mov rax, [blk_args]
        imul rax, [blk_args + 16]
        mov r12, rax
        mov r15, [r14 + VEC_ITEMS]
        mov rax, r13
        mov [blk_args + 8], rax         ; what to report
.piece:
        test r13, r13
        jz .written
        mov rcx, r13
        cmp rcx, DEV_PIECE
        jbe .size
        mov rcx, DEV_PIECE
.size:
        xor eax, eax
.pack:
        cmp qword [r15], T_INT
        jne .not_bytes
        mov rdx, [r15 + 8]
        cmp rdx, 255
        ja .not_bytes
        mov [ata_stage + rax], dl
        add r15, 16
        inc rax
        cmp rax, rcx
        jb .pack
        push rcx
        mov edi, ebx
        mov rax, r12
        mov rsi, ata_stage
        call dev_write
        pop rcx
        jc .failed
        add r12, rcx
        sub r13, rcx
        jmp .piece
.written:
        mov rax, [blk_args + 8]
        jmp .result
.failed:
        mov rax, -1
.result:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rdx
        jmp bi_int_out
.not_bytes:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rdx
        mov eax, e_bwrite_bytes
        jmp bi_error
.not_whole:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rdx
        mov eax, e_bwrite_whole
        jmp bi_error
.bad_numbers:
        pop rdx
        mov eax, e_bread_numbers
        jmp bi_error
.usage:
        mov eax, e_bwrite
        jmp bi_error

; bcopy(n): src slba dst dlba count [sector] -> sectors copied, or -1
bi_bcopy:
        cmp esi, 5
        jb .usage
        cmp esi, 6
        ja .usage
        cmp qword [rdi], T_STR
        jne .usage
        cmp qword [rdi + 32], T_STR
        jne .usage
        cmp qword [rdi + 16], T_INT
        jne .usage
        push rdx
        mov rax, [rdi + 24]
        mov [blk_copy], rax             ; the source's first sector
        mov r8d, 3
        call blk_numbers                ; dlba, count, sector
        jc .bad_numbers
        mov rax, [blk_copy]
        test rax, rax
        js .bad_numbers
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov rsi, rdi
        call ata_drive_of
        mov ebx, eax                    ; the source drive
        lea rsi, [rdi + 32]
        call ata_drive_of
        mov ebp, eax                    ; the destination
        cmp ebx, -1
        je .failed
        cmp ebp, -1
        je .failed
        mov r15, [blk_args + 16]
        mov r12, [blk_copy]
        imul r12, r15                   ; source offset
        mov r13, [blk_args]
        imul r13, r15                   ; destination offset
        mov r14, [blk_args + 8]
        imul r14, r15                   ; bytes
.piece:
        test r14, r14
        jz .copied
        mov rcx, r14
        cmp rcx, DEV_PIECE
        jbe .size
        mov rcx, DEV_PIECE
.size:
        push rcx
        mov edi, ebx
        mov rax, r12
        mov rsi, ata_stage
        call dev_read
        pop rcx
        jc .failed
        push rcx
        mov edi, ebp
        mov rax, r13
        mov rsi, ata_stage
        call dev_write
        pop rcx
        jc .failed
        add r12, rcx
        add r13, rcx
        sub r14, rcx
        jmp .piece
.copied:
        mov rax, [blk_args + 8]
        jmp .result
.failed:
        mov rax, -1
.result:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop rdx
        jmp bi_int_out
.bad_numbers:
        pop rdx
        mov eax, e_bread_numbers
        jmp bi_error
.usage:
        mov eax, e_bcopy
        jmp bi_error

; bsize(n): dev -> bytes, or -1
bi_bsize:
        cmp esi, 1
        jne .none
        cmp qword [rdi], T_STR
        jne .none
        mov rsi, rdi
        call ata_drive_of
        cmp eax, -1
        je .none
        shl rax, 6
        mov rax, [ata_drive + rax + ATAD_SECTORS]
        shl rax, 9
        jmp bi_int_out
.none:
        mov rax, -1
        jmp bi_int_out

; bdevices(v): -> [path, bytes, kind, bus, model, sector] per disk
bi_bdevices:
        push rbx
        push r12
        push rdx
        call vec_new
        mov r12, rax
        xor ebx, ebx
.drive:
        cmp ebx, ATA_DRIVES
        jae .done
        mov rax, rbx
        shl rax, 6
        cmp qword [ata_drive + rax + ATAD_PRESENT], 0
        je .next
        call vec_new
        mov rdi, rax
        mov byte [dev_name + 7], 'a'
        add [dev_name + 7], bl
        mov esi, dev_name
        call new_string_z
        call vec_push_str
        mov rax, rbx
        shl rax, 6
        mov rax, [ata_drive + rax + ATAD_SECTORS]
        shl rax, 9
        call vec_push_int
        mov rax, rbx
        shl rax, 6
        mov esi, s_hdd
        cmp dword [ata_drive + rax + ATAD_MODEL], 'QEMU'
        jne .kind
        mov esi, s_virtual
.kind:
        call new_string_z
        call vec_push_str
        mov esi, s_ata
        call new_string_z
        call vec_push_str
        mov rax, rbx
        shl rax, 6
        lea rsi, [ata_drive + rax + ATAD_MODEL]
        call new_string_z
        call vec_push_str
        mov eax, 512
        call vec_push_int
        sub rsp, 16
        mov qword [rsp], T_VEC
        mov [rsp + 8], rdi
        mov rsi, rsp
        mov rdi, r12
        call vec_push
        add rsp, 16
.next:
        inc ebx
        jmp .drive
.done:
        mov rax, r12
        pop rdx
        pop r12
        pop rbx
        jmp bi_vec_out

n_bread:        db "bread", 0
n_bwrite:       db "bwrite", 0
n_bsize:        db "bsize", 0
n_bcopy:        db "bcopy", 0
n_bdevices:     db "bdevices", 0
dev_name:       db "/dev/hda", 0
s_hdd:          db "HDD", 0
s_virtual:      db "virtual", 0
s_ata:          db "ATA", 0
e_bread:        db "bread(v): device lba [count [sector]]", 0
e_bread_numbers: db "bread/bwrite/bcopy: bad lba, count or sector size", 0
e_bwrite:       db "bwrite(n): device lba bytes [sector]", 0
e_bwrite_bytes: db "bwrite expects a vector of bytes 0..255", 0
e_bwrite_whole: db "bwrite: the bytes are not a whole number of sectors", 0
e_bcopy:        db "bcopy(n): src slba dst dlba count [sector]", 0
