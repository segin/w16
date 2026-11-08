; ldt.inc - the LDT (ring 0, DS = G_HD0).
;
; Layout and allocation as observed with Microsoft's DOSX (DX1, DX2;
; ARCHITECTURE.md 9.5):
;   - 8190 descriptors (limit 0FFEFh), reachable by the client through
;     the vendor entry's alias 009Bh;
;   - descriptors 01h-4Fh are for INT 31h 0002h (segment to descriptor),
;     given out from 4Fh (027Fh) downwards and never freed; 0002h for a
;     segment that already has one returns that one;
;   - 50h-59h are DOSX's own (two scratch descriptors, 0287h and 028Fh);
;   - from 5Ah (02D7h) on, the free descriptors form a chain through
;     their limit fields (the byte offset of the next free one, 0 at the
;     end; descriptors never used have base bits 16-23 = 80h, freed ones
;     keep their base and get access byte 0); 0000h
;     takes from the head, 0001h puts back at the head (both observed:
;     DX2 alloc1_*, free_*, realloc_*), several at once are the first
;     run of free descriptors found walking the chain (DX2 alloc3,
;     alloc2_after);
;   - a fresh descriptor is a present data segment, DPL 3, base 0,
;     limit 0 (DX2 desc_alloc1_a).
; Which descriptors are allocated is also kept in a bitmap (ldt_bm), as
; the client may write any descriptor through the alias.

; ldt_init: a fresh LDT for a new client
ldt_init:
        push es
        mov ax, G_LDTA
        mov es, ax
        xor di, di
        mov cx, (LDT_LIMIT + 1) / 2
        xor ax, ax
        rep stosw
        ; DOSX's own descriptors
        mov di, LDT_SCRA * 8
        mov word [es:di], 0FFFFh
        mov word [es:di+4], 0F200h
        mov di, LDT_SCRB * 8
        mov word [es:di], 0FFFFh
        mov word [es:di+4], 0F200h
        mov di, 54h * 8                 ; as Microsoft's 02A7h
        mov word [es:di], 02A8h
        mov word [es:di+4], 0F80h
        mov di, 55h * 8                 ; the top of extended memory
        mov ax, [emb_end]
        mov dx, [emb_end+2]
        sub ax, 16
        sbb dx, 0
        mov word [es:di], 0Fh
        mov [es:di+2], ax
        mov [es:di+4], dl
        mov byte [es:di+5], 0F2h
        mov [es:di+7], dh
        mov ax, [pool_lin]
        mov dx, [pool_lin+2]
        mov di, 56h * 8
.own:   mov word [es:di], 0Fh
        mov [es:di+2], ax
        mov [es:di+4], dl
        mov byte [es:di+5], 0F2h
        mov [es:di+7], dh
        add di, 8
        cmp di, LDT_RES_END * 8
        jb .own
        cmp byte [is386], 0
        jne .chain
        mov byte [es:55h*8+7], 0        ; no base bits 24-31 on a 286
        mov di, 56h * 8
.z286:  mov byte [es:di+7], 0
        add di, 8
        cmp di, LDT_RES_END * 8
        jb .z286
        ; the free chain
.chain: mov di, LDT_RES_END * 8
.ch:    lea ax, [di+8]
        mov [es:di], ax
        mov byte [es:di+4], 80h
        add di, 8
        cmp di, (LDT_COUNT - 1) * 8
        jb .ch
        mov word [es:di], 0
        mov byte [es:di+4], 80h
        mov word [ldt_head], LDT_RES_END * 8
        mov word [seg_next], LDT_SEGTOP
        ; nothing allocated
        push ds
        pop es
        mov di, ldt_bm
        mov cx, 512
        xor ax, ax
        rep stosw
        pop es
        ret

; bm_*: AX = descriptor index
bm_set: push bx
        push cx
        call bm_pos
        or [ldt_bm+bx], cl
        pop cx
        pop bx
        ret
bm_clr: push bx
        push cx
        call bm_pos
        not cl
        and [ldt_bm+bx], cl
        pop cx
        pop bx
        ret
bm_test:                ; ZF clear if allocated
        push bx
        push cx
        call bm_pos
        test [ldt_bm+bx], cl
        pop cx
        pop bx
        ret
bm_pos: mov bx, ax
        mov cl, al
        and cl, 7
        shr bx, 3
        mov ch, 1
        shl ch, cl
        mov cl, ch
        ret

; ldt_alloc: CX descriptors -> AX = the first one's selector (RPL 3);
; CF (AX = 0) if there are not that many in a row
ldt_alloc:
        push bx
        push cx
        push dx
        push si
        push di
        push es
        mov ax, G_LDTA
        mov es, ax
        or cx, cx
        jz .fail
        mov di, [ldt_head]
        cmp cx, 1
        jne .cand
        or di, di
        jz .fail
        mov ax, [es:di]
        mov [ldt_head], ax
        jmp .got
.cand:  or di, di
        jz .fail
        mov ax, di
        shr ax, 3
        mov dx, ax
        add dx, cx
        cmp dx, LDT_COUNT
        ja .next
        mov si, 1
.chk:   cmp si, cx
        jae .found
        push ax
        add ax, si
        call bm_test
        pop ax
        jnz .next
        inc si
        jmp .chk
.next:  mov di, [es:di]
        jmp .cand
.found: ; unlink descriptors DI .. DI + 8*CX - 1 from the chain
        mov dx, cx
        shl dx, 3
        add dx, di              ; the end
        xor bx, bx              ; the previous free one (0: the head)
        mov si, [ldt_head]
.ul:    or si, si
        jz .got
        mov ax, [es:si]
        cmp si, di
        jb .keep
        cmp si, dx
        jae .keep
        or bx, bx
        jnz .ulm
        mov [ldt_head], ax
        jmp .uln
.ulm:   mov [es:bx], ax
.uln:   mov si, ax
        jmp .ul
.keep:  mov bx, si
        mov si, ax
        jmp .ul
.got:   ; CX fresh descriptors from DI
        mov si, di
        mov dx, cx
.init:  mov word [es:si], 0
        mov word [es:si+2], 0
        mov word [es:si+4], 0F200h
        mov word [es:si+6], 0
        mov ax, si
        shr ax, 3
        call bm_set
        add si, 8
        dec dx
        jnz .init
        mov ax, di
        or ax, 7
        clc
        jmp .ret
.fail:  xor ax, ax
        stc
.ret:   pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        ret

; ldt_free: BX = selector; CF if it is not one the client may free
; (DX2: 0001h fails for 0, a GDT selector, FFFFh and 0002h's)
ldt_free:
        push ax
        push di
        push es
        test bl, 4
        jz .fail
        mov ax, bx
        shr ax, 3
        cmp ax, LDT_RES_END
        jb .fail
        cmp ax, LDT_COUNT
        jae .fail
        call bm_test
        jz .fail
        call bm_clr
        mov di, bx
        and di, 0FFF8h
        mov ax, G_LDTA
        mov es, ax
        mov ax, [ldt_head]
        mov [es:di], ax
        mov byte [es:di+5], 0   ; the base stays (DX2 pm49_1001_after)
        mov [ldt_head], di
        clc
        jmp .ret
.fail:  stc
.ret:   pop es
        pop di
        pop ax
        ret

; segmap: AX = real-mode segment -> AX = a data selector for it (64 KB)
segmap:
        push bx
        push cx
        push dx
        push si
        push es
        mov cx, ax
        mov dx, ax
        shl cx, 4
        shr dx, 12              ; DL:CX = the base
        mov si, G_LDTA
        mov es, si
        mov si, [seg_next]
        inc si
.s:     cmp si, LDT_SEGTOP
        ja .new
        mov bx, si
        shl bx, 3
        cmp byte [es:bx+5], 0
        je .sn
        cmp [es:bx+2], cx
        jne .sn
        cmp [es:bx+4], dl
        jne .sn
        cmp byte [es:bx+7], 0
        jne .sn
        mov ax, bx
        or ax, 7
        jmp .ret
.sn:    inc si
        jmp .s
.new:   mov si, [seg_next]
        cmp si, 1
        jb .chain
        dec word [seg_next]
        mov bx, si
        shl bx, 3
        or bx, 7
        jmp .set
.chain: push cx
        mov cx, 1
        call ldt_alloc
        pop cx
        jc .ret
        mov bx, ax
.set:   mov ax, cx
        shr ax, 4
        mov cl, dl
        shl cl, 4
        or ah, cl               ; AX = the segment again
        mov cx, 0FFFFh
        mov dl, 0F2h
        call desc_seg
        mov ax, bx
        clc
.ret:   pop es
        pop si
        pop dx
        pop cx
        pop bx
        ret

; desc_seg: BX = selector; base AX * 16, limit CX, access DL, byte 6 = 0
desc_seg:
        push ax
        push di
        push dx
        push es
        mov di, G_LDTA
        mov es, di
        mov di, bx
        and di, 0FFF8h
        mov [es:di], cx
        mov dh, dl
        mov dl, ah
        shr dl, 4
        shl ax, 4
        mov [es:di+2], ax
        mov [es:di+4], dl
        mov [es:di+5], dh
        mov word [es:di+6], 0
        pop es
        pop dx
        pop di
        pop ax
        ret

; desc_lin: BX = selector; base DX:AX, limit CX (bytes, < 64 KB), data
desc_lin:
        push di
        push es
        mov di, G_LDTA
        mov es, di
        mov di, bx
        and di, 0FFF8h
        mov [es:di], cx
        mov [es:di+2], ax
        mov [es:di+4], dl
        mov byte [es:di+5], 0F2h
        mov byte [es:di+6], 0
        mov [es:di+7], dh
        cmp byte [is386], 0
        jne .d
        mov byte [es:di+7], 0
.d:     pop es
        pop di
        ret

; desc_base: BX = selector -> DX:AX = its base (by index; CF beyond the LDT)
desc_base:
        push di
        push es
        mov di, bx
        and di, 0FFF8h
        cmp di, LDT_LIMIT
        ja .f
        mov ax, G_LDTA
        mov es, ax
        mov ax, [es:di+2]
        mov dl, [es:di+4]
        mov dh, [es:di+7]
        clc
        jmp .r
.f:     stc
.r:     pop es
        pop di
        ret

; tiles_set: selectors from BX (CX of them) for DX:AX, SI:DI bytes
; (the last one gets what is left, each at most 64 KB)
tiles_set:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        jcxz .d
.l:     push cx
        mov cx, 0FFFFh
        or si, si
        jnz .full
        mov cx, di
        dec cx
.full:  call desc_lin
        pop cx
        add bx, 8
        inc dx
        or si, si
        jz .d
        dec si
        loop .l
.d:     pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret
