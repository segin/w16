; tk67s.asm - assembly half of TK67 (tests/apps/tk67.c), the WIN87EM
; audit probe: WIN87EM's entries called with the direction flag set.

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

extern FPMATH                           ; WIN87EM.1
extern FPSAVE                           ; WIN87EM.5

segment _DATA
global _std_ax
_std_ax dw 0

segment _TEXT public align=16 class=CODE use16

; WORD std_fpm(WORD bx, WORD ax): __fpMath with DF set; returns the flags
; it came back with (std_ax: AX)
global _std_fpm
_std_fpm:
        push bp
        mov bp, sp
        push si
        push di
        mov bx, [bp + 6]
        mov ax, [bp + 8]
        std
        call far FPMATH
        pushf
        cld
        mov [_std_ax wrt DGROUP], ax
        pop ax
        pop di
        pop si
        pop bp
        retf

; WORD std_save(void far *buf, WORD n): __Win87EmSave with DF set; returns
; the flags it came back with (std_ax: AX)
global _std_save
_std_save:
        push bp
        mov bp, sp
        push si
        push di
        push word [bp + 8]
        push word [bp + 6]
        push word [bp + 10]
        std
        call far FPSAVE
        pushf
        cld
        mov [_std_ax wrt DGROUP], ax
        pop ax
        pop di
        pop si
        pop bp
        retf

; An exception handler that notes its call in the log at once (lh_note,
; tk67.c), for the cases after which the 3.1 emulator's program ended:
; lh_ax = AX, lh_ds = the word after the return address (the program's
; DS), lh_calls counted. Returns with RETF, every register kept.
extern _lh_note
segment _DATA
global _lh_ax, _lh_ds, _lh_calls, _lh_regds, _lh_ss, _lh_stk
_lh_ax    dw 0
_lh_ds    dw 0
_lh_calls dw 0
_lh_regds dw 0                          ; DS at entry
_lh_ss    dw 0
_lh_stk   times 8 dw 0                  ; from SS:SP at entry (return first)
segment _TEXT
global _lhandler
_lhandler:
        pushf
        push ds
        push es
        pusha
        mov bp, sp                      ; di si bp sp bx dx cx ax es ds fl ip cs
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 14]
        mov [_lh_ax wrt DGROUP], ax
        mov ax, [bp + 26]
        mov [_lh_ds wrt DGROUP], ax
        mov ax, [bp + 18]
        mov [_lh_regds wrt DGROUP], ax
        mov [_lh_ss wrt DGROUP], ss
        xor si, si
.stk:   mov ax, [bp + si + 22]
        mov [_lh_stk + si wrt DGROUP], ax
        add si, 2
        cmp si, 16
        jb .stk
        inc word [_lh_calls wrt DGROUP]
        cld
        call far _lh_note
        popa
        pop es
        pop ds
        popf
        retf
