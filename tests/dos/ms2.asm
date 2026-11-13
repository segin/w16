; ms2.asm - MS2.COM: a real-mode TSR that records what a Windows mouse
; driver asks of the PC's mouse interfaces while Windows runs (black-box
; observation of MOUSE.DRV, drivers/mouse/README.md): every INT 15h
; AH=C2h call (the PS/2 BIOS pointing-device interface), every INT 74h
; (IRQ 12) the BIOS gets, every call the BIOS makes to the C207h handler,
; and every INT 33h call (an MS-DOS mouse driver: DOSBox has one built
; in). Like MS1 (tests/dos/ms1.asm) but with INT 33h. Run it before WIN
; (a batch file); TK49 (tests/apps/tk49.c) writes the log out: INT 66h's
; vector points at the log header (the same layout as MS1's).
;
; Records of 8 words: kind, AX, BX, w3, w4, INT 15h depth, IVT 74h
; segment at the time, sequence number.
; kind 1 = INT 15h C2xxh in (w3 = ES, w4 = flags), 2 = its return (AX out,
;          w4 = flags out),
; kind 3 = INT 74h in, 4 = INT 74h out,
; kind 5 = the C207h handler called (AX, BX, w3 = the BIOS's first three
;          words: status, X, Y), 6 = it returned,
; kind 7 = INT 33h in (w3 = CX, w4 = DX), 8 = its return (AX, BX, CX, DX
;          out), 9 = INT 33h in, ES (AX = ES, BX = SI, w3 = DI).
; The log stops when full (the first MAXREC records are kept); with "R"
; on the command line (MS2 R) it wraps instead and keeps the last MAXREC
; (count stays at MAXREC; the sequence numbers give the order).
        bits 16
        cpu 8086
        org 100h

MAXREC  equ 1500

start:  jmp init

hdr:
count   dw 0
max     dw MAXREC
in15    dw 0
seq     dw 0
old15   dd 0
old74   dd 0
handler dd 0                    ; the real pointing-device handler
logp    dw log                  ; where the records are (header + 20)
old33   dd 0
ring    dw 0                    ; "R": wrap around
widx    dw 0                    ; the next record's index when wrapping

; rec: AX = kind; BX, CX, DX = the next three words
rec:    push di
        push es
        push ax
        cmp word [cs:ring], 0
        je .once
        mov di, [cs:widx]               ; wrapping: the next slot
        inc word [cs:widx]
        cmp word [cs:widx], MAXREC
        jb .cnt
        mov word [cs:widx], 0
.cnt:   cmp word [cs:count], MAXREC
        jae .slot
        inc word [cs:count]
        jmp .slot
.once:  mov di, [cs:count]
        cmp di, MAXREC
        jae .full
        inc word [cs:count]
.slot:  push cx
        mov cl, 4
        shl di, cl
        pop cx
        add di, log
        push cs
        pop es
        pop ax
        push ax
        cld
        stosw
        mov ax, bx
        stosw
        mov ax, cx
        stosw
        mov ax, dx
        stosw
        mov ax, si
        stosw
        mov ax, [cs:in15]
        stosw
        push ds
        xor ax, ax
        mov ds, ax
        mov ax, [74h * 4 + 2]
        pop ds
        stosw
        mov ax, [cs:seq]
        stosw
.full:  inc word [cs:seq]
        pop ax
        pop es
        pop di
        ret

; INT 15h
int15:  cmp ah, 0C2h
        je .c2
        jmp far [cs:old15]
.c2:    push ax
        push bx
        push cx
        push dx
        push si
        push bp
        mov bp, sp
        mov bx, ax
        mov ax, 1
        mov cx, [bp + 8]                ; BX
        mov dx, es
        mov si, [bp + 16]               ; the caller's flags
        call rec
        pop bp
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        inc word [cs:in15]
        cmp ax, 0C207h
        jne .call
        mov [cs:handler], bx
        mov [cs:handler + 2], es
        push es
        push bx
        push cs
        pop es
        mov bx, ps2stub
        pushf
        call far [cs:old15]
        pop bx
        pop es
        jmp .out
.call:  pushf
        call far [cs:old15]
.out:   dec word [cs:in15]
        pushf                           ; the flags out
        push bp
        push si
        push bx
        push cx
        push dx
        push ax
        mov bp, sp
        mov si, [bp + 12]
        mov bx, ax
        mov ax, 2
        xor cx, cx
        xor dx, dx
        call rec
        pop ax
        pop dx
        pop cx
        pop bx
        pop si
        pop bp
        ; return with the flags the BIOS gave (CF)
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 2]
        mov [bp + 8], ax
        pop ax
        pop bp
        add sp, 2
        iret

; INT 74h: the BIOS's IRQ 12 handler
int74:  push ax
        push bx
        push cx
        push dx
        push si
        push bp
        mov bp, sp
        mov si, [bp + 16]
        mov ax, 3
        xor bx, bx
        xor cx, cx
        xor dx, dx
        call rec
        pop bp
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pushf
        call far [cs:old74]
        push ax
        push bx
        push cx
        push dx
        push si
        pushf
        pop si
        mov ax, 4
        xor bx, bx
        xor cx, cx
        xor dx, dx
        call rec
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        iret

; what the BIOS calls instead of the handler: [sp+4..] the four words
ps2stub:
        push bp
        mov bp, sp
        push ax
        push bx
        push cx
        push dx
        push si
        pushf
        pop si
        mov ax, 5
        mov bx, [bp + 12]               ; the first word pushed (status)
        mov cx, [bp + 10]
        mov dx, [bp + 8]
        call rec
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        call far [cs:handler]
        add sp, 8
        push ax
        push bx
        push cx
        push dx
        push si
        pushf
        pop si
        mov ax, 6
        xor bx, bx
        xor cx, cx
        xor dx, dx
        call rec
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pop bp
        retf

; INT 33h: the MS-DOS mouse driver's interface
int33:  push ax
        push bx
        push cx
        push dx
        push si
        push bp
        mov bp, sp
        mov si, dx                      ; w4 = DX
        mov dx, cx                      ; w3 = CX
        mov cx, bx
        mov bx, ax
        mov ax, 7
        call rec
        mov ax, 9
        mov bx, es
        mov cx, [bp + 2]                ; SI
        mov dx, di
        call rec
        pop bp
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pushf
        call far [cs:old33]
        push ax
        push bx
        push cx
        push dx
        push si
        push bp
        mov si, dx
        mov dx, cx
        mov cx, bx
        mov bx, ax
        mov ax, 8
        call rec
        pop bp
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ; the flags out as the driver left them
        push bp
        mov bp, sp
        push ax
        pushf
        pop ax
        mov [bp + 6], ax
        pop ax
        pop bp
        iret

log:
        times MAXREC * 16 db 0
end_res:

init:   mov si, 81h                     ; "R" in the command tail: wrap
.arg:   lodsb
        cmp al, 13
        je .args
        and al, 0DFh
        cmp al, 'R'
        jne .arg
        mov word [ring], 1
.args:  mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov ax, 3574h
        int 21h
        mov [old74], bx
        mov [old74 + 2], es
        mov ax, 3533h
        int 21h
        mov [old33], bx
        mov [old33 + 2], es
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, int74
        mov ax, 2574h
        int 21h
        mov dx, int33
        mov ax, 2533h
        int 21h
        mov dx, hdr                     ; INT 66h -> the log header
        mov ax, 2566h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'MS2 installed', 13, 10, '$'
