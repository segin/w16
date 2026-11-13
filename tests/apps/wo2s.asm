; wo2s.asm - WO2's protected-mode interrupt hooks (see wo2.c).
;
; hook_setup(alias, dgroup) stores our DGROUP selector in the code segment
; (through a data alias of it); hook_install(alias, n) chains INT n (2Fh,
; 31h or 21h) through DPMI INT 31h 0204h/0205h. Each hooked call that
; passes the filter gets a record in the buffer at bufsel:bufpos, REC words:
;   0 int, 1-8 ax bx cx dx si di ds es (in), 9-10 ip cs (caller),
;   11-18 ax bx cx dx si di ds es (out), 19 flags (out), 20 BIOS tick,
;   21 flags (in), 22 state (1 = still running, 2 = returned)
; The record is reserved before the call is passed on and completed when
; it returns, so calls made meanwhile (a WinExec that runs the new task, a
; switch to a DOS session that returns minutes later) are recorded too.
;
; INT 2Fh stays first in the chain: while the INT 21h hook is installed,
; INT 21h AX=352Fh returns (and AX=252Fh replaces) the handler our INT 2Fh
; hook passes calls to, so a display driver that restores "its" saved
; vector when it is disabled (DisableOEMLayer) cannot unlink us.
        bits 16
        cpu 286

REC     equ 23                          ; words per record
BLKSZ   equ 540h
LINSZ   equ 400h                        ; bytes at the linear address in block+185h
SNAPSZ  equ BLKSZ + LINSZ
NSNAP   equ 12
BUFMAX  equ 65000

segment _DATA public align=2 class=DATA use16
segment CONST public align=2 class=DATA use16
segment CONST2 public align=2 class=DATA use16
segment _BSS public align=2 class=BSS use16
group DGROUP _DATA CONST CONST2 _BSS

segment _DATA
global _bufsel, _bufpos, _dropped, _n1680, _sel40, _f21, _f31, _lsl06
global _watchcs, _blksel, _blkoff, _snapsel, _nsnap, _blk_dx, _blk_ax, _linsel
global _snap_now
_bufsel  dw 0
_bufpos  dw 0
_dropped dw 0
_n1680   dw 0
_lsl06   dw 0                   ; limit of the selector of the last INT 31h 0006h
_sel40   dw 0
; functions recorded: one bit per AH
_f21     times 32 db 0
_f31     times 32 db 0
_watchcs dw 0
; WIN.COM's switcher block (INT 2Fh 4A05h SI=5): snapshots of BLKSZ bytes
; at each INT 2Fh 4B80h (a) and when it returns (b), into _snapsel:
; 2 * BLKSZ per switch, up to NSNAP switches
_blk_dx  dw 0
_blk_ax  dw 0
_blksel  dw 0
_blkoff  dw 0
_snapsel dw 0
_nsnap   dw 0
_linsel  dw 0
busy     db 0
snappend db 0
         db 0
cur      times REC dw 0

segment WO2_HOOK public align=16 class=CODE use16
dsel    dw 0
old2f   dd 0
old31   dd 0
old21   dd 0
have2f  db 0
alias   dw 0                            ; a writable alias of this segment

; void far hook_setup(WORD alias, WORD dgroup)
global _hook_setup
_hook_setup:
        push bp
        mov bp, sp
        push es
        mov es, [bp + 6]
        mov ax, [bp + 8]
        mov [es:dsel], ax
        mov [es:alias], es
        pop es
        pop bp
        retf

; WORD far hook_install(WORD alias, WORD intno): 0 = done
global _hook_install
_hook_install:
        push bp
        mov bp, sp
        push es
        push bx
        push di
        mov bl, [bp + 8]
        mov ax, 0204h
        int 31h
        jc .fail
        mov es, [bp + 6]
        mov ax, [bp + 8]
        mov di, old2f
        cmp al, 2Fh
        je .s
        mov di, old31
        cmp al, 31h
        je .s
        mov di, old21
.s:     mov [es:di], dx
        mov [es:di + 2], cx
        mov bl, al
        mov cx, cs
        mov dx, h2f
        cmp bl, 2Fh
        je .set
        mov dx, h31
        cmp bl, 31h
        je .set
        mov dx, h21
.set:   mov ax, 0205h
        int 31h
        jc .fail
        cmp bl, 2Fh
        jne .ok
        mov byte [es:have2f], 1
.ok:    xor ax, ax
        jmp .r
.fail:  mov ax, 1
.r:     pop di
        pop bx
        pop es
        pop bp
        retf

; void far hook_remove(WORD alias, WORD intno)
global _hook_remove
_hook_remove:
        push bp
        mov bp, sp
        push es
        push bx
        mov bl, [bp + 8]
        mov dx, [cs:old2f]
        mov cx, [cs:old2f + 2]
        cmp bl, 2Fh
        je .set
        mov dx, [cs:old31]
        mov cx, [cs:old31 + 2]
        cmp bl, 31h
        je .set
        mov dx, [cs:old21]
        mov cx, [cs:old21 + 2]
.set:   mov ax, 0205h
        int 31h
        cmp bl, 2Fh
        jne .r
        mov es, [bp + 6]
        mov byte [es:have2f], 0
.r:     pop bx
        pop es
        pop bp
        retf

; ---- the hooks ----
; stack on entry: ip cs flags
%macro HOOK 3                           ; intno, old, label
%3:
%if %1 = 21h
        cmp byte [cs:have2f], 0
        je .nv
        cmp ax, 352Fh
        je .get2f
        cmp ax, 252Fh
        je .set2f
.nv:
%endif
        push ds
        mov ds, [cs:dsel]
        cmp byte [busy], 0
        jne .pass
%if %1 = 2Fh
        cmp ax, 1680h
        je .idle
        cmp ax, 1689h
        je .idle
        cmp ax, 4A05h                   ; WINOLDAP asks for WIN.COM's block:
        jne .nw                         ; watch its code segment from now on
        cmp si, 5
        jne .nw
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 6]
        mov [_watchcs], ax
        pop ax
        pop bp
.nw:    cmp ax, 4B80h
        jne .rec
        call snap_a                     ; the block as the switch starts
%else
        push bp
        mov bp, sp
        push ax
        mov ax, [bp + 6]                ; the caller's CS
        cmp ax, [_watchcs]
        pop ax
        pop bp
        je .rec
        push bx
        push cx
        mov bl, ah
        shr bl, 3
        xor bh, bh
        mov cl, ah
        and cl, 7
        mov ch, 1
        shl ch, cl
%if %1 = 21h
        test [_f21 + bx], ch
%else
        test [_f31 + bx], ch
%endif
        pop cx
        pop bx
        jz .pass
%endif
        jmp .rec
%if %1 = 21h
.get2f: les bx, [cs:old2f]
        iret
.set2f: push es
        mov es, [cs:alias]
        mov [es:old2f], dx
        mov [es:old2f + 2], ds
        pop es
        iret
%endif
.idle:  inc word [_n1680]
.pass:  pop ds
        jmp far [cs:%2]
.rec:   mov byte [busy], 1
        mov word [cur], %1
        mov [cur + 2], ax
        mov [cur + 4], bx
        mov [cur + 6], cx
        mov [cur + 8], dx
        mov [cur + 10], si
        mov [cur + 12], di
        mov [cur + 16], es
        push bp
        mov bp, sp
        mov ax, [bp + 2]
        mov [cur + 14], ax              ; caller's DS
        mov ax, [bp + 4]
        mov [cur + 18], ax              ; ip
        mov ax, [bp + 6]
        mov [cur + 20], ax              ; cs
        mov ax, [bp + 8]
        mov [cur + 42], ax              ; flags
        pop bp
        call reserve                    ; AX = the slot (0: none)
        push ax                         ; slot dsE ip cs flags
        push word [cur + 42]
        push word [cur + 14]
        mov ax, [cur + 2]
        mov byte [busy], 0
        pop ds                          ; the caller's DS and flags
        popf                            ; (stack: slot dsE ip cs flags)
        pushf
        call far [cs:%2]                ; as INT n
        pushf                           ; flo slot dsE ip cs flags
        push es
        push di
        push bp
        mov bp, sp                      ; bp di es flo slot dsE ip cs flags
        mov di, [bp + 8]
        test di, di
        jz .nr
        push ds
        mov ds, [cs:dsel]
        mov es, [_bufsel]
        pop word [es:di + 34]           ; DS out
        mov [es:di + 22], ax
        mov [es:di + 24], bx
        mov [es:di + 26], cx
        mov [es:di + 28], dx
        mov [es:di + 30], si
        push ax
        mov ax, [bp + 2]
        mov [es:di + 32], ax            ; DI out
        mov ax, [bp + 4]
        mov [es:di + 36], ax            ; ES out
        mov ax, [bp + 6]
        mov [es:di + 38], ax            ; flags out
        mov word [es:di + 44], 2
%if %1 = 31h
        cmp word [es:di + 2], 0006h     ; 0006h: note the selector's limit
        jne .nl                         ; (WINOLDAP's buffer: its size)
        push ax
        lsl ax, bx
        jnz .nl0
        mov [_lsl06], ax
.nl0:   pop ax
.nl:
%endif
%if %1 = 21h
        cmp byte [es:di + 3], 0Bh       ; the DOS call after INT 2Fh 4B80h:
        jne .nb                         ; the switch happens inside it
        cmp byte [snappend], 0
        je .nb
        mov byte [snappend], 0
        call snap_b
.nb:
%endif
        mov ds, [es:di + 34]
        pop ax
.nr:    push ax                         ; return over slot and dsE:
        mov ax, [bp + 12]               ; ip cs ip cs flags, then retf 6
        mov [bp + 8], ax
        mov ax, [bp + 14]
        mov [bp + 10], ax
        pop ax
        pop bp
        pop di
        pop es
        popf                            ; flags out
        retf 6
%endmacro


HOOK 2Fh, old2f, h2f
HOOK 31h, old31, h31
HOOK 21h, old21, h21

; reserve a record and copy cur into it (DS = DGROUP); AX = its offset or 0
reserve:
        push es
        push si
        push di
        push cx
        mov ax, [_sel40]
        test ax, ax
        jz .nt
        mov es, ax
        mov ax, [es:6Ch]
.nt:    mov [cur + 40], ax
        mov word [cur + 44], 1
        xor ax, ax
        mov di, [_bufpos]
        cmp di, BUFMAX - REC * 2
        ja .drop
        cmp word [_bufsel], 0
        je .drop
        mov es, [_bufsel]
        mov ax, di
        mov si, cur
        mov cx, REC
        cld
        rep movsw
        mov [_bufpos], di
        jmp .r
.drop:  inc word [_dropped]
.r:     pop cx
        pop di
        pop si
        pop es
        ret

; copy the block to the next snapshot slot (DS = DGROUP); keeps registers
snap_a:
        push ax
        mov ax, [_nsnap]
        cmp ax, NSNAP
        jae .r
        push dx
        mov dx, 2 * SNAPSZ
        mul dx
        pop dx
        call snap
        inc word [_nsnap]
        mov byte [snappend], 1
.r:     pop ax
        ret
snap_b:
        push ax
        mov ax, [_nsnap]
        test ax, ax
        jz .r
        dec ax
        push dx
        mov dx, 2 * SNAPSZ
        mul dx
        pop dx
        add ax, SNAPSZ
        call snap
.r:     pop ax
        ret
; void far snap_now(WORD offset): a snapshot from C (DS = DGROUP)
_snap_now:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        call snap
        pop bp
        retf
; AX = offset in the snapshot segment (DS = DGROUP): the block, then
; LINSZ bytes at the linear address in its dword at 185h (via _linsel)
snap:
        cmp word [_blksel], 0
        je .r
        cmp word [_snapsel], 0
        je .r
        push es
        push si
        push di
        push cx
        push bx
        push dx
        push ax
        push ds
        mov di, ax
        mov es, [_snapsel]
        mov bx, [_linsel]
        mov si, [_blkoff]
        mov ds, [_blksel]
        mov dx, [si + 185h]
        mov cx, [si + 187h]
        push cx
        push dx
        mov cx, BLKSZ / 2
        cld
        rep movsw
        pop dx
        pop cx
        test bx, bx
        jz .done
        mov ax, cx
        or ax, dx
        jz .done
        mov ax, 0007h
        int 31h                         ; base of BX = CX:DX
        jc .done
        mov ds, bx
        xor si, si
        mov cx, LINSZ / 2
        rep movsw
.done:  pop ds
        pop ax
        pop dx
        pop bx
        pop cx
        pop di
        pop si
        pop es
.r:     ret

; void far get_block(void): INT 2Fh AX=4A05h SI=5 (WIN.COM's switcher
; block, ARCHITECTURE.md 9.5) into blk_dx:blk_ax, and a selector for it
; (DPMI 0002h) into blksel/blkoff
global _get_block
_get_block:
        push si
        push di
        push bx
        mov ax, 4A05h
        mov si, 5
        xor dx, dx
        int 2Fh
        cmp ax, 4A05h
        je .no
        test dx, dx
        jz .no
        mov [_blk_dx], dx
        mov [_blk_ax], ax
        mov [_blkoff], ax
        mov bx, dx
        mov ax, 0002h
        int 31h
        jc .no
        mov [_blksel], ax
        xor ax, ax                      ; a selector for linear addresses
        mov cx, 1
        int 31h
        jc .no
        mov [_linsel], ax
        mov bx, ax
        xor cx, cx
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
.no:    pop bx
        pop di
        pop si
        retf
