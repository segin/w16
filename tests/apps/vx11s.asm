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
