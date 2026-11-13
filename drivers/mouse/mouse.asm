; mouse.asm - w16's MOUSE.DRV: the Windows 3.1 mouse driver for a PS/2
; (8042 auxiliary-port) mouse. drivers/mouse/README.md says what it is,
; where each fact comes from and what is verified.
;
; Interface (Windows 3.1 DDK, "Mouse Drivers"; ordinals from the NE export
; table of Windows 3.1's MOUSE.DRV, docs/exports/MOUSE.txt):
;   1 Inquire(LPMOUSEINFO)          fills MOUSEINFO, returns its size
;   2 Enable(lpEventProc)           starts reporting to the event procedure
;   3 Disable()                     stops it
;   4 MouseGetIntVect()             the interrupt vector (74h, IRQ 12)
;   5 GetSetMouseData(lpData, dwFlags)  Mouse 8.20's private settings
;                                   call: only its error for bits 15-31
;   6 CplApplet(hwnd, code, l1, l2) no custom Control Panel dialog: FALSE
;   7 PowerEventProc(wEvent, dwParam)  POWER.DRV's: device set up again
;                                   after a resume (WM_POWER 2, 3)
;   8 WEP(bSystemExit)              turns the mouse off, returns 1
;
; Hardware path: the PS/2 BIOS (INT 15h C2xxh) finds, resets and
; configures the device; IRQ 12 (INT 74h) is taken in protected mode and
; the packet bytes are read from the 8042 directly (ports 60h/64h). When
; IRQ 12 comes with no auxiliary byte in the 8042 the interrupt goes on to
; the previous handler (the BIOS), which delivers the packet to the C207h
; handler; that path decodes the same packets. README.md: why.
;
; The event procedure (USER's Mouse_Event) is called at interrupt time,
; once per IRQ 12 at most, after the IRQ's packets are in: AX = SF_*
; status, BX = X mickeys, CX = Y mickeys (down positive), DX = the number
; of buttons, SI = DI = 0 (DDK, Mouse_Event; Windows for Pen Computing).
; How and when (the latest packet of the IRQ; one held back after each
; set-up) is what Windows 3.1's driver was observed to do: README.md.

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern GETVERSION                       ; KERNEL.3
extern __0040H                          ; KERNEL.193: selector of 0040:0000

; Status flags for the event procedure (DDK, Mouse_Event)
SF_MOVEMENT     equ 0001h
SF_B1_DOWN      equ 0002h
SF_B1_UP        equ 0004h
SF_B2_DOWN      equ 0008h
SF_B2_UP        equ 0010h

; PS/2 packet byte 0 (the IBM PS/2 pointing-device format)
P_LEFT          equ 01h
P_RIGHT         equ 02h
P_ALWAYS1       equ 08h
P_XSIGN         equ 10h
P_YSIGN         equ 20h

; 8042 status (port 64h)
K_OBF           equ 01h                 ; a byte is waiting in port 60h
K_AUX           equ 20h                 ; ... and it is from the aux port

IRQ12_VEC       equ 74h
NBUTTONS        equ 2
MI_SIZE         equ 16                  ; MOUSEINFO with msMouseCommPort
MI_SIZE30       equ 14                  ; without (Windows 3.0)
; MOUSEINFO as Windows 3.1's MOUSE.DRV fills it for a PS/2 mouse (MD1):
; FFh, FFh, 2, 30, 2, 2, 0, 0, 0 (msRate 30 though it sets 40 a second)
MI_RATE         equ 30
MI_THRESHOLD    equ 2

; Device settings, as Windows 3.1's MOUSE.DRV makes them (observed:
; tests/dos/ms2.asm, ms3b.asm): 3-byte packets, 8 counts/mm, scaling 1:1,
; 40 reports a second.
PKT_SIZE        equ 3
RES_8PERMM      equ 3                   ; C203h BH
SCALE_1TO1      equ 1                   ; C206h BH
RATE_40         equ 2                   ; C202h BH
RATE_10         equ 0                   ; C202h BH (3.1's set-up sets it
                                        ; first)
RATE_100        equ 5                   ; C202h BH (3.1's Disable)

segment _DATA
have_mouse      dw 0                    ; LibEntry found a PS/2 device
enabled         dw 0
event_proc      dd 0                    ; Enable's lpEventProc
old74           dd 0                    ; the INT 74h handler before ours
hooked          dw 0
idx             dw 0                    ; bytes of the packet so far
pkt             db 0, 0, 0
buttons         db 0                    ; button bits last reported
last_tick       dw 0                    ; BIOS tick of the last byte
pending         db 0                    ; a packet not yet reported:
p_b0            db 0                    ; the latest one (byte 0, X, Y)
p_x             db 0
p_y             db 0
newpkt          db 0                    ; a packet came during this IRQ
nirq            db 0                    ; IRQs that brought one since the
                                        ; last set-up (counts up to 2)
busy            db 0                    ; the event procedure is running

segment _TEXT

; ---- LibEntry: DS = DGROUP, DI = hInstance. Looks for the device. ----
global libentry
libentry:
        push si
        push di
        call detect
        mov [have_mouse wrt DGROUP], ax
        mov ax, 1                       ; loaded, mouse or not (Inquire says)
        pop di
        pop si
        retf

; detect: AX = 1 when the BIOS has a PS/2 pointing device that resets.
; Leaves it reset, which also disables it. The calls are Windows 3.1's
; detection's (MD2B: C205h, C201h, C202h BH 0, C204h).
detect:
        mov ax, 0C205h                  ; initialise: 3-byte packets
        mov bh, PKT_SIZE
        int 15h
        jc .no
        test ah, ah
        jnz .no
        mov ax, 0C201h                  ; reset
        int 15h
        jc .no
        test ah, ah
        jnz .no
        mov ax, 0C202h                  ; sample rate
        mov bh, RATE_10
        int 15h
        mov ax, 0C204h                  ; device ID: 0 for a mouse
        int 15h
        jc .no
        test ah, ah
        jnz .no
        mov ax, 1
        ret
.no:    xor ax, ax
        ret

; ---- 1: WORD FAR PASCAL Inquire(LPMOUSEINFO) ----
global INQUIRE
INQUIRE:
        push bp
        mov bp, sp
        push ds
        push di
        mov ax, DGROUP
        mov ds, ax
        call far GETVERSION             ; AL major, AH minor
        mov cx, ax
        les di, [bp + 6]
        cld
        mov ax, [have_mouse wrt DGROUP]
        neg al                          ; msExist: 0 or FFh
        sbb al, al
        mov ah, 0FFh                    ; msRelative: relative motion
        stosw
        mov ax, NBUTTONS                ; msNumButtons
        stosw
        mov ax, MI_RATE                 ; msRate
        stosw
        mov ax, MI_THRESHOLD            ; msXThreshold, msYThreshold
        stosw                           ; (reserved)
        stosw
        xor ax, ax                      ; msXRes, msYRes (reserved)
        stosw
        stosw
        mov ax, MI_SIZE30
        cmp cl, 3                       ; msMouseCommPort from 3.1 on
        ja .p
        jb .r
        cmp ch, 10
        jb .r
.p:     xor ax, ax                      ; no COM port
        stosw
        mov ax, MI_SIZE
.r:     pop di
        pop ds
        pop bp
        retf 4

; ---- 2: void FAR PASCAL Enable(lpEventProc) ----
; While enabled, only the event procedure changes (Windows 3.1's driver
; makes no BIOS calls then: MD2C).
global ENABLE
ENABLE:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 6]
        mov dx, [bp + 8]
        cli
        mov [event_proc wrt DGROUP], ax
        mov [event_proc + 2 wrt DGROUP], dx
        sti
        cmp word [enabled wrt DGROUP], 0
        jne .out
        cmp word [have_mouse wrt DGROUP], 0
        je .out
        call on
.out:   pop di
        pop si
        pop ds
        pop bp
        retf 4

; on: take IRQ 12, set the device up, enable it. DS = DGROUP. INT 74h is
; ours before the device is enabled: no packet can come before the
; handler is there.
on:
        push es
        mov ax, 3500h + IRQ12_VEC       ; DOS get/set vector: the DOS
        int 21h                         ; extender then also routes IRQ 12
        mov [old74 wrt DGROUP], bx      ; from real mode to it
        mov [old74 + 2 wrt DGROUP], es
        pop es
        push ds
        mov ax, cs
        mov ds, ax
        mov dx, isr74
        mov ax, 2500h + IRQ12_VEC
        int 21h
        pop ds
        mov word [hooked wrt DGROUP], 1
        mov word [enabled wrt DGROUP], 1
        call setup
        ; IRQ 12 and the cascade unmasked at the interrupt controllers
        cli
        in al, 0A1h
        and al, ~10h
        out 0A1h, al
        in al, 21h
        and al, ~04h
        out 21h, al
        sti
        ret

; setup: the device set up and enabled, in the order Windows 3.1's driver
; does it (MS2, MD2B: C205h, C201h, C202h BH 0, C204h, C203h BH 3, C207h,
; C206h BH 1, C202h BH 2, C200h BH 1), and the packet state reset. Enable
; and PowerEventProc's resume. DS = DGROUP.
setup:
        cli
        mov word [idx wrt DGROUP], 0
        mov byte [buttons wrt DGROUP], 0
        mov byte [pending wrt DGROUP], 0
        mov byte [newpkt wrt DGROUP], 0
        mov byte [nirq wrt DGROUP], 0
        sti
        mov ax, 0C205h                  ; initialise: 3-byte packets
        mov bh, PKT_SIZE
        int 15h
        mov ax, 0C201h                  ; reset (defaults, disabled)
        int 15h
        mov ax, 0C202h                  ; sample rate
        mov bh, RATE_10
        int 15h
        mov ax, 0C204h                  ; device ID
        int 15h
        mov ax, 0C203h                  ; resolution
        mov bh, RES_8PERMM
        int 15h
        push es
        mov ax, cs
        mov es, ax
        mov bx, bios_packet
        mov ax, 0C207h                  ; the BIOS's packet handler
        int 15h
        pop es
        mov ax, 0C206h                  ; scaling
        mov bh, SCALE_1TO1
        int 15h
        mov ax, 0C202h                  ; sample rate
        mov bh, RATE_40
        int 15h
        mov ax, 0C200h                  ; enable
        mov bh, 1
        int 15h
        ret

; off: disable reporting, give IRQ 12 back. DS = DGROUP. C200h BH 0 and
; C202h BH 5 as Windows 3.1's Disable (MD2); C207h 0:0 is w16's own (3.1
; makes no such call, MD2C): the BIOS keeps no handler address into the
; DOS extender, which goes when Windows ends.
off:
        mov word [enabled wrt DGROUP], 0
        mov ax, 0C200h                  ; disable
        mov bh, 0
        int 15h
        mov ax, 0C202h                  ; sample rate
        mov bh, RATE_100
        int 15h
        push es
        xor bx, bx
        mov es, bx
        mov ax, 0C207h                  ; no packet handler
        int 15h
        pop es
        cmp word [hooked wrt DGROUP], 0
        je .ret
        push ds
        lds dx, [old74 wrt DGROUP]
        mov ax, 2500h + IRQ12_VEC
        int 21h
        pop ds
        mov word [hooked wrt DGROUP], 0
.ret:   ret

; ---- 3: void FAR PASCAL Disable(void) ----
global DISABLE
DISABLE:
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        cmp word [enabled wrt DGROUP], 0
        je .ret
        call off
.ret:   pop di
        pop si
        pop ds
        retf

; ---- 4: int FAR PASCAL MouseGetIntVect(void): 74h, or -1 without a mouse
global MOUSEGETINTVECT
MOUSEGETINTVECT:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, -1
        cmp word [have_mouse wrt DGROUP], 0
        je .ret
        mov ax, IRQ12_VEC
.ret:   pop ds
        retf

; Assembled with -DPWRTRACE (tests/md2bprobe.sh's MOUSEPT.DRV, a test
; build only): GetSetMouseData and PowerEventProc report their parameter
; words, first pushed first, and their caller's CS and IP as INT 15h
; AX=C2E8h... and C2E0h... with BX = the word, which MS3B
; (tests/dos/ms3b.asm) records and the BIOS rejects.
%ifdef PWRTRACE
%macro TRACEWORDS 3                     ; first AX, first [bp+n], count
        push bp
        mov bp, sp
        push ax
        push bx
%assign i 0
%rep %3
        mov ax, %1 + i
        mov bx, [bp + %2 - 2 * i]
        int 15h
%assign i i + 1
%endrep
        mov ax, %1 + i
        mov bx, [bp + 4]                ; the caller's CS
        int 15h
        mov ax, %1 + i + 1
        mov bx, [bp + 2]                ; and IP
        int 15h
        pop bx
        pop ax
        pop bp
%endmacro
%endif

; ---- 5: DWORD FAR PASCAL GetSetMouseData(LPVOID lpData, DWORD dwFlags)
; Microsoft Mouse 8.20's private settings interface between its own
; programs (POINT.EXE, POINTLIB.DLL, POINTER.EXE) and its driver; Windows
; 3.1 was not seen calling it (MD2A: tests/apps/md2logs/a/notes.md). Bit 0
; of dwFlags: set (else get); bits 1-14 name fields of the buffer; AX = 0
; or error * 256 + bit. w16 keeps none of Mouse 8.20's settings: bits
; 15-31 get 3.1's answer (AX = 0900h, DX = HIWORD(dwFlags), the buffer
; untouched); for the rest the buffer is left as it is and AX = 0, DX = 0
; after a set, 2 after a get, as 3.1's. (Mouse 8.20's Control Panel then
; stops with its "requires MOUSE.DRV version 8.20" message.)
global GETSETMOUSEDATA
GETSETMOUSEDATA:
%ifdef PWRTRACE
        TRACEWORDS 0C2E8h, 12, 4
%endif
        push bp
        mov bp, sp
        mov dx, [bp + 8]                ; dwFlags, high word
        mov ax, 0900h
        test dx, dx
        jnz .ret
        test byte [bp + 7], 80h         ; bit 15
        jnz .ret
        xor ax, ax
        mov dx, 2
        test byte [bp + 6], 1           ; a set
        jz .ret
        xor dx, dx
.ret:   pop bp
        retf 8

; ---- 7: WORD FAR PASCAL PowerEventProc(WORD wEvent, DWORD dwParam)
; POWER.DRV's call after it sends WM_POWER (MD2B: tests/apps/md2logs/b/
; notes.md), with WM_POWER's codes: 1 suspend request, 2 resume, 3
; critical resume. On 2 and 3 while enabled the device is set up again as
; Enable sets it up (the event procedure and INT 74h stay); nothing on any
; other code or while disabled. dwParam unused. Returns 0.
global POWEREVENTPROC
POWEREVENTPROC:
%ifdef PWRTRACE
        TRACEWORDS 0C2E0h, 10, 3
%endif
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
%ifdef PWRFAIL                          ; -DPWRFAIL as well: -1 (PWR_FAIL) to
        mov ax, -1                      ; event 1 (tests/md2bprobe.sh 4)
        cmp word [bp + 10], 1
        je .out
%endif
        mov ax, [bp + 10]               ; wEvent
        cmp ax, 2
        jb .zero
        cmp ax, 3
        ja .zero
        cmp word [enabled wrt DGROUP], 0
        je .zero
        call setup
.zero:  xor ax, ax
.out:   mov dx, ax
        pop di
        pop si
        pop ds
        pop bp
        retf 6

; ---- 6: LONG FAR PASCAL CplApplet(HWND, WORD, DWORD, DWORD): FALSE, so
; Control Panel shows its own Mouse dialog (DDK, CplApplet)
global CPLAPPLET
CPLAPPLET:
        xor ax, ax
        xor dx, dx
        retf 12

; ---- 8: int FAR PASCAL WEP(int bSystemExit) ----
global WEP
WEP:
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        cmp word [enabled wrt DGROUP], 0
        je .ret
        call off
.ret:   mov ax, 1
        pop di
        pop si
        pop ds
        retf 2

; ---- interrupt time ----
;
; What Windows 3.1's driver was observed to do on the BIOS path (MD2C,
; tests/apps/md2logs/c/notes.md; DOSBox 0.74, Standard mode):
; - the event procedure is called once per IRQ 12, after the chained BIOS
;   INT 74h has returned, with the latest packet the C207h handler got;
;   earlier packets of the same IRQ are overwritten, not added;
; - after each set-up (Enable, a resume), the second IRQ that brings a
;   packet reports nothing; its packet stays pending and is reported at
;   the end of the next IRQ 12, unless a newer packet replaces it first.
; Why 3.1 does this is not known. On the 8042 path (a byte per IRQ, no
; BIOS) the same rules are applied with "an IRQ that completed a packet"
; for "an IRQ that brought one": an EXTRAPOLATION, not observed.

; INT 74h (IRQ 12), protected mode, on the interrupted stack. An
; auxiliary byte in the 8042 is read here; otherwise the previous handler
; (the BIOS, which may call bios_packet) runs as an interrupt and returns
; here. Then the IRQ's packet, if any, goes to the event procedure.
isr74:
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov byte [newpkt wrt DGROUP], 0
        in al, 64h
        and al, K_OBF | K_AUX
        cmp al, K_OBF | K_AUX
        jne .chain
        in al, 60h
        call aux_byte
        mov al, 20h
        out 0A0h, al                    ; end of interrupt: slave, master
        out 20h, al
        jmp .end
.chain: pushf
        call far [old74 wrt DGROUP]     ; the BIOS's IRQ 12 handling
        mov ax, DGROUP
        mov ds, ax
.end:   call endirq
        pop es
        pop ds
        popa
        iret

; the BIOS's packet handler (C207h): far call, the words (last pushed
; first) 0, Y, X, status; returns with RETF, the words left for the BIOS.
; The packet is kept for the end of the IRQ (isr74).
bios_packet:
        push bp
        mov bp, sp
        push ax
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov word [idx wrt DGROUP], 0
        mov al, [bp + 12]               ; status
        mov [p_b0 wrt DGROUP], al
        mov al, [bp + 10]               ; X
        mov [p_x wrt DGROUP], al
        mov al, [bp + 8]                ; Y
        mov [p_y wrt DGROUP], al
        mov byte [pending wrt DGROUP], 1
        mov byte [newpkt wrt DGROUP], 1
        pop ds
        pop ax
        pop bp
        retf

; aux_byte: AL = the next byte from the device. Collects packets; byte 0
; always has bit 3 set, and a packet's bytes come together: a byte after
; a pause of a timer tick or more starts a new packet (resynchronises
; after a lost byte). A whole packet becomes the pending one.
aux_byte:
        mov dl, al
        push es
        mov ax, __0040H
        mov es, ax
        mov ax, [es:006Ch]              ; BIOS timer ticks
        pop es
        mov cx, ax
        xchg ax, [last_tick wrt DGROUP]
        sub cx, ax
        cmp cx, 1
        jbe .near
        mov word [idx wrt DGROUP], 0
.near:  mov bx, [idx wrt DGROUP]
        test bx, bx
        jnz .store
        test dl, P_ALWAYS1
        jz .ret                         ; not a first byte: out of step
.store: mov [pkt + bx wrt DGROUP], dl
        inc bx
        cmp bx, PKT_SIZE
        jae .full
        mov [idx wrt DGROUP], bx
.ret:   ret
.full:  mov word [idx wrt DGROUP], 0
        mov al, [pkt wrt DGROUP]
        mov [p_b0 wrt DGROUP], al
        mov al, [pkt + 1 wrt DGROUP]
        mov [p_x wrt DGROUP], al
        mov al, [pkt + 2 wrt DGROUP]
        mov [p_y wrt DGROUP], al
        mov byte [pending wrt DGROUP], 1
        mov byte [newpkt wrt DGROUP], 1
        ret

; endirq: the end of an IRQ 12 (interrupts disabled). Counts the IRQs
; that brought a packet since the last set-up; the second one reports
; nothing. Otherwise the pending packet, if any, is decoded and reported:
; once, with interrupts enabled (as Windows 3.1's driver calls the event
; procedure: MD1). Not while a call is in progress (an IRQ 12 during the
; event procedure): its packet then stays pending for the next IRQ.
endirq:
        cmp byte [busy wrt DGROUP], 0
        jne .ret
        cmp byte [newpkt wrt DGROUP], 0
        je .report
        cmp byte [nirq wrt DGROUP], 2
        jae .report
        inc byte [nirq wrt DGROUP]
        cmp byte [nirq wrt DGROUP], 2
        je .ret                         ; held back
.report:
        cmp byte [pending wrt DGROUP], 0
        je .ret
        mov byte [pending wrt DGROUP], 0
        mov dl, [p_b0 wrt DGROUP]
        mov bl, [p_x wrt DGROUP]
        mov cl, [p_y wrt DGROUP]
        ; X and Y: 9-bit two's complement, the sign bits in byte 0. The
        ; overflow bits are not looked at: Windows 3.1's driver reports
        ; the 9-bit value as it is (MD1: DOSBox's 300-mickey step, sent
        ; with X overflow as 44, came to the event procedure as 44)
        xor bh, bh
        test dl, P_XSIGN
        jz .y
        mov bh, 0FFh
.y:     xor ch, ch
        test dl, P_YSIGN
        jz .yok
        mov ch, 0FFh
.yok:   neg cx                          ; the device counts up, the screen down
        xor ax, ax
        mov si, bx
        or si, cx
        jz .btn
        mov al, SF_MOVEMENT
.btn:   mov dh, [buttons wrt DGROUP]    ; the last reported buttons
        and dl, P_LEFT | P_RIGHT
        mov [buttons wrt DGROUP], dl
        xor dh, dl                      ; buttons that changed
        test dh, P_LEFT
        jz .r
        test dl, P_LEFT
        mov ah, SF_B1_DOWN
        jnz .l
        mov ah, SF_B1_UP
.l:     or al, ah
.r:     test dh, P_RIGHT
        jz .done
        test dl, P_RIGHT
        mov ah, SF_B2_DOWN
        jnz .rr
        mov ah, SF_B2_UP
.rr:    or al, ah
.done:  xor ah, ah
        test ax, ax
        jz .ret
        cmp word [enabled wrt DGROUP], 0
        je .ret
        mov di, [event_proc wrt DGROUP]
        or di, [event_proc + 2 wrt DGROUP]
        jz .ret
        mov byte [busy wrt DGROUP], 1
        sti
        mov dx, NBUTTONS
        xor si, si
        xor di, di
        push ds
        call far [event_proc wrt DGROUP]
        pop ds
        cli
        mov byte [busy wrt DGROUP], 0
.ret:   ret
