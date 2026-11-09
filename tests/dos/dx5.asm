; dx5.asm - DX5.COM: one DOS extender behaviour per run, chosen by the
; command tail (WIN /S XXXX: the first hex number in KERNEL's tail).
;
; Black-box observation (ARCHITECTURE.md section 3); runs in the KERNEL
; slot like DX1 (tests/dosxtest.sh -a XXXX DX5). Some of these end the
; extender, so each gets a run of its own; what DOSX prints and WIN's
; errorlevel are in the run's output.
;   0000-EFFF  INT 31h with AX = XXXX, BX=CX=DX=SI=0, ES:DI = a zeroed
;              buffer: logs the registers returned
;   F000       a general protection fault, no handler (mov es, bad)
;   F001       divide by zero, no handler
;   F002       invalid opcode, no handler
;   F003       exit with code 1 from protected mode (errorlevel)
;   F004       exit with code 0FFh from protected mode
;   F005       exit through real-mode INT 21h 4C01h (INT 31h 0300h)
;   F006       INT 31h 0503h shrinking a block
;   F007       INT 31h 0503h growing a block
;   F008       INT 21h AH=4Ah from protected mode growing a block
;   F009       INT 21h AH=4Ah from protected mode shrinking a block
;   F00A       exit with code 1 from real mode, before switching
;   F00B       stack fault, no handler (SS:SP at the segment's end)
;   F00C       INT 31h 0000h with CX=0
;   F00D       a GP fault with a handler (0203h) that skips the instruction:
;              logs the exception frame and the stack it ran on
;   F00E       software INT 0Dh (as an interrupt, not a fault)
;   F00F       a GP fault in a handler-less program after hooking INT 0Dh
;              with 0205h: does the interrupt handler see the fault?

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\DX5.LOG'
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
        call logcreate
        NOTE 'DX5 probe'
        call tailhex
        mov [fn], ax
        OBS 'fn', ax
        call flush
        cmp word [fn], 0F00Ah
        jne .sw
        mov ax, 4C01h
        int 21h
.sw:    mov ax, 1687h
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
        NOTE 'DX5 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

pm:     push ds
        pop es
        mov [datasel], ds
        NOTE 'DX5 protected mode'
        call flush
        mov ax, [fn]
        cmp ax, 0F000h
        jae special
        mov di, buf
        xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        int 31h
        cld
        REGS 'result'
        NOTE 'DX5 end'
        call flush
        mov ax, 4C00h
        int 21h

special:
        sub ax, 0F000h
        cmp ax, (jtend - jt) / 2
        jae .none
        mov bx, ax
        shl bx, 1
        jmp [jt+bx]
.none:  NOTE 'DX5 unknown test'
        jmp done

jt:     dw t_gp, t_div, t_ud, t_exit1, t_exitff, t_rmexit, t_503s, t_503g
        dw t_4ag, t_4as, done, t_ss, t_alloc0, t_gph, t_int0d, t_gpint
jtend:

t_gp:   mov ax, 0FFF0h
        mov es, ax
        NOTE 'after fault'
        jmp done
t_div:  xor cx, cx
        mov ax, 1
        xor dx, dx
        div cx
        NOTE 'after divide'
        jmp done
t_ud:   db 0Fh, 0FFh
        NOTE 'after invalid opcode'
        jmp done
t_exit1:
        mov ax, 4C01h
        int 21h
t_exitff:
        mov ax, 4CFFh
        int 21h
t_rmexit:
        mov word [rm_ax], 4C01h
        mov di, rmregs
        mov bx, 21h
        xor cx, cx
        mov ax, 0300h
        int 31h
        REGS 'after rm exit'
        jmp done
t_503s: xor bx, bx
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        REGS 'alloc'
        call flush
        xor bx, bx
        mov cx, 800h
        mov ax, 0503h
        int 31h
        REGS 'shrink'
        jmp done
t_503g: xor bx, bx
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        REGS 'alloc'
        call flush
        xor bx, bx
        mov cx, 2000h
        mov ax, 0503h
        int 31h
        REGS 'grow'
        jmp done
t_4ag:  mov bx, 1
        mov ah, 48h
        int 21h
        REGS 'alloc'
        call flush
        mov es, ax
        mov bx, 2
        mov ah, 4Ah
        int 21h
        REGS 'grow'
        jmp done
t_4as:  mov bx, 100h
        mov ah, 48h
        int 21h
        REGS 'alloc'
        call flush
        mov es, ax
        mov bx, 80h
        mov ah, 4Ah
        int 21h
        REGS 'shrink'
        jmp done
t_ss:   mov ax, 0
        mov cx, 1
        int 31h
        mov bx, ax
        mov [t1], ax
        mov ax, ds
        push bx
        mov bx, ax
        mov ax, 0006h
        int 31h
        pop bx
        mov ax, 0007h
        int 31h
        mov cx, 0
        mov dx, 0FFh
        mov ax, 0008h
        int 31h
        NOTE 'stack fault next'
        call flush
        mov ax, [t1]
        cli
        mov ss, ax
        mov sp, 2
        push ax
        push ax
        NOTE 'after stack fault'
        jmp done
t_alloc0:
        xor cx, cx
        mov ax, 0000h
        int 31h
        REGS 'alloc0'
        jmp done
t_gph:  mov bl, 0Dh
        mov cx, cs
        mov dx, gph
        mov ax, 0203h
        int 31h
        REGS 'set'
        mov [t1], sp
        mov word [hit], 0
        mov ax, 1234h
        mov bx, 0FFF0h
fault_at:
        mov es, bx              ; faults; the handler skips it
        push ds
        pop es
        REGS 'after'
        OBS 'sp_before', [t1]
        OBS 'hit', [hit]
        OBS 'h_ss', [h_ss]
        OBS 'h_sp', [h_sp]
        OBS 'h_fl', [h_fl]
        OBS 'h_ds', [h_ds]
        OBS 'h_ax', [h_ax]
        OBS 'f_retip', [fr]
        OBS 'f_retcs', [fr+2]
        OBS 'f_err', [fr+4]
        OBS 'f_ip', [fr+6]
        OBS 'f_cs', [fr+8]
        OBS 'f_fl', [fr+10]
        OBS 'f_sp', [fr+12]
        OBS 'f_ss', [fr+14]
        OBS 'f_after16', [fr+16]
        mov ax, fault_at
        OBS 'fault_at', ax
        jmp done
t_int0d:
        mov bl, 0Dh
        mov ax, 0204h
        int 31h
        REGS 'pmvec_0d'
        call flush
        int 0Dh
        REGS 'after int 0d'
        jmp done
t_gpint:
        mov bl, 0Dh
        mov cx, cs
        mov dx, gpint
        mov ax, 0205h
        int 31h
        mov word [hit], 0
        call flush
        mov bx, 0FFF0h
        mov es, bx
        push ds
        pop es
        REGS 'after'
        OBS 'hit', [hitc]
        jmp done

gpint:  push ds
        mov ds, [cs:datasel]
        inc word [hitc]
        pop ds
        iret

gph:    push bp
        mov bp, sp
        push ds
        push ax
        push si
        mov ax, ds
        mov ds, [cs:datasel]
        mov [h_ds], ax
        mov ax, [bp-4]
        mov [h_ax], ax
        mov ax, ss
        mov [h_ss], ax
        lea ax, [bp+2]
        mov [h_sp], ax
        pushf
        pop word [h_fl]
        inc word [hit]
        xor si, si
.cp:    mov ax, [ss:bp+2+si]
        mov [fr+si], ax
        add si, 2
        cmp si, 18
        jb .cp
        add word [ss:bp+2+6], 2 ; skip the faulting "mov es, bx"
        pop si
        pop ax
        pop ds
        pop bp
        retf

done:   NOTE 'DX5 end'
        call flush
        mov ax, 4C00h
        int 21h

%include "dxlib.inc"

datasel dw 0
hit     dw 0
hitc    dw 0
h_ss    dw 0
h_sp    dw 0
h_fl    dw 0
h_ds    dw 0
h_ax    dw 0
fr      times 9 dw 0

        section .bss
psp     resw 1
fn      resw 1
t1      resw 1
pment   resd 1
rmregs:
rm_di   resd 1
rm_si   resd 1
rm_bp   resd 1
        resd 1
rm_bx   resd 1
rm_dx   resd 1
rm_cx   resd 1
rm_ax   resd 1
rm_fl   resw 1
rm_es   resw 1
rm_ds   resw 1
rm_fs   resw 1
rm_gs   resw 1
rm_ip   resw 1
rm_cs   resw 1
rm_sp   resw 1
rm_ss   resw 1
buf     resb 256
logbuf  resb LOGSIZE
        resb 1024
stack_top:
