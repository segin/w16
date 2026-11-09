; tk50s.asm - register-level helpers of TK50/TK51 (tests/apps/tk50.c,
; tk51.c):
;  w_call   call a far Pascal procedure with up to 8 word arguments and
;           known values in every register (ES = the caller's DS unless
;           w_es is set, DS = w_ds if set, carry set unless w_clc); record
;           the registers afterwards in w_regs (ax bx cx dx si di bp ds es
;           flags) and the bytes the callee popped in w_popped
;  rec_cb0, rec_cb2, rec_cb6, rec_cb8: callbacks that record their
;           registers and stack arguments in cb_log and return AX = cb_ret
;           with RETF 0, 2, 6 or 8
;  dos_hook: a DOS procedure for GetSetKernelDOSProc: counts calls in
;           dos_count and jumps to dos_next
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _w_regs, _w_popped, _w_ds, _w_es, _w_bx, _w_clc, _w_ax
global _cb_log, _cb_count, _cb_ret, _dos_count, _dos_next
_w_regs times 10 dw 0
_w_popped dw 0
_w_ds   dw 0
_w_es   dw 0
_w_bx   dw 0                    ; 0: 2222h
_w_clc  dw 0
_w_ax   dw 0                    ; 0: 1111h
w_fn    dd 0
w_bp    dw 0
w_ax_v  dw 0
w_sp    dw 0
CB_MAX  equ 64
CB_SIZE equ 32                  ; 16 words per entry
_cb_count dw 0
_cb_ret dw 0
_cb_log times CB_MAX * CB_SIZE db 0
_dos_count dw 0
_dos_next dd 0
segment _TEXT public align=16 class=CODE use16

; void __cdecl __far w_call(void __far *fn, int nargs, u16 a0, ... a7)
; Pascal: a0 is pushed first.
global _w_call
_w_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp + 6]
        mov [w_fn wrt DGROUP], ax
        mov ax, [bp + 8]
        mov [w_fn + 2 wrt DGROUP], ax
        mov cx, [bp + 10]
        lea si, [bp + 12]
.push:  jcxz .go
        push word [ss:si]
        add si, 2
        dec cx
        jmp .push
.go:    mov ax, [_w_ax wrt DGROUP]
        or ax, ax
        jnz .ax
        mov ax, 1111h
.ax:    mov [w_ax_v wrt DGROUP], ax
        push cs
        push word .ret
        push word [w_fn + 2 wrt DGROUP]
        push word [w_fn wrt DGROUP]
        mov ax, [_w_ds wrt DGROUP]
        or ax, ax
        jnz .ds
        mov ax, ds
.ds:    push ax
        push word [_w_clc wrt DGROUP]
        mov ax, [_w_es wrt DGROUP]
        or ax, ax
        jnz .es
        mov ax, ds
.es:    mov es, ax
        mov [w_bp wrt DGROUP], bp
        lea ax, [bp - 8]
        mov [w_sp wrt DGROUP], ax       ; SP before the arguments
        mov bx, [_w_bx wrt DGROUP]
        or bx, bx
        jnz .bx
        mov bx, 2222h
.bx:    mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        mov bp, 7777h
        pop ax                          ; w_clc
        or ax, ax
        jnz .clc
        pop ds
        mov ax, [ss:w_ax_v wrt DGROUP]
        stc
        retf                            ; to the target
.clc:   pop ds
        mov ax, [ss:w_ax_v wrt DGROUP]
        clc
        retf
.ret:   pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [_w_regs wrt DGROUP]
        pop word [_w_regs + 14 wrt DGROUP]
        pop word [_w_regs + 18 wrt DGROUP]
        mov [_w_regs + 2 wrt DGROUP], bx
        mov [_w_regs + 4 wrt DGROUP], cx
        mov [_w_regs + 6 wrt DGROUP], dx
        mov [_w_regs + 8 wrt DGROUP], si
        mov [_w_regs + 10 wrt DGROUP], di
        mov [_w_regs + 12 wrt DGROUP], bp
        mov [_w_regs + 16 wrt DGROUP], es
        ; bytes left on the stack by the callee: SP should be back at
        ; w_sp less what it did not pop
        mov ax, sp
        sub ax, [w_sp wrt DGROUP]
        neg ax
        mov [_w_popped wrt DGROUP], ax  ; 0: all popped; else bytes left
        mov bp, [w_bp wrt DGROUP]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; record one callback: [sp] = return address of the hook's caller
%macro REC 2
global %1
%1:
        push bp
        mov bp, sp
        push ds
        push di
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov di, [_cb_count wrt DGROUP]
        cmp di, CB_MAX
        jae %%full
        inc word [_cb_count wrt DGROUP]
        shl di, 5
        add di, _cb_log
        pop ax
        push ax
        mov word [di], %2
        mov [di + 2], ax
        mov [di + 4], bx
        mov [di + 6], cx
        mov [di + 8], dx
        mov [di + 10], si
        mov ax, [bp - 4]        ; DI
        mov [di + 12], ax
        mov ax, [bp - 2]        ; DS
        mov [di + 14], ax
        mov [di + 16], es
        mov ax, [bp + 6]
        mov [di + 18], ax
        mov ax, [bp + 8]
        mov [di + 20], ax
        mov ax, [bp + 10]
        mov [di + 22], ax
        mov ax, [bp + 12]
        mov [di + 24], ax
        mov ax, [bp + 2]        ; caller
        mov [di + 26], ax
        mov ax, [bp + 4]
        mov [di + 28], ax
        mov [di + 30], ss
%%full:
        pop ax
        mov ax, [_cb_ret wrt DGROUP]
        pop di
        pop ds
        pop bp
        retf %2
%endmacro

        REC _rec_cb0, 0
        REC _rec_cb2, 2
        REC _rec_cb6, 6
        REC _rec_cb8, 8

; GetSetKernelDOSProc's procedure: count, then on to the previous one
; with every register and the flags as they came
global _dos_hook
_dos_hook:
        push ax                 ; room for the far target
        push ax
        pushf
        push bp
        mov bp, sp              ; [bp+2] flags, [bp+4] target
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        inc word [_dos_count wrt DGROUP]
        cmp word [_dos_next + 2 wrt DGROUP], 0
        je .plain
        mov ax, [_dos_next wrt DGROUP]
        mov [bp + 4], ax
        mov ax, [_dos_next + 2 wrt DGROUP]
        mov [bp + 6], ax
        pop ax
        pop ds
        pop bp
        popf
        retf
.plain: pop ax                  ; no previous procedure known yet: DOS
        pop ds
        pop bp
        popf
        add sp, 4
        int 21h
        retf 2

        REC _rec_cb10, 10

; an exported procedure with the prologue of an exported Windows
; procedure (push ds / pop ax / nop / inc bp / push bp / mov bp,sp / push
; ds / mov ds,ax), for PatchCodeHandle; returns AX = the DS it ran with
global PATCHME
PATCHME:
        push ds
        pop ax
        nop
        inc bp
        push bp
        mov bp, sp
        push ds
        mov ds, ax
        mov ax, ds
        pop ds
        pop bp
        dec bp
        retf

; void __cdecl __far sst_test(void __far *to, void __far *back, u16 seg,
;                             u16 sp, u16 top)
; SwitchStackTo(seg, sp, top), record, SwitchStackBack(), record:
; sst_regs = after To: ss sp bp ds ax dx bx cx; after Back: the same;
; sst_hdr = the new stack segment's first 16 bytes and our DGROUP's first
; 16 bytes while switched
segment _DATA
global _sst_regs, _sst_hdr
_sst_regs times 16 dw 0
_sst_hdr times 16 dw 0
sst_to  dd 0
sst_back dd 0
sst_ss  dw 0
sst_sp  dw 0
segment _TEXT
global _sst_test
_sst_test:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp + 6]
        mov [sst_to wrt DGROUP], ax
        mov ax, [bp + 8]
        mov [sst_to + 2 wrt DGROUP], ax
        mov ax, [bp + 10]
        mov [sst_back wrt DGROUP], ax
        mov ax, [bp + 12]
        mov [sst_back + 2 wrt DGROUP], ax
        mov [sst_ss wrt DGROUP], ss
        push bp
        mov [sst_sp wrt DGROUP], sp
        push word [bp + 14]
        push word [bp + 16]
        push word [bp + 18]
        mov bx, 2222h
        mov cx, 3333h
        call far [sst_to wrt DGROUP]
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_sst_regs + 8 wrt DGROUP], ax
        pop ax
        mov [_sst_regs + 6 wrt DGROUP], ax
        mov [_sst_regs + 0 wrt DGROUP], ss
        mov [_sst_regs + 2 wrt DGROUP], sp
        mov [_sst_regs + 4 wrt DGROUP], bp
        mov [_sst_regs + 10 wrt DGROUP], dx
        mov [_sst_regs + 12 wrt DGROUP], bx
        mov [_sst_regs + 14 wrt DGROUP], cx
        push ds                 ; the headers: new SS:0, our DGROUP:0
        pop es
        mov di, _sst_hdr
        push ds
        push ss
        pop ds
        xor si, si
        mov cx, 8
        cld
        rep movsw
        pop ds
        xor si, si
        mov cx, 8
        rep movsw
        mov ax, 1111h
        mov dx, 4444h
        mov bx, 2222h
        mov cx, 3333h
        call far [sst_back wrt DGROUP]
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_sst_regs + 24 wrt DGROUP], ax
        pop ax
        mov [_sst_regs + 22 wrt DGROUP], ax
        mov [_sst_regs + 16 wrt DGROUP], ss
        mov [_sst_regs + 18 wrt DGROUP], sp
        mov [_sst_regs + 20 wrt DGROUP], bp
        mov [_sst_regs + 26 wrt DGROUP], dx
        mov [_sst_regs + 28 wrt DGROUP], bx
        mov [_sst_regs + 30 wrt DGROUP], cx
        mov ss, [sst_ss wrt DGROUP]     ; in case it did not come back
        mov sp, [sst_sp wrt DGROUP]
        pop bp
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
