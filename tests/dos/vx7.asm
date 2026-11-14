; vx7.asm - VX7.COM: port I/O from the System VM to a port a VxD traps
; (ARCHITECTURE.md 12.3, I/O trapping). Black-box observation, with
; VXI.386 (tests/vxd/vxp.asm built with IOPROBE) installed as an I/O
; handler for ports 2F0h and 0E0h; runs in place of SYSTEM\KRNL386.EXE
; (tests/vxobs.sh io, Microsoft's WIN386.EXE or -w for w16's). In V86
; mode and then in protected mode (a 16-bit DPMI client) it makes, in this
; order: IN AL,DX; IN AX,DX; IN EAX,DX; OUT DX,AL; OUT DX,AX; OUT DX,EAX
; (port 2F0h), then IN AL,0E0h; OUT 0E0h,AL; REP INSB (CX=3); REP OUTSW
; (CX=2); INSB; and logs what it read. VXI's COM1 lines give what the
; handler got. Then, in each mode, the virtual IRQ 10 VXI.386 has
; (VPICD_Virtualize_IRQ): VX7 hooks its vector (72h: the IVT in V86 mode,
; DPMI 0205h in protected mode), unmasks it in the virtual PIC, writes A5h
; to port 0E0h (VXI sets the request) and records what its handler sees:
; the slave's IRR and ISR and the master's ISR (OCW3) and IN 0A1h, before
; its EOIs (slave, then master); the second time in V86 mode the handler
; waits about a second with interrupts on before the EOIs (the IRET
; time-out). Logs to C:\VX7.LOG.

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
        mov [dataseg], ds
        ; IRQ 10 in V86 mode: the IVT's 72h
        push es
        xor ax, ax
        mov es, ax
        mov eax, [es:72h*4]
        mov [old72], eax
        mov word [es:72h*4], h72
        mov [es:72h*4+2], cs
        pop es
        call irqtest
        mov byte [slow], 1
        call irqtest
        mov byte [slow], 0
        push es
        xor ax, ax
        mov es, ax
        mov eax, [old72]
        mov [es:72h*4], eax
        pop es
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
        mov [dataseg], ds
        mov ax, 0205h
        mov bl, 72h
        mov cx, cs
        mov dx, h72
        int 31h
        call irqtest
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

; one interrupt: unmask IRQ 10, OUT 0E0h,A5h, wait for the handler, mask
irqtest:
        mov word [hcount], 0
        in al, 0A1h
        mov [imr_before], al
        and al, ~04h
        out 0A1h, al
        sti
        mov al, 0A5h
        out 0E0h, al
        mov ecx, 40000000h
.w:     cmp word [hcount], 0
        jne .got
        dec ecx
        jnz .w
.got:   OBS 'irq_hcount', [hcount]
        OBSB 'irq_imr_before', [imr_before]
        OBSB 'irq_h_slave_irr', [s_irr]
        OBSB 'irq_h_slave_isr', [s_isr]
        OBSB 'irq_h_master_isr', [m_isr]
        OBSB 'irq_h_imr', [h_imr]
        OBSB 'irq_h_slave_isr_after_eoi', [s_isr2]
        OBS 'irq_h_flags', [h_fl]
        in al, 0A1h
        OBSB 'irq_imr_after', al
        or al, 04h
        out 0A1h, al
        ret

; the IRQ 10 handler (vector 72h)
h72:    push ds
        push ax
        push cx
        mov ds, [cs:dataseg]
        inc word [hcount]
        pushf
        pop ax
        mov [h_fl], ax
        mov al, 0Ah                     ; OCW3: read the IRR
        out 0A0h, al
        in al, 0A0h
        mov [s_irr], al
        mov al, 0Bh                     ; OCW3: read the ISR
        out 0A0h, al
        in al, 0A0h
        mov [s_isr], al
        mov al, 0Bh
        out 20h, al
        in al, 20h
        mov [m_isr], al
        in al, 0A1h
        mov [h_imr], al
        cmp byte [slow], 0
        je .eoi
        ; about a second with interrupts on (V86: the BIOS count)
        push es
        xor ax, ax
        mov es, ax
        sti
        mov cx, [es:46Ch]
        add cx, 18
.s:     cmp cx, [es:46Ch]
        jne .s
        cli
        pop es
.eoi:   mov al, 20h
        out 0A0h, al
        mov al, 0Bh
        out 0A0h, al
        in al, 0A0h
        mov [s_isr2], al
        mov al, 0Ah
        out 0A0h, al
        mov al, 20h
        out 20h, al
        pop cx
        pop ax
        pop ds
        iret

dataseg dw 0
slow    db 0

%include "dxlib.inc"

        section .bss
old72   resd 1
hcount  resw 1
h_fl    resw 1
imr_before resb 1
s_irr   resb 1
s_isr   resb 1
m_isr   resb 1
h_imr   resb 1
s_isr2  resb 1
pment   resd 1
d32     resd 1
buf     resb 16
logbuf  resb LOGSIZE
        resb 1024
stack_top:
