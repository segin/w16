; ms1.asm - MS1.COM: a real-mode TSR that records what reaches the BIOS's
; PS/2 pointing-device interface while Windows boots (black-box probe of
; the DOS extender, the boot-time mouse bug): every INT 15h AH=C2h call,
; every INT 74h (IRQ 12) the BIOS gets, and every call the BIOS makes to
; the pointing-device handler set by C207h (MS1 hands the BIOS a stub of
; its own and calls the real handler from it, with the same four words).
; Run it before WIN (a batch file); TK49 (tests/apps/tk49.c) reads the
; log afterwards: INT 66h's vector points at the log header.
;
; Log header: dw count, dw max, dw in15 (INT 15h depth now), then records
; of 8 words: kind, AX, BX, ES (C207h: the handler's segment), flags at
; entry, INT 15h depth, IVT 74h segment at the time, sequence number.
; kind 1 = INT 15h C2xxh in, 2 = its return (AX = AX out, flags out),
; 3 = INT 74h in, 4 = INT 74h out, 5 = pointing-device handler called
; (AX..ES = the BIOS's first three words), 6 = it returned.
; The log stops when full (the first MAXREC records are kept).
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

; rec: AX = kind; BX, CX, DX = the next three words; SI = flags
rec:    push di
        push es
        push ax
        mov di, [cs:count]
        cmp di, MAXREC
        jae .full
        inc word [cs:count]
        mov cl, 4
        shl di, cl
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
        mov si, [bp + 16]               ; the caller's flags
        mov cx, bx
        mov bx, ax
        mov dx, es
        mov ax, 1
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

log:
        times MAXREC * 16 db 0
end_res:

init:   mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov ax, 3574h
        int 21h
        mov [old74], bx
        mov [old74 + 2], es
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, int74
        mov ax, 2574h
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

msg     db 'MS1 installed', 13, 10, '$'
