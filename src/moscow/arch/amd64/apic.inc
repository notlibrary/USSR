; apic.inc - the APIC for the Moscow runtime (ussr_rt.nasm includes it
; after acpi.inc): the local APIC's timer gives the 100 Hz tick in place of
; the PIT through the 8259 PICs, which are masked, and the IOAPICs the
; MADT lists are set up with every line masked (the drivers poll; a device
; that interrupts gets its line there). Without an APIC (CPUID) the PICs
; and the PIT stay as idt_init set them.
;
; The timer runs at the bus clock / 16, which differs from one machine to
; the next: it is counted against the PIT's channel 2 over 10 ms, and that
; count is the period. EOI goes to the local APIC (timer_isr); vector 0xff
; is its spurious one, which takes no EOI. LINT0, the PICs' wire, stays
; as the firmware left it: the PICs are masked, and masking the wire with
; an interrupt the PIC latched meanwhile gives QEMU's CPU a vector of -1.

LAPIC_ID        equ 0x20
LAPIC_TPR       equ 0x80
LAPIC_EOI       equ 0xb0
LAPIC_SVR       equ 0xf0
LAPIC_LVT_TIMER equ 0x320
LAPIC_LVT_LINT0 equ 0x350
LAPIC_TIMER_INIT equ 0x380
LAPIC_TIMER_NOW equ 0x390
LAPIC_TIMER_DIV equ 0x3e0
MSR_APIC_BASE   equ 0x1b

; The local APIC on, its timer at 100 Hz; the IOAPICs masked. Keeps
; every register.
apic_init:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        mov qword [apic_on], 0
        mov eax, 1
        cpuid
        test edx, 1 << 9                ; an APIC
        jz .out
        mov ecx, MSR_APIC_BASE
        rdmsr
        test edx, edx
        jnz .out                        ; above 4 GiB
        or eax, 1 << 11                 ; global enable
        wrmsr
        and eax, 0xfffff000
        mov rbx, rax
        ; the MADT's address, when it has one
        mov rsi, [acpi_madt]
        test rsi, rsi
        jz .base
        mov eax, [rsi + 36]
        test eax, eax
        jz .base
        mov rbx, rax
.base:
        mov [lapic_base], rbx
        mov dword [rbx + LAPIC_TPR], 0
        mov dword [rbx + LAPIC_SVR], 0x1ff      ; on, spurious vector 0xff
        ; count against 10 ms of the PIT's channel 2 (gate in port 0x61)
        mov dword [rbx + LAPIC_TIMER_DIV], 3            ; divide by 16
        mov dword [rbx + LAPIC_LVT_TIMER], 0x10000 | 32 ; masked meanwhile
        in al, 0x61
        and al, 0xfc                    ; gate low, speaker off
        out 0x61, al
        mov al, 0xb0                    ; channel 2, both bytes, mode 0
        out 0x43, al
        mov al, 11932 & 0xff
        out 0x42, al
        mov al, 11932 >> 8
        out 0x42, al
        in al, 0x61
        or al, 1                        ; the gate: the count starts
        out 0x61, al
        mov dword [rbx + LAPIC_TIMER_INIT], 0xffffffff
        mov ecx, 100000000
.wait:
        in al, 0x61
        test al, 0x20                   ; channel 2's output: done
        jnz .counted
        dec ecx
        jnz .wait
        jmp .no_timer
.counted:
        mov eax, [rbx + LAPIC_TIMER_NOW]
        mov dword [rbx + LAPIC_TIMER_INIT], 0
        mov edx, 0xffffffff
        sub edx, eax
        cmp edx, 1000
        jb .no_timer
        mov [lapic_count], rdx
        in al, 0x61
        and al, 0xfc
        out 0x61, al
        ; the PICs masked, the tick from the local APIC
        mov al, 0xff
        out 0xa1, al
        out 0x21, al
        mov dword [rbx + LAPIC_LVT_TIMER], 0x20000 | 32  ; periodic, vector 32
        mov eax, [lapic_count]
        mov [rbx + LAPIC_TIMER_INIT], eax
        mov qword [apic_on], 1
        call ioapic_init
        jmp .out
.no_timer:
        ; the PIT stays: the local APIC's timer off, its wire back
        mov dword [rbx + LAPIC_TIMER_INIT], 0
        mov qword [lapic_base], 0
.out:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; The MADT's IOAPICs (entry type 1), every redirection entry masked
ioapic_init:
        push r8
        mov qword [ioapic_count], 0
        mov rsi, [acpi_madt]
        test rsi, rsi
        jz .out
        mov ecx, [rsi + 4]
        lea rdi, [rsi + rcx]            ; its end
        add rsi, 44                     ; the entries
.entry:
        lea rax, [rsi + 2]
        cmp rax, rdi
        ja .out
        movzx ecx, byte [rsi + 1]       ; its length
        test ecx, ecx
        jz .out
        cmp byte [rsi], 1
        jne .next
        mov ebx, [rsi + 4]              ; the IOAPIC's registers
        mov dword [rbx], 1              ; IOAPICVER
        mov eax, [rbx + 0x10]
        shr eax, 16
        and eax, 0xff                   ; the last redirection entry
        xor edx, edx
.mask:
        lea r8d, [edx * 2 + 0x10]
        mov [rbx], r8d
        mov dword [rbx + 0x10], 0x10000 ; masked
        inc r8d
        mov [rbx], r8d
        mov dword [rbx + 0x10], 0
        inc edx
        cmp edx, eax
        jbe .mask
        inc qword [ioapic_count]
.next:
        add rsi, rcx
        jmp .entry
.out:
        pop r8
        ret
