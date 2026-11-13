; syrec.asm - SYREC.DRV: a recording system driver that stands in front of
; another one (black-box observation of SYSTEM.DRV, drivers/system/README.md;
; modelled on KBREC.DRV, tests/apps/kbrec.asm).
;
; Installed as SYSTEM\SYSTEM.DRV (module SYSTEM). It imports module
; SYSTGT: a scratch copy of the driver under test (Windows 3.1's
; SYSTEM.DRV or w16's, module name renamed in its NE header by
; tools/nerename.py; file SYSTEM\SYSTGT.DLL). Every call of an exported
; ordinal is recorded in SYREC's fixed data segment (no I/O: a caller may
; be at interrupt time) and passed on to the same ordinal of SYSTGT with
; the caller's registers and a copy of the top 8 stack words; as many
; bytes as the real entry took off are taken off the caller's stack, and
; the registers the real entry returned are returned.
;
; CreateSystemTimer's procedure is replaced by one of SYREC's trampolines,
; which records each call the driver makes to it (registers on entry, the
; BIOS tick count, the PIC's in-service and mask registers, nesting) and
; passes it on, registers unchanged, to the procedure the caller gave.
;
; Only values are recorded: registers and stack words. Nothing behind a
; pointer argument is ever read (that could copy Microsoft code or data).
;
; Records (REC = 64 bytes):
;   +0 kind: the ordinal for a call; 100h + slot for a timer procedure call
;   +2 sequence number (shared)
;   +4 AX BX CX DX SI DI BP (+4..+17), +18 flags, +20 DS, +22 ES, +24 SS,
;   +26 SP on entry, +28 return IP, +30 return CS
;   calls:  +32..+47 the 8 stack words above the return address (w0 at +32
;           is the last-pushed argument); +48 AX BX CX DX SI DI BP out
;           (+48..+61), +62 bytes taken off. (Flags, DS and ES out: in the
;           second record half below when the ring has them.)
;   timer:  +32 BIOS tick count low word (0040:006C), +34 high word,
;           +36 PIC ISR (low byte) and IMR (high byte), +38 nesting depth,
;           +40 the procedure's own return values AX, +42 flags after it
; A call also keeps flags, DS, ES out in an extra table (outx, 6 bytes per
; record).
;
; Only the first LIMIT records of each kind are kept (the counts of all
; are kept): GetSystemMsecCount is called many times a second.
;
; Ordinal 200 (SYRLOG) returns DX:AX = the header (below); ordinal 201
; (SYRDUMP) writes the header and records to C:\SYREC.BIN. SYREC also
; writes that file after each DisableSystemTimers call (Windows' exit,
; and each switch to a DOS program), so the end of a run is recorded.
        bits 16
        cpu 286

REC     equ 64
MAXREC  equ 560
NKIND   equ 64                          ; counts: 0..31 ordinals, 32+ slots
NSLOT   equ 16
PAD     equ 4096                        ; bytes before the header (below)

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern __0040H                          ; KERNEL.193

segment _DATA
pad:    times PAD db 0                  ; (see below: what lands here)
hdr:
count   dw 0                            ; +0 records kept
max     dw MAXREC                       ; +2
recsz   dw REC                          ; +4
total   dw 0                            ; +6 sequence numbers given
depth   dw 0                            ; +8 timer procedure nesting now
maxdep  dw 0                            ; +10 deepest seen
nslot   dw NSLOT                        ; +12
nkind   dw NKIND                        ; +14
counts: times NKIND dw 0                ; +16 calls of each kind
kept:   times NKIND dw 0                ; +144 records kept of each kind
slots:  times NSLOT * 8 db 0            ; +272 per slot: used, handle,
                                        ; proc offset, proc segment
outx:   times MAXREC * 6 db 0           ; +400 flags, DS, ES out per record
recs:   times MAXREC * REC db 0         ; +400 + MAXREC*6
hdrend:
fname   db 'C:\SYREC.BIN', 0

segment _TEXT
global libentry
libentry:
        mov ax, 1
        retf

; per-kind record limits
limit:  ; kind -> how many records to keep
        dw 0, 100, 100, 100, 40, 40, 40, 20, 20, 20, 20  ; 0-10
        times 21 dw 20                                    ; 11-31
        times 32 dw 24                                    ; slots

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
thunk 8
thunk 9
thunk 10
thunk 20

extern X_1, X_2, X_3, X_4, X_5, X_6, X_7, X_8, X_9, X_10, X_20

targets:
        dw 1
        dw X_1, seg X_1
        dw 2
        dw X_2, seg X_2
        dw 3
        dw X_3, seg X_3
        dw 4
        dw X_4, seg X_4
        dw 5
        dw X_5, seg X_5
        dw 6
        dw X_6, seg X_6
        dw 7
        dw X_7, seg X_7
        dw 8
        dw X_8, seg X_8
        dw 9
        dw X_9, seg X_9
        dw 10
        dw X_10, seg X_10
        dw 20
        dw X_20, seg X_20
        dw 0

global SYRLOG
SYRLOG:
        mov ax, hdr
        mov dx, seg hdr
        retf

global SYRDUMP
SYRDUMP:
        push ds
        mov ax, seg hdr
        mov ds, ax
        call dumpfile
        pop ds
        retf

; dumpfile: DS = DGROUP. C:\SYREC.BIN = the header and the records kept.
dumpfile:
        pusha
        mov ah, 3Ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc .r
        mov bx, ax
        mov ax, REC
        mul word [count wrt DGROUP]
        add ax, recs - pad
        mov cx, ax
        mov dx, pad
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.r:     popa
        ret

; newrec: DS = DGROUP, AX = kind. DI = a new record (kind and sequence
; filled in) or 0 when not kept; the kind's count goes up either way.
; Interrupts off while it is taken.
newrec:
        pushf
        cli
        push bx
        push ax
        inc word [total wrt DGROUP]
        mov bx, ax
        cmp bx, 100h
        jb .o
        sub bx, 100h - 32
.o:     cmp bx, NKIND
        jb .k
        mov bx, 0
.k:     shl bx, 1
        inc word [counts + bx wrt DGROUP]
        mov ax, [kept + bx wrt DGROUP]
        cmp ax, [cs:limit + bx]
        jae .no
        mov di, [count wrt DGROUP]
        cmp di, MAXREC
        jae .no
        inc word [kept + bx wrt DGROUP]
        inc word [count wrt DGROUP]
        push dx
        mov ax, REC
        mul di
        add ax, recs
        mov di, ax
        pop dx
        pop ax
        mov [di], ax
        push ax
        mov ax, [total wrt DGROUP]
        mov [di + 2], ax
        pop ax
        pop bx
        popf
        ret
.no:    xor di, di
        pop ax
        pop bx
        popf
        ret

; outxof: DI = a record; BX = its outx entry
outxof:
        push ax
        push dx
        mov ax, di
        sub ax, recs
        xor dx, dx
        mov bx, REC
        div bx
        mov bx, 6
        mul bx
        add ax, outx
        mov bx, ax
        pop dx
        pop ax
        ret

; [bp+2] ordinal, [bp+4] return IP, [bp+6] return CS, [bp+8..] w0..w7
thcommon:
        push bp
        mov bp, sp
        sub sp, 10                      ; [bp-4] target offset, [bp-2] seg,
                                        ; [bp-6] record, [bp-8] trampoline
                                        ; offset or 0, [bp-10] slot*8 or -1
        mov word [bp - 8], 0
        mov word [bp - 10], -1
        pushf
        pusha
        push ds
        push es
        ; [bp-12] flags, [bp-14] AX, -16 CX, -18 DX, -20 BX, -22 SP, -24 BP,
        ; -26 SI, -28 DI, -30 DS, -32 ES
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
        mov ax, [bp + 2]
        call newrec
        mov [bp - 6], di
        or di, di
        jz .norec
        push ds
        pop es
        add di, 4
        mov ax, [bp - 14]
        stosw                           ; +4 AX
        mov ax, [bp - 20]
        stosw                           ; BX
        mov ax, [bp - 16]
        stosw                           ; CX
        mov ax, [bp - 18]
        stosw                           ; DX
        mov ax, [bp - 26]
        stosw                           ; SI
        mov ax, [bp - 28]
        stosw                           ; DI
        mov ax, [bp]
        stosw                           ; +16 BP
        mov ax, [bp - 12]
        stosw                           ; +18 flags
        mov ax, [bp - 30]
        stosw                           ; +20 DS
        mov ax, [bp - 32]
        stosw                           ; +22 ES
        mov ax, ss
        stosw                           ; +24 SS
        lea ax, [bp + 4]
        stosw                           ; +26 SP on entry
        mov ax, [bp + 4]
        stosw                           ; +28 return IP
        mov ax, [bp + 6]
        stosw                           ; +30 return CS
        mov cx, 8
        lea si, [bp + 8]
.args:  mov ax, [ss:si]
        stosw                           ; +32..+47
        add si, 2
        loop .args
.norec: ; CreateSystemTimer: the procedure becomes a trampoline
        cmp word [bp + 2], 2
        jne .noct
        mov ax, [bp + 8]                ; a null procedure stays null
        or ax, [bp + 10]
        jz .noct
        call takeslot                   ; BX = slot * 8 or -1
        mov [bp - 10], bx
        cmp bx, -1
        je .noct
        mov ax, [bp + 8]
        mov [slots + bx + 4 wrt DGROUP], ax
        mov ax, [bp + 10]
        mov [slots + bx + 6 wrt DGROUP], ax
        shr bx, 1                       ; slot * 4: 4 bytes a trampoline
        lea ax, [bx + tramps]
        mov [bp - 8], ax
.noct:  pop es
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
        cmp word [bp - 8], 0
        je .plain
        push cs                         ; the procedure: our trampoline
        push word [bp - 8]
        jmp .call
.plain: push word [bp + 10]
        push word [bp + 8]
.call:  push cs
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
        add cx, 48                      ; bytes the real entry took off
        mov ax, seg hdr
        mov ds, ax
        mov di, [bp - 6]
        or di, di
        jz .norec2
        mov ax, [ss:si + 18]
        mov [di + 48], ax               ; AX out
        mov ax, [ss:si + 12]
        mov [di + 50], ax               ; BX
        mov ax, [ss:si + 16]
        mov [di + 52], ax               ; CX
        mov ax, [ss:si + 14]
        mov [di + 54], ax               ; DX
        mov ax, [ss:si + 6]
        mov [di + 56], ax               ; SI
        mov ax, [ss:si + 4]
        mov [di + 58], ax               ; DI
        mov ax, [ss:si + 8]
        mov [di + 60], ax               ; BP
        mov [di + 62], cx               ; bytes taken off
        call outxof
        mov ax, [ss:si + 20]
        mov [bx], ax                    ; flags out
        mov ax, [ss:si + 2]
        mov [bx + 2], ax                ; DS out
        mov ax, [ss:si]
        mov [bx + 4], ax                ; ES out
.norec2:
        ; CreateSystemTimer: the handle into the slot (0: slot freed);
        ; KillSystemTimer returning 0: the slot freed
        cmp word [bp + 2], 2
        jne .nokt
        mov bx, [bp - 10]
        cmp bx, -1
        je .post
        mov ax, [ss:si + 18]
        mov [slots + bx + 2 wrt DGROUP], ax
        or ax, ax
        jnz .post
        mov word [slots + bx wrt DGROUP], 0
        jmp .post
.nokt:  cmp word [bp + 2], 3
        jne .nods
        cmp word [ss:si + 18], 0
        jne .post
        mov ax, [bp + 8]                ; the handle killed
        xor bx, bx
.fk:    cmp word [slots + bx wrt DGROUP], 0
        je .fn
        cmp [slots + bx + 2 wrt DGROUP], ax
        jne .fn
        mov word [slots + bx wrt DGROUP], 0
        jmp .post
.fn:    add bx, 8
        cmp bx, NSLOT * 8
        jb .fk
        jmp .post
.nods:  cmp word [bp + 2], 5
        jne .post
        call dumpfile
.post:
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

; takeslot: DS = DGROUP; BX = a free slot * 8, marked used, or -1
takeslot:
        pushf
        cli
        xor bx, bx
.l:     cmp word [slots + bx wrt DGROUP], 0
        je .t
        add bx, 8
        cmp bx, NSLOT * 8
        jb .l
        mov bx, -1
        popf
        ret
.t:     mov word [slots + bx wrt DGROUP], 1
        mov word [slots + bx + 2 wrt DGROUP], 0FFFFh
        popf
        ret

; the trampolines: 4 bytes each (push imm8 sign-extended: 2, jmp short: 2)
tramps:
%assign i 0
%rep NSLOT
        push byte i
        jmp short tcommon_near
%assign i i + 1
%endrep
tcommon_near:
        jmp tcommon

; A timer procedure call: [bp+2] slot, [bp+4] return IP, [bp+6] return CS
tcommon:
        push bp
        mov bp, sp
        sub sp, 4                       ; [bp-4] target offset, [bp-2] seg
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
        shl bx, 3
        mov ax, [slots + bx + 4 wrt DGROUP]
        mov [bp - 4], ax
        mov ax, [slots + bx + 6 wrt DGROUP]
        mov [bp - 2], ax
        inc word [depth wrt DGROUP]
        mov ax, [depth wrt DGROUP]
        cmp ax, [maxdep wrt DGROUP]
        jbe .d
        mov [maxdep wrt DGROUP], ax
.d:     mov ax, [bp + 2]
        add ax, 100h
        call newrec
        or di, di
        jz .full
        push ds
        pop es
        add di, 4
        mov ax, [bp - 8]
        stosw                           ; +4 AX
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
        mov ax, [bp]
        stosw                           ; +16 BP
        mov ax, [bp - 6]
        stosw                           ; +18 flags
        mov ax, [bp - 24]
        stosw                           ; +20 DS
        mov ax, [bp - 26]
        stosw                           ; +22 ES
        mov ax, ss
        stosw                           ; +24 SS
        lea ax, [bp + 4]
        stosw                           ; +26 SP on entry
        mov ax, [bp + 4]
        stosw                           ; +28 return IP
        mov ax, [bp + 6]
        stosw                           ; +30 return CS
        push ds
        mov ax, __0040H
        mov ds, ax
        mov ax, [6Ch]
        mov dx, [6Eh]
        pop ds
        stosw                           ; +32 tick low
        mov ax, dx
        stosw                           ; +34 tick high
        mov al, 0Bh                     ; OCW3: read the ISR
        out 20h, al
        in al, 20h
        mov ah, al
        mov al, 0Ah                     ; back to the IRR
        out 20h, al
        in al, 21h
        xchg al, ah
        stosw                           ; +36 ISR, IMR
        mov ax, [depth wrt DGROUP]
        stosw                           ; +38 depth
        sub di, 40
.full:  mov [bp - 16], di               ; the record (SP slot: not restored)
        pop es
        pop ds
        popa
        popf
        push cs
        push word tback
        push word [bp - 2]
        push word [bp - 4]
        mov bp, [bp]
        retf
tback:  ; SP = frame - 4; [frame - 16] the record
        push bp
        mov bp, sp
        add bp, 2 + 4                   ; the frame
        pushf
        push ds
        push di
        push ax
        mov di, seg hdr
        mov ds, di
        dec word [depth wrt DGROUP]
        mov di, [bp - 16]
        or di, di
        jz .n
        mov [di + 40], ax
        mov ax, [bp - 8 - 2]            ; flags after the procedure
        mov [di + 42], ax
.n:     pop ax
        pop di
        pop ds
        popf
        pop bp                          ; the procedure's BP
        add sp, 4
        pop bp                          ; ours: the caller's BP
        add sp, 2                       ; the slot
        retf
