; ms5.asm - MS5.COM: a real-mode TSR that counts how often Windows' idle
; calls reach real mode: INT 28h (DOS idle) and INT 2Fh AX=1689h (the
; kernel idle call), and how many INT 74h (IRQ 12) the real-mode vector
; chain gets. Black-box observation of a KERNEL's idle loop (w16's against
; Windows 3.1's; drivers/mouse/README.md). Run it before WIN (a batch
; file); TK49 (tests/apps/tk49.c) writes the counts out: INT 66h points at
; a header laid out as MS1's and MS2's, with three records of 8 words:
; kind (28h, 2Fh, 74h), the count (low word, high word), zeros.
        bits 16
        cpu 8086
        org 100h

start:  jmp init

hdr:
count   dw 3
max     dw 3
        dw 0, 0
        dd 0, 0, 0
logp    dw log
log:
rec28     dw 28h, 0, 0, 0, 0, 0, 0, 0
rec2f     dw 2Fh, 0, 0, 0, 0, 0, 0, 0
rec74     dw 74h, 0, 0, 0, 0, 0, 0, 0
old28   dd 0
old2f   dd 0
old74   dd 0

int28:  add word [cs:rec28 + 2], 1
        adc word [cs:rec28 + 4], 0
        jmp far [cs:old28]
int2f:  cmp ax, 1689h
        jne .go
        add word [cs:rec2f + 2], 1
        adc word [cs:rec2f + 4], 0
.go:    jmp far [cs:old2f]
int74:  add word [cs:rec74 + 2], 1
        adc word [cs:rec74 + 4], 0
        jmp far [cs:old74]
end_res:

init:   mov ax, 3528h
        int 21h
        mov [old28], bx
        mov [old28 + 2], es
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        mov ax, 3574h
        int 21h
        mov [old74], bx
        mov [old74 + 2], es
        mov dx, int28
        mov ax, 2528h
        int 21h
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        mov dx, int74
        mov ax, 2574h
        int 21h
        mov dx, hdr
        mov ax, 2566h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'MS5 installed', 13, 10, '$'
