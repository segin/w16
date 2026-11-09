; wo3.asm - WO3.COM: a real-mode INT 21h and INT 2Fh spy to put between
; two programs of the Windows chain (black-box probe for w16's WINOLDAP.MOD,
; ARCHITECTURE.md 9.6). Copy it over the program to watch (e.g.
; SYSTEM\DOSX.EXE) and the real program to the same name with the last
; letter replaced by "_" (DOSX.EX_). WO3 hooks INT 21h and INT 2Fh, runs
; the real program with its own command tail, FCBs and environment, and
; records every call made through those vectors after it - by the program,
; by its children, and (with WO3 in DOSX's place) by protected-mode code
; whose interrupts DOSX passes down to real mode:
;   INT 21h: the functions marked in want21 (not reads, writes, seeks...);
;   INT 2Fh: everything but 1680h/1689h (idle) and 4B01h/4B02h after the
;            first 8.
; Each record: the registers in, the caller's CS:IP, AX/flags out, and for
; INT 21h AH=4Bh, 3Ch, 3Dh, 41h, 43h, 4Eh, 56h, 5Ah, 5Bh, 6Ch the ASCIIZ
; at DS:DX (or DS:SI), for 4Bh also the command tail and the first 16 bytes
; of the parameter block. The record goes to C:\WO3.LOG when the program
; exits (for DOSX: when Windows ends), and WO3 exits with its exit code.
; Calls made while one is being recorded are passed on unrecorded.

        bits 16
        cpu 8086
        org 100h

NREC    equ 340
RECSZ   equ 2 + 16 + 4 + 4 + 64 + 64 + 16    ; int, regs, caller, out, name, tail, pb

start:
        mov sp, stack_top
        mov bx, (end_of_image - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h

        ; the real program: our own path with the last letter replaced
        push ds
        mov ax, [2Ch]
        mov ds, ax
        xor si, si
.f:     cmp word [si], 0
        je .g
        inc si
        jmp .f
.g:     add si, 4
        mov di, prog
        push cs
        pop es
.c:     lodsb
        stosb
        test al, al
        jnz .c
        pop ds
        mov byte [di-2], '_'

        mov ax, [2Ch]
        mov [pb_env], ax
        mov [pb_tail+2], cs
        mov [pb_fcb1+2], cs
        mov [pb_fcb2+2], cs

        mov ax, 3521h
        int 21h
        mov [old21], bx
        mov [old21+2], es
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f+2], es
        mov dx, int21
        mov ax, 2521h
        int 21h
        mov dx, int2f
        mov ax, 252Fh
        int 21h

        push cs
        pop es
        mov bx, pblock
        mov dx, prog
        mov [save_sp], sp
        mov ax, 4B00h
        int 21h
        cli
        mov ax, cs
        mov ss, ax
        mov sp, [cs:save_sp]
        sti
        push cs
        pop ds
        jnc .ok
        mov [execerr], al
        mov byte [execerr+1], 1
.ok:
        ; unhook (only if nobody hooked after us)
        mov ax, 3521h
        int 21h
        cmp bx, int21
        jne .k1
        push ds
        lds dx, [old21]
        mov ax, 2521h
        int 21h
        pop ds
.k1:    mov ax, 352Fh
        int 21h
        cmp bx, int2f
        jne .k2
        push ds
        lds dx, [old2f]
        mov ax, 252Fh
        int 21h
        pop ds
.k2:
        mov ah, 4Dh
        int 21h
        mov [rc], al
        call dump
        mov al, [rc]
        mov ah, 4Ch
        int 21h

; ---- the hooks ----
int21:
        cmp byte [cs:busy], 0
        jne .pass
        push bx
        mov bl, ah
        xor bh, bh
        cmp byte [cs:want21 + bx], 0
        pop bx
        jne .rec
.pass:  jmp far [cs:old21]
.rec:   mov byte [cs:kind], 21h
        jmp record_call

int2f:
        cmp byte [cs:busy], 0
        jne .pass
        cmp ax, 1680h
        je .pass
        cmp ax, 1689h
        je .pass
        cmp ah, 4Bh
        jne .rec
        cmp al, 2
        ja .rec
        cmp word [cs:n4b], 8
        jae .pass
        inc word [cs:n4b]
.rec:   mov byte [cs:kind], 2Fh
        jmp record_call
.pass:  jmp far [cs:old2f]

; record the call at cs:recp (if room), chain, record AX and flags
record_call:
        mov byte [cs:busy], 1
        push bp
        push di
        push si
        push es
        push ds
        push dx
        push cx
        push bx
        push ax
        ; stack: ax bx cx dx ds es si di bp ip cs fl
        mov bp, sp
        push cs
        pop es
        mov di, [cs:recp]
        cmp di, buf_end - RECSZ
        jbe .room
        inc word [cs:lost]
        mov word [cs:currec], 0
        jmp .chain
.room:  mov [cs:currec], di
        cld
        mov al, [cs:kind]
        stosb
        mov al, 0
        stosb
        mov si, bp
        mov cx, 8
.r:     mov ax, [ss:si]
        stosw
        add si, 2
        loop .r                         ; ax bx cx dx ds es si di
        mov ax, [bp + 18]
        stosw                           ; ip
        mov ax, [bp + 20]
        stosw                           ; cs
        add di, 4                       ; out ax, flags (later)
        ; the name for file functions
        mov si, di
        mov cx, 64 + 64 + 16
        xor al, al
        rep stosb
        mov di, si
        cmp byte [cs:kind], 21h
        jne .done
        mov al, [bp + 1]                ; AH
        cmp al, 6Ch
        je .name_si
        cmp al, 4Bh
        je .name
        cmp al, 3Ch
        je .name
        cmp al, 3Dh
        je .name
        cmp al, 41h
        je .name
        cmp al, 43h
        je .name
        cmp al, 4Eh
        je .name
        cmp al, 56h
        je .name
        cmp al, 5Ah
        je .name
        cmp al, 5Bh
        je .name
        cmp al, 3Bh
        je .name
        cmp al, 39h
        je .name
        cmp al, 3Ah
        je .name
        jmp .done
.name_si:
        mov si, [bp + 12]
        jmp .cp
.name:  mov si, [bp + 6]                ; DX
.cp:    push ds
        mov ds, [bp + 8]
        mov cx, 63
.n:     lodsb
        stosb
        test al, al
        loopnz .n
        pop ds
        cmp byte [bp + 1], 4Bh
        jne .done
        ; EXEC: the tail and the parameter block ES:BX
        mov di, [cs:currec]
        add di, 2 + 16 + 4 + 4 + 64
        push ds
        mov ds, [bp + 10]               ; ES
        mov si, [bp + 2]                ; BX
        push si
        push ds
        lds si, [si + 2]                ; tail pointer
        mov cx, 64
        rep movsb
        pop ds
        pop si
        mov cx, 16
        rep movsb
        pop ds
.done:
        add word [cs:recp], RECSZ
        inc word [cs:nrec]
.chain:
        pop ax
        pop bx
        pop cx
        pop dx
        pop ds
        pop es
        pop si
        pop di
        pop bp
        ; stack: ip cs fl. Keep the record's address on the stack and let
        ; nested calls be recorded while this one runs (an EXEC may not
        ; return until Windows ends)
        push word [cs:currec]           ; rec ip cs fl
        cmp byte [cs:kind], 21h
        mov byte [cs:busy], 0
        je .c21
        push bp                         ; bp rec ip cs fl
        mov bp, sp
        push word [bp + 8]              ; the caller's flags
        mov bp, [bp]                    ; (bp as it was)
        call far [cs:old2f]             ; as INT 2Fh (pops the flags)
        jmp .after
.c21:   push bp
        mov bp, sp
        push word [bp + 8]
        mov bp, [bp]
        call far [cs:old21]
.after: pushf                           ; flo bp rec ip cs fl
        push di
        push bp
        mov bp, sp                      ; [bp+4] flags out, [bp+8] rec
        mov di, [bp + 8]
        test di, di
        jz .nr
        mov [cs:di + 22], ax
        push ax
        mov ax, [bp + 4]
        mov [cs:di + 24], ax
        pop ax
.nr:    pop bp
        pop di
        popf
        pop bp
        pop word [cs:scratch]           ; drop rec (flags kept)
        retf 2

; ---- write the records to C:\WO3.LOG ----
dump:
        mov dx, log_name
        mov ax, 3D02h
        int 21h
        jnc .opened
        mov dx, log_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jnc .opened
        ret
.opened:
        mov [log_handle], ax
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov si, s_head
        call log_str
        mov si, prog
        call log_str
        mov si, s_rc
        call log_str
        mov al, [rc]
        call log_hex2
        cmp byte [execerr+1], 0
        je .noerr
        mov si, s_err
        call log_str
        mov al, [execerr]
        call log_hex2
.noerr: mov si, s_lost
        call log_str
        mov ax, [lost]
        call log_hex4
        call log_nl
        mov bx, buf
        mov cx, [nrec]
.l:     jcxz .end
        push cx
        call one
        pop cx
        add bx, RECSZ
        dec cx
        jmp .l
.end:   mov bx, [log_handle]
        mov ah, 3Eh
        int 21h
        ret

; one record at BX
one:
        mov al, [bx]
        call log_hex2
        mov al, ' '
        call log_chr
        lea si, [bx + 2]
        mov cx, 8
.r:     lodsw
        call log_hex4
        mov al, ' '
        call log_chr
        loop .r
        mov si, s_from
        call log_str
        mov ax, [bx + 20]
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, [bx + 18]
        call log_hex4
        mov si, s_out
        call log_str
        mov ax, [bx + 22]
        call log_hex4
        mov al, ' '
        call log_chr
        mov ax, [bx + 24]
        call log_hex4
        cmp byte [bx + 26], 0
        je .nn
        mov si, s_name
        call log_str
        lea si, [bx + 26]
        call log_str
        mov al, ']'
        call log_chr
.nn:    cmp byte [bx], 21h
        jne .nl
        cmp byte [bx + 3], 4Bh
        jne .nl
        mov si, s_tail
        call log_str
        lea si, [bx + 26 + 64]
        xor cx, cx
        mov cl, [si]
        inc cx
        cmp cx, 63
        jbe .t
        mov cx, 63
.t:     call log_quoted
        mov si, s_pb
        call log_str
        lea si, [bx + 26 + 128]
        mov cx, 8
.p:     lodsw
        call log_hex4
        mov al, ' '
        call log_chr
        loop .p
.nl:    call log_nl
        ret

log_str:
        push ax
.l:     lodsb
        test al, al
        jz .e
        call log_chr
        jmp .l
.e:     pop ax
        ret

log_nl:
        mov al, 0Dh
        call log_chr
        mov al, 0Ah
        jmp log_chr

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
        jbe log_chr
        add al, 7
log_chr:
        push ax
        push bx
        push cx
        push dx
        mov [log_byte], al
        mov bx, [log_handle]
        mov dx, log_byte
        mov cx, 1
        mov ah, 40h
        int 21h
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; DS:SI, CX bytes: printable as is, others \xx
log_quoted:
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
.e:     ret

log_name:   db 'C:\WO3.LOG', 0
log_handle: dw 0
log_byte:   db 0
s_head:   db '== WO3 ', 0
s_rc:     db ' rc=', 0
s_err:    db ' execerror=', 0
s_lost:   db ' lost=', 0
s_from:   db 'from ', 0
s_out:    db ' -> ', 0
s_name:   db ' [', 0
s_tail:   db ' tail=', 0
s_pb:     db ' pb=', 0

; INT 21h functions recorded (1 = yes)
want21:
%assign i 0
%rep 256
%if i = 00h || i = 0Eh || i = 25h || i = 26h || i = 31h || i = 34h || i = 35h || i = 39h || i = 3Ah || i = 3Bh || i = 3Ch || i = 3Dh || i = 41h || i = 43h || i = 48h || i = 49h || i = 4Ah || i = 4Bh || i = 4Ch || i = 4Dh || i = 4Eh || i = 50h || i = 51h || i = 52h || i = 55h || i = 56h || i = 58h || i = 5Ah || i = 5Bh || i = 5Dh || i = 62h || i = 6Ch
        db 1
%else
        db 0
%endif
%assign i i+1
%endrep

old21:    dd 0
old2f:    dd 0
busy:     db 0
kind:     db 0
n4b:      dw 0
rc:       db 0
execerr:  db 0, 0
save_sp:  dw 0
nrec:     dw 0
lost:     dw 0
recp:     dw buf
currec:   dw 0
scratch:  dw 0
pblock:
pb_env:   dw 0
pb_tail:  dw 80h, 0
pb_fcb1:  dw 5Ch, 0
pb_fcb2:  dw 6Ch, 0
prog:     times 128 db 0

        align 2
        times 512 db 0
stack_top:
buf:                                    ; (not in the file)
buf_end      equ buf + NREC*RECSZ
end_of_image equ buf_end
