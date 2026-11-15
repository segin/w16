; ms4b.asm - MS4B.COM: a minimal APM 1.0 BIOS, real-mode interface only, as
; a TSR, so that Windows 3.1's POWER.DRV (an APM driver) has a BIOS to talk
; to in DOSBox, which has none, and power events can be made to happen on
; a schedule (tests/apps/md2logs/b/notes.md: who calls MOUSE.DRV's
; PowerEventProc, and with what). Load it before MS3 (tests/dos/ms3b.asm),
; which then records every INT 15h AH=53h call and its result.
;
; The functions (INT 15h AH=53h, AL = function; APM 1.0 as Ralf Brown's
; Interrupt List describes it, tables 00473-00479):
;   00h installation check: version 1.0, BX = 'PM', CX = 0 (no protected-
;       mode interfaces)
;   01h real-mode connect, 04h disconnect, 05h CPU idle, 06h CPU busy,
;   07h set power state, 08h enable/disable, 09h restore defaults,
;   0Eh driver version (AX = 0100h), 0Fh engage: success
;   02h, 03h protected-mode connects: CF, AH = 06h / 08h (not supported)
;   0Ah power status: BH = 1 (on line), BL = 0 (battery high), CX = FFFFh
;   0Bh get PM event: the next scheduled event, or CF, AH = 80h (none)
;   others: CF, AH = 0Ah
; Schedule, counted from the first 0Bh call (BIOS ticks, 18.2 a second):
;   +5 s: 0002h suspend request; after a set-power-state to suspend
;         (07h, CX = 2) or standby (CX = 1): 0003h normal resume, once;
;   +20 s: 0004h critical resume;
;   +30 s: 0001h standby request (and then 0003h after 07h as above);
;   +40 s: 0003h normal resume with no suspend before it.
; (No vector points at the state: INT 67h, tried first, is EMS's.)
        bits 16
        cpu 8086
        org 100h

start:  jmp init

state:
started dw 0                    ; ticks at the first 0Bh call (+1: valid)
base    dw 0
step    dw 0                    ; schedule entries done
pend    dw 0                    ; event to give at the next 0Bh call
nev     dw 0                    ; events given
old15   dd 0

; schedule: ticks after the first 0Bh call, event
sched:  dw 91, 2
        dw 364, 4
        dw 546, 1
        dw 728, 3
        dw 0FFFFh, 0

int15:  cmp ah, 53h
        je apm
        jmp far [cs:old15]
apm:    push bp
        mov bp, sp
        cmp al, 00h
        je .inst
        cmp al, 02h
        je .pm16
        cmp al, 03h
        je .pm32
        cmp al, 07h
        je .state
        cmp al, 0Ah
        je .status
        cmp al, 0Bh
        je .event
        cmp al, 0Eh
        je .ver
        cmp al, 0Fh
        jbe .ok
        mov ah, 0Ah
        jmp .err
.inst:  mov ax, 0100h
        mov bx, 504Dh
        xor cx, cx
        jmp .ok
.pm16:  mov ah, 06h
        jmp .err
.pm32:  mov ah, 08h
        jmp .err
.state: cmp cx, 1
        je .susp
        cmp cx, 2
        jne .ok
.susp:  mov word [cs:pend], 3
        jmp .ok
.status: mov bx, 0100h
        mov cx, 0FFFFh
        jmp .ok
.ver:   mov ax, 0100h
        jmp .ok
.event: push ds
        push si
        push dx
        xor si, si
        mov ds, si
        mov dx, [046Ch]                 ; BIOS tick count, low word
        cmp word [cs:started], 0
        jne .have
        mov [cs:base], dx
        mov word [cs:started], 1
.have:  mov bx, [cs:pend]
        or bx, bx
        jz .sch
        mov word [cs:pend], 0
        jmp .give
.sch:   sub dx, [cs:base]
        mov si, [cs:step]
        shl si, 1
        shl si, 1
        cmp dx, [cs:sched + si]
        jb .none
        mov bx, [cs:sched + si + 2]
        or bx, bx
        jz .none
        inc word [cs:step]
.give:  inc word [cs:nev]
        pop dx
        pop si
        pop ds
        jmp .ok
.none:  pop dx
        pop si
        pop ds
        mov ah, 80h
        jmp .err
.ok:    and word [bp + 6], 0FFFEh       ; CF clear in the caller's flags
        pop bp
        iret
.err:   or word [bp + 6], 1
        pop bp
        iret

end_res:

init:   mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'MS4 (APM BIOS) installed', 13, 10, '$'
