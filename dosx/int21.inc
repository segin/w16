; int21.inc - INT 21h from protected mode (ring 0, from int21_stub).
;
; DOS runs in real mode; DOSX copies what a function points to between
; the client's memory and a transfer buffer in conventional memory
; (tbuf), and gives back selectors where DOS gives back segments.
; Functions: Ralf Brown's/the MS-DOS Programmer's Reference argument
; lists; which ones DOSX translates, and how, follows Microsoft's DOSX
; where observed (DX4; ARCHITECTURE.md 9.5) and is otherwise UNVERIFIED.
; Memory functions 48h-4Ah allocate extended memory (TK18, DX2).
;
; Each handler gets BX = a real-mode structure filled with the client's
; registers (DS = ES = the image segment), BP = the gate frame. With CF
; clear on return, the general registers and the arithmetic flags go
; back to the client from the structure; with CF set the handler has
; set the client's registers itself.

op_int21:
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
        mov al, [bp+F_AX+1]
        xor ah, ah
        mov si, ax
        mov al, [i21class+si]
        shl ax, 1
        mov si, ax
        push ss
        pop es
        call [i21h+si]
        jc .done
        call rs_to_frame
        call ring3sp
        mov ax, [bx+R_FL]
        and ax, FL_ARITH
        and word [es:si+K_FL], ~FL_ARITH
        or [es:si+K_FL], ax
.done:  pop word [tb_top]
        add sp, R_SIZE
        ret

; handler classes
I_PLAIN equ 0
I_DXZ   equ 1           ; DS:DX ASCIIZ in
I_09    equ 2
I_0A    equ 3
I_0C    equ 4
I_FCB   equ 5
I_1A    equ 6
I_2F    equ 7
I_25    equ 8
I_35    equ 9
I_DXSEG equ 10          ; DX = a PSP selector -> segment (26h, 55h)
I_29    equ 11
I_38    equ 12
I_3F    equ 13
I_40    equ 14
I_44    equ 15
I_47    equ 16
I_48    equ 17
I_49    equ 18
I_4A    equ 19
I_4B    equ 20
I_4C    equ 21
I_4E    equ 22
I_4F    equ 23
I_50    equ 24
I_BXSEG equ 25          ; BX segment out -> selector (51h, 62h)
I_ESBX  equ 26          ; ES:BX out (34h, 52h)
I_DSBX  equ 27          ; DS:BX out (1Bh, 1Ch, 1Fh, 32h)
I_56    equ 28
I_5A    equ 29
I_5E    equ 30
I_5F    equ 31
I_60    equ 32
I_DSSI  equ 33          ; DS:SI out (63h)
I_65    equ 34
I_6C    equ 35
I_FCBD  equ 36          ; FCB functions with DTA results (11h, 12h)

i21h:   dw h_plain, h_dxz, h_09, h_0a, h_0c, h_fcb, h_1a, h_2f, h_25
        dw h_35, h_dxseg, h_29, h_38, h_3f, h_40, h_44, h_47, h_48, h_49
        dw h_4a, h_4b, h_4c, h_4e, h_4f, h_50, h_bxseg, h_esbx, h_dsbx
        dw h_56, h_5a, h_5e, h_5f, h_60, h_dssi, h_65, h_6c, h_fcbd

i21class:
%assign n 0
%rep 256
 %if n = 09h
        db I_09
 %elif n = 0Ah
        db I_0A
 %elif n = 0Ch
        db I_0C
 %elif n = 0Fh || n = 10h || n = 13h || n = 14h || n = 15h || n = 16h || n = 17h || n = 21h || n = 22h || n = 23h || n = 24h || n = 27h || n = 28h
        db I_FCB
 %elif n = 11h || n = 12h
        db I_FCBD
 %elif n = 1Ah
        db I_1A
 %elif n = 1Bh || n = 1Ch || n = 1Fh || n = 32h
        db I_DSBX
 %elif n = 25h
        db I_25
 %elif n = 26h || n = 55h
        db I_DXSEG
 %elif n = 29h
        db I_29
 %elif n = 2Fh
        db I_2F
 %elif n = 34h || n = 52h
        db I_ESBX
 %elif n = 35h
        db I_35
 %elif n = 38h
        db I_38
 %elif n = 39h || n = 3Ah || n = 3Bh || n = 3Ch || n = 3Dh || n = 41h || n = 43h || n = 5Bh
        db I_DXZ
 %elif n = 3Fh
        db I_3F
 %elif n = 40h
        db I_40
 %elif n = 44h
        db I_44
 %elif n = 47h
        db I_47
 %elif n = 48h
        db I_48
 %elif n = 49h
        db I_49
 %elif n = 4Ah
        db I_4A
 %elif n = 4Bh
        db I_4B
 %elif n = 4Ch
        db I_4C
 %elif n = 4Eh
        db I_4E
 %elif n = 4Fh
        db I_4F
 %elif n = 50h
        db I_50
 %elif n = 51h || n = 62h
        db I_BXSEG
 %elif n = 56h
        db I_56
 %elif n = 5Ah
        db I_5A
 %elif n = 5Eh
        db I_5E
 %elif n = 5Fh
        db I_5F
 %elif n = 60h
        db I_60
 %elif n = 63h
        db I_DSSI
 %elif n = 65h
        db I_65
 %elif n = 6Ch
        db I_6C
 %else
        db I_PLAIN
 %endif
 %assign n n + 1
%endrep

; dos: the structure BX through real-mode INT 21h
dos:    mov ax, 2100h
        xor cx, cx
        jmp r0_rmcall

; tb_alloc: CX bytes of the transfer buffer -> DI; CF if it has not got
; that much left. tb_top is restored by op_int21.
tb_alloc:
        mov di, [tb_top]
        sub di, cx
        jb .f
        cmp di, tbuf
        jb .f
        mov [tb_top], di
        clc
        ret
.f:     stc
        ret

; tb_left: CX = what is left of the transfer buffer
tb_left:
        mov cx, [tb_top]
        sub cx, tbuf
        ret

; cpin: CX bytes from the client's AX:SI to host DI; CF if AX is not a
; readable selector
cpin:   push ds
        push es
        push si
        push di
        push cx
        verr ax
        jnz .f
        call clamp
        jc .f
        push ss
        pop es
        mov ds, ax
        rep movsb
        clc
        jmp .r
.f:     stc
.r:     pop cx
        pop di
        pop si
        pop es
        pop ds
        ret

; cpout: CX bytes from host SI to the client's AX:DI
cpout:  push ds
        push es
        push si
        push di
        push cx
        verr ax                 ; readable is enough: KERNEL reads a code
        jnz .f                  ; segment from its file with DS = that
        push si                 ; segment (Microsoft's DOSX lets it)
        mov si, di
        call clamp
        pop si
        jc .f
        verw ax
        jz .w
        ; not writable (code): through a data alias of an LDT selector
        test al, 4
        jz .f
        push ax
        push bx
        push dx
        push cx
        mov bx, ax
        call desc_base
        push di
        mov di, G_SCR2
        mov cx, 0FFFFh
        call gdt_base_lin
        pop di
        pop cx
        pop dx
        pop bx
        pop ax
        mov ax, G_SCR2
.w:     mov es, ax
        push ss
        pop ds
        rep movsb
        clc
        jmp .r
.f:     stc
.r:     pop cx
        pop di
        pop si
        pop es
        pop ds
        ret

; clamp: CX = at most what segment AX holds from offset SI; CF if SI is
; beyond its limit
clamp:  push dx
        cmp byte [is386], 0
        je .w16
        ; a 386 limit may be above 64 KB: then every offset is in it
        cpu 386
        push edx
        lsl edx, ax
        jnz .f386
        test edx, 0FFFF0000h
        pop edx
        cpu 286
        jz .w16
        pop dx
        clc
        ret
.f386:  cpu 386
        pop edx
        cpu 286
        jmp .f
.w16:   lsl dx, ax
        jnz .f
        sub dx, si
        jb .f
        inc dx
        jz .ok                  ; 64 KB
        cmp cx, dx
        jbe .ok
        mov cx, dx
.ok:    pop dx
        clc
        ret
.f:     pop dx
        stc
        ret

; cpin_z: an ASCIIZ string from AX:SI to host DI, at most CX bytes
; (always NUL-terminated)
cpin_z: push ds
        push es
        push si
        push di
        push cx
        push ax
        verr ax
        jnz .f
        call clamp
        jc .f
        push ss
        pop es
        mov ds, ax
        dec cx
        jz .end
.l:     lodsb
        stosb
        or al, al
        jz .ok
        loop .l
.end:   mov byte [es:di], 0
.ok:    clc
        jmp .r
.f:     stc
.r:     pop ax
        pop cx
        pop di
        pop si
        pop es
        pop ds
        ret

; zin: ASCIIZ at the client's [F_DS]:reg (offset in AX) into a new
; 128-byte buffer -> DI; CF on a bad selector
zin:    push si
        push cx
        mov si, ax
        mov cx, 128
        call tb_alloc
        jc .r
        mov ax, [bp+F_DS]
        call cpin_z
.r:     pop cx
        pop si
        ret

; DOS error 9 for a bad pointer
bad_ptr:
        mov word [bx+R_EAX], 9
        or word [bx+R_FL], FL_CF
        clc
        ret

h_plain:
        call dos
        clc
        ret

; DS:DX is an ASCIIZ path
h_dxz:  mov ax, [bp+F_DX]
        call zin
        jc bad_ptr
        mov [bx+R_EDX], di
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 09h: a '$'-terminated string
h_09:   mov ax, [bp+F_DS]
        lsl cx, ax
        jnz bad_ptr
        sub cx, [bp+F_DX]
        jb bad_ptr
        inc cx
        push cx
        call tb_left
        pop ax
        cmp cx, ax
        jbe .n
        mov cx, ax
.n:     call tb_alloc
        jc bad_ptr
        mov si, [bp+F_DX]
        mov ax, [bp+F_DS]
        call cpin
        mov [bx+R_EDX], di
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 0Ah: buffered input: [max] [count] [text]
h_0a:   push ds
        mov ds, [bp+F_DS]
        mov si, [bp+F_DX]
        xor cx, cx
        mov cl, [si]
        pop ds
        add cx, 2
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_DS]
        mov si, [bp+F_DX]
        call cpin
        mov [bx+R_EDX], di
        push cx
        push di
        call dos
        pop si
        pop cx
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 0Ch: flush, then a keyboard function; 0Ah needs its buffer
h_0c:   cmp byte [bp+F_AX], 0Ah
        je h_0a
        jmp h_plain

; FCB functions: 44 bytes at DS:DX (an extended FCB at most), in and out
h_fcb:  call fcb_in
        jc bad_ptr
        call dos
        call fcb_out
        clc
        ret
fcb_in: mov cx, 44
        call tb_alloc
        jc .r
        mov [fcb_tb], di
        mov ax, [bp+F_DS]
        mov si, [bp+F_DX]
        call cpin
        mov [bx+R_EDX], di
.r:     ret
fcb_out:
        mov si, [fcb_tb]
        mov cx, 44
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        ret
; 11h, 12h: FCB search, the result in the DTA
h_fcbd: call rm_dta_set
        call fcb_in
        jc bad_ptr
        call dos
        call fcb_out
        mov cx, 44
        call dta_out
        clc
        ret

; 1Ah/2Fh: the DTA is the client's address; DOS's is DOSX's (rm_dta),
; copied for the functions that use it
h_1a:   mov ax, [bp+F_DS]
        mov [dta_sel], ax
        mov ax, [bp+F_DX]
        mov [dta_off], ax
        stc
        ret
h_2f:   mov ax, [dta_sel]
        mov [bp+F_ES], ax
        mov ax, [dta_off]
        mov [bp+F_BX], ax
        stc
        ret

; rm_dta_set: DOS's DTA := rm_dta
rm_dta_set:
        push bx
        push es
        push ss
        pop es
        sub sp, R_SIZE
        mov bx, sp
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        rep stosw
        mov word [bx+R_EAX], 1A00h
        mov word [bx+R_EDX], rm_dta
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        call dos
        add sp, R_SIZE
        pop es
        pop bx
        ret
; dta_out / dta_in: CX bytes between rm_dta and the client's DTA
dta_out:
        mov si, rm_dta
        mov ax, [dta_sel]
        mov di, [dta_off]
        jmp cpout
dta_in: mov di, rm_dta
        mov ax, [dta_sel]
        mov si, [dta_off]
        jmp cpin

; 25h/35h: protected-mode vectors
h_25:   mov bl, [bp+F_AX]
        mov cx, [bp+F_DS]
        mov dx, [bp+F_DX]
        call set_pmvec
        stc
        ret
h_35:   mov si, [bp+F_AX]
        and si, 0FFh
        shl si, 2
        mov ax, [pmvec+si]
        mov [bp+F_BX], ax
        mov ax, [pmvec+si+2]
        mov [bp+F_ES], ax
        stc
        ret

; sel_to_seg: AX = a selector (TI set) -> AX = its base / 16; other
; values unchanged
sel_to_seg:
        test al, 4
        jz .r
        push bx
        push dx
        mov bx, ax
        call desc_base
        mov bx, dx
        shr ax, 4
        shl bl, 4
        or ah, bl
        pop dx
        pop bx
.r:     ret

; 26h, 55h: DX = the new PSP
h_dxseg:
        mov ax, [bp+F_DX]
        call sel_to_seg
        mov [bx+R_EDX], ax
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret
; 50h: BX = the PSP
h_50:   mov ax, [bp+F_BX]
        call sel_to_seg
        mov [bx+R_EBX], ax
        call dos
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        clc
        ret
; 51h, 62h: BX = the PSP's selector (TK18: the PSP's 64 KB data
; selector, as 0002h gives it)
h_bxseg:
        call dos
        mov ax, [bx+R_EBX]
        call segmap
        mov [bx+R_EBX], ax
        clc
        ret
; ES:BX, DS:BX, DS:SI pointers DOS returns
h_esbx: call dos
        mov ax, [bx+R_ES]
        call segmap
        mov [bp+F_ES], ax
        clc
        ret
h_dsbx: call dos
        mov ax, [bx+R_DS]
        call segmap
        mov [bp+F_DS], ax
        clc
        ret
h_dssi: call dos
        mov ax, [bx+R_DS]
        call segmap
        mov [bp+F_DS], ax
        clc
        ret

; 29h: parse a file name at DS:SI into the FCB at ES:DI; SI advances
h_29:   mov cx, 128
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_DS]
        mov si, [bp+F_SI]
        call cpin
        mov [bx+R_ESI], di
        mov [p29_src], di
        mov cx, 37
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_ES]
        mov si, [bp+F_DI]
        call cpin
        mov [bx+R_EDI], di
        push di
        call dos
        pop si
        mov cx, 37
        mov ax, [bp+F_ES]
        mov di, [bp+F_DI]
        call cpout
        mov ax, [bx+R_ESI]
        sub ax, [p29_src]
        add ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        clc
        ret

; 38h: country information (34 bytes at DS:DX) unless DX = FFFFh (set)
h_38:   cmp word [bp+F_DX], 0FFFFh
        je h_plain
        mov cx, 34
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EDX], di
        push di
        call dos
        pop si
        mov cx, 34
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 3Fh: read CX bytes to DS:DX, through the buffer as often as needed
h_3f:   call tb_left
        and cx, 0FFF0h
        call tb_alloc
        mov [rw_buf], di
        mov [rw_size], cx
        mov word [rw_done], 0
.l:     mov cx, [bp+F_CX]
        sub cx, [rw_done]
        jz .end
        cmp cx, [rw_size]
        jbe .n
        mov cx, [rw_size]
.n:     mov [rw_n], cx
        mov word [bx+R_EAX], 3F00h
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov [bx+R_ECX], cx
        mov ax, [rw_buf]
        mov [bx+R_EDX], ax
        call dos
        test word [bx+R_FL], FL_CF
        jnz .err
        mov cx, [bx+R_EAX]
        jcxz .end
        mov si, [rw_buf]
        mov di, [bp+F_DX]
        add di, [rw_done]
        mov ax, [bp+F_DS]
        call cpout
        jc .badp
        add [rw_done], cx
        cmp cx, [rw_n]
        je .l
.end:   mov ax, [rw_done]
        mov [bx+R_EAX], ax
        and word [bx+R_FL], ~FL_CF
        jmp .ret
.err:   cmp word [rw_done], 0
        jne .end
        jmp .ret
.badp:  mov word [bx+R_EAX], 5
        or word [bx+R_FL], FL_CF
.ret:   mov ax, [bp+F_CX]
        mov [bx+R_ECX], ax
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 40h: write CX bytes from DS:DX (CX = 0: truncate, one call)
h_40:   cmp word [bp+F_CX], 0
        je h_plain
        call tb_left
        and cx, 0FFF0h
        call tb_alloc
        mov [rw_buf], di
        mov [rw_size], cx
        mov word [rw_done], 0
.l:     mov cx, [bp+F_CX]
        sub cx, [rw_done]
        jz .end
        cmp cx, [rw_size]
        jbe .n
        mov cx, [rw_size]
.n:     mov [rw_n], cx
        mov si, [bp+F_DX]
        add si, [rw_done]
        mov di, [rw_buf]
        mov ax, [bp+F_DS]
        call cpin
        jc h_3f.badp
        mov word [bx+R_EAX], 4000h
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov [bx+R_ECX], cx
        mov [bx+R_EDX], di
        call dos
        test word [bx+R_FL], FL_CF
        jnz h_3f.err
        mov cx, [bx+R_EAX]
        add [rw_done], cx
        cmp cx, [rw_n]
        je .l
.end:   jmp h_3f.end

; 44h: IOCTL. 02h-05h move CX bytes at DS:DX; 0Ch/0Dh (generic) move
; a 64-byte parameter block (UNVERIFIED size); the rest have no pointer.
h_44:   mov al, [bp+F_AX]
        cmp al, 2
        jb h_plain
        cmp al, 5
        jbe .rw
        cmp al, 0Ch
        je .gen
        cmp al, 0Dh
        je .gen
        jmp h_plain
.gen:   mov word [io_n], 64
        jmp .go
.rw:    mov ax, [bp+F_CX]
        mov [io_n], ax
.go:    mov cx, [io_n]
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_DS]
        mov si, [bp+F_DX]
        call cpin
        mov [bx+R_EDX], di
        push di
        call dos
        pop si
        mov cx, [io_n]
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 47h: the current directory, 64 bytes at DS:SI
h_47:   mov cx, 64
        call tb_alloc
        jc bad_ptr
        mov [bx+R_ESI], di
        push di
        call dos
        pop si
        mov cx, 64
        mov ax, [bp+F_DS]
        mov di, [bp+F_SI]
        call cpout
        mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        clc
        ret

; 48h: BX paragraphs of extended memory -> AX = a selector (several
; consecutive ones, 64 KB each, for a larger block); failure: AX = 8,
; BX = the largest block in paragraphs (TK18, DX2)
h_48:   mov cx, [bp+F_BX]
        or cx, cx
        jz .f
        mov bx, cx
        shl bx, 4
        shr cx, 12              ; CX:BX = bytes
        push cx
        push bx
        call arena_alloc
        pop bx
        pop cx
        jc .f
        mov [m48_hdr], ax
        mov [m48_hdr+2], dx
        ; the number of selectors
        mov si, cx
        mov di, bx
        mov cx, si
        cmp di, 0
        je .t
        inc cx
.t:     call ldt_alloc
        jc .nosel
        mov [m48_sel], ax
        push cx
        mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        call hdr_map
        pop cx
        mov ax, [m48_sel]
        mov [es:A_SEL], ax
        mov [es:A_NSEL], cx
        mov bx, ax
        mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        add ax, A_HDR
        adc dx, 0
        call tiles_set
        mov ax, [m48_sel]
        mov [bp+F_AX], ax
        call clr_cf
        stc
        ret
.nosel: mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        call arena_free
.f:     call arena_largest
        mov cx, 4
.sh:    shr dx, 1
        rcr ax, 1
        loop .sh
        or dx, dx
        jz .m
        mov ax, 0FFFFh
.m:     mov [bp+F_BX], ax
        mov word [bp+F_AX], 8
        call set_cf
        stc
        ret

; m48_find: the frame's ES = a selector from 48h -> DX:AX = its header,
; ES mapped; CF if it is not one
m48_find:
        mov bx, [bp+F_ES]
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
        mov cx, [bp+F_ES]
        or cx, 7
        cmp [es:A_SEL], cx
        jne .f
        clc
        ret
.f:     stc
        ret

; 49h: free the block whose first selector is ES; ES = 0 afterwards
; (DX2 pm49_1001: ES = 0000); AX = 9 for anything else
h_49:   call m48_find
        jc .f
        mov cx, [es:A_NSEL]
        mov bx, [es:A_SEL]
        mov word [es:A_NSEL], 0
        push ax
        push dx
.fs:    call ldt_free
        add bx, 8
        loop .fs
        pop dx
        pop ax
        call arena_free
        mov word [bp+F_ES], 0
        call clr_cf
        stc
        ret
.f:     mov word [bp+F_AX], 9
        call set_cf
        stc
        ret

; 4Ah: resize the block at ES to BX paragraphs, in place (the selectors
; stay; more of them only if the following ones are free). Failure: AX =
; 8, BX = the most it can have. (Microsoft's DOSX faults here: DX5
; F008/F009.)
h_4a:   call m48_find
        jc h_49.f
        mov [m48_hdr], ax
        mov [m48_hdr+2], dx
        mov cx, [bp+F_BX]
        or cx, cx
        jz .fail
        mov bx, cx
        shl bx, 4
        shr cx, 12
        mov si, cx
        cmp bx, 0
        je .t
        inc si                  ; SI = selectors needed
.t:     mov di, [es:A_NSEL]
        cmp si, di
        jbe .size
        ; the extra selectors must be free and follow the block's
        mov ax, [es:A_SEL]
        shr ax, 3
        add ax, di
        mov dx, si
        sub dx, di
.chk:   cmp ax, LDT_COUNT
        jae .fail
        call bm_test
        jnz .fail
        inc ax
        dec dx
        jnz .chk
.size:  push si
        mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        mov byte [a_move], 0
        call arena_resize
        pop si
        jc .fail
        ; claim or free selectors
        call hdr_map
        mov di, [es:A_NSEL]
        mov bx, [es:A_SEL]
        cmp si, di
        je .tiles
        jb .less
        ; claim di .. si-1 out of the chain
        mov ax, bx
        shr ax, 3
        add ax, di
        mov cx, si
        sub cx, di
        call ldt_claim
        jmp .tiles
.less:  push bx
        mov ax, si
        shl ax, 3
        add bx, ax
        mov cx, di
        sub cx, si
.fl:    call ldt_free
        add bx, 8
        loop .fl
        pop bx
.tiles: mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        call hdr_map
        mov [es:A_NSEL], si
        mov cx, si
        mov si, [bp+F_BX]
        mov di, si
        shl di, 4
        shr si, 12
        add ax, A_HDR
        adc dx, 0
        call tiles_set
        call clr_cf
        stc
        ret
.fail:  mov ax, [m48_hdr]
        mov dx, [m48_hdr+2]
        call hdr_map
        mov ax, [es:A_SIZE]
        mov dx, [es:A_SIZE+2]
        mov cx, 4
.sh:    shr dx, 1
        rcr ax, 1
        loop .sh
        mov [bp+F_BX], ax
        mov word [bp+F_AX], 8
        call set_cf
        stc
        ret

; ldt_claim: take CX descriptors from index AX out of the free chain
; (they are known to be free)
ldt_claim:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push es
        mov di, ax
        shl di, 3
        mov dx, cx
        shl dx, 3
        add dx, di
        mov ax, G_LDTA
        mov es, ax
        xor bx, bx
        mov si, [ldt_head]
.ul:    or si, si
        jz .init
        mov ax, [es:si]
        cmp si, di
        jb .keep
        cmp si, dx
        jae .keep
        or bx, bx
        jnz .m
        mov [ldt_head], ax
        jmp .n
.m:     mov [es:bx], ax
.n:     mov si, ax
        jmp .ul
.keep:  mov bx, si
        mov si, ax
        jmp .ul
.init:  mov word [es:di], 0
        mov word [es:di+2], 0
        mov word [es:di+4], 0F200h
        mov word [es:di+6], 0
        mov ax, di
        shr ax, 3
        call bm_set
        add di, 8
        cmp di, dx
        jb .init
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; 4Bh: EXEC. AL = 0 or 1: DS:DX the program, ES:BX the parameter block
; (environment selector, command tail, two FCBs) rebuilt in real-mode
; memory. UNVERIFIED: Microsoft's DOSX's handling.
h_4b:   cmp byte [bp+F_AX], 1
        ja h_dxz
        mov ax, [bp+F_DX]
        call zin
        jc bad_ptr
        mov [bx+R_EDX], di
        mov cx, 14 + 128 + 2 * 37
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EBX], di
        push di
        mov ax, [bp+F_ES]
        mov si, [bp+F_BX]
        mov di, p4b_blk
        mov cx, 14
        call cpin
        pop di
        jc bad_ptr
        mov ax, [p4b_blk]
        call sel_to_seg
        mov [di], ax
        mov si, 2               ; the tail, then the two FCBs
        mov dx, 14
        mov cx, 128
        call .ptr
        mov si, 6
        mov dx, 14 + 128
        mov cx, 37
        call .ptr
        mov si, 10
        mov dx, 14 + 128 + 37
        mov cx, 37
        call .ptr
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        clc
        ret
; the far pointer at p4b_blk+SI: CX bytes to DI+DX, pointer replaced
.ptr:   push bx
        push di
        mov bx, di
        mov ax, di
        add ax, dx
        mov [bx+si], ax
        mov ax, [imgseg]
        mov [bx+si+2], ax
        mov di, [bx+si]
        mov ax, [p4b_blk+si+2]
        mov si, [p4b_blk+si]
        call cpin
        pop di
        pop bx
        ret

; 4Ch: the client ends
h_4c:   mov al, [bp+F_AX]
        jmp r0_terminate

; 4Eh/4Fh: find first/next, the result in the client's DTA (43 bytes)
h_4e:   call rm_dta_set
        jmp h_dxz_dta
h_4f:   mov cx, 43
        call dta_in
        call rm_dta_set
        call dos
        jmp dta43
h_dxz_dta:
        mov ax, [bp+F_DX]
        call zin
        jc bad_ptr
        mov [bx+R_EDX], di
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
dta43:  mov cx, 43
        call dta_out
        clc
        ret

; 56h: rename DS:DX to ES:DI
h_56:   mov ax, [bp+F_DX]
        call zin
        jc bad_ptr
        mov [bx+R_EDX], di
        mov cx, 128
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_ES]
        mov si, [bp+F_DI]
        call cpin_z
        mov [bx+R_EDI], di
        call dos
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        clc
        ret

; 5Ah: create a temporary file in the directory DS:DX; DOS appends the
; name there
h_5a:   mov cx, 128 + 16
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_DS]
        mov si, [bp+F_DX]
        mov cx, 128
        call cpin_z
        mov [bx+R_EDX], di
        push di
        call dos
        pop si
        test word [bx+R_FL], FL_CF
        jnz .r
        ; the length of the path DOS completed
        push si
        xor cx, cx
.len:   inc cx
        cmp byte [si], 0
        je .gl
        inc si
        jmp .len
.gl:    pop si
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
.r:     mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 5Eh 00h: the machine name, 16 bytes at DS:DX
h_5e:   cmp byte [bp+F_AX], 0
        jne h_plain
        mov cx, 16
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EDX], di
        push di
        call dos
        pop si
        mov cx, 16
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 5Fh: 02h (DS:SI 16 bytes out, ES:DI 128 out), 03h (DS:SI and ES:DI
; strings in), 04h (DS:SI in)
h_5f:   mov al, [bp+F_AX]
        cmp al, 2
        jb h_plain
        cmp al, 4
        ja h_plain
        mov cx, 128
        call tb_alloc
        jc bad_ptr
        mov [bx+R_ESI], di
        mov [p5f_si], di
        mov ax, [bp+F_DS]
        mov si, [bp+F_SI]
        cmp byte [bp+F_AX], 2
        je .n1
        call cpin_z
.n1:    mov cx, 128
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EDI], di
        mov [p5f_di], di
        mov ax, [bp+F_ES]
        mov si, [bp+F_DI]
        cmp byte [bp+F_AX], 3
        jne .n2
        call cpin
.n2:    call dos
        cmp byte [bp+F_AX], 2
        jne .r
        mov si, [p5f_si]
        mov cx, 16
        mov ax, [bp+F_DS]
        mov di, [bp+F_SI]
        call cpout
        mov si, [p5f_di]
        mov cx, 128
        mov ax, [bp+F_ES]
        mov di, [bp+F_DI]
        call cpout
.r:     mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        clc
        ret

; 60h: canonical name of DS:SI into ES:DI (128 bytes)
h_60:   mov ax, [bp+F_SI]
        call zin
        jc bad_ptr
        mov [bx+R_ESI], di
        mov cx, 128
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EDI], di
        push di
        call dos
        pop si
        mov cx, 128
        mov ax, [bp+F_ES]
        mov di, [bp+F_DI]
        call cpout
        mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        clc
        ret

; 65h: extended country information. 01h-07h: CX bytes to ES:DI; 21h:
; CX bytes at DS:DX in place; 22h: an ASCIIZ string at DS:DX in place
h_65:   mov al, [bp+F_AX]
        cmp al, 1
        jb h_plain
        cmp al, 7
        jbe .info
        cmp al, 21h
        je .str
        cmp al, 22h
        je .z
        jmp h_plain
.info:  mov cx, [bp+F_CX]
        call tb_alloc
        jc bad_ptr
        mov [bx+R_EDI], di
        push di
        call dos
        pop si
        mov cx, [bp+F_CX]
        mov ax, [bp+F_ES]
        mov di, [bp+F_DI]
        call cpout
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        clc
        ret
.z:     mov ax, [bp+F_DX]
        call zin
        jc bad_ptr
        push di
        mov cx, 0
.zl:    inc cx
        cmp byte [di], 0
        je .zd
        inc di
        jmp .zl
.zd:    pop di
        jmp .do
.str:   mov cx, [bp+F_CX]
        call tb_alloc
        jc bad_ptr
        mov ax, [bp+F_DS]
        mov si, [bp+F_DX]
        call cpin
.do:    mov [bx+R_EDX], di
        push cx
        push di
        call dos
        pop si
        pop cx
        mov ax, [bp+F_DS]
        mov di, [bp+F_DX]
        call cpout
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        clc
        ret

; 6Ch: extended open, DS:SI the name
h_6c:   mov ax, [bp+F_SI]
        call zin
        jc bad_ptr
        mov [bx+R_ESI], di
        call dos
        mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        clc
        ret
