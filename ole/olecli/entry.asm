; entry.asm - OLECLI.DLL's entry point, WEP, and the entries Windows 3.1's
; OLECLI exports by ordinal only (ARCHITECTURE.md 16.7).
;
; KERNEL calls a library's entry point once, when it is loaded, with
; DS = its data segment, DI = its instance handle, CX = the local heap
; size from the NE header (as for w16's GDI, gdi/entry.asm).

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
extern _olecli_init                     ; int __cdecl far olecli_init(WORD hinst)

segment ENTRY_TEXT public align=16 class=CODE use16

global olecli_libentry
olecli_libentry:
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
        call far _olecli_init
        add sp, 2
.done:
        pop es
        pop di
        pop si
        pop ds
        retf

; WEP (resident, ordinal 1)
global WEP
WEP:
        mov ax, 1
        retf 2

; Ordinals 955-1035 have no names in Windows 3.1's OLECLI and nothing
; imports them (docs/exportdb). They are exported here (the code entries
; return 0 and pop nothing, the data entries are a zero word); what the
; originals do is not known (UNVERIFIED).
global OLECLI_NONAME
OLECLI_NONAME:
        xor ax, ax
        xor dx, dx
        retf

segment _DATA
global OLECLI_NONAME_DATA
OLECLI_NONAME_DATA dw 0
