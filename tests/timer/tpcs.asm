; tpcs.asm - TPC.DLL's register-level parts (tests/timer/tpc.c): the timer
; callback's entry, which records every register as the timer services
; hand them over before C touches any, and two port helpers.
;
; void FAR PASCAL TpcCallback(UINT id, UINT msg, DWORD dwUser, DWORD dw1,
;                             DWORD dw2)                    (pops 16 bytes)
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

; popfi: the flags pushed before a cli back, interrupts enabled again
; explicitly when they were: in 386 Enhanced mode POPF does not change the
; interrupt flag of Windows' code (it runs below IOPL), and interrupts
; would stay off
%macro popfi 0
        push bp
        mov bp, sp
        test byte [bp + 3], 2
        pop bp
        jz %%n
        sti
%%n:    popf
%endmacro

extern __0040H                  ; KERNEL.193
extern _tpc_cb                  ; void __cdecl __far tpc_cb(WORD __far *frame)

global TPCCALLBACK
TPCCALLBACK:
        push bp
        mov bp, sp
        pushf                           ; [bp-2]
        pusha                           ; [bp-4] AX, -6 CX, -8 DX, -10 BX,
        push ds                         ; -12 SP, -14 BP, -16 SI, -18 DI,
        push es                         ; -20 DS, -22 ES
        mov ax, DGROUP
        mov ds, ax
        cld
        lea ax, [bp - 22]
        push ss
        push ax
        call far _tpc_cb
        add sp, 4
        pop es
        pop ds
        popa
        popfi
        pop bp
        retf 16

; WORD get0040(void): the BIOS data selector
global _get0040
_get0040:
        mov ax, __0040H
        retf

; WORD pit0(void): channel 0's count, latched
global _pit0
_pit0:
        pushf
        cli
        xor al, al
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        popfi
        retf

; WORD getflags(void)
global _getflags
_getflags:
        pushf
        pop ax
        retf

; void enable(void)
global _enable
_enable:
        sti
        retf

; WEP(bSystemExit) -> 1
global WEP
WEP:
        mov ax, 1
        retf 2

; ---- an INT 8 hook in front of whatever is there (the timer driver's
; handler): counts the interrupts, for the callbacks' records and
; TpcIrq(); TpcHook(1) puts it in, TpcHook(0) takes it out
segment _DATA
global _irqcnt, _irqlog, _nirqlog
_irqcnt  dd 0
_nirqlog dw 32                          ; entries in _irqlog (32: full)
_irqlog  times 32 dw 0                  ; channel 0's count as each
                                        ; interrupt comes (TpcArm clears)
prev8    dd 0
hooked   dw 0

segment _TEXT
global TPCHOOK
TPCHOOK:
        push bp
        mov bp, sp
        push ds
        push bx
        push es
        push dx
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 6]
        or ax, ax
        jz .off
        cmp word [hooked], 0
        jne .r
        mov ax, 3508h
        int 21h
        mov [prev8], bx
        mov [prev8 + 2], es
        mov word [hooked], 1
        push ds
        push cs
        pop ds
        mov dx, i8
        mov ax, 2508h
        int 21h
        pop ds
        jmp .r
.off:   cmp word [hooked], 0
        je .r
        mov word [hooked], 0
        push ds
        lds dx, [prev8]
        mov ax, 2508h
        int 21h
        pop ds
.r:     pop dx
        pop es
        pop bx
        pop ds
        pop bp
        retf 2

; void TpcArm(void): log channel 0's count at the next 32 interrupts;
; returns DX:AX = far pointer to the log (32 words)
global TPCARM
TPCARM:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov word [_nirqlog], 0
        mov ax, _irqlog
        mov dx, ds
        pop ds
        retf

; DWORD TpcIrq(void)
global TPCIRQ
TPCIRQ:
        push ds
        mov ax, DGROUP
        mov ds, ax
        pushf
        cli
        mov ax, [_irqcnt]
        mov dx, [_irqcnt + 2]
        popfi
        pop ds
        retf

i8:     sub sp, 4                       ; [bp+6]: where to go on
        push ds
        push ax
        push bp
        mov bp, sp
        mov ax, DGROUP
        mov ds, ax
        add word [_irqcnt], 1
        adc word [_irqcnt + 2], 0
        push bx
        mov bx, [_nirqlog]
        cmp bx, 32
        jae .nolog
        inc word [_nirqlog]
        shl bx, 1
        xor al, al
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        mov [_irqlog + bx], ax
.nolog: pop bx
        mov ax, [prev8]
        mov [bp + 6], ax
        mov ax, [prev8 + 2]
        mov [bp + 8], ax
        pop bp
        pop ax
        pop ds
        retf

; ---- MMSYSTEM's StackEnter and StackLeave (MMSYSTEM.32, .33), observed
; black-box: TpcStack() calls StackEnter, then StackLeave, with marker
; values in every register, interrupts off, and records every register
; after each call: _stkrec[0..10] = AX BX CX DX SI DI BP SP DS ES SS
; and flags after StackEnter, [12..23] the same after StackLeave; [24] SS,
; [25] SP before. Returns DX:AX = far pointer to _stkrec.
extern STACKENTER                       ; MMSYSTEM.32
extern STACKLEAVE                       ; MMSYSTEM.33
segment _DATA
global _stkrec
_stkrec times 26 dw 0
segment _TEXT
%macro STKSAVE 1
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_stkrec + %1 + 0], ax
        mov [_stkrec + %1 + 2], bx
        mov [_stkrec + %1 + 4], cx
        mov [_stkrec + %1 + 6], dx
        mov [_stkrec + %1 + 8], si
        mov [_stkrec + %1 + 10], di
        mov [_stkrec + %1 + 12], bp
        mov [_stkrec + %1 + 14], sp
        add word [_stkrec + %1 + 14], 2
        pop word [_stkrec + %1 + 16]
        mov [_stkrec + %1 + 18], es
        mov [_stkrec + %1 + 20], ss
        pushf
        pop ax
        mov [_stkrec + %1 + 22], ax
%endmacro
global TPCSTACK
TPCSTACK:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        pushf
        mov ax, DGROUP
        mov ds, ax
        mov [_stkrec + 48], ss
        mov [_stkrec + 50], sp
        cli
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        mov bp, 7777h
        push ds
        pop es
        call far STACKENTER
        STKSAVE 0
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        mov bp, 7777h
        call far STACKLEAVE
        STKSAVE 24
        popfi
        pop es
        pop ds
        pop di
        pop si
        pop bp
        mov ax, _stkrec
        mov dx, DGROUP
        retf

; WORD picisr(void): the master PIC in-service register (low byte) and
; mask (high byte)
global _picisr
_picisr:
        pushf
        cli
        mov al, 0Bh
        out 20h, al
        in al, 20h
        mov ah, al
        mov al, 0Ah
        out 20h, al
        in al, 21h
        xchg al, ah
        popfi
        retf
