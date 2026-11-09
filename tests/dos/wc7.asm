; wc7.asm - WC7.COM: a TSR that answers one INT 2Fh query the way a running
; Windows (or task switcher) would, to see what WIN.COM says when it finds
; one. Usage: WC7 n, where n picks the answer:
;   1  AX=160Ah -> AX=0, BX=030Ah, CX=2 (Windows 3.10, standard mode)
;   2  AX=160Ah -> AX=0, BX=030Ah, CX=3 (Windows 3.10, enhanced mode)
;   3  AX=1600h -> AX=0A03h (enhanced mode 3.10)
;   4  AX=1600h -> AX=0002h (Windows/386 2.x)
;   5  AX=4680h -> AX=0 (Windows 3.0 real or standard mode)
;   6  AX=4B02h -> BX=0, ES:DI=our entry, AX=0 (a task switcher)
;   7  AX=1600h -> AX=00FFh (Windows/386 2.x, the other value)
; Stays resident (it is never removed: run it in a scratch DOS session).
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

start:  jmp install

old2f:  dd 0
which:  db 0

int2f:  cmp byte [cs:which], 1
        je .w1
        cmp byte [cs:which], 2
        je .w2
        cmp byte [cs:which], 3
        je .w3
        cmp byte [cs:which], 4
        je .w4
        cmp byte [cs:which], 5
        je .w5
        cmp byte [cs:which], 6
        je .w6
        cmp byte [cs:which], 7
        je .w7
.chain: jmp far [cs:old2f]
.w1:    cmp ax, 160Ah
        jne .chain
        xor ax, ax
        mov bx, 030Ah
        mov cx, 2
        iret
.w2:    cmp ax, 160Ah
        jne .chain
        xor ax, ax
        mov bx, 030Ah
        mov cx, 3
        iret
.w3:    cmp ax, 1600h
        jne .chain
        mov ax, 0A03h
        iret
.w4:    cmp ax, 1600h
        jne .chain
        mov ax, 0002h
        iret
.w7:    cmp ax, 1600h
        jne .chain
        mov ax, 00FFh
        iret
.w5:    cmp ax, 4680h
        jne .chain
        xor ax, ax
        iret
.w6:    cmp ax, 4B02h
        jne .chain
        push cs
        pop es
        mov di, switcher
        xor bx, bx
        xor ax, ax
        iret
switcher:
        retf
resident_end:

install:
        mov al, [82h]
        sub al, '0'
        mov [which], al
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f+2], es
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        mov dx, (resident_end - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h
