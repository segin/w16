; vx9.asm - VX9.COM: how a Windows task's exit reaches DOS in 386 Enhanced
; mode (the handover note's "closing an application"; ARCHITECTURE.md
; 12.3). A real-mode TSR loaded before WIN (tests/vx9run.sh); black box:
; it records registers, stack words and PSP fields, and CS:IP only (never
; code bytes, ARCHITECTURE.md 3).
;
; It hooks INT 21h in real/V86 mode and records:
;   kind 1  AH=4Ch or 00h: AX, the caller's CS:IP and SS:SP, DOS's current
;           PSP, that PSP's terminate address (0Ah), parent (16h) and saved
;           stack (2Eh), and the parent's (as a segment) saved stack and
;           parent field. It then points the PSP's terminate address at its
;           own stub (keeping the original).
;   kind 2  the stub, reached by DOS's terminate: every register, SS:SP and
;           the four words on top of the stack; it then sets TF and goes on
;           to the original terminate address.
;   kind 3  each single step after that (INT 1): CS:IP, SS:SP, AX, flags,
;           up to MAXSTEP steps.
;   kind 4  AH=50h (set PSP): BX, caller CS:IP, current PSP.
;   kind 5  AH=55h (create PSP): DX, SI, caller CS:IP, current PSP.
;   kind 6  AH=51h/62h (get PSP): caller CS:IP, current PSP.
; "VX9" installs ("VX9 S": with the single steps); "VX9 D" (after WIN) writes the resident copy's records
; to VX9.LOG as text. INT 66h's vector points at the resident copy's
; header.
        bits 16
        cpu 386
        org 100h

MAXREC  equ 700
MAXSTEP equ 150
RECW    equ 16                  ; words per record

start:  jmp init

hdr:
sig     db 'VX9!'
count   dw 0
seq     dw 0
steps   dw 0
stepping dw 0
sda     dd 0
old21   dd 0
old01   dd 0
origt   dd 0                    ; the terminate address replaced
npatch  dw 0
stepon  dw 0                    ; "VX9 S": single-step after the terminate

; rec: DI -> 15 words at cs:tmp; AX = kind
rec:    push si
        push di
        push es
        push ds
        push cx
        mov di, [cs:count]
        cmp di, MAXREC
        jae .full
        inc word [cs:count]
        imul di, di, RECW * 2
        add di, log
        push cs
        pop es
        push cs
        pop ds
        cld
        stosw
        mov si, tmp
        mov cx, RECW - 1
        rep movsw
.full:  inc word [cs:seq]
        pop cx
        pop ds
        pop es
        pop di
        pop si
        ret

tmp     times RECW dw 0

; DOS's current PSP -> AX
curpsp: push ds
        push si
        lds si, [cs:sda]
        mov ax, [si + 10h]
        pop si
        pop ds
        ret

; INT 21h. Stack at entry: IP, CS, flags.
int21:  cmp ah, 4Ch
        je .exit
        cmp ah, 00h
        je .exit
        cmp ah, 50h
        je .setpsp
        cmp ah, 55h
        je .newpsp
        cmp ah, 51h
        je .getpsp
        cmp ah, 62h
        je .getpsp
.chain: jmp far [cs:old21]

.setpsp:
        push bp
        mov bp, sp
        push ax
        mov [cs:tmp], bx
        mov ax, [bp + 2]
        mov [cs:tmp + 2], ax
        mov ax, [bp + 4]
        mov [cs:tmp + 4], ax
        call curpsp
        mov [cs:tmp + 6], ax
        mov ax, 4
        call rec
        pop ax
        pop bp
        jmp .chain

.newpsp:
        push bp
        mov bp, sp
        push ax
        mov [cs:tmp], dx
        mov [cs:tmp + 2], si
        mov ax, [bp + 2]
        mov [cs:tmp + 4], ax
        mov ax, [bp + 4]
        mov [cs:tmp + 6], ax
        call curpsp
        mov [cs:tmp + 8], ax
        mov ax, 5
        call rec
        pop ax
        pop bp
        jmp .chain

.getpsp:
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 2]
        mov [cs:tmp], ax
        mov ax, [bp + 4]
        mov [cs:tmp + 2], ax
        call curpsp
        mov [cs:tmp + 4], ax
        mov ax, 6
        call rec
        pop ax
        pop bp
        jmp .chain

.exit:  push bp
        mov bp, sp
        push ax
        push ds
        push si
        mov [cs:tmp], ax
        mov ax, [bp + 2]
        mov [cs:tmp + 2], ax            ; IP
        mov ax, [bp + 4]
        mov [cs:tmp + 4], ax            ; CS
        mov [cs:tmp + 6], ss
        lea ax, [bp + 8]
        mov [cs:tmp + 8], ax            ; SP as the caller had it
        call curpsp
        mov [cs:tmp + 10], ax
        mov ds, ax
        mov ax, [0Ah]
        mov [cs:tmp + 12], ax
        mov ax, [0Ch]
        mov [cs:tmp + 14], ax
        mov si, [16h]
        mov [cs:tmp + 16], si
        mov ax, [2Eh]
        mov [cs:tmp + 18], ax
        mov ax, [30h]
        mov [cs:tmp + 20], ax
        mov ds, si
        mov ax, [2Eh]
        mov [cs:tmp + 22], ax
        mov ax, [30h]
        mov [cs:tmp + 24], ax
        mov ax, [16h]
        mov [cs:tmp + 26], ax
        mov ax, [0Ah]
        mov [cs:tmp + 28], ax
        mov ax, 1
        call rec
        ; the terminate address -> the stub (the first exit only)
        cmp word [cs:npatch], 0
        jne .nopatch
        inc word [cs:npatch]
        call curpsp
        mov ds, ax
        mov ax, [0Ah]
        mov [cs:origt], ax
        mov ax, [0Ch]
        mov [cs:origt + 2], ax
        mov word [0Ah], stub
        mov [0Ch], cs
.nopatch:
        pop si
        pop ds
        pop ax
        pop bp
        jmp .chain

; DOS's terminate lands here (flags F202h on its IRET)
stub:   mov [cs:tmp], ax
        mov [cs:tmp + 2], bx
        mov [cs:tmp + 4], cx
        mov [cs:tmp + 6], dx
        mov [cs:tmp + 8], si
        mov [cs:tmp + 10], di
        mov [cs:tmp + 12], bp
        mov [cs:tmp + 14], ds
        mov [cs:tmp + 16], es
        mov [cs:tmp + 18], ss
        mov [cs:tmp + 20], sp
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 2]
        mov [cs:tmp + 22], ax
        mov ax, [bp + 4]
        mov [cs:tmp + 24], ax
        mov ax, [bp + 6]
        mov [cs:tmp + 26], ax
        mov ax, [bp + 8]
        mov [cs:tmp + 28], ax
        mov ax, 2
        call rec
        pop ax
        pop bp
        mov word [cs:steps], 0
        mov word [cs:stepping], 0
        ; flags with IF (and TF when asked), then the original terminate
        ; address
        cmp word [cs:stepon], 0
        je .nostep
        mov word [cs:stepping], 1
        push word 0F302h
        jmp .go
.nostep:
        push word 0F202h
.go:
        push word [cs:origt + 2]
        push word [cs:origt]
        iret

; INT 1: a single step. Stack: IP, CS, flags.
int01:  cmp word [cs:stepping], 0
        je .chain
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 2]
        mov [cs:tmp], ax
        mov ax, [bp + 4]
        mov [cs:tmp + 2], ax
        mov [cs:tmp + 4], ss
        lea ax, [bp + 8]
        mov [cs:tmp + 6], ax
        mov ax, [bp - 2]
        mov [cs:tmp + 8], ax
        mov ax, [bp + 6]
        mov [cs:tmp + 10], ax
        mov [cs:tmp + 12], ds
        mov [cs:tmp + 14], es
        mov ax, 3
        call rec
        inc word [cs:steps]
        cmp word [cs:steps], MAXSTEP
        jb .more
        mov word [cs:stepping], 0
        and word [bp + 6], 0FEFFh       ; TF off
.more:  pop ax
        pop bp
        iret
.chain: jmp far [cs:old01]

log:
; ---------------------------------------------------------------------------
init:   mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        and al, 0DFh
        cmp al, 'D'
        je dump
        cmp al, 'S'
        jne .ins
        mov word [stepon], 1
.ins:
        mov ax, 5D06h
        push ds
        int 21h                         ; DS:SI = the swappable data area
        mov ax, ds
        pop ds
        mov [sda], si
        mov [sda + 2], ax
        mov ax, 3521h
        int 21h
        mov [old21], bx
        mov [old21 + 2], es
        mov ax, 3501h
        int 21h
        mov [old01], bx
        mov [old01 + 2], es
        mov ax, 2521h
        mov dx, int21
        int 21h
        mov ax, 2501h
        mov dx, int01
        int 21h
        mov ax, 2566h
        mov dx, hdr
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        mov dx, log + MAXREC * RECW * 2 + 15
        shr dx, 4
        mov ax, 3100h
        int 21h

msg     db 'VX9 installed', 13, 10, '$'
fname   db 'VX9.LOG', 0
hexd    db '0123456789ABCDEF'
line    times 120 db 0

dump:   push ds
        xor ax, ax
        mov ds, ax
        lds bx, [66h * 4]
        mov ax, ds
        pop ds
        mov [rseg], ax
        mov ah, 3Ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc .out
        mov [fh], ax
        xor bp, bp                      ; record index
.next:  push ds
        mov ds, [rseg]
        mov ax, [count]
        pop ds
        cmp bp, ax
        jae .done
        mov di, line
        push ds
        pop es
        mov ax, bp
        imul si, ax, RECW * 2
        add si, log
        mov cx, RECW
.w:     push ds
        mov ds, [cs:rseg]
        lodsw
        pop ds
        call hex4
        mov al, ' '
        stosb
        loop .w
        mov ax, 0A0Dh
        stosw
        mov cx, di
        sub cx, line
        mov dx, line
        mov bx, [fh]
        mov ah, 40h
        int 21h
        inc bp
        jmp .next
.done:  mov bx, [fh]
        mov ah, 3Eh
        int 21h
.out:   mov ax, 4C00h
        int 21h

; AX -> 4 hex digits at ES:DI
hex4:   push cx
        push bx
        mov cx, 4
.d:     rol ax, 4
        mov bx, ax
        and bx, 15
        push ax
        mov al, [hexd + bx]
        stosb
        pop ax
        loop .d
        pop bx
        pop cx
        ret

rseg    dw 0
fh      dw 0
