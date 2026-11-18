; mmtask.asm - w16's MMTASK.TSK, the program MMSYSTEM's mmTaskCreate
; starts (shell/mmsystem/core.c; ARCHITECTURE.md 23). A Windows task with
; nothing of its own: it starts as programs do (InitTask, WaitEvent,
; InitApp, SetMessageQueue), yields back to mmTaskCreate, then calls the function mmTaskCreate put in
; its data with the instance value (DX:AX and on the stack), and ends
; when the function returns.
;
; Sources: Windows 3.1 SDK, Programmer's Reference Vol. 2 (InitTask's
; registers as Vol. 1's start-up description gives them, WaitEvent,
; InitApp, SetMessageQueue); the module's name, its imports and its
; NE header (4 KB stack, 1Ch bytes of data, flags 030Ah) from 3.1's
; MMTASK.TSK (tools/neinfo.py); the function's frame and registers as
; observed with tests/mmsystem/mmp.c (BP = SP after InitTask, SI = 0,
; DI = the instance, ES = 0).
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern INITTASK                         ; KERNEL.91
extern WAITEVENT                        ; KERNEL.30
extern INITAPP                          ; USER.5
extern SETMESSAGEQUEUE                  ; USER.266
extern OLDYIELD                         ; KERNEL.117

QSIZE   equ 64                          ; the message queue's size (3.1's
                                        ; task holds 64: MMP)

segment _DATA
        times 16 db 0                   ; KERNEL's instance data
fn      dd 0                            ; mmTaskCreate's: the function
inst    dd 0                            ; and its argument
        dd 0

segment _TEXT
global start
start:
        call far INITTASK               ; (it takes the stack's bottom
        or ax, ax                       ; from SP)
        jz .end
        mov bp, sp                      ; the frame: InitTask leaves SP
                                        ; one word below the stack's top
                                        ; (a 0 there), as 3.1's task's BP
        push di                         ; the instance
        push si                         ; the previous one
        xor ax, ax
        push ax
        call far WAITEVENT
        push di
        call far INITAPP
        push word QSIZE
        call far SETMESSAGEQUEUE
        call far OLDYIELD               ; KERNEL's LoadModule runs a new
                                        ; task at once: back to
                                        ; mmTaskCreate, which puts the
                                        ; function in our data
        pop si
        pop di
        mov ax, [fn]
        or ax, [fn + 2]
        jz .end
        mov ax, [inst]
        mov dx, [inst + 2]
        push dx
        push ax
        xor cx, cx
        mov es, cx
        cmp ax, ax
        call far [fn]
.end:   mov ax, 4C00h
        int 21h
