; i4.asm - w16 SHELL.DLL: the unsigned 32-bit multiply and divide helpers
; OpenWatcom's 16-bit code calls (SHELL.DLL is linked without the C
; library). Register interface (OpenWatcom): __U4M: DX:AX * CX:BX -> DX:AX;
; __U4D: DX:AX / CX:BX -> quotient DX:AX, remainder CX:BX. Other registers
; are kept.
        bits 16
        cpu 286

segment SHELL_TEXT public align=16 class=CODE use16

global __U4M
__U4M:
        push si
        push di
        mov si, ax              ; a low
        mov di, dx              ; a high
        mul bx                  ; a low * b low
        push dx
        push ax
        mov ax, di
        mul bx                  ; a high * b low (low word counts)
        mov di, ax
        mov ax, si
        mul cx                  ; a low * b high (low word counts)
        add di, ax
        pop ax
        pop dx
        add dx, di
        pop di
        pop si
        retf

; shift-and-subtract: 32 steps
global __U4D
__U4D:
        push si
        push di
        push bp
        mov si, 32
        xor di, di              ; remainder high
        xor bp, bp              ; remainder low
.step:  shl ax, 1               ; dividend (becomes the quotient) << 1
        rcl dx, 1
        rcl bp, 1               ; remainder << 1 | the bit shifted out
        rcl di, 1
        cmp di, cx
        jb .next
        ja .sub
        cmp bp, bx
        jb .next
.sub:   sub bp, bx
        sbb di, cx
        or ax, 1
.next:  dec si
        jnz .step
        mov bx, bp
        mov cx, di
        pop bp
        pop di
        pop si
        retf
