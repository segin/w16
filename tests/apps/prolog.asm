; prolog.asm - procedures with the two classic Windows prologues, linked
; into TK5.EXE and TKDLL.DLL and exported there, so the probe can see
; whether the loader patches the prologues of exported procedures.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

; Exported procedures with the Windows prologues. Each returns AX as set
; by the prologue (the DS it would load) in AX, and 1 in DX.
global ASM_PRO_MOV, ASM_PRO_PUSH
ASM_PRO_MOV:
        mov ax, ds
        nop
        inc bp
        push bp
        mov bp, sp
        push ds
        mov ds, ax
        mov dx, 1
        pop ds
        pop bp
        dec bp
        retf
ASM_PRO_PUSH:
        push ds
        pop ax
        nop
        inc bp
        push bp
        mov bp, sp
        push ds
        mov ds, ax
        mov dx, 1
        pop ds
        pop bp
        dec bp
        retf
