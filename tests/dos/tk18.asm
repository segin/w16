; tk18.asm - TK18.COM, the DOSX -> KRNL286 contract probe.
;
; Black-box observation (ARCHITECTURE.md section 3): a scratch copy of the
; Windows 3.1 install gets this program as SYSTEM\KRNL286.EXE (and/or
; KRNL386.EXE); WIN /S then starts it the way it starts KERNEL. It writes
; what it was given to C:\TK18.LOG as "OBS name=value" lines:
;   - the registers and CPU mode (MSW) at entry, the PSP, command tail,
;     environment and program path, and those of its parent PSPs;
;   - the DOS memory arena, free memory, the interrupt vectors;
;   - the DPMI host (INT 2Fh 1687h, 1600h, 4300h), then, after switching
;     to protected mode through it: the selectors it handed over, INT 31h
;     0400h, which INT 31h functions it supports, the vendor extension
;     ("MS-DOS", INT 2Fh 168Ah) and the LDT selector it gives, and the
;     non-zero descriptors of that LDT;
; and then exits from protected mode with exit code TK18_EXIT (below),
; so the reference run shows what WIN.COM/DOSX does after KERNEL ends.
;
; A .COM image: DOS loads it by content, whatever the file is called.

        bits 16
        cpu 386
        org 100h

%define LOGSIZE 24000
%ifndef TK18_EXIT
%define TK18_EXIT 0
%endif

start:
        ; -- the entry state, before anything changes it --
        mov [cs:e_ax], ax
        mov [cs:e_bx], bx
        mov [cs:e_cx], cx
        mov [cs:e_dx], dx
        mov [cs:e_si], si
        mov [cs:e_di], di
        mov [cs:e_bp], bp
        mov [cs:e_sp], sp
        mov [cs:e_ds], ds
        mov [cs:e_es], es
        mov [cs:e_ss], ss
        pushf
        pop word [cs:e_fl]
        smsw [cs:e_msw]
        mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov [logseg], ax
        ; keep only this program's 64K
        mov bx, 1000h
        mov ah, 4Ah
        int 21h

        ; create the log
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc near quit_rm
        mov bx, ax
        mov ah, 3Eh
        int 21h

        mov si, s_entry
        call puts
        mov si, n_ax
        mov ax, [e_ax]
        call obs_hex
        mov si, n_bx
        mov ax, [e_bx]
        call obs_hex
        mov si, n_cx
        mov ax, [e_cx]
        call obs_hex
        mov si, n_dx
        mov ax, [e_dx]
        call obs_hex
        mov si, n_si
        mov ax, [e_si]
        call obs_hex
        mov si, n_di
        mov ax, [e_di]
        call obs_hex
        mov si, n_bp
        mov ax, [e_bp]
        call obs_hex
        mov si, n_sp
        mov ax, [e_sp]
        call obs_hex
        mov si, n_cs
        mov ax, cs
        call obs_hex
        mov si, n_ds
        mov ax, [e_ds]
        call obs_hex
        mov si, n_es
        mov ax, [e_es]
        call obs_hex
        mov si, n_ss
        mov ax, [e_ss]
        call obs_hex
        mov si, n_fl
        mov ax, [e_fl]
        call obs_hex
        mov si, n_msw
        mov ax, [e_msw]
        call obs_hex

        ; DOS version
        mov ax, 3000h
        int 21h
        mov si, n_dosver
        call obs_hex
        mov ax, 3306h
        xor bx, bx
        int 21h
        mov ax, bx
        mov si, n_truever
        call obs_hex

        ; this PSP and its parents (up to 6 levels)
        mov ax, [psp]
        mov byte [level], '0'
.pspl:  mov [cur], ax
        call dump_psp
        mov es, [cur]
        mov ax, [es:16h]
        push ds
        pop es
        cmp ax, [cur]
        je .pspd
        or ax, ax
        jz .pspd
        inc byte [level]
        cmp byte [level], '6'
        jb .pspl
.pspd:
        ; free conventional memory
        mov bx, 0FFFFh
        mov ah, 48h
        int 21h
        mov ax, bx
        mov si, n_freepara
        call obs_hex
        ; the memory arena
        call dump_mcbs
        ; interrupt vectors
        call dump_ivt
        ; multiplex checks
        mov ax, 1600h
        int 2Fh
        mov si, n_2f1600
        call obs_hex
        mov ax, 4300h
        int 2Fh
        mov si, n_2f4300
        call obs_hex
        mov ax, 1687h
        int 2Fh
        mov [d_ax], ax
        mov [d_bx], bx
        mov [d_cx], cx
        mov [d_dx], dx
        mov [d_si], si
        mov [d_di], di
        mov [d_es], es
        push ds
        pop es
        mov si, n_d_ax
        mov ax, [d_ax]
        call obs_hex
        mov si, n_d_bx
        mov ax, [d_bx]
        call obs_hex
        mov si, n_d_cx
        mov ax, [d_cx]
        call obs_hex
        mov si, n_d_dx
        mov ax, [d_dx]
        call obs_hex
        mov si, n_d_si
        mov ax, [d_si]
        call obs_hex
        mov si, n_d_es
        mov ax, [d_es]
        call obs_hex
        mov si, n_d_di
        mov ax, [d_di]
        call obs_hex
        call flush
        cmp word [d_ax], 0
        jne near quit_rm

        ; host data
        mov bx, [d_si]
        xor ax, ax
        or bx, bx
        jz .nohost
        mov ah, 48h
        int 21h
        jc near quit_rm
.nohost:
        mov es, ax
        xor ax, ax
        call far [d_di]
        jnc .pm
        push ds
        pop es
        mov si, s_nopm
        call puts
        call flush
        jmp quit_rm

        ; ---------------- protected mode ----------------
.pm:    mov [sel_es], es
        mov [logseg], ds
        push ds
        pop es
        mov si, s_pm
        call puts
        mov si, n_pcs
        mov ax, cs
        call obs_hex
        mov si, n_pds
        mov ax, ds
        call obs_hex
        mov si, n_pss
        mov ax, ss
        call obs_hex
        mov si, n_pes
        mov ax, [sel_es]
        call obs_hex
        smsw ax
        mov si, n_pmsw
        call obs_hex
        sldt ax
        mov si, n_sldt
        call obs_hex
        str ax
        mov si, n_str
        call obs_hex
        ; limits and bases of what we were handed
        mov bx, cs
        call obs_desc
        mov bx, ds
        call obs_desc
        mov bx, [sel_es]
        call obs_desc
        mov es, [sel_es]
        mov bx, [es:2Ch]
        push ds
        pop es
        mov ax, bx
        mov si, n_penv
        call obs_hex
        call obs_desc

        ; INT 31h 0400h
        mov ax, 0400h
        int 31h
        mov [t1], ax
        mov [t2], bx
        mov [t3], cx
        mov [t4], dx
        mov si, n_v_ax
        mov ax, [t1]
        call obs_hex
        mov si, n_v_bx
        mov ax, [t2]
        call obs_hex
        mov si, n_v_cx
        mov ax, [t3]
        call obs_hex
        mov si, n_v_dx
        mov ax, [t4]
        call obs_hex
        ; selector increment
        mov ax, 0003h
        int 31h
        mov si, n_selinc
        call obs_hex
        ; free memory information (0500h)
        mov di, meminfo
        mov ax, 0500h
        int 31h
        sbb ax, ax
        mov si, n_0500cf
        call obs_hex
        mov si, n_mi
        mov bx, 0
.mil:   mov ax, [meminfo+bx]
        call obs_hexidx
        add bx, 2
        cmp bx, 48
        jb .mil
        call flush

        ; which INT 31h functions are there: each with harmless arguments
        mov si, fntab
.fnl:   lodsw
        cmp ax, 0FFFFh
        je .fnd
        mov [t1], ax
        push si
        call try31
        pop si
        jmp .fnl
.fnd:
        ; INT 2Fh 1686h in protected mode
        mov ax, 1686h
        int 2Fh
        mov si, n_1686
        call obs_hex
        ; INT 2Fh 168Ah "MS-DOS"
        mov si, msdos
        mov ax, 168Ah
        int 2Fh
        mov [t1], ax
        mov [t2], es
        mov [t3], di
        push ds
        pop es
        mov si, n_168a
        mov ax, [t1]
        call obs_hex
        cmp byte [t1], 8Ah
        je near .nolx
        mov si, n_168a_es
        mov ax, [t2]
        call obs_hex
        mov si, n_168a_di
        mov ax, [t3]
        call obs_hex
        mov ax, [t3]
        mov [vend], ax
        mov ax, [t2]
        mov [vend+2], ax
        mov ax, 0100h
        call far [vend]
        sbb bx, bx
        mov [ldtsel], ax
        mov [t4], bx
        mov si, n_ldtsel
        call obs_hex
        mov si, n_ldtcf
        mov ax, [t4]
        call obs_hex
        cmp word [t4], 0
        jne near .nolx
        mov bx, [ldtsel]
        call obs_desc
        ; INT 31h 0A00h too
        mov si, msdos
        mov ax, 0A00h
        int 31h
        sbb bx, bx
        push ds
        pop es
        mov si, n_0a00
        mov ax, bx
        call obs_hex
        call flush
        ; non-zero LDT descriptors
        mov bx, [ldtsel]
        lsl cx, bx
        jnz near .nolx
        mov [t4], cx
        mov si, n_ldtlim
        mov ax, cx
        call obs_hex
        xor bx, bx
.ldl:   mov es, [ldtsel]
        mov ax, [es:bx]
        or ax, [es:bx+2]
        or ax, [es:bx+4]
        or ax, [es:bx+6]
        jz .ldn
        mov ax, [es:bx]
        mov [desc], ax
        mov ax, [es:bx+2]
        mov [desc+2], ax
        mov ax, [es:bx+4]
        mov [desc+4], ax
        mov ax, [es:bx+6]
        mov [desc+6], ax
        push ds
        pop es
        call put_ldt
        cmp word [logpos], LOGSIZE - 200
        jb .ldn
        call flush
.ldn:   push ds
        pop es
        add bx, 8
        jc .ldd
        cmp bx, [t4]
        jbe .ldl
.ldd:
.nolx:  push ds
        pop es
        call flush
        ; how DOS calls look: INT 21h AH=62h (PSP), AH=2Fh (DTA)
        mov ah, 62h
        int 21h
        mov ax, bx
        mov si, n_pm62
        call obs_hex
        call flush
        ; DOS memory from protected mode: INT 21h AH=48h directly
        mov bx, 1
        mov ah, 48h
        int 21h
        sbb bx, bx
        mov [t1], ax
        mov [t2], bx
        mov si, n_pm48ax
        call obs_hex
        mov si, n_pm48cf
        mov ax, [t2]
        call obs_hex
        call flush
        cmp word [t2], 0
        jne .no48
        mov bx, [t1]
        call obs_desc
        mov es, [t1]
        mov ah, 49h
        int 21h
        sbb ax, ax
        push ds
        pop es
        mov si, n_pm49cf
        call obs_hex
        call flush
.no48:  ; INT 31h 0100h asking for the largest block
        mov bx, 0FFFFh
        mov ax, 0100h
        int 31h
        mov si, n_0100max
        mov ax, bx
        call obs_hex
        call flush
        ; real-mode INT 21h AH=48h through INT 31h 0300h
        mov word [scratch+1Ch], 4800h   ; eax
        mov word [scratch+10h], 1       ; ebx
        mov word [scratch+20h], 0
        mov word [scratch+2Eh], 0
        mov word [scratch+30h], 0
        push ds
        pop es
        mov di, scratch
        mov bx, 21h
        xor cx, cx
        mov ax, 0300h
        int 31h
        sbb ax, ax
        mov si, n_rm48cf
        call obs_hex
        mov si, n_rm48ax
        mov ax, [scratch+1Ch]
        call obs_hex
        mov si, n_rm48fl
        mov ax, [scratch+20h]
        call obs_hex
        call flush
        ; more INT 31h functions
        mov si, fntab2
.f2l:   lodsw
        cmp ax, 0FFFFh
        je .f2d
        mov [t1], ax
        push si
        call try31b
        pop si
        call flush
        jmp .f2l
.f2d:
        mov si, s_end
        call puts
        call flush
        mov ax, 4C00h | TK18_EXIT
        int 21h

quit_rm:
        push cs
        pop ds
        mov si, s_end
        call puts
        call flush
        mov ax, 4C00h | TK18_EXIT
        int 21h


; ---------------------------------------------------------------------------
; try31: INT 31h function [t1] with harmless arguments; logs CF and AX.
try31:
        push ds
        pop es
        mov ax, [t1]
        xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        mov di, scratch
        cmp ax, 0000h
        jne .a1
        mov cx, 1               ; allocate 1 descriptor
        int 31h
        jc .rep
        mov [tsel], ax
        jmp .rep
.a1:    cmp ax, 0001h
        jne .a2
        mov bx, [tsel]          ; free it
        int 31h
        jmp .rep
.a2:    cmp ax, 0002h
        jne .a3
        mov bx, 0B800h
        int 31h
        jmp .rep
.a3:    cmp ax, 0006h
        je .ds
        cmp ax, 000Bh
        je .ds
        cmp ax, 000Ah
        jne .a4
.ds:    mov bx, ds
        int 31h
        jmp .rep
.a4:    cmp ax, 0100h
        jne .a5
        mov bx, 1               ; 1 paragraph
        int 31h
        jc .rep
        mov [tsel], dx
        jmp .rep
.a5:    cmp ax, 0101h
        jne .a6
        mov dx, [tsel]
        int 31h
        jmp .rep
.a6:    cmp ax, 0200h
        je .v21
        cmp ax, 0204h
        je .v21
        cmp ax, 0202h
        jne .a7
        mov bl, 0Dh
        int 31h
        jmp .rep
.v21:   mov bl, 21h
        int 31h
        jmp .rep
.a7:    cmp ax, 0300h
        jne .a8
        ; simulate real-mode INT 21h AH=30h
        mov word [scratch+1Ch], 3000h
        mov word [scratch+20h], 0       ; flags
        mov word [scratch+2Eh], 0       ; sp: host stack
        mov word [scratch+30h], 0
        mov bx, 21h
        int 31h
        jmp .rep
.a8:    cmp ax, 0501h
        jne .a9
        mov cx, 1000h           ; 4 KB
        int 31h
        jc .rep
        mov [tmem], di
        mov [tmem+2], si
        jmp .rep
.a9:    cmp ax, 0502h
        jne .a10
        mov di, [tmem]
        mov si, [tmem+2]
        int 31h
        jmp .rep
.a10:   cmp ax, 0305h
        je .go
        cmp ax, 0306h
        je .go
        cmp ax, 0400h
        je .go
        cmp ax, 0003h
        je .go
        cmp ax, 0900h
        je .go
        cmp ax, 0901h
        je .go
        cmp ax, 0902h
        je .go
        cmp ax, 0604h
        je .go
        cmp ax, 0800h
        je .go
        ; anything else: only an unsupported-function probe with AX
        ; out of range would be safe; skip
        jmp .skip
.go:    int 31h
.rep:   sbb dx, dx
        push ds
        pop es
        mov [t2], ax
        mov [t3], dx
        mov [t4], bx
        mov si, s_obs31
        call puts
        mov ax, [t1]
        call puthex
        mov si, s_cf
        call puts
        mov ax, [t3]
        and ax, 1
        add al, '0'
        call putc
        mov si, s_ax
        call puts
        mov ax, [t2]
        call puthex
        mov si, s_bx
        call puts
        mov ax, [t4]
        call puthex
        call crlf
.skip:  ret

; try31b: INT 31h function [t1] on a fresh descriptor [tsel] (0007h,
; 0008h, 0009h, 000Ch) or harmless queries; logs CF and AX.
try31b:
        push ds
        pop es
        mov ax, [t1]
        cmp ax, 0007h
        jne .b1
        mov ax, 0000h
        mov cx, 1
        int 31h
        mov [tsel], ax
        mov bx, ax
        mov cx, 0000Bh
        mov dx, 08000h
        mov ax, 0007h
        int 31h
        jmp try31.rep
.b1:    cmp ax, 0008h
        jne .b2
        mov bx, [tsel]
        xor cx, cx
        mov dx, 0FFFh
        int 31h
        jmp try31.rep
.b2:    cmp ax, 0009h
        jne .b3
        mov bx, [tsel]
        mov cx, 00F2h
        int 31h
        jmp try31.rep
.b3:    cmp ax, 000Ch
        jne .b4
        mov bx, [tsel]
        mov di, desc
        mov word [desc], 0FFFFh
        mov word [desc+2], 0
        mov word [desc+4], 0F20Ah
        mov word [desc+6], 0
        int 31h
        jmp try31.rep
.b4:    cmp ax, 0001h
        jne .b5
        mov bx, [tsel]
        int 31h
        jmp try31.rep
.b5:    xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        mov di, scratch
        int 31h
        jmp try31.rep

; ---------------------------------------------------------------------------
; dump_psp: [cur] = PSP segment, [level] = '0'..
dump_psp:
        mov si, s_psp
        call puts
        mov al, [level]
        call putc
        mov al, '='
        call putc
        mov ax, [cur]
        call puthex
        mov si, s_parent
        call puts
        mov es, [cur]
        mov ax, [es:16h]
        call puthex
        mov si, s_env
        call puts
        mov ax, [es:2Ch]
        call puthex
        mov si, s_top
        call puts
        mov ax, [es:2]
        call puthex
        call crlf
        ; command tail
        mov si, s_tail
        call puts
        mov al, [level]
        call putc
        mov si, s_eq_q
        call puts
        mov es, [cur]
        xor cx, cx
        mov cl, [es:80h]
        mov bx, 81h
.tl:    jcxz .td
        mov al, [es:bx]
        call putc_safe
        inc bx
        dec cx
        jmp .tl
.td:    mov al, '"'
        call putc
        call crlf
        ; environment: every string, then the program path
        mov es, [cur]
        mov ax, [es:2Ch]
        or ax, ax
        jz .ed
        mov [envseg], ax
        xor bx, bx
.el:    mov es, [envseg]
        cmp byte [es:bx], 0
        je .eend
        mov si, s_envstr
        call puts
        mov al, [level]
        call putc
        mov al, '='
        call putc
.ec:    mov es, [envseg]
        mov al, [es:bx]
        inc bx
        or al, al
        jz .en
        call putc_safe
        jmp .ec
.en:    call crlf
        cmp word [logpos], LOGSIZE - 300
        jb .el
        call flush
        jmp .el
.eend:  inc bx
        mov si, s_prog
        call puts
        mov al, [level]
        call putc
        mov al, '='
        call putc
        mov es, [envseg]
        mov ax, [es:bx]
        add bx, 2
        push ax
        call puthex
        mov al, ' '
        call putc
        pop ax
        or ax, ax
        jz .pd
.pc:    mov es, [envseg]
        mov al, [es:bx]
        inc bx
        or al, al
        jz .pd
        call putc_safe
        jmp .pc
.pd:    call crlf
.ed:    push ds
        pop es
        call flush
        ret

; dump_mcbs: walk the DOS memory arena from INT 21h AH=52h
dump_mcbs:
        mov ah, 52h
        int 21h
        mov ax, [es:bx-2]
        push ds
        pop es
.ml:    mov [cur], ax
        mov si, s_mcb
        call puts
        mov ax, [cur]
        call puthex
        mov al, ' '
        call putc
        mov es, [cur]
        mov al, [es:0]
        call putc_safe
        mov si, s_owner
        call puts
        mov es, [cur]
        mov ax, [es:1]
        call puthex
        mov si, s_size
        call puts
        mov es, [cur]
        mov ax, [es:3]
        call puthex
        mov si, s_name
        call puts
        mov bx, 8
.nl:    mov es, [cur]
        mov al, [es:bx]
        or al, al
        jz .nd
        call putc_safe
        inc bx
        cmp bx, 16
        jb .nl
.nd:    call crlf
        mov es, [cur]
        mov al, [es:0]
        mov dx, [es:3]
        push ds
        pop es
        cmp al, 'M'
        jne .md
        mov ax, [cur]
        add ax, dx
        inc ax
        cmp word [logpos], LOGSIZE - 200
        jb .ml
        push ax
        call flush
        pop ax
        jmp .ml
.md:    call flush
        ret

; dump_ivt: all 256 real-mode vectors
dump_ivt:
        xor bx, bx
.il:    mov si, s_ivt
        call puts
        mov ax, bx
        shr ax, 2
        call puthex2
        mov al, '='
        call putc
        xor ax, ax
        mov es, ax
        mov ax, [es:bx+2]
        push ds
        pop es
        call puthex
        mov al, ':'
        call putc
        xor ax, ax
        mov es, ax
        mov ax, [es:bx]
        push ds
        pop es
        call puthex
        call crlf
        cmp word [logpos], LOGSIZE - 200
        jb .in
        call flush
.in:    add bx, 4
        cmp bx, 400h
        jb .il
        call flush
        ret

; obs_desc: BX = selector; logs base (INT 31h 0006h), LSL limit, LAR rights
obs_desc:
        push bx
        mov si, s_desc
        call puts
        mov ax, bx
        call puthex
        mov si, s_base
        call puts
        pop bx
        push bx
        mov ax, 0006h
        int 31h
        jnc .b
        mov cx, 0FFFFh
        mov dx, cx
.b:     mov ax, cx
        call puthex
        mov ax, dx
        call puthex
        mov si, s_limit
        call puts
        pop bx
        push bx
        lsl ax, bx
        jz .l
        mov ax, 0FFFFh
.l:     call puthex
        mov si, s_ar
        call puts
        pop bx
        lar ax, bx
        jz .r
        xor ax, ax
.r:     call puthex
        call crlf
        ret

; put_ldt: BX = byte offset in LDT, desc = its 8 bytes
put_ldt:
        mov si, s_ldt
        call puts
        mov ax, bx
        or ax, 7
        call puthex
        mov al, '='
        call putc
        mov ax, [desc+6]
        call puthex
        mov ax, [desc+4]
        call puthex
        mov al, ' '
        call putc
        mov ax, [desc+2]
        call puthex
        mov ax, [desc]
        call puthex
        call crlf
        ret

; ---------------------------------------------------------------------------
; log buffer helpers (DS = our data, in either mode)
obs_hex:        ; "OBS <name>=<AX hex>"
        push ax
        push si
        mov si, s_obs
        call puts
        pop si
        call puts
        mov al, '='
        call putc
        pop ax
        call puthex
        call crlf
        ret

obs_hexidx:     ; "OBS <name><BX>=<AX>"
        push ax
        push si
        push si
        mov si, s_obs
        call puts
        pop si
        call puts
        mov ax, bx
        call puthex2
        mov al, '='
        call putc
        pop si
        pop ax
        call puthex
        call crlf
        ret

puts:           ; DS:SI, NUL-terminated
        lodsb
        or al, al
        jz .d
        call putc
        jmp puts
.d:     ret

putc_safe:
        cmp al, 20h
        jb .x
        cmp al, 7Eh
        ja .x
        jmp putc
.x:     push ax
        mov al, '\'
        call putc
        pop ax
        jmp puthex2

putc:
        push di
        push es
        push ds
        pop es
        mov di, [logpos]
        cmp di, LOGSIZE
        jae .full
        mov [logbuf+di], al
        inc word [logpos]
.full:  pop es
        pop di
        ret

crlf:   mov al, 13
        call putc
        mov al, 10
        jmp putc

puthex:         ; AX
        push ax
        mov al, ah
        call puthex2
        pop ax
puthex2:        ; AL
        push ax
        shr al, 4
        call .n
        pop ax
.n:     and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     jmp putc

; flush: append the buffer to the log file (INT 21h, in either mode)
flush:
        push ax
        push bx
        push cx
        push dx
        cmp word [logpos], 0
        je .d
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jc .d
        mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov cx, [logpos]
        mov dx, logbuf
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov word [logpos], 0
.d:     pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
logname         db 'C:\TK18.LOG', 0
msdos           db 'MS-DOS', 0
s_entry         db 'TK18 probe', 13, 10, 0
s_pm            db 'TK18 protected mode', 13, 10, 0
s_nopm          db 'TK18 DPMI switch failed', 13, 10, 0
s_end           db 'TK18 end', 13, 10, 0
s_obs           db 'OBS ', 0
n_ax            db 'entry_ax', 0
n_bx            db 'entry_bx', 0
n_cx            db 'entry_cx', 0
n_dx            db 'entry_dx', 0
n_si            db 'entry_si', 0
n_di            db 'entry_di', 0
n_bp            db 'entry_bp', 0
n_sp            db 'entry_sp', 0
n_cs            db 'entry_cs', 0
n_ds            db 'entry_ds', 0
n_es            db 'entry_es', 0
n_ss            db 'entry_ss', 0
n_fl            db 'entry_flags', 0
n_msw           db 'entry_msw', 0
n_dosver        db 'dos_30h_ax', 0
n_truever       db 'dos_3306h_bx', 0
n_freepara      db 'dos_free_paras', 0
n_2f1600        db 'int2f_1600_ax', 0
n_2f4300        db 'int2f_4300_ax', 0
n_d_ax          db 'dpmi1687_ax', 0
n_d_bx          db 'dpmi1687_bx', 0
n_d_cx          db 'dpmi1687_cx', 0
n_d_dx          db 'dpmi1687_dx', 0
n_d_si          db 'dpmi1687_si', 0
n_d_es          db 'dpmi1687_es', 0
n_d_di          db 'dpmi1687_di', 0
n_pcs           db 'pm_cs', 0
n_pds           db 'pm_ds', 0
n_pss           db 'pm_ss', 0
n_pes           db 'pm_es_psp', 0
n_pmsw          db 'pm_msw', 0
n_sldt          db 'pm_sldt', 0
n_str           db 'pm_str', 0
n_penv          db 'pm_psp_env', 0
n_v_ax          db 'dpmi0400_ax', 0
n_v_bx          db 'dpmi0400_bx', 0
n_v_cx          db 'dpmi0400_cx', 0
n_v_dx          db 'dpmi0400_dx', 0
n_selinc        db 'dpmi0003_ax', 0
n_0500cf        db 'dpmi0500_cf', 0
n_mi            db 'dpmi0500_info', 0
n_1686          db 'pm_int2f_1686_ax', 0
n_168a          db 'pm_int2f_168a_ax', 0
n_168a_es       db 'pm_int2f_168a_es', 0
n_168a_di       db 'pm_int2f_168a_di', 0
n_ldtsel        db 'vendor_0100_ax', 0
n_ldtcf         db 'vendor_0100_cf', 0
n_0a00          db 'dpmi0a00_cf', 0
n_ldtlim        db 'ldt_limit', 0
n_pm62          db 'pm_int21_62_bx', 0
n_pm48ax        db 'pm_int21_48_ax', 0
n_pm48cf        db 'pm_int21_48_cf', 0
n_pm49cf        db 'pm_int21_49_cf', 0
n_0100max       db 'dpmi0100_max_bx', 0
n_rm48cf        db 'rm_int21_48_via0300_cf', 0
n_rm48ax        db 'rm_int21_48_via0300_ax', 0
n_rm48fl        db 'rm_int21_48_via0300_fl', 0
s_obs31         db 'OBS int31_', 0
s_cf            db ' cf=', 0
s_ax            db ' ax=', 0
s_bx            db ' bx=', 0
s_psp           db 'OBS psp', 0
s_parent        db ' parent=', 0
s_env           db ' env=', 0
s_top           db ' top=', 0
s_tail          db 'OBS tail', 0
s_eq_q          db '="', 0
s_envstr        db 'OBS envstr', 0
s_prog          db 'OBS progpath', 0
s_mcb           db 'OBS mcb ', 0
s_owner         db ' owner=', 0
s_size          db ' size=', 0
s_name          db ' name=', 0
s_ivt           db 'OBS ivt', 0
s_desc          db 'OBS desc ', 0
s_base          db ' base=', 0
s_limit         db ' limit=', 0
s_ar            db ' ar=', 0
s_ldt           db 'OBS ldt ', 0

fntab   dw 0000h, 0001h, 0002h, 0003h, 0006h, 000Ah, 000Bh
        dw 0100h, 0101h, 0200h, 0202h, 0204h, 0300h, 0305h, 0306h
        dw 0400h, 0501h, 0502h, 0604h, 0800h, 0900h, 0901h, 0902h
        dw 0FFFFh
fntab2  dw 0007h, 0008h, 0009h, 000Ch, 0001h, 0004h, 0005h
        dw 0600h, 0601h, 0702h, 0703h, 0FFFFh

align 2
e_ax    dw 0
e_bx    dw 0
e_cx    dw 0
e_dx    dw 0
e_si    dw 0
e_di    dw 0
e_bp    dw 0
e_sp    dw 0
e_ds    dw 0
e_es    dw 0
e_ss    dw 0
e_fl    dw 0
e_msw   dw 0
psp     dw 0
cur     dw 0
envseg  dw 0
logseg  dw 0
logpos  dw 0
level   db 0, 0
d_ax    dw 0
d_bx    dw 0
d_cx    dw 0
d_dx    dw 0
d_si    dw 0
d_di    dw 0
d_es    dw 0
sel_es  dw 0
t1      dw 0
t2      dw 0
t3      dw 0
t4      dw 0
tsel    dw 0
tmem    dd 0
vend    dd 0
ldtsel  dw 0
desc    times 8 db 0
meminfo times 48 db 0
scratch times 64 db 0
logbuf  times LOGSIZE db 0
        times 512 db 0
stack_top:
