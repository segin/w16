; tk19n.asm - the GlobalNotify procedure of the global heap probe TK19
; (tests/apps/tk19.c). It records each call (its argument and the DS, ES
; and SS it was called with) in TK19's DGROUP and returns notify_ret.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

extern _notify_n                ; WORD: number of calls
extern _notify_rec              ; WORD[]: 4 words per call
extern _notify_ret              ; WORD: what to return

; BOOL FAR PASCAL NOTIFY_PROC(HGLOBAL h)
global NOTIFY_PROC
NOTIFY_PROC:
        push bp
        mov bp, sp
        push ds
        push bx
        push ax
        mov ax, seg _notify_n
        mov ds, ax
        mov bx, [_notify_n]
        cmp bx, 8
        jae .full
        shl bx, 3
        mov ax, [bp + 6]
        mov [_notify_rec + bx], ax
        mov ax, [bp - 2]                ; DS on entry
        mov [_notify_rec + bx + 2], ax
        mov [_notify_rec + bx + 4], es
        mov [_notify_rec + bx + 6], ss
.full:
        inc word [_notify_n]
        pop ax
        mov ax, [_notify_ret]
        pop bx
        pop ds
        pop bp
        retf 2
