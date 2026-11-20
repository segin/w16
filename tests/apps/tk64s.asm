; tk64s.asm - assembly helpers of TK64 (tk64.c, KERNEL's FileCdr) and
; TK64A (tk64a.c).
;
;   cb_entry    the callback TK64 registers: void FAR PASCAL (WORD code,
;               LPSTR path). It loads TK64's DS, hands both to cb_c
;               (tk64.c), then returns cb_ret in AX with BX, CX, DX set
;               to BEEFh and ES to 0 (to see what KERNEL gives back to
;               the program whose INT 21h it was)
;   cb2_entry   a second callback (same convention) that only counts its
;               calls in cb2_count and returns 0
;   dos3call_regs  like start.asm's dos_int21, through DOS3Call
;   ptr_ok      1 if VERR accepts the selector
;   cur_ds      DS

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

extern DOS3CALL                 ; KERNEL.102
extern NOHOOKDOSCALL            ; KERNEL.101
extern _cb_c                    ; void __cdecl __far cb_c(WORD, char __far *)
extern _cb_ret                  ; WORD (tk64.c)

segment _DATA
global _cb2_count
_cb2_count      dw 0

segment _TEXT public align=16 class=CODE use16

global CB_ENTRY
CB_ENTRY:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 8]              ; path (segment)
        push word [bp + 6]              ; path (offset)
        push word [bp + 10]             ; code
        cld
        call far _cb_c
        add sp, 6
        mov ax, [_cb_ret wrt DGROUP]
        mov bx, 0BEEFh
        mov cx, bx
        mov dx, bx
        push 0
        pop es
        pop di
        pop si
        pop ds
        pop bp
        retf 6

global CB2_ENTRY
CB2_ENTRY:
        push ds
        mov ax, DGROUP
        mov ds, ax
        inc word [_cb2_count wrt DGROUP]
        pop ds
        xor ax, ax
        retf 6

; int dos3call_regs(WORD far *r): r[] = {ax, bx, cx, dx, si, di, ds, es}
; in and out; returns the carry flag. nohook_regs: the same through
; NoHookDOSCall (KERNEL.101)
global _dos3call_regs, _nohook_regs
_nohook_regs:
        mov ax, NOHOOKDOSCALL
        mov dx, seg NOHOOKDOSCALL
        jmp regs_call
_dos3call_regs:
        mov ax, DOS3CALL
        mov dx, seg DOS3CALL
regs_call:
        push bp
        mov bp, sp
        push ds
        push es
        push si
        push di
        push dx                         ; [bp-10] the target (segment)
        push ax                         ; [bp-12] (offset)
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
        call far [ss:bp - 12]           ; the target
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
        lea sp, [bp - 8]                ; (the target; keeps the flags)
        pop di
        pop si
        pop es
        pop ds
        pop bp
        retf

; WORD ptr_ok(WORD sel): 1 if the selector can be read (VERR)
global _ptr_ok
_ptr_ok:
        push bp
        mov bp, sp
        xor ax, ax
        verr word [bp + 6]
        jnz .no
        inc ax
.no:
        pop bp
        retf

; WORD cur_ds(void)
global _cur_ds
_cur_ds:
        mov ax, ds
        retf
