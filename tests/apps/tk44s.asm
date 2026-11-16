; tk44s.asm - TK44's counters on the protected-mode INT 08h and 09h:
; each increments a word and chains to the previous handler.
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _n08, _n09
_n08    dw 0
_n09    dw 0
segment _TEXT public align=16 class=CODE use16
old08   dd 0
old09   dd 0
dsel    dw 0

h08:    push ds
        mov ds, [cs:dsel]
        inc word [_n08 wrt DGROUP]
        pop ds
        jmp far [cs:old08]
h09:    push ds
        mov ds, [cs:dsel]
        inc word [_n09 wrt DGROUP]
        pop ds
        jmp far [cs:old09]

; void __cdecl __far hook_install(void): writes old08/old09/dsel through
; a data alias of this code segment (AllocCStoDSAlias)
extern ALLOCCSTODSALIAS
global _hook_install
_hook_install:
        push bp
        mov bp, sp
        push si
        push di
        push es
        push cs
        call far ALLOCCSTODSALIAS
        mov es, ax
        mov [es:dsel], ds
        mov ax, 0204h
        mov bl, 08h
        int 31h
        mov [es:old08], dx
        mov [es:old08 + 2], cx
        mov ax, 0204h
        mov bl, 09h
        int 31h
        mov [es:old09], dx
        mov [es:old09 + 2], cx
        mov ax, 0205h
        mov bl, 08h
        mov cx, cs
        mov dx, h08
        int 31h
        mov ax, 0205h
        mov bl, 09h
        mov cx, cs
        mov dx, h09
        int 31h
        pop es
        pop di
        pop si
        pop bp
        retf
