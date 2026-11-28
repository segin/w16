; txtdump.asm - TXTDUMP.COM: the text screen to C:\SCREEN.TXT, for a
; test to read what a DOS screen shows (boot-test faultrep: the fault
; report in 80x50). The first line is "rows=R cols=C" (the BIOS data
; area: 40:84h + 1, or 25 when it is 0; 40:4Ah), then each row's
; characters, the trailing blanks cut, one line each. The text buffer is
; B800h (B000h in mode 7).
        org 100h
        push ds
        xor ax, ax
        mov ds, ax
        mov al, [484h]
        mov bx, [44Ah]
        mov cl, [449h]
        pop ds
        inc al
        cmp al, 1
        ja .rows
        mov al, 25
.rows:  xor ah, ah
        mov [rows], ax
        mov [cols], bx
        mov word [vseg], 0B800h
        cmp cl, 7
        jne .seg
        mov word [vseg], 0B000h
.seg:   mov ah, 3Ch                     ; C:\SCREEN.TXT made
        xor cx, cx
        mov dx, fname
        int 21h
        jc .out
        mov [fh], ax
        mov di, buf                     ; "rows=R cols=C"
        mov si, s_rows
        call copy
        mov ax, [rows]
        call putdec
        mov si, s_cols
        call copy
        mov ax, [cols]
        call putdec
        mov ax, 0A0Dh
        stosw
        call flush
        xor bp, bp                      ; the row
.row:   cmp bp, [rows]
        jae .done
        mov ax, bp
        mul word [cols]
        shl ax, 1
        mov si, ax
        mov cx, [cols]
        mov di, buf
        push ds
        mov ds, [vseg]
.ch:    lodsw
        stosb
        loop .ch
        pop ds
.trim:  cmp di, buf                     ; the trailing blanks cut
        je .eol
        cmp byte [di - 1], ' '
        je .cut
        cmp byte [di - 1], 0
        jne .eol
.cut:   dec di
        jmp .trim
.eol:   mov ax, 0A0Dh
        stosw
        call flush
        inc bp
        jmp .row
.done:  mov ah, 3Eh
        mov bx, [fh]
        int 21h
.out:   mov ax, 4C00h
        int 21h

; buf up to DI to the file
flush:  mov cx, di
        sub cx, buf
        mov dx, buf
        mov bx, [fh]
        mov ah, 40h
        int 21h
        ret

; the string at SI (0-terminated) to DI
copy:   lodsb
        test al, al
        jz .e
        stosb
        jmp copy
.e:     ret

; AX in decimal to DI
putdec: xor cx, cx
        mov bx, 10
.d:     xor dx, dx
        div bx
        push dx
        inc cx
        test ax, ax
        jnz .d
.p:     pop ax
        add al, '0'
        stosb
        loop .p
        ret

fname   db 'C:\SCREEN.TXT', 0
s_rows  db 'rows=', 0
s_cols  db ' cols=', 0
rows    dw 0
cols    dw 0
vseg    dw 0
fh      dw 0
buf     times 300 db 0
