; dx4.asm - DX4.COM: INT 21h from protected mode, function by function.
;
; Black-box observation (ARCHITECTURE.md section 3); runs in the KERNEL
; slot under WIN /S like DX1 (tests/dosxtest.sh DX4). After switching to
; protected mode it calls each DOS function that takes or returns a
; pointer with its buffers in extended memory (a 64 KB block from INT 31h
; 0501h), so a function works only if the DOS extender translates it,
; and logs the registers coming back and what arrived in the buffers:
;   09h; 1Ah/2Fh; 4Eh/4Fh; 3Ch/40h/42h/3Fh/3Eh with 1, 4000h, 9000h and
;   FFF0h bytes; 47h; 39h/3Bh/3Ah; 43h; 56h; 5Ah; 5Bh; 41h; 6Ch; 29h;
;   the FCB functions 0Fh, 10h, 11h, 12h, 13h, 14h, 15h, 16h, 21h, 22h,
;   23h, 27h, 28h; 38h; 44h 00h, 0Dh (CL=60h); 4Bh 00h (a program DX4
;   writes); 5Eh 00h; 5Fh 02h; 60h; 65h 01h/21h/22h; and the functions
;   that return pointers: 1Bh, 1Ch, 1Fh, 32h, 34h, 51h, 52h, 62h, 63h.
; The log is C:\DX4.LOG.

        bits 16
        cpu 286
        org 100h

%define LOGNAME 'C:\DX4.LOG'
%include "dxlib.inc"

; X 'name' : log the registers and the carry flag after a call
%macro X 1
        int 21h
        mov ds, [cs:datasel]
        cld
        REGS %1
        call flush
%endmacro
; PUTX off, 'string' : a NUL-terminated string to xsel:off
%macro PUTX 2
        push si
        push di
        mov di, %1
        call putx_i
        db %2, 0
        pop di
        pop si
%endmacro
; DUMP 'name', off, n : n bytes of xsel:off as text and hex
%macro DUMP 3
        push si
        mov si, %2
        mov cx, %3
        call dump_i
        db %1, 0
        pop si
%endmacro

start:  mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        call logcreate
        NOTE 'DX4 probe'
        ; a program for 4Bh: exits with code 7
        mov dx, s_exe
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .nox
        mov bx, ax
        mov dx, prog7
        mov cx, prog7_len
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.nox:   mov ax, 1687h
        int 2Fh
        mov [pment], di
        mov [pment+2], es
        push ds
        pop es
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        mov es, ax
.nohd:  xor ax, ax
        call far [pment]
        jnc pm
        NOTE 'DX4 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

pm:     push ds
        pop es
        mov [datasel], ds
        NOTE 'DX4 protected mode'
        ; 64 KB of extended memory and a selector for it
        mov bx, 1
        xor cx, cx
        mov ax, 0501h
        int 31h
        jc near fail
        mov [xlin], cx
        mov [xlin+2], bx
        mov cx, 1
        xor ax, ax
        int 31h
        jc near fail
        mov [xsel], ax
        mov bx, ax
        mov cx, [xlin+2]
        mov dx, [xlin]
        mov ax, 0007h
        int 31h
        mov bx, [xsel]
        xor cx, cx
        mov dx, 0FFFFh
        mov ax, 0008h
        int 31h
        ; fill it with a pattern
        mov es, [xsel]
        xor di, di
        mov cx, 8000h
        xor ax, ax
.fill:  mov [es:di], ax
        add ax, 1357h
        add di, 2
        loop .fill
        push ds
        pop es
        call flush

        ; ---- 09h
        PUTX 0, 'DX4 09h output$'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 09h
        int 21h
        mov ds, [cs:datasel]
        REGS 'f09'
        ; ---- 47h, 3Bh, 39h, 3Ah
        mov ds, [xsel]
        mov si, 100h
        xor dl, dl
        mov ah, 47h
        call dosx
        X 'f47'
        DUMP 'f47_buf', 100h, 40h
        PUTX 0, 'C:\DX4DIR'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 39h
        call dosx
        X 'f39'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 3Bh
        call dosx
        X 'f3b'
        mov ds, [xsel]
        mov si, 100h
        xor dl, dl
        mov ah, 47h
        call dosx
        X 'f47_after_cd'
        DUMP 'f47_after_cd', 100h, 20h
        PUTX 0, 'C:\'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 3Bh
        call dosx
        X 'f3b_root'
        PUTX 0, 'C:\DX4DIR'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 3Ah
        call dosx
        X 'f3a'
        ; ---- 3Ch, 40h, 42h, 3Fh, 3Eh with growing sizes
        mov word [size], 1
        call rwtest
        mov word [size], 4000h
        call rwtest
        mov word [size], 9000h
        call rwtest
        mov word [size], 0FFF0h
        call rwtest
        ; ---- 43h, 56h, 5Bh, 5Ah, 41h, 6Ch
        PUTX 0, 'C:\DX4.TMP'
        mov ds, [xsel]
        xor dx, dx
        mov ax, 4300h
        call dosx
        X 'f43'
        PUTX 0, 'C:\DX4.TMP'
        PUTX 80h, 'C:\DX4REN.TMP'
        mov es, [xsel]
        mov ds, [xsel]
        xor dx, dx
        mov di, 80h
        mov ah, 56h
        call dosx
        X 'f56'
        push ds
        pop es
        PUTX 0, 'C:\DX4REN.TMP'
        mov ds, [xsel]
        xor dx, dx
        xor cx, cx
        mov ah, 5Bh
        call dosx
        X 'f5b_exists'
        PUTX 0, 'C:\'
        mov ds, [xsel]
        xor dx, dx
        xor cx, cx
        mov ah, 5Ah
        call dosx
        X 'f5a'
        mov bx, ax
        mov ah, 3Eh
        int 21h
        DUMP 'f5a_name', 0, 20h
        mov ds, [xsel]
        xor dx, dx
        mov ah, 41h
        call dosx
        X 'f41_temp'
        PUTX 0, 'C:\DX4REN.TMP'
        mov ds, [xsel]
        xor dx, dx
        mov ah, 41h
        call dosx
        X 'f41'
        PUTX 0, 'C:\WINDOWS\WIN.INI'
        mov ds, [xsel]
        xor si, si
        mov bx, 0
        mov dx, 1
        xor cx, cx
        mov ax, 6C00h
        call dosx
        X 'f6c'
        mov bx, ax
        mov ah, 3Eh
        int 21h
        ; ---- DTA: 1Ah, 2Fh, 4Eh, 4Fh
        mov ds, [xsel]
        mov dx, 200h
        mov ah, 1Ah
        call dosx
        X 'f1a'
        mov ah, 2Fh
        X 'f2f'
        push ds
        pop es
        PUTX 0, 'C:\WINDOWS\*.INI'
        mov ds, [xsel]
        xor dx, dx
        xor cx, cx
        mov ah, 4Eh
        call dosx
        X 'f4e'
        DUMP 'f4e_dta', 200h, 2Bh
        mov ah, 4Fh
        X 'f4f'
        DUMP 'f4f_dta', 200h, 2Bh
        ; ---- 29h
        PUTX 0, 'C:FOO.BAR rest'
        mov es, [xsel]
        mov ds, [xsel]
        xor si, si
        mov di, 300h
        mov ax, 2900h
        call dosx
        X 'f29'
        push ds
        pop es
        DUMP 'f29_fcb', 300h, 10h
        ; ---- FCBs (DTA still at xsel:200h)
        call fcbtest
        ; ---- 38h, 44h, 65h
        mov ds, [xsel]
        mov dx, 400h
        mov ax, 3800h
        call dosx
        X 'f38'
        DUMP 'f38_buf', 400h, 22h
        mov ax, 4400h
        xor bx, bx
        X 'f44_00'
        mov ds, [xsel]
        mov dx, 500h
        mov bx, 3
        mov cx, 0860h
        mov ax, 440Dh
        call dosx
        X 'f44_0d_60'
        DUMP 'f44_0d_buf', 500h, 20h
        mov es, [xsel]
        mov di, 600h
        mov bx, 0FFFFh
        mov dx, 0FFFFh
        mov cx, 40
        mov ax, 6501h
        X 'f65_01'
        push ds
        pop es
        DUMP 'f65_01_buf', 600h, 28h
        PUTX 700h, 'abcxyz'
        mov ds, [xsel]
        mov dx, 700h
        mov cx, 6
        mov ax, 6521h
        call dosx
        X 'f65_21'
        DUMP 'f65_21_buf', 700h, 8
        PUTX 700h, 'hello'
        mov ds, [xsel]
        mov dx, 700h
        mov ax, 6522h
        call dosx
        X 'f65_22'
        DUMP 'f65_22_buf', 700h, 8
        ; ---- 60h, 5Eh, 5Fh
        PUTX 0, 'c:dx4.com'
        mov es, [xsel]
        mov ds, [xsel]
        xor si, si
        mov di, 800h
        mov ah, 60h
        call dosx
        X 'f60'
        push ds
        pop es
        DUMP 'f60_buf', 800h, 20h
        mov ds, [xsel]
        mov dx, 900h
        mov ax, 5E00h
        call dosx
        X 'f5e00'
        mov es, [xsel]
        mov ds, [xsel]
        mov si, 900h
        mov di, 980h
        xor bx, bx
        mov ax, 5F02h
        call dosx
        X 'f5f02'
        push ds
        pop es
        ; ---- pointers coming back
        mov ah, 62h
        X 'f62'
        mov ah, 51h
        X 'f51'
        push ds
        mov dl, 0
        mov ah, 1Bh
        int 21h
        mov [ss:t1], ds
        pop ds
        REGS 'f1b'
        OBS 'f1b_ds', [t1]
        push ds
        mov dl, 3
        mov ah, 1Ch
        int 21h
        mov [ss:t1], ds
        pop ds
        REGS 'f1c'
        OBS 'f1c_ds', [t1]
        push ds
        mov ah, 1Fh
        int 21h
        mov [ss:t1], ds
        pop ds
        REGS 'f1f'
        OBS 'f1f_ds', [t1]
        push ds
        mov dl, 3
        mov ah, 32h
        int 21h
        mov [ss:t1], ds
        pop ds
        REGS 'f32'
        OBS 'f32_ds', [t1]
        mov ah, 34h
        X 'f34'
        push ds
        pop es
        mov ah, 52h
        X 'f52'
        push ds
        pop es
        push ds
        mov ax, 6300h
        int 21h
        mov [ss:t1], ds
        pop ds
        REGS 'f63'
        OBS 'f63_ds', [t1]
        call flush
        ; ---- 4Bh: C:\DX4E.COM exits with 7
        call exectest
        NOTE 'DX4 end'
        call flush
        mov ax, 4C00h
        int 21h

fail:   NOTE 'DX4 no memory'
        call flush
        mov ax, 4C01h
        int 21h

; dosx: nothing (a place for a breakpoint); keeps the flags
dosx:   ret

; rwtest: create C:\DX4.TMP, write [size] bytes from xsel:0, seek to 0,
; read them back to xsel:8000h... (the second half for sizes up to 8000h,
; the start again for larger ones, after clearing), compare, close
rwtest: PUTX 0F000h, 'C:\DX4.TMP'
        mov ds, [xsel]
        mov dx, 0F000h
        xor cx, cx
        mov ah, 3Ch
        int 21h
        mov ds, [cs:datasel]
        mov [fh], ax
        REGS 'rw_create'
        ; the pattern: refill the start (PUTX may have overwritten)
        call refill
        mov bx, [fh]
        mov cx, [size]
        mov ds, [xsel]
        xor dx, dx
        mov ah, 40h
        int 21h
        mov ds, [cs:datasel]
        mov si, s_w
        call rwlog
        mov bx, [fh]
        xor cx, cx
        xor dx, dx
        mov ax, 4200h
        int 21h
        ; clear, then read back over the start
        mov es, [xsel]
        xor di, di
        mov cx, [size]
        shr cx, 1
        inc cx
        xor ax, ax
        rep stosw
        push ds
        pop es
        mov bx, [fh]
        mov cx, [size]
        mov ds, [xsel]
        xor dx, dx
        mov ah, 3Fh
        int 21h
        mov ds, [cs:datasel]
        mov si, s_r
        call rwlog
        ; compare with the pattern
        mov es, [xsel]
        xor di, di
        mov cx, [size]
        shr cx, 1
        xor ax, ax
        xor dx, dx
.cmp:   jcxz .cd
        cmp [es:di], ax
        je .ok
        inc dx
.ok:    add ax, 1357h
        add di, 2
        dec cx
        jmp .cmp
.cd:    push ds
        pop es
        mov si, s_bad
        call puts
        mov ax, [size]
        call puthex
        mov al, '='
        call putc
        mov ax, dx
        call puthex
        call crlf
        mov bx, [fh]
        mov ah, 3Eh
        int 21h
        call flush
        ret
refill: mov es, [xsel]
        xor di, di
        mov cx, 7800h
        xor ax, ax
.f:     mov [es:di], ax
        add ax, 1357h
        add di, 2
        loop .f
        push ds
        pop es
        ret
; rwlog: "OBS <s><size>=cf:.. ax:.."
rwlog:  pushf
        push ax
        push si
        mov si, s_obs
        call puts
        pop si
        call puts
        mov ax, [size]
        call puthex
        mov si, s_cf
        call puts
        pop ax
        popf
        push ax
        mov al, '0'
        adc al, 0
        call putc
        mov si, s_ax
        call puts
        pop ax
        call puthex
        call crlf
        ret

; fcbtest: the FCB functions on C:\DX4FCB.TMP (the DTA is xsel:200h)
fcbtest:
        call fcbinit
        mov ah, 16h             ; create
        call fcbcall
        db 'fcb16_create', 0
        mov word [es:30Eh], 80h  ; record size 128 (after the drive byte)
        mov byte [es:320h], 0     ; current record
        ; a record to write: the DTA
        mov di, 200h
        mov cx, 40h
        mov ax, 'AB'
        rep stosw
        mov ah, 15h             ; sequential write
        call fcbcall
        db 'fcb15_write', 0
        mov word [es:321h], 1     ; random record 1
        mov word [es:323h], 0
        mov ah, 22h             ; random write
        call fcbcall
        db 'fcb22_write', 0
        mov ah, 10h             ; close
        call fcbcall
        db 'fcb10_close', 0
        call fcbinit
        mov ah, 0Fh             ; open
        call fcbcall
        db 'fcb0f_open', 0
        DUMP 'fcb0f_fcb', 300h, 25h
        mov es, [xsel]
        mov word [es:30Eh], 80h
        mov byte [es:320h], 0
        mov di, 200h
        mov cx, 40h
        xor ax, ax
        rep stosw
        mov ah, 14h             ; sequential read
        call fcbcall
        db 'fcb14_read', 0
        DUMP 'fcb14_dta', 200h, 10h
        mov word [es:321h], 1
        mov word [es:323h], 0
        mov ah, 21h             ; random read
        call fcbcall
        db 'fcb21_read', 0
        mov word [es:321h], 0
        mov word [es:323h], 0
        mov cx, 2
        mov ah, 27h             ; random block read, 2 records
        call fcbcall
        db 'fcb27_read2', 0
        mov ah, 23h             ; file size in records
        call fcbcall
        db 'fcb23_size', 0
        DUMP 'fcb23_fcb', 321h, 4
        mov ah, 10h
        call fcbcall
        db 'fcb10_close2', 0
        ; search with an FCB: 11h, 12h
        call fcbinit_wild
        mov ah, 11h
        call fcbcall
        db 'fcb11_find', 0
        DUMP 'fcb11_dta', 200h, 20h
        mov ah, 12h
        call fcbcall
        db 'fcb12_next', 0
        call fcbinit
        mov ah, 13h             ; delete
        call fcbcall
        db 'fcb13_delete', 0
        push ds
        pop es
        ret
; fcbinit: an extended-less FCB for C:DX4FCB.TMP at xsel:300h (drive
; byte at 300h: the FCB starts at 300h; 301h-30Bh the name)
fcbinit:
        mov es, [xsel]
        mov di, 300h
        mov cx, 18h
        xor ax, ax
        rep stosw
        mov byte [es:300h], 3
        mov si, fcbname
.c:     mov di, 301h
        mov cx, 11
        rep movsb
        ret
fcbinit_wild:
        call fcbinit
        mov si, fcbwild
        jmp fcbinit.c
; fcbcall: AH = function (CX as set), DS:DX = the FCB; logs as named
fcbcall:
        push ds
        mov ds, [xsel]
        mov dx, 300h
        int 21h
        pop ds
        cld
        push bp
        mov bp, sp
        push si
        push ax
        mov si, s_obs
        call puts
        mov si, [bp+2]
        call cputs
        mov [bp+2], si
        mov al, '='
        call putc
        pop ax
        push ax
        call puthex2
        call crlf
        pop ax
        pop si
        pop bp
        mov es, [xsel]
        ret

; exectest: 4B00h with the program name, the parameter block and the
; command tail in extended memory
exectest:
        PUTX 0, 'C:\DX4E.COM'
        PUTX 40h, '  hello'
        mov es, [xsel]
        mov byte [es:40h], 6
        mov byte [es:47h], 0Dh
        mov word [es:80h], 0            ; environment: the parent's
        mov word [es:82h], 40h          ; the tail
        mov ax, [xsel]
        mov [es:84h], ax
        mov word [es:86h], 0C0h         ; FCB 1
        mov [es:88h], ax
        mov word [es:8Ah], 0C0h         ; FCB 2
        mov [es:8Ch], ax
        mov ds, [xsel]
        xor dx, dx
        mov bx, 80h
        mov ax, 4B00h
        int 21h
        mov ds, [cs:datasel]
        cld
        REGS 'f4b00'
        mov ah, 4Dh
        int 21h
        REGS 'f4d'
        push ds
        pop es
        call flush
        ret

; putx_i: copy the inline string to xsel:DI
putx_i: push bp
        mov bp, sp
        push es
        push ax
        mov es, [xsel]
        mov si, [bp+2]
.l:     mov al, [cs:si]
        inc si
        mov [es:di], al
        inc di
        or al, al
        jnz .l
        mov [bp+2], si
        pop ax
        pop es
        pop bp
        ret
; dump_i: "OBS name=<hex bytes> |text|" of CX bytes at xsel:SI
dump_i: push bp
        mov bp, sp
        push ax
        push si
        push cx
        mov si, s_obs
        call puts
        mov si, [bp+2]
        call cputs
        mov [bp+2], si
        mov al, '='
        call putc
        pop cx
        pop si
        push si
        push cx
.h:     push es
        mov es, [xsel]
        mov al, [es:si]
        pop es
        call puthex2
        inc si
        loop .h
        mov al, ' '
        call putc
        pop cx
        pop si
.t:     push es
        mov es, [xsel]
        mov al, [es:si]
        pop es
        call putc_safe
        inc si
        loop .t
        call crlf
        pop ax
        pop bp
        ret

%include "dxlib.inc"

s_exe   db 'C:\DX4E.COM', 0
prog7   db 0B8h, 07h, 4Ch, 0CDh, 21h     ; mov ax, 4C07h / int 21h
prog7_len equ $ - prog7
fcbname db 'DX4FCB  TMP'
fcbwild db 'DX4FCB  ???'
s_w     db 'rw_write_', 0
s_r     db 'rw_read_', 0
s_bad   db 'OBS rw_bad_words_', 0
s_cf    db '=cf:', 0
s_ax    db ' ax:', 0
datasel dw 0

        section .bss
psp     resw 1
pment   resd 1
xlin    resd 1
xsel    resw 1
fh      resw 1
size    resw 1
t1      resw 1
logbuf  resb LOGSIZE
        resb 1024
stack_top:
