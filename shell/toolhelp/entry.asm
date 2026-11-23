; entry.asm - w16's TOOLHELP.DLL: the entry point and WEP, and the parts
; that run with registers rather than arguments (ARCHITECTURE.md 22):
;   th_hook       the procedure KERNEL's ToolHelpHook (KERNEL.341) calls
;                 with a notification in the registers
;   exc_*, vec_*  the processor exceptions (DPMI 0203h) and interrupts 1
;                 and 3 (DPMI 0205h) InterruptRegister's callbacks get
;   dispatch      runs the callbacks on the faulting task's stack
;   sw_entry      where a task switched to by TaskSwitch starts
;   pit_count     the 8253's channel 0 count (TimerCount)
; Facts used: the callback frame and its return conventions, Windows 3.1
; SDK Programmer's Reference Vol. 2 (InterruptRegister); the DPMI 0.9
; exception frame, the DPMI specification; KERNEL's notification
; registers, ARCHITECTURE.md 9.7 (TK51); everything else as observed on
; Windows 3.1 with tests/toolhelp/thp.c.

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16    ; code first: segment 1, as 3.1's
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

extern LOCALINIT                        ; KERNEL.4
extern GETATOMNAME                      ; KERNEL.72
extern _th_notify                       ; WORD __cdecl __far th_notify(WORD FAR *regs)
extern _th_fault_wanted                 ; WORD __cdecl __far th_fault_wanted(void)
extern _th_sw_done                      ; void __cdecl __far th_sw_done(void)
extern _th_init                         ; int __cdecl __far th_init(void)

segment _DATA
global _old_hook, _f_num, _f_ip, _f_cs, _f_fl, _f_handle, _f_cb, _f_cbax
global _f_pass_cs, _f_pass_ip, _old_exc, _old_vec1, _old_vec3, _sw_csip
_old_hook       dd 0                    ; the hook before ours (chained to)
; the fault being dispatched (one at a time: Windows does not switch
; tasks while a fault's callbacks start)
_f_num          dw 0
_f_ip           dw 0
_f_cs           dw 0
_f_fl           dw 0
_f_handle       dw 0
_f_ax           dw 0
_f_ds           dw 0
_f_cb           dd 0                    ; the callback to call next
_f_cbax         dw 0                    ; AX for it
_f_pass_cs      dw 0                    ; a fault passed on: let it through
_f_pass_ip      dw 0                    ; once (it comes back at once)
_old_exc        times 16 dd 0           ; the handlers before ours, by number
_old_vec1       dd 0
_old_vec3       dd 0
_sw_csip        dd 0                    ; TaskSwitch's address

segment _TEXT

; KERNEL calls a library's entry point once, with DS = its data segment,
; DI = its instance handle, CX = the local heap size from the NE header
global th_libentry
th_libentry:
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
        call far _th_init
        pop es
        pop di
        pop si
        pop ds
        retf

; ---- the notification hook ----
; Called by KERNEL with AX = the notification, BX, CX, DX, SI, DI, ES as
; it describes it, DS = KERNEL's. th_notify gets them as an array
; (ax bx cx dx si di es ds) and returns AX; the hook before ours is
; called after, with the same registers.
global _th_hook
_th_hook:
        push bp
        mov bp, sp
        push ds
        push es
        push di
        push si
        push dx
        push cx
        push bx
        push ax                         ; [bp-16] .. [bp-2]: AX BX CX DX SI DI ES DS
        sub sp, 2                       ; [bp-18]: the result
        mov ax, DGROUP
        mov ds, ax
        lea ax, [bp-16]
        push ss
        push ax
        call far _th_notify
        add sp, 4
        mov [bp-18], ax
        mov ax, [_old_hook+2 wrt DGROUP]
        or ax, ax
        jz .done
        push cs                         ; the hook before ours, called with
        push word .done                 ; the same registers
        push word [_old_hook+2 wrt DGROUP]
        push word [_old_hook wrt DGROUP]
        mov ax, [bp-16]
        mov bx, [bp-14]
        mov cx, [bp-12]
        mov dx, [bp-10]
        mov si, [bp-8]
        mov di, [bp-6]
        mov es, [bp-4]
        mov ds, [bp-2]
        retf
.done:  mov ax, [bp-18]
        lea sp, [bp-14]                 ; past the result and AX's slot
        pop bx
        pop cx
        pop dx
        pop si
        pop di
        pop es
        pop ds
        pop bp
        retf

; ---- processor exceptions (DPMI 0203h) ----
; DPMI's frame: [bp+2] return IP CS (to the host), error code, IP, CS,
; flags, SP, SS. A fault some callback may want goes on in dispatch, on
; the faulting task's own stack; else to the handler before ours.
%macro EXC 1
global _exc_%1
_exc_%1:
        push word %1
        jmp exc_common
%endmacro
        EXC 0
        EXC 6
        EXC 12
        EXC 13
        EXC 14

exc_common:
        push bp
        mov bp, sp                      ; [bp+2] number, [bp+4] DPMI's frame
        push ds
        push ax
        push bx
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp+4+8]                ; CS
        cmp ax, [_f_pass_cs wrt DGROUP]
        jne .new
        mov ax, [bp+4+6]
        cmp ax, [_f_pass_ip wrt DGROUP]
        jne .new
        mov word [_f_pass_cs wrt DGROUP], 0
        jmp .chain
.new:
        mov ax, [bp+2]
        mov [_f_num wrt DGROUP], ax
        mov ax, [bp+4+6]
        mov [_f_ip wrt DGROUP], ax
        mov ax, [bp+4+8]
        mov [_f_cs wrt DGROUP], ax
        mov ax, [bp+4+10]
        mov [_f_fl wrt DGROUP], ax
        push es
        push cx
        push dx
        push si
        push di
        call far _th_fault_wanted       ; 0: not one to report
        pop di
        pop si
        pop dx
        pop cx
        pop es
        or ax, ax
        jz .chain
        mov word [bp+4+6], dispatch     ; the task goes on in dispatch
        mov [bp+4+8], cs
        and word [bp+4+10], 0FEFFh      ; (not single-stepped there)
        pop bx
        pop ax
        pop ds
        pop bp
        add sp, 2
        retf
.chain:                                 ; to the handler before ours
        pop bx
        pop ax
        pop ds
        pop bp                          ; stack: number, DPMI's frame
        push ax
        push bx
        push ds
        push bp
        mov bp, sp                      ; [bp+2] DS [bp+4] BX [bp+6] AX [bp+8] number
        mov ax, DGROUP
        mov ds, ax
        mov bx, [bp+8]
        shl bx, 2
        mov ax, [_old_exc+bx+2 wrt DGROUP]
        mov [bp+8], ax                  ; its CS over the number
        mov ax, [_old_exc+bx wrt DGROUP]
        xchg ax, [bp+6]                 ; its IP over AX's slot
        pop bp
        pop ds
        pop bx
        retf                            ; stack: its IP CS, DPMI's frame

; ---- interrupts 1 and 3 (protected-mode vectors, DPMI 0205h) ----
; The frame is the interrupt's: IP CS FLAGS, IP after the instruction.
global _vec_1, _vec_3
_vec_1:
        push word 1
        jmp vec_common
_vec_3:
        push word 3
vec_common:
        push bp
        mov bp, sp                      ; [bp+2] number, [bp+4] IP CS FLAGS
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp+2]
        mov [_f_num wrt DGROUP], ax
        mov ax, [bp+4]
        mov [_f_ip wrt DGROUP], ax
        mov ax, [bp+6]
        mov [_f_cs wrt DGROUP], ax
        mov ax, [bp+8]
        mov [_f_fl wrt DGROUP], ax
        push es
        push bx
        push cx
        push dx
        push si
        push di
        call far _th_fault_wanted
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop es
        or ax, ax
        jz .chain
        pop ax
        pop ds
        pop bp
        add sp, 8                       ; number, IP CS FLAGS: dispatch pushes its own
        jmp dispatch
.chain:
        mov ax, [bp+2]
        pop ax
        pop ds
        pop bp
        add sp, 2                       ; the number: the frame is intact
        jmp pass_trap

; pass a trap on to the vector before ours: [sp] IP CS FLAGS, number in
; f_num; every register as it came
pass_trap:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        cmp word [_f_num wrt DGROUP], 1
        jne .v3
        push word [_old_vec1+2 wrt DGROUP]
        push word [_old_vec1 wrt DGROUP]
        jmp .go
.v3:    push word [_old_vec3+2 wrt DGROUP]
        push word [_old_vec3 wrt DGROUP]
.go:    push bp
        mov bp, sp                      ; [bp+2] IP [bp+4] CS, [bp+6] AX [bp+8] DS
        mov ax, [bp+2]
        xchg ax, [bp+6]
        mov [bp+2], ax
        mov ax, [bp+4]
        xchg ax, [bp+8]
        mov [bp+4], ax
        pop bp
        pop ax
        pop ds
        retf

; ---- the callbacks, on the faulting task's stack ----
; Every register as at the fault (CS:IP excepted). The frame they get:
; [SP] the return into TOOLHELP, AX, the number, a handle, the fault's
; IP CS FLAGS. A callback returns (RETF) to pass the fault on to the next
; one; one that handles it removes the first 10 bytes and IRETs, or ends
; the task (TerminateApp).
extern _th_next_cb                      ; WORD __cdecl __far th_next_cb(void): 0 none left
dispatch:
        sub sp, 12                      ; the frame: AX number handle IP CS FLAGS
        push bp
        mov bp, sp
        push ds
        mov [bp+2], ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_f_num wrt DGROUP]
        mov [bp+4], ax
        mov ax, [_f_handle wrt DGROUP]
        mov [bp+6], ax
        mov ax, [_f_ip wrt DGROUP]
        mov [bp+8], ax
        mov ax, [_f_cs wrt DGROUP]
        mov [bp+10], ax
        mov ax, [_f_fl wrt DGROUP]
        mov [bp+12], ax
        mov ax, [bp+2]
        pop ds
        pop bp
.next:
        sub sp, 4                       ; the return into here, later
        push ds
        push ax
        push bp
        mov bp, sp                      ; [bp+2] AX [bp+4] DS [bp+6] return
        mov ax, DGROUP
        mov ds, ax
        push bx
        push cx
        push dx
        push es
        call far _th_next_cb
        pop es
        pop dx
        pop cx
        pop bx
        or ax, ax
        jz .none
        mov word [bp+6], .back
        mov [bp+8], cs
        push word [bp+4]                ; the fault's DS
        mov ax, [_f_cb wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_f_cb+2 wrt DGROUP]
        mov [bp+4], ax
        mov ax, [_f_cbax wrt DGROUP]
        pop ds
        pop bp
        retf                            ; into the callback
.back:
        jmp .next
.none:                                  ; none took it: pass it on
        pop bp
        add sp, 2                       ; (AX: the frame has the fault's)
        pop ds
        add sp, 4
        pop ax
        add sp, 4                       ; number, handle: IP CS FLAGS left
        cmp word [cs:.dummy], 0         ; (keeps NASM's local labels apart)
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_f_num wrt DGROUP]
        cmp ax, 1
        je .trap
        cmp ax, 3
        je .trap
        mov ax, [_f_cs wrt DGROUP]      ; it faults again at once: let it by
        mov [_f_pass_cs wrt DGROUP], ax
        mov ax, [_f_ip wrt DGROUP]
        mov [_f_pass_ip wrt DGROUP], ax
        pop ax
        pop ds
        iret
.trap:  pop ax
        pop ds
        jmp pass_trap
.dummy  dw 0

; ---- TaskSwitch: the task it switched to starts here, on its own stack
; where the scheduler would have returned; it goes on at sw_csip as if
; called there (TaskSwitch's address), its event count as it was before
; TaskSwitch added one ----
global _sw_entry
_sw_entry:
        sub sp, 4
        push bp
        mov bp, sp
        push ds
        push ax
        push bx
        push cx
        push dx
        push es
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_sw_csip wrt DGROUP]
        mov [bp+2], ax
        mov ax, [_sw_csip+2 wrt DGROUP]
        mov [bp+4], ax
        call far _th_sw_done
        pop es
        pop dx
        pop cx
        pop bx
        pop ax
        pop ds
        pop bp
        retf

; WORD pit_count(void): the 8253's channel 0, counting down
global _pit_count
_pit_count:
        pushf
        cli
        mov al, 0
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        ; IF back explicitly (386 Enhanced mode: POPF leaves it alone
        ; below IOPL, ARCHITECTURE.md 12.14)
        push bp
        mov bp, sp
        test byte [bp + 3], 2
        pop bp
        jz .n
        sti
.n:     popf
        retf

; WORD atom_in(WORD ds, WORD atom, LPSTR buf, WORD n): GetAtomName with
; DS = ds (the atom table of another module's data segment)
global _atom_in
_atom_in:
        push bp
        mov bp, sp
        push ds
        push word [bp+8]                ; atom
        push word [bp+12]               ; buf
        push word [bp+10]
        push word [bp+14]               ; n
        mov ds, [bp+6]
        call far GETATOMNAME
        pop ds
        pop bp
        retf

; WEP (ordinal 1, resident)
global WEP
WEP:
        mov ax, 1
        retf 2

; DWORD vtd_time(DWORD entry, WORD fn): the VTD's protected-mode API (INT
; 2Fh 1684h, BX = 5) called with AX = fn (0101h: milliseconds since
; Windows started, 0102h: this virtual machine's; Schulman ch. 10): EAX
; in DX:AX; FFFFFFFFh when it fails (carry). 386 Enhanced mode only.
        cpu 386
global _vtd_time
_vtd_time:
        push bp
        mov bp, sp
        push si
        push di
        push cs
        push word .back
        push word [bp+8]
        push word [bp+6]
        mov ax, [bp+10]
        retf
.back:  jc .fail
        mov edx, eax
        shr edx, 16
        jmp .done
.fail:  mov ax, 0FFFFh
        mov dx, ax
.done:  pop di
        pop si
        pop bp
        retf
        cpu 286
