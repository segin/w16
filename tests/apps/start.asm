; start.asm - shared start-up code for the KERNEL test programs (TK*).
;
; A minimal Win16 application start-up (no C runtime): record the registers
; KERNEL hands to a new task, call InitTask, record its results, run the
; tests in tmain(), and exit through INT 21h AH=4Ch with the failure count.

        bits 16
        cpu 286

; DGROUP starts with 16 bytes reserved for the Windows instance data.
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern INITTASK                 ; KERNEL.91
extern DOS3CALL                 ; KERNEL.102
extern __AHINCR                 ; KERNEL.114 (constant)
extern __0040H                  ; KERNEL.193 (constant selector)
extern _tmain                   ; int __cdecl __far tmain(void)

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

; u16 get_ahincr(void), u16 get_0040h(void): values of KERNEL constants
global _get_ahincr, _get_0040h
_get_ahincr:
        mov ax, __AHINCR
        retf
_get_0040h:
        mov ax, __0040H
        retf

; u16 dos3call_getver(void): DOS version (AL major, AH minor) via DOS3Call
global _dos3call_getver
_dos3call_getver:
        mov ah, 30h
        call far DOS3CALL
        retf

; Log file shared by w16 and the reference Windows (where a task's stdout
; and OutputDebugString are not visible): every test writes TK.LOG in the
; current directory through plain INT 21h.
segment _DATA
logname db "TK.LOG", 0
loghandle dw 0FFFFh

segment _TEXT
; void log_write(const char far *buf, u16 len)
global _log_write
_log_write:
        push bp
        mov bp, sp
        push ds
        mov bx, [loghandle wrt DGROUP]
        cmp bx, 0FFFFh
        jne .open
        mov ah, 3Ch                     ; create (truncate)
        xor cx, cx
        mov dx, logname wrt DGROUP
        int 21h
        jc .done
        mov [loghandle wrt DGROUP], ax
        mov bx, ax
.open:
        mov cx, [bp + 10]
        lds dx, [bp + 6]
        mov ah, 40h
        int 21h
        pop ds
        push ds
        mov ah, 68h                     ; commit, so the log survives a crash
        int 21h
.done:
        pop ds
        pop bp
        retf

; u32 call_ds(u16 ds, void far *fn, u16 nargs, u16 a1, u16 a2, u16 a3)
; Far-call the Pascal function fn with DS = ds and its first nargs (0-3)
; word arguments a1..a3, as a program does to use a local heap in another
; segment. Returns DX:AX.
global _call_ds
_call_ds:
        push bp
        mov bp, sp
        push ds
        push si
        mov cx, [bp + 12]               ; nargs
        lea si, [bp + 14]               ; a1
.arg:
        jcxz .call
        push word [ss:si]
        add si, 2
        dec cx
        jmp .arg
.call:
        mov ds, [bp + 6]
        call far [ss:bp + 8]
        pop si
        pop ds
        pop bp
        retf

; int dos_int21(WORD far *r): INT 21h with r[] = {ax, bx, cx, dx, si, di,
; ds, es} in and out. Returns the carry flag (1 = error).
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
