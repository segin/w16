; init.inc - real mode: start-up, XMS, starting KERNEL, exit, the
; real-mode INT 2Fh (DPMI 1687h) and INT 24h hooks, the mode-switch
; entry point a client calls.

start:  ; DOS: DS = ES = PSP, SS:SP from the header
        mov ax, cs
        mov ds, ax
        mov [imgseg], ax
        mov [psp_seg], es
        cld
        call cpu_detect
        jnc .cpuok
        mov dx, msg_cpu
        jmp fail_msg
.cpuok: smsw ax
        test al, 1
        jz .rmok
        mov dx, msg_v86
        jmp fail_msg
.rmok:  ; the image's linear address
        mov ax, [imgseg]
        mov dx, ax
        shl ax, 4
        shr dx, 12
        mov [imglin], ax
        mov [imglin+2], dx
        call patch_segs
        call xms_init
        jnc .memok
        mov dx, msg_nomem
        jmp fail_msg
.memok: call tables_init
        call hooks_install
        ; keep only what stays resident: PSP + image + its data
        mov es, [psp_seg]
        mov bx, (img_file_len + bss_len + 31) / 16 + 10h
        mov ah, 4Ah
        int 21h
        push ds
        pop es
        call run_kernel
        mov [exit_code], al
        mov dx, [pending_msg]
        or dx, dx
        jz .nomsg
        mov ah, 09h
        int 21h
.nomsg:
        call cleanup
        mov al, [exit_code]
        mov ah, 4Ch
        int 21h

; fail_msg: DX = '$' message; exit 0FFh (Microsoft's DOSX exits with
; 0FFh after its fatal messages: DX5 run directly, errorlevel 255)
fail_msg:
        mov ah, 09h
        int 21h
        mov ax, 4CFFh
        int 21h

; ---------------------------------------------------------------------------
; cpu_detect: [cpu_type] = 2 (80286), 3 (80386) or 4 (80486 or later: the AC
; flag can be changed), [is386] set on a 386 or later. CF on an 8086.
cpu_detect:
        pushf
        pushf
        pop ax
        and ax, 0FFFh           ; 8086/8088: bits 12-15 stay set
        push ax
        popf
        pushf
        pop ax
        and ax, 0F000h
        cmp ax, 0F000h
        je .old
        pushf                   ; 80286 in real mode: bits 12-15 stay clear
        pop ax
        or ax, 0F000h
        push ax
        popf
        pushf
        pop ax
        test ax, 0F000h
        jnz .386
        mov byte [cpu_type], 2
        jmp .ok
.386:   mov byte [cpu_type], 3
        mov byte [is386], 1
        cpu 386
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 40000h         ; AC
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        xor eax, ecx
        test eax, 40000h
        cpu 286
        jz .ok
        mov byte [cpu_type], 4
.ok:    popf
        clc
        ret
.old:   popf
        stc
        ret

; patch_segs: write the image segment into the far jumps that need it
patch_segs:
        mov si, patch_tab
        mov ax, [imgseg]
.l:     mov bx, [si]
        or bx, bx
        jz .d
        mov [bx], ax
        add si, 2
        jmp .l
.d:     ret

; ---------------------------------------------------------------------------
; xms_init: take all the extended memory XMS will give (Microsoft's DOSX
; leaves XMS with no free memory: DX1 xms_08_free, pm_xms_08), lock it
; and enable A20. CF if there is no XMS or too little memory.
; Source: XMS 3.0 functions 08h (query free), 09h (allocate), 0Ch (lock),
; 05h (local enable A20).
xms_init:
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .fail
        mov ax, 4310h
        int 2Fh
        mov [xms], bx
        mov [xms+2], es
        push ds
        pop es
        mov ah, 08h
        xor bx, bx
        call far [xms]
        ; at least the LDT (64 KB) and 128 KB for the client
        cmp ax, 192
        jb .fail
        mov [emb_kb], ax
        mov dx, ax
        mov ah, 09h
        call far [xms]
        cmp ax, 1
        jne .fail
        mov [xms_h], dx
        mov ah, 0Ch
        call far [xms]
        cmp ax, 1
        jne .freefail
        mov [emb_lin], bx
        mov [emb_lin+2], dx
        mov ah, 05h
        call far [xms]
        cmp ax, 1
        jne .unlockfail
        mov byte [xms_ok], 1
        ; the end of the block
        mov ax, [emb_kb]
        mov dx, ax
        shl ax, 10
        shr dx, 6
        add ax, [emb_lin]
        adc dx, [emb_lin+2]
        mov [emb_end], ax
        mov [emb_end+2], dx
        ; the LDT first, then the client's memory
        mov ax, [emb_lin]
        mov dx, [emb_lin+2]
        mov [ldt_lin], ax
        mov [ldt_lin+2], dx
        inc dx
        mov [pool_lin], ax
        mov [pool_lin+2], dx
        clc
        ret
.unlockfail:
        mov dx, [xms_h]
        mov ah, 0Dh
        call far [xms]
.freefail:
        mov dx, [xms_h]
        mov ah, 0Ah
        call far [xms]
.fail:  push ds
        pop es
        stc
        ret

; ---------------------------------------------------------------------------
; tables_init: the GDT, the TSS, the IDT pointers
tables_init:
        ; GDT
        mov di, gdt
        mov cx, GDT_ENTRIES * 4
        xor ax, ax
        rep stosw
        mov bx, G_GDT
        mov ax, gdt
        call base_img
        mov cx, GDT_ENTRIES * 8 - 1
        mov ax, 0F2h
        call gdt_set
        mov bx, G_IDT
        mov ax, idt
        call base_img
        mov cx, 7FFh
        mov ax, 0F2h
        call gdt_set
        mov bx, G_ZERO
        xor si, si
        xor dx, dx
        mov cx, 0FFFFh
        mov ax, 0F2h
        call gdt_set
        mov bx, G_PSP
        mov ax, [psp_seg]
        call base_seg
        mov cx, 0FFFFh
        mov ax, 0F2h
        call gdt_set
        mov es, [psp_seg]
        mov ax, [es:2Ch]
        push ds
        pop es
        mov bx, G_ENV
        call base_seg
        mov cx, 7FFFh
        mov ax, 0F2h
        call gdt_set
        mov bx, G_BIOS
        xor si, si
        mov dx, 0Fh
        mov cx, 0FFFFh
        mov ax, 0FAh
        call gdt_set
        mov bx, G_LDT
        mov si, [ldt_lin]
        mov dx, [ldt_lin+2]
        mov cx, LDT_LIMIT
        mov ax, 0E2h
        call gdt_set
        mov bx, G_LDTA
        mov ax, 0F2h
        call gdt_set
        mov bx, G_B40
        mov si, 400h
        xor dx, dx
        mov cx, 0FFFFh
        mov ax, 0F2h
        call gdt_set
        mov bx, G_FLAT
        xor si, si
        mov ax, 0F2h
        call gdt_set
        cmp byte [is386], 0
        je .noflat
        mov byte [gdt + G_FLAT + 6], 8Fh        ; 4 GB, page granular
.noflat:
        xor ax, ax
        call base_img
        mov cx, 0FFFFh
        mov bx, G_HD3
        mov ax, 0F2h
        call gdt_set
        mov bx, G_HC3
        mov ax, 0FAh
        call gdt_set
        mov bx, G_RC3
        call gdt_set
        mov bx, G_HD0
        mov ax, 92h
        call gdt_set
        mov bx, G_HC0
        mov ax, 9Ah
        call gdt_set
        mov bx, G_RC0
        call gdt_set
        mov bx, G_HC0B
        call gdt_set
        mov bx, G_HDS
        mov cx, 0FFh
        mov ax, 0F2h
        call gdt_set
        mov bx, G_SCR0
        mov cx, 0FFFFh
        mov ax, 92h
        call gdt_set
        mov bx, G_SCR1
        call gdt_set
        mov bx, G_SCR2
        call gdt_set
        mov bx, G_SCR3
        call gdt_set
        mov ax, tss
        call base_img
        mov cx, 2Bh
        mov bx, G_TSS
        mov ax, 0E1h            ; available 286 TSS, DPL 3
        call gdt_set
        mov bx, G_TSSA
        mov ax, 0F2h
        call gdt_set
        ; call gates (286, DPL 3, no parameters)
        mov word [gdt + G_GATE], gate_entry
        mov word [gdt + G_GATE + 2], G_HC0
        mov word [gdt + G_GATE + 4], 0E400h
        mov word [gdt + G_GATER], ret3_entry
        mov word [gdt + G_GATER + 2], G_HC0
        mov word [gdt + G_GATER + 4], 0E400h
        ; descriptor table registers
        mov word [gdtr], GDT_ENTRIES * 8 - 1
        mov ax, gdt
        call base_img
        mov [gdtr+2], si
        mov [gdtr+4], dx
        mov word [idtr], 7FFh
        mov ax, idt
        call base_img
        mov [idtr+2], si
        mov [idtr+4], dx
        mov word [rmidtr], 3FFh
        ; TSS: ring 0 stack
        mov di, tss
        mov cx, 22
        xor ax, ax
        rep stosw
        mov word [tss+2], hs_stack_top
        mov word [tss+4], G_HD0
        mov word [hs_top], hs_stack_top
        mov word [h3_top], h3_stack_top
        mov word [tb_top], tbuf + TB_SIZE
        ; real-mode callbacks: their canonical address (see cb_entry)
        mov ax, cb_entry
        mov dx, ax
        and ax, 0Fh
        mov [cb_off], ax
        shr dx, 4
        add dx, [imgseg]
        mov [cb_seg], dx
        ; as many as real-mode addresses allow: the segment must not
        ; go below 0, the offset not beyond 0FFFFh
        inc dx
        cmp dx, 1000h
        jbe .cbm
        mov dx, 1000h
.cbm:   mov [cb_max], dx
        ret

; base_img: SI:DX... DX:SI = the image's linear address + AX
base_img:
        mov si, [imglin]
        mov dx, [imglin+2]
        add si, ax
        adc dx, 0
        ret
; base_seg: DX:SI = AX * 16
base_seg:
        mov si, ax
        mov dx, ax
        shl si, 4
        shr dx, 12
        ret

; gdt_set: BX = selector, DX:SI = base, CX = limit, AL = access
gdt_set:
        mov [gdt+bx], cx
        mov [gdt+bx+2], si
        mov [gdt+bx+4], dl
        mov [gdt+bx+5], al
        mov byte [gdt+bx+6], 0
        mov [gdt+bx+7], dh
        cmp byte [is386], 0
        jne .d
        mov byte [gdt+bx+7], 0
.d:     ret

; ---------------------------------------------------------------------------
; hooks_install: real-mode INT 2Fh (DPMI) and INT 24h (DX1: Microsoft's
; DOSX hooks 22h, 24h and 2Fh; 22h is the address DOS gives the program
; it starts, which points back into DOSX by itself)
hooks_install:
        push es
        xor ax, ax
        mov es, ax
        cli
        mov ax, [es:2Fh*4]
        mov [old2f], ax
        mov ax, [es:2Fh*4+2]
        mov [old2f+2], ax
        mov ax, [es:24h*4]
        mov [old24], ax
        mov ax, [es:24h*4+2]
        mov [old24+2], ax
        mov word [es:2Fh*4], int2f_rm
        mov ax, [imgseg]
        mov [es:2Fh*4+2], ax
        mov word [es:24h*4], int24_rm
        mov [es:24h*4+2], ax
        sti
        pop es
        ret

hooks_remove:
        push es
        xor ax, ax
        mov es, ax
        cli
        mov ax, [old2f]
        mov [es:2Fh*4], ax
        mov ax, [old2f+2]
        mov [es:2Fh*4+2], ax
        mov ax, [old24]
        mov [es:24h*4], ax
        mov ax, [old24+2]
        mov [es:24h*4+2], ax
        sti
        pop es
        ret

; INT 2Fh in real mode: AX=1687h, the DPMI host (DPMI 0.9; values as
; Microsoft's DOSX gives them, TK18/DX1: AX=0, BX=0 (16-bit clients
; only), CL = processor, DX = 005Ah (version 0.90), SI = 0 (no host
; data), ES:DI = the mode-switch entry)
int2f_rm:
        cmp ax, 1687h
        jne .chain
        xor ax, ax
        xor bx, bx
        mov cl, [cs:cpu_type]
        xor ch, ch
        mov dx, 005Ah
        xor si, si
        push cs
        pop es
        mov di, pm_entry
        iret
.chain: jmp far [cs:old2f]

; INT 24h (critical error) until a program installs its own: fail the
; call. UNVERIFIED: what Microsoft's DOSX's handler answers.
int24_rm:
        mov al, 3
        iret

; ---------------------------------------------------------------------------
; run_kernel: start KRNL386.EXE (on a 386 or later) or KRNL286.EXE from
; DOSX's own directory with INT 21h 4B00h, as Pietrek ch. 1 and TK18
; describe; returns AL = its exit code (INT 21h AH=4Dh).
; Its command tail is DOSX's minus the first word (WIN.COM passes
; "krnl286" there; TK18: "krnl286     /B C:\FOO.EXE" gives KERNEL
; "     /B C:\FOO.EXE"). It inherits DOSX's environment.
run_kernel:
        ; the command tail
        mov es, [psp_seg]
        xor cx, cx
        mov cl, [es:80h]
        mov si, 81h
.sp:    jcxz .tdone
        cmp byte [es:si], ' '
        je .sp1
        cmp byte [es:si], 9
        jne .word
.sp1:   inc si
        dec cx
        jmp .sp
.word:  jcxz .tdone
        mov al, [es:si]
        cmp al, ' '
        je .tdone
        cmp al, 9
        je .tdone
        cmp al, 0Dh
        je .tdone
        inc si
        dec cx
        jmp .word
.tdone: mov di, ktail+1
        xor bx, bx
.tc:    jcxz .tend
        mov al, [es:si]
        cmp al, 0Dh
        je .tend
        mov [di], al
        inc di
        inc si
        inc bx
        dec cx
        cmp bx, 7Eh
        jb .tc
.tend:  mov byte [di], 0Dh
        mov [ktail], bl
        ; the directory DOSX was started from: the program path after
        ; the environment strings
        mov es, [psp_seg]
        mov es, [es:2Ch]
        xor si, si
.env:   cmp byte [es:si], 0
        je .envd
.env1:  inc si
        cmp byte [es:si-1], 0
        jne .env1
        jmp .env
.envd:  add si, 3               ; the double NUL's second byte, the count
        mov di, kpath
        mov bx, kpath           ; just past the last '\' or ':'
.pc:    mov al, [es:si]
        mov [di], al
        inc si
        inc di
        or al, al
        jz .pd
        cmp al, '\'
        je .ps
        cmp al, ':'
        jne .pc
.ps:    mov bx, di
        jmp .pc
.pd:    push ds
        pop es
        mov [kname], bx
        mov si, s_krnl386
        cmp byte [cpu_type], 3
        jae .try
.k286:  mov si, s_krnl286
.try:   mov di, [kname]
        mov cx, 12
        rep movsb
        ; the parameter block
        mov word [epb], 0
        mov word [epb+2], ktail
        mov ax, [imgseg]
        mov [epb+4], ax
        mov word [epb+6], 5Ch
        mov ax, [psp_seg]
        mov [epb+8], ax
        mov word [epb+10], 6Ch
        mov [epb+12], ax
        mov [exec_sp], sp
        mov dx, kpath
        mov bx, epb
        mov ax, 4B00h
        int 21h
        cli
        mov bx, cs
        mov ss, bx
        mov sp, [cs:exec_sp]
        sti
        mov ds, bx
        mov es, bx
        cld
        jnc .ran
        cmp ax, 2
        jne .nok
        mov di, [kname]
        cmp byte [di+4], '3'
        je .k286
.nok:   mov dx, msg_nokrnl
        mov ah, 09h
        int 21h
        mov al, 0FFh
        ret
.ran:   mov ah, 4Dh
        int 21h
        ret

; cleanup: undo what DOSX did (the client's state if it ended without
; INT 21h 4Ch from protected mode, the hooks, the extended memory)
cleanup:
        cmp byte [client_active], 0
        je .nc
        call client_rm_reset
.nc:    call hooks_remove
        cmp byte [xms_ok], 0
        je .d
        mov ah, 06h
        call far [xms]
        mov dx, [xms_h]
        mov ah, 0Dh
        call far [xms]
        mov dx, [xms_h]
        mov ah, 0Ah
        call far [xms]
.d:     ret

; client_rm_reset (real mode): the client's real-mode traces: the
; interrupt vectors DOSX hooked for it, its environment segment
client_rm_reset:
        push es
        xor ax, ax
        mov es, ax
        xor bx, bx
        cli
.l:     cmp byte [rmhooked+bx], 0
        je .n
        mov byte [rmhooked+bx], 0
        mov si, bx
        shl si, 2
        mov ax, [rmorig+si]
        mov [es:si], ax
        mov ax, [rmorig+si+2]
        mov [es:si+2], ax
.n:     inc bx
        cmp bx, 256
        jb .l
        sti
        mov es, [c_psp]
        mov ax, [c_env]
        mov [es:2Ch], ax
        mov byte [client_active], 0
        pop es
        ret

; ---------------------------------------------------------------------------
; pm_entry: the DPMI mode switch (INT 2Fh 1687h ES:DI). Called far from
; real mode with AX bit 0 = 32-bit client (refused: 16-bit clients only),
; ES = the host data segment (none is asked for). Returns in protected
; mode at ring 3 with CS, DS, SS = selectors for the same memory, ES =
; the PSP's selector (limit 100h), the PSP's environment segment
; replaced by a selector; other registers as they were (DPMI 0.9; TK18,
; DX1).
pm_entry:
        test al, 1
        jnz .fail
        cmp byte [cs:client_active], 0
        jne .fail
        push bx
        mov ah, 62h
        int 21h
        mov [cs:c_psp], bx
        pop bx
        pushf
        pop word [cs:c_fl]
        mov ax, 0               ; AX = 0 on return (DX1: pm_ax)
        cli
        mov [cs:c_ax], ax
        mov [cs:c_bx], bx
        mov [cs:c_cx], cx
        mov [cs:c_dx], dx
        mov [cs:c_si], si
        mov [cs:c_di], di
        mov [cs:c_bp], bp
        mov [cs:c_ds], ds
        pop word [cs:c_ip]
        pop word [cs:c_cs]
        mov [cs:c_ss], ss
        mov [cs:c_sp], sp
        mov ax, cs
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, [hs_top]
        call to_pm
        jmp r0_client_start
.fail:  stc
        retf
