; entry.asm - OLESVR.DLL's entry point and WEP (ARCHITECTURE.md 16)
;
; KERNEL calls a library's entry point once, when it is loaded, with
; DS = its data segment, DI = its instance handle, CX = the local heap
; size from the NE header (as w16's GDI, gdi/entry.asm).

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

extern LOCALINIT                        ; KERNEL.4
extern __AHINCR                         ; KERNEL.114 (constant)
extern _svr_init                        ; int __cdecl far svr_init(WORD hinst)

segment _DATA
global _svr_ahincr
_svr_ahincr dw __AHINCR                 ; selector step of huge blocks

segment ENTRY_TEXT public align=16 class=CODE use16

global svr_libentry
svr_libentry:
        push ds
        push si
        push di
        push es
        jcxz .noheap
        push ds
        push word 0
        push cx
        call far LOCALINIT
        or ax, ax                       ; no local heap: the load fails
        jz .done
.noheap:
        push di
        call far _svr_init
        add sp, 2
.done:
        pop es
        pop di
        pop si
        pop ds
        retf

; WEP (ordinal 1, resident, as in Microsoft's OLESVR)
global WEP
WEP:
        mov ax, 1
        retf 2
