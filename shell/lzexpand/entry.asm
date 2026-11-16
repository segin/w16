; entry.asm - LZEXPAND.DLL's entry point, WEP and ___EXPORTEDSTUB
; (ARCHITECTURE.md, LZEXPAND)
;
; KERNEL calls a library's entry point once, when it is loaded, with
; DS = its data segment, DI = its instance handle, CX = the local heap
; size from the NE header (as w16's DDEML, shell/ddeml/entry.asm).

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

segment ENTRY_TEXT public align=16 class=CODE use16

global lz_libentry
lz_libentry:
        push ds
        push si
        push di
        push es
        jcxz .noheap
        push ds
        push word 0
        push cx
        call far LOCALINIT
.noheap:
        mov ax, 1
        pop es
        pop di
        pop si
        pop ds
        retf

; WEP (ordinal 11, resident, as in Microsoft's LZEXPAND)
global WEP
WEP:
        mov ax, 1
        retf 2

; ___EXPORTEDSTUB (ordinal 12, resident, as in Microsoft's LZEXPAND):
; exported by 3.1's library, imported by nothing in the 3.1 or WfW 3.11
; installs; what it does there is not known (UNVERIFIED). Returns 0.
global ___EXPORTEDSTUB
___EXPORTEDSTUB:
        xor ax, ax
        retf
