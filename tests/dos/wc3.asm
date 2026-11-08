; wc3.asm - WC3.COM: an INT 2Fh spy to put between WIN.COM and the program
; it starts. Copy it over SYSTEM\WSWAP.EXE (or WIN386.EXE, DOSX.EXE) and the
; real program to the same name with the last letter replaced by "_"
; (WSWAP.EX_). WC3 hooks INT 2Fh - after WIN.COM did, so it sees every
; INT 2Fh call before WIN.COM's handler - runs the real program with its
; own command tail, FCBs and environment, and records each call: the
; registers going in, the caller's CS:IP, the registers and flags coming
; back, and the bytes at ES:DI (16), ES:BX (32), DS:SI (16), DX:AX (64)
; coming back. The first
; MAXPER calls of each AX value are recorded (1680h idle calls would flood
; it), all are counted. Afterwards it appends the record to C:\WC.LOG and
; exits with the program's exit code. WIN.COM's block for the switcher
; (INT 2Fh AX=4A05h SI=5) is dumped as first returned and at the end. Calls made while a recorded call is
; still in progress (nested) are passed on unrecorded.
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

MAXPER  equ 6
%ifndef DXAXOFF
%define DXAXOFF 0                       ; dump DX:AX+DXAXOFF
%endif
NKEYS   equ 96
BLKSZ   equ 1E0h                     ; WIN.COM's switcher block, as dumped
RECSZ   equ 2*9 + 4 + 2*9 + 2 + 128    ; in regs, caller, out regs, flags, bytes

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

        ; parameter block: our environment, tail, FCBs
        mov ax, [2Ch]
        mov [pb_env], ax
        mov [pb_tail+2], cs
        mov [pb_fcb1+2], cs
        mov [pb_fcb2+2], cs

        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f+2], es
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
        mov byte [execerr], 0
        jnc .ok
        mov [execerr], al
        mov byte [execerr+1], 1
.ok:
        ; unhook (only if nobody hooked after us)
        mov ax, 352Fh
        int 21h
        cmp bx, int2f
        jne .keep
        mov ax, es
        mov bx, cs
        cmp ax, bx
        jne .keep
        push ds
        lds dx, [old2f]
        mov ax, 252Fh
        int 21h
        pop ds
.keep:
        mov ah, 4Dh
        int 21h
        mov [rc], al
        call dump
        mov al, [rc]
        mov ah, 4Ch
        int 21h

; ---- the INT 2Fh hook ----
int2f:
        cmp byte [cs:busy], 0
        je .rec
        jmp far [cs:old2f]
.rec:   mov byte [cs:busy], 1
        mov [cs:in_ax], ax
        mov [cs:in_bx], bx
        mov [cs:in_cx], cx
        mov [cs:in_dx], dx
        mov [cs:in_si], si
        mov [cs:in_di], di
        mov [cs:in_ds], ds
        mov [cs:in_es], es
        mov [cs:in_bp], bp
        push bp
        mov bp, sp
        mov ax, [bp+2]
        mov [cs:in_ip], ax
        mov ax, [bp+4]
        mov [cs:in_cs], ax
        push word [bp+6]                ; the caller's flags (IP, CS, flags)
        mov ax, [cs:in_ax]
        popf                            ; chain as an interrupt with them
        pop bp
        pushf
        call far [cs:old2f]
        pushf
        mov [cs:out_ax], ax
        mov [cs:out_bx], bx
        mov [cs:out_cx], cx
        mov [cs:out_dx], dx
        mov [cs:out_si], si
        mov [cs:out_di], di
        mov [cs:out_ds], ds
        mov [cs:out_es], es
        mov [cs:out_bp], bp
        pop ax
        mov [cs:out_fl], ax
        push ax                         ; flags to return with
        call record
        call blkfirst
        mov ax, [cs:out_ax]
        mov bx, [cs:out_bx]
        mov si, [cs:out_si]
        mov di, [cs:out_di]
        mov cx, [cs:out_cx]
        mov dx, [cs:out_dx]
        mov bp, [cs:out_bp]
        mov ds, [cs:out_ds]
        mov es, [cs:out_es]
        mov byte [cs:busy], 0
        popf
        retf 2

; record the call in cs:cur (count per AX, keep the first MAXPER)
record:
        push cs
        pop ds
        push cs
        pop es
        ; find or add AX in the key table
        mov ax, [in_ax]
        mov si, keys
        mov cx, [nkeys]
        xor bx, bx
.k:     jcxz .new
        cmp [si], ax
        je .found
        add si, 4
        dec cx
        jmp .k
.new:   cmp word [nkeys], NKEYS
        jae .r
        mov [si], ax
        mov word [si+2], 0
        inc word [nkeys]
.found: inc word [si+2]
        cmp word [si+2], MAXPER
        ja .r
        ; room?
        mov di, [bufp]
        mov ax, di
        add ax, RECSZ
        cmp ax, buf_end
        ja .r
        mov si, in_ax
        mov cx, 9 + 2 + 9 + 1
        rep movsw
        ; 16 bytes at ES:DI, ES:BX, DS:SI as returned
        push ds
        mov ds, [out_es]
        mov si, [cs:out_di]
        mov cx, 8
        rep movsw
        mov si, [cs:out_bx]
        mov cx, 16
        rep movsw
        mov ds, [cs:out_ds]
        mov si, [cs:out_si]
        mov cx, 8
        rep movsw
        mov ds, [cs:out_dx]
        mov si, [cs:out_ax]
        add si, DXAXOFF
        mov cx, 32
        rep movsw
        pop ds
        mov [bufp], di
        inc word [nrec]
.r:     ret

; the first INT 2Fh AX=4A05h SI=5 result: WIN.COM's block for the
; switcher (DX:AX). Keep a copy of its first BLKSZ bytes as they were then.
blkfirst:
        cmp word [cs:in_ax], 4A05h
        jne .r
        cmp word [cs:in_si], 5
        jne .r
        cmp word [cs:blkseg], 0
        jne .r
        push cs
        pop es
        mov ax, [cs:out_ax]
        mov [cs:blkoff], ax
        mov ax, [cs:out_dx]
        mov [cs:blkseg], ax
        mov ds, ax
        mov si, [cs:blkoff]
        mov di, blkcopy
        mov cx, BLKSZ
        rep movsb
.r:     ret

; BLKSZ bytes at DS:SI, 32 a line
blkhex:
        xor bx, bx
.line:  mov al, ' '
        call log_chr
        mov ax, bx
        call log_hex4
        mov al, ':'
        call log_chr
        mov cx, 32
.b:     test cx, 7
        jnz .n
        mov al, ' '
        call log_chr
.n:     lodsb
        call log_hex2
        loop .b
        call log_nl
        add bx, 32
        cmp bx, BLKSZ
        jb .line
        ret

; ---- write the record to C:\WC.LOG ----
dump:
        call log_open
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
.noerr: call log_nl
        mov bx, buf
        mov cx, [nrec]
.rl:    test cx, cx
        jnz .rec1
        jmp .counts
.rec1:
        push cx
        mov si, s_in
        call log_str
        mov cx, 9
        call regs                       ; ax bx cx dx si di ds es bp
        mov si, s_from
        call log_str
        mov ax, [bx+2]
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, [bx]
        call log_hex4
        add bx, 4
        call log_nl
        mov si, s_out
        call log_str
        mov cx, 9
        call regs
        mov si, s_fl
        call log_str
        mov ax, [bx]
        call log_hex4
        add bx, 2
        call log_nl
        mov si, s_esdi
        call log_str
        call bytes16
        mov si, s_esbx
        call log_str
        call bytes16
        call bytes16
        mov si, s_dssi
        call log_str
        call bytes16
        call log_nl
        mov si, s_dxax
        call log_str
        call bytes16
        call bytes16
        call bytes16
        call bytes16
        call log_nl
        pop cx
        dec cx
        jmp .rl
.counts:
        mov si, s_counts
        call log_str
        mov si, keys
        mov cx, [nkeys]
.cl:    jcxz .end
        mov al, ' '
        call log_chr
        mov ax, [si]
        call log_hex4
        mov al, 'x'
        call log_chr
        mov ax, [si+2]
        call log_hex4
        add si, 4
        dec cx
        jmp .cl
.end:   call log_nl
        cmp word [blkseg], 0
        je .noblk
        mov si, s_blk0
        call log_str
        call log_nl
        mov si, blkcopy
        call blkhex
        mov si, s_blk1
        call log_str
        call log_nl
        push ds
        mov si, [blkoff]
        mov ds, [blkseg]
        call blkhex
        pop ds
.noblk: call log_close
        ret

; CX words at BX as " xxxx", BX advanced
regs:
.l:     mov al, ' '
        call log_chr
        mov ax, [bx]
        call log_hex4
        add bx, 2
        loop .l
        ret

bytes16:
        mov cx, 16
.l:     mov al, [bx]
        call log_hex2
        inc bx
        loop .l
        ret

%include "wclib.inc"

s_head:   db '== WC3 ', 0
s_rc:     db ' rc=', 0
s_err:    db ' execerror=', 0
s_in:     db 'in  ax bx cx dx si di ds es bp:', 0
s_from:   db ' from ', 0
s_out:    db 'out ax bx cx dx si di ds es bp:', 0
s_fl:     db ' fl ', 0
s_esdi:   db '  es:di ', 0
s_esbx:   db ' es:bx ', 0
s_dssi:   db ' ds:si ', 0
s_dxax:   db '  dx:ax ', 0
s_counts: db 'counts', 0
s_blk0:   db 'block at first 4A05/5:', 0
s_blk1:   db 'block at the end:', 0
blkseg:   dw 0
blkoff:   dw 0

old2f:    dd 0
busy:     db 0
rc:       db 0
execerr:  db 0, 0
save_sp:  dw 0
nkeys:    dw 0
nrec:     dw 0
bufp:     dw buf
pblock:
pb_env:   dw 0
pb_tail:  dw 80h, 0
pb_fcb1:  dw 5Ch, 0
pb_fcb2:  dw 6Ch, 0
; the current call, in this order (copied to the buffer as is)
in_ax:    dw 0
in_bx:    dw 0
in_cx:    dw 0
in_dx:    dw 0
in_si:    dw 0
in_di:    dw 0
in_ds:    dw 0
in_es:    dw 0
in_bp:    dw 0
in_ip:    dw 0
in_cs:    dw 0
out_ax:   dw 0
out_bx:   dw 0
out_cx:   dw 0
out_dx:   dw 0
out_si:   dw 0
out_di:   dw 0
out_ds:   dw 0
out_es:   dw 0
out_bp:   dw 0
out_fl:   dw 0
prog:     times 128 db 0
keys:     times NKEYS*2 dw 0
blkcopy:  times BLKSZ db 0

        align 2
        times 512 db 0
stack_top:
buf:                                    ; (not in the file)
buf_end      equ buf + 300*RECSZ
end_of_image equ buf_end
