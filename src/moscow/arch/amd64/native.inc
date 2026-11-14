; native.inc - Linux programs on Moscow (ussr_rt.nasm includes it): a
; static PIE x86-64 ELF program (built against musl, for instance) runs in
; ring 3 in its task's private half, and the kernel answers the Linux
; system calls it makes. k_exec_bytes and spawn come here when a program
; file starts with the ELF signature.
;
; A program run has a window of the private half (mm.inc), and in it
;   + 4 GiB          the program's image, then its brk
;   + 8 GiB          what mmap gives
;   top - 4 KiB      its stack, with argv, envp and auxv as Linux lays them
; all paged on demand. Only static PIE fits: lower addresses are the
; kernel's identity map, and there is no dynamic linker to run.
;
; The SYSCALL instruction comes to native_syscall. What needs only the CPU
; and the page tables is answered here (memory, time, signals as no-ops,
; ids); the rest goes to the kernel command __linux (linux.su), with integer
; arguments and an integer result, which reaches the program's memory
; through k_ustr, k_uput and the like. Those check an address against the
; window, so a bad pointer is -EFAULT and not the kernel's page fault.
;
; The timer takes the CPU from a Linux program in ring 3 even when a kernel
; command (run) started it: that command waits at k_exec_bytes until it ends.
; An exception there ends the program with status 139; the kernel goes on.
;
; Starting programs, as musl's posix_spawn does it: clone(CLONE_VM |
; CLONE_VFORK) on a stack of its own, the child's file actions on a copy of
; the descriptors, then execve. The child runs in the parent's memory until
; execve, which runs the new program nested, in the next window, to its end
; (linux.su); then the parent goes on from its clone with the child's pid,
; and wait4 gives its status. So the parent waits for the child to end, not
; only to start. execve outside a vfork child runs the program the same way
; and ends with its status: it has become it. fork, a copy of the memory,
; is not here (ENOSYS).

NATIVE_BASE     equ 1 << 32
NATIVE_BRK_END  equ 8 << 30
NATIVE_MMAP     equ 8 << 30
NATIVE_MMAP_END equ 15 << 30
NATIVE_IMAGE_MAX equ 2 << 30
NATIVE_ARGS     equ 64
MSR_EFER        equ 0xc0000080
MSR_STAR        equ 0xc0000081
MSR_LSTAR       equ 0xc0000082
MSR_FMASK       equ 0xc0000084
MSR_FS_BASE     equ 0xc0000100
PT_INTERP       equ 3
E_NOMEM         equ 12
E_FAULT         equ 14
E_NODEV         equ 19
E_INVAL         equ 22
E_NOTTY         equ 25
E_IO            equ 5
E_AGAIN         equ 11
E_NOSYS         equ 38
CLONE_VM        equ 0x100
CLONE_VFORK     equ 0x4000
; __linux's own numbers, past Linux's: a vfork child's descriptors, its end
LX_VFORK_BEGIN  equ 1001
LX_VFORK_EXIT   equ 1002
; native_syscall's frame from rbp: rdi rsi rdx r10 r8 r9 r15 r14 r13 r12
; rbp rbx r11 rcx, then the program's rsp
N_FRAME         equ 15
FRAME_RSP       equ 14 * 8

; SYSCALL on: from ring 3 to the kernel's segments (STAR's 0x08), back with
; SYSRET to 0x1b and 0x23 (STAR's 0x13 + 8 and + 16: gdt_init's order).
; The 3 is ring 3's RPL, in STAR itself: Intel's SYSRET adds it to both
; selectors, AMD's only to CS, and the program would run with SS 0x18,
; which the next iretq back to it (after a page fault, the timer) refuses
; with #GP(0x18) - on a real CPU, VirtualBox or QEMU with KVM/WHPX; QEMU's
; own CPU does not check.
native_init:
        mov ecx, MSR_EFER
        rdmsr
        or eax, 1
        wrmsr
        mov ecx, MSR_STAR
        xor eax, eax
        mov edx, ((0x10 | 3) << 16) | 0x08
        wrmsr
        mov ecx, MSR_LSTAR
        mov eax, native_syscall
        xor edx, edx
        wrmsr
        mov ecx, MSR_FMASK
        mov eax, 0x700                  ; TF, IF, DF off in the kernel
        xor edx, edx
        wrmsr
        ret

; rax = address, rcx = bytes -> CF clear when they lie in the running
; program's windows. Keeps every register.
n_uok:
        push rdx
        mov rdx, USER_BASE
        cmp rax, rdx
        jb .bad
        lea rdx, [rax + rcx]
        cmp rdx, rax
        jb .bad
        push rax
        push rcx
        mov rax, [cur_task]
        mov rax, [rax + TASK.depth]
        shl rax, POOL_WINDOW_LOG
        mov rcx, USER_BASE
        add rax, rcx
        cmp rdx, rax
        pop rcx
        pop rax
        ja .bad
        pop rdx
        clc
        ret
.bad:
        pop rdx
        stc
        ret

; rbp = the top of the stack being laid out, rsi = bytes, rcx = their count:
; a NUL-terminated copy under it -> rbp: the copy. Keeps the others.
n_push_text:
        push rcx
        push rsi
        push rdi
        lea rax, [rcx + 1]
        sub rbp, rax
        mov rdi, rbp
        rep movsb
        mov byte [rdi], 0
        pop rdi
        pop rsi
        pop rcx
        ret

; rsi = a program file, rdx = its size, r8 = its argv (a vector of strings,
; or 0): runs it in the window prog_heap_push made -> eax: its status.
native_run:
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        pushfq
        ; a program started from another one's system call (execve):
        ; the one that called keeps its state
        mov ecx, N_SAVED
.save:
        mov rax, [n_saved_list + rcx * 8 - 8]
        push qword [rax]
        loop .save
        mov qword [n_vf_active], 0
        sub rsp, 8 * (NATIVE_ARGS + 8)
        mov rbx, rsi
        mov r12, rdx
        mov r13, r8
        cmp r12, 64
        jb .refuse
        cmp word [rbx + 16], 3          ; ET_DYN
        jne .refuse
        cmp word [rbx + 18], 62         ; x86-64
        jne .refuse
        ; the window, the image's place in it
        mov rax, [cur_task]
        mov rax, [rax + TASK.depth]
        dec rax
        shl rax, POOL_WINDOW_LOG
        mov rcx, USER_BASE
        lea r14, [rax + rcx]
        mov rax, NATIVE_BASE
        lea r15, [r14 + rax]
        ; the segments
        mov rax, [rbx + 32]
        lea rax, [rax + 64]
        cmp rax, r12
        ja .refuse
        mov rsi, [rbx + 32]
        add rsi, rbx
        movzx ecx, word [rbx + 56]
        movzx edx, word [rbx + 54]
        xor ebp, ebp                    ; where the image ends
.segment:
        test ecx, ecx
        jz .loaded
        mov eax, [rsi]
        cmp eax, PT_INTERP
        je .refuse                      ; a dynamic program
        cmp eax, PT_LOAD
        jne .segment_next
        mov rax, [rsi + 8]
        add rax, [rsi + 32]
        jc .refuse
        cmp rax, r12
        ja .refuse
        mov rax, [rsi + 16]
        add rax, [rsi + 40]
        jc .refuse
        mov rdi, NATIVE_IMAGE_MAX
        cmp rax, rdi
        ja .refuse
        cmp rax, rbp
        jbe .copy
        mov rbp, rax
.copy:
        push rcx
        push rsi
        mov rdi, [rsi + 16]
        add rdi, r15
        mov rcx, [rsi + 32]
        mov rsi, [rsi + 8]
        add rsi, rbx
        rep movsb
        pop rsi
        pop rcx
.segment_next:
        add rsi, rdx
        dec ecx
        jmp .segment
.loaded:
        lea rax, [r15 + rbp + 4095]
        and rax, -4096
        mov [n_brk_start], rax
        mov [n_brk], rax
        mov rax, NATIVE_MMAP
        add rax, r14
        mov [n_mmap], rax
        mov [n_window], r14
        ; the stack: the strings first, from the top of the window
        mov rax, POOL_WINDOW - 4096
        lea rbp, [r14 + rax]
        xor edx, edx                    ; argc
        test r13, r13
        jz .argv_done
.arg:
        cmp rdx, [r13 + VEC_COUNT]
        jae .argv_done
        cmp rdx, NATIVE_ARGS
        jae .argv_done
        mov rax, rdx
        shl rax, 4
        add rax, [r13 + VEC_ITEMS]
        cmp qword [rax], T_STR
        jne .refuse
        mov rax, [rax + 8]
        lea rsi, [rax + STR_BYTES]
        mov rcx, [rax + STR_LEN]
        call n_push_text
        mov [rsp + rdx * 8], rbp
        inc rdx
        jmp .arg
.argv_done:
        mov r13, rdx                    ; argc
        ; the environment
        xor r8d, r8d
.env:
        mov rsi, [n_env + r8 * 8]
        test rsi, rsi
        jz .env_done
        call strlen_z
        call n_push_text
        mov [n_env_ptrs + r8 * 8], rbp
        inc r8
        jmp .env
.env_done:
        ; AT_RANDOM's bytes
        sub rbp, 16
        call random64
        mov [rbp], rax
        call random64
        mov [rbp + 8], rax
        mov r9, rbp
        and rbp, -16
        ; argc, argv and its NULL, envp and its NULL, auxv: even in all
        lea rax, [r13 + r8 + 3 + N_AUX * 2]
        test al, 1
        jz .even
        sub rbp, 8
.even:
        shl rax, 3
        sub rbp, rax
        mov rdi, rbp
        mov [rdi], r13
        add rdi, 8
        xor ecx, ecx
.put_argv:
        cmp rcx, r13
        jae .argv_put
        mov rax, [rsp + rcx * 8]
        stosq
        inc rcx
        jmp .put_argv
.argv_put:
        xor eax, eax
        stosq
        xor ecx, ecx
.put_env:
        cmp rcx, r8
        jae .env_put
        mov rax, [n_env_ptrs + rcx * 8]
        stosq
        inc rcx
        jmp .put_env
.env_put:
        xor eax, eax
        stosq
        ; auxv: N_AUX pairs
        mov eax, 3                      ; AT_PHDR
        stosq
        mov rax, [rbx + 32]
        add rax, r15
        stosq
        mov eax, 4                      ; AT_PHENT
        stosq
        movzx eax, word [rbx + 54]
        stosq
        mov eax, 5                      ; AT_PHNUM
        stosq
        movzx eax, word [rbx + 56]
        stosq
        mov eax, 6                      ; AT_PAGESZ
        stosq
        mov eax, 4096
        stosq
        mov eax, 9                      ; AT_ENTRY
        stosq
        mov rax, [rbx + 24]
        add rax, r15
        stosq
        mov eax, 17                     ; AT_CLKTCK
        stosq
        mov eax, 100
        stosq
        mov eax, 25                     ; AT_RANDOM
        stosq
        mov rax, r9
        stosq
        xor eax, eax                    ; AT_NULL
        stosq
        stosq
        ; what runs in the kernel for it comes from the kernel's values
        mov rax, [kv_pool]
        mov [cur_pool], rax
        mov rdx, [rbx + 24]
        add rdx, r15
        mov rcx, rbp
        ; in
        cli
        mov [c3_ksp], rsp
        mov [tss + 4], rsp
        mov qword [native_on], 1
        mov qword [n_fsbase], 0
        push rcx
        push rdx
        mov ecx, MSR_FS_BASE
        xor eax, eax
        xor edx, edx
        wrmsr
        pop rdx
        pop rcx
        push qword USER_SS
        push rcx
        push qword 0x202
        push qword USER_CS
        push rdx
        xor eax, eax
        xor ebx, ebx
        xor ecx, ecx
        xor edx, edx                    ; no function for atexit
        xor esi, esi
        xor edi, edi
        xor ebp, ebp
        xor r8d, r8d
        xor r9d, r9d
        xor r10d, r10d
        xor r11d, r11d
        xor r12d, r12d
        xor r13d, r13d
        xor r14d, r14d
        xor r15d, r15d
        iretq
.refuse:
        PUTS msg_native_refused
        mov eax, 126
        add rsp, 8 * (NATIVE_ARGS + 8)
        jmp native_restore

; eax = status: the Linux program has ended, native_run returns it. From
; the system call gate or an exception, whatever the stack holds.
native_exit:
        cli
        mov rsp, [c3_ksp]
        mov cx, 0x10
        mov ds, cx
        mov es, cx
        mov ss, cx
        mov qword [native_on], 0
        mov qword [n_fsbase], 0
        push rax
        mov ecx, MSR_FS_BASE
        xor eax, eax
        xor edx, edx
        wrmsr
        pop rax
        add rsp, 8 * (NATIVE_ARGS + 8)
; eax = status: what native_run kept goes back, the thread pointer and the
; interrupt stack with it
native_restore:
        mov edx, eax
        xor ecx, ecx
.restore:
        mov rax, [n_saved_list + rcx * 8]
        pop qword [rax]
        inc ecx
        cmp ecx, N_SAVED
        jb .restore
        mov rax, [c3_ksp]
        mov [tss + 4], rax
        push rdx
        mov rax, [n_fsbase]
        mov rdx, rax
        shr rdx, 32
        mov ecx, MSR_FS_BASE
        wrmsr
        pop rdx
        mov eax, edx
        popfq
native_pops:
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        ret

; An exception in ring 3 in a Linux program (exc_common, through c3_fault):
; [rsp] the vector, [rsp + 16] rip.
native_fault:
        mov rax, [rsp]
        mov [c3_vector], rax
        mov rax, [rsp + 16]
        mov [c3_rip], rax
        mov rax, cr2
        mov [c3_addr], rax
        PUTS msg_native_broke
        mov rax, [c3_vector]
        call print_int
        PUTS msg_exc_rip
        mov rax, [c3_rip]
        call print_hex
        PUTS msg_c3_addr
        mov rax, [c3_addr]
        call print_hex
        PUTC 10
        mov eax, 139
        jmp native_exit

; -> CF set when the running task is over its memory limit. Keeps every
; register.
n_over_limit:
        push rax
        push rcx
        mov rcx, [cur_task]
        mov rax, [rcx + TASK.limit]
        test rax, rax
        jz .fits
        cmp [rcx + TASK.pages], rax
        jbe .fits
        pop rcx
        pop rax
        stc
        ret
.fits:
        pop rcx
        pop rax
        clc
        ret

; The over-limit ending, on the way out of ring 3
n_end_over:
        PUTS msg_over_limit
        mov eax, 1
        jmp native_exit

; The timer in ring 3 (timer_isr): another task's turn. [rsp] the rax
; timer_isr pushed, then the interrupt's frame. The SSE registers are the
; program's: task_switch does not keep them.
native_preempt:
        mov qword [need_resched], 0
        call n_over_limit
        jc n_end_over
        mov rax, rsp
        and rsp, -16
        sub rsp, 528
        mov [rsp + 512], rax
        fxsave [rsp]
        call schedule
        fxrstor [rsp]
        mov rsp, [rsp + 512]
        pop rax
        iretq

; --- the system call gate

; SYSCALL: rcx the return address, r11 the flags, interrupts off, the stack
; still the program's. Every register comes back but rax, rcx and r11.
native_syscall:
        mov [n_user_rsp], rsp
        mov rsp, [c3_ksp]
        push qword [n_user_rsp]
        push rcx
        push r11
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        push r9
        push r8
        push r10
        push rdx
        push rsi
        push rdi
        mov rbp, rsp
        and rsp, -16
        sub rsp, 512
        fxsave [rsp]
        cld
        sti
        call n_dispatch
        cmp qword [need_resched], 0
        je .no_turn
        mov qword [need_resched], 0
        call schedule
.no_turn:
        call n_over_limit
        jc n_end_over
        cli
        fxrstor [rsp]
        mov rsp, rbp
        pop rdi
        pop rsi
        pop rdx
        pop r10
        pop r8
        pop r9
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        pop r11
        pop rcx
        and r11, 0xcd5
        or r11, 0x202
        pop rsp
        o64 sysret

; rax = the number, rdi rsi rdx r10 r8 r9 = the arguments -> rax
n_dispatch:
        mov ecx, n_table
.next:
        mov rbx, [rcx]
        cmp rbx, -1
        je n_kernel
        cmp rbx, rax
        je .found
        add rcx, 16
        jmp .next
.found:
        jmp [rcx + 8]

; The kernel command __linux: number and arguments as integers
n_kernel:
        push rbp
        mov rbp, rsp
        sub rsp, 128
        mov rbx, rsp
        mov qword [rbx], T_INT
        mov [rbx + 8], rax
        mov qword [rbx + 16], T_INT
        mov [rbx + 24], rdi
        mov qword [rbx + 32], T_INT
        mov [rbx + 40], rsi
        mov qword [rbx + 48], T_INT
        mov [rbx + 56], rdx
        mov qword [rbx + 64], T_INT
        mov [rbx + 72], r10
        mov qword [rbx + 80], T_INT
        mov [rbx + 88], r8
        mov qword [rbx + 96], T_INT
        mov [rbx + 104], r9
        mov rax, [n_linux_cmd]
        test rax, rax
        jnz .have
        mov esi, s_linux
        mov ecx, s_linux_len
        mov edx, 1
        call sys_find
        cmp eax, -1
        je .none
        inc rax
        mov [n_linux_cmd], rax
.have:
        dec rax
        mov rdi, rbx
        mov esi, 7
        lea rdx, [rbx + 112]
        call sys_call
        test eax, eax
        jnz .failed
        cmp qword [rbx + 112], T_INT
        jne .failed
        mov rax, [rbx + 120]
        jmp .out
.none:
        mov rax, -38
        jmp .out
.failed:
        mov rax, -E_IO
.out:
        mov rsp, rbp
        pop rbp
        ret

; --- what the gate answers itself

; brk(addr)
n_sys_brk:
        mov rax, [n_brk]
        cmp rdi, [n_brk_start]
        jb .out
        mov rcx, [n_window]
        mov rdx, NATIVE_BRK_END
        add rcx, rdx
        cmp rdi, rcx
        ja .out
        cmp rdi, rax
        jae .set
        ; smaller: the pages above go back
        push rdi
        lea rsi, [rdi + 4095]
        and rsi, -4096
        mov rdx, rax
        sub rdx, rsi
        jbe .shrunk
        mov rdi, rsi
        mov rsi, rdx
        call n_unmap
.shrunk:
        pop rdi
.set:
        mov [n_brk], rdi
        mov rax, rdi
.out:
        ret

; mmap(addr, len, prot, flags, fd, off): anonymous memory
n_sys_mmap:
        test r10, 0x20                  ; MAP_ANONYMOUS
        jz .no_file
        test rsi, rsi
        jz .invalid
        add rsi, 4095
        jc .no_memory
        and rsi, -4096
        test r10, 0x10                  ; MAP_FIXED
        jnz .fixed
        mov rax, [n_mmap]
        lea rdx, [rax + rsi]
        cmp rdx, rax
        jb .no_memory
        mov rcx, [n_window]
        mov rbx, NATIVE_MMAP_END
        add rcx, rbx
        cmp rdx, rcx
        ja .no_memory
        mov [n_mmap], rdx
        ret
.fixed:
        ; zeroed, as Linux gives it: what was there goes
        test rdi, 4095
        jnz .invalid
        push rdi
        call n_unmap
        pop rax
        ret
.no_file:
        mov rax, -E_NODEV
        ret
.invalid:
        mov rax, -E_INVAL
        ret
.no_memory:
        mov rax, -E_NOMEM
        ret

; munmap(addr, len)
n_sys_munmap:
        test rdi, 4095
        jnz .invalid
        add rsi, 4095
        and rsi, -4096
        call n_unmap
        jc .invalid
        ; the last mapping: its address space is used again
        lea rax, [rdi + rsi]
        cmp rax, [n_mmap]
        jne .done
        mov [n_mmap], rdi
.done:
        xor eax, eax
        ret
.invalid:
        mov rax, -E_INVAL
        ret

; rdi = a page's address, rsi = bytes (whole pages) in the program's window
; above the image's start: unmapped, their frames given back; CF set when
; they are not. Keeps rdi, rsi.
n_unmap:
        push rdi
        push rsi
        mov rax, [n_window]
        mov rcx, NATIVE_BASE
        add rax, rcx
        cmp rdi, rax
        jb .bad
        lea rdx, [rdi + rsi]
        cmp rdx, rdi
        jb .bad
        mov rax, [n_window]
        mov rcx, POOL_WINDOW
        add rax, rcx
        cmp rdx, rax
        ja .bad
        mov r8, rdi
        mov r9, rdx
.page:
        cmp r8, r9
        jae .done
        ; the walk, making nothing
        mov rax, cr3
        and rax, [pte_addr_mask]
        mov ecx, 39
.level:
        mov rdx, r8
        shr rdx, cl
        and edx, 511
        lea rdx, [rax + rdx * 8]
        mov rax, [rdx]
        test rax, PTE_P
        jz .skip_level
        cmp ecx, 12
        je .leaf
        and rax, [pte_addr_mask]
        sub ecx, 9
        jmp .level
.leaf:
        mov qword [rdx], 0
        and rax, [pte_addr_mask]
        mov rdi, rax
        call pmm_free
        invlpg [r8]
        mov rax, [cur_task]
        dec qword [rax + TASK.pages]
        add r8, 4096
        jmp .page
.skip_level:
        ; nothing under this entry: on to the next it would cover
        mov rax, 1
        shl rax, cl
        add r8, rax
        neg rax
        and r8, rax
        jmp .page
.done:
        pop rsi
        pop rdi
        clc
        ret
.bad:
        pop rsi
        pop rdi
        stc
        ret

; arch_prctl(code, addr): the thread pointer
n_sys_arch_prctl:
        cmp rdi, 0x1002                 ; ARCH_SET_FS
        jne .invalid
        mov rax, rsi
        shr rax, 47
        jnz .invalid
        mov [n_fsbase], rsi
        mov ecx, MSR_FS_BASE
        mov eax, esi
        mov rdx, rsi
        shr rdx, 32
        wrmsr
        xor eax, eax
        ret
.invalid:
        mov rax, -E_INVAL
        ret

n_sys_exit:
        cmp qword [n_vf_active], 0
        jne .child
        movzx eax, dil
        jmp native_exit
.child:
        ; a vfork child that ends before execve (one that could not start
        ; the program): its status is for wait4, the parent goes on
        movzx edi, dil
        mov eax, LX_VFORK_EXIT
        call n_kernel
        jmp n_vfork_end

; clone(flags, stack, ...): CLONE_VM | CLONE_VFORK only, as posix_spawn
; uses it; anything else would need a copy of the memory (ENOSYS)
n_sys_clone:
        mov rax, rdi
        and eax, CLONE_VM | CLONE_VFORK
        cmp eax, CLONE_VM | CLONE_VFORK
        je n_vfork_begin
        mov rax, -E_NOSYS
        ret

; vfork(): the same, on the parent's stack
n_sys_vfork:
        xor esi, esi
        jmp n_vfork_begin

; rsi = the child's stack, or 0 for the parent's: the parent's place is
; kept, the child gets a copy of the descriptors (linux.su) and 0
n_vfork_begin:
        cmp qword [n_vf_active], 0
        jne .busy
        mov ecx, N_FRAME
.keep:
        mov rax, [rbp + rcx * 8 - 8]
        mov [n_vf_frame + rcx * 8 - 8], rax
        loop .keep
        mov rax, [n_fsbase]
        mov [n_vf_fsbase], rax
        push rsi
        mov eax, LX_VFORK_BEGIN
        call n_kernel
        pop rsi
        test rax, rax
        js .out
        mov [n_vf_pid], rax
        mov qword [n_vf_active], 1
        test rsi, rsi
        jz .stack
        mov [rbp + FRAME_RSP], rsi
.stack:
        xor eax, eax
.out:
        ret
.busy:
        mov rax, -E_AGAIN
        ret

; execve(path, argv, envp): linux.su runs the program nested to its end and
; gives its status, or -errno when it could not start it
n_sys_execve:
        mov eax, 59
        call n_kernel
        test rax, rax
        js .failed
        cmp qword [n_vf_active], 0
        jne n_vfork_end
        ; this program has become the other one, which has ended
        jmp native_exit
.failed:
        ret

; the vfork child is done: the parent goes on from its clone, with the pid
n_vfork_end:
        mov ecx, N_FRAME
.back:
        mov rax, [n_vf_frame + rcx * 8 - 8]
        mov [rbp + rcx * 8 - 8], rax
        loop .back
        mov rax, [n_vf_fsbase]
        mov [n_fsbase], rax
        mov rdx, rax
        shr rdx, 32
        mov ecx, MSR_FS_BASE
        wrmsr
        mov qword [n_vf_active], 0
        mov rax, [n_vf_pid]
        ret

; tkill(tid, sig), kill(pid, sig): only itself, and it ends (abort)
n_sys_tkill:
        movzx eax, sil
        add eax, 128
        jmp native_exit

; tgkill(pid, tid, sig)
n_sys_tgkill:
        movzx eax, dl
        add eax, 128
        jmp native_exit

; -> rax: seconds since 1970 at boot. Keeps the others.
n_epoch_base:
        mov rax, [n_epoch]
        test rax, rax
        jnz .out
        push rcx
        push rdx
        call rtc_epoch
        mov rcx, rax
        mov rax, [ticks]
        xor edx, edx
        push rbx
        mov ebx, 100
        div rbx
        pop rbx
        sub rcx, rax
        mov [n_epoch], rcx
        mov rax, rcx
        pop rdx
        pop rcx
.out:
        ret

; edi = 0 for the wall clock, else since boot -> rax seconds, rdx their
; nanoseconds. Keeps rsi.
n_now:
        push rdi
        mov rax, [ticks]
        xor edx, edx
        mov ecx, 100
        div rcx
        imul rdx, rdx, 10000000
        pop rdi
        test edi, edi
        jnz .out
        mov rcx, rax
        call n_epoch_base
        add rax, rcx
.out:
        ret

; clock_gettime(clock, ts)
n_sys_clock_gettime:
        mov rax, rsi
        mov ecx, 16
        call n_uok
        jc .fault
        ; CLOCK_REALTIME 0, and its coarse 5
        cmp edi, 5
        jne .clock
        xor edi, edi
.clock:
        call n_now
        mov [rsi], rax
        mov [rsi + 8], rdx
        xor eax, eax
        ret
.fault:
        mov rax, -E_FAULT
        ret

; gettimeofday(tv, tz)
n_sys_gettimeofday:
        test rdi, rdi
        jz .done
        mov rax, rdi
        mov ecx, 16
        call n_uok
        jc .fault
        mov rsi, rdi
        xor edi, edi
        call n_now
        mov [rsi], rax
        mov rax, rdx
        xor edx, edx
        mov ecx, 1000
        div rcx
        mov [rsi + 8], rax
.done:
        xor eax, eax
        ret
.fault:
        mov rax, -E_FAULT
        ret

; nanosleep(req, rem)
n_sys_nanosleep:
        mov rax, rdi
        mov ecx, 16
        call n_uok
        jc .fault
        mov rax, [rdi]
        imul rax, rax, 100
        mov rcx, rax
        mov rax, [rdi + 8]
        add rax, 9999999
        xor edx, edx
        mov ebx, 10000000
        div rbx
        add rax, rcx
        add rax, [ticks]
        call task_sleep_until
        xor eax, eax
        ret
.fault:
        mov rax, -E_FAULT
        ret

n_sys_yield:
        call schedule
        xor eax, eax
        ret

; getpid, gettid: the task's id + 1 (a pid is not 0)
n_sys_getpid:
        mov rax, [cur_task]
        mov rax, [rax + TASK.id]
        inc rax
        ret

n_sys_zero:
        xor eax, eax
        ret

n_sys_notty:
        mov rax, -E_NOTTY
        ret

n_sys_nomem:
        mov rax, -E_NOMEM
        ret

n_sys_umask:
        mov eax, 18                     ; 022
        ret

; getrandom(buf, n, flags): SHA-256 over the last state, the time stamp
; counter and the ticks, for each 32 bytes. The counter is all the entropy
; there is (no RDRAND on every CPU, no device noise collected yet).
n_sys_getrandom:
        mov rax, rdi
        mov rcx, rsi
        call n_uok
        jc .fault
        mov r12, rdi
        mov r13, rsi
.block:
        test r13, r13
        jz .done
        rdtsc
        shl rdx, 32
        or rax, rdx
        mov [n_rng + 32], rax
        mov rax, [ticks]
        mov [n_rng + 40], rax
        mov esi, n_rng
        mov ecx, 48
        call sha256_bytes
        mov esi, sha_state
        mov edi, n_rng
        mov ecx, 32
        rep movsb
        ; what goes out is not the state: one more round over it
        mov esi, n_rng
        mov ecx, 33
        call sha256_bytes
        mov rcx, r13
        cmp rcx, 32
        jbe .take
        mov ecx, 32
.take:
        sub r13, rcx
        mov esi, sha_state
        mov rdi, r12
        add r12, rcx
        rep movsb
        jmp .block
.done:
        mov rax, r12
        sub rax, [rbp]                  ; the buf argument, in the gate's frame
        ret
.fault:
        mov rax, -E_FAULT
        ret

; uname(buf): six fields of 65 bytes
n_sys_uname:
        mov rax, rdi
        mov ecx, 390
        call n_uok
        jc .fault
        mov rdx, rdi
        push rdi
        xor eax, eax
        mov ecx, 390
        rep stosb
        pop rdi
        xor ecx, ecx
.field:
        mov rsi, [n_un_fields + rcx * 8]
        lea rdi, [rdx + 65]
        push rcx
        imul rax, rcx, 65
        lea rdi, [rdx + rax]
        call strlen_z
        rep movsb
        pop rcx
        inc rcx
        cmp rcx, 5
        jb .field
        xor eax, eax
        ret
.fault:
        mov rax, -E_FAULT
        ret

; --- the kernel's way into the program's memory

; k_ustr(s): addr n -> those bytes as a string, null for a bad address
bi_k_ustr:
        mov rax, [rdi + 8]
        mov rcx, [rdi + 24]
        test rcx, rcx
        js n_bi_null
        jz .read
        call n_uok
        jc n_bi_null
.read:
        mov rsi, rax
        push rdx
        call new_string
        pop rdx
        jmp bi_str_out

n_bi_null:
        mov qword [rdx], T_NULL
        xor eax, eax
        ret

; k_ustrz(s): addr -> the NUL-terminated text there (4096 bytes at most),
; null for a bad address
bi_k_ustrz:
        mov rax, [rdi + 8]
        mov ecx, 1
        call n_uok
        jc n_bi_null
        mov rsi, rax
        xor ecx, ecx
.next:
        cmp ecx, 4096
        jae n_bi_null
        push rcx
        lea rax, [rsi + rcx]
        mov ecx, 1
        call n_uok
        pop rcx
        jc n_bi_null
        cmp byte [rsi + rcx], 0
        je .end
        inc ecx
        jmp .next
.end:
        push rdx
        call new_string
        pop rdx
        jmp bi_str_out

; k_uput(n): addr s from n -> n bytes of the string, from byte from, into
; the program's memory: how many, -14 for a bad address
bi_k_uput:
        cmp qword [rdi + 16], T_STR
        jne n_bi_null
        mov rax, [rdi + 8]              ; addr
        mov r8, [rdi + 24]              ; the string
        mov r9, [rdi + 40]              ; from
        mov rcx, [rdi + 56]             ; n
        mov r10, [r8 + STR_LEN]
        sub r10, r9
        jge .some
        xor r10d, r10d
.some:
        cmp rcx, r10
        cmovg rcx, r10
        test rcx, rcx
        jle .none
        call n_uok
        jc .fault
        lea rsi, [r8 + r9 + STR_BYTES]
        mov rdi, rax
        mov rax, rcx
        rep movsb
        jmp bi_int_out
.none:
        xor eax, eax
        jmp bi_int_out
.fault:
        mov rax, -E_FAULT
        jmp bi_int_out

; k_upoke(r): addr value size -> 0: an integer of 1, 2, 4 or 8 bytes into
; the program's memory, -14 for a bad address
bi_k_upoke:
        mov rax, [rdi + 8]
        mov r8, [rdi + 24]
        mov rcx, [rdi + 40]
        call n_uok
        jc .fault
        mov rsi, rax
        cmp ecx, 1
        je .b1
        cmp ecx, 2
        je .b2
        cmp ecx, 4
        je .b4
        mov [rsi], r8
        jmp .done
.b1:
        mov [rsi], r8b
        jmp .done
.b2:
        mov [rsi], r8w
        jmp .done
.b4:
        mov [rsi], r8d
.done:
        xor eax, eax
        jmp bi_int_out
.fault:
        mov rax, -E_FAULT
        jmp bi_int_out

; k_upeek(v): addr -> the 8 bytes there, null for a bad address
bi_k_upeek:
        mov rax, [rdi + 8]
        mov ecx, 8
        call n_uok
        jc n_bi_null
        mov rax, [rax]
        jmp bi_int_out

; k_ucon(n): addr n -> those bytes on the console: n, -14 for a bad address
bi_k_ucon:
        mov rax, [rdi + 8]
        mov rcx, [rdi + 24]
        test rcx, rcx
        jle .none
        call n_uok
        jc .fault
        mov rsi, rax
        call con_write
        mov rax, rcx
        jmp bi_int_out
.none:
        xor eax, eax
        jmp bi_int_out
.fault:
        mov rax, -E_FAULT
        jmp bi_int_out

; k_bytes_str(s): v -> a byte vector as a string, zero bytes too (bytes_text
; gives up at one)
bi_k_bytes_str:
        cmp esi, 1
        jne n_bi_null
        cmp qword [rdi], T_VEC
        jne n_bi_null
        mov r8, [rdi + 8]
        mov rcx, [r8 + VEC_COUNT]
        push rdx
        lea rdi, [rcx + 9]
        call alloc
        mov [rax + STR_LEN], rcx
        mov rsi, [r8 + VEC_ITEMS]
        xor edx, edx
.byte:
        cmp rdx, rcx
        jae .done
        mov r9, [rsi + 8]
        mov [rax + STR_BYTES + rdx], r9b
        add rsi, 16
        inc rdx
        jmp .byte
.done:
        pop rdx
        jmp bi_str_out

; k_line_eof(b): -> true when the last k_getline found the end of the input
bi_k_line_eof:
        mov rax, [read_line_eof]
        mov qword [rdx], T_BOOL
        mov [rdx + 8], rax
        xor eax, eax
        ret

; number, handler
; what native_run keeps for the program that started another
n_saved_list:   dq c3_ksp, native_on, n_fsbase, n_brk_start, n_brk, n_mmap, n_window
                dq n_vf_active, n_vf_pid, n_vf_fsbase
%assign i 0
%rep N_FRAME
                dq n_vf_frame + i * 8
%assign i i + 1
%endrep
N_SAVED         equ ($ - n_saved_list) / 8

n_table:
        dq 12, n_sys_brk
        dq 9, n_sys_mmap
        dq 11, n_sys_munmap
        dq 10, n_sys_zero               ; mprotect: every page is rwx
        dq 28, n_sys_zero               ; madvise
        dq 25, n_sys_nomem              ; mremap: musl copies instead
        dq 158, n_sys_arch_prctl
        dq 218, n_sys_getpid            ; set_tid_address
        dq 228, n_sys_clock_gettime
        dq 96, n_sys_gettimeofday
        dq 35, n_sys_nanosleep
        dq 24, n_sys_yield
        dq 13, n_sys_zero               ; rt_sigaction: no signals come
        dq 14, n_sys_zero               ; rt_sigprocmask
        dq 131, n_sys_zero              ; sigaltstack
        dq 202, n_sys_zero              ; futex: one thread
        dq 273, n_sys_zero              ; set_robust_list
        dq 334, n_sys_zero              ; rseq
        dq 39, n_sys_getpid
        dq 186, n_sys_getpid            ; gettid
        dq 110, n_sys_zero              ; getppid
        dq 102, n_sys_zero              ; getuid
        dq 104, n_sys_zero              ; getgid
        dq 107, n_sys_zero              ; geteuid
        dq 108, n_sys_zero              ; getegid
        dq 16, n_sys_notty              ; ioctl: the console is lines
        dq 73, n_sys_zero               ; flock: one program at a file
        dq 280, n_sys_zero              ; utimensat
        dq 95, n_sys_umask
        dq 318, n_sys_getrandom
        dq 63, n_sys_uname
        dq 60, n_sys_exit
        dq 231, n_sys_exit
        dq 56, n_sys_clone
        dq 58, n_sys_vfork
        dq 59, n_sys_execve
        dq 200, n_sys_tkill
        dq 62, n_sys_tkill
        dq 234, n_sys_tgkill
        dq -1, 0

N_AUX           equ 8                   ; pairs native_run lays out, AT_NULL's too

n_env:
        dq s_env_home, s_env_user, s_env_path, s_env_term, s_env_host, s_env_os, s_env_certs, 0
s_env_home:     db "HOME=/home", 0
s_env_user:     db "USER=comrade", 0
s_env_path:     db "PATH=/bin", 0
s_env_term:     db "TERM=dumb", 0
s_env_host:     db "HOSTNAME=moscow", 0
s_env_os:       db "OS=Moscow", 0
s_env_certs:    db "SSL_CERT_FILE=/etc/ssl/certs.pem", 0
n_un_fields:
        dq s_un_sys, s_un_node, s_un_rel, s_un_ver, s_un_mach
s_un_sys:       db "Moscow", 0
s_un_node:      db "moscow", 0
s_un_rel:       db "USSR", 0
s_un_ver:       db "USSR Moscow", 0
s_un_mach:      db "x86_64", 0
s_linux:        db "__linux"
s_linux_len     equ $ - s_linux
msg_native_refused: db "USSR runtime: only static PIE x86-64 ELF programs run here", 10, 0
msg_native_broke: db "USSR runtime: the program stopped: exception ", 0
