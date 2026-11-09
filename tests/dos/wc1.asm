; wc1.asm - WC1.COM, a logging stand-in for the programs WIN.COM starts
; (copied over WINDOWS\SYSTEM\WSWAP.EXE, DOSX.EXE, WIN386.EXE in a scratch
; copy of an install; DOS runs it whatever the extension, as it has no MZ
; header). It appends what it was given to C:\WC.LOG and exits:
;   - its own path (after the environment), its PSP, the parent PSP and the
;     parent's program path, the top of memory (PSP:2), the largest free
;     DOS block;
;   - the command tail, exactly (non-printable bytes as \xx), and the two
;     FCBs DOS parsed from it;
;   - the environment: segment, size and owner of its block, every string;
;   - the video mode, the cursor, a few interrupt vectors, INT 2Fh 1600h
;     and 4300h, the current directory.
; Exit code: the first decimal number in C:\WCRC.TXT if it exists, else 0;
; the file is rewritten without it unless it is the last one. If
; C:\WCWAIT.TXT exists it waits 4 seconds first (for screenshots).
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

start:
        mov sp, stack_top
        mov bx, (end_of_image - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h
        call log_open

        mov si, s_head
        call log_str
        call own_path                   ; DS:SI, CX = our path
        call log_quoted
        push cs
        pop ds
        call log_nl

        mov si, s_psp
        call log_str
        mov ax, cs
        call log_hex4
        mov si, s_parent
        call log_str
        mov ax, [16h]
        call log_hex4
        mov si, s_pprog
        call log_str
        mov es, [16h]
        mov ax, [es:2Ch]
        call env_prog                   ; DS:SI, CX = program path in env AX
        call log_quoted
        push cs
        pop ds
        mov si, s_top
        call log_str
        mov ax, [2]
        call log_hex4
        mov si, s_free
        call log_str
        mov bx, 0FFFFh
        mov ah, 48h
        int 21h
        mov ax, bx
        call log_hex4
        call log_nl

        mov si, s_tail
        call log_str
        mov al, [80h]
        call log_hex2
        mov al, ' '
        call log_chr
        mov al, '['
        call log_chr
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        call log_quoted
        mov si, s_cr                    ; "] then the terminator byte"
        call log_str
        mov bl, [80h]
        xor bh, bh
        mov al, [bx+81h]
        call log_hex2
        call log_nl
        mov si, s_fcb1
        call log_str
        mov si, 5Ch
        mov cx, 12
        call log_quoted
        mov si, s_fcb2
        call log_str
        mov si, 6Ch
        mov cx, 12
        call log_quoted
        mov al, ']'
        call log_chr
        call log_nl

        ; the parent's command tail (WIN.COM's, as it left it)
        mov si, s_ptail
        call log_str
        mov ds, [16h]
        mov al, [80h]
        call log_hex2
        mov al, ' '
        call log_chr
        mov al, '['
        call log_chr
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        call log_quoted
        mov al, ']'
        call log_chr
        mov si, s_penv
        push ds
        push cs
        pop ds
        call log_str
        pop ds
        mov ax, [2Ch]
        push cs
        pop ds
        call log_hex4
        call log_nl

        ; the environment block
        mov si, s_env
        call log_str
        mov ax, [2Ch]
        call log_hex4
        mov si, s_paras
        call log_str
        dec ax
        mov es, ax
        mov ax, [es:3]
        call log_hex4
        mov si, s_owner
        call log_str
        mov ax, [es:1]
        call log_hex4
        call log_nl
        mov ds, [2Ch]
        xor si, si
.envl:  cmp byte [si], 0
        je .envend
        mov di, si                      ; length of this string
        xor cx, cx
.len:   cmp byte [di], 0
        je .gotlen
        inc di
        inc cx
        jmp .len
.gotlen:
        push si
        mov si, s_envs
        push ds
        push cs
        pop ds
        call log_str
        pop ds
        pop si
        call log_quoted
        add si, cx
        inc si
        mov al, ']'
        call log_chr
        call log_nl
        jmp .envl
.envend:
        inc si
        mov ax, [si]                    ; the count word before the path
        push cs
        pop ds
        mov si, s_envcnt
        call log_str
        call log_hex4
        call log_nl

        ; video, cursor
        mov si, s_video
        call log_str
        mov ah, 0Fh
        int 10h
        push bx
        call log_hex2                   ; AL = mode
        mov si, s_cols
        call log_str
        mov al, ah
        call log_hex2
        mov si, s_cursor
        call log_str
        pop bx
        mov ah, 3
        int 10h
        mov ax, dx
        call log_hex4
        call log_nl

        ; vectors
        mov si, s_vec
        call log_str
        mov si, vecs
.vl:    lodsb
        cmp al, 0FFh
        je .ve
        push si
        push ax
        mov al, ' '
        call log_chr
        pop ax
        push ax
        call log_hex2
        mov al, '='
        call log_chr
        pop ax
        mov ah, 35h
        int 21h
        mov ax, es
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, bx
        call log_hex4
        pop si
        jmp .vl
.ve:    call log_nl

        mov si, s_2f
        call log_str
        mov ax, 1600h
        int 2Fh
        call log_hex4
        mov si, s_xms
        call log_str
        mov ax, 4300h
        int 2Fh
        call log_hex2
        mov si, s_cwd
        call log_str
        mov ah, 19h
        int 21h
        add al, 'A'
        call log_chr
        mov al, ':'
        call log_chr
        mov al, '\'
        call log_chr
        mov si, cwd
        xor dl, dl
        mov ah, 47h
        int 21h
        call log_str
        call log_nl
        call log_close

        ; C:\WCWAIT.TXT exists: wait about 4 seconds (for screenshots)
        mov dx, wait_name
        mov ax, 3D00h
        int 21h
        jc .nowait
        mov bx, ax
        mov ah, 3Eh
        int 21h
        push ds
        xor ax, ax
        mov ds, ax
        mov dx, [46Ch]
.wt:    mov ax, [46Ch]
        sub ax, dx
        cmp ax, 73
        jb .wt
        pop ds
.nowait:
        ; exit code from C:\WCRC.TXT: a list of decimal numbers; the
        ; first is used and removed (the last one stays)
        xor bl, bl
        mov dx, rc_name
        mov ax, 3D00h
        int 21h
        jc .exit
        mov bx, ax
        mov dx, cwd
        mov cx, 60
        mov ah, 3Fh
        int 21h
        mov cx, ax
        mov ah, 3Eh
        int 21h
        mov si, cwd
        xor bl, bl
.dl:    jcxz .exit
        lodsb
        sub al, '0'
        cmp al, 9
        ja .sep
        mov bh, al
        mov al, 10
        mul bl
        add al, bh
        mov bl, al
        dec cx
        jmp .dl
.sep:   dec cx                          ; skip separators to the next number
        jcxz .exit
        mov al, [si]
        sub al, '0'
        cmp al, 9
        jbe .rest
        inc si
        jmp .sep
.rest:  push bx                         ; rewrite the file with the rest
        push cx
        mov dx, rc_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        pop cx
        jc .norw
        mov bx, ax
        mov dx, si
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.norw:  pop bx
.exit:  mov al, bl
        mov ah, 4Ch
        int 21h

; our own program path: DS:SI, CX
own_path:
        mov ax, [2Ch]
; the program path after the environment at segment AX: DS:SI, CX
; (DS = AX; CX = 0 if AX is 0)
env_prog:
        xor cx, cx
        xor si, si
        test ax, ax
        jz .r
        mov ds, ax
.f:     cmp word [si], 0
        je .g
        inc si
        jmp .f
.g:     add si, 4                       ; the two zeros and the count word
        mov di, si
.l:     cmp byte [di], 0
        je .r
        inc di
        inc cx
        jmp .l
.r:     ret

%include "wclib.inc"

s_head:   db '== WC1 ', 0
s_psp:    db 'psp=', 0
s_parent: db ' parent=', 0
s_pprog:  db ' parentprog=', 0
s_top:    db ' top=', 0
s_free:   db ' free=', 0
s_tail:   db 'tail=', 0
s_cr:     db '] end=', 0
s_fcb1:   db 'fcb1=[', 0
s_fcb2:   db '] fcb2=[', 0
s_ptail:  db 'parenttail=', 0
s_penv:   db ' parentenv=', 0
s_env:    db 'envseg=', 0
s_paras:  db ' paras=', 0
s_owner:  db ' owner=', 0
s_envs:   db 'env [', 0
s_envcnt: db 'envcount=', 0
s_video:  db 'video mode=', 0
s_cols:   db ' cols=', 0
s_cursor: db ' cursor=', 0
s_vec:    db 'vec', 0
s_2f:     db 'i2f1600=', 0
s_xms:    db ' i2f4300=', 0
s_cwd:    db ' cwd=', 0
rc_name:  db 'C:\WCRC.TXT', 0
wait_name: db 'C:\WCWAIT.TXT', 0
vecs:     db 08h, 09h, 10h, 15h, 1Ch, 21h, 22h, 23h, 24h, 2Fh, 67h, 0FFh
cwd:      times 68 db 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
