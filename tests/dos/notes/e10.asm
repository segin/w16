; e10.asm - throwaway TSR: records INT 10h calls and INT 21h output calls
; (AH 02h, 06h, 09h, 40h) with the caller's CS:IP in a ring of 1024
; entries: tag (10h/21h), AX, BX, CX, DX, CS, IP (14 bytes; for 09h the
; first 16 bytes of the string, for 40h the first 16 bytes of the buffer,
; follow in a second table). "E10 D" writes the ring to C:\E10.LOG.
        cpu 286
        org 100h
        jmp init
NENT    equ 256
old10:  dd 0
old21:  dd 0
old2f:  dd 0
pos:    dw 0
count:  dw 0
ring:   times NENT * 14 db 0
text:   times NENT * 32 db 0
tails:  times NENT * 32 db 0

; record: AL tag; registers of the call on the stack frame
; [bp+...]: after "push bp; mov bp, sp" in the handler: [bp+2] IP, [bp+4] CS
rec:    ; tag in [cs:tag]; the caller's CS:IP at [bp+4], [bp+2]
        push si
        push ax
        mov si, [cs:pos]
        imul si, si, 14
        mov al, [cs:tag]
        mov [cs:ring + si], al
        pop ax
        push ax
        mov [cs:ring + si + 1], ax
        mov [cs:ring + si + 3], bx
        mov [cs:ring + si + 5], cx
        mov [cs:ring + si + 7], dx
        mov ax, [bp + 4]
        mov [cs:ring + si + 9], ax
        mov ax, [bp + 2]
        mov [cs:ring + si + 11], ax
        pop ax
        pop si
        ret

tag:    db 0

new10:  push bp
        mov bp, sp
        push cx
        mov byte [cs:tag], 10h
        call rec
        pop cx
        call advance
        pop bp
        jmp far [cs:old10]

new2f:  cmp ax, 4A05h
        jne .c
        push bp
        mov bp, sp
        push cx
        mov byte [cs:tag], 2Fh
        push bx
        mov bx, si                      ; BX column: SI
        call rec
        pop bx
        pop cx
        call advance
        pop bp
.c:     jmp far [cs:old2f]

new21:  cmp ah, 02h
        je .log
        cmp ah, 06h
        je .log
        cmp ah, 09h
        je .log
        cmp ah, 40h
        je .log
        cmp ah, 4Bh
        je .log
        cmp ax, 0E10Dh
        jne .chain
        push cs
        pop es
        iret
.chain: jmp far [cs:old21]
.log:   push bp
        mov bp, sp
        push cx
        mov byte [cs:tag], 21h
        call rec
        pop cx
        ; the text: DS:DX (09h, 40h)
        cmp ah, 09h
        je .txt
        cmp ah, 4Bh
        je .txt
        cmp ah, 40h
        jne .adv
.txt:   push si
        push di
        push es
        push cx
        push ax
        push cs
        pop es
        mov di, [cs:pos]
        mov cl, 5
        shl di, cl
        add di, text
        mov si, dx
        mov cx, 32
        cld
        rep movsb
        pop ax
        push ax
        cmp ah, 4Bh
        jne .notail
        ; the tail: the far pointer at the caller's ES:BX + 2 (the
        ; caller's ES pushed above: [sp] AX, [sp+2] CX, [sp+4] ES)
        mov bp, sp
        push ds
        push si
        mov si, [bp + 4]                ; the caller's ES
        mov ds, si
        lds si, [bx + 2]
        mov di, [cs:pos]
        shl di, 5
        add di, tails
        mov cx, 32
        rep movsb
        pop si
        pop ds
.notail:
        pop ax
        pop cx
        pop es
        pop di
        pop si
.adv:   call advance
        pop bp
        jmp far [cs:old21]

advance:
        push ax
        mov ax, [cs:pos]
        inc ax
        and ax, NENT - 1
        mov [cs:pos], ax
        inc word [cs:count]
        pop ax
        ret

init:   mov si, 81h
.s:     lodsb
        cmp al, ' '
        je .s
        cmp al, 'D'
        je dump
        mov ax, 3510h
        int 21h
        mov [old10], bx
        mov [old10 + 2], es
        mov ax, 3521h
        int 21h
        mov [old21], bx
        mov [old21 + 2], es
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        mov ax, 252Fh
        mov dx, new2f
        int 21h
        mov ax, 2510h
        mov dx, new10
        int 21h
        mov ax, 2521h
        mov dx, new21
        int 21h
        mov dx, (init - $$ + 100h + 15) / 16
        mov ax, 3100h
        int 21h
dump:   mov ax, 0E10Dh
        int 21h                         ; ES = the resident copy
        push es
        pop ds
        push cs
        pop es
        mov ah, 3Ch
        xor cx, cx
        push ds
        push cs
        pop ds
        mov dx, fname
        int 21h
        pop ds
        jc .out
        mov bx, ax
        mov ah, 40h                     ; pos, count, then ring, text
        mov dx, pos
        mov cx, 4 + NENT * 14 + NENT * 64
        int 21h
        mov ah, 3Eh
        int 21h
.out:   mov ax, 4C00h
        int 21h
fname:  db 'C:\E10.LOG', 0
