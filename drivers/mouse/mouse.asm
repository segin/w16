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
extern GETWINDOWSDIRECTORY              ; KERNEL.134
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

; GetSetMouseData's settings (the fields of its buffer, MD2A: tests/apps/
; md2logs/a/notes.md). The values a get returns on a PS/2 setup are what
; Windows 3.1's driver returned there; the acceleration profiles and the
; sensitivity curve are w16's own (below).
gs_type         db 4                    ; +0, bit 1: mouse type (PS/2)
gs_irq          db 0                    ; +1, bit 2
raw_x           dw 0                    ; +2, +4, bit 3: the last packet's
raw_y           dw 0                    ; motion before scaling (Y down)
GS_VERSION      equ 0820h               ; +6, bit 4: the version Microsoft
                                        ; Mouse 8.20's programs ask for
gs_10           db 2                    ; +10, +11, bit 6: only 2, 1 are
gs_11           db 1                    ; accepted (meaning unknown)
sens_x          db 10                   ; +13, +14, bit 8: sensitivity,
sens_y          db 10                   ; 10 = 1:1
gs_15           db 0                    ; +15, bit 9: 0 or 1 (no effect seen)
active          db 3                    ; +24, bit 12: the active profile
; bit 10, through the far pointer at +16: the profiles, 260 bytes: a count
; per profile, then 32 thresholds per profile (the larger of |X| and |Y|
; of a packet, in mickeys), then 32 factors per profile in 1/16: the last
; entry whose threshold the packet reaches gives its factor. w16's own
; defaults, chosen so that a 40-mickey step comes out 2.5 times as large
; with Slow and 5 times with Fast, as with Windows 3.1's driver (MD2A
; beh12); the steps in between are w16's choice. Unaccelerated has none.
P_COUNT         equ 4
P_ENTRIES       equ 32
prof_count      db 4, 4, 5, 0
prof_thr        db 0, 8, 16, 24
                times P_ENTRIES - 4 db 0
                db 0, 6, 12, 20
                times P_ENTRIES - 4 db 0
                db 0, 4, 8, 14, 20
                times P_ENTRIES - 5 db 0
                times P_ENTRIES db 0
prof_fac        db 16, 24, 32, 40
                times P_ENTRIES - 4 db 0
                db 16, 28, 44, 56
                times P_ENTRIES - 4 db 0
                db 16, 32, 48, 64, 80
                times P_ENTRIES - 5 db 0
                times P_ENTRIES db 0
PROF_SIZE       equ P_COUNT + 2 * P_COUNT * P_ENTRIES
; bit 11, through the far pointer at +20: the profiles' names, 16
; characters each, padded with spaces (as Mouse 8.20's programs show them)
prof_names      db "Slow            "
                db "Moderate        "
                db "Fast            "
                db "Unaccelerated   "
NAMES_SIZE      equ 4 * 16
; bit 14, through the (unaligned) far pointer at +25: the settings file,
; a length byte (counting the 0) and the name: the Windows directory's
; MOUSE.INI (LibEntry)
INI_MAX         equ 128
ini_path        times INI_MAX + 1 db 0
ini_name        db "\MOUSE.INI", 0

segment _TEXT

; ---- LibEntry: DS = DGROUP, DI = hInstance. Looks for the device. ----
global libentry
libentry:
        push si
        push di
        call detect
        mov [have_mouse wrt DGROUP], ax
        ; GetSetMouseData's settings file: <Windows directory>\MOUSE.INI
        push ds
        push ini_path + 1 wrt DGROUP
        push INI_MAX - 12
        call far GETWINDOWSDIRECTORY    ; AX = length
        mov di, ax
        push ds
        pop es
        cmp byte [ini_path + di wrt DGROUP], '\'    ; (a root: "C:\")
        jne .name
        dec di
.name:  add di, ini_path + 1
        mov si, ini_name
        cld
.copy:  lodsb
        stosb
        test al, al
        jnz .copy
        sub di, ini_path + 1            ; the length, the 0 counted
        mov ax, di
        mov [ini_path wrt DGROUP], al
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
%ifdef LATEHOOK                         ; a test build (MOUSELH.DRV,
        mov word [enabled wrt DGROUP], 1 ; tests/mousestress.sh -M lh): the
        call setup                      ; device enabled before INT 74h is
%endif                                  ; taken, as Windows 3.1's driver does
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
%ifndef LATEHOOK
        mov word [enabled wrt DGROUP], 1
        call setup
%endif
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
        call unstick
        mov ax, 0C200h                  ; enable
        mov bh, 1
        int 15h
        ret

; unstick (task time, before the device is enabled): nothing of an
; earlier IRQ 12 is left to block the next. Bytes waiting in the 8042's
; auxiliary output are read and dropped; an IRQ 12 still marked in
; service at the slave PIC (no handler can be running now: this is task
; time) gets a specific end of interrupt, and then the cascade (IRQ 2) at
; the master too if nothing else is in service at the slave. Such a left
; over IRQ 12 stops every later one while the keyboard (IRQ 1) goes on.
unstick:
        pushf
        cli
        mov cx, 16
.drain: in al, 64h
        and al, K_OBF | K_AUX
        cmp al, K_OBF | K_AUX
        jne .pic
        in al, 60h
        loop .drain
.pic:   mov al, 0Bh                     ; OCW3: read the in-service register
        out 0A0h, al
        in al, 0A0h
        test al, 10h                    ; IRQ 12 (slave input 4)
        jz .done
        mov al, 64h                     ; specific EOI, slave input 4
        out 0A0h, al
        mov al, 0Bh
        out 0A0h, al
        in al, 0A0h
        test al, al
        jnz .done
        mov al, 0Bh
        out 20h, al
        in al, 20h
        test al, 04h                    ; the cascade, master input 2
        jz .done
        mov al, 62h                     ; specific EOI, master input 2
        out 20h, al
.done:  mov al, 0Ah                     ; OCW3: back to the request register
        out 0A0h, al
        out 20h, al
        popf
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
; 3.1 was not seen calling it. What it does, as Windows 3.1's driver was
; observed to do it (MD2A: tests/apps/md2logs/a/notes.md):
; - dwFlags bit 0: set the fields named by bits 1-14 from *lpData; without
;   it, get them into *lpData. Bit 13 means bits 10 and 11.
; - bits 15-31: AX = 0900h, DX = HIWORD(dwFlags), nothing done;
; - otherwise AX = 0, or error * 256 + bit for the first bad bit: 02 the
;   field is not there (bits 5 and 7), 03-05 a bad value in a set; one bad
;   bit and nothing is done. DX = 2 after a get, 0 after a set (whether
;   the result is meant as a DWORD is not known).
; - the fields: the data block above. A set of bits 1-4 (type, IRQ, last
;   motion, version) is accepted and changes nothing (UNVERIFIED).
global GETSETMOUSEDATA
GETSETMOUSEDATA:
%ifdef PWRTRACE
        TRACEWORDS 0C2E8h, 12, 4
%endif
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push es
        mov ax, DGROUP
        mov ds, ax
        mov dx, [bp + 8]                ; dwFlags, high word
        mov ax, 0900h
        test dx, dx
        jnz .ret
        mov cx, [bp + 6]                ; dwFlags, low word
        test ch, 80h                    ; bit 15
        jnz .ret
        test ch, 20h                    ; bit 13: bits 10 and 11
        jz .b13
        or ch, 0Ch
.b13:   mov dx, 2                       ; a get
        test cl, 1
        jz .dx
        xor dx, dx                      ; a set
.dx:    mov ax, 0205h                   ; bits 5 and 7: no such field here
        test cl, 20h
        jnz .ret
        mov ax, 0207h
        test cl, 80h
        jnz .ret
        les di, [bp + 10]               ; lpData
        test cl, 1
        jnz .set
        call gs_get
        jmp .ok
.set:   call gs_check                   ; AX = 0 or the error
        test ax, ax
        jnz .ret
        call gs_set
.ok:    xor ax, ax
.ret:   xor cx, cx                      ; as 3.1's leaves it (SI, DI kept)
        pop es
        pop di
        pop si
        pop ds
        pop bp
        retf 8

; gs_get: the fields CX names into ES:DI. DS = DGROUP.
gs_get:
        test cl, 2                      ; bit 1
        jz .b2
        mov al, [gs_type wrt DGROUP]
        mov [es:di], al
.b2:    test cl, 4
        jz .b3
        mov al, [gs_irq wrt DGROUP]
        mov [es:di + 1], al
.b3:    test cl, 8
        jz .b4
        cli
        mov ax, [raw_x wrt DGROUP]
        mov bx, [raw_y wrt DGROUP]
        sti
        mov [es:di + 2], ax
        mov [es:di + 4], bx
.b4:    test cl, 10h
        jz .b6
        mov word [es:di + 6], GS_VERSION
.b6:    test cl, 40h
        jz .b8
        mov al, [gs_10 wrt DGROUP]
        mov [es:di + 10], al
        mov al, [gs_11 wrt DGROUP]
        mov [es:di + 11], al
.b8:    test ch, 1
        jz .b9
        mov al, [sens_x wrt DGROUP]
        mov [es:di + 13], al
        mov al, [sens_y wrt DGROUP]
        mov [es:di + 14], al
.b9:    test ch, 2
        jz .b10
        mov al, [gs_15 wrt DGROUP]
        mov [es:di + 15], al
.b10:   test ch, 4
        jz .b11
        push cx
        push di
        push es
        les di, [es:di + 16]
        mov si, prof_count
        mov cx, PROF_SIZE
        cld
        rep movsb
        pop es
        pop di
        pop cx
.b11:   test ch, 8
        jz .b12
        push cx
        push di
        push es
        les di, [es:di + 20]
        mov si, prof_names
        mov cx, NAMES_SIZE
        cld
        rep movsb
        pop es
        pop di
        pop cx
.b12:   test ch, 10h
        jz .b14
        mov al, [active wrt DGROUP]
        mov [es:di + 24], al
.b14:   test ch, 40h
        jz .ret
        push cx
        push di
        push es
        les di, [es:di + 25]
        mov si, ini_path
        mov cl, [si]
        xor ch, ch
        inc cx                          ; the length byte as well
        cld
        rep movsb
        pop es
        pop di
        pop cx
.ret:   ret

; gs_check: AX = 0, or the error for the first bad value of a set (CX the
; flags, ES:DI the buffer). Observed: +11 = 0 gives 0406h, other values
; than 2, 1 at +10, +11 0506h; +15 above 1 (5Ah) 0309h. UNVERIFIED: the
; active profile above 3 (030Ch here), limits on sensitivity (none here).
gs_check:
        xor ax, ax
        test cl, 40h                    ; bit 6
        jz .b9
        mov ax, 0406h
        cmp byte [es:di + 11], 0
        je .ret
        mov ax, 0506h
        cmp byte [es:di + 11], 1
        jne .ret
        cmp byte [es:di + 10], 2
        jne .ret
        xor ax, ax
.b9:    test ch, 2
        jz .b12
        mov ax, 0309h
        cmp byte [es:di + 15], 1
        ja .ret
        xor ax, ax
.b12:   test ch, 10h
        jz .ret
        mov ax, 030Ch
        cmp byte [es:di + 24], P_COUNT - 1
        ja .ret
        xor ax, ax
.ret:   ret

; gs_set: the fields CX names from ES:DI (checked). DS = DGROUP. What the
; interrupt-time code uses changes with interrupts disabled.
gs_set:
        test ch, 1                      ; bit 8: sensitivity
        jz .b9
        mov al, [es:di + 13]
        mov ah, [es:di + 14]
        cli
        mov [sens_x wrt DGROUP], al
        mov [sens_y wrt DGROUP], ah
        sti
.b9:    test ch, 2
        jz .b10
        mov al, [es:di + 15]
        mov [gs_15 wrt DGROUP], al
.b10:   test ch, 4                      ; the profiles
        jz .b11
        push cx
        push di
        push ds
        push es
        lds si, [es:di + 16]
        mov ax, DGROUP
        mov es, ax
        mov di, prof_count
        mov cx, PROF_SIZE
        cld
        cli
        rep movsb
        sti
        pop es
        pop ds
        pop di
        pop cx
.b11:   test ch, 8                      ; their names
        jz .b12
        push cx
        push di
        push ds
        push es
        lds si, [es:di + 20]
        mov ax, DGROUP
        mov es, ax
        mov di, prof_names
        mov cx, NAMES_SIZE
        cld
        rep movsb
        pop es
        pop ds
        pop di
        pop cx
.b12:   test ch, 10h                    ; the active profile
        jz .b14
        mov al, [es:di + 24]
        mov [active wrt DGROUP], al
.b14:   test ch, 40h                    ; the settings file's name
        jz .ret
        push cx
        push di
        push ds
        push es
        lds si, [es:di + 25]
        mov ax, DGROUP
        mov es, ax
        mov cl, [si]                    ; its length byte
        xor ch, ch
        cmp cx, INI_MAX
        jbe .len
        mov cx, INI_MAX
.len:   mov di, ini_path
        mov [es:di], cl
        inc si
        inc di
        cld
        rep movsb
        pop es
        pop ds
        pop di
        pop cx
.ret:   ret

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
        mov [raw_x wrt DGROUP], bx
        mov [raw_y wrt DGROUP], cx
        xor ax, ax
        mov si, bx
        or si, cx
        jz .btn
        mov al, SF_MOVEMENT             ; (from the motion before scaling:
        push ax                         ; UNVERIFIED for a step scaled to
        push dx                         ; 0, 0)
        call scale
        pop dx
        pop ax
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

; scale: BX, CX (a packet's X and Y, the screen's signs) scaled as
; GetSetMouseData's settings say, inside the driver as Windows 3.1's does
; it (MD2A beh11, beh12): first the active profile's acceleration (the
; larger of |X| and |Y| picks the factor, both axes get it; UNVERIFIED
; which of |X|, |Y| or their sum 3.1 uses, and the order of the two
; steps), then each axis's sensitivity. Uses AX, DX, SI, DI.
scale:
        mov al, [active wrt DGROUP]
        xor ah, ah
        mov si, ax
        cmp byte [prof_count + si wrt DGROUP], 0
        je .sens                        ; no acceleration (Unaccelerated)
        mov ax, bx                      ; the packet's speed: max |X|, |Y|
        cwd
        xor ax, dx
        sub ax, dx
        mov di, ax
        mov ax, cx
        cwd
        xor ax, dx
        sub ax, dx
        cmp ax, di
        jbe .max
        mov di, ax
.max:   push cx
        mov cl, [prof_count + si wrt DGROUP]
        xor ch, ch
        cmp cx, P_ENTRIES
        jbe .cnt
        mov cx, P_ENTRIES
.cnt:   shl si, 5                       ; the profile's entries
        mov dh, 16                      ; 1:1 below every threshold
.lp:    mov al, [prof_thr + si wrt DGROUP]
        xor ah, ah
        cmp di, ax
        jb .nx
        mov dh, [prof_fac + si wrt DGROUP]
.nx:    inc si
        loop .lp
        pop cx
        mov al, dh
        xor ah, ah
        mov di, ax                      ; X, Y * factor / 16
        mov si, 16
        mov ax, bx
        imul di
        idiv si
        mov bx, ax
        mov ax, cx
        imul di
        idiv si
        mov cx, ax
.sens:  mov al, [sens_x wrt DGROUP]
        mov si, bx
        call sens
        mov bx, si
        mov al, [sens_y wrt DGROUP]
        mov si, cx
        call sens
        mov cx, si
        ret

; sens: SI scaled by sensitivity AL. 10 is 1:1 and 0 stops motion; in
; between and above, w16's own curve fitted to what Windows 3.1's driver
; did with 40 mickeys (MD2A beh11, beh12: 1 gave 1, 5 gave 15, 15 gave
; 90, 19 gave 130, 20 gave 140, and 20 made 20 into 70): (s - 2) / 8 up
; to 10, (s - 6) / 4 above; a step never shrinks below one mickey
; (UNVERIFIED between the observed points). Uses AX, DI.
sens:
        cmp al, 10
        je .ret
        test si, si
        jz .ret
        test al, al
        jnz .nz
        xor si, si
        ret
.nz:    xor ah, ah
        cmp al, 10
        ja .hi
        sub ax, 2
        mov di, 8
        jmp .mul
.hi:    sub ax, 6
        mov di, 4
.mul:   push dx
        push si
        imul si
        idiv di
        pop dx                          ; the step before
        test dx, dx
        js .neg
        cmp ax, 1
        jge .done
        mov ax, 1
        jmp .done
.neg:   cmp ax, -1
        jle .done
        mov ax, -1
.done:  mov si, ax
        pop dx
.ret:   ret
