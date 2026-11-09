; intbios.inc - INT 13h, 25h/26h and 33h from protected mode (ring 0),
; each with buffers copied through the transfer buffer, as Microsoft's
; DOSX's own handlers for these vectors do (DX1: they are not the plain
; reflection stubs; DX8: 13h AH=02h and 25h copy 512 bytes per sector
; back, 33h 16h copies the state, 10h/15h as int10.inc).

; bios_begin: the real-mode structure for the frame's registers on the
; stack (BX), flags from the client, DS = ES = image
%macro BIOS_BEGIN 0
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
        ret
%endmacro

; bios_int: the structure BX through real-mode INT [bios_vec]
bios_int:
        mov ah, [bios_vec]
        mov al, 0
        xor cx, cx
        jmp r0_rmcall

; op_int13: AH=02h/03h (CHS read/write, AL sectors at ES:BX)
op_int13:
        BIOS_BEGIN
        mov byte [bios_vec], 13h
        mov ax, [bp+F_AX]
        cmp ah, 2
        je .rw
        cmp ah, 3
        je .rw
        call bios_int           ; (08h's ES:DI is not translated: DX8)
        jmp .done
.rw:    xor ah, ah
        mov cx, ax
        shl cx, 9               ; AL sectors of 512 bytes
        jz .plain
        call tb_alloc
        jc .toobig
        mov [bx+R_EBX], di
        cmp byte [bp+F_AX+1], 3
        jne .rd
        mov ax, [bp+F_ES]
        mov si, [bp+F_BX]
        call cpin
.rd:    push cx
        push di
        call bios_int
        pop si
        pop cx
        cmp byte [bp+F_AX+1], 2
        jne .back
        mov ax, [bp+F_ES]
        mov di, [bp+F_BX]
        call cpout
.back:  mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        jmp .done
.toobig:
        ; more than the transfer buffer: "DMA boundary" (UNVERIFIED what
        ; Microsoft's DOSX does)
        mov word [bx+R_EAX], 0900h
        or word [bx+R_FL], FL_CF
        jmp .done
.plain: call bios_int
.done:  BIOS_END

; op_int2526: absolute disk read (25h) or write (26h): DS:BX the
; buffer, CX sectors (or CX = FFFFh and DS:BX a packet: dword sector,
; word count, dword buffer). The stub returns with RETF, leaving the
; flags on the stack, as INT 25h/26h do. Sectors of 512 bytes
; (UNVERIFIED for other sector sizes).
op_int2526:
        BIOS_BEGIN
        call ring3sp
        mov ax, [es:si+K_ARG]   ; 25h or 26h
        push ss
        pop es
        mov [bios_vec], al
        cmp word [bp+F_CX], 0FFFFh
        je .pkt
        mov cx, [bp+F_CX]
        mov ax, [bp+F_DS]
        mov si, [bp+F_BX]
        call abs_buf
        jc .err
        mov [bx+R_EBX], di
        call bios_int
        call abs_back
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        jmp .done
.pkt:   ; the packet: 10 bytes
        push cx
        mov cx, 10
        call tb_alloc
        pop cx
        jc .err
        mov [abs_pkt], di
        mov ax, [bp+F_DS]
        mov si, [bp+F_BX]
        mov cx, 10
        call cpin
        mov si, [abs_pkt]
        mov cx, [si+4]          ; the count
        mov ax, [si+8]          ; the buffer's selector
        mov si, [si+6]
        call abs_buf
        jc .err
        mov si, [abs_pkt]
        mov [si+6], di
        mov ax, [imgseg]
        mov [si+8], ax
        mov [bx+R_EBX], si
        call bios_int
        call abs_back
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        jmp .done
.err:   mov word [bx+R_EAX], 0408h      ; sector not found
        or word [bx+R_FL], FL_CF
.done:  BIOS_END

; abs_buf: CX sectors from the client's AX:SI -> DI = the transfer
; buffer (copied in for a write); remembers them for abs_back
abs_buf:
        mov [abs_sel], ax
        mov [abs_off], si
        shl cx, 9
        mov [abs_len], cx
        call tb_alloc
        jc .r
        mov [abs_tb], di
        cmp byte [bios_vec], 26h
        jne .ok
        call cpin
.ok:    clc
.r:     ret
abs_back:
        cmp byte [bios_vec], 25h
        jne .r
        test word [bx+R_FL], FL_CF
        jnz .r
        mov si, [abs_tb]
        mov cx, [abs_len]
        mov ax, [abs_sel]
        mov di, [abs_off]
        call cpout
.r:     ret

; op_int33: the mouse driver. 09h: the cursor (ES:DX, 64 bytes in);
; 16h/17h: the driver's state (ES:DX; its size from 15h); the others
; are reflected as they are (Microsoft's DOSX passes 0Ch's handler
; untranslated: DX8 m0c/m14).
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
        jc .plain
        mov ax, [bp+F_ES]
        mov si, [bp+F_DX]
        call cpin
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
        or cx, cx
        jz .plain
        call tb_alloc
        jc .plain
        mov ax, [bp+F_ES]
        mov si, [bp+F_DX]
        cmp word [bp+F_AX], 17h
        jne .s1
        call cpin
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
        jmp .done
.plain: call bios_int
.done:  BIOS_END
