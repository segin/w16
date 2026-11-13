; kb1s.asm - KB1's register-level helpers (tests/apps/kb1.c,
; drivers/keyboard/README.md).
;
; kb_callx: call a far procedure with 16 words on the stack (as md_call in
; md1s.asm: first element pushed first) with marker values in every
; register the caller can set (AX 1111h, BX 2222h, CX 3333h, DX 4444h,
; SI 5555h, DI 6666h, BP = out's offset, DS = ES = out's segment), and
; report what came back: out[0] bytes taken off, out[1] flags, out[2..9]
; AX BX CX DX SI DI DS ES, out[10] BP. out must be in DGROUP (= SS: the
; program's own data); out[11..16] are kb_callx's scratch.
;
; kb_event: an event procedure for KEYBOARD's Enable that records nothing
; and returns (KB1 enables the driver with it only after its input-less
; tests; Windows' own input is gone then).
        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

; void __cdecl __far kb_callx(FARPROC f, WORD FAR *args16, WORD FAR *out11)
global _kb_callx
_kb_callx:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        les di, [bp + 14]               ; out (its segment = SS: on the
        mov [es:di + 22], sp            ; caller's stack)
        mov [es:di + 24], bp
        mov [es:di + 26], ds
        mov ax, [bp + 6]
        mov [es:di + 28], ax
        mov ax, [bp + 8]
        mov [es:di + 30], ax
        mov [ss:outoff wrt DGROUP], di
        lds si, [bp + 10]
        mov cx, 16
.push:  push word [si]
        add si, 2
        loop .push
        mov [es:di + 32], sp            ; SP with the arguments pushed
        mov bp, di
        push es
        pop ds
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        call far [ds:bp + 28]
        pushf
        push bp
        push es
        push ds
        push di
        push si
        push dx
        push cx
        push bx
        push ax
        mov bp, [ss:outoff wrt DGROUP]  ; out is in SS
        mov ax, sp
        add ax, 20                      ; SP right after the RETF
        mov si, ax
        sub si, [ss:bp + 32]            ; bytes taken off
        mov [ss:bp + 0], si
        pop word [ss:bp + 4]            ; AX
        pop word [ss:bp + 6]            ; BX
        pop word [ss:bp + 8]            ; CX
        pop word [ss:bp + 10]           ; DX
        pop word [ss:bp + 12]           ; SI
        pop word [ss:bp + 14]           ; DI
        pop word [ss:bp + 16]           ; DS
        pop word [ss:bp + 18]           ; ES
        pop word [ss:bp + 20]           ; BP
        pop word [ss:bp + 2]            ; flags
        mov sp, [ss:bp + 22]
        mov ds, [ss:bp + 26]
        mov bp, [ss:bp + 24]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

segment _DATA public align=2 class=DATA use16
group DGROUP _DATA
outoff  dw 0

segment _TEXT
; the do-nothing event procedure
global _kb_event
_kb_event:
        retf

; void __cdecl __far get_vec9(WORD FAR *v): v[0] = selector, v[1] = offset
; of the protected-mode INT 9 vector (INT 21h AH=35h)
global _get_vec9
_get_vec9:
        push bp
        mov bp, sp
        push di
        push es
        mov ax, 3509h
        int 21h
        mov dx, es
        les di, [bp + 6]
        mov [es:di], dx
        mov [es:di + 2], bx
        pop es
        pop di
        pop bp
        retf
