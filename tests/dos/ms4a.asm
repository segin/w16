; ms4a.asm - MS4A.COM: a pretend APM BIOS (INT 15h AH=53h, the Advanced
; Power Management BIOS interface, version 1.0) that records every call,
; for finding out who calls MOUSE.DRV's PowerEventProc and with what
; (tests/apps/md2logs/a/notes.md). DOSBox has no APM BIOS; Windows 3.1's
; POWER.DRV wants one. Everything but AH=53h goes on to the real INT 15h.
;
;   MS4 [N]   N (decimal, default 0 = never): the Nth "get PM event"
;             (530Bh) call returns event 0002h (system suspend request);
;             after a "set power state" (5307h) call, the next one returns
;             0003h (normal resume). Every other one says "no event".
; Answers: 5300h installed, version 1.0, 'PM', flags 0 (real-mode
; interface only); 5301h, 5304h-5309h: success; 530Ah: on line, battery
; high, life unknown; 530Bh: as above; anything else: error 86h.
; INT 65h's vector points at the log header: count, max, seq, polls, N,
; state, the old INT 15h, idle calls; at +18 records of 8 words: kind (11 = call: AX BX CX DX in; 12 = return:
; AX BX CX DX out, CF in word 6), the four registers, CF, 0, sequence.
        bits 16
        cpu 8086
        org 100h

MAXREC  equ 600

start:  jmp init

hdr:
count   dw 0
max     dw MAXREC
seq     dw 0
polls   dw 0
trig    dw 0                    ; N
state   dw 0                    ; 0 idle, 1 suspend request given, 2 resume due
old15   dd 0
idle    dw 0                    ; 5305h/5306h calls (counted, not recorded)
log:    times MAXREC * 16 db 0

; rec: AX = kind, BX CX DX SI = words 1-4, DI = word 5
rec:    push bp
        push ax
        mov bp, [cs:count]
        cmp bp, MAXREC
        jae .full
        inc word [cs:count]
        shl bp, 1
        shl bp, 1
        shl bp, 1
        shl bp, 1
        add bp, log
        mov [cs:bp], ax
        mov [cs:bp + 2], bx
        mov [cs:bp + 4], cx
        mov [cs:bp + 6], dx
        mov [cs:bp + 8], si
        mov [cs:bp + 10], di
        mov word [cs:bp + 12], 0
        mov ax, [cs:seq]
        mov [cs:bp + 14], ax
.full:  inc word [cs:seq]
        pop ax
        pop bp
        ret

apmidle: inc word [cs:idle]
        xor ax, ax
        push bp
        mov bp, sp
        and word [bp + 6], 0FFFEh
        pop bp
        iret

int15:  cmp ah, 53h
        je .apm
        jmp far [cs:old15]
.apm:   cmp al, 05h
        je apmidle
        cmp al, 06h
        je apmidle
        push bp
        mov bp, sp                      ; [bp + 6] the caller's flags
        push si
        push di
        push ax
        push bx
        push cx
        push dx
        mov si, dx
        mov dx, cx
        mov cx, bx
        mov bx, ax
        mov ax, 11
        xor di, di
        call rec
        pop dx
        pop cx
        pop bx
        pop ax
        call apm                        ; AX BX CX DX out, DI = CF
        push ax
        push bx
        push cx
        push dx
        mov si, dx
        mov dx, cx
        mov cx, bx
        mov bx, ax
        mov ax, 12
        call rec
        pop dx
        pop cx
        pop bx
        pop ax
        and word [bp + 6], 0FFFEh
        or [bp + 6], di
        pop di
        pop si
        pop bp
        iret

; apm: AX BX CX DX in and out; DI = CF out
apm:    xor di, di
        cmp al, 00h
        jne .n1
        mov ax, 0100h
        mov bx, 504Dh                   ; 'PM'
        xor cx, cx
        ret
.n1:    cmp al, 01h
        je .ok
        cmp al, 04h
        jb .err
        cmp al, 06h
        jbe .ok
        cmp al, 07h
        je .setp
        cmp al, 09h
        jbe .ok
        cmp al, 0Ah
        je .stat
        cmp al, 0Bh
        je .event
.err:   mov ax, 8600h
        mov di, 1
        ret
.ok:    xor ax, ax
        ret
.setp:  cmp word [cs:state], 1
        jne .ok
        mov word [cs:state], 2
        jmp .ok
.stat:  mov bx, 0100h                   ; on line, battery high
        mov cx, 00FFh                   ; life unknown
        xor ax, ax
        ret
.event: inc word [cs:polls]
        cmp word [cs:state], 2
        jne .e1
        mov word [cs:state], 3
        mov bx, 0003h                   ; normal resume
        xor ax, ax
        ret
.e1:    mov ax, [cs:trig]
        or ax, ax
        jz .none
        cmp ax, [cs:polls]
        jne .none
        mov word [cs:state], 1
        mov bx, 0002h                   ; system suspend request
        xor ax, ax
        ret
.none:  mov ax, 8000h                   ; no events pending
        mov di, 1
        ret
end_res:

init:   mov si, 81h                     ; N from the command line
        xor bx, bx
.d:     lodsb
        cmp al, ' '
        je .d
        sub al, '0'
        cmp al, 9
        ja .dn
        cbw
        xchg ax, bx
        mov cx, 10
        mul cx
        add bx, ax
        jmp .d
.dn:    mov [trig], bx
        mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, hdr
        mov ax, 2565h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'MS4 installed (APM 1.0, recorded)', 13, 10, '$'
