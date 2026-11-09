; tk41s.asm - TK41's fake API: a far pascal function of two word
; arguments with the usual frame (inc bp; push bp; mov bp, sp; push ds)
; that calls K327 (HandleParamError) with BX = the error code given and
; notes whether K327 came back to it.
;
; WORD FAR PASCAL fake_api(WORD err, WORD x)   returns 5555h if K327 returned
; k41_cont: 1 when K327 returned to fake_api; k41_regs: ax bx cx dx si di
; after K327 returned (or at the caller, see tk41.c); k41_sp: SP in
; fake_api before the call
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _k41_cont, _k41_regs, _k41_fn, _k41_back
_k41_cont dw 0
_k41_regs times 6 dw 0
_k41_fn   dd 0                  ; K327's address (tk41.c sets it)
_k41_back times 8 dw 0          ; at the caller: ax bx cx dx si di, SP change, flags
k41_sp    dw 0
k41_bp    dw 0
segment _TEXT public align=16 class=CODE use16

global FAKE_API
FAKE_API:
        inc bp
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov word [_k41_cont wrt DGROUP], 0
        mov bx, [bp + 8]                ; err (first argument)
        mov ax, 1111h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        call far [_k41_fn wrt DGROUP]
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_k41_regs wrt DGROUP], ax
        mov [_k41_regs + 2 wrt DGROUP], bx
        mov [_k41_regs + 4 wrt DGROUP], cx
        mov [_k41_regs + 6 wrt DGROUP], dx
        mov [_k41_regs + 8 wrt DGROUP], si
        mov [_k41_regs + 10 wrt DGROUP], di
        mov word [_k41_cont wrt DGROUP], 1
        pop ds
        mov ax, 5555h
        pop ds
        pop bp
        dec bp
        retf 4

; void __cdecl __far k41_call(WORD err): FAKE_API(err, 7) from a caller
; that records what came back (k41_back) and repairs SP and BP itself
global _k41_call
_k41_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp + 6]
        mov [k41_bp wrt DGROUP], bp
        mov [k41_sp wrt DGROUP], sp
        push ax                         ; err
        push word 7                     ; x
        call far FAKE_API
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_k41_back wrt DGROUP], ax
        mov [_k41_back + 2 wrt DGROUP], bx
        mov [_k41_back + 4 wrt DGROUP], cx
        mov [_k41_back + 6 wrt DGROUP], dx
        mov [_k41_back + 8 wrt DGROUP], si
        mov [_k41_back + 10 wrt DGROUP], di
        pushf
        pop word [_k41_back + 14 wrt DGROUP]
        pop ax                          ; the DS FAKE_API's caller has
        mov ax, sp
        sub ax, [k41_sp wrt DGROUP]
        mov [_k41_back + 12 wrt DGROUP], ax
        mov sp, [k41_sp wrt DGROUP]
        mov bp, [k41_bp wrt DGROUP]
        pop ds
        pop di
        pop si
        pop bp
        retf
