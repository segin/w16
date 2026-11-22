; vx11s.asm - VX11's helpers (tests/apps/vx11.c): INT 31h, INT 21h and
; INT 0F1h with registers, LAR and LSL that do not fault.
;   void __cdecl __far vx_int31(WORD __far *regs)
;   void __cdecl __far vx_int21(WORD __far *regs)
;   void __cdecl __far vx_intf1(WORD __far *regs, int cli)
;   WORD __cdecl __far vx_lar(WORD sel)  access byte, FFFFh if LAR fails
;   DWORD __cdecl __far vx_lsl(WORD sel) limit, FFFFFFFFh if LSL fails
; regs: ax bx cx dx si di es flags (flags: out only; es in and out)

        bits 16
        cpu 386

segment _TEXT public align=16 class=CODE use16
global _vx_int31, _vx_int21, _vx_intf1, _vx_lar, _vx_lsl

_vx_int31:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        les bx, [bp+6]
        push es
        push bx
        call load
        int 31h
        jmp store

_vx_int21:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        les bx, [bp+6]
        push es
        push bx
        call load
        int 21h
        jmp store

_vx_intf1:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        les bx, [bp+6]
        push es
        push bx
        call load
        cmp word [bp+10], 0
        je .on
        cli
        int 0F1h
        sti
        jmp store
.on:    int 0F1h
        jmp store

; registers from ES:BX
load:   mov ax, [es:bx]
        mov cx, [es:bx+4]
        mov dx, [es:bx+6]
        mov si, [es:bx+8]
        mov di, [es:bx+10]
        push word [es:bx+12]
        mov bx, [es:bx+2]
        pop es
        ret

; registers back into the structure whose far address is on the stack
store:  pushf
        push es
        push di
        push bx
        mov bx, sp
        les di, [ss:bx+8]
        mov [es:di], ax
        pop word [es:di+2]
        pop word [es:di+10]
        pop word [es:di+12]
        pop word [es:di+14]
        mov [es:di+4], cx
        mov [es:di+6], dx
        mov [es:di+8], si
        add sp, 4
        pop di
        pop si
        pop ds
        pop bp
        retf

_vx_lar:
        push bp
        mov bp, sp
        mov ax, 0FFFFh
        lar dx, [bp+6]
        jnz .no
        mov al, dh
        xor ah, ah
.no:    pop bp
        retf

_vx_lsl:
        push bp
        mov bp, sp
        movzx ecx, word [bp+6]
        mov eax, 0FFFFFFFFh
        lsl eax, ecx
        mov edx, eax
        shr edx, 16
        pop bp
        retf

; ---- K: the System VM's keyboard bytes (VX11K) ----
; void __cdecl __far vx_kbhook(int on): INT 09h's protected-mode vector
; hooked (DPMI 0204h/0205h); the handler keeps every byte read at port 60h
; (up to 200) in _vx_kbbuf, _vx_kbn their count, and goes on to the old one
global _vx_kbhook, _vx_kbbuf, _vx_kbn
segment _DATA public align=2 class=DATA use16
_vx_kbn   dw 0
old9      dd 0
_vx_kbbuf times 200 db 0
group DGROUP _DATA
segment _TEXT
_vx_kbhook:
        push bp
        mov bp, sp
        push bx
        push cx
        push dx
        cmp word [bp+6], 0
        je .off
        mov ax, 0204h
        mov bl, 9
        int 31h
        mov [old9], dx
        mov [old9 + 2], cx
        mov cx, cs
        mov dx, kbint
        mov ax, 0205h
        mov bl, 9
        int 31h
        jmp .r
.off:   mov dx, [old9]
        mov cx, [old9 + 2]
        mov ax, 0205h
        mov bl, 9
        int 31h
.r:     pop dx
        pop cx
        pop bx
        pop bp
        retf
kbint:  sub sp, 4
        push bp
        mov bp, sp
        push ds
        push ax
        push bx
        mov ax, seg _vx_kbn
        mov ds, ax
        in al, 60h
        mov bx, [_vx_kbn]
        cmp bx, 200
        jae .f
        mov [_vx_kbbuf + bx], al
        inc word [_vx_kbn]
.f:     mov ax, [old9]
        mov [bp + 2], ax
        mov ax, [old9 + 2]
        mov [bp + 4], ax
        pop bx
        pop ax
        pop ds
        pop bp
        retf
