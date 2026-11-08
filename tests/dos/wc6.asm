; wc6.asm - WC6.COM: a stand-in for SYSTEM\WSWAP.EXE (or WIN386.EXE) that
; makes the INT 2Fh calls listed in C:\WC6.TXT, as WIN.COM's switcher
; interface (AX=4A05h, function in SI) or any other: one call per line,
; "AX SI BX CX DX DI" in hex, 4 digits each,
; separated by one blank (DX may be "W" for WIN.COM's segment as returned
; by SI=5, DI "B" for the block offset SI=5 returned). Before the list it
; calls AX=4A05h SI=5 itself. Appends each call's registers out and the flags to
; C:\WC.LOG with the block's bytes 160h-18Fh and 32 bytes at ES:BX, ES:DI
; DX:AX (32) and DX:CX (64) as returned, then exits 0. Its own command tail
; goes on the first line.
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
        mov si, 81h                     ; our command tail
        xor cx, cx
        mov cl, [80h]
        call log_quoted
        call log_nl
        mov ax, 4A05h
        mov si, 5
        mov cx, 0FFh
        mov dx, cs
        mov es, dx
        int 2Fh
        push cs
        pop ds
        mov [winseg], dx
        mov [blk], ax
        ; read the list
        mov dx, list_name
        mov ax, 3D00h
        int 21h
        jc .done
        mov bx, ax
        mov dx, list
        mov cx, 4000
        mov ah, 3Fh
        int 21h
        mov si, list
        add ax, si
        mov [list_end], ax
        mov ah, 3Eh
        int 21h
.next:  cmp si, [list_end]
        jae .done
        call hex4
        jc .skipline
        mov [c_ax], ax
        call hex4
        mov [c_si], ax
        call hex4
        mov [c_bx], ax
        call hex4
        mov [c_cx], ax
        cmp byte [si+1], 'W'
        jne .dxn
        mov ax, [winseg]
        add si, 2
        jmp .dxs
.dxn:   call hex4
.dxs:   mov [c_dx], ax
        cmp byte [si+1], 'B'
        jne .din
        mov ax, [blk]
        add si, 2
        jmp .dis
.din:   call hex4
.dis:   mov [c_di], ax
        push si
        mov si, s_in
        call log_str
        mov si, c_ax
        mov cx, 6
        call words
        mov ax, [c_ax]
        mov si, [c_si]
        mov bx, [c_bx]
        mov cx, [c_cx]
        mov dx, [c_dx]
        mov di, [c_di]
        push cs
        pop es
        int 2Fh
        pushf
        push cs
        pop ds
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_si], si
        mov [r_di], di
        mov [r_es], es
        pop ax
        mov [r_fl], ax
        mov si, s_out
        call log_str
        mov si, r_ax
        mov cx, 8
        call words
        mov si, s_tail
        call log_str
        push ds
        mov ds, [winseg]
        mov si, [cs:blk]
        add si, 160h
        mov cx, 48
.bl:    lodsb
        call log_hex2
        loop .bl
        pop ds
        call log_nl
        mov si, s_esbx
        call log_str
        mov ax, [r_es]
        mov bx, [r_bx]
        call bytes32
        mov si, s_esdi
        call log_str
        mov ax, [r_es]
        mov bx, [r_di]
        call bytes32
        mov si, s_dxax
        call log_str
        mov ax, [r_dx]
        mov bx, [r_ax]
        call bytes32
        mov si, s_dxcx
        call log_str
        mov ax, [r_dx]
        mov bx, [r_cx]
        call bytes32
        call bytes32x
        call log_nl
        pop si
.skipline:
        cmp si, [list_end]
        jae .done
        lodsb
        cmp al, 0Ah
        jne .skipline
        jmp .next
.done:  call log_close
        mov ax, 4C00h
        int 21h

; a blank then 4 hex digits at DS:SI (the first field has no blank) -> AX
hex4:
        cmp byte [si], ' '
        jne .h
        inc si
.h:     xor ax, ax
        mov cx, 4
.l:     mov dl, [si]
        sub dl, '0'
        cmp dl, 9
        jbe .d
        sub dl, 'A' - '0' - 10
        cmp dl, 15
        ja .bad
.d:     push cx
        mov cl, 4
        shl ax, cl
        pop cx
        or al, dl
        inc si
        loop .l
        clc
        ret
.bad:   stc
        ret

; 32 bytes at AX:BX in hex
bytes32:
        push ds
        mov ds, ax
        mov si, bx
        mov cx, 32
.l:     lodsb
        call log_hex2
        loop .l
        pop ds
        ret

; 32 more bytes (after bytes32, SI points past them; DS restored)
bytes32x:
        mov ax, [r_dx]
        mov bx, [r_cx]
        add bx, 32
        jmp bytes32

words:
.l:     mov al, ' '
        call log_chr
        lodsw
        call log_hex4
        loop .l
        ret

%include "wclib.inc"

s_head:   db '== WC6', 0
s_in:     db 'ax si bx cx dx di:', 0
s_out:    db ' -> ax bx cx dx si di es fl:', 0
s_tail:   db ' block+160h: ', 0
s_esbx:   db '  es:bx ', 0
s_esdi:   db ' es:di ', 0
s_dxax:   db ' dx:ax ', 0
s_dxcx:   db ' dx:cx ', 0
list_name: db 'C:\WC6.TXT', 0
winseg:   dw 0
blk:      dw 0
list_end: dw 0
c_ax:     dw 0
c_si:     dw 0
c_bx:     dw 0
c_cx:     dw 0
c_dx:     dw 0
c_di:     dw 0
r_ax:     dw 0
r_bx:     dw 0
r_cx:     dw 0
r_dx:     dw 0
r_si:     dw 0
r_di:     dw 0
r_es:     dw 0
r_fl:     dw 0
list:     times 4000 db 0

        align 2
        times 256 db 0
stack_top:
end_of_image:
