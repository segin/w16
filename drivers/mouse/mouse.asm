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
;   5 GetSetMouseData (8 bytes)     UNVERIFIED purpose: returns 20000h
;   6 CplApplet(hwnd, code, l1, l2) no custom Control Panel dialog: FALSE
;   7 PowerEventProc (6 bytes)      UNVERIFIED purpose: returns 0
;   8 WEP(bSystemExit)              turns the mouse off, returns 1
;
; Hardware path: the PS/2 BIOS (INT 15h C2xxh) finds, resets and
; configures the device; IRQ 12 (INT 74h) is taken in protected mode and
; the packet bytes are read from the 8042 directly (ports 60h/64h). When
; IRQ 12 comes with no auxiliary byte in the 8042 the interrupt goes on to
; the previous handler (the BIOS), which delivers the packet to the C207h
; handler; that path decodes the same packets. README.md: why.
;
; The event procedure (USER's Mouse_Event) is called at interrupt time:
; AX = SF_* status, BX = X mickeys, CX = Y mickeys (down positive), DX =
; the number of buttons, SI = DI = 0 (DDK, Mouse_Event; Windows for Pen
; Computing).

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

; Device settings made by Enable, as Windows 3.1's MOUSE.DRV makes them
; (observed: tests/dos/ms2.asm): 3-byte packets, 8 counts/mm, scaling
; 1:1, 40 reports a second.
PKT_SIZE        equ 3
RES_8PERMM      equ 3                   ; C203h BH
SCALE_1TO1      equ 1                   ; C206h BH
RATE_40         equ 2                   ; C202h BH
Q_LEN           equ 8
Q_MASK          equ Q_LEN * 2 - 1

segment _DATA
have_mouse      dw 0                    ; LibEntry found a PS/2 device
enabled         dw 0
event_proc      dd 0                    ; Enable's lpEventProc
old74           dd 0                    ; the INT 74h handler before ours
hooked          dw 0
idx             dw 0                    ; bytes of the packet so far
pkt             db 0, 0, 0
buttons         db 0                    ; button bits of the last packet
last_tick       dw 0                    ; BIOS tick of the last byte
npkt            db 0                    ; packets since Enable (up to 2)
busy            db 0                    ; report is calling out
rsti            db 0
q_head          dw 0                    ; events for report: a ring of
q_tail          dw 0                    ; Q_LEN (byte offsets into q_*)
q_st            times Q_LEN dw 0        ; status
q_x             times Q_LEN dw 0
q_y             times Q_LEN dw 0

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
; Leaves it reset, which also disables it.
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
global ENABLE
ENABLE:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        cmp word [enabled wrt DGROUP], 0
        je .new
        call off
.new:   mov ax, [bp + 6]
        mov dx, [bp + 8]
        cli
        mov [event_proc wrt DGROUP], ax
        mov [event_proc + 2 wrt DGROUP], dx
        sti
        cmp word [have_mouse wrt DGROUP], 0
        je .out
        call on
.out:   pop di
        pop si
        pop ds
        pop bp
        retf 4

; on: configure the device, take IRQ 12, enable reporting. DS = DGROUP.
on:
        mov ax, 0C205h                  ; initialise: 3-byte packets
        mov bh, PKT_SIZE
        int 15h
        mov ax, 0C201h                  ; reset (defaults, disabled)
        int 15h
        mov ax, 0C203h                  ; resolution
        mov bh, RES_8PERMM
        int 15h
        mov ax, 0C206h                  ; scaling
        mov bh, SCALE_1TO1
        int 15h
        mov ax, 0C202h                  ; sample rate
        mov bh, RATE_40
        int 15h
        cli
        mov word [idx wrt DGROUP], 0
        mov byte [buttons wrt DGROUP], 0
        mov word [q_head wrt DGROUP], 0
        mov word [q_tail wrt DGROUP], 0
        mov byte [npkt wrt DGROUP], 0
        sti
        ; INT 74h first, then the far-call handler and enable: no packet
        ; can come before both are in place
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
        push es
        mov ax, cs
        mov es, ax
        mov bx, bios_packet
        mov ax, 0C207h                  ; the BIOS's packet handler
        int 15h
        pop es
        mov word [enabled wrt DGROUP], 1
        mov ax, 0C200h                  ; enable
        mov bh, 1
        int 15h
        ; IRQ 12 and the cascade unmasked at the interrupt controllers
        cli
        in al, 0A1h
        and al, ~10h
        out 0A1h, al
        in al, 21h
        and al, ~04h
        out 21h, al
        sti
.ret:   ret

; off: disable reporting, give IRQ 12 back. DS = DGROUP.
off:
        mov word [enabled wrt DGROUP], 0
        mov ax, 0C200h                  ; disable
        mov bh, 0
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

; ---- 5: GetSetMouseData, 7: PowerEventProc. UNVERIFIED: what they do.
; Nothing in Windows 3.1 imports them. What is known (MD1, called with
; zero words on the stack): Windows 3.1's take 8 and 6 bytes of
; parameters off; GetSetMouseData returned DX:AX = 0002:0000,
; PowerEventProc 0. These take the same off and return the same.
global GETSETMOUSEDATA
GETSETMOUSEDATA:
        xor ax, ax
        mov dx, 2
        retf 8
global POWEREVENTPROC
POWEREVENTPROC:
        xor ax, ax
        xor dx, dx
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

; INT 74h (IRQ 12), protected mode, on the interrupted stack. An
; auxiliary byte in the 8042 is read here; anything else goes on to the
; previous handler (the BIOS, which may then call bios_packet).
isr74:
        sub sp, 4                       ; room for a far jump onwards
        pusha
        push ds
        push es
        mov bp, sp
        mov ax, DGROUP
        mov ds, ax
        in al, 64h
        and al, K_OBF | K_AUX
        cmp al, K_OBF | K_AUX
        jne .chain
        in al, 60h
        call aux_byte
        mov al, 20h
        out 0A0h, al                    ; end of interrupt: slave, master
        out 20h, al
        mov al, 1
        call report
        pop es
        pop ds
        popa
        add sp, 4
        iret
.chain: mov ax, [old74 wrt DGROUP]
        mov [bp + 20], ax
        mov ax, [old74 + 2 wrt DGROUP]
        mov [bp + 22], ax
        pop es
        pop ds
        popa
        retf                            ; to the previous handler

; the BIOS's packet handler (C207h): far call, the words (last pushed
; first) 0, Y, X, status; returns with RETF, the words left for the BIOS
bios_packet:
        push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov dl, [bp + 12]               ; status
        mov bl, [bp + 10]               ; X
        mov cl, [bp + 8]                ; Y
        mov word [idx wrt DGROUP], 0
        call packet
        pushf                           ; the event procedure gets the
        pop ax                          ; interrupt state the BIOS called
        and ah, 2                       ; with (DOSBox's: enabled)
        mov al, ah
        call report
        pop es
        pop ds
        popa
        popf
        pop bp
        retf

; aux_byte: AL = the next byte from the device. Collects packets; byte 0
; always has bit 3 set, and a packet's bytes come together: a byte after
; a pause of a timer tick or more starts a new packet (resynchronises
; after a lost byte).
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
        mov dl, [pkt wrt DGROUP]
        mov bl, [pkt + 1 wrt DGROUP]
        mov cl, [pkt + 2 wrt DGROUP]
        ; fall through

; packet: DL = byte 0, BL = X, CL = Y. Queues the event (if any) for
; report.
packet:
        ; the second packet after Enable is dropped: Windows 3.1's driver
        ; loses it (MD1 with MS2's packet log: after Enable the packets
        ; 0,0 / 5,0 / 40,0 came as 0,0 and 40,0; 0,140 / 3,0 / 5,0 /
        ; 40,0 as 0,140, 5,0 and 40,0). UNVERIFIED why.
        cmp byte [npkt wrt DGROUP], 2
        jae .use
        inc byte [npkt wrt DGROUP]
        cmp byte [npkt wrt DGROUP], 2
        jne .use
        ret
.use:
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
.btn:   mov dh, [buttons wrt DGROUP]
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
        mov si, [q_tail wrt DGROUP]     ; queue it for report
        mov di, si
        add di, 2
        and di, Q_MASK
        cmp di, [q_head wrt DGROUP]
        je .ret                         ; full: the event procedure has
        mov [q_st + si wrt DGROUP], ax  ; been busy for 8 packets; dropped
        mov [q_x + si wrt DGROUP], bx
        mov [q_y + si wrt DGROUP], cx
        mov [q_tail wrt DGROUP], di
.ret:   ret

; report: the queued events to the event procedure, one call each, with
; interrupts enabled when AL is nonzero (Windows 3.1's MOUSE.DRV calls it
; with interrupts enabled: MD1). Not while a call is in progress (an interrupt during one):
; that call's loop takes the new event. DS = DGROUP; registers not kept;
; returns with interrupts disabled.
report:
        cmp byte [busy wrt DGROUP], 0
        jne .ret
        mov byte [busy wrt DGROUP], 1
        mov [rsti wrt DGROUP], al
.next:  cli
        mov si, [q_head wrt DGROUP]
        cmp si, [q_tail wrt DGROUP]
        je .done
        mov ax, [q_st + si wrt DGROUP]
        mov bx, [q_x + si wrt DGROUP]
        mov cx, [q_y + si wrt DGROUP]
        add si, 2
        and si, Q_MASK
        mov [q_head wrt DGROUP], si
        cmp word [enabled wrt DGROUP], 0
        je .next
        mov di, [event_proc wrt DGROUP]
        or di, [event_proc + 2 wrt DGROUP]
        jz .next
        cmp byte [rsti wrt DGROUP], 0
        je .call
        sti
.call:  mov dx, NBUTTONS
        xor si, si
        xor di, di
        push ds
        call far [event_proc wrt DGROUP]
        pop ds
        jmp .next
.done:  mov byte [busy wrt DGROUP], 0
.ret:   ret
