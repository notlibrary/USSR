; cffi.inc - C libraries for the Moscow runtime (ussr_rt.nasm includes
; it): an ELF loader and the c_* builtins the hosted ussr has (src/cffi.c),
; so a program calls C the same way on Windows, Linux and Moscow.
;
;   k_elf_load(lib): bytes       loads an x86-64 ELF shared object (built
;                                freestanding: clang -ffreestanding -fPIC
;                                -mno-red-zone, ld.lld -shared
;                                --hash-style=sysv) from its bytes: a
;                                library number, or null. moscow.su's
;                                c_load reads the file and calls it.
;   c_sym(f): lib name           the address of a symbol it exports
;   c_call(r): f [args ...]      calls f (System V: rdi rsi rdx rcx r8 r9,
;                                then the stack) with up to 12 integers,
;                                strings (their bytes, NUL-terminated) or
;                                byte vectors (a copy, whose bytes come
;                                back into the vector): rax
;   c_alloc(p): n   c_free(_): p   c_read(v): p n   c_write(n): p data
;   c_string(s): p
;
; The loader lays the PT_LOAD segments out in the heap, applies the
; RELATIVE, 64, GLOB_DAT and JUMP_SLOT relocations, takes the library's
; undefined symbols from the C runtime below (memcpy, memmove, memset,
; memcmp, strlen) and runs its init array. Code it runs shares the
; kernel's stack and its interrupts: the library must not use the red
; zone. A library loaded and memory taken while a program runs belong to
; that program: when it ends they are forgotten (exec_file and
; k_exec_bytes keep clib_count and cfree_head across it).

CLIB_MAX        equ 16
CLIB_SIZE       equ 32                  ; base, symtab, strtab, symbols
C_ARGS          equ 12
C_SCRATCH       equ 1024 * 1024         ; byte vectors passed to c_call
; ELF
PT_LOAD         equ 1
PT_DYNAMIC      equ 2
DT_NULL         equ 0
DT_PLTRELSZ     equ 2
DT_HASH         equ 4
DT_STRTAB       equ 5
DT_SYMTAB       equ 6
DT_RELA         equ 7
DT_RELASZ       equ 8
DT_JMPREL       equ 23
DT_INIT_ARRAY   equ 25
DT_INIT_ARRAYSZ equ 27

cffi_init:
        push rax
        push rdi
        mov edi, C_SCRATCH
        call alloc
        mov [c_scratch], rax
        pop rdi
        pop rax
        ret

; --- the C runtime libraries get: System V calls, rax the result

c_memcpy:
c_memmove:
        mov rax, rdi
        mov rcx, rdx
        cmp rdi, rsi
        jbe .forward
        lea r8, [rsi + rdx]
        cmp rdi, r8
        jae .forward
        ; overlapping, the destination after the source: backwards
        lea rsi, [rsi + rdx - 1]
        lea rdi, [rdi + rdx - 1]
        std
        rep movsb
        cld
        ret
.forward:
        rep movsb
        ret

c_memset:
        mov r8, rdi
        mov eax, esi
        mov rcx, rdx
        rep stosb
        mov rax, r8
        ret

c_memcmp:
        xor eax, eax
        test rdx, rdx
        jz .done
.next:
        movzx eax, byte [rdi]
        movzx ecx, byte [rsi]
        sub eax, ecx
        jnz .done
        inc rdi
        inc rsi
        dec rdx
        jnz .next
.done:
        ret

c_strlen:
        xor eax, eax
.next:
        cmp byte [rdi + rax], 0
        je .done
        inc rax
        jmp .next
.done:
        ret

; the kernel's own, for libraries that drive hardware (lib/gui.c):
; moscow_ticks() -> timer ticks, 100 a second; moscow_gui(on) gives the
; screen and the PS/2 port to the library (1) or back (0);
; moscow_term_raw() -> 1 in terminal_raw's mode
c_ticks:
        mov rax, [ticks]
        ret

c_gui:
        mov [gui_on], rdi
        ret

c_term_raw:
        mov rax, [term_raw]
        ret

; name, address
c_exports:
        dq s_ticks, c_ticks
        dq s_gui, c_gui
        dq s_term_raw, c_term_raw
        dq s_memcpy, c_memcpy
        dq s_memmove, c_memmove
        dq s_memset, c_memset
        dq s_memcmp, c_memcmp
        dq s_strlen, c_strlen
        dq 0, 0
s_ticks:        db "moscow_ticks", 0
s_gui:          db "moscow_gui", 0
s_term_raw:     db "moscow_term_raw", 0
s_memcpy:       db "memcpy", 0
s_memmove:      db "memmove", 0
s_memset:       db "memset", 0
s_memcmp:       db "memcmp", 0
s_strlen:       db "strlen", 0

; rsi, rdi = NUL-terminated names -> ZF set when equal. Keeps the others.
cstr_eq:
        push rax
        push rsi
        push rdi
.next:
        mov al, [rsi]
        cmp al, [rdi]
        jne .out
        test al, al
        jz .out
        inc rsi
        inc rdi
        jmp .next
.out:
        pop rdi
        pop rsi
        pop rax
        ret

; --- the loader

; k_elf_load(lib): bytes -> a library number (from 1), or null
bi_k_elf_load:
        cmp esi, 1
        jne .usage
        cmp qword [rdi], T_VEC
        jne .usage
        push rdx
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        cmp qword [clib_count], CLIB_MAX
        jae .full
        ; the file's bytes, contiguous
        mov r8, [rdi + 8]
        mov rcx, [r8 + VEC_COUNT]
        cmp rcx, 64
        jb .not_elf
        lea rdi, [rcx + 8]
        call alloc
        mov r12, rax                    ; the file
        mov rsi, [r8 + VEC_ITEMS]
        xor edx, edx
.pack:
        mov r9, [rsi + 8]
        mov [r12 + rdx], r9b
        add rsi, 16
        inc rdx
        cmp rdx, rcx
        jb .pack
        ; an ELF64, little-endian, shared object for x86-64
        cmp dword [r12], 0x464c457f
        jne .not_elf
        cmp word [r12 + 4], 0x0102
        jne .not_elf
        cmp word [r12 + 16], 3
        jne .not_elf
        cmp word [r12 + 18], 62
        jne .not_elf
        ; the span of the loaded segments
        mov r13, [r12 + 32]             ; e_phoff
        add r13, r12
        movzx r14d, word [r12 + 56]     ; e_phnum
        movzx r15d, word [r12 + 54]     ; e_phentsize
        xor ebx, ebx                    ; the end of the highest segment
        mov rcx, r14
        mov rsi, r13
.span:
        test rcx, rcx
        jz .spanned
        cmp dword [rsi], PT_LOAD
        jne .span_next
        mov rax, [rsi + 16]             ; p_vaddr
        add rax, [rsi + 40]             ; p_memsz
        cmp rax, rbx
        jbe .span_next
        mov rbx, rax
.span_next:
        add rsi, r15
        dec rcx
        jmp .span
.spanned:
        test rbx, rbx
        jz .not_elf
        lea rdi, [rbx + 4096]
        call alloc
        add rax, 4095
        and rax, -4096
        mov rbp, rax                    ; the base
        ; the segments
        mov rcx, r14
        mov rsi, r13
.load:
        test rcx, rcx
        jz .loaded
        cmp dword [rsi], PT_LOAD
        jne .load_next
        push rcx
        push rsi
        mov rdi, [rsi + 16]
        add rdi, rbp
        mov rcx, [rsi + 32]             ; p_filesz
        mov rsi, [rsi + 8]              ; p_offset
        add rsi, r12
        rep movsb
        pop rsi
        pop rcx
.load_next:
        add rsi, r15
        dec rcx
        jmp .load
.loaded:
        ; the dynamic section
        mov qword [elf_dyn], 0
        mov rcx, r14
        mov rsi, r13
.find_dyn:
        test rcx, rcx
        jz .have_dyn
        cmp dword [rsi], PT_DYNAMIC
        jne .dyn_next
        mov rax, [rsi + 16]
        add rax, rbp
        mov [elf_dyn], rax
.dyn_next:
        add rsi, r15
        dec rcx
        jmp .find_dyn
.have_dyn:
        mov rsi, [elf_dyn]
        test rsi, rsi
        jz .not_elf
        xor eax, eax
        mov [elf_hash], rax
        mov [elf_strtab], rax
        mov [elf_symtab], rax
        mov [elf_rela], rax
        mov [elf_relasz], rax
        mov [elf_jmprel], rax
        mov [elf_pltrelsz], rax
        mov [elf_init], rax
        mov [elf_initsz], rax
.dyn:
        mov rax, [rsi]
        mov rdx, [rsi + 8]
        cmp rax, DT_NULL
        je .dyn_done
        lea r8, [rdx + rbp]             ; an address, if it is one
        cmp rax, DT_HASH
        jne .not_hash
        mov [elf_hash], r8
.not_hash:
        cmp rax, DT_STRTAB
        jne .not_strtab
        mov [elf_strtab], r8
.not_strtab:
        cmp rax, DT_SYMTAB
        jne .not_symtab
        mov [elf_symtab], r8
.not_symtab:
        cmp rax, DT_RELA
        jne .not_rela
        mov [elf_rela], r8
.not_rela:
        cmp rax, DT_RELASZ
        jne .not_relasz
        mov [elf_relasz], rdx
.not_relasz:
        cmp rax, DT_JMPREL
        jne .not_jmprel
        mov [elf_jmprel], r8
.not_jmprel:
        cmp rax, DT_PLTRELSZ
        jne .not_pltrelsz
        mov [elf_pltrelsz], rdx
.not_pltrelsz:
        cmp rax, DT_INIT_ARRAY
        jne .not_init
        mov [elf_init], r8
.not_init:
        cmp rax, DT_INIT_ARRAYSZ
        jne .dyn_next_entry
        mov [elf_initsz], rdx
.dyn_next_entry:
        add rsi, 16
        jmp .dyn
.dyn_done:
        cmp qword [elf_hash], 0
        je .no_hash
        cmp qword [elf_symtab], 0
        je .not_elf
        ; the relocations
        mov rsi, [elf_rela]
        mov rcx, [elf_relasz]
        call elf_relocate
        jc .unresolved
        mov rsi, [elf_jmprel]
        mov rcx, [elf_pltrelsz]
        call elf_relocate
        jc .unresolved
        ; the library's record
        mov rax, [clib_count]
        shl rax, 5
        lea rdi, [clib_table + rax]
        mov [rdi], rbp
        mov rax, [elf_symtab]
        mov [rdi + 8], rax
        mov rax, [elf_strtab]
        mov [rdi + 16], rax
        mov rax, [elf_hash]
        mov eax, [rax + 4]              ; nchain: the number of symbols
        mov [rdi + 24], rax
        inc qword [clib_count]
        ; its constructors
        mov rbx, [elf_init]
        mov r12, [elf_initsz]
        shr r12, 3
.ctor:
        test rbx, rbx
        jz .ctors_done
        test r12, r12
        jz .ctors_done
        mov rax, [rbx]
        test rax, rax
        jz .ctor_next
        mov r13, rsp
        and rsp, -16
        call rax
        mov rsp, r13
.ctor_next:
        add rbx, 8
        dec r12
        jmp .ctor
.ctors_done:
        mov rax, [clib_count]
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop rdx
        jmp bi_int_out
.unresolved:
        PUTS msg_ussr
        PUTS e_elf_needs
        mov rsi, rax
        call con_puts
        PUTS e_elf_needs2
        jmp .null
.no_hash:
        mov eax, e_elf_hash
        jmp .fail
.full:
        mov eax, e_elf_full
        jmp .fail
.not_elf:
        mov eax, e_elf_bad
.fail:
        mov rsi, rax
        PUTS msg_ussr
        call con_puts
        PUTC 10
.null:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop rdx
        mov qword [rdx], T_NULL
        xor eax, eax
        ret
.usage:
        mov eax, e_elf_usage
        jmp bi_error

; rsi = Elf64_Rela entries, rcx = their bytes, rbp = the base -> applies
; them; CF set with rax = the name of a symbol nobody gives
elf_relocate:
        test rsi, rsi
        jz .done
.next:
        cmp rcx, 24
        jb .done
        mov rdi, [rsi]                  ; r_offset
        add rdi, rbp
        mov rax, [rsi + 8]              ; r_info
        mov r8, rax
        shr r8, 32                      ; the symbol
        and eax, 0xffffffff             ; the type
        mov r9, [rsi + 16]              ; r_addend
        cmp eax, 8                      ; RELATIVE
        jne .not_relative
        lea rax, [rbp + r9]
        mov [rdi], rax
        jmp .step
.not_relative:
        test eax, eax                   ; NONE
        jz .step
        push rax
        call elf_symbol
        mov r10, rax
        pop rax
        jc .missing
        cmp eax, 1                      ; 64
        jne .not_64
        add r10, r9
        mov [rdi], r10
        jmp .step
.not_64:
        cmp eax, 6                      ; GLOB_DAT
        je .absolute
        cmp eax, 7                      ; JUMP_SLOT
        jne .unknown
.absolute:
        mov [rdi], r10
.step:
        add rsi, 24
        sub rcx, 24
        jmp .next
.done:
        clc
        ret
.missing:
        mov rax, r10
        stc
        ret
.unknown:
        mov rax, e_elf_reloc
        stc
        ret

; r8 = symbol number -> rax: its address (the library's own, or the C
; runtime's); CF set with rax = its name when nobody gives it. A weak
; symbol nobody gives is 0.
elf_symbol:
        push rsi
        push rdi
        push rcx
        mov rax, r8
        imul rax, rax, 24
        add rax, [elf_symtab]
        cmp word [rax + 6], 0           ; st_shndx: defined here
        je .import
        mov rax, [rax + 8]
        add rax, rbp
        clc
        jmp .out
.import:
        movzx ecx, byte [rax + 4]       ; st_info
        mov esi, [rax]                  ; st_name
        add rsi, [elf_strtab]
        mov rdi, c_exports
.export:
        cmp qword [rdi], 0
        je .none
        push rdi
        mov rdi, [rdi]
        call cstr_eq
        pop rdi
        je .found
        add rdi, 16
        jmp .export
.found:
        mov rax, [rdi + 8]
        clc
        jmp .out
.none:
        shr ecx, 4
        cmp ecx, 2                      ; STB_WEAK
        jne .missing
        xor eax, eax
        clc
        jmp .out
.missing:
        mov rax, rsi
        stc
.out:
        pop rcx
        pop rdi
        pop rsi
        ret

; --- the commands

; rdi = a value -> rax: it as an integer argument; CF when it is none
c_int_arg:
        cmp qword [rdi], T_INT
        je .int
        cmp qword [rdi], T_BOOL
        je .int
        cmp qword [rdi], T_NULL
        jne .bad
        xor eax, eax
        clc
        ret
.int:
        mov rax, [rdi + 8]
        clc
        ret
.bad:
        stc
        ret

; c_sym(f): lib name
bi_c_sym:
        cmp esi, 2
        jne .usage
        cmp qword [rdi], T_INT
        jne .usage
        cmp qword [rdi + 16], T_STR
        jne .usage
        mov rax, [rdi + 8]
        dec rax
        cmp rax, [clib_count]
        jae .none
        push rdx
        push rbx
        push rbp
        shl rax, 5
        lea rbx, [clib_table + rax]
        mov rbp, [rbx]                  ; base
        mov rsi, [rdi + 24]
        add rsi, STR_BYTES              ; the name wanted
        mov rcx, 1
.next:
        cmp rcx, [rbx + 24]
        jae .missing
        mov rax, rcx
        imul rax, rax, 24
        add rax, [rbx + 8]
        cmp word [rax + 6], 0
        je .skip
        mov edi, [rax]
        add rdi, [rbx + 16]
        call cstr_eq
        jne .skip
        mov rax, [rax + 8]
        add rax, rbp
        pop rbp
        pop rbx
        pop rdx
        jmp bi_int_out
.skip:
        inc rcx
        jmp .next
.missing:
        pop rbp
        pop rbx
        pop rdx
.none:
        mov qword [rdx], T_NULL
        xor eax, eax
        ret
.usage:
        mov eax, e_c_sym
        jmp bi_error

; c_call(r): f [args ...]
bi_c_call:
        test esi, esi
        jz .usage
        cmp esi, C_ARGS + 1
        ja .usage
        push rdx
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov rbx, rdi                    ; the values
        mov r12d, esi
        call c_int_arg
        jc .bad_fn
        test rax, rax
        jz .bad_fn
        mov r13, rax                    ; the function
        mov rax, [c_scratch]
        mov [c_scratch_top], rax
        ; the arguments as integers into c_args
        mov r14d, 1
.arg:
        cmp r14d, r12d
        jae .args_done
        mov rdi, r14
        shl rdi, 4
        add rdi, rbx
        cmp qword [rdi], T_STR
        je .string
        cmp qword [rdi], T_VEC
        je .vector
        call c_int_arg
        jc .bad_arg
        jmp .store
.string:
        mov rax, [rdi + 8]
        add rax, STR_BYTES              ; NUL-terminated already
        jmp .store
.vector:
        mov r8, [rdi + 8]
        mov rcx, [r8 + VEC_COUNT]
        mov rax, [c_scratch_top]
        lea r9, [rax + rcx + 16]
        mov r10, [c_scratch]
        add r10, C_SCRATCH
        cmp r9, r10
        ja .too_big
        and r9, -16
        mov [c_scratch_top], r9
        mov rsi, [r8 + VEC_ITEMS]
        xor edx, edx
.copy_in:
        cmp rdx, rcx
        jae .copied_in
        mov r9, [rsi + 8]
        mov [rax + rdx], r9b
        add rsi, 16
        inc rdx
        jmp .copy_in
.copied_in:
        mov byte [rax + rdx], 0
.store:
        mov [c_args + r14 * 8 - 8], rax
        inc r14d
        jmp .arg
.args_done:
        ; the call: six in registers, the rest on the stack, aligned
        mov rbp, rsp
        and rsp, -16
        sub rsp, 48                     ; c_args 7 .. 12
        mov rax, [c_args + 48]
        mov [rsp], rax
        mov rax, [c_args + 56]
        mov [rsp + 8], rax
        mov rax, [c_args + 64]
        mov [rsp + 16], rax
        mov rax, [c_args + 72]
        mov [rsp + 24], rax
        mov rax, [c_args + 80]
        mov [rsp + 32], rax
        mov rax, [c_args + 88]
        mov [rsp + 40], rax
        mov rdi, [c_args]
        mov rsi, [c_args + 8]
        mov rdx, [c_args + 16]
        mov rcx, [c_args + 24]
        mov r8, [c_args + 32]
        mov r9, [c_args + 40]
        cld
        call r13
        cld
        mov rsp, rbp
        mov r15, rax                    ; the result
        ; the buffers back into their vectors
        mov r14d, 1
.back:
        cmp r14d, r12d
        jae .returned
        mov rdi, r14
        shl rdi, 4
        add rdi, rbx
        cmp qword [rdi], T_VEC
        jne .back_next
        mov r8, [rdi + 8]
        mov rcx, [r8 + VEC_COUNT]
        mov rsi, [c_args + r14 * 8 - 8]
        mov r9, [r8 + VEC_ITEMS]
        xor edx, edx
.copy_out:
        cmp rdx, rcx
        jae .back_next
        movzx eax, byte [rsi + rdx]
        mov qword [r9], T_INT
        mov [r9 + 8], rax
        add r9, 16
        inc rdx
        jmp .copy_out
.back_next:
        inc r14d
        jmp .back
.returned:
        mov rax, r15
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop rdx
        jmp bi_int_out
.too_big:
        mov eax, e_c_big
        jmp .fail
.bad_arg:
        mov eax, e_c_arg
        jmp .fail
.bad_fn:
        mov eax, e_c_fn
.fail:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop rdx
        jmp bi_error
.usage:
        mov eax, e_c_call
        jmp bi_error

; c_alloc(p): n -> n zeroed bytes; a block c_free gave back is taken
; again when it is the same size
bi_c_alloc:
        cmp esi, 1
        jne .usage
        cmp qword [rdi], T_INT
        jne .usage
        mov rcx, [rdi + 8]
        test rcx, rcx
        js .usage
        ; a freed block of that size
        lea r8, [cfree_head]
.find:
        mov rax, [r8]
        test rax, rax
        jz .new
        cmp [rax], rcx
        je .reuse
        lea r8, [rax + 8]
        jmp .find
.reuse:
        mov r9, [rax + 8]
        mov [r8], r9                    ; out of the list
        push rdi
        push rcx
        push rax
        lea rdi, [rax + 16]
        xor eax, eax
        rep stosb
        pop rax
        pop rcx
        pop rdi
        add rax, 16
        jmp bi_int_out
.new:
        push rdi
        lea rdi, [rcx + 16]
        call alloc
        pop rdi
        mov [rax], rcx                  ; its size, before it
        add rax, 16
        jmp bi_int_out
.usage:
        mov eax, e_c_alloc
        jmp bi_error

; c_free(_): p -> 0; the block waits for a c_alloc of its size
bi_c_free:
        cmp esi, 1
        jne .done
        cmp qword [rdi], T_INT
        jne .done
        mov rax, [rdi + 8]
        test rax, rax
        jz .done
        sub rax, 16
        mov rcx, [cfree_head]
        mov [rax + 8], rcx
        mov [cfree_head], rax
.done:
        xor eax, eax
        jmp bi_int_out

; c_read(v): p n -> n bytes at p
bi_c_read:
        cmp esi, 2
        jne .usage
        cmp qword [rdi], T_INT
        jne .usage
        cmp qword [rdi + 16], T_INT
        jne .usage
        mov rsi, [rdi + 8]
        test rsi, rsi
        jz .usage
        mov rcx, [rdi + 24]
        push rdx
        push rsi
        push rcx
        call vec_new
        mov rdi, rax
        pop rcx
        pop rsi
        test rcx, rcx
        jz .made
        push rdi
        lea rdi, [rcx * 8 + 8]
        shl rdi, 1
        call alloc
        pop rdi
        mov [rdi + VEC_ITEMS], rax
        mov [rdi + VEC_CAP], rcx
        mov [rdi + VEC_COUNT], rcx
        xor edx, edx
.item:
        mov qword [rax], T_INT
        movzx r8d, byte [rsi + rdx]
        mov [rax + 8], r8
        add rax, 16
        inc rdx
        cmp rdx, rcx
        jb .item
.made:
        mov rax, rdi
        pop rdx
        jmp bi_vec_out
.usage:
        mov eax, e_c_read
        jmp bi_error

; c_write(n): p data -> the bytes written
bi_c_write:
        cmp esi, 2
        jne .usage
        cmp qword [rdi], T_INT
        jne .usage
        mov r8, [rdi + 8]
        test r8, r8
        jz .usage
        lea rsi, [rdi + 16]
        mov r9, -1                      ; any room
        xor ecx, ecx
        mov r10, -1
        push rdi
        call net_copy_bytes
        pop rdi
        cmp rax, -1
        je .usage
        jmp bi_int_out
.usage:
        mov eax, e_c_write
        jmp bi_error

; c_string(s): p -> the NUL-terminated string at p
bi_c_string:
        cmp esi, 1
        jne .null
        cmp qword [rdi], T_INT
        jne .null
        mov rsi, [rdi + 8]
        test rsi, rsi
        jz .null
        call new_string_z
        jmp bi_str_out
.null:
        mov qword [rdx], T_NULL
        xor eax, eax
        ret

e_elf_usage:    db "k_elf_load(lib): the bytes of an ELF shared object", 0
e_elf_bad:      db "k_elf_load: not an x86-64 ELF shared object", 0
e_elf_hash:     db "k_elf_load: the library has no DT_HASH (link it with --hash-style=sysv)", 0
e_elf_full:     db "k_elf_load: too many libraries loaded", 0
e_elf_reloc:    db "a relocation type the loader does not know", 0
e_elf_needs:    db "k_elf_load: the library needs '", 0
e_elf_needs2:   db "', which Moscow does not give", 10, 0
e_c_sym:        db "c_sym(f): lib name", 0
e_c_call:       db "c_call(r): f [up to 12 arguments]", 0
e_c_fn:         db "c_call: no function (address 0)", 0
e_c_arg:        db "c_call: an argument is an integer, a string, a byte vector, true, false or null", 0
e_c_big:        db "c_call: byte vectors over 1 MiB in all", 0
e_c_alloc:      db "c_alloc(p): n", 0
e_c_read:       db "c_read(v): p n", 0
e_c_write:      db "c_write(n): p data (a string or a byte vector)", 0
