; kentry.asm - KERNEL entry point and low-level glue (16-bit protected mode).
;
; Large model. All KERNEL code lives in one segment, _TEXT (C is compiled
; with -nt=_TEXT). DGROUP = _DATA CONST CONST2 _BSS.
;
; Contents:
;   kernel_entry     NE entry point, called by the bootstrap
;   int21_handler    protected-mode INT 21h hook (task termination)
;   int3d_handler    protected-mode INT 3Dh: FWAIT (floating-point fixups)
;   _task_run        start a task on its own stack; returns its exit code
;   unimpl_entry     target of the "unimplemented import" thunks
;   INITTASK         register-based API wrapper around inittask_c
;   DOS3CALL         KERNEL.102
;   LOCAL*           Local heap APIs: pass the caller's DS to local.c
;   _dos21, _dpmi31  generic interrupt helpers for C
;   _seg_move        move a segment's contents and base, even if it is SS
;   KCONST segment   placeholders for KERNEL's constant exports

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

%include "intcall.inc"

extern _kmain                   ; void __cdecl __far kmain(bootinfo __far *)
extern _unimpl_report           ; void __cdecl __far unimpl_report(u16 index)
extern _inittask_c              ; void __cdecl __far inittask_c(regs16 __far *)
extern kexit_to_dos_            ; void __far kexit_to_dos(u8 code), AX = code

segment _DATA
global _old21, _task_active
_old21          dd 0            ; previous protected-mode INT 21h vector
_task_active    dw 0            ; nonzero while a task runs under _task_run
saved_ss        dw 0
saved_sp        dw 0
t_cs            dw 0
t_ip            dw 0
t_ds            dw 0
t_ss            dw 0
t_sp            dw 0
t_bx            dw 0
t_cx            dw 0
t_di            dw 0
t_si            dw 0
t_es            dw 0

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

global int21_handler
int21_handler:
        cmp ah, 4Ch
        jne .chain
        push ds
        push bx
        mov bx, DGROUP
        mov ds, bx
        cmp word [_task_active wrt DGROUP], 0
        pop bx
        pop ds
        jne task_exit_al
.chain:
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

; ---------------------------------------------------------------------------
; u16 __cdecl __far task_run(u16 cs, u16 ip, u16 ds, u16 ss, u16 sp,
;                            u16 bx, u16 cx, u16 di, u16 si, u16 es)
; Enter a task's entry point with Windows' task-start register set:
;   AX=0, BX=stack size, CX=heap size, DX=0, DI=hInstance, SI=hPrevInstance,
;   ES=PSP, DS=DGROUP, SS:SP=initial stack, BP=0.
; UNVERIFIED: AX/DX/BP values at task entry.
; Returns the exit code when the task terminates with INT 21h AH=4Ch.
global _task_run
_task_run:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov [saved_ss wrt DGROUP], ss
        mov [saved_sp wrt DGROUP], sp
        mov ax, [bp+6]
        mov [t_cs wrt DGROUP], ax
        mov ax, [bp+8]
        mov [t_ip wrt DGROUP], ax
        mov ax, [bp+10]
        mov [t_ds wrt DGROUP], ax
        mov ax, [bp+12]
        mov [t_ss wrt DGROUP], ax
        mov ax, [bp+14]
        mov [t_sp wrt DGROUP], ax
        mov ax, [bp+16]
        mov [t_bx wrt DGROUP], ax
        mov ax, [bp+18]
        mov [t_cx wrt DGROUP], ax
        mov ax, [bp+20]
        mov [t_di wrt DGROUP], ax
        mov ax, [bp+22]
        mov [t_si wrt DGROUP], ax
        mov ax, [bp+24]
        mov [t_es wrt DGROUP], ax
        mov word [_task_active wrt DGROUP], 1
        mov ss, [t_ss wrt DGROUP]
        mov sp, [t_sp wrt DGROUP]
        push word [t_cs wrt DGROUP]
        push word [t_ip wrt DGROUP]
        mov bx, [t_bx wrt DGROUP]
        mov cx, [t_cx wrt DGROUP]
        mov di, [t_di wrt DGROUP]
        mov si, [t_si wrt DGROUP]
        mov es, [t_es wrt DGROUP]
        xor ax, ax
        xor dx, dx
        xor bp, bp
        mov ds, [t_ds wrt DGROUP]
        retf

; AL = exit code. Abandons the task's stack and returns from _task_run.
task_exit_al:
        mov bx, DGROUP
        mov ds, bx
        mov word [_task_active wrt DGROUP], 0
        mov ss, [saved_ss wrt DGROUP]
        mov sp, [saved_sp wrt DGROUP]
        sti
        xor ah, ah
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; ---------------------------------------------------------------------------
; Unimplemented-import thunks do:  mov ax, <index> / jmp far unimpl_entry
global unimpl_entry
unimpl_entry:
        mov bx, DGROUP
        mov ds, bx
        push ax
        call far _unimpl_report
        add sp, 2
        cmp word [_task_active wrt DGROUP], 0
        jne .task
        mov ax, 0FFh                    ; no task yet (a DLL's LibMain)
        call far kexit_to_dos_          ; Watcom register convention
.task:
        mov al, 0FFh
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
        retf

; ---------------------------------------------------------------------------
; The Local* APIs work on the local heap in the caller's DS. KAPI functions
; load KERNEL's DS, so these wrappers call the C function (far pascal) with
; the caller's DS as an extra first argument, followed by copies of the
; caller's NARGS word arguments, and restore DS on return.
;   LOCALAPI exported-name, c-function, nargs
%macro LOCALAPI 3
extern %2
global %1
%1:
        push bp
        mov bp, sp
        push ds
        push ds
%assign i %3
%rep %3
        push word [bp + 4 + 2 * i]
%assign i i - 1
%endrep
        call far %2
        pop ds
        pop bp
        retf 2 * %3
%endmacro

LOCALAPI LOCALINIT,    LINIT,    3      ; KERNEL.4
LOCALAPI LOCALALLOC,   LALLOC,   2      ; KERNEL.5
LOCALAPI LOCALREALLOC, LREALLOC, 3      ; KERNEL.6
LOCALAPI LOCALFREE,    LFREE,    1      ; KERNEL.7
LOCALAPI LOCALLOCK,    LLOCK,    1      ; KERNEL.8
LOCALAPI LOCALUNLOCK,  LUNLOCK,  1      ; KERNEL.9
LOCALAPI LOCALSIZE,    LSIZE,    1      ; KERNEL.10
LOCALAPI LOCALHANDLE,  LHANDLE,  1      ; KERNEL.11
LOCALAPI LOCALFLAGS,   LFLAGS,   1      ; KERNEL.12
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

; DOS3Call (KERNEL.102): issue INT 21h with the caller's registers.
global DOS3CALL
DOS3CALL:
        int 21h
        retf

; ---------------------------------------------------------------------------
; void __cdecl seg_move(u16 sel, u16 tmp, u16 nwords, u32 newbase)
; Copy nwords words from sel:0 to tmp:0, then set sel's base to newbase
; (DPMI 0007h) and reload SS. sel may be the current stack segment: after
; the copy nothing is written to the stack until SS describes the new
; memory (only reads, and INT 31h's own frame, which is popped through the
; descriptor SS still caches), so the stack the caller sees afterwards is
; exactly the one it had. The caller frees the old memory afterwards.
global _seg_move
_seg_move:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ds, [bp + 6]
        mov es, [bp + 8]
        mov cx, [bp + 10]
        xor si, si
        xor di, di
        cld
        rep movsw
        mov bx, [bp + 6]
        mov dx, [bp + 12]
        mov cx, [bp + 14]
        mov ax, 0007h
        int 31h
        mov ax, ss
        mov ss, ax
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

INTCALL _dos21, 21h, far
INTCALL _dpmi31, 31h, far

; ---------------------------------------------------------------------------
; Constant exports. wlink cannot export absolute symbols, so these labels
; live in their own segment, which keeps them in their own entry-table
; bundles. KERNEL resolves these ordinals to their runtime values (see
; kconst_lookup in module.c); the offsets here are placeholders.
segment KCONST public align=16 class=KCONST use16
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
