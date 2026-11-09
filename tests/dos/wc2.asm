; wc2.asm - WC2.COM: run right after a program (WIN) to record how it
; left the machine. Appends to C:\WC.LOG:
;   == WC2 <command tail>
;   rc=<INT 21h 4Dh: AH type, AL code> video mode=.. cols=.. cursor=RRCC
;   |<each screen row, trailing blanks dropped; trailing empty rows dropped>
; The rows are read from the text buffer (B800h, or B000h in mode 7); in a
; graphics mode only the mode is recorded. A tail starting with "!" also
; sets video mode 3 afterwards (a known state for the next test).
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

start:
        mov ah, 4Dh                     ; first: before anything else exits
        int 21h
        mov [rc], ax
        mov sp, stack_top
        mov bx, (end_of_image - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h
        call log_open
        mov si, s_head
        call log_str
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        call log_quoted
        call log_nl
        mov si, s_rc
        call log_str
        mov ax, [rc]
        call log_hex4
        mov si, s_video
        call log_str
        mov ah, 0Fh
        int 10h
        mov [mode], al
        mov [cols], ah
        mov [page], bh
        call log_hex2
        mov si, s_cols
        call log_str
        mov al, [cols]
        call log_hex2
        mov si, s_cursor
        call log_str
        mov bh, [page]
        mov ah, 3
        int 10h
        mov ax, dx
        call log_hex4
        call log_nl

        mov al, [mode]
        mov dx, 0B800h
        cmp al, 7
        jne .notmono
        mov dx, 0B000h
        jmp .text
.notmono:
        cmp al, 3
        ja .done                        ; graphics: no text to read
.text:
        ; rows: BIOS 40:84 + 1 (EGA/VGA), page offset 40:4E
        push ds
        mov ax, 40h
        mov ds, ax
        mov bl, [84h]
        mov di, [4Eh]
        pop ds
        inc bl
        mov [rows], bl
        mov es, dx
        ; find the last non-blank row
        xor ax, ax
        mov al, [rows]
        mov [last], al
.findlast:
        cmp byte [last], 0
        je .done
        mov al, [last]
        dec al
        call row_len                    ; CX = length of row AL
        test cx, cx
        jnz .dump
        dec byte [last]
        jmp .findlast
.dump:
        xor al, al
.rowl:  cmp al, [last]
        jae .done
        push ax
        call row_len
        push ax
        mov al, '|'
        call log_chr
        pop ax
        call row_ptr                    ; SI = offset of row AL in ES
.cl:    jcxz .rowe
        mov al, [es:si]
        cmp al, ' '
        jb .esc
        cmp al, 7Eh
        ja .esc
        cmp al, '\'
        je .esc
        call log_chr
        jmp .cn
.esc:   push ax
        mov al, '\'
        call log_chr
        pop ax
        call log_hex2
.cn:    add si, 2
        dec cx
        jmp .cl
.rowe:  call log_nl
        pop ax
        inc al
        jmp .rowl
.done:  call log_close
        cmp byte [82h], '!'             ; "WC2 !...": then set text mode 3
        jne .exit
        mov ax, 0003h
        int 10h
.exit:  mov ax, 4C00h
        int 21h

; SI = page offset + row AL * cols * 2
row_ptr:
        push ax
        push dx
        mul byte [cols]
        shl ax, 1
        add ax, di
        mov si, ax
        pop dx
        pop ax
        ret

; CX = row AL's length without trailing blanks
row_len:
        push si
        push ax
        call row_ptr
        xor cx, cx
        mov cl, [cols]
        mov ax, cx
        dec ax
        shl ax, 1
        add si, ax                      ; last cell
.l:     jcxz .e
        mov al, [es:si]
        cmp al, ' '
        je .b
        test al, al
        jnz .e
.b:     sub si, 2
        dec cx
        jmp .l
.e:     pop ax
        pop si
        ret

%include "wclib.inc"

s_head:   db '== WC2 ', 0
s_rc:     db 'rc=', 0
s_video:  db ' video mode=', 0
s_cols:   db ' cols=', 0
s_cursor: db ' cursor=', 0
rc:       dw 0
mode:     db 0
cols:     db 0
page:     db 0
rows:     db 0
last:     db 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
