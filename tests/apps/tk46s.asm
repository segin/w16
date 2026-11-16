; tk46s.asm - TK46's pointing-device handler (INT 15h C207h with a
; protected-mode address): counts its calls and records the state of the
; last one: the four words above the return address, SS, SP, DS, ES and
; the flags; returns with RETF (the BIOS convention).
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _p_calls, _p_rec
_p_calls dw 0
_p_rec   times 12 dw 0      ; w0 w1 w2 w3, ret IP, ret CS, SS, SP, DS, ES, flags, AX
segment _TEXT public align=16 class=CODE use16
dsel    dw 0

global _ps2_handler
_ps2_handler:
        push bp
        mov bp, sp
        push ds
        push ax
        pushf
        mov ax, [cs:dsel]
        mov ds, ax
        inc word [_p_calls wrt DGROUP]
        mov ax, [bp + 6]
        mov [_p_rec wrt DGROUP], ax
        mov ax, [bp + 8]
        mov [_p_rec + 2 wrt DGROUP], ax
        mov ax, [bp + 10]
        mov [_p_rec + 4 wrt DGROUP], ax
        mov ax, [bp + 12]
        mov [_p_rec + 6 wrt DGROUP], ax
        mov ax, [bp + 2]
        mov [_p_rec + 8 wrt DGROUP], ax
        mov ax, [bp + 4]
        mov [_p_rec + 10 wrt DGROUP], ax
        mov [_p_rec + 12 wrt DGROUP], ss
        mov ax, bp
        add ax, 2
        mov [_p_rec + 14 wrt DGROUP], ax
        mov ax, [bp - 2]
        mov [_p_rec + 16 wrt DGROUP], ax
        mov [_p_rec + 18 wrt DGROUP], es
        pop ax                          ; flags
        mov [_p_rec + 20 wrt DGROUP], ax
        pop ax
        mov [_p_rec + 22 wrt DGROUP], ax
        pop ds
        pop bp
        retf

; void __cdecl __far ps2_set(void): dsel := DS through a code alias, then
; INT 15h C207h with ES:BX = ps2_handler; returns AX = AH status, CF in
; the high byte
extern ALLOCCSTODSALIAS
global _ps2_set
_ps2_set:
        push bp
        mov bp, sp
        push es
        push bx
        push cs
        call far ALLOCCSTODSALIAS
        mov es, ax
        mov [es:dsel], ds
        push cs
        pop es
        mov bx, _ps2_handler
        mov ax, 0C207h
        int 15h
        mov al, ah
        mov ah, 0
        adc ah, 0
        pop bx
        pop es
        pop bp
        retf
