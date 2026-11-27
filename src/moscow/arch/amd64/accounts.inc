; Fixed PBKDF2-HMAC-SHA256 for Moscow's account records.
; k_password_hash(hex): password salt (salt is 32 ASCII characters).
%define PASSWORD_ROUNDS 600000

bi_k_password_hash:
        cmp esi, 2
        jne .bad
        cmp qword [rdi], T_STR
        jne .bad
        cmp qword [rdi + 16], T_STR
        jne .bad
        mov rax, [rdi + 24]
        cmp qword [rax + STR_LEN], 32
        jne .bad
        mov rsi, [rdi + 8]
        mov rcx, [rsi + STR_LEN]
        cmp rcx, 128
        ja .bad
        add rsi, STR_BYTES
        push rdx
        push rbx
        push r12
        push r13
        sub rsp, 288
        ; Inner and outer HMAC pads, followed by their respective data.
        mov r12, rsp
        lea r13, [rsp + 112]
        mov rbx, rax
        mov rdi, r12
        xor eax, eax
        push rcx
        mov ecx, 36
        rep stosq
        pop rcx
        cmp rcx, 64
        jbe .key
        call sha256_bytes
        mov rdi, r12
        call .digest
        mov ecx, 32
        jmp .pads
.key:
        mov rdi, r12
        rep movsb
.pads:
        xor ecx, ecx
.pad:
        mov al, [r12 + rcx]
        xor al, 0x5c
        mov [r13 + rcx], al
        xor byte [r12 + rcx], 0x36
        inc ecx
        cmp ecx, 64
        jb .pad
        lea rdi, [r12 + 64]
        lea rsi, [rbx + STR_BYTES]
        mov ecx, 4
        rep movsq
        mov dword [r12 + 96], 0x01000000
        mov ebx, PASSWORD_ROUNDS
        mov ecx, 100
.round:
        mov rsi, r12
        call sha256_bytes
        lea rdi, [r13 + 64]
        call .digest
        mov rsi, r13
        mov ecx, 96
        call sha256_bytes
        lea rdi, [r12 + 64]
        call .digest
        xor ecx, ecx
.mix:
        mov rax, [r12 + 64 + rcx]
        xor [r12 + 208 + rcx], rax
        add ecx, 8
        cmp ecx, 32
        jb .mix
        mov ecx, 96
        dec ebx
        jnz .round
        xor ecx, ecx
.hex:
        movzx eax, byte [r12 + 208 + rcx]
        mov edx, eax
        shr eax, 4
        and edx, 15
        mov al, [account_hex + rax]
        mov dl, [account_hex + rdx]
        mov [r12 + rcx * 2], al
        mov [r12 + rcx * 2 + 1], dl
        inc ecx
        cmp ecx, 32
        jb .hex
        mov rsi, r12
        mov ecx, 64
        call new_string
        mov rdx, rax
        mov rdi, r12
        xor eax, eax
        mov ecx, 36
        rep stosq
        mov rax, rdx
        add rsp, 288
        pop r13
        pop r12
        pop rbx
        pop rdx
        jmp bi_str_out
.digest:
        xor ecx, ecx
.word:
        mov eax, [sha_state + rcx * 4]
        bswap eax
        mov [rdi + rcx * 4], eax
        inc ecx
        cmp ecx, 8
        jb .word
        ret
.bad:
        mov eax, e_password_hash
        jmp bi_error

; Only the Moscow program can change the current console's privilege.
bi_k_account_admin:
        cmp r13, [kernel_program]
        jne account_denied
        cmp esi, 1
        jne account_denied
        cmp qword [rdi], T_BOOL
        jne account_denied
        mov rax, [rdi + 8]
        mov [account_admin], al
        xor eax, eax
        ret

account_denied:
        mov eax, e_account_denied
        jmp bi_error

; Check builtin or syscall permission at the VM boundary. Keep all regs.
; rdi = NUL-terminated name, rsi = packed allowlist, CF set when denied.
account_allowed:
        cmp byte [account_admin], 0
        jne .yes
        cmp r13, [kernel_program]
        je .yes
        push rax
        push rcx
        push rsi
        push rdi
        mov rax, rsi
.next:
        cmp byte [rax], 0
        je .no
        mov rsi, rdi
        call strlen_z
        push rdi
        mov rdi, rax
        call bytes_eq_z
        pop rdi
        je .matched
.skip:
        cmp byte [rax], 0
        lea rax, [rax + 1]
        jne .skip
        jmp .next
.matched:
        clc
        jmp .restore
.no:
        stc
.restore:
        pop rdi
        pop rsi
        pop rcx
        pop rax
        ret
.yes:
        clc
        ret

; The ordinary console can run VM code and access files through Moscow.
; Kernel file commands enforce writable paths.
account_builtins:
        db "at",0,"byte",0,"chr",0,"len",0,"slice",0,"vec",0,"push",0,"pop",0
        db "encode",0,"decode",0,"typeof",0,"struct",0,"new",0,"getf",0,"setf",0
        db "btree",0,"btget",0,"btput",0,"bthas",0,"btdel",0,"btlen",0
        db "btfirst",0,"btlast",0,"btfloor",0,"btceil",0,"btnext",0,"btprev",0
        db "btrange",0,"btkeys",0,"write_stderr",0,"fail",0,"try",0
        db "text_bytes",0,"bytes_text",0,"sha256",0,"crc32",0
        db "re_test",0,"re_find",0,"re_pos",0,"re_all",0,"re_replace",0,"re_split",0,"k_re_valid",0
        db "keybind_match",0,"getenv",0,"setenv",0,"unsetenv",0
        db "terminal_getch",0,"terminal_raw",0,"terminal_sane",0,"terminal_size",0
        db "terminal_key_ready",0,"terminal_escape",0,"k_prompt",0,"sleep",0
        db "k_shell_eval",0,"k_boot_log",0,"uptime",0,"uname",0,"pwd",0,"clear",0,"cpu",0,"mem",0
        db 0

account_syscalls:
        db "permissions",0,"run",0,"shell_run",0,"runtime",0,"whoami",0,"account_identity",0,"account_register",0,"help",0
        db "getcwd",0,"cd",0,"china_chdir",0,"china_path",0,"china_read",0,"china_stat",0
        db "china_du",0,"china_mount",0,"df",0,"ls",0,"cat",0,"tree",0,"readfile",0
        db "read_bytes",0,"file_exists",0,"is_dir",0,"list_dir",0,"localtime",0,"tasks",0
        db "writefile",0,"write_bytes",0,"make_dir",0,"remove_dir",0,"remove_file",0
        db "copy_file",0,"rename_path",0,"china_copy",0
        db "open_file",0,"close_file",0,"read_line",0,"read_file",0,"write_file",0
        db 0

account_hex: db "0123456789abcdef"
n_k_password_hash: db "k_password_hash", 0
n_k_account_admin: db "k_account_admin", 0
e_password_hash: db "k_password_hash(hex): password salt", 0
e_account_denied: db "command requires a master admin session", 0
