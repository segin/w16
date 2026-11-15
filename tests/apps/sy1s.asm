; sy1s.asm - SY1's register-level helpers (tests/apps/sy1.c,
; drivers/system/README.md).
;
; sy_code: timer procedures for CreateSystemTimer and an INT 8 hook. sy1.c
; copies the code into a fixed block at H_OFF and hands out a code alias
; of it, so the procedures run from fixed memory at interrupt time and
; reach their data (the same block) through the data selector at D_SEL
; (read through CS). Procedure i (0-15) is at H_OFF + 5 * i; each call
; records (24 bytes at D_REC): the procedure number, AX, the flags, DS,
; ES, SS, SP on entry, the BIOS tick count's low word, the PIC's ISR (low
; byte) and IMR (high), the nesting depth, and GetSystemMsecCount's DX:AX
; (through D_MSEC when set). Every register is kept for the caller.
; Options in the block: D_SPINSLOT (the procedure that, on its first call,
; enables interrupts and waits up to 3 BIOS ticks: reentrancy), D_SELF*
; (a procedure that kills its own timer on its Nth call).
; The INT 8 hook at H_OFF + sy_i8off counts interrupts (D_I8CNT) and goes
; on to D_I8PREV.
        bits 16
        cpu 286

D_SEL      equ 0
D_N        equ 2
D_MAX      equ 4
D_DEPTH    equ 6
D_MAXDEP   equ 8
D_I8CNT    equ 10
D_I8PREV   equ 12           ; far
D_MSEC     equ 16           ; far: GetSystemMsecCount or 0
D_KILL     equ 20           ; far: KillSystemTimer
D_SELFSLOT equ 24
D_SELFH    equ 26
D_SELFN    equ 28
D_SELFRES  equ 30
D_SPINSLOT equ 32
D_SPINDONE equ 34
D_SPINTK   equ 36           ; ticks seen to pass during the wait
D_SPINDEP  equ 38           ; deepest nesting during it
D_0040     equ 40
D_SPINN    equ 42           ; calls of any procedure during the wait
D_I8TICK   equ 44           ; the BIOS tick count when the INT 8 hook last ran
D_CNT      equ 48           ; 16 words: calls of each procedure
H_OFF      equ 96
D_REC      equ 1024
RSZ        equ 26

segment _TEXT public align=16 class=CODE use16
extern __0000H                  ; KERNEL.183

global _sy_code, _sy_len, _sy_i8off
_sy_len   dw h_end - h_start
_sy_i8off dw i8hook - h_start
_sy_code:
h_start:
%assign i 0
%rep 16
        push byte i
        jmp near cbcommon
%assign i i + 1
%endrep
cbcommon:
        push bp
        mov bp, sp                      ; [bp+2] procedure, [bp+4] return
        pushf
        pusha
        push ds
        push es
        ; [bp-2] flags, -4 AX, -6 CX, -8 DX, -10 BX, -12 SP, -14 BP,
        ; -16 SI, -18 DI, -20 DS, -22 ES
        mov ds, [cs:D_SEL]
        inc word [D_DEPTH]
        mov ax, [D_DEPTH]
        cmp ax, [D_MAXDEP]
        jbe .d
        mov [D_MAXDEP], ax
.d:     cmp word [D_SPINDONE], 1        ; a wait in progress: counted
        jne .ns
        inc word [D_SPINN]
.ns:    mov bx, [bp + 2]
        shl bx, 1
        inc word [D_CNT + bx]
        mov bx, [D_N]
        cmp bx, [D_MAX]
        jae .norec
        inc word [D_N]
        imul bx, bx, RSZ
        add bx, D_REC
        mov ax, [bp + 2]
        mov [bx], ax
        mov ax, [bp - 4]
        mov [bx + 2], ax                ; AX
        mov ax, [bp - 2]
        mov [bx + 4], ax                ; flags
        mov ax, [bp - 20]
        mov [bx + 6], ax                ; DS
        mov ax, [bp - 22]
        mov [bx + 8], ax                ; ES
        mov [bx + 10], ss
        lea ax, [bp + 4]
        mov [bx + 12], ax               ; SP on entry
        mov es, [D_0040]
        mov ax, [es:6Ch]
        mov [bx + 14], ax
        pushf                           ; (interrupts off: an interrupt
        cli                             ; between the two OCW3 writes would
        mov al, 0Bh                     ; find the PIC reading its ISR)
        out 20h, al                     ; OCW3: read the ISR
        in al, 20h
        mov ah, al
        mov al, 0Ah                     ; back to the IRR
        out 20h, al
        in al, 21h
        popf
        xchg al, ah
        mov [bx + 16], ax
        mov ax, [D_DEPTH]
        mov [bx + 18], ax
        mov ax, [D_I8TICK]
        mov [bx + 24], ax               ; +24 the INT 8 hook's tick count
        mov word [bx + 20], 0
        mov word [bx + 22], 0
        cmp word [D_MSEC + 2], 0
        je .norec
        push bx
        call far [D_MSEC]
        pop bx
        mov [bx + 20], ax
        mov [bx + 22], dx
.norec:
        ; the reentrancy wait
        mov ax, [bp + 2]
        cmp ax, [D_SPINSLOT]
        jne .nospin
        cmp word [D_SPINDONE], 0
        jne .nospin
        mov word [D_SPINDONE], 1
        mov word [D_SPINN], 0
        mov ax, [D_DEPTH]
        mov [D_SPINDEP], ax
        mov es, [D_0040]
        mov dx, [es:6Ch]
        sti
        mov si, 400                     ; at most 400 x 65536 rounds
.w1:    xor cx, cx
.w2:    mov ax, [es:6Ch]
        sub ax, dx
        cmp ax, 3
        jae .wd
        mov ax, [D_DEPTH]
        cmp ax, [D_SPINDEP]
        jbe .w3
        mov [D_SPINDEP], ax
.w3:    loop .w2
        dec si
        jnz .w1
.wd:    cli
        mov ax, [es:6Ch]
        sub ax, dx
        mov [D_SPINTK], ax
        mov word [D_SPINDONE], 2
.nospin:
        ; the self-killing procedure
        mov ax, [bp + 2]
        cmp ax, [D_SELFSLOT]
        jne .nok
        mov bx, ax
        shl bx, 1
        mov ax, [D_CNT + bx]
        cmp ax, [D_SELFN]
        jne .nok
        push word [D_SELFH]
        call far [D_KILL]
        mov [D_SELFRES], ax
.nok:   dec word [D_DEPTH]
        pop es
        pop ds
        popa
        popf
        pop bp
        add sp, 2
        retf

i8hook:
        push ds
        push ax
        mov ds, [cs:D_SEL]
        inc word [D_I8CNT]
        push ds
        mov ds, [D_0040]
        mov ax, [6Ch]
        pop ds
        mov [D_I8TICK], ax
        pop ax
        pop ds
        jmp far [cs:D_I8PREV]
h_end:

; WORD __cdecl get_0000h(void)
global _get_0000h
_get_0000h:
        mov ax, __0000H
        retf

; void __cdecl __far get_vec(WORD n, WORD FAR *v): v[0] selector, v[1]
; offset of protected-mode vector n (INT 21h AH=35h)
global _get_vec
_get_vec:
        push bp
        mov bp, sp
        push di
        push es
        mov al, [bp + 6]
        mov ah, 35h
        int 21h
        mov dx, es
        les di, [bp + 8]
        mov [es:di], dx
        mov [es:di + 2], bx
        pop es
        pop di
        pop bp
        retf

; void __cdecl __far set_vec(WORD n, WORD sel, WORD off)
global _set_vec
_set_vec:
        push bp
        mov bp, sp
        push ds
        mov al, [bp + 6]
        mov ds, [bp + 8]
        mov dx, [bp + 10]
        mov ah, 25h
        int 21h
        pop ds
        pop bp
        retf

; WORD __cdecl __far pit_read(void): channel 0's count, latched
global _pit_read
_pit_read:
        pushf
        cli
        xor al, al
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        popf
        retf

; WORD __cdecl __far get_flags(void)
global _get_flags
_get_flags:
        pushf
        pop ax
        retf

segment _DATA public align=2 class=DATA use16
group DGROUP _DATA
cw      dw 0
v1      dw 1234h
v2      dw 0FEDCh

segment _TEXT
; void __cdecl __far fpu_prep(void): a known coprocessor state: the
; control word 0362h (rounding up, double precision, some exceptions
; unmasked: none raised), then 1, pi, 1234h, -124h loaded
global _fpu_prep
_fpu_prep:
        fninit
        mov word [cw wrt DGROUP], 0362h
        fldcw [cw wrt DGROUP]
        fld1
        fldpi
        fild word [v1 wrt DGROUP]
        fild word [v2 wrt DGROUP]
        fwait
        retf

; void __cdecl __far fpu_env(WORD FAR *out): out[0..6] = FSTENV (control,
; status, tag, IP, CS, operand offset, operand selector), the
; environment then put back as it was (FSTENV masks the exceptions);
; out[7..11] = ST(0) as 80 bits (when the tag says it is not empty)
global _fpu_env
_fpu_env:
        push bp
        mov bp, sp
        push es
        push bx
        les bx, [bp + 6]
        fnstenv [es:bx]
        fwait
        fldenv [es:bx]
        mov word [es:bx + 14], 0
        mov word [es:bx + 16], 0
        mov word [es:bx + 18], 0
        mov word [es:bx + 20], 0
        mov word [es:bx + 22], 0
        mov ax, [es:bx + 2]             ; status: TOP in bits 11-13
        shr ax, 11
        and ax, 7
        mov cx, ax
        shl cx, 1
        mov ax, [es:bx + 4]             ; tag word
        shr ax, cl
        and ax, 3
        cmp ax, 3
        je .e
        fld st0
        fstp tword [es:bx + 14]
        fwait
.e:     pop bx
        pop es
        pop bp
        retf

; void __cdecl __far fpu_init(void)
global _fpu_init
_fpu_init:
        fninit
        retf

; WORD __cdecl __far pic_state(void): the master PIC's ISR (low byte) and
; IMR (high byte)
global _pic_state
_pic_state:
        pushf
        cli
        mov al, 0Bh
        out 20h, al
        in al, 20h
        mov ah, al
        mov al, 0Ah
        out 20h, al
        in al, 21h
        xchg al, ah
        popf
        retf
