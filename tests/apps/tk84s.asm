; tk84s.asm - TK84's stack meter (tests/apps/tk84.c).
;
; WORD __cdecl __far stackuse(FARPROC fn, WORD nargs, WORD a0, ...)
; fills the 1024 bytes below the stack pointer with CDh, pushes a0 ...
; a(nargs-1) in that order (a far pascal call's arguments, first one
; first), calls fn, and returns how many bytes below its return address
; the call wrote: the distance from the return address's slot down to
; the lowest byte that is no longer CDh (0: none written). DX gets the
; call's AX back.
        bits 16
        cpu 286

FILL    equ 1024

segment _TEXT public align=16 class=CODE use16
global _stackuse
_stackuse:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        ; SP0 = BP - 8: fill [SP0 - FILL, SP0)
        mov ax, ss
        mov es, ax
        mov di, sp
        sub di, FILL
        mov cx, FILL / 2
        mov ax, 0CDCDh
        cld
        rep stosw
        ; the arguments: [bp+6] fn, [bp+10] nargs, [bp+12] a0 ...
        mov cx, [bp+10]
        lea bx, [bp+12]
.push:  jcxz .call
        push word [ss:bx]
        add bx, 2
        dec cx
        jmp .push
.call:  call far [bp+6]
        mov si, ax                      ; the call's result
        lea sp, [bp-8]
        mov ax, ss
        mov es, ax
        mov di, bp
        sub di, 8 + FILL
        mov cx, FILL
        mov al, 0CDh
        cld
        repe scasb
        je .none
        dec di                          ; the lowest byte written
        ; the return address's slot: SP0 - 2 * nargs - 4
        mov ax, [bp+10]
        shl ax, 1
        neg ax
        add ax, bp
        sub ax, 8 + 4
        sub ax, di
        jae .done
.none:  xor ax, ax
.done:  mov dx, si
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
