; bytes.inc - byte vectors as buffers, for the kernel's TCP and TLS
; (ussr_rt.nasm includes it). A USSR string holds no byte 0, so binary
; data - TLS records - travels in vectors of the integers 0..255; these
; keep such a vector a buffer in native time, where a USSR loop would
; push and copy byte by byte.
;
;   k_bytes_append(v): v data [from [count]]
;                                appends a string's or a byte vector's
;                                bytes (or a part of them) to v; v
;   k_bytes_cut(v): v from count a new vector of those items of v
;   k_bytes_drop(v): v n         takes the first n items off v; v

; rdi = vector, rcx = the items it must hold -> room for them, the
; capacity doubled as needed. Keeps registers.
vec_reserve:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        mov r8, rdi
        mov rdx, [r8 + VEC_CAP]
        cmp rcx, rdx
        jbe .done
        test rdx, rdx
        jnz .double
        mov edx, 8
.double:
        cmp rdx, rcx
        jae .grow
        shl rdx, 1
        jmp .double
.grow:
        mov [r8 + VEC_CAP], rdx
        mov rdi, rdx
        shl rdi, 4
        call alloc
        mov rsi, [r8 + VEC_ITEMS]
        mov rdi, rax
        mov rcx, [r8 + VEC_COUNT]
        shl rcx, 4
        rep movsb
        mov [r8 + VEC_ITEMS], rax
.done:
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; k_bytes_append(v): v data [from [count]]
bi_k_bytes_append:
        cmp esi, 2
        jb .usage
        cmp esi, 4
        ja .usage
        cmp qword [rdi], T_VEC
        jne .usage
        push rdx
        push rbx
        push r12
        mov r11d, esi                   ; the count of arguments
        mov rbx, [rdi + 8]              ; the vector
        mov r12, rdi                    ; the arguments
        ; the source: bytes at r8, r9 apart; r10 of them
        lea rsi, [rdi + 16]
        cmp qword [rsi], T_STR
        je .string
        cmp qword [rsi], T_VEC
        jne .bad
        mov rax, [rsi + 8]
        mov r10, [rax + VEC_COUNT]
        mov r8, [rax + VEC_ITEMS]
        add r8, 8
        mov r9d, 16
        jmp .range
.string:
        mov rax, [rsi + 8]
        mov r10, [rax + STR_LEN]
        lea r8, [rax + STR_BYTES]
        mov r9d, 1
.range:
        xor ecx, ecx                    ; from
        cmp r11d, 3
        jb .from
        cmp qword [r12 + 32], T_INT
        jne .bad
        mov rcx, [r12 + 40]
        cmp rcx, r10
        ja .bad
.from:
        sub r10, rcx
        imul rcx, r9
        add r8, rcx
        cmp r11d, 4
        jb .counted
        cmp qword [r12 + 48], T_INT
        jne .bad
        mov rax, [r12 + 56]
        cmp rax, r10
        ja .bad
        mov r10, rax
.counted:
        mov rcx, [rbx + VEC_COUNT]
        add rcx, r10
        mov rdi, rbx
        call vec_reserve
        mov rdi, [rbx + VEC_COUNT]
        shl rdi, 4
        add rdi, [rbx + VEC_ITEMS]
        add [rbx + VEC_COUNT], r10
.copy:
        test r10, r10
        jz .done
        movzx eax, byte [r8]
        mov qword [rdi], T_INT
        mov [rdi + 8], rax
        add rdi, 16
        add r8, r9
        dec r10
        jmp .copy
.done:
        mov rax, rbx
        pop r12
        pop rbx
        pop rdx
        jmp bi_vec_out
.bad:
        pop r12
        pop rbx
        pop rdx
.usage:
        mov eax, e_bytes_append
        jmp bi_error

; k_bytes_cut(v): v from count
bi_k_bytes_cut:
        cmp esi, 3
        jne .usage
        call byte_range
        jc .usage
        push rdx
        push rcx
        push r8
        call vec_new
        mov rdi, rax
        pop r8
        pop rcx
        call vec_reserve
        mov [rdi + VEC_COUNT], rcx
        mov rsi, r8
        push rdi
        mov rdi, [rdi + VEC_ITEMS]
        shl rcx, 4
        rep movsb
        pop rax
        pop rdx
        jmp bi_vec_out
.usage:
        mov eax, e_bytes_cut
        jmp bi_error

; k_bytes_drop(v): v n
bi_k_bytes_drop:
        cmp esi, 2
        jne .usage
        cmp qword [rdi], T_VEC
        jne .usage
        cmp qword [rdi + 16], T_INT
        jne .usage
        mov r8, [rdi + 8]
        mov rcx, [rdi + 24]
        mov rax, [r8 + VEC_COUNT]
        cmp rcx, rax
        jbe .take
        mov rcx, rax
.take:
        sub rax, rcx                    ; what stays
        mov [r8 + VEC_COUNT], rax
        push rdi
        mov rdi, [r8 + VEC_ITEMS]
        mov rsi, rcx
        shl rsi, 4
        add rsi, rdi
        mov rcx, rax
        shl rcx, 4
        rep movsb
        pop rdi
        mov rax, r8
        jmp bi_vec_out
.usage:
        mov eax, e_bytes_drop
        jmp bi_error

e_bytes_append: db "k_bytes_append(v): v data [from [count]] (data a string or a byte vector)", 0
e_bytes_cut:    db "k_bytes_cut(v): v from count", 0
e_bytes_drop:   db "k_bytes_drop(v): v n", 0
