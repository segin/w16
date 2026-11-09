; entry.asm - w16 COMMDLG.DLL: the library's entry point.
;
; KERNEL calls a DLL's entry point once, when it loads it, with DS = the
; library's data segment, DI = its instance handle and CX = the local heap
; size from the NE header (the Windows SDK's convention, on Windows 2.x
; and 3.x alike). We set up the local heap and call cd_libmain(hinst);
; its result (nonzero: loaded) goes back in AX.

        bits 16
        cpu 8086

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

; DGROUP starts with 16 bytes the instance data uses
segment _NULL
        times 16 db 0

segment COMMDLG_TEXT public align=16 class=CODE use16

extern LOCALINIT                ; KERNEL.4
extern _cd_libmain              ; int __cdecl __far cd_libmain(HINSTANCE)

global libentry
libentry:
        push di                 ; the instance handle
        jcxz .noheap
        push ds                 ; LocalInit(DS, 0, CX)
        xor ax, ax
        push ax
        push cx
        call far LOCALINIT
        or ax, ax
        jz .fail
.noheap:
        pop di
        push di
        push di
        call far _cd_libmain
        add sp, 2
        pop di
        retf
.fail:
        pop di
        xor ax, ax
        retf
