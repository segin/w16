; vxlog.asm - [w16] vxdlog=1, a debugging aid for 386 enhanced mode: the
; protected-mode INT 2Fh AX=1684h (get a VxD's API entry point) is hooked
; and each entry it hands out is replaced by a KERNEL thunk that writes
; every call through it to the debuglog (vx_note, thunk.c): the device,
; the caller, the 32-bit registers before and after, the flags. The call
; itself gets the caller's registers unchanged and returns the VxD's.
; Up to 8 devices.

        bits 16
        cpu 386

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _DATA CONST CONST2 _BSS

extern _vx_note         ; void __cdecl __far vx_note(u16 slot4, u16 phase,
                        ;     u16 far *regs, u16 ip, u16 cs)

segment _DATA
global _vx_old2f, _vx_real, _vx_id
_vx_old2f:  dd 0
_vx_real:   times 8 dd 0
_vx_id:     times 8 dw 0
_vx_busy:   dw 0

segment _TEXT

; the protected-mode INT 2Fh handler (DPMI 0205h, vx_hook in thunk.c)
global _vx_i2f
_vx_i2f:
        cmp ax, 1684h
        je .api
        sub sp, 4                       ; to the old handler, IRET frame intact
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_vx_old2f]
        mov [bp+2], ax
        mov ax, [_vx_old2f+2]
        mov [bp+4], ax
        pop ax
        pop ds
        pop bp
        retf
.api:
        pushf                           ; an IRET frame back to .back
        push cs
        push word .back
        sub sp, 4
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_vx_old2f]
        mov [bp+2], ax
        mov ax, [_vx_old2f+2]
        mov [bp+4], ax
        pop ax
        pop ds
        pop bp
        retf
.back:                                  ; ES:DI the entry (0:0: none)
        push ds
        push ax
        push si
        mov ax, DGROUP
        mov ds, ax
        mov ax, es
        or ax, di
        jz .done
        xor si, si
.find:  cmp word [_vx_id+si], bx        ; the device's slot, or a free one
        je .slot
        cmp word [_vx_id+si], 0
        je .slot
        add si, 2
        cmp si, 16
        jb .find
        jmp .done                       ; (all taken: not traced)
.slot:  mov [_vx_id+si], bx
        shl si, 1
        mov [_vx_real+si], di
        mov [_vx_real+si+2], es
        mov di, si
        shl di, 1                       ; 8 bytes per thunk
        add di, vx_thunks
        push cs
        pop es
.done:  pop si
        pop ax
        pop ds
        retf 2                          ; the flags as the handler left them

; the thunks: the slot (times 4) and the common part
vx_thunks:
%assign i 0
%rep 8
        push strict word i * 4
        jmp strict near vx_common
        nop
        nop
%assign i i + 1
%endrep

vx_common:
        push bp
        mov bp, sp                      ; [bp+2] slot*4, [bp+4] IP, [bp+6] CS
        push word 0
        call vx_log                     ; the call as it is made
        add sp, 2
        push cs                         ; the VxD's entry, returning to .after
        push word .after
        sub sp, 4
        push bp
        push ds
        push si
        mov si, DGROUP
        mov ds, si
        mov si, [bp+2]
        push word [_vx_real+si]
        pop word [bp-8]
        push word [_vx_real+si+2]
        pop word [bp-6]
        pop si
        pop ds
        pop bp
        mov bp, [bp]                    ; the caller's BP for the VxD
        retf
.after:                                 ; [sp] the caller's BP, then slot, IP, CS
        push bp
        mov bp, sp
        push word 1
        call vx_log2                    ; what came back
        add sp, 2
        pushf                           ; [bp-2]
        push word [bp-2]
        pop word [bp+4]                 ; the flags where the slot was
        push word [bp]
        pop word [bp+2]                 ; the returned BP where the caller's was
        add sp, 4                       ; SP = bp + 2
        pop bp
        popf
        retf

; vx_log: [bp+2] slot*4, [bp+4] IP, [bp+6] CS; the phase on the stack
; (near call: [sp+2]). Keeps every register and the flags.
vx_log:
        pushf
        pushad
        push ds
        push es
        mov si, sp                      ; SS:SI: ES, DS, EDI ... EAX, FLAGS
        push word [bp+6]
        push word [bp+4]
        push ss
        push si
        push word [ss:si+4+32+2+2]      ; the phase
        push word [bp+2]
        jmp vx_log_common
vx_log2:                                ; the same with [bp+4] slot, IP, CS
        pushf
        pushad
        push ds
        push es
        mov si, sp
        push word [bp+8]
        push word [bp+6]
        push ss
        push si
        push word [ss:si+4+32+2+2]
        push word [bp+4]
vx_log_common:
        mov ax, DGROUP
        mov ds, ax
        cmp word [_vx_busy], 0
        jne .no
        inc word [_vx_busy]
        cld
        call far _vx_note
        dec word [_vx_busy]
.no:    add sp, 12
        pop es
        pop ds
        popad
        popf
        ret
