; init.inc - real mode: start-up, XMS, starting KERNEL, exit, the
; real-mode INT 2Fh (DPMI 1687h) and INT 24h hooks, the mode-switch
; entry point a client calls.

start:  ; DOS: DS = ES = PSP, SS:SP from the header
        mov ax, cs
        mov ds, ax
        mov [imgseg], ax
        mov [psp_seg], es
        cld
        ; the uninitialised data zeroed (but the stack we are on): DOS
        ; does not clear the memory a program gets. The callback-stack
        ; selector cache (cbsel) held whatever was there before, and with
        ; w16's WSWAP below (Microsoft's leaves zeros there) the first PS/2
        ; mouse packet loaded a stale "selector" (EC=4440: "Standard Mode:
        ; Fault in MS-DOS Extender" at a click, w16's install tree)
        push ds
        pop es
        mov di, bss_start
        mov cx, init_stack - bss_start
        xor al, al
        rep stosb
        mov es, [psp_seg]
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
        ; the Windows start-up broadcast, before XMS (as Microsoft's DOSX:
        ; INT 2Fh 1605h with DX=1, standard mode, DI=030Ah, ES:BX = DS:SI =
        ; 0, CX = 0; WC3 between WSWAP and DOSX). A driver or TSR that
        ; answers with CX other than 0 cannot run under Windows: Windows
        ; then prints nothing of its own, broadcasts 1606h and returns to
        ; DOS (Windows 3.1 DDK, INT 2Fh 1605h). Once 1605h has gone out,
        ; every way out sends 1606h (DDK, INT 2Fh 1606h; w16's DOSX did not
        ; when XMS failed). UNVERIFIED: the exit code then (here FFh, as
        ; after DOSX's other fatal messages), and an ES:BX chain of instance
        ; data (w16's ignores it).
        push ds
        xor bx, bx
        mov es, bx
        mov ds, bx
        xor si, si
        xor cx, cx
        mov dx, 1
        mov di, 030Ah
        mov ax, 1605h
        int 2Fh
        pop ds
        push ds
        pop es
        mov byte [did1605], 1
        jcxz .go
        call end_bcast
        mov ax, 4CFFh
        int 21h
.go:    call ini_read
        call xms_init
        jnc .memok
        call end_bcast
        mov dx, msg_nomem
        jmp fail_msg
.memok:
%ifdef TRACE
        call trc_create
%endif
        call tables_init
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
; Source: XMS 3.0 functions 00h (version), 08h (query free), 09h
; (allocate), 88h (query any free), 89h (allocate any), 0Ch (lock), 0Dh
; (unlock), 0Ah (free), 05h (local enable A20).
; 08h and 09h count KB in 16 bits, so one block is 65535 KB at most. On a
; 386 w16 takes blocks until XMS has none left (up to XB_MAX), as many
; as the driver gives: with an XMS 3.0 driver it asks with 88h and 89h
; first (32-bit KB counts in EAX and EDX: the largest free block whatever
; its size), then with 08h and 09h, unless SYSTEM.INI says [w16]
; LargeMemory=0 (stock 3.1: 08h and 09h only, so no block over 64 MB;
; ARCHITECTURE.md 9.6, "More than 64 MB"). Both are needed: Microsoft's
; WSWAP, which starts DOSX, keeps a block of its own (64 MB at most) that
; it gives only through its 08h/09h, while 88h/89h go past it to the
; driver (XM1 as DOSX.EXE under WSWAP, 80 MB: 08h FFFFh, 88h 3A40h KB).
; The blocks are sorted by address: the LDT and the IDT go at the start
; of the lowest, the client's arena runs over all of them (arena_init).
; On an 80286 (24-bit addresses, 16 MB at most) one 08h/09h block.
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
        mov byte [xb_n], 0
        mov byte [xms3], 0
        cmp byte [is386], 0
        je .one
        cmp byte [xms_stock], 0
        jne .more
        mov ah, 00h
        call far [xms]
        cmp ax, 0300h           ; the version, BCD
        jb .more
        mov byte [xms3], 1
.more:  call xb_take
        jnc .more
        jmp .have
.one:   call xb_take
.have:  cmp byte [xb_n], 0
        je .fail
        cmp byte [xb_n], 1
        je .low
        cpu 386
        call xb_sort
        cpu 286
.low:   ; the lowest block holds the LDT, the IDT, the ring-3 host stack
        ; (and the trace buffer): one too small for them is given back
        mov ax, [xb_end]
        mov dx, [xb_end+2]
        sub ax, [xb_lin]
        sbb dx, [xb_lin+2]
        cmp dx, LOW_MIN >> 16
        ja .lowok
        jb .small
        cmp ax, LOW_MIN & 0FFFFh
        jae .lowok
.small: cmp byte [xb_n], 1
        je .freeall
        call xb_drop0
        jmp .low
.lowok: mov ah, 05h
        call far [xms]
        cmp ax, 1
        jne .freeall
        mov byte [xms_ok], 1
        mov ax, [xb_lin]
        mov dx, [xb_lin+2]
        mov [emb_lin], ax
        mov [emb_lin+2], dx
        mov bl, [xb_n]
        xor bh, bh
        shl bx, 2
        mov ax, [xb_end+bx-4]
        mov dx, [xb_end+bx-2]
        mov [emb_end], ax
        mov [emb_end+2], dx
        ; the LDT first, then the client's memory
        mov ax, [emb_lin]
        mov dx, [emb_lin+2]
        mov [ldt_lin], ax
        mov [ldt_lin+2], dx
        inc dx                  ; the IDT (as Microsoft's: in extended
        mov [idt_lin], ax       ; memory, DX1 idt_base)
        mov [idt_lin+2], dx
        add ax, 800h
        adc dx, 0
        mov [h3_lin], ax        ; the ring-3 host stack (G_HD3)
        mov [h3_lin+2], dx
        add ax, H3_SIZE
        adc dx, 0
%ifdef TRACE
        mov [trc_lin], ax       ; the trace buffer between LDT and pool
        mov [trc_lin+2], dx
        add dx, TRC_SIZE >> 16
%endif
        mov [pool_lin], ax
        mov [pool_lin+2], dx
        clc
        ret
.freeall:
        call xms_release
.fail:  push ds
        pop es
        stc
        ret

; xb_take (real mode): one more block from XMS, locked, into the table
; (xb_h, xb_lin, xb_end). With [xms3] 88h/89h are tried first, then
; 08h/09h. CF if XMS has no block of 64 KB or more left or the table is
; full. AX, BX, CX, DX changed (on a 386 the high halves are left 0:
; 88h puts sizes and an address there, and the client DOSX EXECs finds
; them as with 08h/09h).
xb_take:
        cmp byte [xb_n], XB_MAX
        jae .none
        cmp byte [xms3], 0
        je .x2
        cpu 386
        mov ah, 88h
        xor bl, bl
        call far [xms]
        mov edx, eax            ; the largest free block, KB
        xor eax, eax
        or bl, bl
        jnz .x3c                ; not implemented (80h), nothing free (A0h)
        cmp edx, EMB_KB_MAX
        jbe .x3k
        mov edx, EMB_KB_MAX
.x3k:   cmp edx, 64
        jb .x3c
        mov [xb_kb], edx
        mov ah, 89h
        call far [xms]
.x3c:   movzx eax, ax
        movzx edx, dx
        xor ecx, ecx
        xor ebx, ebx
        cpu 286
        cmp ax, 1
        je .got
.x2:    mov ah, 08h
        xor bx, bx
        call far [xms]
        cmp ax, 64
        jb .none
        mov [xb_kb], ax
        mov word [xb_kb+2], 0
        mov dx, ax
        mov ah, 09h
        call far [xms]
        cmp ax, 1
        jne .none
.got:   mov [xb_hcur], dx
        mov ah, 0Ch
        call far [xms]
        cmp ax, 1
        jne .unfree
        push si
        push di
        mov cx, dx              ; the address, CX:BX
        mov al, [xb_n]
        xor ah, ah
        mov si, ax
        shl si, 1
        mov di, si
        shl di, 1
        mov ax, [xb_hcur]
        mov [xb_h+si], ax
        mov [xb_lin+di], bx
        mov [xb_lin+di+2], cx
        ; the end: xb_kb * 1024 bytes after the start
        mov ax, [xb_kb]
        mov dx, ax
        shl ax, 10
        shr dx, 6
        mov si, [xb_kb+2]
        shl si, 10
        or dx, si
        add ax, bx
        adc dx, cx
        mov [xb_end+di], ax
        mov [xb_end+di+2], dx
        inc byte [xb_n]
        pop di
        pop si
        clc
        ret
.unfree:
        mov dx, [xb_hcur]
        mov ah, 0Ah
        call far [xms]
.none:  stc
        ret

; xb_sort (386, real mode): the table in address order (insertion sort)
        cpu 386
xb_sort:
        push si
        push di
        mov si, 1
.o:     mov al, [xb_n]
        xor ah, ah
        cmp si, ax
        jae .d
        mov di, si
.i:     or di, di
        jz .n
        mov bx, di
        shl bx, 2
        mov eax, [xb_lin+bx-4]
        cmp eax, [xb_lin+bx]
        jbe .n
        xchg eax, [xb_lin+bx]
        mov [xb_lin+bx-4], eax
        mov eax, [xb_end+bx-4]
        xchg eax, [xb_end+bx]
        mov [xb_end+bx-4], eax
        shr bx, 1
        mov ax, [xb_h+bx-2]
        xchg ax, [xb_h+bx]
        mov [xb_h+bx-2], ax
        dec di
        jmp .i
.n:     inc si
        jmp .o
.d:     xor eax, eax
        pop di
        pop si
        ret
        cpu 286

; xb_drop0: give the lowest block back and take it out of the table
xb_drop0:
        mov dx, [xb_h]
        mov ah, 0Dh
        call far [xms]
        mov dx, [xb_h]
        mov ah, 0Ah
        call far [xms]
        dec byte [xb_n]
        push si
        push di
        push cx
        push ds
        pop es
        mov si, xb_h + 2
        mov di, xb_h
        mov cx, XB_MAX - 1
        rep movsw
        mov si, xb_lin + 4
        mov di, xb_lin
        mov cx, (XB_MAX - 1) * 2
        rep movsw
        mov si, xb_end + 4
        mov di, xb_end
        mov cx, (XB_MAX - 1) * 2
        rep movsw
        pop cx
        pop di
        pop si
        ret

; xms_release (real mode): unlock and free every block taken
xms_release:
.l:     cmp byte [xb_n], 0
        je .d
        dec byte [xb_n]
        mov bl, [xb_n]
        xor bh, bh
        shl bx, 1
        mov dx, [xb_h+bx]
        push dx
        mov ah, 0Dh
        call far [xms]
        pop dx
        mov ah, 0Ah
        call far [xms]
        jmp .l
.d:     ret

; ---------------------------------------------------------------------------
; ini_read: w16's own key [w16] LargeMemory= from SYSTEM.INI, looked for in
; DOSX's directory and then in the one above it (WINDOWS\SYSTEM\DOSX.EXE,
; WINDOWS\SYSTEM.INI), as w16's WIN386 finds it. A value starting with 0
; sets [xms_stock] (XMS 08h/09h only, as 3.1); no file, section or key
; leaves it clear. Uses the transfer buffer (kpath, the file's text, a
; line). ES = DS after.
INI_LINE        equ tbuf + 300h         ; a line, at most 0FFh characters
INI_BUF         equ tbuf + 400h         ; the file, 0C00h bytes at a time
INI_BUFSZ       equ 0C00h
ini_read:
        call own_path
        push ds
        pop es
        mov [ini_name], bx
        call ini_try
        jnc .d
        ; the directory above: back from the '\' before the name to the
        ; '\' or ':' before that
        mov di, [ini_name]
        cmp di, kpath
        je .d
        dec di
.up:    cmp di, kpath
        je .d
        dec di
        cmp byte [di], '\'
        je .p
        cmp byte [di], ':'
        jne .up
.p:     inc di
        mov [ini_name], di
        call ini_try
.d:     ret

; ini_try: SYSTEM.INI at [ini_name] in kpath, scanned line by line. CF if
; it cannot be opened.
ini_try:
        mov di, [ini_name]
        mov si, s_sysini
.cp:    lodsb
        mov [di], al
        inc di
        or al, al
        jnz .cp
        mov dx, kpath
        mov ax, 3D00h
        int 21h
        jc .r
        mov [ini_h], ax
        mov byte [ini_sec], 0
        xor cx, cx              ; the line's length so far
.rd:    push cx
        mov bx, [ini_h]
        mov dx, INI_BUF
        mov cx, INI_BUFSZ
        mov ah, 3Fh
        int 21h
        pop cx
        jc .eof
        or ax, ax
        jz .eof
        mov si, INI_BUF
        mov dx, ax
.ch:    lodsb
        cmp al, 0Ah
        je .eol
        cmp al, 0Dh
        je .nx
        cmp cx, 0FFh
        jae .nx
        mov di, cx
        mov [INI_LINE + di], al
        inc cx
.nx:    dec dx
        jnz .ch
        jmp .rd
.eol:   call ini_line
        xor cx, cx
        jmp .nx
.eof:   call ini_line           ; a last line without a line feed
        mov bx, [ini_h]
        mov ah, 3Eh
        int 21h
        clc
.r:     ret

; ini_line: INI_LINE, CX characters: a section header, or the key in [w16]
ini_line:
        push si
        push dx
        mov si, INI_LINE
        mov bx, si
        add bx, cx              ; the line's end
        call ini_blank
        cmp si, bx
        jae .d
        cmp byte [si], '['
        jne .key
        mov di, s_w16sec
        call ini_match
        mov al, 0
        jnz .sec
        inc ax
.sec:   mov [ini_sec], al
        jmp .d
.key:   cmp byte [ini_sec], 0
        je .d
        mov di, s_largemem
        call ini_match
        jnz .d
        call ini_blank
        cmp si, bx
        jae .d
        cmp byte [si], '='
        jne .d
        inc si
        call ini_blank
        mov al, 0
        cmp si, bx
        jae .set
        cmp byte [si], '0'
        jne .set
        inc ax
.set:   mov [xms_stock], al
.d:     pop dx
        pop si
        ret

; ini_blank: SI past blanks and tabs (up to BX)
ini_blank:
.l:     cmp si, bx
        jae .d
        cmp byte [si], ' '
        je .n
        cmp byte [si], 9
        jne .d
.n:     inc si
        jmp .l
.d:     ret

; ini_match: does the text at SI (up to BX) start with the lower-case,
; NUL-ended pattern at DI, in any case? ZF if so, SI then past it.
ini_match:
        push si
.l:     mov ah, [di]
        or ah, ah
        jz .yes
        cmp si, bx
        jae .no
        lodsb
        cmp al, 'A'
        jb .c
        cmp al, 'Z'
        ja .c
        add al, 20h
.c:     inc di
        cmp al, ah
        je .l
.no:    pop si
        or sp, sp               ; ZF clear
        ret
.yes:   add sp, 2
        cmp al, al              ; ZF set
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
        mov si, [idt_lin]
        mov dx, [idt_lin+2]
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
        mov bx, G_HD3           ; the ring-3 host stack, in extended memory
        mov si, [h3_lin]
        mov dx, [h3_lin+2]
        mov cx, 0FFFFh
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
%ifdef TRACE
        mov bx, G_TRC
        call gdt_set
%endif
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
        mov ax, [idt_lin]
        mov [idtr+2], ax
        mov ax, [idt_lin+2]
        mov [idtr+4], ax
        mov word [rmidtr], 3FFh
        ; TSS: ring 0 stack
        mov di, tss
        mov cx, 22
        xor ax, ax
        rep stosw
        mov word [tss+2], hs_stack_top
        mov word [tss+4], G_HD0
        mov word [hs_top], hs_stack_top
        mov word [h3_top], H3_SIZE
        mov word [tb_top], tbuf + TB_SIZE
        ; real-mode callbacks: entry 0's canonical address (cb_entries;
        ; the entries are a paragraph apart, so entry e is cb_seg + e)
        mov ax, cb_entries
        mov dx, ax
        and ax, 0Fh
        mov [cb_off], ax
        shr dx, 4
        add dx, [imgseg]
        mov [cb_seg], dx
        ; as many as real-mode addresses allow: i up to min(0FEFh, cb_seg)
        ; (the segment must not go below 0, and entry e reached as
        ; (cb_seg + e - i):(cb_off + 16 i) runs its near call to cb_c0,
        ; 100h bytes on, and the far jump after it at offsets that must
        ; stay below 10000h: i - e at most 0FEFh, so i at most 0FEFh for
        ; every e), times the 16 entries
        cmp dx, 0FEFh
        jbe .cbi
        mov dx, 0FEFh
.cbi:   inc dx
        shl dx, 4
        dec dx                  ; (at most 0FFFFh)
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
; own_path: the path DOSX was started from (the program name after the
; environment strings) into kpath; BX = just past its last '\' or ':'
; (where a file name of its directory goes). ES changed.
own_path:
        mov di, kpath
        mov bx, kpath
        mov byte [di], 0
        mov es, [psp_seg]
        mov ax, [es:2Ch]
        or ax, ax               ; no environment: no path (DOSX's
        jz .pd                  ; directory is the current one)
        mov es, ax
        xor si, si
.env:   cmp byte [es:si], 0
        je .envd
.env1:  inc si
        cmp byte [es:si-1], 0
        jne .env1
        jmp .env
.envd:  ; past the strings' end: the count (1), then the path. An empty
        ; environment may be one NUL or two before the count.
        cmp word [es:si+1], 1
        je .cnt
        cmp byte [es:si+1], 0
        jne .cnt
        cmp word [es:si+2], 1
        jne .cnt
        inc si
.cnt:   add si, 3
.pc:    cmp di, epb - 13        ; room for the name after it (KRNLx86.EXE)
        jae .long
        mov al, [es:si]
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
.long:  mov bx, kpath           ; too long for DOS anyway: no path
.pd:    ret

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
        call own_path
        push ds
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
.nc:    ; (the video mode is left as the client left it: Microsoft's DOSX
        ; makes no INT 10h call after Windows' display driver set mode 3
        ; (E10, docs/notes/keyloss-handover.md); it is WIN.COM that sets
        ; text mode after WSWAP. w16's DOSX used to set back the mode it
        ; found at its start: 12h, the logo's, and Windows ended in VGA
        ; graphics with the BIOS's text on it)
        call end_bcast
        call hooks_remove
        cmp byte [xms_ok], 0
        je .d

        mov ah, 06h
        call far [xms]
        call xms_release
.d:     ret

; end_bcast: the end broadcast, INT 2Fh 1606h with DX=1 (WC3), once,
; if 1605h went out
end_bcast:
        cmp byte [did1605], 0
        je .r
        mov byte [did1605], 0
        mov dx, 1
        mov ax, 1606h
        int 2Fh
.r:     ret

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
        mov di, bx
        shl di, 2
        mov al, [hook_vecs+bx]
        xor ah, ah
        mov si, ax
        shl si, 2
        mov ax, [rmorig+di]
        mov [es:si], ax
        mov ax, [rmorig+di+2]
        mov [es:si+2], ax
.n:     inc bx
        cmp bx, NHOOK
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
        cmp byte [cs:is386], 0
        je .nfs
        cpu 386
        mov [cs:rm_fsv], fs     ; real mode's FS and GS (to_rm)
        mov [cs:rm_gsv], gs
        cpu 286
.nfs:   pop word [cs:c_ip]
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
