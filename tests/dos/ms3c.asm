; ms3c.asm - MS3C.COM: a real-mode TSR, loaded before WIN, that records the
; PS/2 BIOS traffic of a Windows mouse driver (like MS2, tests/dos/ms2.asm)
; and can put packets of its own choosing into the driver's C207h packet
; handler instead of the BIOS's (black-box observation of MOUSE.DRV:
; tests/apps/md2c.c drives it; tests/apps/md2logs/c/notes.md has the results).
;
; Hooks: INT 15h AH=C2h (every call logged; C207h's handler is kept and
; the BIOS is given MS3's stub instead, so every packet the BIOS delivers
; passes through MS3), INT 74h (IRQ 12, the BIOS's entry: logged).
; INT 66h's vector points at the header below.
;
; Modes (header word MODE, set by MD2 through a data selector):
;   0  pass: each BIOS packet goes to the handler as it is
;   1  substitute: each BIOS packet is a carrier only. MS3 gives the
;      handler packets from its own table instead: up to MULT table
;      entries one after the other within the one BIOS callback, provided
;      HOLDOFF ticks have passed since T0 (MD2 sets T0 when it arms) and
;      GAP ticks since the last delivery; otherwise, or when the table is
;      used up, the BIOS packet is swallowed (the handler is not called).
;   2  swallow: the handler is never called.
; Ticks are the BIOS timer count at 0040:006Ch (18.2 a second).
;
; Records of 8 words: kind, a, b, c, d, tick, INT 15h depth, sequence.
; kind 1 = INT 15h C2xxh in (a = AX, b = BX, c = ES, d = CX),
;      2 = its return (a = AX, b = BX, c = flags),
;      3 = INT 74h in, 4 = INT 74h out,
;      8 = a BIOS callback reached MS3 (a, b, c = status, X, Y; d = mode),
;      5 = MS3 calls the driver's handler (a, b, c = status, X, Y; d =
;          table index, FFFFh in mode 0), 6 = the handler returned
;          (a = flags after it),
;      7 = a BIOS packet swallowed (d = reason: 1 holdoff, 2 table done,
;          3 gap, 4 mode 2, 5 no handler).
; The log stops when full (the first MAXREC records are kept).
        bits 16
        cpu 286
        org 100h

MAXREC  equ 3000
TABMAX  equ 64

start:  jmp init

        align 2
hdr:
count   dw 0                    ; +0
max     dw MAXREC               ; +2
in15    dw 0                    ; +4
seq     dw 0                    ; +6
logp    dw log                  ; +8
mode    dw 0                    ; +10
tabn    dw 0                    ; +12 entries in the table
tabi    dw 0                    ; +14 next entry
mult    dw 1                    ; +16 entries per BIOS callback
holdoff dw 0                    ; +18 ticks after T0 before the first
gap     dw 0                    ; +20 ticks between deliveries
t0      dw 0                    ; +22
tlast   dw 0                    ; +24
magic   dw 'M3'                 ; +26
ndeliv  dw 0                    ; +28 deliveries (BIOS callbacks used)
nswal   dw 0                    ; +30 BIOS packets swallowed
old15   dd 0                    ; +32
old74   dd 0                    ; +36
handler dd 0                    ; +40 the driver's handler
tabp    dw table                ; +44
table:  times TABMAX * 3 dw 0   ; +46 status, X, Y

tick:   push ds
        xor ax, ax
        mov ds, ax
        mov ax, [46Ch]
        pop ds
        ret

; rec: AX = kind; BX, CX, DX, SI = a, b, c, d
rec:    pushf
        cli
        push di
        push es
        push ax
        mov di, [cs:count]
        cmp di, MAXREC
        jae .full
        inc word [cs:count]
        shl di, 4
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
        call tick
        stosw
        mov ax, [cs:in15]
        stosw
        mov ax, [cs:seq]
        stosw
.full:  inc word [cs:seq]
        pop ax
        pop es
        pop di
        popf
        ret

; INT 15h
int15:  cmp ah, 0C2h
        je .c2
        jmp far [cs:old15]
.c2:    pusha
        mov si, cx
        mov cx, bx
        mov bx, ax
        mov dx, es
        mov ax, 1
        call rec
        popa
        inc word [cs:in15]
        cmp ax, 0C207h
        jne .call
        mov [cs:handler], bx
        mov [cs:handler + 2], es
        push es
        push bx
        mov bx, es
        or bx, [cs:handler]
        jz .zero                        ; 0:0 goes to the BIOS as it is
        push cs
        pop es
        mov bx, ps2stub
.zero:  pushf
        call far [cs:old15]
        pop bx
        pop es
        jmp .out
.call:  pushf
        call far [cs:old15]
.out:   dec word [cs:in15]
        pushf
        pusha
        mov bp, sp
        mov dx, [bp + 16]               ; flags out
        mov cx, bx
        mov bx, ax
        mov ax, 2
        xor si, si
        call rec
        popa
        ; return with the flags the BIOS gave (as MS2 does)
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
int74:  pusha
        mov ax, 3
        xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        call rec
        popa
        pushf
        call far [cs:old74]
        pusha
        mov ax, 4
        xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        call rec
        popa
        iret

; call the driver's handler with status BX, X CX, Y DX (index SI logged)
callh:  mov ax, 5
        call rec
        push ds
        push es
        pusha
        push bx
        push cx
        push dx
        push word 0
        call far [cs:handler]
        add sp, 8
        pushf
        pop bx
        mov ax, 6
        xor cx, cx
        xor dx, dx
        xor si, si
        call rec
        popa
        pop es
        pop ds
        ret

; what the BIOS calls instead of the handler: the four
; words, status first pushed ([bp+12]), X [bp+10], Y [bp+8], 0 [bp+6]
ps2stub:
        push bp
        mov bp, sp
        pusha
        mov ax, 8
        mov bx, [bp + 12]
        mov cx, [bp + 10]
        mov dx, [bp + 8]
        mov si, [cs:mode]
        call rec
        mov ax, [cs:handler]
        or ax, [cs:handler + 2]
        mov si, 5
        jz .swal
        mov ax, [cs:mode]
        cmp ax, 1
        je .sub
        mov si, 4
        ja .swal
        ; mode 0: as it is
        mov bx, [bp + 12]
        mov cx, [bp + 10]
        mov dx, [bp + 8]
        mov si, 0FFFFh
        call callh
        jmp .done
.sub:   call tick
        mov di, ax                      ; now
        sub ax, [cs:t0]
        mov si, 1
        cmp ax, [cs:holdoff]
        jb .swal
        mov ax, [cs:tabi]
        mov si, 2
        cmp ax, [cs:tabn]
        jae .swal
        cmp word [cs:ndeliv], 0
        je .go
        mov ax, di
        sub ax, [cs:tlast]
        mov si, 3
        cmp ax, [cs:gap]
        jb .swal
.go:    mov [cs:tlast], di
        inc word [cs:ndeliv]
        mov di, [cs:mult]
.next:  mov si, [cs:tabi]
        cmp si, [cs:tabn]
        jae .done
        inc word [cs:tabi]
        mov ax, si
        shl ax, 1
        add ax, si
        shl ax, 1                       ; index * 6
        xchg ax, si
        mov bx, [cs:table + si]
        mov cx, [cs:table + si + 2]
        mov dx, [cs:table + si + 4]
        mov si, ax
        call callh
        dec di
        jnz .next
        jmp .done
.swal:  inc word [cs:nswal]
        mov ax, 7
        mov bx, [bp + 12]
        mov cx, [bp + 10]
        mov dx, [bp + 8]
        call rec
.done:  popa
        pop bp
        retf

        align 16
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
        mov dx, hdr                     ; INT 66h -> the header
        mov ax, 2566h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, (end_res - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg     db 'MS3 installed', 13, 10, '$'
