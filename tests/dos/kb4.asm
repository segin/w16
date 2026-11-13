; kb4.asm - KB4.COM: a real-mode TSR that records what reaches the PC's
; keyboard interfaces in real mode while Windows runs (black-box
; observation of KEYBOARD.DRV, drivers/keyboard/README.md): every INT 9
; (IRQ 1) that reaches the real-mode vector (a protected-mode handler that
; passes the interrupt on makes the DOS extender reflect it here), every
; INT 16h call and every INT 15h AH=4Fh (the BIOS's keyboard intercept,
; which the BIOS's INT 9 makes) and AH=91h/85h. Run it before WIN (a
; batch file); KB2 (tests/apps/kb2.c) writes the log out: INT 65h's
; vector points at the log header.
;
; Records of 8 words: kind, w1, w2, w3, w4, w5, BIOS tick (low word),
; sequence number.
; kind 1 = INT 9 in: w1 = port 60h (read again: the byte the controller
;          holds), w2 = 40:17h/18h, w3 = the BIOS buffer head, w4 = its
;          tail, w5 = 40:96h/97h
; kind 2 = INT 9 out: the same after the BIOS's handler
; kind 3 = INT 16h in: w1 = AX, w2 = BX, w3 = CX, w4 = 40:17h/18h,
;          w5 = 40:97h
; kind 4 = INT 16h out: w1 = AX, w2 = flags, w3 = BX
; kind 5 = INT 15h AH=4Fh in: w1 = AX, w2 = flags; kind 6 = its out (AX,
;          flags)
; kind 7 = INT 15h AH=85h (SysReq) or 91h in: w1 = AX
; The log stops when full (the first MAXREC records are kept).
        bits 16
        cpu 8086
        org 100h

MAXREC  equ 2000

start:  jmp init

hdr:
count   dw 0
max     dw MAXREC
magic   dw 'K4'
seq     dw 0
old9    dd 0
old16   dd 0
old15   dd 0
logp    dw log

; rec: AX = kind; BX, CX, DX, SI, DI = w1..w5 (all kept)
rec:    push es
        push di                         ; w5
        push ax                         ; kind
        mov di, [cs:count]
        cmp di, MAXREC
        jae .full
        inc word [cs:count]
        push cx
        mov cl, 4
        shl di, cl
        pop cx
        add di, log
        push cs
        pop es
        cld
        pop ax
        stosw                           ; kind
        mov ax, bx
        stosw
        mov ax, cx
        stosw
        mov ax, dx
        stosw
        mov ax, si
        stosw
        pop ax
        stosw                           ; w5
        push ds
        xor ax, ax
        mov ds, ax
        mov ax, [46Ch]
        pop ds
        stosw
        mov ax, [cs:seq]
        stosw
        jmp .done
.full:  pop ax
        pop ax
.done:  inc word [cs:seq]
        pop es
        ret

; the BIOS state for INT 9 records: BX = port 60h, CX = 40:17h, DX = head,
; SI = tail, DI = 40:96h
state9: push ds
        xor ax, ax
        mov ds, ax
        in al, 60h
        xor ah, ah
        mov bx, ax
        mov cx, [417h]
        mov dx, [41Ah]
        mov si, [41Ch]
        mov di, [496h]
        pop ds
        ret

int9:   push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        call state9
        mov ax, 1
        call rec
        pushf
        call far [cs:old9]
        call state9
        mov ax, 2
        call rec
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        iret

int16:  push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        xor si, si
        mov ds, si
        mov si, [417h]                  ; w4
        mov di, [496h]                  ; w5
        mov dx, cx                      ; w3 = CX
        mov cx, bx                      ; w2 = BX
        mov bx, ax                      ; w1 = AX
        mov ax, 3
        call rec
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pushf
        call far [cs:old16]
        pushf
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ax
        mov bp, sp
        mov bx, ax                      ; w1 = AX out
        mov cx, [bp + 14]               ; w2 = flags out
        mov dx, [bp + 12]               ; w3 = BX out
        xor si, si
        xor di, di
        mov ax, 4
        call rec
        pop ax
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        popf
        ; return with the flags the BIOS gave
        push bp
        mov bp, sp
        push ax
        pushf
        pop ax
        mov [bp + 6], ax
        pop ax
        pop bp
        iret

int15:  cmp ah, 4Fh
        je .k
        cmp ah, 85h
        je .s
        cmp ah, 91h
        je .s
        jmp far [cs:old15]
.s:     push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        mov bx, ax
        xor cx, cx
        xor dx, dx
        xor si, si
        xor di, di
        mov ax, 7
        call rec
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        jmp far [cs:old15]
.k:     push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        mov bp, sp
        mov bx, ax
        mov cx, [bp + 18]               ; the caller's flags
        xor dx, dx
        xor si, si
        xor di, di
        mov ax, 5
        call rec
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pushf
        call far [cs:old15]
        pushf
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ax
        mov bp, sp
        mov bx, ax
        mov cx, [bp + 14]
        xor dx, dx
        xor si, si
        xor di, di
        mov ax, 6
        call rec
        pop ax
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        popf
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

init:   mov ax, 3509h
        int 21h
        mov [old9], bx
        mov [old9 + 2], es
        mov ax, 3516h
        int 21h
        mov [old16], bx
        mov [old16 + 2], es
        mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov dx, int9
        mov ax, 2509h
        int 21h
        mov dx, int16
        mov ax, 2516h
        int 21h
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, hdr                     ; INT 65h -> the log header
        mov ax, 2565h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'KB4 installed', 13, 10, '$'
