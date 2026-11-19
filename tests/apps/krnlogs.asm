; krnlogs.asm - KRNLOG.DLL's __WINFLAGS (tests/apps/krnlog.c): KERNEL's
; ordinal 178 is a constant entry, which wlink cannot export, so the
; driver's reference to it is resolved to a label at offset WINFLAGS of a
; segment of its own and fixed up with that offset. WINFLAGS is the
; GetWinFlags() of the runs (Microsoft's KRNL286, Standard mode, a
; coprocessor: WF_PMODE, WF_CPU286, WF_STANDARD, WF_80x87); kl_winflags hands it to KLRECS so
; that SN1 can check it against the run's own.
        bits 16
        cpu 286

%ifndef WINFLAGS
%define WINFLAGS 0415h
%endif

segment _TEXT public align=16 class=CODE use16
segment KLWF align=16 class=FAR_DATA use16

segment KLWF
        times WINFLAGS db 0
global __WINFLAGS
__WINFLAGS:
        db 0

segment _TEXT
global _kl_winflags
_kl_winflags:
        mov ax, WINFLAGS
        retf
