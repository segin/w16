; tk6fp.asm - assembly half of TK6 (tests/apps/tk6.c), the floating-point
; probe: code sequences that carry each kind of floating-point OS fixup,
; a register-level caller for WIN87EM's register interface (__fpMath),
; direct access to the coprocessor, and an exception handler that records
; how it was called.
;
; The FIxRQQ symbols (tests/apps/fixrqq.asm) are referenced at the
; floating-point instructions; linked for Windows they become NE OS fixups.

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

extern FIWRQQ, FIDRQQ, FIERQQ, FICRQQ, FISRQQ, FIARQQ, FJCRQQ, FJSRQQ, FJARQQ

; ---- fixup sites (never executed) ----
; Each site is 8 bytes; the probe dumps them after loading.
segment FIXSEG public align=16 class=CODE use16
global _fix_sites
_fix_sites:
fx0:    dw 0D99Bh + FIDRQQ              ; FWAIT FLD ST(0)      (FIDRQQ)
        db 0C0h
        times 5 nop
fx1:    dw 0DD9Bh + FIDRQQ              ; FWAIT FLD QWORD [BX] (FIDRQQ)
        db 07h
        times 5 nop
fx2:    dw 9B90h + FIWRQQ               ; NOP FWAIT            (FIWRQQ)
        times 6 nop
fx3:    dw 269Bh + FIERQQ               ; FWAIT ES: FLD DWORD [BX]
        db 0D9h, 07h
        times 4 nop
fx4:    dw 3E9Bh + FIARQQ               ; FWAIT DS: FLD DWORD [BX]
        db 0D9h, 07h
        times 4 nop
fx5:    dw 369Bh + FISRQQ               ; FWAIT SS: FLD DWORD [BX]
        db 0D9h, 07h
        times 4 nop
fx6:    dw 2E9Bh + FICRQQ               ; FWAIT CS: FLD DWORD [BX]
        db 0D9h, 07h
        times 4 nop
fx7:    dw 0DE9Bh + FIDRQQ              ; FWAIT FDIVP ST(1),ST  (DE F9)
        db 0F9h
        times 5 nop
fx_end:

segment _TEXT public align=16 class=CODE use16

; void FAR *fix_site(WORD i): the i-th 8-byte fixup site
global _fix_site
_fix_site:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        shl ax, 3
        add ax, _fix_sites
        mov dx, seg _fix_sites
        pop bp
        retf

; void regcall(FARPROC fn, WORD FAR *r)
; Far-call fn with r[] = {ax, bx, cx, dx, si, di, es} loaded (DS stays
; DGROUP), then store {ax, bx, cx, dx, si, di, es, flags, ds, sp change}
; back into r[0..9]. SP is restored afterwards, so a routine that pops
; arguments it was not given does not break the caller.
global _regcall
_regcall:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov [rc_sp wrt DGROUP], sp
        les bx, [bp + 10]
        mov ax, [es:bx + 0]
        mov cx, [es:bx + 4]
        mov dx, [es:bx + 6]
        mov si, [es:bx + 8]
        mov di, [es:bx + 10]
        push word [es:bx + 2]
        mov es, [es:bx + 12]
        pop bx
        call far [bp + 6]
        pushf
        push ds
        push es
        push bx
        mov bx, DGROUP
        mov ds, bx
        mov bp, [rc_sp wrt DGROUP]      ; frame: bp+0 di, +2 si, +4 ds, +6 bp
        add bp, 6
        les bx, [ss:bp + 10]
        mov [es:bx + 0], ax
        mov [es:bx + 4], cx
        mov [es:bx + 6], dx
        mov [es:bx + 8], si
        mov [es:bx + 10], di
        pop word [es:bx + 2]            ; bx
        pop word [es:bx + 12]           ; es
        pop word [es:bx + 16]           ; ds
        pop word [es:bx + 14]           ; flags
        mov ax, sp
        sub ax, [rc_sp wrt DGROUP]
        mov [es:bx + 18], ax            ; SP after the call - SP before
        mov sp, [rc_sp wrt DGROUP]
        pop di
        pop si
        pop ds
        pop bp
        retf

; ---- coprocessor access through emulator-fixup sequences ----
; Every floating-point instruction below is FWAIT + ESC with an FIDRQQ
; fixup (and FWAIT alone as NOP FWAIT with FIWRQQ), as compiled code has
; them, so the same code works with a coprocessor and with the emulator.
; Operands are the DGROUP variables _fq (qword), _fw (word), _fd (dword).

%macro EMI 1                    ; FWAIT + escape byte %1 (FIDRQQ)
        dw ((%1) << 8 | 9Bh) + FIDRQQ
%endmacro
%macro EMWAIT 0                 ; NOP FWAIT (FIWRQQ)
        dw 9B90h + FIWRQQ
%endmacro

segment _DATA
global _fq, _fw, _fd
_fq     dq 0
_fw     dw 0
_fd     dd 0
segment _TEXT

global _fpu_cw, _fpu_sw, _fpu_setcw, _fpu_init, _fpu_clex
global _fp_ldq, _fp_stpq, _fp_ld1, _fp_ldz, _fp_pop, _fp_fistpd
global _fpu_div0, _fpu_invalid, _fpu_cw_code
; WORD fpu_cw(void): FSTCW
_fpu_cw:
_fpu_cw_code:
        push ds
        mov ax, DGROUP
        mov ds, ax
        EMI 0D9h                        ; fstcw [_fw]
        db 3Eh
        dw _fw wrt DGROUP
        EMWAIT
        mov ax, [_fw wrt DGROUP]
        pop ds
        retf
; WORD fpu_sw(void): FSTSW
_fpu_sw:
        push ds
        mov ax, DGROUP
        mov ds, ax
        EMI 0DDh                        ; fstsw [_fw]
        db 3Eh
        dw _fw wrt DGROUP
        EMWAIT
        mov ax, [_fw wrt DGROUP]
        pop ds
        retf
; void fpu_setcw(WORD cw): FLDCW
_fpu_setcw:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 6]
        mov [_fw wrt DGROUP], ax
        EMI 0D9h                        ; fldcw [_fw]
        db 2Eh
        dw _fw wrt DGROUP
        pop ds
        pop bp
        retf
_fpu_init:
        EMI 0DBh                        ; finit
        db 0E3h
        retf
_fpu_clex:
        EMI 0DBh                        ; fclex
        db 0E2h
        retf
; void fp_ldq(void): FLD QWORD [_fq]
_fp_ldq:
        push ds
        mov ax, DGROUP
        mov ds, ax
        EMI 0DDh
        db 06h
        dw _fq wrt DGROUP
        pop ds
        retf
; void fp_stpq(void): FSTP QWORD [_fq]
_fp_stpq:
        push ds
        mov ax, DGROUP
        mov ds, ax
        EMI 0DDh
        db 1Eh
        dw _fq wrt DGROUP
        EMWAIT
        pop ds
        retf
; void fp_fistpd(void): FISTP DWORD [_fd]
_fp_fistpd:
        push ds
        mov ax, DGROUP
        mov ds, ax
        EMI 0DBh
        db 1Eh
        dw _fd wrt DGROUP
        EMWAIT
        pop ds
        retf
_fp_ld1:
        EMI 0D9h                        ; fld1
        db 0E8h
        retf
_fp_ldz:
        EMI 0D9h                        ; fldz
        db 0EEh
        retf
_fp_pop:
        EMI 0DDh                        ; fstp st0
        db 0D8h
        EMWAIT
        retf
; WORD fpu_div0(void): 1/0, FWAIT (where an unmasked exception is
; reported), pop; returns the status word afterwards
_fpu_div0:
        EMI 0D9h                        ; fld1
        db 0E8h
        EMI 0D9h                        ; fldz
        db 0EEh
        EMI 0DEh                        ; fdivp st1, st
        db 0F9h
        EMWAIT
        EMI 0DDh                        ; fstp st0
        db 0D8h
        EMWAIT
        jmp _fpu_sw
; WORD fpu_invalid(void): sqrt(-1)
_fpu_invalid:
        EMI 0D9h                        ; fld1
        db 0E8h
        EMI 0D9h                        ; fchs
        db 0E0h
        EMI 0D9h                        ; fsqrt
        db 0FAh
        EMWAIT
        EMI 0DDh                        ; fstp st0
        db 0D8h
        EMWAIT
        jmp _fpu_sw

; The exception handler given to __fpMath (BX=3): records registers and
; the top of its stack, counts calls, and returns with RETF.
; h_regs = {ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, flags}
; h_stack = 8 words from SS:SP at entry (return address first)
segment _DATA
global _h_regs, _h_stack, _h_calls, _h_sw, _h_cw
_h_regs  times 12 dw 0
_h_stack times 8 dw 0
_h_calls dw 0
_h_sw    dw 0
_h_cw    dw 0
rc_sp    dw 0
segment _TEXT
global _fp_handler
_fp_handler:
        pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_h_regs + 0 wrt DGROUP], ax
        mov [_h_regs + 2 wrt DGROUP], bx
        mov [_h_regs + 4 wrt DGROUP], cx
        mov [_h_regs + 6 wrt DGROUP], dx
        mov [_h_regs + 8 wrt DGROUP], si
        mov [_h_regs + 10 wrt DGROUP], di
        mov [_h_regs + 12 wrt DGROUP], bp
        pop ax                          ; ds
        mov [_h_regs + 16 wrt DGROUP], ax
        pop ax                          ; flags
        mov [_h_regs + 22 wrt DGROUP], ax
        mov [_h_regs + 14 wrt DGROUP], sp
        mov [_h_regs + 18 wrt DGROUP], es
        mov [_h_regs + 20 wrt DGROUP], ss
        inc word [_h_calls wrt DGROUP]
        EMI 0DDh                        ; fstsw [_h_sw]
        db 3Eh
        dw _h_sw wrt DGROUP
        EMI 0D9h                        ; fstcw [_h_cw]
        db 3Eh
        dw _h_cw wrt DGROUP
        EMWAIT
        push si
        push bx
        mov si, sp
        add si, 4
        xor bx, bx
.st:    mov ax, [ss:si + bx]
        mov [_h_stack + bx wrt DGROUP], ax
        add bx, 2
        cmp bx, 16
        jb .st
        pop bx
        pop si
        push word [_h_regs + 22 wrt DGROUP]
        push word [_h_regs + 16 wrt DGROUP]
        mov ax, [_h_regs + 0 wrt DGROUP]
        pop ds
        popf
        retf

; a second handler that only counts its calls
segment _DATA
global _h2_calls
_h2_calls dw 0
segment _TEXT
global _fp_handler2
_fp_handler2:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        inc word [_h2_calls wrt DGROUP]
        pop ax
        pop ds
        retf
