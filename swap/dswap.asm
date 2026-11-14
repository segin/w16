; dswap.asm - DSWAP.EXE for w16: the DOS side of the Standard-mode task
; switcher (ARCHITECTURE.md 9.9).
;
; WIN.COM runs it after WSWAP has swapped Windows out for a DOS program,
; with WSWAP's tail " \x08". The program is in WIN.COM's switcher block
; (path at 189h, tail at 05h, its task record at 17Eh); block 173h says
; whether it is a new one (0) or one swapped out before (its swap file
; number, 10xxh). A new program: DSWAP sets the text screen up (INT 10h
; AX=1202h BL=30h, mode 3, cursor 0607h), shows the MS-DOS Prompt's banner
; when the record asks for it (flag 08h: COMMAND.COM without parameters)
; and runs the program as its child. Alt+Tab, Alt+Esc and Ctrl+Esc while it
; runs switch away: DSWAP saves the program's memory, screen and DOS state
; in ~WOA10xx.TMP in the swap directory and ends; WIN.COM then runs WSWAP
; (Windows) or DSWAP again (another DOS program). When the program ends,
; DSWAP tells WIN.COM (INT 2Fh 4A05h SI=2: its record is freed) and ends.
;
; Observed on Windows 3.1 with the SW1 probe (tests/dos/sw1.asm), black
; box: the order of the INT 2Fh 4A05h calls and the block bytes DSWAP
; writes, the MS-DOS 5 task switcher notifications (create, activate,
; session active, query suspend, suspend, destroy; session 10xxh), the
; text screen it sets, the banner's bytes, Alt+Tab's title screen (INT 10h
; calls: the screen in 70h, a blue bar on row 1 from column 2 to 77, the
; title centred in 1Fh), the swap file (name, hidden and read-only, deleted
; when read back). Microsoft's DSWAP.EXE was never disassembled. What is
; saved and how is w16's own: Windows 3.1's DSWAP loads the 286 grabber
; (block 87h) for the screen; w16's saves the screen itself (text modes,
; CGA, EGA/VGA planar modes and mode 13h; UNVERIFIED: fonts and register
; states other than the DAC and the attribute palette) and does not need
; a grabber.
;
; Layout as WSWAP's: R, the loader, after the PSP; T, the rest, saved with
; the program (R reads it back at a resume and jumps into it).
;
; Copyright (c) 2026 The w16 contributors, MIT License (LICENSE).

        bits 16
        cpu 286
        org 100h

%include "swap.inc"

start:  jmp t_init

; ========================================================================
; R: the loader
; ========================================================================

r_name:     times PATHMAX db 0  ; the swap file
r_hdr:                          ; its header
rh_magic:   db 'w16D'
rh_mcb:     times 16 db 0       ; our MCB before the switch
rh_psp2e:   dd 0                ; our PSP's 2Eh (DOS's saved SS:SP)
rh_tseg:    dw 0                ; T's segment
rh_vlen:    dw 0                ; the screen memory: bytes a plane,
rh_vplanes: dw 0                ;   planes
HDR_LEN     equ $ - r_hdr
r_chunk:    dw 0, 0             ; a chunk: segment, paragraphs
r_fh:       dw 0
            align 2
            times 256 db 0
r_stack_top:

; read CX paragraphs from the file BX to AX:0 (DS = CS); CF on failure
r_readp:
        push ds
.l:     jcxz .ok
        mov dx, cx
        cmp dx, 800h                    ; 32 KB at a time
        jbe .n
        mov dx, 800h
.n:     push cx
        push dx
        mov cl, 4
        shl dx, cl
        mov cx, dx
        mov ds, ax
        xor dx, dx
        push ax
        mov ah, 3Fh
        int 21h
        mov dx, ax
        pop ax
        jc .err
        cmp dx, cx
        jne .err
        pop dx
        pop cx
        add ax, dx
        sub cx, dx
        jmp .l
.err:   pop dx
        pop cx
        pop ds
        stc
        ret
.ok:    pop ds
        clc
        ret

; Resume: read the program back (T's start-up code jumps here with SS:SP
; in R, DS = CS, r_name set). Never returns.
r_resume:
        mov dx, r_name                  ; (read-only: its attributes off
        mov ax, 4301h                   ; first, as Windows 3.1's DSWAP)
        xor cx, cx
        int 21h
        mov dx, r_name
        mov ax, 3D02h
        int 21h
        jc r_fatal
        mov [r_fh], ax
        mov bx, ax
        mov dx, r_hdr
        mov cx, HDR_LEN
        mov ah, 3Fh
        int 21h
        jc r_fatal
        cmp ax, HDR_LEN
        jne r_fatal
        cmp word [rh_magic], 'w1'
        jne r_fatal
        cmp word [rh_magic + 2], '6D'
        jne r_fatal
        mov ax, cs
        add ax, T_PARA
        cmp ax, [rh_tseg]
        jne r_fatal
        ; past the screen (T reads it once it is back)
        mov ax, [rh_vlen]
        mul word [rh_vplanes]
        add ax, HDR_LEN + VB_LEN
        adc dx, 0
        mov cx, dx
        mov dx, ax
        mov bx, [r_fh]
        mov ax, 4200h
        int 21h
        jc r_fatal
.c:     mov bx, [r_fh]
        mov dx, r_chunk
        mov cx, 4
        mov ah, 3Fh
        int 21h
        jc r_fatal
        cmp ax, 4
        jne r_fatal
        mov ax, [r_chunk]
        test ax, ax
        jz .done
        mov cx, [r_chunk + 2]
        call r_readp
        jc r_fatal
        jmp .c
.done:  mov ax, cs
        dec ax
        mov es, ax
        xor di, di
        mov si, rh_mcb
        mov cx, 8
        cld
        rep movsw
        mov ax, [rh_psp2e]
        mov [2Eh], ax
        mov ax, [rh_psp2e + 2]
        mov [30h], ax
        mov ax, [r_fh]
        mov [t_fh], ax                  ; (T is back: the file's handle
        jmp t_resume                    ; for the rest)

r_fatal:
        mov dx, m_fatal
        mov ah, 09h
        int 21h
        mov ax, 4CFFh
        int 21h
m_fatal: db 'Fatal exit! The program cannot be swapped back', 0Dh, 0Ah, '$'

; ========================================================================
; T
; ========================================================================
            align 16
T_START:
T_PARA      equ (T_START - $$ + 100h) / 16

blk:        dd 0
psp:        dw 0
rec:        db 0                ; our task record
arena_end:  dw 0
arena_type: db 0
t_fh:       dw 0
start_drv:  db 0                ; the drive and directory at the start
start_dirb: db '\'
start_dir:  times 68 db 0
ioctl0:     times 5 dw 0        ; device information of handles 0-4
brk0:       db 0
exit_code:  db 0
pending:    db 0                ; 1 Alt+Tab, 2 Alt+Esc, 3 Ctrl+Esc
in_switch:  db 0
in_bios:    db 0
in28:       db 0
sda:        dd 0                ; DOS's swappable data area
sda_len:    dw 0                ; (bytes to keep while in DOS)
indos:      dd 0
old08:      dd 0
old15:      dd 0
old16:      dd 0
old28:      dd 0
old10:      dd 0
old13:      dd 0
; the program's state at a switch
p_ss:       dw 0
p_sp:       dw 0
p_psp:      dw 0
p_dta:      dd 0
p_strat:    dw 0
p_umb:      dw 0
p_brk:      db 0
p_drv:      db 0
p_dirb:     db '\'
p_dir:      times 68 db 0
p_ioctl:    times 5 dw 0
p_pic:      dw 0
p_sda:      db 0                ; 1: the SDA was kept
p_kbflags:  dw 0
pic0:       dw 0
SDAMAX      equ 2048
sdabuf:     times SDAMAX db 0
; EXEC
exec_path:  times PATHMAX db 0
exec_tail:  times PATHMAX db 0
fcb1:       times 37 db 0
fcb2:       times 37 db 0
exec_pb:    dw 0, exec_tail, 0, fcb1, 0, fcb2, 0
save_ss:    dw 0
save_sp:    dw 0
n_sess:     dw 0
n_cx:       dw 0
chain:      dd 0
sw_version: dw 3, 0Ah
            db 'w16', 0
            align 2
ivt0:       times 1024 db 0     ; at the start (WIN.COM's)
ivtp:       times 1024 db 0     ; the program's at a switch
; the screen
VB_LEN      equ 30 + 7 + 768 + 17 + 2
vbuf:
vb_bda1:    times 30 db 0       ; 0040:0049-0066
vb_bda2:    times 7 db 0        ; 0040:0084-008A
vb_dac:     times 768 db 0
vb_pal:     times 17 db 0
vb_pad:     dw 0

; ---- INT 15h AH=4Fh: the hot keys ----
int15:  cmp ah, 4Fh
        jne .c
        cmp byte [cs:in_switch], 0
        jne .c
        push ds
        push bx
        mov bx, 40h
        mov ds, bx
        mov bh, [17h]                   ; the shift flags
        cmp al, 0Fh                     ; Tab
        jne .n1
        test bh, 08h                    ; Alt
        jz .no
        mov byte [cs:pending], 1
        jmp .eat
.n1:    cmp al, 01h                     ; Esc
        jne .no
        test bh, 08h
        jz .n2
        mov byte [cs:pending], 2
        jmp .eat
.n2:    test bh, 04h                    ; Ctrl
        jz .no
        mov byte [cs:pending], 3
.eat:   pop bx
        pop ds
        clc                             ; the key is ours
        retf 2
.no:    pop bx
        pop ds
.c:     jmp far [cs:old15]

; ---- where a switch can happen ----
; INT 16h: a program waiting for a key
int16:  cmp byte [cs:pending], 0
        je .c
        cmp ah, 00h
        je .t
        cmp ah, 01h
        je .t
        cmp ah, 10h
        je .t
        cmp ah, 11h
        jne .c
.t:     call try_switch
.c:     inc byte [cs:in_bios]
        pushf
        call far [cs:old16]
        jmp bios_ret
; INT 28h: DOS waiting for console input
int28:  cmp byte [cs:pending], 0
        je .c
        mov byte [cs:in28], 1
        call try_switch
        mov byte [cs:in28], 0
.c:     jmp far [cs:old28]
; INT 08h: a program that never asks for a key
int08:  pushf
        call far [cs:old08]
        cmp byte [cs:pending], 0
        je .r
        cmp byte [cs:in_bios], 0
        jne .r
        call try_switch
.r:     iret
; INT 10h, 13h: BIOS calls in progress (no switch from the timer then)
int10:  inc byte [cs:in_bios]
        pushf
        call far [cs:old10]
        jmp bios_ret
int13:  inc byte [cs:in_bios]
        pushf
        call far [cs:old13]
; back to the caller with the BIOS's result flags, but the caller's IF and
; TF (a "retf 2" would leave interrupts disabled)
bios_ret:
        pushf
        dec byte [cs:in_bios]
        push bp
        mov bp, sp                      ; [bp+2] result, [bp+4] IP, [bp+6]
        push ax                         ; CS, [bp+8] the caller's flags
        mov ax, [bp + 2]
        and ax, 0FCFFh
        and word [bp + 8], 0300h
        or [bp + 8], ax
        pop ax
        pop bp
        popf
        iret

; try_switch: a switch if DOS allows it now (not inside DOS, or inside
; it but waiting for console input, INT 28h). Keeps all registers. When it
; returns, the program goes on (after the switch, when it was switched
; back to).
try_switch:
        cmp byte [cs:in_switch], 0
        jne .r
        push ds
        push bx
        lds bx, [cs:indos]
        mov bl, [bx]
        cmp bl, 0
        je .go
        cmp byte [cs:in28], 0
        je .no
        cmp bl, 1
        je .go
.no:    pop bx
        pop ds
.r:     ret
.go:    pop bx
        pop ds
        mov byte [cs:in_switch], 1
        push ds
        push es
        pusha
        mov [cs:p_ss], ss
        mov [cs:p_sp], sp
        cli
        push cs
        pop ss
        mov sp, sw_stack_top
        sti
        push cs
        pop ds
        push cs
        pop es
        cld
        call switch_out                 ; returns when no switch was made
sw_back:                                ; (and here after a resume)
        cli
        mov ss, [cs:p_ss]
        mov sp, [cs:p_sp]
        sti
        popa
        pop es
        pop ds
        mov byte [cs:pending], 0
        mov byte [cs:in_switch], 0
        ret

%ifdef DEBUG
; dbg: append "TAG AX BX CX DX" to C:\DSWAP.DBG (debug builds: -dDEBUG)
%macro DBG 1
        push word %1
        call dbg
%endmacro
dbg:    push bp
        mov bp, sp
        pushf
        pusha
        push ds
        push es
        push cs
        pop ds
        push cs
        pop es
        mov di, dbg_line
        mov ax, [bp + 4]
        call dbg_hex
        mov al, ' '
        stosb
        mov ax, [bp - 4]                ; AX (pusha: AX first)
        call dbg_hex
        mov al, ' '
        stosb
        mov ax, [bp - 10]               ; BX
        call dbg_hex
        mov al, ' '
        stosb
        mov ax, [bp - 6]                ; CX
        call dbg_hex
        mov al, ' '
        stosb
        mov ax, [bp - 8]                ; DX
        call dbg_hex
        mov ax, 0A0Dh
        stosw
        mov dx, dbg_name
        mov ax, 3D01h
        int 21h
        jnc .o
        mov dx, dbg_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .x
.o:     mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov dx, dbg_line
        mov cx, di
        sub cx, dbg_line
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.x:     pop es
        pop ds
        popa
        popf
        pop bp
        ret 2
dbg_hex:
        push cx
        mov cx, 4
.l:     rol ax, 4
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     stosb
        pop ax
        loop .l
        pop cx
        ret
dbg_name: db 'C:\DSWAP.DBG', 0
dbg_line: times 40 db 0
%else
%macro DBG 1
%endmacro
%endif

; ---- the switcher's entry point for MS-DOS 5 clients ----
sw_entry:
        test ax, ax
        jnz .ns
        push cs
        pop es
        mov bx, sw_version
        clc
        retf
.ns:    stc
        retf

sw_call:
        mov ax, 4A05h
        int 2Fh
        ret

get_blk:
        les di, [cs:blk]
        ret

; MS-DOS 5 task switcher notifications, AX = function, to every client
; (as WSWAP's); AX nonzero back if one refused
notify: push bx
        push cx
        push dx
        push si
        push di
        push es
        push bp
        push ax
        mov ax, 4B01h
        mov cx, cs
        mov dx, sw_entry
        xor bx, bx
        mov es, bx
        int 2Fh
        mov [cs:chain], bx
        mov [cs:chain + 2], es
        pop ax
        xor si, si
.l:     les bx, [cs:chain]
        mov dx, es
        or dx, bx
        jz .done
        mov dx, [es:bx]
        mov [cs:chain], dx
        mov dx, [es:bx + 2]
        mov [cs:chain + 2], dx
        push ax
        push si
        push word [es:bx + 6]
        push word [es:bx + 4]
        mov bp, sp
        push cs
        pop es
        mov di, sw_entry
        mov bx, [cs:n_sess]
        mov cx, [cs:n_cx]
        cmp ax, 2
        je .cli
        cmp ax, 3
        jne .call
.cli:   cli
.call:  call far [bp]
        sti
        add sp, 4
        mov dx, ax
        pop si
        pop ax
        or si, dx
        jmp .l
.done:  mov ax, si
        pop bp
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        push cs
        pop ds
        ret

; ---- the text screen for a DOS program (Windows 3.1: the grabber's
; InitScreen, INT 10h AX=1202h BL=30h, AX=0003h, AX=0100h CX=0607h) ----
init_screen:
        mov ax, 1202h
        mov bl, 30h
        int 10h
        mov ax, 0003h
        int 10h
        mov ax, 0100h
        mov cx, 0607h
        int 10h
        ret

; ---- the program's DOS state ----
save_dos:
        mov ax, 3300h
        int 21h
        mov [p_brk], dl
        mov ax, 3301h
        xor dl, dl
        int 21h
        mov ax, 5800h
        int 21h
        mov [p_strat], ax
        mov ax, 5802h
        int 21h
        mov [p_umb], ax
        push es
        mov ah, 2Fh
        int 21h
        mov [p_dta], bx
        mov [p_dta + 2], es
        pop es
        mov ah, 51h
        int 21h
        mov [p_psp], bx
        mov ah, 19h
        int 21h
        mov [p_drv], al
        mov si, p_dir
        xor dl, dl
        mov ah, 47h
        int 21h
        mov di, p_ioctl
        call ioctl_save
        ; our PSP current
        mov bx, [psp]
        mov ah, 50h
        int 21h
        ; the start's drive and directory (Windows 3.1's DSWAP)
        mov dl, [start_drv]
        mov ah, 0Eh
        int 21h
        mov dx, start_dirb
        mov ah, 3Bh
        int 21h
        ret
restore_dos:
        mov dl, [p_drv]
        mov ah, 0Eh
        int 21h
        mov dx, p_dirb
        mov ah, 3Bh
        int 21h
        mov bx, [p_psp]
        mov ah, 50h
        int 21h
        mov bx, [p_strat]
        mov ax, 5801h
        int 21h
        mov bx, [p_umb]
        xor bh, bh
        mov ax, 5803h
        int 21h
        push ds
        lds dx, [p_dta]
        mov ah, 1Ah
        int 21h
        pop ds
        mov si, p_ioctl
        call ioctl_load
        mov dl, [p_brk]
        mov ax, 3301h
        int 21h
        ret

; device information of handles 0-4: to DI, from SI (for character devices
; only, as DOS allows)
ioctl_save:
        xor bx, bx
.l:     mov ax, 4400h
        int 21h
        jnc .s
        xor dx, dx
.s:     mov [di], dx
        add di, 2
        inc bx
        cmp bx, 5
        jb .l
        ret
ioctl_load:
        xor bx, bx
.l:     mov dx, [si]
        add si, 2
        test dl, 80h
        jz .n
        xor dh, dh
        mov ax, 4401h
        int 21h
.n:     inc bx
        cmp bx, 5
        jb .l
        ret

; ---- the vectors: the program's kept, the start's back; and back ----
ivt_out:
        push cs
        pop es
        cli
        push ds
        xor si, si
        mov ds, si
        mov di, ivtp
        mov cx, 512
        rep movsw
        pop ds
        xor di, di
        push es
        mov es, di
        mov si, ivt0
        mov cx, 512
        rep movsw
        pop es
        in al, 0A1h
        mov ah, al
        in al, 21h
        mov [p_pic], ax
        mov ax, [pic0]
        out 21h, al
        mov al, ah
        out 0A1h, al
        sti
        ret
ivt_in: cli
        push es
        xor di, di
        mov es, di
        mov si, ivtp
        mov cx, 512
        rep movsw
        pop es
        mov ax, [p_pic]
        out 21h, al
        mov al, ah
        out 0A1h, al
        sti
        ret

; ---- the switch ----
; The swap file: the header, the screen (vbuf, then the screen memory),
; then the chunks of memory (T and the rest of our block, then each block
; after it: in use whole, free only its header), ended by a chunk of
; segment 0. The screen is written first, from the screen itself, before
; the text screen replaces it; the chunks last, when T's state is final.
switch_out:
        mov word [n_cx], 0FFh
        mov ax, 1                       ; MS-DOS 5 clients may refuse
        call notify
        test ax, ax
        jnz .r
        mov ax, 2
        call notify
        test ax, ax
        jnz .r
        ; DOS: the SDA when inside DOS (INT 28h)
        mov byte [p_sda], 0
        push ds
        lds si, [indos]
        cmp byte [si], 0
        pop ds
        je .nsda
        mov cx, [sda_len]
        jcxz .nsda
        cmp cx, SDAMAX
        ja .nsda
        push ds
        push cs
        pop es
        lds si, [sda]
        mov di, sdabuf
        rep movsb
        pop ds
        mov byte [p_sda], 1
        push es                         ; (DOS not busy any more for ours)
        les di, [indos]
        mov byte [es:di], 0
        pop es
.nsda:  call save_dos
        call vsave_regs
        ; room for it all? (UNVERIFIED what Windows 3.1's DSWAP does
        ; without: w16's beeps and the program stays)
        call file_size                  ; DX:AX
        call disk_room
        jc .full
        call swap_name
        mov dx, r_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .full
        mov [t_fh], ax
        call vmem_area
        mov [rh_vlen], cx               ; (bytes a plane)
        mov [rh_vplanes], dx
        mov ax, cs
        add ax, T_PARA
        mov [rh_tseg], ax
        mov bx, [t_fh]
        mov dx, r_hdr
        mov cx, HDR_LEN
        mov ah, 40h
        int 21h
        jc .ferr
        mov dx, vbuf
        mov cx, VB_LEN
        mov ah, 40h
        int 21h
        jc .ferr
        call vmem_write
        jc .ferr
        ; the text screen; where to
        call init_screen
        mov si, SW_COUNT
        call sw_call
        DBG 1
        mov al, [pending]
        cmp al, 1
        je .alttab
        cmp al, 3
        je .ctrlesc
        mov si, SW_NEXT                 ; Alt+Esc: 17Eh := the next record
        call sw_call
        jmp .go
.ctrlesc:
        call ctrl_esc
        jmp .go
.alttab:
        call alt_tab
        jc .back
.go:    call get_blk
        mov al, [es:di + B_CUR]
        mov ah, [rec]
        DBG 2
        cmp al, [rec]
        je .back
        ; away: the memory, and end
        call rec_ptr
        mov byte [es:di + bx + B_RECS + R_SWAPPED], 1
        mov si, SW_COUNT
        call sw_call
        call get_blk
        mov al, [rec]
        mov ah, SESS_HI
        mov [es:di + B_SID], ax
        call ivt_out
        mov dl, [brk0]
        mov ax, 3301h
        int 21h
        ; (from here T's state is what comes back)
        push ds
        mov ax, [psp]
        dec ax
        mov ds, ax
        xor si, si
        mov di, rh_mcb
        push cs
        pop es
        mov cx, 8
        rep movsw
        pop ds
        mov ax, [2Eh]
        mov [rh_psp2e], ax
        mov ax, [30h]
        mov [rh_psp2e + 2], ax
        mov bx, [t_fh]
        xor cx, cx
        xor dx, dx
        mov ax, 4200h
        int 21h
        mov dx, r_hdr
        mov cx, HDR_LEN
        mov ah, 40h
        int 21h
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov byte [cw_write], 1
        call chunks_walk
        mov word [r_chunk], 0
        mov word [r_chunk + 2], 0
        mov bx, [t_fh]
        mov dx, r_chunk
        mov cx, 4
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ax, 4301h
        mov cx, 3                       ; hidden, read-only (as 3.1's)
        int 21h
        push es
        mov ax, [psp]
        dec ax
        mov es, ax
        mov al, [arena_type]
        mov [es:0], al
        mov ax, [arena_end]
        sub ax, [psp]
        mov [es:3], ax
        pop es
        mov ax, 4C00h
        int 21h
.ferr:  mov bx, [t_fh]
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
.full:  mov ax, 0E07h                   ; (a beep)
        int 10h
        jmp .dos
.back:  ; back to the program without a switch: the screen from the file
        call get_blk
        mov al, [rec]
        mov [es:di + B_CUR], al
        mov bx, [t_fh]
        mov dx, HDR_LEN + VB_LEN
        xor cx, cx
        mov ax, 4200h
        int 21h
        call vrestore_regs
        mov bx, [t_fh]
        call vmem_read
        mov bx, [t_fh]
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
.dos:   call restore_dos
        call sda_back
        mov word [n_cx], 1
        mov ax, 3
        call notify
        mov ax, 4
        call notify
.r:     ret

; BX = our record's offset from the block's records... (ES:DI the block,
; BX = rec * REC_SIZE)
rec_ptr:
        call get_blk
        mov al, [rec]
        mov ah, REC_SIZE
        mul ah
        mov bx, ax
        ret

sda_back:
        cmp byte [p_sda], 0
        je .r
        push es
        mov cx, [sda_len]
        les di, [sda]
        mov si, sdabuf
        rep movsb
        pop es
.r:     ret

; Ctrl+Esc: to Windows, whose WINOLDAP then shows the Task List (block
; 178h bit 08h). Windows 3.1's DSWAP with one DOS program: SI=5, 178h bit
; 08h, SI=7 BX=1 (the Windows application after it in the ring); w16
; takes the first record in the ring that is Windows' (session 0Fh)
; (UNVERIFIED with several DOS programs)
ctrl_esc:
        mov si, SW_BLOCK
        call sw_call
        call get_blk
        or byte [es:di + B_FLAGS], 08h
        mov bx, 1
.l:     push bx
        mov si, SW_PEEK
        call sw_call                    ; ES:AX -> a record's name
        mov si, ax
        pop bx
        cmp byte [es:si + R_SESS], SESS_WIN
        je .t
        inc bx
        cmp bx, 16
        jbe .l
        mov bx, 1
.t:     mov si, SW_TAKE
        call sw_call
        ret

; Alt+Tab: the title of the next task in the ring on a screen of its own,
; Tab for the one after, until Alt is let go (CF clear, 17Eh the chosen
; one) or Esc (CF set). IRQ 1 is ours meanwhile.
alt_tab:
        mov word [at_n], 1
        mov byte [at_key], 0
        cli
        push es
        xor ax, ax
        mov es, ax
        mov ax, [es:9 * 4]
        mov [at_old9], ax
        mov ax, [es:9 * 4 + 2]
        mov [at_old9 + 2], ax
        mov word [es:9 * 4], at_int9
        mov [es:9 * 4 + 2], cs
        mov ax, 40h                     ; Alt let go already (a quick
        mov es, ax                      ; Alt+Tab): the next one
        test byte [es:17h], 08h
        pop es
        sti
        jz .take
.show:  mov bx, [at_n]
        mov si, SW_PEEK
        call sw_call                    ; ES:AX -> the title
        call at_draw
.wait:  sti
        hlt
        cli
        mov al, [at_key]
        mov byte [at_key], 0
        sti
        cmp al, 0Fh                     ; Tab
        jne .w1
        inc word [at_n]
        jmp .show
.w1:    cmp al, 0B8h                    ; Alt let go
        je .take
        cmp al, 01h                     ; Esc
        jne .wait
        call at_unhook
        stc
        ret
.take:  call at_unhook
        mov si, SW_BLOCK
        call sw_call
        mov bx, [at_n]
        mov si, SW_TAKE
        call sw_call
        DBG 3
        clc
        ret
at_unhook:
        cli
        push es
        xor ax, ax
        mov es, ax
        mov ax, [at_old9]
        mov [es:9 * 4], ax
        mov ax, [at_old9 + 2]
        mov [es:9 * 4 + 2], ax
        ; (the BIOS never saw Alt let go)
        mov ax, 40h
        mov es, ax
        and byte [es:17h], 0F7h
        and byte [es:18h], 0FDh
        pop es
        sti
        ret
at_int9:
        push ax
        in al, 60h
        cmp al, 0E0h
        je .e
        mov [cs:at_key], al
.e:     mov al, 20h
        out 20h, al
        pop ax
        iret
at_n:       dw 0
at_key:     db 0
at_old9:    dd 0

; the title screen (Windows 3.1's DSWAP: the screen in 70h, row 1 a blue
; bar from column 2 to 77 with the title in 1Fh in its middle); ES:AX the
; title
at_draw:
        push es
        push ax
        mov ah, 02h
        xor bh, bh
        xor dx, dx
        int 10h
        mov ax, 0920h
        mov bx, 0070h
        mov cx, 2000
        int 10h
        pop si
        pop es
        xor cx, cx                      ; its length (at most 76)
.len:   cmp byte [es:si], 0
        je .lend
        inc si
        inc cx
        cmp cx, 76
        jb .len
.lend:  sub si, cx
        mov bp, cx
        mov ax, 76
        sub ax, cx
        inc ax
        shr ax, 1                       ; the bar left of it
        push ax
        mov ah, 02h
        xor bh, bh
        mov dx, 0102h
        int 10h
        pop cx
        push cx
        mov ax, 0920h
        mov bx, 0010h
        int 10h
        pop cx
        add cl, 2
        mov dl, cl
        mov dh, 1
        mov cx, bp
        jcxz .right
.ch:    push cx
        mov ah, 02h
        xor bh, bh
        int 10h
        mov al, [es:si]
        inc si
        mov ah, 09h
        mov bx, 001Fh
        mov cx, 1
        int 10h
        inc dl
        pop cx
        loop .ch
.right: mov ah, 02h
        xor bh, bh
        int 10h
        mov cx, 78
        sub cl, dl
        mov ax, 0920h
        mov bx, 0010h
        int 10h
        mov ah, 02h
        xor bh, bh
        mov dx, 1900h                   ; the cursor off the screen
        int 10h
        ret

; ---- the screen ----
; the BIOS's video fields, the DAC and the attribute palette into vbuf
vsave_regs:
        push es
        push cs
        pop es
        call .go
        pop es
        ret
.go:    push ds
        push es
        mov ax, 40h
        mov ds, ax
        push cs
        pop es
        mov si, 49h
        mov di, vb_bda1
        mov cx, 30
        rep movsb
        mov si, 84h
        mov cx, 7
        rep movsb
        pop es
        pop ds
        mov ax, 1017h
        xor bx, bx
        mov cx, 256
        mov dx, vb_dac
        int 10h
        mov ax, 1009h
        mov dx, vb_pal
        int 10h
        ret

; the screen memory of the mode in vbuf: AX segment, CX bytes (a plane),
; DX planes (0: nothing kept; 4 for the planar modes)
vmem_area:
        mov al, [vb_bda1]
        and al, 7Fh
        mov dx, 1
        cmp al, 3
        jbe .text
        cmp al, 7
        je .mono
        cmp al, 6
        jbe .cga
        cmp al, 13h
        je .v256
        mov dx, 4
        mov cx, 8000
        cmp al, 0Dh
        je .pl
        mov cx, 16000
        cmp al, 0Eh
        je .pl
        mov cx, 28000
        cmp al, 10h
        je .pl
        mov cx, 38400
        cmp al, 12h
        je .pl
        xor dx, dx
        xor cx, cx
        ret
.text:  mov ax, 0B800h
        mov cx, 8000h
        ret
.mono:  mov ax, 0B000h
        mov cx, 8000h
        ret
.cga:   mov ax, 0B800h
        mov cx, 4000h
        ret
.v256:  mov ax, 0A000h
        mov cx, 64000
        ret
.pl:    mov ax, 0A000h
        ret

; write the screen memory to the file BX; CF on failure
vmem_write:
        call vmem_area
        cmp dx, 1
        jb .ok
        ja .planar
        push ds
        mov ds, ax
        xor dx, dx
        mov ah, 40h
        int 21h
        pop ds
        ret
.ok:    clc
        ret
.planar:
        mov dx, 3CEh                    ; read mode 0
        mov ax, 0005h
        out dx, ax
        xor di, di
.p:     mov dx, 3CEh                    ; read map select
        mov ax, di
        mov ah, al
        mov al, 4
        out dx, ax
        push ds
        mov ax, 0A000h
        mov ds, ax
        xor dx, dx
        mov ah, 40h
        int 21h
        pop ds
        jc .r
        inc di
        cmp di, 4
        jb .p
        clc
.r:     ret

; the mode, palette and BIOS fields from vbuf
vrestore_regs:
        push es
        push cs
        pop es
        call .go
        pop es
        ret
.go:    mov al, [vb_bda1]
        and al, 7Fh
        cmp al, 3
        ja .nt
        mov ax, 1202h                   ; 400 lines for the text modes
        mov bl, 30h
        int 10h
.nt:    mov al, [vb_bda1]
        and al, 7Fh
        xor ah, ah
        int 10h
        mov al, [vb_bda1]
        and al, 7Fh
        cmp al, 3
        ja .nf
        mov al, [vb_bda2 + 1]           ; 0040:0085, the character height
        cmp al, 8
        jne .n14
        mov ax, 1112h
        xor bl, bl
        int 10h
        jmp .nf
.n14:   cmp al, 14
        jne .nf
        mov ax, 1111h
        xor bl, bl
        int 10h
.nf:    mov ax, 1012h
        xor bx, bx
        mov cx, 256
        mov dx, vb_dac
        int 10h
        mov ax, 1002h
        mov dx, vb_pal
        int 10h
        push es
        mov ax, 40h
        mov es, ax
        mov si, vb_bda1
        mov di, 49h
        mov cx, 30
        rep movsb
        mov si, vb_bda2
        mov di, 84h
        mov cx, 7
        rep movsb
        pop es
        mov al, [vb_bda1 + 62h - 49h]   ; the page
        mov ah, 05h
        int 10h
        mov cx, [vb_bda1 + 60h - 49h]   ; the cursor's shape
        mov ah, 01h
        int 10h
        mov bl, [vb_bda1 + 62h - 49h]   ; its place
        xor bh, bh
        shl bx, 1
        mov dx, [vb_bda1 + 50h - 49h + bx]
        mov bh, [vb_bda1 + 62h - 49h]
        mov ah, 02h
        int 10h
        ret

; read the screen memory from the file BX (the mode set)
vmem_read:
        call vmem_area
        cmp dx, 1
        jb .ok
        ja .planar
        push ds
        mov ds, ax
        xor dx, dx
        mov ah, 3Fh
        int 21h
        pop ds
        ret
.ok:    clc
        ret
.planar:
        push cx
        mov dx, 3CEh                    ; write mode 0, no set/reset, no
        mov ax, 0005h                   ; rotation, every bit
        out dx, ax
        mov ax, 0001h
        out dx, ax
        mov ax, 0003h
        out dx, ax
        mov ax, 0FF08h
        out dx, ax
        pop cx
        mov si, vr_planes
.p:     mov dx, 3C4h                    ; map mask: one plane
        mov al, 2
        mov ah, [si]
        out dx, ax
        push ds
        mov ax, 0A000h
        mov ds, ax
        xor dx, dx
        mov ah, 3Fh
        int 21h
        pop ds
        jc .r
        inc si
        cmp si, vr_planes + 4
        jb .p
        mov dx, 3C4h
        mov ax, 0F02h
        out dx, ax
        clc
.r:     ret
vr_planes: db 1, 2, 4, 8

; ---- the swap file ----
; its name: the swap directory, "~WOA", 10h and our record in
; hexadecimal, ".TMP"
swap_name:
        push ds
        call get_blk
        lea si, [di + B_SWAP1]
        push es
        pop ds
        push cs
        pop es
        mov di, r_name
.c:     lodsb
        test al, al
        jz .e
        stosb
        jmp .c
.e:     pop ds
        mov word [di], '~W'
        mov word [di + 2], 'OA'
        add di, 4
        mov al, SESS_HI
        call hex2
        mov al, [rec]
        call hex2
        mov si, s_tmp
.t:     lodsb
        stosb
        test al, al
        jnz .t
        ret
s_tmp:  db '.TMP', 0
hex2:   push ax
        shr al, 4
        call .d
        pop ax
        and al, 0Fh
.d:     add al, '0'
        cmp al, '9'
        jbe .s
        add al, 'A' - '9' - 1
.s:     stosb
        ret

; the swap file's size: DX:AX
file_size:
        mov byte [cw_write], 0
        mov word [cw_total], HDR_LEN + VB_LEN + 4
        mov word [cw_total + 2], 0
        call vmem_area
        test dx, dx
        jz .nv
.pl:    add [cw_total], cx
        adc word [cw_total + 2], 0
        dec dx
        jnz .pl
.nv:    call chunks_walk
        mov ax, [cw_total]
        mov dx, [cw_total + 2]
        ret
cw_write:   db 0
cw_total:   dd 0

; CF clear when the swap drive (block D7h) has DX:AX bytes free
disk_room:
        push dx
        push ax
        call get_blk
        mov dl, [es:di + B_SWAP1]
        and dl, 0DFh
        sub dl, 'A' - 1
        mov ah, 36h
        int 21h
        cmp ax, 0FFFFh
        je .no2
        mul cx
        mul bx                          ; DX:AX free
        pop bx
        pop cx                          ; CX:BX wanted
        cmp dx, cx
        ja .yes
        jb .no
        cmp ax, bx
        jae .yes
.no:    stc
        ret
.no2:   pop ax
        pop ax
        stc
        ret
.yes:   clc
        ret

; each chunk: write it to t_fh (cw_write) or count it into cw_total
chunks_walk:
        mov ax, cs
        add ax, T_PARA
        push es
        mov bx, [psp]
        dec bx
        mov es, bx
        mov cx, [es:3]
        add cx, [psp]                   ; our block's end
        pop es
        mov dx, cx
        sub cx, ax
        call chunk
        jc .r
        mov ax, dx
.l:     cmp ax, [arena_end]
        jae .ok
        push es
        mov es, ax
        mov bl, [es:0]
        mov cx, [es:3]
        mov dx, [es:1]
        pop es
        push bx
        push cx
        inc cx                          ; the header and the block
        test dx, dx
        jnz .used
        mov cx, 1                       ; free: the header only
.used:  call chunk
        pop cx
        pop bx
        jc .r
        add ax, cx
        inc ax
        cmp bl, 'Z'
        jne .l
.ok:    clc
.r:     ret
; one chunk: AX segment, CX paragraphs; keeps AX
chunk:  push ax
        push dx
        mov [r_chunk], ax
        mov [r_chunk + 2], cx
        cmp byte [cw_write], 0
        jne .w
        mov ax, cx
        xor dx, dx
        mov cx, 4
.s:     shl ax, 1
        rcl dx, 1
        loop .s
        add ax, 4
        adc dx, 0
        add [cw_total], ax
        adc [cw_total + 2], dx
        clc
        jmp .r
.w:     mov bx, [t_fh]
        push cx
        mov dx, r_chunk
        mov cx, 4
        mov ah, 40h
        int 21h
        pop cx
        jc .r
        mov ax, [r_chunk]
.l:     jcxz .ok
        mov dx, cx
        cmp dx, 800h
        jbe .n
        mov dx, 800h
.n:     push cx
        push dx
        push ax
        mov cl, 4
        shl dx, cl
        mov cx, dx
        push ds
        mov ds, ax
        xor dx, dx
        mov ah, 40h
        int 21h
        pop ds
        pop ax
        pop dx
        pop cx
        jc .r
        add ax, dx
        sub cx, dx
        jmp .l
.ok:    clc
.r:     pop dx
        pop ax
        ret

; ---- back from DOS: R has read the program back (DS = CS) ----
t_resume:
        cli
        push cs
        pop ss
        mov sp, sw_stack_top
        sti
        push cs
        pop ds
        push cs
        pop es
        cld
        mov al, [rec]
        DBG 6
        ; the screen, from after the header
        mov bx, [t_fh]
        mov dx, HDR_LEN
        xor cx, cx
        mov ax, 4200h
        int 21h
        mov dx, vbuf
        mov cx, VB_LEN
        mov ah, 3Fh
        int 21h
        call vrestore_regs
        mov bx, [t_fh]
        call vmem_read
        mov bx, [t_fh]
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ax, 4301h
        xor cx, cx
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
        ; the program's vectors and DOS state
        call ivt_in
        call restore_dos
        call sda_back
        mov ax, 0C00h
        int 21h
        mov word [n_cx], 1
        mov ax, 3
        call notify
        mov ax, 4
        call notify
        jmp sw_back

            align 2
            times 1024 db 0
t_stack_top:
            times 1024 db 0
sw_stack_top:

; ---- start-up ----
t_init: cld
        mov sp, t_stack_top
        mov [psp], cs
        mov si, SW_BLOCK
        mov ax, 4A05h
        int 2Fh
        cmp ax, 4A05h
        jne .blk
        mov dx, m_noswitch
        mov ah, 09h
        int 21h
        mov ax, 4CFFh
        int 21h
.blk:   mov [blk], ax
        mov [blk + 2], dx
        call get_blk
        mov al, [es:di + B_CUR]
        mov [rec], al
        mov ah, SESS_HI
        mov [n_sess], ax
        ; a program swapped out before: back it comes
        cmp word [es:di + B_SID], 0
        je fresh
        mov al, [es:di + B_SID]
        mov [rec], al
        mov ah, SESS_HI
        mov [n_sess], ax
        call swap_name
        cli
        mov sp, r_stack_top
        sti
        jmp r_resume

fresh:  ; the arena's end (as WSWAP)
        int 12h
        mov cl, 6
        shl ax, cl
        mov bx, ax
        mov ax, cs
        dec ax
.w:     mov es, ax
        mov dl, [es:0]
        mov cx, ax
        add cx, [es:3]
        inc cx
        cmp dl, 'Z'
        je .last
        cmp cx, bx
        jae .last
        mov ax, cx
        jmp .w
.last:  mov [arena_end], cx
        mov [arena_type], dl
        ; DOS's swappable data area and InDOS flag
        push ds
        mov ax, 5D06h
        int 21h
        mov ax, ds
        pop ds
        jc .nsda
        mov [sda], si
        mov [sda + 2], ax
        mov [sda_len], cx
.nsda:  push es
        mov ah, 34h
        int 21h
        mov [indos], bx
        mov [indos + 2], es
        pop es
        ; the program and its tail from the block
        call get_blk
        push ds
        push es
        pop ds
        lea si, [di + B_PROGRAM]
        push cs
        pop es
        mov di, exec_path
        mov cx, PATHMAX - 1
        rep movsb
        pop ds
        call get_blk
        push ds
        push es
        pop ds
        lea si, [di + B_TAIL]
        push cs
        pop es
        mov di, exec_tail
        mov cx, PATHMAX
        rep movsb
        pop ds
        mov byte [exec_tail + PATHMAX - 1], 0Dh
        mov si, exec_tail + 1
        mov di, fcb1
        mov ax, 2901h
        int 21h
        mov di, fcb2
        mov ax, 2901h
        int 21h
        ; the start's vectors and PICs
        cli
        push ds
        xor si, si
        mov ds, si
        mov di, ivt0
        mov cx, 512
        rep movsw
        pop ds
        in al, 0A1h
        mov ah, al
        in al, 21h
        mov [pic0], ax
        sti
        ; keep T only
        mov bx, t_end
        add bx, 15
        mov cl, 4
        shr bx, cl
        mov es, [psp]
        mov ah, 4Ah
        int 21h
        ; hooks
        mov ax, 3508h
        int 21h
        mov [old08], bx
        mov [old08 + 2], es
        mov ax, 3510h
        int 21h
        mov [old10], bx
        mov [old10 + 2], es
        mov ax, 3513h
        int 21h
        mov [old13], bx
        mov [old13 + 2], es
        mov ax, 3515h
        int 21h
        mov [old15], bx
        mov [old15 + 2], es
        mov ax, 3516h
        int 21h
        mov [old16], bx
        mov [old16 + 2], es
        mov ax, 3528h
        int 21h
        mov [old28], bx
        mov [old28 + 2], es
        push cs
        pop es
        mov dx, int08
        mov ax, 2508h
        int 21h
        mov dx, int10
        mov ax, 2510h
        int 21h
        mov dx, int13
        mov ax, 2513h
        int 21h
        mov dx, int15
        mov ax, 2515h
        int 21h
        mov dx, int16
        mov ax, 2516h
        int 21h
        mov dx, int28
        mov ax, 2528h
        int 21h
        ; the text screen; the MS-DOS Prompt's banner (record flag 08h)
        call init_screen
        call get_blk
        mov al, [rec]
        mov ah, REC_SIZE
        mul ah
        mov bx, ax
        test byte [es:di + bx + B_RECS + R_FLAGS], 08h
        jz .nb
        mov dx, m_banner
        mov ah, 09h
        int 21h
.nb:    ; the drive, directory and handles to come back to
        mov ah, 19h
        int 21h
        mov [start_drv], al
        mov si, start_dir
        xor dl, dl
        mov ah, 47h
        int 21h
        mov di, ioctl0
        call ioctl_save
        mov ax, 0C00h
        int 21h
        mov ax, 3300h
        int 21h
        mov [brk0], dl
        ; MS-DOS 5 clients: the session is created and active
        mov word [n_cx], 1
        mov ax, 5
        call notify
        mov ax, 3
        call notify
        mov ax, 4
        call notify
        ; the record: a session now (Windows 3.1's DSWAP: block 16Fh one
        ; more, the record's flag 08h off, its session = its number, 10h)
        call get_blk
        inc byte [es:di + B_RUNS]
        mov al, [rec]
        mov ah, REC_SIZE
        mul ah
        mov bx, ax
        and byte [es:di + bx + B_RECS + R_FLAGS], 0F7h
        mov al, [rec]
        mov [es:di + bx + B_RECS + R_SESS], al
        mov byte [es:di + bx + B_RECS + R_SESSHI], SESS_HI
        ; the program
        mov ax, [2Ch]
        mov [exec_pb], ax
        mov [exec_pb + 4], cs
        mov [exec_pb + 8], cs
        mov [exec_pb + 12], cs
        push cs
        pop es
        mov dx, exec_path
        mov bx, exec_pb
        mov [save_ss], ss
        mov [save_sp], sp
        mov ax, 4B00h
        int 21h
        cli
        mov ss, [cs:save_ss]
        mov sp, [cs:save_sp]
        sti
        push cs
        pop ds
        push cs
        pop es
        cld
        DBG 4
        ; the program has ended (Windows 3.1's DSWAP: destroy session,
        ; the text screen, the start's directory and handles, SI=5, SI=2
        ; - WIN.COM frees the record -, SI=5, 178h bit 08h, SI=4)
        mov word [n_cx], 0
        mov ax, 6
        call notify
        ; our hooks off
        cli
        push es
        xor di, di
        mov es, di
        mov si, ivt0
        mov cx, 512
        rep movsw
        pop es
        sti
        mov dl, [brk0]
        mov ax, 3301h
        int 21h
        call init_screen
        mov dl, [start_drv]
        mov ah, 0Eh
        int 21h
        mov dx, start_dirb
        mov ah, 3Bh
        int 21h
        mov si, ioctl0
        call ioctl_load
        mov bx, [psp]
        mov ah, 50h
        int 21h
        mov si, SW_BLOCK
        call sw_call
        mov si, SW_END                  ; (BX=0, CX=10h, DX the block's
        xor bx, bx                      ; segment, as Windows 3.1's)
        mov cx, 10h
        call sw_call
        mov si, SW_BLOCK
        call sw_call
        call get_blk
        mov al, [es:di + B_CUR]
        mov ah, [rec]
        DBG 5
        or byte [es:di + B_FLAGS], 08h
        mov si, SW_COUNT
        call sw_call
        mov ax, 4C00h
        int 21h

m_noswitch: db 'w16 is not running in standard mode.', 0Dh, 0Ah, '$'
; the MS-DOS Prompt's banner, as Windows 3.1's DSWAP writes it (to
; standard output), "w16" where it says "Windows"
m_banner:
        db 0Dh, 0Ah
        db '       ', 0C9h
        times 64 db 0CDh
        db 0BBh, 0Dh, 0Ah
        db '       ', 0BAh, '  ', 0FEh, ' Type EXIT and press ENTER to quit this MS-DOS prompt and    ', 0BAh, 0Dh, 0Ah
        db '       ', 0BAh, '    return to w16.                                              ', 0BAh, 0Dh, 0Ah
        db '       ', 0BAh, '  ', 0FEh, ' Press ALT+TAB to switch to w16 or another application.      ', 0BAh, 0Dh, 0Ah
        db '       ', 0C8h
        times 64 db 0CDh
        db 0BCh, 0Dh, 0Ah, '$'

t_end:
