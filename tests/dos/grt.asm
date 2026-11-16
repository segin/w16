; grt.asm - GRT.COM: calls a Standard-mode display grabber (.2GR) directly,
; without Windows, the way DSWAP does (Windows 3.1 DDK, "Display Grabbers";
; the calls and registers DSWAP uses as observed with GR1, ARCHITECTURE.md
; 9.11), and records what it answers (tests/grabber.sh compares two
; grabbers' records).
;
;   GRT file mode [flags]       mode in hex (as GV1); flags:
;       p   change the palette too (GV1's "p")
;       w   wait for a key (screenshots) after the switch away and after
;           the restore
;       l   InitScreen with 50 lines (default 25)
;
; Steps, each recorded in C:\GRT.LOG (appended): the file loaded at offset
; 0 of a segment of its own (CS = DS = that segment for every call);
; SetSwapDrive('C', "C:\TMP\~WOAFFEE.TMP"); InquireSave(1), (2);
; InquireGrab(1), (2); GetVersion; EnableSave; InitScreen(25); GV1's screen
; for the mode (tests/dos/gvdraw.inc); GetInfo; InquireGrab(1), (2);
; GetBlock: clipboard format (size, then the data: its GRABST header and a
; checksum; the data also to C:\GRTmm.BIN), native format (size,
; checksum), a 10x4 block at 2,3 (clipboard format); a snapshot of the
; display (the VGA's registers, the DAC, the BIOS's video fields, the four
; planes' checksums); SaveScreen; DisableSave; InitScreen(25); the screen
; scribbled over (every plane, the DAC) as Windows would; EnableSave;
; RestoreScreen; the snapshot again and every difference from the first;
; DisableSave; InitScreen(25). The INT 10h calls the grabber makes inside
; each call are recorded too (AX BX CX DX).

        bits 16
        cpu 386
        org 100h

LOGSZ   equ 6000h

start:
        mov sp, stack_top
        mov bx, (prog_end - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h
        ; arguments: file, mode, flags
        mov si, 81h
        call skip
        mov di, gname
.n:     lodsb
        cmp al, ' '
        jbe .ne
        stosb
        jmp .n
.ne:    mov byte [di], 0
        dec si
        call skip
        call hexbyte
        mov [mode], al
.fl:    lodsb
        cmp al, 13
        je .fe
        cmp al, 'p'
        jne .f1
        mov byte [pal], 1
.f1:    cmp al, 'w'
        jne .f2
        mov byte [waitk], 1
.f2:    cmp al, 'l'
        jne .fl
        mov word [lines], 50
        jmp .fl
.fe:
        ; the grabber's file
        mov dx, gname
        mov ax, 3D00h
        int 21h
        jc fail
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov [gsize], ax
        mov ax, 4200h
        xor cx, cx
        xor dx, dx
        int 21h
        mov [gfh], bx
        mov bx, 1000h           ; 64 KB for it (and DSWAP's request block
        mov ah, 48h             ; after it, as DSWAP has it)
        int 21h
        jc fail
        mov [gseg], ax
        mov bx, [gfh]
        push ds
        mov ds, ax
        xor dx, dx
        mov cx, [cs:gsize]
        mov ah, 3Fh
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h
        mov bx, 1000h           ; the grab buffer (64 KB)
        mov ah, 48h
        int 21h
        jc fail
        mov [bseg], ax

        mov si, s_head
        call lstr
        mov si, gname
        call lstr
        mov si, s_mode
        call lstr
        mov al, [mode]
        call lhex2
        mov si, s_size
        call lstr
        mov ax, [gsize]
        call lhex4
        call lnl

        ; SetSwapDrive
        mov ax, 0FFFDh
        mov bx, 'C'
        mov di, swaptpl
        push ds
        pop es
        mov si, s_swapd
        call gcall
        ; InquireSave 1, 2
        mov ax, 1
        mov si, s_isave1
        mov word [goff], 0Ch
        call gcall0
        mov [svsize], ax
        mov [svsize+2], dx
        mov ax, 2
        mov si, s_isave2
        mov word [goff], 0Ch
        call gcall0
        call igrab
        mov ax, 0FFFAh
        mov si, s_ver
        call gcall
        mov ax, 0FFFCh
        mov si, s_enable
        call gcall
        mov ax, [lines]
        mov si, s_init
        mov word [goff], 15h
        call gcall0
        call flush

        ; the save buffer
        mov bx, [svsize]
        add bx, 15
        shr bx, 4
        mov ax, [svsize+2]
        shl ax, 12
        add bx, ax
        inc bx
        mov ah, 48h
        int 21h
        jc fail
        mov [sseg], ax

        ; the program's screen
        call gv_draw
        ; GetInfo
        mov ax, 0FFFEh
        mov di, ginfo
        push ds
        pop es
        mov si, s_info
        call gcall
        mov si, s_infod
        call lstr
        mov si, ginfo
        push ds
        pop es
        mov cx, 48
        call ldump
        call lnl
        call igrab
        ; GetBlock, clipboard format: size, data
        mov byte [req_style], 80h
        call getblock
        ; native
        mov byte [req_style], 00h
        call getblock
        ; a block
        mov byte [req_style], 80h
        mov word [req_org], 0302h
        mov word [req_ext], 040Ah
        call getblock
        mov word [req_org], 0
        mov word [req_ext], 0
        call flush

        ; the snapshot, SaveScreen, away
        mov di, snap1
        call snapshot
        mov ax, [svsize]
        mov es, [sseg]
        xor di, di
        mov si, s_save
        mov word [goff], 0Fh
        call gcall0e
        mov ax, 0FFFBh
        mov si, s_disable
        call gcall
        mov ax, [lines]
        mov si, s_init
        mov word [goff], 15h
        call gcall0
        call flush
        call waitkey
        call scribble
        ; back
        mov ax, 0FFFCh
        mov si, s_enable
        call gcall
        mov ax, [svsize]
        mov es, [sseg]
        xor di, di
        mov si, s_restore
        mov word [goff], 12h
        call gcall0e
        mov di, snap2
        call snapshot
        call compare
        call flush
        call waitkey
        mov ax, 0FFFBh
        mov si, s_disable
        call gcall
        mov ax, [lines]
        mov si, s_init
        mov word [goff], 15h
        call gcall0
        mov si, s_end
        call lstr
        call lnl
        call flush
        mov ax, 4C00h
        int 21h

fail:   mov si, s_fail
        call lstr
        call lnl
        call flush
        mov ax, 4C01h
        int 21h

waitkey:
        cmp byte [waitk], 0
        je .e
        mov ah, 0
        int 16h
.e:     ret

; InquireGrab 1 and 2
igrab:
        mov ax, 1
        mov si, s_igrab1
        call gcall
        mov ax, 2
        mov si, s_igrab2
        jmp gcall

; GetBlock with the request in req_*: first with no buffer (the size),
; then into the grab buffer when it fits
getblock:
        mov word [req_ptr], 0
        mov word [req_ptr+2], 0
        call req_put
        mov ax, 0FFF8h
        mov si, s_gblk0
        call gcall_req
        jc .e
        mov [gsz0], ax
        mov ax, [bseg]
        mov word [req_ptr], 0
        mov [req_ptr+2], ax
        call req_put
        ; the buffer filled with a marker first
        mov es, [bseg]
        xor di, di
        mov cx, 8000h
        mov ax, 0A5A5h
        rep stosw
        mov ax, 0FFF8h
        mov si, s_gblk1
        call gcall_req
        jc .e
        cmp ax, [gsz0]          ; (3.1's answers less here in CGA modes:
        jae .l1                 ; the data as long as the size it gave)
        mov ax, [gsz0]
.l1:    mov [glen], ax
        mov si, s_gdata
        call lstr
        mov es, [bseg]
        xor si, si
        mov cx, 32
        call ldump
        mov si, s_sum
        call lstr
        mov es, [bseg]
        xor si, si
        mov cx, [glen]
        add cx, 8               ; and what follows (the marker or not)
        call lsum
        call lnl
        ; the data to C:\GRTmm[N|B].BIN
        mov al, [mode]
        call hexdig
        mov [binname+6], ax
        mov al, 'O'
        test byte [req_style], 80h
        jnz .o
        mov al, 'N'
.o:     cmp word [req_ext], 0
        je .o2
        mov al, 'B'
.o2:    mov [binname+8], al
        mov dx, binname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .e
        mov bx, ax
        push ds
        mov cx, [glen]
        mov ds, [bseg]
        xor dx, dx
        mov ah, 40h
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h
.e:     ret

; the request block after the grabber's image (where DSWAP puts it)
req_put:
        mov es, [gseg]
        mov di, [gsize]
        mov si, req
        mov cx, 11
        rep movsb
        ret

gcall_req:
        mov es, [gseg]
        mov di, [gsize]
        call gcall
        pushf
        push ax
        mov si, s_reqo
        call lstr
        mov es, [gseg]
        mov si, [gsize]
        mov cx, 11
        call ldump
        call lnl
        pop ax
        popf
        ret

; call function at offset 0 with AX..., SI = its name
gcall:  mov word [goff], 0
        jmp gcall0e
gcall0: push ds
        pop es
gcall0e:
        mov [c_ax], ax
        mov [c_bx], bx
        mov [c_cx], cx
        mov [c_dx], dx
        mov [c_di], di
        mov [c_es], es
        call lstr
        mov si, s_in
        call lstr
        mov ax, [c_ax]
        call lhex4
        call lnl
        ; INT 10h recorded meanwhile
        xor ax, ax
        mov es, ax
        cmp byte [i10chained], 0
        jne .hooked
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
        mov ax, [goff]
        mov [target], ax
        mov ax, [gseg]
        mov [target+2], ax
        mov ax, [c_ax]
        mov bx, [c_bx]
        mov cx, [c_cx]
        mov dx, [c_dx]
        mov di, [c_di]
        mov es, [c_es]
        xor si, si
        xor bp, bp
        push ds
        mov ds, [cs:target+2]
        clc
        call far [cs:target]
        pop ds
        pushf
        mov [r_ax], ax
        mov [r_bx], bx
        mov [r_cx], cx
        mov [r_dx], dx
        mov [r_si], si
        mov [r_di], di
        pop ax
        mov [r_fl], ax
        mov byte [i10on], 0
        xor ax, ax
        mov es, ax
        cli
        mov byte [i10chained], 1
        cmp word [es:40h], i10hook
        jne .un
        mov ax, [es:42h]
        mov bx, cs
        cmp ax, bx
        jne .un
        mov ax, [i10old]
        mov [es:40h], ax
        mov ax, [i10old+2]
        mov [es:42h], ax
        mov byte [i10chained], 0
.un:    sti
        mov si, s_out
        call lstr
        mov ax, [r_ax]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [r_bx]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [r_cx]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [r_dx]
        call lhex4
        mov si, s_cf
        call lstr
        mov ax, [r_fl]
        and al, 1
        add al, '0'
        call lchr
        call lnl
        push word [r_fl]
        popf
        mov ax, [r_ax]
        mov dx, [r_dx]
        ret

i10hook:
        cmp byte [cs:i10on], 0
        je .c
        push ds
        push si
        push ax
        push cs
        pop ds
        mov [t_ax], ax
        mov [t_bx], bx
        mov [t_cx], cx
        mov [t_dx], dx
        mov si, s_i10
        call lstr
        mov ax, [t_ax]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [t_bx]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [t_cx]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [t_dx]
        call lhex4
        call lnl
        pop ax
        pop si
        pop ds
.c:     jmp far [cs:i10old]

; ---- the display's state into DI (SNAPSZ bytes)
; misc 1, seq 5, crtc 25, gc 9, attr 21, pel mask 1, dac 768, bda 37,
; planes 4 x (sum, rx)
SN_MISC equ 0
SN_SEQ  equ 1
SN_CRTC equ 6
SN_GC   equ 31
SN_ATTR equ 40
SN_PEL  equ 61
SN_DAC  equ 62
SN_BDA  equ 830
SN_PL   equ 867
SNAPSZ  equ 883

snapshot:
        push ds
        pop es
        mov dx, 3CCh
        in al, dx
        mov [di+SN_MISC], al
        mov bx, 3D4h
        test al, 1
        jnz .c
        mov bx, 3B4h
.c:     mov [crtc], bx
        lea si, [di+SN_SEQ]
        mov dx, 3C4h
        mov cx, 5
        call rdidx
        lea si, [di+SN_CRTC]
        mov dx, [crtc]
        mov cx, 25
        call rdidx
        lea si, [di+SN_GC]
        mov dx, 3CEh
        mov cx, 9
        call rdidx
        ; attribute controller: index and data at 3C0/3C1
        xor cx, cx
.at:    mov dx, [crtc]
        add dx, 6               ; 3DAh/3BAh: reset the flip-flop
        in al, dx
        mov dx, 3C0h
        mov al, cl
        out dx, al
        inc dx
        in al, dx
        mov bx, cx
        mov [di+SN_ATTR+bx], al
        inc cx
        cmp cx, 21
        jb .at
        mov dx, [crtc]
        add dx, 6
        in al, dx
        mov dx, 3C0h
        mov al, 20h             ; the display on again
        out dx, al
        mov dx, 3C6h
        in al, dx
        mov [di+SN_PEL], al
        mov dx, 3C7h
        xor al, al
        out dx, al
        mov dx, 3C9h
        lea bx, [di+SN_DAC]
        mov cx, 768
.dac:   in al, dx
        mov [bx], al
        inc bx
        loop .dac
        push ds
        lea bx, [di+SN_BDA]
        mov ax, 40h
        mov ds, ax
        mov si, 49h
        mov cx, 30
.b1:    lodsb
        mov [cs:bx], al
        inc bx
        loop .b1
        mov si, 84h
        mov cx, 7
.b2:    lodsb
        mov [cs:bx], al
        inc bx
        loop .b2
        pop ds
        ; the planes, read as planes (sequencer memory mode, graphics
        ; mode and miscellaneous kept and put back)
        mov al, [di+SN_SEQ+4]
        mov [k_seq4], al
        mov al, [di+SN_GC+4]
        mov [k_gc4], al
        mov al, [di+SN_GC+5]
        mov [k_gc5], al
        mov al, [di+SN_GC+6]
        mov [k_gc6], al
        mov dx, 3C4h
        mov ax, 0604h
        out dx, ax
        mov dx, 3CEh
        mov ax, 0005h
        out dx, ax
        mov ax, 0506h
        out dx, ax
        xor cx, cx
.pl:    mov dx, 3CEh
        mov al, 4
        mov ah, cl
        out dx, ax
        push cx
        push di
        mov ax, 0A000h
        mov es, ax
        xor si, si
        xor bx, bx
        xor dx, dx
        mov cx, 0               ; 65536
.ps:    xor ax, ax
        mov al, [es:si]
        inc si
        add bx, ax
        rol dx, 1
        xor dl, al
        dec cx
        jnz .ps
        pop di
        pop cx
        mov si, cx
        shl si, 2
        add si, di
        mov [si+SN_PL], bx
        mov [si+SN_PL+2], dx
        inc cx
        cmp cx, 4
        jb .pl
        mov dx, 3C4h
        mov al, 4
        mov ah, [k_seq4]
        out dx, ax
        mov dx, 3CEh
        mov al, 4
        mov ah, [k_gc4]
        out dx, ax
        mov al, 5
        mov ah, [k_gc5]
        out dx, ax
        mov al, 6
        mov ah, [k_gc6]
        out dx, ax
        push ds
        pop es
        ret

; CX registers from index 0 of the pair at DX into [SI]
rdidx:  xor bx, bx
.l:     mov al, bl
        out dx, al
        inc dx
        in al, dx
        dec dx
        mov [si+bx], al
        inc bx
        loop .l
        ret

; every plane and the DAC overwritten (Windows using the display)
scribble:
        mov ax, 0012h
        int 10h
        mov dx, 3C4h
        mov ax, 0F02h
        out dx, ax
        mov ax, 0A000h
        mov es, ax
        xor di, di
        mov cx, 8000h
        mov ax, 5A3Ch
        rep stosw
        mov dx, 3C8h
        xor al, al
        out dx, al
        inc dx
        mov cx, 768
.d:     mov al, cl
        and al, 3Fh
        out dx, al
        loop .d
        push ds
        pop es
        ret

; the differences between snap1 and snap2
compare:
        mov si, s_cmp
        call lstr
        call lnl
        xor bx, bx
.l:     cmp bx, SN_PL
        jae .pl
        mov al, [snap1+bx]
        cmp al, [snap2+bx]
        je .n
        cmp bx, SN_DAC
        jb .r
        cmp bx, SN_BDA
        jae .r
        inc word [dacdiff]
        jmp .n
.r:     push bx
        mov si, s_diff
        call lstr
        mov ax, bx
        call lhex4
        mov al, ' '
        call lchr
        mov al, [snap1+bx]
        call lhex2
        mov al, ' '
        call lchr
        mov al, [snap2+bx]
        call lhex2
        call lnl
        pop bx
.n:     inc bx
        jmp .l
.pl:    mov si, s_dacd
        call lstr
        mov ax, [dacdiff]
        call lhex4
        call lnl
        xor cx, cx
.p:     mov si, s_plane
        call lstr
        mov al, cl
        call lhex2
        mov al, ' '
        call lchr
        mov bx, cx
        shl bx, 2
        mov ax, [snap1+SN_PL+bx]
        call lhex4
        mov ax, [snap1+SN_PL+bx+2]
        call lhex4
        mov al, ' '
        call lchr
        mov ax, [snap2+SN_PL+bx]
        call lhex4
        mov ax, [snap2+SN_PL+bx+2]
        call lhex4
        mov ax, [snap1+SN_PL+bx]
        cmp ax, [snap2+SN_PL+bx]
        jne .pd
        mov ax, [snap1+SN_PL+bx+2]
        cmp ax, [snap2+SN_PL+bx+2]
        jne .pd
        mov si, s_same
        jmp .ps
.pd:    mov si, s_differs
.ps:    call lstr
        call lnl
        inc cx
        cmp cx, 4
        jb .p
        ret

; ---- the log
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

; CX bytes at ES:SI as hex
ldump:
.l:     mov al, [es:si]
        inc si
        call lhex2
        loop .l
        ret

; " n=CX sum rx" of CX bytes at ES:SI
lsum:   push cx
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
        jmp lhex4

hexdig: mov ah, al              ; AL as two hex digits: AL high, AH low
        shr al, 4
        call .d
        xchg al, ah
        and al, 0Fh
        call .d
        xchg al, ah
        ret
.d:     add al, '0'
        cmp al, '9'
        jbe .e
        add al, 7
.e:     ret

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
        cmp bx, LOGSZ
        jae .f
        mov [cs:logbuf+bx], al
        inc word [cs:logpos]
.f:     pop bx
        ret

flush:  push ax
        push bx
        push cx
        push dx
        push es
        cmp word [logpos], 0
        je .e
        mov dx, s_log
        mov ax, 3D01h
        int 21h
        jnc .o
        mov dx, s_log
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .e
.o:     mov bx, ax
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
.e:     pop es
        pop dx
        pop cx
        pop bx
        pop ax
        ret

skip:   cmp byte [si], ' '
        jne .e
        inc si
        jmp skip
.e:     ret

hexbyte:
        mov ax, [si]
        mov [title+4], ax
        xor ah, ah
        mov cx, 2
.l:     mov al, [si]
        sub al, '0'
        cmp al, 9
        jbe .d
        and al, 0DFh
        sub al, 7
.d:     shl ah, 4
        or ah, al
        inc si
        loop .l
        mov al, ah
        ret

%include "gvdraw.inc"

s_head:  db '== GRT ', 0
s_mode:  db ' mode ', 0
s_size:  db ' size ', 0
s_swapd: db 'SetSwapDrive', 0
s_isave1: db 'InquireSave(1)', 0
s_isave2: db 'InquireSave(2)', 0
s_igrab1: db 'InquireGrab(1)', 0
s_igrab2: db 'InquireGrab(2)', 0
s_ver:   db 'GetVersion', 0
s_enable: db 'EnableSave', 0
s_disable: db 'DisableSave', 0
s_init:  db 'InitScreen', 0
s_info:  db 'GetInfo', 0
s_infod: db '  info ', 0
s_gblk0: db 'GetBlock(size)', 0
s_gblk1: db 'GetBlock(data)', 0
s_reqo:  db '  req ', 0
s_gdata: db '  data ', 0
s_sum:   db ' n+8 s x ', 0
s_save:  db 'SaveScreen', 0
s_restore: db 'RestoreScreen', 0
s_in:    db ' in ', 0
s_out:   db '  out ', 0
s_cf:    db ' cf ', 0
s_i10:   db '  int10 ', 0
s_cmp:   db '-- after RestoreScreen, against before SaveScreen:', 0
s_diff:  db '  differs ', 0
s_dacd:  db '  dac bytes differing ', 0
s_plane: db '  plane ', 0
s_same:  db ' same', 0
s_differs: db ' DIFFERS', 0
s_end:   db '== end', 0
s_fail:  db '== GRT failed', 0
s_log:   db 'C:\GRT.LOG', 0
swaptpl: db 'C:\TMP\~WOAFFEE.TMP', 0
binname: db 'C:\GRTmmx.BIN', 0

        align 2
waitk:  db 0
i10on:  db 0
i10chained: db 0
k_seq4: db 0
k_gc4:  db 0
k_gc5:  db 0
k_gc6:  db 0
lines:  dw 25
gsize:  dw 0
gfh:    dw 0
gseg:   dw 0
bseg:   dw 0
sseg:   dw 0
goff:   dw 0
glen:   dw 0
gsz0:   dw 0
crtc:   dw 3D4h
dacdiff: dw 0
svsize: dd 0
target: dd 0
i10old: dd 0
req:
req_ptr: dd 0
req_org: dw 0
req_ext: dw 0
req_style: db 0
req_fill: dw 0
c_ax:   dw 0
c_bx:   dw 0
c_cx:   dw 0
c_dx:   dw 0
c_di:   dw 0
c_es:   dw 0
r_ax:   dw 0
r_bx:   dw 0
r_cx:   dw 0
r_dx:   dw 0
r_si:   dw 0
r_di:   dw 0
r_fl:   dw 0
t_ax:   dw 0
t_bx:   dw 0
t_cx:   dw 0
t_dx:   dw 0
logpos: dw 0
gname:  times 80 db 0
ginfo:  times 48 db 0
snap1:  times SNAPSZ db 0
snap2:  times SNAPSZ db 0
logbuf: times LOGSZ db 0
        times 512 db 0
stack_top:
prog_end:
