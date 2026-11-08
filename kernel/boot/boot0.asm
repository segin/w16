; boot0.asm - startup and low-level helpers for the KRNL286.EXE bootstrap.
;
; The bootstrap is the MZ stub of KRNL286.EXE. It runs in real mode, loads
; the NE KERNEL module that follows it in the same file, switches to 16-bit
; protected mode through DPMI, and transfers control to KERNEL.
;
; Small model: one code segment, DGROUP = _DATA CONST CONST2 _BSS STACK.
; This object must be linked first so that the segment order is fixed here.

        bits 16
        cpu 8086

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
segment STACK  stack  align=16 class=STACK use16
group DGROUP _DATA CONST CONST2 _BSS STACK

%include "intcall.inc"

STACK_BYTES equ 8192

segment _BSS
bss_start:

segment STACK
stack_base:
        resb STACK_BYTES
stack_top:

segment _DATA
global _psp_seg, _psp_sel
_psp_seg        dw 0
_psp_sel        dw 0
dpmi_entry      dd 0
kernel_entry    dd 0

segment _TEXT
extern _boot_main

..start:
        mov ax, DGROUP
        mov ds, ax
        cli
        mov ss, ax
        mov sp, stack_top wrt DGROUP
        sti
        mov [_psp_seg wrt DGROUP], es
        cld
        ; zero _BSS (the stack does not need it)
        push ds
        pop es
        mov di, bss_start wrt DGROUP
        mov cx, stack_base wrt DGROUP
        sub cx, di
        xor al, al
        rep stosb
        ; give back the memory beyond the end of the stack
        mov bx, stack_top wrt DGROUP
        add bx, 15
        mov cl, 4
        shr bx, cl
        mov ax, ds
        add bx, ax
        sub bx, [_psp_seg wrt DGROUP]
        mov es, [_psp_seg wrt DGROUP]
        mov ah, 4Ah
        int 21h
        push ds
        pop es
        call _boot_main
        mov ax, 4CFFh
        int 21h

INTCALL _int21, 21h, near
INTCALL _int2f, 2Fh, near
INTCALL _int31, 31h, near

; int dpmi_switch(u16 entry_off, u16 entry_seg, u16 host_seg)
; Enter 16-bit protected mode. Returns 0 on success (CS/DS/SS are then
; selectors for the same memory; _psp_sel holds the PSP selector).
global _dpmi_switch
_dpmi_switch:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp+4]
        mov [dpmi_entry wrt DGROUP], ax
        mov ax, [bp+6]
        mov [dpmi_entry+2 wrt DGROUP], ax
        mov es, [bp+8]
        xor ax, ax                      ; 16-bit client
        call far [dpmi_entry wrt DGROUP]
        jc .fail
        mov [_psp_sel wrt DGROUP], es
        push ds
        pop es
        xor ax, ax
        jmp .out
.fail:  push ds
        pop es
        mov ax, 1
.out:   pop di
        pop si
        pop bp
        ret

; void call_kernel(u16 cs, u16 ip, u16 bootinfo_off)
; Far-call the KERNEL entry point with ES:BX -> bootinfo (in DGROUP).
global _call_kernel
_call_kernel:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp+6]
        mov [kernel_entry wrt DGROUP], ax
        mov ax, [bp+4]
        mov [kernel_entry+2 wrt DGROUP], ax
        mov bx, [bp+8]
        push ds
        pop es
        call far [kernel_entry wrt DGROUP]
        push ss
        pop ds
        push ds
        pop es
        pop di
        pop si
        pop bp
        ret

global _get_cs, _get_ds
_get_cs:
        mov ax, cs
        ret
_get_ds:
        mov ax, ds
        ret
