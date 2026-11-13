; intcall.inc - generic "issue INT n with a regs16 block" helper.
;
;   INTCALL name, intno, far|near [, pm]
;
; Defines a cdecl function  int name(regs16 *r)  that loads AX..ES from *r,
; executes INT intno, stores all registers and FLAGS back into *r and
; returns the carry flag (0 or 1). `far` builds a large-model version
; (far return, far pointer argument), `near` a small-model one. `pm`
; (protected mode only): a caller's ES the call made invalid comes back 0.
; regs16 layout: ax 0, bx 2, cx 4, dx 6, si 8, di 10, ds 12, es 14, flags 16.

%macro INTCALL 3-4
global %1
%1:
        push bp
        mov bp, sp
        push si                 ; [bp-2]
        push di                 ; [bp-4]
        push ds                 ; [bp-6]
        push es                 ; [bp-8]
%ifidni %3, far
        mov bx, [bp+6]          ; offset of regs16
        push word [bp+8]        ; [bp-10] segment of regs16
        push bx                 ; [bp-12] offset of regs16
        mov ds, [bp+8]
%else
        mov bx, [bp+4]
        push ds                 ; [bp-10]
        push bx                 ; [bp-12]
%endif
        mov es, [bx+14]
        mov ax, [bx+0]
        mov cx, [bx+4]
        mov dx, [bx+6]
        mov si, [bx+8]
        mov di, [bx+10]
        push word [bx+12]
        mov bx, [bx+2]
        pop ds
        int %2
        pushf
        push ds
        push bx
        mov ds, [bp-10]
        mov bx, [bp-12]
        mov [bx+0], ax
        pop word [bx+2]
        mov [bx+4], cx
        mov [bx+6], dx
        mov [bx+8], si
        mov [bx+10], di
        pop word [bx+12]
        mov [bx+14], es
        pop ax
        mov [bx+16], ax
        and ax, 1
        ; the caller's ES may hold a selector the call freed (DPMI 0001h:
        ; the host zeroes segment registers holding it); it comes back 0
        ; then, as from 3.1's FreeSelector (tests/apps/tk17.c), not
        ; reloaded (a fault, or a dead selector)
%ifidni %4, pm
        mov dx, ax
        mov ax, [bp-8]
        lar ax, ax
        jz %%esok
        mov word [bp-8], 0
%%esok: mov ax, dx
%endif
        lea sp, [bp-8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
%ifidni %3, far
        retf
%else
        ret
%endif
%endmacro
