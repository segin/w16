; atm.asm - CreateDC's exported entry, as Adobe Type Manager expects it
; (ARCHITECTURE.md 11.13).
;
; ATM (its ATMSYS.DRV, from its LoadModule hook, once GDI has loaded)
; takes CreateDC's address with GetProcAddress(GDI, 53) and looks in the
; first 40h bytes there for a standard far frame (inc bp, push bp,
; mov bp,sp, push ds); it copies the code up to the end of those 5 bytes
; to a segment of its own and later puts a far jump to its CreateDC hook
; over them; the hook runs its copy and so enters the procedure again
; after them. Before the frame are three far calls (LoadLibrary,
; GetProcAddress, FreeLibrary), never executed, whose operands it replaces
; with its own procedures: GDI binds device drivers through them, as
; through pointers (driver.c). The entry jumps over the calls to the
; procedure that has the frame. Windows 3.1's CreateDC is laid out so
; (Pietrek, Windows Internals, ch. 5; Schulman, Undocumented Windows,
; ch. 1); what ATM reads and writes here was observed at run time
; (where its code ran and its registers, the extent of its reads through
; data aliases, ARCHITECTURE.md 11.13), never by reading its code or
; Microsoft's GDI.

        bits 16
        cpu 286

extern LOADLIBRARY                      ; KERNEL.95
extern GETPROCADDRESS                   ; KERNEL.50
extern FREELIBRARY                      ; KERNEL.96
extern CREATEDC                         ; dc.c: GDI.53

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment ENTRY_TEXT public align=16 class=CODE use16

global GCREATEDC, _gdi_bait
GCREATEDC:
        jmp short gcreatedc_body
_gdi_bait:                              ; driver.c: the operands are
        call far LOADLIBRARY            ; at _gdi_bait + 1, + 6, + 11
        call far GETPROCADDRESS
        call far FREELIBRARY
gcreatedc_body:                         ; CreateDC + 11h: a far procedure
        mov ax, DGROUP                  ; with the standard frame
        inc bp                          ; + 14h: the 5 bytes ATM moves
        push bp
        db 8Bh, 0ECh                    ; mov bp, sp
        push ds
        mov ds, ax                      ; + 19h
        push word [bp + 14h]            ; lpszDriver
        push word [bp + 12h]
        push word [bp + 10h]            ; lpszDevice
        push word [bp + 0Eh]
        push word [bp + 0Ch]            ; lpszOutput
        push word [bp + 0Ah]
        push word [bp + 8]              ; lpInitData
        push word [bp + 6]
        call far CREATEDC
        lea sp, [bp - 2]
        pop ds
        pop bp
        dec bp
        retf 16
