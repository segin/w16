; kpad.asm - padding for a layout-shifted KRNL386 (Makefile, kpad/N):
; KPAD bytes of INT 3 in _TEXT, linked right after thunk.o, so everything
; after it in KERNEL's code segment moves by KPAD bytes. Never part of the
; normal build: the boot test's win386pad runs a case on such a KERNEL to
; catch bugs that depend on where KERNEL's code lands (an address, a
; limit or an uninitialised value that happens to work at one layout).

        bits 16

segment _TEXT  public align=1 class=CODE use16
        times KPAD db 0CCh
