; khook.asm - register-level glue for kernel/toolhelp.c:
;   _ptrace_call      call a notification hook (ToolHelpHook/RegisterPtrace)
;                     with the registers it takes
;   task_start_hook   called by task_tramp (kentry.asm) on a new task's
;                     first switch: the task-start notification
;   DOS3CALL          KERNEL.102 and NOHOOKDOSCALL, KERNEL.101: INT 21h
;                     through KERNEL's DOS procedure (GetSetKernelDOSProc)
;   _kdos_default     that procedure as KERNEL starts: INT 21h

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

extern _th_task_start           ; void __cdecl __far th_task_start(u16 ip, u16 cs)

segment _DATA
global _dos_proc
_dos_proc       dd 0            ; 0: _kdos_default

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

; The DOS procedure KERNEL starts with: INT 21h, called as an interrupt
; handler (PUSHF / CALL FAR); returns the flags DOS gave.
global _kdos_default
_kdos_default:
        int 21h
        retf 2

; DOS3Call (KERNEL.102): INT 21h with the caller's registers, through
; KERNEL's DOS procedure (Schulman, GetSetKernelDOSProc; observed on 3.1,
; tests/apps/tk50.c).
global DOS3CALL
DOS3CALL:
        pushf
        push cs
        push word .back
        sub sp, 4                       ; the far target
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_dos_proc + 2 wrt DGROUP]
        or ax, ax
        jnz .set
        mov word [bp + 2], _kdos_default
        mov word [bp + 4], seg _kdos_default
        jmp .go
.set:   mov [bp + 4], ax
        mov ax, [_dos_proc wrt DGROUP]
        mov [bp + 2], ax
.go:    pop ax
        pop ds
        pop bp
        retf
.back:  retf

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
