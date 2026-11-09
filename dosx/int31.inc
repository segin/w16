; int31.inc - INT 31h, the DPMI functions (ring 0, from int31_stub).
;
; The functions Microsoft's DOSX has (TK18, DX2, DX5; ARCHITECTURE.md
; 9.5): 0000h-000Dh, 0100h-0102h (which fail), 0200h-0205h, 0300h-0306h
; (0305h/0306h fail with AX=0), 0400h, 0500h-0503h, 0600h-0604h,
; 0700h-0703h (0700h/0701h fail with AX=0), 0800h, 0900h-0902h, 0A00h
; (fails with AX=0), 0B00h-0B03h. Any other function ends Windows with
; "Insufficient extended memory to run Windows in standard mode."
; (DX5), and so it does here.
; Each handler returns CF for the client; registers are in the frame.

op_int31:
        mov ax, [bp+F_AX]
        mov si, i31tab
.f:     cmp word [si], 0FFFFh
        je near fatal_nomem
        cmp [si], ax
        je .go
        add si, 4
        jmp .f
.go:    call [si+2]
        jc .err
        jmp clr_cf
.err:   jmp set_cf

i31tab: dw 0000h, d_alloc
        dw 0001h, d_free
        dw 0002h, d_segmap
        dw 0003h, d_incr
        dw 0004h, d_ok
        dw 0005h, d_ok
        dw 0006h, d_getbase
        dw 0007h, d_setbase
        dw 0008h, d_setlimit
        dw 0009h, d_setrights
        dw 000Ah, d_alias
        dw 000Bh, d_getdesc
        dw 000Ch, d_setdesc
        dw 000Dh, d_specific
        dw 0100h, d_dosfail
        dw 0101h, d_ok
        dw 0102h, d_dosfail
        dw 0200h, d_getrm
        dw 0201h, d_setrm
        dw 0202h, d_getexc
        dw 0203h, d_setexc
        dw 0204h, d_getpm
        dw 0205h, d_setpm
        dw 0300h, d_rmint
        dw 0301h, d_rmfar
        dw 0302h, d_rmiret
        dw 0303h, d_cballoc
        dw 0304h, d_cbfree
        dw 0305h, d_fail0
        dw 0306h, d_fail0
        dw 0400h, d_version
        dw 0500h, d_meminfo
        dw 0501h, d_memalloc
        dw 0502h, d_memfree
        dw 0503h, d_memresize
        dw 0600h, d_ok
        dw 0601h, d_ok
        dw 0602h, d_ok
        dw 0603h, d_ok
        dw 0604h, d_pagesize
        dw 0700h, d_fail0
        dw 0701h, d_fail0
        dw 0702h, d_ok
        dw 0703h, d_ok
        dw 0800h, d_physmap
        dw 0900h, d_vif_off
        dw 0901h, d_vif_on
        dw 0902h, d_vif_get
        dw 0A00h, d_fail0
        dw 0B00h, d_wpset
        dw 0B01h, d_wpclr
        dw 0B02h, d_wpget
        dw 0B03h, d_wpreset
        dw 0FFFFh

d_ok:   clc
        ret
d_fail0:
        mov word [bp+F_AX], 0
        stc
        ret
; 0100h/0102h: refused, AX=8, BX=0 (TK18, DX2: with DOS memory free)
d_dosfail:
        mov word [bp+F_AX], 8
        mov word [bp+F_BX], 0
        stc
        ret

; 0000h: CX descriptors. Failure: AX=0 (DX2 alloc2000). CX=0 hangs
; Microsoft's DOSX (DX5 F00C); here it fails.
d_alloc:
        mov cx, [bp+F_CX]
        call ldt_alloc
        mov [bp+F_AX], ax
        ret
d_free: mov bx, [bp+F_BX]
        jmp ldt_free
d_segmap:
        mov ax, [bp+F_BX]
        call segmap
        mov [bp+F_AX], ax
        ret
d_incr: mov word [bp+F_AX], 8
        clc
        ret

; ldtp: BX = the frame's BX selector -> ES:DI = its descriptor, by index
; (Microsoft's DOSX ignores the table bit: 0006h on a GDT selector reads
; the LDT descriptor with the same index; DX1, DX2); CF beyond the LDT
ldtp:   mov di, [bp+F_BX]
        and di, 0FFF8h
        cmp di, LDT_LIMIT
        ja .f
        mov ax, G_LDTA
        mov es, ax
        clc
        ret
.f:     stc
        ret

d_getbase:
        call ldtp
        jc .r
        mov ax, [es:di+2]
        mov [bp+F_DX], ax
        mov al, [es:di+4]
        mov ah, [es:di+7]
        mov [bp+F_CX], ax
        clc
.r:     ret
d_setbase:
        call ldtp
        jc .r
        mov ax, [bp+F_DX]
        mov [es:di+2], ax
        mov ax, [bp+F_CX]
        mov [es:di+4], al
        cmp byte [is386], 0
        je .286
        mov [es:di+7], ah
.286:   clc
.r:     ret
; 0008h: limits above 1 MB are page granular (DX2 setlimit_1fffff)
d_setlimit:
        call ldtp
        jc .r
        mov ax, [bp+F_DX]
        mov cx, [bp+F_CX]
        cmp byte [is386], 0
        jne .386
        or cx, cx
        jz .lo
        stc
        ret
.386:   mov dl, [es:di+6]
        and dl, 70h
        cmp cx, 0Fh
        jbe .byte
        ; page granular: limit >> 12
        shr ax, 12
        mov bx, cx
        shl bx, 4
        or ax, bx
        shr cx, 12
        or dl, 80h
.byte:  and cl, 0Fh
        or dl, cl
        mov [es:di+6], dl
.lo:    mov [es:di], ax
        clc
.r:     ret
; 0009h: the access byte and (on a 386) the high nibble of byte 6; no
; checks, as with Microsoft's DOSX (DX2 rights_*)
d_setrights:
        call ldtp
        jc .r
        mov ax, [bp+F_CX]
        mov [es:di+5], al
        cmp byte [is386], 0
        je .ok
        and ah, 0F0h
        mov al, [es:di+6]
        and al, 0Fh
        or al, ah
        mov [es:di+6], al
.ok:    clc
.r:     ret
; 000Ah: a data descriptor like BX's
d_alias:
        call ldtp
        jc .r
        mov cx, 1
        call ldt_alloc
        jc .r
        mov [bp+F_AX], ax
        mov si, di
        mov di, ax
        and di, 0FFF8h
        mov ax, [es:si]
        mov [es:di], ax
        mov ax, [es:si+2]
        mov [es:di+2], ax
        mov ax, [es:si+4]
        and ah, 60h
        or ah, 92h
        mov [es:di+4], ax
        mov ax, [es:si+6]
        mov [es:di+6], ax
        clc
.r:     ret
; 000Bh/000Ch: copy a descriptor out or in, unchecked (DX2)
d_getdesc:
        call ldtp
        jc .r
        push ds
        push es
        pop ds
        mov si, di
        mov es, [bp+F_ES]
        mov di, [bp+F_DI]
        movsw
        movsw
        movsw
        movsw
        pop ds
        clc
.r:     ret
d_setdesc:
        call ldtp
        jc .r
        push ds
        mov ds, [bp+F_ES]
        mov si, [bp+F_DI]
        movsw
        movsw
        movsw
        movsw
        pop ds
        clc
.r:     ret
; 000Dh: a specific descriptor among the first 16. UNVERIFIED beyond
; BX=0 succeeding (DX5).
d_specific:
        call ldtp
        jc .r
        cmp di, 10h * 8
        jae .f
        mov ax, [es:di+4]
        or ax, [es:di+2]
        jnz .f
        mov word [es:di+4], 0F200h
        clc
        ret
.f:     stc
.r:     ret

; 0200h/0201h: real-mode vectors. One DOSX hooked for the client reads
; and writes as its previous owner.
d_getrm:
        mov bl, [bp+F_BX]
        xor bh, bh
        mov si, bx
        shl si, 2
        cmp byte [rmhooked+bx], 0
        je .ivt
        mov ax, [rmorig+si]
        mov dx, [rmorig+si+2]
        jmp .r
.ivt:   mov ax, G_ZERO
        mov es, ax
        mov ax, [es:si]
        mov dx, [es:si+2]
.r:     mov [bp+F_DX], ax
        mov [bp+F_CX], dx
        clc
        ret
d_setrm:
        mov bl, [bp+F_BX]
        xor bh, bh
        mov si, bx
        shl si, 2
        mov ax, [bp+F_DX]
        mov dx, [bp+F_CX]
        cmp byte [rmhooked+bx], 0
        je .ivt
        mov [rmorig+si], ax
        mov [rmorig+si+2], dx
        clc
        ret
.ivt:   mov cx, G_ZERO
        mov es, cx
        mov [es:si], ax
        mov [es:si+2], dx
        clc
        ret
; 0202h/0203h: exception handlers 00h-1Fh (DX1: 20h fails)
d_getexc:
        mov bx, [bp+F_BX]
        xor bh, bh
        cmp bl, 20h
        jae .f
        shl bx, 2
        mov ax, [excvec+bx]
        mov [bp+F_DX], ax
        mov ax, [excvec+bx+2]
        mov [bp+F_CX], ax
        clc
        ret
.f:     stc
        ret
d_setexc:
        mov bx, [bp+F_BX]
        xor bh, bh
        cmp bl, 20h
        jae .f
        shl bx, 2
        mov ax, [bp+F_DX]
        mov [excvec+bx], ax
        mov ax, [bp+F_CX]
        mov [excvec+bx+2], ax
        clc
        ret
.f:     stc
        ret
; 0204h/0205h: protected-mode interrupt vectors; 0205h does not check
; the selector (DX2 set_pmvec_bad_sel)
d_getpm:
        mov bx, [bp+F_BX]
        xor bh, bh
        shl bx, 2
        mov ax, [pmvec+bx]
        mov [bp+F_DX], ax
        mov ax, [pmvec+bx+2]
        mov [bp+F_CX], ax
        clc
        ret
d_setpm:
        mov bl, [bp+F_BX]
        mov dx, [bp+F_DX]
        mov cx, [bp+F_CX]
        call set_pmvec
        clc
        ret

; set_pmvec: vector BL <- CX:DX, with the IDT; the hardware interrupts
; (08h-0Fh, 70h-77h) and INT 1Ch, 23h and 24h are then also hooked in
; real mode, so they reach the protected-mode handler from real mode too
; (DPMI 0.9); setting the default handler back unhooks them.
set_pmvec:
        push ax
        push bx
        push cx
        push si
        push es
        xor bh, bh
        mov si, bx
        shl si, 2
        mov [pmvec+si], dx
        mov [pmvec+si+2], cx
        cmp bl, 20h
        jb .h
        push cx
        mov cx, bx
        call idt_from_pmvec
        pop cx
.h:     cmp bl, 8
        jb .d
        cmp bl, 0Fh
        jbe .hk
        cmp bl, 1Ch
        je .hk
        cmp bl, 23h
        je .hk
        cmp bl, 24h
        je .hk
        cmp bl, 70h
        jb .d
        cmp bl, 77h
        ja .d
.hk:    mov ax, G_ZERO
        mov es, ax
        ; the default?
        cmp cx, G_HC3 | 3
        jne .hook
        mov ax, bx
        mov cx, 3
        push dx
        mul cx
        pop dx
        add ax, refstub_tab
        cmp dx, ax
        jne .hook
        cmp byte [rmhooked+bx], 0
        je .d
        mov byte [rmhooked+bx], 0
        mov ax, [rmorig+si]
        mov [es:si], ax
        mov ax, [rmorig+si+2]
        mov [es:si+2], ax
        jmp .d
.hook:  cmp byte [rmhooked+bx], 0
        jne .d
        mov ax, [es:si]
        mov [rmorig+si], ax
        mov ax, [es:si+2]
        mov [rmorig+si+2], ax
        mov ax, bx
        mov cx, 3
        mul cx
        add ax, rmhook_tab
        mov [es:si], ax
        mov ax, [imgseg]
        mov [es:si+2], ax
        mov byte [rmhooked+bx], 1
.d:     pop es
        pop si
        pop cx
        pop bx
        pop ax
        ret

; 0300h-0302h: real-mode code with the client's structure ES:DI and CX
; words from its stack (after the INT 31h frame)
d_rmint:
        mov al, 0
        mov ah, [bp+F_BX]
        jmp rmcall
d_rmfar:
        mov al, 1
        jmp rmcall
d_rmiret:
        mov al, 2
rmcall: sub sp, R_SIZE
        mov bx, sp
        push ax
        push ds
        mov ds, [bp+F_ES]
        mov si, [bp+F_DI]
        push ss
        pop es
        mov di, bx
        mov cx, R_SIZE / 2
        rep movsw
        pop ds
        pop ax
        call ring3sp
        add si, K_FL + 2
        mov cx, [bp+F_CX]
        call r0_rmcall
        ; back to the client's structure: everything up to and
        ; including DS, except the reserved dword
        push ds
        push ss
        pop ds
        mov si, bx
        mov es, [bp+F_ES]
        mov di, [bp+F_DI]
        mov cx, R_RES / 2
        rep movsw
        add si, 4
        add di, 4
        mov cx, (R_FS - R_EBX) / 2
        rep movsw
        pop ds
        add sp, R_SIZE
        clc
        ret

; 0303h: a real-mode callback for the procedure DS:SI with the
; structure ES:DI -> CX:DX. Callback c = 16 i + e gets the address
; (cb_seg + e - i):(cb_off + 16 i): entry e of cb_entries, written with
; segment and offset shifted by i paragraphs. How many there can be is
; what real-mode addresses allow (see tables_init), not a table size
; (Microsoft's DOSX gives 16: DX2 cb_count). The
; first 16 records are DOSX's own (a client such as KERNEL may have taken
; all extended memory before COMM.DRV asks for its callback), further
; ones live in extended memory.
d_cballoc:
        xor ax, ax
.f:     cmp ax, [cb_cap]
        jae .grow
        call cb_rec
        cmp word [es:di+2], 0
        je .got
        inc ax
        jmp .f
.grow:  call cb_grow
        jc .r
        jmp .f
.got:   mov bx, [bp+F_SI]
        mov [es:di], bx
        mov bx, [bp+F_DS]
        mov [es:di+2], bx
        mov bx, [bp+F_DI]
        mov [es:di+4], bx
        mov bx, [bp+F_ES]
        mov [es:di+6], bx
        mov bx, ax
        and bx, CB_ENTRIES - 1  ; e
        add bx, [cb_seg]
        shr ax, 4               ; i
        sub bx, ax
        mov [bp+F_CX], bx
        shl ax, 4
        add ax, [cb_off]
        mov [bp+F_DX], ax
        clc
.r:     ret
; 0304h: CX:DX = a callback's address
d_cbfree:
        mov ax, [bp+F_DX]       ; i = (DX - cb_off) / 16
        sub ax, [cb_off]
        jb .f
        test al, 0Fh
        jnz .f
        shr ax, 4
        mov bx, [bp+F_CX]       ; e = CX - cb_seg + i
        sub bx, [cb_seg]
        add bx, ax
        cmp bx, CB_ENTRIES
        jae .f
        shl ax, 4
        add ax, bx              ; c = 16 i + e
        cmp ax, [cb_cap]
        jae .f
        call cb_rec
        mov word [es:di+2], 0   ; freeing twice succeeds (DX2 cb304_again)
        clc
        ret
.f:     stc
        ret

; cb_rec: AX = callback number -> ES:DI = its record (in extended memory)
cb_rec: push ax
        push dx
        push cx
        cmp ax, CB_STATIC
        jae .ext
        mov di, ax
        shl di, 3
        add di, cb_static
        push ss
        pop es
        jmp .r
.ext:   sub ax, CB_STATIC
        xor dx, dx              ; DX:AX = (c - 16) * 8
        mov cx, 3
.sh:    shl ax, 1
        rcl dx, 1
        loop .sh
        add ax, [cb_lin]
        adc dx, [cb_lin+2]
        mov di, G_SCR0
        mov cx, 0FFFFh
        call gdt_base_lin
        mov ax, G_SCR0
        mov es, ax
        xor di, di
.r:     pop cx
        pop dx
        pop ax
        ret

; cb_grow: 64 more callback records (copying the table to a larger
; block); CF if no memory or no more real-mode addresses
cb_grow:
        push ax
        mov ax, [cb_cap]
        add ax, 64
        cmp ax, [cb_max]
        jbe .ok
        mov ax, [cb_max]
        cmp ax, [cb_cap]
        jbe .f
.ok:    mov [cb_newcap], ax
        mov bx, ax
        sub bx, CB_STATIC
        xor cx, cx
        shl bx, 1
        rcl cx, 1
        shl bx, 1
        rcl cx, 1
        shl bx, 1
        rcl cx, 1
        call arena_alloc
        jc .f
        push ax
        push dx
        ; clear it
        add ax, A_HDR
        adc dx, 0
        mov [cb_newlin], ax
        mov [cb_newlin+2], dx
        ; copy the old records (SI:CX bytes)
        mov cx, [cb_cap]
        sub cx, CB_STATIC
        jcxz .new
        xor si, si
        shl cx, 1
        rcl si, 1
        shl cx, 1
        rcl si, 1
        shl cx, 1
        rcl si, 1
        mov ax, [cb_lin]
        mov dx, [cb_lin+2]
        mov bx, [cb_newlin]
        mov di, [cb_newlin+2]
        call lin_copy
        mov ax, [cb_lin]
        mov dx, [cb_lin+2]
        sub ax, A_HDR
        sbb dx, 0
        call arena_free
.new:   pop dx
        pop ax
        mov ax, [cb_newlin]
        mov [cb_lin], ax
        mov ax, [cb_newlin+2]
        mov [cb_lin+2], ax
        ; clear the new records
        mov ax, [cb_cap]
.clr:   cmp ax, [cb_newcap]
        jae .cd
        mov dx, ax
        mov [cb_cap], dx        ; (cb_rec reads the new table)
        call cb_rec
        mov word [es:di], 0
        mov word [es:di+2], 0
        mov word [es:di+4], 0
        mov word [es:di+6], 0
        inc ax
        jmp .clr
.cd:    mov ax, [cb_newcap]
        mov [cb_cap], ax
        pop ax
        clc
        ret
.f:     pop ax
        stc
        ret

; 0400h: version 0.90, flags 000Ah, the processor, PIC bases 08h/70h
d_version:
        mov word [bp+F_AX], 005Ah
        mov word [bp+F_BX], 000Ah
        mov al, [cpu_type]
        mov [bp+F_CX], al
        mov word [bp+F_DX], 0870h
        clc
        ret

; 0500h: the largest free block; the page counts at 14h and 1Ch are it
; in 4 KB pages, 20h is 0, the rest -1 (DX1)
d_meminfo:
        call arena_largest
        mov es, [bp+F_ES]
        mov di, [bp+F_DI]
        mov [es:di], ax
        mov [es:di+2], dx
        push ax
        push dx
        push di
        add di, 4
        mov cx, 22
        mov ax, 0FFFFh
        rep stosw
        pop di
        pop dx
        pop ax
        mov cx, 12
.sh:    shr dx, 1
        rcr ax, 1
        loop .sh
        mov [es:di+14h], ax
        mov [es:di+16h], dx
        mov [es:di+1Ch], ax
        mov [es:di+1Eh], dx
        mov word [es:di+20h], 0
        mov word [es:di+22h], 0
        clc
        ret

; 0501h: BX:CX bytes -> BX:CX = linear address, SI:DI = handle (the
; header's address). Failure leaves AX alone (DX2 m501_huge).
d_memalloc:
        ; Microsoft's DOSX takes two descriptors for every block and
        ; gives the first one's selector as the handle, in SI and DI (DX2
        ; m501_*: 8347h, 8357h, ...; DX4); w16's does the same, so the
        ; selectors given out after it are the same too. The first
        ; describes the block, the second its header.
        mov cx, 2
        call ldt_alloc
        jc .r
        mov [m48_sel], ax
        mov bx, [bp+F_CX]
        mov cx, [bp+F_BX]
        call arena_alloc
        jc .nomem
        push ax
        push dx
        call hdr_map
        mov ax, [m48_sel]
        mov [es:A_SEL], ax
        or word [es:A_FLAGS], 2
        pop dx
        pop ax
        add ax, A_HDR
        adc dx, 0
        mov [bp+F_CX], ax
        mov [bp+F_BX], dx
        mov cx, 0FFFFh
        mov bx, [m48_sel]
        call desc_lin
        add bx, 8
        sub ax, A_HDR
        sbb dx, 0
        mov cx, A_HDR - 1
        call desc_lin
        mov ax, [m48_sel]
        mov [bp+F_DI], ax
        mov [bp+F_SI], ax
        clc
        ret
.nomem: mov bx, [m48_sel]
        call ldt_free
        add bx, 8
        call ldt_free
        stc
.r:     ret

; mh_find: SI = DI = a 0501h handle -> DX:AX = its header (ES mapped);
; CF if it is not one
mh_find:
        mov bx, [bp+F_DI]
        cmp bx, [bp+F_SI]
        jne .f
        test bl, 4
        jz .f
        call desc_base
        jc .f
        sub ax, A_HDR
        sbb dx, 0
        push ax
        push dx
        call arena_check
        pop dx
        pop ax
        jc .f
        test word [es:A_FLAGS], 2
        jz .f
        cmp [es:A_SEL], bx
        jne .f
        clc
        ret
.f:     stc
        ret

; 0502h: SI:DI = handle. (Microsoft's DOSX faults on a bad handle,
; DX5 0502h; here it fails.)
d_memfree:
        call mh_find
        jc .r
        push bx
        call arena_free
        pop bx
        add bx, 8               ; the second first: the first ends up at
        call ldt_free           ; the head of the free chain (DX2
        sub bx, 8               ; m501_800_after_free)
        call ldt_free
        clc
.r:     ret
; 0503h: SI:DI = handle, BX:CX = the new size; it may move; the handle
; stays. (Microsoft's DOSX faults on every resize tried, DX5 F006/F007.)
d_memresize:
        call mh_find
        jc .r
        push bx
        mov bx, [bp+F_CX]
        mov cx, [bp+F_BX]
        mov byte [a_move], 1
        call arena_resize
        mov byte [a_move], 0
        pop bx
        jc .r
        push ax
        push dx
        call hdr_map
        mov [es:A_SEL], bx
        or word [es:A_FLAGS], 2
        pop dx
        pop ax
        add ax, A_HDR
        adc dx, 0
        mov [bp+F_CX], ax
        mov [bp+F_BX], dx
        mov cx, 0FFFFh
        call desc_lin
        add bx, 8
        sub ax, A_HDR
        sbb dx, 0
        mov cx, A_HDR - 1
        call desc_lin
        clc
.r:     ret

; 0604h: 4 KB pages (DX5: BX:CX = 0000:1000)
d_pagesize:
        mov word [bp+F_BX], 0
        mov word [bp+F_CX], 1000h
        clc
        ret
; 0800h: no paging, the linear address is the physical one (DX5: BX:CX
; unchanged for 0)
d_physmap:
        clc
        ret

; 0900h-0902h: the virtual interrupt flag is the client's IF; AL = the
; state before (TK18, DX5: AX = 0901h with interrupts enabled)
d_vif_off:
        call vif_get
        call ring3sp
        and word [es:si+K_FL], ~FL_IF
        clc
        ret
d_vif_on:
        call vif_get
        call ring3sp
        or word [es:si+K_FL], FL_IF
        clc
        ret
d_vif_get:
        call vif_get
        clc
        ret
vif_get:
        call ring3sp
        mov al, 0
        test word [es:si+K_FL], FL_IF
        jz .s
        inc al
.s:     mov [bp+F_AX], al
        ret

; 0B00h-0B03h: debug watchpoints. Only the bookkeeping: handles 0-3
; (DX5: 0B00h gives BX=0; 0B01h-0B03h fail for a handle never given).
; UNVERIFIED: whether Microsoft's DOSX programs the debug registers.
d_wpset:
        xor bx, bx
.l:     call wp_mask
        test [wp_used], al
        jz .got
        inc bx
        cmp bx, 4
        jb .l
        stc
        ret
.got:   or [wp_used], al
        mov [bp+F_BX], bx
        clc
        ret
d_wpclr:
        call wp_chk
        jc .r
        not al
        and [wp_used], al
.r:     ret
d_wpget:
        call wp_chk
        jc .r
        mov word [bp+F_AX], 0
.r:     ret
d_wpreset:
        jmp wp_chk
wp_chk: mov bx, [bp+F_BX]
        cmp bx, 4
        jae .f
        call wp_mask
        test [wp_used], al
        jz .f
        clc
        ret
.f:     stc
        ret
wp_mask:                        ; AL = 1 << BL
        mov cl, bl
        mov al, 1
        shl al, cl
        ret
