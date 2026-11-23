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
; Sizes that do not fit in 16 bits (CX of 5556h or more DAC registers,
; ...) are refused (nothing called): they wrapped, the BIOS then writing
; past the transfer buffer. 1Ch (save and restore the video state, ES:BX)
; and 4Fh (VESA: 00h, 01h, 09h ES:DI, 04h ES:BX) get an empty buffer
; (bios_scr): Microsoft's DOSX passes them on as they are, the client's
; buffer untouched (DX9 13); w16's gave the BIOS DOSX's own image.

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
        cmp ah, 1Ch
        je .vstate
        cmp ah, 4Fh
        je .vesa
.plain: call v10
        jmp .done
.vstate:
        mov dl, 1               ; ES:BX: 01h, 02h (00h gives the size in BX)
        cmp al, 1
        je .scr
        cmp al, 2
        je .scr
        jmp .plain
.vesa:  mov dl, 4               ; ES:DI
        cmp al, 0
        je .scr
        cmp al, 1
        je .scr
        cmp al, 9
        je .scr
        mov dl, 1               ; ES:BX: 04h's 01h and 02h (00h gives the
        cmp al, 4               ; size in BX)
        jne .plain
        cmp byte [bp+F_DX], 0
        je .plain
.scr:   push word [bios_vec]    ; (an INT 13h, 25h or 33h may be under way)
        mov byte [bios_vec], 10h
        call bios_scr
        pop word [bios_vec]
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
.dacin: mov ax, 3
        mul word [bp+F_CX]
        or dx, dx
        jnz .done
        mov cx, ax
        mov dx, R_EDX
        mov si, F_DX
        call v_in
        jmp .done
.dacout:
        mov ax, 3
        mul word [bp+F_CX]
        or dx, dx
        jnz .done
        mov cx, ax
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
        or dx, dx
        jnz .done
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
        jc .done
.s1:    mov dx, R_EBP
        mov si, F_BP
        call v_in
        jmp .done
.state: mov cx, 64
        call tb_alloc
        jc .done
        mov [bx+R_EDI], di
        push di
        call v10
        pop si
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        ; the table starts with a far pointer to the static
        ; functionality table: its segment becomes a selector (DX8 v1b:
        ; Microsoft's does so), in the transfer buffer before the copy
        ; (w16's patched the client's buffer at ring 0, unchecked)
        cmp byte [bx+R_EAX], 1Bh        ; the BIOS has the function
        jne .st1
        mov ax, [si+2]
        call segmap
        jc .st1
        mov [si+2], ax
.st1:   mov cx, 64
        mov ax, [bp+F_ES]
        mov di, [bp+F_DI]
        call cpout
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
        jc .bad
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
.bad:   pop dx                  ; a buffer that cannot be read: no call
        pop si
        ret

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
; to look for a PS/2 mouse port). AH=C207h (set the pointing-device
; handler; MOUSE.DRV gives it a protected-mode ES:BX) gives the BIOS
; ps2_entry instead, which far-calls the handler in protected mode with
; the BIOS's four words. Observed with Microsoft's DOSX (TK47, a handler
; of its own in a block below 1 MB given to C207h): it runs in protected
; mode, on a stack that is neither the caller's nor the block, with the
; BIOS's words (the last pushed first: 0, Y, X, status 08h). Passed on
; as it was (w16 before), the BIOS called the selector:offset as a
; real-mode address once the mouse was enabled and input stopped.
; Microsoft's DOSX has its own INT 15h handler too (DX1: vector 15h);
; what else it translates is UNVERIFIED.
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
        ; C207h (set the pointing-device handler): ES:BX is a
        ; protected-mode address; the BIOS gets ps2_entry, which calls it
        ; (0:0, no handler, goes as it is)
        cmp word [bp+F_AX], 0C207h
        jne .call
        mov ax, [bp+F_BX]
        or ax, [bp+F_ES]
        jz .call
        mov word [bx+R_EBX], ps2_entry
        mov ax, [imgseg]
        mov [bx+R_ES], ax
.call:  push ss
        pop es
        mov ax, 1500h
        xor cx, cx
        call r0_rmcall
        cmp word [bp+F_AX], 0C207h
        jne .nb
        mov ax, [bp+F_BX]               ; the client's BX back
        mov [bx+R_EBX], ax
        ; the client's handler, once the BIOS has taken ps2_entry (one
        ; it refused leaves the one before)
        test word [bx+R_FL], FL_CF
        jnz .nb
        mov [ps2_pm], ax
        mov ax, [bp+F_ES]
        mov [ps2_pm+2], ax
.nb:    cmp byte [bp+F_AX+1], 0C0h
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
