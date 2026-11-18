; tmsent.asm - the DriverProc entry of TMS.DRV, the logging pass-through timer
; driver (tests/timer/tms.c; ARCHITECTURE.md 21). It records what C cannot
; see from inside a function: the flags at entry (interrupts on or off),
; SS and SP, and the caller's return address, then calls tms_proc() with
; them and a far pointer to the arguments.
;
; LRESULT FAR PASCAL DriverProc(DWORD id, HDRVR h, UINT msg, LPARAM l1,
;                               LPARAM l2)        (pops 16 bytes)
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

; DWORD __cdecl __far tms_proc(WORD flags, WORD ss, WORD sp, WORD retip,
;                              WORD retcs, void __far *args)
extern _tms_proc

global DRIVERPROC
DRIVERPROC:
        push bp
        mov bp, sp
        pushf                           ; [bp-2] the flags at entry
        push ds
        push bx
        push cx
        push si
        push di
        push es
        mov ax, DGROUP
        mov ds, ax
        push ss                         ; the arguments: l2 at [bp+6],
        lea ax, [bp + 6]                ; l1 +10, msg +14, h +16, id +18
        push ax
        push word [bp + 4]              ; return CS
        push word [bp + 2]              ; return IP
        lea ax, [bp + 2]                ; SP at entry
        push ax
        push ss
        push word [bp - 2]
        cld
        call far _tms_proc
        add sp, 14
        pop es
        pop di
        pop si
        pop cx
        pop bx
        pop ds
        mov sp, bp
        pop bp
        retf 16

; WORD __cdecl __far tms_peek(const void __far *src, void __far *dst,
;                             WORD n): copies n bytes when src's selector
; is readable and its limit covers them; returns n, else 0 (nothing read).
; Safe at interrupt time: verr/lsl, no KERNEL call.
global _tms_peek
_tms_peek:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push es
        xor ax, ax
        mov cx, [bp + 14]
        jcxz .r
        mov dx, [bp + 8]                ; src selector
        verr dx
        jnz .r
        lsl bx, dx
        jnz .r
        mov si, [bp + 6]
        mov ax, si
        add ax, cx
        jc .no
        dec ax
        cmp ax, bx
        ja .no
        mov ds, dx
        les di, [bp + 10]
        cld
        rep movsb
        mov ax, [bp + 14]
        jmp .r
.no:    xor ax, ax
.r:     pop es
        pop di
        pop si
        pop ds
        pop bp
        retf

; WEP(bSystemExit) -> 1
global WEP
WEP:
        mov ax, 1
        retf 2
