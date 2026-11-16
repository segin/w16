; md2as.asm - MD2's call thunks (tests/apps/md2a.c).
;
; (MD2 links MD1's md1s.asm too: its md_call, and its event procedure.)
; md_rcall: as md_call (call a far procedure with 16 words on the stack,
; first element pushed first), but the registers are loaded first from regs[]
; (AX BX CX DX SI DI ES) and stored back after the call into regs[]
; (AX BX CX DX SI DI ES, flags, bytes taken off).
        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

; DWORD __cdecl __far md_rcall(FARPROC f, WORD FAR *args16, WORD FAR *regs)
global _md_rcall
_md_rcall:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        sub sp, 2                       ; [bp - 10]: SP with the arguments
        lds si, [bp + 10]
        mov cx, 16
.push:  push word [si]
        add si, 2
        loop .push
        mov [bp - 10], sp
        lds si, [bp + 14]
        mov ax, [si]
        mov bx, [si + 2]
        mov cx, [si + 4]
        mov dx, [si + 6]
        mov di, [si + 10]
        mov es, [si + 12]
        mov si, [si + 8]
        call far [bp + 6]
        pushf
        push ds
        push si
        lds si, [bp + 14]
        mov [si], ax
        mov [si + 2], bx
        mov [si + 4], cx
        mov [si + 6], dx
        mov [si + 10], di
        mov [si + 12], es
        pop word [si + 8]
        pop ax                          ; DS out (dropped)
        pop word [si + 14]              ; flags
        mov ax, sp
        sub ax, [bp - 10]
        mov [si + 16], ax               ; bytes taken off
        mov sp, [bp - 10]
        add sp, 34
        mov ax, [si]
        mov dx, [si + 6]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
