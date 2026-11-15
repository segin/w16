; gvdraw.inc - the fixed screens of GV1 (tests/dos/gv1.asm) and GRT
; (tests/dos/grt.asm): gv_draw sets the BIOS mode [mode] and draws its
; pattern, with the palette changed when [pal] is 1 (see gv1.asm).

gv_draw:
        mov al, [mode]
        xor ah, ah
        int 10h
        mov al, [mode]
        cmp al, 4
        jb text
        cmp al, 7
        je text
        jb cga
        cmp al, 13h
        je m13
        jmp planar

; ---- text modes
text:
        mov ax, 0B800h
        cmp byte [mode], 7
        jne .t1
        mov ax, 0B000h
.t1:    mov es, ax
        mov ax, 40h
        mov ds, ax
        mov cx, [4Ah]           ; columns
        push cs
        pop ds
        mov [cols], cx
        xor di, di
        xor dx, dx              ; row
.row:   xor bx, bx              ; column
.col:   mov al, dl
        mov ah, 7
        mul ah
        add al, bl
        mov cl, al              ; char = row*7+col
        mov al, dl
        mov ah, 9
        mul ah
        add al, bl
        mov ah, al              ; attribute = row*9+col
        mov al, cl
        stosw
        inc bx
        cmp bx, [cols]
        jb .col
        inc dx
        cmp dx, 25
        jb .row
        ; the title on row 0
        xor di, di
        mov si, title
.tt:    lodsb
        or al, al
        jz .td
        mov ah, 1Fh
        stosw
        jmp .tt
.td:    mov ah, 2               ; cursor 10,20
        xor bh, bh
        mov dx, 0A14h
        int 10h
        mov ah, 1
        mov cx, 0407h
        int 10h
        cmp byte [pal], 0
        je .tp
        mov ax, 1003h           ; blink off: bright backgrounds
        xor bl, bl
        int 10h
        call setpal
.tp:    ret

; ---- CGA modes 4, 5, 6
cga:
        mov ax, 0B800h
        mov es, ax
        xor di, di
        mov cx, 8000            ; bank 0
.b0:    mov ax, di
        xor al, ah
        add al, 35h
        stosb
        loop .b0
        mov di, 2000h
        mov cx, 8000            ; bank 1
.b1:    mov ax, di
        rol al, 1
        xor al, ah
        stosb
        loop .b1
        cmp byte [pal], 0
        je .cp
        mov ah, 0Bh             ; background/border 1
        mov bx, 0001h
        int 10h
        cmp byte [mode], 6
        je .cp
        mov ah, 0Bh             ; palette 0
        mov bx, 0100h
        int 10h
.cp:    call btitle
        ret

; ---- planar modes 0Dh-12h
planar:
        mov al, [mode]
        mov bx, 8000            ; 0Dh: 40 x 200
        cmp al, 0Dh
        je .sz
        mov bx, 16000           ; 0Eh: 80 x 200
        cmp al, 0Eh
        je .sz
        mov bx, 28000           ; 0Fh, 10h: 80 x 350
        cmp al, 11h
        jb .sz
        mov bx, 38400           ; 11h, 12h: 80 x 480
.sz:    mov [psize], bx
        mov ax, 0A000h
        mov es, ax
        xor cx, cx              ; plane
.pl:    mov dx, 3C4h
        mov al, 2
        mov ah, 1
        shl ah, cl
        out dx, ax
        xor di, di
        push cx
.px:    mov ax, di
        add al, cl
        mov ah, cl
        inc ah
        mul ah
        xor al, ah
        stosb
        cmp di, [psize]
        jb .px
        pop cx
        inc cx
        cmp cx, 4
        jb .pl
        mov dx, 3C4h
        mov ax, 0F02h
        out dx, ax
        cmp byte [pal], 0
        je .pp
        call setpal
.pp:    call btitle
        ret

; ---- 13h
m13:
        mov ax, 0A000h
        mov es, ax
        xor di, di
        mov cx, 64000
.p:     mov ax, di
        xor al, ah
        stosb
        loop .p
        cmp byte [pal], 0
        je .mp
        call setpal
.mp:    call btitle
        ret

; the title through the BIOS at 0,0
btitle:
        mov ah, 2
        xor bh, bh
        xor dx, dx
        int 10h
        mov si, title
.l:     lodsb
        or al, al
        jz .e
        mov ah, 0Eh
        mov bx, 000Fh
        int 10h
        jmp .l
.e:     ret

; palette registers 1 and 2, DAC entries 1-15 and (13h) 16-255
setpal:
        mov ax, 1000h
        mov bx, 3A01h           ; register 1 := 3Ah
        int 10h
        mov ax, 1000h
        mov bx, 1402h           ; register 2 := 14h
        int 10h
        mov ax, 1001h           ; overscan 5
        mov bh, 5
        int 10h
        xor bx, bx
.d:     mov ax, 1010h
        mov dh, bl              ; red = index
        and dh, 3Fh
        mov ch, bl
        shl ch, 1
        and ch, 3Fh             ; green
        mov cl, 3Fh
        sub cl, dh              ; blue
        int 10h
        inc bx
        cmp bx, 256
        jb .d
        ret

mode:   db 0
pal:    db 0
cols:   dw 80
psize:  dw 0
title:  db 'GV1 ', 0, 0, 0, 0
more:   db 'more text ', 0
