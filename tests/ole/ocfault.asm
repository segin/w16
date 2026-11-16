; ocfault.asm - OCSPY's fault hook (TOOLHELP InterruptRegister, a
; documented Windows 3.1 interface): logs the exception number and the
; faulting CS:IP, then passes the fault on (to the next handler, in the end
; the system's). TOOLHELP calls the hook with, from SP: its return IP and
; CS, AX, the exception number, a handle, then the faulting IP, CS, flags
; (TOOLHELP documentation, Windows 3.1 SDK).

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment FAULT_TEXT public align=16 class=CODE use16

extern _spy_fault       ; void __cdecl far spy_fault(WORD no, WORD cs, WORD ip, WORD ax)

global SPYFAULTHOOK
SPYFAULTHOOK:
        push bp
        mov bp, sp
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 6]              ; AX
        push word [bp + 12]             ; IP
        push word [bp + 14]             ; CS
        push word [bp + 8]              ; exception number
        call far _spy_fault
        add sp, 8
        pop es
        pop ds
        popa
        pop bp
        retf
