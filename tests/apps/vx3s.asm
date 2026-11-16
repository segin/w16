; vx3s.asm - VX3's helpers (tests/apps/vx3.c): INT 2Fh with registers, and
; a far call to a VxD API entry with registers.
;   void __cdecl __far vx_int2f(WORD __far *regs)
;   void __cdecl __far vx_call(void __far *entry, WORD __far *regs)
; regs: ax bx cx dx si di es flags (flags: out only)

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16
global _vx_int2f, _vx_call

_vx_int2f:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        les bx, [bp+6]
        push es
        push bx
        call load
        int 2Fh
        jmp store

_vx_call:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        les bx, [bp+10]
        push es
        push bx
        call load
        call far [bp+6]
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
store:  pushf                           ; sp+0 flags
        push es                         ; sp+0 es, +2 flags
        push di                         ; sp+0 di, +2 es, +4 flags
        push bx                         ; sp+0 bx ... +8: regs offset, +10 segment
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
