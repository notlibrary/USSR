; gc.inc - the kernel's values and their collector (ussr_rt.nasm
; includes it).
;
; Kernel commands are system calls: the kernel is one program (moscow.ru)
; whose state - sockets, windows, TLS sessions, files - every task uses.
; While a command runs, what it allocates comes from the kernel's value
; heap, a TLSF pool in PML4 slot 2 that every address space shares,
; paged on demand. Its arguments are copied in first, so the kernel never
; keeps a pointer into a task's private memory, and its result is copied
; out into the caller's heap, so a program never keeps one into the
; kernel's.
;
; Nothing in the VM frees a value by itself, so the value heap has a
; mark-and-sweep collector. It runs when a command returns and the heap
; has grown since the last collection: a safe point, where every value
; the kernel still uses is reachable from its roots - the kernel's
; variables (moscow.ru's globals and hash) and, for every task, the VM
; areas of the kernel commands it is inside (their registers, frames and
; locals; a task waits only between the kernel's instructions). Marking
; follows strings, vectors, structs (and their types) and btrees (nodes,
; and the leaf chain) through the value heap and the kernel's own heap;
; the mark is bit 2 of a block's size word. The sweep frees what is
; unmarked in the value heap; the kernel's own heap only loses its marks.

KV_BASE         equ 0x10000000000       ; PML4 slot 2
KV_SIZE         equ 64 << 30
BF_MARK         equ 4
GC_SLACK        equ 32 << 20            ; collect after this much growth, at least

; At boot: the slot every address space shares, and the heap in it.
kv_init:
        push rax
        push rdi
        push rsi
        call pmm_alloc
        or rax, PTE_P | PTE_W
        mov [PAGE_PML4 + 16], rax       ; slot 2
        mov rax, cr3
        mov cr3, rax
        mov rdi, KV_BASE
        mov rsi, KV_SIZE
        call pool_init
        mov [kv_pool], rax
        mov qword [gc_next], GC_SLACK
        pop rsi
        pop rdi
        pop rax
        ret

; rax = address -> CF clear when it is a block of the value heap or the
; kernel's own heap that is in use (rax untouched). Keeps every register.
gc_ours:
        push rcx
        test al, 15
        jnz .no
        mov rcx, KV_BASE
        cmp rax, rcx
        jb .kernel
        mov rcx, KV_BASE + KV_SIZE
        cmp rax, rcx
        jae .no
        jmp .block
.kernel:
        mov rcx, [kpool]
        cmp rax, rcx
        jb .no
        add rcx, [kpool_size]
        cmp rax, rcx
        jae .no
.block:
        test qword [rax - BLK_HDR + BLK_SIZE], BF_FREE
        jnz .no
        pop rcx
        clc
        ret
.no:
        pop rcx
        stc
        ret

; rax = address -> CF clear when it is one of ours and was not marked
; yet: it is marked now. Keeps every register.
gc_mark_block:
        test rax, rax
        jz .old
        call gc_ours
        jc .old
        test qword [rax - BLK_HDR + BLK_SIZE], BF_MARK
        jnz .old
        or qword [rax - BLK_HDR + BLK_SIZE], BF_MARK
        clc
        ret
.old:
        stc
        ret

; rsi = rcx values: each marked. Keeps every register.
gc_mark_values:
        push rcx
        push rsi
.next:
        test rcx, rcx
        jz .done
        call gc_mark_value
        add rsi, 16
        dec rcx
        jmp .next
.done:
        pop rsi
        pop rcx
        ret

; rsi = a value: it and what it holds marked. Keeps every register.
gc_mark_value:
        push rax
        push rcx
        push rdx
        push rsi
        mov rdx, [rsi]
        mov rax, [rsi + 8]
        cmp rdx, T_STR
        je .string
        cmp rdx, T_VEC
        je .vector
        cmp rdx, T_STRUCT
        je .struct
        cmp rdx, T_BTREE
        je .btree
        jmp .done
.string:
        call gc_mark_block
        jmp .done
.vector:
        call gc_mark_block
        jc .done
        mov rdx, rax
        mov rax, [rdx + VEC_ITEMS]
        call gc_mark_block
        mov rsi, [rdx + VEC_ITEMS]
        test rsi, rsi
        jz .done
        mov rcx, [rdx + VEC_COUNT]
        call gc_mark_values
        jmp .done
.struct:
        call gc_mark_block
        jc .done
        mov rdx, rax
        mov rax, [rdx + SOBJ_TYPE]
        call gc_mark_type
        mov rcx, [rax + STYPE.nfields]
        lea rsi, [rdx + SOBJ_FIELDS]
        call gc_mark_values
        jmp .done
.btree:
        call gc_mark_block
        jc .done
        mov rax, [rax + BT_ROOT]
        call gc_mark_node
.done:
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rax = struct type: its block, name and field names. Keeps every
; register.
gc_mark_type:
        push rax
        push rcx
        push rdx
        push rsi
        test rax, rax
        jz .done
        mov rdx, rax
        call gc_mark_block
        jc .done
        mov rax, [rdx + STYPE.name]
        call gc_mark_block
        mov rsi, [rdx + STYPE.fields]
        mov rax, rsi
        call gc_mark_block
        test rsi, rsi
        jz .done
        mov rcx, [rdx + STYPE.nfields]
.field:
        test rcx, rcx
        jz .done
        mov rax, [rsi]
        call gc_mark_block
        add rsi, 8
        dec rcx
        jmp .field
.done:
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rax = btree node: it, its keys and values or children, and the leaves
; chained to it. Keeps every register.
gc_mark_node:
        push rax
        push rcx
        push rsi
        test rax, rax
        jz .done
        call gc_mark_block
        jc .done
        cmp qword [rax + BTN_LEAF], 0
        jne .leaf
        ; children: one more than keys
        mov rcx, [rax + BTN_N]
        inc rcx
        lea rsi, [rax + BTN_VALS]
.child:
        push rax
        mov rax, [rsi]
        call gc_mark_node
        pop rax
        add rsi, 8
        dec rcx
        jnz .child
        mov rcx, [rax + BTN_N]
        lea rsi, [rax + BTN_KEYS]
        call gc_mark_values
        jmp .done
.leaf:
        call gc_mark_leaf
        ; the chain both ways, a leaf at a time
        push rax
.right:
        mov rax, [rax + BTN_NEXT]
        call gc_mark_block
        jc .right_done
        call gc_mark_leaf
        jmp .right
.right_done:
        pop rax
.left:
        mov rax, [rax + BTN_PREV]
        call gc_mark_block
        jc .done
        call gc_mark_leaf
        jmp .left
.done:
        pop rsi
        pop rcx
        pop rax
        ret

; rax = leaf (marked): its keys and values. Keeps every register.
gc_mark_leaf:
        push rcx
        push rsi
        mov rcx, [rax + BTN_N]
        lea rsi, [rax + BTN_KEYS]
        call gc_mark_values
        mov rcx, [rax + BTN_N]
        lea rsi, [rax + BTN_VALS]
        call gc_mark_values
        pop rsi
        pop rcx
        ret

; rax = a kernel command's VM area: what its registers, frames and
; locals hold. Keeps every register.
gc_mark_area:
        push rcx
        push rdx
        push rsi
        test rax, rax
        jz .done
        lea rsi, [rax + VM.regs]
        mov ecx, 32
        call gc_mark_values
        lea rsi, [rax + VM.tmp]
        mov ecx, 1
        call gc_mark_values
        mov rsi, [rax + VM.frames]
        test rsi, rsi
        jz .done
        mov rdx, [rax + VM.nframes]
.frame:
        test rdx, rdx
        jz .locals
        mov ecx, 32
        call gc_mark_values
        add rsi, FRAME_size
        dec rdx
        jmp .frame
.locals:
        mov rsi, [rax + VM.frames]
        add rsi, FRAME_size * MAX_FRAMES
        mov rcx, [rax + VM.ltop]
        sub rcx, rsi
        jbe .done
        shr rcx, 4
        call gc_mark_values
.done:
        pop rsi
        pop rdx
        pop rcx
        ret

; rcx = depth, rsi = the areas: the first depth of them marked. Keeps
; every register.
gc_mark_areas:
        push rax
        push rcx
        push rsi
        cmp rcx, SYS_AREAS
        jbe .next
        mov ecx, SYS_AREAS
.next:
        test rcx, rcx
        jz .done
        mov rax, [rsi]
        call gc_mark_area
        add rsi, 8
        dec rcx
        jmp .next
.done:
        pop rsi
        pop rcx
        pop rax
        ret

; The collection. Keeps every register.
gc_collect:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        ; the kernel's variables
        mov rbx, [kprog]
        test rbx, rbx
        jz .tasks
        mov rsi, [rbx + PROG.globals]
        mov rcx, [rbx + PROG.nsyms]
        call gc_mark_values
        mov rsi, [rbx + PROG.hash]
        call gc_mark_values
.tasks:
        ; the running task's kernel commands, then every other one's
        mov rcx, [sys_depth]
        mov rsi, sys_areas
        call gc_mark_areas
        cmp qword [tasks_on], 0
        je .sweep
        mov rbx, [task_table]
        xor edx, edx
.task:
        cmp rbx, [cur_task]
        je .task_next
        mov rax, [rbx + TASK.state]
        cmp rax, TS_READY
        je .live
        cmp rax, TS_BLOCKED
        jne .task_next
.live:
        mov rcx, [rbx + TASK.vars + TVI_DEPTH * 8]
        lea rsi, [rbx + TASK.vars + TVI_AREAS * 8]
        call gc_mark_areas
.task_next:
        add rbx, TASK_size
        inc edx
        cmp edx, MAX_TASKS
        jb .task
.sweep:
        ; the value heap: the unmarked go, the marks go
        mov rdi, [kv_pool]
        lea rbx, [rdi + POOL_HDR]
.block:
        mov rax, [rbx + BLK_SIZE]
        mov rcx, rax
        and rcx, -16
        jz .swept
        lea rdx, [rbx + rcx]            ; the next one, before any merge
        test rax, BF_FREE
        jnz .block_next
        test rax, BF_MARK
        jz .garbage
        and qword [rbx + BLK_SIZE], ~BF_MARK
        jmp .block_next
.garbage:
        lea rsi, [rbx + BLK_HDR]
        call pool_free
.block_next:
        mov rbx, rdx
        jmp .block
.swept:
        ; the kernel's own heap keeps everything: only its marks go
        mov rdi, [kpool]
        lea rbx, [rdi + POOL_HDR]
.kblock:
        mov rax, [rbx + BLK_SIZE]
        mov rcx, rax
        and rcx, -16
        jz .cleared
        and qword [rbx + BLK_SIZE], ~BF_MARK
        add rbx, rcx
        jmp .kblock
.cleared:
        ; the next one when the heap has doubled, or grown by the slack
        mov rax, [kv_pool]
        mov rax, [rax + POOL_USED]
        lea rcx, [rax * 2]
        add rax, GC_SLACK
        cmp rcx, rax
        jae .next_set
        mov rcx, rax
.next_set:
        mov [gc_next], rcx
        inc qword [gc_runs]
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; After a kernel command: a collection when the value heap has grown
; enough. Keeps every register.
gc_maybe:
        push rax
        mov rax, [kv_pool]
        mov rax, [rax + POOL_USED]
        cmp rax, [gc_next]
        pop rax
        jb .no
        call gc_collect
.no:
        ret

; k_gc(v): -> [bytes of kernel values in use, collections so far]
bi_k_gc:
        push rdx
        call vec_new
        mov rdi, rax
        mov rax, [kv_pool]
        mov rax, [rax + POOL_USED]
        call vec_push_int
        mov rax, [gc_runs]
        call vec_push_int
        mov rax, [kv_pages]
        shl rax, 12
        call vec_push_int
        mov rax, rdi
        pop rdx
        jmp bi_vec_out
