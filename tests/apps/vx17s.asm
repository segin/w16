; vx17s.asm - VX17's counters (tests/apps/vx17.c): protected-mode handlers
; for INT 28h, INT 2Fh and INT 31h that count KERNEL's idle calls and
; chain to the previous handler (DPMI 0204h/0205h; black box: only what
; reaches the vectors).
;   _n28     INT 28h made in protected mode
;   _n89     INT 2Fh AX=1689h made in protected mode; _n89b0 those with BX=0
;   _n2f     every other INT 2Fh in protected mode
;   _r28     INT 31h AX=0300h (simulate a real-mode interrupt) for INT 28h
;   _r2f     INT 31h AX=0300h for INT 2Fh; _r89 those whose structure's AX
;            is 1689h
;   _r31     every other INT 31h AX=0300h
; Each a dword. void __far hook_install(void), hook_remove(void),
; vx_mark(WORD n): INT 2Fh AX=B170h+n (VXT.386's mark).
        bits 16
        cpu 386
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _n28, _n89, _n89b0, _n2f, _r28, _r2f, _r89, _r31
_n28    dd 0
_n89    dd 0
_n89b0  dd 0
_n2f    dd 0
_r28    dd 0
_r2f    dd 0
_r89    dd 0
_r31    dd 0
segment _TEXT public align=16 class=CODE use16
old28   dd 0
old2f   dd 0
old31   dd 0
dsel    dw 0

%macro INC32 1
        add word [%1 wrt DGROUP], 1
        adc word [%1 + 2 wrt DGROUP], 0
%endmacro

h28:    push ds
        mov ds, [cs:dsel]
        INC32 _n28
        pop ds
        jmp far [cs:old28]

h2f:    push ds
        mov ds, [cs:dsel]
        cmp ax, 1689h
        jne .o
        INC32 _n89
        test bx, bx
        jnz .x
        INC32 _n89b0
        jmp .x
.o:     INC32 _n2f
.x:     pop ds
        jmp far [cs:old2f]

h31:    cmp ax, 0300h
        jne .go
        push ds
        mov ds, [cs:dsel]
        cmp bl, 28h
        jne .n28
        INC32 _r28
        jmp .x
.n28:   cmp bl, 2Fh
        jne .oth
        INC32 _r2f
        cmp word [es:di + 1Ch], 1689h   ; the structure's AX
        jne .x
        INC32 _r89
        jmp .x
.oth:   INC32 _r31
.x:     pop ds
.go:    jmp far [cs:old31]

extern ALLOCCSTODSALIAS, FREESELECTOR

; void __cdecl __far hook_install(void): old vectors and DGROUP's selector
; written through a data alias of this code segment
global _hook_install
_hook_install:
        push bp
        mov bp, sp
        push si
        push di
        push es
        push cs
        call far ALLOCCSTODSALIAS
        push ax
        mov es, ax
        mov [es:dsel], ds
        mov bl, 28h
        mov si, old28
        call getv
        mov bl, 2Fh
        mov si, old2f
        call getv
        mov bl, 31h
        mov si, old31
        call getv
        pop ax
        push ax
        push ax
        call far FREESELECTOR
        pop ax
        mov ax, 0205h
        mov bl, 28h
        mov cx, cs
        mov dx, h28
        int 31h
        mov ax, 0205h
        mov bl, 2Fh
        mov cx, cs
        mov dx, h2f
        int 31h
        mov ax, 0205h
        mov bl, 31h
        mov cx, cs
        mov dx, h31
        int 31h
        pop es
        pop di
        pop si
        pop bp
        retf

getv:   mov ax, 0204h
        int 31h
        mov [es:si], dx
        mov [es:si + 2], cx
        ret

; void __cdecl __far hook_remove(void): the previous handlers back
global _hook_remove
_hook_remove:
        push bp
        mov bp, sp
        mov ax, 0205h
        mov bl, 31h
        mov cx, [cs:old31 + 2]
        mov dx, [cs:old31]
        int 31h
        mov ax, 0205h
        mov bl, 2Fh
        mov cx, [cs:old2f + 2]
        mov dx, [cs:old2f]
        int 31h
        mov ax, 0205h
        mov bl, 28h
        mov cx, [cs:old28 + 2]
        mov dx, [cs:old28]
        int 31h
        pop bp
        retf

; void __cdecl __far vx_mark(WORD n)
global _vx_mark
_vx_mark:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        and ax, 0Fh
        or ax, 0B170h
        int 2Fh
        pop bp
        retf
