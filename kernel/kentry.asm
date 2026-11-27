; kentry.asm - KERNEL entry point and low-level glue (16-bit protected mode).
;
; Large model. All KERNEL code lives in one segment, _TEXT (C is compiled
; with -nt=_TEXT). DGROUP = _DATA CONST CONST2 _BSS.
;
; Contents:
;   kernel_entry     NE entry point, called by the bootstrap
;   int21_handler    protected-mode INT 21h hook (task termination, FileCdr)
;   int3d_handler    protected-mode INT 3Dh: FWAIT (floating-point fixups)
;   _task_switch     switch stacks between tasks (the scheduler, task.c)
;   task_tramp       first code a new task runs (from its first switch)
;   unimpl_entry     target of the "unimplemented import" thunks
;   INITTASK         register-based API wrapper around inittask_c
;   DOS3CALL         KERNEL.102
;   LOCAL*           Local heap APIs: pass the caller's DS to local.c
;   _dos21, _dpmi31  generic interrupt helpers for C
;   _seg_move        move a segment's contents and base, even if it is SS
;   __AHINCR ...     placeholders for KERNEL's constant exports

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS
extern _api_caller                      ; global.c: the last heap API caller

%include "intcall.inc"

extern _kmain                   ; void __cdecl __far kmain(bootinfo __far *)
extern _unimpl_report           ; void __cdecl __far unimpl_report(u16 index)
extern _inittask_c              ; void __cdecl __far inittask_c(regs16 __far *)
extern kexit_to_dos_            ; void __far kexit_to_dos(u8 code), AX = code
extern _task_exit_c             ; void __cdecl __far task_exit_c(u16 code)
extern _cur_task, _cur_pdb      ; task.c: the running task and its PDB
extern _dos_owner_task, _dos_nosync ; task.c: lazy DOS context state
extern _sync_dos_context        ; task.c: void __cdecl __far sync_dos_context(void)
extern _int_vec_cur             ; task.c: the vectors installed (TDB 32h)
extern _dos_exit_note           ; fault.c
extern _task_signal_exit        ; task.c
extern _filecdr_proc            ; kmisc.c: FileCdr's callback (0: none)
extern _filecdr_notify          ; kmisc.c

segment _DATA
global _old21
_old21          dd 0            ; previous protected-mode INT 21h vector

segment _BSS
; The stack a task's end runs on (task_exit_c: freeing the task, its
; module and DLLs, whose WEPs run here), as the task's own stack goes.
exit_stack      resb 4096
exit_stack_top:

segment _DATA

segment _TEXT

; ---------------------------------------------------------------------------
; kernel_entry: ES:BX -> bootinfo. Called far by the bootstrap.
global kernel_entry
kernel_entry:
        mov ax, DGROUP
        mov ds, ax
        push es
        push bx
        call far _kmain
        add sp, 4
        retf

; ---------------------------------------------------------------------------
; Protected-mode INT 21h hook. AH=4Ch from a running task ends the task;
; everything else goes to the previous handler.
; INT 3Dh: the loader turns the NOP FWAIT of a floating-point OS fixup
; (FIWRQQ) into INT 3Dh also when there is a coprocessor; observed on
; Windows 3.1 to work in a program that does not import WIN87EM, so KERNEL
; handles it (kernel/loader.c, os_fixup). Without a coprocessor WIN87EM
; replaces this vector with its emulator's.
global int3d_handler
int3d_handler:
        fwait
        iret

; AH=51h and 62h from a task return its PDB (observed on Windows 3.1,
; tests/apps/tk15.c: the selector GetCurrentPDB gives).
vec_ints        db 00h, 02h, 04h, 06h, 07h, 3Eh, 75h   ; TDB 32h-4Dh
global int21_handler
int21_handler:
        ; record the call (AX, BX, DX, caller CS:IP) in a ring of 16 for
        ; the fault report (fault.c)
        push ds
        push bx
        push bp
        mov bp, sp
        mov bx, DGROUP
        mov ds, bx
        mov bx, [_i21_pos wrt DGROUP]
        add bx, 10
        cmp bx, 160
        jb .rok
        xor bx, bx
.rok:   mov [_i21_pos wrt DGROUP], bx
        mov [_i21_ring + bx wrt DGROUP], ax
        push word [bp + 2]
        pop word [_i21_ring + bx + 2 wrt DGROUP]        ; caller's BX
        mov [_i21_ring + bx + 4 wrt DGROUP], dx
        push word [bp + 6]
        pop word [_i21_ring + bx + 6 wrt DGROUP]        ; IP
        push word [bp + 8]
        pop word [_i21_ring + bx + 8 wrt DGROUP]        ; CS
        cmp word [_filecdr_proc + 2 wrt DGROUP], 0      ; FileCdr's callback?
        pop bp
        pop bx
        pop ds
        jne cdr_check
cdr_none:
        cmp ah, 4Ch
        je .exit
        ; AH=00h (terminate, exit code 0): as 4C00h. Windows 3.1 hangs on
        ; it from a Windows program under both its KERNELs (the DOSX audit's
        ; DX10, ARCHITECTURE.md 9.6); DOS would end whatever
        ; program it takes for the caller. w16 ends the task.
        or ah, ah
        jnz .not00
        mov ax, 4C00h
        jmp .exit
.not00:
        cmp ah, 1Ah
        jne .not1a
        ; set DTA: the running task's (TDB 62h), then to DOS as before
        push ds
        push bx
        push dx
        mov bx, DGROUP
        mov ds, bx
        mov bx, [_cur_task wrt DGROUP]
        or bx, bx
        jz .dta0
        pop dx
        push dx
        push bp
        mov bp, sp
        push word [bp + 6]              ; the caller's DS
        mov ds, bx
        pop word [64h]
        mov [62h], dx
        pop bp
.dta0:  pop dx
        pop bx
        pop ds
        jmp .chain
.not1a:
        cmp ah, 25h
        jne .not25
        ; set vector: for INT 0, 2, 4, 6, 7, 3Eh, 75h the running task's
        ; (TDB 32h-4Dh) and int_vec_cur too (task.c), then DOS as before
        push bx
        push cx
        push si
        push di
        xor si, si
.v25l:  cmp al, [cs:vec_ints + si]
        je .v25f
        inc si
        cmp si, 7
        jb .v25l
        jmp .v25x
.v25f:  mov cx, ds                      ; the caller's DS: the vector's CS
        mov di, DGROUP
        mov ds, di
        shl si, 2
        mov [_int_vec_cur + si wrt DGROUP], dx
        mov [_int_vec_cur + si + 2 wrt DGROUP], cx
        mov di, [_cur_task wrt DGROUP]
        or di, di
        jz .v25r
        mov ds, di
        mov [si + 32h], dx
        mov [si + 34h], cx
.v25r:  mov ds, cx
.v25x:  pop di
        pop si
        pop cx
        pop bx
        jmp .chain
.not25:
        cmp ah, 51h
        je .pdb
        cmp ah, 62h
        jne .chain
.pdb:
        push ds
        mov bx, DGROUP
        mov ds, bx
        cmp word [_cur_task wrt DGROUP], 0
        je .nopdb
        mov bx, [_cur_pdb wrt DGROUP]
        pop ds
        iret
.nopdb:
        pop ds
        jmp .chain
.exit:
        push ds
        push bx
        mov bx, DGROUP
        mov ds, bx
        cmp word [_cur_task wrt DGROUP], 0
        pop bx
        pop ds
        jne task_exit_al
        ; no task: the process ends (a debugging note while booting
        ; Windows: who ended it)
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, sp
        push ss
        push ax
        cld                             ; (the process ends: as task_exit_al)
        call far _dos_exit_note         ; void __cdecl dos_exit_note(u16 far *)
        add sp, 4
        pop ax
        pop ds
.chain:
        call sync_dos_if_needed
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        cmp word [_i21_log wrt DGROUP], 0
        je .nolog
        cmp word [_i21_in wrt DGROUP], 0
        jne .nolog
        pop ax
        pop ds
        jmp i21_logcall
.nolog: pop ax
        pop ds
        sub sp, 4                       ; room for the far jump target
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_old21 wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_old21+2 wrt DGROUP]
        mov [bp+4], ax
        pop ax
        pop ds
        pop bp
        retf                            ; to the old handler; IRET frame intact

; [w16] int21log=1 (a debugging aid): the call goes to DOS from here and
; i21_note (fault.c) writes the registers before and after it to the
; debuglog, in the caller's task and on its stack; the caller gets DOS's
; registers and arithmetic flags. i21_note's own INT 21h calls (the log
; file) are not logged.
extern _i21_note                ; void __cdecl i21_note(u16 far *in, u16 far *out)
i21_logcall:
        push bp
        mov bp, sp                      ; [bp+2] IP, [bp+4] CS, [bp+6] FLAGS
        push es                         ; [bp-2] .. [bp-16]: the call
        push ds
        push di
        push si
        push dx
        push cx
        push bx
        push ax
        push word [bp+6]                ; an IRET frame back to .back
        push cs
        push .back
        sub sp, 4
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_old21 wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_old21+2 wrt DGROUP]
        mov [bp+4], ax
        pop ax
        pop ds
        pop bp
        retf
.back:
        pushf                           ; [bp-18] .. [bp-34]: DOS's answer
        push es
        push ds
        push di
        push si
        push dx
        push cx
        push bx
        push ax
        mov ax, sp
        mov bx, DGROUP
        mov ds, bx
        mov word [_i21_in wrt DGROUP], 1
        push ss
        push ax
        lea ax, [bp-16]
        push ss
        push ax
        cld
        call far _i21_note
        add sp, 8
        mov word [_i21_in wrt DGROUP], 0
        pop ax
        pop bx
        pop cx
        pop dx
        pop si
        pop di
        pop ds
        pop es
        push ax
        mov ax, [bp-18]
        and ax, 08D5h
        and word [bp+6], 0F72Ah
        or [bp+6], ax
        pop ax
        mov sp, bp
        pop bp
        iret

; Ensure DOS's PSP/DTA/CWD matches _cur_task before chaining to _old21.
; Preserves all registers and flags.
sync_dos_if_needed:
        pushf
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        cmp byte [_dos_nosync wrt DGROUP], 0
        jne .done
        mov ax, [_cur_task wrt DGROUP]
        or ax, ax
        jz .done
        cmp ax, [_dos_owner_task wrt DGROUP]
        je .done
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push es
        cld
        call far _sync_dos_context
        pop es
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
.done:
        pop ax
        pop ds
        popf
        ret

; With a FileCdr callback installed (kmisc.c): the calls that change the
; file system (observed on 3.1, TK64: 39h, 3Ah, 3Ch, 41h, 4301h, 56h, 5Ah,
; 5Bh, 6Ch) go to DOS from here, and when DOS succeeds filecdr_notify
; calls the callback with the caller's AX and the path (DS:DX; DS:SI for
; 6Ch), in the caller's task and on its stack. The caller gets DOS's
; registers and arithmetic flags. Everything else goes on as before.
cdr_check:
        cmp ah, 3Ch
        je .do
        cmp ah, 41h
        je .do
        cmp ax, 4301h
        je .do
        cmp ah, 39h
        je .do
        cmp ah, 3Ah
        je .do
        cmp ah, 56h
        je .do
        cmp ah, 5Ah
        je .do
        cmp ah, 5Bh
        je .do
        cmp ah, 6Ch
        jne cdr_none
.do:
        push bp
        mov bp, sp                      ; [bp+2] IP, [bp+4] CS, [bp+6] FLAGS
        push ax                         ; [bp-2] the code
        push ds                         ; [bp-4] the path
        push dx                         ; [bp-6]
        cmp ah, 6Ch
        jne .dos
        mov [bp-6], si
.dos:
        call sync_dos_if_needed
        ; INT 21h to the previous handler: an IRET frame back to .back
        push word [bp+6]
        push cs
        push .back
        sub sp, 4                       ; room for the far jump target
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_old21 wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_old21+2 wrt DGROUP]
        mov [bp+4], ax
        pop ax
        pop ds
        pop bp
        retf
.back:
        jc .ret                         ; failed: nothing changed
        pushf
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        push word [bp-4]
        push word [bp-6]
        push word [bp-2]
        cld
        call far _filecdr_notify        ; void __cdecl filecdr_notify(u16, char far *)
        add sp, 6
        pop es
        pop ds
        popa
        popf
.ret:
        ; DOS's arithmetic flags (OF SF ZF AF PF CF) into the caller's
        push ax
        pushf
        pop ax
        and ax, 08D5h
        and word [bp+6], 0F72Ah
        or [bp+6], ax
        pop ax
        mov sp, bp
        pop bp
        iret

; ---------------------------------------------------------------------------
; void __cdecl task_switch(u16 far *save, u16 ss, u16 sp)
; Push the registers a task keeps across a switch, store SS:SP at *save
; (SP first; not if save is NULL), load the other task's SS:SP and pop its
; registers: it returns from its own task_switch, or, for a new task, into
; task_tramp (task.c builds that frame). The frame a parked task's TDB
; 02h points at is Windows 3.1's, which TOOLHELP and debuggers read and
; change (TaskGetCSIP, TaskSetCSIP, TaskSwitch, StackTraceFirst): DX BX
; ES CX AX DI SI DS (Pietrek, Windows Internals ch. 6, lists these), then
; BP at 10h and the far address the task goes on at at 12h (Schulman,
; Undocumented Windows ch. 10; observed under Windows 3.1 with
; tests/toolhelp/thp.c, section k). AX is not given back to the task.
global _task_switch
_task_switch:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push ax
        push cx
        push es
        push bx
        push dx
        mov ax, [bp+10]                 ; ss
        mov dx, [bp+12]                 ; sp
        les bx, [bp+6]
        mov cx, es
        or cx, bx
        jz .load
        mov [es:bx], sp
        mov [es:bx+2], ss
.load:
        mov ss, ax
        mov sp, dx
        pop dx
        pop bx
        pop ax                          ; ES: it may name a block freed
        verr ax                         ; since (an ended task's TDB)
        jnz .esz
        push bx                         ; or discarded since: not present
        lar bx, ax                      ; (VERR passes it; KERNEL audit,
        test bh, 80h                    ; medium 20)
        pop bx
        jnz .es
.esz:   xor ax, ax
.es:
        mov es, ax
        pop cx
        pop ax
        pop di
        pop si
        pop ds
        pop bp
        retf

; A new task's first switch lands here, with its start-up registers
; popped; the far address of its entry point is next on the stack, at its
; initial SP. AX = 0 at a task's entry (observed on Windows 3.1).
global task_tramp
extern task_start_hook          ; khook.asm: the task-start notification
task_tramp:
        call far task_start_hook
        xor ax, ax
        retf

; AL = exit code. Leaves the task's stack for KERNEL's exit stack and ends
; the task there (task_exit_c does not return).
task_exit_al:
        ; first, on the task's own stack, the task's signal procedure
        ; (USER's, TDB 2Ah) hears that it ends (task.c, Pietrek: ExitCall)
        sti
        cld                             ; (the C code below copies forwards;
                                        ; a task may end with DF set: KERNEL
                                        ; audit, medium 18)
        mov bx, DGROUP
        mov ds, bx
        push ax
        call far _task_signal_exit
        pop ax
        mov bx, DGROUP
        mov ds, bx
        mov ss, bx
        mov sp, exit_stack_top wrt DGROUP
        sti
        xor ah, ah
        push ax
        call far _task_exit_c

; ---------------------------------------------------------------------------
; Unimplemented-import thunks do:  mov ax, <index> / jmp far unimpl_entry
global unimpl_entry
unimpl_entry:
        mov bx, DGROUP
        mov ds, bx
        push ax
        call far _unimpl_report
        add sp, 2
        cmp word [_cur_task wrt DGROUP], 0
        jne .task
        mov ax, 0FFh                    ; no task yet (a DLL's LibMain)
        call far kexit_to_dos_          ; Watcom register convention
.task:
        mov ax, 4CFFh                   ; as if INT 21h AX=4CFFh
        jmp task_exit_al

; ---------------------------------------------------------------------------
; InitTask (KERNEL.91) is register-based: it takes and returns values in
; registers. Build a regs16 on the stack and let C fill in the results.
global INITTASK
INITTASK:
        pushf
        push es
        push ds
        push di
        push si
        push dx
        push cx
        push bx
        push ax
        mov ax, sp
        push ss
        push ax
        mov ax, DGROUP
        mov ds, ax
        call far _inittask_c
        add sp, 4
        pop ax
        pop bx
        pop cx
        pop dx
        pop si
        pop di
        pop ds
        pop es
        popf
        ; Windows 3.1 returns with a zero word pushed on the caller's stack
        ; and BP pointing at it (observed with tests/toolhelp/thp.c,
        ; section s: SP is 2 less than at the call, BP = SP, [BP] = 0), so
        ; that a chain of frames (a stack trace) ends with the start-up
        ; code's frame. Moves the return address down a word; no flag
        ; changes.
        push ax                         ; the room
        push ax
        mov bp, sp                      ; [bp] AX [bp+2] room [bp+4] IP [bp+6] CS
        mov ax, [bp+4]
        mov [bp+2], ax
        mov ax, [bp+6]
        mov [bp+4], ax
        mov word [bp+6], 0
        pop ax
        mov bp, sp
        lea bp, [bp+4]                  ; the zero word: SP once returned
        retf

; ---------------------------------------------------------------------------
; The Local* APIs work on the local heap in the caller's DS. KAPI functions
; load KERNEL's DS, so these wrappers call the C function (far pascal) with
; the caller's DS as an extra first argument, followed by copies of the
; caller's NARGS word arguments, and restore DS on return.
;   LOCALAPI exported-name, c-function, nargs
%macro LOCALAPI 3-4 L_NONE
extern %2
global %1
%1:
        push bp
        mov bp, sp
        push ds                 ; the caller's CS:IP for heapcheck
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 2]
        mov [_api_caller wrt DGROUP], ax
        mov ax, [bp + 4]
        mov [_api_caller + 2 wrt DGROUP], ax
        pop ax
        pop ds
        push ds
        push es
        push ds
%assign i %3
%rep %3
        push word [bp + 4 + 2 * i]
%assign i i - 1
%endrep
        call far %2
        %4
        pop es
        pop ds
        pop bp
        retf 2 * %3
%endmacro
; What the local-heap functions leave in CX (TK43, 3.1: LocalAlloc,
; LocalReAlloc, LocalLock, LocalUnlock, LocalSize and LocalFlags the
; result, = AX; LocalFree 1; ES kept by all; GDI relies on CX: it did
; not start when LocalAlloc kept CX)
%macro L_NONE 0
%endmacro
%macro L_CXAX 0
        mov cx, ax
%endmacro
%macro L_CX1 0
        mov cx, 1
%endmacro

LOCALAPI LOCALINIT,    LINIT,    3      ; KERNEL.4
LOCALAPI LOCALALLOC,   LALLOC,   2, L_CXAX      ; KERNEL.5
LOCALAPI LOCALREALLOC, LREALLOC, 3, L_CXAX      ; KERNEL.6
LOCALAPI LOCALFREE,    LFREE,    1, L_CX1      ; KERNEL.7
LOCALAPI LOCALLOCK,    LLOCK,    1, L_CXAX      ; KERNEL.8
LOCALAPI LOCALUNLOCK,  LUNLOCK,  1, L_CXAX      ; KERNEL.9
LOCALAPI LOCALSIZE,    LSIZE,    1, L_CXAX      ; KERNEL.10
LOCALAPI LOCALHANDLE,  LHANDLE,  1      ; KERNEL.11
LOCALAPI LOCALFLAGS,   LFLAGS,   1, L_CXAX      ; KERNEL.12
LOCALAPI LOCALCOMPACT, LCOMPACT, 1      ; KERNEL.13
; Other APIs that need the caller's DS (procinst.c)
LOCALAPI MAKEPROCINSTANCE, MPI, 3       ; KERNEL.51
LOCALAPI GETINSTANCEDATA, GIDATA, 3     ; KERNEL.54
; Local atoms (atom.c)
LOCALAPI INITATOMTABLE, AINIT,  1      ; KERNEL.68
LOCALAPI FINDATOM,     AFIND,    2      ; KERNEL.69
LOCALAPI ADDATOM,      AADD,     2      ; KERNEL.70
LOCALAPI DELETEATOM,   ADEL,     1      ; KERNEL.71
LOCALAPI GETATOMNAME,  ANAME,    4      ; KERNEL.72
LOCALAPI GETATOMHANDLE, AHANDLE, 1      ; KERNEL.73

; LockSegment and UnlockSegment: -1 names the caller's DS (TK62, 3.1), so
; the C function gets the caller's DS first, as LOCALAPI's do; unlike
; them, ES is left as the C function sets it (0: observed, tk17.c).
;   SEGLOCKAPI exported-name, c-function
%macro SEGLOCKAPI 2
extern %2
global %1
%1:
        push bp
        mov bp, sp
        push ds
        push ds
        push word [bp + 6]
        call far %2
        pop ds
        pop bp
        retf 2
%endmacro
SEGLOCKAPI LOCKSEGMENT, LSEGLOCK    ; KERNEL.23
SEGLOCKAPI UNLOCKSEGMENT, LSEGUNLOCK ; KERNEL.24
; for the Windows boot (local.c)
LOCALAPI LOCALSHRINK,  LSHRINK,  2      ; KERNEL.121
LOCALAPI LOCALCOUNTFREE, LCOUNTFREE, 0  ; KERNEL.161
LOCALAPI LOCALHEAPSIZE, LHEAPSIZE, 0    ; KERNEL.162
LOCALAPI LOCALHANDLEDELTA, LHDELTA, 1   ; KERNEL.310

; ---------------------------------------------------------------------------
; u16 __cdecl mod_call(u16 cs, u16 ip, u16 ds, u16 cx, u16 di,
;                      u16 nargs, u16 arg)
; Far-call cs:ip on the current stack the way KERNEL calls a DLL's entry
; point (observed on Windows 3.1: AX = 1, CX = heap size, DX = DS, SI = 0,
; DI = hInstance, ES = 0, DS = DGROUP), pushing `arg` first if nargs is
; nonzero (a Pascal procedure such as WEP pops it). Returns AX. Relies on
; the callee keeping BP, as Windows procedures do.
global _mod_call
_mod_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        push word [bp + 6]              ; far call target: cs
        push word [bp + 8]              ;                   ip
        mov bx, sp
        cmp word [bp + 16], 0
        je .call
        push word [bp + 18]
.call:
        mov cx, [bp + 12]
        mov di, [bp + 14]
        mov dx, [bp + 10]
        xor si, si
        mov es, si
        mov ax, 1
        mov ds, dx
        call far [ss:bx]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; u32 __cdecl sl_call(u16 cs, u16 ip, u16 sel1, u16 hnd1, u16 nargs,
;                     u16 a0, u16 a1, u16 a2)
; A self-loading program's BootApp or LoadAppSeg (loader.c boot_app): the
; far Pascal procedure cs:ip with nargs (0-3) word arguments, a0 pushed
; first, and the registers Windows 3.1's KERNEL gives (TK78): DS =
; KERNEL's data segment, CX = ES = sel1 (segment 1's selector), DX =
; hnd1 (its handle). Returns DX:AX. Relies on the callee keeping BP.
global _sl_call
_sl_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        push word [bp + 6]              ; far call target: cs
        push word [bp + 8]              ;                   ip
        mov bx, sp
        mov cx, [bp + 14]
        lea si, [bp + 16]
.arg:   jcxz .go
        push word [ss:si]
        add si, 2
        dec cx
        jmp .arg
.go:    mov ax, DGROUP
        mov ds, ax
        mov cx, [bp + 10]
        mov es, cx
        mov dx, [bp + 12]
        xor ax, ax
        call far [ss:bx]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; DWORD FAR PASCAL EntryAddrProc(WORD hBlock, WORD wEntryNo): the
; self-loading table's (loader.c boot_app). DX:AX from sl_entry_c; as
; Windows 3.1's (TK78): CX = AX | DX (ZF set for none, CF clear), ES =
; hBlock, BX = the entry's record in the module table (sl_entry_c; the
; caller's BX when there is none).
extern _sl_entry_c              ; u32 __cdecl sl_entry_c(u16, u16, u16 far *)
global sl_entryaddr
sl_entryaddr:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push bx                         ; [bp - 8]: BX, or the record
        mov ax, DGROUP
        mov ds, ax
        push ss
        lea ax, [bp - 8]
        push ax
        push word [bp + 6]              ; wEntryNo
        push word [bp + 8]              ; hBlock
        cld
        call far _sl_entry_c
        add sp, 8
        mov es, [bp + 8]
        pop bx
        pop di
        pop si
        pop ds
        pop bp
        mov cx, ax
        or cx, dx
        retf 4

; DOS3Call (KERNEL.102): issue INT 21h with the caller's registers.
global DOS3CALL
DOS3CALL:
        int 21h
        retf

; NoHookDosCall (KERNEL.101): INT 21h past KERNEL's own hook (Schulman).
; w16's hook only ends tasks (4Ch) and answers 51h/62h; this calls the
; handler that was there before KERNEL (old21) with the caller's
; registers, through an IRET frame back to here. UNVERIFIED.
global NOHOOKDOSCALL
NOHOOKDOSCALL:
        call sync_dos_if_needed
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
        mov ax, [_old21 wrt DGROUP]
        mov [bp + 2], ax
        mov ax, [_old21 + 2 wrt DGROUP]
        mov [bp + 4], ax
        pop ax
        pop ds
        pop bp
        retf
.back:  retf

; GetTaskQueueDS / GetTaskQueueES (KERNEL.118, 119): load DS (ES) with
; the current task's message queue (TDB 20h; Schulman). Observed on 3.1
; (TK24): every other register and the carry flag are kept. (w16's first
; version returned the queue in AX too, and USER's PostQuitMessage then
; never ended GetMessage.)
global GETTASKQUEUEDS, GETTASKQUEUEES
GETTASKQUEUEDS:
        pushf
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_cur_task wrt DGROUP]
        or ax, ax
        jz .none
        mov ds, ax
        mov ax, [20h]
.none:  mov ds, ax
        pop ax
        popf
        retf
GETTASKQUEUEES:
        push ds
        call far GETTASKQUEUEDS
        push ds
        pop es
        pop ds
        retf

; ---------------------------------------------------------------------------
; void __cdecl seg_move(u16 sel, u16 tmp, u16 nwords, u32 newbase)
; Copy nwords words from sel:0 to tmp:0, then set sel's base to newbase
; (DPMI 0007h). sel may be the current stack segment: then the call runs
; on a stack of KERNEL's from before the copy until SS is sel again with
; the new base, so nothing is pushed onto the old memory after the copy.
; (HDPMI16 pops INT 31h's frame through the SS it has cached; DOSX does
; not: under DOSX, KERNEL's first version, which called INT 31h on the
; moving stack, returned through zeros - observed with TK19 as the shell
; in tests/dropin.sh.) The caller frees the old memory afterwards.
; Interrupts stay off from before the copy until the new base is set: an
; interrupt handler (the mouse's or keyboard's, USER's event code) may
; write into the segment being moved, which would be lost in the old
; copy, and must not run on this stack. UNVERIFIED: how 3.1's KRNL286
; does it. The stack only holds the INT 31h call's frame then; 1 KB.
segment _BSS
move_stack      resb 1024
move_stack_top:
segment _TEXT
global _seg_move
_seg_move:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov dx, [bp + 12]               ; newbase
        mov cx, [bp + 14]
        mov bx, [bp + 6]                ; sel
        mov ax, [bp + 10]               ; nwords
        mov ds, [bp + 6]
        mov es, [bp + 8]
        pushf                           ; the caller's interrupt flag
        ; onto KERNEL's stack (SS:SP kept in DI:SI)
        mov si, sp
        mov di, ss
        cli
        mov ss, [cs:dgroup_word]
        mov sp, move_stack_top wrt DGROUP
        push si
        push di
        push cx
        mov cx, ax
        xor si, si
        xor di, di
        cld
        rep movsw
        pop cx
        mov ax, 0007h
        int 31h
        cli                             ; (a host that set IF: KERNEL
                                        ; audit, medium 24)
        sbb ax, ax                      ; AX = 1 moved, 0 refused
        inc ax
        pop di
        pop si
        mov ss, di                      ; sel, now at its new base, or SS
        mov sp, si
        ; the caller's IF back explicitly: in 386 Enhanced mode POPF
        ; leaves it alone below IOPL (ARCHITECTURE.md 12.14)
        push bp
        mov bp, sp
        test byte [bp + 3], 2
        pop bp
        jz .ifoff
        sti
.ifoff: popf
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
dgroup_word     dw DGROUP

; ---------------------------------------------------------------------------
; void __cdecl kcopyw(u16 dsel, u16 ssel, u16 nwords): ssel:0 to dsel:0
; void __cdecl kzerow(u16 sel, u16 nwords): zero sel:0
global _kcopyw, _kzerow
_kcopyw:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov es, [bp + 6]
        mov ds, [bp + 8]
        mov cx, [bp + 10]
        xor si, si
        xor di, di
        cld
        rep movsw
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
_kzerow:
        push bp
        mov bp, sp
        push di
        push es
        mov es, [bp + 6]
        mov cx, [bp + 8]
        xor di, di
        xor ax, ax
        cld
        rep stosw
        pop es
        pop di
        pop bp
        retf

; u16 __cdecl notify_call(u32 proc, u16 h)
; Call a GlobalNotify procedure, BOOL FAR PASCAL proc(HGLOBAL h), with
; DS = ES = SS (Pietrek, "Windows Internals", GlobalNotify). Returns AX.
global _notify_call
_notify_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        push word [bp + 10]             ; h
        mov ax, ss
        mov ds, ax
        mov es, ax
        call far [ss:bp + 6]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

INTCALL _dos21, 21h, far, pm
INTCALL _dpmi31_host, 31h, far, pm ; dosx.c: dpmi31 wraps it

; ---------------------------------------------------------------------------
; Constant exports. wlink cannot export absolute symbols, so these labels
; are placeholders in BOOT_TEXT. KERNEL resolves these ordinals to their
; runtime values before it looks at its entry table (see kconst_lookup in
; module.c). (They had a segment of their own, KCONST, KERNEL's fifth:
; 3.1's KERNEL has four, its DGROUP the fourth; TK62 "kseg".)
segment BOOT_TEXT public align=1 class=CODE use16
global __AHSHIFT, __AHINCR, __ROMBIOS, __A000H, __WINFLAGS, __D000H
global __B000H, __B800H, __0000H, __E000H, __0040H, __F000H, __C000H
__AHSHIFT:      db 0
__AHINCR:       db 0
__ROMBIOS:      db 0
__A000H:        db 0
__WINFLAGS:     db 0
__D000H:        db 0
__B000H:        db 0
__B800H:        db 0
__0000H:        db 0
__E000H:        db 0
__0040H:        db 0
__F000H:        db 0
__C000H:        db 0

; ---------------------------------------------------------------------------
; Fault reporting while booting Windows (fault.c). A DPMI exception
; handler (DPMI 0.9, 16-bit frame: return CS:IP to the host, error code,
; IP, CS, FLAGS, SP, SS) records the faulting state, points the frame at
; fault_exit on KERNEL's exit stack and returns to the host, which
; resumes there; fault_exit calls fault_report on the report's own stack
; (does not return), or fault_report_again for a fault inside the report.
extern _fault_report            ; void __cdecl __far fault_report(void)
extern _fault_report_again      ; void __cdecl __far fault_report_again(void)
extern _gp_handler              ; u16 __cdecl __far gp_handler(void)
global _fault_regs, _rpt_active
segment _BSS
; the report's own stack: never the faulting one (a fault with SS:SP at
; the end of a stack, or SS not a stack at all, is reported too), nor
; the exit stack a task's end may be using
report_stack    resb 3072
report_stack_top:
_rpt_active     resb 1          ; a report is running (fault.c)
global _i21_ring, _i21_pos
_i21_ring       resw 80         ; 16 x (ax bx dx ip cs)
_i21_pos        resw 1
global _i21_log, _i21_in
_i21_log        resw 1          ; [w16] int21log: log every call (i21_note)
_i21_in         resw 1          ; i21_note is running (its own calls not logged)
_fault_regs    resw 16         ; exc err ip cs fl sp ss ax bx cx dx si di bp ds es
in_fault        resb 1          ; set while a fault is being handled
gp_ip           resw 1
segment _TEXT

%macro FAULTENTRY 1
global fault_entry_%1
fault_entry_%1:
        push word %1
        jmp fault_common
%endmacro
FAULTENTRY 00h
FAULTENTRY 06h
FAULTENTRY 0Bh
FAULTENTRY 0Ch
FAULTENTRY 0Dh
FAULTENTRY 0Eh

fault_common:
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop word [_fault_regs + 14 wrt DGROUP]          ; ax
        pop word [_fault_regs + 28 wrt DGROUP]          ; ds
        mov ax, [bp + 2]
        mov [_fault_regs wrt DGROUP], ax                ; exception
        mov ax, [bp + 8]
        mov [_fault_regs + 2 wrt DGROUP], ax            ; error code
        mov ax, [bp + 10]
        mov [_fault_regs + 4 wrt DGROUP], ax            ; ip
        mov ax, [bp + 12]
        mov [_fault_regs + 6 wrt DGROUP], ax            ; cs
        mov ax, [bp + 14]
        mov [_fault_regs + 8 wrt DGROUP], ax            ; flags
        mov ax, [bp + 16]
        mov [_fault_regs + 10 wrt DGROUP], ax           ; sp
        mov ax, [bp + 18]
        mov [_fault_regs + 12 wrt DGROUP], ax           ; ss
        mov [_fault_regs + 16 wrt DGROUP], bx
        mov [_fault_regs + 18 wrt DGROUP], cx
        mov [_fault_regs + 20 wrt DGROUP], dx
        mov [_fault_regs + 22 wrt DGROUP], si
        mov [_fault_regs + 24 wrt DGROUP], di
        mov ax, [bp]
        mov [_fault_regs + 26 wrt DGROUP], ax           ; bp
        mov [_fault_regs + 30 wrt DGROUP], es
        mov word [bp + 10], fault_exit
        mov word [bp + 12], cs
        ; on the exit stack: a fault while on it (task_exit_c, the WEPs it
        ; calls) goes on below the faulting SP, not over the frames in use
        ; there (KERNEL audit #12); else from the top
        mov ax, exit_stack_top wrt DGROUP
        cmp word [bp + 18], DGROUP
        jne .top
        mov bx, [bp + 16]
        cmp bx, exit_stack_top wrt DGROUP
        ja .top
        cmp bx, (exit_stack + 1024) wrt DGROUP
        jb .top                         ; (no room left: from the top)
        lea ax, [bx - 512]
.top:   mov [bp + 16], ax
        mov word [bp + 18], DGROUP
        mov bx, [_fault_regs + 16 wrt DGROUP]
        pop bp
        add sp, 2
        retf
fault_exit:
        mov ax, DGROUP
        mov ds, ax
        mov es, ax
        cld                             ; (as task_exit_al)
        ; a fault inside the report: fault.c notes it and reports again,
        ; on a fresh report stack (the frames of the attempt are dropped)
        cmp byte [_rpt_active wrt DGROUP], 0
        jne .again
        ; a fault while one is being handled (fault_regs holds the second
        ; one now): no recovery, reported as it is
        cmp byte [in_fault wrt DGROUP], 0
        jne .report
        mov byte [in_fault wrt DGROUP], 1
        call far _gp_handler            ; a "__GP" table entry for CS:IP?
        test ax, ax
        jnz .gp
.report:
        mov byte [_rpt_active wrt DGROUP], 1
        mov ax, DGROUP
        mov ss, ax
        mov sp, report_stack_top wrt DGROUP
        call far _fault_report
.hang:  jmp .hang
.again: mov ax, DGROUP
        mov ss, ax
        mov sp, report_stack_top wrt DGROUP
        call far _fault_report_again
        jmp .hang
; resume at the faulting CS:AX, the registers as they were (DS and ES
; made 0 by gp_handler when not valid), on the faulting stack with the
; error code and the faulting IP pushed (TK40, 3.1: [SP] = IP, [SP+2] =
; the error code, flags 3202h: the arithmetic flags clear)
.gp:    mov [gp_ip wrt DGROUP], ax
        mov ss, [_fault_regs + 12 wrt DGROUP]
        mov sp, [_fault_regs + 10 wrt DGROUP]
        push word [_fault_regs + 2 wrt DGROUP]          ; error code
        push word [_fault_regs + 4 wrt DGROUP]          ; faulting IP
        mov ax, [_fault_regs + 8 wrt DGROUP]            ; flags: IF, DF, IOPL
        and ax, 7600h                                   ; and NT kept, the
        or al, 2                                        ; others clear (3.1)
        push ax
        push word [_fault_regs + 6 wrt DGROUP]          ; cs
        push word [gp_ip wrt DGROUP]                    ; ip
        push word [_fault_regs + 28 wrt DGROUP]         ; ds
        mov es, [_fault_regs + 30 wrt DGROUP]
        mov ax, [_fault_regs + 14 wrt DGROUP]
        mov bx, [_fault_regs + 16 wrt DGROUP]
        mov cx, [_fault_regs + 18 wrt DGROUP]
        mov dx, [_fault_regs + 20 wrt DGROUP]
        mov si, [_fault_regs + 22 wrt DGROUP]
        mov di, [_fault_regs + 24 wrt DGROUP]
        mov bp, [_fault_regs + 26 wrt DGROUP]
        mov byte [in_fault wrt DGROUP], 0
        pop ds
        iret

; ---------------------------------------------------------------------------
; Boot watchdog (fault.c): samples the interrupted CS:IP into a ring of 64
; from the RTC's periodic interrupt (IRQ 8, INT 70h, 16 per second),
; which nothing in Windows Standard mode uses, and counts them; when
; wd_limit pass without progress (fault.c resets wd_ticks), it points the
; interrupt's return at wd_fire, which reports the samples. (A first
; version sampled IRQ 0; standing in front of SYSTEM.DRV's timer handler
; stopped SYSTEM's timer procedures once a task had called InitApp.)
extern _wd_report               ; void __cdecl __far wd_report(void)
global _wd_ticks, _wd_limit, _wd_samples, _wd_pos
global wd_int70
segment _DATA
_wd_ticks       dw 0
_wd_limit       dw 0
_wd_pos         dw 0
segment _BSS
_wd_samples     resw 128        ; 64 x (ip cs)
segment _TEXT
wd_int70:
        push ds
        push bx
        push ax
        push bp
        mov bp, sp                      ; [bp+8] IP, CS, FLAGS
        mov al, 0Ch                     ; acknowledge the RTC (register C)
        out 70h, al
        in al, 71h
        mov al, 20h                     ; end of interrupt, slave and master
        out 0A0h, al
        out 20h, al
        mov bx, DGROUP
        mov ds, bx
        mov bx, [_wd_pos wrt DGROUP]
        add bx, 4
        and bx, 255
        mov [_wd_pos wrt DGROUP], bx
        push word [bp + 8]
        pop word [_wd_samples + bx wrt DGROUP]
        push word [bp + 10]
        pop word [_wd_samples + bx + 2 wrt DGROUP]
        inc word [_wd_ticks wrt DGROUP]
        mov bx, [_wd_limit wrt DGROUP]
        or bx, bx
        jz .out
        cmp [_wd_ticks wrt DGROUP], bx
        jb .out
        mov word [_wd_limit wrt DGROUP], 0
        mov word [bp + 8], wd_fire
        mov word [bp + 10], cs
.out:   pop bp
        pop ax
        pop bx
        pop ds
        iret
wd_fire:
        mov ax, DGROUP
        mov ds, ax
        mov es, ax
        cld                             ; (as task_exit_al)
        mov ss, ax
        mov sp, exit_stack_top wrt DGROUP
        sti
        call far _wd_report
.hang:  jmp .hang

; ---------------------------------------------------------------------------
; Global-heap exports with the registers 3.1 leaves (TK43, KRNL286, a
; moveable, a fixed and a discardable block): GlobalLock keeps BX and
; returns the selector in CX as well as in DX (GDI relies on CX: it did
; not start when CX was kept), ES = 0; GlobalUnlock keeps BX, CX = 0,
; ES = 0; GlobalFlags CX = the flags (= AX), DX = the selector, ES = 0;
; GlobalFree CX = DX = 0, ES = 0; GlobalReAlloc CX = the handle (= AX),
; ES = 0; GlobalSize ES = 0. (BX after GlobalSize, GlobalFlags,
; GlobalReAlloc and GlobalFree, CX after GlobalSize, DX after KRNL286's
; GlobalUnlock and GlobalReAlloc hold other values on 3.1, not copied.)
; The C functions do the work, called with KERNEL's DS. The caller's DS
; comes back 0 when it was the block freed (Pietrek, GlobalFree: the DS
; pushed on entry is zeroed when it is the selector or handle freed, so
; that restoring it does not fault; observed, tests/apps/tk17.c "fs":
; GlobalFree with DS = the block returns DS = 0) or is no longer a
; present selector (GlobalReAlloc, GlobalDiscard). w16 restored it
; before the call, and the C function's own epilogue reloaded the dead
; selector.
;   GHEAPAPI exported-name, c-function, nargs, after
%macro GHEAPAPI 4
extern %2
global %1
%1:
        push bp
        mov bp, sp
        push ds                 ; [bp-2] the caller's DS
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 2]        ; the caller's CS:IP for heapcheck
        mov [_api_caller wrt DGROUP], ax
        mov ax, [bp + 4]
        mov [_api_caller + 2 wrt DGROUP], ax
        pop ax
        push bx
%assign i %3
%rep %3
        push word [bp + 4 + 2 * i]
%assign i i - 1
%endrep
        call far %2
        %4
        push ax
        push bx
        mov ax, [bp - 2]
        or ax, ax
        jz %%dsok
%ifidn %4, AFTER_FREE
        mov bx, [bp + 4 + 2 * %3]       ; GlobalFree's handle
        or bx, 1
        or ax, 1
        cmp ax, bx
        je %%dsz
        mov ax, [bp - 2]
%endif
        lar bx, ax
        jnz %%dsz
        test bh, 80h                    ; present
        jnz %%dsok
%%dsz:  mov word [bp - 2], 0
%%dsok: pop bx
        pop ax
        push 0
        pop es
        pop bx
        pop ds
        pop bp
        retf 2 * %3
%endmacro
%macro AFTER_LOCK 0
        mov cx, dx
%endmacro
%macro AFTER_UNLOCK 0
        xor cx, cx
%ifdef KRNL386
        ; KRNL386 returns the block's handle in DX (TK43 on 3.1, both
        ; modes: the selector - 1 for a moveable, a fixed and a
        ; discardable block). Win32s's WIN32S16.DLL hands that DX to
        ; W32S.386 as the selector of the buffer it just unlocked.
        mov dx, [bp + 6]
        and dx, 0FFFEh
%endif
%endmacro
%macro AFTER_FLAGS 0
        mov cx, ax
        mov dx, [bp + 6]
        or dx, 1
%endmacro
%macro AFTER_FREE 0
        xor cx, cx
        xor dx, dx
%ifdef KRNL386
        ; KRNL386 returns FS = GS = 0, whatever they held (DRP, tests/drv/
        ; drp.c "galloc": FS and GS holding the freed block, and GS holding
        ; DS, all came back 0; KRNL286 keeps them). QuickTime frees blocks
        ; it still has in FS: DOSX faulted popping the dead selector into
        ; FS on the way back from an interrupt (docs/notes/
        ; quicktime-handover.md)
        push cx
        db 8Eh, 0E1h            ; mov fs, cx
        db 8Eh, 0E9h            ; mov gs, cx
        pop cx
%endif
%endmacro
%macro AFTER_NONE 0
%endmacro
%macro AFTER_REALLOC 0
        mov cx, ax
%endmacro
; GlobalAlloc: the caller's CS noted for the C function (a GMEM_SHARE
; block belongs to the calling module), the stack left as it was
extern GLOBALALLOC, _galloc_cs
global GLOBALALLOCX
GLOBALALLOCX:
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 4]
        mov [_galloc_cs wrt DGROUP], ax
        pop ax
        pop ds
        pop bp
        jmp far GLOBALALLOC

GHEAPAPI GLOBALLOCKX,    GLOBALLOCK,    1, AFTER_LOCK
GHEAPAPI GLOBALUNLOCKX,  GLOBALUNLOCK,  1, AFTER_UNLOCK
GHEAPAPI GLOBALFLAGSX,   GLOBALFLAGS,   1, AFTER_FLAGS
GHEAPAPI GLOBALFREEX,    GLOBALFREE,    1, AFTER_FREE
GHEAPAPI GLOBALSIZEX,    GLOBALSIZE,    1, AFTER_NONE
GHEAPAPI GLOBALREALLOCX, GLOBALREALLOC, 4, AFTER_REALLOC

; FarSetOwner (KERNEL.403) keeps every register: AX, BX, CX, DX and ES
; come back as they went in (TK43 on 3.1's KRNL386). Win32s's
; WIN32S16.DLL relies on it: it hands the DX and ES it had before the
; call (GlobalDOSAlloc's segment) on to W32S.386.
extern FARSETOWNER
global FARSETOWNERX
FARSETOWNERX:
        push bp
        mov bp, sp
        push ax
        push bx
        push cx
        push dx
        push es
        push ds
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 8]              ; the selector
        push word [bp + 6]              ; the owner
        call far FARSETOWNER
        pop ds
        pop es
        pop dx
        pop cx
        pop bx
        pop ax
        pop bp
        retf 4

; GetExePtr (KERNEL.133): AX and CX the hModule, ES the task the handle
; belongs to (an hInstance, a block the task owns) or the hModule itself
; (an hModule), DX and BX kept; for a task handle AX and CX 0, DX and ES
; the task (TK43 on 3.1's KRNL386). Win32s's WIN32S16.DLL takes ES after
; GetExePtr(hInstance) as the program's TDB and reads its module name
; at F2h (ARCHITECTURE.md 12.19).
extern GETEXEPTRC
extern _task_of_handle          ; u16 __cdecl task_of_handle(u16)
global GETEXEPTR
GETEXEPTR:
        push bp
        mov bp, sp
        push bx
        push dx
        push ds
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 6]
        call far GETEXEPTRC             ; (pascal: takes its argument)
        push ax                         ; the hModule
        push word [bp + 6]
        call far _task_of_handle
        add sp, 2
        pop cx                          ; CX the hModule, AX the task (0: none)
        test ax, ax
        jz .mod
        mov bx, [bp + 6]
        or bx, 3
        mov dx, ax
        or dx, 3
        cmp bx, dx
        jne .task
        mov es, ax                      ; a task handle: AX = CX = 0, DX = ES = it
        mov dx, ax
        pop ds
        add sp, 2                       ; (DX not restored)
        pop bx
        xor ax, ax
        xor cx, cx
        pop bp
        retf 2
.task:  mov es, ax                      ; the task the handle belongs to
        jmp .done
.mod:   jcxz .done                      ; nothing: ES kept
        mov es, cx                      ; an hModule: itself
.done:  mov ax, cx
        pop ds
        pop dx
        pop bx
        pop bp
        retf 2

; ---------------------------------------------------------------------------
; The default signal handler (task.c, SetSigHandler): a RETF
global sig_default
sig_default:
        retf

; ---------------------------------------------------------------------------
; K327 (HandleParamError, Schulman): register-based; USER's and GDI's
; parameter validation far-calls it when a parameter is bad, with the
; error code in BX (observed from PROGMAN under w16: BX=6040h,
; ERR_BAD_HWND, called from USER's SendMessage layer), and continues
; after it returns. The retail KERNEL's LogParamError does nothing
; (Pietrek); w16 prints the first few (a debugging aid) and returns with
; every register kept. UNVERIFIED: what else the retail K327 does.
extern _k327_note               ; void __cdecl __far k327_note(u16 err, u16 ip, u16 cs)
global K327
K327:
        pushf
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        push bp
        mov bp, sp
        mov ax, DGROUP
        mov ds, ax
        push word [bp + 22]             ; caller's CS
        push word [bp + 20]             ; caller's IP
        push bx                         ; error code
        call far _k327_note
        add sp, 6
        pop bp
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        retf

; ---------------------------------------------------------------------------
; Catch (KERNEL.55) and Throw (KERNEL.56), as the SDK describes them:
; Catch(LPCATCHBUF) saves the execution state in the 9-word buffer and
; returns 0; Throw(LPCATCHBUF, n) returns from that Catch again, with n.
; The buffer's layout is w16's own (UNVERIFIED against Windows'): return
; IP, CS, the SP after Catch returns, SS, BP, SI, DI, DS.
global CATCH, THROW
CATCH:
        push bp
        mov bp, sp
        push es
        push bx
        les bx, [bp + 6]
        mov ax, [bp + 2]
        mov [es:bx], ax                 ; IP
        mov ax, [bp + 4]
        mov [es:bx + 2], ax             ; CS
        ; 3.1's order (TK62 "catchmap", all modes: CS in word 1, the
        ; caller's BP in 3, DS in 6, word 7 not written, SS in 8; w16 had
        ; SS in 3 and DS in 7: KERNEL audit, low)
        lea ax, [bp + 10]
        mov [es:bx + 4], ax             ; SP after the return
        mov ax, [bp]
        mov [es:bx + 6], ax             ; BP
        mov [es:bx + 8], si
        mov [es:bx + 10], di
        mov [es:bx + 12], ds
        mov [es:bx + 16], ss
        pop bx
        pop es
        pop bp
        xor ax, ax
        retf 4
THROW:
        mov bx, sp
        mov ax, [ss:bx + 4]             ; n
        mov es, [ss:bx + 8]
        mov bx, [ss:bx + 6]             ; the buffer
        mov si, [es:bx + 8]
        mov di, [es:bx + 10]
        mov ds, [es:bx + 12]
        mov bp, [es:bx + 6]
        mov ss, [es:bx + 16]
        mov sp, [es:bx + 4]
        push word [es:bx + 2]
        push word [es:bx]
        retf

; ---------------------------------------------------------------------------
; trace_entry: target of the import trace thunks (thunk.c), entered with
; the thunk's index pushed above the caller's far return address. Records
; the call (trace_note, which returns the real target) and jumps there
; with every register as the caller left it.
extern _trace_note
global trace_entry
trace_entry:
        sub sp, 2                       ; with the index: the real target
        push bp
        mov bp, sp                      ; [bp+4] index, [bp+6] return
        pushf
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        mov si, DGROUP
        mov ds, si
        push dx
        push cx
        push bx
        push ax
        push ss
        lea si, [bp + 6]
        push si                         ; the caller's frame
        push word [bp + 4]
        call far _trace_note
        add sp, 14
        mov [bp + 2], ax
        mov [bp + 4], dx
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        pop bp
        retf                            ; to the real target

; trace_ret: where a traced call returns with [w16] traceret=1 (thunk.c:
; trace_note put this in place of the caller's return address). Records
; DX:AX (trace_ret_note, which gives back the caller's own return address)
; and returns there with every register and flag as the callee left them.
extern _trace_ret_note
global trace_ret
trace_ret:
        push ax                         ; room for the caller's return
        push ax                         ; (PUSH keeps the flags)
        push bp
        mov bp, sp                      ; [bp+2] the return, [bp+6] the caller's SP
        pushf
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        mov si, DGROUP
        mov ds, si
        push dx
        push ax
        lea si, [bp + 6]
        push si
        push ss
        call far _trace_ret_note
        add sp, 8
        mov [bp + 2], ax
        mov [bp + 4], dx
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        pop bp
        retf

; GetDOSEnvironment (KERNEL.131): task.c's GDEnv, keeping BX and ES, as
; observed on Windows 3.1 (TK24). MOUSE.DRV uses ES afterwards as it was.
extern GDENV
global GETDOSENVIRONMENT
GETDOSENVIRONMENT:
        push bx
        push es
        call far GDENV
        pop es
        pop bx
        retf

; ---------------------------------------------------------------------------
; u16 __cdecl sig_call(u32 proc, u16 h, u16 code, u16 di, u16 ds, u16 q)
; Call a task signal procedure as KERNEL does (Schulman, SetTaskSignalProc:
; Pascal (hTaskOrModule, wCode, DI, hInstance, hQueue), BX = wCode).
global _sig_call
_sig_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        push word [bp + 10]             ; hTask or hModule
        push word [bp + 12]             ; code
        push word [bp + 14]             ; DI
        push word [bp + 16]             ; hInstance
        push word [bp + 18]             ; queue
        mov bx, [bp + 12]
        mov di, [bp + 14]
        call far [bp + 6]
        lea sp, [bp - 8]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
