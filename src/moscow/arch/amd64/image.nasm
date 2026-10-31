; image.nasm - the Moscow boot disk: boot loader, file table, files.
;
;     nasm -f bin -i BUILD/ -i src/moscow/arch/amd64/ image.nasm -o moscow.img
;
; BUILD holds boot.bin, ussr_rt.bin, moscow.ru and russia.ru (build.sh
; makes them). The file table lists the runtime first: stage 2 jumps to
; the first file. Every file starts on a sector; the image is padded to
; a whole number of 64 KiB so emulators see a plausible disk.
%include "layout.inc"

%macro FILE_ENTRY 2                     ; name, label
        db %1
        times 16 - %strlen(%1) db 0
        dd (%2 - image) / 512           ; first sector
        dd %2 %+ _end - %2              ; bytes
        dd (%2 %+ _end - %2 + 511) / 512 ; sectors
        dd 0
%endmacro

%macro FILE_DATA 2                      ; label, file
%1:
        incbin %2
%1 %+ _end:
        align 512, db 0
%endmacro

image:
        incbin "boot.bin"
%if $ - image != TOC_LBA * 512
%error "boot.bin must be (1 + STAGE2_SECTORS) sectors"
%endif

toc:
        db "MOSCOWFS"
        dd 3, 0
        FILE_ENTRY "ussr_rt.bin", f_runtime
        FILE_ENTRY "moscow.ru", f_moscow
        FILE_ENTRY "russia.ru", f_russia
        times 512 - ($ - toc) db 0

        FILE_DATA f_runtime, "ussr_rt.bin"
        FILE_DATA f_moscow, "moscow.ru"
        FILE_DATA f_russia, "russia.ru"

        align 65536, db 0
