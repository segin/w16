; i4.asm - the 32-bit arithmetic helpers OpenWatcom calls in 16-bit code
; (GDI is linked without the C library). Register interface (OpenWatcom C
; library): multiply DX:AX by CX:BX giving DX:AX; divide DX:AX by CX:BX
; giving the quotient in DX:AX and the remainder in CX:BX. Other
; registers are kept. 286 code.

        bits 16
        cpu 286

segment I4_TEXT public align=16 class=CODE use16

global __U4M, __I4M
__U4M:
__I4M:
        push si
        push di
        mov si, ax                      ; a_lo
        mov di, dx                      ; a_hi
        mul bx                          ; dx:ax = a_lo * b_lo
        push ax
        push dx
        mov ax, di
        mul bx                          ; a_hi * b_lo (low word)
        mov di, ax
        mov ax, si
        mul cx                          ; a_lo * b_hi (low word)
        add di, ax
        pop dx
        add dx, di
        pop ax
        pop di
        pop si
        retf

; unsigned DX:AX / CX:BX
global __U4D
__U4D:
        push si
        push di
        push bp
        call udiv
        pop bp
        pop di
        pop si
        retf

; signed: the quotient's sign is the XOR of the signs, the remainder's
; the dividend's
global __I4D
__I4D:
        push si
        push di
        push bp
        xor bp, bp
        test dx, dx
        jns .a
        neg dx
        neg ax
        sbb dx, 0
        xor bp, 3                       ; bit 0: negate quotient, bit 1: remainder
.a:     test cx, cx
        jns .b
        neg cx
        neg bx
        sbb cx, 0
        xor bp, 1
.b:     call udiv
        test bp, 1
        jz .c
        neg dx
        neg ax
        sbb dx, 0
.c:     test bp, 2
        jz .d
        neg cx
        neg bx
        sbb cx, 0
.d:     pop bp
        pop di
        pop si
        retf

; dx:ax / cx:bx unsigned -> quotient dx:ax, remainder cx:bx (shift and
; subtract); uses si, di, bp
udiv:
        test cx, cx
        jnz .long
        mov si, ax                      ; 32 / 16: two DIVs
        mov ax, dx
        xor dx, dx
        div bx
        mov di, ax
        mov ax, si
        div bx
        mov bx, dx
        xor cx, cx
        mov dx, di
        ret
.long:
        mov si, 32
        xor di, di                      ; remainder hi
        xor bp, bp                      ; remainder lo
.loop:  shl ax, 1
        rcl dx, 1
        rcl bp, 1
        rcl di, 1
        cmp di, cx
        jb .no
        ja .sub
        cmp bp, bx
        jb .no
.sub:   sub bp, bx
        sbb di, cx
        or ax, 1
.no:    dec si
        jnz .loop
        mov bx, bp
        mov cx, di
        ret
