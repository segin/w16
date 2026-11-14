; tk57s.asm - TK57's helpers (tests/apps/tk57.c): INT 2Fh with registers,
; and a far call to a VxD API entry with registers, DS included both ways.
;   void __cdecl __far k57_int2f(WORD __far *regs)
;   void __cdecl __far k57_call(void __far *entry, WORD __far *regs)
; regs: ax bx cx dx si di es flags ds (flags: out only; ds 0 in: the
; caller's)

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16
global _k57_int2f, _k57_call

_k57_int2f:
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

_k57_call:
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

; registers from ES:BX (DS too when the structure's is not 0)
load:   mov ax, [es:bx+16]
        or ax, ax
        jz .keep
        mov ds, ax
.keep:  mov ax, [es:bx]
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
        push ds
        push es
        push di
        push bx                         ; sp+0 bx, +2 di, +4 es, +6 ds,
        mov bx, sp                      ; +8 flags, +10 regs offset, +12 seg
        les di, [ss:bx+10]
        mov [es:di], ax
        pop word [es:di+2]
        pop word [es:di+10]
        pop word [es:di+12]
        pop word [es:di+16]
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
