; vx12.asm - VX12.COM: what a DOS program in a DOS box sees in 386
; Enhanced mode, for the WIN386 audit's last items (its triage file,
; docs/notes/win386-audit-triage.md, is in git history); run in the
; MS-DOS Prompt by the input scripts
; tests/scripts/input/vx12*.txt under Windows 3.1's WIN386 and w16's.
; Black box: what ports, interrupts and memory give back (ARCHITECTURE.md
; 3). Each run prints its lines and writes them to VX12.LOG in the current
; directory. The first letter of the command tail picks the run:
;   K  keyboard: INT 09h hooked, every byte read at port 60h in it kept;
;      Enter ends the run and the bytes are listed (Ctrl+Alt+Del and the
;      screen Windows shows for a DOS program come in between)
;   G  a privileged instruction in V86 mode (LGDT): what Windows does
;   A  A20 through the 8042's output port (D1h, DDh / DFh): whether
;      FFFF:0010 then wraps to 0000:0000
;   B  A20 through port 92h bit 1, the same; port 92h read back
;   R  the 8042's reset pulse (FEh): does the machine reset?
;   V  INT 2Fh 1684h for device IDs 0-40h: the V86 API entries, and each
;      entry's function 0 (AX=0): AX, BX and carry back
;   T  80x50 text (INT 10h 1112h), each row numbered, until Enter
;   F  40x25 text (mode 1), likewise
        bits 16
        cpu 386
        org 100h

start:  mov dx, logname                 ; appended to: one log for several runs
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
        cmp al, 'k'
        je near run_k
        cmp al, 'g'
        je near run_g
        cmp al, 'a'
        je near run_a
        cmp al, 'r'
        je near run_r
        cmp al, 'b'
        je near run_b
        cmp al, 'v'
        je near run_v
        cmp al, 't'
        je near run_t
        cmp al, 'f'
        je near run_f
        mov si, m_usage
        call puts
        jmp done

done:   mov bx, [logh]
        cmp bx, -1
        je .x
        mov ah, 3Eh
        int 21h
.x:     mov ax, 4C00h
        int 21h

; ---- output: a string at SI (NUL-ended) to the screen and the log ----
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

; AL as two hex digits
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

; ---- K ----
run_k:  mov ax, 3509h
        int 21h
        mov [old9], bx
        mov [old9 + 2], es
        mov dx, int9
        mov ax, 2509h
        int 21h
        mov si, m_kready
        call puts
.wait:  xor ah, ah
        int 16h
        cmp al, 13
        jne .wait
        push ds
        lds dx, [old9]
        mov ax, 2509h
        int 21h
        pop ds
        mov si, m_kbytes
        call puts
        mov cx, [nkb]
        mov si, kbuf
.p:     jcxz .e
        lodsb
        call hex2
        call sp1
        dec cx
        jmp .p
.e:     call crlf
        jmp done

int9:   push ax
        push bx
        in al, 60h
        mov bx, [cs:nkb]
        cmp bx, 200
        jae .full
        mov [cs:kbuf + bx], al
        inc word [cs:nkb]
.full:  pop bx
        pop ax
        jmp far [cs:old9]

; ---- G ----
run_g:  mov si, m_g
        call puts
        lgdt [gdtp]
        mov si, m_gsurv
        call puts
        jmp done

; ---- A ----
; ZF set when FFFF:0010 is 0000:0000 (A20 off)
wraps:  push ds
        push es
        xor ax, ax
        mov ds, ax
        mov ax, 0FFFFh
        mov es, ax
        mov al, [0]
        mov ah, [es:10h]
        mov byte [es:10h], 5Ah
        mov bl, [0]
        mov byte [es:10h], 0A5h
        mov bh, [0]
        mov [es:10h], ah
        mov [0], al
        cmp bx, 0A55Ah
        pop es
        pop ds
        ret

wrapout:                                ; prints " wrap=1" or " wrap=0"
        push si
        call wraps
        mov si, m_w1
        je .y
        mov si, m_w0
.y:     call puts
        pop si
        ret

kbc_wait:
        push cx
        mov cx, 0FFFFh
.l:     in al, 64h
        test al, 2
        jz .r
        loop .l
.r:     pop cx
        ret

run_a:  mov si, m_a0
        call puts
        call wrapout
        in al, 92h
        mov si, m_p92
        call puts
        call hex2
        call crlf
        mov si, m_a1                    ; 8042: A20 off
        call puts
        call kbc_wait
        mov al, 0D1h
        out 64h, al
        call kbc_wait
        mov al, 0DDh
        out 60h, al
        call kbc_wait
        call wrapout
        call crlf
        mov si, m_a2                    ; 8042: A20 on
        call puts
        call kbc_wait
        mov al, 0D1h
        out 64h, al
        call kbc_wait
        mov al, 0DFh
        out 60h, al
        call kbc_wait
        call wrapout
        call crlf
        jmp done

; ---- B: port 92h alone ----
run_b:  mov si, m_a0
        call puts
        call wrapout
        in al, 92h
        mov si, m_p92
        call puts
        call hex2
        call crlf
        mov si, m_a3                    ; port 92h: bit 1 off
        call puts
        in al, 92h
        mov [p92], al
        and al, 0FDh
        out 92h, al
        call wrapout
        in al, 92h
        mov si, m_p92
        call puts
        call hex2
        call crlf
        mov si, m_a4                    ; port 92h as it was
        call puts
        mov al, [p92]
        or al, 2
        out 92h, al
        call wrapout
        in al, 92h
        mov si, m_p92
        call puts
        call hex2
        call crlf
        jmp done

; ---- R ----
run_r:  mov si, m_r
        call puts
        call kbc_wait
        mov al, 0FEh
        out 64h, al
        mov cx, 40                      ; about 2 s of BIOS ticks
.t:     push cx
        xor ax, ax
        int 1Ah
        mov bx, dx
.t2:    xor ax, ax
        int 1Ah
        cmp dx, bx
        je .t2
        pop cx
        loop .t
        mov si, m_rsurv
        call puts
        jmp done

; ---- V ----
run_v:  mov si, m_v
        call puts
        xor bx, bx
.d:     push bx
        xor di, di
        mov es, di
        mov ax, 1684h
        int 2Fh
        mov ax, es
        or ax, di
        jz .next
        pop bx
        push bx
        mov si, m_vdev
        call puts
        mov ax, bx
        call hex4
        mov si, m_vent
        call puts
        mov ax, es
        call hex4
        mov si, m_colon
        call puts
        mov ax, di
        call hex4
        mov [vent], di
        mov [vent + 2], es
        xor ax, ax
        xor bx, bx
        xor cx, cx
        xor dx, dx
        clc
        call far [vent]
        pushf
        push bx
        push ax
        mov si, m_vax
        call puts
        pop ax
        call hex4
        mov si, m_vbx
        call puts
        pop ax
        call hex4
        mov si, m_vcf
        call puts
        pop ax
        and al, 1
        call hex2
        call crlf
.next:  pop bx
        inc bx
        cmp bx, 41h
        jb .d
        mov si, m_vend
        call puts
        jmp done

; ---- T, F ----
run_t:  mov ax, 0003h
        int 10h
        mov ax, 1112h                   ; 8x8 font: 50 rows
        xor bl, bl
        int 10h
        mov cx, 50
        jmp rows
run_f:  mov ax, 0001h
        int 10h
        mov cx, 25
rows:   xor bx, bx
.r:     push cx
        mov si, m_row
        call puts
        mov al, bl
        call hex2
        call crlf
        pop cx
        inc bx
        cmp bx, cx
        jb .r
.w:     xor ah, ah
        int 16h
        cmp al, 13
        jne .w
        mov ax, 0003h
        int 10h
        mov si, m_tend
        call puts
        jmp done

logname db 'VX12.LOG', 0
logh    dw -1
hbuf    db 0, 0, 0
m_crlf  db 13, 10, 0
m_sp    db ' ', 0
m_colon db ':', 0
m_usage db 'VX12 K|G|A|R|V|T|F', 13, 10, 0
m_kready db 'VX12 K ready', 13, 10, 0
m_kbytes db 'VX12 K bytes: ', 0
m_g     db 'VX12 G: LGDT', 13, 10, 0
m_gsurv db 'VX12 G survived', 13, 10, 0
m_a0    db 'VX12 A start', 0
m_a1    db 'VX12 A 8042 off', 0
m_a2    db 'VX12 A 8042 on', 0
m_a3    db 'VX12 A 92h off', 0
m_a4    db 'VX12 A 92h back', 0
m_p92   db ' 92h=', 0
m_w0    db ' wrap=0', 0
m_w1    db ' wrap=1', 0
m_r     db 'VX12 R: FEh to 64h', 13, 10, 0
m_rsurv db 'VX12 R survived', 13, 10, 0
m_v     db 'VX12 V', 13, 10, 0
m_vdev  db 'VX12 V dev ', 0
m_vent  db ' entry ', 0
m_vax   db ' ax=', 0
m_vbx   db ' bx=', 0
m_vcf   db ' cf=', 0
m_vend  db 'VX12 V done', 13, 10, 0
m_row   db 'row ', 0
m_tend  db 'VX12 T done', 13, 10, 0
gdtp    dw 0, 0, 0
p92     db 0
        align 2
old9    dd 0
vent    dd 0
nkb     dw 0
kbuf    times 200 db 0
