; win87em.asm - WIN87EM.DLL: floating-point support for Win16 programs.
;
; Exports (ordinals as observed on Windows 3.1, docs/exports/WIN87EM.txt):
;   1 __FPMATH          register interface, function number in BX
;   2 (no name)         UNVERIFIED: purpose unknown; a stub here
;   3 __WIN87EMINFO     (void far *buf, WORD size), Pascal
;   4 __WIN87EMRESTORE  (void far *buf, WORD size), Pascal
;   5 __WIN87EMSAVE     (void far *buf, WORD size), Pascal
;
; Behaviour observed black-box on Windows 3.1 (Standard and 386 Enhanced
; mode agree) with tests/apps/tk6.c, in DOSBox-X with a coprocessor ("87")
; and without one ("em", the emulator); ARCHITECTURE.md 9.2 lists the
; findings. Without a coprocessor the loader turns floating-point
; instructions into INT 34h-3Dh (kernel/loader.c); this DLL then installs
; the emulator (emu.c) on those vectors. With one, the coprocessor runs
; them, and only the register interface below and Save/Restore are needed.

        bits 16
        cpu 386                         ; 387 instructions: only with have87

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

extern GETWINFLAGS                      ; KERNEL.132
extern _emu_entry                       ; void __cdecl far emu_entry(emu_frame far *)
extern _emu_fpmath                      ; void __cdecl far emu_fpmath(u16 fn, fpm_regs far *)
extern _emu_save                        ; void __cdecl far emu_save(void far *)
extern _emu_restore                     ; void __cdecl far emu_restore(void far *)
extern _emu_reset                       ; void __cdecl far emu_reset(void)

WF_80x87        equ 0400h
SAVE_SIZE       equ 01CDh               ; observed (__WIN87EMINFO word 1)
FSAVE_SIZE      equ 94                  ; FSAVE image, 16-bit protected mode
INFO_SIZE       equ 12
CW_INIT         equ 1332h               ; observed after function 0
CW_HWMASK       equ 0FF3Ch              ; function 4: IM and DM are unmasked
                                        ; (bits 6, 7 reserved)

segment _DATA
global _have87, _user_cw, _handler, _fn12, _em_acc
_have87         dw 0
_user_cw        dw CW_INIT              ; the control word function 4 got
_handler        dd 0                    ; function 3: exception handler
_fn12           dw 0                    ; function 12: AX kept (meaning unknown)
_em_acc         dw 0                    ; emulator: exceptions since fn 9/0
old_vec         times 2 * (3Eh - 34h + 1) dw 0
hooked          dw 0
tmp             dw 0
tmp32           dd 0
env             times 7 dw 0            ; FSTENV image (16-bit)

segment _TEXT

; ---- initialisation and WEP ----

global libentry
libentry:
        push ds
        mov ax, DGROUP
        mov ds, ax
        call far GETWINFLAGS
        and ax, WF_80x87
        jz .noco
        mov word [_have87 wrt DGROUP], 1
        jmp .done
.noco:
        call far _emu_reset
        call hook_vectors
.done:
        mov ax, 1
        pop ds
        retf

global WEP
WEP:
        push ds
        mov ax, DGROUP
        mov ds, ax
        cmp word [hooked wrt DGROUP], 0
        je .ret
        call unhook_vectors
.ret:
        mov ax, 1
        pop ds
        retf 2

; INT 34h-3Eh -> the emulator, without a coprocessor. DS = DGROUP.
hook_vectors:
        push si
        push di
        mov bl, 34h
        mov si, old_vec
        mov di, stubs
.next:
        mov ax, 0204h
        int 31h
        mov [si], dx
        mov [si + 2], cx
        mov ax, 0205h
        mov cx, cs
        mov dx, di
        int 31h
        add si, 4
        add di, STUB_LEN
        inc bl
        cmp bl, 3Eh
        jbe .next
        mov word [hooked wrt DGROUP], 1
        pop di
        pop si
        ret

unhook_vectors:
        push si
        mov bl, 34h
        mov si, old_vec
.next:
        mov dx, [si]
        mov cx, [si + 2]
        mov ax, 0205h
        int 31h
        add si, 4
        inc bl
        cmp bl, 3Eh
        jbe .next
        mov word [hooked wrt DGROUP], 0
        pop si
        ret

; One stub per vector: push the interrupt number, go to emu_common.
STUB_LEN        equ 5
stubs:
%assign n 34h
%rep 3Eh - 34h + 1
        push byte n                     ; 2 bytes (6A ib)
        jmp near emu_common             ; 3 bytes
%assign n n + 1
%endrep

; Frame for emu_entry (emu.c, emu_frame), from SS:SP upwards:
;   es ds di si bp sp bx dx cx ax intno ip cs flags
emu_common:
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov ax, sp
        push ss
        push ax
        call far _emu_entry
        add sp, 4
        pop es
        pop ds
        popa
        add sp, 2                       ; intno
        iret

; ---- __FPMATH ----
; In: BX = function, AX/DX as the function needs. Out: AX (DX for 7),
; CF as observed; BX = 2 * function (as observed); CX = our DS; SI, DI,
; BP, DS kept. Functions 0-12 observed; others do nothing (UNVERIFIED).
global __FPMATH
__FPMATH:
        push ds
        push si
        push di
        push bp
        mov si, DGROUP
        mov ds, si
        cmp bx, 12
        ja fpm_none
        shl bx, 1
        cmp word [_have87 wrt DGROUP], 0
        je fpm_emu
        jmp [cs:fpm_table + bx]

fpm_table:
        dw fpm_init, fpm_init1, fpm_init1, fpm_handler, fpm_setcw
        dw fpm_getcw, fpm_round, fpm_pop32, fpm_status, fpm_clear
        dw fpm_depth, fpm_have87, fpm_12

fpm_ret_cf0:
        clc
fpm_ret:
        mov cx, ds
        pop bp
        pop di
        pop si
        pop ds
        retf
fpm_ret_cf1:
        stc
        jmp fpm_ret

fpm_none:
        jmp fpm_ret_cf0

; 0: initialise: FINIT, CW 1332h (the coprocessor gets 1330h); AX = 0.
; It does not change the exception handler (observed).
fpm_init:
        call init87
        xor ax, ax
        jmp fpm_ret_cf0
; 1, 2: the same, with CF = 1 (observed; 2 is what OpenWatcom's run-time
; calls at exit).
fpm_init1:
        call init87
        xor ax, ax
        jmp fpm_ret_cf1

init87:
        fninit
        mov word [_user_cw wrt DGROUP], CW_INIT
        mov word [tmp wrt DGROUP], CW_INIT & CW_HWMASK
        fldcw [tmp wrt DGROUP]
        mov word [_em_acc wrt DGROUP], 0
        ret

; 3: DX:AX = exception handler. AX = 253Eh afterwards (observed).
fpm_handler:
        mov [_handler wrt DGROUP], ax
        mov [_handler + 2 wrt DGROUP], dx
        mov ax, 253Eh
        jmp fpm_ret_cf0

; 4: set the control word AX; the coprocessor gets AX & FF3Ch (invalid and
; denormal unmasked); returns AX & FF3Ch. Function 5 returns AX itself.
fpm_setcw:
        mov [_user_cw wrt DGROUP], ax
        and ax, CW_HWMASK
        mov [tmp wrt DGROUP], ax
        fldcw [tmp wrt DGROUP]
        jmp fpm_ret_cf0

; 5: AX = the control word last given to function 4 (or 1332h after 0),
; not the coprocessor's (observed after an FLDCW behind its back).
fpm_getcw:
        mov ax, [_user_cw wrt DGROUP]
        jmp fpm_ret_cf0

; 6: round ST to an integer with the rounding control AX & 0C00h; returns
; the control word used (the coprocessor's, with that rounding control).
fpm_round:
        call cw_round
        frndint
        fldcw [tmp wrt DGROUP]
        mov ax, [env wrt DGROUP]
        jmp fpm_ret_cf0

; tmp = current CW; env[0] = CW with rounding control AX & 0C00h, loaded
cw_round:
        fnstcw [tmp wrt DGROUP]
        fwait
        and ax, 0C00h
        mov cx, [tmp wrt DGROUP]
        and cx, ~0C00h
        or ax, cx
        mov [env wrt DGROUP], ax
        fldcw [env wrt DGROUP]
        ret

; 7: pop ST into DX:AX as a 32-bit integer, rounded with AX & 0C00h
; (observed: 74565.75 with AX 5555h (down) -> 74565, -3.5 -> -4 with
; nearest, -3 with chop; out of range: 8000:0000).
fpm_pop32:
        call cw_round
        fistp dword [tmp32 wrt DGROUP]
        fldcw [tmp wrt DGROUP]
        fwait
        mov ax, [tmp32 wrt DGROUP]
        mov dx, [tmp32 + 2 wrt DGROUP]
        jmp fpm_ret_cf0

; 8: AX = the exception flags of the status word (not cleared).
; UNVERIFIED: whether SF and ES (bits 6, 7) are included.
fpm_status:
        fnstsw ax
        and ax, 3Fh
        jmp fpm_ret_cf0

; 9: clear the exceptions; AX = 0, CF = 1.
fpm_clear:
        fnclex
        mov word [_em_acc wrt DGROUP], 0
        xor ax, ax
        jmp fpm_ret_cf1

; 10: AX = the number of registers in use (tag not 11b).
fpm_depth:
        fnstenv [env wrt DGROUP]
        fldcw [env wrt DGROUP]          ; FSTENV masks all exceptions
        fwait
        mov dx, [env + 4 wrt DGROUP]    ; tag word
        xor ax, ax
        mov cx, 8
.tag:
        mov bx, dx
        and bx, 3
        cmp bx, 3
        je .empty
        inc ax
.empty:
        shr dx, 2
        loop .tag
        mov bx, 10 * 2
        jmp fpm_ret_cf0

; 11: AX = 1 with a coprocessor, 0 without; DX kept.
fpm_have87:
        mov ax, [_have87 wrt DGROUP]
        jmp fpm_ret_cf0

; 12: keeps AX (returned unchanged; meaning unknown, UNVERIFIED).
fpm_12:
        mov [_fn12 wrt DGROUP], ax
        jmp fpm_ret_cf0

; Without a coprocessor: emu_fpmath(BX / 2, regs) with regs = {ax, dx,
; flags} in and out.
fpm_emu:
        push bx
        pushf
        push dx
        push ax
        mov ax, sp
        push ss
        push ax
        shr bx, 1
        push bx
        call far _emu_fpmath
        add sp, 6
        pop ax
        pop dx
        popf
        pop bx
        mov cx, ds
        pop bp
        pop di
        pop si
        pop ds
        retf

; ---- ordinal 2 ----
; UNVERIFIED: unnamed on Windows 3.1 and not observed; does nothing.
global WIN87EM_2
WIN87EM_2:
        retf

; ---- __WIN87EMINFO(buf, size) ----
; Observed: size < 12 -> FFFFh, nothing written; else 12 bytes: 0600h,
; the save area size (01CDh), WIN87EM's data and code selectors, 1 with
; a coprocessor (0 without), 0; returns 0.
global __WIN87EMINFO
__WIN87EMINFO:
        push bp
        mov bp, sp
        push es
        push di
        mov ax, 0FFFFh
        cmp word [bp + 6], INFO_SIZE
        jb .ret
        les di, [bp + 8]
        cld
        mov ax, 0600h
        stosw
        mov ax, SAVE_SIZE
        stosw
        mov ax, DGROUP
        stosw
        mov ax, cs
        stosw
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_have87 wrt DGROUP]
        pop ds
        stosw
        xor ax, ax
        stosw
.ret:
        pop di
        pop es
        pop bp
        retf 6

; ---- __WIN87EMSAVE(buf, size), __WIN87EMRESTORE(buf, size) ----
; Observed: size < 01CDh -> FFFFh, nothing done; else 0. With a
; coprocessor the area starts with an FSAVE image (94 bytes; FSAVE
; reinitialises the coprocessor, as observed); without one those bytes
; are not written. WIN87EM's own state follows (w16's layout, not the
; original's; programs treat the area as opaque).
global __WIN87EMSAVE
__WIN87EMSAVE:
        push bp
        mov bp, sp
        push ds
        mov ax, 0FFFFh
        cmp word [bp + 6], SAVE_SIZE
        jb .ret
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 10]
        push word [bp + 8]
        cmp word [_have87 wrt DGROUP], 0
        je .emu
        push es
        push bx
        les bx, [bp + 8]
        fnsave [es:bx]
        fwait
        pop bx
        pop es
.emu:
        call far _emu_save
        add sp, 4
        xor ax, ax
.ret:
        pop ds
        pop bp
        retf 6

global __WIN87EMRESTORE
__WIN87EMRESTORE:
        push bp
        mov bp, sp
        push ds
        mov ax, 0FFFFh
        cmp word [bp + 6], SAVE_SIZE
        jb .ret
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 10]
        push word [bp + 8]
        cmp word [_have87 wrt DGROUP], 0
        je .emu
        push es
        push bx
        les bx, [bp + 8]
        frstor [es:bx]
        pop bx
        pop es
.emu:
        call far _emu_restore
        add sp, 4
        xor ax, ax
.ret:
        pop ds
        pop bp
        retf 6

; ---- calling the program's exception handler (emulator) ----
; void call_handler(u16 code, u16 app_ds): far call the handler set by
; function 3 with AX = 0600h | code (code: 81h invalid, 83h zero divide,
; ... as observed: 0683h for 1/0, 0688h for the square root of -1),
; DS = WIN87EM's DGROUP, and the program's DS pushed (OpenWatcom's handler
; takes its DS from there). UNVERIFIED: the meaning of AH = 6.
global _call_handler
_call_handler:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [_handler wrt DGROUP]
        or ax, [_handler + 2 wrt DGROUP]
        jz .none
        push word [bp + 8]
        mov ax, [bp + 6]
        or ax, 0600h
        call far [_handler wrt DGROUP]
        add sp, 2
        mov ax, DGROUP
        mov ds, ax
.none:
        pop di
        pop si
        pop bp
        retf
