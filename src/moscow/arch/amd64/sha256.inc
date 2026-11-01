; sha256.inc - sha256(v): data for the Moscow runtime (ussr_rt.nasm
; includes it): the SHA-256 of a string or a byte vector, as a vector of
; 32 bytes, like ussr's. Stalin hashes with it to make GUIDs and disk
; signatures.

; rsi = bytes, rcx = length -> sha_state: the digest (8 dwords). Keeps
; rbx, rbp, r12 .. r15.
sha256_bytes:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov r12, rsi
        mov r13, rcx
        ; the initial state
        mov rsi, sha_init
        mov rdi, sha_state
        mov ecx, 8
        rep movsd
        mov r14, r13                    ; bytes left of the message
        mov r15, r12
.block:
        ; the next 64 bytes, with the padding where the message ends
        cmp r14, 64
        jb .last
        mov rsi, r15
        call sha_compress
        add r15, 64
        sub r14, 64
        jmp .block
.last:
        mov rdi, sha_block
        xor eax, eax
        mov ecx, 128
        rep stosb
        mov rsi, r15
        mov rdi, sha_block
        mov rcx, r14
        rep movsb
        mov byte [sha_block + r14], 0x80
        mov rax, r13
        shl rax, 3                      ; the length in bits, big-endian
        bswap rax
        mov ebx, 56
        cmp r14, 56
        jb .one
        mov ebx, 120
.one:
        mov [sha_block + rbx], rax
        mov rsi, sha_block
        call sha_compress
        cmp ebx, 120
        jne .done
        mov rsi, sha_block + 64
        call sha_compress
.done:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; rsi = 64 bytes: one round of compression into sha_state
sha_compress:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        ; the schedule w[0..63]
        xor ecx, ecx
.load:
        mov eax, [rsi + rcx * 4]
        bswap eax
        mov [sha_w + rcx * 4], eax
        inc ecx
        cmp ecx, 16
        jb .load
.extend:
        mov eax, [sha_w + rcx * 4 - 15 * 4]
        mov edx, eax
        ror edx, 7
        mov ebx, eax
        ror ebx, 18
        xor edx, ebx
        shr eax, 3
        xor edx, eax                    ; s0
        mov eax, [sha_w + rcx * 4 - 2 * 4]
        mov ebx, eax
        ror ebx, 17
        mov ebp, eax
        ror ebp, 19
        xor ebx, ebp
        shr eax, 10
        xor ebx, eax                    ; s1
        add edx, ebx
        add edx, [sha_w + rcx * 4 - 16 * 4]
        add edx, [sha_w + rcx * 4 - 7 * 4]
        mov [sha_w + rcx * 4], edx
        inc ecx
        cmp ecx, 64
        jb .extend
        ; a .. h in r8d .. r15d
        mov r8d, [sha_state]
        mov r9d, [sha_state + 4]
        mov r10d, [sha_state + 8]
        mov r11d, [sha_state + 12]
        mov r12d, [sha_state + 16]
        mov r13d, [sha_state + 20]
        mov r14d, [sha_state + 24]
        mov r15d, [sha_state + 28]
        xor ecx, ecx
.round:
        ; t1 = h + S1(e) + ch(e, f, g) + k[i] + w[i]
        mov eax, r12d
        ror eax, 6
        mov ebx, r12d
        ror ebx, 11
        xor eax, ebx
        mov ebx, r12d
        ror ebx, 25
        xor eax, ebx
        mov ebx, r12d
        and ebx, r13d
        mov edx, r12d
        not edx
        and edx, r14d
        xor ebx, edx
        add eax, ebx
        add eax, r15d
        add eax, [sha_k + rcx * 4]
        add eax, [sha_w + rcx * 4]      ; t1
        ; t2 = S0(a) + maj(a, b, c)
        mov ebx, r8d
        ror ebx, 2
        mov edx, r8d
        ror edx, 13
        xor ebx, edx
        mov edx, r8d
        ror edx, 22
        xor ebx, edx
        mov edx, r8d
        and edx, r9d
        mov ebp, r8d
        and ebp, r10d
        xor edx, ebp
        mov ebp, r9d
        and ebp, r10d
        xor edx, ebp
        add ebx, edx                    ; t2
        mov r15d, r14d
        mov r14d, r13d
        mov r13d, r12d
        lea r12d, [r11d + eax]
        mov r11d, r10d
        mov r10d, r9d
        mov r9d, r8d
        lea r8d, [eax + ebx]
        inc ecx
        cmp ecx, 64
        jb .round
        add [sha_state], r8d
        add [sha_state + 4], r9d
        add [sha_state + 8], r10d
        add [sha_state + 12], r11d
        add [sha_state + 16], r12d
        add [sha_state + 20], r13d
        add [sha_state + 24], r14d
        add [sha_state + 28], r15d
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; sha256(v): data -> 32 bytes
bi_sha256:
        cmp esi, 1
        jne .bad
        push rdx
        cmp qword [rdi], T_STR
        je .string
        cmp qword [rdi], T_VEC
        jne .bad_pop
        ; a byte vector: its bytes into a buffer first
        mov r8, [rdi + 8]
        mov rcx, [r8 + VEC_COUNT]
        push rcx
        lea rdi, [rcx + 1]
        call alloc
        pop rcx
        mov rsi, [r8 + VEC_ITEMS]
        xor edx, edx
.pack:
        cmp rdx, rcx
        jae .packed
        mov r9, [rsi + 8]
        mov [rax + rdx], r9b
        add rsi, 16
        inc rdx
        jmp .pack
.packed:
        mov rsi, rax
        jmp .hash
.string:
        mov rsi, [rdi + 8]
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
.hash:
        call sha256_bytes
        call vec_new
        mov rdi, rax
        xor ecx, ecx
.out:
        movzx eax, byte [sha_state + rcx]
        ; the state is dwords: each one big-endian
        mov edx, ecx
        xor edx, 3
        movzx eax, byte [sha_state + rdx]
        call vec_push_int
        inc ecx
        cmp ecx, 32
        jb .out
        mov rax, rdi
        pop rdx
        jmp bi_vec_out
.bad_pop:
        pop rdx
.bad:
        mov eax, e_sha256
        jmp bi_error

        align 4
sha_init:
        dd 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a
        dd 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
sha_k:
        dd 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
        dd 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
        dd 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
        dd 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
        dd 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
        dd 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
        dd 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
        dd 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
        dd 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
        dd 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
        dd 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
        dd 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
        dd 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
        dd 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
        dd 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
        dd 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

n_sha256:       db "sha256", 0
e_sha256:       db "sha256(v): a string or a byte vector", 0
