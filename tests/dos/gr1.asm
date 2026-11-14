; gr1.asm - GR1.2GR, a recording stand-in for a Standard-mode display
; grabber (.2GR; ARCHITECTURE.md 9.11). Black-box probe: it has the
; documented .2GR layout (Windows 3.1 DDK, "Display Grabbers": a binary
; image run at offset 0 with CS = DS, a table of 3-byte jumps at 0, one per
; function: 00h InquireGrab (AX 1/2 sizes, AX >= 3 the extended functions
; FFF4h-FFFFh), 03h-09h obsolete, 0Ch InquireSave, 0Fh SaveScreen, 12h
; RestoreScreen, 15h InitScreen) and forwards every call to the real
; grabber, which the test appends to this file at offset GR_OFS (a scratch
; file made at test time from the user's own install, never kept:
; tests/grabber.sh). It records, as values only:
;   - each call: the function's offset, the registers in and out, the BIOS
;     video mode, rows and columns and the timer tick;
;   - the INT 10h calls the grabber makes during the call (AX BX CX DX);
;   - for the documented structures: SetSwapDrive's path and drive,
;     GetInfo's GRABINFO, GetBlock's GRABREQUEST and the size, the GRABST
;     header and a checksum of what it copied.
; The log goes to C:\GR1.LOG (appended), buffered in memory and written when
; DOS is not busy (InDOS 0). Nothing of the real grabber's code is read or
; recorded.

        bits 16
        cpu 386
        org 0

GR_OFS  equ 2000h               ; where the real grabber is appended
LOGMAX  equ 1A00h               ; the log buffer

table:  jmp near e00
        jmp near e03
        jmp near e06
        jmp near e09
        jmp near e0c
        jmp near e0f
        jmp near e12
        jmp near e15
        db 'GR1'                ; (18h: after the table)

e00:    mov byte [cs:fn], 00h
        jmp gcommon
e03:    mov byte [cs:fn], 03h
        jmp gcommon
e06:    mov byte [cs:fn], 06h
        jmp gcommon
e09:    mov byte [cs:fn], 09h
        jmp gcommon
e0c:    mov byte [cs:fn], 0Ch
        jmp gcommon
e0f:    mov byte [cs:fn], 0Fh
        jmp gcommon
e12:    mov byte [cs:fn], 12h
        jmp gcommon
e15:    mov byte [cs:fn], 15h
        jmp gcommon

gcommon:
        pushf
        pop word [cs:i_fl]
        mov [cs:i_ax], ax
        mov [cs:i_bx], bx
        mov [cs:i_cx], cx
        mov [cs:i_dx], dx
        mov [cs:i_si], si
        mov [cs:i_di], di
        mov [cs:i_bp], bp
        mov [cs:i_ds], ds
        mov [cs:i_es], es
        mov [cs:i_sp], sp
        mov [cs:i_ss], ss
        push cs
        pop ds
        cld
        cmp byte [inited], 0
        jne .go
        mov byte [inited], 1
        mov ah, 34h             ; InDOS (reentrant)
        int 21h
        mov [indos], bx
        mov [indos+2], es
        mov si, s_hello
        call lstr
        mov ax, cs
        add ax, GR_OFS / 16
        call lhex4
        call lnl
.go:
        ; "C fn ax bx cx dx si di bp ds es fl ss:sp mode cols rows tick"
        mov al, 'C'
        call lchr
        mov al, ' '
        call lchr
        mov al, [fn]
        call lhex2
        mov si, i_ax
        mov cx, 10
        call lregs
        mov al, ' '
        call lchr
        mov ax, [i_ss]
        call lhex4
        mov al, ':'
        call lchr
        mov ax, [i_sp]
        call lhex4
        call lbios
        call lnl
        ; documented inputs
        cmp byte [fn], 0
        jne .pre_done
        mov ax, [i_ax]
        cmp ax, 0FFFDh          ; SetSwapDrive: BL, ES:DI path
        jne .pre1
        mov si, s_swap
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        call lasciiz
        call lnl
        jmp .pre_done
.pre1:  cmp ax, 0FFF8h          ; GetBlock: ES:DI GRABREQUEST (11 bytes)
        jne .pre_done
        mov si, s_req
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        mov cx, 11
        call ldump
        call lnl
.pre_done:
        ; the INT 10h calls during the call (once something has hooked
        ; INT 10h over us we stay where we are in the chain)
        cmp byte [i10chained], 0
        jne .hooked
        xor ax, ax
        mov es, ax
        cli
        mov ax, [es:40h]
        mov [i10old], ax
        mov ax, [es:42h]
        mov [i10old+2], ax
        mov word [es:40h], i10hook
        mov [es:42h], cs
        sti
.hooked:
        mov byte [i10on], 1
        ; forward
        mov ax, cs
        add ax, GR_OFS / 16
        mov [target+2], ax
        xor ax, ax
        mov al, [fn]
        mov [target], ax
        push word [i_fl]
        mov ax, [i_ax]
        mov bx, [i_bx]
        mov cx, [i_cx]
        mov dx, [i_dx]
        mov si, [i_si]
        mov di, [i_di]
        mov bp, [i_bp]
        mov es, [i_es]
        mov ds, [cs:target+2]
        popf
        call far [cs:target]
        pushf
        pop word [cs:o_fl]
        mov [cs:o_ax], ax
        mov [cs:o_bx], bx
        mov [cs:o_cx], cx
        mov [cs:o_dx], dx
        mov [cs:o_si], si
        mov [cs:o_di], di
        mov [cs:o_bp], bp
        mov [cs:o_ds], ds
        mov [cs:o_es], es
        mov [cs:o_sp], sp
        push cs
        pop ds
        cld
        ; unhook when the vector is ours again (also when the grabber has
        ; just taken its own hook, which chained to us, away)
        mov byte [i10on], 0
        xor ax, ax
        mov es, ax
        cli
        mov byte [i10chained], 1
        cmp word [es:40h], i10hook
        jne .unhooked
        mov ax, [es:42h]
        mov bx, cs
        cmp ax, bx
        jne .unhooked
        mov ax, [i10old]
        mov [es:40h], ax
        mov ax, [i10old+2]
        mov [es:42h], ax
        mov byte [i10chained], 0
.unhooked:
        sti
        mov al, 'R'
        call lchr
        mov al, ' '
        call lchr
        mov al, [fn]
        call lhex2
        mov si, o_ax
        mov cx, 10
        call lregs
        mov al, ' '
        call lchr
        mov ax, [o_sp]
        call lhex4
        call lbios
        call lnl
        ; documented outputs
        cmp byte [fn], 0
        jne .post_done
        mov ax, [i_ax]
        cmp ax, 0FFFEh          ; GetInfo: GRABINFO at ES:DI (48 bytes)
        jne .post1
        mov si, s_info
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        mov cx, 48
        call ldump
        call lnl
        jmp .post_done
.post1: cmp ax, 0FFF8h
        jne .post_done
        mov si, s_req
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        mov cx, 11
        call ldump
        call lnl
        test byte [o_fl], 1
        jnz .post_done
        mov es, [i_es]
        mov si, [i_di]
        les si, [es:si]         ; grlpData
        mov ax, es
        or ax, si
        jz .post_done
        mov si, s_data
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        les si, [es:si]
        mov cx, 32              ; the GRABST header and a little more
        call ldump
        mov si, s_sum
        call lstr
        mov es, [i_es]
        mov si, [i_di]
        les si, [es:si]
        mov cx, [o_ax]
        call lsum
        call lnl
.post_done:
        call flush
        ; return as the grabber did (its DS/ES given back as ours if they
        ; were its own segment)
        mov ax, [o_ds]
        cmp ax, [target+2]
        jne .ds1
        mov ax, cs
.ds1:   mov [o_ds], ax
        mov ax, [o_es]
        cmp ax, [target+2]
        jne .es1
        mov ax, cs
.es1:   mov [o_es], ax
        push word [o_fl]
        mov ax, [o_ax]
        mov bx, [o_bx]
        mov cx, [o_cx]
        mov dx, [o_dx]
        mov si, [o_si]
        mov di, [o_di]
        mov bp, [o_bp]
        mov es, [o_es]
        mov ds, [cs:o_ds]
        popf
        retf

; INT 10h during a call: "  i ax bx cx dx", then the previous handler
i10hook:
        cmp byte [cs:i10on], 0
        je .chain
        push ds
        push si
        push cx
        push ax
        push cs
        pop ds
        mov [t_ax], ax
        mov [t_bx], bx
        mov [t_cx], cx
        mov [t_dx], dx
        mov si, s_i10
        call lstr
        mov si, t_ax
        mov cx, 4
        call lregs
        call lnl
        pop ax
        pop cx
        pop si
        pop ds
.chain: jmp far [cs:i10old]

; " mode cols rows tick"
lbios:
        push es
        mov ax, 40h
        mov es, ax
        mov al, ' '
        call lchr
        mov al, [es:49h]
        call lhex2
        mov al, ' '
        call lchr
        mov al, [es:4Ah]
        call lhex2
        mov al, ' '
        call lchr
        mov al, [es:84h]
        call lhex2
        mov al, ' '
        call lchr
        mov ax, [es:6Ch]
        call lhex4
        pop es
        ret

; CX words at DS:SI, each " xxxx"
lregs:
.l:     mov al, ' '
        call lchr
        lodsw
        call lhex4
        loop .l
        ret

; CX bytes at ES:SI as hex
ldump:
.l:     mov al, [es:si]
        inc si
        call lhex2
        loop .l
        ret

; " n=xxxx s=xxxx": CX bytes at ES:SI, their 16-bit sum and xor-rotate
lsum:
        push cx
        xor bx, bx
        xor dx, dx
        jcxz .e
.l:     xor ax, ax
        mov al, [es:si]
        inc si
        add bx, ax
        rol dx, 1
        xor dl, al
        loop .l
.e:     pop ax
        call lhex4
        mov al, ' '
        call lchr
        mov ax, bx
        call lhex4
        mov al, ' '
        call lchr
        mov ax, dx
        call lhex4
        ret

; ASCIIZ at ES:SI (at most 80)
lasciiz:
        mov cx, 80
.l:     mov al, [es:si]
        inc si
        or al, al
        jz .e
        call lchr
        loop .l
.e:     ret

lstr:   lodsb
        or al, al
        jz .e
        call lchr
        jmp lstr
.e:     ret

lnl:    mov al, 13
        call lchr
        mov al, 10
        jmp lchr

lhex4:  push ax
        mov al, ah
        call lhex2
        pop ax
lhex2:  push ax
        shr al, 4
        call .n
        pop ax
.n:     and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe lchr
        add al, 7
lchr:   push bx
        mov bx, [cs:logpos]
        cmp bx, LOGMAX
        jae .full
        mov [cs:logbuf+bx], al
        inc word [cs:logpos]
.full:  pop bx
        ret

; write the buffer to C:\GR1.LOG when DOS is free
flush:
        cmp word [logpos], 0
        je .e
        les bx, [indos]
        cmp byte [es:bx], 0
        jne .e
        mov dx, s_file
        mov ax, 3D01h
        int 21h
        jnc .op
        mov dx, s_file
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .e
.op:    mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov dx, logbuf
        mov cx, [logpos]
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov word [logpos], 0
.e:     ret

s_hello: db 'GR1 grabber at ', 0
s_swap: db '  swap ', 0
s_req:  db '  req ', 0
s_info: db '  info ', 0
s_data: db '  data ', 0
s_sum:  db ' n s x ', 0
s_i10:  db '  i', 0
s_file: db 'C:\GR1.LOG', 0

        align 2
fn:     db 0
inited: db 0
i10on:  db 0
i10chained: db 0
target: dd 0
i10old: dd 0
indos:  dd 0
logpos: dw 0
i_ax:   dw 0
i_bx:   dw 0
i_cx:   dw 0
i_dx:   dw 0
i_si:   dw 0
i_di:   dw 0
i_bp:   dw 0
i_ds:   dw 0
i_es:   dw 0
i_fl:   dw 0
i_sp:   dw 0
i_ss:   dw 0
o_ax:   dw 0
o_bx:   dw 0
o_cx:   dw 0
o_dx:   dw 0
o_si:   dw 0
o_di:   dw 0
o_bp:   dw 0
o_ds:   dw 0
o_es:   dw 0
o_fl:   dw 0
o_sp:   dw 0
t_ax:   dw 0
t_bx:   dw 0
t_cx:   dw 0
t_dx:   dw 0
        times (GR_OFS - LOGMAX) - ($ - $$) db 0
logbuf: times LOGMAX db 0
