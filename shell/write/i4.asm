; i4.asm - w16 Write: the 32-bit arithmetic helpers OpenWatcom calls in
; 16-bit code (Write is linked without the C library), and KERNEL's
; __AHINCR for huge blocks. Register interface (OpenWatcom's): multiply
; DX:AX by CX:BX giving DX:AX; divide DX:AX by CX:BX giving the quotient
; in DX:AX and the remainder in CX:BX; other registers kept.

        bits 16
        cpu 8086

extern __AHINCR                         ; KERNEL.114, a constant

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

; DX:AX / CX:BX, unsigned, by shift and subtract: quotient DX:AX,
; remainder CX:BX. Uses SI, DI, BP.
udiv:
        test cx, cx
        jnz .long
        ; 32 / 16: two divides
        mov si, ax
        mov ax, dx
        xor dx, dx
        div bx                          ; high word
        mov di, ax
        mov ax, si
        div bx                          ; low word, remainder in dx
        mov bx, dx
        mov dx, di
        xor cx, cx
        ret
.long:
        mov si, 32
        xor di, di                      ; remainder di:bp
        xor bp, bp
.loop:  shl ax, 1
        rcl dx, 1
        rcl bp, 1
        rcl di, 1
        cmp di, cx
        jb .next
        ja .sub
        cmp bp, bx
        jb .next
.sub:   sub bp, bx
        sbb di, cx
        inc ax                          ; quotient bit (ax bit 0 was 0)
.next:  dec si
        jnz .loop
        mov bx, bp
        mov cx, di
        ret

; WORD ahincr(void): the selector increment between the 64K pieces of a
; huge block (KERNEL exports it as a constant)
global ahincr_
ahincr_:
        mov ax, __AHINCR
        retf

; void dos_rename(LPCSTR from, LPCSTR to): DOS function 56h through
; KERNEL's DOS3Call (the documented way for Windows programs)
extern DOS3CALL
global dos_rename_
dos_rename_:                            ; watcall far: from in DX:AX, to in CX:BX
        push ds
        push es
        push di
        mov ds, dx
        mov dx, ax
        mov es, cx
        mov di, bx
        mov ah, 56h
        call far DOS3CALL
        pop di
        pop es
        pop ds
        retf
