; vmtaskhk.asm - VM Task Manager (VMTASK.EXE; ARCHITECTURE.md 15.12): the
; parts that run while other code is in the middle of something, kept in a
; fixed code segment (VMTHOOK_TEXT, FIXED in vmtask.lnk):
;   - NotifyProc, TOOLHELP's notification callback (NotifyRegister with
;     NF_TASKSWITCH): at NFY_TASKIN it notes the task switched to and the
;     time, at NFY_TASKOUT it adds the time since to that task's count;
;   - the protected-mode INT 2Fh handler: KERNEL's idle call (AX=1689h,
;     made in protected mode once each pass of its idle loop; ARCHITECTURE
;     12.15) counts the time between two such calls with no task switch
;     between as idle, when VMTASK.386 is not there to count it;
;   - vmt_api: a call to VMTASK.386's API entry with registers.
; "The time" is VMTASK.386's function 2 when the device is there (the
; System VM's run time less its idle time: a task's count leaves out other
; VMs' time slices and idling), else TOOLHELP's TimerCount less the idle
; time the INT 2Fh handler counted.
; DGROUP's selector and the previous INT 2Fh handler are kept in this code
; segment, written through a data alias (AllocCStoDSAlias), as the
; handlers start with nothing but CS.
; Black box (ARCHITECTURE.md 3): the SDK's TOOLHELP, DPMI 0.9 (0204h,
; 0205h), the DDK's INT 2Fh 1689h.

        bits 16
        cpu 286

segment _DATA  public align=2 class=DATA use16
segment CONST  public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS   public align=2 class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

NSLOTS  equ 64                          ; tasks counted (vmtask.h VMT_NSLOTS)

extern TIMERCOUNT                       ; TOOLHELP.80
extern GETCURRENTTASK                   ; KERNEL.36
extern ALLOCCSTODSALIAS                 ; KERNEL.170
extern FREESELECTOR                     ; KERNEL.176

segment _DATA
global _vmt_entry, _vmt_slots, _vmt_cur, _vmt_tin, _vmt_hidle, _vmt_hcalls
_vmt_entry      dd 0                    ; VMTASK.386's API entry (0: none)
_vmt_cur        dw 0                    ; the task switched to last (0: none)
_vmt_tin        dd 0                    ; the time it was switched to
_vmt_hidle      dd 0                    ; idle ms the INT 2Fh handler counted
_vmt_hcalls     dd 0                    ; 1689h calls it saw
hlast           dd 0
hrun            dw 0
ti1             dd 12, 0, 0             ; TIMERINFO (dwSize 12) for the callback
ti2             dd 12, 0, 0             ; and for the handler
; {task, count (dword)} per slot; a task's count is the milliseconds it
; ran; the last slot takes what does not fit
_vmt_slots      times NSLOTS * 6 db 0

segment VMTHOOK_TEXT public align=16 class=CODE use16
dsel    dw 0
old2f   dd 0

; DX:AX = the time (see the header); DS = DGROUP; BX, CX changed
now:    mov ax, [_vmt_entry wrt DGROUP]
        or ax, [_vmt_entry + 2 wrt DGROUP]
        jz .tc
        push si
        push di
        push bp
        push es
        mov ax, 2
        call far [_vmt_entry wrt DGROUP]
        pop es
        pop bp
        pop di
        pop si
        ret
.tc:    push ds
        push word ti1 wrt DGROUP
        call far TIMERCOUNT
        mov ax, [ti1 + 4 wrt DGROUP]
        mov dx, [ti1 + 6 wrt DGROUP]
        sub ax, [_vmt_hidle wrt DGROUP]
        sbb dx, [_vmt_hidle + 2 wrt DGROUP]
        ret

; BX = the slot of task AX (a free one if it has none; the last if full)
slot:   push cx
        push si
        mov bx, _vmt_slots wrt DGROUP
        mov cx, NSLOTS - 1
        xor si, si
.l:     cmp [bx], ax
        je .ok
        test si, si
        jnz .n
        cmp word [bx], 0
        jne .n
        mov si, bx
.n:     add bx, 6
        loop .l
        test si, si                     ; none: a free one, else the last
        jz .ok
        mov bx, si
        mov [bx], ax
.ok:    pop si
        pop cx
        ret

; BOOL FAR PASCAL NotifyProc(WORD wID, DWORD dwData)
global NOTIFYPROC
NOTIFYPROC:
        push bp
        mov bp, sp
        push ds
        push es
        push bx
        push cx
        push dx
        push si
        push di
        mov ds, [cs:dsel]
        mov ax, [bp + 10]               ; wID
        cmp ax, 8                       ; NFY_TASKIN
        je .in
        cmp ax, 9                       ; NFY_TASKOUT
        jne .x
        mov word [hrun wrt DGROUP], 0
        call now
        push dx
        push ax
        call far GETCURRENTTASK
        pop cx                          ; the time: SI:CX
        pop si
        cmp ax, [_vmt_cur wrt DGROUP]
        jne .x0
        test ax, ax
        jz .x0
        call slot
        sub cx, [_vmt_tin wrt DGROUP]
        sbb si, [_vmt_tin + 2 wrt DGROUP]
        js .x0                          ; (never backwards)
        add [bx + 2], cx
        adc [bx + 4], si
.x0:    mov word [_vmt_cur wrt DGROUP], 0
        jmp .x
.in:    mov word [hrun wrt DGROUP], 0
        call now
        mov [_vmt_tin wrt DGROUP], ax
        mov [_vmt_tin + 2 wrt DGROUP], dx
        call far GETCURRENTTASK
        mov [_vmt_cur wrt DGROUP], ax
.x:     pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop es
        pop ds
        xor ax, ax                      ; FALSE: the next callback too
        pop bp
        retf 6

; the protected-mode INT 2Fh handler
h2f:    cmp ax, 1689h
        jne .go
        push ds
        push es
        pusha
        mov ds, [cs:dsel]
        add word [_vmt_hcalls wrt DGROUP], 1
        adc word [_vmt_hcalls + 2 wrt DGROUP], 0
        push ds
        push word ti2 wrt DGROUP
        call far TIMERCOUNT
        mov ax, [ti2 + 4 wrt DGROUP]
        mov dx, [ti2 + 6 wrt DGROUP]
        mov cx, ax
        mov bx, dx
        sub cx, [hlast wrt DGROUP]
        sbb bx, [hlast + 2 wrt DGROUP]
        mov [hlast wrt DGROUP], ax
        mov [hlast + 2 wrt DGROUP], dx
        cmp word [hrun wrt DGROUP], 0
        mov word [hrun wrt DGROUP], 1
        je .z
        test bx, bx                     ; a gap of at most 2 ms: still idle
        jnz .z
        cmp cx, 2
        ja .z
        add [_vmt_hidle wrt DGROUP], cx
        adc word [_vmt_hidle + 2 wrt DGROUP], 0
.z:     popa
        pop es
        pop ds
.go:    jmp far [cs:old2f]

; void __cdecl __far vmt_init(void): DGROUP's selector and the INT 2Fh
; vector into this segment
global _vmt_init
_vmt_init:
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
        mov ax, 0204h
        mov bl, 2Fh
        int 31h
        mov [es:old2f], dx
        mov [es:old2f + 2], cx
        call far FREESELECTOR           ; (the alias, pushed above)
        pop es
        pop di
        pop si
        pop bp
        retf

; void __cdecl __far vmt_hook(int on): the INT 2Fh handler in or out
global _vmt_hook
_vmt_hook:
        push bp
        mov bp, sp
        mov ax, 0205h
        mov bl, 2Fh
        cmp word [bp + 6], 0
        je .off
        mov cx, cs
        mov dx, h2f
        int 31h
        pop bp
        retf
.off:   mov cx, [cs:old2f + 2]
        mov dx, [cs:old2f]
        int 31h
        pop bp
        retf

; DWORD __cdecl __far vmt_find(WORD id): INT 2Fh AX=1684h, BX=id: the
; device's protected-mode API entry (0: none)
global _vmt_find
_vmt_find:
        push bp
        mov bp, sp
        push si
        push di
        push es
        xor di, di
        mov es, di
        mov ax, 1684h
        mov bx, [bp + 6]
        int 2Fh
        mov ax, di
        mov dx, es
        pop es
        pop di
        pop si
        pop bp
        retf

; int __cdecl __far vmt_api(WORD __far *regs): VMTASK.386's API with
; regs = {ax, bx, cx, dx, si, di, es}, the results back there; returns 1
; for carry clear, 0 for carry set (or no entry)
global _vmt_api
_vmt_api:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, [_vmt_entry wrt DGROUP]
        or ax, [_vmt_entry + 2 wrt DGROUP]
        jz .none
        push word [_vmt_entry + 2 wrt DGROUP]
        push word [_vmt_entry wrt DGROUP]   ; [bp-10] the entry
        les bx, [bp + 6]
        mov ax, [es:bx]
        mov cx, [es:bx + 4]
        mov dx, [es:bx + 6]
        mov si, [es:bx + 8]
        mov di, [es:bx + 10]
        push word [es:bx + 12]
        mov bx, [es:bx + 2]
        pop es
        push bp
        call far [bp - 10]
        pop bp
        pushf
        push es
        push bx
        les bx, [bp + 6]
        mov [es:bx], ax
        pop word [es:bx + 2]
        mov [es:bx + 4], cx
        mov [es:bx + 6], dx
        mov [es:bx + 8], si
        mov [es:bx + 10], di
        pop word [es:bx + 12]
        popf
        mov ax, 0
        jc .r
        inc ax
.r:     add sp, 4
        pop di
        pop si
        pop ds
        pop bp
        retf
.none:  xor ax, ax
        pop di
        pop si
        pop ds
        pop bp
        retf
