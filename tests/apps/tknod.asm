; tknod.asm - TKNOD.DLL: a DLL without a data segment. Its entry point
; returns 1 (success); NOD_GET returns 1234h.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

global nodentry, NOD_GET
nodentry:
        mov ax, 1
        retf
NOD_GET:
        mov ax, 1234h
        retf
