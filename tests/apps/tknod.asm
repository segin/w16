; tknod.asm - TKNOD.DLL: a DLL without a data segment. Its entry point
; writes the registers it got to C:\TKNOD.REG (ax bx cx dx si di bp ds es
; ss sp, 22 bytes; there is no data segment to keep them in) and returns
; 1 (success); NOD_GET returns 1234h.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

global nodentry, NOD_GET
nodentry:
        push sp
        push ss
        push es
        push ds
        push bp
        push di
        push si
        push dx
        push cx
        push bx
        push ax
        mov bp, sp
        push cs                         ; code segments are readable
        pop ds
        mov dx, regfile
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .done
        mov bx, ax
        push ss
        pop ds
        mov dx, bp
        mov cx, 22
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.done:
        pop ax
        pop bx
        pop cx
        pop dx
        pop si
        pop di
        pop bp
        pop ds
        pop es
        add sp, 4
        mov ax, 1
        retf
NOD_GET:
        mov ax, 1234h
        retf

regfile db "C:\TKNOD.REG", 0
