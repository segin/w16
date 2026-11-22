; vx14.asm - VX14.COM: DOS programs that wait or work in a DOS box, for
; the idle-detection measurements (VMPOLL; ARCHITECTURE.md 12.12): run in
; boxes started by VX13.EXE (tests/apps/vx13.c) under Windows 3.1's WIN386
; and w16's. The first letter of the command tail picks what it does:
;   P  polls the keyboard: INT 16h AH=01h in a loop (a key: read, go on)
;   W  waits for keys: INT 16h AH=00h in a loop
;   R  polls with INT 2Fh AX=1680h (release time slice) between the
;      INT 16h AH=01h calls, as the DDK asks idle programs to
;   D  polls with INT 21h AH=0Bh (DOS's input status) in a loop
;   Q  as P, with a delay loop of 4000 iterations between polls (fewer
;      polls a second: is detection by count or by time?)
;   V  as P, and it writes a character to the screen (B800:0000) at each
;      poll: a program that polls and is busy on the screen
;   B  busy: counts loop iterations, reading the real-time clock's
;      seconds (CMOS register 0) every 4096; every 4 seconds it appends
;      "VX14 B n=XXXXXXXX" (iterations in those 4 s, hex) to VX14.LOG in
;      the current directory, so another program's share and this one's
;      can be compared
;   S  as B, and it writes a character to the screen (B800:0000) at each
;      clock read: a program that is busy on the screen
; Escape ends P, W, R and D. Black box (ARCHITECTURE.md 3): the published
; BIOS and DOS interfaces, the DDK's INT 2Fh 1680h, the MC146818's
; registers.
        bits 16
        cpu 386
        org 100h

start:  mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        or al, 20h
        mov [mode], al
        cmp al, 'p'
        je poll
        cmp al, 'w'
        je kwait
        cmp al, 'r'
        je relp
        cmp al, 'd'
        je dosst
        cmp al, 'q'
        je qpoll
        cmp al, 'v'
        je vpoll
        cmp al, 'b'
        je near busy
        cmp al, 's'
        je near busy
        mov dx, m_usage
        mov ah, 9
        int 21h
        jmp exit

poll:   mov ah, 1
        int 16h
        jz poll
        xor ah, ah
        int 16h
        cmp al, 27
        jne poll
        jmp exit

qpoll:  mov cx, 4000
.d:     loop .d
        mov ah, 1
        int 16h
        jz qpoll
        xor ah, ah
        int 16h
        cmp al, 27
        jne qpoll
        jmp exit

vpoll:  push es
        push 0B800h
        pop es
        inc byte [es:0]
        pop es
        mov ah, 1
        int 16h
        jz vpoll
        xor ah, ah
        int 16h
        cmp al, 27
        jne vpoll
        jmp exit

kwait:  xor ah, ah
        int 16h
        cmp al, 27
        jne kwait
        jmp exit

relp:   mov ax, 1680h
        int 2Fh
        mov ah, 1
        int 16h
        jz relp
        xor ah, ah
        int 16h
        cmp al, 27
        jne relp
        jmp exit

dosst:  mov ah, 0Bh
        int 21h
        test al, al
        jz dosst
        mov ah, 8
        int 21h
        cmp al, 27
        jne dosst
        jmp exit

exit:   mov ax, 4C00h
        int 21h

; ---- busy: EDI counts iterations, EBP since the last report ----
busy:   call rtcsec
        mov [last], al
        xor ebp, ebp
        mov byte [nsec], 0
.l:     inc ebp
        test bp, 0FFFh
        jnz .l
        cmp byte [mode], 's'
        jne .ns
        push es
        push 0B800h
        pop es
        inc byte [es:0]
        pop es
.ns:    call rtcsec
        cmp al, [last]
        je .l
        mov [last], al
        inc byte [nsec]
        cmp byte [nsec], 4
        jb .l
        mov byte [nsec], 0
        mov eax, ebp
        call report
        xor ebp, ebp
        jmp .l

; the RTC's seconds register (waiting while an update is in progress)
rtcsec: mov al, 0Ah
        out 70h, al
        in al, 71h
        test al, 80h
        jnz rtcsec
        xor al, al
        out 70h, al
        in al, 71h
        ret

; append "VX14 B n=EAX" to VX14.LOG
report: pushad
        mov di, hexout
        mov cx, 8
.h:     rol eax, 4
        mov bl, al
        and bl, 15
        add bl, '0'
        cmp bl, '9'
        jbe .d
        add bl, 7
.d:     mov [di], bl
        inc di
        loop .h
        mov al, [mode]
        sub al, 20h
        mov [line + 5], al
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jnc .o
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .x
.o:     mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov dx, line
        mov cx, linelen
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.x:     popad
        ret

mode    db 0
last    db 0
nsec    db 0
logname db 'VX14.LOG', 0
line    db 'VX14 B n='
hexout  db '00000000', 13, 10
linelen equ $ - line
m_usage db 'VX14 P|W|R|D|Q|V|B|S', 13, 10, '$'
