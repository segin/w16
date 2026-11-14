; sw1.asm - SW1.COM: a resident spy for the Standard-mode task switcher
; (black-box probe for w16's WSWAP.EXE and DSWAP.EXE, ARCHITECTURE.md 9.9).
;
; Load it before WIN ("SW1"), run Windows, then "SW1 D" writes C:\SW1.LOG;
; tests/dos/sw1dump.py prints it. It records, into an XMS block of its own
; (so that it takes little DOS memory):
;   - INT 21h: every call made by a program whose name contains "SWAP"
;     (and its own children's calls only for EXEC, termination and names
;     of files), every EXEC, termination, INT 21h AH=0Bh with AL=80h,
;     AH=36h, and file-name calls naming a "~" file, from anyone;
;   - INT 2Fh: everything but 1680h/1689h (idle);
;   - INT 15h: 87h (block move, with its descriptors), 88h, 89h, 24h;
;   - INT 10h: from the watched programs, and mode, font, state and
;     display-combination calls from anyone;
;   - XMS: every call through the entry INT 2Fh AX=4310h returns (SW1 hands
;     out its own, which records and passes on);
; each with the registers in and out, the caller's CS:IP, the current PSP,
; the InDOS flag and the BIOS tick count; and at every EXEC (before and
; after), termination and INT 2Fh 4A05h/4B80h: the MCB chain, the
; interrupt vector table, the BIOS data area and WIN.COM's switcher block
; (once INT 2Fh AX=4A05h SI=5 has shown where it is).
; INT 21h and 2Fh are kept in front: when a program hooks one, SW1 puts an
; entry of its own before it (up to NSLOT per vector; the slot is part of
; each record), so it sees the calls WIN.COM, WSWAP and DSWAP answer
; themselves. "SW1 N" installs without that.
; Copyright (c) 2026 The w16 contributors, MIT License (LICENSE).

        bits 16
        cpu 286
        org 100h

NSLOT   equ 16
NINT    equ 4                   ; 0: INT 21h, 1: 2Fh, 2: 15h, 3: 10h
RECSZ   equ 32
LOGKB   equ 3072
BLKSZ   equ 540h

start:  jmp init

; the entries: NINT * NSLOT of "cli; call dispatch" (4 bytes each)
stubs:
%rep NINT * NSLOT
        cli
        call dispatch
%endrep
stubs_end:

chains:  times NINT * NSLOT dd 0
nslots:  times NINT db 0
intno:   db 21h, 2Fh, 15h, 10h
sticky:  db 1, 1, 0, 0
xms:     dd 0
xh:      dw 0
seq:     dd 0
cap:     dd LOGKB * 1024 / RECSZ
lost:    dw 0
busy:    db 0
blk:     dd 0                   ; WIN.COM's switcher block (off, seg)
sda:     dd 0                   ; DOS's swappable data area
mcb0:    dw 0
old08:   dd 0
watch:   times 16 dw 0          ; PSPs of the programs watched
nwatch:  dw 0
arm:     db 0
stick_on: db 0                  ; from the first EXEC of a "SWAP" program
armpsp:  dw 0
l_key:   times 4 dw 0           ; the last record: idx, AX, CS, IP
l_rep:   dw 0
t_ret:   dw 0
t_idx:   dw 0
t_ax:    dw 0
t_si:    dw 0
tmp_t:   dd 0
mv:                             ; XMS move structure
mv_len:  dd 0
mv_sh:   dw 0
mv_so:   dd 0
mv_dh:   dw 0
mv_do:   dd 0
rec:     times RECSZ db 0
mcbbuf:  times 32 * 16 db 0

; ---- the entries ----
; IF = 0 (the stub's cli), stack: return into the stub, IP, CS, FLAGS
dispatch:
        pop word [cs:t_ret]
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        push cs
        pop ds
        mov ax, [t_ret]
        sub ax, stubs + 4
        shr ax, 2
        push ax                         ; [bp-22] idx
        call check_sticky
        call check_arm
        mov ax, [bp - 22]
        mov cl, 4
        shr ax, cl                      ; NSLOT = 16
        cmp al, 0
        je h21
        cmp al, 1
        je near h2f
        cmp al, 2
        je near h15
        jmp h10

; frame offsets (in-frame)
%define F_DS  [bp-2]
%define F_ES  [bp-4]
%define F_AX  [bp-6]
%define F_CX  [bp-8]
%define F_DX  [bp-10]
%define F_BX  [bp-12]
%define F_BP  [bp-16]
%define F_SI  [bp-18]
%define F_DI  [bp-20]
%define F_IDX [bp-22]

; pass the call on unrecorded
chain:
        mov bx, F_IDX
        shl bx, 2
        mov ax, [chains + bx]
        mov [tmp_t], ax
        mov ax, [chains + bx + 2]
        mov [tmp_t + 2], ax
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        jmp far [cs:tmp_t]

; record the call and pass it on, recording what comes back
through:
        mov al, 'i'
        xor ah, ah
        call hdr_in
        call put_hdr
through2:
        mov bx, F_IDX
        mov [t_idx], bx
        shl bx, 2
        mov ax, [chains + bx]
        mov [tmp_t], ax
        mov ax, [chains + bx + 2]
        mov [tmp_t + 2], ax
        mov ax, F_AX
        mov [t_ax], ax
        mov ax, F_SI
        mov [t_si], ax
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        push word [cs:t_si]
        push word [cs:t_ax]
        push word [cs:t_idx]
        pushf
        call far [cs:tmp_t]
        cli
        push bp
        mov bp, sp              ; [bp+2] idx, [bp+4] AX in, [bp+6] SI in, [bp+8] IP
        push ds
        push es
        pusha
        pushf                   ; [bp-22] the flags back
        push cs
        pop ds
        ; INT 2Fh AX=4310h: hand out our XMS entry
        mov ax, [bp + 2]
        shr ax, 4
        cmp ax, 1
        jne .n43
        cmp word [bp + 4], 4310h
        jne .n43
        cmp word F_BX, xms_shim         ; (a slot further down did it)
        je .n43
        mov ax, F_BX
        mov [xms], ax
        mov ax, F_ES
        mov [xms + 2], ax
        mov word F_BX, xms_shim
        mov F_ES, cs
        ; INT 2Fh AX=4B01h at slot 0: SW1 joins the task switcher's
        ; notification chain (its calls are recorded as 'N')
.n43:   mov ax, [bp + 2]
        cmp ax, 10h
        jne .n4b
        cmp word [bp + 4], 4B01h
        jne .n4b
        mov ax, F_BX
        mov [cbinfo], ax
        mov ax, F_ES
        mov [cbinfo + 2], ax
        mov word F_BX, cbinfo
        mov F_ES, cs
.n4b:   mov al, 'o'
        xor ah, ah
        mov bx, 8
        mov dx, [bp - 22]
        mov cl, [bp + 2]
        call hdr
        call put_hdr
        ; INT 2Fh 4A05h SI=5 tells where the block is; 4A05h and 4B80h
        ; may change it: a snapshot after them
        mov ax, [bp + 2]
        shr ax, 4
        cmp ax, 1
        jne .nb
        mov ax, [bp + 4]
        cmp ax, 4B80h
        je .snapb
        cmp ax, 4A05h
        jne .nb
        cmp word [bp + 6], 5            ; SI in
        jne .snapb
        mov ax, F_AX
        mov [blk], ax
        mov ax, F_DX
        mov [blk + 2], ax
.snapb: call snap_block
.nb:    ; after an EXEC: every snapshot again
        mov ax, [bp + 2]
        shr ax, 4
        jnz .ne
        cmp byte [bp + 5], 4Bh
        jne .ne
        call snap_all
.ne:
        ; the flags back, with the caller's IF, TF, DF
        mov ax, [bp - 22]
        and ax, 0F8FFh
        mov bx, [bp + 12]
        and bx, 0700h
        or ax, bx
        mov [bp + 12], ax
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        add sp, 6
        iret

; ---- INT 21h ----
h21:    mov ax, F_AX
        cmp ah, 4Ch
        je .term
        cmp ah, 00h
        je .term
        cmp ah, 31h
        je .term
        cmp ah, 4Bh
        je .exec
        cmp ah, 36h
        je .rec
        cmp ax, 0B80h
        je .sw
        call is_name
        jnc .nm
        call watched
        jc .recn
        call tilde
        jc .recn
        jmp chain
.nm:    call watched
        jnc .ch
        call ratelimit
        jc .ch
.rec:   jmp through
.ch:    jmp chain
.recn:  ; with the name (64 bytes at DS:DX, or DS:SI for 6Ch)
        mov al, 'i'
        mov ah, 2
        call hdr_in
        call put_hdr
        mov es, F_DS
        mov si, F_DX
        mov ax, F_AX
        cmp ah, 6Ch
        jne .n6
        mov si, F_SI
.n6:    mov cx, 64
        call wr
        jmp through2
.sw:    mov al, 'S'
        call event
        jmp through2
.term:  mov al, 'T'
        call event
        jmp chain
.exec:  ; the path, the tail, the parameter block
        call is_swap
        jnc .ne
        mov byte [arm], 1
        mov byte [stick_on], 1
        call check_sticky
        call cur_psp
        mov [armpsp], ax
.ne:    mov al, 'E'
        mov ah, 7
        call hdr_in
        call put_hdr
        mov es, F_DS
        mov si, F_DX
        mov cx, 64
        call wr
        mov es, F_ES
        mov si, F_BX
        push es
        push si
        mov cx, 32                      ; the parameter block
        call wr
        pop si
        pop es
        les si, [es:si + 2]             ; the tail
        mov cx, 128
        call wr
        call snap_all
        jmp through2

; ---- INT 2Fh ----
h2f:    mov ax, F_AX
        cmp ax, 1680h
        je .ch
        cmp ax, 1689h
        je .ch
        cmp ax, 0DE57h                  ; ours
        je .me
        call ratelimit
        jc .ch
        mov ax, F_AX
        cmp ax, 4B80h
        je .ev
        cmp ax, 4A05h
        jne .th
.ev:    mov al, 'P'
        call event
        jmp through2
.th:    jmp through
.ch:    jmp chain
.me:    mov word F_AX, 5357h
        mov F_ES, cs
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        iret

; ---- INT 15h ----
h15:    mov ax, F_AX
        cmp ah, 87h
        je .mv
        cmp ah, 88h
        je .th
        cmp ah, 89h
        je .th
        cmp ah, 24h
        je .th
        jmp chain
.th:    jmp through
.mv:    mov al, 'i'
        mov ah, 2
        call hdr_in
        call put_hdr
        mov es, F_ES
        mov si, F_SI
        mov cx, 64
        call wr
        jmp through2

; ---- INT 10h ----
h10:    call watched
        jc .w
        mov ax, F_AX
        cmp ah, 00h
        je .w
        cmp ah, 05h
        je .w
        cmp ah, 11h
        je .w
        cmp ah, 12h
        je .w
        cmp ah, 1Ah
        je .w
        cmp ah, 1Bh
        je .w
        cmp ah, 1Ch
        je .w
        cmp ah, 4Fh
        je .w
        cmp ah, 6Fh
        je .w
        jmp chain
.w:     call ratelimit
        jc .ch
        jmp through
.ch:    jmp chain

; ---- XMS: our entry ----
xms_shim:
        jmp short .go
        nop
        nop
        nop
.go:    pushf
        cli
        push bp
        mov bp, sp              ; [bp+2] flags, [bp+4] IP, [bp+6] CS
        push ds
        push es
        pusha
        push cs
        pop ds
        push word 0FFFFh        ; idx
        mov ax, [bp + 6]        ; only the watched programs' calls
        call watched_cs
        jnc .skip
        mov al, 'X'
        xor ah, ah
        mov bx, F_AX
        cmp bh, 0Bh
        jne .n
        mov ah, 1
.n:     mov bx, 4
        mov dx, [bp + 2]
        mov cl, 0FFh
        call hdr
        call put_hdr
        cmp byte [rec + 1], 0
        je .nm
        mov es, F_DS
        mov si, F_SI
        mov cx, 32
        call wr
.nm:    add sp, 2
        popa
        pop es
        pop ds
        pop bp
        popf
        call far [cs:xms]
        jmp .out
.skip:  add sp, 2
        popa
        pop es
        pop ds
        pop bp
        popf
        jmp far [cs:xms]
.out:   pushf
        cli
        push bp
        mov bp, sp
        push ds
        push es
        pusha
        push cs
        pop ds
        push word 0FFFFh
        mov al, 'x'
        xor ah, ah
        mov bx, 4
        mov dx, [bp + 2]
        mov cl, 0FFh
        call hdr
        call put_hdr
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        popf
        retf

; ---- our task switcher client (MS-DOS 5 task switcher API) ----
cbinfo:  dd 0                   ; next
         dw notify, 0           ; notification function (segment set at init)
         dd 0
         dw api_list, 0
api_list: dw 0, 0
notify: pushf
        cli
        push bp
        mov bp, sp              ; [bp+2] flags, [bp+4] IP, [bp+6] CS
        push ds
        push es
        pusha
        push cs
        pop ds
        push word 0FFFEh
        mov al, 'N'
        xor ah, ah
        mov bx, 4
        mov dx, [bp + 2]
        mov cl, 0FEh
        call hdr
        call put_hdr
        add sp, 2
        popa
        pop es
        pop ds
        pop bp
        popf
        xor ax, ax
        retf

; ---- the timer: keep in front ----
int08:  push ds
        push ax
        push cs
        pop ds
        pushf
        cli
        pusha
        push es
        call check_sticky
        pop es
        popa
        popf
        pop ax
        pop ds
        jmp far [cs:old08]

; ---- helpers (DS = CS, IF = 0) ----

; keep our entry first on the sticky vectors
check_sticky:
        cmp byte [stick_on], 0
        jne .go
        ret
.go:    push es
        xor ax, ax
        mov es, ax
        xor si, si                      ; interrupt index
.l:     cmp byte [sticky + si], 0
        je .next
        mov bl, [intno + si]
        xor bh, bh
        shl bx, 2
        mov ax, [es:bx + 2]
        mov dx, [es:bx]
        mov cx, cs
        cmp ax, cx
        jne .foreign
        cmp dx, stubs
        jb .foreign
        cmp dx, stubs_end
        jb .next
.foreign:
        ; a slot already chaining there? (a program hooking again at the
        ; same place)
        mov di, si
        shl di, 4                       ; first slot of this interrupt
        mov cx, 1
.f:     cmp cl, [nslots + si]
        jae .new
        push di
        add di, cx
        shl di, 2
        cmp [chains + di], dx
        jne .fn
        cmp [chains + di + 2], ax
        jne .fn
        pop di
        add di, cx
        jmp .set
.fn:    pop di
        inc cx
        jmp .f
.new:   cmp byte [nslots + si], NSLOT
        jae .next
        mov cl, [nslots + si]
        xor ch, ch
        inc byte [nslots + si]
        add di, cx
        push di
        shl di, 2
        mov [chains + di], dx
        mov [chains + di + 2], ax
        pop di
.set:   ; DI = slot index: its stub at stubs + 4 * DI
        push di
        shl di, 2
        add di, stubs
        mov [es:bx], di
        mov [es:bx + 2], cs
        pop di
        ; a record of it: AX = the foreign segment, DX its offset
        push si
        mov [rec + 4], ax
        mov [rec + 10], dx
        mov ax, di
        mov [rec + 2], al
        mov byte [rec], 'H'
        mov byte [rec + 1], 0
        call put_hdr
        pop si
.next:  inc si
        cmp si, NINT
        jb .l
        pop es
        ret

; the current PSP (from DOS's swappable data area)
cur_psp:
        push es
        push bx
        les bx, [sda]
        mov ax, [es:bx + 10h]
        pop bx
        pop es
        ret

; after an EXEC of a "SWAP" program: the next new current PSP is its own
check_arm:
        cmp byte [arm], 0
        je .r
        call cur_psp
        cmp ax, [armpsp]
        je .r
        mov byte [arm], 0
        mov bx, [nwatch]
        cmp bx, 16
        jae .r
        shl bx, 1
        mov [watch + bx], ax
        inc word [nwatch]
.r:     ret

; CF = the caller is watched: the current PSP is one of them or the
; caller's CS lies in one's memory block
watched:
        mov ax, [bp + 4]                ; caller's CS (in-frame)
watched_cs:                             ; AX = the caller's CS
        push ax
        call cur_psp
        mov dx, ax
        pop ax
        xor si, si
.l:     cmp si, [nwatch]
        jae .no
        mov bx, si
        shl bx, 1
        mov bx, [watch + bx]
        cmp dx, bx
        je .yes
        cmp ax, bx
        jb .n
        push es
        dec bx
        mov es, bx
        mov cx, [es:3]
        inc bx
        add cx, bx
        pop es
        cmp ax, cx
        jb .yes
.n:     inc si
        jmp .l
.no:    clc
        ret
.yes:   stc
        ret

; CF = INT 21h AH (frame) takes a file name
is_name:
        mov ax, F_AX
        mov al, ah
        mov di, namefn
        mov cx, namefn_end - namefn
        push es
        push ds
        pop es
        repne scasb
        pop es
        je .y
        clc
        ret
.y:     stc
        ret
namefn: db 39h, 3Ah, 3Bh, 3Ch, 3Dh, 41h, 43h, 4Eh, 56h, 5Ah, 5Bh, 6Ch
namefn_end:

; CF = the name at DS:DX (frame; DS:SI for 6Ch) has a "~"
tilde:
        push es
        mov es, F_DS
        mov si, F_DX
        mov ax, F_AX
        cmp ah, 6Ch
        jne .l
        mov si, F_SI
.l:     mov al, [es:si]
        inc si
        test al, al
        jz .no
        cmp al, '~'
        jne .l
        pop es
        stc
        ret
.no:    pop es
        clc
        ret

; CF = the path at DS:DX (frame) has "SWAP" in it
is_swap:
        push es
        mov es, F_DS
        mov si, F_DX
.l:     mov al, [es:si]
        test al, al
        jz .no
        mov cx, 4
        xor di, di
.c:     mov bx, si
        mov al, [es:bx + di]
        and al, 0DFh
        cmp al, [swapname + di]
        jne .nx
        inc di
        loop .c
        pop es
        stc
        ret
.nx:    inc si
        jmp .l
.no:    pop es
        clc
        ret
swapname: db 'SWAP'

; CF = too many of the same call in a row (idx, AX, CS, IP)
ratelimit:
        mov ax, F_IDX
        cmp ax, [l_key]
        jne .new
        mov ax, F_AX
        cmp ax, [l_key + 2]
        jne .new
        mov ax, [bp + 4]
        cmp ax, [l_key + 4]
        jne .new
        mov ax, [bp + 2]
        cmp ax, [l_key + 6]
        jne .new
        inc word [l_rep]
        cmp word [l_rep], 12
        jbe .ok
        stc
        ret
.new:   mov ax, F_IDX
        mov [l_key], ax
        mov ax, F_AX
        mov [l_key + 2], ax
        mov ax, [bp + 4]
        mov [l_key + 4], ax
        mov ax, [bp + 2]
        mov [l_key + 6], ax
        mov word [l_rep], 0
.ok:    clc
        ret

; the header from an in-frame: AL type, AH data records
hdr_in:
        mov bx, 2
        mov dx, [bp + 6]
        mov cl, F_IDX
        jmp hdr

; build the header in rec: AL type, AH data records that follow, CL slot,
; BX = offset of the caller's IP from BP (CS after it), DX flags;
; registers from the frame
hdr:    mov [rec], al
        mov [rec + 1], ah
        mov [rec + 2], cl
        push es
        les si, [sda]
        mov al, [es:si + 1]
        mov [rec + 3], al
        mov ax, [es:si + 10h]
        mov [rec + 24], ax
        pop es
        mov ax, F_AX
        mov [rec + 4], ax
        mov ax, F_BX
        mov [rec + 6], ax
        mov ax, F_CX
        mov [rec + 8], ax
        mov ax, F_DX
        mov [rec + 10], ax
        mov ax, F_SI
        mov [rec + 12], ax
        mov ax, F_DI
        mov [rec + 14], ax
        mov ax, F_DS
        mov [rec + 16], ax
        mov ax, F_ES
        mov [rec + 18], ax
        mov si, bp
        add si, bx
        mov ax, [ss:si + 2]
        mov [rec + 20], ax
        mov ax, [ss:si]
        mov [rec + 22], ax
        mov [rec + 26], dx
        push es
        push 40h
        pop es
        mov ax, [es:6Ch]
        mov [rec + 28], ax
        pop es
        mov ax, F_BP
        mov [rec + 30], ax
        ret

put_hdr:
        push es
        push cs
        pop es
        mov si, rec
        mov cx, RECSZ
        call wr
        pop es
        ret

; an event (AL type) with every snapshot
event:  xor ah, ah
        call hdr_in
        call put_hdr
snap_all:
        ; the MCB chain
        push es
        mov di, mcbbuf
        mov ax, [mcb0]
        mov cx, 32
.m:     mov es, ax
        mov [di], ax
        mov bl, [es:0]
        mov [di + 2], bl
        mov byte [di + 3], 0
        mov dx, [es:1]
        mov [di + 4], dx
        mov dx, [es:3]
        mov [di + 6], dx
        mov dx, [es:8]
        mov [di + 8], dx
        mov dx, [es:10]
        mov [di + 10], dx
        mov dx, [es:12]
        mov [di + 12], dx
        mov dx, [es:14]
        mov [di + 14], dx
        add di, 16
        cmp bl, 'M'
        jne .me
        add ax, [es:3]
        inc ax
        loop .m
.me:    cmp di, mcbbuf + 32 * 16
        jae .md
        mov word [di], 0
        add di, 16
        jmp .me
.md:    pop es
        mov byte [rec], 'M'
        mov byte [rec + 1], 16
        call put_hdr
        push es
        push cs
        pop es
        mov si, mcbbuf
        mov cx, 32 * 16
        call wr
        pop es
        ; the vector table and the BIOS data area
        mov byte [rec], 'V'
        mov byte [rec + 1], 32 + 8
        call put_hdr
        push es
        push 0
        pop es
        xor si, si
        mov cx, 1024 + 256
        call wr
        pop es
snap_block:
        cmp word [blk + 2], 0
        je .r
        mov byte [rec], 'B'
        mov byte [rec + 1], BLKSZ / RECSZ
        mov ax, [blk]
        mov [rec + 4], ax
        mov ax, [blk + 2]
        mov [rec + 6], ax
        call put_hdr
        push es
        les si, [blk]
        mov cx, BLKSZ
        call wr
        pop es
.r:     ret

; write CX bytes (a multiple of 32) from ES:SI to the log
wr:     push ax
        push bx
        push cx
        push dx
        push si
        mov [mv_len], cx
        mov word [mv_len + 2], 0
        shr cx, 5
        mov ax, [seq]
        mov dx, [seq + 2]
        add ax, cx
        adc dx, 0
        cmp dx, [cap + 2]
        jb .ok
        ja .full
        cmp ax, [cap]
        jbe .ok
.full:  inc word [lost]
        jmp .r
.ok:    push ax
        push dx
        mov word [mv_sh], 0
        mov [mv_so], si
        mov [mv_so + 2], es
        mov ax, [xh]
        mov [mv_dh], ax
        mov ax, [seq]
        mov dx, [seq + 2]
        mov cx, 5
.s:     shl ax, 1
        rcl dx, 1
        loop .s
        mov [mv_do], ax
        mov [mv_do + 2], dx
        mov si, mv
        mov ah, 0Bh
        call far [xms_real]
        pop dx
        pop ax
        mov [seq], ax
        mov [seq + 2], dx
.r:     pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret
xms_real: dd 0

resident_end:

; ========================================================================
; start-up and the dump
; ========================================================================
init:   mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        and al, 0DFh
        cmp al, 'D'
        je dump
        cmp al, 'F'                     ; INT 2Fh only kept in front
        jne .nf
        mov byte [sticky], 0
.nf:    cmp al, 'N'
        jne .inst
        mov byte [sticky], 0
        mov byte [sticky + 1], 0
.inst:  mov ax, 0DE57h
        int 2Fh
        cmp ax, 5357h
        jne .new
        mov dx, m_already
        jmp msg_exit
.new:   mov ax, 4300h
        int 2Fh
        cmp al, 80h
        je .x
        mov dx, m_noxms
        jmp msg_exit
.x:     mov ax, 4310h
        int 2Fh
        mov [xms_real], bx
        mov [xms_real + 2], es
        mov [xms], bx
        mov [xms + 2], es
        mov ah, 09h
        mov dx, LOGKB
        call far [xms_real]
        test ax, ax
        jnz .h
        mov dx, m_noxms
        jmp msg_exit
.h:     mov [xh], dx
        mov ax, 5D06h
        push ds
        int 21h
        mov ax, ds
        pop ds
        mov [sda], si
        mov [sda + 2], ax
        mov ah, 52h
        int 21h
        mov ax, [es:bx - 2]
        mov [mcb0], ax
        mov [cbinfo + 6], cs
        mov [cbinfo + 14], cs
        ; hook
        xor si, si
.hk:    mov al, [intno + si]
        mov ah, 35h
        int 21h
        mov di, si
        shl di, 6                       ; slot 0 of this interrupt: 16 * 4
        mov [chains + di], bx
        mov [chains + di + 2], es
        mov byte [nslots + si], 1
        mov dx, si
        shl dx, 6                       ; its stub: stubs + 4 * 16 * si
        add dx, stubs
        mov al, [intno + si]
        mov ah, 25h
        int 21h
        inc si
        cmp si, NINT
        jb .hk
        mov ax, 3508h
        int 21h
        mov [old08], bx
        mov [old08 + 2], es
        mov dx, int08
        mov ax, 2508h
        int 21h
        ; the first snapshot
        mov byte [rec], 'I'
        mov byte [rec + 1], 0
        call put_hdr
        call snap_all
        mov dx, m_ok
        mov ah, 09h
        int 21h
        mov dx, (resident_end - start + 100h + 15) / 16
        mov ax, 3100h
        int 21h

msg_exit:
        mov ah, 09h
        int 21h
        mov ax, 4C01h
        int 21h

dump:   mov ax, 0DE57h
        int 2Fh
        cmp ax, 5357h
        je .f
        mov dx, m_notres
        jmp msg_exit
.f:     mov [res], es
        mov ah, 3Ch
        xor cx, cx
        mov dx, logname
        int 21h
        jc .err
        mov [fh], ax
        ; header: "SW1", the record count, records lost, the block
        push ds
        mov ds, [res]
        mov ax, [seq]
        mov dx, [seq + 2]
        mov bx, [lost]
        mov cx, [blk]
        mov si, [blk + 2]
        mov di, [xh]
        push word [xms_real]
        push word [xms_real + 2]
        pop word [cs:xms_real + 2]
        pop word [cs:xms_real]
        pop ds
        mov [hd_seq], ax
        mov [hd_seq + 2], dx
        mov [hd_lost], bx
        mov [hd_blk], cx
        mov [hd_blk + 2], si
        mov [dxh], di
        mov bx, [fh]
        mov cx, 32
        mov dx, hd
        mov ah, 40h
        int 21h
        ; the records, 16 KB at a time
        mov ax, [hd_seq]
        mov dx, [hd_seq + 2]
        mov cx, 5
.s:     shl ax, 1
        rcl dx, 1
        loop .s
        mov [left], ax
        mov [left + 2], dx
.ch:    mov ax, [left]
        or ax, [left + 2]
        jz .done
        mov cx, 4000h
        cmp word [left + 2], 0
        jne .big
        cmp [left], cx
        jae .big
        mov cx, [left]
.big:   mov [dmv_len], cx
        mov ax, [dxh]
        mov [dmv_sh], ax
        mov word [dmv_dh], 0
        mov word [dmv_do], buf
        mov [dmv_do + 2], ds
        push cx
        mov si, dmv
        mov ah, 0Bh
        call far [xms_real]
        pop cx
        add [dmv_so], cx
        adc word [dmv_so + 2], 0
        sub [left], cx
        sbb word [left + 2], 0
        mov bx, [fh]
        mov dx, buf
        mov ah, 40h
        int 21h
        jmp .ch
.done:  mov bx, [fh]
        mov ah, 3Eh
        int 21h
        mov ax, 4C00h
        int 21h
.err:   mov dx, m_err
        jmp msg_exit

; (the dump uses the resident copy's XMS entry: get our own)
dump_xms:

res:    dw 0
fh:     dw 0
dxh:    dw 0
left:   dd 0
dmv:
dmv_len: dd 0
dmv_sh:  dw 0
dmv_so:  dd 0
dmv_dh:  dw 0
dmv_do:  dd 0
hd:     db 'SW1', 0
hd_seq: dd 0
hd_lost: dw 0
hd_blk: dd 0
        times 32 - 14 db 0
logname: db 'C:\SW1.LOG', 0
m_ok:   db 'SW1 installed', 13, 10, '$'
m_already: db 'SW1 already installed', 13, 10, '$'
m_noxms: db 'SW1: no XMS', 13, 10, '$'
m_notres: db 'SW1 not installed', 13, 10, '$'
m_err:  db 'SW1: cannot write C:\SW1.LOG', 13, 10, '$'
        align 16
buf:
