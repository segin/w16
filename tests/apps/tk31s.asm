; tk31s.asm - TK31's register-level caller: call a far Pascal procedure
; with up to 6 word arguments and known values in every register (ES =
; the caller's DS, carry set); record the registers afterwards in
; w_regs (ax bx cx dx si di bp ds es flags).
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _w_regs
_w_regs times 10 dw 0
w_fn    dd 0
w_bp    dw 0
segment _TEXT public align=16 class=CODE use16

; void __cdecl __far w_call(void __far *fn, int nargs, u16 a0, ... a5)
; Pascal: a0 is pushed first.
global _w_call
_w_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp + 6]
        mov [w_fn wrt DGROUP], ax
        mov ax, [bp + 8]
        mov [w_fn + 2 wrt DGROUP], ax
        mov cx, [bp + 10]
        lea si, [bp + 12]
.push:  jcxz .go
        push word [ss:si]
        add si, 2
        dec cx
        jmp .push
.go:    push ds
        pop es
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        mov [w_bp wrt DGROUP], bp
        mov bp, 7777h
        stc
        call far [w_fn wrt DGROUP]
        pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [_w_regs wrt DGROUP]
        pop word [_w_regs + 14 wrt DGROUP]
        pop word [_w_regs + 18 wrt DGROUP]
        mov [_w_regs + 2 wrt DGROUP], bx
        mov [_w_regs + 4 wrt DGROUP], cx
        mov [_w_regs + 6 wrt DGROUP], dx
        mov [_w_regs + 8 wrt DGROUP], si
        mov [_w_regs + 10 wrt DGROUP], di
        mov [_w_regs + 12 wrt DGROUP], bp
        mov [_w_regs + 16 wrt DGROUP], es
        mov bp, [w_bp wrt DGROUP]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
