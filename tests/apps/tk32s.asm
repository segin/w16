; tk32s.asm - TK32's signal procedure entry: record BX and DI (the
; registers KERNEL may pass the signal in), then go to sig_c (Pascal,
; the same five arguments, so the stack is passed through untouched).
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _TEXT public align=16 class=CODE use16
extern _sig_bx, _sig_di, SIG_C
global sig_entry
sig_entry:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov [_sig_bx wrt DGROUP], bx
        mov [_sig_di wrt DGROUP], di
        pop ax
        pop ds
        jmp far SIG_C
