; mdsent.asm - the entries of MDS*.DRV, the logging pass-through device
; drivers (tests/mmsystem/mds.c): each records the flags at entry and the
; caller's code segment and hands its arguments to mds_entry with its
; kind (tests/mmsystem/mdl.h). All take 16 bytes of arguments:
;   DriverProc(DWORD id, HDRVR h, UINT msg, LPARAM l1, LPARAM l2)
;   xxxMessage(UINT dev, UINT msg, DWORD user, DWORD p1, DWORD p2)
; A variant exports only some of them (its .lnk file).
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

; DWORD __cdecl __far mds_entry(WORD kind, WORD flags, WORD retcs, void __far *args)
extern _mds_entry

global DRIVERPROC, WODMESSAGE, WIDMESSAGE, MODMESSAGE, MIDMESSAGE, AUXMESSAGE
DRIVERPROC:
        push word 0
        jmp mds_common
WODMESSAGE:
        push word 1
        jmp mds_common
WIDMESSAGE:
        push word 2
        jmp mds_common
MODMESSAGE:
        push word 3
        jmp mds_common
MIDMESSAGE:
        push word 4
        jmp mds_common
AUXMESSAGE:
        push word 5
mds_common:
        push bp
        mov bp, sp                      ; [bp+2] kind, [bp+4] return IP,
        pushf                           ; [bp+6] CS, [bp+8] the arguments
        push ds
        push bx
        push cx
        push si
        push di
        push es
        mov ax, DGROUP
        mov ds, ax
        push ss
        lea ax, [bp + 8]
        push ax
        push word [bp + 6]
        push word [bp - 2]
        push word [bp + 2]
        cld
        call far _mds_entry
        add sp, 10
        pop es
        pop di
        pop si
        pop cx
        pop bx
        pop ds
        mov sp, bp
        pop bp
        add sp, 2                       ; the kind
        retf 16

global WEP
WEP:
        mov ax, 1
        retf 2
