; vx8s.asm - VX8's Ctrl+Alt+Del handler (tests/apps/vx8.c): set with the
; Reboot device's PM API AX=0201h in place of KERNEL's, it records the
; registers and the 8 words on the stack each time it is called (in buf,
; and appended to C:\VX8H.LOG at once: the system may not let VX8 run
; again), then jumps on to KERNEL's handler with everything as it came
; but DS, which it sets to KERNEL's data segment (kds; KERNEL's own 0201h
; call gave Reboot its DS with the sanity byte's offset in SI).
;   void __cdecl __far vx8_setup(WORD __far *buf, void __far *kernel, WORD kds)
;   void __far vx8_handler(void)
; buf: a count, then up to 8 records of 24 words: ax bx cx dx si di bp ds
; es ss sp flags, then 8 stack words (from SS:SP at entry), 4 unused.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16
global _vx8_setup, _vx8_handler

buf_ptr dw 0, 0
kern    dw 0, 0
kds     dw 0

_vx8_setup:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, 000Ah                   ; a data alias of this code segment
        mov bx, cs
        int 31h
        jc .x
        mov es, ax
        mov ax, [bp+6]
        mov [es:buf_ptr], ax
        mov ax, [bp+8]
        mov [es:buf_ptr+2], ax
        mov ax, [bp+10]
        mov [es:kern], ax
        mov ax, [bp+12]
        mov [es:kern+2], ax
        mov ax, [bp+14]
        mov [es:kds], ax
        mov bx, es
        xor ax, ax
        mov es, ax
        mov ax, 0001h
        int 31h
.x:     pop di
        pop si
        pop ds
        pop bp
        retf

_vx8_handler:
        push es
        push di
        push ax
        pushf
        push bp
        mov bp, sp                      ; [bp+2] flags, +4 ax, +6 di, +8 es, +10 the caller's
        les di, [cs:buf_ptr]
        mov ax, [es:di]
        cmp ax, 8
        jae .out
        inc word [es:di]
        push dx
        mov dx, 48
        mul dx
        pop dx
        add di, ax
        add di, 2
        mov ax, [bp+4]
        mov [es:di], ax
        mov [es:di+2], bx
        mov [es:di+4], cx
        mov [es:di+6], dx
        mov [es:di+8], si
        mov ax, [bp+6]
        mov [es:di+10], ax
        mov ax, [bp]
        mov [es:di+12], ax
        mov [es:di+14], ds
        mov ax, [bp+8]
        mov [es:di+16], ax
        mov [es:di+18], ss
        lea ax, [bp+10]
        mov [es:di+20], ax
        mov ax, [bp+2]
        mov [es:di+22], ax
        push si
        push cx
        lea si, [bp+10]
        mov cx, 8
.c:     mov ax, [ss:si]
        mov [es:di+24], ax
        add si, 2
        add di, 2
        loop .c
        sub di, 16                      ; the record's start
        ; append the record to C:\VX8H.LOG
        push bx
        push dx
        push ds
        push es
        pop ds
        mov dx, [cs:buf_ptr]
        add dx, (1 + 8 * 24) * 2        ; the name, after the records (vx8.c)
        mov ax, 3D01h
        int 21h
        jnc .op
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .nf
.op:    mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        push es
        pop ds
        mov dx, di
        mov cx, 48
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.nf:    pop ds
        pop dx
        pop bx
        pop cx
        pop si
.out:   pop bp
        popf
        pop ax
        pop di
        pop es
        cmp word [cs:kds], 0
        je .j
        mov ds, [cs:kds]
.j:     jmp far [cs:kern]
