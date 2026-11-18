; tprs.asm - the timer probe's port and DOS helpers (tests/timer/tpr.c)
        bits 16
        cpu 286

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

; WORD pitcount(void): channel 0's count, latched
global _pitcount
_pitcount:
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

; WORD cmos(WORD reg): a CMOS (real-time clock) register, waiting for no
; update in progress first
global _cmos
_cmos:
        push bp
        mov bp, sp
        pushf
        cli
.u:     mov al, 0Ah
        out 70h, al
        in al, 71h
        test al, 80h
        jnz .u
        mov al, [bp + 6]
        out 70h, al
        in al, 71h
        xor ah, ah
        popfi
        pop bp
        retf

; DWORD dostime(void): INT 21h 2Ch, DX:AX = CX:DX (h m : s 1/100)
global _dostime
_dostime:
        mov ah, 2Ch
        int 21h
        mov ax, dx
        mov dx, cx
        retf

; WORD getflags(void)
global _getflags
_getflags:
        pushf
        pop ax
        retf

; WORD pitstat(void): channel 0's status byte (8254 read-back command)
global _pitstat
_pitstat:
        pushf
        cli
        mov al, 0E2h
        out 43h, al
        in al, 40h
        xor ah, ah
        popfi
        retf
