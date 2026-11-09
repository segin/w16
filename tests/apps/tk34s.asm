; tk34s.asm - TK34's caller for an export of unknown arguments: push N
; zero words, call the procedure, and record how far SP moved back
; (bytes the callee popped) and the registers afterwards. The words
; pushed all hold the AX value given.
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _u_regs, _u_popped
_u_regs   times 10 dw 0         ; ax bx cx dx si di bp ds es flags
_u_popped dw 0
u_fn      dd 0
u_sp      dw 0
u_bp      dw 0
segment _TEXT public align=16 class=CODE use16

; void __cdecl __far u_call(void __far *fn, int nzero, u16 ax, u16 bx, u16 cx)
global _u_call
_u_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp + 6]
        mov [u_fn wrt DGROUP], ax
        mov ax, [bp + 8]
        mov [u_fn + 2 wrt DGROUP], ax
        mov cx, [bp + 10]
        mov [u_bp wrt DGROUP], bp
        mov [u_sp wrt DGROUP], sp
.push:  jcxz .go
        push word [bp + 12]             ; every word = the AX value
        dec cx
        jmp .push
.go:    mov ax, [bp + 12]
        mov bx, [bp + 14]
        mov cx, [bp + 16]
        xor dx, dx
        mov si, 5555h
        mov di, 6666h
        push ds
        pop es
        call far [u_fn wrt DGROUP]
        pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [_u_regs wrt DGROUP]
        pop word [_u_regs + 14 wrt DGROUP]
        pop word [_u_regs + 18 wrt DGROUP]
        mov [_u_regs + 2 wrt DGROUP], bx
        mov [_u_regs + 4 wrt DGROUP], cx
        mov [_u_regs + 6 wrt DGROUP], dx
        mov [_u_regs + 8 wrt DGROUP], si
        mov [_u_regs + 10 wrt DGROUP], di
        mov [_u_regs + 16 wrt DGROUP], es
        mov ax, sp
        sub ax, [u_sp wrt DGROUP]       ; 0: popped all it was given
        add ax, [bp + 10]
        add ax, [bp + 10]               ; bytes popped = 2n + (sp - sp0)
        mov [_u_popped wrt DGROUP], ax
        mov sp, [u_sp wrt DGROUP]
        mov bp, [u_bp wrt DGROUP]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
