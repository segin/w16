; intbios.inc - INT 13h, 25h/26h and 33h from protected mode (ring 0),
; each with buffers copied through the transfer buffer, as Microsoft's
; DOSX's own handlers for these vectors do (DX1: they are not the plain
; reflection stubs; DX8: 13h AH=02h and 25h copy 512 bytes per sector
; back, 33h 16h copies the state, 10h/15h as int10.inc).

; bios_begin: the real-mode structure for the frame's registers on the
; stack (BX), flags from the client, DS = ES = image
%macro BIOS_BEGIN 0
        call st_save
        sub sp, R_SIZE
        mov bx, sp
        call rs_from_frame
        call ring3sp
        mov ax, [es:si+K_FL]
        mov [bx+R_FL], ax
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov [bx+R_ES], ax
        push word [tb_top]
        push ss
        pop es
%endmacro
%macro BIOS_END 0
        call rs_to_frame
        call ring3sp
        mov ax, [bx+R_FL]
        and ax, FL_ARITH
        and word [es:si+K_FL], ~FL_ARITH
        or [es:si+K_FL], ax
        pop word [tb_top]
        add sp, R_SIZE
        jmp st_restore
%endmacro

; bios_int: the structure BX through real-mode INT [bios_vec]
bios_int:
        mov ah, [bios_vec]
        mov al, 0
        xor cx, cx
        jmp r0_rmcall

; bios_scr: the structure BX through real-mode INT [bios_vec] with the
; registers DX names (bit 0 BX, 1 SI, 2 DI) pointing into an empty
; buffer, all that is left of the transfer buffer (at least 1 KB), with
; DS = ES = the image; the client's values back in them afterwards. For
; the BIOS functions with buffers that Microsoft's DOSX passes on as they
; are, so that the BIOS writes into low memory at the selector's value
; read as a segment and the client's buffer stays as it was (DX9 13:
; INT 10h 4F00h, 4F01h, 1C01h); here into the empty buffer, not low
; memory or DOSX's image (w16's DOSX gave the BIOS its image with the
; client's offset, and INT 10h 1C01h overwrote it, DX9 13).
bios_scr:
        push dx
        call tb_left
        cmp cx, 400h
        pop dx
        jb .r
        call tb_alloc
        push di
        push cx
        shr cx, 1
        xor ax, ax
        rep stosw               ; (ES = SS = host data)
        pop cx
        pop di
        test dl, 1
        jz .n1
        mov [bx+R_EBX], di
.n1:    test dl, 2
        jz .n2
        mov [bx+R_ESI], di
.n2:    test dl, 4
        jz .n3
        mov [bx+R_EDI], di
.n3:    call bios_int
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
.r:     ret

; op_int13: AH=02h/03h (CHS read/write, AL sectors at ES:BX) through the
; transfer buffer in pieces: Microsoft's DOSX takes 1-81h sectors in one
; call (DX9 0E; w16's refused more than 8 sectors, and wrapped its 16-bit
; byte count at 128, the BIOS then writing 64 KB over DOSX's image), each
; piece a BIOS call of its own within one track, the next piece's
; cylinder, head and sector from the drive's geometry (INT 13h AH=08h;
; without it the sector number in CL goes on, up to 63). (Of more than 32
; sectors Microsoft's DOSX gives the first 32 right, then sectors from
; elsewhere (DX9 0E s32); w16's gives the disk's.) The client's buffer is
; reached by its linear address, as INT 21h 3Fh (rw_setup). 0Ah/0Bh (long
; read/write, ES:BX) and 42h-44h, 47h and 48h (a packet at DS:SI) get an
; empty buffer (bios_scr; UNVERIFIED what Microsoft's DOSX does with
; them: DOSBox has no such BIOS); the others go as they are (08h's ES:DI
; is not translated: DX8).
op_int13:
        BIOS_BEGIN
        mov byte [bios_vec], 13h
        mov ax, [bp+F_AX]
        cmp ah, 2
        je .rw
        cmp ah, 3
        je .rw
        mov dl, 1               ; ES:BX
        cmp ah, 0Ah
        je .scr
        cmp ah, 0Bh
        je .scr
        mov dl, 2               ; DS:SI
        cmp ah, 42h
        jb .plain
        cmp ah, 44h
        jbe .scr
        cmp ah, 47h
        je .scr
        cmp ah, 48h
        je .scr
.plain: call bios_int
        jmp .done
.scr:   call bios_scr
        jmp .done
.rw:    call c13_geom
        mov ax, [bp+F_AX]
        xor ah, ah
        mov [abs_n], ax         ; sectors to go
        mov word [abs_k], 0     ; sectors done
        mov word [abs_ss], 512
        mov cx, 9               ; SI:CX = AL * 512 bytes
        xor si, si
        mov dx, ax
.sh:    shl ax, 1
        rcl si, 1
        loop .sh
        mov cx, ax
        mov ax, [bp+F_ES]
        mov dx, [bp+F_BX]
        call cl_setup
        jc .bad
        call tb_left
        shr cx, 9               ; sectors a piece
        jz .bad
        mov [abs_tb], cx
        mov ax, [tb_top]
        mov [abs_big], ax       ; (the transfer buffer's top, for every piece)
.lp:    mov ax, [abs_big]
        mov [tb_top], ax
        mov cx, [abs_n]
        cmp cx, [abs_tb]
        jbe .k
        mov cx, [abs_tb]
.k:     cmp word [c13_spt], 0   ; no further than the track's end
        je .k1
        mov ax, [c13_spt]
        sub ax, [c13_sec]
        inc ax
        jbe .nosec0
        cmp cx, ax
        jbe .k1
        mov cx, ax
.k1:    push cx                 ; this piece's sectors
        shl cx, 9
        call tb_alloc
        mov [rw_buf], di
        cmp byte [bp+F_AX+1], 3
        jne .rd
        mov al, 1
        call rw_xfer            ; the client's sectors in
.rd:    pop ax
        push ax
        mov ah, [bp+F_AX+1]
        mov [bx+R_EAX], ax
        mov [bx+R_EBX], di
        mov ax, [c13_sec]       ; the piece's cylinder, head, sector
        cmp ax, 3Fh
        ja .nosec
        mov dx, [c13_cyl]
        xchg dl, dh
        ror dl, 1
        ror dl, 1
        and dl, 0C0h
        or al, dl
        mov ah, dh
        mov [bx+R_ECX], ax
        mov al, [c13_head]
        mov [bx+R_EDX+1], al
        call bios_int
        pop cx
        test word [bx+R_FL], FL_CF
        jnz .fail
        cmp byte [bp+F_AX+1], 2
        jne .wr
        push cx
        shl cx, 9
        xor al, al
        call rw_xfer            ; the sectors out
        pop cx
.wr:    add [abs_k], cx
        sub [abs_n], cx
        add [c13_sec], cx       ; the next piece's place
        mov ax, [c13_spt]
        or ax, ax
        jz .adv
        cmp [c13_sec], ax
        jbe .adv
        mov word [c13_sec], 1
        mov al, [c13_head]
        inc al
        mov [c13_head], al
        cmp al, [c13_hmax]
        jbe .adv
        mov byte [c13_head], 0
        inc word [c13_cyl]
.adv:   shl cx, 9
        add [rw_lin], cx
        adc word [rw_lin+2], 0
        cmp word [abs_n], 0
        jne .lp
        mov al, [abs_k]         ; AL = the sectors done, AH = 0
        xor ah, ah
        mov [bx+R_EAX], ax
        jmp .back
.nosec0:
        push cx
.nosec: pop cx
        mov word [bx+R_EAX], 0400h      ; sector not found
        or word [bx+R_FL], FL_CF
        jmp .back
.fail:  mov al, [abs_k]         ; AH = the BIOS's error, AL = the
        mov [bx+R_EAX], al      ; sectors done before
        jmp .back
.bad:   ; a selector that is not there, or a buffer over DOSX's memory:
        ; "DMA boundary" (as for more than the transfer buffer before;
        ; UNVERIFIED what Microsoft's DOSX does)
        mov word [bx+R_EAX], 0900h
        or word [bx+R_FL], FL_CF
.back:  mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov ax, [bp+F_CX]
        mov [bx+R_ECX], ax
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
.done:  BIOS_END

; c13_geom: the request's cylinder, head and sector (the frame's CX, DH)
; into c13_cyl, c13_head, c13_sec, and drive DL's geometry: c13_spt the
; sectors per track, c13_hmax the last head (INT 13h AH=08h; c13_spt 0
; if the BIOS does not say)
c13_geom:
        mov ax, [bp+F_CX]
        mov dl, al
        and ax, 3Fh
        mov [c13_sec], ax
        mov ax, [bp+F_CX]
        rol al, 1
        rol al, 1
        and al, 3
        xchg al, ah
        mov [c13_cyl], ax
        mov al, [bp+F_DX+1]
        mov [c13_head], al
        mov word [c13_spt], 0
        push bx
        sub sp, R_SIZE
        mov bx, sp
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        rep stosw
        mov word [bx+R_EAX], 0800h
        mov al, [bp+F_DX]
        mov [bx+R_EDX], al
        mov word [bx+R_FL], 0202h
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov [bx+R_ES], ax
        call bios_int
        test word [bx+R_FL], FL_CF
        jnz .d
        mov ax, [bx+R_ECX]
        and ax, 3Fh
        jz .d
        mov [c13_spt], ax
        mov al, [bx+R_EDX+1]
        mov [c13_hmax], al
.d:     add sp, R_SIZE
        pop bx
        ret

; op_int2526: absolute disk read (25h) or write (26h): DS:BX the buffer,
; CX sectors from sector DX (or CX = FFFFh and DS:BX a packet: dword
; sector, word count, dword buffer). The stub returns with RETF, leaving
; the flags on the stack, as INT 25h/26h do. In pieces of the transfer
; buffer, each a DOS call of its own (Microsoft's DOSX reads 80h sectors
; in one call, DX9 17; w16's refused more than 8, and wrapped its 16-bit
; byte count at 128, so that DOS wrote 64 KB over DOSX's image), of the
; drive's sector size (IOCTL 440Dh 0860h's default BPB: a RAM drive's
; may be 1024 bytes; 512 when DOS does not say). The client's buffer is
; reached by its linear address, as INT 21h 3Fh (rw_setup). After a
; transfer that worked AX is the client's as it was, as Microsoft's DOSX
; leaves it (DX9 0F, 17: DOS's AX is not passed back then); after one
; that failed, DOS's error code.
op_int2526:
        BIOS_BEGIN
        call ring3sp
        mov ax, [es:si+K_ARG]   ; 25h or 26h
        push ss
        pop es
        mov [bios_vec], al
        mov al, [bp+F_AX]       ; the drive (0 = A:), DOS's 1 = A:
        inc al
        call abs_secsize
        cmp word [bp+F_CX], 0FFFFh
        je .pkt
        mov word [abs_big], 0
        mov ax, [bp+F_CX]
        mov [abs_n], ax
        mov ax, [bp+F_DX]
        mov [abs_sec], ax
        mov word [abs_sec+2], 0
        mov ax, [bp+F_DS]
        mov dx, [bp+F_BX]
        jmp .go
.pkt:   ; the packet: 10 bytes
        mov word [abs_big], 1
        mov cx, 10
        call tb_alloc
        jc .err
        mov ax, [bp+F_DS]
        mov si, [bp+F_BX]
        call cpin
        jc .err
        cmp cx, 10
        jne .err
        mov si, di
        mov ax, [si]
        mov [abs_sec], ax
        mov ax, [si+2]
        mov [abs_sec+2], ax
        mov ax, [si+4]
        mov [abs_n], ax
        mov dx, [si+6]
        mov ax, [si+8]
.go:    ; the client's buffer, abs_n * abs_ss bytes
        push ax
        push dx
        mov ax, [abs_n]
        mul word [abs_ss]
        mov cx, ax
        mov si, dx
        pop dx
        pop ax
        call cl_setup
        jc .err
        ; sectors a piece: the transfer buffer less a packet's room
        call tb_left
        sub cx, 16
        jb .err
        mov ax, cx
        xor dx, dx
        div word [abs_ss]
        or ax, ax
        jz .err
        mov [abs_k], ax
        mov ax, [tb_top]
        mov [abs_pkt], ax       ; (the transfer buffer's top, for every piece)
.lp:    mov ax, [abs_pkt]
        mov [tb_top], ax
        mov cx, [abs_n]
        cmp cx, [abs_k]
        jbe .k
        mov cx, [abs_k]
.k:     push cx                 ; this piece's sectors
        mov ax, cx
        mul word [abs_ss]
        mov cx, ax
        call tb_alloc
        mov [rw_buf], di
        mov [abs_tb], di
        cmp byte [bios_vec], 26h
        jne .rd
        mov al, 1
        call rw_xfer            ; the client's sectors in
.rd:    pop cx
        push cx
        mov al, [bp+F_AX]       ; the drive
        mov [bx+R_EAX], al
        cmp word [abs_big], 0
        jne .rpk
        mov [bx+R_ECX], cx
        mov ax, [abs_sec]
        mov [bx+R_EDX], ax
        mov [bx+R_EBX], di
        jmp .call
.rpk:   push cx
        mov cx, 10
        call tb_alloc
        pop cx
        mov ax, [abs_sec]
        mov [di], ax
        mov ax, [abs_sec+2]
        mov [di+2], ax
        mov [di+4], cx
        mov ax, [abs_tb]
        mov [di+6], ax
        mov ax, [imgseg]
        mov [di+8], ax
        mov word [bx+R_ECX], 0FFFFh
        mov [bx+R_EBX], di
.call:  call bios_int
        pop cx
        test word [bx+R_FL], FL_CF
        jnz .back               ; DOS's error in AX
        cmp byte [bios_vec], 25h
        jne .wr
        push cx
        mov ax, cx
        mul word [abs_ss]
        mov cx, ax
        xor al, al
        call rw_xfer            ; the sectors out
        pop cx
.wr:    add [abs_sec], cx
        adc word [abs_sec+2], 0
        sub [abs_n], cx
        mov ax, cx
        mul word [abs_ss]
        add [rw_lin], ax
        adc [rw_lin+2], dx
        cmp word [abs_n], 0
        jne .lp
        mov ax, [bp+F_AX]       ; it worked: the client's AX
        mov [bx+R_EAX], ax
        jmp .back
.err:   mov word [bx+R_EAX], 0408h      ; sector not found
        or word [bx+R_FL], FL_CF
.back:  mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov ax, [bp+F_CX]
        mov [bx+R_ECX], ax
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        BIOS_END

; abs_secsize: [abs_ss] = the sector size of drive AL (1 = A:, 0 the
; current one):
; INT 21h 440Dh 0860h (get device parameters, the default BPB: no disk
; access) in real mode; 512 if DOS says nothing usable (128 to 2048, a
; power of 2). The transfer buffer is used and given back.
abs_secsize:
        push bx
        push dx
        mov dl, al
        push word [tb_top]
        mov word [abs_ss], 512
        sub sp, R_SIZE
        mov bx, sp
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        rep stosw
        mov cx, 64
        call tb_alloc
        jc .d
        push di
        mov cx, 32
        xor ax, ax
        rep stosw
        pop di
        mov word [bx+R_EAX], 440Dh
        mov word [bx+R_ECX], 0860h
        mov [bx+R_EBX], dl
        mov [bx+R_EDX], di
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov [bx+R_ES], ax
        mov word [bx+R_FL], 0202h
        push di
        call dos
        pop di
        test word [bx+R_FL], FL_CF
        jnz .d
        mov ax, [di+7]          ; the BPB's bytes per sector
        mov cx, 128
.p2:    cmp ax, cx
        je .ok
        shl cx, 1
        cmp cx, 800h
        jbe .p2
        jmp .d
.ok:    mov [abs_ss], ax
.d:     add sp, R_SIZE
        pop word [tb_top]
        pop dx
        pop bx
        ret

; op_int33: the mouse driver. 09h: the cursor (ES:DX, 64 bytes in);
; 16h/17h: the driver's state (ES:DX; its size from 15h); the others
; are reflected as they are (Microsoft's DOSX passes 0Ch's handler
; untranslated: DX8 m0c/m14). A buffer that cannot be copied (a bad
; pointer, or a state larger than the transfer buffer) and the driver is
; not called (it used to get DOSX's image at the client's offset).
op_int33:
        BIOS_BEGIN
        mov byte [bios_vec], 33h
        mov ax, [bp+F_AX]
        cmp ax, 9
        je .cur
        cmp ax, 16h
        je .state
        cmp ax, 17h
        je .state
        call bios_int
        jmp .done
.cur:   mov cx, 64
        call tb_alloc
        jc .done
        mov ax, [bp+F_ES]
        mov si, [bp+F_DX]
        call cpin
        jc .done
        mov [bx+R_EDX], di
        call bios_int
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        jmp .done
.state: ; the size: INT 33h 15h
        push word [bx+R_EAX]
        mov word [bx+R_EAX], 15h
        call bios_int
        pop word [bx+R_EAX]
        mov cx, [bx+R_EBX]
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        jcxz .done
        call tb_alloc
        jc .done
        mov ax, [bp+F_ES]
        mov si, [bp+F_DX]
        cmp word [bp+F_AX], 17h
        jne .s1
        call cpin
        jc .done
.s1:    mov [bx+R_EDX], di
        push cx
        push di
        call bios_int
        pop si
        pop cx
        cmp word [bp+F_AX], 16h
        jne .s2
        mov ax, [bp+F_ES]
        mov di, [bp+F_DX]
        call cpout
.s2:    mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
.done:  BIOS_END
