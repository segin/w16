; mmcs.asm - MMC.DLL's register-level parts (tests/mmsystem/mmc.c), the
; MMSYSTEM probe's library: entries that record every register as
; MMSYSTEM hands them over, before C touches any.
;
;   MMCCALLBACK  a DriverCallback / timeSetEvent function:
;                void FAR PASCAL f(WORD h, WORD msg, DWORD dwUser,
;                                  DWORD dw1, DWORD dw2)      (pops 16)
;   MMCTASK      an mmTaskCreate task function: void FAR PASCAL f(DWORD)
;   MMCSTACK     StackEnter and StackLeave with marker registers, nested
;   MMCHOOK      protected-mode hooks on INT 21h and INT 41h that count
;                what OutputDebugStr does (DPMI 0204h/0205h)
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

%macro popfi 0
        push bp
        mov bp, sp
        test byte [bp + 3], 2
        pop bp
        jz %%n
        sti
%%n:    popf
%endmacro

extern _mmc_cb                  ; void __cdecl __far mmc_cb(WORD __far *frame)
extern _mmc_task                ; void __cdecl __far mmc_task(WORD __far *frame)

; frame passed to C (low to high): ES DS DI SI BP SP BX DX CX AX FLAGS BP0
; then the return address and the arguments
global MMCCALLBACK
MMCCALLBACK:
        push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        cld
        lea ax, [bp - 22]
        push ss
        push ax
        call far _mmc_cb
        add sp, 4
        pop es
        pop ds
        popa
        popfi
        pop bp
        retf 16

global MMCTASK
MMCTASK:
        push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        cld
        lea ax, [bp - 22]
        push ss
        push ax
        call far _mmc_task
        add sp, 4
        pop es
        pop ds
        popa
        popfi
        pop bp
        retf 4

extern STACKENTER                       ; MMSYSTEM.32
extern STACKLEAVE                       ; MMSYSTEM.33

segment _DATA
global _stk
; per step: SS SP FLAGS AX BX CX DX SI DI BP DS ES (12 words), 5 steps:
; before, after StackEnter, after a second StackEnter, after StackLeave,
; after the second StackLeave
_stk    times 60 dw 0
stkp    dw 0
segment _TEXT

%macro snap 0
        pushf
        push ds
        push di
        mov di, DGROUP
        mov ds, di
        mov di, [stkp]
        add word [stkp], 24
        mov [di + _stk + 6], ax
        mov [di + _stk + 8], bx
        mov [di + _stk + 10], cx
        mov [di + _stk + 12], dx
        mov [di + _stk + 14], si
        mov [di + _stk + 18], bp
        mov [di + _stk + 22], es
        mov [di + _stk + 0], ss
        mov [di + _stk + 2], sp
        add word [di + _stk + 2], 6
        push bp
        mov bp, sp
        mov ax, [bp + 2]                ; DI
        mov [di + _stk + 16], ax
        mov ax, [bp + 4]                ; DS
        mov [di + _stk + 20], ax
        mov ax, [bp + 6]                ; the flags
        mov [di + _stk + 4], ax
        pop bp
        mov ax, [di + _stk + 6]
        pop di
        pop ds
        popfi
%endmacro

; void FAR PASCAL MMCSTACK(void): StackEnter twice and StackLeave twice,
; with markers in every general register (DS = ES = DGROUP)
global MMCSTACK
MMCSTACK:
        push bp
        mov bp, sp
        push ds
        push es
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        mov es, ax
        mov word [stkp], 0
        mov ax, 0A11h
        mov bx, 0B22h
        mov cx, 0C33h
        mov dx, 0D44h
        mov si, 05E5h
        mov di, 0D1Dh
        push bp
        mov bp, 0B0Bh
        snap
        call far STACKENTER
        snap
        call far STACKENTER
        snap
        call far STACKLEAVE
        snap
        call far STACKLEAVE
        snap
        pop bp
        pop di
        pop si
        pop es
        pop ds
        pop bp
        retf

; DWORD FAR PASCAL MMCPOPS(FARPROC f, WORD n): n zero words pushed and f
; called: AX = the bytes it popped, DX = its AX
global MMCPOPS
MMCPOPS:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov si, sp
        mov cx, [bp + 6]
.l:     jcxz .c
        push word 0
        dec cx
        jmp .l
.c:     call far [bp + 8]
        mov dx, ax
        mov ax, sp
        sub ax, si
        add ax, [bp + 6]
        add ax, [bp + 6]
        mov sp, si
        pop ds
        pop di
        pop si
        pop bp
        retf 6

; WEP(bSystemExit) -> 1
global WEP
WEP:
        mov ax, 1
        retf 2

; ---- INT 21h and INT 41h hooks (protected mode, DPMI) ----
segment _DATA
global _n21, _n41, _ax21, _ax41
_n21    dw 0                            ; INT 21h calls while hooked
_n41    dw 0                            ; INT 41h calls
_ax21   times 16 dw 0                   ; AX of the first 16 INT 21h
_ax41   times 16 dw 0                   ; AX of the first 16 INT 41h
old21   dd 0
old41   dd 0
hooked  dw 0
segment _TEXT

h21:    push ds
        push bx
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov bx, [_n21]
        cmp bx, 16
        jae .n
        shl bx, 1
        mov [_ax21 + bx], ax
.n:     inc word [_n21]
        pop bx
        pushf                           ; chain: an interrupt frame for
        call far [old21]                ; the old handler
        pop ds
        retf 2                          ; keep its flags (carry)

h41:    push ds
        push bx
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov bx, [_n41]
        cmp bx, 16
        jae .n
        shl bx, 1
        mov [_ax41 + bx], ax
.n:     inc word [_n41]
        pop bx
        pushf
        call far [old41]
        pop ds
        retf 2

; void FAR PASCAL MMCHOOK(WORD on)
global MMCHOOK
MMCHOOK:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        cmp word [bp + 6], 0
        je .off
        cmp word [hooked], 0
        jne .done
        mov word [_n21], 0
        mov word [_n41], 0
        mov ax, 0204h
        mov bl, 21h
        int 31h
        mov [old21], dx
        mov [old21 + 2], cx
        mov ax, 0204h
        mov bl, 41h
        int 31h
        mov [old41], dx
        mov [old41 + 2], cx
        mov ax, 0205h
        mov bl, 21h
        mov cx, cs
        mov dx, h21
        int 31h
        mov ax, 0205h
        mov bl, 41h
        mov cx, cs
        mov dx, h41
        int 31h
        mov word [hooked], 1
        jmp .done
.off:   cmp word [hooked], 0
        je .done
        mov ax, 0205h
        mov bl, 21h
        mov dx, [old21]
        mov cx, [old21 + 2]
        int 31h
        mov ax, 0205h
        mov bl, 41h
        mov dx, [old41]
        mov cx, [old41 + 2]
        int 31h
        mov word [hooked], 0
.done:  pop di
        pop si
        pop ds
        pop bp
        retf 2
