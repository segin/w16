; usentry.asm - USTRACE.DLL's entry point (tests/user/ustrace.c): calls
; hub_init with DS = the DLL's data segment and returns its result.
        bits 16
        cpu 286
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS
segment _NULL
        times 16 db 0
segment _TEXT public align=16 class=CODE use16
extern _hub_init
global usentry
usentry:
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        call far _hub_init
        pop di
        pop si
        pop ds
        retf
