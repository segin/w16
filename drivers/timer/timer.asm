; timer.asm - w16's TIMER.DRV: the Windows 3.1 multimedia timer driver
; (module TIMER), the installable driver MMSYSTEM.DLL opens through
; SYSTEM.INI [drivers] timer=timer.drv and that serves its time*
; functions. drivers/timer/README.md and ARCHITECTURE.md 21 say what it
; is, where each fact comes from and what is verified.
;
; Interface (NE export table of Windows 3.1's TIMER.DRV: WEP 1,
; DriverProc 2): MMSYSTEM calls DriverProc directly (not through USER's
; SendDriverMessage) with identifier 0 and its driver handle. The messages
; were found black-box with the logging driver TMS.DRV between Microsoft's
; MMSYSTEM and Microsoft's TIMER.DRV (tests/timer/tms.c, tpr.c); none is
; in the SDK. Results: 0 (TIMERR_NOERROR) or 97 (TIMERR_NOCANDO).
;   0800h kill event    lParam1 = the event's ID
;   0804h set event     lParam1 -> {WORD delay, WORD resolution, DWORD
;                       callback, DWORD dwUser, WORD flags}: the ID, or 0
;   0808h system time   DX:AX = milliseconds (timeGetTime jumps straight
;                       here; timeGetSystemTime calls it)
;   080Ch capabilities  lParam1 -> TIMECAPS, lParam2 = its size
;   0810h begin period  lParam1 = the period in ms
;   0814h end period    lParam1 = the period in ms
; The usual driver messages (DRV_LOAD ... DRV_FREE) as Windows 3.1's.
;
; How it keeps time (Standard mode; 386 Enhanced mode at the end): the
; 8254's channel 0 runs in mode 2 at the shortest period anyone asked for
; (a timeBeginPeriod still in force, or an event's resolution: its
; resolution, or its delay when that is 0), 1-55 ms, else 65536 counts
; (the BIOS's 54.9 ms). It is written at once (control word and count:
; the count restarts) whenever that shortest period changes, as 3.1's
; does. Every interrupt adds the counts that passed to the millisecond
; clock and to a count toward the next BIOS tick: the handler that was on
; INT 8 before (SYSTEM.DRV's, which goes on to the BIOS) is called once
; per 65536 counts, so the BIOS time of day, SYSTEM.DRV's timers and its
; millisecond count advance as without this driver. timeGetTime is the
; clock plus the counts since the last interrupt, read from the 8254.
; Events are looked at after each interrupt (after the end of interrupt,
; interrupts enabled, never two callbacks at once) and called through
; MMSYSTEM's StackEnter, DriverCallback and StackLeave, on MMSYSTEM's
; interrupt stack, as 3.1's are.
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern __WINFLAGS                       ; KERNEL.178 (a constant)
extern DEFDRIVERPROC                    ; USER.255
extern DRIVERCALLBACK                   ; MMSYSTEM.31
extern STACKENTER                       ; MMSYSTEM.32
extern STACKLEAVE                       ; MMSYSTEM.33

WF_ENHANCED     equ 0020h
TIMERR_NOCANDO  equ 97
DCB_FUNCTION    equ 3
MAXPER          equ 55                  ; longest period (ms) of the 8254
NEV             equ 16                  ; event slots; 1-15 used (8 in 386
                                        ; Enhanced mode, 0-7)

; an event slot
E_FLAGS equ 0                           ; the caller's flags
E_USED  equ 2                           ; 1 in use
E_DELAY equ 4
E_RES   equ 6                           ; its effective resolution (ms)
E_CB    equ 8                           ; callback
E_USER  equ 12                          ; dwUser
E_DUE   equ 16                          ; when it is due: ms (dword) and
E_DUEF  equ 20                          ; 1/65536 ms
E_ID    equ 22
ESIZE   equ 24

TIME_PERIODIC   equ 1
EV_NEVER        equ 8000h               ; flags bit 15: 3.1's never calls
                                        ; such an event (tpr.c, flags); in
EV_NEVER3       equ 0002h               ; 386 Enhanced mode bit 1 instead

segment _DATA
enabled db 0                            ; INT 8 is ours
enh     db 0                            ; 386 Enhanced mode
busy    db 0                            ; callbacks running
skip    db 0                            ; the next interrupt was counted
                                        ; already (the 8254 restarted
                                        ; with it pending)
prev8   dd 0                            ; INT 8 before ours
cntrun  dd 65536                        ; counts of the 8254's period
perms   dw MAXPER                       ; that period in ms (MAXPER: the
                                        ; BIOS's 65536 counts)
bacc    dd 0                            ; counts toward the next BIOS tick
owed    dw 0                            ; BIOS ticks owed (from restarts)
cms     dd 0                            ; the clock: ms since enabled
cfrac   dw 0                            ; and 1/65536 ms
lastms  dd 0                            ; the last time given out
epoch   dw 0                            ; counted periods (interrupts and
                                        ; restarts)
lastep  dw 0                            ; the epoch and the counts elapsed
lastel  dd 0                            ; at the last reading
seq     dw 0                            ; event IDs: (seq << 4) | slot
nevmask dw EV_NEVER                     ; the flags of events never called
begun   times MAXPER + 1 dw 0           ; timeBeginPeriod counts, 1-55
events  times NEV * ESIZE db 0
nused   dw 0                            ; events in use
stkdone db 0                            ; StackEnter called once at task
                                        ; time
curcb   dd 0                            ; snapshotted event fields while
curuser dd 0                            ; busy == 1
curid   dw 0

; the 8254's count for 1-55 ms (55: 0, 65536 counts)
counts:
        dw 0
%assign p 1
%rep MAXPER - 1
        dw (p * 1193182 + 500) / 1000
%assign p p + 1
%endrep
        dw 0

segment _TEXT

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

%macro prolog 0
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
%endmacro
%macro epilog 1
        pop di
        pop si
        pop ds
        pop bp
        retf %1
%endmacro

; ---- the library's start: 386 Enhanced mode or not
global libentry
libentry:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, __WINFLAGS
        test ax, WF_ENHANCED
        jz .s
        mov byte [enh], 1
        mov word [nevmask], EV_NEVER3
        mov word [seq], 1               ; (3.1's first ID there is 10h)
.s:     mov ax, 1
        pop ds
        retf

; ---- 1: WEP(bSystemExit) -> 1
global WEP
WEP:
        push ds
        push dx
        mov ax, DGROUP
        mov ds, ax
        call disable
        pop dx
        pop ds
        mov ax, 1
        retf 2

; ---- 2: DriverProc(dwId, hDriver, wMsg, lParam1, lParam2) -> DX:AX
; [bp+6] lParam2, +10 lParam1, +14 wMsg, +16 hDriver, +18 dwId
global DRIVERPROC
DRIVERPROC:
        prolog
        mov ax, [bp + 14]
        cmp ax, 0808h                   ; the most frequent first
        je .time
        cmp ax, 0800h
        je .kill
        cmp ax, 0804h
        je .set
        cmp ax, 080Ch
        je .caps
        cmp ax, 0810h
        je .begin
        cmp ax, 0814h
        je .end
        cmp ax, 1                       ; DRV_LOAD
        je .one
        cmp ax, 2                       ; DRV_ENABLE
        je .enable
        cmp ax, 3                       ; DRV_OPEN
        je .one
        cmp ax, 4                       ; DRV_CLOSE
        je .one
        cmp ax, 5                       ; DRV_DISABLE
        je .disable
        cmp ax, 6                       ; DRV_FREE: 0, as 3.1's
        je .zero
        cmp ax, 9                       ; DRV_INSTALL: 2 (DRVCNF_RESTART),
        je .two                         ; as 3.1's
        ; everything else as DefDriverProc answers it
        push word [bp + 20]
        push word [bp + 18]
        push word [bp + 16]
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        call far DEFDRIVERPROC
        jmp .r
.two:   mov ax, 2
        xor dx, dx
        jmp .r
.zero:  xor ax, ax
        xor dx, dx
        jmp .r
.enable:
        call enable
.one:   mov ax, 1
        xor dx, dx
        jmp .r
.disable:
        call disable
        jmp .one
.time:  call gettime
        jmp .r
.kill:  mov ax, [bp + 10]
        call killev
        jmp .r0
.set:   les bx, [bp + 10]
        call setev
        cmp byte [enh], 0               ; 386 Enhanced mode: DX:AX the ID
        je .r0                          ; sign-extended (3.1's: FFFF8118)
        cwd
        jmp .r
.caps:  les bx, [bp + 10]
        mov cx, [bp + 6]
        call caps
        jmp .r0
.begin: mov bx, [bp + 10]
        mov cx, [bp + 12]
        mov ax, 1
        call period
        jmp .r0
.end:   mov bx, [bp + 10]
        mov cx, [bp + 12]
        mov ax, -1
        call period
.r0:    xor dx, dx
.r:     epilog 16

; ---- timeGetDevCaps: ES:BX the TIMECAPS, CX its size -> AX. 3.1's fills
; {1, 65535} when the size is 4 (in 386 Enhanced mode 4 or more), else 97
; and nothing written.
caps:
        cmp cx, 4
        je .ok
        jb .no
        cmp byte [enh], 0
        jne .ok
.no:    mov ax, TIMERR_NOCANDO
        ret
.ok:    mov word [es:bx], 1
        mov word [es:bx + 2], 0FFFFh
        xor ax, ax
        ret

; ---- timeBeginPeriod (AX 1) / timeEndPeriod (AX -1): CX:BX the period
; -> AX. 3.1's takes 1-55 ms and counts each; an end without its begin is
; 97. In 386 Enhanced mode 3.1's answers 0 to anything; this one still
; uses 1-55 for the 8254.
period:
        or cx, cx
        jnz .bad
        or bx, bx
        jz .bad
        cmp bx, MAXPER
        ja .bad
        shl bx, 1
        pushf
        cli
        cmp ax, 1
        je .inc
        cmp word [begun + bx], 0
        je .bad1
        dec word [begun + bx]
        jmp .re
.inc:   cmp word [begun + bx], 0FFFFh
        je .bad1
        inc word [begun + bx]
.re:    popfi
        call reprogram
        xor ax, ax
        ret
.bad1:  popfi
.bad:   xor ax, ax
        cmp byte [enh], 0
        jne .r
        mov ax, TIMERR_NOCANDO
.r:     ret

; ---- timeSetEvent: ES:BX -> {delay, resolution, callback, dwUser,
; flags} -> AX the ID or 0. A delay of 0 is refused; any resolution and
; flags are taken (bit 0: periodic). The first free slot (1-15; 386
; Enhanced mode 0-7) is used; the ID is a running count above the slot's
; number (3.1's: (count << 4) | slot, the count 1-FFFh; in 386 Enhanced
; mode (count << 3) | slot).
setev:
        cmp byte [stkdone], 0           ; the first time: MMSYSTEM's
        jne .v                          ; interrupt stack is made now, at
        mov byte [stkdone], 1           ; task time, and is the timer
        call far STACKENTER             ; driver's (3.1's: GetExePtr of
        call far STACKLEAVE             ; the callbacks' SS is TIMER)
.v:     mov ax, [es:bx]
        or ax, ax
        jz .fail0
        ; the first free slot
        pushf
        cli
        mov si, events + ESIZE
        mov cx, 1
        mov dx, 15
        cmp byte [enh], 0
        je .find
        mov si, events
        xor cx, cx
        mov dx, 7
.find:  cmp word [si + E_USED], 0
        je .got
        add si, ESIZE
        inc cx
        cmp cx, dx
        jbe .find
        popfi
.fail0: xor ax, ax
        ret
.got:   mov ax, [es:bx]                 ; delay
        mov [si + E_DELAY], ax
        mov dx, [es:bx + 2]             ; resolution (0: the delay)
        or dx, dx
        jnz .res
        mov dx, ax
.res:   cmp dx, MAXPER
        jbe .r1
        mov dx, MAXPER
.r1:    mov [si + E_RES], dx
        mov ax, [es:bx + 4]
        mov [si + E_CB], ax
        mov ax, [es:bx + 6]
        mov [si + E_CB + 2], ax
        mov ax, [es:bx + 8]
        mov [si + E_USER], ax
        mov ax, [es:bx + 10]
        mov [si + E_USER + 2], ax
        mov ax, [es:bx + 12]
        mov [si + E_FLAGS], ax
        ; the ID
        mov ax, [seq]
        inc ax
        cmp byte [enh], 0
        jne .e
        and ax, 0FFFh
        jnz .s
        inc ax
.s:     mov [seq], ax
        shl ax, 4
        jmp .id
.e:     and ax, 1FFFh
        jnz .e1
        inc ax
.e1:    mov [seq], ax
        shl ax, 3
.id:    or ax, cx
        mov [si + E_ID], ax
        ; due: now + delay
        call gettime                    ; DX:AX ms, BX its fraction
        add ax, [si + E_DELAY]
        adc dx, 0
        mov [si + E_DUE], ax
        mov [si + E_DUE + 2], dx
        mov [si + E_DUEF], bx
        mov word [si + E_USED], 1
        inc word [nused]
        mov ax, [si + E_ID]
        popfi
        call reprogram
        ret

; ---- timeKillEvent: AX the ID -> AX 0, or 97 when no event has it (in
; 386 Enhanced mode 3.1's answers 0 to any ID)
killev:
        mov bx, ax
        and bx, 15
        cmp byte [enh], 0
        je .s
        and bx, 7
        jmp .k
.s:     or bx, bx
        jz .no
.k:     imul bx, bx, ESIZE
        add bx, events
        pushf
        cli
        cmp word [bx + E_USED], 0
        je .no1
        cmp [bx + E_ID], ax
        jne .no1
        call free
        popfi
        call reprogram
        xor ax, ax
        ret
.no1:   popfi
.no:    xor ax, ax
        cmp byte [enh], 0
        jne .r
        mov ax, TIMERR_NOCANDO
.r:     ret

; free: the slot at BX is no longer in use (interrupts off)
free:
        mov word [bx + E_USED], 0
        dec word [nused]
        ret

; ---- the period the 8254 should run at: the shortest of the periods
; begun and the events' resolutions (ms, 1-55) -> AX
wanted:
        push bx
        push cx
        mov ax, MAXPER
        mov bx, 2
.b:     cmp word [begun + bx], 0
        jne .bf
        add bx, 2
        cmp bx, 2 * MAXPER
        jbe .b
        jmp .ev
.bf:    mov ax, bx
        shr ax, 1
.ev:    mov bx, events
        mov cx, NEV
.e:     cmp word [bx + E_USED], 0
        je .n
        cmp [bx + E_RES], ax
        jae .n
        mov ax, [bx + E_RES]
.n:     add bx, ESIZE
        loop .e
        pop cx
        pop bx
        ret

; ---- reprogram: the 8254 to the period wanted, at once, when it changes
; (as 3.1's: the count restarts). The counts of the period under way are
; added to the clock first.
reprogram:
        cmp byte [enabled], 0
        je .r
        push ax
        push bx
        push cx
        push dx
        pushf
        cli
        call wanted
        cmp ax, [perms]
        je .same
        push ax
        call elapsed                    ; DX:AX counts since the last
        call addcounts                  ; interrupt counted
        mov byte [skip], 0
        call irq0pending
        jz .np
        mov byte [skip], 1              ; it comes when interrupts are
.np:    pop ax                          ; enabled: not counted again
        call setpit
.same:  popfi
        pop dx
        pop cx
        pop bx
        pop ax
.r:     ret

; setpit: channel 0, mode 2, to AX ms (interrupts off)
setpit:
        mov [perms], ax
        mov bx, ax
        shl bx, 1
        mov ax, [counts + bx]
        mov word [cntrun], ax
        mov word [cntrun + 2], 0
        or ax, ax
        jnz .w
        mov word [cntrun + 2], 1        ; 0: 65536
.w:     mov bx, ax
        mov al, 34h
        out 43h, al
        mov al, bl
        out 40h, al
        mov al, bh
        out 40h, al
        ret

; irq0pending: ZF clear when IRQ 0 is requested and not yet serviced (the
; master PIC's IRR; interrupts off)
irq0pending:
        mov al, 0Ah
        out 20h, al
        in al, 20h
        test al, 1
        ret

; elapsed: DX:AX = the counts since the period last counted (interrupts
; off): the 8254's period less its count, and a whole period more when
; the count has started again before its interrupt was counted: when the
; interrupt is waiting (skip clear) and the count is in the first half of
; its period, or when fewer counts have passed than at the last reading in
; the same period (DOSBox, at least, may show the restarted count a little
; before it raises the interrupt, or raise it a little before)
elapsed:
        push bx
        xor al, al
        out 43h, al                     ; latch channel 0
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        mov bx, ax
        cmp word [cntrun + 2], 0        ; a count above the period (386
        jne .in                         ; Enhanced mode: the 8254 is VTD's
        cmp bx, [cntrun]                ; virtual one, which need not
        jbe .in                         ; follow the count written at
        mov bx, [cntrun]                ; once): nothing elapsed
.in:    mov ax, [cntrun]
        mov dx, [cntrun + 2]
        sub ax, bx
        sbb dx, 0
        cmp byte [skip], 0
        jne .keep
        push ax
        call irq0pending
        pop ax
        jz .low
        push cx                         ; waiting: counted only when less
        mov bx, [cntrun]                ; than half the period has passed
        mov cx, [cntrun + 2]            ; (the interrupt may come a little
        shr cx, 1                       ; before the count restarts:
        rcr bx, 1                       ; DOSBox)
        cmp dx, cx
        jne .hc
        cmp ax, bx
.hc:    pop cx
        jb .wrap
.low:   mov bx, [epoch]                 ; lower than before, same period?
        cmp bx, [lastep]
        jne .keep
        cmp dx, [lastel + 2]
        ja .keep
        jb .wrap
        cmp ax, [lastel]
        jae .keep
.wrap:  add ax, [cntrun]
        adc dx, [cntrun + 2]
.keep:  mov bx, [epoch]
        mov [lastep], bx
        mov [lastel], ax
        mov [lastel + 2], dx
        pop bx
        ret

; addcounts: DX:AX counts onto the clock and toward the BIOS tick
; (interrupts off). A count is 1000/1193182 ms = (55 - 4889/65536)/65536
; ms; 65536 counts are 54 ms and 60527/65536.
addcounts:
        inc word [epoch]
        add [bacc], ax
        adc [bacc + 2], dx
.big:   or dx, dx
        jz .small
        add word [cfrac], 60527         ; 65536 counts
        adc word [cms], 54
        adc word [cms + 2], 0
        dec dx
        jmp .big
.small: call tofrac                     ; AX counts -> DX:AX in 1/65536 ms
        add [cfrac], ax
        adc [cms], dx
        adc word [cms + 2], 0
.bios:  cmp word [bacc + 2], 0          ; a BIOS tick's worth
        je .r
        sub word [bacc + 2], 1
        inc word [owed]
        jmp .bios
.r:     ret

; tofrac: AX counts (below 65536) -> DX:AX the time in 1/65536 ms:
; 55 * n - n * 4889 / 65536
tofrac:
        push bx
        push cx
        mov cx, ax
        mov bx, 4889
        mul bx                          ; DX = n * 4889 / 65536
        mov bx, dx
        mov ax, cx
        mov cx, 55
        mul cx                          ; DX:AX = 55 n
        sub ax, bx
        sbb dx, 0
        pop cx
        pop bx
        ret

; ---- gettime: DX:AX the clock in ms now, BX its fraction; never less
; than a time given out before (interrupts off on return as on entry)
gettime:
        pushf
        cli
        xor ax, ax
        xor dx, dx
        xor bx, bx
        cmp byte [enabled], 0
        je .r
        push cx
        call elapsed                    ; DX:AX counts
        mov cx, dx                      ; whole 65536s (after a restart
        call tofrac                     ; with the interrupt waiting)
        add ax, [cfrac]                 ; DX ms, AX the fraction
        adc dx, 0
        jcxz .n
.l:     add ax, 60527                   ; 65536 counts
        adc dx, 54
        loop .l
.n:     mov bx, ax
        mov ax, dx
        xor dx, dx
        add ax, [cms]
        adc dx, [cms + 2]
        pop cx
        push dx
        push ax
        sub ax, [lastms]
        sbb dx, [lastms + 2]
        pop ax
        pop dx
        jns .new
        mov ax, [lastms]
        mov dx, [lastms + 2]
        jmp .r
.new:   mov [lastms], ax
        mov [lastms + 2], dx
.r:     popfi
        ret

; ---- DRV_ENABLE: INT 8 taken, the 8254 in mode 2 at the period wanted;
; the clock starts at 0
enable:
        cmp byte [enabled], 0
        jne .r
        push es
        mov ax, 3508h
        int 21h
        mov [prev8], bx
        mov [prev8 + 2], es
        pop es
        pushf
        cli
        xor ax, ax
        mov [cms], ax
        mov [cms + 2], ax
        mov [cfrac], ax
        mov [lastms], ax
        mov [lastms + 2], ax
        mov [lastel], ax
        mov [lastel + 2], ax
        inc word [epoch]
        mov [owed], ax
        mov [skip], al
        mov byte [enabled], 1
        push ds
        push cs
        pop ds
        mov dx, int8
        mov ax, 2508h
        int 21h
        pop ds
        call wanted
        call setpit
        mov word [bacc], 0
        mov word [bacc + 2], 0
        popfi
.r:     ret

; ---- DRV_DISABLE: INT 8 given back, the 8254 as the BIOS has it (mode
; 3, 65536 counts)
disable:
        cmp byte [enabled], 0
        je .r
        pushf
        cli
        mov byte [enabled], 0
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
        push ds
        lds dx, [prev8]
        mov ax, 2508h
        int 21h
        pop ds
        popfi
.r:     ret

; ---- INT 8 (IRQ 0), protected mode. The period that ended goes onto the
; clock; every 65536 counts the handler before ours is called (SYSTEM.DRV,
; then the BIOS: the end of interrupt is theirs), otherwise the end of
; interrupt is sent here. Then, with interrupts enabled, the events due
; are called, unless callbacks are running already.
int8:
        push ds
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push es
        push bp
        mov ax, DGROUP
        mov ds, ax
        cld
        cmp byte [skip], 0
        je .count
        mov byte [skip], 0              ; counted at the restart
        inc word [epoch]
        jmp .bios
.count: mov ax, [cntrun]
        mov dx, [cntrun + 2]
        call addcounts
.bios:  cmp word [owed], 0
        je .eoi
        dec word [owed]
        pushf
        call far [prev8]
        jmp .events
.eoi:   mov al, 20h
        out 20h, al
.events:
        cli
        cmp byte [busy], 0
        jne .out
        cmp word [nused], 0
        je .out
        mov byte [busy], 1
        call runevents
        cli
        mov byte [busy], 0
.out:   pop bp
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pop ds
        iret

; runevents: each event due is called (interrupts off on entry; enabled
; around the callbacks)
runevents:
        mov si, events
.e:     cmp word [si + E_USED], 0
        je .n
        ; due when the clock has passed E_DUE (wrap-safe)
        mov ax, [cms]
        mov dx, [cms + 2]
        sub ax, [si + E_DUE]
        sbb dx, [si + E_DUE + 2]
        js .n
        or ax, dx
        jnz .due
        mov ax, [cfrac]
        cmp ax, [si + E_DUEF]
        jbe .n
.due:   test word [si + E_FLAGS], TIME_PERIODIC
        jz .once
        mov ax, [si + E_DELAY]          ; periodic: the next one
        add [si + E_DUE], ax
        adc word [si + E_DUE + 2], 0
        jmp .call
.once:  mov bx, si                      ; one-shot: gone before its call
        call free
        push si
        call reprogram
        pop si
.call:  mov ax, [si + E_FLAGS]
        test ax, [nevmask]
        jnz .n
        mov ax, [si + E_CB]
        mov [curcb], ax
        mov ax, [si + E_CB + 2]
        mov [curcb + 2], ax
        mov ax, [si + E_ID]
        mov [curid], ax
        mov ax, [si + E_USER]
        mov [curuser], ax
        mov ax, [si + E_USER + 2]
        mov [curuser + 2], ax
        push si
        sti
        call far STACKENTER
        push si                         ; (3.1's callbacks find SP 6 bytes
        push di                         ; lower on MMSYSTEM's stack than
        push bp                         ; DriverCallback's arguments alone)
        push word [curcb + 2]           ; dwCallback
        push word [curcb]
        push word DCB_FUNCTION          ; uFlags
        push word [curid]               ; hDevice: the ID
        push word 0                     ; uMessage
        push word [curuser + 2]         ; dwUser
        push word [curuser]
        push word 0                     ; dw1
        push word 0
        push word 0                     ; dw2
        push word 0
        xor dx, dx
        call far DRIVERCALLBACK
        pop bp
        pop di
        pop si
        call far STACKLEAVE
        cli
        mov ax, DGROUP
        mov ds, ax
        pop si
.n:     add si, ESIZE
        cmp si, events + NEV * ESIZE
        jb .e
        ret
