; thps.asm - the assembly parts of THP.EXE and THC.EXE, the TOOLHELP.DLL
; probe and its child (tests/toolhelp/thp.c, thc.c; ARCHITECTURE.md 22):
;  - st_run: Windows stack frames of known kinds for StackTraceCSIPFirst/
;    StackTraceNext (far frames with an odd and an even saved BP, a near
;    frame), then st_walk(SS, CS, IP, BP) from the innermost one;
;  - int_cb: an InterruptRegister callback that records the frame TOOLHELP
;    built and then chains, restarts or ends the task (int_mode);
;  - sw_routine: the code TaskSwitch/TaskSetCSIP make a task run: it
;    records which task runs it and goes on at sw_back (a far address);
;  - faults: the instructions the child faults with, each followed by a
;    label so that a restart can skip it (fault_len).
; Facts used: the callback frame and its return conventions, Windows 3.1
; SDK Programmer's Reference Vol. 2 (InterruptRegister); TaskSwitch, Vol.
; 2 and Schulman et al., Undocumented Windows (1992) ch. 10 (facts only).
        bits 16
        cpu 286

segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS
segment _TEXT public align=16 class=CODE use16

extern _st_walk                 ; void __cdecl __far st_walk(ss, cs, ip, bp)

segment _DATA
; int_cb: what to do (0 chain, 1 restart after int_skip bytes, 2
; TerminateApp(0, NO_UAE_BOX), 3 TerminateApp(0, UAE_BOX)); its records:
; 16 words each: [0] the callback's own AX at entry, [1..11] the frame
; (IP CS of the return into TOOLHELP, AX, number, handle, IP CS FLAGS of
; the fault, SS SP when there are), [12] SS, [13] SP at entry, [14] the
; current task, [15] DS at entry
global _int_mode, _int_skip, _int_count, _int_rec, _int_task_fn, _int_term_fn
global _sw_back, _sw_count, _sw_rec, _int_tag
_int_mode       dw 0
_int_skip       dw 0
_int_count      dw 0
_int_tag        dw 0            ; which program's callback (THP 1, THC 2)
_int_task_fn    dd 0            ; GetCurrentTask (KERNEL.36)
_int_term_fn    dd 0            ; TerminateApp (TOOLHELP.77)
_int_rec        times 16*16 dw 0
; sw_routine: the address it goes on at, and its records (4 words each:
; current task, SS, SP, DS)
_sw_back        dd 0
_sw_count       dw 0
_sw_rec         times 8*4 dw 0

segment _TEXT

; ---- st_run(void): frames for the stack trace ----
global _st_run
_st_run:
        inc bp                          ; a far frame, odd BP
        push bp
        mov bp, sp
        push ds
        call far st_far_even
st_ret_run:
        pop ds
        pop bp
        dec bp
        retf

st_far_even:                            ; a far frame, even BP
        push bp
        mov bp, sp
        call st_near                    ; a near call
st_ret_even:
        pop bp
        retf

st_near:
        push bp
        mov bp, sp
        call far st_inner
st_ret_near:
        pop bp
        ret

st_inner:
        inc bp
        push bp
        mov bp, sp
        push ds
.here:  push bp
        push word .here
        push cs
        push ss
        call far _st_walk
        add sp, 8
        pop ds
        pop bp
        dec bp
        retf

; the return addresses st_run's frames leave, for the probe to compare:
; void st_addrs(WORD far *out): out[0] the far return into st_run, out[1]
; the near return into st_far_even, out[2] the far return into st_near,
; out[3] the inner .here
global _st_addrs
_st_addrs:
        push bp
        mov bp, sp
        les bx, [bp+6]
        mov word [es:bx], st_ret_run
        mov word [es:bx+2], st_ret_even
        mov word [es:bx+4], st_ret_near
        mov word [es:bx+6], st_inner.here
        pop bp
        retf

; ---- the interrupt callback ----
; Entered from TOOLHELP with the frame above SP; keeps every register.
global _int_cb
_int_cb:
        push bp
        mov bp, sp                      ; [bp+2] the frame
        push ds
        push es
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        mov dx, ds
        mov bx, ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_int_count wrt DGROUP]
        cmp ax, 16
        jae .full
        inc word [_int_count wrt DGROUP]
        shl ax, 5
        mov si, ax
        add si, _int_rec wrt DGROUP
        mov [si], bx                    ; AX at entry
        mov [si+30], dx                 ; DS at entry
        mov cx, 11
        xor di, di
        mov bx, si
.copy:  mov ax, [bp+di+2]
        mov [bx+di+2], ax
        add di, 2
        loop .copy
        mov [si+24], ss
        lea ax, [bp+2]
        mov [si+26], ax
        mov ax, [_int_task_fn+2 wrt DGROUP]
        or ax, ax
        jz .full
        push si
        call far [_int_task_fn wrt DGROUP]
        pop si
        mov [si+28], ax
.full:
        mov ax, [_int_mode wrt DGROUP]
        cmp ax, 1
        je .restart
        cmp ax, 2
        je .term
        cmp ax, 3
        je .termbox
        ; chain: back into TOOLHELP with every register as it came
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pop es
        pop ds
        pop bp
        retf
.restart:
        mov ax, [_int_skip wrt DGROUP]
        add [bp+2+10], ax               ; the faulting IP
        and byte [bp+2+15], 0FEh        ; TF off (single step)
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pop es
        pop ds
        mov ax, [bp+2+4]                ; the faulting task's AX
        pop bp
        add sp, 10                      ; TOOLHELP's IP CS, AX, number, handle
        iret
.term:
        push word 0
        push word 1                     ; NO_UAE_BOX
        call far [_int_term_fn wrt DGROUP]
        jmp .full                       ; (not reached)
.termbox:
        push word 0
        push word 0                     ; UAE_BOX
        call far [_int_term_fn wrt DGROUP]
        jmp .full

; ---- a ToolHelpHook (KERNEL.341) procedure that records what KERNEL
; reports (AX BX CX ES, 4 words each, up to 64) and returns 0 ----
global _raw_hook, _raw_count, _raw_rec
segment _DATA
_raw_count      dw 0
_raw_rec        times 64*4 dw 0
segment _TEXT
_raw_hook:
        push ds
        push si
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov si, [_raw_count wrt DGROUP]
        cmp si, 64
        jae .full
        inc word [_raw_count wrt DGROUP]
        shl si, 3
        mov [_raw_rec+si wrt DGROUP], ax
        mov [_raw_rec+si+2 wrt DGROUP], bx
        mov [_raw_rec+si+4 wrt DGROUP], cx
        mov [_raw_rec+si+6 wrt DGROUP], es
.full:  xor ax, ax
        pop si
        pop ds
        retf

; ---- TaskSwitch / TaskSetCSIP target ----
; Runs as whichever task was switched to, on its stack, where the
; scheduler would have returned: record, then go on at sw_back.
global _sw_routine
_sw_routine:
        sub sp, 4                       ; room for the far address
        push bp
        mov bp, sp
        push ds
        push ax
        push bx
        push cx
        push dx
        push es
        mov bx, ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_sw_back wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_sw_back+2 wrt DGROUP]
        mov [bp+4], ax
        push si
        mov ax, [_sw_count wrt DGROUP]
        cmp ax, 8
        jae .done
        inc word [_sw_count wrt DGROUP]
        shl ax, 3
        mov si, ax
        add si, _sw_rec wrt DGROUP
        mov [si+2], ss
        lea ax, [bp+6]
        mov [si+4], ax
        mov [si+6], bx
        mov ax, [_int_task_fn+2 wrt DGROUP]
        or ax, ax
        jz .done
        push si
        call far [_int_task_fn wrt DGROUP]
        pop si
        mov [si], ax
.done:
        pop si
        pop es
        pop dx
        pop cx
        pop bx
        pop ax
        pop ds
        pop bp
        retf

; a code area THP writes with MemoryWrite (its code is read-only through CS)
global _code_area
_code_area:     times 16 db 0

; ---- faults (in a segment of their own: the same offsets in THP and THC) ----
segment FAULT_TEXT public align=16 class=CODE use16
; void do_fault(WORD kind): AX = 1234h at the fault; each faulting
; instruction's length is in fault_len(kind)
global _do_fault, _fault_len
_do_fault:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov bx, [bp+6]
        mov ax, 1234h
        cmp bl, 'd'
        je .div
        cmp bl, 'g'
        je .gpw
        cmp bl, 'n'
        je .nul
        cmp bl, 'u'
        je .ud
        cmp bl, '3'
        je .i3
        cmp bl, 's'
        je .stk
        cmp bl, 'i'
        je .int1
        jmp .out
.div:   xor cx, cx
        xor dx, dx
.fdiv:  div cx
.fdive:
        jmp .out
.gpw:   push cs
        pop es
.fgpw:  mov word [es:0], ax
.fgpwe:
        jmp .out
.nul:   xor dx, dx
        mov es, dx
.fnul:  mov dx, [es:0]
.fnule:
        jmp .out
.ud:
.fud:   db 0Fh, 0FFh                    ; an invalid opcode
.fude:
        jmp .out
.i3:
.fi3:   int3                            ; a trap: IP is after it
.fi3e:
        jmp .out
.stk:   push bp
        mov bp, 0FFF0h                  ; past SS's limit
.fstk:  mov dx, [bp]
.fstke:
        pop bp
        jmp .out
.int1:  pushf
        pop dx
        or dh, 1                        ; TF
        push dx
        popf
        nop                             ; the trap comes after this
        pushf
        pop dx
        and dh, 0FEh
        push dx
        popf
.out:   pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

_fault_len:
        push bp
        mov bp, sp
        mov bx, [bp+6]
        xor ax, ax
        cmp bl, 'd'
        jne .1
        mov ax, _do_fault.fdive - _do_fault.fdiv
.1:     cmp bl, 'g'
        jne .2
        mov ax, _do_fault.fgpwe - _do_fault.fgpw
.2:     cmp bl, 'n'
        jne .3
        mov ax, _do_fault.fnule - _do_fault.fnul
.3:     cmp bl, 'u'
        jne .4
        mov ax, _do_fault.fude - _do_fault.fud
.4:     cmp bl, 's'
        jne .5
        mov ax, _do_fault.fstke - _do_fault.fstk
.5:     pop bp
        retf

; WORD fault_ip(WORD kind): the offset of the faulting instruction
global _fault_ip
_fault_ip:
        push bp
        mov bp, sp
        mov bx, [bp+6]
        xor ax, ax
        cmp bl, 'd'
        jne .1
        mov ax, _do_fault.fdiv
.1:     cmp bl, 'g'
        jne .2
        mov ax, _do_fault.fgpw
.2:     cmp bl, 'n'
        jne .3
        mov ax, _do_fault.fnul
.3:     cmp bl, 'u'
        jne .4
        mov ax, _do_fault.fud
.4:     cmp bl, '3'
        jne .5
        mov ax, _do_fault.fi3
.5:     cmp bl, 's'
        jne .6
        mov ax, _do_fault.fstk
.6:     pop bp
        retf
