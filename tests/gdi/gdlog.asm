; gdlog.asm - the forwarding machinery of the logging stand-ins (GD1, GD2).
;
; Each export is a stub (tools/mkgdlog.py) that pushes its table index
; twice and jumps to gdl_common, so the stack holds
;     [idx] [idx] [return IP] [return CS] [arguments...]
; gdl_common records the call (gdl_enter in gdlog.c), puts the real entry
; point (gdl_realtab, in this code segment, filled by the loader's import
; fixups) in place of the two index words and, when the result is to be
; recorded, replaces the return address with gdl_retcatch (gdl_enter keeps
; the caller's on a side stack). Then every register is restored and a
; RETF jumps to the real function with the caller's stack and registers.
; gdl_retcatch records DX:AX (gdl_exit, which returns the caller's return
; address) and returns there with the real function's registers. It also
; passes gdl_exit the stack pointer the real function returned with, so
; the bytes it popped can be recorded (VGREC, gdlog.c's GD_POPS).

        bits 16
        cpu 286

%define GDLOG_MAIN
%include "gdlog.inc"

extern _gdl_enter               ; u16 __cdecl far gdl_enter(u16 idx, u16 far *frame)
extern _gdl_exit                ; u32 __cdecl far gdl_exit(u32 result, u16 sp)
extern _gdl_init                ; void __cdecl far gdl_init(void)

segment _NULL
        times 16 db 0

segment _TEXT
global gdl_common
gdl_common:
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        mov ax, DGROUP
        mov ds, ax
        push ss
        lea ax, [bp + 6]
        push ax
        push word [bp + 2]
        call far _gdl_enter
        add sp, 6
        mov bx, [bp + 2]
        shl bx, 2
        mov cx, [cs:gdl_realtab + bx]
        mov dx, [cs:gdl_realtab + bx + 2]
        mov [bp + 2], cx
        mov [bp + 4], dx
        test ax, ax
        jz .go
        mov word [bp + 6], gdl_retcatch
        mov [bp + 8], cs
.go:    popa
        pop es
        pop ds
        pop bp
        retf

gdl_retcatch:
        sub sp, 4
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        mov bx, DGROUP
        mov ds, bx
        lea bx, [bp + 6]                ; SP as the real function left it
        push bx
        push dx
        push ax
        call far _gdl_exit
        add sp, 6
        mov [bp + 2], ax
        mov [bp + 4], dx
        popa
        pop es
        pop ds
        pop bp
        retf

; the DLL's entry point: nothing to set up but the log
global gdl_libentry
gdl_libentry:
        push ds
        mov ax, DGROUP
        mov ds, ax
        pusha
        call far _gdl_init
        popa
        pop ds
        mov ax, 1
        retf

; void __cdecl far gdl_append(const char far *path, const char far *buf, u16 len)
; Append buf to the file path (created if missing), opening and closing it
; each time: DOS handles belong to the task that opened them, and the log
; is written from whichever task called.
global _gdl_append
_gdl_append:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        lds dx, [bp + 6]
        mov ax, 3D01h                   ; open for writing
        int 21h
        jnc .opened
        mov ah, 3Ch                     ; create
        xor cx, cx
        int 21h
        jc .done
.opened:
        mov bx, ax
        mov ax, 4202h                   ; to the end
        xor cx, cx
        xor dx, dx
        int 21h
        mov cx, [bp + 14]
        lds dx, [bp + 10]
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.done:  pop di
        pop si
        pop ds
        pop bp
        retf
