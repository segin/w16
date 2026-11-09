; wc5.asm - WC5.COM: a stand-in for SYSTEM\WSWAP.EXE that makes WIN.COM's
; INT 2Fh AX=4A05h calls the real WSWAP makes when Windows ends (observed
; with WC3), in the same order and with the same inputs, and records what
; comes back and what changed: SI=5; SI=2 (CX=10h, DX=WIN.COM's segment);
; SI=5; SI=4 (BX=32h, DX=WIN.COM's segment, DI=the offset SI=5 returned).
; Before and after, the DOS memory chain (MCB segment, owner, size) and
; the INT 2Fh vector. Then exits with the code in C:\WC5.TXT's first
; character minus '0' if a digit, else 0; a second character g skips
; SI=2, h skips SI=4. Appends to C:\WC.LOG.
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

start:
        mov sp, stack_top
        mov bx, (end_of_image - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h
        mov dx, opt_name                ; options from C:\WC5.TXT
        mov ax, 3D00h
        int 21h
        jc .noopt
        mov bx, ax
        mov dx, opts
        mov cx, 2
        mov ah, 3Fh
        int 21h
        mov ah, 3Eh
        int 21h
.noopt:
        call log_open
        mov si, s_head
        call log_str
        call log_nl
        call mcbs
        call vec2f

        mov si, 5
        call call4a05
        mov [winseg], dx
        mov [blk], ax
        call blkdump
        cmp byte [opts+1], 'g'             ; tail "Ng": skip SI=2
        je .no2
        mov si, 2
        mov cx, 10h
        mov dx, [winseg]
        call call4a05
.no2:
        call blkdump
        call vec2f
        call mcbs
        mov si, 5
        call call4a05
        cmp byte [opts+1], 'h'             ; tail "Nh": skip SI=4
        je .no4
        mov si, 4
        mov bx, 32h
        mov dx, [winseg]
        mov di, [blk]
        call call4a05
.no4:   call blkdump
        call vec2f
        call mcbs
        call log_close
        xor al, al
        mov bl, [opts]
        sub bl, '0'
        cmp bl, 9
        ja .x
        mov al, bl
.x:     mov ah, 4Ch
        int 21h

; INT 2Fh AX=4A05h with SI (and BX CX DX DI as set); log in and out
call4a05:
        push si
        mov [i_si], si
        mov si, s_call
        call log_str
        mov ax, [i_si]
        call log_hex4
        mov si, [i_si]
        mov ax, 4A05h
        push cs
        pop es
        int 2Fh
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_si], si
        mov [r_di], di
        mov [r_es], es
        pushf
        pop ax
        mov [r_fl], ax
        push cs
        pop ds
        mov si, s_out
        call log_str
        mov si, r_ax
        mov cx, 8
.l:     mov al, ' '
        call log_chr
        lodsw
        call log_hex4
        loop .l
        call log_nl
        mov ax, [r_ax]
        mov bx, [r_bx]
        mov cx, [r_cx]
        mov dx, [r_dx]
        mov di, [r_di]
        pop si
        ret

vec2f:
        mov si, s_vec
        call log_str
        mov ax, 352Fh
        int 21h
        mov ax, es
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, bx
        call log_hex4
        call log_nl
        ret

; the MCB chain: "mcb SSSS owner OOOO size ZZZZ"
mcbs:
        mov ah, 52h
        int 21h
        mov ax, [es:bx-2]
        mov si, s_mcb
        call log_str
.l:     mov es, ax
        mov al, ' '
        call log_chr
        mov ax, es
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, [es:1]
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, [es:3]
        call log_hex4
        cmp byte [es:0], 'Z'
        je .e
        mov ax, es
        add ax, [es:3]
        inc ax
        jmp .l
.e:     call log_nl
        ret

; the 1D0h-byte block at winseg:blk, 32 bytes a line
blkdump:
        push ds
        mov si, s_blk
        call log_str
        call log_nl
        mov ds, [winseg]
        mov si, [cs:blk]
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
        cmp bx, 1D0h
        jb .line
        pop ds
        ret

%include "wclib.inc"

s_head:   db '== WC5', 0
s_call:   db '4A05 si=', 0
s_out:    db ' -> ax bx cx dx si di es fl:', 0
s_vec:    db 'vec2F=', 0
s_blk:    db 'block:', 0
s_mcb:    db 'mcb seg/owner/size:', 0
opt_name: db 'C:\WC5.TXT', 0
opts:     db '0a'
winseg:   dw 0
blk:      dw 0
i_si:     dw 0
r_ax:     dw 0
r_bx:     dw 0
r_cx:     dw 0
r_dx:     dw 0
r_si:     dw 0
r_di:     dw 0
r_es:     dw 0
r_fl:     dw 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
