; ms3a.asm - MS3A.DRV: a recording mouse driver that stands in front of
; another one (black-box observation of MOUSE.DRV's undocumented entries
; GetSetMouseData (5) and PowerEventProc (7), tests/apps/md2logs/a/notes.md).
;
; Installed as SYSTEM\MOUSE.DRV (module MOUSE). It imports module MOUSX:
; a scratch copy of the real driver (Windows 3.1's MOUSE.DRV, module name
; renamed in its NE header by tools/nerename.py; file SYSTEM\MOUSX.DLL).
; Every call of ordinals 1-7 is recorded in a ring in MS3's fixed data
; segment (no I/O: a caller may be at interrupt time) and passed to the
; same ordinal of MOUSX with the caller's registers and a copy of the top
; 8 stack words (16 bytes; Windows 3.1's 5, 6 and 7 take 8, 12 and 6). As
; many bytes as the real entry took off are taken off the caller's stack.
; WEP (8) is answered here (MOUSX gets its own WEP from KERNEL).
;
; A record (REC bytes):
;   +0 ordinal, +2 caller IP, +4 caller CS, +6..+21 entry AX BX CX DX SI
;   DI DS ES, +22..+37 the 8 stack words above the return address (w0 at
;   +22 is the last-pushed argument), +38 AX out, +40 DX out, +42 flags
;   out, +44 bytes taken off, +46 sequence number, +48 mask of the word
;   pairs (w[i+1]:w[i], i = 0..3) that were readable far pointers to 32
;   bytes, +50 BX out, +52 CX out, +54 ES out,
;   +64 + 64*i: 32 bytes at pair i before the call, then 32 bytes after.
; Ordinal 20 (MS3LOG) returns DX:AX = the log header: count, max, REC,
; total calls, then the records at header + 16.
        bits 16
        cpu 286

REC     equ 320
MAXREC  equ 96

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

segment _DATA
hdr:
count   dw 0
max     dw MAXREC
recsz   dw REC
total   dw 0
cur     dw 0                    ; the record of the call in progress
        dw 0, 0, 0
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

global WEP
WEP:
        mov ax, 1
        retf 2

global MS3LOG
MS3LOG:
        mov ax, hdr
        mov dx, seg hdr
        retf

; [bp+2] ordinal, [bp+4] return IP, [bp+6] return CS, [bp+8..] w0..w7
thcommon:
        push bp
        mov bp, sp
        sub sp, 4                       ; [bp-4] target offset, [bp-2] segment
        pushf
        pusha
        push ds
        push es
        ; [bp-6] flags, [bp-8] AX, -10 CX, -12 DX, -14 BX, -16 SP, -18 BP,
        ; -20 SI, -22 DI, -24 DS, -26 ES
        cld
        mov ax, seg hdr
        mov ds, ax
        mov bx, [bp + 2]
        dec bx
        shl bx, 2
        mov ax, [cs:targets + bx]
        mov [bp - 4], ax
        mov ax, [cs:targets + bx + 2]
        mov [bp - 2], ax
        inc word [total wrt DGROUP]
        mov word [cur wrt DGROUP], 0
        mov di, [count wrt DGROUP]
        cmp di, MAXREC
        jae .norec
        inc word [count wrt DGROUP]
        mov ax, REC
        mul di
        add ax, recs
        mov di, ax
        mov [cur wrt DGROUP], di
        push ds
        pop es
        mov ax, [bp + 2]
        stosw                           ; +0
        mov ax, [bp + 4]
        stosw                           ; +2
        mov ax, [bp + 6]
        stosw                           ; +4
        mov ax, [bp - 8]
        stosw                           ; +6 AX
        mov ax, [bp - 14]
        stosw                           ; BX
        mov ax, [bp - 10]
        stosw                           ; CX
        mov ax, [bp - 12]
        stosw                           ; DX
        mov ax, [bp - 20]
        stosw                           ; SI
        mov ax, [bp - 22]
        stosw                           ; DI
        mov ax, [bp - 24]
        stosw                           ; DS
        mov ax, [bp - 26]
        stosw                           ; ES
        mov cx, 8
        lea si, [bp + 8]
.args:  mov ax, [ss:si]
        stosw                           ; +22..+37
        add si, 2
        loop .args
        mov ax, [total wrt DGROUP]
        mov [di + 46 - 38], ax          ; +46 sequence
        ; pointers: before
        mov si, [cur wrt DGROUP]
        xor dx, dx                      ; mask
        xor bx, bx                      ; pair index
.pb:    call pairok
        jc .pbn
        or dl, [cs:pbits + bx]
        push si
        push di
        push ds
        mov di, si
        mov ax, bx
        shl ax, 6
        add di, ax
        add di, 64                      ; es:di = record slot
        mov si, bx
        shl si, 1
        mov ax, [bp + 10 + si]          ; selector = w[i+1]
        mov si, [bp + 8 + si]           ; offset = w[i]
        mov ds, ax
        mov cx, 16
        rep movsw
        pop ds
        pop di
        pop si
.pbn:   inc bx
        cmp bx, 4
        jb .pb
        mov [si + 48], dx
.norec: pop es
        pop ds
        popa
        popf
        ; call the real entry with a copy of the 8 words
        push word [bp + 22]
        push word [bp + 20]
        push word [bp + 18]
        push word [bp + 16]
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
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
        add cx, 42                      ; bytes the real entry took off
        mov ax, seg hdr
        mov ds, ax
        mov di, [cur wrt DGROUP]
        or di, di
        jz .norec2
        mov ax, [ss:si + 18]
        mov [di + 38], ax
        mov ax, [ss:si + 14]
        mov [di + 40], ax
        mov ax, [ss:si + 20]
        mov [di + 42], ax
        mov [di + 44], cx
        mov ax, [ss:si + 12]
        mov [di + 50], ax
        mov ax, [ss:si + 16]
        mov [di + 52], ax
        mov ax, [ss:si]
        mov [di + 54], ax
        ; pointers: after (the same pairs as before)
        push cx
        mov si, di
        xor bx, bx
.pa:    mov al, [cs:pbits + bx]
        test [si + 48], al
        jz .pan
        call pairok
        jc .pan
        push si
        push ds
        push ds
        pop es
        mov di, si
        mov ax, bx
        shl ax, 6
        add di, ax
        add di, 64 + 32
        mov si, bx
        shl si, 1
        mov ax, [bp + 10 + si]
        mov si, [bp + 8 + si]
        mov ds, ax
        mov cx, 16
        rep movsw
        pop ds
        pop si
.pan:   inc bx
        cmp bx, 4
        jb .pa
        pop cx
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

; pairok: CF clear when pair BX (w[BX+1]:w[BX]) is a readable far pointer
; to at least 32 bytes. Keeps every register but flags.
pairok:
        ; Disabled: dumping memory behind arguments recorded bytes of
        ; Microsoft code and data (e.g. USER's event procedure handed to
        ; Enable), which w16's clean-room rules forbid (ARCHITECTURE.md 3).
        ; Every pair is reported unreadable, so nothing is dumped.
        stc
        ret
        push ax
        push si
        push cx
        mov si, bx
        shl si, 1
        mov ax, [bp + 10 + si]
        verr ax
        jnz .no
        lsl cx, ax
        jnz .no
        mov ax, [bp + 8 + si]
        cmp ax, 0FFE0h
        ja .no
        add ax, 31
        cmp ax, cx
        ja .no
        clc
        jmp .out
.no:    stc
.out:   pop cx
        pop si
        pop ax
        ret

pbits   db 1, 2, 4, 8
align 2
extern X_1, X_2, X_3, X_4, X_5, X_6, X_7
targets:
        dw X_1, seg X_1
        dw X_2, seg X_2
        dw X_3, seg X_3
        dw X_4, seg X_4
        dw X_5, seg X_5
        dw X_6, seg X_6
        dw X_7, seg X_7
