; vx6.asm - VX6.COM: the interrupt flag a program in the System VM sees
; under 386 Enhanced mode (ARCHITECTURE.md 12.3, the virtual IF).
; Black-box observation; runs in place of SYSTEM\KRNL386.EXE (tests/
; vxobs.sh vif, with Microsoft's WIN386.EXE or -w for w16's). It records
;   - in V86 mode: the flags PUSHF shows, after CLI and after STI, and
;     whether timer ticks (the BIOS count at 0:46Ch) still come after CLI;
;   - in protected mode (a 16-bit DPMI client): the same flags, and, with a
;     protected-mode INT 08h handler counting ticks, whether ticks come
;     after CLI; after PUSHF, CLI, POPF (the IF=1 image popped); after CLI
;     and an IRET of a frame with IF=1; after STI and a POPF of an image
;     with IF=0; after CLI and an INT 21h; around INT 31h 0900h-0902h;
;     IN AL,21h (the PIC mask) in both modes.
; A "delta" is the ticks that came in a busy loop three times as long as
; two ticks took with interrupts on: 0 means interrupts were off. Logs to
; C:\VX6.LOG.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\VX6.LOG'
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
        NOTE 'VX6 probe'

        ; ---- V86 mode ----
        pushf
        pop ax
        OBS 'v86_fl', ax
        cli
        pushf
        pop ax
        sti
        OBS 'v86_cli_fl', ax
        sti
        pushf
        pop ax
        OBS 'v86_sti_fl', ax
        in al, 21h
        OBSB 'v86_in21', al
        ; calibrate: loop iterations while two BIOS ticks pass
        push es
        xor ax, ax
        mov es, ax
        sti
        call v86_cal
        mov [n3], ecx
        OBSD 'v86_n', [n3+2], [n3]
        ; CLI, then a loop of 3N
        mov bx, [es:46Ch]
        cli
        mov ecx, [n3]
.l1:    mov ax, [es:46Ch]
        cmp ax, 0FFFFh
        dec ecx
        jnz .l1
        mov ax, [es:46Ch]
        sti
        sub ax, bx
        pop es
        OBS 'v86_cli_delta', ax
        call flush

        ; ---- the switch to protected mode ----
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
        NOTE 'VX6 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

.pm:    mov [datasel], ds
        push ds
        pop es
        pushf
        pop ax
        mov [r_pmfl], ax
        cli
        pushf
        pop ax
        sti
        mov [r_clifl], ax
        sti
        pushf
        pop ax
        mov [r_stifl], ax
        in al, 21h
        mov [r_in21], al
        ; the tick counter
        mov ax, 0204h
        mov bl, 08h
        int 31h
        mov [old08], dx
        mov [old08+2], cx
        mov ax, 0205h
        mov bl, 08h
        mov cx, cs
        mov dx, handler
        int 31h
        sti
        call pm_cal
        mov [n3], ecx

        ; A: CLI
        cli
        call spin
        sti
        mov [r_a], ax
        ; B: PUSHF (IF=1), CLI, POPF
        sti
        pushf
        cli
        popf
        call spin
        sti
        mov [r_b], ax
        ; B2: flags PUSHF shows after that POPF... (taken in C2 below)
        ; C: CLI, IRET of a frame with IF=1
        cli
        pushf
        pop ax
        or ax, 0200h
        push ax
        push cs
        push word .c1
        iret
.c1:    pushf
        pop ax
        mov [r_cfl], ax
        call spin
        sti
        mov [r_c], ax
        ; D: STI, POPF of an image with IF=0
        sti
        pushf
        pop ax
        and ax, ~0200h
        push ax
        popf
        pushf
        pop ax
        mov [r_dfl], ax
        call spin
        sti
        mov [r_d], ax
        ; E: CLI, INT 21h AH=30h
        cli
        mov ah, 30h
        int 21h
        pushf
        pop ax
        mov [r_efl], ax
        call spin
        sti
        mov [r_e], ax
        ; F: INT 31h 0900h, 0902h, spin, 0901h, 0902h, spin
        sti
        mov ax, 0900h
        int 31h
        mov [r_f0900], ax
        pushf
        pop ax
        mov [r_ffl], ax
        mov ax, 0902h
        int 31h
        mov [r_f0902a], ax
        call spin
        mov [r_f], ax
        mov ax, 0901h
        int 31h
        mov [r_f0901], ax
        mov ax, 0902h
        int 31h
        mov [r_f0902b], ax
        call spin
        mov [r_f2], ax
        ; G: CLI, INT 31h 0902h
        cli
        mov ax, 0902h
        int 31h
        mov [r_g0902], ax
        sti
        ; H: STI with interrupts already on: the reference delta
        sti
        call spin
        mov [r_h], ax

        ; I/J: a software interrupt to a handler of ours (INT 60h, 0205h)
        mov ax, 0205h
        mov bl, 60h
        mov cx, cs
        mov dx, h60
        int 31h
        sti
        int 60h
        pushf
        pop ax
        mov [r_jfl], ax
        mov ax, [r_h60fl]
        mov [r_jhfl], ax
        mov ax, [r_h60fr]
        mov [r_jhfr], ax
        cli
        int 60h
        pushf
        pop ax
        sti
        mov [r_ifl], ax
        mov ax, [r_h60fl]
        mov [r_ihfl], ax
        mov ax, [r_h60fr]
        mov [r_ihfr], ax

        ; unhook, log
        mov ax, 0205h
        mov bl, 08h
        mov cx, [old08+2]
        mov dx, [old08]
        int 31h
        OBS 'pm_cs', cs
        OBS 'pm_fl', [r_pmfl]
        OBS 'pm_cli_fl', [r_clifl]
        OBS 'pm_sti_fl', [r_stifl]
        OBSB 'pm_in21', [r_in21]
        OBSD 'pm_n', [n3+2], [n3]
        OBS 'A_cli_delta', [r_a]
        OBS 'B_pushf_cli_popf_delta', [r_b]
        OBS 'C_cli_iret_if1_fl', [r_cfl]
        OBS 'C_cli_iret_if1_delta', [r_c]
        OBS 'D_sti_popf_if0_fl', [r_dfl]
        OBS 'D_sti_popf_if0_delta', [r_d]
        OBS 'E_cli_int21_fl', [r_efl]
        OBS 'E_cli_int21_delta', [r_e]
        OBS 'F_0900_ax', [r_f0900]
        OBS 'F_0900_fl', [r_ffl]
        OBS 'F_0902_ax', [r_f0902a]
        OBS 'F_0900_delta', [r_f]
        OBS 'F_0901_ax', [r_f0901]
        OBS 'F_0902b_ax', [r_f0902b]
        OBS 'F_0901_delta', [r_f2]
        OBS 'G_cli_0902_ax', [r_g0902]
        OBS 'H_sti_delta', [r_h]
        OBS 'handler_fl', [r_hfl]
        OBS 'handler_frame_fl', [r_hframe]
        OBS 'J_sti_int60_after_fl', [r_jfl]
        OBS 'J_sti_int60_handler_fl', [r_jhfl]
        OBS 'J_sti_int60_frame_fl', [r_jhfr]
        OBS 'I_cli_int60_after_fl', [r_ifl]
        OBS 'I_cli_int60_handler_fl', [r_ihfl]
        OBS 'I_cli_int60_frame_fl', [r_ihfr]
        NOTE 'VX6 done'
        call flush
        mov ax, 4C00h
        int 21h

; V86: ECX = 3 x the loop iterations while two BIOS ticks pass (ES=0)
v86_cal:
        mov bx, [es:46Ch]
.w:     cmp bx, [es:46Ch]
        je .w
        mov bx, [es:46Ch]
        add bx, 2
        xor ecx, ecx
.l:     inc ecx
        mov ax, [es:46Ch]
        cmp ax, bx
        jne .l
        lea ecx, [ecx+ecx*2]
        ret

; PM: the same with the handler's count
pm_cal: mov bx, [ticks]
.w:     cmp bx, [ticks]
        je .w
        mov bx, [ticks]
        add bx, 2
        xor ecx, ecx
.l:     inc ecx
        mov ax, [ticks]
        cmp ax, bx
        jne .l
        lea ecx, [ecx+ecx*2]
        ret

; PM: AX = the ticks counted during a loop of [n3] (flags kept)
spin:   push bx
        push ecx
        mov bx, [ticks]
        mov ecx, [n3]
.l:     mov ax, [ticks]
        cmp ax, 0FFFFh
        dec ecx
        jnz .l
        mov ax, [ticks]
        sub ax, bx
        pop ecx
        pop bx
        ret

; the protected-mode INT 08h handler: count, note the flags once, chain
handler:
        push ds
        push bp
        mov bp, sp
        mov ds, [cs:datasel]
        inc word [ticks]
        cmp byte [hnoted], 0
        jne .c
        mov byte [hnoted], 1
        push ax
        pushf
        pop ax
        mov [r_hfl], ax
        mov ax, [bp+8]                  ; the frame: IP, CS, FLAGS
        mov [r_hframe], ax
        pop ax
.c:     pop bp
        pop ds
        jmp far [cs:old08v]

; INT 60h: note the flags in the handler and in the frame, IRET
h60:    push ds
        push bp
        mov bp, sp
        mov ds, [cs:datasel]
        push ax
        pushf
        pop ax
        mov [r_h60fl], ax
        mov ax, [bp+8]
        mov [r_h60fr], ax
        pop ax
        pop bp
        pop ds
        iret

%include "dxlib.inc"

old08v:
old08           dw 0, 0
datasel         dw 0
ticks           dw 0
hnoted          db 0

        section .bss
pment   resd 1
n3      resd 1
r_pmfl  resw 1
r_clifl resw 1
r_stifl resw 1
r_in21  resb 1
        resb 1
r_a     resw 1
r_b     resw 1
r_cfl   resw 1
r_c     resw 1
r_dfl   resw 1
r_d     resw 1
r_efl   resw 1
r_e     resw 1
r_f0900 resw 1
r_ffl   resw 1
r_f0902a resw 1
r_f     resw 1
r_f0901 resw 1
r_f0902b resw 1
r_f2    resw 1
r_g0902 resw 1
r_h     resw 1
r_hfl   resw 1
r_hframe resw 1
r_jfl   resw 1
r_jhfl  resw 1
r_jhfr  resw 1
r_ifl   resw 1
r_ihfl  resw 1
r_ihfr  resw 1
r_h60fl resw 1
r_h60fr resw 1
logbuf  resb LOGSIZE
        resb 1024
stack_top:
