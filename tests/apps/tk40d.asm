; tk40d.asm - PENWIN.DLL for TK40 (tests/apps/tk40.c): a stand-in with the
; module name PENWIN, one of the module names whose "__GP" table
; KERNEL's fault handler consults (Schulman, Undocumented Windows ch. 5,
; HasGPHandler; PENWIN is not loaded by the reference install), to see
; in what state a "__GP" recovery routine is entered.
;
; GPFAULT (far pascal, no arguments) sets known register values, pushes
; two marker words, and loads ES from BADSEL (an invalid selector the
; caller set with SETBADSEL): that instruction lies in the range of the
; exported __GP table, whose recovery routine records every register,
; the flags and 8 words at SS:SP into RESULTS, then returns to
; GPFAULT's caller from the frame GPFAULT saved.
;
;
; KCALL(err) calls K327 from inside a second range, to see whether K327
; goes to the range's recovery routine.
;
; RESULTS: ax bx cx dx si di bp sp ds es ss flags, stk[8], recovered,
; nofault, sp_at_fault (SP just before the faulting instruction), cs_seen,
; ES before the fault
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA _BSS

segment _NULL
        times 16 db 0

segment _DATA
global RESULTS, __GP, BADSEL
RESULTS:
r_ax    dw 0
r_bx    dw 0
r_cx    dw 0
r_dx    dw 0
r_si    dw 0
r_di    dw 0
r_bp    dw 0
r_sp    dw 0
r_ds    dw 0
r_es    dw 0
r_ss    dw 0
r_fl    dw 0
r_stk   times 8 dw 0
r_rec   dw 0
r_nof   dw 0
r_spf   dw 0
r_cs    dw 0
r_esb   dw 0                    ; ES just before the fault
BADSEL  dw 0
saved_ss dw 0
saved_sp dw 0
kc_mode  dw 0                  ; 1 while in KCALL
; { code selector, low, high, recovery }, then a 0 selector
__GP:
        dw seg gp_lo, gp_lo, gp_hi, recover
        dw seg kc_lo, kc_lo, kc_hi, recover
        dw 0, 0, 0, 0

segment _TEXT public align=16 class=CODE use16
extern K327                     ; KERNEL.327 (HandleParamError)

global libentry
libentry:
        mov ax, 1
        retf

global WEP
WEP:    mov ax, 1
        retf 2

; void FAR PASCAL SETBADSEL(WORD sel)
global SETBADSEL
SETBADSEL:
        push bp
        mov bp, sp
        push ds
        mov ax, seg BADSEL
        mov ds, ax
        mov ax, [bp + 6]
        mov [BADSEL], ax
        pop ds
        pop bp
        retf 2

; void FAR PASCAL GPFAULT(void)
global GPFAULT
GPFAULT:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, seg saved_ss
        mov ds, ax
        mov [saved_ss], ss
        mov [saved_sp], sp
        mov word [r_rec], 0
        mov word [r_nof], 0
        push 0AAAAh
        push 0BBBBh
        mov [r_spf], sp
        mov [r_esb], es
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        mov bp, 7777h
gp_lo:  mov es, [BADSEL]
gp_hi:  mov word [r_nof], 1
        jmp done
recover:
        push ds
        push ax
        mov ax, seg r_ax
        mov ds, ax
        pop ax
        mov [r_ax], ax
        pop ax
        mov [r_ds], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_si], si
        mov [r_di], di
        mov [r_bp], bp
        mov [r_sp], sp
        mov [r_es], es
        mov [r_ss], ss
        pushf
        pop ax
        mov [r_fl], ax
        mov [r_cs], cs
        mov si, sp
        mov cx, 8
        mov di, r_stk
.c:     mov ax, [ss:si]
        mov [di], ax
        add si, 2
        add di, 2
        loop .c
        inc word [r_rec]
done:   mov ax, seg saved_ss
        mov ds, ax
        mov ss, [saved_ss]
        mov sp, [saved_sp]
        cmp word [kc_mode], 0
        jne .kc
        pop di
        pop si
        pop ds
        pop bp
        retf
.kc:    mov word [kc_mode], 0
        pop di
        pop si
        pop ds
        pop bp
        dec bp
        retf 2

; void FAR PASCAL KCALL(WORD err): calls K327 with BX = err from inside
; the second __GP range (its return address lies in it); the recovery
; routine records the state if K327 goes there
global KCALL
KCALL:
        inc bp                          ; a far frame, as Windows' APIs have
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov bx, [bp + 6]
        mov ax, seg saved_ss
        mov ds, ax
        mov [saved_ss], ss
        mov [saved_sp], sp
        mov word [r_rec], 0
        mov word [r_nof], 0
        mov word [kc_mode], 1
        push 0AAAAh
        push 0BBBBh
        mov [r_spf], sp
        mov [r_esb], es
        mov ax, 1111h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
kc_lo:  call far K327
        nop
kc_hi:  times 64 inc si                 ; a sled: SI - 5555h tells where it came back
        mov [r_si], si
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_sp], sp
        mov word [r_nof], 1
        jmp done
