; libentry.asm - entry code of w16's Control Panel applet DLLs (*.CPL).
;
; KERNEL calls a DLL's NE entry point once, when it loads the library, with
; DI = instance handle, DS = the DLL's data segment, CX = the local heap
; size from the NE header and ES:SI = the command line (Windows SDK, "DLL
; initialization"). The local heap is set up with LocalInit, then the C
; routine cpl_libmain(hInstance) runs; AX = its result (nonzero: loaded).

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern LOCALINIT                ; KERNEL.4  BOOL LocalInit(WORD, WORD, WORD)
extern _cpl_libmain             ; int __cdecl __far cpl_libmain(HINSTANCE)

segment _NULL
        times 16 db 0

segment _TEXT
global libentry
libentry:
        push di
        jcxz .noheap
        push ds
        xor ax, ax
        push ax
        push cx
        call far LOCALINIT
        or ax, ax
        jz .fail
.noheap:
        pop di
        push di
        call far _cpl_libmain
        add sp, 2
        retf
.fail:
        pop di
        xor ax, ax
        retf
