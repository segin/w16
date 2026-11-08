; tk15s.asm - start-up code of TK15A.EXE (the helper task of TK15): like
; start.asm, but it calls pre_init() before InitTask (to see what has
; happened before the task's own start-up runs), and it has no log: TK15A
; reports through TK15's DGROUP (tk15.h).

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern INITTASK                 ; KERNEL.91
extern _tmain                   ; int __cdecl __far tmain(void)
extern _pre_init                ; void __cdecl __far pre_init(void)

segment _NULL
        times 16 db 0

segment _DATA
global _entry_regs, _init_regs
; {ax, bx, cx, dx, si, di, bp, sp, ds, es, ss}
_entry_regs     times 11 dw 0
; {ax, bx, cx, dx, si, di, es}
_init_regs      times 7 dw 0

segment _TEXT
global start
start:
        mov [_entry_regs + 0 wrt DGROUP], ax
        mov [_entry_regs + 2 wrt DGROUP], bx
        mov [_entry_regs + 4 wrt DGROUP], cx
        mov [_entry_regs + 6 wrt DGROUP], dx
        mov [_entry_regs + 8 wrt DGROUP], si
        mov [_entry_regs + 10 wrt DGROUP], di
        mov [_entry_regs + 12 wrt DGROUP], bp
        mov [_entry_regs + 14 wrt DGROUP], sp
        mov [_entry_regs + 16 wrt DGROUP], ds
        mov [_entry_regs + 18 wrt DGROUP], es
        mov [_entry_regs + 20 wrt DGROUP], ss
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push es
        call far _pre_init
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        call far INITTASK
        mov [_init_regs + 0 wrt DGROUP], ax
        mov [_init_regs + 2 wrt DGROUP], bx
        mov [_init_regs + 4 wrt DGROUP], cx
        mov [_init_regs + 6 wrt DGROUP], dx
        mov [_init_regs + 8 wrt DGROUP], si
        mov [_init_regs + 10 wrt DGROUP], di
        mov [_init_regs + 12 wrt DGROUP], es
        call far _tmain
        mov ah, 4Ch
        int 21h

; int dos_int21(WORD far *r): as in start.asm
global _dos_int21
_dos_int21:
        push bp
        mov bp, sp
        push ds
        push es
        push si
        push di
        les bx, [bp + 6]
        mov ax, [es:bx + 0]
        mov cx, [es:bx + 4]
        mov dx, [es:bx + 6]
        mov si, [es:bx + 8]
        mov di, [es:bx + 10]
        mov ds, [es:bx + 12]
        push word [es:bx + 2]
        mov es, [es:bx + 14]
        pop bx
        int 21h
        push es
        push bx
        les bx, [bp + 6]
        mov [es:bx + 0], ax
        pop word [es:bx + 2]
        mov [es:bx + 4], cx
        mov [es:bx + 6], dx
        mov [es:bx + 8], si
        mov [es:bx + 10], di
        mov [es:bx + 12], ds
        pop word [es:bx + 14]
        mov ax, 0
        adc ax, 0
        pop di
        pop si
        pop es
        pop ds
        pop bp
        retf
