; entry.asm - VGA.DRV's library entry, WEP, the cursor entry points that
; run at interrupt time, and small helpers for the C code.
;
; KERNEL calls a library's entry point once, when it is loaded, with
; DS = its data segment and DI = its instance handle (Pietrek ch. 3).
; MoveCursor is called by USER from the mouse driver's interrupt and
; CheckCursor by SYSTEM.DRV's timer interrupt (US1: <SYSTEM:1> calls it as
; a timer procedure): both save every register they use and load the
; driver's own fixed data segment; everything they reach (cursor.c) is in
; fixed segments (tools/irqcheck.py --fixed).

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

extern __A000H                          ; KERNEL.174 (a selector, as a constant)
extern __AHINCR                         ; KERNEL.114
extern __WINFLAGS                       ; KERNEL.178
extern __0040H                          ; KERNEL.193

segment _DATA
global _vga_sel, _ahincr, _winflags, _hInst, _bios_sel
_vga_sel  dw __A000H
_bios_sel dw __0040H
_ahincr   dw __AHINCR
_winflags dw __WINFLAGS
_hInst    dw 0

extern _vga_init                        ; int __cdecl far vga_init(void)
extern _cur_move                        ; void __cdecl far cur_move(int x, int y)
extern _cur_check                       ; void __cdecl far cur_check(void)
extern _cur_set                         ; void __cdecl far cur_set(void far *shape)

segment ENTRY_TEXT public align=16 class=CODE use16

global vga_libentry
vga_libentry:
        push ds
        push si
        push di
        push es
        mov [_hInst wrt DGROUP], di
        call far _vga_init
        pop es
        pop di
        pop si
        pop ds
        retf

; WEP(nExitType): nothing to free (Disable gave the screen back)
global WEP
WEP:
        mov ax, 1
        retf 2

; void MoveCursor(WORD x, WORD y) - interrupt time (USER's mouse event)
global MOVECURSOR
MOVECURSOR:
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        cld
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 6]              ; y
        push word [bp + 8]              ; x
        call far _cur_move
        add sp, 4
        popa
        pop es
        pop ds
        pop bp
        retf 4

; void CheckCursor(void) - interrupt time (SYSTEM.DRV's timer)
global CHECKCURSOR
CHECKCURSOR:
        push ds
        push es
        pusha
        cld
        mov ax, DGROUP
        mov ds, ax
        call far _cur_check
        popa
        pop es
        pop ds
        mov ax, 1
        retf

; void SetCursor(LPCURSORSHAPE) - from USER's main line; the same care
global SETCURSOR
SETCURSOR:
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 8]              ; selector
        push word [bp + 6]              ; offset
        call far _cur_set
        add sp, 4
        popa
        pop es
        pop ds
        pop bp
        mov ax, 1
        retf 4

; void __cdecl far int10(WORD ax, WORD bx, WORD cx, WORD dx)
global _int10
_int10:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp + 6]
        mov bx, [bp + 8]
        mov cx, [bp + 10]
        mov dx, [bp + 12]
        push bp
        int 10h
        pop bp
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl far outw(WORD port, WORD v), outb(WORD port, BYTE v)
global _outw, _outb, _inb
_outw:
        push bp
        mov bp, sp
        mov dx, [bp + 6]
        mov ax, [bp + 8]
        out dx, ax
        pop bp
        retf
_outb:
        push bp
        mov bp, sp
        mov dx, [bp + 6]
        mov al, [bp + 8]
        out dx, al
        pop bp
        retf
; BYTE __cdecl far inb(WORD port)
_inb:
        push bp
        mov bp, sp
        mov dx, [bp + 6]
        in al, dx
        xor ah, ah
        pop bp
        retf

; void __cdecl far memcpyf(void far *d, const void far *s, WORD n)
global _memcpyf
_memcpyf:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        les di, [bp + 6]
        lds si, [bp + 10]
        mov cx, [bp + 14]
        cld
        rep movsb
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl far memsetf(void far *d, BYTE v, WORD n)
global _memsetf
_memsetf:
        push bp
        mov bp, sp
        push di
        push es
        les di, [bp + 6]
        mov al, [bp + 10]
        mov cx, [bp + 12]
        cld
        rep stosb
        pop es
        pop di
        pop bp
        retf

; BYTE __cdecl far xchg_busy(void): busy set to 1, its old value returned
; (one instruction: the mouse and timer interrupts test it the same way)
extern _busy
global _xchg_busy
_xchg_busy:
        mov al, 1
        xchg al, [_busy wrt DGROUP]
        xor ah, ah
        retf

; WORD __cdecl far irq_off(void): interrupts off, the FLAGS they had
; returned; void __cdecl far irq_restore(WORD): those FLAGS back (the
; DDK's EnterCrit and LeaveCrit: a few instructions around the queue the
; cursor entries share, never cli/sti alone)
global _irq_off, _irq_restore
_irq_off:
        pushf
        pop ax
        cli
        retf
_irq_restore:
        push bp
        mov bp, sp
        push word [bp + 6]
        popf
        pop bp
        retf

; ---- 386 Enhanced mode: the INT 2Fh hook (vga.c scr_switch; the DDK's
; Display Driver Adaptation Guide: hooked with INT 21h 35h/25h when GDI
; calls Enable, given back at Disable). The driver keeps bitmaps in the
; video memory beyond the screen (SaveScreenBitmap), so it calls Enable
; VM-Assisted Save/Restore (4000h) after hooking and 4007h before letting
; go, as the DDK asks of such drivers: the VDD then leaves the video
; memory to the driver, which repaints after a switch back. 4001h, 4002h
; and 4006h (Restore Video Register State) go to scr_switch, then every
; call goes on to the handler that was there before.
extern _scr_switch
segment _DATA
old2f   dd 0
hooked  db 0
segment ENTRY_TEXT

int2f_hook:
        push ax                         ; slot for old2f segment
        push ax                         ; slot for old2f offset
        pushf                           ; preserve caller FLAGS
        cmp ax, 4001h
        je .ours
        cmp ax, 4002h
        je .ours
        cmp ax, 4006h
        jne .chain
.ours:
        push ds
        push es
        pusha
        cld
        mov bx, DGROUP
        mov ds, bx
        push ax
        call far _scr_switch
        add sp, 2
        popa
        pop es
        pop ds
.chain:
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [old2f wrt DGROUP]
        mov [bp + 4], ax
        mov ax, [old2f + 2 wrt DGROUP]
        mov [bp + 6], ax
        pop ax
        pop ds
        pop bp
        popf
        retf                            ; to the old handler, the IRET frame as it came

; void __cdecl far hook2f(void), unhook2f(void)
global _hook2f, _unhook2f
_hook2f:
        cmp byte [hooked wrt DGROUP], 0
        jne .done
        push es
        push bx
        push dx
        mov ax, 352Fh
        int 21h
        mov [old2f wrt DGROUP], bx
        mov [old2f + 2 wrt DGROUP], es
        push ds
        mov ax, cs
        mov ds, ax
        mov dx, int2f_hook
        mov ax, 252Fh
        int 21h
        pop ds
        mov byte [hooked wrt DGROUP], 1
        mov ax, 4000h                   ; Enable VM-Assisted Save/Restore
        int 2Fh
        pop dx
        pop bx
        pop es
.done:
        retf
_unhook2f:
        cmp byte [hooked wrt DGROUP], 0
        je .done
        mov ax, 4007h                   ; Disable VM-Assisted Save/Restore
        int 2Fh
        push dx
        push ds
        lds dx, [old2f wrt DGROUP]
        mov ax, 252Fh
        int 21h
        pop ds
        pop dx
        mov byte [hooked wrt DGROUP], 0
.done:
        retf

; the exports whose interface is not documented and that nothing was seen
; to call (VGREC): DO_POLYLINES (90), DO_SCANLINES (91), and the two
; ordinal-only entries 501 and 502. They do nothing and return 0.
global DO_POLYLINES, DO_SCANLINES, ORD501, ORD502
DO_POLYLINES:
DO_SCANLINES:
ORD501:
ORD502:
        xor ax, ax
        xor dx, dx
        retf
