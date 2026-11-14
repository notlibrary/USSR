; acpi.inc - the ACPI tables, for the Moscow runtime (ussr_rt.nasm
; includes it): the RSDP (from a UEFI loader's boot information, else the
; BIOS areas: the EBDA's first KiB and 0xe0000 .. 0xfffff), the RSDT or
; XSDT it points to, and from them the FADT (power management: the PM1
; control ports, the SMI port that switches ACPI on, the DSDT) and the
; MADT (the local APIC's address and the IOAPICs, for apic.inc).
;
; Power off is the S5 sleep state: its SLP_TYP values are in the DSDT's
; \_S5_ package, AML, found by its name and read without an AML
; interpreter (a package of byte constants, as firmware writes it); SLP_TYP
; and SLP_EN go to PM1a's control port, and PM1b's when there is one.
; Everything is below 4 GiB, which the kernel maps; tables above are not
; looked at.

; acpi_* variables in the runtime's data: see ussr_rt.nasm

; Finds the tables. Keeps every register.
acpi_init:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        mov rax, [BOOTINFO + BI_RSDP]
        test rax, rax
        jnz .have
        ; the EBDA's first KiB, at the segment the BIOS data area says
        movzx eax, word [0x40e]
        shl eax, 4
        test eax, eax
        jz .bios_rom
        mov rsi, rax
        lea rdi, [rax + 1024]
        call acpi_scan
        jnc .have
.bios_rom:
        mov esi, 0xe0000
        mov edi, 0x100000
        call acpi_scan
        jc .out
.have:
        mov [acpi_rsdp], rax
        mov rbx, rax
        movzx eax, byte [rbx + 15]      ; revision: 2 and up have the XSDT
        mov [acpi_rev], rax
        xor esi, esi                    ; the table of tables
        mov ecx, 4                      ; bytes an entry
        cmp eax, 2
        jb .rsdt
        mov rsi, [rbx + 24]
        test rsi, rsi
        jz .rsdt
        mov ecx, 8
        mov rax, rsi
        shr rax, 32
        jz .sdt
        xor esi, esi
.rsdt:
        test rsi, rsi
        jnz .sdt
        mov esi, [rbx + 16]
        mov ecx, 4
.sdt:
        test rsi, rsi
        jz .out
        mov eax, [rsi + 4]              ; its length
        lea rdi, [rsi + rax]
        add rsi, 36                     ; the entries
.entry:
        lea rax, [rsi + rcx]
        cmp rax, rdi
        ja .tables_done
        mov rax, [rsi]
        cmp ecx, 8
        je .wide
        mov eax, eax
.wide:
        add rsi, rcx
        mov rdx, rax
        shr rdx, 32
        jnz .entry                      ; above 4 GiB
        test rax, rax
        jz .entry
        cmp dword [rax], 'FACP'
        jne .not_fadt
        mov [acpi_fadt], rax
        jmp .entry
.not_fadt:
        cmp dword [rax], 'APIC'
        jne .entry
        mov [acpi_madt], rax
        jmp .entry
.tables_done:
        call acpi_fadt_read
.out:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; rsi .. rdi: looks on 16-byte boundaries for "RSD PTR " whose first 20
; bytes sum to 0 -> rax, CF clear; CF set when there is none
acpi_scan:
        mov rax, 'RSD PTR '
.next:
        cmp rsi, rdi
        jae .none
        cmp [rsi], rax
        jne .step
        push rcx
        push rdx
        xor edx, edx
        xor ecx, ecx
.sum:
        add dl, [rsi + rcx]
        inc ecx
        cmp ecx, 20
        jb .sum
        test dl, dl
        pop rdx
        pop rcx
        jnz .step
        mov rax, rsi
        clc
        ret
.step:
        add rsi, 16
        jmp .next
.none:
        stc
        ret

; The FADT's power management, and S5 from the DSDT
acpi_fadt_read:
        mov rbx, [acpi_fadt]
        test rbx, rbx
        jz .out
        mov eax, [rbx + 48]             ; SMI_CMD
        mov [acpi_smi_cmd], rax
        movzx eax, byte [rbx + 52]      ; ACPI_ENABLE
        mov [acpi_enable_value], rax
        mov eax, [rbx + 64]             ; PM1a_CNT_BLK
        mov [acpi_pm1a], rax
        mov eax, [rbx + 68]             ; PM1b_CNT_BLK
        mov [acpi_pm1b], rax
        ; the DSDT: X_DSDT when the FADT is long enough to hold it
        mov eax, [rbx + 40]
        cmp dword [rbx + 4], 148
        jb .dsdt
        mov rdx, [rbx + 140]
        test rdx, rdx
        jz .dsdt
        mov rax, rdx
.dsdt:
        mov rdx, rax
        shr rdx, 32
        jnz .out
        test rax, rax
        jz .out
        cmp dword [rax], 'DSDT'
        jne .out
        ; \_S5_: NameOp (08), the name, PackageOp (12)
        mov ecx, [rax + 4]
        lea rdi, [rax + rcx - 8]
        lea rsi, [rax + 36]
.find:
        cmp rsi, rdi
        jae .out
        cmp dword [rsi], '_S5_'
        jne .find_next
        cmp byte [rsi + 4], 0x12
        jne .find_next
        cmp byte [rsi - 1], 0x08
        je .found
        cmp byte [rsi - 1], '\'
        jne .find_next
        cmp byte [rsi - 2], 0x08
        je .found
.find_next:
        inc rsi
        jmp .find
.found:
        add rsi, 5                      ; the PkgLength
        movzx ecx, byte [rsi]
        shr ecx, 6                      ; bytes of it after the first
        lea rsi, [rsi + rcx + 2]        ; past it and NumElements
        call acpi_byte
        mov [acpi_slp_a], rax
        call acpi_byte
        mov [acpi_slp_b], rax
        mov qword [acpi_s5], 1
.out:
        ret

; rsi = an AML byte constant -> rax its value, rsi past it
acpi_byte:
        movzx eax, byte [rsi]
        inc rsi
        cmp al, 0x0a                    ; BytePrefix
        jne .constant
        movzx eax, byte [rsi]
        inc rsi
        ret
.constant:
        cmp al, 0xff                    ; OnesOp
        jne .plain
        mov eax, 0xff
.plain:
        ret                             ; ZeroOp 0, OneOp 1, or a raw value

; S5 when the tables gave it; returns only when the machine is still on
acpi_poweroff:
        cmp qword [acpi_s5], 0
        je .out
        ; ACPI mode on (SCI_EN), through the SMI port when it is off
        mov rdx, [acpi_pm1a]
        test edx, edx
        jz .out
        in ax, dx
        test ax, 1
        jnz .on
        mov rdx, [acpi_smi_cmd]
        test edx, edx
        jz .on
        mov rax, [acpi_enable_value]
        test eax, eax
        jz .on
        out dx, al
        mov ecx, 10000000
.wait:
        mov rdx, [acpi_pm1a]
        in ax, dx
        test ax, 1
        jnz .on
        pause
        dec ecx
        jnz .wait
.on:
        mov rax, [acpi_slp_a]
        and eax, 7
        shl eax, 10
        or eax, 1 << 13                 ; SLP_EN
        mov rdx, [acpi_pm1a]
        out dx, ax
        mov rdx, [acpi_pm1b]
        test edx, edx
        jz .out
        mov rax, [acpi_slp_b]
        and eax, 7
        shl eax, 10
        or eax, 1 << 13
        out dx, ax
.out:
        ret

; k_acpi(v): -> [the ACPI revision (-1: no tables), S5 found, the APIC on
; (0: the 8259 PIC), IOAPICs, local APIC timer counts a tick (at its
; divider of 16), PM1a's control port, S5's SLP_TYPa]
bi_k_acpi:
        push rdx
        call vec_new
        mov rdi, rax
        mov rax, -1
        cmp qword [acpi_rsdp], 0
        je .rev
        mov rax, [acpi_rev]
.rev:
        call vec_push_int
        mov rax, [acpi_s5]
        call vec_push_int
        mov rax, [apic_on]
        call vec_push_int
        mov rax, [ioapic_count]
        call vec_push_int
        mov rax, [lapic_count]
        call vec_push_int
        mov rax, [acpi_pm1a]
        call vec_push_int
        mov rax, [acpi_slp_a]
        call vec_push_int
        mov rax, rdi
        pop rdx
        jmp bi_vec_out
