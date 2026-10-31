; ussr_rt.nasm - the NASM USSR runtime: a .ru bytecode VM on bare amd64.
;
;     nasm -f bin -i src/moscow/arch/amd64/ ussr_rt.nasm -o ussr_rt.bin
;
; boot.nasm loads this file at RT_BASE, enters long mode and jumps here
; with the boot information block (layout.inc) in rdi. The runtime sets
; up the console (VGA text and COM1), interrupts (the PIT at 100 Hz) and
; the heap, then loads moscow.ru from the boot image and runs it. A
; program runs other programs of the image with k_exec(status): name.
;
; The VM follows docs/USSR_VM_SPEC.md and reads the .ru format of
; docs/RU_BYTECODE_FORMAT.md (format 3, ABI 3; signature, versions,
; length and CRC-32 are checked). What it has:
;   - every opcode except EXTERNAL, EVAL, SCAN, DECODE_UNO, GET, CHAIN,
;     FILE, CD and TEMPLATE (those stop the program with a message);
;   - values null, integer (64-bit, wrapping), real (SSE2), string,
;     boolean and vector; no structs;
;   - the builtins len byte slice chr vec push pop at typeof, and the
;     kernel services k_* (see bi_table at the end of the code).
; Names are interned per program at load time, so variables, hash
; entries (name! and name?) and builtins are found by index.
;
; Memory is a bump heap. k_exec marks it before loading a program and
; releases everything the program allocated when it ends, so a shell
; that exits gives all of its memory back.
%include "layout.inc"

        bits 64
        default abs
        org RT_BASE

T_NULL          equ 0
T_INT           equ 1
T_REAL          equ 2
T_STR           equ 3
T_BOOL          equ 4
T_VEC           equ 5
T_UNSET         equ 0xff            ; a hash entry never stored

OP_ADD          equ 0x10
OP_SUB          equ 0x11
OP_MUL          equ 0x12
OP_DIV          equ 0x13
OP_MOD          equ 0x14
OP_EQ           equ 0x16
OP_NE           equ 0x17
OP_LT           equ 0x18
OP_LE           equ 0x19
OP_GT           equ 0x1a
OP_GE           equ 0x1b
OP_LAND         equ 0x1c
OP_LOR          equ 0x1d
OP_XOR          equ 0x1e
OP_BAND         equ 0x1f
OP_BOR          equ 0x20
OP_SHL          equ 0x21
OP_SHR          equ 0x22
OP_HALT         equ 0xf0

MAX_FRAMES      equ 1024
LSTACK_BYTES    equ 1024 * 1024     ; locals of every active call
HCAP            equ 32768           ; intern hash slots (power of two)
SYM_MAX         equ 16384           ; distinct names in a program
LINE_MAX        equ 1000
VGA             equ 0xb8000

; A value: 16 bytes.
VAL_TAG         equ 0
VAL_DATA        equ 8
; A string: length, bytes, a NUL.  A vector: count, capacity, items.
STR_LEN         equ 0
STR_BYTES       equ 8
VEC_COUNT       equ 0
VEC_CAP         equ 8
VEC_ITEMS       equ 16

struc PROG
.code:          resq 1              ; the instructions, in the loaded file
.ncode:         resq 1
.consts:        resq 1              ; values
.nconsts:       resq 1
.strsym:        resq 1              ; u32 symbol per string-pool entry
.nstrings:      resq 1
.symtext:       resq 1              ; string per symbol
.nsyms:         resq 1
.htab:          resq 1              ; intern table: symbol + 1, or 0
.symbuiltin:    resq 1              ; u32 per symbol: builtin + 1, or 0
.funcs:         resq 1              ; FUNC records
.nfuncs:        resq 1
.globals:       resq 1              ; a value per symbol
.hash:          resq 1              ; a value per symbol, T_UNSET if none
.init:          resq 1              ; function index of init, or -1
endstruc

struc FUNC
.name:          resd 1
.ret:           resd 1              ; symbol, or -1
.entry:         resd 1
.nparams:       resd 1
.nlocals:       resd 1
.pad:           resd 1
.params:        resq 1              ; u32 symbols
.locals:        resq 1              ; u32 symbols
endstruc

struc FRAME
.regs:          resb 512            ; the caller's registers
.ret_ip:        resq 1
.ret_reg:       resq 1
.func:          resq 1
.prev_func:     resq 1
.prev_locals:   resq 1
.pad:           resq 1
endstruc

struc VM
.regs:          resb 512            ; R0..R31
.tmp:           resb 16             ; a builtin's result
.prog:          resq 1
.frames:        resq 1
.nframes:       resq 1
.ltop:          resq 1
.lend:          resq 1
.curfunc:       resq 1              ; FUNC of the innermost call, or 0
.curlocals:     resq 1
.rsp:           resq 1              ; native stack at entry, for errors
.has_init:      resq 1
endstruc

%macro COPYVAL 3                        ; dst, src, scratch
        mov %3, [%2]
        mov [%1], %3
        mov %3, [%2 + 8]
        mov [%1 + 8], %3
%endmacro

%macro REGADDR 2                        ; reg32, reg64: index -> address
        cmp %1, 32
        jae vm_bad_reg
        shl %1, 4
        add %2, rbx
%endmacro

%macro PUTC 1
        push rdi
        mov edi, %1
        call con_putc
        pop rdi
%endmacro

%macro PUTS 1
        push rsi
        mov esi, %1
        call con_puts
        pop rsi
%endmacro

; =====================================================================
; entry
; =====================================================================

start:
        cli
        cld
        mov rsp, RT_STACK_TOP
        mov rbx, rdi
        mov edi, RT_DATA
        mov ecx, (RT_STACK_TOP - RT_DATA) / 8
        xor eax, eax
        rep stosq
        mov [boot_info], rbx
        mov qword [vga_attr], 0x07

        ; SSE: no FPU emulation, monitor coprocessor, OSFXSR, OSXMMEXCPT
        mov rax, cr0
        and ax, 0xfffb
        or ax, 2
        mov cr0, rax
        mov rax, cr4
        or ax, 3 << 9
        mov cr4, rax

        call serial_init
        call vga_clear
        call idt_init
        call heap_init
        sti

        PUTS msg_start
        mov esi, name_moscow
        mov ecx, name_moscow_len
        call exec_file
        PUTS msg_ended
        movsxd rax, eax
        call print_int
        PUTS msg_halted
        jmp sys_halt

; Stops the machine. QEMU leaves with exit code 1 when started with
; -device isa-debug-exit,iobase=0xf4,iosize=4; otherwise ACPI power off
; (QEMU), otherwise the CPU halts.
sys_halt:
        cli
        mov dx, 0xf4
        xor eax, eax
        out dx, al
        mov dx, 0x604
        mov ax, 0x2000
        out dx, ax
.stop:
        hlt
        jmp .stop

sys_reboot:
        cli
.wait:
        in al, 0x64
        test al, 2
        jnz .wait
        mov al, 0xfe
        out 0x64, al
        lidt [null_idtr]                ; no luck: a triple fault resets
        int3
        jmp sys_halt

; The heap: from HEAP_BASE to the end of the RAM region holding it, as
; the BIOS map says, at most to the end of the identity map.
heap_init:
        mov qword [heap_ptr], HEAP_BASE
        mov qword [heap_end], 0
        mov rsi, [boot_info]
        mov ecx, [rsi + BI_NE820]
        lea rsi, [rsi + BI_E820]
.next:
        test ecx, ecx
        jz .done
        cmp dword [rsi + 16], 1         ; usable RAM
        jne .skip
        mov rax, [rsi]
        mov rdx, rax
        add rdx, [rsi + 8]
        cmp rax, HEAP_BASE
        ja .skip
        cmp rdx, HEAP_BASE
        jbe .skip
        mov [heap_end], rdx
.skip:
        add rsi, 24
        dec ecx
        jmp .next
.done:
        mov rax, [heap_end]
        test rax, rax
        jnz .have
        mov rax, 0x1000000              ; no map: assume 16 MiB
.have:
        cmp rax, MAP_LIMIT
        jbe .ok
        mov rax, MAP_LIMIT
.ok:
        mov [heap_end], rax
        ret

; rdi = bytes -> rax: zeroed memory, 16-byte aligned. Keeps every other
; register.
alloc:
        push rcx
        push rdi
        add rdi, 15
        and rdi, -16
        mov rax, [heap_ptr]
        lea rcx, [rax + rdi]
        cmp rcx, [heap_end]
        ja .oom
        mov [heap_ptr], rcx
        mov rcx, rdi
        push rax
        mov rdi, rax
        shr rcx, 3
        xor eax, eax
        rep stosq
        pop rax
        pop rdi
        pop rcx
        ret
.oom:
        PUTS msg_oom
        jmp sys_halt

; rsi = bytes, rcx = length -> rax: a new string. Keeps the others.
new_string:
        push rcx
        push rsi
        push rdi
        lea rdi, [rcx + 9]
        call alloc
        mov [rax + STR_LEN], rcx
        lea rdi, [rax + STR_BYTES]
        rep movsb
        pop rdi
        pop rsi
        pop rcx
        ret

; rsi = NUL-terminated text -> rax: a new string
new_string_z:
        push rcx
        call strlen_z
        call new_string
        pop rcx
        ret

; rsi -> rcx: length of the NUL-terminated text
strlen_z:
        xor ecx, ecx
.next:
        cmp byte [rsi + rcx], 0
        je .done
        inc rcx
        jmp .next
.done:
        ret

; -> rax: an empty vector
vec_new:
        push rdi
        mov edi, 24
        call alloc
        pop rdi
        ret

; rdi = vector, rsi = value: appends a copy. Keeps every register.
vec_push:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        mov r8, rdi
        mov rax, [r8 + VEC_COUNT]
        cmp rax, [r8 + VEC_CAP]
        jb .room
        mov rdx, [r8 + VEC_CAP]
        shl rdx, 1
        jnz .grow
        mov edx, 8
.grow:
        mov [r8 + VEC_CAP], rdx
        mov rdi, rdx
        shl rdi, 4
        call alloc
        push rsi
        mov rsi, [r8 + VEC_ITEMS]
        mov rdi, rax
        mov rcx, [r8 + VEC_COUNT]
        shl rcx, 4
        rep movsb
        pop rsi
        mov [r8 + VEC_ITEMS], rax
.room:
        mov rax, [r8 + VEC_COUNT]
        shl rax, 4
        add rax, [r8 + VEC_ITEMS]
        COPYVAL rax, rsi, rcx
        inc qword [r8 + VEC_COUNT]
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rdi = vector, rax = integer: appends it. Keeps every register.
vec_push_int:
        push rsi
        sub rsp, 16
        mov qword [rsp], T_INT
        mov [rsp + 8], rax
        mov rsi, rsp
        call vec_push
        add rsp, 16
        pop rsi
        ret

; rdi = vector, rax = string: appends it. Keeps every register.
vec_push_str:
        push rsi
        sub rsp, 16
        mov qword [rsp], T_STR
        mov [rsp + 8], rax
        mov rsi, rsp
        call vec_push
        add rsp, 16
        pop rsi
        ret

; =====================================================================
; console: VGA text mode and COM1
; =====================================================================

serial_init:
        mov dx, COM1 + 1
        xor al, al
        out dx, al                      ; no UART interrupts
        mov dx, COM1 + 3
        mov al, 0x80
        out dx, al                      ; divisor latch
        mov dx, COM1
        mov al, 1
        out dx, al                      ; 115200 baud
        mov dx, COM1 + 1
        xor al, al
        out dx, al
        mov dx, COM1 + 3
        mov al, 3
        out dx, al                      ; 8N1
        mov dx, COM1 + 2
        mov al, 0xc1
        out dx, al                      ; FIFO on, kept: input may be waiting
        mov dx, COM1 + 4
        mov al, 3
        out dx, al                      ; DTR, RTS
        ret

; al -> COM1 as is
serial_raw:
        push rdx
        push rax
        mov dx, COM1 + 5
.wait:
        in al, dx
        test al, 0x20
        jz .wait
        pop rax
        mov dx, COM1
        out dx, al
        pop rdx
        ret

; dil -> both screens; "\n" is CR LF on the serial line. Keeps registers.
con_putc:
        push rax
        mov eax, edi
        cmp al, 10
        jne .raw
        mov al, 13
        call serial_raw
        mov al, 10
.raw:
        call serial_raw
        call vga_putc
        pop rax
        ret

; rsi = bytes, rcx = length. Keeps registers.
con_write:
        push rcx
        push rsi
        push rdi
.next:
        test rcx, rcx
        jz .done
        movzx edi, byte [rsi]
        call con_putc
        inc rsi
        dec rcx
        jmp .next
.done:
        pop rdi
        pop rsi
        pop rcx
        ret

; rsi = NUL-terminated text. Keeps registers.
con_puts:
        push rcx
        call strlen_z
        call con_write
        pop rcx
        ret

; Erases the character before the cursor.
con_backspace:
        push rax
        mov al, 8
        call serial_raw
        mov al, ' '
        call serial_raw
        mov al, 8
        call serial_raw
        cmp qword [vga_col], 0
        je .done
        dec qword [vga_col]
        push rdi
        call vga_cell
        mov ah, [vga_attr]
        mov al, ' '
        mov [rdi], ax
        pop rdi
        call vga_cursor
.done:
        pop rax
        ret

; -> rdi: the screen cell under the cursor
vga_cell:
        push rax
        mov rdi, [vga_row]
        imul rdi, rdi, 80
        add rdi, [vga_col]
        shl rdi, 1
        add rdi, VGA
        pop rax
        ret

; al -> the screen. Keeps registers.
vga_putc:
        push rax
        push rcx
        push rdi
        push rsi
        cmp al, 10
        je .newline
        cmp al, 13
        je .cr
        cmp al, 8
        je .back
        call vga_cell
        mov ah, [vga_attr]
        mov [rdi], ax
        inc qword [vga_col]
        cmp qword [vga_col], 80
        jb .done
.newline:
        mov qword [vga_col], 0
        inc qword [vga_row]
        cmp qword [vga_row], 25
        jb .done
        mov rsi, VGA + 160
        mov rdi, VGA
        mov ecx, 24 * 160
        rep movsb
        mov ah, [vga_attr]
        mov al, ' '
        mov ecx, 80
        rep stosw
        mov qword [vga_row], 24
        jmp .done
.cr:
        mov qword [vga_col], 0
        jmp .done
.back:
        cmp qword [vga_col], 0
        je .done
        dec qword [vga_col]
.done:
        call vga_cursor
        pop rsi
        pop rdi
        pop rcx
        pop rax
        ret

vga_cursor:
        push rax
        push rcx
        push rdx
        mov rcx, [vga_row]
        imul rcx, rcx, 80
        add rcx, [vga_col]
        mov dx, 0x3d4
        mov al, 0x0f
        out dx, al
        mov dx, 0x3d5
        mov al, cl
        out dx, al
        mov dx, 0x3d4
        mov al, 0x0e
        out dx, al
        mov dx, 0x3d5
        mov al, ch
        out dx, al
        pop rdx
        pop rcx
        pop rax
        ret

vga_clear:
        push rax
        push rcx
        push rdi
        mov rdi, VGA
        mov ah, [vga_attr]
        mov al, ' '
        mov ecx, 80 * 25
        rep stosw
        mov qword [vga_row], 0
        mov qword [vga_col], 0
        call vga_cursor
        pop rdi
        pop rcx
        pop rax
        ret

; -> eax: a character from COM1 or the PS/2 keyboard, or -1 when there
; is none yet.
con_poll:
        mov dx, COM1 + 5
        in al, dx
        test al, 1
        jz .keyboard
        mov dx, COM1
        in al, dx
        movzx eax, al
        ret
.keyboard:
        in al, 0x64
        test al, 1
        jz .none
        test al, 0x20                   ; mouse data: drop it
        jnz .drop
        in al, 0x60
        movzx eax, al
        cmp al, 0x2a
        je .shift_down
        cmp al, 0x36
        je .shift_down
        cmp al, 0xaa
        je .shift_up
        cmp al, 0xb6
        je .shift_up
        test al, 0x80                   ; key releases, 0xe0 prefixes
        jnz .none
        cmp al, KEYMAP_SIZE
        jae .none
        cmp qword [kbd_shift], 0
        jne .shifted
        movzx eax, byte [keymap + rax]
        jmp .key
.shifted:
        movzx eax, byte [keymap_shift + rax]
.key:
        test eax, eax
        jz .none
        ret
.shift_down:
        mov qword [kbd_shift], 1
        jmp .none
.shift_up:
        mov qword [kbd_shift], 0
        jmp .none
.drop:
        in al, 0x60
.none:
        mov eax, -1
        ret

; -> rax: a string, the next console line without its end. Echoes what
; is typed; Backspace erases. CR, LF and CR LF all end a line.
read_line:
        push rbx
        xor ebx, ebx
.wait:
        call con_poll
        cmp eax, -1
        jne .got
        sti
        hlt                             ; the timer wakes us
        jmp .wait
.got:
        cmp eax, 10
        jne .not_lf
        cmp qword [last_cr], 0
        mov qword [last_cr], 0
        jne .wait                       ; the LF of a CR LF
        jmp .end
.not_lf:
        mov qword [last_cr], 0
        cmp eax, 13
        je .cr
        cmp eax, 8
        je .back
        cmp eax, 127
        je .back
        cmp eax, 32
        jb .wait
        cmp ebx, LINE_MAX
        jae .wait
        mov [line_buf + rbx], al
        inc ebx
        mov edi, eax
        call con_putc
        jmp .wait
.back:
        test ebx, ebx
        jz .wait
        dec ebx
        call con_backspace
        jmp .wait
.cr:
        mov qword [last_cr], 1
.end:
        mov edi, 10
        call con_putc
        mov esi, line_buf
        mov ecx, ebx
        call new_string
        pop rbx
        ret

; --- numbers ----------------------------------------------------------

; rax: a signed integer, in decimal. Keeps registers.
print_int:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        sub rsp, 32
        test rax, rax
        jns .positive
        PUTC '-'
        neg rax                         ; INT64_MIN stays 2^63, unsigned
.positive:
        lea rdi, [rsp + 32]
        mov ecx, 10
.digit:
        xor edx, edx
        div rcx
        add dl, '0'
        dec rdi
        mov [rdi], dl
        test rax, rax
        jnz .digit
        mov rsi, rdi
        lea rcx, [rsp + 32]
        sub rcx, rdi
        call con_write
        add rsp, 32
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rax in hex, 0x and 16 digits. Keeps registers.
print_hex:
        push rax
        push rcx
        push rdi
        push rdx
        mov rdx, rax
        PUTC '0'
        PUTC 'x'
        mov ecx, 16
.digit:
        rol rdx, 4
        mov eax, edx
        and eax, 15
        movzx edi, byte [hex_digits + rax]
        call con_putc
        dec ecx
        jnz .digit
        pop rdx
        pop rdi
        pop rcx
        pop rax
        ret

; rcx = k >= 0 -> xmm1 = 10^k
pow10:
        push rcx
        movsd xmm1, [f_one]
.next:
        test rcx, rcx
        jz .done
        mulsd xmm1, [f_ten]
        dec rcx
        jmp .next
.done:
        pop rcx
        ret

; xmm0: a real as C's %g prints it (6 significant digits, trailing
; zeros dropped, exponent form below 1e-4 and from 1e6). Keeps
; registers but xmm0..xmm4.
print_real:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        sub rsp, 16                     ; six digits
        movq rax, xmm0
        btr rax, 63
        jnc .positive
        PUTC '-'
.positive:
        mov rdx, rax
        shr rdx, 52
        cmp edx, 0x7ff
        jne .finite
        shl rax, 12
        jnz .nan
        PUTS s_inf
        jmp .out
.nan:
        PUTS s_nan
        jmp .out
.finite:
        test rax, rax
        jnz .nonzero
        PUTC '0'
        jmp .out
.nonzero:
        movq xmm0, rax
        ; e: the decimal exponent, by stepping the value into [1, 10)
        xor r8d, r8d
        movsd xmm2, xmm0
.up:
        comisd xmm2, [f_ten]
        jb .down
        divsd xmm2, [f_ten]
        inc r8
        jmp .up
.down:
        comisd xmm2, [f_one]
        jae .scaled
        mulsd xmm2, [f_ten]
        dec r8
        jmp .down
.scaled:
        mov r10d, 3                     ; retries when e was off by one
.digits:
        ; r9 = round(x * 10^(5 - e)): six significant digits
        mov rcx, 5
        sub rcx, r8
        movsd xmm4, xmm0
        test rcx, rcx
        js .divide
        call pow10
        mulsd xmm4, xmm1
        jmp .round
.divide:
        neg rcx
        call pow10
        divsd xmm4, xmm1
.round:
        cvtsd2si r9, xmm4
        cmp r9, 1000000
        jb .not_high
        add r9, 5
        mov rax, r9
        xor edx, edx
        mov ecx, 10
        div rcx
        mov r9, rax
        inc r8
.not_high:
        cmp r9, 100000
        jae .text
        dec r10d
        jz .text
        dec r8
        jmp .digits
.text:
        ; the six digits, then sig = how many count
        mov rax, r9
        mov ecx, 10
        mov rdi, 5
.fill:
        xor edx, edx
        div rcx
        add dl, '0'
        mov [rsp + rdi], dl
        dec rdi
        jns .fill
        mov r9d, 6
.trim:
        cmp r9d, 1
        je .format
        cmp byte [rsp + r9 - 1], '0'
        jne .format
        dec r9d
        jmp .trim
.format:
        cmp r8, -4
        jl .exponent
        cmp r8, 6
        jge .exponent
        test r8, r8
        js .small
        ; e >= 0: e + 1 digits, then the rest after a point
        xor ecx, ecx
.int_part:
        movzx edi, byte [rsp + rcx]
        call con_putc
        inc rcx
        cmp rcx, r8
        jle .int_part
        cmp r9, rcx
        jle .out
        PUTC '.'
.frac_part:
        movzx edi, byte [rsp + rcx]
        call con_putc
        inc rcx
        cmp rcx, r9
        jl .frac_part
        jmp .out
.small:
        PUTC '0'
        PUTC '.'
        mov rcx, r8
        neg rcx
        dec rcx
.zeros:
        test rcx, rcx
        jz .small_digits
        PUTC '0'
        dec rcx
        jmp .zeros
.small_digits:
        xor ecx, ecx
.small_next:
        movzx edi, byte [rsp + rcx]
        call con_putc
        inc rcx
        cmp rcx, r9
        jl .small_next
        jmp .out
.exponent:
        movzx edi, byte [rsp]
        call con_putc
        cmp r9, 1
        je .e
        PUTC '.'
        mov ecx, 1
.exp_digits:
        movzx edi, byte [rsp + rcx]
        call con_putc
        inc rcx
        cmp rcx, r9
        jl .exp_digits
.e:
        PUTC 'e'
        mov rax, r8
        test rax, rax
        js .e_negative
        PUTC '+'
        jmp .e_value
.e_negative:
        PUTC '-'
        neg rax
.e_value:
        cmp rax, 10
        jae .e_print
        PUTC '0'
.e_print:
        call print_int
.out:
        add rsp, 16
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rsi = value; dl = 1 inside a vector (strings get backquotes). Prints
; it as ussr's print does, without the line end. Keeps registers.
print_value:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        mov rax, [rsi + VAL_TAG]
        cmp rax, T_NULL
        je .null
        cmp rax, T_INT
        je .int
        cmp rax, T_REAL
        je .real
        cmp rax, T_STR
        je .str
        cmp rax, T_BOOL
        je .bool
        cmp rax, T_VEC
        je .vec
        PUTC '?'
        jmp .done
.null:
        PUTS s_null
        jmp .done
.int:
        mov rax, [rsi + VAL_DATA]
        call print_int
        jmp .done
.real:
        movq xmm0, [rsi + VAL_DATA]
        call print_real
        jmp .done
.str:
        test dl, dl
        jz .str_body
        PUTC '`'
.str_body:
        mov r8, [rsi + VAL_DATA]
        mov rcx, [r8 + STR_LEN]
        lea rsi, [r8 + STR_BYTES]
        call con_write
        test dl, dl
        jz .done
        PUTC '`'
        jmp .done
.bool:
        cmp qword [rsi + VAL_DATA], 0
        je .false
        PUTS s_true
        jmp .done
.false:
        PUTS s_false
        jmp .done
.vec:
        PUTC '['
        mov r8, [rsi + VAL_DATA]
        xor ecx, ecx
.item:
        cmp rcx, [r8 + VEC_COUNT]
        jae .vec_end
        test rcx, rcx
        jz .no_comma
        PUTC ','
.no_comma:
        mov rsi, rcx
        shl rsi, 4
        add rsi, [r8 + VEC_ITEMS]
        mov dl, 1
        call print_value
        inc rcx
        jmp .item
.vec_end:
        PUTC ']'
.done:
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; =====================================================================
; interrupts: exceptions stop the machine, the PIT counts ticks
; =====================================================================

idt_init:
        xor ecx, ecx
.exceptions:
        mov rax, [exc_table + rcx * 8]
        call idt_set
        inc ecx
        cmp ecx, 32
        jb .exceptions
.rest:
        mov eax, timer_isr
        cmp ecx, 32
        je .set
        mov eax, irq_master_isr
        cmp ecx, 40
        jb .set
        mov eax, irq_slave_isr
        cmp ecx, 48
        jb .set
        mov eax, iret_isr
.set:
        call idt_set
        inc ecx
        cmp ecx, 256
        jb .rest
        lidt [idtr]

        ; PICs: IRQs 0..15 at vectors 32..47; only the timer unmasked
        mov al, 0x11
        out 0x20, al
        out 0xa0, al
        mov al, 32
        out 0x21, al
        mov al, 40
        out 0xa1, al
        mov al, 4
        out 0x21, al
        mov al, 2
        out 0xa1, al
        mov al, 1
        out 0x21, al
        out 0xa1, al
        mov al, 0xfe
        out 0x21, al
        mov al, 0xff
        out 0xa1, al

        ; PIT channel 0: 1193182 Hz / 11932 = 100 Hz
        mov al, 0x36
        out 0x43, al
        mov ax, 11932
        out 0x40, al
        mov al, ah
        out 0x40, al
        ret

; ecx = vector, rax = handler
idt_set:
        push rdi
        push rax
        mov edi, ecx
        shl rdi, 4
        add rdi, idt
        mov [rdi], ax
        mov word [rdi + 2], 0x08
        mov byte [rdi + 4], 0
        mov byte [rdi + 5], 0x8e        ; present interrupt gate
        shr rax, 16
        mov [rdi + 6], ax
        shr rax, 16
        mov [rdi + 8], eax
        mov dword [rdi + 12], 0
        pop rax
        pop rdi
        ret

timer_isr:
        push rax
        inc qword [ticks]
        mov al, 0x20
        out 0x20, al
        pop rax
        iretq

irq_master_isr:
        push rax
        mov al, 0x20
        out 0x20, al
        pop rax
        iretq

irq_slave_isr:
        push rax
        mov al, 0x20
        out 0xa0, al
        out 0x20, al
        pop rax
        iretq

iret_isr:
        iretq

%assign v 0
%rep 32
exc_stub_%[v]:
%if v != 8 && v != 10 && v != 11 && v != 12 && v != 13 && v != 14 && v != 17 && v != 21 && v != 29 && v != 30
        push qword 0
%endif
        push qword v
        jmp exc_common
%assign v v + 1
%endrep

; [rsp] vector, [rsp + 8] error code, [rsp + 16] rip
exc_common:
        PUTS msg_exc
        mov rax, [rsp]
        call print_int
        PUTS msg_exc_err
        mov rax, [rsp + 8]
        call print_hex
        PUTS msg_exc_rip
        mov rax, [rsp + 16]
        call print_hex
        PUTC 10
        cli
        mov dx, 0xf4
        mov al, 2                       ; QEMU exit code 5
        out dx, al
.stop:
        hlt
        jmp .stop

; =====================================================================
; the .ru loader
; =====================================================================

; rsi = file, rdx = size -> rax: a program, or 0 after a message.
rt_load:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov [ld_rsp], rsp
        mov rbx, rsi
        lea r13, [rsi + rdx]
        cmp rdx, 32
        jb .short
        mov rax, [rbx]
        mov rcx, 0x1a0a0d5253535589     ; 89 'USSR' 0d 0a 1a
        cmp rax, rcx
        jne .signature
        cmp word [rbx + 8], 3
        jne .version
        cmp word [rbx + 10], 0
        jne .version
        cmp dword [rbx + 12], 3
        jne .version
        mov eax, [rbx + 20]
        add rax, 28
        cmp rax, rdx
        jne .length
        mov rsi, rbx
        lea rcx, [rdx - 4]
        call crc32
        cmp eax, [r13 - 4]
        jne .crc
        sub r13, 4
        lea r12, [rbx + 24]

        mov edi, PROG_size
        call alloc
        mov r14, rax
        mov edi, HCAP * 4
        call alloc
        mov [r14 + PROG.htab], rax
        mov edi, SYM_MAX * 8
        call alloc
        mov [r14 + PROG.symtext], rax

        ; 1. code
        call rd_u32
        test eax, eax
        jz .bad_code
        cmp eax, 1000000
        ja .bad_code
        mov [r14 + PROG.ncode], rax
        mov [r14 + PROG.code], r12
        shl rax, 3
        add r12, rax
        cmp r12, r13
        ja .truncated
        cmp byte [r12 - 8], OP_HALT
        jne .no_halt

        ; 2. constants
        call rd_u32
        mov [r14 + PROG.nconsts], rax
        mov rdi, rax
        shl rdi, 4
        call alloc
        mov [r14 + PROG.consts], rax
        mov r15, rax
        xor ebp, ebp
.const:
        cmp rbp, [r14 + PROG.nconsts]
        jae .consts_done
        call rd_u8
        cmp eax, 0
        je .c_null
        cmp eax, 1
        je .c_int
        cmp eax, 2
        je .c_real
        cmp eax, 3
        je .c_str
        cmp eax, 4
        je .c_bool
        jmp .bad_const
.c_null:
        mov qword [r15], T_NULL
        jmp .c_next
.c_int:
        call rd_u64
        mov qword [r15], T_INT
        mov [r15 + 8], rax
        jmp .c_next
.c_real:
        call rd_u64
        mov qword [r15], T_REAL
        mov [r15 + 8], rax
        jmp .c_next
.c_str:
        call rd_string
        cmp rcx, -1
        je .bad_const
        call new_string
        mov qword [r15], T_STR
        mov [r15 + 8], rax
        jmp .c_next
.c_bool:
        call rd_u8
        mov qword [r15], T_BOOL
        mov [r15 + 8], rax
.c_next:
        add r15, 16
        inc rbp
        jmp .const
.consts_done:

        ; 3. strings: interned
        call rd_u32
        mov [r14 + PROG.nstrings], rax
        lea rdi, [rax * 4 + 4]
        call alloc
        mov [r14 + PROG.strsym], rax
        mov r15, rax
        xor ebp, ebp
.string:
        cmp rbp, [r14 + PROG.nstrings]
        jae .strings_done
        call rd_string
        mov eax, -1
        cmp rcx, -1
        je .s_store
        call intern
.s_store:
        mov [r15 + rbp * 4], eax
        inc rbp
        jmp .string
.strings_done:

        ; 4. functions
        call rd_u32
        mov [r14 + PROG.nfuncs], rax
        imul rdi, rax, FUNC_size
        add rdi, 8
        call alloc
        mov [r14 + PROG.funcs], rax
        mov r15, rax
        xor ebp, ebp
.func:
        cmp rbp, [r14 + PROG.nfuncs]
        jae .funcs_done
        call rd_string
        cmp rcx, -1
        je .bad_func
        call intern
        mov [r15 + FUNC.name], eax
        call rd_string
        mov eax, -1
        cmp rcx, -1
        je .f_ret
        call intern
.f_ret:
        mov [r15 + FUNC.ret], eax
        call rd_u32
        cmp rax, [r14 + PROG.ncode]
        jae .bad_func
        mov [r15 + FUNC.entry], eax
        call rd_u32
        cmp eax, 32
        ja .bad_func
        mov [r15 + FUNC.nparams], eax
        call rd_names
        mov [r15 + FUNC.params], rax
        call rd_u32
        cmp eax, 65536
        ja .bad_func
        mov [r15 + FUNC.nlocals], eax
        call rd_names
        mov [r15 + FUNC.locals], rax
        add r15, FUNC_size
        inc rbp
        jmp .func
.funcs_done:
        ; the call-site sections are not needed: no SCAN, and builtins
        ; take values only

        ; per-symbol tables
        mov rdi, [r14 + PROG.nsyms]
        shl rdi, 4
        add rdi, 16
        call alloc
        mov [r14 + PROG.globals], rax
        call alloc
        mov [r14 + PROG.hash], rax
        mov rcx, [r14 + PROG.nsyms]
.unset:
        test rcx, rcx
        jz .unset_done
        dec rcx
        mov rdx, rcx
        shl rdx, 4
        mov qword [rax + rdx], T_UNSET
        jmp .unset
.unset_done:
        mov rdi, [r14 + PROG.nsyms]
        lea rdi, [rdi * 4 + 4]
        call alloc
        mov [r14 + PROG.symbuiltin], rax
        mov r15, rax
        xor ebp, ebp
.sym:
        cmp rbp, [r14 + PROG.nsyms]
        jae .syms_done
        mov rax, [r14 + PROG.symtext]
        mov rsi, [rax + rbp * 8]
        xor ecx, ecx
.builtin:
        mov rdi, rcx
        shl rdi, 4
        mov rdi, [bi_table + rdi]
        test rdi, rdi
        jz .sym_next
        call str_eq_z
        je .is_builtin
        inc ecx
        jmp .builtin
.is_builtin:
        inc ecx
        mov [r15 + rbp * 4], ecx
.sym_next:
        inc rbp
        jmp .sym
.syms_done:

        ; init(ret): arg_cnt arg_vec, if there is one
        mov qword [r14 + PROG.init], -1
        mov r15, [r14 + PROG.funcs]
        xor ebp, ebp
.find_init:
        cmp rbp, [r14 + PROG.nfuncs]
        jae .loaded
        mov eax, [r15 + FUNC.name]
        mov rdx, [r14 + PROG.symtext]
        mov rsi, [rdx + rax * 8]
        mov edi, s_init
        call str_eq_z
        jne .not_init
        cmp dword [r15 + FUNC.nparams], 2
        jne .bad_init
        mov [r14 + PROG.init], rbp
        jmp .loaded
.not_init:
        add r15, FUNC_size
        inc rbp
        jmp .find_init
.loaded:
        mov rax, r14
        jmp .out

.short:
.signature:
        mov esi, e_signature
        jmp load_fail
.version:
        mov esi, e_version
        jmp load_fail
.length:
        mov esi, e_length
        jmp load_fail
.crc:
        mov esi, e_crc
        jmp load_fail
.bad_code:
        mov esi, e_code
        jmp load_fail
.truncated:
        mov esi, e_truncated
        jmp load_fail
.no_halt:
        mov esi, e_no_halt
        jmp load_fail
.bad_const:
        mov esi, e_const
        jmp load_fail
.bad_func:
        mov esi, e_func
        jmp load_fail
.bad_init:
        mov esi, e_init
        jmp load_fail
.out:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; rsi = message: reports it and leaves rt_load with 0
load_fail:
        mov rsp, [ld_rsp]
        PUTS msg_ussr
        call con_puts
        PUTC 10
        xor eax, eax
        jmp rt_load.out

ld_truncated:
        mov esi, e_truncated
        jmp load_fail

; Readers: r12 the cursor, r13 the end.
rd_u8:
        cmp r12, r13
        jae ld_truncated
        movzx eax, byte [r12]
        inc r12
        ret

rd_u32:
        lea rax, [r12 + 4]
        cmp rax, r13
        ja ld_truncated
        mov eax, [r12]
        add r12, 4
        ret

rd_u64:
        lea rax, [r12 + 8]
        cmp rax, r13
        ja ld_truncated
        mov rax, [r12]
        add r12, 8
        ret

; -> rsi = bytes, rcx = length; rcx = -1 when the string is absent
rd_string:
        call rd_u32
        cmp eax, -1
        je .absent
        mov ecx, eax
        lea rax, [r12 + rcx]
        cmp rax, r13
        ja ld_truncated
        mov rsi, r12
        add r12, rcx
        ret
.absent:
        mov rcx, -1
        ret

; eax = count -> rax: u32 symbols of that many required strings
rd_names:
        push rbx
        push rbp
        mov ebp, eax
        lea rdi, [rax * 4 + 4]
        call alloc
        mov rbx, rax
        xor edx, edx
.next:
        cmp edx, ebp
        jae .done
        push rdx
        call rd_string
        cmp rcx, -1
        je .absent
        call intern
        pop rdx
        mov [rbx + rdx * 4], eax
        inc edx
        jmp .next
.absent:
        mov esi, e_func
        jmp load_fail
.done:
        mov rax, rbx
        pop rbp
        pop rbx
        ret

; rsi = bytes, rcx = length -> eax: the symbol of that name in program
; r14, added when new.
intern:
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
        mov eax, 2166136261             ; FNV-1a
        xor r8d, r8d
.hash:
        cmp r8, rcx
        jae .hashed
        movzx edx, byte [rsi + r8]
        xor eax, edx
        imul eax, eax, 16777619
        inc r8
        jmp .hash
.hashed:
        and eax, HCAP - 1
        mov r9d, eax
.probe:
        mov r10, [r14 + PROG.htab]
        mov eax, [r10 + r9 * 4]
        test eax, eax
        jz .new
        dec eax
        mov r11, [r14 + PROG.symtext]
        mov r11, [r11 + rax * 8]
        cmp [r11 + STR_LEN], rcx
        jne .next_slot
        test rcx, rcx
        jz .found
        push rcx
        push rsi
        lea rdi, [r11 + STR_BYTES]
        repe cmpsb
        pop rsi
        pop rcx
        je .found
.next_slot:
        inc r9d
        and r9d, HCAP - 1
        jmp .probe
.new:
        mov r11, [r14 + PROG.nsyms]
        cmp r11, SYM_MAX
        jae .too_many
        call new_string
        mov r10, [r14 + PROG.symtext]
        mov [r10 + r11 * 8], rax
        inc qword [r14 + PROG.nsyms]
        lea eax, [r11 + 1]
        mov r10, [r14 + PROG.htab]
        mov [r10 + r9 * 4], eax
        mov eax, r11d
.found:
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret
.too_many:
        mov esi, e_symbols
        jmp load_fail

; rsi = bytes, rcx = length -> eax: CRC-32 (IEEE)
crc32:
        mov eax, -1
.byte:
        test rcx, rcx
        jz .done
        xor al, [rsi]
        inc rsi
        mov edx, 8
.bit:
        shr eax, 1
        jnc .no_poly
        xor eax, 0xedb88320
.no_poly:
        dec edx
        jnz .bit
        dec rcx
        jmp .byte
.done:
        not eax
        ret

; rsi = string, rdi = NUL-terminated text -> ZF set when they are equal.
; Keeps registers.
str_eq_z:
        push rax
        push rcx
        push rdx
        mov rcx, [rsi + STR_LEN]
        xor edx, edx
.next:
        cmp rdx, rcx
        je .end
        mov al, [rdi + rdx]
        test al, al
        jz .differ
        cmp al, [rsi + STR_BYTES + rdx]
        jne .differ
        inc rdx
        jmp .next
.end:
        cmp byte [rdi + rdx], 0         ; ZF when the text ends too
        jmp .out
.differ:
        or edx, 1                       ; ZF clear
.out:
        pop rdx
        pop rcx
        pop rax
        ret

; =====================================================================
; running programs
; =====================================================================

; rsi = name bytes, rcx = length -> rax = address, rdx = size, or rax = 0.
; Keeps rsi and rcx.
find_file:
        push rbx
        push r8
        push r9
        mov rbx, [boot_info]
        mov r8d, [rbx + BI_NFILES]
        lea rbx, [rbx + BI_FILES]
        cmp rcx, 16
        ja .none
.file:
        test r8d, r8d
        jz .none
        xor r9d, r9d
.cmp:
        cmp r9, rcx
        je .end
        mov al, [rsi + r9]
        cmp al, [rbx + BIF_NAME + r9]
        jne .next
        inc r9
        jmp .cmp
.end:
        cmp rcx, 16
        je .found
        cmp byte [rbx + BIF_NAME + rcx], 0
        je .found
.next:
        add rbx, BIF_SIZE
        dec r8d
        jmp .file
.found:
        mov rax, [rbx + BIF_ADDR]
        mov rdx, [rbx + BIF_SIZE_FIELD]
        jmp .out
.none:
        xor eax, eax
.out:
        pop r9
        pop r8
        pop rbx
        ret

; rsi = name bytes, rcx = length -> eax: the program's status (127 when
; the image has no such file, 1 when it does not load or fails). The
; program's memory is released when it ends.
exec_file:
        push rbx
        push r12
        call find_file
        test rax, rax
        jz .missing
        mov r12, [heap_ptr]
        mov rsi, rax
        call rt_load
        test rax, rax
        jz .load_failed
        mov rdi, rax
        call vm_run
        jmp .release
.load_failed:
        mov eax, 1
.release:
        mov [heap_ptr], r12
        pop r12
        pop rbx
        ret
.missing:
        PUTS msg_nofile
        call con_write
        PUTS msg_nofile2
        mov eax, 127
        pop r12
        pop rbx
        ret

; rdi = program -> eax: its status. init's integer result 0..255 is the
; status (other integers 1, anything else 0); without init, 0. Errors
; give 1.
vm_run:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov r13, rdi
        mov edi, VM_size
        call alloc
        mov r12, rax
        mov rbx, rax
        mov [r12 + VM.prog], r13
        mov edi, FRAME_size * MAX_FRAMES
        call alloc
        mov [r12 + VM.frames], rax
        mov edi, LSTACK_BYTES
        call alloc
        mov [r12 + VM.ltop], rax
        add rax, LSTACK_BYTES
        mov [r12 + VM.lend], rax
        mov [r12 + VM.rsp], rsp
        mov r14, [r13 + PROG.code]
        xor r15d, r15d
        mov r9, [r13 + PROG.init]
        cmp r9, -1
        je vm_next
        ; init(ret): arg_cnt arg_vec, returning to the final HALT
        mov qword [rbx], T_INT
        mov qword [rbx + 8], 0
        call vec_new
        mov qword [rbx + 16], T_VEC
        mov [rbx + 24], rax
        mov r15, [r13 + PROG.ncode]
        dec r15
        mov qword [r12 + VM.has_init], 1
        xor ecx, ecx
        mov edx, 2
        mov r8d, 31
        call vm_do_call
        jmp vm_next

vm_halt:
        xor eax, eax
        cmp qword [r12 + VM.has_init], 0
        je vm_leave
        cmp qword [rbx + 31 * 16], T_INT
        jne vm_leave
        mov rax, [rbx + 31 * 16 + 8]
        cmp rax, 255
        jbe vm_leave
        mov eax, 1
vm_leave:
        mov rsp, [r12 + VM.rsp]
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; rsi = message: "USSR VM: message (ip N)", status 1
vm_error:
        mov rsp, [r12 + VM.rsp]
        PUTS msg_vm
vm_error_text:                          ; the "USSR VM: " is out already
        mov rsp, [r12 + VM.rsp]
        call con_puts
        PUTS msg_at_ip
        lea rax, [r15 - 1]
        call print_int
        PUTC ')'
        PUTC 10
        mov eax, 1
        jmp vm_leave

vm_fail:
        mov esi, e_failed
        jmp vm_error
vm_bad_reg:
        mov esi, e_reg
        jmp vm_error
vm_bad_const:
        mov esi, e_const_index
        jmp vm_error
vm_bad_string:
        mov esi, e_string_index
        jmp vm_error
vm_bad_jump:
        mov esi, e_jump
        jmp vm_error
vm_bad_types:
        mov esi, e_types
        jmp vm_error

; The loop: r12 = rbx the VM, r13 the program, r14 its code, r15 the
; next instruction. Handlers get a in ecx, b in edx, c in r8d and the
; immediate in r9d.
vm_next:
        cmp r15, [r13 + PROG.ncode]
        jae .bad_ip
        lea rsi, [r14 + r15 * 8]
        inc r15
        movzx eax, byte [rsi]
        movzx ecx, byte [rsi + 1]
        movzx edx, byte [rsi + 2]
        movzx r8d, byte [rsi + 3]
        mov r9d, [rsi + 4]
        jmp [optable + rax * 8]
.bad_ip:
        mov esi, e_ip
        jmp vm_error

; r9 = string index -> eax: its symbol
sym_of:
        cmp r9, [r13 + PROG.nstrings]
        jae vm_bad_string
        mov rax, [r13 + PROG.strsym]
        mov eax, [rax + r9 * 4]
        cmp eax, -1
        je vm_bad_string
        ret

; eax = symbol, edx = place (0 by name, 1 the call's slot r8d,
; 2 global) -> rax: the variable's value cell. Keeps the others.
var_cell:
        push r9
        push r10
        push r11
        mov eax, eax
        cmp edx, 1
        je .slot
        cmp edx, 2
        je .global
        test edx, edx
        jnz .bad
        mov r10, [r12 + VM.curfunc]
        test r10, r10
        jz .global
        mov r11, [r10 + FUNC.locals]
        xor r9d, r9d
.search:
        cmp r9d, [r10 + FUNC.nlocals]
        jae .global
        cmp eax, [r11 + r9 * 4]
        je .local
        inc r9d
        jmp .search
.local:
        mov rax, r9
        shl rax, 4
        add rax, [r12 + VM.curlocals]
        jmp .out
.slot:
        mov r10, [r12 + VM.curfunc]
        test r10, r10
        jz .bad
        cmp r8d, [r10 + FUNC.nlocals]
        jae .bad
        mov eax, r8d
        jmp .local_index
.local_index:
        shl rax, 4
        add rax, [r12 + VM.curlocals]
        jmp .out
.global:
        shl rax, 4
        add rax, [r13 + PROG.globals]
.out:
        pop r11
        pop r10
        pop r9
        ret
.bad:
        mov esi, e_place
        jmp vm_error

; rsi = value -> eax: 1 when it counts as true. Keeps the others.
truthy:
        mov rax, [rsi + VAL_TAG]
        cmp rax, T_STR
        je .counted
        cmp rax, T_VEC
        je .counted
        cmp rax, T_REAL
        je .real
        cmp rax, T_NULL
        je .false
        cmp qword [rsi + VAL_DATA], 0   ; integer, boolean
        jne .true
.false:
        xor eax, eax
        ret
.true:
        mov eax, 1
        ret
.counted:
        mov rax, [rsi + VAL_DATA]
        cmp qword [rax], 0
        jne .true
        jmp .false
.real:
        mov rax, [rsi + VAL_DATA]
        shl rax, 1                      ; -0.0 is false too
        jnz .true
        jmp .false

; rsi, rdi = values -> eax: 1 when equal (numbers by value, strings by
; text, vectors by identity). Keeps the others.
vm_equal:
        push rcx
        push rdx
        mov rax, [rsi + VAL_TAG]
        mov rdx, [rdi + VAL_TAG]
        cmp rax, T_INT
        je .left_number
        cmp rax, T_REAL
        je .left_number
        cmp rax, rdx
        jne .no
        cmp rax, T_NULL
        je .yes
        cmp rax, T_STR
        je .string
        mov rax, [rsi + VAL_DATA]       ; boolean, vector
        cmp rax, [rdi + VAL_DATA]
        je .yes
        jmp .no
.left_number:
        cmp rdx, T_INT
        je .numbers
        cmp rdx, T_REAL
        jne .no
.numbers:
        cmp rax, T_INT
        jne .reals
        cmp rdx, T_INT
        jne .reals
        mov rax, [rsi + VAL_DATA]
        cmp rax, [rdi + VAL_DATA]
        je .yes
        jmp .no
.reals:
        call to_real_left
        call to_real_right
        ucomisd xmm0, xmm1
        jp .no
        je .yes
        jmp .no
.string:
        push rsi
        push rdi
        mov rsi, [rsi + VAL_DATA]
        mov rdi, [rdi + VAL_DATA]
        mov rcx, [rsi + STR_LEN]
        cmp rcx, [rdi + STR_LEN]
        jne .string_no
        add rsi, STR_BYTES
        add rdi, STR_BYTES
        test rcx, rcx
        jz .string_yes
        repe cmpsb
        jne .string_no
.string_yes:
        pop rdi
        pop rsi
.yes:
        mov eax, 1
        pop rdx
        pop rcx
        ret
.string_no:
        pop rdi
        pop rsi
.no:
        xor eax, eax
        pop rdx
        pop rcx
        ret

; xmm0 = the number at rsi; xmm1 = the number at rdi
to_real_left:
        cmp qword [rsi + VAL_TAG], T_INT
        jne .real
        cvtsi2sd xmm0, qword [rsi + VAL_DATA]
        ret
.real:
        movsd xmm0, [rsi + VAL_DATA]
        ret

to_real_right:
        cmp qword [rdi + VAL_TAG], T_INT
        jne .real
        cvtsi2sd xmm1, qword [rdi + VAL_DATA]
        ret
.real:
        movsd xmm1, [rdi + VAL_DATA]
        ret

; --- handlers ---------------------------------------------------------

op_nop:
        jmp vm_next

op_bad:
        PUTS msg_vm
        PUTS e_opcode1
        movzx eax, byte [r14 + r15 * 8 - 8]
        call print_int
        mov esi, e_opcode2
        jmp vm_error_text

op_load_const:
        REGADDR ecx, rcx
        cmp r9, [r13 + PROG.nconsts]
        jae vm_bad_const
        shl r9, 4
        add r9, [r13 + PROG.consts]
        COPYVAL rcx, r9, rax
        jmp vm_next

op_load_var:
        REGADDR ecx, rcx
        call sym_of
        call var_cell
        COPYVAL rcx, rax, rdx
        jmp vm_next

op_store_var:
        REGADDR ecx, rcx
        call sym_of
        call var_cell
        COPYVAL rax, rcx, rdx
        jmp vm_next

op_set:
        REGADDR ecx, rcx
        call sym_of
        xor edx, edx
        call var_cell
        COPYVAL rax, rcx, rdx
        jmp vm_next

op_load_hash:
        REGADDR ecx, rcx
        call sym_of
        mov r10d, eax
        shl rax, 4
        add rax, [r13 + PROG.hash]
        cmp qword [rax], T_UNSET
        je .undefined
        COPYVAL rcx, rax, rdx
        jmp vm_next
.undefined:
        PUTS msg_hash1
        mov eax, r10d
        call print_sym
        PUTS msg_hash2
        jmp vm_fail

op_store_hash:
        REGADDR ecx, rcx
        call sym_of
        shl rax, 4
        add rax, [r13 + PROG.hash]
        COPYVAL rax, rcx, rdx
        jmp vm_next

; eax = symbol: its name on the console
print_sym:
        push rsi
        push rcx
        mov rsi, [r13 + PROG.symtext]
        mov rsi, [rsi + rax * 8]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        call con_write
        pop rcx
        pop rsi
        ret

op_vector_get:                          ; a = b[c]
        REGADDR ecx, rcx
        REGADDR edx, rdx
        REGADDR r8d, r8
        cmp qword [rdx], T_VEC
        jne .bad
        cmp qword [r8], T_INT
        jne .bad
        mov rax, [rdx + VAL_DATA]
        mov r9, [r8 + VAL_DATA]
        cmp r9, [rax + VEC_COUNT]
        jae .range
        shl r9, 4
        add r9, [rax + VEC_ITEMS]
        COPYVAL rcx, r9, rax
        jmp vm_next
.bad:
        mov esi, e_vector_get
        jmp vm_error
.range:
        mov esi, e_vector_range
        jmp vm_error

op_vector_set:                          ; a[b] = c
        REGADDR ecx, rcx
        REGADDR edx, rdx
        REGADDR r8d, r8
        cmp qword [rcx], T_VEC
        jne op_vector_get.range
        cmp qword [rdx], T_INT
        jne op_vector_get.range
        mov rax, [rcx + VAL_DATA]
        mov r9, [rdx + VAL_DATA]
        cmp r9, [rax + VEC_COUNT]
        jae op_vector_get.range
        shl r9, 4
        add r9, [rax + VEC_ITEMS]
        COPYVAL r9, r8, rax
        jmp vm_next

op_neg:
        REGADDR ecx, rcx
        REGADDR edx, rdx
        mov rax, [rdx + VAL_DATA]
        cmp qword [rdx], T_INT
        je .int
        cmp qword [rdx], T_REAL
        jne vm_bad_types
        btc rax, 63
        mov qword [rcx], T_REAL
        mov [rcx + 8], rax
        jmp vm_next
.int:
        neg rax
        mov qword [rcx], T_INT
        mov [rcx + 8], rax
        jmp vm_next

; ADD .. SHR, opcode in eax
op_binary:
        mov r10d, eax
        REGADDR ecx, rcx
        REGADDR edx, rdx
        REGADDR r8d, r8
        cmp r10d, OP_EQ
        je .equal
        cmp r10d, OP_NE
        je .equal
        cmp r10d, OP_LAND
        je .logic
        cmp r10d, OP_LOR
        je .logic
        cmp qword [rdx], T_INT
        jne .not_ints
        cmp qword [r8], T_INT
        jne .not_ints
        mov rax, [rdx + VAL_DATA]
        mov r9, [r8 + VAL_DATA]
        cmp r10d, OP_ADD
        je .iadd
        cmp r10d, OP_SUB
        je .isub
        cmp r10d, OP_MUL
        je .imul
        cmp r10d, OP_DIV
        je .idiv
        cmp r10d, OP_MOD
        je .imod
        cmp r10d, OP_LT
        je .ilt
        cmp r10d, OP_LE
        je .ile
        cmp r10d, OP_GT
        je .igt
        cmp r10d, OP_GE
        je .ige
        cmp r10d, OP_XOR
        je .ixor
        cmp r10d, OP_BAND
        je .iand
        cmp r10d, OP_BOR
        je .ior
        cmp r10d, OP_SHL
        je .ishl
        jmp .ishr
.iadd:
        add rax, r9
        jmp .int_out
.isub:
        sub rax, r9
        jmp .int_out
.imul:
        imul rax, r9
        jmp .int_out
.idiv:
        test r9, r9
        jz .div_zero
        cvtsi2sd xmm0, rax
        cvtsi2sd xmm1, r9
        divsd xmm0, xmm1
        jmp .real_out
.imod:
        test r9, r9
        jz .mod_zero
        cmp r9, -1
        jne .imod_div
        xor eax, eax
        jmp .int_out
.imod_div:
        cqo
        idiv r9
        mov rax, rdx
        jmp .int_out
.ilt:
        cmp rax, r9
        setl al
        jmp .bool_out
.ile:
        cmp rax, r9
        setle al
        jmp .bool_out
.igt:
        cmp rax, r9
        setg al
        jmp .bool_out
.ige:
        cmp rax, r9
        setge al
        jmp .bool_out
.ixor:
        xor rax, r9
        jmp .int_out
.iand:
        and rax, r9
        jmp .int_out
.ior:
        or rax, r9
        jmp .int_out
.ishl:
        cmp r9, 63
        ja .bad_shift
        mov r11, rcx
        mov ecx, r9d
        shl rax, cl
        mov rcx, r11
        jmp .int_out
.ishr:
        cmp r9, 63
        ja .bad_shift
        mov r11, rcx
        mov ecx, r9d
        sar rax, cl
        mov rcx, r11
        jmp .int_out

.not_ints:
        mov rax, [rdx]
        cmp rax, T_INT
        je .left_ok
        cmp rax, T_REAL
        jne vm_bad_types
.left_ok:
        mov rax, [r8]
        cmp rax, T_INT
        je .right_ok
        cmp rax, T_REAL
        jne vm_bad_types
.right_ok:
        mov rsi, rdx
        mov rdi, r8
        call to_real_left
        call to_real_right
        cmp r10d, OP_ADD
        je .radd
        cmp r10d, OP_SUB
        je .rsub
        cmp r10d, OP_MUL
        je .rmul
        cmp r10d, OP_DIV
        je .rdiv
        cmp r10d, OP_LT
        je .rlt
        cmp r10d, OP_LE
        je .rle
        cmp r10d, OP_GT
        je .rgt
        cmp r10d, OP_GE
        je .rge
        jmp vm_bad_types                ; %, bit operations
.radd:
        addsd xmm0, xmm1
        jmp .real_out
.rsub:
        subsd xmm0, xmm1
        jmp .real_out
.rmul:
        mulsd xmm0, xmm1
        jmp .real_out
.rdiv:
        xorpd xmm2, xmm2
        ucomisd xmm1, xmm2
        jp .rdiv_ok
        je .div_zero
.rdiv_ok:
        divsd xmm0, xmm1
        jmp .real_out
.rlt:
        ucomisd xmm1, xmm0
        seta al
        jmp .bool_out
.rle:
        ucomisd xmm1, xmm0
        setae al
        jmp .bool_out
.rgt:
        ucomisd xmm0, xmm1
        seta al
        jmp .bool_out
.rge:
        ucomisd xmm0, xmm1
        setae al
        jmp .bool_out

.equal:
        mov rsi, rdx
        mov rdi, r8
        call vm_equal
        cmp r10d, OP_NE
        jne .bool_out
        xor eax, 1
        jmp .bool_out
.logic:
        mov rsi, rdx
        call truthy
        mov r9d, eax
        mov rsi, r8
        call truthy
        cmp r10d, OP_LAND
        jne .lor
        and eax, r9d
        jmp .bool_out
.lor:
        or eax, r9d
        jmp .bool_out

.int_out:
        mov qword [rcx], T_INT
        mov [rcx + 8], rax
        jmp vm_next
.bool_out:
        movzx eax, al
        mov qword [rcx], T_BOOL
        mov [rcx + 8], rax
        jmp vm_next
.real_out:
        mov qword [rcx], T_REAL
        movq [rcx + 8], xmm0
        jmp vm_next
.div_zero:
        mov esi, e_div_zero
        jmp vm_error
.mod_zero:
        mov esi, e_mod_zero
        jmp vm_error
.bad_shift:
        mov esi, e_shift
        jmp vm_error

op_jmp:
        cmp r9, [r13 + PROG.ncode]
        jae vm_bad_jump
        mov r15, r9
        jmp vm_next

op_jmp_true:
        REGADDR ecx, rcx
        mov rsi, rcx
        call truthy
        test eax, eax
        jz vm_next
        jmp op_jmp

op_jmp_false:
        REGADDR ecx, rcx
        mov rsi, rcx
        call truthy
        test eax, eax
        jnz vm_next
        jmp op_jmp

op_print:
        REGADDR ecx, rcx
        mov rsi, rcx
        xor edx, edx
        call print_value
        PUTC 10
        jmp vm_next

; CONCAT a, c: the strings in R0 .. R(c-1) joined, into Ra
op_concat:
        cmp ecx, 31
        jae vm_bad_reg
        cmp r8d, ecx
        ja vm_bad_reg
        xor r10d, r10d                  ; total length
        xor r11d, r11d
.measure:
        cmp r11d, r8d
        jae .join
        mov rax, r11
        shl rax, 4
        cmp qword [rbx + rax], T_STR
        jne .not_string
        mov rax, [rbx + rax + 8]
        add r10, [rax + STR_LEN]
        inc r11d
        jmp .measure
.join:
        lea rdi, [r10 + 9]
        call alloc
        mov [rax + STR_LEN], r10
        mov r9, rax
        lea rdi, [rax + STR_BYTES]
        xor r11d, r11d
.copy:
        cmp r11d, r8d
        jae .done
        mov rdx, r11
        shl rdx, 4
        mov rdx, [rbx + rdx + 8]
        push rcx
        mov rcx, [rdx + STR_LEN]
        lea rsi, [rdx + STR_BYTES]
        rep movsb
        pop rcx
        inc r11d
        jmp .copy
.done:
        shl ecx, 4
        add rcx, rbx
        mov qword [rcx], T_STR
        mov [rcx + 8], r9
        jmp vm_next
.not_string:
        mov esi, e_concat
        jmp vm_error

; OOP a, b, name: builtin name with arguments R0 .. R(b-1), into Ra
op_oop:
        cmp ecx, 32
        jae vm_bad_reg
        cmp edx, 31
        jae vm_bad_reg
        push rcx
        call sym_of
        mov r10, [r13 + PROG.symbuiltin]
        mov r10d, [r10 + rax * 4]
        test r10d, r10d
        jz .unknown
        dec r10d
        shl r10, 4
        mov esi, edx
        lea rdx, [r12 + VM.tmp]
        mov qword [rdx], T_NULL
        mov qword [rdx + 8], 0
        mov rdi, rbx
        call [bi_table + r10 + 8]
        pop rcx
        test eax, eax
        jnz vm_fail
        shl ecx, 4
        add rcx, rbx
        lea rax, [r12 + VM.tmp]
        COPYVAL rcx, rax, rdx
        jmp vm_next
.unknown:
        mov r10d, eax
        PUTS msg_unknown1
        mov eax, r10d
        call print_sym
        PUTS msg_unknown2
        jmp vm_fail

op_call:
        cmp r9, [r13 + PROG.nfuncs]
        jae .bad
        call vm_do_call
        jmp vm_next
.bad:
        mov esi, e_function
        jmp vm_error

; ecx = a (first argument), edx = b (count), r8d = c (result register),
; r9d = function: a new frame and locals, parameters bound, r15 at the
; entry.
vm_do_call:
        mov r10, r9
        imul r10, r10, FUNC_size
        add r10, [r13 + PROG.funcs]
        cmp edx, [r10 + FUNC.nparams]
        jne .arity
        lea eax, [ecx + edx]
        cmp eax, 31
        ja vm_bad_reg
        cmp r8d, 32
        jae vm_bad_reg
        mov rax, [r12 + VM.nframes]
        cmp rax, MAX_FRAMES
        jae .overflow
        imul rax, rax, FRAME_size
        add rax, [r12 + VM.frames]
        mov r11, rax
        ; the registers move into the frame; the callee starts empty
        push rcx
        mov rsi, rbx
        mov rdi, r11
        mov ecx, 64
        rep movsq
        mov rdi, rbx
        mov ecx, 64
        xor eax, eax
        rep stosq
        pop rcx
        mov [r11 + FRAME.ret_ip], r15
        mov [r11 + FRAME.ret_reg], r8
        mov [r11 + FRAME.func], r10
        mov rax, [r12 + VM.curfunc]
        mov [r11 + FRAME.prev_func], rax
        mov rax, [r12 + VM.curlocals]
        mov [r11 + FRAME.prev_locals], rax
        ; fresh locals
        mov eax, [r10 + FUNC.nlocals]
        shl rax, 4
        mov rdi, [r12 + VM.ltop]
        lea rsi, [rdi + rax]
        cmp rsi, [r12 + VM.lend]
        ja .overflow
        mov [r12 + VM.ltop], rsi
        mov [r12 + VM.curlocals], rdi
        push rcx
        mov rcx, rax
        shr rcx, 3
        xor eax, eax
        rep stosq
        pop rcx
        mov [r12 + VM.curfunc], r10
        ; parameters from the saved registers a .. a + b - 1
        xor esi, esi
.param:
        cmp esi, edx
        jae .bound
        mov rax, [r10 + FUNC.params]
        mov eax, [rax + rsi * 4]
        push rdx
        xor edx, edx
        call var_cell
        lea edi, [ecx + esi]
        shl rdi, 4
        add rdi, r11
        COPYVAL rax, rdi, r9
        pop rdx
        inc esi
        jmp .param
.bound:
        inc qword [r12 + VM.nframes]
        mov eax, [r10 + FUNC.entry]
        mov r15, rax
        ret
.arity:
        mov esi, e_arity
        jmp vm_error
.overflow:
        mov esi, e_overflow
        jmp vm_error

op_ret:
        mov rax, [r12 + VM.nframes]
        test rax, rax
        jz .no_frame
        dec rax
        imul rax, rax, FRAME_size
        add rax, [r12 + VM.frames]
        mov r11, rax
        mov r10, [r11 + FRAME.func]
        ; the result: the function's return variable
        xor r8d, r8d
        xor r9d, r9d
        mov eax, [r10 + FUNC.ret]
        cmp eax, -1
        je .result
        xor edx, edx
        call var_cell
        mov r8, [rax]
        mov r9, [rax + 8]
.result:
        mov rax, [r12 + VM.curlocals]
        mov [r12 + VM.ltop], rax
        mov rsi, r11
        mov rdi, rbx
        mov ecx, 64
        rep movsq
        mov rax, [r11 + FRAME.prev_func]
        mov [r12 + VM.curfunc], rax
        mov rax, [r11 + FRAME.prev_locals]
        mov [r12 + VM.curlocals], rax
        dec qword [r12 + VM.nframes]
        mov rax, [r11 + FRAME.ret_reg]
        shl rax, 4
        mov [rbx + rax], r8
        mov [rbx + rax + 8], r9
        mov r15, [r11 + FRAME.ret_ip]
        jmp vm_next
.no_frame:
        mov esi, e_no_frame
        jmp vm_error

op_time:
        REGADDR ecx, rcx
        call rtc_epoch
        mov qword [rcx], T_INT
        mov [rcx + 8], rax
        jmp vm_next

op_random64:
        REGADDR ecx, rcx
        call random64
        mov qword [rcx], T_INT
        mov [rcx + 8], rax
        jmp vm_next

op_seed64:
        REGADDR ecx, rcx
        cmp qword [rbx], T_INT
        jne .bad
        mov rax, [rbx + 8]
        mov [rng_state], rax
        COPYVAL rcx, rbx, rax
        jmp vm_next
.bad:
        mov esi, e_seed
        jmp vm_error

; -> rax: xorshift64*, seeded from the time stamp counter
random64:
        push rdx
        mov rax, [rng_state]
        test rax, rax
        jnz .have
        rdtsc
        shl rdx, 32
        or rax, rdx
        or rax, 1
.have:
        mov rdx, rax
        shr rdx, 12
        xor rax, rdx
        mov rdx, rax
        shl rdx, 25
        xor rax, rdx
        mov rdx, rax
        shr rdx, 27
        xor rax, rdx
        mov [rng_state], rax
        mov rdx, 0x2545f4914f6cdd1d
        imul rax, rdx
        pop rdx
        ret

; al = CMOS register -> al: its value (binary)
cmos_read:
        out 0x70, al
        in al, 0x71
        ret

; -> rax: seconds since 1970 from the RTC (taken as UTC)
rtc_epoch:
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
.busy:
        mov al, 0x0a
        call cmos_read
        test al, 0x80
        jnz .busy
        mov al, 0x0b
        call cmos_read
        movzx esi, al                   ; status B
        mov al, 0
        call cmos_read
        movzx r8d, al                   ; seconds
        mov al, 2
        call cmos_read
        movzx r9d, al                   ; minutes
        mov al, 4
        call cmos_read
        movzx ebx, al                   ; hours
        mov al, 7
        call cmos_read
        movzx ecx, al                   ; day
        mov al, 8
        call cmos_read
        movzx edi, al                   ; month
        mov al, 9
        call cmos_read
        movzx edx, al                   ; year
        test esi, 4
        jnz .binary
        mov eax, r8d
        call bcd
        mov r8d, eax
        mov eax, r9d
        call bcd
        mov r9d, eax
        mov eax, ebx
        and eax, 0x7f
        call bcd
        and ebx, 0x80
        or ebx, eax
        mov eax, ecx
        call bcd
        mov ecx, eax
        mov eax, edi
        call bcd
        mov edi, eax
        mov eax, edx
        call bcd
        mov edx, eax
.binary:
        test esi, 2
        jnz .h24
        test ebx, 0x80
        jz .h24
        and ebx, 0x7f
        add ebx, 12
        cmp ebx, 24
        jb .h24
        sub ebx, 24
.h24:
        and ebx, 0x7f
        add edx, 2000
        ; days from 1970-01-01 (Howard Hinnant's days_from_civil)
        cmp edi, 2
        ja .no_shift
        dec edx
.no_shift:
        mov eax, edx
        push rdx
        xor edx, edx
        mov esi, 400
        div esi                         ; era (years are after 0)
        pop rdx
        mov esi, eax                    ; era
        imul eax, eax, 400
        sub edx, eax                    ; yoe
        mov eax, edi
        cmp eax, 2
        ja .m3
        add eax, 9
        jmp .mdone
.m3:
        sub eax, 3
.mdone:
        imul eax, eax, 153
        add eax, 2
        push rdx
        xor edx, edx
        push rcx
        mov ecx, 5
        div ecx
        pop rcx
        pop rdx
        add eax, ecx
        dec eax                         ; doy
        mov edi, eax
        imul eax, edx, 365
        add edi, eax
        mov eax, edx
        shr eax, 2
        add edi, eax
        mov eax, edx
        push rdx
        xor edx, edx
        push rcx
        mov ecx, 100
        div ecx
        pop rcx
        pop rdx
        sub edi, eax                    ; doe
        imul rax, rsi, 146097
        add rax, rdi
        sub rax, 719468                 ; days since the epoch
        imul rax, rax, 86400
        imul rbx, rbx, 3600
        add rax, rbx
        imul r9, r9, 60
        add rax, r9
        add rax, r8
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

; eax: BCD -> binary
bcd:
        push rdx
        mov edx, eax
        shr edx, 4
        imul edx, edx, 10
        and eax, 15
        add eax, edx
        pop rdx
        ret

; =====================================================================
; builtins: rdi = arguments (values), esi = count, rdx = result.
; eax = 0, or nonzero after a message. They keep rbx, rbp, r12..r15.
; =====================================================================

; rax = message: "USSR: message", failure
bi_error:
        mov rsi, rax
        PUTS msg_ussr
        call con_puts
        PUTC 10
        mov eax, -1
        ret

bi_ok:
        xor eax, eax
        ret

; out: integer rax
bi_int_out:
        mov qword [rdx], T_INT
        mov [rdx + 8], rax
        xor eax, eax
        ret

; out: string rax
bi_str_out:
        mov qword [rdx], T_STR
        mov [rdx + 8], rax
        xor eax, eax
        ret

; out: vector rax
bi_vec_out:
        mov qword [rdx], T_VEC
        mov [rdx + 8], rax
        xor eax, eax
        ret

bi_len:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        je .counted
        cmp qword [rdi], T_VEC
        jne .bad
.counted:
        mov rax, [rdi + 8]
        mov rax, [rax]                  ; STR_LEN and VEC_COUNT
        jmp bi_int_out
.bad:
        mov eax, e_len
        jmp bi_error

bi_byte:
        cmp esi, 2
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        cmp qword [rdi + 16], T_INT
        jne .bad
        mov rax, [rdi + 8]
        mov rcx, [rdi + 24]
        cmp rcx, [rax + STR_LEN]
        jae .range
        movzx eax, byte [rax + STR_BYTES + rcx]
        jmp bi_int_out
.bad:
        mov eax, e_byte
        jmp bi_error
.range:
        mov eax, e_byte_range
        jmp bi_error

bi_slice:
        cmp esi, 3
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        cmp qword [rdi + 16], T_INT
        jne .bad
        cmp qword [rdi + 32], T_INT
        jne .bad
        mov rax, [rdi + 8]
        mov rsi, [rdi + 24]             ; from
        mov rcx, [rdi + 40]             ; count
        mov r8, [rax + STR_LEN]
        cmp rsi, r8
        ja .range
        sub r8, rsi
        cmp rcx, r8
        ja .range
        lea rsi, [rax + STR_BYTES + rsi]
        call new_string
        jmp bi_str_out
.bad:
        mov eax, e_slice
        jmp bi_error
.range:
        mov eax, e_slice_range
        jmp bi_error

bi_chr:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_INT
        jne .bad
        mov rax, [rdi + 8]
        test rax, rax
        jz .bad
        cmp rax, 255
        ja .bad
        mov [chr_buf], al
        mov esi, chr_buf
        mov ecx, 1
        call new_string
        jmp bi_str_out
.bad:
        mov eax, e_chr
        jmp bi_error

bi_vec:
        call vec_new
        jmp bi_vec_out

bi_push:
        cmp esi, 2
        jne .bad
        cmp qword [rdi], T_VEC
        jne .bad
        lea rsi, [rdi + 16]
        mov rdi, [rdi + 8]
        call vec_push
        xor eax, eax
        ret
.bad:
        mov eax, e_push
        jmp bi_error

bi_pop:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_VEC
        jne .bad
        mov rax, [rdi + 8]
        mov rcx, [rax + VEC_COUNT]
        test rcx, rcx
        jz .empty
        dec rcx
        mov [rax + VEC_COUNT], rcx
        shl rcx, 4
        add rcx, [rax + VEC_ITEMS]
        COPYVAL rdx, rcx, rax
        xor eax, eax
        ret
.bad:
        mov eax, e_pop
        jmp bi_error
.empty:
        mov eax, e_pop_empty
        jmp bi_error

bi_at:
        cmp esi, 2
        jne .bad
        cmp qword [rdi + 16], T_INT
        jne .bad
        mov rcx, [rdi + 24]
        mov rax, [rdi + 8]
        cmp qword [rdi], T_VEC
        je .vector
        cmp qword [rdi], T_STR
        jne .bad
        cmp rcx, [rax + STR_LEN]
        jae .range
        lea rsi, [rax + STR_BYTES + rcx]
        mov ecx, 1
        call new_string
        jmp bi_str_out
.vector:
        cmp rcx, [rax + VEC_COUNT]
        jae .range
        shl rcx, 4
        add rcx, [rax + VEC_ITEMS]
        COPYVAL rdx, rcx, rax
        xor eax, eax
        ret
.bad:
        mov eax, e_at
        jmp bi_error
.range:
        mov eax, e_at_range
        jmp bi_error

bi_typeof:
        cmp esi, 1
        jne .bad
        mov rax, [rdi]
        cmp rax, T_VEC
        ja .bad
        mov rsi, [type_names + rax * 8]
        call new_string_z
        jmp bi_str_out
.bad:
        mov eax, e_typeof
        jmp bi_error

bi_k_getline:
        push rdx
        call read_line
        pop rdx
        jmp bi_str_out

bi_k_prompt:
        cmp esi, 1
        jne .bad
        mov rsi, rdi
        push rdx
        xor edx, edx
        call print_value
        pop rdx
        xor eax, eax
        ret
.bad:
        mov eax, e_prompt
        jmp bi_error

bi_k_clear:
        call vga_clear
        xor eax, eax
        ret

bi_k_color:
        cmp esi, 2
        jne .bad
        cmp qword [rdi], T_INT
        jne .bad
        cmp qword [rdi + 16], T_INT
        jne .bad
        mov rax, [rdi + 24]
        and eax, 15
        shl eax, 4
        mov rcx, [rdi + 8]
        and ecx, 15
        or eax, ecx
        mov [vga_attr], rax
        xor eax, eax
        ret
.bad:
        mov eax, e_color
        jmp bi_error

bi_k_ticks:
        mov rax, [ticks]
        jmp bi_int_out

bi_k_mem:
        push rdx
        call vec_new
        mov rdi, rax
        mov rax, [heap_end]
        sub rax, HEAP_BASE
        call vec_push_int
        mov rax, [heap_end]
        sub rax, [heap_ptr]
        call vec_push_int
        mov rax, rdi
        pop rdx
        jmp bi_vec_out

; "vendor, brand" from CPUID
bi_k_cpu:
        push rbx
        push rdx
        push rdi
        mov edi, cpu_buf
        xor eax, eax
        cpuid
        mov [rdi], ebx
        mov [rdi + 4], edx
        mov [rdi + 8], ecx
        add rdi, 12
        mov eax, 0x80000000
        cpuid
        cmp eax, 0x80000004
        jb .done
        mov byte [rdi], ','
        mov byte [rdi + 1], ' '
        add rdi, 2
        mov r8, rdi                     ; the brand starts here
        mov esi, 0x80000002
.brand:
        mov eax, esi
        cpuid
        mov [rdi], eax
        mov [rdi + 4], ebx
        mov [rdi + 8], ecx
        mov [rdi + 12], edx
        add rdi, 16
        inc esi
        cmp esi, 0x80000004
        jbe .brand
        ; drop the brand's leading spaces and its end padding
        mov rsi, r8
.lead:
        cmp byte [rsi], ' '
        jne .lead_done
        inc rsi
        jmp .lead
.lead_done:
        mov rdi, r8
.copy:
        mov al, [rsi]
        mov [rdi], al
        test al, al
        jz .trail
        inc rsi
        inc rdi
        cmp rsi, cpu_buf + 14 + 48
        jb .copy
.trail:
        cmp rdi, r8
        je .done
        cmp byte [rdi - 1], ' '
        jne .done
        dec rdi
        jmp .trail
.done:
        mov rcx, rdi
        sub rcx, cpu_buf
        mov esi, cpu_buf
        call new_string
        pop rdi
        pop rdx
        pop rbx
        jmp bi_str_out

bi_k_modules:
        push rbx
        push rdx
        push r8
        call vec_new
        mov rdi, rax
        mov rbx, [boot_info]
        mov r8d, [rbx + BI_NFILES]
        lea rbx, [rbx + BI_FILES]
.file:
        test r8d, r8d
        jz .done
        lea rsi, [rbx + BIF_NAME]
        xor ecx, ecx
.len:
        cmp ecx, 16
        je .named
        cmp byte [rsi + rcx], 0
        je .named
        inc ecx
        jmp .len
.named:
        call new_string
        call vec_push_str
        add rbx, BIF_SIZE
        dec r8d
        jmp .file
.done:
        mov rax, rdi
        pop r8
        pop rdx
        pop rbx
        jmp bi_vec_out

bi_k_exec:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        push rdx
        mov rax, [rdi + 8]
        lea rsi, [rax + STR_BYTES]
        mov rcx, [rax + STR_LEN]
        call exec_file
        pop rdx
        movsxd rax, eax
        jmp bi_int_out
.bad:
        mov eax, e_exec
        jmp bi_error

bi_k_reboot:
        jmp sys_reboot

bi_k_halt:
        jmp sys_halt

; =====================================================================
; tables and texts
; =====================================================================

        align 8
optable:
%assign i 0
%rep 256
%if i == 0x00
        dq op_nop
%elif i == 0x01
        dq op_load_const
%elif i == 0x02
        dq op_load_var
%elif i == 0x03
        dq op_store_var
%elif i == 0x04
        dq op_load_hash
%elif i == 0x05
        dq op_store_hash
%elif i == 0x08
        dq op_vector_get
%elif i == 0x09
        dq op_vector_set
%elif i >= 0x10 && i <= 0x14
        dq op_binary
%elif i == 0x15
        dq op_neg
%elif i >= 0x16 && i <= 0x22
        dq op_binary
%elif i == 0x30
        dq op_jmp
%elif i == 0x31
        dq op_jmp_true
%elif i == 0x32
        dq op_jmp_false
%elif i == 0x40
        dq op_set
%elif i == 0x41
        dq op_print
%elif i == 0x42
        dq op_concat
%elif i == 0x44
        dq op_oop
%elif i == 0x46
        dq op_random64
%elif i == 0x47
        dq op_seed64
%elif i == 0x49
        dq op_time
%elif i == 0x50
        dq op_call
%elif i == 0x51 || i == 0x52
        dq op_ret
%elif i == 0xf0
        dq vm_halt
%else
        dq op_bad
%endif
%assign i i + 1
%endrep

exc_table:
%assign v 0
%rep 32
        dq exc_stub_%[v]
%assign v v + 1
%endrep

; name, handler
bi_table:
        dq n_len, bi_len
        dq n_byte, bi_byte
        dq n_slice, bi_slice
        dq n_chr, bi_chr
        dq n_vec, bi_vec
        dq n_push, bi_push
        dq n_pop, bi_pop
        dq n_at, bi_at
        dq n_typeof, bi_typeof
        dq n_k_getline, bi_k_getline
        dq n_k_prompt, bi_k_prompt
        dq n_k_clear, bi_k_clear
        dq n_k_color, bi_k_color
        dq n_k_ticks, bi_k_ticks
        dq n_k_mem, bi_k_mem
        dq n_k_cpu, bi_k_cpu
        dq n_k_modules, bi_k_modules
        dq n_k_exec, bi_k_exec
        dq n_k_reboot, bi_k_reboot
        dq n_k_halt, bi_k_halt
        dq 0, 0

type_names:
        dq s_null, s_integer, s_real, s_string, s_boolean, s_vector

idtr:
        dw 256 * 16 - 1
        dq idt
null_idtr:
        dw 0
        dq 0

        align 8
f_one:          dq 1.0
f_ten:          dq 10.0

; scan codes (set 1) 0x00 .. 0x39 to ASCII
keymap:
        db 0, 27, "1234567890-=", 8, 9
        db "qwertyuiop[]", 10, 0
        db "asdfghjkl;'`", 0, "\zxcvbnm,./", 0, "*", 0, " "
KEYMAP_SIZE equ $ - keymap
keymap_shift:
        db 0, 27, "!@#$%^&*()_+", 8, 9
        db "QWERTYUIOP{}", 10, 0
        db 'ASDFGHJKL:"~', 0, "|ZXCVBNM<>?", 0, "*", 0, " "

hex_digits:     db "0123456789abcdef"

n_len:          db "len", 0
n_byte:         db "byte", 0
n_slice:        db "slice", 0
n_chr:          db "chr", 0
n_vec:          db "vec", 0
n_push:         db "push", 0
n_pop:          db "pop", 0
n_at:           db "at", 0
n_typeof:       db "typeof", 0
n_k_getline:    db "k_getline", 0
n_k_prompt:     db "k_prompt", 0
n_k_clear:      db "k_clear", 0
n_k_color:      db "k_color", 0
n_k_ticks:      db "k_ticks", 0
n_k_mem:        db "k_mem", 0
n_k_cpu:        db "k_cpu", 0
n_k_modules:    db "k_modules", 0
n_k_exec:       db "k_exec", 0
n_k_reboot:     db "k_reboot", 0
n_k_halt:       db "k_halt", 0

name_moscow:    db "moscow.ru"
name_moscow_len equ $ - name_moscow
s_init:         db "init", 0
s_null:         db "null", 0
s_integer:      db "integer", 0
s_real:         db "real", 0
s_string:       db "string", 0
s_boolean:      db "boolean", 0
s_vector:       db "vector", 0
s_true:         db "true", 0
s_false:        db "false", 0
s_inf:          db "inf", 0
s_nan:          db "nan", 0

msg_start:      db "USSR runtime: NASM VM for amd64, running moscow.ru", 10, 0
msg_ended:      db "USSR runtime: moscow.ru ended with status ", 0
msg_halted:     db "; halted", 10, 0
msg_oom:        db 10, "USSR runtime: out of memory; halted", 10, 0
msg_exc:        db 10, "USSR runtime: CPU exception ", 0
msg_exc_err:    db ", error ", 0
msg_exc_rip:    db ", rip ", 0
msg_nofile:     db "USSR runtime: no file '", 0
msg_nofile2:    db "' in the boot image", 10, 0
msg_ussr:       db "USSR: ", 0
msg_vm:         db "USSR VM: ", 0
msg_at_ip:      db " (ip ", 0
msg_hash1:      db "USSR: undefined hash key '", 0
msg_hash2:      db "'", 10, 0
msg_unknown1:   db "USSR: unknown command '", 0
msg_unknown2:   db "' (not in the Moscow runtime)", 10, 0

e_signature:    db "not a .ru file (bad signature)", 0
e_version:      db ".ru format or VM ABI is not 3", 0
e_length:       db ".ru payload length does not match the file", 0
e_crc:          db ".ru checksum mismatch", 0
e_code:         db ".ru code count out of range", 0
e_truncated:    db ".ru file is truncated", 0
e_no_halt:      db ".ru program does not end with HALT", 0
e_const:        db ".ru constant is invalid", 0
e_func:         db ".ru function table is invalid", 0
e_init:         db "init must have parameters arg_cnt and arg_vec", 0
e_symbols:      db "too many names in the program", 0
e_failed:       db "execution failed", 0
e_reg:          db "invalid register", 0
e_const_index:  db "invalid constant", 0
e_string_index: db "invalid string or name", 0
e_jump:         db "invalid jump target", 0
e_types:        db "invalid operand types", 0
e_ip:           db "instruction pointer outside the code", 0
e_place:        db "invalid variable place", 0
e_opcode1:      db "opcode ", 0
e_opcode2:      db " is not supported by the Moscow runtime", 0
e_vector_get:   db "vector get requires vector and integer index", 0
e_vector_range: db "vector index out of range or incompatible value", 0
e_div_zero:     db "division by zero", 0
e_mod_zero:     db "modulo by zero", 0
e_shift:        db "shift count is outside 0..63", 0
e_concat:       db "concat expects strings", 0
e_function:     db "invalid function index", 0
e_arity:        db "invalid function argument count", 0
e_overflow:     db "call stack overflow", 0
e_no_frame:     db "return without a call frame", 0
e_seed:         db "seed_random64 requires an integer seed", 0
e_len:          db "len() expects a string or vector", 0
e_byte:         db "byte() expects a string and an index", 0
e_byte_range:   db "byte() index out of range", 0
e_slice:        db "slice() expects a string, a start and a count", 0
e_slice_range:  db "slice() range out of bounds", 0
e_chr:          db "chr() expects an integer byte 1..255", 0
e_push:         db "push() expects a vector and a value", 0
e_pop:          db "pop() expects a vector", 0
e_pop_empty:    db "pop() from an empty vector", 0
e_at:           db "at() expects a vector or string and an index", 0
e_at_range:     db "at() index out of range", 0
e_typeof:       db "typeof() expects one value", 0
e_prompt:       db "k_prompt() expects one value", 0
e_color:        db "k_color() expects two integers, 0..15", 0
e_exec:         db "k_exec() expects a file name", 0

rt_end:

; =====================================================================
; variables (zeroed at start), below the stack
; =====================================================================

absolute RT_DATA
boot_info:      resq 1
heap_ptr:       resq 1
heap_end:       resq 1
ticks:          resq 1
vga_row:        resq 1
vga_col:        resq 1
vga_attr:       resq 1
kbd_shift:      resq 1
last_cr:        resq 1
rng_state:      resq 1
ld_rsp:         resq 1
chr_buf:        resq 1
line_buf:       resb LINE_MAX + 8
cpu_buf:        resb 128
                alignb 16
idt:            resb 256 * 16
rt_data_end:
