; vx1.asm - VX1.COM: what 386 Enhanced mode gives the program it starts in
; the System VM. Black-box observation (ARCHITECTURE.md section 3 and 12).
; Runs in place of SYSTEM\KRNL386.EXE under WIN /3 (tests/vxobs.sh), with
; Microsoft's WIN386.EXE or w16's, and logs to C:\VX1.LOG:
;   V86 mode: the machine status word, its command tail, environment
;     strings and program path, the parent PSP chain, free DOS memory,
;     XMS (version, free, A20), INT 2Fh 1600h, 160Ah, 1683h (current VM),
;     1680h (release time slice), 1684h (V86 API entry) for every device
;     ID from 0000h to 0040h and the external ones known (0442h, 0483h,
;     0484h, 0486h, 048Bh), 1687h (DPMI entry), 4300h/4310h;
;   protected mode (after the DPMI switch as a 16-bit client, as KRNL386
;     is UNVERIFIED to do): the selectors and their descriptors, INT 31h
;     0400h (version), 0006h, 0500h (free memory), INT 2Fh 1684h in
;     protected mode for the same device IDs, 168Ah (vendor API), 1600h,
;     160Ah, 1683h; then exits with INT 21h 4Ch 0, which ends Windows.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\VX1.LOG'
%include "dxlib.inc"

start:  mov ax, cs
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
        NOTE 'VX1 probe'
        smsw ax
        OBS 'v86_msw', ax
        pushf
        pop ax
        OBS 'v86_flags', ax
        ; the command tail
        mov si, s_tail
        call puts
        xor cx, cx
        mov cl, [80h]
        mov si, 81h
.tl:    jcxz .tld
        lodsb
        call putc_safe
        dec cx
        jmp .tl
.tld:   mov al, '|'
        call putc
        call crlf
        ; environment strings and program path
        push ds
        mov ax, [2Ch]
        OBS 'envseg_nonzero', ax
        pop ds
        mov es, [2Ch]
        xor di, di
.es:    cmp byte [es:di], 0
        je .esd
        mov si, s_env
        call puts
.ec:    mov al, [es:di]
        inc di
        test al, al
        jz .ecd
        call putc_safe
        jmp .ec
.ecd:   call crlf
        jmp .es
.esd:   inc di
        mov ax, [es:di]
        OBS 'env_count_word', ax
        add di, 2
        mov si, s_prog
        call puts
.pc:    mov al, [es:di]
        inc di
        test al, al
        jz .pcd
        call putc_safe
        jmp .pc
.pcd:   call crlf
        push ds
        pop es
        call flush
        ; parent PSP chain: each parent's memory block owner name and size
        mov ax, [psp]
        mov cx, 4
.pp:    push cx
        mov es, ax
        mov bx, [es:16h]
        cmp bx, ax
        je .ppd
        push bx
        dec bx
        mov es, bx
        mov si, 8
        mov di, tname
.pn:    mov al, [es:si]
        mov [di], al
        inc si
        inc di
        cmp si, 16
        jb .pn
        mov byte [di], 0
        mov ax, [es:3]
        mov [t1], ax
        mov si, s_par
        call puts
        mov si, tname
        call puts
        mov al, ' '
        call putc
        mov ax, [t1]
        call puthex
        call crlf
        pop ax
        pop cx
        loop .pp
        push cx
.ppd:   pop cx
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
        cmp al, 80h
        jne .noxms
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        push ds
        pop es
        xor ax, ax
        call far [xms]
        REGS 'xms_00'
        mov ah, 08h
        xor bx, bx
        call far [xms]
        REGS 'xms_08_free'
        mov ah, 07h
        call far [xms]
        OBS 'xms_a20', ax
.noxms: push ds
        pop es
        call flush
        ; Windows INT 2Fh interface, V86 view
        mov ax, 1600h
        int 2Fh
        REGS 'v86_2f_1600'
        mov ax, 160Ah
        xor bx, bx
        xor cx, cx
        int 2Fh
        REGS 'v86_2f_160A'
        mov ax, 1683h
        xor bx, bx
        int 2Fh
        REGS 'v86_2f_1683'
        mov ax, 1680h
        int 2Fh
        REGS 'v86_2f_1680'
        mov ax, 1689h
        xor bx, bx
        int 2Fh
        REGS 'v86_2f_1689'
        mov word [mode], 0
        call devapis
        call flush
        ; DPMI
        mov ax, 1687h
        int 2Fh
        REGS 'v86_2f_1687'
        mov [pment], di
        mov [pment+2], es
        mov [hdsize], si
        push ds
        pop es
        call flush
        mov si, [hdsize]
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        jnc .hd
        NOTE 'VX1 no memory for the host data'
        call flush
        mov ax, 4C02h
        int 21h
.hd:    mov es, ax
.nohd:  mov ax, 0                       ; a 16-bit client
        call far [pment]
        jnc .pm
        mov [p_ax], ax
        push ds
        pop es
        NOTE 'VX1 switch failed'
        OBS 'ax', [p_ax]
        call flush
        mov ax, 4C01h
        int 21h

; ---------------- protected mode ----------------
.pm:    mov [p_es], es
        push ds
        pop es
        NOTE 'VX1 protected mode'
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
        OBS 'pm_envsel', bx
        call obs_desc
        smsw ax
        OBS 'pm_msw', ax
        pushf
        pop ax
        OBS 'pm_flags', ax
        mov ax, 0400h
        int 31h
        REGS 'dpmi_0400'
        mov di, tbuf
        mov ax, 0500h
        int 31h
        sbb ax, ax
        OBS 'dpmi_0500_cf', ax
        OBSD 'dpmi_0500_largest', [tbuf+2], [tbuf]
        OBSD 'dpmi_0500_maxunlocked', [tbuf+6], [tbuf+4]
        OBSD 'dpmi_0500_maxlocked', [tbuf+10], [tbuf+8]
        OBSD 'dpmi_0500_linpages', [tbuf+14], [tbuf+12]
        OBSD 'dpmi_0500_unlocked', [tbuf+18], [tbuf+16]
        OBSD 'dpmi_0500_freepages', [tbuf+22], [tbuf+20]
        OBSD 'dpmi_0500_physpages', [tbuf+26], [tbuf+24]
        OBSD 'dpmi_0500_freelin', [tbuf+30], [tbuf+28]
        OBSD 'dpmi_0500_swappages', [tbuf+34], [tbuf+32]
        call flush
        mov ax, 1600h
        int 2Fh
        REGS 'pm_2f_1600'
        mov ax, 160Ah
        xor bx, bx
        xor cx, cx
        int 2Fh
        REGS 'pm_2f_160A'
        mov ax, 1683h
        xor bx, bx
        int 2Fh
        REGS 'pm_2f_1683'
        mov ax, 1686h
        int 2Fh
        REGS 'pm_2f_1686'
        ; vendor API ("MS-DOS")
        mov si, msdos
        mov ax, 168Ah
        int 2Fh
        push ds
        pop es
        REGS 'pm_2f_168A_msdos'
        mov word [mode], 1
        call devapis
        call flush
        NOTE 'VX1 done'
        call flush
        mov ax, 4C00h
        int 21h

; devapis: INT 2Fh 1684h for each device ID; logs the non-null entries
devapis:
        xor bx, bx
.d:     call .one
        inc bx
        cmp bx, 41h
        jb .d
        mov si, xids
.x:     lodsw
        test ax, ax
        jz .r
        mov bx, ax
        push si
        call .one
        pop si
        jmp .x
.r:     ret
.one:   push bx
        xor di, di
        mov es, di
        mov ax, 1684h
        int 2Fh
        mov ax, es
        mov [t1], ax
        mov [t2], di
        push ds
        pop es
        pop bx
        mov ax, [t1]
        or ax, [t2]
        jz .z
        mov si, s_api
        cmp word [mode], 0
        je .v
        mov si, s_papi
.v:     call puts
        mov ax, bx
        call puthex
        mov al, '='
        call putc
        mov ax, [t1]
        call puthex
        mov al, ':'
        call putc
        mov ax, [t2]
        call puthex
        call crlf
.z:     ret

; obs_desc: BX = selector; its base (INT 31h 0006h), LSL limit, LAR rights
obs_desc:
        push ax
        push cx
        push dx
        mov si, s_desc
        call puts
        mov ax, bx
        call puthex
        mov si, s_base
        call puts
        mov ax, 0006h
        int 31h
        mov ax, cx
        call puthex
        mov ax, dx
        call puthex
        mov si, s_lim
        call puts
        lsl ax, bx
        call puthex
        mov si, s_ar
        call puts
        lar ax, bx
        call puthex
        call crlf
        pop dx
        pop cx
        pop ax
        ret

%include "dxlib.inc"

msdos           db 'MS-DOS', 0
xids            dw 0442h, 0483h, 0484h, 0486h, 048Bh, 0
s_tail          db 'OBS tail=|', 0
s_env           db 'OBS env=', 0
s_prog          db 'OBS progpath=', 0
s_par           db 'OBS parent ', 0
s_api           db 'OBS v86_api_', 0
s_papi          db 'OBS pm_api_', 0
s_desc          db 'OBS desc ', 0
s_base          db ' base=', 0
s_lim           db ' limit=', 0
s_ar            db ' ar=', 0

        section .bss
psp     resw 1
xms     resd 1
pment   resd 1
hdsize  resw 1
mode    resw 1
p_ax    resw 1
p_es    resw 1
t1      resw 1
t2      resw 1
tname   resb 10
tbuf    resb 64
logbuf  resb LOGSIZE
        resb 1024
stack_top:
