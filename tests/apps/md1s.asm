; md1s.asm - MD1's interrupt-time event procedure and call thunk
; (tests/apps/md1.c, drivers/mouse/README.md).
;
; md_code: an event procedure for the mouse driver's Enable. md1.c copies
; it into a fixed block at H_OFF and gives Enable a code alias of it. It
; records each call's registers (AX BX CX DX SI DI, the flags and SS) in
; the block and passes the call on, registers unchanged, to the procedure
; at D_NEXT (USER's Mouse_Event), so Windows goes on taking the input.
; The data selector of the block is at D_SEL (read through CS).
;
; md_call: call a far procedure with 16 words on the stack (the caller's
; array, first element pushed first: put a Pascal function's arguments at
; the end) and report how many bytes it took off (its RETF n), AX, DX and
; the flags it returned.
        bits 16
        cpu 286

D_SEL   equ 0           ; data selector of the block
D_N     equ 2           ; records so far
D_MAX   equ 4           ; room for records
D_NEXT  equ 6           ; far pointer: where calls go on to
D_DEPTH equ 10          ; calls in progress (nesting)
D_NEST  equ 12          ; deepest nesting seen
H_OFF   equ 16          ; the procedure starts here
D_REC   equ 256         ; records (16 bytes each) start here

segment _TEXT public align=16 class=CODE use16
global _md_code, _md_len
_md_len dw h_end - h_start
_md_code:
h_start:
        push ds
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
        mov [ds:bp + 8], si
        mov [ds:bp + 10], di
        pushf
        pop word [ds:bp + 12]
        mov [ds:bp + 14], ss
.skip:  pop ax
        pop bp
        call far [D_NEXT]
        dec word [D_DEPTH]
        pop ds
        retf
h_end:

; DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args16, WORD FAR *out3)
; out3: bytes popped, flags out, SP-independent; returns DX:AX
global _md_call
_md_call:
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
        sub di, si                      ; bytes the procedure took off
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
