; tk73s.asm - TK73's INT 21h AH=36h through DOS3Call (KERNEL.102):
; void __cdecl __far free36(u8 drive, u16 __far *out) stores AX, BX, CX,
; DX and the carry (0 or FFFFh) at out[0..4]. BX and CX are EEEEh before
; the call.
        bits 16
        cpu 286
segment _TEXT public align=16 class=CODE use16
extern DOS3CALL                 ; KERNEL.102

global _free36
_free36:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov dl, [bp + 6]
        mov ah, 36h
        mov bx, 0EEEEh
        mov cx, 0EEEEh
        call far DOS3CALL
        mov si, 0
        sbb si, 0                       ; carry: FFFFh
        push ax
        les di, [bp + 8]
        pop ax
        mov [es:di], ax
        mov [es:di + 2], bx
        mov [es:di + 4], cx
        mov [es:di + 6], dx
        mov [es:di + 8], si
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
