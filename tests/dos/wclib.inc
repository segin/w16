; wclib.inc - shared helpers for the WIN.COM probes (WC1, WC2): append
; text to C:\WC.LOG. 8086, plain INT 21h. Include after "org 100h" code.
;
;   log_open       open (or create) C:\WC.LOG, seek to its end
;   log_close
;   log_str  SI    write the ASCIIZ string at DS:SI
;   log_chr  AL    write one byte
;   log_hex4 AX / log_hex2 AL
;   log_nl         CR LF
;   log_quoted     DS:SI, CX bytes: printable as is, others as \xx

log_open:
        push ax
        push cx
        push dx
        mov dx, log_name
        mov ax, 3D02h
        int 21h
        jnc .opened
        mov dx, log_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .fail
.opened:
        mov [cs:log_handle], ax
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
.fail:
        pop dx
        pop cx
        pop ax
        ret

log_close:
        push ax
        push bx
        mov bx, [cs:log_handle]
        mov ah, 3Eh
        int 21h
        pop bx
        pop ax
        ret

log_chr:
        push ax
        push bx
        push cx
        push dx
        push ds
        push cs
        pop ds                          ; callers may point DS elsewhere
        mov [log_byte], al
        mov bx, [cs:log_handle]
        mov dx, log_byte
        mov cx, 1
        mov ah, 40h
        int 21h
        pop ds
        pop dx
        pop cx
        pop bx
        pop ax
        ret

log_str:
        push ax
        push si
.l:     lodsb
        test al, al
        jz .e
        call log_chr
        jmp .l
.e:     pop si
        pop ax
        ret

log_nl:
        push ax
        mov al, 0Dh
        call log_chr
        mov al, 0Ah
        call log_chr
        pop ax
        ret

log_hex4:
        push ax
        mov al, ah
        call log_hex2
        pop ax
log_hex2:
        push ax
        push cx
        mov ah, al
        mov cl, 4
        shr al, cl
        call .dig
        mov al, ah
        and al, 0Fh
        call .dig
        pop cx
        pop ax
        ret
.dig:   add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     jmp log_chr

; DS:SI, CX bytes
log_quoted:
        push ax
        push cx
        push si
        jcxz .e
.l:     lodsb
        cmp al, ' '
        jb .esc
        cmp al, 7Eh
        ja .esc
        cmp al, '\'
        je .esc
        call log_chr
        jmp .n
.esc:   push ax
        mov al, '\'
        call log_chr
        pop ax
        call log_hex2
.n:     loop .l
.e:     pop si
        pop cx
        pop ax
        ret

log_name:   db 'C:\WC.LOG', 0
log_handle: dw 0
log_byte:   db 0
