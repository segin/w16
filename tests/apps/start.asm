; start.asm - shared start-up code for the KERNEL test programs (TK*).
;
; A minimal Win16 application start-up (no C runtime): record the registers
; KERNEL hands to a new task, call InitTask, record its results, run the
; tests in tmain(), and exit through INT 21h AH=4Ch with the failure count.

        bits 16
        cpu 286

; DGROUP starts with 16 bytes reserved for the Windows instance data.
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern INITTASK                 ; KERNEL.91
extern DOS3CALL                 ; KERNEL.102
extern __AHINCR                 ; KERNEL.114 (constant)
extern __0040H                  ; KERNEL.193 (constant selector)
extern _tmain                   ; int __cdecl __far tmain(void)

segment _NULL
        times 16 db 0

segment _DATA
global _entry_regs, _init_regs
; {ax, bx, cx, dx, si, di, bp, sp, ds, es, ss}
_entry_regs     times 11 dw 0
; {ax, bx, cx, dx, si, di, es}
_init_regs      times 7 dw 0

segment _TEXT
global start
start:
        mov [_entry_regs + 0 wrt DGROUP], ax
        mov [_entry_regs + 2 wrt DGROUP], bx
        mov [_entry_regs + 4 wrt DGROUP], cx
        mov [_entry_regs + 6 wrt DGROUP], dx
        mov [_entry_regs + 8 wrt DGROUP], si
        mov [_entry_regs + 10 wrt DGROUP], di
        mov [_entry_regs + 12 wrt DGROUP], bp
        mov [_entry_regs + 14 wrt DGROUP], sp
        mov [_entry_regs + 16 wrt DGROUP], ds
        mov [_entry_regs + 18 wrt DGROUP], es
        mov [_entry_regs + 20 wrt DGROUP], ss
        call far INITTASK
        mov [_init_regs + 0 wrt DGROUP], ax
        mov [_init_regs + 2 wrt DGROUP], bx
        mov [_init_regs + 4 wrt DGROUP], cx
        mov [_init_regs + 6 wrt DGROUP], dx
        mov [_init_regs + 8 wrt DGROUP], si
        mov [_init_regs + 10 wrt DGROUP], di
        mov [_init_regs + 12 wrt DGROUP], es
        call far _tmain
        mov ah, 4Ch
        int 21h

; u16 get_ahincr(void), u16 get_0040h(void): values of KERNEL constants
global _get_ahincr, _get_0040h
_get_ahincr:
        mov ax, __AHINCR
        retf
_get_0040h:
        mov ax, __0040H
        retf

; u16 dos3call_getver(void): DOS version (AL major, AH minor) via DOS3Call
global _dos3call_getver
_dos3call_getver:
        mov ah, 30h
        call far DOS3CALL
        retf
