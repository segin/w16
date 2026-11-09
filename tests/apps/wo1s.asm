; wo1s.asm - WO1's register-level call: WinOldApCall (KERNEL.151), which
; returns values in registers (Schulman ch. 5); records them in woa_regs.
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _woa_regs
_woa_regs times 6 dw 0          ; ax bx cx dx si di
segment _TEXT public align=16 class=CODE use16
extern WINOLDAPCALL

; void __cdecl __far woa_call(WORD mode)
global _woa_call
_woa_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push word [bp + 6]
        call far WINOLDAPCALL
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_woa_regs], ax
        mov [_woa_regs + 2], bx
        mov [_woa_regs + 4], cx
        mov [_woa_regs + 6], dx
        mov [_woa_regs + 8], si
        mov [_woa_regs + 10], di
        pop ax
        pop ds
        pop di
        pop si
        pop bp
        retf
