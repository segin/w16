; entry.asm - w16 SHELL.DLL: the DLL entry point and small helpers
; (ARCHITECTURE.md 13).
;
; KERNEL calls a DLL's entry point with DI = hInstance, CX = the local
; heap size and DS = its data segment (ARCHITECTURE.md 9.2, TK5); the DLL
; makes its local heap itself (LocalInit) and returns AX = 1 to stay loaded.
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0           ; instance data (Windows' convention)

segment SHELL_TEXT public align=16 class=CODE use16

extern LOCALINIT                ; KERNEL.4
extern __AHINCR                 ; KERNEL.114 (a constant)
extern _shell_init              ; int __cdecl shell_init(WORD hinst) (C, far)

global libentry
libentry:
        push ds
        push si
        push di
        push es
        jcxz .noheap
        push ds                 ; LocalInit(DS, 0, CX)
        push word 0
        push cx
        call far LOCALINIT
.noheap:
        pop es
        pop di
        push di
        push di                 ; hInstance
        push cs                 ; a far call within SHELL_TEXT, with no
        call _shell_init        ; segment fixup (no entry needed)
        add sp, 2
        pop di
        pop si
        pop ds
        retf

; WORD __cdecl get_ahincr(void): the selector increment of huge blocks
global _get_ahincr
_get_ahincr:
        mov ax, __AHINCR
        retf
