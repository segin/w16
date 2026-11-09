; dx8.asm - DX8.COM: BIOS and driver interrupts with pointers, from
; protected mode. Black-box observation (ARCHITECTURE.md section 3); runs
; in the KERNEL slot under WIN /S like DX4 (tests/dosxtest.sh DX8), with
; buffers in extended memory, and logs what comes back:
;   INT 10h 1130h (ES:BP back), 1Bh (64 bytes to ES:DI), 1C00h (size),
;     1017h (DAC registers to ES:DX), 1009h (palette to ES:DX)
;   INT 13h 08h, 02h (a sector to ES:BX)
;   INT 15h C0h (ES:BX back), C1h (ES back), 88h
;   INT 25h (a sector to DS:BX)
;   INT 33h 00h, 15h (state size), 16h (state to ES:DX), 0Ch (a
;     protected-mode handler at ES:DX) and 14h (swap it back: what real
;     mode got), 09h (a cursor at ES:DX)
;   INT 4Bh 8102h (VDS version)
; The log is C:\DX8.LOG.

        bits 16
        cpu 286
        org 100h

%define LOGNAME 'C:\DX8.LOG'
%include "dxlib.inc"

%macro I 2              ; I vector, 'name': the interrupt, then the registers
        int %1
        mov ds, [cs:datasel]
        cld
        REGS %2
        call flush
%endmacro
%macro DUMP 3
        push si
        mov si, %2
        mov cx, %3
        call dump_i
        db %1, 0
        pop si
%endmacro

start:  mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        call logcreate
        NOTE 'DX8 probe'
        mov ax, 1687h
        int 2Fh
        mov [pment], di
        mov [pment+2], es
        push ds
        pop es
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        mov es, ax
.nohd:  xor ax, ax
        call far [pment]
        jnc pm
        NOTE 'DX8 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

pm:     push ds
        pop es
        mov [datasel], ds
        NOTE 'DX8 protected mode'
        mov bx, 1
        xor cx, cx
        mov ax, 0501h
        int 31h
        jc near done
        mov [xlin], cx
        mov [xlin+2], bx
        mov cx, 1
        xor ax, ax
        int 31h
        mov [xsel], ax
        mov bx, ax
        mov cx, [xlin+2]
        mov dx, [xlin]
        mov ax, 0007h
        int 31h
        mov bx, [xsel]
        xor cx, cx
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
        mov es, [xsel]
        xor di, di
        mov cx, 8000h
        mov ax, 0A55Ah
        rep stosw
        push ds
        pop es
        ; ---- INT 10h
        mov ax, 1130h
        mov bh, 6
        I 10h, 'v1130'
        mov es, [xsel]
        xor di, di
        xor bx, bx
        mov ax, 1B00h
        I 10h, 'v1b'
        push ds
        pop es
        DUMP 'v1b_buf', 0, 20h
        mov ax, 1C00h
        mov cx, 7
        I 10h, 'v1c00'
        mov es, [xsel]
        mov dx, 100h
        xor bx, bx
        mov cx, 4
        mov ax, 1017h
        I 10h, 'v1017'
        push ds
        pop es
        DUMP 'v1017_buf', 100h, 0Ch
        mov es, [xsel]
        mov dx, 200h
        mov ax, 1009h
        I 10h, 'v1009'
        push ds
        pop es
        DUMP 'v1009_buf', 200h, 11h
        ; ---- INT 13h
        mov ah, 08h
        mov dl, 80h
        I 13h, 'd08_80'
        push ds
        pop es
        mov es, [xsel]
        mov bx, 300h
        mov ax, 0201h
        mov cx, 1
        mov dx, 0080h
        I 13h, 'd02_80'
        push ds
        pop es
        DUMP 'd02_buf', 300h, 10h
        ; ---- INT 15h
        mov ah, 0C0h
        I 15h, 'b_c0'
        push ds
        pop es
        mov ah, 0C1h
        I 15h, 'b_c1'
        push ds
        pop es
        mov ah, 88h
        I 15h, 'b_88'
        ; ---- INT 25h: drive C, sector 0
        push ds
        mov ds, [xsel]
        mov bx, 600h
        mov al, 2
        mov cx, 1
        xor dx, dx
        int 25h
        mov ds, [cs:datasel]
        pop word [t1]           ; the flags INT 25h leaves
        pop ds
        cld
        REGS 'a25'
        OBS 'a25_flags', [t1]
        DUMP 'a25_buf', 600h, 10h
        ; ---- INT 33h
        xor ax, ax
        I 33h, 'm00'
        mov ax, 15h
        I 33h, 'm15'
        mov es, [xsel]
        mov dx, 800h
        mov ax, 16h
        I 33h, 'm16'
        push ds
        pop es
        DUMP 'm16_buf', 800h, 10h
        push cs
        pop es
        mov dx, mhandler
        mov cx, 1Fh
        mov ax, 0Ch
        I 33h, 'm0c'
        push ds
        pop es
        ; swap back: what the driver had
        xor cx, cx
        xor dx, dx
        push ds
        pop es
        mov ax, 14h
        int 33h
        mov ds, [cs:datasel]
        mov [t1], es
        push ds
        pop es
        REGS 'm14'
        OBS 'm14_es', [t1]
        mov es, [xsel]
        mov dx, 900h
        xor bx, bx
        xor cx, cx
        mov ax, 09h
        I 33h, 'm09'
        push ds
        pop es
        ; ---- INT 4Bh VDS
        mov ax, 8102h
        xor dx, dx
        I 4Bh, 'vds8102'
done:   NOTE 'DX8 end'
        call flush
        mov ax, 4C00h
        int 21h

mhandler:
        retf

dump_i: push bp
        mov bp, sp
        push ax
        push si
        push cx
        mov si, s_obs
        call puts
        mov si, [bp+2]
        call cputs
        mov [bp+2], si
        mov al, '='
        call putc
        pop cx
        pop si
.h:     push es
        mov es, [xsel]
        mov al, [es:si]
        pop es
        call puthex2
        inc si
        loop .h
        call crlf
        pop ax
        pop bp
        ret

%include "dxlib.inc"

datasel dw 0

        section .bss
pment   resd 1
xlin    resd 1
xsel    resw 1
t1      resw 1
logbuf  resb LOGSIZE
        resb 1024
stack_top:
