; bss.inc - uninitialised data (part of the resident image).

CBSEL_MAX       equ H3_SIZE / H3_CHUNK + 2

                alignb 16
gdt             resb GDT_ENTRIES * 8
idt             resb 800h
tss             resb 2Ch
ldt_bm          resb 1024               ; one bit per LDT descriptor
pmvec           resd 256
excvec          resd 32
rmorig          resd 256
rmhooked        resb 256
cbsel           resw CBSEL_MAX
ktail           resb 130
kpath           resb 144
epb             resb 14
msgbuf          resb 256
rm_dta          resb 128
                alignb 2
h3_stack        resb H3_SIZE
h3_stack_top:
tbuf            resb TB_SIZE
hs_stack        resb HS_SIZE
hs_stack_top:
; DOSX's own real-mode stack: DOS keeps the EXEC call's return frame on
; it while KERNEL runs, so the host stack must be another one
init_stack      resb INIT_STACK
stack_init:
