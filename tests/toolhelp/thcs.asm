; thcs.asm - THC.EXE's wait under Windows stack frames (tests/toolhelp/
; thc.c): wait_frames makes a far frame (odd BP) and a near one around
; wait_loop, so that a stack trace of the waiting child has frames of its
; own to find.
        bits 16
        cpu 286
segment _TEXT public align=16 class=CODE use16

extern _wait_loop               ; WORD __cdecl __far wait_loop(void)

global _wait_frames
_wait_frames:
        inc bp
        push bp
        mov bp, sp
        push ds
        call wf_near
        pop ds
        pop bp
        dec bp
        retf

wf_near:
        push bp
        mov bp, sp
        call far _wait_loop
        pop bp
        ret
