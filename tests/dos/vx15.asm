; vx15.asm - VX15.COM: what a DOS program in a DOS box gets from the XMS
; and EMS drivers (V86MMGR, ARCHITECTURE.md 12.13), run in a box under
; Windows 3.1's WIN386 and w16's (tests/v86mmrun.sh, PIFs with various
; XMS and EMS settings). One line per call, to the screen and VX15.LOG in
; the current directory (appended):
;   X tag ax=.... bx=.... dx=....    an XMS call (the driver's far entry)
;   E tag ax=.... bx=.... cx=.... dx=....    an INT 67h call
; Values that depend on where things are (the entry's address, handles,
; physical addresses) are left out or shown as nonzero/zero only. With a
; command tail starting with Q only the version and free-memory calls are
; made. Black box (ARCHITECTURE.md 3): the published XMS 3.0 and LIM EMS
; 4.0 specifications' functions and the BIOS's INT 12h.
        bits 16
        cpu 386
        org 100h

start:  mov dx, logname
        mov ax, 3D02h
        int 21h
        jnc .open
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .nolog
.open:  mov [logh], ax
        mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
.nolog: mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        or al, 20h
        mov [quick], al
        mov si, m_start
        call puts
        int 12h
        mov si, m_int12
        call puts
        call hex4
        call crlf
        call xms
        call ems
        mov si, m_end
        call puts
        mov bx, [logh]
        cmp bx, -1
        je .x
        mov ah, 3Eh
        int 21h
.x:     mov ax, 4C00h
        int 21h

; ---------------------------------------------------------------- XMS
xms:    mov ax, 4300h
        int 2Fh
        mov si, m_xinst
        call puts
        call hex2
        call crlf
        cmp al, 80h
        je .there
        ret
.there: mov ax, 4310h
        int 2Fh
        mov [xent], bx
        mov [xent + 2], es
        push cs
        pop es
        mov si, m_x00
        mov ah, 00h
        call xc
        mov [xver], ax
        mov si, m_x08
        mov ah, 08h
        xor bl, bl
        call xc
        cmp byte [xver + 1], 3
        jb .no3
        mov si, m_x88
        call puts
        mov ah, 88h
        xor bl, bl
        call far [xent]
        call hex8
        call sp1
        mov eax, edx
        call hex8
        call sp1
        mov eax, ecx
        call hex8
        call sp1
        mov al, bl
        call hex2
        call crlf
.no3:   cmp byte [quick], 'q'
        jne .full
        ret
.full:  ; the HMA
        mov si, m_x01
        mov ah, 01h
        mov dx, 0FFFFh
        call xc
        mov si, m_x01b
        mov ah, 01h
        mov dx, 0FFFFh
        call xc
        mov si, m_x02
        mov ah, 02h
        call xc
        mov si, m_x02b
        mov ah, 02h
        call xc
        ; A20
        mov si, m_x07
        mov ah, 07h
        call xc
        mov si, m_x05
        mov ah, 05h
        call xc
        mov si, m_x07
        mov ah, 07h
        call xc
        mov si, m_x06
        mov ah, 06h
        call xc
        mov si, m_x07
        mov ah, 07h
        call xc
        mov si, m_x03
        mov ah, 03h
        call xc
        mov si, m_x07
        mov ah, 07h
        call xc
        mov si, m_x04
        mov ah, 04h
        call xc
        mov si, m_x07
        mov ah, 07h
        call xc
        ; a block of 64 KB
        mov si, m_x09
        mov ah, 09h
        mov dx, 64
        call xch
        mov [hnd], dx
        test ax, ax
        jz near .nomem
        mov si, m_x0e
        mov ah, 0Eh
        mov dx, [hnd]
        call xc
        ; moves: 512 bytes of a pattern to the block at 1000h and back
        mov cx, 512
        mov di, buf1
        xor al, al
.pat:   stosb
        add al, 37
        loop .pat
        mov dword [mv_len], 512
        mov word [mv_sh], 0
        mov word [mv_so], buf1
        mov [mv_so + 2], cs
        mov ax, [hnd]
        mov [mv_dh], ax
        mov dword [mv_do], 1000h
        mov si, m_x0b1
        mov ah, 0Bh
        push si
        mov si, mv_len
        call xcs
        pop si
        call xpr
        mov ax, [hnd]
        mov [mv_sh], ax
        mov dword [mv_so], 1000h
        mov word [mv_dh], 0
        mov word [mv_do], buf2
        mov [mv_do + 2], cs
        mov si, mv_len
        mov ah, 0Bh
        call xcs
        mov si, m_x0b2
        call xpr
        mov si, buf1
        mov di, buf2
        mov cx, 512
        repe cmpsb
        mov si, m_same
        je .same
        mov si, m_diff
.same:  call puts
        ; odd length: an error
        mov dword [mv_len], 511
        mov si, mv_len
        mov ah, 0Bh
        call xcs
        mov si, m_x0b3
        call xpr
        ; past the block's end
        mov dword [mv_len], 512
        mov ax, [hnd]
        mov [mv_sh], ax
        mov dword [mv_so], 0FF00h
        mov si, mv_len
        mov ah, 0Bh
        call xcs
        mov si, m_x0b4
        call xpr
        ; lock (the physical address: nonzero or not), lock count, unlock
        mov si, m_x0c
        call puts
        mov ah, 0Ch
        mov dx, [hnd]
        call far [xent]
        call regs3z
        mov si, m_x0e
        mov ah, 0Eh
        mov dx, [hnd]
        call xc
        mov si, m_x0d
        mov ah, 0Dh
        mov dx, [hnd]
        call xch
        mov si, m_x0d2
        mov ah, 0Dh
        mov dx, [hnd]
        call xch
        ; resize, free, free again
        mov si, m_x0f
        mov ah, 0Fh
        mov bx, 128
        mov dx, [hnd]
        call xch
        mov si, m_x0e
        mov ah, 0Eh
        mov dx, [hnd]
        call xc
        mov si, m_x08
        mov ah, 08h
        call xc
        mov si, m_x0a
        mov ah, 0Ah
        mov dx, [hnd]
        call xch
        mov si, m_x0a2
        mov ah, 0Ah
        mov dx, [hnd]
        call xch
.nomem: ; too much; a block of 0 KB
        mov si, m_x09b
        mov ah, 09h
        mov dx, 0FFFFh
        call xch
        mov si, m_x09z
        mov ah, 09h
        xor dx, dx
        call xch
        test ax, ax
        jz .nz
        mov ah, 0Ah
        call far [xent]
.nz:    ; 1 KB blocks until it fails: how many handles
        xor bp, bp
.many:  mov ah, 09h
        mov dx, 1
        call far [xent]
        test ax, ax
        jz .mend
        mov di, bp
        shl di, 1
        mov [hnds + di], dx
        inc bp
        cmp bp, 200
        jb .many
        xor bl, bl
.mend:  mov si, m_xmany
        call puts
        mov ax, bp
        call hex4
        mov si, m_bl
        call puts
        mov al, bl
        call hex2
        call crlf
.mfree: test bp, bp
        jz .mfd
        dec bp
        mov di, bp
        shl di, 1
        mov dx, [hnds + di]
        mov ah, 0Ah
        call far [xent]
        jmp .mfree
.mfd:   ; upper memory blocks
        mov si, m_x10
        mov ah, 10h
        mov dx, 0FFFFh
        call xc
        mov si, m_x08
        mov ah, 08h
        xor bl, bl
        call xc
        ret

; XMS call AH, BX, DX; prints SI's tag, AX BX DX (BL when AX is 0)
xc:     push si
        call far [xent]
        pop si
xpr:    push ax
        call puts
        pop ax
        call regs3
        ret
; the same, DX a handle on return of 09h (shown as nonzero/zero)
xch:    push si
        call far [xent]
        pop si
        push dx
        push bx
        push ax
        call puts
        pop ax
        call hex4
        mov si, m_bx
        call puts
        pop ax
        call hex4
        mov si, m_dxnz
        call puts
        pop dx
        mov al, '0'
        test dx, dx
        jz .z
        mov al, '1'
.z:     call putc
        call crlf
        ret
; with DS:SI the move structure
xcs:    call far [xent]
        ret

regs3:  push dx
        push bx
        call hex4
        mov si, m_bx
        call puts
        pop ax
        call hex4
        mov si, m_dx
        call puts
        pop ax
        call hex4
        call crlf
        ret
; AX, BL, and whether DX:BX is nonzero (an address)
regs3z: push bx
        push dx
        call hex4
        mov si, m_bl
        call puts
        pop dx
        pop bx
        push bx
        push dx
        mov al, bl
        call hex2
        mov si, m_addr
        call puts
        pop dx
        pop bx
        or bx, dx
        mov al, '0'
        jz .z
        mov al, '1'
.z:     call putc
        call crlf
        ret

; ---------------------------------------------------------------- EMS
ems:    mov ax, 3567h
        int 21h
        mov di, 0Ah
        mov si, emmname
        mov cx, 8
        repe cmpsb
        mov al, '1'
        je .n1
        mov al, '0'
.n1:    push cs
        pop es
        mov si, m_ename
        call puts
        push ax
        call putc
        call crlf
        pop ax
        cmp al, '1'
        je .there
        ret
.there: xor bx, bx                     ; (BX: the vector's offset, where things are)
        mov si, m_e40
        mov ah, 40h
        call ec
        mov si, m_e41
        mov ah, 41h
        call ec
        mov [frame], bx
        mov si, m_e42
        mov ah, 42h
        call ec
        mov si, m_e46
        mov ah, 46h
        call ec
        mov si, m_e4b
        mov ah, 4Bh
        call ec
        cmp byte [quick], 'q'
        jne .full
        ret
.full:  mov si, m_e43
        mov ah, 43h
        mov bx, 4
        call ech
        mov [ehnd], dx
        test ah, ah
        jnz near .noalloc
        ; map logical 0-3 into physical 0-3, a byte to each, then 3-0
        ; into 0-3 and read them back
        xor bx, bx
.m1:    mov ax, 4400h
        add al, bl
        mov dx, [ehnd]
        push bx
        int 67h
        pop bx
        test ah, ah
        jnz .mbad
        push es
        mov ax, [frame]
        mov cx, bx
        shl cx, 10
        add ax, cx
        mov es, ax
        mov al, bl
        add al, 'A'
        mov [es:0], al
        mov [es:3FFFh], al
        pop es
        inc bx
        cmp bx, 4
        jb .m1
        ; physical page 0 <- logical 3: should show 'D'
        mov ax, 4400h
        mov bx, 3
        mov dx, [ehnd]
        int 67h
        mov si, m_e44
        call epr
        push es
        mov es, [frame]
        mov al, [es:0]
        mov ah, [es:3FFFh]
        pop es
        mov si, m_eread
        call puts
        push ax
        call putc
        pop ax
        mov al, ah
        call putc
        call crlf
        jmp .mok
.mbad:  mov si, m_e44
        call epr
.mok:   ; unmap physical 1 (BX FFFFh), bad logical page, bad physical page
        mov ax, 4401h
        mov bx, 0FFFFh
        mov dx, [ehnd]
        int 67h
        mov si, m_e44u
        call epr
        mov ax, 4400h
        mov bx, 99
        mov dx, [ehnd]
        int 67h
        mov si, m_e44l
        call epr
        mov ax, 4420h
        xor bx, bx
        mov dx, [ehnd]
        int 67h
        mov si, m_e44p
        call epr
        mov si, m_e4c
        mov ah, 4Ch
        mov dx, [ehnd]
        call ec
        mov si, m_e4b
        mov ah, 4Bh
        call ec
        mov si, m_e42
        mov ah, 42h
        call ec
        ; save/restore the map
        mov si, m_e47
        mov ah, 47h
        mov dx, [ehnd]
        call ec
        mov si, m_e47
        mov ah, 47h
        mov dx, [ehnd]
        call ec
        mov si, m_e48
        mov ah, 48h
        mov dx, [ehnd]
        call ec
        ; resize to 8 pages, names
        mov si, m_e51
        mov ah, 51h
        mov bx, 8
        mov dx, [ehnd]
        call ec
        mov si, m_e4c
        mov ah, 4Ch
        mov dx, [ehnd]
        call ec
        mov ax, 5301h
        mov dx, [ehnd]
        mov si, ename
        int 67h
        mov si, m_e5301
        call epr
        mov ax, 5300h
        mov dx, [ehnd]
        mov di, buf2
        int 67h
        mov si, m_e5300
        call epr
        mov si, buf2
        mov cx, 8
.nm:    lodsb
        test al, al
        jz .nmz
        call putc
.nmz:   loop .nm
        call crlf
        ; the mappable physical pages
        mov ax, 5801h
        int 67h
        mov si, m_e5801
        call epr
        mov ax, 5800h
        mov di, buf2
        int 67h
        mov si, m_e5800
        call epr
        mov si, buf2
        mov cx, 12
.ml:    lodsw
        call hex4
        call sp1
        loop .ml
        call crlf
        mov ax, 5901h
        int 67h
        mov si, m_e5901
        call epr
        ; the whole map: its size, get it, map logical 1 at 0, set it back
        mov ax, 4E03h
        int 67h
        mov si, m_e4e3
        call epr
        mov ax, 4E00h
        mov di, mapbuf
        int 67h
        mov si, m_e4e0
        call epr
        mov ax, 4400h
        mov bx, 1
        mov dx, [ehnd]
        int 67h
        mov ax, 4E01h
        mov si, mapbuf
        int 67h
        mov si, m_e4e1
        call epr
        call fr0
        ; map several: logical 2 to physical 0, 0 to 1 (by number)
        mov word [mlist], 2
        mov word [mlist + 2], 0
        mov word [mlist + 4], 0
        mov word [mlist + 6], 1
        mov ax, 5000h
        mov cx, 2
        mov dx, [ehnd]
        mov si, mlist
        int 67h
        mov si, m_e50
        call epr
        call fr0
        ; by segment: logical 1 at the frame's segment
        mov word [mlist], 1
        mov ax, [frame]
        mov [mlist + 2], ax
        mov ax, 5001h
        mov cx, 1
        mov dx, [ehnd]
        mov si, mlist
        int 67h
        mov si, m_e501
        call epr
        call fr0
        ; move 16 bytes from conventional memory to logical page 5, offset
        ; 100h, and back to buf2
        mov dword [mreg], 16
        mov byte [mreg + 4], 0
        mov word [mreg + 5], 0
        mov word [mreg + 7], buf1
        mov [mreg + 9], cs
        mov byte [mreg + 11], 1
        mov ax, [ehnd]
        mov [mreg + 12], ax
        mov word [mreg + 14], 100h
        mov word [mreg + 16], 5
        mov ax, 5700h
        mov si, mreg
        int 67h
        mov si, m_e57a
        call epr
        mov byte [mreg + 4], 1
        mov ax, [ehnd]
        mov [mreg + 5], ax
        mov word [mreg + 7], 100h
        mov word [mreg + 9], 5
        mov byte [mreg + 11], 0
        mov word [mreg + 12], 0
        mov word [mreg + 14], buf2 + 100
        mov [mreg + 16], cs
        mov ax, 5700h
        mov si, mreg
        int 67h
        mov si, m_e57b
        call epr
        mov si, buf1
        mov di, buf2 + 100
        mov cx, 16
        repe cmpsb
        mov si, m_esame
        je .s57
        mov si, m_ediff
.s57:   call puts
        ; attributes, the directory's size, the handle count of the directory
        mov ax, 5200h
        mov dx, [ehnd]
        int 67h
        mov si, m_e520
        call epr
        mov ax, 5402h
        int 67h
        mov si, m_e542
        call epr
        mov ax, 5400h
        mov di, buf2
        int 67h
        mov si, m_e540
        call epr
        ; free, free again
        mov si, m_e45
        mov ah, 45h
        mov dx, [ehnd]
        call ec
        mov si, m_e45b
        mov ah, 45h
        mov dx, [ehnd]
        call ec
.noalloc:
        mov si, m_e43z
        mov ah, 43h
        xor bx, bx
        call ech
        test ah, ah
        jnz .nz
        mov ah, 45h
        int 67h
.nz:    mov si, m_e43b
        mov ah, 43h
        mov bx, 7FFFh
        call ech
        ; how many handles
        xor bp, bp
.many:  mov ah, 43h
        mov bx, 1
        int 67h
        test ah, ah
        jnz .mend
        mov di, bp
        shl di, 1
        mov [hnds + di], dx
        inc bp
        cmp bp, 200
        jb .many
        xor ah, ah
.mend:  mov si, m_emany
        call puts
        mov al, ah
        push ax
        mov ax, bp
        call hex4
        mov si, m_ah
        call puts
        pop ax
        call hex2
        call crlf
.mf:    test bp, bp
        jz .mfd
        dec bp
        mov di, bp
        shl di, 1
        mov dx, [hnds + di]
        mov ah, 45h
        int 67h
        jmp .mf
.mfd:   mov si, m_e42
        mov ah, 42h
        call ec
        ret

; the byte at the frame's start
fr0:    push es
        mov es, [frame]
        mov al, [es:0]
        pop es
        mov si, m_eread
        call puts
        call putc
        call crlf
        ret

; INT 67h with AX/BX/DX; prints SI's tag, AX BX CX DX
ec:     push si
        int 67h
        pop si
epr:    push dx
        push cx
        push bx
        push ax
        call puts
        pop ax
        call hex4
        mov si, m_bx
        call puts
        pop ax
        call hex4
        mov si, m_cx
        call puts
        pop ax
        call hex4
        mov si, m_dx
        call puts
        pop ax
        call hex4
        call crlf
        ret
; 43h: DX a handle (nonzero/zero)
ech:    push si
        int 67h
        pop si
        push dx
        push ax
        call puts
        pop ax
        call hex4
        mov si, m_dxnz
        call puts
        pop dx
        push ax
        mov al, '0'
        test dx, dx
        jz .z
        mov al, '1'
.z:     call putc
        call crlf
        pop ax
        ret

; ---------------------------------------------------------------- output
puts:   push ax
        push bx
        push cx
        push dx
        mov dx, si
        xor cx, cx
.l:     cmp byte [si], 0
        je .w
        inc si
        inc cx
        jmp .l
.w:     jcxz .r
        push cx
        push dx
        mov bx, 1
        mov ah, 40h
        int 21h
        pop dx
        pop cx
        mov bx, [logh]
        cmp bx, -1
        je .r
        mov ah, 40h
        int 21h
.r:     pop dx
        pop cx
        pop bx
        pop ax
        ret

putc:   push si
        mov [cbuf], al
        mov si, cbuf
        call puts
        pop si
        ret

hex2:   push si
        push ax
        mov si, hbuf
        mov ah, al
        shr al, 4
        call .d
        mov al, ah
        and al, 15
        call .d
        mov byte [si], 0
        mov si, hbuf
        call puts
        pop ax
        pop si
        ret
.d:     add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     mov [si], al
        inc si
        ret

hex4:   push ax
        mov al, ah
        call hex2
        pop ax
        call hex2
        ret

hex8:   push eax
        shr eax, 16
        call hex4
        pop eax
        call hex4
        ret

crlf:   push si
        mov si, m_crlf
        call puts
        pop si
        ret

sp1:    push si
        mov si, m_sp
        call puts
        pop si
        ret

logname db 'VX15.LOG', 0
logh    dw -1
quick   db 0
xent    dd 0
xver    dw 0
hnd     dw 0
ehnd    dw 0
frame   dw 0
hbuf    db 0, 0, 0
cbuf    db 0, 0
emmname db 'EMMXXXX0'
ename   db 'VX15', 0, 0, 0, 0
mv_len  dd 0
mv_sh   dw 0
mv_so   dd 0
mv_dh   dw 0
mv_do   dd 0
m_crlf  db 13, 10, 0
m_sp    db ' ', 0
m_bx    db ' bx=', 0
m_cx    db ' cx=', 0
m_dx    db ' dx=', 0
m_bl    db ' bl=', 0
m_ah    db ' ah=', 0
m_dxnz  db ' dx!=0:', 0
m_addr  db ' addr!=0:', 0
m_same  db 'X move back: same', 13, 10, 0
m_diff  db 'X move back: DIFFERENT', 13, 10, 0
m_start db 'VX15 start', 13, 10, 0
m_end   db 'VX15 end', 13, 10, 0
m_int12 db 'BIOS int12 ax=', 0
m_xinst db 'X 4300 al=', 0
m_x00   db 'X 00 ax=', 0
m_x08   db 'X 08 ax=', 0
m_x88   db 'X 88 eax edx ecx bl=', 0
m_x01   db 'X 01 ax=', 0
m_x01b  db 'X 01again ax=', 0
m_x02   db 'X 02 ax=', 0
m_x02b  db 'X 02again ax=', 0
m_x03   db 'X 03 ax=', 0
m_x04   db 'X 04 ax=', 0
m_x05   db 'X 05 ax=', 0
m_x06   db 'X 06 ax=', 0
m_x07   db 'X 07 ax=', 0
m_x09   db 'X 09(64) ax=', 0
m_x09b  db 'X 09(FFFF) ax=', 0
m_x09z  db 'X 09(0) ax=', 0
m_x0e   db 'X 0E ax=', 0
m_x0b1  db 'X 0B to ax=', 0
m_x0b2  db 'X 0B from ax=', 0
m_x0b3  db 'X 0B odd ax=', 0
m_x0b4  db 'X 0B past ax=', 0
m_x0c   db 'X 0C ax=', 0
m_x0d   db 'X 0D ax=', 0
m_x0d2  db 'X 0Dagain ax=', 0
m_x0f   db 'X 0F(128) ax=', 0
m_x0a   db 'X 0A ax=', 0
m_x0a2  db 'X 0Aagain ax=', 0
m_xmany db 'X 1KB blocks: ', 0
m_x10   db 'X 10 ax=', 0
m_ename db 'E EMMXXXX0 at INT 67h: ', 0
m_e40   db 'E 40 ax=', 0
m_e41   db 'E 41 ax=', 0
m_e42   db 'E 42 ax=', 0
m_e43   db 'E 43(4) ax=', 0
m_e43z  db 'E 43(0) ax=', 0
m_e43b  db 'E 43(7FFF) ax=', 0
m_e44   db 'E 44 ax=', 0
m_e44u  db 'E 44 unmap ax=', 0
m_e44l  db 'E 44 bad logical ax=', 0
m_e44p  db 'E 44 bad physical ax=', 0
m_eread db 'E frame reads ', 0
m_e45   db 'E 45 ax=', 0
m_e45b  db 'E 45again ax=', 0
m_e46   db 'E 46 ax=', 0
m_e47   db 'E 47 ax=', 0
m_e48   db 'E 48 ax=', 0
m_e4b   db 'E 4B ax=', 0
m_e4c   db 'E 4C ax=', 0
m_e51   db 'E 51(8) ax=', 0
m_e5301 db 'E 5301 ax=', 0
m_e5300 db 'E 5300 ax=', 0
m_e5801 db 'E 5801 ax=', 0
m_e5800 db 'E 5800 ax=', 0
m_e5901 db 'E 5901 ax=', 0
m_emany db 'E 1-page handles: ', 0
m_e4e3  db 'E 4E03 ax=', 0
m_e4e0  db 'E 4E00 ax=', 0
m_e4e1  db 'E 4E01 ax=', 0
m_e50   db 'E 5000 ax=', 0
m_e501  db 'E 5001 ax=', 0
m_e57a  db 'E 5700 to ax=', 0
m_e57b  db 'E 5700 from ax=', 0
m_esame db 'E move back: same', 13, 10, 0
m_ediff db 'E move back: DIFFERENT', 13, 10, 0
m_e520  db 'E 5200 ax=', 0
m_e542  db 'E 5402 ax=', 0
m_e540  db 'E 5400 ax=', 0
mlist   times 8 dw 0
mreg    times 18 db 0
mapbuf  times 256 db 0
hnds    times 200 dw 0
buf1    times 512 db 0
buf2    times 512 db 0
