; ahci.inc - SATA disks for the Moscow runtime (ussr_rt.nasm includes it
; after ata.inc): an AHCI driver for the disks VirtualBox, QEMU's q35
; and -device ahci, and real PCs put on their SATA controllers. They are
; drives SATA_FIRST and up in ata.inc's table, named /dev/sda, /dev/sdb
; ... as Linux names them, and everything over the table - bread, bwrite,
; bdevices, the root file system, the installer - takes them as it takes
; the IDE ones.
;
; The first PCI device of class 01:06:01 (mass storage, SATA, AHCI) is
; the controller; its registers are at BAR5 (ABAR). Each implemented port
; with a disk on it (SStatus DET 3, signature 0x101: ATAPI and port
; multipliers are left alone) gets a command list, a received-FIS area
; and one command table of its own, in the runtime's data (identity
; mapped, so their addresses are what the controller sees), and one
; command slot is used. Transfers go by DMA (READ/WRITE DMA EXT, LBA48)
; to ata_buffer, the drive's cache is flushed after every write, and the
; driver polls: the controller's interrupts stay off.

AHCI_PORT_MEM   equ 2048                ; command list 1024, FIS 256, table 256
AHCI_FIS        equ 1024
AHCI_TABLE      equ 1280
AHCI_TIMEOUT    equ 20000000

; port registers, from the port's base (ABAR + 0x100 + port * 0x80)
AP_CLB          equ 0x00
AP_CLBU         equ 0x04
AP_FB           equ 0x08
AP_FBU          equ 0x0c
AP_IS           equ 0x10
AP_IE           equ 0x14
AP_CMD          equ 0x18
AP_TFD          equ 0x20
AP_SIG          equ 0x24
AP_SSTS         equ 0x28
AP_SERR         equ 0x30
AP_CI           equ 0x38

AP_CMD_ST       equ 1
AP_CMD_SUD      equ 2
AP_CMD_FRE      equ 0x10
AP_CMD_FR       equ 0x4000
AP_CMD_CR       equ 0x8000

; Finds the controller and its disks; drives SATA_FIRST + n. Keeps every
; register.
ahci_init:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r12
        push r13
        mov qword [ahci_abar], 0
        mov qword [ahci_count], 0
        ; bus, device and function, as pci_read takes them
        xor r8d, r8d
.find:
        cmp r8d, 8 << 16
        jae .out
        mov eax, r8d
        call pci_read
        cmp ax, 0xffff
        je .next_fn
        mov eax, r8d
        or eax, 8
        call pci_read
        shr eax, 8
        cmp eax, 0x010601
        je .found
.next_fn:
        add r8d, 1 << 8
        jmp .find
.found:
        mov eax, r8d
        or eax, 0x24                    ; BAR5
        call pci_read
        and eax, ~0xf
        jz .out
        mov [ahci_abar], rax
        ; memory space and bus mastering on
        mov eax, r8d
        or eax, 4
        call pci_read
        mov ecx, eax
        and ecx, 0xffff
        or ecx, 6
        mov eax, r8d
        or eax, 4
        call pci_write
        mov rbx, [ahci_abar]
        or dword [rbx + 4], 0x80000000  ; GHC.AE: AHCI mode
        mov r13d, [rbx + 0x0c]          ; PI: the ports there are
        xor esi, esi                    ; the port
.port:
        cmp esi, 32
        jae .out
        cmp qword [ahci_count], AHCI_MAX
        jae .out
        bt r13d, esi
        jnc .next_port
        mov rbx, [ahci_abar]
        mov eax, esi
        shl eax, 7
        lea rbx, [rbx + rax + 0x100]
        mov eax, [rbx + AP_SSTS]
        and eax, 0xf
        cmp eax, 3                      ; a device, and the link up
        jne .next_port
        cmp dword [rbx + AP_SIG], 0x00000101
        jne .next_port
        mov rax, [ahci_count]
        imul r12, rax, AHCI_PORT_MEM
        add r12, ahci_mem
        call ahci_port_start
        jc .next_port
        call ahci_identify
        jc .next_port
        mov rax, [ahci_count]
        mov [ahci_ports + rax], sil
        inc qword [ahci_count]
.next_port:
        inc esi
        jmp .port
.out:
        pop r13
        pop r12
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; rbx = the port's registers, edx = mask -> waits until [rbx + ecx] &
; edx is 0 (ecx: the register). CF set on a timeout. Keeps the others.
ahci_wait_clear:
        push rax
        push r8
        mov r8d, AHCI_TIMEOUT
.poll:
        mov eax, [rbx + rcx]
        test eax, edx
        jz .ok
        pause
        dec r8d
        jnz .poll
        stc
        jmp .out
.ok:
        clc
.out:
        pop r8
        pop rax
        ret

; rbx = the port's registers, r12 = its memory: the command engine
; stopped, the lists set, then started. CF set when it does not come up.
ahci_port_start:
        push rax
        push rcx
        push rdx
        push rdi
        and dword [rbx + AP_CMD], ~AP_CMD_ST
        mov ecx, AP_CMD
        mov edx, AP_CMD_CR
        call ahci_wait_clear
        jc .out
        and dword [rbx + AP_CMD], ~AP_CMD_FRE
        mov edx, AP_CMD_FR
        call ahci_wait_clear
        jc .out
        mov rdi, r12
        mov ecx, AHCI_PORT_MEM / 8
        xor eax, eax
        rep stosq
        mov [rbx + AP_CLB], r12d
        mov dword [rbx + AP_CLBU], 0
        lea rax, [r12 + AHCI_FIS]
        mov [rbx + AP_FB], eax
        mov dword [rbx + AP_FBU], 0
        mov dword [rbx + AP_SERR], 0xffffffff
        mov dword [rbx + AP_IS], 0xffffffff
        mov dword [rbx + AP_IE], 0
        or dword [rbx + AP_CMD], AP_CMD_FRE | AP_CMD_SUD
        mov ecx, AP_TFD
        mov edx, 0x88                   ; BSY, DRQ
        call ahci_wait_clear
        jc .out
        or dword [rbx + AP_CMD], AP_CMD_ST
        clc
.out:
        pop rdi
        pop rdx
        pop rcx
        pop rax
        ret

; rbx = the port's registers, r12 = its memory, al = the ATA command, r9 =
; LBA, r10d = sectors (0: no data), r11 = buffer, r8d = 1 for a write ->
; CF set on an error or a timeout. Keeps the others.
ahci_command:
        push rax
        push rcx
        push rdx
        push rdi
        mov dl, al
        ; the command header, slot 0: 5 dwords of FIS, one PRD or none
        mov eax, 5
        test r8d, r8d
        jz .read
        or eax, 1 << 6                  ; W: to the device
.read:
        test r10d, r10d
        jz .no_prd
        or eax, 1 << 16
.no_prd:
        mov [r12], eax
        mov dword [r12 + 4], 0
        lea rax, [r12 + AHCI_TABLE]
        mov [r12 + 8], eax
        mov dword [r12 + 12], 0
        ; the command table: a register FIS, host to device
        lea rdi, [r12 + AHCI_TABLE]
        push rdi
        mov ecx, 256 / 8
        xor eax, eax
        rep stosq
        pop rdi
        mov byte [rdi], 0x27
        mov byte [rdi + 1], 0x80        ; C: a command
        mov [rdi + 2], dl
        mov rax, r9
        mov [rdi + 4], al
        shr rax, 8
        mov [rdi + 5], al
        shr rax, 8
        mov [rdi + 6], al
        mov byte [rdi + 7], 0x40        ; LBA
        shr rax, 8
        mov [rdi + 8], al
        shr rax, 8
        mov [rdi + 9], al
        shr rax, 8
        mov [rdi + 10], al
        mov eax, r10d
        mov [rdi + 12], ax
        test r10d, r10d
        jz .issue
        ; the PRD: the buffer, its bytes - 1
        mov [rdi + 128], r11d
        mov dword [rdi + 132], 0
        mov eax, r10d
        shl eax, 9
        dec eax
        mov [rdi + 140], eax
.issue:
        mov ecx, AP_TFD
        mov edx, 0x88
        call ahci_wait_clear
        jc .out
        mov dword [rbx + AP_IS], 0xffffffff
        mov dword [rbx + AP_CI], 1
        mov ecx, AHCI_TIMEOUT
.poll:
        test dword [rbx + AP_IS], 1 << 30       ; TFES: the task file says error
        jnz .fail
        test dword [rbx + AP_CI], 1
        jz .done
        pause
        dec ecx
        jnz .poll
        jmp .fail
.done:
        test dword [rbx + AP_TFD], 1    ; ERR
        jnz .fail
        clc
        jmp .out
.fail:
        stc
.out:
        pop rdi
        pop rdx
        pop rcx
        pop rax
        ret

; rbx = the port's registers, r12 = its memory: IDENTIFY DEVICE into the
; drive table's next SATA entry. CF set when the disk does not answer.
ahci_identify:
        push rax
        push rcx
        push rdx
        push r8
        push r9
        push r10
        push r11
        push r14
        mov al, 0xec
        xor r8d, r8d
        xor r9d, r9d
        mov r10d, 1
        mov r11, ata_sector
        call ahci_command
        jc .out
        mov rax, [ahci_count]
        add rax, SATA_FIRST
        shl rax, 6
        lea r14, [ata_drive + rax]
        mov rax, [ata_sector + 200]     ; words 100-103: LBA48 sectors
        test rax, rax
        jnz .sectors
        mov eax, [ata_sector + 120]     ; words 60-61: LBA28 sectors
        test eax, eax
        jz .none
.sectors:
        mov [r14 + ATAD_SECTORS], rax
        mov rdx, r14
        call ata_model
        mov qword [r14 + ATAD_PRESENT], 1
        clc
        jmp .out
.none:
        stc
.out:
        pop r14
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdx
        pop rcx
        pop rax
        ret

; As ata_transfer, for edi = a SATA drive (SATA_FIRST ..): rax = first
; sector, ecx = sectors (1 .. ATA_CHUNK), rsi = ata_buffer, r8d = 1 to
; write -> CF set on an error. Keeps rbx, rbp, r12 .. r15.
ahci_transfer:
        push rbx
        push r9
        push r10
        push r11
        push r12
        mov r9, rax
        mov r10d, ecx
        mov r11, rsi
        lea eax, [edi - SATA_FIRST]
        cmp rax, [ahci_count]
        jae .fail
        imul r12, rax, AHCI_PORT_MEM
        add r12, ahci_mem
        movzx eax, byte [ahci_ports + rax]
        shl eax, 7
        mov rbx, [ahci_abar]
        lea rbx, [rbx + rax + 0x100]
        mov al, 0x25                    ; READ DMA EXT
        test r8d, r8d
        jz .go
        mov al, 0x35                    ; WRITE DMA EXT
.go:
        call ahci_command
        jc .out
        test r8d, r8d
        jz .out
        mov al, 0xea                    ; FLUSH CACHE EXT, no data
        xor r10d, r10d
        push r8
        xor r8d, r8d
        call ahci_command
        pop r8
        jmp .out
.fail:
        stc
.out:
        pop r12
        pop r11
        pop r10
        pop r9
        pop rbx
        ret
