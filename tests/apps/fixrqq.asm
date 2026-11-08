; fixrqq.asm - the floating-point emulator fixup symbols, as absolute
; values, for programs built without a C run-time library (TK6).
;
; Compilers reference FIxRQQ / FJxRQQ at floating-point instructions
; (OpenWatcom with -fpi); for Windows, wlink turns each reference into an
; NE OS fixup (relocation type 3). Added to the word at the FWAIT (9Bh)
; and the next byte, the values turn the instruction into INT 34h-3Dh for
; an emulator. Values as defined in OpenWatcom's lib286/win/emu87.lib
; (initemuw.asm; OWPL), used as facts.

        bits 16
global FIWRQQ, FIDRQQ, FIERQQ, FICRQQ, FISRQQ, FIARQQ, FJCRQQ, FJSRQQ, FJARQQ
FIWRQQ equ 0A23Dh
FIDRQQ equ 5C32h
FIERQQ equ 1632h
FICRQQ equ 0E32h
FISRQQ equ 0632h
FIARQQ equ 0FE32h
FJCRQQ equ 0C000h
FJSRQQ equ 8000h
FJARQQ equ 4000h
