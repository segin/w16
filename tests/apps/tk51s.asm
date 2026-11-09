; tk51s.asm - TK51's stack frames for StackTraceCSIPFirst/Next: st_run
; makes two Windows far frames (inc bp / push bp / mov bp,sp / push ds)
; and calls st_walk(SS, CS, IP, BP) from the inner one.
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _TEXT public align=16 class=CODE use16

extern _st_walk                 ; void __cdecl __far st_walk(ss, cs, ip, bp)

global _st_run
_st_run:
        inc bp
        push bp
        mov bp, sp
        push ds
        call far st_run2
        pop ds
        pop bp
        dec bp
        retf

st_run2:
        inc bp
        push bp
        mov bp, sp
        push ds
.here:  push bp
        push word .here
        push cs
        push ss
        call far _st_walk
        add sp, 8
        pop ds
        pop bp
        dec bp
        retf
