; wc4.asm - WC4.COM: a stand-in for SYSTEM\WSWAP.EXE that asks WIN.COM what
; it hands the Standard-mode switcher, the way the real WSWAP asks (observed
; with WC3): INT 2Fh AX=4A05h SI=0005h (CX=00FFh, DX=ES=our PSP) returns
; DX:AX -> a block in WIN.COM; INT 2Fh AX=4B05h (ES:BX=0) returns ES:BX ->
; WIN.COM's switcher startup-info structure (MS-DOS 5 task switcher API).
; Appends to C:\WC.LOG: the registers returned, BLK bytes at DX:AX, and
; the 4B05h structure and the instance-data list it points to.
; Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

BLK     equ 512

start:
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

        mov ax, 4A05h
        mov si, 5
        mov cx, 0FFh
        mov dx, cs
        mov es, dx
        xor bx, bx
        xor di, di
        int 2Fh
        push ax
        push dx
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_si], si
        mov [r_di], di
        mov [r_es], es
        mov si, s_4a05
        call log_str
        mov si, r_ax
        mov cx, 7
        call words
        call log_nl
        pop ds
        pop si
        mov cx, BLK
        call hexdump
        push cs
        pop ds

        mov ax, 4B05h
        xor bx, bx
        mov es, bx
        mov cx, cs
        mov dx, dummy_cb
        int 2Fh
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_es], es
        mov si, s_4b05
        call log_str
        mov ax, [r_ax]
        call log_hex4
        mov al, ' '
        call log_chr
        mov ax, [r_es]
        call log_hex4
        mov al, ':'
        call log_chr
        mov ax, [r_bx]
        call log_hex4
        call log_nl
        mov ax, [r_es]
        or ax, [r_bx]
        jz .done
        ; SWSTARTUPINFO: version, next, virtdev, refdata, instance list
        mov ds, [r_es]
        mov si, [cs:r_bx]
        mov cx, 18
        call hexdump
        mov ax, [si+14]                 ; instance data list
        mov dx, [si+16]
        push cs
        pop ds
        mov si, s_inst
        call log_str
        mov ax, dx
        call log_hex4
        mov al, ':'
        call log_chr
        mov ds, [r_es]
        mov si, [cs:r_bx]
        mov ax, [si+14]
        push cs
        pop ds
        call log_hex4
        call log_nl
        ; the list: DWORD address, WORD size, ... until a zero address
        mov ds, [r_es]
        mov si, [cs:r_bx]
        les si, [si+14]
        push es
        pop ds
.il:    mov ax, [si]
        or ax, [si+2]
        jz .done0
        mov cx, 6
        call hexdump
        add si, 6
        jmp .il
.done0: push cs
        pop ds
.done:  call log_close
        mov ax, 4C00h
        int 21h

dummy_cb:
        xor ax, ax
        retf

; CX words from DS:SI as " xxxx"
words:
.l:     mov al, ' '
        call log_chr
        lodsw
        call log_hex4
        loop .l
        ret

; CX bytes at DS:SI, 32 per line "  oooo: hh..."; SI preserved
hexdump:
        push si
        push cx
        xor bx, bx
.line:  jcxz .end
        push ax
        mov al, ' '
        call log_chr
        call log_chr
        mov ax, bx
        call log_hex4
        mov al, ':'
        call log_chr
        pop ax
        mov dx, 32
.b:     mov al, ' '
        test dx, 7
        jnz .nosp
        call log_chr
.nosp:  mov al, [si]
        call log_hex2
        inc si
        inc bx
        dec cx
        jz .eol
        dec dx
        jnz .b
.eol:   call log_nl
        jmp .line
.end:   pop cx
        pop si
        ret

%include "wclib.inc"

s_head:   db '== WC4 ', 0
s_4a05:   db '4A05/5 ax bx cx dx si di es:', 0
s_4b05:   db '4B05 ax es:bx ', 0
s_inst:   db 'instance list at ', 0
r_ax:     dw 0
r_bx:     dw 0
r_cx:     dw 0
r_dx:     dw 0
r_si:     dw 0
r_di:     dw 0
r_es:     dw 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
