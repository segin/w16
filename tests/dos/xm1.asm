; xm1.asm - XM1.COM: what the XMS driver reports (boot-test case bigmem,
; ARCHITECTURE.md 9.6 "More than 64 MB"). Prints, in hex:
;   ver   function 00h: AX (version, BCD), BX (revision)
;   08h   AX (largest free block, KB), DX (total free, KB), BL
;   88h   EAX (largest free block, KB), EDX (total free, KB), ECX
;         (highest address), BL (0: no error)
; then allocates with 89h the size 88h gave, locks it (0Ch), and prints
; the handle, the block's address, and 08h/88h again; frees it.
; Source: XMS 3.0 specification. Needs a 386.
        org 100h
        cpu 386
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne nox
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        push cs
        pop es
        mov si, s_ver
        call puts
        mov ah, 00h
        call far [xms]
        call hex16
        mov ax, bx
        call hex16
        call crlf
        call q08
        call q88
        mov [sz], eax
        mov edx, eax
        mov ah, 89h
        call far [xms]
        mov si, s_89
        call puts
        call hex16
        mov ax, dx
        mov [h], dx
        call hex16
        call crlf
        mov dx, [h]
        mov ah, 0Ch
        call far [xms]
        mov si, s_0c
        call puts
        call hex16
        mov ax, dx
        call hex16
        mov ax, bx
        call hex16
        call crlf
        call q08
        call q88
        mov dx, [h]
        mov ah, 0Dh
        call far [xms]
        mov dx, [h]
        mov ah, 0Ah
        call far [xms]
        mov ax, 4C00h
        int 21h
nox:    mov si, s_nox
        call puts
        mov ax, 4C01h
        int 21h

q08:    mov si, s_08
        call puts
        mov ah, 08h
        xor bx, bx
        call far [xms]
        push bx
        call hex16
        mov ax, dx
        call hex16
        pop ax
        call hex16
        jmp crlf

q88:    mov si, s_88
        call puts
        mov ah, 88h
        xor bx, bx
        call far [xms]
        push eax
        call hex32
        mov eax, edx
        call hex32
        mov eax, ecx
        call hex32
        movzx eax, bl
        call hex16
        call crlf
        pop eax
        ret

hex32:  push eax
        shr eax, 16
        call hex4
        pop eax
hex16:  call hex4
        push dx
        mov dl, ' '
        call putc
        pop dx
        ret
hex4:   push ax
        push cx
        push dx
        mov cx, 4
.l:     rol ax, 4
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     mov dl, al
        call putc
        pop ax
        loop .l
        pop dx
        pop cx
        pop ax
        ret
putc:   push ax
        mov ah, 02h
        int 21h
        pop ax
        ret
crlf:   push ax
        push dx
        mov dl, 13
        call putc
        mov dl, 10
        call putc
        pop dx
        pop ax
        ret
puts:   push ax
        push dx
.l:     lodsb
        or al, al
        jz .d
        mov dl, al
        call putc
        jmp .l
.d:     pop dx
        pop ax
        ret

s_ver   db 'ver ', 0
s_08    db '08h ', 0
s_88    db '88h ', 0
s_89    db '89h ', 0
s_0c    db '0Ch ', 0
s_nox   db 'no XMS', 13, 10, 0
xms     dd 0
sz      dd 0
h       dw 0
