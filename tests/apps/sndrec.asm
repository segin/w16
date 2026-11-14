; sndrec.asm - SNDREC.DRV: a recording sound driver that stands in front
; of another one (black-box observation of the sound driver,
; drivers/sound/README.md; modelled on SYREC.DRV, tests/apps/syrec.asm).
;
; Installed as SYSTEM\MMSOUND.DRV (module SOUND). It imports module
; SOUNT: a scratch copy of the driver under test (Windows 3.1's
; MMSOUND.DRV or w16's, module name renamed in its NE header by
; tools/nerename.py; file SYSTEM\SOUNT.DLL). Every call of an exported
; ordinal is recorded in SNDREC's fixed record segment (no I/O) and
; passed on to the same ordinal of SOUNT with the caller's registers
; and a copy of the top 8 stack words; as many bytes as the real entry
; took off are taken off the caller's stack, and the registers the real
; entry returned are returned.
;
; Only values are recorded: registers and stack words. Nothing behind a
; pointer is ever read.
;
; Records (REC = 48 bytes):
;   +0 ordinal, +2 sequence number, +4 AX BX CX DX in, +12 return IP,
;   +14 return CS, +16..+31 the 8 stack words above the return address
;   (w0 at +16 is the last-pushed argument), +32 AX out, +34 DX out,
;   +36 bytes taken off, +38 flags out, +40 the current task (KERNEL's
;   GetCurrentTask, taken at the call)
; Ordinal 200 (SNRLOG) returns DX:AX = the header: +0 records kept, +2
; the most kept, +4 REC, +6 calls seen; the records follow at +16.
        bits 16
        cpu 286

REC     equ 48
MAXREC  equ 1200

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA
segment RECS   align=16 class=FAR_DATA use16

extern GETCURRENTTASK                   ; KERNEL.36

segment _DATA
dummy   dw 0

segment RECS
hdr:
count   dw 0                            ; +0 records kept
max     dw MAXREC                       ; +2
recsz   dw REC                          ; +4
total   dw 0                            ; +6 calls seen
        times 8 db 0
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
%assign i 1
%rep 18
thunk i
%assign i i + 1
%endrep

extern X_1, X_2, X_3, X_4, X_5, X_6, X_7, X_8, X_9, X_10, X_11, X_12
extern X_13, X_14, X_15, X_16, X_17, X_18

targets:
%assign i 1
%rep 18
        dw X_ %+ i, seg X_ %+ i
%assign i i + 1
%endrep

global SNRLOG
SNRLOG:
        mov ax, hdr
        mov dx, seg hdr
        retf

; newrec: DS = RECS. DI = a new record (sequence number in) or 0.
; Interrupts off while it is taken.
newrec:
        pushf
        cli
        push ax
        inc word [total]
        mov di, [count]
        cmp di, MAXREC
        jae .no
        inc word [count]
        push dx
        mov ax, REC
        mul di
        add ax, recs
        mov di, ax
        pop dx
        mov ax, [total]
        mov [di + 2], ax
        pop ax
        popf
        ret
.no:    xor di, di
        pop ax
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
        ; [bp-8] flags, [bp-10] AX, -12 CX, -14 DX, -16 BX, -18 SP,
        ; -20 BP, -22 SI, -24 DI, -26 DS, -28 ES
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
        call newrec
        mov [bp - 6], di
        or di, di
        jz .norec
        push ds
        pop es
        mov ax, [bp + 2]
        mov [di], ax                    ; +0 ordinal
        add di, 4
        mov ax, [bp - 10]
        stosw                           ; +4 AX
        mov ax, [bp - 16]
        stosw                           ; BX
        mov ax, [bp - 12]
        stosw                           ; CX
        mov ax, [bp - 14]
        stosw                           ; DX
        mov ax, [bp + 4]
        stosw                           ; +12 return IP
        mov ax, [bp + 6]
        stosw                           ; +14 return CS
        mov cx, 8
        lea si, [bp + 8]
.args:  mov ax, [ss:si]
        stosw                           ; +16..+31
        add si, 2
        loop .args
        call far GETCURRENTTASK
        mov di, [bp - 6]
        mov [di + 40], ax
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
        add cx, 22 + 6 + 16             ; bytes the real entry took off
        mov ax, seg hdr
        mov ds, ax
        mov di, [bp - 6]
        or di, di
        jz .norec2
        mov ax, [ss:si + 18]
        mov [di + 32], ax               ; AX out
        mov ax, [ss:si + 14]
        mov [di + 34], ax               ; DX out
        mov [di + 36], cx               ; bytes taken off
        mov ax, [ss:si + 20]
        mov [di + 38], ax               ; flags out
.norec2:
        ; return to the caller with the real entry's registers, taking
        ; off as many bytes as it did (CX): the return address and the
        ; caller's BP move up by CX, highest word first
        mov di, cx
        mov ax, [bp + 6]
        mov [bp + 6 + di], ax
        mov ax, [bp + 4]
        mov [bp + 4 + di], ax
        mov ax, [bp]
        mov [bp + 2 + di], ax
        lea ax, [bp + 2 + di]
        mov [bp - 2], ax                ; the SP to return with
        mov [ss:si + 8], bp             ; popa gives back this BP
        pop es
        pop ds
        popa
        popf
        mov sp, [bp - 2]                ; (no flags changed from here)
        pop bp
        retf
