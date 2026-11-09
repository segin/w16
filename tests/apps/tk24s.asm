; tk24s.asm - TK24's register-level calls: GetTaskQueueDS / GetTaskQueueES
; (KERNEL.118, 119) entered with known values in every register; records
; the registers afterwards in q_regs (ax bx cx dx si di bp ds es flags).
        bits 16
        cpu 286
segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _DATA
global _q_regs
_q_regs times 10 dw 0
q_ds    dw 0
q_which dw 0
segment _TEXT public align=16 class=CODE use16
extern GETTASKQUEUEDS, GETTASKQUEUEES, GETDOSENVIRONMENT

; void __cdecl __far q_call(int which): 0 GetTaskQueueDS, 1 GetTaskQueueES,
; 2 GetDOSEnvironment (KERNEL.131)
global _q_call
_q_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov [q_ds wrt DGROUP], ds
        mov ax, [bp + 6]
        mov [q_which wrt DGROUP], ax
        mov ax, 1111h
        mov bx, 2222h
        mov cx, 3333h
        mov dx, 4444h
        mov si, 5555h
        mov di, 6666h
        push bp
        mov bp, 7777h
        cmp word [q_which wrt DGROUP], 1
        je .es
        ja .env
        stc
        call far GETTASKQUEUEDS
        jmp .rec
.env:   stc
        call far GETDOSENVIRONMENT
        jmp .rec
.es:    stc
        call far GETTASKQUEUEES
.rec:   pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [_q_regs wrt DGROUP]
        pop word [_q_regs + 14 wrt DGROUP]
        pop word [_q_regs + 18 wrt DGROUP]
        mov [_q_regs + 2 wrt DGROUP], bx
        mov [_q_regs + 4 wrt DGROUP], cx
        mov [_q_regs + 6 wrt DGROUP], dx
        mov [_q_regs + 8 wrt DGROUP], si
        mov [_q_regs + 10 wrt DGROUP], di
        mov [_q_regs + 12 wrt DGROUP], bp
        mov [_q_regs + 16 wrt DGROUP], es
        pop bp
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
