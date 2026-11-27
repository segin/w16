; tk77d.asm - TK77D.COM, the DOS program TK77 (tests/apps/tk77.c) has
; ExitWindowsExec run: appends "D [tail]" and the tail's length byte (two
; hex digits) to C:\TK77.LOG, the tail being its command tail as DOS
; gives it (PSP 81h, up to the CR).
        org 100h
        mov ax, 3D01h           ; open for writing
        mov dx, fname
        int 21h
        jnc .open
        mov ah, 3Ch             ; or create
        xor cx, cx
        mov dx, fname
        int 21h
        jc .out
.open:  mov bx, ax
        mov ax, 4202h           ; to the end
        xor cx, cx
        xor dx, dx
        int 21h
        ; the line: "D [" tail "] " len CR LF
        mov di, line + 3
        mov si, 81h
        xor ch, ch
        mov cl, [80h]
        cmp cl, 126
        jbe .n
        mov cl, 126
.n:     jcxz .e
        rep movsb
.e:     mov al, ']'
        stosb
        mov al, ' '
        stosb
        mov al, [80h]
        mov ah, al
        shr al, 4
        call hex
        mov al, ah
        and al, 0Fh
        call hex
        mov ax, 0A0Dh
        stosw
        mov cx, di
        sub cx, line
        mov dx, line
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.out:   mov ax, 4C00h
        int 21h

hex:    add al, '0'
        cmp al, '9'
        jbe .d
        add al, 7
.d:     stosb
        ret

fname:  db 'C:\TK77.LOG', 0
line:   db 'D ['
        times 140 db 0
