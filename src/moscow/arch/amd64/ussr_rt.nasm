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
T_STRUCT        equ 6
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
HCAP            equ 131072           ; intern hash slots (power of two)
SYM_MAX         equ 65536           ; distinct names in a program
LINE_MAX        equ 1000
CAPTURE_BUF     equ 0x30000         ; encode's text, eval's strings
CAPTURE_MAX     equ 0x20000
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
.siteret:       resq 1              ; u32 per instruction: an OOP's return
                                    ; name symbol, or -1
endstruc

; A struct type (types live as long as the program that declared them).
struc STYPE
.next:          resq 1
.name:          resq 1              ; string
.nfields:       resq 1
.fields:        resq 1              ; strings, "name" only
endstruc
; A struct value: its type, then a value per field.
SOBJ_TYPE       equ 0
SOBJ_FIELDS     equ 16

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
        call gdt_init
        call idt_init
        call heap_init
        call env_init
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


; The environment a session starts with.
env_init:
        mov rsi, env_ps1
        mov rdi, val_ps1
        call env_set_z
        mov rsi, env_user
        mov rdi, val_user
        call env_set_z
        mov rsi, env_host
        mov rdi, val_host
        call env_set_z
        mov rsi, env_home
        mov rdi, s_root
        call env_set_z
        mov rsi, env_os
        mov rdi, val_os
        call env_set_z
        ret

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

; The runtime's own GDT (the boot loader's is in memory it no longer
; owns): a null descriptor, 64-bit code at 0x08, data at 0x10.
gdt_init:
        lgdt [rt_gdtr]
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        xor eax, eax
        mov fs, ax
        mov gs, ax
        pop rax                         ; reload CS with a far return
        push qword 0x08
        push rax
        retfq

; The heap: the largest usable RAM region of the BIOS map above the
; loaded files, from its first 2 MiB boundary after them. When RAM goes
; past the boot loader's 4 GiB identity map, the map is first extended
; with 2 MiB pages (up to 64 GiB); those tables sit right after the
; files, in memory that is mapped already, and the heap starts after
; them.
heap_init:
        mov rsi, [boot_info]
        mov r8, [rsi + BI_FILES_END]
        ; the top of usable RAM
        xor r10d, r10d
        mov ecx, [rsi + BI_NE820]
        lea rdi, [rsi + BI_E820]
.top:
        test ecx, ecx
        jz .have_top
        cmp dword [rdi + 16], 1
        jne .top_next
        mov rax, [rdi]
        add rax, [rdi + 8]
        cmp rax, r10
        jbe .top_next
        mov r10, rax
.top_next:
        add rdi, 24
        dec ecx
        jmp .top
.have_top:
        mov rax, 64 << 30
        cmp r10, rax
        jbe .top_ok
        mov r10, rax
.top_ok:
        mov [ram_top], r10
        mov rax, MAP_LIMIT
        cmp r10, rax
        jbe .mapped
        ; a PDPT and one PD per GiB after the files: identity, 2 MiB pages
        add r8, 0xfff
        and r8, -0x1000
        mov rcx, r10
        add rcx, (1 << 30) - 1
        shr rcx, 30                     ; GiBs
        mov rdi, r8
        lea rdx, [rcx + 1]
        shl rdx, 9                      ; qwords of tables
        push rcx
        mov rcx, rdx
        xor eax, eax
        rep stosq
        pop rcx
        mov rdi, r8                     ; the PDPT
        lea rdx, [r8 + 4096]            ; the first PD
        xor r11d, r11d                  ; GiB
.gib:
        cmp r11, rcx
        jae .tables_done
        lea rax, [rdx + 3]
        mov [rdi + r11 * 8], rax
        mov rax, r11
        shl rax, 30
        or rax, 0x83
        xor r9d, r9d
.page:
        mov [rdx + r9 * 8], rax
        add rax, 0x200000
        inc r9d
        cmp r9d, 512
        jb .page
        add rdx, 4096
        inc r11
        jmp .gib
.tables_done:
        mov r8, rdx                     ; the heap goes after the tables
        or rdi, 3
        mov [PAGE_PML4], rdi
        mov rax, cr3
        mov cr3, rax
.mapped:
        add r8, 0x1fffff
        and r8, -0x200000               ; nothing below this
        xor r9d, r9d                    ; best start
        xor r10d, r10d                  ; best end
        mov rsi, [boot_info]
        mov ecx, [rsi + BI_NE820]
        lea rsi, [rsi + BI_E820]
.next:
        test ecx, ecx
        jz .chosen
        cmp dword [rsi + 16], 1         ; usable RAM
        jne .skip
        mov rax, [rsi]
        mov rdx, rax
        add rdx, [rsi + 8]
        cmp rdx, [ram_top]
        jbe .end_ok
        mov rdx, [ram_top]
.end_ok:
        add rax, 0x1fffff
        and rax, -0x200000
        cmp rax, r8
        jae .start_ok
        mov rax, r8
.start_ok:
        cmp rdx, rax
        jbe .skip
        mov r11, rdx
        sub r11, rax
        mov rdi, r10
        sub rdi, r9
        cmp r11, rdi
        jbe .skip
        mov r9, rax
        mov r10, rdx
.skip:
        add rsi, 24
        dec ecx
        jmp .next
.chosen:
        test r10, r10
        jnz .have
        mov r9, r8                      ; no map: assume 16 MiB
        lea r10, [r8 + 0x1000000]
.have:
        mov [heap_base], r9
        mov [heap_ptr], r9
        mov [heap_end], r10
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
; While capture_on is set the byte goes to the capture buffer instead
; (encode builds UNO text with the printing code).
con_putc:
        push rax
        cmp qword [capture_on], 0
        jne .capture
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
.capture:
        mov rax, [capture_len]
        cmp rax, CAPTURE_MAX
        jae .full
        mov [CAPTURE_BUF + rax], dil
        inc qword [capture_len]
.full:
        pop rax
        ret

; Starts capturing console output.
capture_begin:
        mov qword [capture_len], 0
        mov qword [capture_on], 1
        ret

; Stops capturing -> rax: what was captured, as a string.
capture_end:
        push rsi
        push rcx
        mov qword [capture_on], 0
        mov esi, CAPTURE_BUF
        mov rcx, [capture_len]
        call new_string
        pop rcx
        pop rsi
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
        mov qword [read_line_eof], 0
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
        cmp eax, 4
        je .ctrl_d
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
.ctrl_d:
        test ebx, ebx
        jnz .wait
        mov qword [read_line_eof], 1
        jmp .end
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
        mov qword [real_plain], 1       ; no '.', 'e' or letters yet
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
        mov qword [real_plain], 0
        PUTS s_inf
        jmp .out
.nan:
        mov qword [real_plain], 0
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
        mov qword [real_plain], 0
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
        mov qword [real_plain], 0
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
        mov qword [real_plain], 0
        PUTC '.'
        mov ecx, 1
.exp_digits:
        movzx edi, byte [rsp + rcx]
        call con_putc
        inc rcx
        cmp rcx, r9
        jl .exp_digits
.e:
        mov qword [real_plain], 0
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
        cmp rax, T_STRUCT
        je .struct
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
        test dl, dl                     ; UNO: 3.0, not 3
        jz .done
        cmp qword [real_plain], 0
        je .done
        PUTC '.'
        PUTC '0'
        jmp .done
.struct:                                ; Type{field=value,...}
        mov r8, [rsi + VAL_DATA]
        mov rdi, [r8 + SOBJ_TYPE]
        push rsi
        mov rsi, [rdi + STYPE.name]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        call con_write
        pop rsi
        PUTC '{'
        xor ecx, ecx
.field:
        cmp rcx, [rdi + STYPE.nfields]
        jae .struct_end
        test rcx, rcx
        jz .field_name
        PUTC ','
.field_name:
        push rcx
        mov rsi, [rdi + STYPE.fields]
        mov rsi, [rsi + rcx * 8]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        call con_write
        pop rcx
        PUTC '='
        mov rsi, rcx
        shl rsi, 4
        lea rsi, [r8 + SOBJ_FIELDS + rsi]
        mov dl, 1
        call print_value
        inc rcx
        jmp .field
.struct_end:
        PUTC '}'
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
        ; 5. OOP call sites: the return name of each OOP instruction, for
        ; builtins that need it (struct takes the type name from it)
        mov rdi, [r14 + PROG.ncode]
        lea rdi, [rdi * 4 + 4]
        call alloc
        mov [r14 + PROG.siteret], rax
        mov rcx, [r14 + PROG.ncode]
.no_site:
        mov dword [rax + rcx * 4 - 4], -1
        loop .no_site
        call rd_u32
        mov r15, rax
.site:
        test r15, r15
        jz .sites_done
        call rd_u32                     ; instruction
        cmp rax, [r14 + PROG.ncode]
        jae .bad_func
        mov rbp, rax
        call rd_string                  ; command name
        call rd_string                  ; return name
        cmp rcx, -1
        je .site_args
        call intern
        mov rdx, [r14 + PROG.siteret]
        mov [rdx + rbp * 4], eax
.site_args:
        call rd_u8
        mov ebp, eax
.site_arg:
        test ebp, ebp
        jz .site_next
        call rd_u8                      ; kind
        push rax
        call rd_u8                      ; assignment
        pop rax
        cmp eax, 1
        jne .site_arg_block
        call rd_string
        jmp .site_arg_next
.site_arg_block:
        cmp eax, 2
        jb .site_arg_next
        call rd_skip_block
.site_arg_next:
        dec ebp
        jmp .site_arg
.site_next:
        dec r15
        jmp .site
.sites_done:
        ; scan sites follow; the runtime has no SCAN

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
; Skips a block of a call site (docs/RU_BYTECODE_FORMAT.md, sections 5
; and 6): commands with their arguments, nested blocks and expressions.
rd_skip_block:
        push rbx
        push rbp
        call rd_u32
        mov ebx, eax                    ; commands
.command:
        test ebx, ebx
        jz .done
        call rd_string                  ; path
        call rd_string                  ; name
        call rd_string                  ; return name
        call rd_u8
        mov ebp, eax                    ; arguments
.argument:
        test ebp, ebp
        jz .command_next
        call rd_u8                      ; node kind
        push rax
        call rd_u8                      ; assignment
        pop rax
        cmp eax, 0
        je .value
        cmp eax, 3
        je .uno
        cmp eax, 3
        ja .block
        call rd_skip_expr               ; 1 expression, 2 name?
        jmp .argument_next
.value:
        call rd_skip_value
        jmp .argument_next
.uno:
        call rd_string
        jmp .argument_next
.block:
        call rd_skip_block
.argument_next:
        dec ebp
        jmp .argument
.command_next:
        dec ebx
        jmp .command
.done:
        pop rbp
        pop rbx
        ret

rd_skip_value:
        call rd_u8
        cmp eax, 1
        je .eight
        cmp eax, 2
        je .eight
        cmp eax, 3
        je .string
        cmp eax, 4
        je .byte
        ret
.eight:
        jmp rd_u64
.string:
        jmp rd_string
.byte:
        jmp rd_u8

rd_skip_expr:
        call rd_u8
        cmp eax, 0
        je rd_skip_value
        cmp eax, 1
        je rd_string
        cmp eax, 2
        je .binary
        call rd_u8                      ; unary: operator, operand
        jmp rd_skip_expr
.binary:
        call rd_u8
        call rd_skip_expr
        jmp rd_skip_expr

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
        push qword [struct_types]
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
        pop qword [struct_types]
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
        mov rsi, [rdi + PROG.init]
        cmp rsi, -1
        je .top
        ; init(ret): arg_cnt arg_vec
        sub rsp, 32
        mov qword [rsp], T_INT
        mov qword [rsp + 8], 0
        call vec_new
        mov qword [rsp + 16], T_VEC
        mov [rsp + 24], rax
        mov rdx, rsp
        mov ecx, 2
        mov r8d, 1
        call vm_exec
        add rsp, 32
        ret
.top:
        xor ecx, ecx
        xor r8d, r8d
        jmp vm_exec

; rdi = program, rsi = function (-1: the top-level code), rdx = argument
; values, ecx = their count, r8d = mode (1: init, its result is the
; status; 2: try, failures are quiet) -> eax: status, 0 or 1 (init:
; its result). A VM of its own: registers, frames and locals; the
; program's globals and hash entries are shared.
vm_exec:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov r13, rdi
        mov r10, rsi
        mov r11, rdx
        mov edi, VM_size
        call alloc
        mov r12, rax
        mov rbx, rax
        mov [r12 + VM.prog], r13
        mov [r12 + VM.has_init], r8
        ; the arguments into R0 ..
        push rcx
        mov rsi, r11
        mov rdi, rbx
        shl ecx, 4
        rep movsb
        pop rcx
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
        cmp r10, -1
        je vm_next
        ; the function returns to the program's final HALT
        mov r15, [r13 + PROG.ncode]
        dec r15
        mov r9, r10
        mov edx, ecx
        xor ecx, ecx
        mov r8d, 31
        cmp r9, [r13 + PROG.nfuncs]
        jae op_call.bad
        call vm_do_call
        jmp vm_next

vm_halt:
        xor eax, eax
        cmp qword [r12 + VM.has_init], 1
        jne vm_leave
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

vm_fail:                                ; a builtin failed; it said why
        cmp qword [r12 + VM.has_init], 2
        jne .loud
        mov rsp, [r12 + VM.rsp]
        mov eax, 1
        jmp vm_leave
.loud:
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
        ; the command's return name, for builtins that want it
        mov qword [bi_retname], 0
        mov rax, [r13 + PROG.siteret]
        mov eax, [rax + r15 * 4 - 4]
        cmp eax, -1
        je .no_name
        mov r11, [r13 + PROG.symtext]
        mov rax, [r11 + rax * 8]
        mov [bi_retname], rax
.no_name:
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
        cmp rax, T_STRUCT
        je .struct
        cmp rax, T_VEC
        ja .bad
        mov rsi, [type_names + rax * 8]
        call new_string_z
        jmp bi_str_out
.struct:
        mov rax, [rdi + 8]
        mov rax, [rax + SOBJ_TYPE]
        mov rax, [rax + STYPE.name]
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
        sub rax, [heap_base]
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

; --- strings ------------------------------------------------------------

; rsi, rdi = strings -> ZF set when equal. Keeps registers.
str_eq:
        push rcx
        push rsi
        push rdi
        mov rcx, [rsi + STR_LEN]
        cmp rcx, [rdi + STR_LEN]
        jne .out
        add rsi, STR_BYTES
        add rdi, STR_BYTES
        test rcx, rcx
        jz .out                         ; ZF is set from the cmp
        repe cmpsb
.out:
        pop rdi
        pop rsi
        pop rcx
        ret

; rsi = bytes, rcx = length, rdi = NUL-terminated text -> ZF when equal.
; Keeps registers.
bytes_eq_z:
        push rax
        push rdx
        xor edx, edx
.next:
        cmp rdx, rcx
        je .end
        mov al, [rdi + rdx]
        test al, al
        jz .differ
        cmp al, [rsi + rdx]
        jne .differ
        inc rdx
        jmp .next
.end:
        cmp byte [rdi + rdx], 0
        jmp .out
.differ:
        or edx, 1
.out:
        pop rdx
        pop rax
        ret

; rsi = bytes, rcx = length -> eax: the symbol of that name in program
; r13, or -1 when the program never mentions it.
intern_find:
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
        mov eax, 2166136261
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
        mov r10, [r13 + PROG.htab]
        mov eax, [r10 + r9 * 4]
        test eax, eax
        jz .none
        dec eax
        mov r11, [r13 + PROG.symtext]
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
.none:
        mov eax, -1
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

; rsi = bytes, rcx = length -> rax: handler of the builtin of that name,
; or 0
builtin_find:
        push rdi
        push rdx
        xor edx, edx
.next:
        mov rdi, [bi_table + rdx]
        test rdi, rdi
        jz .none
        call bytes_eq_z
        je .found
        add rdx, 16
        jmp .next
.found:
        mov rax, [bi_table + rdx + 8]
        jmp .out
.none:
        xor eax, eax
.out:
        pop rdx
        pop rdi
        ret

; --- TEMPLATE and EVAL ----------------------------------------------------

; TEMPLATE a, b, format: R0 .. R(b-1) printed where the format says STR,
; INT, REAL or BOOL (";;" is a ";"); the rest of the format as it is
op_template:
        REGADDR ecx, rcx
        cmp edx, 31
        ja vm_bad_reg
        push rcx
        call sym_of
        mov rsi, [r13 + PROG.symtext]
        mov rsi, [rsi + rax * 8]
        mov r8, [rsi + STR_LEN]
        add rsi, STR_BYTES
        mov rdi, rsi                    ; start
        lea r9, [rsi + r8]              ; end
        mov r10d, edx                   ; values
        xor r11d, r11d                  ; next value
.loop:
        cmp rsi, r9
        jae .end
        cmp byte [rsi], ';'
        jne .token
        lea rax, [rsi + 1]
        cmp rax, r9
        jae .token
        cmp byte [rax], ';'
        jne .token
        PUTC ';'
        add rsi, 2
        jmp .loop
.token:
        cmp rsi, rdi
        je .boundary
        mov al, [rsi - 1]
        cmp al, ' '
        je .boundary
        cmp al, 9
        je .boundary
        cmp al, ';'
        jne .plain
.boundary:
        call template_type              ; eax: 0 none, 1..4; rcx: length
        test eax, eax
        jz .plain
        cmp r11d, r10d
        jae .mismatch
        mov rdx, r11
        shl rdx, 4
        add rdx, rbx
        call template_emit
        jc .mismatch
        inc r11d
        add rsi, rcx
        cmp rsi, r9
        jae .loop
        cmp byte [rsi], ';'
        jne .loop
        inc rsi
        jmp .loop
.plain:
        push rdi
        movzx edi, byte [rsi]
        call con_putc
        pop rdi
        inc rsi
        jmp .loop
.end:
        cmp r11d, r10d
        jne .mismatch
        pop rcx
        mov qword [rcx], T_BOOL
        mov qword [rcx + 8], 1
        jmp vm_next
.mismatch:
        mov esi, e_template
        jmp vm_error

; rsi = cursor, r9 = end -> eax = 1 STR, 2 INT, 3 REAL, 4 BOOL or 0;
; rcx = its length. The type word must end the format or be followed by
; ';', a space or a tab.
template_type:
        push rdx
        mov rdx, r9
        sub rdx, rsi                    ; bytes left
        mov eax, 1
        mov ecx, 3
        cmp rdx, 3
        jb .none
        cmp word [rsi], 'ST'
        jne .int
        cmp byte [rsi + 2], 'R'
        je .follow
.int:
        mov eax, 2
        cmp word [rsi], 'IN'
        jne .real
        cmp byte [rsi + 2], 'T'
        je .follow
.real:
        mov ecx, 4
        cmp rdx, 4
        jb .none
        mov eax, 3
        cmp dword [rsi], 'REAL'
        je .follow
        mov eax, 4
        cmp dword [rsi], 'BOOL'
        je .follow
        jmp .none
.follow:
        cmp rdx, rcx
        je .ok
        push rax
        mov al, [rsi + rcx]
        cmp al, ';'
        je .follow_ok
        cmp al, ' '
        je .follow_ok
        cmp al, 9
        je .follow_ok
        pop rax
        jmp .none
.follow_ok:
        pop rax
.ok:
        pop rdx
        ret
.none:
        xor eax, eax
        pop rdx
        ret

; eax = type, rdx = value: prints it; carry set when it is not that type
template_emit:
        push rax
        push rcx
        push rsi
        cmp eax, 1
        je .str
        cmp eax, 2
        je .int
        cmp eax, 3
        je .real
        cmp qword [rdx], T_BOOL
        jne .bad
        mov rsi, rdx
        push rdx
        xor edx, edx
        call print_value
        pop rdx
        jmp .ok
.str:
        cmp qword [rdx], T_STR
        jne .bad
        jmp .print
.int:
        cmp qword [rdx], T_INT
        jne .bad
        jmp .print
.real:
        cmp qword [rdx], T_REAL
        jne .bad
.print:
        mov rsi, rdx
        push rdx
        xor edx, edx
        call print_value
        pop rdx
.ok:
        pop rsi
        pop rcx
        pop rax
        clc
        ret
.bad:
        pop rsi
        pop rcx
        pop rax
        stc
        ret

; EVAL a, name: runs the text in R0, then Ra = the variable name. The
; Moscow runtime evaluates what russia.su hands its host: lines of
; builtin calls, `name(ret): arg ...`, whose arguments are variable names,
; "strings", integers, true, false or null.
op_eval:
        REGADDR ecx, rcx
        push rcx
        push r9
        cmp qword [rbx], T_STR
        jne .bad
        mov rsi, [rbx + 8]
        call eval_text
        test eax, eax
        jnz vm_fail
        pop r9
        call sym_of
        xor edx, edx
        call var_cell
        pop rcx
        COPYVAL rcx, rax, rdx
        jmp vm_next
.bad:
        mov esi, e_eval
        jmp vm_error

EV_MAX_ARGS     equ 16
EV_ARGS         equ 0                   ; values
EV_OUT          equ EV_MAX_ARGS * 16
EV_NAME         equ EV_OUT + 16         ; bytes, length
EV_RET          equ EV_NAME + 16
EV_COUNT        equ EV_RET + 16
EV_SIZE         equ EV_COUNT + 16

; rsi = string -> eax: 0, or -1 after a message
eval_text:
        push rbp
        push r14
        push r15
        sub rsp, EV_SIZE
        mov rbp, rsp
        lea r14, [rsi + STR_BYTES]      ; cursor
        mov r15, [rsi + STR_LEN]
        add r15, r14                    ; end
.command:
        call ev_skip_blank_lines
        cmp r14, r15
        jae .ok
        call ev_ident
        jc .syntax
        mov [rbp + EV_NAME], rsi
        mov [rbp + EV_NAME + 8], rcx
        cmp r14, r15
        jae .syntax
        cmp byte [r14], '('
        jne .syntax
        inc r14
        call ev_ident
        jc .syntax
        mov [rbp + EV_RET], rsi
        mov [rbp + EV_RET + 8], rcx
        cmp r14, r15
        jae .syntax
        cmp byte [r14], ')'
        jne .syntax
        inc r14
        cmp r14, r15
        jae .syntax
        cmp byte [r14], ':'
        jne .syntax
        inc r14
        mov qword [rbp + EV_COUNT], 0
.argument:
        call ev_skip_spaces
        cmp r14, r15
        jae .call
        cmp byte [r14], 10
        je .call
        mov rax, [rbp + EV_COUNT]
        cmp rax, EV_MAX_ARGS
        jae .syntax
        shl rax, 4
        lea rdx, [rbp + EV_ARGS + rax]
        call ev_value
        jc .syntax
        inc qword [rbp + EV_COUNT]
        jmp .argument
.call:
        mov rsi, [rbp + EV_NAME]
        mov rcx, [rbp + EV_NAME + 8]
        call builtin_find
        test rax, rax
        jz .unknown
        mov r8, rax
        mov rsi, [rbp + EV_RET]
        mov rcx, [rbp + EV_RET + 8]
        call new_string
        mov [bi_retname], rax
        lea rdi, [rbp + EV_ARGS]
        mov esi, [rbp + EV_COUNT]
        lea rdx, [rbp + EV_OUT]
        mov qword [rdx], T_NULL
        mov qword [rdx + 8], 0
        call r8
        test eax, eax
        jnz .failed
        ; the result into the return variable, when the program has it
        mov rsi, [rbp + EV_RET]
        mov rcx, [rbp + EV_RET + 8]
        call intern_find
        cmp eax, -1
        je .command
        xor edx, edx
        call var_cell
        lea rsi, [rbp + EV_OUT]
        COPYVAL rax, rsi, rdx
        jmp .command
.unknown:
        PUTS msg_unknown1
        mov rsi, [rbp + EV_NAME]
        mov rcx, [rbp + EV_NAME + 8]
        call con_write
        PUTS msg_unknown2
        jmp .failed
.syntax:
        PUTS msg_eval1
        mov rsi, r14
        mov rcx, r15
        sub rcx, r14
        call con_write
        PUTS msg_eval2
.failed:
        mov eax, -1
        jmp .out
.ok:
        xor eax, eax
.out:
        add rsp, EV_SIZE
        pop r15
        pop r14
        pop rbp
        ret

ev_skip_spaces:
        cmp r14, r15
        jae .done
        cmp byte [r14], ' '
        je .skip
        cmp byte [r14], 9
        je .skip
        cmp byte [r14], 13
        je .skip
.done:
        ret
.skip:
        inc r14
        jmp ev_skip_spaces

ev_skip_blank_lines:
        cmp r14, r15
        jae .done
        mov al, [r14]
        cmp al, ' '
        je .skip
        cmp al, 9
        je .skip
        cmp al, 10
        je .skip
        cmp al, 13
        je .skip
.done:
        ret
.skip:
        inc r14
        jmp ev_skip_blank_lines

; -> rsi, rcx: a name at r14 (letters, digits, _), r14 past it; carry
; set when there is none
ev_ident:
        mov rsi, r14
.next:
        cmp r14, r15
        jae .end
        movzx eax, byte [r14]
        call is_name_byte
        jnc .end
        inc r14
        jmp .next
.end:
        mov rcx, r14
        sub rcx, rsi
        test rcx, rcx
        jz .none
        clc
        ret
.none:
        stc
        ret

; al -> carry set for letters, digits and _
is_name_byte:
        cmp al, '_'
        je .yes
        cmp al, '0'
        jb .no
        cmp al, '9'
        jbe .yes
        or al, 0x20
        cmp al, 'a'
        jb .no
        cmp al, 'z'
        jbe .yes
.no:
        clc
        ret
.yes:
        stc
        ret

; rdx = where: one argument at r14 as a value; carry set on bad syntax
ev_value:
        mov al, [r14]
        cmp al, '"'
        je .string
        cmp al, '-'
        je .number
        cmp al, '0'
        jb .name
        cmp al, '9'
        jbe .number
.name:
        call ev_ident
        jc .bad
        mov rdi, s_null
        call bytes_eq_z
        je .null
        mov rdi, s_true
        call bytes_eq_z
        je .true
        mov rdi, s_false
        call bytes_eq_z
        je .false
        call intern_find
        cmp eax, -1
        je .null
        push rdx
        xor edx, edx
        call var_cell
        pop rdx
        COPYVAL rdx, rax, rcx
        clc
        ret
.null:
        mov qword [rdx], T_NULL
        mov qword [rdx + 8], 0
        clc
        ret
.true:
        mov qword [rdx], T_BOOL
        mov qword [rdx + 8], 1
        clc
        ret
.false:
        mov qword [rdx], T_BOOL
        mov qword [rdx + 8], 0
        clc
        ret
.number:
        xor ecx, ecx                    ; negative?
        cmp byte [r14], '-'
        jne .digits
        mov ecx, 1
        inc r14
.digits:
        xor eax, eax
        mov rsi, r14
.digit:
        cmp r14, r15
        jae .number_end
        movzx edi, byte [r14]
        sub edi, '0'
        cmp edi, 9
        ja .number_end
        imul rax, rax, 10
        add rax, rdi
        inc r14
        jmp .digit
.number_end:
        cmp r14, rsi
        je .bad
        test ecx, ecx
        jz .positive
        neg rax
.positive:
        mov qword [rdx], T_INT
        mov [rdx + 8], rax
        clc
        ret
.string:                                ; "..." with \" \\ \n \t \r
        inc r14
        xor ecx, ecx
.char:
        cmp r14, r15
        jae .bad
        mov al, [r14]
        inc r14
        cmp al, '"'
        je .string_end
        cmp al, '\'
        jne .store
        cmp r14, r15
        jae .bad
        mov al, [r14]
        inc r14
        cmp al, 'n'
        jne .not_n
        mov al, 10
        jmp .store
.not_n:
        cmp al, 't'
        jne .not_t
        mov al, 9
        jmp .store
.not_t:
        cmp al, 'r'
        jne .store
        mov al, 13
.store:
        cmp rcx, CAPTURE_MAX
        jae .bad
        mov [CAPTURE_BUF + rcx], al
        inc rcx
        jmp .char
.string_end:
        mov esi, CAPTURE_BUF
        call new_string
        mov qword [rdx], T_STR
        mov [rdx + 8], rax
        clc
        ret
.bad:
        stc
        ret

; --- structs --------------------------------------------------------------

; rsi = name string -> rax: its type, or 0
struct_find:
        mov rax, [struct_types]
.next:
        test rax, rax
        jz .done
        push rdi
        mov rdi, [rax + STYPE.name]
        call str_eq
        pop rdi
        je .done
        mov rax, [rax + STYPE.next]
        jmp .next
.done:
        ret

; struct(Name): "field:type" ... ; Name is the return name
bi_struct:
        push rbx
        push r12
        push rdx
        mov rbx, rdi                    ; specs
        mov r12d, esi
        mov rsi, [bi_retname]
        test rsi, rsi
        jz .no_name
        call struct_find
        test rax, rax
        jnz .have_type
        mov edi, STYPE_size
        call alloc
        mov [rax + STYPE.name], rsi
        mov rdx, [struct_types]
        mov [rax + STYPE.next], rdx
        mov [struct_types], rax
.have_type:
        mov r8, rax
        mov [r8 + STYPE.nfields], r12
        lea rdi, [r12 * 8 + 8]
        call alloc
        mov [r8 + STYPE.fields], rax
        xor r9d, r9d
.spec:
        cmp r9d, r12d
        jae .done
        mov rax, r9
        shl rax, 4
        cmp qword [rbx + rax], T_STR
        jne .bad_spec
        mov rsi, [rbx + rax + 8]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        xor edx, edx
.colon:
        cmp rdx, rcx
        je .named
        cmp byte [rsi + rdx], ':'
        je .named
        inc rdx
        jmp .colon
.named:
        mov rcx, rdx
        call new_string
        mov rdx, [r8 + STYPE.fields]
        mov [rdx + r9 * 8], rax
        inc r9d
        jmp .spec
.done:
        pop rdx
        pop r12
        pop rbx
        xor eax, eax
        ret
.no_name:
        pop rdx
        pop r12
        pop rbx
        mov eax, e_struct_name
        jmp bi_error
.bad_spec:
        pop rdx
        pop r12
        pop rbx
        mov eax, e_struct_spec
        jmp bi_error

; new(o): "Type" value ...  (one value per field, in order)
bi_new:
        test esi, esi
        jz .bad
        cmp qword [rdi], T_STR
        jne .bad
        push rdi
        push rsi
        mov rsi, [rdi + 8]
        call struct_find
        pop rsi
        pop rdi
        test rax, rax
        jz .unknown
        mov r8, rax
        lea ecx, [esi - 1]
        cmp rcx, [r8 + STYPE.nfields]
        jne .count
        push rdi
        push rcx
        mov rdi, rcx
        shl rdi, 4
        add rdi, SOBJ_FIELDS
        call alloc
        pop rcx
        pop rdi
        mov [rax + SOBJ_TYPE], r8
        push rax
        lea rsi, [rdi + 16]
        lea rdi, [rax + SOBJ_FIELDS]
        shl rcx, 4
        rep movsb
        pop rax
        mov qword [rdx], T_STRUCT
        mov [rdx + 8], rax
        xor eax, eax
        ret
.bad:
        mov eax, e_new
        jmp bi_error
.unknown:
        PUTS msg_ussr
        PUTS e_unknown_type
        mov rsi, [rdi + 8]
        call print_string
        PUTS msg_quote_nl
        mov eax, -1
        ret
.count:
        mov eax, e_new_count
        jmp bi_error

; rsi = string: its text
print_string:
        push rsi
        push rcx
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        call con_write
        pop rcx
        pop rsi
        ret

; rdi = arguments (struct, field name) -> rax: the field's value cell, or
; 0 after a message
struct_field:
        cmp qword [rdi], T_STRUCT
        jne .bad
        cmp qword [rdi + 16], T_STR
        jne .bad
        push rsi
        push rcx
        push r8
        push r9
        mov r8, [rdi + 8]               ; the object
        mov r9, [r8 + SOBJ_TYPE]
        mov rsi, [rdi + 24]             ; the name
        xor ecx, ecx
.next:
        cmp rcx, [r9 + STYPE.nfields]
        jae .missing
        push rdi
        mov rdi, [r9 + STYPE.fields]
        mov rdi, [rdi + rcx * 8]
        call str_eq
        pop rdi
        je .found
        inc rcx
        jmp .next
.found:
        shl rcx, 4
        lea rax, [r8 + SOBJ_FIELDS + rcx]
        jmp .out
.missing:
        PUTS msg_ussr
        push rsi
        mov rsi, [r9 + STYPE.name]
        call print_string
        pop rsi
        PUTS e_no_field
        call print_string
        PUTS msg_quote_nl
        xor eax, eax
.out:
        pop r9
        pop r8
        pop rcx
        pop rsi
        ret
.bad:
        PUTS msg_ussr
        PUTS e_getf
        PUTC 10
        xor eax, eax
        ret

bi_getf:
        cmp esi, 2
        jne struct_field.bad_count
        call struct_field
        test rax, rax
        jz .failed
        COPYVAL rdx, rax, rcx
        xor eax, eax
        ret
.failed:
        mov eax, -1
        ret

bi_setf:
        cmp esi, 3
        jne struct_field.bad_count
        call struct_field
        test rax, rax
        jz bi_getf.failed
        lea rsi, [rdi + 32]
        COPYVAL rax, rsi, rcx
        xor eax, eax
        ret

struct_field.bad_count:
        mov eax, e_getf
        jmp bi_error

; --- UNO ------------------------------------------------------------------

bi_encode:
        cmp esi, 1
        jne .bad
        push rdx
        call capture_begin
        mov rsi, rdi
        mov dl, 1
        call print_value
        call capture_end
        pop rdx
        jmp bi_str_out
.bad:
        mov eax, e_encode
        jmp bi_error

bi_decode:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        push rdx
        mov rsi, [rdi + 8]
        mov r8, [rsi + STR_LEN]
        add rsi, STR_BYTES
        add r8, rsi                     ; end
        call uno_read
        jc .failed
        call uno_skip_ws
        cmp rsi, r8
        jne .trailing
        mov rcx, rdx
        pop rdx
        mov [rdx], rax
        mov [rdx + 8], rcx
        xor eax, eax
        ret
.trailing:
        pop rdx
        mov eax, e_decode
        jmp bi_error
.failed:
        pop rdx
        mov eax, -1
        ret
.bad:
        mov eax, e_decode
        jmp bi_error

; rsi = cursor, r8 = end
uno_skip_ws:
        cmp rsi, r8
        jae .done
        cmp byte [rsi], ' '
        ja .done
        inc rsi
        jmp uno_skip_ws
.done:
        ret

; rsi = cursor, r8 = end -> rax = tag, rdx = payload, rsi past the value;
; carry set after a message
uno_read:
        call uno_skip_ws
        cmp rsi, r8
        jae .bad
        mov al, [rsi]
        cmp al, '['
        je .vector
        cmp al, '`'
        je .string
        cmp al, '-'
        je .number
        cmp al, '0'
        jb .word
        cmp al, '9'
        jbe .number
.word:
        push r14
        push r15
        mov r14, rsi
        mov r15, r8
        call ev_ident
        mov rsi, r14
        pop r15
        pop r14
        jc .bad
        ; rsi past the word; the word itself at rsi - rcx
        push rsi
        sub rsi, rcx
        mov rdi, s_true
        call bytes_eq_z
        je .true
        mov rdi, s_false
        call bytes_eq_z
        je .false
        mov rdi, s_null
        call bytes_eq_z
        je .null
        call new_string                 ; the type's name
        pop rsi
        jmp uno_object
.true:
        pop rsi
        mov eax, T_BOOL
        mov edx, 1
        clc
        ret
.false:
        pop rsi
        mov eax, T_BOOL
        xor edx, edx
        clc
        ret
.null:
        pop rsi
        xor eax, eax
        xor edx, edx
        clc
        ret
.string:
        inc rsi
        mov rdi, rsi
.string_char:
        cmp rsi, r8
        jae .bad
        cmp byte [rsi], '`'
        je .string_end
        cmp byte [rsi], 10
        je .bad
        inc rsi
        jmp .string_char
.string_end:
        mov rcx, rsi
        sub rcx, rdi
        push rsi
        mov rsi, rdi
        call new_string
        pop rsi
        inc rsi
        mov rdx, rax
        mov eax, T_STR
        clc
        ret
.number:
        xor ecx, ecx
        cmp byte [rsi], '-'
        jne .int_digits
        mov ecx, 1
        inc rsi
.int_digits:
        xor eax, eax
        mov rdi, rsi
.int_digit:
        cmp rsi, r8
        jae .int_end
        movzx edx, byte [rsi]
        sub edx, '0'
        cmp edx, 9
        ja .int_end
        imul rax, rax, 10
        add rax, rdx
        inc rsi
        jmp .int_digit
.int_end:
        cmp rsi, rdi
        je .bad
        ; a fraction makes it a real
        lea rdx, [rsi + 1]
        cmp rdx, r8
        jae .integer
        cmp byte [rsi], '.'
        jne .integer
        movzx edx, byte [rsi + 1]
        sub edx, '0'
        cmp edx, 9
        ja .integer
        inc rsi
        cvtsi2sd xmm0, rax
        movsd xmm1, [f_one]
.frac:
        cmp rsi, r8
        jae .frac_end
        movzx edx, byte [rsi]
        sub edx, '0'
        cmp edx, 9
        ja .frac_end
        divsd xmm1, [f_ten]
        cvtsi2sd xmm2, edx
        mulsd xmm2, xmm1
        addsd xmm0, xmm2
        inc rsi
        jmp .frac
.frac_end:
        test ecx, ecx
        jz .real_out
        movq rdx, xmm0
        btc rdx, 63
        jmp .real_tag
.real_out:
        movq rdx, xmm0
.real_tag:
        mov eax, T_REAL
        clc
        ret
.integer:
        test ecx, ecx
        jz .int_out
        neg rax
.int_out:
        mov rdx, rax
        mov eax, T_INT
        clc
        ret
.vector:
        inc rsi
        push rbx
        call vec_new
        mov rbx, rax
        call uno_skip_ws
        cmp rsi, r8
        jae .vector_bad
        cmp byte [rsi], ']'
        je .vector_end
.item:
        call uno_read
        jc .vector_failed
        sub rsp, 16
        mov [rsp], rax
        mov [rsp + 8], rdx
        push rsi
        lea rsi, [rsp + 8]
        mov rdi, rbx
        call vec_push
        pop rsi
        add rsp, 16
        call uno_skip_ws
        cmp rsi, r8
        jae .vector_bad
        cmp byte [rsi], ','
        jne .item_end
        inc rsi
        jmp .item
.item_end:
        cmp byte [rsi], ']'
        jne .vector_bad
.vector_end:
        inc rsi
        mov rdx, rbx
        pop rbx
        mov eax, T_VEC
        clc
        ret
.vector_bad:
        pop rbx
        jmp .bad
.vector_failed:
        pop rbx
        stc
        ret
.bad:
        PUTS msg_ussr
        PUTS e_uno
        PUTC 10
        stc
        ret

; rax = type name string, rsi at "{": Type{field=value,...}
uno_object:
        push rbx
        push r12
        mov rdi, rax                    ; the name, for a message
        push rsi
        mov rsi, rdi
        call struct_find
        pop rsi
        test rax, rax
        jz .unknown
        mov r12, rax
        mov rdi, [r12 + STYPE.nfields]
        shl rdi, 4
        add rdi, SOBJ_FIELDS
        call alloc
        mov rbx, rax
        mov [rbx + SOBJ_TYPE], r12
        call uno_skip_ws
        cmp rsi, r8
        jae .bad
        cmp byte [rsi], '{'
        jne .bad
        inc rsi
        call uno_skip_ws
        cmp rsi, r8
        jae .bad
        cmp byte [rsi], '}'
        je .end
.field:
        call uno_skip_ws
        push r14
        push r15
        mov r14, rsi
        mov r15, r8
        call ev_ident
        mov rsi, r14
        pop r15
        pop r14
        jc .bad
        ; which field: rsi - rcx, rcx
        push rsi
        sub rsi, rcx
        call new_string
        mov rdi, rax
        xor ecx, ecx
.which:
        cmp rcx, [r12 + STYPE.nfields]
        jae .no_field
        mov rsi, [r12 + STYPE.fields]
        mov rsi, [rsi + rcx * 8]
        call str_eq
        je .found
        inc rcx
        jmp .which
.no_field:
        pop rsi
        jmp .bad
.found:
        pop rsi
        push rcx
        call uno_skip_ws
        cmp rsi, r8
        jae .bad_pop
        cmp byte [rsi], '='
        jne .bad_pop
        inc rsi
        call uno_read
        pop rcx
        jc .failed
        shl rcx, 4
        mov [rbx + SOBJ_FIELDS + rcx], rax
        mov [rbx + SOBJ_FIELDS + rcx + 8], rdx
        call uno_skip_ws
        cmp rsi, r8
        jae .bad
        cmp byte [rsi], ','
        jne .close
        inc rsi
        jmp .field
.close:
        cmp byte [rsi], '}'
        jne .bad
.end:
        inc rsi
        mov rdx, rbx
        mov eax, T_STRUCT
        pop r12
        pop rbx
        clc
        ret
.bad_pop:
        pop rcx
.bad:
        pop r12
        pop rbx
        jmp uno_read.bad
.failed:
        pop r12
        pop rbx
        stc
        ret
.unknown:
        pop r12
        pop rbx
        PUTS msg_ussr
        PUTS e_unknown_type
        push rsi
        mov rsi, rdi
        call print_string
        pop rsi
        PUTS msg_quote_nl
        stc
        ret

; --- try, errors, environment ----------------------------------------------

; try(failed): "function" arguments ... -> 0, or 1 when the call failed
bi_try:
        test esi, esi
        jz .bad
        cmp qword [rdi], T_STR
        jne .bad
        push rdx
        mov r8, [rdi + 8]               ; the name
        mov r9, [r13 + PROG.funcs]
        xor r10d, r10d
.find:
        cmp r10, [r13 + PROG.nfuncs]
        jae .none
        mov eax, [r9 + FUNC.name]
        mov r11, [r13 + PROG.symtext]
        push rsi
        push rdi
        mov rsi, [r11 + rax * 8]
        mov rdi, r8
        call str_eq
        pop rdi
        pop rsi
        je .found
        add r9, FUNC_size
        inc r10
        jmp .find
.found:
        lea rdx, [rdi + 16]
        lea ecx, [esi - 1]
        mov rdi, r13
        mov rsi, r10
        mov r8d, 2
        call vm_exec
        pop rdx
        jmp bi_int_out
.none:
        pop rdx
        PUTS msg_ussr
        PUTS e_try_none
        mov rsi, r8
        call print_string
        PUTS msg_quote_nl
        mov eax, -1
        ret
.bad:
        mov eax, e_try
        jmp bi_error

bi_write_stderr:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        mov rsi, [rdi + 8]
        call print_string
        mov rax, [rsi + STR_LEN]
        jmp bi_int_out
.bad:
        mov eax, e_write_stderr
        jmp bi_error

bi_fail:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        PUTS msg_ussr
        mov rsi, [rdi + 8]
        call print_string
        PUTC 10
        mov eax, -1
        ret
.bad:
        mov eax, e_fail
        jmp bi_error

bi_null:                                ; gc, alias, keybind, keybind_match
        xor eax, eax
        ret

bi_getcwd:
        mov rsi, s_root
        call new_string_z
        jmp bi_str_out

; The environment: ENV_MAX fixed slots of ENV_NAME + ENV_VALUE bytes in
; the runtime's own memory, so it outlives the programs that set it.
ENV_MAX         equ 32
ENV_NAME        equ 32
ENV_VALUE       equ 224
ENV_SLOT        equ ENV_NAME + ENV_VALUE

; rdi = name string -> rax: its slot, or 0
env_find:
        push rcx
        push rsi
        mov rsi, rdi
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        mov rax, env_table
.next:
        cmp rax, env_table + ENV_MAX * ENV_SLOT
        jae .none
        cmp byte [rax], 0
        je .skip
        push rdi
        mov rdi, rax
        call bytes_eq_z
        pop rdi
        je .done
.skip:
        add rax, ENV_SLOT
        jmp .next
.none:
        xor eax, eax
.done:
        pop rsi
        pop rcx
        ret

bi_getenv:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        mov rdi, [rdi + 8]
        call env_find
        test rax, rax
        jz bi_ok
        lea rsi, [rax + ENV_NAME]
        call new_string_z
        jmp bi_str_out
.bad:
        mov eax, e_getenv
        jmp bi_error

; rsi = name text, rdi = value text (NUL-terminated): sets it
env_set_z:
        push rax
        push rcx
        push rsi
        push rdi
        push r8
        mov r8, rdi
        call strlen_z
        cmp rcx, ENV_NAME - 1
        jae .done
        mov rax, env_table
.slot:
        cmp rax, env_table + ENV_MAX * ENV_SLOT
        jae .free
        cmp byte [rax], 0
        je .next
        mov rdi, rax
        call bytes_eq_z
        je .store
.next:
        add rax, ENV_SLOT
        jmp .slot
.free:
        mov rax, env_table
.free_slot:
        cmp rax, env_table + ENV_MAX * ENV_SLOT
        jae .done
        cmp byte [rax], 0
        je .store
        add rax, ENV_SLOT
        jmp .free_slot
.store:
        mov rdi, rax
        rep movsb
        mov byte [rdi], 0
        lea rdi, [rax + ENV_NAME]
        mov rsi, r8
        call strlen_z
        cmp rcx, ENV_VALUE - 1
        jb .copy
        mov ecx, ENV_VALUE - 1
.copy:
        rep movsb
        mov byte [rdi], 0
.done:
        pop r8
        pop rdi
        pop rsi
        pop rcx
        pop rax
        ret

bi_setenv:
        cmp esi, 2
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        cmp qword [rdi + 16], T_STR
        jne .bad
        mov rsi, [rdi + 8]
        mov rdi, [rdi + 24]
        add rsi, STR_BYTES
        add rdi, STR_BYTES
        call env_set_z
        xor eax, eax
        jmp bi_int_out
.bad:
        mov eax, e_setenv
        jmp bi_error

bi_unsetenv:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        mov rdi, [rdi + 8]
        call env_find
        test rax, rax
        jz .done
        mov byte [rax], 0
.done:
        xor eax, eax
        jmp bi_int_out
.bad:
        mov eax, e_getenv
        jmp bi_error

; --- the terminal -----------------------------------------------------------

; terminal_getch(c): the next byte of the console, a line at a time:
; the line is read (echoed, Backspace erases) when the last one is used
; up, and ends with a 10. Ctrl-D on an empty line is the end: -1.
bi_terminal_getch:
        push rdx
.again:
        mov rax, [tg_pos]
        cmp rax, [tg_len]
        jb .byte
        cmp qword [tg_newline], 0
        jne .newline
        call read_line
        cmp qword [read_line_eof], 0
        jne .eof
        mov [tg_line], rax
        mov rcx, [rax + STR_LEN]
        mov [tg_len], rcx
        mov qword [tg_pos], 0
        mov qword [tg_newline], 1
        jmp .again
.byte:
        mov rcx, [tg_line]
        movzx eax, byte [rcx + STR_BYTES + rax]
        inc qword [tg_pos]
        pop rdx
        jmp bi_int_out
.newline:
        mov qword [tg_newline], 0
        mov eax, 10
        pop rdx
        jmp bi_int_out
.eof:
        mov rax, -1
        pop rdx
        jmp bi_int_out

bi_terminal_raw:                        ; not a terminal: russia reads lines
        mov rax, -1
        jmp bi_int_out

bi_zero:                                ; terminal_sane
        xor eax, eax
        jmp bi_int_out

bi_terminal_key_ready:
        mov qword [rdx], T_BOOL
        mov qword [rdx + 8], 0
        xor eax, eax
        ret

bi_terminal_size:
        push rdx
        call vec_new
        mov rdi, rax
        mov eax, 80
        call vec_push_int
        mov eax, 25
        call vec_push_int
        mov rax, rdi
        pop rdx
        jmp bi_vec_out

; sleep(_): milliseconds, at the timer's 10 ms
bi_sleep:
        cmp esi, 1
        jne .done
        cmp qword [rdi], T_INT
        jne .done
        push rdx
        mov rax, [rdi + 8]
        add rax, 9
        xor edx, edx
        mov ecx, 10
        div rcx
        pop rdx
        add rax, [ticks]
.wait:
        cmp [ticks], rax
        jae .done
        sti
        hlt
        jmp .wait
.done:
        xor eax, eax
        ret

; file_exists(b): name -> true for a file of the boot image
bi_file_exists:
        cmp esi, 1
        jne .no
        cmp qword [rdi], T_STR
        jne .no
        push rdx
        mov rax, [rdi + 8]
        lea rsi, [rax + STR_BYTES]
        mov rcx, [rax + STR_LEN]
        call find_file
        pop rdx
        test rax, rax
        setnz al
        movzx eax, al
        mov qword [rdx], T_BOOL
        mov [rdx + 8], rax
        xor eax, eax
        ret
.no:
        mov qword [rdx], T_BOOL
        mov qword [rdx + 8], 0
        xor eax, eax
        ret

; readfile(s): name -> a file of the boot image as a string, or null
bi_readfile:
        cmp esi, 1
        jne .none
        cmp qword [rdi], T_STR
        jne .none
        push rdx
        mov rax, [rdi + 8]
        lea rsi, [rax + STR_BYTES]
        mov rcx, [rax + STR_LEN]
        call find_file
        test rax, rax
        jz .missing
        mov rsi, rax
        mov rcx, rdx
        call new_string
        pop rdx
        jmp bi_str_out
.missing:
        pop rdx
.none:
        xor eax, eax
        ret

; --- bytes ------------------------------------------------------------------

; text_bytes(v): s -> its bytes as a vector of integers
bi_text_bytes:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        push rdx
        push rbx
        mov rbx, [rdi + 8]
        call vec_new
        mov rdi, rax
        ; room for all of them at once
        mov rcx, [rbx + STR_LEN]
        push rdi
        lea rdi, [rcx * 8 + 8]
        shl rdi, 1
        call alloc
        pop rdi
        mov [rdi + VEC_ITEMS], rax
        mov [rdi + VEC_CAP], rcx
        mov [rdi + VEC_COUNT], rcx
        xor edx, edx
.byte:
        cmp rdx, rcx
        jae .done
        mov qword [rax], T_INT
        movzx esi, byte [rbx + STR_BYTES + rdx]
        mov [rax + 8], rsi
        add rax, 16
        inc rdx
        jmp .byte
.done:
        mov rax, rdi
        pop rbx
        pop rdx
        jmp bi_vec_out
.bad:
        mov eax, e_text_bytes
        jmp bi_error

; rdi = arguments (vector [from [count]]), esi = count -> r8 = first
; item, rcx = items; carry set when the range is wrong
byte_range:
        cmp qword [rdi], T_VEC
        jne .bad
        mov r8, [rdi + 8]
        mov r9, [r8 + VEC_COUNT]
        xor eax, eax                    ; from
        cmp esi, 2
        jb .have_from
        cmp qword [rdi + 16], T_INT
        jne .bad
        mov rax, [rdi + 24]
        cmp rax, r9
        ja .bad
.have_from:
        mov rcx, r9
        sub rcx, rax                    ; count
        cmp esi, 3
        jb .have_count
        cmp qword [rdi + 32], T_INT
        jne .bad
        mov r10, [rdi + 40]
        cmp r10, rcx
        ja .bad
        mov rcx, r10
.have_count:
        shl rax, 4
        mov r8, [r8 + VEC_ITEMS]
        add r8, rax
        clc
        ret
.bad:
        stc
        ret

; bytes_text(s): v [from [count]] -> the bytes as a string; null when one
; of them is 0
bi_bytes_text:
        test esi, esi
        jz .bad
        cmp esi, 3
        ja .bad
        call byte_range
        jc .bad
        push rdx
        lea rdi, [rcx + 9]
        call alloc
        mov [rax + STR_LEN], rcx
        xor edx, edx
.byte:
        cmp rdx, rcx
        jae .done
        mov r9, [r8 + 8]
        test r9b, r9b
        jz .zero
        mov [rax + STR_BYTES + rdx], r9b
        add r8, 16
        inc rdx
        jmp .byte
.done:
        pop rdx
        jmp bi_str_out
.zero:
        pop rdx
        xor eax, eax
        ret
.bad:
        mov eax, e_bytes_text
        jmp bi_error

; crc32(n): data [from [count]] -> CRC-32 of a string or a byte vector
bi_crc32:
        test esi, esi
        jz .bad
        cmp qword [rdi], T_STR
        je .string
        push rdx
        call byte_range
        pop rdx
        jc .bad
        mov eax, -1
.item:
        test rcx, rcx
        jz .done
        xor al, [r8 + 8]
        add r8, 16
        mov r9d, 8
.bit:
        shr eax, 1
        jnc .no_poly
        xor eax, 0xedb88320
.no_poly:
        dec r9d
        jnz .bit
        dec rcx
        jmp .item
.done:
        not eax
        jmp bi_int_out
.string:
        push rdx
        mov rsi, [rdi + 8]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
        call crc32
        pop rdx
        jmp bi_int_out
.bad:
        mov eax, e_crc32
        jmp bi_error

; --- shell commands of the kernel: they print, and return the value ----------

bi_help:
        PUTS s_help
        xor eax, eax
        ret

bi_uptime:
        mov rax, [ticks]
        push rdx
        xor edx, edx
        mov ecx, 100
        div rcx
        pop rdx
        PUTS s_up
        call print_int
        PUTS s_seconds
        jmp bi_int_out

bi_ls:
        call bi_k_modules
        mov rax, [rdx + 8]
        xor ecx, ecx
.next:
        cmp rcx, [rax + VEC_COUNT]
        jae .done
        mov rsi, rcx
        shl rsi, 4
        add rsi, [rax + VEC_ITEMS]
        mov rsi, [rsi + 8]
        call print_string
        PUTC 10
        inc rcx
        jmp .next
.done:
        xor eax, eax
        ret

bi_mem:
        call bi_k_mem
        mov rax, [rdx + 8]
        mov rax, [rax + VEC_ITEMS]
        PUTS s_heap
        mov rcx, [rax + 8]
        push rax
        mov rax, rcx
        shr rax, 10
        call print_int
        PUTS s_kib_free
        pop rax
        mov rax, [rax + 24]
        shr rax, 10
        call print_int
        PUTS s_kib_nl
        xor eax, eax
        ret

bi_cpu:
        call bi_k_cpu
        mov rsi, [rdx + 8]
        call print_string
        PUTC 10
        xor eax, eax
        ret

bi_uname:
        mov rsi, s_uname
        call new_string_z
        mov rsi, rax
        call print_string
        PUTC 10
        jmp bi_str_out

bi_pwd:
        mov rsi, s_root
        call new_string_z
        mov rsi, rax
        call print_string
        PUTC 10
        jmp bi_str_out

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
%elif i == 0x45
        dq op_eval
%elif i == 0x46
        dq op_random64
%elif i == 0x47
        dq op_seed64
%elif i == 0x49
        dq op_time
%elif i == 0x4d
        dq op_template
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
        dq n_struct, bi_struct
        dq n_new, bi_new
        dq n_getf, bi_getf
        dq n_setf, bi_setf
        dq n_encode, bi_encode
        dq n_decode, bi_decode
        dq n_try, bi_try
        dq n_write_stderr, bi_write_stderr
        dq n_fail, bi_fail
        dq n_gc, bi_null
        dq n_alias, bi_null
        dq n_keybind, bi_null
        dq n_keybind_match, bi_null
        dq n_getenv, bi_getenv
        dq n_setenv, bi_setenv
        dq n_unsetenv, bi_unsetenv
        dq n_getcwd, bi_getcwd
        dq n_terminal_getch, bi_terminal_getch
        dq n_terminal_raw, bi_terminal_raw
        dq n_terminal_sane, bi_zero
        dq n_terminal_key_ready, bi_terminal_key_ready
        dq n_terminal_size, bi_terminal_size
        dq n_sleep, bi_sleep
        dq n_file_exists, bi_file_exists
        dq n_readfile, bi_readfile
        dq n_help, bi_help
        dq n_uptime, bi_uptime
        dq n_ls, bi_ls
        dq n_mem, bi_mem
        dq n_cpu, bi_cpu
        dq n_clear, bi_k_clear
        dq n_color, bi_k_color
        dq n_uname, bi_uname
        dq n_pwd, bi_pwd
        dq n_run, bi_k_exec
        dq n_reboot, bi_k_reboot
        dq n_halt, bi_k_halt
        dq n_text_bytes, bi_text_bytes
        dq n_bytes_text, bi_bytes_text
        dq n_crc32, bi_crc32
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
n_struct:         db "struct", 0
n_new:            db "new", 0
n_getf:           db "getf", 0
n_setf:           db "setf", 0
n_encode:         db "encode", 0
n_decode:         db "decode", 0
n_try:            db "try", 0
n_write_stderr:   db "write_stderr", 0
n_fail:           db "fail", 0
n_gc:             db "gc", 0
n_alias:          db "alias", 0
n_keybind:        db "keybind", 0
n_keybind_match:  db "keybind_match", 0
n_getenv:         db "getenv", 0
n_setenv:         db "setenv", 0
n_unsetenv:       db "unsetenv", 0
n_getcwd:         db "getcwd", 0
n_terminal_getch:  db "terminal_getch", 0
n_terminal_raw:   db "terminal_raw", 0
n_terminal_sane:  db "terminal_sane", 0
n_terminal_key_ready:  db "terminal_key_ready", 0
n_terminal_size:  db "terminal_size", 0
n_sleep:          db "sleep", 0
n_file_exists:    db "file_exists", 0
n_readfile:       db "readfile", 0
n_help:           db "help", 0
n_uptime:         db "uptime", 0
n_ls:             db "ls", 0
n_mem:            db "mem", 0
n_cpu:            db "cpu", 0
n_clear:          db "clear", 0
n_color:          db "color", 0
n_uname:          db "uname", 0
n_pwd:            db "pwd", 0
n_run:            db "run", 0
n_reboot:         db "reboot", 0
n_halt:           db "halt", 0
n_text_bytes:   db "text_bytes", 0
n_bytes_text:   db "bytes_text", 0
n_crc32:        db "crc32", 0
e_text_bytes:   db "text_bytes expects a string", 0
e_bytes_text:   db "bytes_text(s): v [from [count]]", 0
e_crc32:        db "crc32(n): data [from [count]]", 0

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


s_struct:       db "struct", 0
s_root:         db "/", 0
s_up:           db "up ", 0
s_seconds:      db " s", 10, 0
s_heap:         db "heap ", 0
s_kib_free:     db " KiB, free ", 0
s_kib_nl:       db " KiB", 10, 0
s_uname:        db "USSR Moscow amd64", 0
s_help:         db "Moscow kernel commands (USSR commands; each prints and returns):", 10
                db "  uptime(s):            seconds since boot", 10
                db "  ls(v):                files of the boot image", 10
                db "  mem(v):               [heap bytes, free bytes]", 10
                db "  cpu(s):               the processor", 10
                db "  uname(s):             the system", 10
                db "  pwd(s):               the current folder (always /)", 10
                db "  clear(_):             clear the screen", 10
                db "  color(_): fg bg       VGA colours 0..15", 10
                db "  run(status): ", 34, "f.ru", 34, "   run a program of the image", 10
                db "  sleep(_): ms          wait", 10
                db "  getenv(v): n / setenv(_): n v / unsetenv(_): n", 10
                db "  reboot(_): / halt(_):", 10
                db "Everything else is USSR, run by russia.su. Ctrl-D leaves it.", 10, 0
msg_quote_nl:   db "'", 10, 0
msg_eval1:      db "USSR: eval: the Moscow runtime cannot run '", 0
msg_eval2:      db "'", 10, 0
e_template:     db "template arguments do not match format", 0
e_eval:         db "eval expects a string", 0
e_struct_name:  db "struct(Name): needs a type name", 0
e_struct_spec:  db "struct(...) field specs must be strings (", 34, "name:type", 34, ")", 0
e_new:          db "new(x): ", 34, "TypeName", 34, " args...", 0
e_new_count:    db "new(): the number of values is not the number of fields", 0
e_unknown_type: db "unknown struct type '", 0
e_no_field:     db " has no field '", 0
e_getf:         db "getf() expects (struct instance, field name string)", 0
e_encode:       db "encode(s): value", 0
e_decode:       db "decode(v): UNO text", 0
e_uno:          db "invalid UNO text", 0
e_try:          db "try(failed): ", 34, "function", 34, " args ...", 0
e_try_none:     db "try: no function '", 0
e_write_stderr: db "write_stderr expects a string", 0
e_fail:         db "fail() expects a message string", 0
e_getenv:       db "getenv(v): name", 0
e_setenv:       db "setenv(r): name value", 0

env_ps1:        db "PS1", 0
val_ps1:        db "russia> ", 0
env_user:       db "USER", 0
val_user:       db "comrade", 0
env_host:       db "HOSTNAME", 0
val_host:       db "moscow", 0
env_home:       db "HOME", 0
env_os:         db "OS", 0
val_os:         db "Moscow", 0

        align 8
rt_gdt:
        dq 0
        dq 0x00af9a000000ffff           ; 0x08: 64-bit code
        dq 0x00cf92000000ffff           ; 0x10: data
rt_gdtr:
        dw rt_gdtr - rt_gdt - 1
        dq rt_gdt

rt_end:

; =====================================================================
; variables (zeroed at start), below the stack
; =====================================================================

absolute RT_DATA
boot_info:      resq 1
heap_base:      resq 1
ram_top:        resq 1
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
capture_on:     resq 1
capture_len:    resq 1
real_plain:     resq 1
bi_retname:     resq 1
struct_types:   resq 1
tg_line:        resq 1
tg_len:         resq 1
tg_pos:         resq 1
tg_newline:     resq 1
read_line_eof:  resq 1
env_table:      resb 32 * 256
rt_data_end:
