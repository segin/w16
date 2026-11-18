; mmfix.asm - w16's MMSYSTEM.DLL, its fixed code (ARCHITECTURE.md 23):
; what runs at interrupt time or must not move, and what reaches into
; another module by a jump.
;   mm_libentry      the library's entry point
;   STACKENTER/LEAVE the interrupt stack drivers' callbacks run on
;   DRIVERCALLBACK   a driver's notification to a window, task or function
;   time*            the timer services, sent to the timer driver
;   MMTASKSIGNAL     a task's wake-up message
;   Drv*, mmGetCurrentTask: USER's and KERNEL's functions under MMSYSTEM's
;                    names (far jumps)
;   OUTPUTDEBUGSTR   nothing (3.1's retail one does nothing observable)
;
; Sources: Windows 3.1 SDK, Multimedia Programmer's Guide ch. 6 (timer
; services), Programmer's Reference Vol. 1 ch. 25 and Vol. 2 (installable
; drivers, PostMessage, PostAppMessage); DriverCallback's arguments and
; flags from Wine's MMSYSTEM of before 2002 (X11 licence); everything else
; observed on Windows 3.1 with tests/mmsystem/mmp.c and tests/timer/tpr.c
; (docs/notes/mmsystem-handover.md has the observations): the timer
; driver's messages (ARCHITECTURE.md 21.2), which time* functions jump
; into the driver and which call it, the stack frames whose addresses the
; driver sees, the interrupt stack's size and nesting, the registers a
; callback gets.
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16    ; fixed: segment 1
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

extern LOCALINIT                        ; KERNEL.4
extern GETCURRENTTASK                   ; KERNEL.36
extern POSTMESSAGE                      ; USER.110
extern POSTAPPMESSAGE                   ; USER.116
extern OPENDRIVER                       ; USER.252
extern CLOSEDRIVER                      ; USER.253
extern SENDDRIVERMESSAGE                ; USER.251
extern GETDRIVERMODULEHANDLE            ; USER.254
extern DEFDRIVERPROC                    ; USER.255
extern _mm_init                         ; int __cdecl __far mm_init(void)
extern _mm_makestack                    ; void __cdecl __far mm_makestack(void)
extern __AHINCR                         ; KERNEL.114

TDRV_KILL       equ 0800h               ; the timer driver's messages
TDRV_SET        equ 0804h               ; (ARCHITECTURE.md 21.2)
TDRV_TIME       equ 0808h
TDRV_CAPS       equ 080Ch
TDRV_BEGIN      equ 0810h
TDRV_END        equ 0814h
TIMERR_NOCANDO  equ 97
TIMERR_STRUCT   equ 129
DCB_WINDOW      equ 1
DCB_TASK        equ 2
DCB_FUNCTION    equ 3
DCB_TYPEMASK    equ 7
DCB_NOSWITCH    equ 8
ISTACK_SIZE     equ 600h                ; 3.1's interrupt stack (observed)

segment _DATA
global _mm_tdrv, _mm_tproc, _mm_istack
_mm_tdrv        dw 0                    ; the timer driver's handle (USER's)
_mm_tproc       dd 0                    ; its DriverProc
_mm_istack      dw 0                    ; the interrupt stack (a selector)
ibusy           db 0                    ; a caller is on it
                db 0
; StackEnter's scratch (interrupts are off while it is used)
t_ax            dw 0
t_ds            dw 0
t_fl            dw 0
t_ip            dw 0
t_cs            dw 0
t_ss            dw 0
t_sp            dw 0

segment _TEXT

; KERNEL calls a library's entry point once, with DS = its data segment,
; DI = its instance handle, CX = the local heap size from the NE header
global mm_libentry
mm_libentry:
        push ds
        push si
        push di
        push es
        jcxz .noheap
        push ds
        push word 0
        push cx
        call far LOCALINIT
.noheap:
        call far _mm_init
        pop es
        pop di
        pop si
        pop ds
        retf

; WORD __cdecl __far mm_ahincr(void): KERNEL's selector increment
global _mm_ahincr
_mm_ahincr:
        mov ax, __AHINCR
        retf

global WEP
WEP:
        mov ax, 1
        retf 2

; ---- StackEnter / StackLeave ----
; StackEnter moves its caller onto MMSYSTEM's interrupt stack, with the
; caller's SS:SP pushed at its top (SP 05FCh after it, carry clear); when
; there is none yet (timeSetEvent makes it) or it is in use, the caller
; stays where it is, one word lower (a mark for StackLeave), carry set.
; Every register but SS:SP is kept; the arithmetic flags are left as 3.1's
; (observed: 3202h-like after a switch, ZF PF CF after a stay; StackLeave
; ZF PF after a stay, PF after a switch back).
global STACKENTER
STACKENTER:
        pushf
        cli
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [t_ax]
        pop word [t_ds]
        pop word [t_fl]
        pop word [t_ip]
        pop word [t_cs]
        cmp word [_mm_istack], 0
        je .stay
        cmp byte [ibusy], 0
        jne .stay
        mov byte [ibusy], 1
        mov [t_ss], ss
        mov [t_sp], sp
        mov ax, [_mm_istack]
        mov ss, ax
        mov sp, ISTACK_SIZE
        push word [t_ss]
        push word [t_sp]
        push word [t_cs]
        push word [t_ip]
        push word [t_ds]
        test byte [t_fl + 1], 2         ; interrupts were enabled?
        mov ax, [t_ax]
        pop ds
        jz .s1
        sti
.s1:    push ax                         ; the flags 3.1's leaves: a
        mov ax, 2                       ; result of 1 (ZF, PF, CF, SF
        cmp ax, 1                       ; clear)
        pop ax
        retf
.stay:  push word 0                     ; the mark
        push word [t_cs]
        push word [t_ip]
        push word [t_ds]
        test byte [t_fl + 1], 2
        mov ax, [t_ax]
        pop ds
        jz .s2
        sti
.s2:    cmp ax, ax                      ; ZF, PF
        stc                             ; and CF
        retf

global STACKLEAVE
STACKLEAVE:
        push bp
        mov bp, sp
        cmp word [bp + 6], 0            ; the mark: stayed
        jne .back
        push ax                         ; the return address up over the
        mov ax, [bp + 4]                ; mark
        mov [bp + 6], ax
        mov ax, [bp + 2]
        mov [bp + 4], ax
        pop ax
        pop bp
        add sp, 2
        cmp ax, ax                      ; ZF, PF
        retf
.back:  pop bp
        pushf
        cli
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [t_ax]
        pop word [t_ds]
        pop word [t_fl]
        pop word [t_ip]
        pop word [t_cs]
        pop word [t_sp]
        pop word [t_ss]
        mov byte [ibusy], 0
        mov ss, [t_ss]
        mov sp, [t_sp]
        push word [t_cs]
        push word [t_ip]
        push word [t_ds]
        test byte [t_fl + 1], 2
        mov ax, [t_ax]
        pop ds
        jz .b1
        sti
.b1:    push ax                         ; PF only: a result of 3
        mov ax, 4
        cmp ax, 1
        pop ax
        retf

; ---- DriverCallback ----
; BOOL FAR PASCAL DriverCallback(DWORD dwCallback, UINT uFlags, HANDLE
;     hDevice, UINT uMessage, DWORD dwUser, DWORD dw1, DWORD dw2)
; DCB_WINDOW: PostMessage(LOWORD(dwCallback), uMessage, hDevice, dw1), its
; result (0 for window 0); DCB_TASK: PostAppMessage likewise (0 for task
; 0); DCB_FUNCTION: dwCallback(hDevice, uMessage, dwUser, dw1, dw2) on the
; interrupt stack unless DCB_NOSWITCH, 1, or 0 when dwCallback is not
; code. The function gets AX = the access rights of its code segment (as
; LAR gives them), BX = hDevice, CX = ES = the caller's SS, and the
; caller's DX, SI, DI and DS (all as 3.1's).
DCB_DW2         equ 6
DCB_DW1         equ 10
DCB_USER        equ 14
DCB_MSG         equ 18
DCB_DEV         equ 20
DCB_FLAGS       equ 22
DCB_CB          equ 24
global DRIVERCALLBACK
DRIVERCALLBACK:
        push bp
        mov bp, sp
        mov ax, [bp + DCB_FLAGS]
        and ax, DCB_TYPEMASK
        cmp ax, DCB_FUNCTION
        je .func
        cmp ax, DCB_WINDOW
        je .post
        cmp ax, DCB_TASK
        je .post
.fail:  xor ax, ax
        jmp .out
.post:  mov bx, [bp + DCB_CB]           ; a window or task: not 0
        or bx, bx
        jz .fail
        push bx
        push word [bp + DCB_MSG]
        push word [bp + DCB_DEV]
        push word [bp + DCB_DW1 + 2]
        push word [bp + DCB_DW1]
        cmp ax, DCB_TASK
        je .task
        call far POSTMESSAGE
        jmp .out
.task:  call far POSTAPPMESSAGE
        jmp .out
.func:  lar ax, [bp + DCB_CB + 2]       ; code?
        jnz .fail
        test ax, 0800h
        jz .fail
        mov cx, ss
        mov es, cx
        test byte [bp + DCB_FLAGS], DCB_NOSWITCH
        jnz .f1
        call far STACKENTER
.f1:    push es
        mov bx, [es:bp + DCB_DEV]
        push bx
        push word [es:bp + DCB_MSG]
        push word [es:bp + DCB_USER + 2]
        push word [es:bp + DCB_USER]
        push word [es:bp + DCB_DW1 + 2]
        push word [es:bp + DCB_DW1]
        push word [es:bp + DCB_DW2 + 2]
        push word [es:bp + DCB_DW2]
        test ax, 0800h                  ; the flags 3.1's callbacks find
        call far [es:bp + DCB_CB]
        pop es
        test byte [es:bp + DCB_FLAGS], DCB_NOSWITCH
        jnz .f2
        call far STACKLEAVE
.f2:    mov ax, 1
.out:   pop bp
        retf 22

; ---- the timer services ----
; The driver's DriverProc(DWORD id, HDRVR h, UINT msg, LPARAM l1, LPARAM
; l2) is called directly, identifier 0. timeGetTime, timeBeginPeriod,
; timeEndPeriod and timeKillEvent jump to it, so that it returns straight
; to the application (as 3.1's: the driver sees the application's return
; address); the others call it.

; DWORD timeGetTime(void)
global TIMEGETTIME
TIMEGETTIME:
        pop dx                          ; the caller's return address
        pop cx
        xor bx, bx
        mov ax, TDRV_TIME
        jmp tjump

; UINT timeBeginPeriod(UINT), timeEndPeriod(UINT), timeKillEvent(UINT)
global TIMEBEGINPERIOD
TIMEBEGINPERIOD:
        mov ax, TDRV_BEGIN
        jmp targ
global TIMEENDPERIOD
TIMEENDPERIOD:
        mov ax, TDRV_END
        jmp targ
global TIMEKILLEVENT
TIMEKILLEVENT:
        mov ax, TDRV_KILL
targ:   pop dx                          ; the caller's return address
        pop cx
        pop bx                          ; the argument
; tjump: AX = the message, BX = lParam1 (its high word 0), CX:DX = the
; caller's return address, which DriverProc returns to; lParam2 0
tjump:  push ax
        mov ax, DGROUP
        mov es, ax
        pop ax
        cmp word [es:_mm_tdrv], 0
        je .none
        push word 0                     ; id
        push word 0
        push word [es:_mm_tdrv]         ; h
        push ax                         ; msg
        push word 0                     ; l1
        push bx
        push word 0                     ; l2
        push word 0
        push cx
        push dx
        jmp far [es:_mm_tproc]
.none:  push cx
        push dx
        mov ax, TIMERR_NOCANDO
        xor dx, dx
        retf

; UINT timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK f, DWORD user,
;     UINT flags): the arguments copied in 3.1's order on the stack (the
;     driver sees their address), the interrupt stack made the first time
global TIMESETEVENT
TIMESETEVENT:
        push bp
        mov bp, sp
        sub sp, 14
        ; [bp+6] flags [bp+8] user [bp+12] f [bp+16] res [bp+18] delay
        lar ax, [bp + 14]               ; a callback that is not code:
        jnz .bad                        ; MMSYSERR_INVALPARAM (3.1's
        test ax, 0800h                  ; answer to NULL, to a data
        jnz .ok                         ; pointer and to 0001:0000)
.bad:   mov ax, 11
        jmp .out
.ok:    push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        cmp word [_mm_istack], 0
        jne .have
        call far _mm_makestack
.have:  mov ax, [bp + 18]
        mov [bp - 14], ax               ; delay
        mov ax, [bp + 16]
        mov [bp - 12], ax               ; resolution
        mov ax, [bp + 12]
        mov [bp - 10], ax               ; callback
        mov ax, [bp + 14]
        mov [bp - 8], ax
        mov ax, [bp + 8]
        mov [bp - 6], ax                ; dwUser
        mov ax, [bp + 10]
        mov [bp - 4], ax
        mov ax, [bp + 6]
        mov [bp - 2], ax                ; flags
        xor ax, ax
        cmp [_mm_tdrv], ax
        je .done
        push ax
        push ax                         ; id 0
        push word [_mm_tdrv]
        push word TDRV_SET
        push ss
        lea ax, [bp - 14]
        push ax
        push word 0
        push word 0
        call far [_mm_tproc]
.done:  pop di
        pop si
        pop ds
.out:   mov sp, bp
        pop bp
        retf 14

; UINT timeGetSystemTime(LPMMTIME, UINT size): wType TIME_MS (1) and the
; milliseconds; TIMERR_STRUCT for a size below 8 (as 3.1's, without
; asking the driver)
global TIMEGETSYSTEMTIME
TIMEGETSYSTEMTIME:
        push bp
        mov bp, sp
        push ds
        mov ax, TIMERR_STRUCT
        cmp word [bp + 6], 8
        jb .out
        mov ax, DGROUP
        mov ds, ax
        mov ax, TIMERR_NOCANDO
        cmp word [_mm_tdrv], 0
        je .out
        xor ax, ax
        push ax
        push ax
        push word [_mm_tdrv]
        push word TDRV_TIME
        push ax
        push ax
        push ax
        push ax
        call far [_mm_tproc]
        push bx
        les bx, [bp + 8]
        mov word [es:bx], 1
        mov [es:bx + 2], ax
        mov [es:bx + 4], dx
        pop bx
        xor ax, ax
.out:   pop ds
        pop bp
        retf 6

; UINT timeGetDevCaps(LPTIMECAPS, UINT size): the driver answers
global TIMEGETDEVCAPS
TIMEGETDEVCAPS:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, TIMERR_NOCANDO
        cmp word [_mm_tdrv], 0
        je .out
        xor ax, ax
        push ax
        push ax
        push word [_mm_tdrv]
        push word TDRV_CAPS
        push word [bp + 10]
        push word [bp + 8]
        push ax
        push word [bp + 6]
        call far [_mm_tproc]
.out:   pop ds
        pop bp
        retf 6

; ---- tasks ----
; BOOL mmTaskSignal(HTASK): WM_USER posted to the task, PostAppMessage's
; answer (0 for task 0)
global MMTASKSIGNAL
MMTASKSIGNAL:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        or ax, ax
        jz .out
        push ax
        push word 0400h                 ; WM_USER
        push word 0
        push word 0
        push word 0
        call far POSTAPPMESSAGE
.out:   pop bp
        retf 2

global MMGETCURRENTTASK
MMGETCURRENTTASK:
        jmp far GETCURRENTTASK

; ---- the Drv* functions: USER's ----
global DRVOPEN, DRVCLOSE, DRVSENDMESSAGE, DRVGETMODULEHANDLE, DRVDEFDRIVERPROC
DRVOPEN:            jmp far OPENDRIVER
DRVCLOSE:           jmp far CLOSEDRIVER
DRVGETMODULEHANDLE: jmp far GETDRIVERMODULEHANDLE
DRVSENDMESSAGE:     jmp far SENDDRIVERMESSAGE
DRVDEFDRIVERPROC:   jmp far DEFDRIVERPROC

; void OutputDebugStr(LPCSTR): 3.1's retail MMSYSTEM does nothing a probe
; can see (no INT 21h, no INT 41h: tests/mmsystem/mmp.c section ods)
global OUTPUTDEBUGSTR
OUTPUTDEBUGSTR:
        retf 4
