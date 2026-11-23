/* A position-independent blob embedded at a page boundary in ussr_rt. */
ENTRY(xhci)
SECTIONS
{
    . = 0;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) }
    /* Materialize the zero-initialized DMA buffers in the flat image. */
    .end : { BYTE(0) }
    /DISCARD/ : { *(.comment .note.* .eh_frame*) }
}
ASSERT(xhci == 0, "xhci entry must be first")
