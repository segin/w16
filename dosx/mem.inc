; mem.inc - the client's extended memory (ring 0, DS = G_HD0).
;
; Like Microsoft's DOSX (DX2: INT 31h 0501h gives blocks at byte, not
; paragraph, boundaries, each 16 bytes after the previous block's end,
; first fit; 0500h's largest block shrinks by size + 16 per block), the
; memory is an arena: a 16-byte header (A_*) before every block, the
; next header right after the block, a used sentinel header at the end.
; The headers live in extended memory and are reached through G_SCR1.
; No table of blocks: as many blocks as fit.

; hdr_map: ES = G_SCR1 at linear DX:AX
hdr_map:
        push cx
        push di
        mov di, G_SCR1
        mov cx, 0FFFFh
        call gdt_base_lin
        mov di, G_SCR1
        mov es, di
        pop di
        pop cx
        ret

; arena_init: one free block over the pool, then the sentinel
arena_init:
        push es
        mov ax, [emb_end]
        mov dx, [emb_end+2]
        sub ax, A_HDR
        sbb dx, 0
        mov [pool_last], ax
        mov [pool_last+2], dx
        call hdr_map
        mov word [es:A_SIZE], 0
        mov word [es:A_SIZE+2], 0
        mov ax, [pool_lin]
        mov [es:A_PREV], ax
        mov ax, [pool_lin+2]
        mov [es:A_PREV+2], ax
        mov word [es:A_FLAGS], 1
        mov word [es:A_SEL], 0
        mov word [es:A_NSEL], 0
        mov word [es:A_MAGIC], MAGIC
        mov ax, [pool_lin]
        mov dx, [pool_lin+2]
        call hdr_map
        mov ax, [pool_last]
        mov dx, [pool_last+2]
        sub ax, [pool_lin]
        sbb dx, [pool_lin+2]
        sub ax, A_HDR
        sbb dx, 0
        mov [es:A_SIZE], ax
        mov [es:A_SIZE+2], dx
        xor ax, ax
        mov [es:A_PREV], ax
        mov [es:A_PREV+2], ax
        mov [es:A_FLAGS], ax
        mov [es:A_SEL], ax
        mov [es:A_NSEL], ax
        mov word [es:A_MAGIC], MAGIC
        pop es
        ret

; a_next: DX:AX = header -> DX:AX = the next header (ES = mapped header)
a_next: add ax, [es:A_SIZE]
        adc dx, [es:A_SIZE+2]
        add ax, A_HDR
        adc dx, 0
        ret

; arena_alloc: CX:BX = size -> DX:AX = header; CF if no block is large
; enough. ES changed.
arena_alloc:
        push si
        push di
        mov ax, [pool_lin]
        mov dx, [pool_lin+2]
.l:     cmp ax, [pool_last]
        jne .m
        cmp dx, [pool_last+2]
        je .fail
.m:     call hdr_map
        test word [es:A_FLAGS], 1
        jnz .n
        cmp [es:A_SIZE+2], cx
        ja .fit
        jb .n
        cmp [es:A_SIZE], bx
        jae .fit
.n:     call a_next
        jmp .l
.fit:   ; split if at least a header and 16 bytes remain
        mov si, [es:A_SIZE]
        mov di, [es:A_SIZE+2]
        sub si, bx
        sbb di, cx
        jnz .split
        cmp si, 2 * A_HDR
        jb .take
.split: mov [es:A_SIZE], bx
        mov [es:A_SIZE+2], cx
        push ax
        push dx
        call a_next             ; the new free header
        call hdr_map
        sub si, A_HDR
        sbb di, 0
        mov [es:A_SIZE], si
        mov [es:A_SIZE+2], di
        pop word [es:A_PREV+2]
        pop word [es:A_PREV]
        mov word [es:A_FLAGS], 0
        mov word [es:A_SEL], 0
        mov word [es:A_NSEL], 0
        mov word [es:A_MAGIC], MAGIC
        push word [es:A_PREV]
        push word [es:A_PREV+2]
        mov si, ax
        mov di, dx
        call a_next             ; the one after it points back
        call hdr_map
        mov [es:A_PREV], si
        mov [es:A_PREV+2], di
        pop dx
        pop ax
        call hdr_map
.take:  mov word [es:A_FLAGS], 1
        mov word [es:A_SEL], 0
        mov word [es:A_NSEL], 0
        clc
        jmp .r
.fail:  stc
.r:     pop di
        pop si
        ret

; arena_check: DX:AX = header of a used block? CF if not. ES = it.
arena_check:
        cmp dx, [pool_lin+2]
        jb .f
        ja .c1
        cmp ax, [pool_lin]
        jb .f
.c1:    cmp dx, [pool_last+2]
        ja .f
        jb .c2
        cmp ax, [pool_last]
        jae .f
.c2:    call hdr_map
        cmp word [es:A_MAGIC], MAGIC
        jne .f
        test word [es:A_FLAGS], 1
        jz .f
        clc
        ret
.f:     stc
        ret

; arena_free: DX:AX = header of a used block; merges with free
; neighbours. ES changed.
arena_free:
        push bx
        push cx
        push si
        push di
        call hdr_map
        mov word [es:A_FLAGS], 0
        mov word [es:A_NSEL], 0
        mov si, ax
        mov di, dx
        ; the next one
        call a_next
        call hdr_map
        test word [es:A_FLAGS], 1
        jnz .prev
        mov bx, [es:A_SIZE]
        mov cx, [es:A_SIZE+2]
        add bx, A_HDR
        adc cx, 0
        call a_next             ; the one after that points back to us
        call hdr_map
        mov [es:A_PREV], si
        mov [es:A_PREV+2], di
        mov ax, si
        mov dx, di
        call hdr_map
        add [es:A_SIZE], bx
        adc [es:A_SIZE+2], cx
.prev:  mov ax, si
        mov dx, di
        call hdr_map
        mov ax, [es:A_PREV]
        mov dx, [es:A_PREV+2]
        mov bx, ax
        or bx, dx
        jz .d
        push ax
        push dx
        call hdr_map
        pop dx
        pop ax
        test word [es:A_FLAGS], 1
        jnz .d
        ; merge into the previous one
        push ax
        push dx
        mov ax, si
        mov dx, di
        call hdr_map
        mov bx, [es:A_SIZE]
        mov cx, [es:A_SIZE+2]
        add bx, A_HDR
        adc cx, 0
        call a_next
        call hdr_map
        pop word [es:A_PREV+2]
        pop word [es:A_PREV]
        mov ax, [es:A_PREV]
        mov dx, [es:A_PREV+2]
        call hdr_map
        add [es:A_SIZE], bx
        adc [es:A_SIZE+2], cx
.d:     pop di
        pop si
        pop cx
        pop bx
        ret

; arena_largest: DX:AX = the largest free block's size. ES changed.
arena_largest:
        push bx
        push cx
        xor bx, bx
        xor cx, cx
        mov ax, [pool_lin]
        mov dx, [pool_lin+2]
.l:     cmp ax, [pool_last]
        jne .m
        cmp dx, [pool_last+2]
        je .d
.m:     call hdr_map
        test word [es:A_FLAGS], 1
        jnz .n
        cmp [es:A_SIZE+2], cx
        jb .n
        ja .big
        cmp [es:A_SIZE], bx
        jbe .n
.big:   mov bx, [es:A_SIZE]
        mov cx, [es:A_SIZE+2]
.n:     call a_next
        jmp .l
.d:     mov ax, bx
        mov dx, cx
        pop cx
        pop bx
        ret

; arena_resize: DX:AX = header of a used block, CX:BX = the new size ->
; DX:AX = the header (moved if it could not grow in place, when [a_move]
; is set); CF if there is no room
arena_resize:
        push si
        push di
        mov [a_hdr], ax
        mov [a_hdr+2], dx
        mov [a_new], bx
        mov [a_new+2], cx
        call hdr_map
        ; growing?
        cmp cx, [es:A_SIZE+2]
        ja .grow
        jb .shrink
        cmp bx, [es:A_SIZE]
        ja .grow
.shrink:
        ; give back the rest as a free block if it is worth a header
        mov si, [es:A_SIZE]
        mov di, [es:A_SIZE+2]
        sub si, bx
        sbb di, cx
        jnz .cut
        cmp si, 2 * A_HDR
        jb .same
.cut:   mov [es:A_SIZE], bx
        mov [es:A_SIZE+2], cx
        call a_next
        call hdr_map
        sub si, A_HDR
        sbb di, 0
        mov [es:A_SIZE], si
        mov [es:A_SIZE+2], di
        mov si, [a_hdr]
        mov [es:A_PREV], si
        mov si, [a_hdr+2]
        mov [es:A_PREV+2], si
        mov word [es:A_FLAGS], 1        ; freed (and merged) just below
        mov word [es:A_SEL], 0
        mov word [es:A_NSEL], 0
        mov word [es:A_MAGIC], MAGIC
        push ax
        push dx
        call a_next
        call hdr_map
        pop word [es:A_PREV+2]
        pop word [es:A_PREV]
        mov ax, [es:A_PREV]
        mov dx, [es:A_PREV+2]
        call arena_free
.same:  mov ax, [a_hdr]
        mov dx, [a_hdr+2]
        clc
        jmp .r
.grow:  ; the next block free and large enough?
        mov si, [es:A_SIZE]
        mov di, [es:A_SIZE+2]
        call a_next
        cmp ax, [pool_last]
        jne .g1
        cmp dx, [pool_last+2]
        je .move
.g1:    call hdr_map
        test word [es:A_FLAGS], 1
        jnz .move
        add si, [es:A_SIZE]
        adc di, [es:A_SIZE+2]
        add si, A_HDR
        adc di, 0
        cmp di, cx
        jb .move
        ja .absorb
        cmp si, bx
        jb .move
.absorb:
        ; take the next block in, then cut back to the new size
        call a_next
        call hdr_map
        mov bx, [a_hdr]
        mov [es:A_PREV], bx
        mov bx, [a_hdr+2]
        mov [es:A_PREV+2], bx
        mov ax, [a_hdr]
        mov dx, [a_hdr+2]
        call hdr_map
        mov [es:A_SIZE], si
        mov [es:A_SIZE+2], di
        mov bx, [a_new]
        mov cx, [a_new+2]
        jmp .shrink
.move:  cmp byte [a_move], 0
        je .fail
        mov bx, [a_new]
        mov cx, [a_new+2]
        call arena_alloc
        jc .fail
        mov [a_dst], ax
        mov [a_dst+2], dx
        ; copy the old contents
        mov ax, [a_hdr]
        mov dx, [a_hdr+2]
        call hdr_map
        mov cx, [es:A_SIZE]
        mov si, [es:A_SIZE+2]
        add ax, A_HDR
        adc dx, 0
        mov bx, [a_dst]
        mov di, [a_dst+2]
        add bx, A_HDR
        adc di, 0
        call lin_copy
        mov ax, [a_hdr]
        mov dx, [a_hdr+2]
        call arena_free
        mov ax, [a_dst]
        mov dx, [a_dst+2]
        clc
        jmp .r
.fail:  stc
.r:     pop di
        pop si
        ret

; lin_copy: SI:CX bytes from linear DX:AX to linear DI:BX (no overlap
; that matters: the destination is a different block)
lin_copy:
        push ds
        push es
.l:     mov [lc_src], ax
        mov [lc_src+2], dx
        mov [lc_dst], bx
        mov [lc_dst+2], di
        ; this round: at most 8000h bytes
        mov [lc_n], cx
        or si, si
        jnz .big
        cmp cx, 8000h
        jbe .go
.big:   mov word [lc_n], 8000h
.go:    cmp word [lc_n], 0
        je .d
        push cx
        push di
        mov cx, 0FFFFh
        mov di, G_SCR2
        call gdt_base_lin
        mov ax, bx
        mov dx, [lc_dst+2]
        mov di, G_SCR3
        call gdt_base_lin
        pop di
        mov cx, [lc_n]
        push si
        push di
        push ds
        mov ax, G_SCR2
        mov ds, ax
        mov ax, G_SCR3
        mov es, ax
        xor si, si
        xor di, di
        rep movsb
        pop ds
        pop di
        pop si
        pop cx
        mov ax, [lc_n]
        sub cx, ax
        sbb si, 0
        mov ax, [lc_src]
        mov dx, [lc_src+2]
        add ax, [lc_n]
        adc dx, 0
        mov bx, [lc_dst]
        add bx, [lc_n]
        adc di, 0
        jmp .l
.d:     pop es
        pop ds
        ret
