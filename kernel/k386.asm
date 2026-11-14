; k386.asm - KRNL386 only (built with -DKRNL386; empty for KRNL286): the
; register-level glue for kernel/k386.c.
;   int  __cdecl int2f_pm(regs16 FAR *r)      INT 2Fh in protected mode
;   int  __cdecl vxd_call(u32 entry, regs16 FAR *r)
;                     far call to a VxD's protected-mode API entry with
;                     the registers of *r (DS and ES included), which get
;                     what it returns; returns the carry flag
;   reboot_proc       KERNEL's Ctrl+Alt+Del procedure, given to the Reboot
;                     device with its function 0201h (k386.c)

        bits 16
        cpu 386

%include "intcall.inc"

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

%ifdef KRNL386

segment BOOT_TEXT public align=16 class=CODE use16

INTCALL _int2f_pm, 2Fh, far, pm

; regs16 layout: ax 0, bx 2, cx 4, dx 6, si 8, di 10, ds 12, es 14, flags 16
global _vxd_call
_vxd_call:
        push bp
        mov bp, sp
        push si                 ; [bp-2]
        push di                 ; [bp-4]
        push ds                 ; [bp-6]
        push es                 ; [bp-8]
        les bx, [bp+10]         ; regs16
        push es                 ; [bp-10]
        push bx                 ; [bp-12]
        mov ax, [es:bx+0]
        mov cx, [es:bx+4]
        mov dx, [es:bx+6]
        mov si, [es:bx+8]
        mov di, [es:bx+10]
        push word [es:bx+12]
        push word [es:bx+14]
        mov bx, [es:bx+2]
        pop es
        pop ds
        call far [bp+6]
        pushf
        push ds
        push es
        push bx
        lds bx, [bp-12]
        mov [bx+0], ax
        pop word [bx+2]
        mov [bx+4], cx
        mov [bx+6], dx
        mov [bx+8], si
        mov [bx+10], di
        pop word [bx+14]
        pop word [bx+12]
        pop ax
        mov [bx+16], ax
        and ax, 1
        lea sp, [bp-8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; u32 __cdecl k386_lsl(u16 sel): GetSelectorLimit under KRNL386 (observed,
; tk17.c: LSL's 32-bit limit, 0 when LSL fails)
global _k386_lsl
_k386_lsl:
        push bp
        mov bp, sp
        movzx eax, word [bp+6]
        lsl eax, eax
        jz .ok
        xor eax, eax
.ok:    mov edx, eax
        shr edx, 16
        pop bp
        retf

; KERNEL's Ctrl+Alt+Del procedure (Reboot 0201h ES:DI) and the byte
; given with it (DS:SI). Windows 3.1's KRNL386 gives the Reboot device a
; procedure in its own code segment (TK57: 0202h returns ES = a KERNEL
; code selector); Pietrek ch. 3: the local reboot of a hung application.
; Observed by the WIN386 work (docs/notes/win386-handover.md, VX8): on
; Ctrl+Alt+Del Microsoft's Reboot device zeroes the byte, KERNEL sets it to
; 1 again whenever it runs, and the device makes the System VM take the
; procedure as an interrupt (Build_Int_Stack_Frame); for a hung task the
; procedure calls Reboot 0203h (the "not responding" screen). w16's sets
; the byte and returns (IRET): the screen for no hung application.
; UNVERIFIED: telling a hung task, 0203h and ending the task.
segment _DATA
global _k386_sane
_k386_sane      db 1

segment BOOT_TEXT
global reboot_proc
reboot_proc:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov byte [_k386_sane wrt DGROUP], 1
        pop ax
        pop ds
        iret

%endif
