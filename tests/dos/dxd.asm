; dxd.asm - DXD.COM: a DOS program run from Standard-mode Windows by DX7
; (tests/apps/dx7.c). Black-box observation (ARCHITECTURE.md section 3):
; what a DOS program finds while Windows is swapped out by the task
; switcher, under Microsoft's DOSX and under w16's. Appends to C:\DXD.LOG:
; the processor mode (MSW), its PSP chain (owner names from the memory
; arena), free DOS memory, XMS (free memory, A20), the answers to INT 2Fh
; 1600h, 160Ah, 1687h, 4680h, 4B02h, and which program owns the
; interrupt vectors that matter (08h, 09h, 10h, 13h, 15h, 16h, 1Ch, 21h,
; 23h, 24h, 28h, 2Fh, 33h, 67h). Then it creates C:\DXD.OK and exits
; with 0.

        bits 16
        cpu 286
        org 100h

%define LOGNAME 'C:\DXD.LOG'
%include "dxlib.inc"

start:  mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        ; append to the log (create it the first time)
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jc .new
        mov bx, ax
        mov ah, 3Eh
        int 21h
        jmp .go
.new:   call logcreate
.go:    NOTE 'DXD start'
        smsw ax
        OBS 'msw', ax
        ; the PSP chain
        mov ax, [psp]
        mov cx, 6
.pc:    push cx
        push ax
        mov si, s_psp
        call puts
        call puthex
        mov al, ' '
        call putc
        pop ax
        push ax
        call ownername
        call crlf
        pop ax
        pop cx
        mov es, ax
        mov bx, [es:16h]
        push ds
        pop es
        cmp bx, ax
        je .pd
        mov ax, bx
        loop .pc
.pd:    mov bx, 0FFFFh
        mov ah, 48h
        int 21h
        OBS 'dos_free_paras', bx
        ; XMS
        mov ax, 4300h
        int 2Fh
        OBSB 'xms', al
        cmp al, 80h
        jne .nox
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        push ds
        pop es
        mov ah, 08h
        xor bx, bx
        call far [xms]
        REGS 'xms_08'
        mov ah, 07h
        call far [xms]
        OBS 'xms_a20', ax
.nox:   ; INT 2Fh
        mov ax, 1600h
        int 2Fh
        OBS 'int2f_1600', ax
        mov ax, 160Ah
        xor bx, bx
        xor cx, cx
        int 2Fh
        REGS 'int2f_160a'
        mov ax, 1687h
        int 2Fh
        REGS 'int2f_1687'
        push ds
        pop es
        mov ax, 4680h
        int 2Fh
        OBS 'int2f_4680', ax
        xor bx, bx
        xor di, di
        mov es, bx
        mov ax, 4B02h
        int 2Fh
        REGS 'int2f_4b02'
        push ds
        pop es
        ; vectors
        mov si, vecs
.vl:    lodsb
        cmp al, 0FFh
        je .vd
        push si
        mov bl, al
        xor bh, bh
        shl bx, 2
        push ax
        mov si, s_vec
        call puts
        pop ax
        call puthex2
        mov al, '='
        call putc
        push es
        xor ax, ax
        mov es, ax
        mov ax, [es:bx]
        mov dx, [es:bx+2]
        pop es
        ; owner of the paragraph seg + off/16
        shr ax, 4
        add ax, dx
        call ownername
        call crlf
        pop si
        jmp .vl
.vd:    NOTE 'DXD end'
        call flush
        mov dx, okname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .x
        mov bx, ax
        mov ah, 3Eh
        int 21h
.x:     mov ax, 4C00h
        int 21h

; ownername: AX = a paragraph; logs the name of the program owning the
; memory block it is in ("dos" below the first block, "?" if none)
ownername:
        push ax
        push bx
        push dx
        push es
        mov dx, ax
        mov ah, 52h
        int 21h
        mov ax, [es:bx-2]
        cmp dx, ax
        jb .dos
.l:     mov es, ax
        mov bx, ax
        add bx, [es:3]
        cmp dx, ax
        jbe .nx
        cmp dx, bx
        ja .nx
        ; found: the owner's name, from the owner's arena header
        mov ax, [es:1]
        or ax, ax
        jz .free
        dec ax
        mov es, ax
        mov bx, 8
.n:     mov al, [es:bx]
        or al, al
        jz .d
        call putc_safe
        inc bx
        cmp bx, 16
        jb .n
        jmp .d
.nx:    cmp byte [es:0], 'Z'
        je .hi
        mov ax, bx
        inc ax
        jmp .l
.hi:    push ds
        pop es
        mov si, s_high
        call puts
        jmp .d
.free:  push ds
        pop es
        mov si, s_free
        call puts
        jmp .d
.dos:   push ds
        pop es
        mov si, s_dos
        call puts
.d:     pop es
        pop dx
        pop bx
        pop ax
        ret

%include "dxlib.inc"

vecs    db 08h, 09h, 10h, 13h, 15h, 16h, 1Ch, 21h, 23h, 24h, 28h, 2Fh, 33h, 67h, 0FFh
okname  db 'C:\DXD.OK', 0
s_psp   db 'OBS psp ', 0
s_vec   db 'OBS ivt', 0
s_high  db 'high', 0
s_free  db 'free', 0
s_dos   db 'dos', 0

        section .bss
psp     resw 1
xms     resd 1
logbuf  resb LOGSIZE
        resb 512
stack_top:
