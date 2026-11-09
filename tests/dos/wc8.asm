; wc8.asm - WC8.COM: take extended memory away through XMS, to find how much
; WIN.COM wants before it picks 386 enhanced mode by itself.
; Usage: WC8 n  - allocates n KB (decimal) with XMS function 09h and never
; frees it (the block outlives the program). Appends to C:\WC.LOG the XMS
; version (00h), free memory (08h: largest, total) before and after, and
; INT 15h AH=88h; and where DOS keeps its data: INT 21h 52h (ES:BX),
; the system file table blocks (LoL+4 chain, with counts), 5D06h (DS:SI,
; CX, DX), INT 2Fh 1203h (DS).
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
        ; n from the tail
        mov si, 81h
        xor dx, dx
.sp:    lodsb
        cmp al, ' '
        je .sp
.dig:   sub al, '0'
        cmp al, 9
        ja .gotn
        xor ah, ah
        push ax
        mov ax, dx
        mov bx, 10
        mul bx
        pop bx
        add ax, bx
        mov dx, ax
        lodsb
        jmp .dig
.gotn:  mov [n], dx
        mov ax, dx
        call log_hex4
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .noxms
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        mov si, s_ver
        call log_str
        xor ah, ah
        call far [xms]
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, bx
        call log_hex4
        call free
        mov ah, 9
        mov dx, [n]
        call far [xms]
        mov si, s_alloc
        call log_str
        call log_hex4
        call free
.noxms: mov si, s_lol                   ; DOS internals, for WIN.COM's
        call log_str                    ; switcher instance data (9.5)
        mov ah, 52h
        int 21h
        mov ax, es
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, bx
        call log_hex4
        ; the system file table chain from LoL+4: block (count) ...
        mov si, s_sft
        call log_str
        les bx, [es:bx+4]
.sft:   mov al, ' '
        call log_chr
        mov ax, es
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, bx
        call log_hex4
        mov al, '('
        call log_chr
        mov ax, [es:bx+4]
        call log_hex4
        mov al, ')'
        call log_chr
        cmp word [es:bx], 0FFFFh
        je .sfte
        les bx, [es:bx]
        jmp .sft
.sfte:  mov si, s_sda
        call log_str
        push ds
        mov ax, 5D06h
        int 21h
        mov ax, ds
        mov bx, si
        pop ds
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, bx
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, cx
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, dx
        call log_hex4
        mov si, s_dds
        call log_str
        push ds
        mov ax, 1203h
        int 2Fh
        mov ax, ds
        pop ds
        call log_hex4
        mov si, s_i15
        call log_str
        mov ah, 88h
        int 15h
        call log_hex4
        call log_nl
        call log_close
        mov ax, 4C00h
        int 21h

free:   mov si, s_free
        call log_str
        mov ah, 8
        call far [xms]
        call log_hex4
        mov al, '/'
        call log_chr
        mov ax, dx
        call log_hex4
        ret

%include "wclib.inc"

s_head:   db '== WC8 kb=', 0
s_ver:    db ' xmsver=', 0
s_free:   db ' free(largest/total)=', 0
s_alloc:  db ' alloc=', 0
s_i15:    db ' int15/88=', 0
s_lol:    db ' lol=', 0
s_sda:    db ' sda(ds:si/cx/dx)=', 0
s_dds:    db ' dosds=', 0
s_sft:    db ' sft:', 0
n:        dw 0
xms:      dd 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
