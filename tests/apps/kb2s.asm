; kb2s.asm - KB2's scan-code logger (tests/apps/kb2.c).
;
; sc_code: a protected-mode INT 9 handler put in front of the keyboard
; driver's: it reads port 60h (reading it does not take the byte away:
; the driver reads the same byte after it), records it with a sequence
; number shared with nothing else, and jumps on to the handler that was
; there before (the driver's). kb2.c copies it into a fixed block at
; H_OFF and sets INT 9 to a code alias of it. Block layout:
;   +0 data selector of the block, +2 records so far, +4 room,
;   +6 the previous handler (far), +16 the code, +256 the records (one
;   byte each).
        bits 16
        cpu 286

D_SEL   equ 0
D_N     equ 2
D_MAX   equ 4
D_NEXT  equ 6
D_REC   equ 256

segment _TEXT public align=16 class=CODE use16
global _sc_code, _sc_len
_sc_len dw h_end - h_start
_sc_code:
h_start:
        push ds
        push ax
        push bx
        mov ds, [cs:D_SEL]
        in al, 60h
        mov bx, [D_N]
        cmp bx, [D_MAX]
        jae .full
        mov [bx + D_REC], al
        inc word [D_N]
.full:  pop bx
        pop ax
        pop ds
        jmp far [cs:D_NEXT]
h_end:

; void __cdecl __far get_vec9(WORD FAR *v): v[0] = selector, v[1] = offset
; of the protected-mode INT 9 vector (INT 21h AH=35h)
global _get_vec9
_get_vec9:
        push bp
        mov bp, sp
        push di
        push es
        mov ax, 3509h
        int 21h
        mov dx, es
        les di, [bp + 6]
        mov [es:di], dx
        mov [es:di + 2], bx
        pop es
        pop di
        pop bp
        retf

; void __cdecl __far set_vec9(WORD sel, WORD off) (INT 21h AH=25h)
global _set_vec9
_set_vec9:
        push bp
        mov bp, sp
        push ds
        mov dx, [bp + 8]
        mov ds, [bp + 6]
        mov ax, 2509h
        int 21h
        pop ds
        pop bp
        retf
