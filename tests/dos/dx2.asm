; dx2.asm - DX2.COM: the DOS extender's INT 31h services in detail.
;
; Black-box observation (ARCHITECTURE.md section 3); runs in the KERNEL
; slot under WIN /S like DX1 (tests/dosxtest.sh). Logs to C:\DX2.LOG:
;   - descriptor management: allocation order and reuse, what a fresh
;     descriptor holds (read through the vendor LDT alias), 0002h, 000Ah,
;     errors for bad selectors and arguments, 0007h-000Ch edge cases;
;   - memory: 0501h-0503h addresses and handles, 0500h before and after,
;     INT 21h AH=48h/49h/4Ah from protected mode (selectors, tiling,
;     resizing, the largest block), 0100h-0102h;
;   - interrupt vectors (0200h-0205h), real-mode calls (0300h-0302h with
;     stack words), callbacks (0303h/0304h, and how many there can be);
;   - a sweep of the remaining functions with harmless arguments.
; Functions that end DOSX (0507h, 0E00h) are left to DX5.

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\DX2.LOG'
%include "dxlib.inc"

; DESC 'name', selector: logs the descriptor's 8 bytes through the LDT alias
%macro DESC 2
        push bx
        mov bx, %2
        call desc_i
        db %1, 0
        pop bx
%endmacro

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
        NOTE 'DX2 probe'
        mov ax, 1687h
        int 2Fh
        mov [pment], di
        mov [pment+2], es
        push ds
        pop es
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        mov es, ax
.nohd:  xor ax, ax
        call far [pment]
        jnc pm
        NOTE 'DX2 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

pm:     push ds
        pop es
        mov [sel_es], es
        mov si, msdos
        mov ax, 168Ah
        int 2Fh
        mov [vend], di
        mov [vend+2], es
        push ds
        pop es
        mov ax, 0100h
        call far [vend]
        mov [ldtsel], ax
        NOTE 'DX2 protected mode'

; ---------------- descriptors ----------------
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'alloc1_a'
        mov [s1], ax
        DESC 'alloc1_a', [s1]
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'alloc1_b'
        mov [s2], ax
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'alloc1_c'
        mov [s3], ax
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'alloc1_d'
        mov [s4], ax
        mov bx, [s2]
        mov ax, 0001h
        int 31h
        REGS 'free_b'
        DESC 'freed_b', [s2]
        mov bx, [s3]
        mov ax, 0001h
        int 31h
        REGS 'free_c'
        DESC 'freed_c', [s3]
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'realloc_1'
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'realloc_2'
        mov cx, 3
        mov ax, 0000h
        int 31h
        REGS 'alloc3'
        mov [s5], ax
        DESC 'alloc3_0', [s5]
        mov bx, [s5]
        add bx, 16
        DESC 'alloc3_2', bx
        ; free the middle one of three, then ask for 2
        mov bx, [s5]
        add bx, 8
        mov ax, 0001h
        int 31h
        REGS 'free_mid3'
        mov cx, 2
        mov ax, 0000h
        int 31h
        REGS 'alloc2_after'
        mov cx, 1
        mov ax, 0000h
        int 31h
        REGS 'alloc1_after'
        mov cx, 2000h
        mov ax, 0000h
        int 31h
        REGS 'alloc2000'
        mov cx, 1000h
        mov ax, 0000h
        int 31h
        REGS 'alloc1000'
        jc .no1k
        mov bx, ax
        mov ax, 0001h
        int 31h
        REGS 'free_first_of_1000'
.no1k:  mov bx, [s2]
        mov ax, 0001h
        int 31h
        REGS 'free_freed'
        xor bx, bx
        mov ax, 0001h
        int 31h
        REGS 'free_null'
        mov bx, 0053h
        mov ax, 0001h
        int 31h
        REGS 'free_gdt'
        mov bx, 0FFFFh
        mov ax, 0001h
        int 31h
        REGS 'free_ffff'
        call flush
        ; segment to descriptor
        mov bx, 0B800h
        mov ax, 0002h
        int 31h
        REGS 'seg_b800'
        mov [s6], ax
        DESC 'seg_b800', [s6]
        mov bx, 0B800h
        mov ax, 0002h
        int 31h
        REGS 'seg_b800_again'
        mov bx, 0B000h
        mov ax, 0002h
        int 31h
        REGS 'seg_b000'
        mov bx, 0040h
        mov ax, 0002h
        int 31h
        REGS 'seg_0040'
        mov bx, 0000h
        mov ax, 0002h
        int 31h
        REGS 'seg_0000'
        mov bx, [psp]
        mov ax, 0002h
        int 31h
        REGS 'seg_psp'
        mov bx, 0A000h
        mov ax, 0002h
        int 31h
        REGS 'seg_a000'
        mov bx, [s6]
        mov ax, 0001h
        int 31h
        REGS 'free_seg'
        mov bx, 0B800h
        mov ax, 0002h
        int 31h
        REGS 'seg_b800_after_free'
        ; alias
        mov bx, cs
        mov ax, 000Ah
        int 31h
        REGS 'alias_cs'
        mov [s7], ax
        DESC 'alias_cs', [s7]
        mov bx, [s1]
        mov ax, 000Ah
        int 31h
        REGS 'alias_s1'
        mov bx, 0053h
        mov ax, 000Ah
        int 31h
        REGS 'alias_gdt'
        mov bx, [s2]
        mov ax, 000Ah
        int 31h
        REGS 'alias_free'
        ; base/limit/rights edge cases on s1
        mov bx, [s1]
        mov cx, 12h
        mov dx, 3456h
        mov ax, 0007h
        int 31h
        REGS 'setbase_123456'
        DESC 'setbase_123456', [s1]
        mov bx, [s1]
        mov cx, 1234h
        mov dx, 5678h
        mov ax, 0007h
        int 31h
        REGS 'setbase_12345678'
        DESC 'setbase_12345678', [s1]
        mov bx, [s1]
        mov ax, 0006h
        int 31h
        REGS 'getbase_12345678'
        mov bx, [s1]
        mov cx, 1
        mov dx, 0
        mov ax, 0008h
        int 31h
        REGS 'setlimit_10000'
        DESC 'setlimit_10000', [s1]
        mov bx, [s1]
        mov cx, 0Fh
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
        REGS 'setlimit_fffff'
        DESC 'setlimit_fffff', [s1]
        mov bx, [s1]
        mov cx, 10h
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
        REGS 'setlimit_1fffff'
        DESC 'setlimit_1fffff', [s1]
        mov bx, [s1]
        mov cx, 0
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
        mov bx, [s1]
        mov cx, 0
        mov dx, 0
        mov ax, 0007h
        int 31h
        mov bx, [s1]
        mov cx, 00FAh
        mov ax, 0009h
        int 31h
        REGS 'rights_fa'
        DESC 'rights_fa', [s1]
        mov bx, [s1]
        mov cx, 009Ah
        mov ax, 0009h
        int 31h
        REGS 'rights_9a'
        mov bx, [s1]
        mov cx, 0072h
        mov ax, 0009h
        int 31h
        REGS 'rights_72'
        mov bx, [s1]
        mov cx, 0F0h
        mov ax, 0009h
        int 31h
        REGS 'rights_f0'
        mov bx, [s1]
        mov cx, 0F4h
        mov ax, 0009h
        int 31h
        REGS 'rights_f4'
        mov bx, [s1]
        mov cx, 40F2h
        mov ax, 0009h
        int 31h
        REGS 'rights_40f2'
        DESC 'rights_40f2', [s1]
        mov bx, [s1]
        mov cx, 80F2h
        mov ax, 0009h
        int 31h
        REGS 'rights_80f2'
        DESC 'rights_80f2', [s1]
        mov bx, [s1]
        mov cx, 0072h
        mov ax, 0009h
        int 31h
        DESC 'rights_72_after', [s1]
        mov bx, [s1]
        mov cx, 0F2h
        mov ax, 0009h
        int 31h
        mov bx, [s2]
        mov cx, 0F2h
        mov ax, 0009h
        int 31h
        REGS 'rights_on_free'
        mov bx, cs
        mov ax, 000Bh
        mov di, dbuf
        int 31h
        REGS 'getdesc_cs'
        OBSD 'getdesc_cs_lo', [dbuf+2], [dbuf]
        OBSD 'getdesc_cs_hi', [dbuf+6], [dbuf+4]
        mov bx, 0053h
        mov di, dbuf
        mov ax, 000Bh
        int 31h
        REGS 'getdesc_gdt'
        mov bx, [s2]
        mov di, dbuf
        mov ax, 000Bh
        int 31h
        REGS 'getdesc_free'
        OBSD 'getdesc_free_lo', [dbuf+2], [dbuf]
        OBSD 'getdesc_free_hi', [dbuf+6], [dbuf+4]
        ; 000Ch with various descriptors
        mov word [dbuf], 0FFFFh
        mov word [dbuf+2], 0
        mov word [dbuf+4], 9A01h
        mov word [dbuf+6], 0
        mov bx, [s1]
        mov di, dbuf
        mov ax, 000Ch
        int 31h
        REGS 'setdesc_dpl0'
        DESC 'setdesc_dpl0', [s1]
        mov word [dbuf+4], 0F201h
        mov word [dbuf+6], 0FFh
        mov bx, [s1]
        mov di, dbuf
        mov ax, 000Ch
        int 31h
        REGS 'setdesc_hi'
        DESC 'setdesc_hi', [s1]
        mov word [dbuf+4], 7201h
        mov word [dbuf+6], 0
        mov bx, [s1]
        mov di, dbuf
        mov ax, 000Ch
        int 31h
        REGS 'setdesc_notpresent'
        DESC 'setdesc_notpresent', [s1]
        mov word [dbuf+4], 0E401h
        mov bx, [s1]
        mov di, dbuf
        mov ax, 000Ch
        int 31h
        REGS 'setdesc_gate'
        mov bx, [s2]
        mov word [dbuf+4], 0F201h
        mov di, dbuf
        mov ax, 000Ch
        int 31h
        REGS 'setdesc_free'
        DESC 'setdesc_free', [s2]
        ; 0007h on a GDT selector, on the free one
        mov bx, 0053h
        xor cx, cx
        xor dx, dx
        mov ax, 0007h
        int 31h
        REGS 'setbase_gdt'
        mov bx, 0004h
        mov ax, 0006h
        int 31h
        REGS 'getbase_ldt0'
        call flush

; ---------------- memory ----------------
        mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free0', [mbuf+2], [mbuf]
        xor bx, bx
        mov cx, 1
        mov ax, 0501h
        int 31h
        REGS 'm501_1'
        mov [h1], di
        mov [h1+2], si
        xor bx, bx
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        REGS 'm501_1000'
        mov [h2], di
        mov [h2+2], si
        mov bx, 1
        xor cx, cx
        mov ax, 0501h
        int 31h
        REGS 'm501_10000'
        mov [h3], di
        mov [h3+2], si
        xor bx, bx
        mov cx, 10h
        mov ax, 0501h
        int 31h
        REGS 'm501_10'
        mov [h4], di
        mov [h4+2], si
        mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free1', [mbuf+2], [mbuf]
        mov di, [h2]
        mov si, [h2+2]
        mov ax, 0502h
        int 31h
        REGS 'm502_1000'
        xor bx, bx
        mov cx, 800h
        mov ax, 0501h
        int 31h
        REGS 'm501_800_after_free'
        xor bx, bx
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        REGS 'm501_1000_after_free'
        mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free2', [mbuf+2], [mbuf]
        xor bx, bx
        xor cx, cx
        mov ax, 0501h
        int 31h
        REGS 'm501_0'
        mov bx, 0FFFFh
        mov cx, 0
        mov ax, 0501h
        int 31h
        REGS 'm501_huge'
        mov si, 1234h
        mov di, 5678h
        mov ax, 0502h
        int 31h
        REGS 'm502_bad'
        ; the largest block, allocated and freed
        mov di, mbuf
        mov ax, 0500h
        int 31h
        mov bx, [mbuf+2]
        mov cx, [mbuf]
        mov ax, 0501h
        int 31h
        REGS 'm501_largest'
        mov [h5], di
        mov [h5+2], si
        jc .nolg
        mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free_after_largest', [mbuf+2], [mbuf]
        mov di, [h5]
        mov si, [h5+2]
        mov ax, 0502h
        int 31h
.nolg:  mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free3', [mbuf+2], [mbuf]
        call flush
        ; INT 21h AH=48h/49h/4Ah in protected mode
        mov bx, 1
        mov ah, 48h
        int 21h
        REGS 'pm48_1'
        mov [m1], ax
        DESC 'pm48_1', [m1]
        mov bx, 1000h
        mov ah, 48h
        int 21h
        REGS 'pm48_1000'
        mov [m2], ax
        DESC 'pm48_1000', [m2]
        mov bx, 1001h
        mov ah, 48h
        int 21h
        REGS 'pm48_1001'
        mov [m3], ax
        DESC 'pm48_1001_0', [m3]
        mov bx, [m3]
        add bx, 8
        DESC 'pm48_1001_1', bx
        mov bx, 2800h
        mov ah, 48h
        int 21h
        REGS 'pm48_2800'
        mov [m4], ax
        DESC 'pm48_2800_0', [m4]
        mov bx, [m4]
        add bx, 8
        DESC 'pm48_2800_1', bx
        mov bx, [m4]
        add bx, 16
        DESC 'pm48_2800_2', bx
        mov bx, [m4]
        add bx, 24
        DESC 'pm48_2800_3', bx
        mov bx, 0FFFFh
        mov ah, 48h
        int 21h
        REGS 'pm48_ffff'
        mov bx, 0
        mov ah, 48h
        int 21h
        REGS 'pm48_0'
        mov es, [m3]
        mov ah, 49h
        int 21h
        REGS 'pm49_1001'
        push ds
        pop es
        DESC 'pm49_1001_after', [m3]
        mov es, [s4]
        mov ah, 49h
        int 21h
        REGS 'pm49_not48'
        push ds
        pop es
        mov es, [sel_es]
        mov ah, 49h
        int 21h
        REGS 'pm49_psp_sel'
        push ds
        pop es
        mov bx, 1
        mov ah, 48h
        int 21h
        REGS 'pm48_1_after_free'
        ; DOS memory functions
        mov bx, 1
        mov ax, 0100h
        int 31h
        REGS 'd0100_1'
        mov bx, 0FFFFh
        mov ax, 0100h
        int 31h
        REGS 'd0100_ffff'
        mov dx, [m1]
        mov ax, 0101h
        int 31h
        REGS 'd0101'
        mov dx, [m1]
        mov bx, 1
        mov ax, 0102h
        int 31h
        REGS 'd0102'
        mov di, mbuf
        mov ax, 0500h
        int 31h
        OBSD 'free4', [mbuf+2], [mbuf]
        call flush

; ---------------- interrupts ----------------
        mov bl, 60h
        mov ax, 0200h
        int 31h
        REGS 'rmvec_60'
        mov [t1], cx
        mov [t2], dx
        mov bl, 60h
        mov cx, 1234h
        mov dx, 5678h
        mov ax, 0201h
        int 31h
        REGS 'set_rmvec_60'
        mov bx, 0
        mov ax, 0002h
        int 31h
        mov es, ax
        OBS 'ivt60_off', [es:180h]
        OBS 'ivt60_seg', [es:182h]
        push ds
        pop es
        mov bl, 60h
        mov cx, [t1]
        mov dx, [t2]
        mov ax, 0201h
        int 31h
        mov bl, 60h
        mov ax, 0204h
        int 31h
        REGS 'pmvec_60'
        mov [t1], cx
        mov [t2], dx
        mov bl, 60h
        mov cx, cs
        mov dx, pm_iret
        mov ax, 0205h
        int 31h
        REGS 'set_pmvec_60'
        mov bl, 60h
        mov ax, 0204h
        int 31h
        REGS 'pmvec_60_after'
        ; does the real-mode vector change with it?
        mov bx, 0
        mov ax, 0002h
        int 31h
        mov es, ax
        OBS 'ivt60_off_after_pmset', [es:180h]
        push ds
        pop es
        mov bl, 60h
        mov cx, [t1]
        mov dx, [t2]
        mov ax, 0205h
        int 31h
        mov bl, 60h
        mov cx, 1234h
        mov dx, 5678h
        mov ax, 0205h
        int 31h
        REGS 'set_pmvec_bad_sel'
        mov bl, 60h
        mov ax, 0204h
        int 31h
        REGS 'pmvec_60_after_bad'
        mov bl, 60h
        mov cx, [t1]
        mov dx, [t2]
        mov ax, 0205h
        int 31h
        mov bl, 0Dh
        mov ax, 0202h
        int 31h
        mov [t1], cx
        mov [t2], dx
        mov bl, 0Dh
        mov cx, cs
        mov dx, pm_iret
        mov ax, 0203h
        int 31h
        REGS 'set_exc_0d'
        mov bl, 0Dh
        mov ax, 0202h
        int 31h
        REGS 'exc_0d_after'
        mov bl, 0Dh
        mov cx, [t1]
        mov dx, [t2]
        mov ax, 0203h
        int 31h
        mov bl, 20h
        mov cx, cs
        mov dx, pm_iret
        mov ax, 0203h
        int 31h
        REGS 'set_exc_20'
        call flush

; ---------------- real-mode calls ----------------
        call rmclear
        mov word [rm_ax], 3000h
        mov bx, 21h
        xor cx, cx
        mov di, rmregs
        mov ax, 0300h
        int 31h
        REGS 'rm300_int21_30'
        OBS 'rm300_ax', [rm_ax]
        OBS 'rm300_fl', [rm_fl]
        OBS 'rm300_ss', [rm_ss]
        OBS 'rm300_sp', [rm_sp]
        OBS 'rm300_cs', [rm_cs]
        OBS 'rm300_ip', [rm_ip]
        ; 0301h: call a far procedure of ours in real mode that
        ; reports SS:SP and the words on its stack
        call rmclear
        mov ax, [psp]
        mov [rm_cs], ax
        mov word [rm_ip], rm_proc
        mov [rm_ds], ax
        push word 0BEEFh
        push word 0CAFEh
        mov cx, 2
        xor bx, bx
        mov di, rmregs
        mov ax, 0301h
        int 31h
        REGS 'rm301'
        add sp, 4
        OBS 'rm301_ss', [rmss]
        OBS 'rm301_sp', [rmsp]
        OBS 'rm301_w0', [rmw0]
        OBS 'rm301_w1', [rmw1]
        OBS 'rm301_w2', [rmw2]
        OBS 'rm301_w3', [rmw3]
        OBS 'rm301_fl', [rmfl]
        OBS 'rm301_out_ax', [rm_ax]
        OBS 'rm301_out_ss', [rm_ss]
        OBS 'rm301_out_sp', [rm_sp]
        ; with our own real-mode stack
        call rmclear
        mov ax, [psp]
        mov [rm_cs], ax
        mov [rm_ds], ax
        mov [rm_ss], ax
        mov word [rm_sp], rmstack_top
        mov word [rm_ip], rm_proc
        xor cx, cx
        xor bx, bx
        mov di, rmregs
        mov ax, 0301h
        int 31h
        REGS 'rm301_ownstack'
        OBS 'rm301o_ss', [rmss]
        OBS 'rm301o_sp', [rmsp]
        OBS 'rm301o_out_sp', [rm_sp]
        ; 0302h: an IRET procedure
        call rmclear
        mov ax, [psp]
        mov [rm_cs], ax
        mov [rm_ds], ax
        mov word [rm_ip], rm_iproc
        mov word [rm_fl], 0002h
        xor cx, cx
        xor bx, bx
        mov di, rmregs
        mov ax, 0302h
        int 31h
        REGS 'rm302'
        OBS 'rm302_fl', [rmfl]
        OBS 'rm302_out_fl', [rm_fl]
        OBS 'rm302_w0', [rmw0]
        OBS 'rm302_w1', [rmw1]
        OBS 'rm302_w2', [rmw2]
        ; flags given in the structure: what the real-mode code sees
        call rmclear
        mov ax, [psp]
        mov [rm_cs], ax
        mov [rm_ds], ax
        mov word [rm_ip], rm_iproc
        mov word [rm_fl], 0ED7h
        xor cx, cx
        xor bx, bx
        mov di, rmregs
        mov ax, 0302h
        int 31h
        pushf
        pop ax
        cld
        OBS 'rm302_given_ed7_pmflags', ax
        OBS 'rm302_given_ed7_seen', [rmfl]
        OBS 'rm302_given_ed7_out', [rm_fl]
        call flush
        ; callbacks
        push ds
        pop es
        push ds
        mov si, cb_proc
        mov ax, cs
        mov ds, ax
        mov di, cbregs
        mov ax, 0303h
        int 31h
        pop ds
        REGS 'cb303'
        mov [cb], dx
        mov [cb+2], cx
        jc near .nocb
        ; call it from real mode through 0301h
        call rmclear
        mov ax, [cb+2]
        mov [rm_cs], ax
        mov ax, [cb]
        mov [rm_ip], ax
        mov word [rm_ax], 0AAAAh
        mov word [rm_bx], 0BBBBh
        mov ax, [psp]
        mov [rm_ds], ax
        mov [rm_es], ax
        push word 1111h
        push word 2222h
        mov cx, 2
        xor bx, bx
        mov di, rmregs
        mov ax, 0301h
        int 31h
        REGS 'cb_call'
        add sp, 4
        OBS 'cb_in_ds', [cbin_ds]
        OBS 'cb_in_si', [cbin_si]
        OBS 'cb_in_es', [cbin_es]
        OBS 'cb_in_di', [cbin_di]
        OBS 'cb_in_ss', [cbin_ss]
        OBS 'cb_in_sp', [cbin_sp]
        OBS 'cb_in_fl', [cbin_fl]
        OBS 'cb_in_ax', [cbin_ax]
        OBS 'cb_in_w0', [cbin_w0]
        OBS 'cb_in_w1', [cbin_w1]
        OBS 'cb_in_w2', [cbin_w2]
        OBS 'cb_in_w3', [cbin_w3]
        OBS 'cb_struct_ax', [cbin_sax]
        OBS 'cb_struct_ss', [cbin_sss]
        OBS 'cb_struct_sp', [cbin_ssp]
        OBS 'cb_struct_cs', [cbin_scs]
        OBS 'cb_struct_ip', [cbin_sip]
        OBS 'cb_out_ax', [rm_ax]
        OBS 'cb_out_bx', [rm_bx]
        mov cx, [cb+2]
        mov dx, [cb]
        mov ax, 0304h
        int 31h
        REGS 'cb304'
        mov cx, [cb+2]
        mov dx, [cb]
        mov ax, 0304h
        int 31h
        REGS 'cb304_again'
        ; how many callbacks: allocate until it fails (at most 400h)
        xor bp, bp
.cbl:   push ds
        mov si, cb_proc
        mov ax, cs
        mov ds, ax
        mov di, cbregs
        mov ax, 0303h
        int 31h
        pop ds
        jc .cbd
        cmp bp, 2
        jae .cbn
        REGS 'cb303_n'
.cbn:   mov bx, bp
        shl bx, 2
        mov [cbs+bx], dx
        mov [cbs+bx+2], cx
        inc bp
        cmp bp, 400h
        jb .cbl
.cbd:   REGS 'cb303_fail'
        OBS 'cb_count', bp
.cbf:   or bp, bp
        jz .nocb
        dec bp
        mov bx, bp
        shl bx, 2
        mov dx, [cbs+bx]
        mov cx, [cbs+bx+2]
        mov ax, 0304h
        int 31h
        jmp .cbf
.nocb:  call flush

; ---------------- the remaining functions ----------------
        mov si, sweep
.sw:    cmp word [si], 0FFFFh
        je .swd
        mov [t1], si
        mov ax, [si]
        mov bx, [si+2]
        mov cx, [si+4]
        mov dx, [si+6]
        mov di, [si+10]
        mov si, [si+8]
        push ds
        pop es
        int 31h
        push si
        mov si, [t1]
        mov si, [si]
        mov [t2], si
        pop si
        call regs_sw
        call flush
        mov si, [t1]
        add si, 12
        jmp .sw
.swd:   NOTE 'DX2 end'
        call flush
        mov ax, 4C00h
        int 21h

; regs_sw: "OBS f_XXXX=cf:.." for function [t2]
regs_sw:
        pushf
        push si
        push ax
        mov si, s_obs
        call puts
        mov si, s_f
        call puts
        mov ax, [t2]
        call puthex
        pop ax
        pop si
        popf
        mov byte [regs_hdr], 0
        call regs_i
        db '', 0
        mov byte [regs_hdr], 1
        ret

pm_iret:
        iret

; cb_proc: the callback's protected-mode procedure (DS:SI = real-mode
; SS:SP, ES:DI = the register structure): note what it was given, return
; to real mode with a far return popped from the real-mode stack
cb_proc:
        push ax
        mov [es:cbin_ds], ds
        mov [es:cbin_si], si
        mov [es:cbin_es], es
        mov [es:cbin_di], di
        mov [es:cbin_ss], ss
        mov ax, sp
        add ax, 2
        mov [es:cbin_sp], ax
        pushf
        pop word [es:cbin_fl]
        mov ax, [es:di+1Ch]
        mov [es:cbin_sax], ax
        mov ax, [es:di+30h]
        mov [es:cbin_sss], ax
        mov ax, [es:di+2Eh]
        mov [es:cbin_ssp], ax
        mov ax, [es:di+2Ch]
        mov [es:cbin_scs], ax
        mov ax, [es:di+2Ah]
        mov [es:cbin_sip], ax
        mov ax, [si]
        mov [es:cbin_w0], ax
        mov ax, [si+2]
        mov [es:cbin_w1], ax
        mov ax, [si+4]
        mov [es:cbin_w2], ax
        mov ax, [si+6]
        mov [es:cbin_w3], ax
        ; simulate RETF: CS:IP from the real-mode stack
        mov ax, [si]
        mov [es:di+2Ah], ax
        mov ax, [si+2]
        mov [es:di+2Ch], ax
        add word [es:di+2Eh], 4
        mov word [es:di+1Ch], 0C0DEh
        pop ax
        iret

; ---------------- real-mode procedures (run with CS = our PSP) ----------
rm_proc:
        push bp
        mov bp, sp
        push ax
        mov ax, ss
        mov [cs:rmss], ax
        lea ax, [bp+2]
        mov [cs:rmsp], ax
        mov ax, [bp+2]
        mov [cs:rmw0], ax
        mov ax, [bp+4]
        mov [cs:rmw1], ax
        mov ax, [bp+6]
        mov [cs:rmw2], ax
        mov ax, [bp+8]
        mov [cs:rmw3], ax
        pushf
        pop ax
        mov [cs:rmfl], ax
        pop ax
        pop bp
        mov ax, 4321h
        retf

rm_iproc:
        push bp
        mov bp, sp
        push ax
        mov ax, [bp+2]
        mov [cs:rmw0], ax
        mov ax, [bp+4]
        mov [cs:rmw1], ax
        mov ax, [bp+6]
        mov [cs:rmw2], ax
        pushf
        pop ax
        mov [cs:rmfl], ax
        pop ax
        pop bp
        iret

desc_i: push bp
        mov bp, sp
        push si
        push ax
        push es
        mov si, s_obs
        call puts
        mov si, s_desc
        call puts
        mov si, [bp+2]
        call cputs
        mov [bp+2], si
        mov al, '='
        call putc
        mov ax, bx
        call puthex
        mov al, ' '
        call putc
        and bx, 0FFF8h
        mov es, [ldtsel]
        mov ax, [es:bx+6]
        call puthex
        mov ax, [es:bx+4]
        call puthex
        mov al, ' '
        call putc
        mov ax, [es:bx+2]
        call puthex
        mov ax, [es:bx]
        call puthex
        call crlf
        pop es
        pop ax
        pop si
        pop bp
        ret

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

%include "dxlib.inc"

; sweep: AX, BX, CX, DX, SI, DI (ES = DS)
sweep:  dw 0003h, 0, 0, 0, 0, 0
        dw 0004h, 0, 0, 0, 0, 0
        dw 0005h, 0, 0, 0, 0, 0
        dw 000Dh, 0, 0, 0, 0, 0
        dw 000Eh, 0, 0, 0, 0, swbuf
        dw 000Fh, 0, 0, 0, 0, swbuf
        dw 0102h, 0, 0, 0, 0, 0
        dw 0305h, 0, 0, 0, 0, 0
        dw 0306h, 0, 0, 0, 0, 0
        dw 0400h, 0, 0, 0, 0, 0
        dw 0401h, 0, 0, 0, 0, 0
        dw 0504h, 0, 0, 0, 0, 0
        dw 0600h, 0, 0, 1000h, 0, 0
        dw 0601h, 0, 0, 1000h, 0, 0
        dw 0602h, 0, 0, 1000h, 0, 0
        dw 0603h, 0, 0, 1000h, 0, 0
        dw 0604h, 0, 0, 0, 0, 0
        dw 0700h, 0, 0, 0, 0, 0
        dw 0701h, 0, 0, 0, 0, 0
        dw 0702h, 0, 0, 1000h, 0, 0
        dw 0703h, 0, 0, 1000h, 0, 0
        dw 0800h, 000Ah, 0, 0, 1, 0
        dw 0800h, 0010h, 0, 0, 1, 0
        dw 0800h, 0FEh, 0, 0, 1, 0
        dw 0801h, 0, 0, 0, 0, 0
        dw 0900h, 0, 0, 0, 0, 0
        dw 0901h, 0, 0, 0, 0, 0
        dw 0902h, 0, 0, 0, 0, 0
        dw 0A00h, 0, 0, 0, msdos, 0
        dw 0B00h, 0, 0, 0, 0, 0
        dw 0B01h, 0, 0, 0, 0, 0
        dw 0B02h, 0, 0, 0, 0, 0
        dw 0B03h, 0, 0, 0, 0, 0
        dw 0C00h, 0, 0, 0, 0, swbuf
        dw 0C01h, 0, 0, 0, 0, 0
        dw 0D00h, 0, 0, 0, 0, 0
        dw 1234h, 0, 0, 0, 0, 0
        dw 0FFFFh

msdos           db 'MS-DOS', 0
s_desc          db 'desc_', 0
s_f             db 'f_', 0
rmss            dw 0
rmsp            dw 0
rmw0            dw 0
rmw1            dw 0
rmw2            dw 0
rmw3            dw 0
rmfl            dw 0

        section .bss
psp     resw 1
sel_es  resw 1
pment   resd 1
vend    resd 1
ldtsel  resw 1
s1      resw 1
s2      resw 1
s3      resw 1
s4      resw 1
s5      resw 1
s6      resw 1
s7      resw 1
t1      resw 1
t2      resw 1
h1      resd 1
h2      resd 1
h3      resd 1
h4      resd 1
h5      resd 1
m1      resw 1
m2      resw 1
m3      resw 1
m4      resw 1
cb      resd 1
cbin_ds resw 1
cbin_si resw 1
cbin_es resw 1
cbin_di resw 1
cbin_ss resw 1
cbin_sp resw 1
cbin_fl resw 1
cbin_ax resw 1
cbin_w0 resw 1
cbin_w1 resw 1
cbin_w2 resw 1
cbin_w3 resw 1
cbin_sax resw 1
cbin_sss resw 1
cbin_ssp resw 1
cbin_scs resw 1
cbin_sip resw 1
dbuf    resb 8
mbuf    resb 48
swbuf   resb 64
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
cbregs  resb 50
cbs     resd 400h
logbuf  resb LOGSIZE
        resb 512
rmstack_top:
        resb 1024
stack_top:
