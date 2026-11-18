; mdlent.asm - MMSYSLOG.DLL's register-level parts (tests/mmsystem/mdl.c):
; the MMSYSTEM functions a device driver whose imports were renamed to
; MMSYSLOG (tools/nerename.py, on a scratch copy) calls here instead.
; Each goes on to MMSYSTEM's own by a far jump with every register, the
; flags and the stack as the driver left them, so MMSYSTEM sees the call
; as if it came straight from the driver; DriverCallback and the MIDI
; output functions are recorded first (md_dcb, md_fwd). Also the peeks
; the records use, safe at interrupt time.
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern _md_dcb                  ; void __cdecl __far md_dcb(WORD retcs, WORD fl, void __far *args)
extern _md_fwd                  ; void __cdecl __far md_fwd(WORD ord, WORD retcs, WORD __far *args)

; the MMSYSTEM functions passed on, and where (filled by md_resolve)
%define FWDS 31, 32, 33, 201, 202, 204, 205, 206, 207, 208, 209, 210, 213, 214, 216, 601, 602, 603, 605, 606, 607

segment _DATA
global _md_ords, _md_tbl, _md_nfwd
_md_ords        dw FWDS
_md_nfwd        dw 21
_md_tbl         times 21 dd 0

segment _TEXT

; on to MMSYSTEM's function number %1 (table slot %2): the target is put
; on the stack and reached by RETF, everything else untouched
%macro jumpon 1
        push ax                         ; room for the target
        push ax
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_md_tbl + 4 * %1]
        mov [bp + 2], ax
        mov ax, [_md_tbl + 4 * %1 + 2]
        mov [bp + 4], ax
        pop ax
        pop ds
        pop bp
        retf
%endmacro

; a recorded one: md_fwd(ord, return CS, the arguments) or md_dcb
%macro record 2
        push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        cld
        push ss
        lea ax, [bp + 6]
        push ax
%ifidn %2, dcb
        push word [bp - 2]
        push word [bp + 4]
        call far _md_dcb
%else
        push word [bp + 4]
        push word %1
        call far _md_fwd
%endif
        add sp, 8
        pop es
        pop ds
        popa
        popf
        pop bp
%endmacro

global DRIVERCALLBACK, STACKENTER, STACKLEAVE
DRIVERCALLBACK:
        record 31, dcb
        jumpon 0
STACKENTER:
        jumpon 1
STACKLEAVE:
        jumpon 2

%macro fwd 3                            ; name, ordinal, slot
global %1
%1:
        record %2, fwd
        jumpon %3
%endmacro
        fwd MIDIOUTGETNUMDEVS, 201, 3
        fwd MIDIOUTGETDEVCAPS, 202, 4
        fwd MIDIOUTOPEN, 204, 5
        fwd MIDIOUTCLOSE, 205, 6
        fwd MIDIOUTPREPAREHEADER, 206, 7
        fwd MIDIOUTUNPREPAREHEADER, 207, 8
        fwd MIDIOUTSHORTMSG, 208, 9
        fwd MIDIOUTLONGMSG, 209, 10
        fwd MIDIOUTRESET, 210, 11
        fwd MIDIOUTCACHEPATCHES, 213, 12
        fwd MIDIOUTCACHEDRUMPATCHES, 214, 13
        fwd MIDIOUTMESSAGE, 216, 14

global TIMEGETSYSTEMTIME, TIMESETEVENT, TIMEKILLEVENT, TIMEBEGINPERIOD, TIMEENDPERIOD, TIMEGETTIME
TIMEGETSYSTEMTIME:
        jumpon 15
TIMESETEVENT:
        jumpon 16
TIMEKILLEVENT:
        jumpon 17
TIMEBEGINPERIOD:
        jumpon 18
TIMEENDPERIOD:
        jumpon 19
TIMEGETTIME:
        jumpon 20

; WORD __cdecl __far md_peek(const void __far *src, void __far *dst,
;                            WORD n): copies n bytes when src's selector
; is readable and its limit covers them; returns n, else 0
global _md_peek
_md_peek:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push es
        xor ax, ax
        mov cx, [bp + 14]
        jcxz .r
        mov dx, [bp + 8]
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

; WORD __cdecl __far md_valid(WORD sel): 1 when readable
global _md_valid
_md_valid:
        push bp
        mov bp, sp
        xor ax, ax
        mov dx, [bp + 6]
        or dx, dx
        jz .r
        verr dx
        jnz .r
        inc ax
.r:     pop bp
        retf

; WORD __cdecl __far md_cli(void): the flags, interrupts off
global _md_cli
_md_cli:
        pushf
        pop ax
        cli
        retf
; void __cdecl __far md_sti(WORD fl): interrupts on again if they were
global _md_sti
_md_sti:
        push bp
        mov bp, sp
        test byte [bp + 7], 2
        jz .r
        sti
.r:     pop bp
        retf

global WEP
WEP:
        mov ax, 1
        retf 2
