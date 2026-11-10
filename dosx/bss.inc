; bss.inc - uninitialised data (part of the resident image).

CBSEL_MAX       equ H3_SIZE / H3_CHUNK + 2

                alignb 16
gdt             resb GDT_ENTRIES * 8
tss             resb 2Ch
ldt_bm          resb 1024               ; one bit per LDT descriptor
pmvec           resd 256
excvec          resd 32
rmorig          resd NHOOK
rmhooked        resb NHOOK
                alignb 2
ps2_pm          resd 1                  ; INT 15h C207h: the client's handler
cbsel           resw CBSEL_MAX
cb_static       resb CB_STATIC * 8
rm_dta          resb 128
                alignb 2
tbuf            resb TB_SIZE
; used only before KERNEL runs (its command tail, path, EXEC block) or
; after it has ended (a fatal message): in the transfer buffer
ktail           equ tbuf
kpath           equ tbuf + 130
epb             equ tbuf + 274
msgbuf          equ tbuf + 288
hs_stack        resb HS_SIZE
hs_stack_top:
; DOSX's own real-mode stack: DOS keeps the EXEC call's return frame on
; it while KERNEL runs, so the host stack must be another one
init_stack      resb INIT_STACK
stack_init:
