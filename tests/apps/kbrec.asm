; kbrec.asm - KBREC.DRV: a recording keyboard driver that stands in front
; of another one (black-box observation of KEYBOARD.DRV,
; drivers/keyboard/README.md; the mouse work's MS3A did the same).
;
; Installed as SYSTEM\KEYBOARD.DRV (module KEYBOARD). It imports module
; KBTARGET: a scratch copy of the driver under test (Windows 3.1's
; KEYBOARD.DRV or w16's, module name renamed in its NE header by
; tools/nerename.py; file SYSTEM\KBTARGET.DLL). Every call of an exported
; ordinal is recorded in a ring in KBREC's fixed data segment (no I/O: a
; caller may be at interrupt time) and passed on to the same ordinal of
; KBTARGET with the caller's registers and a copy of the top 8 stack words.
; As many bytes as the real entry took off are taken off the caller's
; stack. Enable's event procedure is replaced by KBREC's own, which
; records every call the driver makes to it (registers, flags, the BIOS
; keyboard flags) and passes it on to the procedure Enable was given,
; registers unchanged. WEP (8) is answered here.
;
; Only values are recorded: registers and stack words. Nothing behind a
; pointer argument is ever read (that could copy Microsoft code or data).
;
; A record (REC = 48 bytes):
;   +0 kind: the ordinal for a call, E0h for an event-procedure call
;   +2 sequence number (calls and events share it)
;   +4 AX BX CX DX SI DI (+4..+15), +16 flags, +18 DS, +20 ES, +22 SS
;   calls:  +24..+39 the 8 stack words above the return address (w0 at
;           +24 is the last-pushed argument), +40 AX out, +42 DX out,
;           +44 flags out, +46 bytes taken off
;   events: +24 BIOS 40:17h/18h, +26 40:96h/97h, +28 nesting depth,
;           +30 BIOS tick count (low word), +32 caller IP, +34 caller CS
; Ordinal 200 (KBRLOG) returns DX:AX = the log header: count, max, REC,
; total, then the records at header + 16.
        bits 16
        cpu 286

REC     equ 48
MAXREC  equ 1300

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern __0040H                          ; KERNEL.193

segment _DATA
hdr:
count   dw 0
max     dw MAXREC
recsz   dw REC
total   dw 0
userproc dd 0                           ; the event procedure Enable got
depth   dw 0
        dw 0
recs:   times MAXREC * REC db 0

segment _TEXT
global libentry
libentry:
        mov ax, 1
        retf

%macro thunk 1
global T_%1
T_%1:
        push word %1
        jmp thcommon
%endmacro
thunk 1
thunk 2
thunk 3
thunk 4
thunk 5
thunk 6
thunk 7
thunk 100
thunk 126
thunk 127
thunk 128
thunk 129
thunk 130
thunk 131
thunk 132
thunk 133
thunk 134
thunk 135
thunk 136
thunk 137

extern X_1, X_2, X_3, X_4, X_5, X_6, X_7, X_100, X_126, X_127, X_128
extern X_129, X_130, X_131, X_132, X_133, X_134, X_135, X_136, X_137

; ordinal -> target
targets:
        dw 1
        dd X_1
        dw 2
        dd X_2
        dw 3
        dd X_3
        dw 4
        dd X_4
        dw 5
        dd X_5
        dw 6
        dd X_6
        dw 7
        dd X_7
        dw 100
        dd X_100
        dw 126
        dd X_126
        dw 127
        dd X_127
        dw 128
        dd X_128
        dw 129
        dd X_129
        dw 130
        dd X_130
        dw 131
        dd X_131
        dw 132
        dd X_132
        dw 133
        dd X_133
        dw 134
        dd X_134
        dw 135
        dd X_135
        dw 136
        dd X_136
        dw 137
        dd X_137
        dw 0

global WEP
WEP:
        mov ax, 1
        retf 2

global KBRLOG
KBRLOG:
        mov ax, hdr
        mov dx, seg hdr
        retf

; newrec: DS = DGROUP; DI = a new record (zeroed kind) or 0 when full;
; AX = its sequence number. Interrupts off while it is taken.
newrec:
        pushf
        cli
        inc word [total wrt DGROUP]
        mov ax, [total wrt DGROUP]
        mov di, [count wrt DGROUP]
        cmp di, MAXREC
        jae .full
        inc word [count wrt DGROUP]
        push ax
        push dx
        mov ax, REC
        mul di
        add ax, recs
        mov di, ax
        pop dx
        pop ax
        popf
        ret
.full:  xor di, di
        popf
        ret

; [bp+2] ordinal, [bp+4] return IP, [bp+6] return CS, [bp+8..] w0..w7
thcommon:
        push bp
        mov bp, sp
        sub sp, 6                       ; [bp-4] target offset, [bp-2] seg,
                                        ; [bp-6] record
        pushf
        pusha
        push ds
        push es
        ; [bp-8] flags, [bp-10] AX, -12 CX, -14 DX, -16 BX, -18 SP, -20 BP,
        ; -22 SI, -24 DI, -26 DS, -28 ES
        cld
        mov ax, seg hdr
        mov ds, ax
        mov bx, targets
.find:  mov ax, [cs:bx]
        cmp ax, [bp + 2]
        je .found
        add bx, 6
        jmp .find
.found: mov ax, [cs:bx + 2]
        mov [bp - 4], ax
        mov ax, [cs:bx + 4]
        mov [bp - 2], ax
        call newrec
        mov [bp - 6], di
        or di, di
        jz .norec
        push ds
        pop es
        mov si, ax                      ; sequence
        mov ax, [bp + 2]
        stosw                           ; +0 kind
        mov ax, si
        stosw                           ; +2 seq
        mov ax, [bp - 10]
        stosw                           ; +4 AX
        mov ax, [bp - 16]
        stosw                           ; BX
        mov ax, [bp - 12]
        stosw                           ; CX
        mov ax, [bp - 14]
        stosw                           ; DX
        mov ax, [bp - 22]
        stosw                           ; SI
        mov ax, [bp - 24]
        stosw                           ; DI
        mov ax, [bp - 8]
        stosw                           ; +16 flags
        mov ax, [bp - 26]
        stosw                           ; +18 DS
        mov ax, [bp - 28]
        stosw                           ; +20 ES
        mov ax, ss
        stosw                           ; +22 SS
        mov cx, 8
        lea si, [bp + 8]
.args:  mov ax, [ss:si]
        stosw                           ; +24..+39
        add si, 2
        loop .args
.norec: ; Enable: the event procedure becomes ours
        cmp word [bp + 2], 2
        jne .noen
        mov ax, [bp + 12]
        mov [userproc wrt DGROUP], ax
        mov ax, [bp + 14]
        mov [userproc + 2 wrt DGROUP], ax
.noen:  pop es
        pop ds
        popa
        popf
        ; call the real entry with a copy of the 8 words
        push word [bp + 22]
        push word [bp + 20]
        push word [bp + 18]
        push word [bp + 16]
        cmp word [bp + 2], 2
        jne .plain
        push cs
        push word event
        jmp .rest
.plain: push word [bp + 14]
        push word [bp + 12]
.rest:  push word [bp + 10]
        push word [bp + 8]
        push cs
        push word back
        push word [bp - 2]
        push word [bp - 4]
        retf
back:
        pushf
        pusha
        push ds
        push es
        ; [sp] ES, +2 DS, +4 DI, +6 SI, +8 BP, +10 SP, +12 BX, +14 DX,
        ; +16 CX, +18 AX, +20 flags
        cld
        mov si, sp
        mov cx, sp
        sub cx, bp
        add cx, 44                      ; bytes the real entry took off
        mov ax, seg hdr
        mov ds, ax
        mov di, [bp - 6]
        or di, di
        jz .norec2
        mov ax, [ss:si + 18]
        mov [di + 40], ax
        mov ax, [ss:si + 14]
        mov [di + 42], ax
        mov ax, [ss:si + 20]
        mov [di + 44], ax
        mov [di + 46], cx
.norec2:
        ; return address to [bp+4+n]; the caller's BP to [bp+2+n]; the
        ; new SP to [bp-4]
        mov di, bp
        add di, cx
        mov ax, [bp + 4]
        mov dx, [bp + 6]
        mov [ss:di + 4], ax
        mov [ss:di + 6], dx
        mov ax, [bp]
        mov [ss:di + 2], ax
        add di, 2
        mov [bp - 4], di
        pop es
        pop ds
        popa
        popf
        mov sp, [bp - 4]
        pop bp
        retf

; The event procedure KBTARGET is given: record, then on to the real one
; with every register as it came, then back to KBTARGET.
event:
        push cs
        push word .ret                  ; where the real procedure returns
        sub sp, 4                       ; its address, filled in below
        push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        ; [bp-2] flags, [bp-4] AX, -6 CX, -8 DX, -10 BX, -12 SP, -14 BP,
        ; -16 SI, -18 DI, -20 DS, -22 ES; [bp+2] the procedure's offset,
        ; [bp+4] its segment, [bp+6] .ret, [bp+8] CS, [bp+10] caller IP,
        ; [bp+12] caller CS
        cld
        mov ax, seg hdr
        mov ds, ax
        mov ax, [userproc wrt DGROUP]
        mov [bp + 2], ax
        mov ax, [userproc + 2 wrt DGROUP]
        mov [bp + 4], ax
        inc word [depth wrt DGROUP]
        call newrec
        or di, di
        jz .full
        push ds
        pop es
        mov si, ax
        mov ax, 0E0h
        stosw                           ; +0 kind
        mov ax, si
        stosw                           ; +2 seq
        mov ax, [bp - 4]
        stosw                           ; AX
        mov ax, [bp - 10]
        stosw                           ; BX
        mov ax, [bp - 6]
        stosw                           ; CX
        mov ax, [bp - 8]
        stosw                           ; DX
        mov ax, [bp - 16]
        stosw                           ; SI
        mov ax, [bp - 18]
        stosw                           ; DI
        mov ax, [bp - 2]
        stosw                           ; +16 flags
        mov ax, [bp - 20]
        stosw                           ; +18 DS
        mov ax, [bp - 22]
        stosw                           ; +20 ES
        mov ax, ss
        stosw                           ; +22 SS
        push ds
        mov ax, __0040H
        mov ds, ax
        mov ax, [17h]
        mov bx, [96h]
        mov cx, [6Ch]
        pop ds
        stosw                           ; +24 40:17h/18h
        mov ax, bx
        stosw                           ; +26 40:96h/97h
        mov ax, [depth wrt DGROUP]
        stosw                           ; +28 depth
        mov ax, cx
        stosw                           ; +30 tick
        mov ax, [bp + 10]
        stosw                           ; +32 caller IP
        mov ax, [bp + 12]
        stosw                           ; +34 caller CS
.full:  pop es
        pop ds
        popa
        popf
        pop bp
        retf                            ; to the real procedure
.ret:   pushf
        push ds
        push ax
        mov ax, seg hdr
        mov ds, ax
        dec word [depth wrt DGROUP]
        pop ax
        pop ds
        popf
        retf
