; vx7.asm - VX7.COM: port I/O from the System VM to a port a VxD traps
; (ARCHITECTURE.md 12.3, I/O trapping). Black-box observation, with
; VXI.386 (tests/vxd/vxp.asm built with IOPROBE) installed as an I/O
; handler for ports 2F0h and 0E0h; runs in place of SYSTEM\KRNL386.EXE
; (tests/vxobs.sh io, Microsoft's WIN386.EXE or -w for w16's). In V86
; mode and then in protected mode (a 16-bit DPMI client) it makes, in this
; order: IN AL,DX; IN AX,DX; IN EAX,DX; OUT DX,AL; OUT DX,AX; OUT DX,EAX
; (port 2F0h), then IN AL,0E0h; OUT 0E0h,AL; REP INSB (CX=3); REP OUTSW
; (CX=2); INSB; and logs what it read. VXI's COM1 lines give what the
; handler got. Logs to C:\VX7.LOG.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\VX7.LOG'
%include "dxlib.inc"

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
        NOTE 'VX7 probe: V86'
        call ioseq
        call flush
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
        jnc .pm
        NOTE 'VX7 switch failed'
        call flush
        mov ax, 4C01h
        int 21h
.pm:    push ds
        pop es
        NOTE 'VX7: protected mode'
        call ioseq
        NOTE 'VX7 done'
        call flush
        mov ax, 4C00h
        int 21h

; the sequence (DS = ES = our data)
ioseq:  mov dx, 2F0h
        xor eax, eax
        in al, dx
        OBSD 'in_al', 0, ax
        xor eax, eax
        in ax, dx
        OBSD 'in_ax', 0, ax
        xor eax, eax
        in eax, dx
        mov [d32], eax
        OBSD 'in_eax', [d32+2], [d32]
        mov eax, 0A1B2C3D4h
        out dx, al
        out dx, ax
        out dx, eax
        xor eax, eax
        in al, 0E0h
        OBSD 'in_al_imm', 0, ax
        mov al, 5Ah
        out 0E0h, al
        mov dword [buf], 0
        mov dword [buf+4], 0
        mov di, buf
        mov cx, 3
        rep insb
        OBS 'insb_cx', cx
        OBS 'insb_di', di
        OBSD 'insb_buf0', [buf+2], [buf]
        OBSD 'insb_buf4', [buf+6], [buf+4]
        mov word [buf], 1111h
        mov word [buf+2], 2222h
        mov si, buf
        mov cx, 2
        rep outsw
        OBS 'outsw_cx', cx
        OBS 'outsw_si', si
        mov di, buf
        insb
        OBS 'insb1_di', di
        OBSB 'insb1_buf', [buf]
        ret

%include "dxlib.inc"

        section .bss
pment   resd 1
d32     resd 1
buf     resb 16
logbuf  resb LOGSIZE
        resb 1024
stack_top:
