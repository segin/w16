; int10.inc - INT 10h from protected mode (ring 0, from int10_stub).
;
; The video BIOS functions that take or return a pointer in ES get the
; data copied through the transfer buffer; the rest are reflected as
; they are. Microsoft's DOSX has a protected-mode INT 10h handler of its
; own (DX1: vector 10h does not go to the plain reflection stubs), so it
; translates some of them; which ones exactly is UNVERIFIED. These follow
; the IBM VGA BIOS interface:
;   10h/02h, 10h/09h  ES:DX, 17 bytes (palette registers and overscan)
;   10h/12h, 10h/17h  ES:DX, CX * 3 bytes (DAC registers from BX)
;   11h/00h, 11h/10h  ES:BP, CX * BH bytes (a user font)
;   11h/30h           returns ES:BP (a font in the BIOS): a selector
;   13h               ES:BP, CX characters (* 2 with attributes)
;   1Bh               ES:DI, 64 bytes back (functionality and state)

op_int10:
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
        mov ax, [bp+F_AX]
        cmp ah, 10h
        je .pal
        cmp ah, 11h
        je .font
        cmp ah, 13h
        je .str
        cmp ah, 1Bh
        je .state
.plain: call v10
        jmp .done
.pal:   cmp al, 2
        je .p17in
        cmp al, 9
        je .p17out
        cmp al, 12h
        je .dacin
        cmp al, 17h
        je .dacout
        jmp .plain
.p17in: mov cx, 17
        mov dx, R_EDX
        mov si, F_DX
        call v_in
        jmp .done
.p17out:
        mov cx, 17
        mov dx, R_EDX
        mov si, F_DX
        call v_out
        jmp .done
.dacin: mov cx, [bp+F_CX]
        mov ax, cx
        shl cx, 1
        add cx, ax
        mov dx, R_EDX
        mov si, F_DX
        call v_in
        jmp .done
.dacout:
        mov cx, [bp+F_CX]
        mov ax, cx
        shl cx, 1
        add cx, ax
        mov dx, R_EDX
        mov si, F_DX
        call v_out
        jmp .done
.font:  cmp al, 30h
        je .fontinfo
        test al, 0EFh           ; 00h or 10h
        jnz .plain
        mov al, [bp+F_BX+1]
        xor ah, ah
        mul word [bp+F_CX]
        mov cx, ax
        mov dx, R_EBP
        mov si, F_BP
        call v_in
        jmp .done
.fontinfo:
        call v10
        mov ax, [bx+R_ES]
        call segmap
        mov [bp+F_ES], ax
        jmp .done
.str:   mov cx, [bp+F_CX]
        test byte [bp+F_AX], 2
        jz .s1
        shl cx, 1
.s1:    mov dx, R_EBP
        mov si, F_BP
        call v_in
        jmp .done
.state: mov cx, 64
        mov dx, R_EDI
        mov si, F_DI
        call v_out
        ; the table starts with a far pointer to the static
        ; functionality table: its segment becomes a selector (DX8 v1b:
        ; Microsoft's does so)
        push es
        mov es, [bp+F_ES]
        mov di, [bp+F_DI]
        mov ax, [es:di+2]
        pop es
        call segmap
        push es
        mov es, [bp+F_ES]
        mov [es:di+2], ax
        pop es
.done:  call rs_to_frame
        call ring3sp
        mov ax, [bx+R_FL]
        and ax, FL_ARITH
        and word [es:si+K_FL], ~FL_ARITH
        or [es:si+K_FL], ax
        pop word [tb_top]
        add sp, R_SIZE
        ret

; v10: the structure BX through real-mode INT 10h
v10:    mov ax, 1000h
        xor cx, cx
        jmp r0_rmcall

; v_in: CX bytes from the client's ES:[frame SI] to the transfer buffer,
; the structure's register at DX pointing there, then INT 10h
v_in:   push si
        push dx
        call tb_alloc
        pop dx
        pop si
        jc .r
        push si
        push dx
        mov ax, [bp+F_ES]
        mov si, [bp+si]
        call cpin
        pop dx
        push dx
        mov si, dx
        mov [bx+si], di
        call v10
        pop dx
        pop si
        mov ax, [bp+si]         ; the client's offset back
        mov di, dx
        mov [bx+di], ax
.r:     ret

; v_out: INT 10h with the structure's register at DX pointing to CX
; bytes of the transfer buffer, which then go to the client's ES:[frame
; SI]
v_out:  push si
        push dx
        call tb_alloc
        pop dx
        pop si
        jc .r
        push si
        push dx
        push di
        push cx
        mov si, dx
        mov [bx+si], di
        call v10
        pop cx
        pop si                  ; the buffer
        pop dx
        pop ax                  ; the frame offset
        push si
        mov si, ax
        mov di, [bp+si]
        mov si, dx
        mov [bx+si], di
        pop si
        mov ax, [bp+F_ES]
        call cpout
.r:     ret

; op_int15 (ring 0, from int15_stub): INT 15h from protected mode. AH=C0h
; (system configuration) returns ES:BX pointing at a table in the BIOS:
; ES becomes a selector for it (MOUSE.DRV reads the feature bytes there
; to look for a PS/2 mouse port). Microsoft's DOSX has its own INT 15h
; handler too (DX1: vector 15h); what else it translates is UNVERIFIED.
op_int15:
        sub sp, R_SIZE
        mov bx, sp
        call rs_from_frame
        call ring3sp
        mov ax, [es:si+K_FL]
        mov [bx+R_FL], ax
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov ax, [bp+F_ES]
        mov [bx+R_ES], ax
        push ss
        pop es
        mov ax, 1500h
        xor cx, cx
        call r0_rmcall
        cmp byte [bp+F_AX+1], 0C0h
        jne .d
        test word [bx+R_FL], FL_CF
        jnz .d
        mov ax, [bx+R_ES]
        call segmap
        mov [bp+F_ES], ax
.d:     call rs_to_frame
        call ring3sp
        mov ax, [bx+R_FL]
        and ax, FL_ARITH
        and word [es:si+K_FL], ~FL_ARITH
        or [es:si+K_FL], ax
        add sp, R_SIZE
        ret
