; task.inc - Moscow's tasks (ussr_rt.nasm includes it): programs that
; run at the same time, each in an address space of its own, switched by
; the timer.
;
; A task is a program and what it runs inside itself: its own stack, its
; own page tables (the private half, mm.inc), its own heaps (a TLSF pool
; per program run, given back when the run ends), its own copy of the
; kernel's variables and of the runtime's per-program state. Task 0 is
; the one the machine booted with: the kernel and the shell. spawn
; starts another from a program's bytes; it runs until its program
; ends, or kill ends it.
;
; Switching: the PIT ticks at 100 Hz; when a task has had its TASK_SLICE
; the VM gives the CPU away at its next instruction (vm_next), unless it
; is inside a kernel command, which runs to the end or to where it
; waits. Waiting is the scheduler's business, as russia's scheduler and
; ussr's kqueue-like event queue (src/eventq.c) do it: a task that
; sleeps, waits for a key or for another task goes off the run queue
; with what it waits for - a time, console input, a task's end - and the
; scheduler checks the waits each time it picks the next task, round
; robin. With nothing to run the CPU halts until the next interrupt.
;
; The console is task 0's: other tasks read the end of their input.
; Hardware that keeps state - the network card, the screen - is one
; task's at a time: kernel commands of the class "net" or "gui"
; (k_syscall's third argument) belong to the first task that uses them
; until the program that did ends.

MAX_TASKS       equ 32
TASK_STACK      equ 256 * 1024
TASK_SLICE      equ 1                   ; ticks, 10 ms each
TS_FREE         equ 0
TS_READY        equ 1
TS_BLOCKED      equ 2
TS_ZOMBIE       equ 3
W_NONE          equ 0
W_TICK          equ 1
W_TASK          equ 2
W_INPUT         equ 3
OWN_CLASSES     equ 3                   ; 1 net, 2 gui

; --- per-task state of the runtime: swapped with the task

task_var_list:
        dq cur_pool, struct_types, clib_count, cfree_head, sys_depth
        dq sys_areas, sys_areas + 8, sys_areas + 16, sys_areas + 24
        dq vm_result, vm_result + 8, bi_retname
        dq tg_line, tg_len, tg_pos, tg_newline, read_line_eof, last_cr
        dq c_scratch_top, kglob_now, khash_now
task_var_end:
TASK_VARS       equ (task_var_end - task_var_list) / 8

struc TASK
.id:            resq 1
.state:         resq 1
.rsp:           resq 1
.cr3:           resq 1
.stack:         resq 1                  ; kernel heap, 0 for task 0
.depth:         resq 1                  ; program runs: heaps in use
.status:        resq 1
.wait:          resq 1
.wake:          resq 1
.wait_id:       resq 1
.ticks:         resq 1                  ; CPU time
.started:       resq 1
.reaped:        resq 1
.spawn_buf:     resq 1                  ; the program and arguments, until it starts
.spawn_len:     resq 1
.name:          resb 32
.vars:          resq TASK_VARS
.clib:          resb CLIB_MAX * CLIB_SIZE
endstruc

; rax = task: the runtime's per-task state into it. Keeps every register.
vars_save:
        push rcx
        push rdx
        push rsi
        push rdi
        ; the kernel's variables are the kernel program's globals
        mov rcx, [kprog]
        mov rdx, [rcx + PROG.globals]
        mov [kglob_now], rdx
        mov rdx, [rcx + PROG.hash]
        mov [khash_now], rdx
        xor ecx, ecx
.var:
        mov rdx, [task_var_list + rcx * 8]
        mov rdx, [rdx]
        mov [rax + TASK.vars + rcx * 8], rdx
        inc ecx
        cmp ecx, TASK_VARS
        jb .var
        lea rdi, [rax + TASK.clib]
        mov rsi, clib_table
        mov ecx, CLIB_MAX * CLIB_SIZE
        rep movsb
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret

; rax = task: its state back into the runtime. Keeps every register.
vars_load:
        push rcx
        push rdx
        push rsi
        push rdi
        xor ecx, ecx
.var:
        mov rdx, [rax + TASK.vars + rcx * 8]
        mov rdi, [task_var_list + rcx * 8]
        mov [rdi], rdx
        inc ecx
        cmp ecx, TASK_VARS
        jb .var
        mov rcx, [kprog]
        mov rdx, [kglob_now]
        mov [rcx + PROG.globals], rdx
        mov rdx, [khash_now]
        mov [rcx + PROG.hash], rdx
        lea rsi, [rax + TASK.clib]
        mov rdi, clib_table
        mov ecx, CLIB_MAX * CLIB_SIZE
        rep movsb
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret

; --- starting

; The first program run inside the kernel (the shell) makes the tasks:
; the running one is task 0, and the kernel's state as it is now - its
; variables, struct types, C libraries - is kept, copied into the
; kernel's heap, for the tasks spawned later. r13 = the kernel program.
; Keeps every register.
tasks_init:
        cmp qword [tasks_on], 0
        jne .done
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        mov [kprog], r13
        mov rdi, TASK_size * MAX_TASKS
        call alloc
        mov [task_table], rax
        mov qword [rax + TASK.id], 0
        mov qword [rax + TASK.state], TS_READY
        mov qword [rax + TASK.cr3], PAGE_PML4
        mov rcx, [ticks]
        mov [rax + TASK.started], rcx
        mov rdi, rax
        add rdi, TASK.name
        mov rsi, name_task0
        mov ecx, name_task0_len
        rep movsb
        mov [cur_task], rax
        mov qword [next_id], 1
        ; what a new task starts from
        mov rax, [struct_types]
        mov [pristine_types], rax
        mov rax, [clib_count]
        mov [pristine_clibs], rax
        mov rax, [cfree_head]
        mov [pristine_cfree], rax
        mov rdi, pristine_clib_table
        mov rsi, clib_table
        mov ecx, CLIB_MAX * CLIB_SIZE
        rep movsb
        mov rsi, [r13 + PROG.globals]
        mov rcx, [r13 + PROG.nsyms]
        call values_copy
        mov [pristine_globals], rax
        mov rsi, [r13 + PROG.hash]
        mov rcx, [r13 + PROG.nsyms]
        call values_copy
        mov [pristine_hash], rax
        mov ecx, OWN_CLASSES
.own:
        mov qword [own_task + rcx * 8 - 8], -1
        dec ecx
        jnz .own
        mov qword [tasks_on], 1
        mov rax, [ticks]
        mov [slice_start], rax
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
.done:
        ret

; --- deep copies, for a task's own kernel state

; rsi = rcx values -> rax: a copy of them, every string, vector, struct
; and btree in them copied too, in the current heap
values_copy:
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        mov r8, rcx
        mov rdi, rcx
        shl rdi, 4
        add rdi, 16
        call alloc
        mov rdx, rax
        xor ecx, ecx
.one:
        cmp rcx, r8
        jae .done
        push rsi
        push rdi
        mov rdi, rcx
        shl rdi, 4
        add rsi, rdi
        add rdi, rdx
        push rcx
        xor ecx, ecx
        call value_copy
        pop rcx
        pop rdi
        pop rsi
        inc rcx
        jmp .one
.done:
        mov rax, rdx
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret


; rsi = value, rdi = where its copy goes, ecx = depth (below 64; deeper
; is null). Keeps every register.
value_copy:
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
        push r12
        push r13
        push r14
        mov rax, [rsi]
        mov rdx, [rsi + 8]
        mov [rdi], rax
        mov [rdi + 8], rdx
        cmp ecx, 64
        jae .null
        inc ecx
        test rdx, rdx
        jz .out
        cmp rax, T_STR
        je .string
        cmp rax, T_VEC
        je .vector
        cmp rax, T_STRUCT
        je .struct
        cmp rax, T_BTREE
        je .btree
        jmp .out
.null:
        mov qword [rdi], T_NULL
        mov qword [rdi + 8], 0
        jmp .out
.string:
        push rcx
        mov rcx, [rdx + STR_LEN]
        lea rsi, [rdx + STR_BYTES]
        call new_string
        pop rcx
        mov [rdi + 8], rax
        jmp .out
.vector:
        mov rbx, rdi
        mov r13, rdx                    ; the source
        mov edi, 24
        call alloc
        mov r14, rax                    ; the copy
        mov [rbx + 8], r14
        mov rax, [r13 + VEC_COUNT]
        mov [r14 + VEC_COUNT], rax
        mov [r14 + VEC_CAP], rax
        test rax, rax
        jz .out
        mov rdi, rax
        shl rdi, 4
        call alloc
        mov [r14 + VEC_ITEMS], rax
        xor r12d, r12d
.item:
        cmp r12, [r13 + VEC_COUNT]
        jae .out
        mov rsi, r12
        shl rsi, 4
        mov rdi, rsi
        add rsi, [r13 + VEC_ITEMS]
        add rdi, [r14 + VEC_ITEMS]
        call value_copy
        inc r12
        jmp .item
.struct:
        mov rbx, rdi
        mov r13, rdx
        mov rax, [r13 + SOBJ_TYPE]
        mov r12, [rax + STYPE.nfields]
        mov rdi, r12
        shl rdi, 4
        add rdi, SOBJ_FIELDS
        call alloc
        mov r14, rax
        mov [rbx + 8], r14
        mov rax, [r13 + SOBJ_TYPE]
        mov [r14 + SOBJ_TYPE], rax
        xor ebx, ebx
.field:
        cmp rbx, r12
        jae .out
        mov rsi, rbx
        shl rsi, 4
        lea rdi, [r14 + rsi + SOBJ_FIELDS]
        lea rsi, [r13 + rsi + SOBJ_FIELDS]
        call value_copy
        inc rbx
        jmp .field
.btree:
        mov rbx, rdi
        mov r13, rdx                    ; the source tree
        mov edi, BT_SIZE
        call alloc
        mov r14, rax                    ; the new one
        mov [rbx + 8], r14
        ; its leaves, left to right
        mov rbx, [r13 + BT_ROOT]
.first:
        test rbx, rbx
        jz .out
        cmp qword [rbx + BTN_LEAF], 0
        jne .leaf
        mov rbx, [rbx + BTN_VALS]
        jmp .first
.leaf:
        xor r12d, r12d
.pair:
        cmp r12, [rbx + BTN_N]
        jae .next_leaf
        sub rsp, 32                     ; the key, then the value
        mov rsi, r12
        shl rsi, 4
        lea rsi, [rbx + rsi + BTN_KEYS]
        mov rdi, rsp
        call value_copy
        mov rsi, r12
        shl rsi, 4
        lea rsi, [rbx + rsi + BTN_VALS]
        lea rdi, [rsp + 16]
        call value_copy
        mov rdi, r14
        mov rsi, rsp
        lea rdx, [rsp + 16]
        call bt_put                     ; keeps rbx, r12 .. r15
        add rsp, 32
        inc r12
        jmp .pair
.next_leaf:
        mov rbx, [rbx + BTN_NEXT]
        test rbx, rbx
        jnz .leaf
.out:
        pop r14
        pop r13
        pop r12
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        ret

; --- switching

; rdi = task: the CPU goes to it. Comes back when this task is chosen
; again. Keeps every register but rax.
task_switch:
        pushfq
        cli
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        push r11
        mov rax, [cur_task]
        mov [rax + TASK.rsp], rsp
        call vars_save
        mov [cur_task], rdi
        mov rax, [rdi + TASK.cr3]
        mov rcx, cr3
        cmp rax, rcx
        je .same_space
        mov cr3, rax
.same_space:
        mov rax, rdi
        call vars_load
        mov rsp, [rdi + TASK.rsp]
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        popfq
        ret
TASK_FRAME      equ 15 * 8              ; what task_switch pops, then ret

; rax = task -> CF clear when what it waits for has come. Keeps the
; others.
task_can_run:
        push rcx
        mov rcx, [rax + TASK.wait]
        cmp rcx, W_TICK
        je .tick
        cmp rcx, W_TASK
        je .task
        cmp rcx, W_INPUT
        je .input
        clc
        pop rcx
        ret
.tick:
        mov rcx, [ticks]
        cmp rcx, [rax + TASK.wake]
        jb .no
        jmp .yes
.task:
        push rax
        mov rax, [rax + TASK.wait_id]
        call task_by_id
        test rax, rax
        jz .gone
        cmp qword [rax + TASK.state], TS_ZOMBIE
        pop rax
        je .yes
        jmp .no
.gone:
        pop rax
        jmp .yes
.input:
        call input_ready
        jc .no
.yes:
        pop rcx
        clc
        ret
.no:
        pop rcx
        stc
        ret

; -> CF clear when the console has a byte waiting (the key queue, COM1,
; the keyboard while the screen is the console's). Keeps every register.
input_ready:
        push rax
        push rdx
        mov rax, [kq_head]
        cmp rax, [kq_tail]
        jne .yes
        mov dx, COM1 + 5
        in al, dx
        test al, 1
        jnz .yes
        cmp qword [gui_on], 0
        jne .no
        in al, 0x64
        test al, 1
        jnz .yes
.no:
        pop rdx
        pop rax
        stc
        ret
.yes:
        pop rdx
        pop rax
        clc
        ret

; rax = task id -> rax: its entry (any state but free), or 0. Keeps the
; others.
task_by_id:
        push rcx
        push rdx
        mov rdx, rax
        mov rax, [task_table]
        xor ecx, ecx
.next:
        cmp qword [rax + TASK.state], TS_FREE
        je .skip
        cmp [rax + TASK.id], rdx
        je .found
.skip:
        add rax, TASK_size
        inc ecx
        cmp ecx, MAX_TASKS
        jb .next
        xor eax, eax
.found:
        pop rdx
        pop rcx
        ret

; Gives back what a task that ended still holds: its stack, its VM
; areas, its address space. Not the running task. rax = task. Keeps
; every register.
task_reap:
        cmp qword [rax + TASK.reaped], 0
        jne .done
        cmp rax, [cur_task]
        je .done
        push rdi
        push rsi
        push rcx
        mov qword [rax + TASK.reaped], 1
        mov rdi, [kpool]
        mov rsi, [rax + TASK.stack]
        call pool_free
        xor ecx, ecx
.area:
        mov rsi, [rax + TASK.vars + 5 * 8 + rcx * 8]   ; sys_areas
        call pool_free
        inc ecx
        cmp ecx, SYS_AREAS
        jb .area
        mov rsi, [rax + TASK.spawn_buf]
        call pool_free
        mov qword [rax + TASK.spawn_buf], 0
        mov rdi, [rax + TASK.cr3]
        test rdi, rdi
        jz .space_done
        cmp rdi, PAGE_PML4
        je .space_done
        call space_free
        mov qword [rax + TASK.cr3], PAGE_PML4
.space_done:
        pop rcx
        pop rsi
        pop rdi
.done:
        ret

; Picks the task to run: the waits are checked, the next ready task
; after the running one gets the CPU (round robin, the running one last);
; with nothing ready the CPU halts until an interrupt. Keeps every
; register.
schedule:
        push rax
        push rcx
        push rdx
        push rdi
.again:
        ; the waits, and what ended tasks hold
        mov rax, [task_table]
        xor ecx, ecx
.check:
        mov rdx, [rax + TASK.state]
        cmp rdx, TS_BLOCKED
        jne .not_blocked
        call task_can_run
        jc .check_next
        mov qword [rax + TASK.state], TS_READY
        mov qword [rax + TASK.wait], W_NONE
        jmp .check_next
.not_blocked:
        cmp rdx, TS_ZOMBIE
        jne .check_next
        call task_reap
.check_next:
        add rax, TASK_size
        inc ecx
        cmp ecx, MAX_TASKS
        jb .check
        ; the next ready one after the running one
        mov rdx, [cur_task]
        mov rax, rdx
        mov ecx, MAX_TASKS
.pick:
        add rax, TASK_size
        mov rdi, [task_table]
        add rdi, TASK_size * MAX_TASKS
        cmp rax, rdi
        jb .in_table
        mov rax, [task_table]
.in_table:
        cmp qword [rax + TASK.state], TS_READY
        je .chosen
        dec ecx
        jnz .pick
        ; nothing to run: wait for an interrupt
        sti
        hlt
        jmp .again
.chosen:
        mov qword [need_resched], 0
        mov rcx, [ticks]
        mov [slice_start], rcx
        cmp rax, rdx
        je .out
        mov rdi, rax
        call task_switch
.out:
        pop rdi
        pop rdx
        pop rcx
        pop rax
        ret

; The running task gives the CPU to the others that are ready. Keeps
; every register.
task_yield:
        cmp qword [tasks_on], 0
        je .done
        call schedule
.done:
        ret

; The timer said the slice is over (vm_next): another task's turn, when
; this one is not inside a kernel command. Keeps every register.
task_preempt:
        mov qword [need_resched], 0
        cmp qword [sys_depth], 0
        jne .done
        call schedule
.done:
        ret

; rax = tick: the running task sleeps until then. Keeps every register.
task_sleep_until:
        push rcx
        mov rcx, [cur_task]
        mov qword [rcx + TASK.state], TS_BLOCKED
        mov qword [rcx + TASK.wait], W_TICK
        mov [rcx + TASK.wake], rax
        pop rcx
        call schedule
        ret

; The console has nothing for the running task yet: it waits until it
; has (with a terminal's hooks, whose input comes as the screen is
; pumped, a tick at a time). Keeps every register.
con_wait:
        cmp qword [tasks_on], 0
        je .halt
        push rax
        push rcx
        mov rcx, [cur_task]
        cmp qword [con_hook_read], 0
        jne .tick
        mov qword [rcx + TASK.state], TS_BLOCKED
        mov qword [rcx + TASK.wait], W_INPUT
        pop rcx
        pop rax
        call schedule
        ret
.tick:
        mov rax, [ticks]
        inc rax
        pop rcx
        call task_sleep_until
        pop rax
        ret
.halt:
        sti
        hlt
        ret

; -> CF set when the running task is not the console's (task 0 is): its
; reads find the end of the input. Keeps every register.
con_not_mine:
        cmp qword [tasks_on], 0
        je .mine
        push rax
        mov rax, [cur_task]
        cmp qword [rax + TASK.id], 0
        pop rax
        je .mine
        stc
        ret
.mine:
        clc
        ret

; --- heaps per program run

; A program starts inside the running one: its heap, a new TLSF pool in
; the next window of the task's private half. -> rax: the heap before,
; for prog_heap_pop. Keeps the others.
prog_heap_push:
        mov rax, [cur_pool]
        cmp qword [tasks_on], 0
        je .done
        push rcx
        push rdx
        push rdi
        push rsi
        push rax
        mov rcx, [cur_task]
        mov rax, [rcx + TASK.depth]
        cmp rax, POOL_WINDOWS
        jae .deep
        mov rdx, rax
        inc qword [rcx + TASK.depth]
        mov rdi, rdx
        shl rdi, POOL_WINDOW_LOG
        mov rax, USER_BASE
        add rdi, rax
        mov rsi, POOL_WINDOW
        call pool_init
        mov [cur_pool], rax
        pop rax
        pop rsi
        pop rdi
        pop rdx
        pop rcx
.done:
        ret
.deep:
        PUTS msg_too_deep
        jmp sys_halt

; rax = the heap prog_heap_push gave: the program's heap is given back,
; every page of it, and what it took of the network or the screen is
; free again. Keeps every register.
prog_heap_pop:
        cmp qword [tasks_on], 0
        je .done
        push rcx
        push rdx
        push rdi
        push rsi
        mov rcx, [cur_task]
        dec qword [rcx + TASK.depth]
        mov rdx, [rcx + TASK.depth]
        mov rsi, rdx
        shl rsi, POOL_WINDOW_LOG
        mov rdi, USER_BASE
        add rsi, rdi
        mov rdi, cr3
        and rdi, [pte_addr_mask]
        push rdx
        mov rdx, POOL_WINDOW
        call window_free
        pop rdx
        mov [cur_pool], rax
        ; ownership taken at this depth or deeper ends
        push rax
        mov rax, [rcx + TASK.id]
        call own_release_deeper
        pop rax
        pop rsi
        pop rdi
        pop rdx
        pop rcx
.done:
        ret

; rax = task id, rdx = depth: the classes it took at a depth deeper than
; rdx are free again. Keeps every register.
own_release_deeper:
        push rcx
        xor ecx, ecx
.class:
        cmp [own_task + rcx * 8], rax
        jne .next
        cmp [own_depth + rcx * 8], rdx
        jle .next
        mov qword [own_task + rcx * 8], -1
.next:
        inc ecx
        cmp ecx, OWN_CLASSES
        jb .class
        pop rcx
        ret

; eax = the class of a kernel command about to run (0 none; 1, 2 taken
; by the first user; 0x81, 0x82 used only when it is free or ours) ->
; CF set when another task has it. Keeps every register.
own_check:
        cmp qword [tasks_on], 0
        je .ok
        test eax, eax
        jz .ok
        push rax
        push rcx
        push rdx
        mov ecx, eax
        and ecx, 0x7f
        mov rdx, [cur_task]
        mov rdx, [rdx + TASK.id]
        cmp qword [own_task + rcx * 8], -1
        je .free
        cmp [own_task + rcx * 8], rdx
        je .mine
        mov [own_refused], rcx
        pop rdx
        pop rcx
        pop rax
        stc
        ret
.free:
        test eax, 0x80
        jnz .mine
        mov [own_task + rcx * 8], rdx
        mov rdx, [cur_task]
        mov rdx, [rdx + TASK.depth]
        mov [own_depth + rcx * 8], rdx
.mine:
        pop rdx
        pop rcx
        pop rax
.ok:
        clc
        ret

; --- spawn, the start and the end of a task

; rsi = program file bytes, rdx = their count, rdi = the arguments (a
; vector of strings), rcx = the name (a string) -> rax: the new task's
; id, or -1 when the table is full. The task runs when the scheduler
; picks it. Keeps the others.
task_spawn:
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        push r10
        mov r8, rsi
        mov r9, rdx
        mov r10, rdi
        ; a free entry
        mov rbx, [task_table]
        xor eax, eax
.slot:
        cmp qword [rbx + TASK.state], TS_FREE
        je .have
        add rbx, TASK_size
        inc eax
        cmp eax, MAX_TASKS
        jb .slot
        mov rax, -1
        jmp .out
.have:
        mov rdi, rbx
        push rcx
        mov ecx, TASK_size / 8
        xor eax, eax
        rep stosq
        pop rcx
        mov rax, [next_id]
        inc qword [next_id]
        mov [rbx + TASK.id], rax
        mov rax, [ticks]
        mov [rbx + TASK.started], rax
        ; its name, cut to the field
        mov rsi, rcx
        mov rcx, [rsi + STR_LEN]
        cmp rcx, 31
        jbe .name_ok
        mov ecx, 31
.name_ok:
        add rsi, STR_BYTES
        lea rdi, [rbx + TASK.name]
        rep movsb
        ; the program and its arguments, packed into the kernel's heap
        ; (the new address space does not see this one's heap): the file,
        ; then the arguments as count, then length and bytes each
        mov rcx, r9
        add rcx, 16
        mov rax, [r10 + VEC_COUNT]
        xor edx, edx
.arg_size:
        cmp rdx, [r10 + VEC_COUNT]
        jae .sized
        mov rsi, rdx
        shl rsi, 4
        add rsi, [r10 + VEC_ITEMS]
        mov rsi, [rsi + 8]
        add rcx, [rsi + STR_LEN]
        add rcx, 8
        inc rdx
        jmp .arg_size
.sized:
        mov rdi, [kpool]
        mov rsi, rcx
        call pool_alloc
        test rax, rax
        jz .full
        mov [rbx + TASK.spawn_buf], rax
        mov [rbx + TASK.spawn_len], r9
        mov rdi, rax
        mov rsi, r8
        mov rcx, r9
        rep movsb
        mov rax, [r10 + VEC_COUNT]
        stosq
        xor edx, edx
.arg_copy:
        cmp rdx, [r10 + VEC_COUNT]
        jae .packed
        mov rsi, rdx
        shl rsi, 4
        add rsi, [r10 + VEC_ITEMS]
        mov rsi, [rsi + 8]
        mov rcx, [rsi + STR_LEN]
        mov rax, rcx
        stosq
        add rsi, STR_BYTES
        rep movsb
        inc rdx
        jmp .arg_copy
.packed:
        ; its stack, its VM areas, its address space
        mov rdi, [kpool]
        mov rsi, TASK_STACK
        call pool_alloc
        test rax, rax
        jz .full
        mov [rbx + TASK.stack], rax
        xor ecx, ecx
.area:
        mov rsi, VM_AREA
        call pool_alloc
        test rax, rax
        jz .full
        mov [rbx + TASK.vars + 5 * 8 + rcx * 8], rax
        inc ecx
        cmp ecx, SYS_AREAS
        jb .area
        call space_new
        mov [rbx + TASK.cr3], rax
        ; the runtime's state it starts with: the kernel's as it booted
        mov rax, [kpool]
        mov [rbx + TASK.vars + 0 * 8], rax              ; cur_pool
        mov rax, [pristine_types]
        mov [rbx + TASK.vars + 1 * 8], rax              ; struct_types
        mov rax, [pristine_clibs]
        mov [rbx + TASK.vars + 2 * 8], rax              ; clib_count
        mov rax, [pristine_cfree]
        mov [rbx + TASK.vars + 3 * 8], rax              ; cfree_head
        mov rax, [c_scratch]
        mov [rbx + TASK.vars + 18 * 8], rax             ; c_scratch_top
        mov rax, [pristine_globals]
        mov [rbx + TASK.vars + 19 * 8], rax             ; until task_entry copies them
        mov rax, [pristine_hash]
        mov [rbx + TASK.vars + 20 * 8], rax
        lea rdi, [rbx + TASK.clib]
        mov rsi, pristine_clib_table
        mov ecx, CLIB_MAX * CLIB_SIZE
        rep movsb
        ; a stack that task_switch returns from into task_entry
        mov rax, [rbx + TASK.stack]
        add rax, TASK_STACK - 64
        and rax, -16
        mov qword [rax], task_entry
        sub rax, 8
        mov qword [rax], 0x202          ; the flags: interrupts on
        sub rax, 14 * 8
        mov rdi, rax
        mov ecx, 14
        push rax
        xor eax, eax
        rep stosq
        pop rax
        mov [rbx + TASK.rsp], rax
        mov qword [rbx + TASK.state], TS_READY
        mov rax, [rbx + TASK.id]
        jmp .out
.full:
        ; no room in the kernel's heap: what was taken goes back
        mov qword [rbx + TASK.state], TS_ZOMBIE
        mov qword [rbx + TASK.status], 1
        mov rax, rbx
        call task_reap
        mov qword [rbx + TASK.state], TS_FREE
        mov rax, -1
.out:
        pop r10
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

; A new task's first instructions, on its own stack in its own address
; space: its heap, its copy of the kernel's variables, its program and
; arguments out of the kernel's heap, then the program; its end ends
; the task.
task_entry:
        sti
        mov rbx, [cur_task]
        call prog_heap_push
        ; the kernel's variables, its own
        mov rsi, [pristine_globals]
        mov rcx, [kprog]
        mov rcx, [rcx + PROG.nsyms]
        call values_copy
        mov rcx, [kprog]
        mov [rcx + PROG.globals], rax
        mov rsi, [pristine_hash]
        mov rcx, [kprog]
        mov rcx, [rcx + PROG.nsyms]
        call values_copy
        mov rcx, [kprog]
        mov [rcx + PROG.hash], rax
        ; the program file
        mov rdi, [rbx + TASK.spawn_len]
        add rdi, 8
        call alloc
        mov r12, rax
        mov rdi, rax
        mov rsi, [rbx + TASK.spawn_buf]
        mov rcx, [rbx + TASK.spawn_len]
        rep movsb
        ; the arguments
        lodsq
        mov r14, rax
        call vec_new
        mov r13, rax
.arg:
        test r14, r14
        jz .args_done
        lodsq
        mov rcx, rax
        call new_string
        add rsi, rcx
        push rsi
        sub rsp, 16
        mov qword [rsp], T_STR
        mov [rsp + 8], rax
        mov rdi, r13
        mov rsi, rsp
        call vec_push
        add rsp, 16
        pop rsi
        dec r14
        jmp .arg
.args_done:
        mov rdi, [kpool]
        mov rsi, [rbx + TASK.spawn_buf]
        call pool_free
        mov qword [rbx + TASK.spawn_buf], 0
        mov rsi, r12
        mov rdx, [rbx + TASK.spawn_len]
        call rt_load
        test rax, rax
        jz .failed
        mov rdi, rax
        mov rdx, r13
        call vm_run
        jmp task_exit
.failed:
        mov eax, 1
        ; falls through

; eax = status: the running task ends. It leaves its address space,
; which the scheduler gives back with its stack once it runs elsewhere.
task_exit:
        cli
        mov rbx, [cur_task]
        movsxd rax, eax
        mov [rbx + TASK.status], rax
        ; ownership it took
        mov rax, [rbx + TASK.id]
        mov rdx, -1
        call own_release_deeper
        mov qword [rbx + TASK.state], TS_ZOMBIE
        mov qword [rbx + TASK.reaped], 0
        ; its stack and address space go once another task runs
        call schedule
        ; never chosen again
        jmp sys_halt

; --- the builtins

; k_spawn(id): bytes args name -> a new task running the program file
; bytes (a vector of byte values) with args (a vector of strings); its
; id, or -1 when the table is full. The kernel's spawn calls it.
bi_k_spawn:
        cmp esi, 3
        jne .bad
        cmp qword [rdi], T_VEC
        jne .bad
        cmp qword [rdi + 16], T_VEC
        jne .bad
        cmp qword [rdi + 32], T_STR
        jne .bad
        push rdx
        push rbx
        push r12
        call tasks_init
        mov rbx, rdi
        ; every argument a string
        mov r8, [rbx + 24]
        xor ecx, ecx
.check:
        cmp rcx, [r8 + VEC_COUNT]
        jae .checked
        mov rax, rcx
        shl rax, 4
        add rax, [r8 + VEC_ITEMS]
        cmp qword [rax], T_STR
        jne .bad_pop
        inc rcx
        jmp .check
.checked:
        ; the bytes, packed
        mov r8, [rbx + 8]
        mov rcx, [r8 + VEC_COUNT]
        lea rdi, [rcx + 8]
        call alloc
        mov r12, rax
        mov rsi, [r8 + VEC_ITEMS]
        xor edx, edx
.pack:
        cmp rdx, rcx
        jae .packed
        mov r9, [rsi + 8]
        mov [r12 + rdx], r9b
        add rsi, 16
        inc rdx
        jmp .pack
.packed:
        mov rsi, r12
        mov rdx, rcx
        mov rdi, [rbx + 24]
        mov rcx, [rbx + 40]
        call task_spawn
        pop r12
        pop rbx
        pop rdx
        jmp bi_int_out
.bad_pop:
        pop r12
        pop rbx
        pop rdx
.bad:
        mov eax, e_k_spawn
        jmp bi_error

; k_tasks(v): -> [[id, name, state, cpu ms, KiB], ...], every task there
; is; state "run", "wait" or "done N" (its status)
bi_k_tasks:
        push rdx
        push rbx
        push r12
        push r13
        call vec_new
        mov r12, rax
        cmp qword [tasks_on], 0
        je .done
        mov rbx, [task_table]
        xor r13d, r13d
.task:
        cmp qword [rbx + TASK.state], TS_FREE
        je .next
        call vec_new
        mov rdi, rax
        mov rax, [rbx + TASK.id]
        call vec_push_int
        ; the name
        push rdi
        lea rdi, [rbx + TASK.name]
        mov rsi, rdi
        call strlen_z
        pop rdi
        call new_string
        call vec_push_str
        ; the state
        mov rax, [rbx + TASK.state]
        mov rsi, s_ts_run
        cmp rax, TS_READY
        je .state
        mov rsi, s_ts_wait
        cmp rax, TS_BLOCKED
        je .state
        ; done: "done N"
        push rdi
        mov rdi, ts_text
        mov rsi, s_ts_done
        mov ecx, 5
        rep movsb
        mov rax, [rbx + TASK.status]
        call dec_put
        mov byte [rdi], 0
        pop rdi
        mov rsi, ts_text
.state:
        push rcx
        call new_string_z
        pop rcx
        call vec_push_str
        mov rax, [rbx + TASK.ticks]
        imul rax, rax, 10
        call vec_push_int
        ; memory: the private pages it has, in KiB
        xor eax, eax
        cmp qword [rbx + TASK.state], TS_ZOMBIE
        je .mem
        push rdi
        mov rdi, [rbx + TASK.cr3]
        call space_pages
        pop rdi
        shl rax, 2
.mem:
        call vec_push_int
        mov rax, rdi
        mov rdi, r12
        mov rsi, rsp
        sub rsp, 16
        mov qword [rsp], T_VEC
        mov [rsp + 8], rax
        mov rsi, rsp
        call vec_push
        add rsp, 16
.next:
        add rbx, TASK_size
        inc r13
        cmp r13, MAX_TASKS
        jb .task
.done:
        mov rax, r12
        pop r13
        pop r12
        pop rbx
        pop rdx
        jmp bi_vec_out

; rdi = PML4 -> rax: the pages mapped in its private half. Keeps the
; others.
space_pages:
        push rcx
        push rdx
        push rsi
        push r8
        push r9
        push r10
        xor eax, eax
        mov rsi, [rdi + 8]
        test rsi, PTE_P
        jz .done
        and rsi, [pte_addr_mask]
        xor ecx, ecx                    ; PDPT entries
.pdpt:
        mov r8, [rsi + rcx * 8]
        test r8, PTE_P
        jz .pdpt_next
        and r8, [pte_addr_mask]
        xor edx, edx
.pd:
        mov r9, [r8 + rdx * 8]
        test r9, PTE_P
        jz .pd_next
        and r9, [pte_addr_mask]
        xor r10d, r10d
.pt:
        test qword [r9 + r10 * 8], PTE_P
        jz .pt_next
        inc rax
.pt_next:
        inc r10d
        cmp r10d, 512
        jb .pt
.pd_next:
        inc edx
        cmp edx, 512
        jb .pd
.pdpt_next:
        inc ecx
        cmp ecx, 512
        jb .pdpt
.done:
        pop r10
        pop r9
        pop r8
        pop rsi
        pop rdx
        pop rcx
        ret

; k_kill(r): id -> 0 when the task is ended (status 137), -1 when there
; is no such task running (task 0, the kernel's, never)
bi_k_kill:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_INT
        jne .bad
        mov rax, -1
        cmp qword [tasks_on], 0
        je .out
        mov rax, [rdi + 8]
        test rax, rax
        jz .refuse
        call task_by_id
        test rax, rax
        jz .refuse
        cmp qword [rax + TASK.state], TS_ZOMBIE
        je .refuse
        cmp rax, [cur_task]
        jne .other
        mov eax, 137
        jmp task_exit
.other:
        push rdx
        push rax
        mov rax, [rax + TASK.id]
        mov rdx, -1
        call own_release_deeper
        pop rax
        pop rdx
        mov qword [rax + TASK.status], 137
        mov qword [rax + TASK.state], TS_ZOMBIE
        mov qword [rax + TASK.reaped], 0
        call task_reap
        xor eax, eax
        jmp .out
.refuse:
        mov rax, -1
.out:
        jmp bi_int_out
.bad:
        mov eax, e_k_kill
        jmp bi_error

; k_wait(status): id -> waits for the task to end; its status, or -1
; when there is no such task (or it is the one waiting)
bi_k_wait:
        cmp esi, 1
        jne .bad
        cmp qword [rdi], T_INT
        jne .bad
        mov rax, -1
        cmp qword [tasks_on], 0
        je .out
        push rbx
        mov rbx, [rdi + 8]
.again:
        mov rax, rbx
        call task_by_id
        test rax, rax
        jz .none
        cmp rax, [cur_task]
        je .none
        cmp qword [rax + TASK.state], TS_ZOMBIE
        je .ended
        push rcx
        mov rcx, [cur_task]
        mov qword [rcx + TASK.state], TS_BLOCKED
        mov qword [rcx + TASK.wait], W_TASK
        mov [rcx + TASK.wait_id], rbx
        pop rcx
        call schedule
        jmp .again
.ended:
        call task_reap
        push qword [rax + TASK.status]
        mov qword [rax + TASK.state], TS_FREE
        pop rax
        pop rbx
        jmp .out
.none:
        pop rbx
        mov rax, -1
.out:
        jmp bi_int_out
.bad:
        mov eax, e_k_wait
        jmp bi_error

; k_task(id): -> the running task's id (0 before there are tasks)
bi_k_task:
        xor eax, eax
        cmp qword [tasks_on], 0
        je .out
        mov rax, [cur_task]
        mov rax, [rax + TASK.id]
.out:
        jmp bi_int_out

msg_no_frames:  db "USSR runtime: out of memory (no free page frames)", 10, 0
msg_pf:         db "USSR runtime: page fault at ", 0
msg_too_deep:   db "USSR runtime: programs nested too deep", 10, 0
msg_oom_task:   db "USSR runtime: the task is out of memory and ends", 10, 0
msg_own_net:    db "USSR: the network is another task's now: ", 0
msg_own_gui:    db "USSR: the screen is another task's now: ", 0
name_task0:     db "moscow"
name_task0_len  equ $ - name_task0
s_ts_run:       db "run", 0
s_ts_wait:      db "wait", 0
s_ts_done:      db "done "
e_k_spawn:      db "k_spawn(id): bytes args name (a vector of bytes, of strings, a string)", 0
e_k_kill:       db "k_kill(r): id", 0
e_k_wait:       db "k_wait(status): id", 0
                align 8
pte_addr_mask:  dq PTE_ADDR
