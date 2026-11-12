; khook.asm - register-level glue for kernel/toolhelp.c:
;   _ptrace_call      call a notification hook (ToolHelpHook/RegisterPtrace)
;                     with the registers it takes
;   task_start_hook   called by task_tramp (kentry.asm) on a new task's
;                     first switch: the task-start notification
;   no-op exports that keep every register

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

extern _th_task_start           ; void __cdecl __far th_task_start(u16 ip, u16 cs)

segment BOOT_TEXT public align=16 class=CODE use16

; u16 __cdecl ptrace_call(u32 proc, u16 ax, u16 bx, u16 cx, u16 dx,
;                         u16 si, u16 di, u16 es)
; DS stays KERNEL's (observed on 3.1: every notification came with DS =
; KERNEL's data segment). Returns AX.
global _ptrace_call
_ptrace_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        push word [bp + 8]
        push word [bp + 6]
        mov ax, [bp + 10]
        mov bx, [bp + 12]
        mov cx, [bp + 14]
        mov dx, [bp + 16]
        mov si, [bp + 18]
        mov di, [bp + 20]
        mov es, [bp + 22]
        push bp
        mov bp, sp
        call far [bp + 2]
        pop bp
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; Far-called by task_tramp with the new task's start-up registers and its
; entry point next on the stack. Keeps every register.
global task_start_hook
task_start_hook:
        pusha
        push ds
        push es
        mov bp, sp
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 26]     ; the entry point's CS
        push word [bp + 24]     ; and IP
        call far _th_task_start
        add sp, 4
        pop es
        pop ds
        popa
        retf

; ---------------------------------------------------------------------------
; Exports that do nothing on Windows 3.1 and keep every register, the
; carry flag included (observed, tests/apps/tk50.c: AX and DX come back
; as they went in). Schulman: EnableDOS, DisableDOS and EnableKernel do
; nothing in 3.1; A20Proc takes one word, ReplaceInst three.
global VALIDATECODESEGMENTS, VALIDATEFREESPACES, ENABLEDOS, DISABLEDOS
global ENABLEKERNEL, DISABLEKERNEL, A20PROC, REPLACEINST, K329
global _DEBUGOUTPUT, FLUSHCACHEDFILEHANDLE
VALIDATECODESEGMENTS:           ; KERNEL.100 (the debugging KERNEL's)
VALIDATEFREESPACES:             ; KERNEL.200 (the same)
ENABLEDOS:                      ; KERNEL.41
DISABLEDOS:                     ; KERNEL.42
ENABLEKERNEL:                   ; KERNEL.124
; DisableKernel (KERNEL.125): Schulman: in 3.1 it restores the interrupt
; vectors KERNEL took, at the end of Windows. UNVERIFIED (not called by
; the probe, it would end DOS access); w16 does that in ExitKernel.
DISABLEKERNEL:
; _DebugOutput (KERNEL.328): the debugging KERNEL's formatted output;
; observed to leave its arguments to the caller (C convention).
_DEBUGOUTPUT:
        retf
A20PROC:                        ; KERNEL.165
        retf 2

; KbdRst (KERNEL.123): Schulman: KEYBOARD.DRV's INT 9 handler calls it;
; in 3.x it does nothing. DoSignal (KERNEL.139): Schulman: KEYBOARD's
; Ctrl-Break, for the focus task's signal procedure (w16: nothing yet).
; Observed (tests/apps/tk50.c, KRNL286): both keep every register, AX
; and DX included. (They were C functions returning with AX = KERNEL's
; data selector, inside the keyboard interrupt.)
global KBDRST, DOSIGNAL
KBDRST:
DOSIGNAL:
        retf

; NetBIOSCall (KERNEL.103): INT 5Ch with the caller's ES:BX (an NCB).
; Observed (tests/apps/tk50.c, KRNL286 in DOSBox, which has no NetBIOS):
; every register kept, the NCB untouched. w16 does no more. UNVERIFIED:
; with a NetBIOS present (DPMI hosts translate INT 5Ch NCBs; Schulman).
global NETBIOSCALL
NETBIOSCALL:
        retf

; EMSCopy (KERNEL.160): Schulman: a holdover from 2.x EMS support that
; does nothing in 3.x, 14 bytes of arguments. UNVERIFIED: with seven
; zero words it ended the probe on 3.1 (tests/apps/tk50.c).
global EMSCOPY
EMSCOPY:
        retf 14

; CVWBreak (KERNEL.205): the keyboard interrupt handler jumps to it on
; Ctrl+Alt+SysReq once EnableKBSysReq asks for it (Schulman: "void FAR
; _interrupt"); it returns as an interrupt procedure. Called as a far
; procedure on 3.1 it ended the probe (tests/apps/tk50.c). UNVERIFIED
; beyond that: w16 has no debugger to break into, so it only returns.
global CVWBREAK
CVWBREAK:
        iret

; LogError (KERNEL.324), LogParamError (325): kmisc.c, with AX and DX
; kept as on 3.1 (observed, tests/apps/tk50.c)
extern LOGERROR_, LOGPARAMERROR_
global LOGERROR, LOGPARAMERROR
LOGERROR:
        push ax
        push dx
        push bp
        mov bp, sp
        push word [bp + 14]             ; err
        push word [bp + 12]             ; info
        push word [bp + 10]
        call far LOGERROR_
        pop bp
        pop dx
        pop ax
        retf 6
LOGPARAMERROR:
        push ax
        push dx
        push bp
        mov bp, sp
        push word [bp + 18]             ; err
        push word [bp + 16]             ; proc
        push word [bp + 14]
        push word [bp + 12]             ; param
        push word [bp + 10]
        call far LOGPARAMERROR_
        pop bp
        pop dx
        pop ax
        retf 10
; K329 (KERNEL.329), Schulman's DebugFillBuffer(lpBuf, n): the retail
; KERNEL leaves the buffer as it is (observed).
K329:
REPLACEINST:                    ; KERNEL.201
        retf 6
; FlushCachedFileHandle (KERNEL.319): Schulman: closes KERNEL's cached
; DOS handle of the module's file. w16 keeps no such cache. Observed: AX
; and DX unchanged, the carry clear.
FLUSHCACHEDFILEHANDLE:
        clc
        retf 2

; ---------------------------------------------------------------------------
; CallProcInstance (KERNEL.53): Schulman: ES:BX points to an hInstance
; followed by a far procedure (in Windows 3.0, a window's); the stack
; holds a window procedure's arguments. Observed on 3.1 (TK50): the
; procedure is entered with AX = the hInstance (as from a
; MakeProcInstance thunk), BX and ES as given, the arguments left for it
; to pop; its DX:AX comes back.
global CALLPROCINSTANCE
CALLPROCINSTANCE:
        push word [es:bx + 4]           ; the procedure's selector
        push word [es:bx + 2]           ; and offset
        mov ax, [es:bx]                 ; the hInstance
        retf                            ; to the procedure

; InitLib (KERNEL.116): Schulman: LocalInit for the caller's DS and ES =
; KERNEL's data segment. Observed (TK50, with DS a zeroed block): AX = 0,
; the block untouched, ES = KERNEL's data, DS kept. UNVERIFIED: a DS whose
; instance data asks for a heap (w16 makes none).
global INITLIB
INITLIB:
        mov ax, DGROUP
        mov es, ax
        xor ax, ax
        retf

; SwitchStackTo (KERNEL.108, SDK): SwitchStackTo(wSeg, wSP, wTop) moves
; the caller onto a stack in wSeg. Observed on 3.1 (TK50): wSeg's words
; 02h/04h get the caller's SS:SP (its arguments popped) less 2, where the
; caller's BP goes (SwitchStackBack returns with SP 2 above it), 0Ah wTop,
; 0Ch and 0Eh wSP; the caller's frame below BP (BP - SP bytes) is copied
; to just below wSP; it returns with SS = wSeg, BP = wSP, SP = wSP less
; the frame, BX = wSP, CX = 0, DS kept.
segment _DATA
sw_ds   dw 0
sw_ip   dw 0
sw_cs   dw 0
sw_top  dw 0
sw_sp   dw 0
sw_seg  dw 0
sw_si   dw 0
sw_di   dw 0
sw_es   dw 0
sw_new  dw 0
segment BOOT_TEXT
global SWITCHSTACKTO, SWITCHSTACKBACK
SWITCHSTACKTO:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        pop word [sw_ds wrt DGROUP]
        pop word [sw_ip wrt DGROUP]
        pop word [sw_cs wrt DGROUP]
        pop word [sw_top wrt DGROUP]
        pop word [sw_sp wrt DGROUP]
        pop word [sw_seg wrt DGROUP]
        mov [sw_si wrt DGROUP], si
        mov [sw_di wrt DGROUP], di
        mov [sw_es wrt DGROUP], es
        push bp                         ; the caller's BP, for SwitchStackBack
        mov es, [sw_seg wrt DGROUP]
        mov [es:2], sp
        mov [es:4], ss
        mov ax, [sw_top wrt DGROUP]
        mov [es:0Ah], ax
        mov ax, [sw_sp wrt DGROUP]
        mov [es:0Ch], ax
        mov [es:0Eh], ax
        mov si, sp
        add si, 2                       ; the caller's SP
        mov cx, bp
        sub cx, si                      ; the frame below BP
        jae .frame
        xor cx, cx
.frame: mov di, ax
        sub di, cx
        mov [sw_new wrt DGROUP], di
        push ss
        pop ds
        cld
        rep movsb
        mov ax, DGROUP
        mov ds, ax
        mov bx, [sw_sp wrt DGROUP]
        mov bp, bx
        cli
        mov ss, [sw_seg wrt DGROUP]
        mov sp, [sw_new wrt DGROUP]
        sti
        push word [sw_cs wrt DGROUP]
        push word [sw_ip wrt DGROUP]
        mov si, [sw_si wrt DGROUP]
        mov di, [sw_di wrt DGROUP]
        mov es, [sw_es wrt DGROUP]
        mov ds, [sw_ds wrt DGROUP]
        retf

; SwitchStackBack (KERNEL.109, SDK): back to the stack SwitchStackTo
; left. Observed (TK50): AX and DX kept, BX = the old SS, CX = the
; stored SP (the stack segment's words 04h and 02h), SS back, SP = that +
; 2: the BP saved there is popped. UNVERIFIED: whether 3.1 copies any of
; the frame back (w16 does not).
SWITCHSTACKBACK:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        pop word [sw_ds wrt DGROUP]
        pop word [sw_ip wrt DGROUP]
        pop word [sw_cs wrt DGROUP]
        mov bx, [ss:4]
        mov cx, [ss:2]
        cli
        mov ss, bx
        mov sp, cx
        sti
        pop bp
        push word [sw_cs wrt DGROUP]
        push word [sw_ip wrt DGROUP]
        mov ds, [sw_ds wrt DGROUP]
        retf

; DefineHandleTable (KERNEL.94, SDK): a private handle table in the
; caller's DGROUP, for real mode. Observed (TK50, wOffset 0): AX = the
; caller's DS (nonzero: success), DX = 0. Nothing to do in protected mode.
global DEFINEHANDLETABLE
DEFINEHANDLETABLE:
        mov ax, ds
        xor dx, dx
        retf 2

; IsROMFile (KERNEL.326): Schulman: FALSE outside ROM Windows, one word
; argument. UNVERIFIED: both calls TK50 tried ended the probe on 3.1.
global ISROMFILE
ISROMFILE:
        xor ax, ax
        retf 2

; LocalNotify (KERNEL.14): local.c's LNotify with the caller's DS
extern LNOTIFY
global LOCALNOTIFY
LOCALNOTIFY:
        push bp
        mov bp, sp
        push ds
        push ds
        push word [bp + 8]
        push word [bp + 6]
        call far LNOTIFY
        pop ds
        pop bp
        retf 4

; DebugDefineSegment (KERNEL.314): Schulman: tells a system debugger
; (INT 41h) about a segment; six words. UNVERIFIED: the call TK50 tried
; ended the probe on 3.1; w16 has no system debugger to tell.
global DEBUGDEFINESEGMENT
DEBUGDEFINESEGMENT:
        retf 12

; ---------------------------------------------------------------------------
; Exports in C that keep more registers than a KAPI function does, because
; Windows 3.1 keeps them (tests/apps/tk50.c, "kept" in tk50ref.h): the
; wrapper keeps BX, CX, ES and the flags, and DX unless the result is a
; DWORD. KEEPREGS exported-name, c-function, argument bytes, keep-DX
%macro KEEPREGS 4
extern %2
global %1
%1:
        push bp
        mov bp, sp
        pushf
        push bx
        push cx
        push dx
        push es
%assign i %3 / 2
%rep %3 / 2
        push word [bp + 4 + 2 * i]
%assign i i - 1
%endrep
        call far %2
        pop es
%if %4
        pop dx
%else
        add sp, 2
%endif
        pop cx
        pop bx
        popf
        pop bp
        retf %3
%endmacro

KEEPREGS SETPRIORITY,      KSETPRIORITY,      4, 1  ; KERNEL.32
KEEPREGS DIAGQUERY,        KDIAGQUERY,        0, 1  ; KERNEL.339
KEEPREGS SWAPRECORDING,    KSWAPRECORDING,    2, 1  ; KERNEL.204
KEEPREGS OPENPATHNAME,     KOPENPATHNAME,     6, 1  ; KERNEL.75
KEEPREGS ISSHAREDSELECTOR, KISSHAREDSELECTOR, 2, 1  ; KERNEL.345
KEEPREGS GETCODEHANDLE,    KGETCODEHANDLE,    4, 0  ; KERNEL.93
