; net.inc - the network card for the Moscow runtime (ussr_rt.nasm
; includes it): an Intel 8254x (e1000) driver, the card QEMU gives a pc
; machine, found on PCI bus 0, polled (its interrupts are off), and the
; builtins the kernel's TCP/IP stack (src/moscow/net.su) is written on:
;
;   net_info(v):              [mac, link up], mac a vector of 6 bytes;
;                             null when there is no card
;   net_recv(v):              the next Ethernet frame as a byte vector,
;                             or null when none has come
;   net_send(n): head [tail [from [count]]]
;                             sends one frame: head, a byte vector, then
;                             tail (a string or a byte vector, or a part
;                             of one); its length, -1 when there is no
;                             card or it is too long
;   net_sum(n): data [from [count]]
;                             the 16-bit one's complement sum of a string
;                             or a byte vector (big-endian words, folded,
;                             not inverted): checksums of IP, ICMP, UDP
;                             and TCP add such sums up
;
; Descriptor rings and buffers come from the heap once, at start, and
; are never given back.

E1000_VENDOR    equ 0x8086
NET_RX          equ 32                  ; receive descriptors
NET_TX          equ 16                  ; transmit descriptors
NET_BUF         equ 2048
NET_FRAME_MAX   equ 1514
; registers
E_CTRL          equ 0x0000
E_STATUS        equ 0x0008
E_IMC           equ 0x00d8
E_RCTL          equ 0x0100
E_TCTL          equ 0x0400
E_TIPG          equ 0x0410
E_RDBAL         equ 0x2800
E_RDBAH         equ 0x2804
E_RDLEN         equ 0x2808
E_RDH           equ 0x2810
E_RDT           equ 0x2818
E_TDBAL         equ 0x3800
E_TDBAH         equ 0x3804
E_TDLEN         equ 0x3808
E_TDH           equ 0x3810
E_TDT           equ 0x3818
E_MTA           equ 0x5200
E_RAL           equ 0x5400
E_RAH           equ 0x5404

; the 8254x models QEMU and real machines have, by PCI device id
e1000_ids:
        dw 0x100e, 0x100f, 0x1004, 0x10d3, 0x153a, 0x10ea, 0
e1000_ids_end:

; eax = bus << 16 | device << 11 | function << 8 | register -> eax: the
; dword there. Keeps the others.
pci_read:
        push rdx
        or eax, 0x80000000
        mov dx, 0xcf8
        out dx, eax
        mov dx, 0xcfc
        in eax, dx
        pop rdx
        ret

; eax = address as for pci_read, ecx = value. Keeps the others.
pci_write:
        push rdx
        push rax
        or eax, 0x80000000
        mov dx, 0xcf8
        out dx, eax
        mov eax, ecx
        mov dx, 0xcfc
        out dx, eax
        pop rax
        pop rdx
        ret

; register -> eax: its value
%macro EREAD 1
        mov rax, [net_mmio]
        mov eax, [rax + %1]
%endmacro
; register, value (a register or an immediate)
%macro EWRITE 2
        push r11
        mov r11, [net_mmio]
        mov dword [r11 + %1], %2
        pop r11
%endmacro

; Finds and starts the card. Keeps registers.
net_init:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        xor ebx, ebx                    ; device << 11, bus 0, function 0
.slot:
        cmp ebx, 32 << 11
        jae .none
        mov eax, ebx
        call pci_read
        cmp ax, E1000_VENDOR
        jne .next
        shr eax, 16
        mov rsi, e1000_ids
.id:
        cmp word [rsi], 0
        je .next
        cmp [rsi], ax
        je .found
        add rsi, 2
        jmp .id
.next:
        add ebx, 1 << 11
        jmp .slot
.found:
        mov eax, ebx
        or eax, 0x10                    ; BAR0: the registers, in memory
        call pci_read
        test eax, 1
        jnz .none                       ; an I/O BAR: not this driver's
        and eax, 0xfffffff0
        mov [net_mmio], rax
        mov eax, ebx
        or eax, 0x04                    ; command: memory space, bus master
        call pci_read
        mov ecx, eax
        or ecx, 0x06
        and ecx, 0xffff
        mov eax, ebx
        or eax, 0x04
        call pci_write
        ; reset, then link up with its speed found by itself
        EREAD E_CTRL
        or eax, 1 << 26
        EWRITE E_CTRL, eax
        mov ecx, 1000000
.reset:
        EREAD E_CTRL
        test eax, 1 << 26
        jz .reset_done
        pause
        loop .reset
.reset_done:
        EWRITE E_IMC, 0xffffffff        ; no interrupts: it is polled
        EREAD E_CTRL
        or eax, 0x60                    ; SLU, ASDE
        and eax, 0x7ffffff7              ; not LRST, not PHY_RST
        EWRITE E_CTRL, eax
        mov ecx, 128
        mov rdi, [net_mmio]
        add rdi, E_MTA
        xor eax, eax
        rep stosd                       ; no multicast
        ; the address the card was given (QEMU's -device e1000,mac=...)
        EREAD E_RAL
        mov [net_mac], eax
        EREAD E_RAH
        mov [net_mac + 4], ax
        or eax, 1 << 31                 ; address valid
        EWRITE E_RAH, eax
        ; the receive ring: every descriptor with a buffer
        mov edi, NET_RX * 16
        call alloc
        mov [net_rx_ring], rax
        mov edi, NET_RX * NET_BUF
        call alloc
        mov [net_rx_bufs], rax
        mov rsi, [net_rx_ring]
        xor ecx, ecx
.rx_desc:
        mov rdx, rcx
        imul rdx, rdx, NET_BUF
        add rdx, rax
        mov [rsi], rdx
        mov qword [rsi + 8], 0
        add rsi, 16
        inc ecx
        cmp ecx, NET_RX
        jb .rx_desc
        mov rax, [net_rx_ring]
        EWRITE E_RDBAL, eax
        shr rax, 32
        EWRITE E_RDBAH, eax
        EWRITE E_RDLEN, NET_RX * 16
        EWRITE E_RDH, 0
        EWRITE E_RDT, NET_RX - 1
        mov qword [net_rx_next], 0
        ; EN, BAM (broadcasts), 2048-byte buffers, strip the CRC
        EWRITE E_RCTL, (1 << 1) | (1 << 15) | (1 << 26)
        ; the transmit ring: every descriptor done, so free
        mov edi, NET_TX * 16
        call alloc
        mov [net_tx_ring], rax
        mov edi, NET_TX * NET_BUF
        call alloc
        mov [net_tx_bufs], rax
        mov rsi, [net_tx_ring]
        xor ecx, ecx
.tx_desc:
        mov rdx, rcx
        imul rdx, rdx, NET_BUF
        add rdx, rax
        mov [rsi], rdx
        mov qword [rsi + 8], 0
        mov byte [rsi + 12], 1          ; status DD
        add rsi, 16
        inc ecx
        cmp ecx, NET_TX
        jb .tx_desc
        mov rax, [net_tx_ring]
        EWRITE E_TDBAL, eax
        shr rax, 32
        EWRITE E_TDBAH, eax
        EWRITE E_TDLEN, NET_TX * 16
        EWRITE E_TDH, 0
        EWRITE E_TDT, 0
        mov qword [net_tx_next], 0
        EWRITE E_TIPG, 0x0060200a
        ; EN, PSP, collision threshold 15, collision distance 64
        EWRITE E_TCTL, (1 << 1) | (1 << 3) | (0x0f << 4) | (0x40 << 12)
        mov qword [net_up], 1
        jmp .out
.none:
        mov qword [net_up], 0
.out:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; net_info(v): -> [mac, link up], or null without a card
bi_net_info:
        cmp qword [net_up], 0
        je .none
        push rdx
        call vec_new
        mov rdi, rax
        call vec_new
        mov r8, rax
        xor ecx, ecx
.byte:
        movzx eax, byte [net_mac + rcx]
        push rdi
        mov rdi, r8
        call vec_push_int
        pop rdi
        inc ecx
        cmp ecx, 6
        jb .byte
        sub rsp, 16
        mov qword [rsp], T_VEC
        mov [rsp + 8], r8
        mov rsi, rsp
        call vec_push
        EREAD E_STATUS
        shr eax, 1
        and eax, 1                      ; LU
        mov qword [rsp], T_BOOL
        mov [rsp + 8], rax
        mov rsi, rsp
        call vec_push
        add rsp, 16
        mov rax, rdi
        pop rdx
        jmp bi_vec_out
.none:
        mov qword [rdx], T_NULL
        xor eax, eax
        ret

; net_recv(v): -> the next frame, or null
bi_net_recv:
        cmp qword [net_up], 0
        je .none
        mov rcx, [net_rx_next]
        mov r8, rcx
        shl r8, 4
        add r8, [net_rx_ring]
        test byte [r8 + 12], 1          ; DD: the card has filled it
        jz .none
        push rdx
        push rbx
        push r12
        mov rbx, r8
        movzx r12d, word [rbx + 8]      ; length
        test byte [rbx + 12], 2         ; EOP: all of a frame
        jnz .whole
        xor r12d, r12d                  ; a frame over 2048 bytes: dropped
.whole:
        call vec_new
        mov rdi, rax
        test r12, r12
        jz .given
        push rdi
        lea rdi, [r12 * 8 + 8]
        shl rdi, 1
        call alloc
        pop rdi
        mov [rdi + VEC_ITEMS], rax
        mov [rdi + VEC_CAP], r12
        mov [rdi + VEC_COUNT], r12
        mov rsi, [rbx]                  ; the buffer
        xor ecx, ecx
.item:
        mov qword [rax], T_INT
        movzx edx, byte [rsi + rcx]
        mov [rax + 8], rdx
        add rax, 16
        inc ecx
        cmp rcx, r12
        jb .item
.given:
        ; the descriptor back to the card
        mov byte [rbx + 12], 0
        mov rcx, [net_rx_next]
        EWRITE E_RDT, ecx
        inc rcx
        and ecx, NET_RX - 1
        mov [net_rx_next], rcx
        mov rax, rdi
        pop r12
        pop rbx
        pop rdx
        jmp bi_vec_out
.none:
        mov qword [rdx], T_NULL
        xor eax, eax
        ret

; rsi = a value (string or byte vector), r8 = destination, r9 = room ->
; copies its bytes from rcx (count r10, -1: to the end) -> rax: bytes
; copied, -1 when it does not fit or is not one. Uses r11.
net_copy_bytes:
        cmp qword [rsi], T_STR
        je .string
        cmp qword [rsi], T_VEC
        jne .bad
        mov r11, [rsi + 8]
        mov rax, [r11 + VEC_COUNT]
        cmp rcx, rax
        ja .bad
        sub rax, rcx
        cmp r10, -1
        je .vec_count
        cmp r10, rax
        ja .bad
        mov rax, r10
.vec_count:
        cmp rax, r9
        ja .bad
        push rax
        mov r11, [r11 + VEC_ITEMS]
        shl rcx, 4
        add r11, rcx
.vec_byte:
        test rax, rax
        jz .vec_done
        mov cl, [r11 + 8]
        mov [r8], cl
        inc r8
        add r11, 16
        dec rax
        jmp .vec_byte
.vec_done:
        pop rax
        ret
.string:
        mov r11, [rsi + 8]
        mov rax, [r11 + STR_LEN]
        cmp rcx, rax
        ja .bad
        sub rax, rcx
        cmp r10, -1
        je .str_count
        cmp r10, rax
        ja .bad
        mov rax, r10
.str_count:
        cmp rax, r9
        ja .bad
        push rax
        push rsi
        push rdi
        lea rsi, [r11 + STR_BYTES + rcx]
        mov rdi, r8
        mov rcx, rax
        rep movsb
        mov r8, rdi
        pop rdi
        pop rsi
        pop rax
        ret
.bad:
        mov rax, -1
        ret

; net_send(n): head [tail [from [count]]] -> the frame's length, or -1
bi_net_send:
        cmp qword [net_up], 0
        je .fail
        test esi, esi
        jz .fail
        cmp esi, 4
        ja .fail
        push rdx
        push rbx
        push r12
        mov rbx, rdi
        mov r12d, esi
        ; wait for the descriptor to be free (the card is quick)
        mov rcx, [net_tx_next]
        shl rcx, 4
        add rcx, [net_tx_ring]
        mov rdx, rcx
        mov ecx, 10000000
.busy:
        test byte [rdx + 12], 1
        jnz .free
        pause
        loop .busy
        jmp .fail_pop
.free:
        mov r8, [rdx]                   ; its buffer
        mov r9, NET_FRAME_MAX
        mov rsi, rbx
        xor ecx, ecx
        mov r10, -1
        cmp qword [rsi], T_VEC
        jne .fail_pop
        call net_copy_bytes
        cmp rax, -1
        je .fail_pop
        sub r9, rax
        cmp r12d, 2
        jb .built
        lea rsi, [rbx + 16]
        xor ecx, ecx
        mov r10, -1
        cmp r12d, 3
        jb .tail
        cmp qword [rbx + 32], T_INT
        jne .fail_pop
        mov rcx, [rbx + 40]
        cmp r12d, 4
        jb .tail
        cmp qword [rbx + 48], T_INT
        jne .fail_pop
        mov r10, [rbx + 56]
.tail:
        call net_copy_bytes
        cmp rax, -1
        je .fail_pop
.built:
        sub r8, [rdx]                   ; the length
        cmp r8, 60                      ; the shortest frame, padded
        jae .long_enough
        mov rdi, [rdx]
        add rdi, r8
        mov rcx, 60
        sub rcx, r8
        xor eax, eax
        rep stosb
        mov r8d, 60
.long_enough:
        mov [rdx + 8], r8w              ; length
        mov byte [rdx + 10], 0          ; cso
        mov byte [rdx + 11], 0x0b       ; cmd: EOP, IFCS, RS
        mov byte [rdx + 12], 0          ; status
        mov rcx, [net_tx_next]
        inc rcx
        and ecx, NET_TX - 1
        mov [net_tx_next], rcx
        EWRITE E_TDT, ecx
        mov rax, r8
        pop r12
        pop rbx
        pop rdx
        jmp bi_int_out
.fail_pop:
        pop r12
        pop rbx
        pop rdx
.fail:
        mov rax, -1
        jmp bi_int_out

; net_sum(n): data [from [count]] -> the folded one's complement sum
bi_net_sum:
        test esi, esi
        jz .bad
        cmp esi, 3
        ja .bad
        cmp qword [rdi], T_STR
        je .string
        push rdx
        call byte_range
        pop rdx
        jc .bad
        xor eax, eax                    ; the sum
        xor r9d, r9d                    ; 0: a high byte next, 1: a low one
.item:
        test rcx, rcx
        jz .fold
        movzx r10d, byte [r8 + 8]
        test r9d, r9d
        jnz .low
        shl r10d, 8
.low:
        add rax, r10
        xor r9d, 1
        add r8, 16
        dec rcx
        jmp .item
.string:
        ; the same over a string's bytes [from, from + count)
        mov r8, [rdi + 8]
        mov r11, [r8 + STR_LEN]
        xor ecx, ecx
        cmp esi, 2
        jb .s_from
        cmp qword [rdi + 16], T_INT
        jne .bad
        mov rcx, [rdi + 24]
        cmp rcx, r11
        ja .bad
.s_from:
        mov r10, r11
        sub r10, rcx
        cmp esi, 3
        jb .s_count
        cmp qword [rdi + 32], T_INT
        jne .bad
        cmp [rdi + 40], r10
        ja .bad
        mov r10, [rdi + 40]
.s_count:
        lea r8, [r8 + STR_BYTES + rcx]
        mov rcx, r10
        xor eax, eax
        xor r9d, r9d
.s_byte:
        test rcx, rcx
        jz .fold
        movzx r10d, byte [r8]
        test r9d, r9d
        jnz .s_low
        shl r10d, 8
.s_low:
        add rax, r10
        xor r9d, 1
        inc r8
        dec rcx
        jmp .s_byte
.fold:
        mov rcx, rax
        shr rcx, 16
        jz .done
        and eax, 0xffff
        add rax, rcx
        jmp .fold
.done:
        jmp bi_int_out
.bad:
        mov eax, e_net_sum
        jmp bi_error
