; system.asm - w16's SYSTEM.DRV: the Windows 3.1 system driver (timers,
; the msec count, the coprocessor's state, drive and timer inquiries).
; drivers/system/README.md says what it is, where each fact comes from and
; what is verified.
;
; Interface (ordinals and names from the NE export table of Windows 3.1's
; SYSTEM.DRV, docs/exports/SYSTEM.txt; signatures from Schulman et al.,
; Undocumented Windows ch. 9, and Pietrek, Windows Internals ch. 1; the
; bytes each entry takes off the stack, the registers it keeps and what it
; returns measured on Windows 3.1's driver with SY1, tests/apps/sy1.c):
;   1 InquireSystem(wWhat, wDrive)  DX:AX: 0 the timer resolution (54925
;                                   us), 1 a drive's type, 2 one-drive
;                                   logic on/off (nothing visible: 0)
;   2 CreateSystemTimer(wRate, lpProc)  a timer handle, 0 if none is free
;   3 KillSystemTimer(hTimer)       0, or hTimer when it is not a timer
;   4 EnableSystemTimers()          INT 8 taken
;   5 DisableSystemTimers()         INT 8 given back
;   6 GetSystemMsecCount()          DX:AX ms counted while enabled
;   7 Get80x87SaveSize()            94 with a coprocessor, else 0
;   8 Save80x87State(lpSave)        FNSAVE
;   9 Restore80x87State(lpSave)     FRSTOR
;  10 WEP(bSystemExit)              1
;  20 A20_Proc(w)                   2 (nothing done)
;
; Timers (SY1, part C, on Windows 3.1's driver): eight, handles 18h, 20h,
; ... 50h (each the offset of its entry here: the procedure gets it in
; AX). A timer of rate R ms is called every max(1, R * 1000 / 54925)
; ticks of IRQ 0 (truncated), the first time that many ticks after it was
; made; timers are served in table order. INT 8 goes first to the handler
; it replaced (the BIOS: tick count, end of interrupt), then the msec
; count goes up by 54.925 ms (54 plus a carried 925 us), then, with
; interrupts enabled, each timer due is called (AX its handle, DS this
; driver's data, on the interrupted stack). An IRQ 0 that comes while
; procedures are running counts the msec and goes to the BIOS, and no
; timer is counted down or called for it. The PIT is not reprogrammed.
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern NOHOOKDOSCALL                    ; KERNEL.101
extern __0040H                          ; KERNEL.193: selector of 0040:0000

NTIMER  equ 8
TSIZE   equ 8                           ; a timer entry: below
T_RELOAD equ 0                          ; ticks between calls; 0 free
T_COUNT equ 2                           ; ticks until the next call
T_PROC  equ 4                           ; the procedure

segment _DATA
; (the timer table is at offset 18h: its handles are Windows 3.1's)
msec    dd 0                            ; +0 GetSystemMsecCount
usec    dw 0                            ; +4 the msec count's 1/1000s
prev8   dd 0                            ; +6 INT 8 before Enable
enabled db 0                            ; +10
busy    db 0                            ; +11 timer procedures running
have87  db 0                            ; +12 a coprocessor
floppies db 0                           ; +13 floppy drives (BIOS)
onedrv  dw 0                            ; +14 InquireSystem(2) state
        times 18h - ($ - $$) db 0
timers: times NTIMER * TSIZE db 0       ; +18h
tend:

segment _TEXT

; ---- the library's start: the coprocessor and the floppy drives from the
; BIOS's equipment word (Schulman ch. 9: INT 11h; FNINIT when there is a
; coprocessor)
global libentry
libentry:
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov ax, __0040H
        mov es, ax
        mov ax, [es:10h]                ; the equipment word (INT 11h's)
        test al, 02h                    ; bit 1: a coprocessor
        jz .no87
        mov byte [have87 wrt DGROUP], 1
        fninit
.no87:  mov cl, al                      ; bits 6-7: floppy drives - 1. (Bit
        shr cl, 6                       ; 0, "there are floppy drives", is
        inc cl                          ; not looked at: in DOSBox it is
        mov [floppies wrt DGROUP], cl   ; clear and 3.1's driver still
                                        ; answers A: removable, SY1)
        mov ax, 1
        pop es
        pop ds
        retf

; ---- 10: WEP(bSystemExit) -> 1
global WEP
WEP:
        mov ax, 1
        retf 2

; ---- 20: A20_Proc(w) -> 2; nothing done (Schulman ch. 9; SY1)
global A20_PROC
A20_PROC:
        mov ax, 2
        retf 2

; ---- 6: GetSystemMsecCount() -> DX:AX. Every other register kept (SY1).
global GETSYSTEMMSECCOUNT
GETSYSTEMMSECCOUNT:
        push ds
        mov ax, DGROUP
        mov ds, ax
        pushf
        cli
        mov ax, [msec wrt DGROUP]
        mov dx, [msec + 2 wrt DGROUP]
        popf
        pop ds
        retf

; ---- 7: Get80x87SaveSize() -> 94 (FNSAVE's area, 16-bit form) or 0
global GET80X87SAVESIZE
GET80X87SAVESIZE:
        push ds
        mov ax, DGROUP
        mov ds, ax
        xor ax, ax
        cmp byte [have87 wrt DGROUP], 0
        je .r
        mov ax, 94
.r:     pop ds
        retf

; ---- 8: Save80x87State(lpSave): FNSAVE there (which leaves the
; coprocessor initialised, as on 3.1: SY1); nothing without one
global SAVE80X87STATE
SAVE80X87STATE:
        push bp
        mov bp, sp
        push ds
        push bx
        mov bx, DGROUP
        mov ds, bx
        cmp byte [have87 wrt DGROUP], 0
        je .r
        lds bx, [bp + 6]
        fnsave [bx]
        fwait
.r:     pop bx
        pop ds
        pop bp
        retf 4

; ---- 9: Restore80x87State(lpSave): FRSTOR from there
global RESTORE80X87STATE
RESTORE80X87STATE:
        push bp
        mov bp, sp
        push ds
        push bx
        mov bx, DGROUP
        mov ds, bx
        cmp byte [have87 wrt DGROUP], 0
        je .r
        lds bx, [bp + 6]
        frstor [bx]
        fwait
.r:     pop bx
        pop ds
        pop bp
        retf 4

; ---- 1: InquireSystem(wWhat, wDrive) -> DX:AX
;   0: 0:54925, the timer's resolution in us (Schulman; SY1: D68Dh)
;   1: drive wDrive (0 = A:): AX 0 none, 2 removable, 3 fixed, 4 remote;
;      DX 1 for B: when the BIOS has one floppy drive (B: is then A:
;      under DOS's one-drive logic; SY1)
;   2: one-drive logic on (wDrive != 0) or off: kept, 0 returned
;   anything else: 0
global INQUIRESYSTEM
INQUIRESYSTEM:
        push bp
        mov bp, sp
        push ds
        push bx
        push cx
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 8]                ; wWhat
        mov bx, [bp + 6]                ; wDrive
        or ax, ax
        jnz .n0
        mov ax, 54925
        xor dx, dx
        jmp .r
.n0:    cmp ax, 2
        jne .n2
        mov [onedrv wrt DGROUP], bx
        jmp .zero
.n2:    cmp ax, 1
        jne .zero
        cmp bx, 26
        jae .zero
        cmp bx, 2
        jae .dos
        ; A: and B: from the BIOS's floppy count
        mov cl, [floppies wrt DGROUP]
        xor ch, ch
        cmp bx, cx
        jb .rem
        cmp bx, 1                       ; B: on a one-floppy machine
        jne .zero
        cmp cx, 1
        jne .zero
        xor ax, ax
        mov dx, 1
        jmp .r
.rem:   mov ax, 2
        xor dx, dx
        jmp .r
.dos:   ; C: on: DOS. IOCTL 4409h: a remote drive (bit 12) or no drive
        ; (an error); else IOCTL 4408h: removable (0) or fixed
        inc bx
        mov ax, 4409h
        call dos
        jc .zero
        test dx, 1000h
        jnz .remote
        mov ax, 4408h
        call dos
        jc .fixed
        or ax, ax
        jz .rem
.fixed: mov ax, 3
        xor dx, dx
        jmp .r
.remote:
        mov ax, 4
        xor dx, dx
        jmp .r
.zero:  xor ax, ax
        xor dx, dx
.r:     pop cx
        pop bx
        pop ds
        pop bp
        retf 4

; dos: INT 21h through KERNEL's NoHookDOSCall (not KERNEL's own INT 21h
; handler); AX, BX as for INT 21h; CF, AX, DX back
dos:
        push bx
        call far NOHOOKDOSCALL
        pop bx
        ret

; ---- 2: CreateSystemTimer(wRate, lpProc) -> the handle, 0 if all eight
; are in use (SY1: the first free entry, in table order)
global CREATESYSTEMTIMER
CREATESYSTEMTIMER:
        push bp
        mov bp, sp
        push ds
        push bx
        push cx
        mov ax, DGROUP
        mov ds, ax
        ; ticks = rate * 1000 / 54925, at least 1
        mov ax, [bp + 10]
        mov cx, 1000
        mul cx
        mov cx, 54925
        div cx
        or ax, ax
        jnz .t
        inc ax
.t:     mov cx, ax
        mov bx, timers
        pushf
        cli
.find:  cmp word [bx + T_RELOAD], 0
        je .got
        add bx, TSIZE
        cmp bx, tend
        jb .find
        popf
        xor ax, ax
        jmp .r
.got:   mov ax, [bp + 6]
        mov [bx + T_PROC], ax
        mov ax, [bp + 8]
        mov [bx + T_PROC + 2], ax
        mov [bx + T_COUNT], cx
        mov [bx + T_RELOAD], cx         ; last: the entry is in use now
        popf
        mov ax, bx
.r:     pop cx
        pop bx
        pop ds
        pop bp
        retf 6

; ---- 3: KillSystemTimer(hTimer) -> 0, or hTimer when it is no timer in
; use (SY1: a timer killed twice, 0). May be called from a timer
; procedure, its own timer too (SY1).
global KILLSYSTEMTIMER
KILLSYSTEMTIMER:
        push bp
        mov bp, sp
        push ds
        push bx
        mov ax, DGROUP
        mov ds, ax
        mov bx, [bp + 6]
        mov ax, bx
        cmp bx, timers
        jb .r
        cmp bx, tend
        jae .r
        sub ax, timers
        test ax, TSIZE - 1
        mov ax, bx
        jnz .r
        cmp word [bx + T_RELOAD], 0
        je .r
        mov word [bx + T_RELOAD], 0
        xor ax, ax
.r:     pop bx
        pop ds
        pop bp
        retf 2

; ---- 4: EnableSystemTimers(): INT 8 (protected mode) to timer_int; the
; one it replaces is kept for Disable and called first on every tick.
; Nothing when already enabled.
global ENABLESYSTEMTIMERS
ENABLESYSTEMTIMERS:
        push ds
        push es
        push bx
        push dx
        mov ax, DGROUP
        mov ds, ax
        cmp byte [enabled wrt DGROUP], 0
        jne .r
        mov ax, 3508h
        int 21h
        mov [prev8 wrt DGROUP], bx
        mov [prev8 + 2 wrt DGROUP], es
        mov byte [enabled wrt DGROUP], 1
        push ds
        push cs
        pop ds
        mov dx, timer_int
        mov ax, 2508h
        int 21h
        pop ds
.r:     pop dx
        pop bx
        pop es
        pop ds
        retf

; ---- 5: DisableSystemTimers(): INT 8 back as Enable found it. The msec
; count and the timers stand still until Enable (SY1, part E).
global DISABLESYSTEMTIMERS
DISABLESYSTEMTIMERS:
        push ds
        push dx
        mov ax, DGROUP
        mov ds, ax
        cmp byte [enabled wrt DGROUP], 0
        je .r
        mov byte [enabled wrt DGROUP], 0
        push ds
        lds dx, [prev8 wrt DGROUP]
        mov ax, 2508h
        int 21h
        pop ds
.r:     pop dx
        pop ds
        retf

; ---- INT 8 (IRQ 0), protected mode
timer_int:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pushf                           ; the BIOS first (tick count, EOI)
        call far [prev8 wrt DGROUP]
        ; the msec count: 54.925 ms a tick
        add word [msec wrt DGROUP], 54
        adc word [msec + 2 wrt DGROUP], 0
        add word [usec wrt DGROUP], 925
        cmp word [usec wrt DGROUP], 1000
        jb .c
        sub word [usec wrt DGROUP], 1000
        add word [msec wrt DGROUP], 1
        adc word [msec + 2 wrt DGROUP], 0
.c:     cmp byte [busy wrt DGROUP], 0
        jne .out
        mov byte [busy wrt DGROUP], 1
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push es
        sti
        mov bx, timers
.next:  cmp word [bx + T_RELOAD], 0
        je .skip
        dec word [bx + T_COUNT]
        jnz .skip
        mov ax, [bx + T_RELOAD]
        mov [bx + T_COUNT], ax
        mov ax, [bx + T_PROC]
        or ax, [bx + T_PROC + 2]
        jz .skip                        ; a null procedure: not called
        push bx
        mov ax, bx                      ; AX = the handle
        call far [bx + T_PROC]
        mov ax, DGROUP
        mov ds, ax
        pop bx
.skip:  add bx, TSIZE
        cmp bx, tend
        jb .next
        cli
        mov byte [busy wrt DGROUP], 0
        pop es
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
.out:   pop ax
        pop ds
        iret
