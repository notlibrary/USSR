; regex.inc - regular expressions for the Moscow runtime (ussr_rt.nasm includes
; it): re_test, re_find, re_pos, re_all, re_replace and re_split, the same engine
; as src/regex.c, which says how it works; tests/regex_test.su holds them to one
; behaviour. A Pike VM: a search takes time proportional to pattern size times
; text size, whatever the pattern, so no pattern hangs the kernel. Text is UTF-8,
; offsets are bytes. A command runs to its end (the timer does not switch tasks
; inside one), so the engine's state is plain variables.
;
; A parse error unwinds: re_die puts the stack back as re_compile found it.

RE_MAX_CODE     equ 4096
RE_MAX_RANGES   equ 4096
RE_MAX_REPEAT   equ 1000
RE_MAX_DEPTH    equ 100
RE_MAX_GROUPS   equ 32
RE_MAX_CHAR     equ 0x10ffff
RE_BAD_BYTE     equ 0x110000
RE_MEM_MAX      equ 12
RE_INST         equ 16                  ; dwords: op, x, y, z

OP_RE_CHAR      equ 0
OP_RE_CHARI     equ 1
OP_RE_ANY       equ 2
OP_RE_ANYNL     equ 3
OP_RE_CLASS     equ 4
OP_RE_CLASSI    equ 5
OP_RE_SPLIT     equ 6
OP_RE_JMP       equ 7
OP_RE_SAVE      equ 8
OP_RE_BOL       equ 9
OP_RE_EOL       equ 10
OP_RE_MBOL      equ 11
OP_RE_MEOL      equ 12
OP_RE_WORDB     equ 13
OP_RE_NWORDB    equ 14
OP_RE_MATCH     equ 15

; --- memory: everything goes back when the command ends

; rdi = bytes -> rax: zeroed memory, kept on the list. Keeps the others.
re_alloc:
        call alloc
        push rcx
        mov rcx, [re_mem_n]
        mov [re_mem + rcx * 8], rax
        inc qword [re_mem_n]
        pop rcx
        ret

re_free_all:
        push rax
        push rsi
        push rdi
        mov rdi, [cur_pool]
.next:
        mov rax, [re_mem_n]
        test rax, rax
        jz .done
        dec rax
        mov [re_mem_n], rax
        mov rsi, [re_mem + rax * 8]
        call pool_free
        jmp .next
.done:
        pop rdi
        pop rsi
        pop rax
        ret

; rax = why: the pattern does not compile
re_die:
        mov [re_err], rax
        mov rsp, [re_sp]
        stc
        jmp re_pops                     ; pops leave CF as it is

; --- characters

; rsi = bytes, rdi = their end -> eax: the character at rsi, rcx its bytes. Keeps
; the others.
re_decode:
        push rbx
        push rdx
        movzx eax, byte [rsi]
        mov ecx, 1
        cmp eax, 0x80
        jb .out
        mov ebx, eax                    ; the first byte
        cmp eax, 0xc2
        jb .bad
        cmp eax, 0xdf
        ja .three
        mov edx, 1
        and eax, 0x1f
        jmp .rest
.three:
        cmp eax, 0xef
        ja .four
        mov edx, 2
        and eax, 0x0f
        jmp .rest
.four:
        cmp eax, 0xf4
        ja .bad
        mov edx, 3
        and eax, 0x07
.rest:
        ; need = edx bytes after this one
        push rax
        mov rax, rdi
        sub rax, rsi
        cmp rax, rdx
        pop rax
        jle .bad
        mov ecx, 1
.more:
        cmp ecx, edx
        ja .whole
        push rcx
        movzx ecx, byte [rsi + rcx]
        push rcx
        and ecx, 0xc0
        cmp ecx, 0x80
        pop rcx
        jne .bad_pop
        and ecx, 0x3f
        shl eax, 6
        or eax, ecx
        pop rcx
        inc ecx
        jmp .more
.whole:
        cmp edx, 2
        jne .not_three
        cmp eax, 0x800
        jb .bad
.not_three:
        cmp edx, 3
        jne .sized
        cmp eax, 0x10000
        jb .bad
        cmp eax, RE_MAX_CHAR
        ja .bad
.sized:
        lea ecx, [rdx + 1]
.out:
        pop rdx
        pop rbx
        ret
.bad_pop:
        pop rcx
.bad:
        lea eax, [rbx + RE_BAD_BYTE]
        mov ecx, 1
        pop rdx
        pop rbx
        ret

; eax = a byte -> CF clear when it is a word character. Keeps every register.
re_is_word:
        cmp eax, '0'
        jb .no
        cmp eax, '9'
        jbe .yes
        cmp eax, 'A'
        jb .no
        cmp eax, 'Z'
        jbe .yes
        cmp eax, '_'
        je .yes
        cmp eax, 'a'
        jb .no
        cmp eax, 'z'
        jbe .yes
.no:
        stc
        ret
.yes:
        clc
        ret

; --- making code

; eax = op, ecx = x, edx = y, r8d = z -> eax: where it went
re_emit:
        mov r9, [re_len]
        cmp r9, RE_MAX_CODE
        jae .big
        push rbx
        mov rbx, r9
        shl rbx, 4
        add rbx, [re_code]
        mov [rbx], eax
        mov [rbx + 4], ecx
        mov [rbx + 8], edx
        mov [rbx + 12], r8d
        pop rbx
        inc qword [re_len]
        mov rax, r9
        ret
.big:
        mov eax, e_re_big
        jmp re_die

; r9 = where, eax = op, ecx = x, edx = y: room for it there, the code after it
; up one: its jumps are relative and stay inside it
re_insert:
        mov r10, [re_len]
        cmp r10, RE_MAX_CODE
        jae re_emit.big
        push rax
        push rcx
        mov r11, [re_code]
        ; from the top down
.move:
        cmp r10, r9
        jbe .moved
        dec r10
        mov rcx, r10
        shl rcx, 4
        mov rax, [r11 + rcx]
        mov [r11 + rcx + RE_INST], rax
        mov rax, [r11 + rcx + 8]
        mov [r11 + rcx + RE_INST + 8], rax
        jmp .move
.moved:
        inc qword [re_len]
        pop rcx
        pop rax
        mov r10, r9
        shl r10, 4
        mov [r11 + r10], eax
        mov [r11 + r10 + 4], ecx
        mov [r11 + r10 + 8], edx
        mov dword [r11 + r10 + 12], 0
        ret

; rax = from, rcx = to: the code between them again, at the end
re_copy:
        mov rdx, rcx
        sub rdx, rax
        mov r9, [re_len]
        lea r10, [r9 + rdx]
        cmp r10, RE_MAX_CODE
        ja re_emit.big
        push rsi
        push rdi
        mov rcx, rdx
        shl rcx, 4
        mov rsi, rax
        shl rsi, 4
        add rsi, [re_code]
        mov rdi, r9
        shl rdi, 4
        add rdi, [re_code]
        rep movsb
        pop rdi
        pop rsi
        mov [re_len], r10
        ret

; eax = low, ecx = high
re_add_range:
        mov r9, [re_nranges]
        cmp r9, RE_MAX_RANGES
        jae re_emit.big
        push rbx
        mov rbx, [re_ranges]
        mov [rbx + r9 * 8], eax
        mov [rbx + r9 * 8 + 4], ecx
        pop rbx
        inc qword [re_nranges]
        ret

; al = a letter: its shorthand's ranges into the class being made. CF clear when
; it was one.
re_shorthand:
        mov ecx, re_sh_table
.find:
        cmp byte [rcx], 0
        je .no
        cmp [rcx], al
        je .found
        add rcx, 16
        jmp .find
.no:
        stc
        ret
.found:
        push rbx
        push r12
        mov rbx, [rcx + 8]              ; pairs, ended by -1
.pair:
        mov eax, [rbx]
        cmp eax, -1
        je .end
        mov ecx, [rbx + 4]
        call re_add_range
        add rbx, 8
        jmp .pair
.end:
        pop r12
        pop rbx
        clc
        ret

; cl = a hex digit -> eax, -1 when it is none. Keeps rcx.
re_hex:
        movzx eax, cl
        cmp eax, '0'
        jb .none
        cmp eax, '9'
        ja .letter
        sub eax, '0'
        ret
.letter:
        or eax, 0x20
        cmp eax, 'a'
        jb .none
        cmp eax, 'f'
        ja .none
        sub eax, 'a' - 10
        ret
.none:
        mov eax, -1
        ret

; the character after a backslash, re_p at it -> eax
re_escape:
        mov rsi, [re_p]
        movzx eax, byte [rsi]
        test eax, eax
        jnz .some
        mov eax, e_re_trail
        jmp re_die
.some:
        cmp eax, '1'
        jb .letters
        cmp eax, '9'
        ja .letters
        mov eax, e_re_backref
        jmp re_die
.letters:
        cmp eax, '0'
        je .known
        mov ecx, eax
        or ecx, 0x20
        cmp ecx, 'a'
        jb .other
        cmp ecx, 'z'
        ja .other
.known:
        inc qword [re_p]
        cmp eax, 'n'
        je .nl
        cmp eax, 't'
        je .tab
        cmp eax, 'r'
        je .cr
        cmp eax, 'f'
        je .ff
        cmp eax, 'v'
        je .vt
        cmp eax, 'x'
        je .hex
        mov eax, e_re_escape
        jmp re_die
.nl:
        mov eax, 10
        ret
.tab:
        mov eax, 9
        ret
.cr:
        mov eax, 13
        ret
.ff:
        mov eax, 12
        ret
.vt:
        mov eax, 11
        ret
.hex:
        mov rsi, [re_p]
        mov cl, [rsi]
        call re_hex
        cmp eax, -1
        je .bad_hex
        mov edx, eax
        mov cl, [rsi + 1]
        test cl, cl
        jz .bad_hex
        call re_hex
        cmp eax, -1
        je .bad_hex
        shl edx, 4
        add eax, edx
        add qword [re_p], 2
        ret
.bad_hex:
        mov eax, e_re_hex
        jmp re_die
.other:
        mov rdi, [re_pend]
        call re_decode
        add [re_p], rcx
        ret

; a plain character at re_p -> eax, re_p past it
re_take_char:
        mov rsi, [re_p]
        mov rdi, [re_pend]
        call re_decode
        add [re_p], rcx
        ret

; [...], re_p after the [
re_parse_class:
        push rbx
        push r12
        push r13
        mov rbx, [re_nranges]           ; the first range
        xor r12d, r12d                  ; negated
        xor r13d, r13d                  ; any member yet
        mov rsi, [re_p]
        cmp byte [rsi], '^'
        jne .member
        mov r12d, 1
        inc qword [re_p]
.member:
        mov rsi, [re_p]
        movzx eax, byte [rsi]
        test eax, eax
        jnz .open
        mov eax, e_re_bracket
        jmp re_die
.open:
        cmp eax, ']'
        jne .take
        test r13d, r13d
        jz .take
        inc qword [re_p]
        jmp .closed
.take:
        mov r13d, 1
        cmp eax, '\'
        jne .plain
        inc qword [re_p]
        inc rsi
        mov al, [rsi]
        push rsi
        call re_shorthand
        pop rsi
        jc .not_short
        inc qword [re_p]
        jmp .member
.not_short:
        cmp byte [rsi], 'b'
        jne .escaped
        inc qword [re_p]
        mov eax, 8
        jmp .low
.escaped:
        call re_escape
        jmp .low
.plain:
        call re_take_char
.low:
        mov rsi, [re_p]
        cmp byte [rsi], '-'
        jne .single
        cmp byte [rsi + 1], ']'
        je .single
        cmp byte [rsi + 1], 0
        je .single
        push rax
        inc qword [re_p]
        inc rsi
        cmp byte [rsi], '\'
        jne .high_plain
        inc qword [re_p]
        call re_escape
        jmp .high
.high_plain:
        call re_take_char
.high:
        mov ecx, eax
        pop rax
        cmp ecx, eax
        jge .range
        mov eax, e_re_range
        jmp re_die
.range:
        call re_add_range
        jmp .member
.single:
        mov ecx, eax
        call re_add_range
        jmp .member
.closed:
        mov eax, OP_RE_CLASS
        cmp qword [re_icase], 0
        je .emit
        mov eax, OP_RE_CLASSI
.emit:
        mov ecx, ebx
        mov rdx, [re_nranges]
        sub rdx, rbx
        mov r8d, r12d
        call re_emit
        pop r13
        pop r12
        pop rbx
        ret

; op eax, no operands
re_emit0:
        xor ecx, ecx
        xor edx, edx
        xor r8d, r8d
        jmp re_emit

; one atom's code
re_parse_atom:
        mov rsi, [re_p]
        movzx eax, byte [rsi]
        cmp eax, '('
        je .group
        cmp eax, '['
        je .class
        cmp eax, '.'
        je .dot
        cmp eax, '^'
        je .bol
        cmp eax, '$'
        je .eol
        cmp eax, '*'
        je .nothing
        cmp eax, '+'
        je .nothing
        cmp eax, '?'
        je .nothing
        cmp eax, '\'
        je .backslash
        call re_take_char
        jmp .literal
.nothing:
        mov eax, e_re_repeat
        jmp re_die
.class:
        inc qword [re_p]
        jmp re_parse_class
.dot:
        inc qword [re_p]
        mov eax, OP_RE_ANY
        cmp qword [re_dotall], 0
        je re_emit0
        mov eax, OP_RE_ANYNL
        jmp re_emit0
.bol:
        inc qword [re_p]
        mov eax, OP_RE_BOL
        cmp qword [re_multi], 0
        je re_emit0
        mov eax, OP_RE_MBOL
        jmp re_emit0
.eol:
        inc qword [re_p]
        mov eax, OP_RE_EOL
        cmp qword [re_multi], 0
        je re_emit0
        mov eax, OP_RE_MEOL
        jmp re_emit0
.backslash:
        inc qword [re_p]
        inc rsi
        movzx eax, byte [rsi]
        cmp eax, 'b'
        jne .not_b
        inc qword [re_p]
        mov eax, OP_RE_WORDB
        jmp re_emit0
.not_b:
        cmp eax, 'B'
        jne .not_nb
        inc qword [re_p]
        mov eax, OP_RE_NWORDB
        jmp re_emit0
.not_nb:
        push rbx
        mov rbx, [re_nranges]
        push rsi
        call re_shorthand
        pop rsi
        jc .no_short
        inc qword [re_p]
        mov eax, OP_RE_CLASS            ; no case to fold in these
        mov ecx, ebx
        mov rdx, [re_nranges]
        sub rdx, rbx
        xor r8d, r8d
        pop rbx
        jmp re_emit
.no_short:
        pop rbx
        call re_escape
.literal:
        cmp qword [re_icase], 0
        jne .folded
        mov ecx, eax
        mov eax, OP_RE_CHAR
        xor edx, edx
        xor r8d, r8d
        jmp re_emit
.folded:
        cmp eax, 'A'
        jb .lower
        cmp eax, 'Z'
        ja .lower
        add eax, 32
.lower:
        mov ecx, eax
        mov eax, OP_RE_CHARI
        xor edx, edx
        xor r8d, r8d
        jmp re_emit
.group:
        push rbx
        inc qword [re_p]
        inc rsi
        xor ebx, ebx                    ; the group's number; 0: none
        cmp byte [rsi], '?'
        jne .capture
        cmp byte [rsi + 1], ':'
        je .plain_group
        mov eax, e_re_question
        jmp re_die
.plain_group:
        add qword [re_p], 2
        jmp .inside
.capture:
        mov rax, [re_groups]
        cmp rax, RE_MAX_GROUPS
        jb .count
        mov eax, e_re_groups
        jmp re_die
.count:
        inc rax
        mov [re_groups], rax
        mov rbx, rax
        lea ecx, [rax * 2]
        mov eax, OP_RE_SAVE
        xor edx, edx
        xor r8d, r8d
        call re_emit
.inside:
        inc qword [re_depth]
        cmp qword [re_depth], RE_MAX_DEPTH
        jbe .deep_ok
        mov eax, e_re_big
        jmp re_die
.deep_ok:
        call re_parse_alternatives
        dec qword [re_depth]
        mov rsi, [re_p]
        cmp byte [rsi], ')'
        je .close
        mov eax, e_re_paren
        jmp re_die
.close:
        inc qword [re_p]
        test rbx, rbx
        jz .group_out
        lea ecx, [rbx * 2 + 1]
        mov eax, OP_RE_SAVE
        xor edx, edx
        xor r8d, r8d
        call re_emit
.group_out:
        pop rbx
        ret

; {n}, {n,} or {n,m} at re_p -> CF clear, r12 = n, r13 = m (-1: no limit), re_p
; past it; CF set when it is not one
re_parse_counts:
        mov rsi, [re_p]
        inc rsi
        movzx eax, byte [rsi]
        cmp eax, '0'
        jb .not
        cmp eax, '9'
        ja .not
        xor r10d, r10d
.low:
        movzx eax, byte [rsi]
        cmp eax, '0'
        jb .low_done
        cmp eax, '9'
        ja .low_done
        imul r10, r10, 10
        lea r10, [r10 + rax - '0']
        inc rsi
        cmp r10, RE_MAX_REPEAT
        ja .bad
        jmp .low
.low_done:
        cmp eax, '}'
        jne .comma
        mov r11, r10
        jmp .done
.comma:
        cmp eax, ','
        jne .not
        inc rsi
        mov r11, -1
        movzx eax, byte [rsi]
        cmp eax, '}'
        je .done
        cmp eax, '0'
        jb .not
        cmp eax, '9'
        ja .not
        xor r11d, r11d
.high:
        movzx eax, byte [rsi]
        cmp eax, '0'
        jb .high_done
        cmp eax, '9'
        ja .high_done
        imul r11, r11, 10
        lea r11, [r11 + rax - '0']
        inc rsi
        cmp r11, RE_MAX_REPEAT
        ja .bad
        jmp .high
.high_done:
        cmp r11, r10
        jb .bad
        cmp eax, '}'
        jne .not
.done:
        inc rsi
        mov [re_p], rsi
        mov r12, r10
        mov r13, r11
        clc
        ret
.not:
        stc
        ret
.bad:
        mov eax, e_re_counts
        jmp re_die

; rbx = the atom's start, r12 = n, r13 = m (-1: no limit), r14 = lazy: the atom
; repeated. Uses r15.
re_repeat:
        mov r15, [re_len]
        sub r15, rbx                    ; its size
        test r12, r12
        jnz .some
        test r13, r13
        jnz .some
        mov [re_len], rbx
        ret
.some:
        ; n copies
        mov rbp, 1
.copies:
        cmp rbp, r12
        jae .copied
        mov rax, rbx
        lea rcx, [rbx + r15]
        call re_copy
        inc rbp
        jmp .copies
.copied:
        cmp r13, -1
        jne .limited
        test r12, r12
        jnz .plus
        ; L1: split L2 L3; L2: atom; jmp L1; L3:
        mov r9, rbx
        mov ecx, 1
        lea edx, [r15 + 2]
        test r14, r14
        jz .star
        xchg ecx, edx
.star:
        mov eax, OP_RE_SPLIT
        call re_insert
        mov eax, OP_RE_JMP
        lea ecx, [r15 + 1]
        neg ecx
        xor edx, edx
        xor r8d, r8d
        jmp re_emit
.plus:
        ; atom; split back, on
        mov ecx, r15d
        neg ecx
        mov edx, 1
        test r14, r14
        jz .plus_emit
        xchg ecx, edx
.plus_emit:
        mov eax, OP_RE_SPLIT
        xor r8d, r8d
        jmp re_emit
.limited:
        test r12, r12
        jnz .optional
        ; {0,m}: m optional ones, the first with the atom where it is
        lea rax, [r15 + 1]
        imul rax, r13                   ; all of it
        cmp rax, RE_MAX_CODE
        jbe .fits
        mov eax, e_re_big
        jmp re_die
.fits:
        mov r9, rbx
        mov ecx, 1
        mov edx, eax
        test r14, r14
        jz .first
        xchg ecx, edx
.first:
        mov eax, OP_RE_SPLIT
        call re_insert
        mov rbp, 1
.zero_more:
        cmp rbp, r13
        jae .done
        mov rax, r13
        sub rax, rbp
        lea rcx, [r15 + 1]
        imul rax, rcx
        mov ecx, 1
        mov edx, eax
        test r14, r14
        jz .zero_emit
        xchg ecx, edx
.zero_emit:
        mov eax, OP_RE_SPLIT
        xor r8d, r8d
        call re_emit
        lea rax, [rbx + 1]
        lea rcx, [rbx + r15 + 1]
        call re_copy
        inc rbp
        jmp .zero_more
.optional:
        mov rbp, r12
.opt_more:
        cmp rbp, r13
        jae .done
        mov rax, r13
        sub rax, rbp
        lea rcx, [r15 + 1]
        imul rax, rcx
        mov ecx, 1
        mov edx, eax
        test r14, r14
        jz .opt_emit
        xchg ecx, edx
.opt_emit:
        mov eax, OP_RE_SPLIT
        xor r8d, r8d
        call re_emit
        mov rax, rbx
        lea rcx, [rbx + r15]
        call re_copy
        inc rbp
        jmp .opt_more
.done:
        ret

; an atom and what repeats it
re_parse_piece:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov rbx, [re_len]
        call re_parse_atom
        mov rsi, [re_p]
        movzx eax, byte [rsi]
        cmp eax, '*'
        je .star
        cmp eax, '+'
        je .plus
        cmp eax, '?'
        je .opt
        cmp eax, '{'
        jne .out
        call re_parse_counts
        jc .out                         ; a { of its own
        jmp .suffix
.star:
        xor r12d, r12d
        mov r13, -1
        inc qword [re_p]
        jmp .suffix
.plus:
        mov r12d, 1
        mov r13, -1
        inc qword [re_p]
        jmp .suffix
.opt:
        xor r12d, r12d
        mov r13d, 1
        inc qword [re_p]
.suffix:
        xor r14d, r14d
        mov rsi, [re_p]
        cmp byte [rsi], '?'
        jne .again
        mov r14d, 1
        inc qword [re_p]
        inc rsi
.again:
        movzx eax, byte [rsi]
        cmp eax, '*'
        je .twice
        cmp eax, '+'
        je .twice
        cmp eax, '?'
        je .twice
        cmp eax, '{'
        jne .repeat
        ; is it another count?
        push r12
        push r13
        mov rax, [re_p]
        push rax
        call re_parse_counts
        pop rax
        pop r13
        pop r12
        jc .repeat
        mov [re_p], rax
.twice:
        mov eax, e_re_repeat
        jmp re_die
.repeat:
        call re_repeat
.out:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; a | b | c
re_parse_alternatives:
        push rbx
        push r12
        mov rbx, [re_len]
.piece:
        mov rsi, [re_p]
        movzx eax, byte [rsi]
        test eax, eax
        jz .end
        cmp eax, '|'
        je .bar
        cmp eax, ')'
        je .end
        call re_parse_piece
        jmp .piece
.bar:
        inc qword [re_p]
        ; split A B; A: ...; jmp end; B: ...
        mov rdx, [re_len]
        sub rdx, rbx
        add edx, 2
        mov r9, rbx
        mov eax, OP_RE_SPLIT
        mov ecx, 1
        call re_insert
        mov eax, OP_RE_JMP
        xor ecx, ecx
        xor edx, edx
        xor r8d, r8d
        call re_emit
        mov r12, rax
        call re_parse_alternatives
        mov rax, [re_len]
        sub rax, r12
        shl r12, 4
        add r12, [re_code]
        mov [r12 + 4], eax
.end:
        pop r12
        pop rbx
        ret

; rax = the pattern (a string), rcx = the flags (a string, or 0). CF set when it
; does not compile, re_err saying why.
re_compile:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov rbx, rax
        xor eax, eax
        mov [re_icase], rax
        mov [re_multi], rax
        mov [re_dotall], rax
        mov [re_len], rax
        mov [re_nranges], rax
        mov [re_groups], rax
        mov [re_depth], rax
        mov [re_generation], rax
        mov [re_sp], rsp
        test rcx, rcx
        jz .flags_done
        mov rsi, rcx
        mov rcx, [rsi + STR_LEN]
        add rsi, STR_BYTES
.flag:
        test rcx, rcx
        jz .flags_done
        mov al, [rsi]
        cmp al, 'i'
        jne .not_i
        mov qword [re_icase], 1
        jmp .flag_next
.not_i:
        cmp al, 'm'
        jne .not_m
        mov qword [re_multi], 1
        jmp .flag_next
.not_m:
        cmp al, 's'
        jne .bad_flag
        mov qword [re_dotall], 1
.flag_next:
        inc rsi
        dec rcx
        jmp .flag
.bad_flag:
        mov eax, e_re_flags
        jmp re_die
.flags_done:
        mov edi, RE_MAX_CODE * RE_INST
        call re_alloc
        mov [re_code], rax
        mov edi, RE_MAX_RANGES * 8
        call re_alloc
        mov [re_ranges], rax
        lea rax, [rbx + STR_BYTES]
        mov [re_p], rax
        add rax, [rbx + STR_LEN]
        mov [re_pend], rax
        call re_parse_alternatives
        mov rsi, [re_p]
        cmp byte [rsi], ')'
        jne .closed
        mov eax, e_re_unmatched
        jmp re_die
.closed:
        mov eax, OP_RE_MATCH
        call re_emit0
        mov rax, [re_groups]
        inc rax
        shl rax, 1
        mov [re_slots], rax
        ; the search's memory
        mov rdi, [re_len]
        lea rcx, [rax + 1]
        imul rdi, rcx
        shl rdi, 3
        mov rbx, rdi
        call re_alloc
        mov [re_threads], rax
        mov rdi, rbx
        call re_alloc
        mov [re_threads + 8], rax
        mov rdi, [re_slots]
        shl rdi, 3
        call re_alloc
        mov [re_saves], rax
        mov rdi, [re_len]
        lea rdi, [rdi * 2 + 2]
        imul rdi, rdi, 24
        call re_alloc
        mov [re_stack], rax
        mov rdi, [re_len]
        shl rdi, 2
        call re_alloc
        mov [re_marks], rax
        mov rdi, [re_slots]
        shl rdi, 3
        call re_alloc
        mov [re_spans], rax
        clc
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; --- running

; rdi = list (0 or 1), rax = pc, rdx = position: pc and all it leads to without
; taking a character, in priority order, into the list; re_saves are the slots
; so far. Uses rax rcx rdx rsi r8 .. r11.
re_add_thread:
        push rbx
        push r12
        mov r12, [re_stack]
        mov [r12], rax
        mov qword [r12 + 8], -1
        mov qword [r12 + 16], 0
        mov r8d, 1                      ; the top
.pop:
        test r8, r8
        jz .done
        dec r8
        imul r9, r8, 24
        add r9, r12                     ; e
        mov rax, [r9 + 8]
        cmp rax, 0
        jl .pc
        ; a slot to put back
        mov rcx, [re_saves]
        mov rsi, [r9 + 16]
        mov [rcx + rax * 8], rsi
        jmp .pop
.pc:
        mov rax, [r9]
        mov rcx, [re_marks]
        mov esi, [re_generation]
        cmp [rcx + rax * 4], esi
        je .pop
        mov [rcx + rax * 4], esi
        mov rbx, rax
        shl rbx, 4
        add rbx, [re_code]
        mov ecx, [rbx]                  ; op
        cmp ecx, OP_RE_JMP
        je .jmp
        cmp ecx, OP_RE_SPLIT
        je .split
        cmp ecx, OP_RE_SAVE
        je .save
        cmp ecx, OP_RE_BOL
        je .bol
        cmp ecx, OP_RE_EOL
        je .eol
        cmp ecx, OP_RE_MBOL
        je .mbol
        cmp ecx, OP_RE_MEOL
        je .meol
        cmp ecx, OP_RE_WORDB
        je .wordb
        cmp ecx, OP_RE_NWORDB
        je .wordb
        ; a thread
        mov rcx, [re_counts + rdi * 8]
        mov rsi, [re_slots]
        inc rsi
        imul rcx, rsi
        shl rcx, 3
        add rcx, [re_threads + rdi * 8]
        mov [rcx], rax
        add rcx, 8
        push rdi
        mov rdi, rcx
        mov rsi, [re_saves]
        mov rcx, [re_slots]
        rep movsq
        pop rdi
        inc qword [re_counts + rdi * 8]
        jmp .pop
.jmp:
        movsxd rcx, dword [rbx + 4]
        add rax, rcx
        mov [r9], rax
        inc r8
        jmp .pop
.split:
        ; the second way waits under the first
        movsxd rcx, dword [rbx + 8]
        add rcx, rax
        mov [r9], rcx
        movsxd rcx, dword [rbx + 4]
        add rcx, rax
        mov [r9 + 24], rcx
        mov qword [r9 + 32], -1
        mov qword [r9 + 40], 0
        add r8, 2
        jmp .pop
.save:
        ; put back what it was when the ways after this one are done
        movsxd rcx, dword [rbx + 4]
        mov rsi, [re_saves]
        mov r10, [rsi + rcx * 8]
        mov qword [r9], 0
        mov [r9 + 8], rcx
        mov [r9 + 16], r10
        inc rax
        mov [r9 + 24], rax
        mov qword [r9 + 32], -1
        mov qword [r9 + 40], 0
        add r8, 2
        mov [rsi + rcx * 8], rdx
        jmp .pop
.bol:
        test rdx, rdx
        jz .on
        jmp .pop
.eol:
        cmp rdx, [re_tlen]
        je .on
        jmp .pop
.mbol:
        test rdx, rdx
        jz .on
        mov rsi, [re_text]
        cmp byte [rsi + rdx - 1], 10
        je .on
        jmp .pop
.meol:
        cmp rdx, [re_tlen]
        je .on
        mov rsi, [re_text]
        cmp byte [rsi + rdx], 10
        je .on
        jmp .pop
.wordb:
        ; before and after: which of them is a word character
        mov rsi, [re_text]
        xor r10d, r10d
        xor r11d, r11d
        push rax
        test rdx, rdx
        jz .no_before
        movzx eax, byte [rsi + rdx - 1]
        call re_is_word
        jc .no_before
        mov r10d, 1
.no_before:
        cmp rdx, [re_tlen]
        jae .no_after
        movzx eax, byte [rsi + rdx]
        call re_is_word
        jc .no_after
        mov r11d, 1
.no_after:
        pop rax
        xor r10d, r11d                  ; 1: at a boundary
        cmp dword [rbx], OP_RE_WORDB
        je .want_boundary
        xor r10d, 1
.want_boundary:
        test r10d, r10d
        jz .pop
.on:
        inc rax
        mov [r9], rax
        inc r8
        jmp .pop
.done:
        pop r12
        pop rbx
        ret

; rbx = the instruction, eax = a character -> CF clear when its ranges have it.
; Keeps every register.
re_class_has:
        push rcx
        push rdx
        push rsi
        mov esi, [rbx + 4]
        mov rdx, [re_ranges]
        lea rdx, [rdx + rsi * 8]
        mov ecx, [rbx + 8]
.next:
        test ecx, ecx
        jz .no
        cmp eax, [rdx]
        jl .skip
        cmp eax, [rdx + 4]
        jle .yes
.skip:
        add rdx, 8
        dec ecx
        jmp .next
.yes:
        pop rsi
        pop rdx
        pop rcx
        clc
        ret
.no:
        pop rsi
        pop rdx
        pop rcx
        stc
        ret

; rax = from: the first match at or after it -> eax 1 with re_spans set, or 0.
; re_text and re_tlen are the text. Keeps nothing but rbx rbp r12 .. r15.
re_search:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov r12, rax                    ; at
        xor r13d, r13d                  ; the list
        xor r14d, r14d                  ; found
        xor eax, eax
        mov [re_counts], rax
        mov [re_counts + 8], rax
        cmp r12, [re_tlen]
        ja .out
        inc dword [re_generation]
.step:
        test r14, r14
        jnz .no_start
        ; a match could start here, after all that started before it
        mov rcx, [re_slots]
        mov rdi, [re_saves]
        mov rax, -1
        rep stosq
        mov rdi, [re_saves]
        mov [rdi], r12
        mov rdi, r13
        xor eax, eax
        mov rdx, r12
        call re_add_thread
.no_start:
        cmp qword [re_counts + r13 * 8], 0
        jne .live
        test r14, r14
        jnz .out
        cmp r12, [re_tlen]
        jae .out
.live:
        ; this character
        mov r15d, -1                    ; c
        mov ebp, 1                      ; its size
        cmp r12, [re_tlen]
        jae .have_char
        mov rsi, [re_text]
        add rsi, r12
        mov rdi, [re_text]
        add rdi, [re_tlen]
        call re_decode
        mov r15d, eax
        mov ebp, ecx
.have_char:
        inc dword [re_generation]
        mov rax, r13
        xor rax, 1
        mov qword [re_counts + rax * 8], 0
        xor ecx, ecx                    ; t
.thread:
        cmp rcx, [re_counts + r13 * 8]
        jae .threads_done
        push rcx
        mov rax, [re_slots]
        inc rax
        imul rax, rcx
        shl rax, 3
        add rax, [re_threads + r13 * 8]
        mov r8, rax                     ; th
        mov r9, [r8]                    ; pc
        mov rbx, r9
        shl rbx, 4
        add rbx, [re_code]
        mov ecx, [rbx]
        cmp ecx, OP_RE_MATCH
        jne .not_match
        ; the threads after it have lower priority
        lea rsi, [r8 + 8]
        mov rdi, [re_spans]
        mov rcx, [re_slots]
        rep movsq
        mov rax, [re_spans]
        mov [rax + 8], r12
        mov r14d, 1
        pop rcx
        jmp .threads_done
.not_match:
        test r15d, r15d
        js .next_thread                 ; the end of the text: nothing to take
        cmp ecx, OP_RE_CHAR
        jne .not_char
        cmp r15d, [rbx + 4]
        je .take
        jmp .next_thread
.not_char:
        cmp ecx, OP_RE_CHARI
        jne .not_chari
        mov eax, r15d
        cmp eax, 'A'
        jb .chari
        cmp eax, 'Z'
        ja .chari
        add eax, 32
.chari:
        cmp eax, [rbx + 4]
        je .take
        jmp .next_thread
.not_chari:
        cmp ecx, OP_RE_ANY
        jne .not_any
        cmp r15d, 10
        jne .take
        jmp .next_thread
.not_any:
        cmp ecx, OP_RE_ANYNL
        je .take
        cmp ecx, OP_RE_CLASS
        jne .classi
        mov eax, r15d
        call re_class_has
        setnc al
        movzx eax, al
        cmp eax, [rbx + 12]
        jne .take
        jmp .next_thread
.classi:
        cmp ecx, OP_RE_CLASSI
        jne .next_thread
        mov eax, r15d
        call re_class_has
        jnc .classi_has
        cmp eax, 'a'
        jb .upper
        cmp eax, 'z'
        ja .upper
        sub eax, 32
        call re_class_has
        jnc .classi_has
        jmp .classi_not
.upper:
        cmp eax, 'A'
        jb .classi_not
        cmp eax, 'Z'
        ja .classi_not
        add eax, 32
        call re_class_has
        jnc .classi_has
.classi_not:
        xor eax, eax
        jmp .classi_cmp
.classi_has:
        mov eax, 1
.classi_cmp:
        cmp eax, [rbx + 12]
        je .next_thread
.take:
        lea rsi, [r8 + 8]
        mov rdi, [re_saves]
        mov rcx, [re_slots]
        rep movsq
        mov rdi, r13
        xor rdi, 1
        lea rax, [r9 + 1]
        lea rdx, [r12 + rbp]
        call re_add_thread
.next_thread:
        pop rcx
        inc rcx
        jmp .thread
.threads_done:
        xor r13, 1
        test r15d, r15d
        js .out
        add r12, rbp
        jmp .step
.out:
        mov rax, r14
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; --- the commands

; rdi = arguments, esi = count, r8 = the command's name, r9 = values before the
; optional ones, r10 = it takes a from. CF set when the command has failed (it has
; said why), with nothing left to give back. Sets re_pat, re_repl, re_from, and
; compiles the pattern.
re_begin:
        mov [re_name], r8
        mov rax, r9
        add rax, 1
        add rax, r10
        cmp rsi, r9
        jb .usage
        cmp rsi, rax
        ja .usage
        cmp qword [rdi], T_STR
        jne .types
        cmp qword [rdi + 16], T_STR
        jne .types
        mov rax, [rdi + 24]
        lea rcx, [rax + STR_BYTES]
        mov [re_text], rcx
        mov rcx, [rax + STR_LEN]
        mov [re_tlen], rcx
        xor eax, eax
        mov [re_from], rax
        mov [re_repl], rax
        cmp r9, 3
        jne .no_repl
        cmp qword [rdi + 32], T_STR
        jne .types
        mov rax, [rdi + 40]
        mov [re_repl], rax
.no_repl:
        ; the optional ones: from, flags
        xor ecx, ecx                    ; flags
        mov rdx, r9                     ; next
        test r10, r10
        jz .flags
        cmp rdx, rsi
        jae .compile
        mov rax, rdx
        shl rax, 4
        cmp qword [rdi + rax], T_STR
        je .flags
        cmp qword [rdi + rax], T_INT
        jne .bad_from
        mov r8, [rdi + rax + 8]
        test r8, r8
        js .bad_from
        mov [re_from], r8
        inc rdx
.flags:
        cmp rdx, rsi
        jae .compile
        lea rax, [rdx + 1]
        cmp rax, rsi
        jne .bad_flags
        mov rax, rdx
        shl rax, 4
        cmp qword [rdi + rax], T_STR
        jne .bad_flags
        mov rcx, [rdi + rax + 8]
.compile:
        mov rax, [rdi + 8]
        call re_compile
        jc .bad_pattern
        clc
        ret
.bad_pattern:
        call re_free_all
        PUTS msg_ussr
        mov rsi, [re_name]
        call con_puts
        PUTS e_re_bad
        mov rsi, [re_err]
        call con_puts
        PUTC 10
        stc
        ret
.usage:
        mov esi, e_re_usage
        jmp .say
.types:
        mov esi, e_re_types
        jmp .say
.bad_from:
        mov esi, e_re_from
        jmp .say
.bad_flags:
        mov esi, e_re_flagstr
.say:
        push rsi
        PUTS msg_ussr
        mov rsi, [re_name]
        call con_puts
        pop rsi
        call con_puts
        PUTC 10
        stc
        ret

; the piece of text [rax, rcx) -> rax: a new string
re_piece:
        mov rsi, [re_text]
        add rsi, rax
        sub rcx, rax
        jmp new_string

; where to look next after the match in re_spans -> rax
re_next:
        mov rsi, [re_spans]
        mov rax, [rsi + 8]
        cmp rax, [rsi]
        ja .out
        cmp rax, [re_tlen]
        jae .step1
        push rax
        mov rsi, [re_text]
        add rsi, rax
        mov rdi, [re_text]
        add rdi, [re_tlen]
        call re_decode
        pop rax
        add rax, rcx
.out:
        ret
.step1:
        inc rax
        ret

%macro RE_ENTER 3                       ; name, values before the optional, from
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        mov [re_out], rdx
        mov r8d, %1
        mov r9d, %2
        mov r10d, %3
        movsxd rsi, esi
        call re_begin
        jc re_failed
%endmacro

re_failed:
        mov eax, -1
        jmp re_pops

; rax = the result's tag, rcx its value: the end of a command
re_finish:
        mov rdx, [re_out]
        mov [rdx], rax
        mov [rdx + 8], rcx
        call re_free_all
        xor eax, eax
re_pops:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

bi_re_test:
        RE_ENTER n_re_test, 2, 0
        xor eax, eax
        call re_search
        mov ecx, eax
        mov eax, T_BOOL
        jmp re_finish

bi_re_find:
        RE_ENTER n_re_find, 2, 1
        mov rax, [re_from]
        call re_search
        test eax, eax
        jz re_none
        call vec_new
        mov r12, rax
        xor ebx, ebx                    ; the group
.group:
        cmp rbx, [re_groups]
        ja .done
        mov rsi, [re_spans]
        mov rax, rbx
        shl rax, 4
        mov rcx, [rsi + rax + 8]
        mov rax, [rsi + rax]
        test rax, rax
        jns .text
        sub rsp, 16
        mov qword [rsp], T_NULL
        mov qword [rsp + 8], 0
        mov rdi, r12
        mov rsi, rsp
        call vec_push
        add rsp, 16
        jmp .next
.text:
        call re_piece
        mov rdi, r12
        call vec_push_str
.next:
        inc rbx
        jmp .group
.done:
        mov eax, T_VEC
        mov rcx, r12
        jmp re_finish

re_none:
        mov eax, T_NULL
        xor ecx, ecx
        jmp re_finish

bi_re_pos:
        RE_ENTER n_re_pos, 2, 1
        mov rax, [re_from]
        call re_search
        test eax, eax
        jz re_none
        call vec_new
        mov rdi, rax
        xor ebx, ebx
.slot:
        cmp rbx, [re_slots]
        jae .done
        mov rsi, [re_spans]
        mov rax, [rsi + rbx * 8]
        call vec_push_int
        inc rbx
        jmp .slot
.done:
        mov eax, T_VEC
        mov rcx, rdi
        jmp re_finish

bi_re_all:
        RE_ENTER n_re_all, 2, 0
        call vec_new
        mov r12, rax
        xor r13d, r13d                  ; at
.next:
        cmp r13, [re_tlen]
        ja .done
        mov rax, r13
        call re_search
        test eax, eax
        jz .done
        mov rsi, [re_spans]
        mov rax, [rsi]
        mov rcx, [rsi + 8]
        call re_piece
        mov rdi, r12
        call vec_push_str
        call re_next
        mov r13, rax
        jmp .next
.done:
        mov eax, T_VEC
        mov rcx, r12
        jmp re_finish

bi_re_split:
        RE_ENTER n_re_split, 2, 0
        call vec_new
        mov r12, rax
        xor r13d, r13d                  ; at
        xor r14d, r14d                  ; where this piece began
.next:
        cmp r13, [re_tlen]
        ja .rest
        mov rax, r13
        call re_search
        test eax, eax
        jz .rest
        mov rsi, [re_spans]
        mov rax, [rsi]
        mov rcx, [rsi + 8]
        ; an empty match splits nothing at either end, or where a piece has
        ; just begun
        cmp rcx, rax
        ja .split
        cmp rax, r14
        jbe .skip
        cmp rax, [re_tlen]
        jae .skip
.split:
        mov r15, rcx
        mov rcx, rax
        mov rax, r14
        call re_piece
        mov rdi, r12
        call vec_push_str
        mov r14, r15
.skip:
        call re_next
        mov r13, rax
        jmp .next
.rest:
        mov rax, r14
        mov rcx, [re_tlen]
        call re_piece
        mov rdi, r12
        call vec_push_str
        mov eax, T_VEC
        mov rcx, r12
        jmp re_finish

; rsi = bytes, rcx = how many: onto the growing text. Keeps nothing but rbx rbp
; r12 .. r15.
re_append:
        test rcx, rcx
        jz .out
        mov rax, [re_buf_len]
        add rax, rcx
        cmp rax, [re_buf_room]
        jb .room
        ; twice what it needs, the old one copied and given back
        push rsi
        push rcx
        lea rdi, [rax * 2 + 64]
        mov [re_buf_room], rdi
        call alloc
        mov rdi, rax
        mov rsi, [re_buf]
        mov rcx, [re_buf_len]
        test rsi, rsi
        jz .moved
        push rdi
        rep movsb
        mov rdi, [cur_pool]
        mov rsi, [re_buf]
        call pool_free
        pop rax
        jmp .kept
.moved:
        mov rax, rdi
.kept:
        mov [re_buf], rax
        pop rcx
        pop rsi
.room:
        mov rdi, [re_buf]
        add rdi, [re_buf_len]
        add [re_buf_len], rcx
        rep movsb
.out:
        ret

bi_re_replace:
        RE_ENTER n_re_replace, 3, 0
        xor eax, eax
        mov [re_buf], rax
        mov [re_buf_len], rax
        mov [re_buf_room], rax
        xor r13d, r13d                  ; at
        xor r14d, r14d                  ; copied up to here
.next:
        cmp r13, [re_tlen]
        ja .rest
        mov rax, r13
        call re_search
        test eax, eax
        jz .rest
        ; what came before it
        mov rsi, [re_spans]
        mov rcx, [rsi]
        sub rcx, r14
        mov rsi, [re_text]
        add rsi, r14
        call re_append
        ; the replacement, \0 .. \9 and \\ in it
        mov r15, [re_repl]
        mov r12, [r15 + STR_LEN]
        add r15, STR_BYTES
.repl:
        test r12, r12
        jz .replaced
        movzx eax, byte [r15]
        cmp eax, '\'
        jne .repl_char
        cmp r12, 1
        je .repl_char
        movzx eax, byte [r15 + 1]
        cmp eax, '\'
        je .repl_backslash
        cmp eax, '0'
        jb .repl_lone
        cmp eax, '9'
        ja .repl_lone
        sub eax, '0'
        add r15, 2
        sub r12, 2
        cmp rax, [re_groups]
        ja .repl
        mov rsi, [re_spans]
        shl rax, 4
        mov rcx, [rsi + rax + 8]
        mov rsi, [rsi + rax]
        test rsi, rsi
        js .repl
        sub rcx, rsi
        add rsi, [re_text]
        call re_append
        jmp .repl
.repl_backslash:
        inc r15
        dec r12
.repl_lone:
.repl_char:
        mov rsi, r15
        mov ecx, 1
        call re_append
        inc r15
        dec r12
        jmp .repl
.replaced:
        mov rsi, [re_spans]
        mov r14, [rsi + 8]
        call re_next
        mov r13, rax
        jmp .next
.rest:
        mov rcx, [re_tlen]
        sub rcx, r14
        mov rsi, [re_text]
        add rsi, r14
        call re_append
        mov rsi, [re_buf]
        mov rcx, [re_buf_len]
        test rsi, rsi
        jnz .have
        mov esi, re_empty
.have:
        call new_string
        mov r12, rax
        mov rsi, [re_buf]
        test rsi, rsi
        jz .freed
        mov rdi, [cur_pool]
        call pool_free
.freed:
        mov eax, T_STR
        mov rcx, r12
        jmp re_finish

        align 8
re_digit:       dd '0', '9', -1
re_not_digit:   dd 0, '0' - 1, '9' + 1, RE_BAD_BYTE + 255, -1
re_word:        dd '0', '9', 'A', 'Z', '_', '_', 'a', 'z', -1
re_not_word:    dd 0, '0' - 1, '9' + 1, 'A' - 1, 'Z' + 1, '_' - 1, '_' + 1, 'a' - 1, 'z' + 1, RE_BAD_BYTE + 255, -1
re_space:       dd 9, 13, ' ', ' ', -1
re_not_space:   dd 0, 8, 14, ' ' - 1, ' ' + 1, RE_BAD_BYTE + 255, -1
        align 8
re_sh_table:
        dq 'd', re_digit
        dq 'D', re_not_digit
        dq 'w', re_word
        dq 'W', re_not_word
        dq 's', re_space
        dq 'S', re_not_space
        dq 0, 0
re_empty:       db 0
n_re_test:      db "re_test", 0
n_re_find:      db "re_find", 0
n_re_pos:       db "re_pos", 0
n_re_all:       db "re_all", 0
n_re_replace:   db "re_replace", 0
n_re_split:     db "re_split", 0
e_re_bad:       db ": bad pattern: ", 0
e_re_usage:     db "(r): pattern text [from] [flags]", 0
e_re_types:     db ": the pattern and the text are strings", 0
e_re_from:      db ": from is an offset in bytes", 0
e_re_flagstr:   db ": the flags are a string", 0
e_re_big:       db "too big", 0
e_re_trail:     db "\ at the end", 0
e_re_backref:   db "backreferences are not supported", 0
e_re_escape:    db "unknown escape", 0
e_re_hex:       db "\x takes two hex digits", 0
e_re_bracket:   db "missing ]", 0
e_re_range:     db "bad range", 0
e_re_repeat:    db "nothing to repeat", 0
e_re_question:  db "of (? only (?: is supported", 0
e_re_groups:    db "too many groups", 0
e_re_paren:     db "missing )", 0
e_re_unmatched: db "unmatched )", 0
e_re_counts:    db "bad {n,m}", 0
e_re_flags:     db "the flags are i, m and s", 0
