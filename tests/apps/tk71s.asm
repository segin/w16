; tk71s.asm - TK71's window procedure entry (tests/apps/tk71.c): notes
; the registers USER calls a window procedure with (AX BX CX DX SI DI ES
; and DS) in tk71_regs, then goes on to the C procedure tk71_proc. A
; program's DGROUP is its stack segment, so SS addresses the record.

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _DATA
global _tk71_regs
_tk71_regs      times 8 dw 0     ; ax bx cx dx si di es ds

segment _TEXT public align=16 class=CODE use16
extern TK71_PROC                 ; LRESULT FAR PASCAL tk71_proc(...)
global TK71_ENTRY
TK71_ENTRY:
        mov [ss:_tk71_regs + 0 wrt DGROUP], ax
        mov [ss:_tk71_regs + 2 wrt DGROUP], bx
        mov [ss:_tk71_regs + 4 wrt DGROUP], cx
        mov [ss:_tk71_regs + 6 wrt DGROUP], dx
        mov [ss:_tk71_regs + 8 wrt DGROUP], si
        mov [ss:_tk71_regs + 10 wrt DGROUP], di
        mov [ss:_tk71_regs + 12 wrt DGROUP], es
        mov [ss:_tk71_regs + 14 wrt DGROUP], ds
        jmp far TK71_PROC
