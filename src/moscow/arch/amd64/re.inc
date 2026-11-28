; Realtek RTL8111H/RTL8168H, PCI 10ec:8168. The PCI ID is shared with
; other revisions, so only the H/M MACs with this setup are enabled.
; The runtime polls the normal TX ring and RX ring, with offloads disabled.

RE_TX_BASE      equ 0x20
RE_COMMAND      equ 0x37
RE_TX_POLL      equ 0x38
RE_IMR          equ 0x3c
RE_ISR          equ 0x3e
RE_TX_CONFIG    equ 0x40
RE_RX_CONFIG    equ 0x44
RE_CONFIG_LOCK  equ 0x50
RE_PHY_STATUS   equ 0x6c
RE_ERI_DATA     equ 0x70
RE_ERI_COMMAND  equ 0x74
RE_EPHY         equ 0x80
RE_PHY_OCP      equ 0xb8
RE_MAC_OCP      equ 0xb0
RE_MCU          equ 0xd3
RE_MISC         equ 0xf0
RE_RX_MAX       equ 0xda
RE_CPLUS        equ 0xe0
RE_RX_BASE      equ 0xe4
RE_OWN          equ 1 << 31
RE_EOR          equ 1 << 30
RE_FRAME        equ (1 << 29) | (1 << 28)
RE_RX_ERROR     equ 0x00780000
RE_XID_MASK     equ 0x7cf

; Only probe an assigned, low MMIO BAR. The loader guarantees a 4 GiB
; identity map, even on a machine with less RAM.
rtl_bar:
        call pci_read
        cmp eax, -1
        je .bad
        test al, 9                      ; memory, non-prefetchable registers
        jnz .bad
        mov ecx, eax
        and ecx, 6
        cmp ecx, 4
        je .wide
        test ecx, ecx
        jnz .bad
        jmp .base
.wide:
        push rax
        lea eax, [rbx + rsi + 4]
        call pci_read
        test eax, eax
        pop rax
        jnz .bad
.base:
        and eax, -16
        ret
.bad:
        xor eax, eax
        ret

; ebx = PCI function. Bring an existing PM capability to D0 and clear
; ASPM/clock request on this endpoint. The list is bounded even if corrupt.
rtl_pci_wake:
        lea eax, [rbx + 4]
        call pci_read
        test eax, 1 << 20               ; status: capabilities list
        jz .done
        lea eax, [rbx + 0x34]
        call pci_read
        movzx esi, al
        mov edi, 48
.cap:
        and esi, 0xfc
        cmp esi, 0x40
        jb .done
        lea eax, [rbx + rsi]
        call pci_read
        movzx edx, ah
        cmp al, 1                       ; power management
        jne .express
        cmp esi, 0xf8
        ja .next
        lea eax, [rbx + rsi + 4]
        call pci_read
        test al, 3
        jz .next
        movzx ecx, ax
        and ecx, ~(3 | (1 << 15))        ; D0, leave PME status untouched
        lea eax, [rbx + rsi + 4]
        call pci_write
        ; D3hot to D0 can require 10 ms before MMIO works.
        mov ecx, 30000
.delay:
        mov eax, ebx
        call pci_read
        loop .delay
        jmp .next
.express:
        cmp al, 0x10
        jne .next
        cmp esi, 0xec
        ja .next
        lea eax, [rbx + rsi + 0x10]
        call pci_read
        movzx ecx, ax
        and ecx, ~(3 | (1 << 8))
        lea eax, [rbx + rsi + 0x10]
        call pci_write
.next:
        mov esi, edx
        dec edi
        jnz .cap
.done:
        ret

; Keeps the registers used by net_init. Returns with net_up set on success.
rtl_init:
        push r8
        push r9
        push r12
        xor ebx, ebx
.scan:
        mov eax, ebx
        call pci_read
        cmp eax, 0x816810ec
        je .candidate
.next:
        add ebx, 1 << 8
        cmp ebx, 256 << 16
        jb .scan
        jmp .out
.candidate:
        mov esi, 0x18                   ; BAR2 on RTL8111H
        lea eax, [rbx + rsi]
        call rtl_bar
        test eax, eax
        jnz .mapped
        mov esi, 0x14
        lea eax, [rbx + rsi]
        call rtl_bar
        test eax, eax
        jz .next
.mapped:
        mov r8, rax
        call rtl_pci_wake
        lea eax, [rbx + 4]
        call pci_read
        movzx r9d, ax
        mov ecx, r9d
        or ecx, 0x402                   ; memory decode and INTx disabled
        and ecx, ~4                    ; DMA only after rings are ready
        lea eax, [rbx + 4]
        call pci_write
        mov eax, [r8 + RE_TX_CONFIG]
        shr eax, 20
        and eax, RE_XID_MASK
        cmp eax, 0x541
        je .reset
        cmp eax, 0x540
        je .reset
        cmp eax, 0x6c0
        je .reset
        mov r12d, eax
        mov esi, rtl_unsupported
        call boot_progress
        mov eax, r12d
        call print_hex
        mov esi, rtl_newline
        call boot_progress
        jmp .restore
.reset:
        mov word [r8 + RE_IMR], 0
        ; Stop firmware RX before resetting the MAC. Discard any
        ; out-of-band controller state carried over from firmware.
        or dword [r8 + RE_MISC], 1 << 19
        and dword [r8 + RE_RX_CONFIG], ~0x3f
        mov ecx, 100000
.drain:
        mov al, [r8 + RE_MCU]
        and al, 0x30                   ; TX and RX FIFOs empty
        cmp al, 0x30
        je .stop
        pause
        loop .drain
.stop:
        and byte [r8 + RE_COMMAND], ~0x0c
        and byte [r8 + RE_MCU], ~0x80  ; NOW_IS_OOB
        mov byte [r8 + RE_COMMAND], 0x10
        mov ecx, 1000000
.wait:
        test byte [r8 + RE_COMMAND], 0x10
        jz .ready
        pause
        loop .wait
.failed:
        mov esi, rtl_failed
        call boot_progress
.restore:
        mov ecx, r9d
        and ecx, ~4                    ; leave DMA stopped after a failed probe
        lea eax, [rbx + 4]
        call pci_write
        jmp .next
.ready:
        mov eax, [r8]
        movzx edx, word [r8 + 4]
        test al, 1
        jnz .failed
        mov ecx, eax
        or ecx, edx
        jz .failed
        mov [net_mac], eax
        mov [net_mac + 4], dx

        mov byte [r8 + RE_CONFIG_LOCK], 0xc0
        and byte [r8 + 0x53], ~(1 << 7)  ; no clock request
        and byte [r8 + 0x56], ~1         ; no ASPM at the MAC
        and byte [r8 + 0x54], ~(1 << 1)  ; no L2/L3 ready
        or byte [r8 + 0x6f], 0xc0        ; keep the PHY PLL powered
        call rtl_configure_h
        jc .lock_failed
        mov word [r8 + RE_CPLUS], 0      ; no checksum/VLAN offload
        mov word [r8 + 0xe2], 0         ; no interrupt moderation
        mov byte [r8 + 0xec], 0x27       ; TX FIFO threshold
        mov word [r8 + RE_RX_MAX], NET_BUF
        mov word [r8 + RE_ISR], 0xffff

        mov edi, NET_RX * 16
        call rtl_ring
        mov [net_rx_ring], rsi
        mov edi, NET_RX * NET_BUF
        call alloc
        mov [net_rx_bufs], rax
        mov edx, NET_RX
        mov ecx, RE_OWN | NET_BUF
        call rtl_fill_ring
        mov rax, [net_rx_ring]
        mov rdx, rax
        shr rdx, 32
        mov [r8 + RE_RX_BASE + 4], edx
        mov [r8 + RE_RX_BASE], eax

        mov edi, NET_TX * 16
        call rtl_ring
        mov [net_tx_ring], rsi
        mov edi, NET_TX * NET_BUF
        call alloc
        mov [net_tx_bufs], rax
        mov edx, NET_TX
        xor ecx, ecx
        call rtl_fill_ring
        mov rax, [net_tx_ring]
        mov rdx, rax
        shr rdx, 32
        mov [r8 + RE_TX_BASE + 4], edx
        mov [r8 + RE_TX_BASE], eax
        mov byte [r8 + RE_CONFIG_LOCK], 0

        mov ecx, r9d
        or ecx, 0x406
        lea eax, [rbx + 4]
        call pci_write
        mov byte [r8 + RE_COMMAND], 0x0c
        mov dword [r8 + RE_RX_CONFIG], 0x0000cf0a ; own unicast and broadcast
        mov dword [r8 + RE_TX_CONFIG], 0x03000780 ; IFG, burst, automatic FIFO
        mov [net_mmio], r8
        mov qword [net_up], NET_REALTEK
        mov esi, rtl_detected
        call boot_progress
        jmp .out
.lock_failed:
        mov byte [r8 + RE_CONFIG_LOCK], 0
        jmp .failed
.out:
        pop r12
        pop r9
        pop r8
        ret

; Realtek descriptor bases must be 256-byte aligned.
rtl_ring:
        add edi, 255
        call alloc
        lea rsi, [rax + 255]
        and rsi, -256
        ret

; rsi = descriptor base, rax = buffer base, edx = count, ecx = options.
rtl_fill_ring:
.next:
        mov [rsi], ecx
        mov [rsi + 8], rax
        add rax, NET_BUF
        add rsi, 16
        dec edx
        jnz .next
        or dword [rsi - 16], RE_EOR
        ret

; Configure the H MAC FIFO/filter and exit receive gating. The same
; register meanings are documented by Linux r8169 and FreeBSD re.
rtl_configure_h:
        mov esi, rtl_h_ephy
.ephy:
        movzx eax, byte [rsi]
        cmp al, 0xff
        je .fifo
        shl eax, 16
        mov [r8 + RE_EPHY], eax
        lea rdi, [r8 + RE_EPHY]
        call rtl_wait_set
        jc .out
        movzx ecx, word [rsi + 1]        ; bits to clear
        not ecx
        and eax, ecx
        movzx ecx, word [rsi + 3]
        and eax, 0xffff
        or eax, ecx
        movzx ecx, byte [rsi]
        shl ecx, 16
        or eax, ecx
        or eax, RE_OWN
        mov [rdi], eax
        call rtl_wait_clear
        jc .out
        add rsi, 5
        jmp .ephy
.fifo:
        mov esi, rtl_h_eri
.eri:
        lodsd
        test eax, eax
        jz .filter
        mov ecx, eax
        lodsd
        mov [r8 + RE_ERI_DATA], eax
        lea rdi, [r8 + RE_ERI_COMMAND]
        mov [rdi], ecx
        call rtl_wait_clear
        jnc .eri
        ret
.filter:
        mov dword [rdi], 0xf0dc
        call rtl_wait_set
        jc .out
        mov eax, [r8 + RE_ERI_DATA]
        or eax, 0x1c
        and eax, ~1
        mov [r8 + RE_ERI_DATA], eax
        mov dword [rdi], 0x8000f0dc
        call rtl_wait_clear
        jc .out
        or dword [r8 + RE_ERI_DATA], 1
        mov dword [rdi], 0x8000f0dc
        call rtl_wait_clear
        jc .out
        and dword [r8 + RE_MISC], ~(1 << 19)
        and byte [r8 + 0xd0], ~0xc0
        and byte [r8 + 0xf2], ~0x40

        ; MAC OCP setup for H/M, with low-power entry disabled.
        mov esi, rtl_h_mac
.mac:
        movzx edx, word [rsi]
        test edx, edx
        jz .phy
        shl edx, 15
        mov [r8 + RE_MAC_OCP], edx
        movzx eax, word [r8 + RE_MAC_OCP]
        movzx ecx, word [rsi + 2]
        not ecx
        and eax, ecx
        movzx ecx, word [rsi + 4]
        or eax, ecx
        or eax, edx
        or eax, RE_OWN
        mov [r8 + RE_MAC_OCP], eax
        add rsi, 6
        jmp .mac
.phy:

        ; Wake the copper PHY and request negotiation if not already linked.
        test byte [r8 + RE_PHY_STATUS], 2
        jnz .configured
        lea rdi, [r8 + RE_PHY_OCP]
        mov dword [rdi], 0xa400 << 15   ; clause 22 BMCR
        call rtl_wait_set
        jc .out
        and eax, 0xffff
        and eax, ~0xcc00               ; reset, loopback, powerdown, isolate
        or eax, 0x92001200              ; write, BMCR address, restart autoneg
        mov [rdi], eax
        call rtl_wait_clear
        jc .out
.configured:
        clc
.out:
        ret

; Bit 31 is the ready/busy flag. Return the read value in eax, CF on timeout.
rtl_wait_set:
        mov ecx, 100000
.next:
        mov eax, [rdi]
        test eax, eax
        js rtl_wait_ok
        pause
        loop .next
        stc
        ret
rtl_wait_clear:
        mov ecx, 100000
.next:
        mov eax, [rdi]
        test eax, eax
        jns rtl_wait_ok
        pause
        loop .next
        stc
        ret
rtl_wait_ok:
        clc
        ret

rtl_h_ephy:
        db 0x1e
        dw 0x0800, 0x0001
        db 0x1d
        dw 0x0000, 0x0800
        db 0x05
        dw 0xffff, 0x2089
        db 0x06
        dw 0xffff, 0x5881
        db 0x04
        dw 0xffff, 0x854a
        db 0x01
        dw 0xffff, 0x068b
        db 0xff
rtl_h_eri:
        dd 0x8000f0c8, 0x00080002
        dd 0x8000f0e8, 0x00100006
        dd 0x800010cc, 0x38
        dd 0x800010d0, 0x48
        dd 0x800035f0, 0x4f87
        dd 0x800030c0, 0
        dd 0x800030b8, 0
        dd 0

rtl_h_mac:
        dw 0xe092, 0x00ff, 0
        dw 0xe056, 0x00f0, 0x0070
        dw 0xe052, 0x6000, 0x8008
        dw 0xe0d6, 0x01ff, 0x017f
        dw 0xd420, 0x0fff, 0x047f
        dw 0xe63e, 0xffff, 1
        dw 0xe63e, 0xffff, 0
        dw 0xc094, 0xffff, 0
        dw 0xc09e, 0xffff, 0
        dw 0

rtl_detected:    db "Moscow runtime: re0 (Realtek RTL8111H/RTL8168H) ready", 10, 0
rtl_failed:      db "Moscow runtime: re0 initialization failed", 10, 0
rtl_unsupported: db "Moscow runtime: Realtek 10ec:8168 unsupported MAC XID ", 0
rtl_newline:     db 10, 0
