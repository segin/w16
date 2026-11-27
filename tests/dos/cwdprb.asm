; cwdprb.asm - CWDPRB.COM: does a DOS box keep its own current directory
; while other VMs change theirs? (ARCHITECTURE.md 12.16; tests/dpmirun.sh
; kind d). CWDPRB TAG DIR: changes to DIR (on the current drive), then for
; about 15 s asks DOS for the current directory (INT 21h 47h) between
; time-slice releases (INT 2Fh 1680h) and counts the answers that are not
; DIR; writes "CWD TAG dir=... other=N" to CW<TAG>.LOG. Two boxes run it
; at once with different directories. Black box: MS-DOS INT 21h 3Bh, 47h,
; 3Ch, 40h; the BIOS tick count at 40:6Ch.
        bits 16
        cpu 386
        org 100h

start:  mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        mov [tag], al
        mov [logname + 2], al
.sk2:   lodsb
        cmp al, ' '
        je .sk2
        dec si
        mov di, dir
.cp:    lodsb
        cmp al, 13
        je .cd
        cmp al, ' '
        je .cd
        stosb
        jmp .cp
.cd:    mov byte [di], 0
        mov dx, dir
        mov ah, 3Bh
        int 21h
        push word 40h
        pop es
        mov bx, [es:6Ch]
        add bx, 273                     ; 15 s
        mov [until], bx
.loop:  mov ax, 1680h
        int 2Fh
        mov si, cur
        xor dl, dl
        mov ah, 47h
        int 21h
        ; DIR is "\X" or "X": compare without the leading backslash
        mov si, dir
        cmp byte [si], '\'
        jne .c0
        inc si
.c0:    mov di, cur
.c1:    mov al, [si]
        mov ah, [di]
        and ax, 0DFDFh
        cmp al, ah
        jne .diff
        cmp byte [si], 0
        je .same
        inc si
        inc di
        jmp .c1
.diff:  inc word [other]
.same:  inc word [polls]
        mov ax, [es:6Ch]
        cmp ax, [until]
        js .loop
        ; the log
        push ds
        pop es
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        mov bx, ax
        mov di, line + 4
        mov al, [tag]
        mov [line + 4], al
        mov ax, [other]
        mov di, hexo
        call hex4
        mov ax, [polls]
        mov di, hexp
        call hex4
        mov dx, line
        mov cx, line_end - line
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov ax, 4C00h
        int 21h

hex4:   mov cx, 4
.h:     rol ax, 4
        push ax
        and al, 15
        add al, '0'
        cmp al, '9'
        jbe .d
        add al, 7
.d:     stosb
        pop ax
        loop .h
        ret

logname db 'CWx.LOG', 0
line    db 'CWD x other='
hexo    db '0000 polls='
hexp    db '0000', 13, 10
line_end:
tag     db 0
other   dw 0
polls   dw 0
until   dw 0
dir     times 80 db 0
cur     times 80 db 0
