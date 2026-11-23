; sb16a.asm - w16's SB16.DRV, the assembly part (drivers/sb16/README.md,
; ARCHITECTURE.md 24):
;   libentry         the library's entry point
;   WEP              nothing to undo
;   sb_isr           the card's interrupt (protected mode)
;   sb_hook/unhook   the interrupt vector and the PIC's mask
;   sb_cli/sb_sti    interrupts off and back as they were
;   sb_hcopy         a copy to or from a huge buffer (__AHINCR)
;   sb_ahincr        KERNEL's selector increment
;
; Sources: Windows 3.1 SDK Programmer's Reference Vol. 1 (DLL entry,
; WEP), the Multimedia Programmer's Guide (callbacks at interrupt time);
; StackEnter/StackLeave as ARCHITECTURE.md 23.2 records them (observed:
; SNDBLST2.DRV's interrupt-time callbacks run on MMSYSTEM's interrupt
; stack, its task-time ones on the caller's); Creative's Sound Blaster
; Series Hardware Programming Guide for the card (mixer register 82h, the
; interrupt status: bit 0 the 8-bit DMA / SB-MIDI interrupt, acknowledged
; by reading base+0Eh; bit 1 the 16-bit DMA interrupt, base+0Fh; bit 2 the
; MPU-401's); the 8259's data sheet (mask registers 21h/A1h, end of
; interrupt 20h to 20h/A0h). INT 21h AH=25h/35h in protected mode set and
; read the protected-mode vector (DOS extender, as w16's other drivers
; hook theirs: drivers/timer/timer.asm).
        bits 16
        cpu 286

segment SB_TEXT public align=16 class=CODE use16
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

extern LOCALINIT                        ; KERNEL.4
extern __AHINCR                         ; KERNEL.114
extern STACKENTER                       ; MMSYSTEM.32
extern STACKLEAVE                       ; MMSYSTEM.33
extern _sb_work                         ; void __cdecl __far sb_work(void)
extern _sb_port                         ; WORD (sb16.c)
extern _sb_mpu                          ; WORD (sb16.c)
extern _mpu_ok                          ; BYTE (sb16.c)

segment _DATA
global _sb_hinst, _ev8, _ev16, _mpuq, _mpuqh, _mpuqt
_sb_hinst       dw 0
_ev8            db 0                    ; 8-bit DMA interrupts not yet handled
_ev16           db 0                    ; 16-bit ones
_mpuqh          db 0                    ; MPU-401 input: head (sb_work takes)
_mpuqt          db 0                    ; and tail (the handler puts)
_mpuq           times 64 db 0
busy            db 0                    ; sb_work running
hooked          db 0
irqno           db 0                    ; the IRQ (2 taken as 9)
vecno           db 0                    ; its vector
maskbit         db 0                    ; the PIC's mask bit as it was
                db 0
oldvec          dd 0
vsbd_entry      dd 0                    ; VSBD.386's API (sb_vxd_entry)

segment SB_TEXT

; popfi: the flags pushed before a cli back, interrupts enabled again
; explicitly when they were (in 386 Enhanced mode POPF does not change the
; interrupt flag: Windows' code runs below IOPL)
%macro popfi 0
        push bp
        mov bp, sp
        test byte [bp + 3], 2
        pop bp
        jz %%n
        sti
%%n:    popf
%endmacro

; ---- the library's start: DI = the instance, CX = the local heap's size
global libentry
libentry:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov [_sb_hinst], di
        jcxz .noheap
        push ds
        push word 0
        push cx
        call far LOCALINIT
.noheap:
        mov ax, 1
        pop ds
        retf

global WEP
WEP:
        mov ax, 1
        retf 2

; DWORD __cdecl __far sb_vxd_entry(void): VSBD.386's API entry (INT 2Fh
; AX=1684h BX=0445h, the DDK's Get Device Entry Point Address), 0 if there
; is none
global _sb_vxd_entry
_sb_vxd_entry:
        push ds
        push es
        push di
        mov ax, DGROUP
        mov ds, ax
        xor di, di
        mov es, di
        mov ax, 1684h
        mov bx, 0445h
        int 2Fh
        mov [vsbd_entry], di
        mov [vsbd_entry + 2], es
        mov ax, di
        mov dx, es
        pop di
        pop es
        pop ds
        retf

; DWORD __cdecl __far sb_vxd(WORD fn, WORD ax): VSBD.386's API, DX the
; function, AX the argument (the port); AX back in the low word, 1 in the
; high word if the carry came back set (Windows 3.1's SNDBLST2.DRV calls
; Windows 3.1's VSBD.386 so: win386/vxd/vsbd.c)
global _sb_vxd
_sb_vxd:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        mov dx, [bp + 6]
        mov ax, [bp + 8]
        call far [vsbd_entry]
        mov dx, 0
        adc dx, 0
        pop di
        pop si
        pop ds
        pop bp
        retf

; WORD __cdecl __far sb_ahincr(void)
global _sb_ahincr
_sb_ahincr:
        mov ax, __AHINCR
        retf

; WORD __cdecl __far sb_cli(void): the flags, then interrupts off
global _sb_cli
_sb_cli:
        pushf
        pop ax
        cli
        retf

; void __cdecl __far sb_sti(WORD flags): interrupts on when IF was set
global _sb_sti
_sb_sti:
        push bp
        mov bp, sp
        test byte [bp + 7], 2
        jz .n
        sti
.n:     pop bp
        retf

; void __cdecl __far sb_hcopy(LPSTR buf, LPSTR huge, DWORD off, WORD n, WORD dir)
; dir 0: buf <- huge + off; dir 1: huge + off <- buf. The huge side may
; cross 64 KB boundaries (selectors __AHINCR apart); buf may not.
; [bp+6] buf, [bp+10] huge, [bp+14] off, [bp+18] n, [bp+20] dir
global _sb_hcopy
_sb_hcopy:
        push bp
        mov bp, sp
        push ds
        push es
        push si
        push di
        cld
        mov si, [bp + 10]               ; the huge address: offset
        add si, [bp + 14]
        mov ax, 0
        adc ax, [bp + 16]               ; 64 KB units above its selector
        mov cx, __AHINCR
        mul cx
        add ax, [bp + 12]
        mov dx, ax                      ; DX:SI the huge side
        mov di, [bp + 6]                ; the plain side's offset
        mov bx, [bp + 18]               ; bytes left
.chunk: or bx, bx
        jz .done
        mov cx, si                      ; to the end of this 64 KB
        neg cx                          ; (0: a whole segment)
        jz .all
        cmp bx, cx
        jae .some
.all:   mov cx, bx
.some:  sub bx, cx
        cmp word [bp + 20], 0
        jne .out
        mov ds, dx                      ; buf <- huge
        mov es, [bp + 8]
        rep movsb
        jmp .next
.out:   push si                         ; huge <- buf
        push di
        xchg si, di
        mov es, dx
        mov ds, [bp + 8]
        rep movsb
        mov ax, di                      ; the huge offset after
        mov cx, si                      ; and buf's
        pop di
        pop si
        mov si, ax
        mov di, cx
.next:  or si, si                       ; crossed into the next 64 KB
        jnz .chunk
        mov ax, __AHINCR
        add dx, ax
        jmp .chunk
.done:  pop di
        pop si
        pop es
        pop ds
        pop bp
        retf

; int __cdecl __far sb_hook(WORD irq): the IRQ's protected-mode vector
; ours and the IRQ unmasked (1); 0 when an IRQ is hooked already
global _sb_hook
_sb_hook:
        push bp
        mov bp, sp
        push ds
        push es
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        xor ax, ax
        cmp byte [hooked], 0
        jne .r
        mov bx, [bp + 6]
        and bx, 0Fh
        cmp bl, 2                       ; IRQ 2 is the slave's 9
        jne .n2
        mov bl, 9
.n2:    mov [irqno], bl
        mov al, bl
        add al, 8                       ; master: 08h-0Fh
        cmp bl, 8
        jb .v
        add al, 70h - 16                ; slave: 70h-77h
.v:     mov [vecno], al
        mov ah, 35h
        int 21h
        mov [oldvec], bx
        mov [oldvec + 2], es
        mov al, [vecno]
        mov ah, 25h
        push ds
        push cs
        pop ds
        mov dx, sb_isr
        int 21h
        pop ds
        pushf
        cli
        mov cl, [irqno]
        cmp cl, 8
        jae .slave
        mov ah, 1
        shl ah, cl
        in al, 21h
        mov [maskbit], al
        and [maskbit], ah
        not ah
        and al, ah
        out 21h, al
        jmp .m
.slave: sub cl, 8
        mov ah, 1
        shl ah, cl
        in al, 0A1h
        mov [maskbit], al
        and [maskbit], ah
        not ah
        and al, ah
        out 0A1h, al
        in al, 21h                      ; the cascade (IRQ 2) open
        and al, 0FBh
        out 21h, al
.m:     popfi
        mov byte [hooked], 1
        mov ax, 1
.r:     pop di
        pop si
        pop es
        pop ds
        pop bp
        retf

; void __cdecl __far sb_unhook(void): the mask bit and the vector as they were
global _sb_unhook
_sb_unhook:
        push ds
        mov ax, DGROUP
        mov ds, ax
        cmp byte [hooked], 0
        je .r
        pushf
        cli
        mov cl, [irqno]
        cmp cl, 8
        jae .slave
        in al, 21h
        or al, [maskbit]
        out 21h, al
        jmp .m
.slave: in al, 0A1h
        or al, [maskbit]
        out 0A1h, al
.m:     popfi
        mov al, [vecno]
        mov ah, 25h
        push ds
        lds dx, [oldvec]
        int 21h
        pop ds
        mov byte [hooked], 0
.r:     pop ds
        retf

; ---- the card's interrupt, protected mode. The card is acknowledged
; first (mixer register 82h says which of its parts interrupted), the
; MPU-401's input bytes taken into _mpuq, the end of interrupt sent; then,
; with interrupts enabled and unless it is running already, sb_work (C)
; does the rest on MMSYSTEM's interrupt stack (StackEnter), where the
; callbacks are made.
sb_isr:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        cld
        mov dx, [_sb_port]
        add dx, 4
        mov al, 82h
        out dx, al
        inc dx
        in al, dx                       ; the interrupt status
        mov bl, al
        test bl, 1
        jz .n8
        mov dx, [_sb_port]
        add dx, 0Eh
        in al, dx
        inc byte [_ev8]
.n8:    test bl, 2
        jz .n16
        mov dx, [_sb_port]
        add dx, 0Fh
        in al, dx
        inc byte [_ev16]
.n16:   cmp byte [_mpu_ok], 0           ; the MPU-401's input (its bit 2,
        je .eoi                         ; or other bytes waiting)
        mov dx, [_sb_mpu]
        inc dx
        mov cx, 16
.mpu:   in al, dx                       ; status: bit 7 clear = data
        test al, 80h
        jnz .eoi
        dec dx
        in al, dx
        inc dx
        mov bl, [_mpuqt]
        mov bh, bl
        inc bh
        and bh, 63
        cmp bh, [_mpuqh]
        je .full                        ; full: the byte is lost
        xor bh, bh
        mov [_mpuq + bx], al
        inc bl
        and bl, 63
        mov [_mpuqt], bl
.full:  loop .mpu
.eoi:   mov al, 20h
        cmp byte [irqno], 8
        jb .m
        out 0A0h, al
.m:     out 20h, al
        cmp byte [busy], 0
        jne .out
        mov byte [busy], 1
.again: sti
        call far STACKENTER
        call far _sb_work
        call far STACKLEAVE
        cli
        mov ax, DGROUP
        mov ds, ax
        cmp byte [_ev8], 0              ; more came at the very end
        jne .again
        cmp byte [_ev16], 0
        jne .again
        mov al, [_mpuqh]
        cmp al, [_mpuqt]
        jne .again
        mov byte [busy], 0
.out:   pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        iret
