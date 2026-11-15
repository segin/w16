; entry.asm - GDI.EXE's entry point and small helpers (ARCHITECTURE.md 11)
;
; KERNEL calls a library's entry point once, when it is loaded, with
; DS = its data segment, DI = its instance handle, CX = the local heap
; size from the NE header (Pietrek ch. 3, LibMain; as w16's KERNEL does
; for its own test DLLs, tests/apps/dllstart.asm).

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
extern LOCALALLOC, LOCALREALLOC, LOCALFREE, LOCALSIZE
extern __AHINCR                         ; KERNEL.114 (constant)

segment _DATA
global _gdi_ahincr
_gdi_ahincr dw __AHINCR
extern _gdi_init                        ; int __cdecl far gdi_init(WORD hinst)
extern _gdi_unimpl                      ; void __cdecl far gdi_unimpl(WORD ord)

segment ENTRY_TEXT public align=16 class=CODE use16

global gdi_libentry
gdi_libentry:
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
        push di
        call far _gdi_init
        add sp, 2
        pop es
        pop di
        pop si
        pop ds
        retf

; __GP (resident, ordinal 46): the table of code ranges whose faults the
; module handles itself (Pietrek; KERNEL's parameter validation). Empty:
; w16's GDI validates its parameters instead. UNVERIFIED: the table layout
; (a terminating zero word suffices for an empty one).
global GDI_GP
GDI_GP: dw 0, 0, 0, 0

; WEP (GDI's resident name table has it, ordinal 59)
global WEP
WEP:
        mov ax, 1
        retf 2

; void __cdecl far gdi_append(const char far *path, const char far *buf, WORD len)
; append to a file, opening and closing it each time (the log of w16's
; GDI; DOS handles belong to the task that opens them)
global _gdi_append
_gdi_append:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov ax, 3D01h
        int 21h
        jnc .opened
        mov ah, 3Ch
        xor cx, cx
        int 21h
        jc .done
.opened:
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov cx, [bp + 14]
        lds dx, [bp + 10]
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.done:  pop ds
        pop bp
        retf

; unimplemented exports (gdi/stubs.asm, tools/mkgdiexp.py) come here with
; the ordinal pushed: record it, return 0
global gdi_unimpl_entry
gdi_unimpl_entry:
        push bp
        mov bp, sp
        push ds
        pusha
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 4]
        call far _gdi_unimpl
        add sp, 2
        popa
        pop ds
        pop bp
        retn 2

; lh_alloc, lh_realloc, lh_free, lh_size: KERNEL's LocalAlloc, LocalReAlloc,
; LocalFree and LocalSize on the local heap in another segment (GDI's own
; heaps, heap.c): DS is that segment for the call (the Local* functions
; work on the caller's DS, 3.1 SDK) and GDI's own again afterwards.
; WORD __far __cdecl lh_alloc(WORD sel, WORD flags, WORD size)
global _lh_alloc
_lh_alloc:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]        ; flags
        push word [bp+10]       ; size
        call far LOCALALLOC
        pop ds
        pop bp
        retf

; WORD __far __cdecl lh_realloc(WORD sel, WORD h, WORD size, WORD flags)
global _lh_realloc
_lh_realloc:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]        ; h
        push word [bp+10]       ; size
        push word [bp+12]       ; flags
        call far LOCALREALLOC
        pop ds
        pop bp
        retf

; WORD __far __cdecl lh_free(WORD sel, WORD h)
global _lh_free
_lh_free:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far LOCALFREE
        pop ds
        pop bp
        retf

; WORD __far __cdecl lh_size(WORD sel, WORD h)
global _lh_size
_lh_size:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far LOCALSIZE
        pop ds
        pop bp
        retf
