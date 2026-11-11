; vx4.asm - VX4.COM: what a protected-mode hardware interrupt handler gets
; under 386 Enhanced mode (ARCHITECTURE.md 12.3). Black-box observation;
; runs in place of SYSTEM\KRNL386.EXE (tests/vxobs.sh vx4, with Microsoft's
; WIN386.EXE or -w for w16's). It switches to protected mode through DPMI
; as a 16-bit client, hooks the protected-mode timer interrupt (INT 08h,
; DPMI 0205h) and records, for the first ticks that arrive
;   - while V86 code runs for it (a real-mode loop called with DPMI 0301h),
;   - while it runs in protected mode itself,
; the handler's SS, SP, DS, ES, flags and the 16 words at SS:SP (the frame
; it was given), then chains to the previous handler. Logs to C:\VX4.LOG.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\VX4.LOG'
%include "dxlib.inc"

NREC    equ 4                   ; records of each kind
RECW    equ 22                  ; words per record

start:  mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov [rseg], ax
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        call logcreate
        NOTE 'VX4 probe'
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
        NOTE 'VX4 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

; ---------------- protected mode ----------------
.pm:    mov [datasel], ds
        push ds
        pop es
        OBS 'pm_cs', cs
        OBS 'pm_ds', ds
        OBS 'pm_ss', ss
        mov ax, sp
        OBS 'pm_sp', ax
        mov ax, 0204h
        mov bl, 08h
        int 31h
        mov [old08], dx
        mov [old08+2], cx
        OBS 'old08_sel', cx
        mov ax, 0205h
        mov bl, 08h
        mov cx, cs
        mov dx, handler
        int 31h
        call flush
        ; ticks while V86 code runs for us: a real-mode loop (0301h)
        mov byte [kind], 1
        mov di, rmregs
        mov cx, 25
        xor ax, ax
.z:     mov [di], ax
        add di, 2
        loop .z
        mov ax, [rseg]
        mov [rm_cs], ax
        mov [rm_ds], ax
        mov word [rm_ip], rmloop
        mov word [rm_flags], 0202h
        push ds
        pop es
        mov di, rmregs
        mov ax, 0301h
        xor bx, bx
        xor cx, cx
        int 31h
        ; ticks in protected mode
        mov byte [kind], 2
        sti
        mov cx, 0
.w:     mov al, [nrec2]
        cmp al, NREC
        jae .wd
        push cx
        mov cx, 1000
.ww:    loop .ww
        pop cx
        loop .w
.wd:    mov byte [kind], 0
        ; unhook
        mov ax, 0205h
        mov bl, 08h
        mov cx, [old08+2]
        mov dx, [old08]
        int 31h
        ; the records
        mov si, recs
        mov bx, 0
.r:     cmp bx, NREC * 2
        jae .rd
        push bx
        push si
        cmp word [si], 0
        je .rn
        push si
        mov si, s_rec
        call puts
        pop si
        mov cx, RECW
.rw:    lodsw
        call puthex
        mov al, ' '
        call putc
        loop .rw
        call crlf
.rn:    pop si
        add si, RECW * 2
        pop bx
        inc bx
        jmp .r
.rd:    NOTE 'VX4 done'
        call flush
        mov ax, 4C00h
        int 21h

; the protected-mode INT 08h handler
handler:
        push ds
        push bp
        mov bp, sp
        mov ds, [cs:datasel]
        push ax
        push bx
        push si
        push di
        push dx
        mov al, [kind]
        cmp al, 1
        jne .k2
        mov bl, [nrec1]
        cmp bl, NREC
        jae .out
        inc byte [nrec1]
        jmp .take
.k2:    cmp al, 2
        jne .out
        mov bl, [nrec2]
        cmp bl, NREC
        jae .out
        inc byte [nrec2]
        add bl, NREC
.take:  xor bh, bh
        mov ax, RECW * 2
        mul bx
        mov di, recs
        add di, ax
        xor ah, ah
        mov al, [kind]
        mov [di], ax                    ; 0: kind
        mov [di+2], ss                  ; 1: SS
        lea ax, [bp+4]
        mov [di+4], ax                  ; 2: SP at entry
        mov ax, [bp+2]
        mov [di+6], ax                  ; 3: DS at entry
        mov [di+8], es                  ; 4: ES at entry
        pushf
        pop ax
        mov [di+10], ax                 ; 5: flags in the handler
        lea si, [bp+4]
        add di, 12
        mov bx, 16
.cp:    mov ax, [ss:si]
        mov [di], ax
        add si, 2
        add di, 2
        dec bx
        jnz .cp
.out:   pop dx
        pop di
        pop si
        pop bx
        pop ax
        pop bp
        pop ds
        jmp far [cs:old08v]

; real mode, called with 0301h: wait for six timer ticks
rmloop: push ds
        push ax
        push bx
        xor ax, ax
        mov ds, ax
        sti
        mov bx, [46Ch]
        add bx, 6
.l:     mov ax, [46Ch]
        cmp ax, bx
        jne .l
        pop bx
        pop ax
        pop ds
        retf

%include "dxlib.inc"

s_rec           db 'OBS rec kind ss sp ds es fl stack: ', 0
old08v:
old08           dw 0, 0
datasel         dw 0
kind            db 0
nrec1           db 0
nrec2           db 0
recs            times NREC * 2 * RECW dw 0

        section .bss
psp     resw 1
rseg    resw 1
pment   resd 1
rmregs:
rm_edi  resd 1
rm_esi  resd 1
rm_ebp  resd 1
        resd 1
rm_ebx  resd 1
rm_edx  resd 1
rm_ecx  resd 1
rm_eax  resd 1
rm_flags resw 1
rm_es   resw 1
rm_ds   resw 1
rm_fs   resw 1
rm_gs   resw 1
rm_ip   resw 1
rm_cs   resw 1
rm_sp   resw 1
rm_ss   resw 1
logbuf  resb LOGSIZE
        resb 1024
stack_top:
