; md2cs.asm - MD2's interrupt-time event procedure and call thunk
; (tests/apps/md2c.c; like md1s.asm, with a time stamp and MS3's sequence
; number in each record instead of SI, DI and SS).
;
; md2_code: an event procedure for the mouse driver's Enable, copied by
; md2c.c into a fixed block at H_OFF and given to Enable as a code alias.
; Each call is recorded (AX BX CX DX, BIOS tick, MS3's next sequence
; number, MD2's cycle number, flags) and passed on, registers unchanged,
; to the procedure at D_NEXT (USER's Mouse_Event).
;
; md2_call: as md1s.asm's md_call.
        bits 16
        cpu 286

D_SEL   equ 0           ; data selector of the block
D_N     equ 2           ; records so far
D_MAX   equ 4           ; room for records
D_NEXT  equ 6           ; far pointer: where calls go on to
D_DEPTH equ 10          ; calls in progress (nesting)
D_NEST  equ 12          ; deepest nesting seen
D_BIOS  equ 14          ; selector of the BIOS data area (segment 40h)
D_M3SEL equ 16          ; selector of MS3's segment (0: none)
D_M3OFF equ 18          ; offset of MS3's header there
D_CYCLE equ 20          ; MD2's current cycle (set by md2c.c)
H_OFF   equ 32          ; the procedure starts here
D_REC   equ 512         ; records (16 bytes each) start here

segment _TEXT public align=16 class=CODE use16
global _md2_code, _md2_len
_md2_len dw h_end - h_start
_md2_code:
h_start:
        push ds
        push es
        push bp
        push ax
        mov ds, [cs:D_SEL]
        inc word [D_DEPTH]
        mov ax, [D_DEPTH]
        cmp ax, [D_NEST]
        jbe .n
        mov [D_NEST], ax
.n:     mov bp, [D_N]
        cmp bp, [D_MAX]
        jae .skip
        inc word [D_N]
        shl bp, 4
        add bp, D_REC
        pop ax
        push ax
        mov [ds:bp], ax
        mov [ds:bp + 2], bx
        mov [ds:bp + 4], cx
        mov [ds:bp + 6], dx
        push si
        mov es, [D_BIOS]
        mov ax, [es:6Ch]
        mov [ds:bp + 8], ax
        xor ax, ax
        cmp word [D_M3SEL], 0
        je .noms
        mov es, [D_M3SEL]
        mov si, [D_M3OFF]
        mov ax, [es:si + 6]             ; MS3's next sequence number
.noms:  mov [ds:bp + 10], ax
        pop si
        mov ax, [D_CYCLE]
        mov [ds:bp + 12], ax
        pushf
        pop word [ds:bp + 14]
.skip:  pop ax
        pop bp
        pop es
        call far [D_NEXT]
        dec word [D_DEPTH]
        pop ds
        retf
h_end:

; DWORD __cdecl __far md2_call(FARPROC f, WORD FAR *args16, WORD FAR *out3)
global _md2_call
_md2_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        lds si, [bp + 10]
        mov cx, 16
.push:  push word [si]
        add si, 2
        loop .push
        mov si, sp
        call far [bp + 6]
        pushf
        pop cx
        mov di, sp
        sub di, si
        mov sp, si
        add sp, 32
        pop es
        pop ds
        push bx
        les bx, [bp + 14]
        mov [es:bx], di
        mov [es:bx + 2], cx
        pop bx
        pop di
        pop si
        pop bp
        retf
