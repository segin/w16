; dx1.asm - DX1.COM: what the Standard-mode DOS extender sets up.
;
; Black-box observation (ARCHITECTURE.md section 3). Runs in place of
; SYSTEM\KRNL286.EXE (KRNL386.EXE removed) under WIN /S, as TK18 does;
; tests/dosxtest.sh runs it under Microsoft's DOSX and under w16's.
; Logs to C:\DX1.LOG:
;   real mode: XMS (version, free memory, A20, which handles exist and
;     their sizes and lock counts), INT 15h 88h, which interrupt vectors
;     point into DOSX's or WSWAP's memory, DOSX's memory block, INT 2Fh
;     1687h;
;   protected mode: every register after the switch, the descriptor
;     tables (SGDT/SIDT/SLDT/STR, the GDT entries a ring-3 program can
;     see), XMS and INT 15h 88h again (through INT 31h 0300h/0301h), the
;     default protected-mode interrupt and exception vectors (0204h,
;     0202h), the vendor entry and the start of the LDT.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\DX1.LOG'
%include "dxlib.inc"

start:  mov [cs:e_fl], sp
        pushf
        pop word [cs:e_fl]
        mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        call logcreate
        NOTE 'DX1 probe'
        smsw ax
        OBS 'rm_msw', ax
        ; parents: DOSX's PSP and memory block, WSWAP's
        mov ax, [psp]
        mov es, ax
        mov ax, [es:16h]
        mov [dpsp], ax
        dec ax
        mov es, ax
        mov ax, [es:3]
        mov [dsize], ax
        OBS 'dosx_block_paras', ax
        mov es, [dpsp]
        mov ax, [es:16h]
        mov [wpsp], ax
        dec ax
        mov es, ax
        mov ax, [es:3]
        mov [wsize], ax
        mov es, [dpsp]
        mov ax, [es:2Ch]
        dec ax
        mov es, ax
        OBS 'dosx_env_paras', [es:3]
        mov es, [dpsp]
        OBS 'dosx_psp_top_minus_psp', [es:2]
        push ds
        pop es
        ; free DOS memory
        mov bx, 0FFFFh
        mov ah, 48h
        int 21h
        OBS 'dos_free_paras', bx
        ; XMS
        mov ax, 4300h
        int 2Fh
        OBSB 'xms_present', al
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        push ds
        pop es
        xor ax, ax
        call far [xms]
        OBS 'xms_ver', ax
        mov ah, 08h
        xor bx, bx
        call far [xms]
        REGS 'xms_08_free'
        mov ah, 07h
        call far [xms]
        OBS 'xms_a20', ax
        call xms_handles
        ; INT 15h 88h
        clc
        mov ah, 88h
        int 15h
        REGS 'int15_88'
        ; INT 2Fh
        mov ax, 1600h
        int 2Fh
        OBS 'int2f_1600', ax
        ; vectors pointing into DOSX's (D) or WSWAP's (W) memory
        xor bx, bx
.iv:    push ds
        xor ax, ax
        mov es, ax
        mov dx, [es:bx+2]
        mov cx, [es:bx]
        pop ds
        push ds
        pop es
        ; linear paragraph of the vector: seg + off/16
        mov ax, cx
        shr ax, 4
        add ax, dx
        mov si, [dpsp]
        cmp ax, si
        jb .iw
        add si, [dsize]
        cmp ax, si
        ja .iw
        mov al, 'D'
        jmp .ip
.iw:    mov si, [wpsp]
        cmp ax, si
        jb .in
        add si, [wsize]
        cmp ax, si
        ja .in
        mov al, 'W'
.ip:    push ax
        mov si, s_ivt
        call puts
        mov ax, bx
        shr ax, 2
        call puthex2
        mov al, '='
        call putc
        pop ax
        call putc
        call crlf
.in:    add bx, 4
        cmp bx, 400h
        jb .iv
        ; DPMI
        mov ax, 1687h
        int 2Fh
        REGS 'dpmi_1687'
        mov [pment], di
        mov [pment+2], es
        mov ax, es
        sub ax, [dpsp]
        OBS 'dpmi_1687_es_minus_dosx_psp', ax
        push ds
        pop es
        call flush
        ; switch (no host data: SI=0 under DOSX; allocate if asked)
        mov ax, 1687h
        int 2Fh
        push ds
        pop es
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        mov es, ax
.nohd:  mov ax, 0
        mov bx, 1111h
        mov cx, 2222h
        mov dx, 3333h
        mov si, 4444h
        mov di, 5555h
        mov bp, 6666h
        call far [pment]
        mov [p_bp], bp
        mov [p_sp], sp
        pushf
        pop word [p_fl]
        jnc .pm
        mov [p_ax], ax
        push ds
        pop es
        NOTE 'DX1 switch failed'
        OBS 'ax', [p_ax]
        call flush
        mov ax, 4C01h
        int 21h

; ---------------- protected mode ----------------
.pm:    mov [p_ax], ax
        mov [p_es], es
        mov [p_bx], bx
        mov [p_cx], cx
        mov [p_dx], dx
        mov [p_si], si
        mov [p_di], di
        push ds
        pop es
        NOTE 'DX1 protected mode'
        OBS 'pm_ax', [p_ax]
        OBS 'pm_bx', [p_bx]
        OBS 'pm_cx', [p_cx]
        OBS 'pm_dx', [p_dx]
        OBS 'pm_si', [p_si]
        OBS 'pm_di', [p_di]
        OBS 'pm_bp', [p_bp]
        OBS 'pm_sp', [p_sp]
        OBS 'pm_flags', [p_fl]
        OBS 'pm_cs', cs
        OBS 'pm_ds', ds
        OBS 'pm_ss', ss
        OBS 'pm_es', [p_es]
        mov bx, cs
        call obs_desc
        mov bx, ds
        call obs_desc
        mov bx, [p_es]
        call obs_desc
        mov es, [p_es]
        mov bx, [es:2Ch]
        push ds
        pop es
        call obs_desc
        ; descriptor tables
        sgdt [tbuf]
        OBS 'gdt_limit', [tbuf]
        OBSD 'gdt_base', [tbuf+4], [tbuf+2]
        sidt [tbuf]
        OBS 'idt_limit', [tbuf]
        OBSD 'idt_base', [tbuf+4], [tbuf+2]
        sldt ax
        OBS 'ldtr', ax
        str ax
        OBS 'tr', ax
        smsw ax
        OBS 'pm_msw', ax
        ; flags: IOPL, and whether CLI/STI change IF
        cli
        pushf
        pop ax
        sti
        OBS 'flags_after_cli', ax
        pushf
        pop ax
        OBS 'flags_after_sti', ax
        ; GDT entries a ring-3 program can see (LAR succeeds)
        sgdt [tbuf]
        mov cx, [tbuf]
        mov bx, 0Bh
.gl:    cmp bx, cx
        ja .gd
        lar ax, bx
        jnz .gn
        mov [t1], ax
        lsl ax, bx
        jz .gl2
        mov ax, 0FFFFh
.gl2:   mov [t2], ax
        mov si, s_gdt
        call puts
        mov ax, bx
        call puthex
        mov si, s_ar
        call puts
        mov ax, [t1]
        call puthex
        mov si, s_lim
        call puts
        mov ax, [t2]
        call puthex
        mov si, s_base
        call puts
        push bx
        push cx
        mov ax, 0006h
        int 31h
        sbb ax, ax
        and ax, 1
        add al, '0'
        call putc
        mov al, ':'
        call putc
        mov ax, cx
        call puthex
        mov ax, dx
        call puthex
        pop cx
        pop bx
        call crlf
.gn:    add bx, 8
        jnc .gl
.gd:    call flush
        ; the whole GDT and IDT through their aliases (000Bh, 0013h)
        mov ax, 000Bh
        mov si, s_gdtr
        call dumptab
        mov ax, 0013h
        mov si, s_idtr
        call dumptab
        ; DPMI version and memory
        mov ax, 0400h
        int 31h
        REGS 'dpmi_0400'
        mov ax, 0003h
        int 31h
        OBS 'dpmi_0003', ax
        mov di, tbuf
        mov ax, 0500h
        int 31h
        sbb ax, ax
        OBS 'dpmi_0500_cf', ax
        xor bx, bx
.mi:    mov si, s_mi
        call puts
        mov ax, bx
        call puthex2
        mov al, '='
        call putc
        mov ax, [tbuf+bx+2]
        call puthex
        mov ax, [tbuf+bx]
        call puthex
        call crlf
        add bx, 4
        cmp bx, 30h
        jb .mi
        ; XMS through INT 31h 0300h (INT 2Fh 4310h) and 0301h
        call rmclear
        mov word [rm_ax], 4310h
        mov bx, 2Fh
        mov ax, 0300h
        call rmint
        mov ax, [rm_bx]
        mov [xms], ax
        mov ax, [rm_es]
        mov [xms+2], ax
        OBS 'pm_xms_entry_seg_minus_dosx_psp', [rm_es]
        call rmclear
        mov word [rm_ax], 0800h
        call xmscall
        OBS 'pm_xms_08_ax', [rm_ax]
        OBS 'pm_xms_08_dx', [rm_dx]
        OBSB 'pm_xms_08_bl', [rm_bx]
        call rmclear
        mov word [rm_ax], 0700h
        call xmscall
        OBS 'pm_xms_a20', [rm_ax]
        ; handles
        mov word [t1], 0
.ph:    call rmclear
        mov word [rm_ax], 0E00h
        mov ax, [t1]
        mov [rm_dx], ax
        call xmscall
        cmp word [rm_ax], 1
        jne .phn
        mov si, s_pxh
        call puts
        mov ax, [t1]
        call puthex
        mov si, s_lock
        call puts
        mov ax, [rm_bx]
        call puthex
        mov si, s_kb
        call puts
        mov ax, [rm_dx]
        call puthex
        call crlf
        ; lock it to see its address (only if it is locked already)
        cmp byte [rm_bx+1], 0
        je .phn
        call rmclear
        mov word [rm_ax], 0C00h
        mov ax, [t1]
        mov [rm_dx], ax
        call xmscall
        OBS 'pm_xms_lock_ax', [rm_ax]
        OBSD 'pm_xms_lock_addr', [rm_dx], [rm_bx]
        call rmclear
        mov word [rm_ax], 0D00h
        mov ax, [t1]
        mov [rm_dx], ax
        call xmscall
.phn:   inc word [t1]
        cmp word [t1], 100h
        jb .ph
        ; INT 15h 88h from real mode
        call rmclear
        mov word [rm_ax], 8800h
        mov bx, 15h
        mov ax, 0300h
        call rmint
        OBS 'pm_int15_88_ax', [rm_ax]
        OBS 'pm_int15_88_fl', [rm_fl]
        call flush
        ; default protected-mode interrupt vectors
        xor bx, bx
.pv:    mov ax, 0204h
        int 31h
        mov [t1], cx
        mov [t2], dx
        mov si, s_pmv
        call puts
        mov al, bl
        call puthex2
        mov al, '='
        call putc
        mov ax, [t1]
        call puthex
        mov al, ':'
        call putc
        mov ax, [t2]
        call puthex
        call crlf
        inc bl
        jnz .pv
        call flush
        ; default exception handlers
        xor bx, bx
.pe:    mov ax, 0202h
        int 31h
        jc .pen
        mov [t1], cx
        mov [t2], dx
        mov si, s_exc
        call puts
        mov al, bl
        call puthex2
        mov al, '='
        call putc
        mov ax, [t1]
        call puthex
        mov al, ':'
        call putc
        mov ax, [t2]
        call puthex
        call crlf
.pen:   inc bl
        cmp bl, 20h
        jb .pe
        mov bl, 20h
        mov ax, 0202h
        int 31h
        REGS 'dpmi_0202_20'
        ; real-mode vectors as INT 31h 0200h sees them
        mov bl, 08h
        mov ax, 0200h
        int 31h
        REGS 'dpmi_0200_08'
        mov bl, 21h
        mov ax, 0200h
        int 31h
        REGS 'dpmi_0200_21'
        call flush
        ; vendor entry and the LDT
        mov si, msdos
        mov ax, 168Ah
        int 2Fh
        mov [vend], di
        mov [vend+2], es
        push ds
        pop es
        OBSB 'vendor_168a_al', al
        OBS 'vendor_168a_es', [vend+2]
        OBS 'vendor_168a_di', [vend]
        mov bx, [vend+2]
        call obs_desc
        mov ax, 0100h
        call far [vend]
        REGS 'vendor_0100'
        mov [ldtsel], ax
        mov bx, ax
        call obs_desc
        ; INT 2Fh 168Ah with another name
        mov si, notms
        mov ax, 168Ah
        int 2Fh
        push ds
        pop es
        OBS 'vendor_other_ax', ax
        ; the LDT up to the first descriptors KERNEL would get, and the
        ; free chain's shape: count free (access 0, base 23..16 = 80h)
        mov bx, [ldtsel]
        lsl cx, bx
        mov [ldtlim], cx
        xor bx, bx
        mov word [nfree], 0
        mov word [nused], 0
        mov word [nzero], 0
.ld:    mov es, [ldtsel]
        mov ax, [es:bx]
        or ax, [es:bx+2]
        or ax, [es:bx+4]
        or ax, [es:bx+6]
        jnz .ld1
        inc word [nzero]
        jmp .ldn
.ld1:   cmp word [es:bx+4], 0080h
        jne .ld2
        inc word [nfree]
        ; log the free ones only while few have been seen
        cmp word [nfree], 4
        ja .ldn
        jmp .ldp
.ld2:   inc word [nused]
.ldp:   mov ax, [es:bx]
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
.ldn:   push ds
        pop es
        add bx, 8
        jc .ldd
        cmp bx, [ldtlim]
        jb .ld
.ldd:   OBS 'ldt_zero', [nzero]
        OBS 'ldt_free', [nfree]
        OBS 'ldt_used', [nused]
        ; the last descriptor's link
        mov bx, [ldtlim]
        and bx, 0FFF8h
        mov es, [ldtsel]
        OBSD 'ldt_last_hi', [es:bx+6], [es:bx+4]
        OBSD 'ldt_last_lo', [es:bx+2], [es:bx]
        push ds
        pop es
        NOTE 'DX1 end'
        call flush
        mov ax, 4C00h
        int 21h

; ---------------------------------------------------------------------------
; xms_handles (real mode): every XMS handle 0..FFh that function 0Eh knows
xms_handles:
        xor dx, dx
.l:     push dx
        mov ah, 0Eh
        call far [xms]
        cmp ax, 1
        jne .n
        mov [t1], bx
        mov [t2], dx
        pop dx
        push dx
        mov si, s_xh
        call puts
        mov ax, dx
        call puthex
        mov si, s_lock
        call puts
        mov ax, [t1]
        call puthex
        mov si, s_kb
        call puts
        mov ax, [t2]
        call puthex
        call crlf
.n:     pop dx
        inc dx
        cmp dx, 100h
        jb .l
        ret

; dumptab: AX = a data selector, SI = line prefix; every 8-byte entry
dumptab:
        mov [t1], ax
        mov [t2], si
        lsl cx, ax
        jnz .d
        xor bx, bx
.l:     mov es, [t1]
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
        mov si, [t2]
        call puts
        mov ax, bx
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
        add bx, 8
        cmp bx, cx
        jb .l
.d:     push ds
        pop es
        call flush
        ret

; rmclear: zero the real-mode call structure
rmclear:
        push di
        push cx
        push ax
        push es
        push ds
        pop es
        mov di, rmregs
        mov cx, 25
        xor ax, ax
        rep stosw
        pop es
        pop ax
        pop cx
        pop di
        ret

; rmint: INT 31h AX (0300h, BX = interrupt) on rmregs
rmint:  push es
        push ds
        pop es
        mov di, rmregs
        xor cx, cx
        int 31h
        pop es
        ret

; xmscall: call the XMS entry with rmregs (INT 31h 0301h)
xmscall:
        push es
        push ds
        pop es
        mov ax, [xms]
        mov [rm_ip], ax
        mov ax, [xms+2]
        mov [rm_cs], ax
        mov di, rmregs
        xor bx, bx
        xor cx, cx
        mov ax, 0301h
        int 31h
        pop es
        ret

; obs_desc: BX = selector; base (0006h), LSL limit, LAR rights
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
        mov si, s_lim
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

%include "dxlib.inc"

msdos           db 'MS-DOS', 0
notms           db 'XYZZY', 0
s_ivt           db 'OBS ivt_owner_', 0
s_gdt           db 'OBS gdt ', 0
s_ar            db ' ar=', 0
s_lim           db ' limit=', 0
s_base          db ' base=', 0
s_mi            db 'OBS dpmi_0500_', 0
s_xh            db 'OBS xms_handle ', 0
s_pxh           db 'OBS pm_xms_handle ', 0
s_lock          db ' lock_free=', 0
s_kb            db ' kb=', 0
s_pmv           db 'OBS pmvec_', 0
s_exc           db 'OBS excvec_', 0
s_desc          db 'OBS desc ', 0
s_gdtr          db 'OBS gdtraw ', 0
s_idtr          db 'OBS idtraw ', 0
s_ldt           db 'OBS ldt ', 0

        section .bss
psp     resw 1
dpsp    resw 1
dsize   resw 1
wpsp    resw 1
wsize   resw 1
xms     resd 1
pment   resd 1
vend    resd 1
e_fl    resw 1
p_ax    resw 1
p_bx    resw 1
p_cx    resw 1
p_dx    resw 1
p_si    resw 1
p_di    resw 1
p_bp    resw 1
p_sp    resw 1
p_fl    resw 1
p_es    resw 1
t1      resw 1
t2      resw 1
ldtsel  resw 1
ldtlim  resw 1
nfree   resw 1
nused   resw 1
nzero   resw 1
desc    resw 4
tbuf    resb 64
rmregs:
rm_di   resd 1
rm_si   resd 1
rm_bp   resd 1
        resd 1
rm_bx   resd 1
rm_dx   resd 1
rm_cx   resd 1
rm_ax   resd 1
rm_fl   resw 1
rm_es   resw 1
rm_ds   resw 1
rm_fs   resw 1
rm_gs   resw 1
rm_ip   resw 1
rm_cs   resw 1
rm_sp   resw 1
rm_ss   resw 1
logbuf  resb LOGSIZE
        resb 1024
stack_top:
