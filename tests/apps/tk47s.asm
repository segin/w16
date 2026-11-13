; tk47s.asm - TK47's pointing-device handler, copied by tk47.c into a
; block below 1 MB (GlobalDOSAlloc) and given to INT 15h C207h as a code
; selector of that block, as MOUSE.DRV does. It notes whether it runs in
; real or protected mode (MSW bit 0), counts its calls of each kind and
; keeps the four words of the last call and its SS. Data lives in the
; block itself (offsets below): in real mode through CS, in protected
; mode through the data selector stored at D_SEL.
        bits 16
        cpu 286
D_SEL   equ 0           ; a data selector of the block (tk47.c)
D_RM    equ 2           ; calls in real mode
D_PM    equ 4           ; calls in protected mode
D_W     equ 6           ; the last call's four words
D_SS    equ 14          ; and its SS
D_SP    equ 16          ; and SP
H_OFF   equ 32          ; the handler starts here in the block

segment _TEXT public align=16 class=CODE use16
global _h47_code, _h47_len
_h47_len dw h_end - h_start
_h47_code:
h_start:
        push bp
        mov bp, sp
        push ax
        push ds
        smsw ax
        test al, 1
        jnz .pm
        mov ax, cs
        mov ds, ax
        inc word [D_RM]
        jmp .rec
.pm:    mov ds, [cs:D_SEL]
        inc word [D_PM]
.rec:   mov ax, [bp + 6]
        mov [D_W], ax
        mov ax, [bp + 8]
        mov [D_W + 2], ax
        mov ax, [bp + 10]
        mov [D_W + 4], ax
        mov ax, [bp + 12]
        mov [D_W + 6], ax
        mov [D_SS], ss
        mov [D_SP], bp
        pop ds
        pop ax
        pop bp
        retf
h_end:

; WORD __cdecl __far c207(WORD sel, WORD off): INT 15h C207h with ES:BX =
; sel:off; AL = AH out, AH = CF
global _c207
_c207:
        push bp
        mov bp, sp
        push es
        push bx
        mov es, [bp + 6]
        mov bx, [bp + 8]
        mov ax, 0C207h
        int 15h
        mov al, ah
        mov ah, 0
        adc ah, 0
        pop bx
        pop es
        pop bp
        retf
