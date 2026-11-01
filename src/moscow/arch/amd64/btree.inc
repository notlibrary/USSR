; btree.inc - the btree container of the Moscow runtime (ussr_rt.nasm
; includes it): an ordered map kept in a B+ tree, as src/btree.c keeps it
; for ussr, with the same commands - btree btput btget bthas btdel btlen
; btfirst btlast btfloor btceil btprev btnext btrange btkeys.
;
; Keys are numbers or strings: numbers first, by value (1 and 1.0 are one
; key), then strings byte by byte. A leaf holds up to BT_MAX keys and
; their values and is linked to its neighbours; an internal node holds
; separator keys and one child more than keys, child i covering the keys
; from keys[i-1] up to keys[i]. An insert that overflows a node splits
; it, up to a new root. A delete takes the key out of its leaf and stops
; there: the bump heap gives no memory back anyway, an emptied leaf
; stays in the chain and the scans step over it.

BT_MAX          equ 32
; the tree: the count first, so len and truthiness read it like a
; vector's
BT_COUNT        equ 0
BT_ROOT         equ 8
BT_SIZE         equ 16
; a node
BTN_LEAF        equ 0
BTN_N           equ 8
BTN_NEXT        equ 16
BTN_PREV        equ 24
BTN_KEYS        equ 32                          ; BT_MAX + 1 values
BTN_VALS        equ BTN_KEYS + (BT_MAX + 1) * 16 ; values, or children
BTN_SIZE        equ BTN_VALS + (BT_MAX + 2) * 16
BT_DEPTH        equ 24                          ; levels a path records

; rsi, rdi = keys -> eax: -1, 0 or 1. Keeps every other register but
; xmm0 and xmm1.
bt_cmp:
        push rcx
        push rdx
        push rsi
        push rdi
        mov rax, [rsi + VAL_TAG]
        mov rdx, [rdi + VAL_TAG]
        cmp rax, T_STR
        je .left_string
        cmp rdx, T_STR
        je .less                        ; a number before a string
        cmp rax, T_INT
        jne .reals
        cmp rdx, T_INT
        jne .reals
        mov rax, [rsi + VAL_DATA]
        cmp rax, [rdi + VAL_DATA]
        jl .less
        jg .more
        jmp .same
.reals:
        call to_real_left
        call to_real_right
        ucomisd xmm0, xmm1
        jb .less
        ja .more
        jmp .same
.left_string:
        cmp rdx, T_STR
        jne .more
        mov rsi, [rsi + VAL_DATA]
        mov rdi, [rdi + VAL_DATA]
        mov rcx, [rsi + STR_LEN]
        mov rdx, [rdi + STR_LEN]
        cmp rcx, rdx
        jbe .shorter
        mov rcx, rdx
.shorter:
        add rsi, STR_BYTES
        add rdi, STR_BYTES
        test rcx, rcx
        jz .lengths
        repe cmpsb
        jne .byte_differs
.lengths:
        mov rsi, [rsp + 8]              ; the saved rsi, rdi
        mov rdi, [rsp]
        mov rsi, [rsi + VAL_DATA]
        mov rdi, [rdi + VAL_DATA]
        mov rax, [rsi + STR_LEN]
        cmp rax, [rdi + STR_LEN]
        jb .less
        ja .more
        jmp .same
.byte_differs:
        movzx eax, byte [rsi - 1]
        movzx edx, byte [rdi - 1]
        cmp eax, edx
        jb .less
        jmp .more
.less:
        mov eax, -1
        jmp .out
.more:
        mov eax, 1
        jmp .out
.same:
        xor eax, eax
.out:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret

; rsi = value -> CF clear when it can be a key
bt_key_ok:
        cmp qword [rsi + VAL_TAG], T_INT
        je .ok
        cmp qword [rsi + VAL_TAG], T_REAL
        je .ok
        cmp qword [rsi + VAL_TAG], T_STR
        je .ok
        stc
        ret
.ok:
        clc
        ret

; rdi = node, rsi = key, edx = 1: the number of keys <= key (the child
; to descend into), 0: the number of keys < key (where key is or goes)
; -> rax. Keeps the others.
bt_bound:
        push rcx
        push r8
        push r9
        push r10
        push rdi
        xor r8d, r8d                    ; lo
        mov r9, [rdi + BTN_N]           ; hi
        mov r10, rdi
.loop:
        cmp r8, r9
        jae .done
        lea rcx, [r8 + r9]
        shr rcx, 1                      ; mid
        push rsi
        mov rdi, rsi
        mov rsi, rcx
        shl rsi, 4
        lea rsi, [r10 + BTN_KEYS + rsi]
        call bt_cmp                     ; keys[mid] against key
        pop rsi
        cmp eax, 0
        jl .right
        jg .left
        test edx, edx
        jz .left
.right:
        lea r8, [rcx + 1]
        jmp .loop
.left:
        mov r9, rcx
        jmp .loop
.done:
        mov rax, r8
        pop rdi
        pop r10
        pop r9
        pop r8
        pop rcx
        ret

; rdi = tree, rsi = key -> rax: the leaf where key is or goes, or 0.
; Keeps the others.
bt_leaf_for:
        push rdi
        push rdx
        mov rdi, [rdi + BT_ROOT]
        mov edx, 1
.down:
        test rdi, rdi
        jz .done
        cmp qword [rdi + BTN_LEAF], 0
        jne .done
        call bt_bound
        mov rdi, [rdi + BTN_VALS + rax * 8]
        jmp .down
.done:
        mov rax, rdi
        pop rdx
        pop rdi
        ret

; rdi = destination, rsi = source, rcx = bytes; the ranges may overlap.
; Keeps the others.
bt_move:
        push rcx
        push rsi
        push rdi
        test rcx, rcx
        jz .done
        cmp rdi, rsi
        jbe .forward
        lea rsi, [rsi + rcx - 1]
        lea rdi, [rdi + rcx - 1]
        std
        rep movsb
        cld
        jmp .done
.forward:
        rep movsb
.done:
        pop rdi
        pop rsi
        pop rcx
        ret

; edi = 1 for a leaf -> rax: a new node
bt_node_new:
        push rdi
        mov edi, BTN_SIZE
        call alloc
        pop rdi
        mov [rax + BTN_LEAF], rdi
        ret

; rdi = tree, rsi = key, rdx = value: stores a copy of value under key.
bt_put:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        sub rsp, BT_DEPTH * 16 + 16     ; the path, then the separator
        mov rbx, rdi
        mov r12, rsi
        mov r13, rdx
        cmp qword [rbx + BT_ROOT], 0
        jne .rooted
        mov edi, 1
        call bt_node_new
        mov [rbx + BT_ROOT], rax
.rooted:
        mov rdi, [rbx + BT_ROOT]
        xor ebp, ebp                    ; depth
.descend:
        cmp qword [rdi + BTN_LEAF], 0
        jne .leaf
        mov rsi, r12
        mov edx, 1
        call bt_bound
        mov rcx, rbp
        shl rcx, 4
        mov [rsp + rcx], rdi
        mov [rsp + rcx + 8], rax
        inc rbp
        mov rdi, [rdi + BTN_VALS + rax * 8]
        jmp .descend
.leaf:
        mov r14, rdi
        mov rsi, r12
        xor edx, edx
        call bt_bound
        mov r15, rax                    ; i
        cmp r15, [r14 + BTN_N]
        jae .insert
        mov rsi, r15
        shl rsi, 4
        lea rsi, [r14 + BTN_KEYS + rsi]
        mov rdi, r12
        call bt_cmp
        test eax, eax
        jnz .insert
        mov rdi, r15                    ; the key is there: a new value
        shl rdi, 4
        lea rdi, [r14 + BTN_VALS + rdi]
        COPYVAL rdi, r13, rax
        jmp .done
.insert:
        mov rcx, [r14 + BTN_N]
        sub rcx, r15
        shl rcx, 4
        mov rsi, r15
        shl rsi, 4
        lea rsi, [r14 + BTN_KEYS + rsi]
        lea rdi, [rsi + 16]
        call bt_move
        COPYVAL rsi, r12, rax
        mov rsi, r15
        shl rsi, 4
        lea rsi, [r14 + BTN_VALS + rsi]
        lea rdi, [rsi + 16]
        call bt_move
        COPYVAL rsi, r13, rax
        inc qword [r14 + BTN_N]
        inc qword [rbx + BT_COUNT]
        ; split what overflows, from the leaf up
.settle:
        cmp qword [r14 + BTN_N], BT_MAX
        jbe .done
        mov rdi, r14
        lea rsi, [rsp + BT_DEPTH * 16]
        call bt_split                   ; rax: the right half
        mov r15, rax
        test rbp, rbp
        jnz .into_parent
        xor edi, edi
        call bt_node_new
        mov qword [rax + BTN_N], 1
        lea rsi, [rsp + BT_DEPTH * 16]
        lea rdi, [rax + BTN_KEYS]
        COPYVAL rdi, rsi, rcx
        mov [rax + BTN_VALS], r14
        mov [rax + BTN_VALS + 8], r15
        mov [rbx + BT_ROOT], rax
        jmp .done
.into_parent:
        dec rbp
        mov rcx, rbp
        shl rcx, 4
        mov r14, [rsp + rcx]            ; the parent
        mov r8, [rsp + rcx + 8]         ; the child it went down
        ; keys[i..] and children[i+1..] move up one
        mov rcx, [r14 + BTN_N]
        sub rcx, r8
        shl rcx, 4
        mov rsi, r8
        shl rsi, 4
        lea rsi, [r14 + BTN_KEYS + rsi]
        lea rdi, [rsi + 16]
        call bt_move
        mov rdi, rsi
        lea rsi, [rsp + BT_DEPTH * 16]
        COPYVAL rdi, rsi, rax
        mov rcx, [r14 + BTN_N]
        sub rcx, r8
        shl rcx, 3
        lea rsi, [r14 + BTN_VALS + r8 * 8 + 8]
        lea rdi, [rsi + 8]
        call bt_move
        mov [r14 + BTN_VALS + r8 * 8 + 8], r15
        inc qword [r14 + BTN_N]
        jmp .settle
.done:
        add rsp, BT_DEPTH * 16 + 16
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; rdi = a node with BT_MAX + 1 keys, rsi = room for the separator ->
; rax: the new right half. Keeps rbx, rbp, r12 .. r15.
bt_split:
        push rbx
        push r12
        push r13
        mov rbx, rdi
        mov r12, rsi
        mov edi, [rbx + BTN_LEAF]
        call bt_node_new
        mov r13, rax
        mov rax, [rbx + BTN_N]
        shr rax, 1                      ; half
        cmp qword [rbx + BTN_LEAF], 0
        je .internal
        ; leaf: the right half takes keys and values from half on
        mov rcx, [rbx + BTN_N]
        sub rcx, rax
        mov [r13 + BTN_N], rcx
        shl rcx, 4
        mov rsi, rax
        shl rsi, 4
        push rsi
        lea rsi, [rbx + BTN_KEYS + rsi]
        lea rdi, [r13 + BTN_KEYS]
        call bt_move
        pop rsi
        lea rsi, [rbx + BTN_VALS + rsi]
        lea rdi, [r13 + BTN_VALS]
        call bt_move
        mov [rbx + BTN_N], rax
        mov rcx, [rbx + BTN_NEXT]       ; into the chain
        mov [r13 + BTN_NEXT], rcx
        mov [r13 + BTN_PREV], rbx
        test rcx, rcx
        jz .linked
        mov [rcx + BTN_PREV], r13
.linked:
        mov [rbx + BTN_NEXT], r13
        lea rsi, [r13 + BTN_KEYS]
        COPYVAL r12, rsi, rcx
        jmp .out
.internal:
        ; the middle key goes up; the right half takes what follows it
        mov rsi, rax
        shl rsi, 4
        lea rsi, [rbx + BTN_KEYS + rsi]
        COPYVAL r12, rsi, rcx
        mov rcx, [rbx + BTN_N]
        sub rcx, rax
        dec rcx
        mov [r13 + BTN_N], rcx
        shl rcx, 4
        add rsi, 16
        lea rdi, [r13 + BTN_KEYS]
        call bt_move
        mov rcx, [r13 + BTN_N]
        inc rcx
        shl rcx, 3
        lea rsi, [rbx + BTN_VALS + rax * 8 + 8]
        lea rdi, [r13 + BTN_VALS]
        call bt_move
        mov [rbx + BTN_N], rax
.out:
        mov rax, r13
        pop r13
        pop r12
        pop rbx
        ret

; rdi = tree, rsi = key -> rax: the leaf, rcx: the index of key in it;
; rax = 0 when key is not there. Keeps the others.
bt_find:
        push rdx
        call bt_leaf_for
        test rax, rax
        jz .none
        push rdi
        mov rdi, rax
        xor edx, edx
        push rax
        call bt_bound
        mov rcx, rax
        pop rax
        pop rdi
        cmp rcx, [rax + BTN_N]
        jae .none
        push rax
        push rsi
        push rdi
        mov rdi, rsi
        mov rsi, rcx
        shl rsi, 4
        lea rsi, [rax + BTN_KEYS + rsi]
        call bt_cmp
        pop rdi
        pop rsi
        test eax, eax
        pop rax
        jnz .none
        pop rdx
        ret
.none:
        xor eax, eax
        pop rdx
        ret

; rdi = tree, rsi = key, edx = 1 for strictly after -> rax: the leaf of
; the first key >= key (> key), rcx: its index; rax = 0 when none
bt_seek:
        push rdi
        call bt_leaf_for
        test rax, rax
        jz .out
        mov rdi, rax
        call bt_bound
        mov rcx, rax
        mov rax, rdi
.walk:
        cmp rcx, [rax + BTN_N]
        jb .out
        mov rax, [rax + BTN_NEXT]
        xor ecx, ecx
        test rax, rax
        jnz .walk
.out:
        pop rdi
        ret

; rdi = tree, rsi = key, edx = 1 for strictly before -> rax: the leaf of
; the last key <= key (< key), rcx: its index; rax = 0 when none
bt_seek_back:
        push rdi
        push rdx
        call bt_leaf_for
        test rax, rax
        jz .out
        mov rdi, rax
        xor edx, 1                      ; before: count the keys < key
        call bt_bound
        lea rcx, [rax - 1]
        mov rax, rdi
.walk:
        test rcx, rcx
        jns .out
        mov rax, [rax + BTN_PREV]
        test rax, rax
        jz .out
        mov rcx, [rax + BTN_N]
        dec rcx
        jmp .walk
.out:
        pop rdx
        pop rdi
        ret

; rdi = tree -> rax: the first leaf with a key (rcx = 0), or 0
bt_first_pos:
        mov rax, [rdi + BT_ROOT]
.down:
        test rax, rax
        jz .out
        cmp qword [rax + BTN_LEAF], 0
        jne .walk
        mov rax, [rax + BTN_VALS]
        jmp .down
.walk:
        cmp qword [rax + BTN_N], 0
        jne .out
        mov rax, [rax + BTN_NEXT]
        test rax, rax
        jnz .walk
.out:
        xor ecx, ecx
        ret

; rdi = tree -> rax: the last leaf with a key, rcx its last index; or 0
bt_last_pos:
        mov rax, [rdi + BT_ROOT]
.down:
        test rax, rax
        jz .out
        cmp qword [rax + BTN_LEAF], 0
        jne .walk
        mov rcx, [rax + BTN_N]
        mov rax, [rax + BTN_VALS + rcx * 8]
        jmp .down
.walk:
        mov rcx, [rax + BTN_N]
        dec rcx
        jns .out
        mov rax, [rax + BTN_PREV]
        test rax, rax
        jnz .walk
.out:
        ret

; rax = leaf, rcx = index -> rax, rcx: the next position (rax = 0 at
; the end)
bt_step:
        inc rcx
.walk:
        cmp rcx, [rax + BTN_N]
        jb .out
        mov rax, [rax + BTN_NEXT]
        xor ecx, ecx
        test rax, rax
        jnz .walk
.out:
        ret

; --- the commands ---------------------------------------------------------

; The tree argument: CF set (after a message) when argument 0 is not one.
bt_tree_arg:
        test esi, esi
        jz .bad
        cmp qword [rdi], T_BTREE
        jne .bad
        clc
        ret
.bad:
        PUTS msg_ussr
        PUTS e_bt_tree
        PUTC 10
        stc
        ret

; argument 1 as a key, or CF set after a message
bt_key_arg:
        push rsi
        lea rsi, [rdi + 16]
        call bt_key_ok
        pop rsi
        jnc .ok
        PUTS msg_ussr
        PUTS e_bt_key
        PUTC 10
        stc
.ok:
        ret

bi_btree:
        push rdx
        mov edi, BT_SIZE
        call alloc
        pop rdx
        mov qword [rdx], T_BTREE
        mov [rdx + 8], rax
        xor eax, eax
        ret

bi_btput:
        call bt_tree_arg
        jc .fail
        cmp esi, 3
        jne .usage
        call bt_key_arg
        jc .fail
        push rdx
        push rdi
        lea rsi, [rdi + 16]
        lea rdx, [rdi + 32]
        mov rdi, [rdi + 8]
        call bt_put
        pop rdi
        pop rdx
        COPYVAL rdx, rdi, rax
        xor eax, eax
        ret
.usage:
        mov eax, e_btput
        jmp bi_error
.fail:
        mov eax, -1
        ret

; btget(v): t key [default] and bthas(b): t key
bi_btget:
        call bt_tree_arg
        jc bi_btput.fail
        cmp esi, 2
        jb .usage
        cmp esi, 3
        ja .usage
        call bt_key_arg
        jc bi_btput.fail
        push rsi
        push rdi
        lea rsi, [rdi + 16]
        mov rdi, [rdi + 8]
        call bt_find
        pop rdi
        pop rsi
        test rax, rax
        jz .missing
        shl rcx, 4
        lea rax, [rax + BTN_VALS + rcx]
        COPYVAL rdx, rax, rcx
        xor eax, eax
        ret
.missing:
        cmp esi, 3
        jne .null
        lea rax, [rdi + 32]
        COPYVAL rdx, rax, rcx
.null:
        xor eax, eax
        ret
.usage:
        mov eax, e_btget
        jmp bi_error

bi_bthas:
        call bt_tree_arg
        jc bi_btput.fail
        cmp esi, 2
        jne .usage
        call bt_key_arg
        jc bi_btput.fail
        push rdi
        lea rsi, [rdi + 16]
        mov rdi, [rdi + 8]
        call bt_find
        pop rdi
        test rax, rax
        setnz al
        movzx eax, al
        mov qword [rdx], T_BOOL
        mov [rdx + 8], rax
        xor eax, eax
        ret
.usage:
        mov eax, e_bthas
        jmp bi_error

bi_btdel:
        call bt_tree_arg
        jc bi_btput.fail
        cmp esi, 2
        jne .usage
        call bt_key_arg
        jc bi_btput.fail
        push rdx
        push rdi
        mov r8, [rdi + 8]
        lea rsi, [rdi + 16]
        mov rdi, r8
        call bt_find
        pop rdi
        pop rdx
        mov qword [rdx], T_BOOL
        mov qword [rdx + 8], 0
        test rax, rax
        jz .done
        mov qword [rdx + 8], 1
        dec qword [r8 + BT_COUNT]
        ; keys and values after it move down one
        mov r9, rax
        mov rax, [r9 + BTN_N]
        sub rax, rcx
        dec rax
        shl rax, 4
        shl rcx, 4
        lea rdi, [r9 + BTN_KEYS + rcx]
        lea rsi, [rdi + 16]
        xchg rax, rcx
        call bt_move
        lea rdi, [r9 + BTN_VALS + rax]
        lea rsi, [rdi + 16]
        call bt_move
        dec qword [r9 + BTN_N]
.done:
        xor eax, eax
        ret
.usage:
        mov eax, e_btdel
        jmp bi_error

bi_btlen:
        call bt_tree_arg
        jc bi_btput.fail
        mov rax, [rdi + 8]
        mov rax, [rax + BT_COUNT]
        jmp bi_int_out

; rax = leaf, rcx = index (rax = 0: none) -> the key into [rdx]
bt_key_out:
        test rax, rax
        jz .null
        shl rcx, 4
        lea rax, [rax + BTN_KEYS + rcx]
        COPYVAL rdx, rax, rcx
.null:
        xor eax, eax
        ret

bi_btfirst:
        call bt_tree_arg
        jc bi_btput.fail
        mov rdi, [rdi + 8]
        call bt_first_pos
        jmp bt_key_out

bi_btlast:
        call bt_tree_arg
        jc bi_btput.fail
        mov rdi, [rdi + 8]
        call bt_last_pos
        jmp bt_key_out

; btfloor btprev btceil btnext: r8d = 0 floor, 1 prev, 2 ceil, 3 next
bt_near:
        call bt_tree_arg
        jc bi_btput.fail
        cmp esi, 2
        jne .usage
        call bt_key_arg
        jc bi_btput.fail
        push rdx
        lea rsi, [rdi + 16]
        mov rdi, [rdi + 8]
        mov edx, r8d
        and edx, 1
        cmp r8d, 2
        jae .after
        call bt_seek_back
        jmp .found
.after:
        call bt_seek
.found:
        pop rdx
        jmp bt_key_out
.usage:
        mov eax, e_btnear
        jmp bi_error

bi_btfloor:
        xor r8d, r8d
        jmp bt_near
bi_btprev:
        mov r8d, 1
        jmp bt_near
bi_btceil:
        mov r8d, 2
        jmp bt_near
bi_btnext:
        mov r8d, 3
        jmp bt_near

; btrange(v): t [lo [hi [max]]] (r8d = 1) and btkeys(v): ... (r8d = 0)
bt_range:
        call bt_tree_arg
        jc bi_btput.fail
        cmp esi, 4
        ja .usage
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        push rdx
        mov rbx, rdi                    ; the arguments
        mov ebp, esi
        mov r15d, r8d
        mov r14, -1                     ; the most to give
        cmp ebp, 4
        jb .no_max
        cmp qword [rbx + 48], T_NULL
        je .no_max
        cmp qword [rbx + 48], T_INT
        jne .bad_bound
        mov r14, [rbx + 56]
.no_max:
        ; lo and hi: keys or null
        cmp ebp, 2
        jb .bounds_ok
        cmp qword [rbx + 16], T_NULL
        je .lo_ok
        lea rsi, [rbx + 16]
        call bt_key_ok
        jc .bad_bound
.lo_ok:
        cmp ebp, 3
        jb .bounds_ok
        cmp qword [rbx + 32], T_NULL
        je .bounds_ok
        lea rsi, [rbx + 32]
        call bt_key_ok
        jc .bad_bound
.bounds_ok:
        call vec_new
        mov r12, rax                    ; the result
        mov rdi, [rbx + 8]
        cmp ebp, 2
        jb .from_first
        cmp qword [rbx + 16], T_NULL
        je .from_first
        lea rsi, [rbx + 16]
        xor edx, edx
        call bt_seek
        jmp .walk
.from_first:
        call bt_first_pos
.walk:
        test rax, rax
        jz .done
        test r14, r14
        jz .done
        mov r13, rax
        mov rsi, rcx
        shl rsi, 4
        lea rsi, [r13 + BTN_KEYS + rsi]
        cmp ebp, 3
        jb .take
        cmp qword [rbx + 32], T_NULL
        je .take
        lea rdi, [rbx + 32]
        push rax
        call bt_cmp
        mov edx, eax
        pop rax
        cmp edx, 0
        jge .done
.take:
        mov rdi, r12
        test r15d, r15d
        jz .key_only
        push rcx
        push rsi
        call vec_new
        mov rdi, rax
        call vec_push                   ; the key
        mov rsi, [rsp + 8]
        shl rsi, 4
        lea rsi, [r13 + BTN_VALS + rsi]
        call vec_push                   ; the value
        sub rsp, 16
        mov qword [rsp], T_VEC
        mov [rsp + 8], rdi
        mov rsi, rsp
        mov rdi, r12
        call vec_push
        add rsp, 16
        pop rsi
        pop rcx
        jmp .next
.key_only:
        call vec_push
.next:
        dec r14
        mov rax, r13
        call bt_step
        jmp .walk
.done:
        pop rdx
        mov qword [rdx], T_VEC
        mov [rdx + 8], r12
        xor eax, eax
        jmp .out
.bad_bound:
        pop rdx
        PUTS msg_ussr
        PUTS e_btrange
        PUTC 10
        mov eax, -1
.out:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret
.usage:
        mov eax, e_btrange
        jmp bi_error

bi_btrange:
        mov r8d, 1
        jmp bt_range
bi_btkeys:
        xor r8d, r8d
        jmp bt_range

; print_value of a tree: its [key, value] pairs, as encode writes them.
; rsi = the value. Keeps every register.
bt_print:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        PUTC '['
        mov rdi, [rsi + VAL_DATA]
        call bt_first_pos
        xor edx, edx                    ; pairs so far
.pair:
        test rax, rax
        jz .end
        test rdx, rdx
        jz .first
        PUTC ','
.first:
        PUTC '['
        mov rsi, rcx
        shl rsi, 4
        push rdx
        push rsi
        lea rsi, [rax + BTN_KEYS + rsi]
        mov dl, 1
        call print_value
        PUTC ','
        pop rsi
        lea rsi, [rax + BTN_VALS + rsi]
        mov dl, 1
        call print_value
        pop rdx
        PUTC ']'
        inc rdx
        call bt_step
        jmp .pair
.end:
        PUTC ']'
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

n_btree:        db "btree", 0
n_btput:        db "btput", 0
n_btget:        db "btget", 0
n_bthas:        db "bthas", 0
n_btdel:        db "btdel", 0
n_btlen:        db "btlen", 0
n_btfirst:      db "btfirst", 0
n_btlast:       db "btlast", 0
n_btfloor:      db "btfloor", 0
n_btceil:       db "btceil", 0
n_btprev:       db "btprev", 0
n_btnext:       db "btnext", 0
n_btrange:      db "btrange", 0
n_btkeys:       db "btkeys", 0
s_btree:        db "btree", 0
e_bt_tree:      db "the bt* commands take a btree first", 0
e_bt_key:       db "btree keys are numbers or strings", 0
e_btput:        db "btput(t): t key value", 0
e_btget:        db "btget(v): t key [default]", 0
e_bthas:        db "bthas(b): t key", 0
e_btdel:        db "btdel(b): t key", 0
e_btnear:       db "btfloor/btceil/btprev/btnext(k): t key", 0
e_btrange:      db "btrange/btkeys(v): t [lo [hi [max]]]: bounds are keys or null", 0
