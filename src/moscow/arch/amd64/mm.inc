; mm.inc - Moscow's memory (ussr_rt.nasm includes it): physical pages,
; page tables, page faults and TLSF heaps.
;
; Physical memory is 4 KiB frames: what lies past the kernel's own heap
; in the largest RAM region of the BIOS map. A frame given back goes on a
; free list threaded through the frames themselves (RAM is identity
; mapped, so the kernel reaches every frame where it is).
;
; Virtual memory: every address space shares PML4 slot 0, the identity
; map of the first 512 GiB (the runtime, the kernel's heap, the devices)
; and has a private slot 1, 512 GiB at USER_BASE, that only it sees. The
; private half is mapped a 4 KiB page at a time, on first touch: the page
; fault handler gives the faulting address a zeroed frame when it lies in
; a heap of the running task, and stops the machine otherwise.
;
; Heaps are TLSF, Two-Level Segregated Fit (Masmano, Ripoll, Crespo and
; Real, 2004): free blocks are kept in lists by size class, a first level
; per power of two and SL_COUNT second-level lists inside it, with a
; bitmap for each level, so finding a block, splitting it and merging it
; with its free neighbours takes the same few steps whatever the heap
; holds. The kernel's heap is a TLSF pool over identity-mapped RAM; every
; program run gets a pool of its own, a POOL_WINDOW of virtual memory in
; its task's private half, demand paged and given back whole, every page
; of it, when the program ends.
;
;   pmm_alloc -> rax: a zeroed frame        pmm_free: rdi = frame
;   space_new -> rax: a PML4                space_free: rdi = PML4
;   pool_init: rdi = base, rsi = size -> rax: the pool
;   pool_alloc: rdi = pool, rsi = bytes -> rax: memory, or 0
;   pool_free: rdi = pool, rsi = memory
;   window_free: rdi = PML4, rsi = base, rdx = size: unmaps, frees frames

PAGE            equ 4096
USER_BASE       equ 0x8000000000        ; PML4 slot 1
USER_SIZE       equ 0x8000000000        ; 512 GiB
POOL_WINDOW_LOG equ 34
POOL_WINDOW     equ 1 << POOL_WINDOW_LOG ; 16 GiB of address space a pool
POOL_WINDOWS    equ USER_SIZE / POOL_WINDOW
KPOOL_MAX       equ 384 << 20           ; the kernel's own heap
PTE_P           equ 1
PTE_W           equ 2
PTE_PS          equ 0x80
PTE_ADDR        equ 0x000ffffffffff000

; TLSF: sizes are multiples of 16, a block has a 16-byte header
FL_COUNT        equ 40
SL_LOG          equ 4
SL_COUNT        equ 1 << SL_LOG
TLSF_SMALL      equ 256                 ; below: first level 0, 16 bytes a list
BLK_PREV        equ 0                   ; the block before, when it is free
BLK_SIZE        equ 8                   ; the whole block; bit 0 free, bit 1 prev free
BLK_NEXTF       equ 16                  ; free blocks: the list links
BLK_PREVF       equ 24
BLK_HDR         equ 16
BLK_MIN         equ 32
BF_FREE         equ 1
BF_PREVFREE     equ 2
; a pool's header, at its start
POOL_FLMAP      equ 0
POOL_USED       equ 8                   ; bytes handed out
POOL_SIZE_F     equ 16                  ; bytes managed
POOL_SLMAP      equ 24                  ; FL_COUNT qwords
POOL_HEADS      equ POOL_SLMAP + FL_COUNT * 8
POOL_HDR        equ (POOL_HEADS + FL_COUNT * SL_COUNT * 8 + 15) / 16 * 16

; --- physical frames

; [rsi, rdx) -> the frames between, all free (first used ones the bump
; way, given back ones on the list)
pmm_init:
        add rsi, PAGE - 1
        and rsi, -PAGE
        and rdx, -PAGE
        mov [pmm_next], rsi
        mov [pmm_end], rdx
        mov qword [pmm_list], 0
        mov rax, rdx
        sub rax, rsi
        shr rax, 12
        mov [pmm_total], rax
        mov [pmm_free_n], rax
        ret

; -> rax: a zeroed frame. Out of frames stops the machine. Keeps the
; others.
pmm_alloc:
        push rcx
        push rdi
        mov rax, [pmm_list]
        test rax, rax
        jz .fresh
        mov rcx, [rax]
        mov [pmm_list], rcx
        jmp .got
.fresh:
        mov rax, [pmm_next]
        cmp rax, [pmm_end]
        jae .none
        lea rcx, [rax + PAGE]
        mov [pmm_next], rcx
.got:
        dec qword [pmm_free_n]
        mov rdi, rax
        mov ecx, PAGE / 8
        push rax
        xor eax, eax
        rep stosq
        pop rax
        pop rdi
        pop rcx
        ret
.none:
        PUTS msg_no_frames
        jmp sys_halt

; rdi = frame: given back. Keeps every register.
pmm_free:
        push rax
        mov rax, [pmm_list]
        mov [rdi], rax
        mov [pmm_list], rdi
        inc qword [pmm_free_n]
        pop rax
        ret

; --- page tables

; rdi = PML4, rsi = address in the private half -> rax: its PTE, the
; tables on the way made as needed. Keeps the others.
pt_walk:
        push rcx
        push rdx
        mov rdx, rdi
        mov ecx, 39
.level:
        mov rax, rsi
        shr rax, cl
        and eax, 511
        lea rdx, [rdx + rax * 8]
        cmp ecx, 12
        je .leaf
        mov rax, [rdx]
        test rax, PTE_P
        jnz .down
        call pmm_alloc
        or rax, PTE_P | PTE_W
        mov [rdx], rax
.down:
        and rax, [pte_addr_mask]
        mov rdx, rax
        sub ecx, 9
        jmp .level
.leaf:
        mov rax, rdx
        pop rdx
        pop rcx
        ret

; rdi = PML4, rsi = address: a zeroed frame there, if there is none.
; Keeps every register.
map_page:
        push rax
        push rsi
        and rsi, -PAGE
        call pt_walk
        test qword [rax], PTE_P
        jnz .there
        push rax
        call pmm_alloc
        or rax, PTE_P | PTE_W
        mov rsi, rax
        pop rax
        mov [rax], rsi
.there:
        pop rsi
        pop rax
        ret

; -> rax: a new address space: the shared identity map, nothing private
space_new:
        push rcx
        call pmm_alloc
        mov rcx, [PAGE_PML4]
        mov [rax], rcx
        pop rcx
        ret

; rdi = table, ecx = its level (3 PDPT, 2 PD, 1 PT), rsi = first entry,
; rdx = entries: everything below freed, the entries cleared. Keeps the
; others.
pt_free_entries:
        push rax
        push rbx
        push rsi
        push rdx
        push rdi
        push r8
        mov r8, rdi
.entry:
        test rdx, rdx
        jz .done
        mov rax, [r8 + rsi * 8]
        test rax, PTE_P
        jz .next
        and rax, [pte_addr_mask]
        cmp ecx, 1
        je .frame
        push rsi
        push rdx
        push rcx
        mov rdi, rax
        dec ecx
        xor esi, esi
        mov edx, 512
        call pt_free_entries
        pop rcx
        pop rdx
        pop rsi
.frame:
        mov rdi, rax
        call pmm_free
        mov qword [r8 + rsi * 8], 0
.next:
        inc rsi
        dec rdx
        jmp .entry
.done:
        pop r8
        pop rdi
        pop rdx
        pop rsi
        pop rbx
        pop rax
        ret

; rdi = PML4, rsi = base, rdx = size, both whole GiBs inside the private
; half: every page there unmapped and its frame given back
window_free:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        mov rax, [rdi + 8]              ; slot 1, the private half
        test rax, PTE_P
        jz .flush
        and rax, [pte_addr_mask]
        mov rdi, rax
        mov rax, USER_BASE
        sub rsi, rax
        shr rsi, 30
        shr rdx, 30
        mov ecx, 3
        call pt_free_entries
.flush:
        mov rax, cr3
        mov cr3, rax
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rdi = PML4: its private half and the PML4 itself given back (not the
; address space in use)
space_free:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        mov rax, [rdi + 8]
        test rax, PTE_P
        jz .pml4
        push rdi
        and rax, [pte_addr_mask]
        mov rdi, rax
        xor esi, esi
        mov edx, 512
        mov ecx, 3
        call pt_free_entries
        call pmm_free                   ; the PDPT
        pop rdi
.pml4:
        call pmm_free
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; --- page faults

; #PF: a page of a heap of the running task gets a frame; anything else
; stops the machine as the other exceptions do
pf_isr:
        push rax
        push rcx
        push rsi
        push rdi
        mov rsi, cr2
        mov rax, USER_BASE
        cmp rsi, rax
        jb .fatal
        mov rcx, [cur_task]
        test rcx, rcx
        jz .fatal
        mov rax, [rcx + TASK.depth]
        shl rax, POOL_WINDOW_LOG
        mov rcx, USER_BASE
        add rax, rcx
        cmp rsi, rax
        jae .fatal
        mov rdi, cr3
        and rdi, [pte_addr_mask]
        call map_page
        inc qword [pf_count]
        pop rdi
        pop rsi
        pop rcx
        pop rax
        add rsp, 8                      ; the error code
        iretq
.fatal:
        PUTS msg_pf
        mov rax, cr2
        call print_hex
        PUTC 10
        pop rdi
        pop rsi
        pop rcx
        pop rax
        push qword 14
        jmp exc_common

; --- TLSF

; rsi = size (>= TLSF_SMALL or not) -> ecx = first level, edx = second
; level. Keeps the others.
tlsf_map:
        cmp rsi, TLSF_SMALL
        jae .large
        xor ecx, ecx
        mov rdx, rsi
        shr rdx, 4
        ret
.large:
        push rax
        bsr rax, rsi                    ; the top bit
        mov ecx, eax
        sub ecx, SL_LOG
        mov rdx, rsi
        shr rdx, cl
        and edx, SL_COUNT - 1
        mov ecx, eax
        sub ecx, 7                      ; 256 .. 511 is level 1
        pop rax
        ret

; rdi = pool, rsi = free block: on its list. Keeps every register.
tlsf_insert:
        push rax
        push rcx
        push rdx
        push rsi
        mov rax, [rsi + BLK_SIZE]
        and rax, -16
        push rsi
        mov rsi, rax
        call tlsf_map
        pop rsi
        mov eax, ecx
        shl eax, SL_LOG
        add eax, edx
        lea rax, [rdi + POOL_HEADS + rax * 8]
        push rax
        mov rax, [rax]
        mov [rsi + BLK_NEXTF], rax
        mov qword [rsi + BLK_PREVF], 0
        test rax, rax
        jz .first
        mov [rax + BLK_PREVF], rsi
.first:
        pop rax
        mov [rax], rsi
        bts qword [rdi + POOL_FLMAP], rcx
        bts qword [rdi + POOL_SLMAP + rcx * 8], rdx
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; rdi = pool, rsi = free block: off its list. Keeps every register.
tlsf_remove:
        push rax
        push rcx
        push rdx
        push r8
        mov rax, [rsi + BLK_SIZE]
        and rax, -16
        push rsi
        mov rsi, rax
        call tlsf_map
        pop rsi
        mov r8, [rsi + BLK_PREVF]
        mov rax, [rsi + BLK_NEXTF]
        test rax, rax
        jz .no_next
        mov [rax + BLK_PREVF], r8
.no_next:
        test r8, r8
        jz .head
        mov [r8 + BLK_NEXTF], rax
        jmp .done
.head:
        mov r8d, ecx
        shl r8d, SL_LOG
        add r8d, edx
        mov [rdi + POOL_HEADS + r8 * 8], rax
        test rax, rax
        jnz .done
        btr qword [rdi + POOL_SLMAP + rcx * 8], rdx
        cmp qword [rdi + POOL_SLMAP + rcx * 8], 0
        jne .done
        btr qword [rdi + POOL_FLMAP], rcx
.done:
        pop r8
        pop rdx
        pop rcx
        pop rax
        ret

; rdi = base, rsi = size (both 16-byte aligned) -> rax: a pool over it,
; one free block and a sentinel at the end. Keeps the others.
pool_init:
        push rcx
        push rdx
        push rsi
        push rdi
        mov rdx, rdi
        mov rcx, POOL_HDR / 8
        xor eax, eax
        rep stosq
        mov rdi, rdx
        mov [rdi + POOL_SIZE_F], rsi
        ; the block: from the header to 16 bytes before the end
        lea rax, [rdi + POOL_HDR]
        lea rcx, [rsi - POOL_HDR - BLK_HDR]
        and rcx, -16
        lea rdx, [rcx + BF_FREE]
        mov [rax + BLK_SIZE], rdx
        mov qword [rax + BLK_PREV], 0
        ; the sentinel: used, size 0, its previous free
        lea rdx, [rax + rcx]
        mov [rdx + BLK_PREV], rax
        mov qword [rdx + BLK_SIZE], BF_PREVFREE
        mov rsi, rax
        call tlsf_insert
        mov rax, rdi
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        ret

; rdi = pool, rsi = bytes -> rax: 16-byte aligned memory (not zeroed),
; or 0. Keeps the others.
pool_alloc:
        push rbx
        push rcx
        push rdx
        push rsi
        push r8
        push r9
        ; the block: header and payload, 16-byte multiples, BLK_MIN least
        add rsi, BLK_HDR + 15
        and rsi, -16
        cmp rsi, BLK_MIN
        jae .sized
        mov esi, BLK_MIN
.sized:
        mov rbx, rsi                    ; what is needed
        ; round up to the start of the next class, so any block of the
        ; class found is large enough
        cmp rsi, TLSF_SMALL
        jb .search
        bsr rcx, rsi
        sub ecx, SL_LOG
        mov rax, 1
        shl rax, cl
        dec rax
        add rsi, rax
.search:
        call tlsf_map
        mov rax, [rdi + POOL_SLMAP + rcx * 8]
        mov r8, -1
        push rcx
        mov ecx, edx
        shl r8, cl
        pop rcx
        and rax, r8
        jnz .have_sl
        ; a larger first level
        lea r8d, [rcx + 1]
        cmp r8d, 64
        jae .none
        mov rax, -1
        push rcx
        mov ecx, r8d
        shl rax, cl
        pop rcx
        and rax, [rdi + POOL_FLMAP]
        jz .none
        bsf rcx, rax
        mov rax, [rdi + POOL_SLMAP + rcx * 8]
.have_sl:
        bsf rdx, rax
        mov eax, ecx
        shl eax, SL_LOG
        add eax, edx
        mov rsi, [rdi + POOL_HEADS + rax * 8]
        test rsi, rsi
        jz .none
        call tlsf_remove
        ; split off what is left, when it makes a block
        mov rax, [rsi + BLK_SIZE]
        mov r9, rax
        and r9, BF_PREVFREE
        and rax, -16
        lea r8, [rsi + rax]             ; the next block
        mov rcx, rax
        sub rcx, rbx
        cmp rcx, BLK_MIN
        jb .whole
        lea rdx, [rsi + rbx]            ; the rest
        mov [rdx + BLK_PREV], rsi
        lea rax, [rcx + BF_FREE]
        mov [rdx + BLK_SIZE], rax
        mov [r8 + BLK_PREV], rdx        ; the next one's previous: the rest, free
        or qword [r8 + BLK_SIZE], BF_PREVFREE
        push rsi
        mov rsi, rdx
        call tlsf_insert
        pop rsi
        mov rax, rbx
        jmp .mark
.whole:
        and qword [r8 + BLK_SIZE], ~BF_PREVFREE
.mark:
        or rax, r9                      ; used, its previous as it was
        mov [rsi + BLK_SIZE], rax
        and rax, -16
        add [rdi + POOL_USED], rax
        lea rax, [rsi + BLK_HDR]
        jmp .out
.none:
        xor eax, eax
.out:
        pop r9
        pop r8
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

; rdi = pool, rsi = memory from pool_alloc: given back, merged with the
; free blocks next to it. Keeps every register.
pool_free:
        test rsi, rsi
        jz .nothing
        push rax
        push rcx
        push rdx
        push rsi
        sub rsi, BLK_HDR
        mov rax, [rsi + BLK_SIZE]
        mov rcx, rax
        and rcx, -16
        sub [rdi + POOL_USED], rcx
        ; the one before, when free
        test rax, BF_PREVFREE
        jz .after
        mov rdx, [rsi + BLK_PREV]
        push rsi
        mov rsi, rdx
        call tlsf_remove
        pop rsi
        mov rax, [rdx + BLK_SIZE]
        and rax, -16
        add rcx, rax
        mov rax, [rdx + BLK_SIZE]
        and rax, BF_PREVFREE
        mov rsi, rdx
        jmp .next
.after:
        and rax, BF_PREVFREE
.next:
        ; rsi = the block, rcx = its size, rax = its prev-free bit
        lea rdx, [rsi + rcx]
        test qword [rdx + BLK_SIZE], BF_FREE
        jz .alone
        push rsi
        mov rsi, rdx
        call tlsf_remove
        pop rsi
        mov rdx, [rdx + BLK_SIZE]
        and rdx, -16
        add rcx, rdx
        lea rdx, [rsi + rcx]
.alone:
        lea rax, [rax + rcx + BF_FREE]
        mov [rsi + BLK_SIZE], rax
        mov [rdx + BLK_PREV], rsi
        or qword [rdx + BLK_SIZE], BF_PREVFREE
        call tlsf_insert
        pop rsi
        pop rdx
        pop rcx
        pop rax
.nothing:
        ret
