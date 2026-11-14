; fbcon.inc - the console on a framebuffer, for the Moscow runtime
; (ussr_rt.nasm includes it): booted from UEFI there is no VGA text mode,
; so the console's 80x25 cells (character, attribute) go to a page of RAM
; shaped as VGA's text memory (vga_mem), and on every tick the cells that
; changed since are drawn into the GOP framebuffer the loader handed over
; (BI_FB_*), 8x16 pixels each from fbfont.inc - each pixel a square of
; fb_scale, the largest whole number the screen holds 80x25 of (2 on
; 1280x800) - in VGA's sixteen colours, the grid in the middle of the
; screen; the cursor is a bar under its cell. While the GUI has the screen nothing is drawn, and everything is
; again when it gives it back (fbcon_all).

FB_COLS         equ 80
FB_ROWS         equ 25
FB_CELLS        equ FB_COLS * FB_ROWS

; Under UEFI (a framebuffer in the boot information): the console on it.
; vga_mem is VGA's text memory otherwise. Keeps every register.
fbcon_init:
        push rax
        push rcx
        push rdi
        push rsi
        mov qword [vga_mem], 0xb8000
        mov qword [fb_on], 0
        mov rsi, [boot_info]
        mov rax, [rsi + BI_FB_BASE]
        test rax, rax
        jz .out
        mov [fb_base], rax
        mov rax, [rsi + BI_FB_WIDTH]
        mov [fb_width], rax
        mov rcx, [rsi + BI_FB_HEIGHT]
        mov [fb_height], rcx
        cmp rax, FB_COLS * 8
        jb .out
        cmp rcx, FB_ROWS * 16
        jb .out
        mov rax, [rsi + BI_FB_STRIDE]
        mov [fb_stride], rax
        ; the scale: as many times 640x400 as the screen holds, up to 4
        push rdx
        mov rax, [fb_width]
        xor edx, edx
        mov ecx, FB_COLS * 8
        div rcx
        mov rcx, rax
        mov rax, [fb_height]
        xor edx, edx
        push rcx
        mov ecx, FB_ROWS * 16
        div rcx
        pop rcx
        pop rdx
        cmp rax, rcx
        jbe .smaller
        mov rax, rcx
.smaller:
        cmp rax, 4
        jbe .scale
        mov eax, 4
.scale:
        mov [fb_scale], rax
        ; the grid in the middle
        mov rcx, rax
        imul rax, rcx, FB_COLS * 8
        neg rax
        add rax, [fb_width]
        shr rax, 1
        mov [fb_ox], rax
        imul rax, rcx, FB_ROWS * 16
        neg rax
        add rax, [fb_height]
        shr rax, 1
        mov [fb_oy], rax
        ; VGA's colours as the framebuffer takes them
        xor ecx, ecx
.colour:
        mov eax, [fb_vga_rgb + rcx * 4]
        cmp qword [rsi + BI_FB_FORMAT], 1
        je .bgrx
        bswap eax                       ; 00RRGGBB -> BBGGRR00
        shr eax, 8                      ; -> 00BBGGRR
.bgrx:
        mov [fb_palette + rcx * 4], eax
        inc ecx
        cmp ecx, 16
        jb .colour
        ; the whole screen black, the cells blank
        mov rdi, [fb_base]
        mov rcx, [fb_stride]
        imul rcx, [fb_height]
        xor eax, eax
        rep stosd
        mov rdi, fb_text
        mov ecx, FB_CELLS
        mov ax, 0x0720
        rep stosw
        mov qword [vga_mem], fb_text
        mov qword [fb_all], 1
        mov qword [fb_cursor], -1
        mov qword [fb_on], 1
.out:
        pop rsi
        pop rdi
        pop rcx
        pop rax
        ret

; The cells that changed onto the framebuffer, and the cursor (the timer's
; tick). Keeps every register and the flags.
fbcon_sync:
        pushfq
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        cld
        cmp qword [fb_wipe], 0
        je .cells
        ; the GUI gave the screen back: all of it black first
        mov qword [fb_wipe], 0
        mov rdi, [fb_base]
        mov rcx, [fb_stride]
        imul rcx, [fb_height]
        xor eax, eax
        rep stosd
        mov qword [fb_all], 1
        mov qword [fb_cursor], -1
.cells:
        xor ebx, ebx                    ; the cell
.cell:
        mov ax, [fb_text + rbx * 2]
        cmp qword [fb_all], 0
        jne .draw
        cmp ax, [fb_drawn + rbx * 2]
        je .next
.draw:
        mov [fb_drawn + rbx * 2], ax
        call fbcon_cell
        cmp rbx, [fb_cursor]
        jne .next
        mov qword [fb_cursor], -1       ; drawn over: the bar again below
.next:
        inc ebx
        cmp ebx, FB_CELLS
        jb .cell
        mov qword [fb_all], 0
        ; the cursor: its old cell plain, a bar under the new one
        call vga_here
        cmp rax, [fb_cursor]
        je .out
        mov rbx, [fb_cursor]
        cmp rbx, -1
        je .bar
        mov ax, [fb_text + rbx * 2]
        call fbcon_cell
.bar:
        call vga_here
        mov rbx, rax
        mov [fb_cursor], rax
        call fbcon_place                ; rdi: the cell's top left pixel
        movzx eax, byte [fb_text + rbx * 2 + 1]
        and eax, 15
        mov eax, [fb_palette + rax * 4]
        mov rdx, [fb_stride]
        imul rcx, rdx, 4 * 14           ; 14 glyph rows down
        imul rcx, [fb_scale]
        add rdi, rcx
        mov ecx, [fb_scale]
        shl ecx, 1                      ; 2 glyph rows
.bar_row:
        push rcx
        push rdi
        mov ecx, [fb_scale]
        shl ecx, 3
        rep stosd
        pop rdi
        pop rcx
        lea rdi, [rdi + rdx * 4]
        dec ecx
        jnz .bar_row
.out:
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
        popfq
        ret

; rbx = a cell -> rdi: its top left pixel in the framebuffer
fbcon_place:
        push rax
        push rcx
        push rdx
        mov rax, rbx
        xor edx, edx
        mov ecx, FB_COLS
        div rcx                         ; rax row, rdx column
        shl rax, 4
        imul rax, [fb_scale]
        add rax, [fb_oy]
        imul rax, [fb_stride]
        shl rdx, 3
        imul rdx, [fb_scale]
        add rax, rdx
        add rax, [fb_ox]
        mov rdi, [fb_base]
        lea rdi, [rdi + rax * 4]
        pop rdx
        pop rcx
        pop rax
        ret

; rbx = a cell, ax = its character and attribute -> drawn
fbcon_cell:
        push rax
        push rcx
        push rdx
        push rsi
        push rdi
        push r8
        push r9
        call fbcon_place
        movzx esi, al
        shl esi, 4
        add rsi, fbfont                 ; the glyph's 16 rows
        movzx edx, ah
        mov r8d, edx
        and edx, 15
        mov edx, [fb_palette + rdx * 4] ; foreground
        shr r8d, 4
        and r8d, 15
        mov r8d, [fb_palette + r8 * 4]  ; background
        mov r9, [fb_stride]
        shl r9, 2
        mov ecx, 16
.row:
        push rcx
        mov rcx, [fb_scale]             ; the row, scale lines of it
.line:
        movzx eax, byte [rsi]
        push rcx
        push rdi
        mov ecx, 8
.pixel:
        push rcx
        mov rcx, [fb_scale]
.wide:
        test al, 0x80
        jz .back
        mov [rdi], edx
        jmp .moved
.back:
        mov [rdi], r8d
.moved:
        add rdi, 4
        dec ecx
        jnz .wide
        pop rcx
        shl al, 1
        dec ecx
        jnz .pixel
        pop rdi
        pop rcx
        add rdi, r9
        dec ecx
        jnz .line
        inc rsi
        pop rcx
        dec ecx
        jnz .row
        pop r9
        pop r8
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rax
        ret

; VGA's colours, 0x00RRGGBB, in its attribute order
fb_vga_rgb:
        dd 0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa
        dd 0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff

%include "fbfont.inc"
