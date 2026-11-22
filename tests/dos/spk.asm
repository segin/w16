; spk.asm - SPK.COM: a DOS program that plays a tone on the PC speaker the
; way DOS programs do (the 8254's channel 2 in mode 3, then port 61h's
; gate and data bits on), for the Virtual Sound Device's probes
; (tests/vsdprobe.sh, ARCHITECTURE.md 12.8 VSD): run in one or two DOS
; boxes, in front and behind, under Windows 3.1's WIN386 and w16's, with
; DOSBox-X's sound recorded. Black box: only what the ports give back
; (ARCHITECTURE.md 3). Sources: the Intel 8254 data sheet (control word
; B6h: channel 2, low then high byte, mode 3; the read-back command), the
; IBM PC/AT technical reference (port 61h: bit 0 the timer 2 gate, bit 1
; the speaker data, bit 4 the refresh toggle, bit 5 timer 2's output).
;
;   usage: SPK D T F [C]
;     waits D timer ticks (the BIOS count at 0040:006Ch, 18.2 a second),
;     plays F Hz for T ticks, then turns the speaker off; with C (any
;     fourth word) it does not program the 8254 and only sets port 61h's
;     two bits (whatever channel 2 holds then sounds); with X it plays
;     nothing and runs D ticks with interrupts off instead (port 61h's
;     refresh toggle counted: 3641 toggles of 15.085 us a tick), keys
;     typed meanwhile wait (the keyboard device's queue), and prints
;     "SPK cli done"; with B it writes B5h to port 0E0h half way through
;     the tone, where the probe VxD VXV.386 calls VSD_Bell.
; It prints, and appends to SPK.LOG in the current directory, one line:
;   SPK f=F 61h start=XX on=XX end=XX polls=N off=M
; start: port 61h before the tone, on: after setting the bits, end: at
; the end of the tone before clearing them; polls: reads of port 61h while
; it played, off: how many of them had bit 0 or 1 clear (someone else
; turned the speaker off: Windows, or another DOS box).
        bits 16
        cpu 386
        org 100h

start:  mov si, 81h
        call num
        mov [delay], ax
        call num
        mov [dur], ax
        call num
        mov [freq], ax
.sk:    lodsb                           ; a fourth word: C
        cmp al, ' '
        je .sk
        cmp al, 13
        je .args
        or al, 20h
        cmp al, 'x'
        je cli_run
        cmp al, 'b'
        jne .c
        mov byte [bell], 1
        jmp .args
.c:     mov byte [conly], 1
.args:  push ds
        push 40h
        pop ds
        mov bx, [6Ch]
        pop ds
        sti
.w1:    push ds                         ; the delay
        push 40h
        pop ds
        mov ax, [6Ch]
        pop ds
        sub ax, bx
        cmp ax, [delay]
        jb .w1
        in al, 61h
        mov [v_start], al
        cmp byte [conly], 0
        jne .gate
        movzx ecx, word [freq]
        mov eax, 1193182
        xor edx, edx
        test ecx, ecx
        jz .gate
        div ecx
        mov cx, ax
        mov al, 0B6h                    ; channel 2, low/high, mode 3
        out 43h, al
        mov al, cl
        out 42h, al
        mov al, ch
        out 42h, al
.gate:  in al, 61h
        or al, 3
        out 61h, al
        in al, 61h
        mov [v_on], al
        push ds
        push 40h
        pop ds
        mov bx, [6Ch]
        pop ds
.play:  in al, 61h
        inc dword [polls]
        and al, 3
        cmp al, 3
        je .on
        inc dword [offs]
.on:    push ds
        push 40h
        pop ds
        mov ax, [6Ch]
        pop ds
        sub ax, bx
        cmp byte [bell], 1              ; half way: the bell (SPK B)
        jne .nb
        mov cx, [dur]
        shr cx, 1
        cmp ax, cx
        jb .nb
        mov byte [bell], 2
        push ax
        mov al, 0B5h
        out 0E0h, al
        pop ax
.nb:
        cmp ax, [dur]
        jb .play
        in al, 61h
        mov [v_end], al
        and al, 0FCh
        out 61h, al
        ; the line
        mov di, line
        mov si, s_f
        call str
        movzx eax, word [freq]
        call dec
        mov si, s_st
        call str
        mov al, [v_start]
        call hex2
        mov si, s_on
        call str
        mov al, [v_on]
        call hex2
        mov si, s_end
        call str
        mov al, [v_end]
        call hex2
        mov si, s_po
        call str
        mov eax, [polls]
        call dec
        mov si, s_off
        call str
        mov eax, [offs]
        call dec
        mov ax, 0A0Dh
        stosw
        mov cx, di
        sub cx, line
        mov [len], cx
        mov dx, line                    ; to the screen
        mov bx, 1
        mov ah, 40h
        int 21h
        mov dx, logname                 ; and appended to SPK.LOG
        mov ax, 3D02h
        int 21h
        jnc .open
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .done
.open:  mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov dx, line
        mov cx, [len]
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.done:  mov ax, 4C00h
        int 21h

; D ticks with interrupts off
cli_run:
        movzx eax, word [delay]
        imul eax, 3641
        mov ecx, eax
        cli
        in al, 61h
        and al, 10h
        mov ah, al
.t:     in al, 61h
        and al, 10h
        cmp al, ah
        je .t
        mov ah, al
        dec ecx
        jnz .t
        sti
        mov dx, s_cli
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h

; a decimal number at SI (blanks skipped) into AX
num:    xor ax, ax
.b:     cmp byte [si], ' '
        jne .d
        inc si
        jmp .b
.d:     movzx cx, byte [si]
        sub cx, '0'
        cmp cx, 9
        ja .e
        imul ax, 10
        add ax, cx
        inc si
        jmp .d
.e:     ret

; the string at SI (NUL ended) to DI
str:    lodsb
        test al, al
        jz .e
        stosb
        jmp str
.e:     ret

; EAX in decimal to DI
dec:    mov ecx, 10
        xor bx, bx
.d:     xor edx, edx
        div ecx
        push dx
        inc bx
        test eax, eax
        jnz .d
.p:     pop ax
        add al, '0'
        stosb
        dec bx
        jnz .p
        ret

; AL in two hex digits to DI
hex2:   push ax
        shr al, 4
        call .n
        pop ax
.n:     and al, 15
        add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     stosb
        ret

logname db 'SPK.LOG', 0
s_cli   db 'SPK cli done', 13, 10, '$'
s_f     db 'SPK f=', 0
s_st    db ' 61h start=', 0
s_on    db ' on=', 0
s_end   db ' end=', 0
s_po    db ' polls=', 0
s_off   db ' off=', 0
delay   dw 0
dur     dw 0
freq    dw 0
conly   db 0
bell    db 0
v_start db 0
v_on    db 0
v_end   db 0
polls   dd 0
offs    dd 0
len     dw 0
line    times 100 db 0
