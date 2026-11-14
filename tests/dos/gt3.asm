; gt3.asm - GT3.COM: fixed text output for the 386 grabber tests
; (tests/grabber3.sh): a windowed DOS box's screen changes in known ways,
; so screenshots of Microsoft's VGA.3GR and w16's can be compared.
;
; It clears the screen (mode 3, through the BIOS), writes 30 numbered lines
; through DOS (the screen scrolls), then waits for keys:
;   l   one more numbered line (scrolls by one)
;   a   a line with every attribute 00-FF (two characters each, written
;       through the BIOS at the cursor row), then a new line
;   c   the cursor moves to row 5 column 33 (BIOS)
;   s   the cursor shape becomes a block (0007h); S: back (0607h)
;   w   80 'W' characters written directly to B800:0000 (row 0)
;   q   end
        bits 16
        cpu 8086
        org 100h

start:
        mov ax, 0003h
        int 10h
        mov cx, 30
.l:     call numline
        loop .l
keys:
        mov ah, 0
        int 16h
        cmp al, 'q'
        je .q
        cmp al, 'l'
        jne .na
        call numline
        jmp keys
.na:    cmp al, 'a'
        jne .nb
        call attrline
        jmp keys
.nb:    cmp al, 'c'
        jne .nc
        mov ah, 2
        mov bh, 0
        mov dx, 0521h
        int 10h
        jmp keys
.nc:    cmp al, 's'
        jne .nd
        mov ah, 1
        mov cx, 0007h
        int 10h
        jmp keys
.nd:    cmp al, 'S'
        jne .ne
        mov ah, 1
        mov cx, 0607h
        int 10h
        jmp keys
.ne:    cmp al, 'w'
        jne keys
        push es
        mov ax, 0B800h
        mov es, ax
        xor di, di
        mov ax, 1E57h
        mov cx, 80
        rep stosw
        pop es
        jmp keys
.q:     mov ax, 4C00h
        int 21h

; "line nn: the quick brown fox ..." CR LF through DOS
numline:
        push cx
        inc word [count]
        mov ax, [count]
        mov bl, 10
        div bl
        add ax, 3030h
        mov [num], ax
        mov dx, msg
        mov ah, 9
        int 21h
        pop cx
        ret

; 128 cells with attributes 00h-FFh in steps of 2 (a character each)
; on the cursor row, columns 0-79 for 00h-9Eh, then a new line
attrline:
        push cx
        mov ah, 3
        mov bh, 0
        int 10h                         ; DH = row
        xor bl, bl
        xor dl, dl
.c:     mov ah, 2
        mov bh, 0
        int 10h
        mov ah, 9
        mov al, 'A'
        add al, dl
        cmp al, 'z'
        jbe .ok
        mov al, '0'
.ok:    mov bh, 0
        mov cx, 1
        int 10h
        add bl, 2
        inc dl
        cmp dl, 80
        jb .c
        mov dx, crlf
        mov ah, 9
        int 21h
        pop cx
        ret

count   dw 0
msg     db 'line '
num     db '00: the quick brown fox jumps over the lazy dog 0123456789', 13, 10, '$'
crlf    db 13, 10, '$'
