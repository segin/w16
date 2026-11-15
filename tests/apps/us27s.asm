; us27s.asm - the fault hooks of probe US27 (tests/apps/us27.c): note each
; fault a case's call causes and, for one KERNEL will not recover through
; a module's "__GP" table (Windows 3.1 would end the task with an
; Application Error there), throw back to the probe's case loop instead.
;
; The probe hooks exceptions 0Bh (not present), 0Ch (stack) and 0Dh
; (general protection) itself with DPMI 0203h while it runs, in front of
; KERNEL's handlers. (TOOLHELP's InterruptRegister is no help: it is not
; given 0Bh at all, Schulman ch. 10, and on 3.1 a fault in KERNEL's code
; reached KERNEL's Application Error without reaching the callback.)
; While a case is armed, a fault comes here first:
;   - 0Bh whose error code is not the case's not-present selector (np_sel)
;     is KERNEL loading a segment: passed on;
;   - otherwise it is noted (flt_n, flt_int, flt_cs, flt_ip) and KERNEL is
;     asked, with HasGPHandler (KERNEL.338) for the faulting address only,
;     as its own fault handler does, whether it recovers it: then it is
;     passed on (flt_rec 1), else the fault returns to throw_stub, which
;     throws back to the case loop (us27_throw). HasGPHandler's result
;     decides that and is never logged.
; The DPMI exception frame (16-bit), after the 4 bytes reserved for the
; next handler's address and BP: [bp+6] the host's return address, [bp+0Ah]
; the error code, [bp+0Ch] IP, [bp+0Eh] CS, [bp+10h] flags, [bp+12h] SP,
; [bp+14h] SS. Returning to the host with a changed CS:IP resumes there.
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

extern HASGPHANDLER             ; KERNEL.338
extern _us27_throw              ; void __cdecl __far us27_throw(void)

segment _DATA
global _flt_n, _flt_int, _flt_cs, _flt_ip, _flt_rec, _armed, _np_sel
_flt_n   dw 0                   ; faults seen in the current case
_flt_int dw 0                   ; the last one's exception number,
_flt_cs  dw 0                   ; CS and IP
_flt_ip  dw 0
_flt_rec dw 0                   ; 1: KERNEL recovers it
_armed   dw 0                   ; 1 while a case runs
_np_sel  dw 0                   ; the case's not-present selector | 7
old_0b   dd 0
old_0c   dd 0
old_0d   dd 0
cur_exc  dw 0

segment US27S_TEXT public align=16 class=CODE use16
global _flt_install, _flt_remove

; void __cdecl __far flt_install(void)
_flt_install:
        push ds
        push si
        push di
        mov ax, seg _flt_n
        mov ds, ax
        mov bl, 0Bh
        mov si, old_0b
        mov di, h_0b
        call hook
        mov bl, 0Ch
        mov si, old_0c
        mov di, h_0c
        call hook
        mov bl, 0Dh
        mov si, old_0d
        mov di, h_0d
        call hook
        pop di
        pop si
        pop ds
        retf
hook:                                   ; BL exception, SI old vector, DI handler
        push bx
        mov ax, 0202h
        int 31h
        mov [si], dx
        mov [si + 2], cx
        pop bx
        mov ax, 0203h
        mov cx, cs
        mov dx, di
        int 31h
        ret

; void __cdecl __far flt_remove(void)
_flt_remove:
        push ds
        mov ax, seg _flt_n
        mov ds, ax
        mov bl, 0Bh
        mov dx, [old_0b wrt DGROUP]
        mov cx, [old_0b + 2 wrt DGROUP]
        mov ax, 0203h
        int 31h
        mov bl, 0Ch
        mov dx, [old_0c wrt DGROUP]
        mov cx, [old_0c + 2 wrt DGROUP]
        mov ax, 0203h
        int 31h
        mov bl, 0Dh
        mov dx, [old_0d wrt DGROUP]
        mov cx, [old_0d + 2 wrt DGROUP]
        mov ax, 0203h
        int 31h
        pop ds
        retf

%macro entry 2                          ; exception, old vector
h_%1:
        sub sp, 4                       ; room for the next handler's address
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, seg _flt_n
        mov ds, ax
        mov ax, [%2 wrt DGROUP]
        mov [bp + 2], ax
        mov ax, [%2 + 2 wrt DGROUP]
        mov [bp + 4], ax
        mov word [cur_exc wrt DGROUP], %1h
        jmp flt_common
%endmacro
        entry 0b, old_0b
        entry 0c, old_0c
        entry 0d, old_0d

flt_common:
        cmp word [_armed wrt DGROUP], 0
        je .chain
        cmp word [cur_exc wrt DGROUP], 0Bh
        jne .ours
        mov ax, [bp + 0Ah]              ; not present: the case's selector?
        or ax, 7
        cmp ax, [_np_sel wrt DGROUP]
        jne .chain
.ours:
        inc word [_flt_n wrt DGROUP]
        mov ax, [cur_exc wrt DGROUP]
        mov [_flt_int wrt DGROUP], ax
        mov ax, [bp + 0Ch]
        mov [_flt_ip wrt DGROUP], ax
        mov ax, [bp + 0Eh]
        mov [_flt_cs wrt DGROUP], ax
        pusha
        push es
        push word [bp + 0Eh]            ; HasGPHandler(CS:IP)
        push word [bp + 0Ch]
        call far HASGPHANDLER
        neg ax                          ; 1 if nonzero, else 0
        sbb ax, ax
        neg ax
        mov [_flt_rec wrt DGROUP], ax
        pop es
        popa
        cmp word [_flt_rec wrt DGROUP], 0
        jne .chain                      ; KERNEL recovers it
        mov word [_armed wrt DGROUP], 0
        mov word [bp + 0Ch], throw_stub
        mov word [bp + 0Eh], cs
        pop ax
        pop ds
        pop bp
        add sp, 4
        retf                            ; to the host: resume at throw_stub
.chain:
        pop ax
        pop ds
        pop bp
        retf                            ; to the next handler

throw_stub:
        mov ax, seg _flt_n
        mov ds, ax
        call far _us27_throw            ; does not return
