; keyboard.asm - w16's KEYBOARD.DRV: the Windows 3.1 keyboard driver for
; the PC/AT keyboard (scan-code set 1 as the 8042 gives it), US layout,
; code page 437. drivers/keyboard/README.md says what it is, where each
; fact comes from and what is verified.
;
; Interface (Windows 3.1 DDK, "Keyboard Drivers"; ordinals from the NE
; export table of Windows 3.1's KEYBOARD.DRV, docs/exports/KEYBOARD.txt;
; argument sizes measured with KB1, tests/apps/kb1.c):
;   1 Inquire(LPKBINFO)                     RETF 4
;   2 Enable(lpEventProc, lpKeyState)       RETF 8
;   3 Disable()                             RETF 0
;   4 ToAscii(vk, scan, lpKeyState, lpState, flags)   RETF 14
;   5 AnsiToOem(lpSrc, lpDst)               RETF 8
;   6 OemToAnsi(lpSrc, lpDst)               RETF 8
;   7 SetSpeed(rate)                        RETF 2
;   8 WEP(bSystemExit)                      RETF 2
; 100 ScreenSwitchEnable(w)                 RETF 2
; 126 GetTableSeg()                         RETF 0
; 127 NewTable()                            RETF 0
; 128 OemKeyScan(ch)                        RETF 2, DX:AX
; 129 VkKeyScan(ch)                         RETF 2
; 130 GetKeyboardType(which)                RETF 2
; 131 MapVirtualKey(code, type)             RETF 4
; 132 GetKBCodePage()                       RETF 0
; 133 GetKeyNameText(lParam, lpBuf, size)   RETF 10
; 134 AnsiToOemBuff(lpSrc, lpDst, n)        RETF 10
; 135 OemToAnsiBuff(lpSrc, lpDst, n)        RETF 10
; 136 EnableKBSysReq(w)                     RETF 2
; 137 GetBIOSKeyProc()                      RETF 0, DX:AX
;
; Every behaviour below that is not in the DDK was observed black-box on
; Windows 3.1's driver with KB1 (every export over all inputs) and KB2
; (real key input with a recording driver in front, tests/apps/kbrec.asm,
; and a real-mode TSR, tests/dos/kb4.asm); tests/kbprobe.sh compares both
; with this driver.
;
; Interrupt time: INT 9 is taken in protected mode (INT 21h AH=25h: the
; DOS extender routes IRQ 1 to it from real mode too). Scan codes are
; read from port 60h; the interrupt is ended at the PIC here and is not
; passed on to the BIOS, except for the controller's answers (ACK and
; the like) and Ctrl+Alt+Del. USER's event procedure gets AL = VK, AH =
; 0 down / 80h up, BL = scan code, BH = 1 for an E0-prefixed key (and
; Num Lock), with interrupts enabled and IRQ 1 masked meanwhile.
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment KBTAB  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern GETPRIVATEPROFILEINT             ; KERNEL.127
extern __0040H                          ; KERNEL.193
extern __AHINCR                         ; KERNEL.114
extern DOSIGNAL                         ; KERNEL.139
extern CVWBREAK                         ; KERNEL.205

; Virtual keys (Windows 3.1 DDK, "Virtual-Key Codes"; SDK windows.h)
VK_CANCEL       equ 03h
VK_BACK         equ 08h
VK_TAB          equ 09h
VK_CLEAR        equ 0Ch
VK_RETURN       equ 0Dh
VK_SHIFT        equ 10h
VK_CONTROL      equ 11h
VK_MENU         equ 12h
VK_PAUSE        equ 13h
VK_CAPITAL      equ 14h
VK_ESCAPE       equ 1Bh
VK_SPACE        equ 20h
VK_PRIOR        equ 21h
VK_NEXT         equ 22h
VK_END          equ 23h
VK_HOME         equ 24h
VK_LEFT         equ 25h
VK_UP           equ 26h
VK_RIGHT        equ 27h
VK_DOWN         equ 28h
VK_SNAPSHOT     equ 2Ch
VK_INSERT       equ 2Dh
VK_DELETE       equ 2Eh
VK_NUMPAD0      equ 60h
VK_MULTIPLY     equ 6Ah
VK_ADD          equ 6Bh
VK_SUBTRACT     equ 6Dh
VK_DECIMAL      equ 6Eh
VK_DIVIDE       equ 6Fh
VK_F1           equ 70h
VK_NUMLOCK      equ 90h
VK_SCROLL       equ 91h
VK_OEM_1        equ 0BAh
VK_OEM_PLUS     equ 0BBh
VK_OEM_COMMA    equ 0BCh
VK_OEM_MINUS    equ 0BDh
VK_OEM_PERIOD   equ 0BEh
VK_OEM_2        equ 0BFh
VK_OEM_3        equ 0C0h
VK_OEM_4        equ 0DBh
VK_OEM_5        equ 0DCh
VK_OEM_6        equ 0DDh
VK_OEM_7        equ 0DEh
VK_OEM_102      equ 0E2h

; BIOS data area (IBM PC/AT technical reference)
KB_FLAG         equ 17h                 ; shift and lock states
KB_FLAG1        equ 18h                 ; keys held
KB_FLAG3        equ 96h
F_RSHIFT        equ 01h
F_LSHIFT        equ 02h
F_CTRL          equ 04h
F_ALT           equ 08h
F_SCROLL        equ 10h
F_NUM           equ 20h
F_CAPS          equ 40h
F1_LCTRL        equ 01h
F1_LALT         equ 02h
F1_NUMDOWN      equ 20h
F3_101          equ 10h                 ; an enhanced (101/102-key) keyboard

; scan codes (IBM PC/AT scan-code set 1, as the 8042 translates)
SC_CTRL         equ 1Dh
SC_LSHIFT       equ 2Ah
SC_RSHIFT       equ 36h
SC_ALT          equ 38h
SC_CAPS         equ 3Ah
SC_NUM          equ 45h
SC_SCROLL       equ 46h
SC_DEL          equ 53h
SC_SYSREQ       equ 54h
SC_LAST         equ 58h                 ; the table's last code (F12)

; ToAscii's lpScan bits (DDK, ToAscii)
TA_UP           equ 8000h

segment _DATA
event_proc      dd 0                    ; Enable's lpEventProc
key_state       dd 0                    ; Enable's lpKeyState
enabled         db 0
old9            dd 0                    ; INT 9 before Enable
have_old9       db 0
ssw_enable      dw 1                    ; ScreenSwitchEnable's flag
sysreq          dw 0                    ; EnableKBSysReq: bit 0 INT 2,
                                        ; bit 1 CVWBreak (DDK: off at
                                        ; first; KERNEL turns CVW on)
kb_type         dw 4
kb_subtype      dw 0
prefix          db 0                    ; E0h after an E0 prefix
e1_state        db 0                    ; Pause's E1 sequence: 1 after
                                        ; E1, 2 after its 1Dh/9Dh
shift_scan      db SC_LSHIFT            ; the last Shift pressed
fake_shift      db 0                    ; Shift reported up for the keypad
alt_last        db 0                    ; Alt+keypad: the digit key held
pic_mask        db 0                    ; the PIC's mask before an event
alt_ansi        db 0                    ; Alt+keypad: began with 0 (ANSI)
                                        ; (no conversion at Alt's release)

ini_keyboard    db 'keyboard', 0
ini_type        db 'type', 0
ini_subtype     db 'subtype', 0
ini_system      db 'SYSTEM.INI', 0

%include "kbtables.inc"                 ; ansi_to_oem, oem_to_ansi

segment _TEXT

; ---- LibEntry: DS = DGROUP. The keyboard type from SYSTEM.INI. ----
global libentry
libentry:
        push si
        push di
        call newtable_
        mov ax, 1
        pop di
        pop si
        retf

; newtable_: [keyboard] type= and subtype= (DDK, NewTable; the default
; type: an enhanced keyboard when the BIOS says so, else AT). DS = DGROUP.
newtable_:
        push es
        mov ax, __0040H
        mov es, ax
        mov ax, 3
        test byte [es:KB_FLAG3], F3_101
        jz .t
        mov ax, 4
.t:     pop es
        push ds
        push word ini_keyboard
        push ds
        push word ini_type
        push ax
        push ds
        push word ini_system
        call far GETPRIVATEPROFILEINT
        mov [kb_type wrt DGROUP], ax
        push ds
        push word ini_keyboard
        push ds
        push word ini_subtype
        push word 0
        push ds
        push word ini_system
        call far GETPRIVATEPROFILEINT
        mov [kb_subtype wrt DGROUP], ax
        ret

; ---- 1: WORD Inquire(LPKBINFO): no double-byte ranges (first above
; last), a 4-byte ToAscii state block; returns the 6 bytes written
; (KB1: FF FE FF FE 04 00, 6) ----
global INQUIRE
INQUIRE:
        push bp
        mov bp, sp
        push di
        les di, [bp + 6]
        cld
        mov ax, 0FEFFh
        stosw
        stosw
        mov ax, 4
        stosw
        mov ax, 6
        pop di
        pop bp
        retf 4

; ---- 2: void Enable(lpEventProc, lpKeyState) ----
; The key-state array gets the shift and lock states from the BIOS
; flags: Shift, Ctrl, Alt 80h when held; Caps, Num, Scroll Lock 01h when
; on; 00h otherwise (KB1: every time, enabled or not). INT 9 is taken
; once; then the lights from the BIOS flags (one INT 16h AH=01h, KB2).
global ENABLE
ENABLE:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        les ax, [bp + 10]
        mov [event_proc wrt DGROUP], ax
        mov [event_proc + 2 wrt DGROUP], es
        les di, [bp + 6]
        mov [key_state wrt DGROUP], di
        mov [key_state + 2 wrt DGROUP], es
        push ds
        mov ax, __0040H
        mov ds, ax
        mov bl, [KB_FLAG]
        pop ds
        xor ax, ax
        test bl, F_LSHIFT | F_RSHIFT
        jz .s
        mov al, 80h
.s:     mov [es:di + VK_SHIFT], al
        xor ax, ax
        test bl, F_CTRL
        jz .c
        mov al, 80h
.c:     mov [es:di + VK_CONTROL], al
        xor ax, ax
        test bl, F_ALT
        jz .a
        mov al, 80h
.a:     mov [es:di + VK_MENU], al
        xor ax, ax
        test bl, F_CAPS
        jz .k
        inc al
.k:     mov [es:di + VK_CAPITAL], al
        xor ax, ax
        test bl, F_NUM
        jz .n
        inc al
.n:     mov [es:di + VK_NUMLOCK], al
        xor ax, ax
        test bl, F_SCROLL
        jz .l
        inc al
.l:     mov [es:di + VK_SCROLL], al
        ; the BIOS's "enhanced keyboard" flag (40:96h bit 4) cleared, as
        ; Windows 3.1's Enable does (KB2: set by DOSBox's BIOS, still set
        ; at SetSpeed, clear from Enable on; KB1: Enable leaves bit 1).
        ; UNVERIFIED: the other bits (DOSBox sets no other)
        push es
        mov ax, __0040H
        mov es, ax
        and byte [es:KB_FLAG3], ~F3_101
        pop es
        cli
        mov byte [prefix wrt DGROUP], 0
        mov byte [e1_state wrt DGROUP], 0
        mov byte [fake_shift wrt DGROUP], 0
        sti
        cmp byte [enabled wrt DGROUP], 0
        jne .on
        push es
        mov ax, 3509h
        int 21h
        mov [old9 wrt DGROUP], bx
        mov [old9 + 2 wrt DGROUP], es
        mov byte [have_old9 wrt DGROUP], 1
        pop es
        push ds
        mov ax, cs
        mov ds, ax
        mov dx, isr9
        mov ax, 2509h
        int 21h
        pop ds
        mov byte [enabled wrt DGROUP], 1
.on:    mov ax, 0102h
        int 16h
        pop di
        pop si
        pop ds
        pop bp
        retf 8

; ---- 3: void Disable(void): INT 9 given back ----
global DISABLE
DISABLE:
        push ds
        mov ax, DGROUP
        mov ds, ax
        cmp byte [enabled wrt DGROUP], 0
        je .r
        mov byte [enabled wrt DGROUP], 0
        push ds
        lds dx, [old9 wrt DGROUP]
        mov ax, 2509h
        int 21h
        pop ds
.r:     pop ds
        retf

; ---- 8: WEP ----
global WEP
WEP:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        cmp byte [enabled wrt DGROUP], 0
        je .r
        push cs
        call DISABLE
.r:     mov ax, 1
        pop ds
        pop bp
        retf 2

; ---- 100: void ScreenSwitchEnable(WORD) (DDK: OS/2 only; kept) ----
global SCREENSWITCHENABLE
SCREENSWITCHENABLE:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [bp + 6]
        mov [ssw_enable wrt DGROUP], ax
        pop ds
        pop bp
        retf 2

; ---- 127: void NewTable(void): the type again (no KEYBOARD.DLL: US) ----
global NEWTABLE
NEWTABLE:
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        call newtable_
        xor ax, ax
        pop di
        pop si
        pop ds
        retf

; ---- 130: int GetKeyboardType(which): 0 the type, 1 the subtype, 2 the
; number of function keys (KB1: 4, 0, 12 for [keyboard] type=4; the
; counts for the other types as Windows documents them), else 0 ----
global GETKEYBOARDTYPE
GETKEYBOARDTYPE:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov bx, [bp + 6]
        xor ax, ax
        cmp bx, 0
        jne .s
        mov ax, [kb_type wrt DGROUP]
        jmp .r
.s:     cmp bx, 1
        jne .f
        mov ax, [kb_subtype wrt DGROUP]
        jmp .r
.f:     cmp bx, 2
        jne .r
        mov bx, [kb_type wrt DGROUP]
        mov ax, 10                      ; types 1, 3, 5: 10
        cmp bx, 2
        jne .f4
        mov ax, 18
        jmp .r
.f4:    cmp bx, 4
        jne .f6
        mov ax, 12
        jmp .r
.f6:    cmp bx, 6
        jne .r
        mov ax, 24
.r:     pop ds
        pop bp
        retf 2

; ---- 132: GetKBCodePage(void): 437 (no OEMANSI.BIN support) ----
global GETKBCODEPAGE
GETKBCODEPAGE:
        mov ax, 437
        retf

; ---- 136: EnableKBSysReq(w): 1 INT 2 on, 2 off, 4 CVWBreak on, 8 off
; (the offs after the ons: KB1, 5 gives 3, 0Ah and FFFFh 0); returns the
; state (bit 0 INT 2, bit 1 CVWBreak) ----
global ENABLEKBSYSREQ
ENABLEKBSYSREQ:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov bx, [bp + 6]
        mov ax, [sysreq wrt DGROUP]
        test bl, 1
        jz .1
        or al, 1
.1:     test bl, 4
        jz .2
        or al, 2
.2:     test bl, 2
        jz .3
        and al, ~1
.3:     test bl, 8
        jz .4
        and al, ~2
.4:     mov [sysreq wrt DGROUP], ax
        pop ds
        pop bp
        retf 2

; ---- 137: FARPROC GetBIOSKeyProc(void): the INT 9 handler before ours
; (KB1: the vector Disable puts back) ----
global GETBIOSKEYPROC
GETBIOSKEYPROC:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [old9 wrt DGROUP]
        mov dx, [old9 + 2 wrt DGROUP]
        pop ds
        retf

; ---- 7: int SetSpeed(rate): -1 asks (0: it can); otherwise the rate
; (bits 0-4) and delay (bits 5-6) go to the keyboard (command F3h) and
; come back as given. ----
global SETSPEED
SETSPEED:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        cmp ax, -1
        jne .set
        xor ax, ax
        jmp .r
.set:   pushf
        cli
        mov al, 0F3h
        call kbd_send
        ; Windows' speed counts up (31 the fastest), the keyboard's rate
        ; down (0 the fastest: IBM PC/AT); the delay as it is (KB2's
        ; repeat counts against Windows 3.1's driver's)
        mov al, [bp + 6]
        mov ah, al
        and ax, 1F60h
        xor ah, 1Fh
        or al, ah
        call kbd_send
        popf
        mov ax, [bp + 6]
.r:     pop bp
        retf 2

; kbd_send: AL to the keyboard (port 60h) once the controller takes
; input, then its answer read (polled, interrupts off). Gives up after a
; while (no keyboard).
kbd_send:
        push cx
        mov ah, al
        xor cx, cx
.w:     in al, 64h
        test al, 2
        jz .out
        loop .w
.out:   mov al, ah
        out 60h, al
        xor cx, cx
.a:     in al, 64h
        test al, 1
        jnz .got
        loop .a
        jmp .r
.got:   in al, 60h
.r:     pop cx
        ret

; ---- 5, 6: AnsiToOem / OemToAnsi(lpSrc, lpDst): through the NUL,
; across 64K (KB1: a string from offset FFF0h goes on in the next
; selector, +__AHINCR); returns -1 ----
global ANSITOOEM
ANSITOOEM:
        mov bx, ansi_to_oem
        jmp short xlat_str
global OEMTOANSI
OEMTOANSI:
        mov bx, oem_to_ansi
xlat_str:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        lds si, [bp + 10]
        les di, [bp + 6]
        mov cx, DGROUP
        cld
.l:     lodsb
        push ds
        mov ds, cx
        xlatb
        pop ds
        stosb
        or si, si
        jnz .s
        mov dx, ds
        add dx, __AHINCR
        mov ds, dx
.s:     or di, di
        jnz .d
        mov dx, es
        add dx, __AHINCR
        mov es, dx
.d:     or al, al
        jnz .l
        mov ax, -1
        pop di
        pop si
        pop ds
        pop bp
        retf 8

; ---- 134, 135: AnsiToOemBuff / OemToAnsiBuff(lpSrc, lpDst, n): n bytes,
; within the segment (KB1: these do not go on into the next selector) ----
global ANSITOOEMBUFF
ANSITOOEMBUFF:
        mov bx, ansi_to_oem
        jmp short xlat_buf
global OEMTOANSIBUFF
OEMTOANSIBUFF:
        mov bx, oem_to_ansi
xlat_buf:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov cx, [bp + 6]
        lds si, [bp + 12]
        les di, [bp + 8]
        mov dx, DGROUP
        cld
        jcxz .r
.l:     lodsb
        push ds
        mov ds, dx
        xlatb
        pop ds
        stosb
        loop .l
.r:     pop di
        pop si
        pop ds
        pop bp
        retf 10

; ---- 129: int VkKeyScan(ch): the key and shifts that type ANSI char ch
; (low byte): AL = VK, AH = 0, 1 Shift, 2 Ctrl; -1 if none. The order
; of the search is what makes the choice among keys (KB1): the letters,
; then the plain keys unshifted and shifted, then the Ctrl keys, then
; Ctrl+letter. ----
global VKKEYSCAN
VKKEYSCAN:
        push bp
        mov bp, sp
        mov al, [bp + 6]
        call vkscan
        pop bp
        retf 2

; vkscan: AL = char -> AX
vkscan:
        push si
        push ds
        mov cx, seg plain_tab
        mov ds, cx
        cmp al, 'a'
        jb .uc
        cmp al, 'z'
        ja .uc
        sub al, 20h
        xor ah, ah
        jmp .r
.uc:    cmp al, 'A'
        jb .plain
        cmp al, 'Z'
        ja .plain
        mov ah, 1
        jmp .r
.plain: mov si, plain_tab
.p:     mov ah, [si]
        or ah, ah
        jz .ctrl
        cmp ah, VK_NUMPAD0              ; the keypad is left out (DDK)
        jb .p1
        cmp ah, VK_DIVIDE
        jbe .pn
.p1:    cmp al, [si + 1]
        je .p0
        cmp al, [si + 2]
        je .ps
.pn:    add si, 3
        jmp .p
.p0:    mov al, ah
        xor ah, ah
        jmp .r
.ps:    mov al, ah
        mov ah, 1
        jmp .r
.ctrl:  mov si, ctrl_tab
.c:     mov ah, [si]
        or ah, ah
        jz .cl
        cmp al, [si + 1]
        je .c0
        add si, 2
        jmp .c
.c0:    mov al, ah
        mov ah, 2
        jmp .r
.cl:    cmp al, 1
        jb .no
        cmp al, 1Ah
        ja .no
        add al, 40h
        mov ah, 2
        jmp .r
.no:    mov ax, -1
.r:     pop ds
        pop si
        ret

; ---- 128: DWORD OemKeyScan(ch): AX = scan code, DX = shifts (2 Shift,
; 4 Ctrl), from VkKeyScan; -1:-1 for OEM 80h-FFh and keys there are none
; for; a control character whose key has no scan code (03h, VK_CANCEL)
; as Ctrl+letter (KB1) ----
global OEMKEYSCAN
OEMKEYSCAN:
        push bp
        mov bp, sp
        mov al, [bp + 6]
        cmp al, 80h
        jae .no
        call vkscan
        cmp ax, -1
        je .no
        mov dl, ah
        xor dh, dh
        shl dx, 1
        push dx
        call vk2scan
        pop dx
        or ax, ax
        jnz .r
        ; a key with no scan code (VK_CANCEL for 03h): Ctrl and the letter
        ; (KB1: 03h gives Ctrl+C)
        mov al, [bp + 6]
        cmp al, 1
        jb .no
        cmp al, 1Ah
        ja .no
        add al, 40h
        call vk2scan
        mov dx, 4
        jmp .r
.no:    mov ax, -1
        mov dx, ax
.r:     pop bp
        retf 2

; vk2scan: AL = VK -> AX = its scan code (the first in the scan-code
; table; the keypad's digits and decimal point as the keypad's; else 0)
vk2scan:
        push si
        push ds
        or al, al
        jz .n0
        mov cx, seg scan_vk
        mov ds, cx
        mov si, scan_vk + 1
        mov cx, SC_LAST
.l:     cmp al, [si]
        je .f
        inc si
        loop .l
        cmp al, VK_NUMPAD0
        jb .n
        cmp al, VK_NUMPAD0 + 9
        ja .dec
        sub al, VK_NUMPAD0
        mov bx, pad_digit_scan
        xlatb
        xor ah, ah
        jmp .r
.dec:   cmp al, VK_DECIMAL
        jne .n
        mov ax, SC_DEL
        jmp .r
.f:     mov ax, si
        sub ax, scan_vk
        jmp .r
.n:     xor ax, ax
.r:     pop ds
        pop si
        ret
.n0:    xor ax, ax
        jmp .r

; vk2char: AL = VK -> AL = its unshifted character, 0 if none (letters
; upper case; the keypad's digits none: MapVirtualKey type 2, KB1)
vk2char:
        push si
        push ds
        mov cx, seg plain_tab
        mov ds, cx
        cmp al, 'A'
        jb .t
        cmp al, 'Z'
        jbe .r
.t:     mov si, plain_tab
.l:     mov ah, [si]
        or ah, ah
        jz .n
        cmp al, ah
        je .f
        add si, 3
        jmp .l
.f:     mov al, [si + 1]
        jmp .r
.n:     xor al, al
.r:     pop ds
        pop si
        ret

; ---- 131: int MapVirtualKey(code, type): type (low byte) 0 VK to scan
; code (the low byte of code), 1 scan code to VK (codes above 58h: 0; no
; key: FFh), else VK to character: the high byte of code is kept (KB1:
; 141h gives 141h, 1BAh 13Bh). ----
global MAPVIRTUALKEY
MAPVIRTUALKEY:
        push bp
        mov bp, sp
        mov ax, [bp + 8]
        mov bl, [bp + 6]
        cmp bl, 0
        jne .t1
        call vk2scan
        jmp .r
.t1:    cmp bl, 1
        jne .t2
        cmp ax, SC_LAST
        ja .z
        push ds
        mov bx, seg scan_vk
        mov ds, bx
        mov bx, scan_vk
        xlatb
        pop ds
        xor ah, ah
        or al, al
        jnz .r
        mov al, 0FFh
        jmp .r
.t2:    push ax
        call vk2char
        pop dx
        or al, al
        jz .z
        mov ah, dh
        jmp .r
.z:     xor ax, ax
.r:     pop bp
        retf 4

; ---- 133: int GetKeyNameText(LONG lParam, LPSTR buf, int size) ----
; lParam bits 16-23 the scan code, bit 24 extended, bit 25 "don't care"
; (left and right Shift, Ctrl, Alt, the two Enter keys the same). The
; key's name (the observed names: UI text), else its character for the
; keys that type one (the letters upper case); 0 for none. A name is
; cut to size-1 characters; the result is then the size (KB1: "Space" in
; 2 bytes gives "S" and 2). A character is written with its NUL whatever
; the size, if size is not 0 (KB1).
global GETKEYNAMETEXT
GETKEYNAMETEXT:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov dx, [bp + 14]               ; lParam high word
        mov bl, dl                      ; scan code
        mov bh, dh
        and bh, 1                       ; extended
        test dh, 2
        jz .look
        cmp bl, SC_RSHIFT
        jne .dc
        mov bl, SC_LSHIFT
.dc:    or bh, bh
        jz .look
        cmp bl, 1Ch
        je .dc0
        cmp bl, SC_CTRL
        je .dc0
        cmp bl, SC_ALT
        jne .look
.dc0:   xor bh, bh
.look:  mov ax, seg names
        mov ds, ax
        mov si, names
.n:     mov al, [si]
        cmp al, 0FFh
        je .char
        cmp al, bl
        jne .nn
        cmp [si + 1], bh
        je .found
.nn:    add si, 2
.skip:  lodsb
        or al, al
        jnz .skip
        jmp .n
.found: add si, 2
        cmp byte [si], 0
        je .zero                        ; listed as having no name
        mov cx, [bp + 6]
        jcxz .zero
        les di, [bp + 8]
        xor dx, dx                      ; the name's length
.len:   mov bx, si
        add bx, dx
        cmp byte [bx], 0
        je .cp
        inc dx
        jmp .len
.cp:    mov ax, dx                      ; result: min(length, size)
        cmp ax, cx
        jbe .cp1
        mov ax, cx
.cp1:   mov bx, cx                      ; copied: min(length, size - 1)
        dec bx
        cmp dx, bx
        jae .cp2
        mov bx, dx
.cp2:   push cx
        mov cx, bx
        cld
        rep movsb
        pop cx
        mov byte [es:di], 0
        jmp .r
.char:  ; the keys that type a character: VK 30h-5Ah and the OEM keys
        cmp bl, SC_LAST
        ja .zero
        mov al, bl
        mov bx, scan_vk
        xlatb
        cmp al, '0'
        jb .zero
        cmp al, 'Z'
        jbe .ch
        cmp al, VK_OEM_1
        jb .zero
.ch:    call vk2char
        or al, al
        jz .zero
        mov cx, [bp + 6]
        jcxz .zero
        les di, [bp + 8]
        mov [es:di], al
        mov byte [es:di + 1], 0
        mov ax, 1
        jmp .r
.zero:  xor ax, ax
.r:     pop di
        pop si
        pop ds
        pop bp
        retf 10

; ---- 4: int ToAscii(vk, scan, lpKeyState, lpState, flags) ----
; (Behaviour as observed with KB1 and KB2; README.md, "ToAscii".)
; The state block (4 bytes, Inquire) carries the output and the Alt+
; keypad number: lpState[0] is the character, or the number so far.
;   - key up: nothing written, 0; but Alt (VK_MENU) up ends an Alt+keypad
;     number: lpState[0] 0 gives 0 (a 0 word written), else 1 with the
;     state block converted OEM to ANSI in place (as a string) unless the
;     number began with 0
;   - VK 0: the lights from the key-state array
;   - Alt (and not Ctrl) held and a keypad digit's scan code: the number
;     is lpState[0] * 10 + digit (a byte; a word written), 0 returned;
;     the same digit key again before it is released is ignored
;   - otherwise the character for the shift state, 1 (one byte written);
;     none: a 0 word written, 0 returned, and the lights set from the
;     key-state array (INT 16h AH=01h, twice for Num Lock: KB2)
global TOASCII
TOASCII:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
        ; pushed in the order vk, scan, lpKeyState, lpState, flags:
        ; [bp+18] vk, [bp+16] scan, [bp+12] lpKeyState, [bp+8] lpState,
        ; [bp+6] flags (not used: the DDK's duplicates)
        mov ax, [bp + 18]
        mov dx, [bp + 16]
        les di, [bp + 8]                ; the state block
        lds si, [bp + 12]               ; the key-state array
        ; DS:SI key state, ES:DI state block
        or al, al                       ; VK 0: the lights, up or down
        jz .down
        test dx, TA_UP
        jz .down
        ; a key released: anything only for Alt
        mov cx, DGROUP
        push ds
        mov ds, cx
        mov byte [alt_last wrt DGROUP], 0
        pop ds
        cmp al, VK_MENU
        jne .zero_nw
        mov al, [es:di]
        or al, al
        jz .zero_w
        push ds
        mov ds, cx
        cmp byte [alt_ansi wrt DGROUP], 0
        mov byte [alt_ansi wrt DGROUP], 0
        pop ds
        jne .one
        ; OEM to ANSI in place, up to the NUL
        mov bx, di
.cv:    mov al, [es:bx]
        or al, al
        jz .one
        push ds
        mov ds, cx
        push bx
        mov bx, oem_to_ansi
        xlatb
        pop bx
        pop ds
        mov [es:bx], al
        inc bx
        jmp .cv
.one:   mov ax, 1
        jmp .r
.down:  or al, al
        jnz .notzero
        call lights
        jmp .zero_w
.notzero:
        ; Alt+keypad: Alt held, Ctrl not, a keypad digit's scan code
        test byte [si + VK_MENU], 80h
        jz .chr
        test byte [si + VK_CONTROL], 80h
        jnz .chr
        test dx, 100h                   ; not an extended key
        jnz .chr
        mov bl, dl
        and bl, 7Fh
        cmp bl, 47h
        jb .chr
        cmp bl, 52h
        ja .chr
        sub bl, 47h
        push ds
        mov cx, seg pad_digit
        mov ds, cx
        mov al, bl
        mov bx, pad_digit
        xlatb
        pop ds
        cmp al, 0FFh
        je .chr
        ; a digit
        mov cx, DGROUP
        push ds
        mov ds, cx
        cmp [alt_last wrt DGROUP], dl
        je .rep
        mov [alt_last wrt DGROUP], dl
        mov bl, al
        mov al, [es:di]
        or al, al
        jnz .acc
        or bl, bl
        jnz .acc
        mov byte [alt_ansi wrt DGROUP], 1
.acc:   mov ah, 10
        mul ah
        add al, bl
        xor ah, ah
        mov [es:di], ax
        pop ds
        xor ax, ax
        jmp .r
.rep:   pop ds
        xor ax, ax
        jmp .r
.chr:   ; Alt down starts a new Alt+keypad number
        cmp al, VK_MENU
        jne .c1
        push ds
        mov cx, DGROUP
        mov ds, cx
        mov byte [alt_ansi wrt DGROUP], 0
        mov byte [alt_last wrt DGROUP], 0
        pop ds
.c1:    call vk_char                    ; AL = VK -> AL char, ZF if none
        jz .none
        mov [es:di], al
        mov ax, 1
        jmp .r
.none:  call lights
        cmp byte [bp + 18], VK_NUMLOCK
        jne .zero_w
        call lights
.zero_w:
        mov word [es:di], 0
.zero_nw:
        xor ax, ax
.r:     pop di
        pop si
        pop ds
        pop bp
        retf 14

; lights: the lock bits of the BIOS keyboard flags from the key-state
; array at DS:SI (VK_CAPITAL, VK_NUMLOCK, VK_SCROLL toggled), then INT
; 16h AH=01h, which has the BIOS set the keyboard's lights from them
; (DDK, ToAscii; KB2 sees the call)
lights:
        push ax
        push es
        mov ax, __0040H
        mov es, ax
        mov ah, [es:KB_FLAG]
        and ah, ~(F_CAPS | F_NUM | F_SCROLL)
        test byte [si + VK_CAPITAL], 1
        jz .c
        or ah, F_CAPS
.c:     test byte [si + VK_NUMLOCK], 1
        jz .n
        or ah, F_NUM
.n:     test byte [si + VK_SCROLL], 1
        jz .s
        or ah, F_SCROLL
.s:     mov [es:KB_FLAG], ah
        pop es
        mov ah, 1
        int 16h
        pop ax
        ret

; vk_char: AL = VK, DS:SI the key-state array -> AL the character for
; the shift state, ZF clear; ZF set if none. Ctrl+Alt: none; Alt alone
; changes nothing; Caps Lock affects the letters only; Shift+Ctrl
; gives the Ctrl table's entries with bit 7 (as 00h, 1Eh, 1Fh) and the
; letters; Ctrl alone its other entries and the letters (KB1).
vk_char:
        push bx
        push cx
        push si
        push ds
        mov bl, [si + VK_SHIFT]
        and bl, 80h                     ; BL: Shift
        mov bh, [si + VK_CONTROL]
        and bh, 80h                     ; BH: Ctrl
        mov cl, [si + VK_CAPITAL]
        mov ch, [si + VK_MENU]
        mov dx, seg plain_tab
        mov ds, dx
        or bh, bh
        jz .noctrl
        test ch, 80h
        jnz .none                       ; Ctrl+Alt: nothing (US)
        cmp al, 'A'
        jb .ct
        cmp al, 'Z'
        ja .ct
        sub al, 40h
        jmp .yes
.ct:    mov si, ctrl_tab
.cl:    mov ah, [si]
        or ah, ah
        jz .none
        cmp ah, al
        jne .cn
        mov ah, [si + 1]
        test ah, 80h                    ; a Shift+Ctrl entry?
        jz .c0
        or bl, bl
        jz .cn
        and ah, 7Fh
        mov al, ah
        jmp .yes
.c0:    or bl, bl
        jnz .cn
        mov al, ah
        jmp .yes
.cn:    add si, 2
        jmp .cl
.noctrl:
        cmp al, 'A'
        jb .pl
        cmp al, 'Z'
        ja .pl
        and cl, 1                       ; Caps Lock on
        jz .lc
        xor bl, 80h
.lc:    or bl, bl
        jnz .yes                        ; upper case
        add al, 20h
        jmp .yes
.pl:    cmp al, VK_NUMPAD0
        jb .pt
        cmp al, VK_NUMPAD0 + 9
        ja .pt
        sub al, VK_NUMPAD0 - '0'
        jmp .yes
.pt:    mov si, plain_tab
.pp:    mov ah, [si]
        or ah, ah
        jz .none
        cmp ah, al
        je .pf
        add si, 3
        jmp .pp
.pf:    mov al, [si + 1]
        or bl, bl
        jz .yes
        mov al, [si + 2]
.yes:   or sp, sp                       ; ZF clear
        jmp .r
.none:  cmp al, al                      ; ZF set
.r:     pop ds
        pop si
        pop cx
        pop bx
        ret

; ---- 126: GetTableSeg(void): the selector of the table segment (KB1:
; the segment GetTableSeg itself is in, and no other entry) ----
segment KBTAB
global GETTABLESEG
GETTABLESEG:
        mov ax, cs
        retf

; scan code -> VK (scan-code set 1, keys of the US enhanced keyboard; 0
; for none: 00h, 54h SysReq, 55h). The keypad's keys are the cursor keys
; here; Num Lock makes them digits (isr9).
scan_vk:
        db 0, VK_ESCAPE, '1', '2', '3', '4', '5', '6'          ; 00
        db '7', '8', '9', '0', VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK, VK_TAB
        db 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I'              ; 10
        db 'O', 'P', VK_OEM_4, VK_OEM_6, VK_RETURN, VK_CONTROL, 'A', 'S'
        db 'D', 'F', 'G', 'H', 'J', 'K', 'L', VK_OEM_1         ; 20
        db VK_OEM_7, VK_OEM_3, VK_SHIFT, VK_OEM_5, 'Z', 'X', 'C', 'V'
        db 'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2, VK_SHIFT, VK_MULTIPLY ; 30
        db VK_MENU, VK_SPACE, VK_CAPITAL, VK_F1, VK_F1 + 1, VK_F1 + 2, VK_F1 + 3, VK_F1 + 4
        db VK_F1 + 5, VK_F1 + 6, VK_F1 + 7, VK_F1 + 8, VK_F1 + 9, VK_NUMLOCK, VK_SCROLL, VK_HOME ; 40
        db VK_UP, VK_PRIOR, VK_SUBTRACT, VK_LEFT, VK_CLEAR, VK_RIGHT, VK_ADD, VK_END
        db VK_DOWN, VK_NEXT, VK_INSERT, VK_DELETE, 0, 0, VK_OEM_102, VK_F1 + 10 ; 50
        db VK_F1 + 11                                           ; 58

; the keypad, scan codes 47h-53h: the digit (FFh: not a digit key)
pad_digit:
        db 7, 8, 9, 0FFh, 4, 5, 6, 0FFh, 1, 2, 3, 0
; ... and with Num Lock on, the VKs (47h-53h)
pad_numvk:
        db VK_NUMPAD0 + 7, VK_NUMPAD0 + 8, VK_NUMPAD0 + 9, VK_SUBTRACT
        db VK_NUMPAD0 + 4, VK_NUMPAD0 + 5, VK_NUMPAD0 + 6, VK_ADD
        db VK_NUMPAD0 + 1, VK_NUMPAD0 + 2, VK_NUMPAD0 + 3, VK_NUMPAD0, VK_DECIMAL
; the scan code of keypad digit n
pad_digit_scan:
        db 52h, 4Fh, 50h, 51h, 4Bh, 4Ch, 4Dh, 47h, 48h, 49h

; E0-prefixed scan code -> VK (the enhanced keyboard's extra keys); the
; others as without the prefix
ext_vk:
        db 1Ch, VK_RETURN
        db 1Dh, VK_CONTROL
        db 35h, VK_DIVIDE
        db 37h, VK_SNAPSHOT
        db 38h, VK_MENU
        db 46h, VK_CANCEL               ; Ctrl+Break
        db 47h, VK_HOME
        db 48h, VK_UP
        db 49h, VK_PRIOR
        db 4Bh, VK_LEFT
        db 4Dh, VK_RIGHT
        db 4Fh, VK_END
        db 50h, VK_DOWN
        db 51h, VK_NEXT
        db 52h, VK_INSERT
        db 53h, VK_DELETE
        db 0

; VK, unshifted, shifted character (US layout; the order is VkKeyScan's
; search order). The letters are handled apart.
plain_tab:
        db VK_CANCEL, 03h, 03h
        db VK_BACK, 08h, 08h
        db VK_TAB, 09h, 09h
        db VK_RETURN, 0Dh, 0Dh
        db VK_ESCAPE, 1Bh, 1Bh
        db VK_SPACE, ' ', ' '
        db '0', '0', ')'
        db '1', '1', '!'
        db '2', '2', '@'
        db '3', '3', '#'
        db '4', '4', '$'
        db '5', '5', '%'
        db '6', '6', '^'
        db '7', '7', '&'
        db '8', '8', '*'
        db '9', '9', '('
        db VK_MULTIPLY, '*', '*'
        db VK_ADD, '+', '+'
        db VK_SUBTRACT, '-', '-'
        db VK_DECIMAL, '.', '.'
        db VK_DIVIDE, '/', '/'
        db VK_OEM_1, ';', ':'
        db VK_OEM_PLUS, '=', '+'
        db VK_OEM_COMMA, ',', '<'
        db VK_OEM_MINUS, '-', '_'
        db VK_OEM_PERIOD, '.', '>'
        db VK_OEM_2, '/', '?'
        db VK_OEM_3, '`', '~'
        db VK_OEM_4, '[', '{'
        db VK_OEM_5, '\', '|'
        db VK_OEM_6, ']', '}'
        db VK_OEM_7, "'", '"'
        db VK_OEM_102, '\', '|'
        db 0

; VK, character with Ctrl (bit 7: with Shift+Ctrl, the character in bits
; 0-6). The letters are handled apart.
ctrl_tab:
        db VK_CANCEL, 03h
        db VK_BACK, 7Fh
        db VK_RETURN, 0Ah
        db VK_ESCAPE, 1Bh
        db VK_SPACE, ' '
        db VK_OEM_4, 1Bh
        db VK_OEM_5, 1Ch
        db VK_OEM_6, 1Dh
        db VK_OEM_102, 1Ch
        db '2', 80h
        db '6', 9Eh
        db VK_OEM_MINUS, 9Fh
        db 0

; key names (GetKeyNameText): scan code, extended, the name (UI text, as
; Windows 3.1 names its keys); FFh ends the list
names:
        db 01h, 0, 'Esc', 0
        db 0Eh, 0, 'Backspace', 0
        db 0Fh, 0, 'Tab', 0
        db 1Ch, 0, 'Enter', 0
        db 1Dh, 0, 'Ctrl', 0
        db 2Ah, 0, 'Shift', 0
        db 36h, 0, 'Right Shift', 0
        db 37h, 0, 'Num *', 0
        db 38h, 0, 'Alt', 0
        db 39h, 0, 'Space', 0
        db 3Ah, 0, 'Caps Lock', 0
        db 3Bh, 0, 'F1', 0
        db 3Ch, 0, 'F2', 0
        db 3Dh, 0, 'F3', 0
        db 3Eh, 0, 'F4', 0
        db 3Fh, 0, 'F5', 0
        db 40h, 0, 'F6', 0
        db 41h, 0, 'F7', 0
        db 42h, 0, 'F8', 0
        db 43h, 0, 'F9', 0
        db 44h, 0, 'F10', 0
        db 45h, 0, 'Pause', 0
        db 46h, 0, 'Scroll Lock', 0
        db 47h, 0, 'Num 7', 0
        db 48h, 0, 'Num 8', 0
        db 49h, 0, 'Num 9', 0
        db 4Ah, 0, 'Num -', 0
        db 4Bh, 0, 'Num 4', 0
        db 4Ch, 0, 'Num 5', 0
        db 4Dh, 0, 'Num 6', 0
        db 4Eh, 0, 'Num +', 0
        db 4Fh, 0, 'Num 1', 0
        db 50h, 0, 'Num 2', 0
        db 51h, 0, 'Num 3', 0
        db 52h, 0, 'Num 0', 0
        db 53h, 0, 'Num Del', 0
        db 54h, 0, '<00>', 0
        db 57h, 0, 'F11', 0
        db 58h, 0, 'F12', 0
        db 60h, 0, 'F11', 0             ; (scan codes some keyboards give
        db 61h, 0, 'F12', 0             ; their F11-F18)
        db 62h, 0, 'F13', 0
        db 63h, 0, 'F14', 0
        db 64h, 0, 'F15', 0
        db 65h, 0, 'F16', 0
        db 66h, 0, 'F17', 0
        db 67h, 0, 'F18', 0
        db 01h, 1, 'Prnt Scrn', 0
        db 0Eh, 1, 0
        db 0Fh, 1, 0
        db 1Ch, 1, 'Num Enter', 0
        db 1Dh, 1, 'Right Ctrl', 0
        db 35h, 1, 'Num /', 0
        db 37h, 1, 0
        db 38h, 1, 'Right Alt', 0
        db 39h, 1, 0
        db 45h, 1, 'Num Lock', 0
        db 46h, 1, 'Break', 0
        db 47h, 1, 'Home', 0
        db 48h, 1, 'Up', 0
        db 49h, 1, 'Page Up', 0
        db 4Ah, 1, 0
        db 4Bh, 1, 'Left', 0
        db 4Dh, 1, 'Right', 0
        db 4Eh, 1, 0
        db 4Fh, 1, 'End', 0
        db 50h, 1, 'Down', 0
        db 51h, 1, 'Page Down', 0
        db 52h, 1, 'Insert', 0
        db 53h, 1, 'Delete', 0
        db 54h, 1, '<00>', 0
        db 5Ch, 1, 'Clear', 0
        db 0FFh

segment _TEXT

; ======================== interrupt time ========================

; isr9: IRQ 1. Interrupts are off on entry.
isr9:
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
        in al, 60h
        ; the controller's answers (ACK, resend, echo, overrun) are the
        ; BIOS's (DDK: "a command byte"): the BIOS waits for them
        cmp al, 0FAh
        je .bios
        cmp al, 0FEh
        je .bios
        cmp al, 0EEh
        je .bios
        cmp al, 0FFh
        je .bios
        cmp al, 0
        je .bios
        cmp al, 0E0h
        jne .ne0
        mov byte [prefix wrt DGROUP], 0E0h
        jmp .eoi
.ne0:   cmp al, 0E1h
        jne .ne1
        mov byte [e1_state wrt DGROUP], 1
        jmp .eoi
.ne1:   mov ah, [prefix wrt DGROUP]
        mov byte [prefix wrt DGROUP], 0
        cmp byte [e1_state wrt DGROUP], 0
        je .key
        ; Pause: E1 1D 45 E1 9D C5
        cmp byte [e1_state wrt DGROUP], 1
        jne .e12
        mov byte [e1_state wrt DGROUP], 2
        jmp .eoi
.e12:   mov byte [e1_state wrt DGROUP], 0
        mov bl, al
        and bl, 7Fh
        mov cl, al
        and cl, 80h                     ; up?
        mov al, VK_PAUSE
        mov bh, 0
        call eoi
        call event
        jmp .ret
.key:   ; Ctrl+Alt+Del: the BIOS's (DDK)
        cmp al, SC_DEL
        jne .k1
        push es
        mov dx, __0040H
        mov es, dx
        mov dl, [es:KB_FLAG]
        pop es
        and dl, F_CTRL | F_ALT
        cmp dl, F_CTRL | F_ALT
        je .bios
.k1:    call eoi
        call key
        jmp .ret
.bios:  ; on to the handler before ours, as an interrupt
        pushf
        call far [old9 wrt DGROUP]
        jmp .ret
.eoi:   call eoi
.ret:   pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        iret

eoi:
        push ax
        mov al, 20h
        out 20h, al
        pop ax
        ret

; key: AL = scan code byte (bit 7 up), AH = E0h after the prefix. DS =
; DGROUP, interrupts off, the interrupt ended.
key:
        mov bl, al
        and bl, 7Fh                     ; BL scan code
        mov cl, al
        and cl, 80h                     ; CL 80h up
        xor bh, bh
        mov dx, __0040H
        mov es, dx
        or ah, ah
        jz .plain
        ; E0: the fake Shifts some keyboards send around the cursor keys
        cmp bl, SC_LSHIFT
        je .ignore
        cmp bl, SC_RSHIFT
        je .ignore
        mov bh, 1
        push ds
        mov dx, seg ext_vk
        mov ds, dx
        mov si, ext_vk
.x:     mov al, [si]
        or al, al
        jz .xn
        cmp al, bl
        je .xf
        add si, 2
        jmp .x
.xf:    mov al, [si + 1]
        pop ds
        jmp .vk
.xn:    pop ds
        jmp .tab
.plain: cmp bl, SC_NUM                  ; Num Lock: reported extended
        jne .tab
        mov bh, 1
.tab:   cmp bl, SC_LAST
        ja .ignore
        push ds
        mov dx, seg scan_vk
        mov ds, dx
        mov al, bl
        mov si, scan_vk
        push bx
        mov bx, si
        xlatb
        pop bx
        pop ds
        or al, al
        jz .sysreq
        ; the keypad (not E0): digits with Num Lock on, unless Shift
        or bh, bh
        jnz .vk
        cmp bl, 47h
        jb .vk
        cmp bl, 53h
        ja .vk
        cmp bl, 4Ah
        je .vk
        cmp bl, 4Eh
        je .vk
        test byte [es:KB_FLAG], F_NUM
        jz .vk
        test byte [es:KB_FLAG], F_LSHIFT | F_RSHIFT
        jnz .padshift
        push ds
        mov dx, seg pad_numvk
        mov ds, dx
        mov al, bl
        sub al, 47h
        push bx
        mov bx, pad_numvk
        xlatb
        pop bx
        pop ds
        jmp .vk
.padshift:
        ; Shift held with Num Lock on: the cursor key, Shift reported up
        ; first (KB2: VK_SHIFT up with scan code 36h, then the key)
        or cl, cl
        jnz .vk
        cmp byte [fake_shift wrt DGROUP], 0
        jne .vk
        mov byte [fake_shift wrt DGROUP], 1
        push ax
        push bx
        push cx
        mov al, VK_SHIFT
        mov bl, SC_RSHIFT
        xor bh, bh
        mov cl, 80h
        call event
        pop cx
        pop bx
        pop ax
        jmp .vk
.sysreq:
        cmp bl, SC_SYSREQ
        jne .ignore
        ; Alt+Print Screen (the enhanced keyboard's 54h): VK_SNAPSHOT, BX
        ; 0 (the active window; DDK, "Keyboard-Interrupt Handler").
        ; UNVERIFIED: DOSBox under the harness sends no Print Screen.
        test byte [es:KB_FLAG], F_CTRL
        jnz .sr
        test byte [es:KB_FLAG], F_ALT
        jz .sr
        mov al, VK_SNAPSHOT
        xor bx, bx
        jmp event
.sr:
        ; Ctrl+Alt+SysReq: the debugger (EnableKBSysReq), on the make
        or cl, cl
        jnz .ignore
        mov dl, [es:KB_FLAG]
        and dl, F_CTRL | F_ALT
        cmp dl, F_CTRL | F_ALT
        jne .ignore
        test byte [sysreq wrt DGROUP], 2
        jz .i2
        sti
        call far CVWBREAK
        ret
.i2:    test byte [sysreq wrt DGROUP], 1
        jz .ignore
        sti
        int 2
.ignore:
        ret
.vk:    ; the BIOS flags, then the event
        cmp al, VK_SHIFT
        je .shift
        ; a Shift reported up for the keypad comes back with the next
        ; key that is not a keypad cursor key
        cmp byte [fake_shift wrt DGROUP], 0
        je .nofake
        cmp bl, 47h
        jb .unfake
        cmp bl, 53h
        jbe .nofake
.unfake:
        call restore_shift
.nofake:
        cmp al, VK_SNAPSHOT
        jne .nosnap
        mov bx, 1                       ; the whole screen (DDK; UNVERIFIED)
        jmp event
.nosnap:
        cmp al, VK_CONTROL
        je .ctrl
        cmp al, VK_MENU
        je .alt
        cmp al, VK_NUMLOCK
        je .num
        cmp al, VK_CANCEL
        je .brk
        jmp event
.shift: cmp byte [fake_shift wrt DGROUP], 0
        je .sh1
        call restore_shift
.sh1:   or cl, cl
        jnz .shup
        mov [shift_scan wrt DGROUP], bl
        mov dl, F_LSHIFT
        cmp bl, SC_LSHIFT
        je .shd
        mov dl, F_RSHIFT
.shd:   or [es:KB_FLAG], dl
        jmp event
.shup:  ; either Shift up: both are up for Windows (KB2: reported with the
        ; scan code of the last Shift pressed; the other's release then
        ; reports nothing)
        test byte [es:KB_FLAG], F_LSHIFT | F_RSHIFT
        jz .ignore
        and byte [es:KB_FLAG], ~(F_LSHIFT | F_RSHIFT)
        mov bl, [shift_scan wrt DGROUP]
        jmp event
.ctrl:  or cl, cl
        jnz .cup
        or byte [es:KB_FLAG], F_CTRL
        or bh, bh
        jnz event
        or byte [es:KB_FLAG1], F1_LCTRL
        jmp event
.cup:   and byte [es:KB_FLAG], ~F_CTRL
        or bh, bh
        jnz event
        and byte [es:KB_FLAG1], ~F1_LCTRL
        jmp event
.alt:   or cl, cl
        jnz .aup
        or byte [es:KB_FLAG], F_ALT
        or bh, bh
        jnz event
        or byte [es:KB_FLAG1], F1_LALT
        jmp event
.aup:   and byte [es:KB_FLAG], ~F_ALT
        or bh, bh
        jnz event
        and byte [es:KB_FLAG1], ~F1_LALT
        jmp event
.num:   or cl, cl
        jnz .nup
        test byte [es:KB_FLAG1], F1_NUMDOWN
        jnz .nrep
        xor byte [es:KB_FLAG], F_NUM
.nrep:  or byte [es:KB_FLAG1], F1_NUMDOWN
        jmp event
.nup:   and byte [es:KB_FLAG1], ~F1_NUMDOWN
        jmp event
.brk:   ; Ctrl+Break: the event, then the focus task's signal (KERNEL's
        ; DoSignal, Schulman ch. 5)
        call event
        or cl, cl
        jnz .ignore
        test byte [es:KB_FLAG], F_CTRL
        jz .ignore
        sti
        call far DOSIGNAL
        cli
        ret

; restore_shift: the Shift reported up for the keypad down again (KB2:
; VK_SHIFT, scan code 36h, interrupts off)
restore_shift:
        push ax
        push bx
        push cx
        mov byte [fake_shift wrt DGROUP], 0
        mov al, VK_SHIFT
        mov bl, SC_RSHIFT
        xor bh, bh
        xor cl, cl
        call event_cli
        pop cx
        pop bx
        pop ax
        ret

; event: USER's event procedure with AL = VK, AH = CL (0 down, 80h up),
; BL = scan code, BH = extended; interrupts on during the call, IRQ 1
; masked at the PIC meanwhile (one key at a time into USER: DDK).
; event_cli: the same with interrupts left off.
event:
        cmp byte [enabled wrt DGROUP], 0
        je .r
        pusha
        mov ah, cl
        push ax
        in al, 21h
        mov [pic_mask wrt DGROUP], al
        or al, 2
        out 21h, al
        pop ax
        sti
        call far [event_proc wrt DGROUP]
        cli
        mov dx, DGROUP
        mov ds, dx
        in al, 21h
        and al, ~2
        mov ah, [pic_mask wrt DGROUP]
        and ah, 2
        or al, ah
        out 21h, al
        mov dx, __0040H
        mov es, dx
        popa
.r:     ret

event_cli:
        cmp byte [enabled wrt DGROUP], 0
        je .r
        pusha
        mov ah, cl
        call far [event_proc wrt DGROUP]
        mov dx, DGROUP
        mov ds, dx
        mov dx, __0040H
        mov es, dx
        popa
.r:     ret
