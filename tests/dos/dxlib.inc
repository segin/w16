; dxlib.inc - logging helpers for the DX probes (DOSX observation).
;
; The DX probes are .COM images that run in place of SYSTEM\KRNL286.EXE
; (as TK18 does), started by DOSX in real mode; they switch to protected
; mode through DPMI and log "OBS name=value" lines to a file with plain
; INT 21h (3Dh/42h/40h/3Eh), in either mode.
;
; The including file defines LOGNAME (a string), then includes this file
; twice: at the top (macros) and at the end of its code (helpers).
; DS must address the probe's data (its PSP
; segment in real mode, the DPMI data selector in protected mode) when
; any helper is called.
;
;   OBS 'name', src     "OBS name=XXXX" (src: any word operand)
;   OBSB 'name', src    "OBS name=XX"   (src: any byte operand)
;   REGS 'name'         "OBS name=cf:C ax:.. bx:.. cx:.. dx:.. si:.. di:.. es:.."
;                       (all registers and flags preserved)
;   NOTE 'text'         "text" as a line
;   OBSD 'name', hi, lo "OBS name=HHHHLLLL"
; Each line is appended to the buffer; it is written out when it fills
; and by an explicit "call flush".

%ifndef DXLIB_MACROS
%define DXLIB_MACROS
%ifndef LOGSIZE
%define LOGSIZE 16000
%endif

%macro OBS 2
        push ax
        mov ax, %2
        call obs_i
        db %1, 0
        pop ax
%endmacro

%macro OBSB 2
        push ax
        mov al, %2
        mov ah, 0
        call obsb_i
        db %1, 0
        pop ax
%endmacro

%macro OBSD 3
        push ax
        push dx
        mov dx, %2
        mov ax, %3
        call obsd_i
        db %1, 0
        pop dx
        pop ax
%endmacro

%macro REGS 1
        call regs_i
        db %1, 0
%endmacro

%macro NOTE 1
        call note_i
        db %1, 0
%endmacro

%else
; ---------------------------------------------------------------------------
; inline-string helpers: the string follows the call

cputs:          ; CS:SI, NUL-terminated; SI ends past the NUL
        push ax
.l:     mov al, [cs:si]
        inc si
        or al, al
        jz .d
        call putc
        jmp .l
.d:     pop ax
        ret

obs_i:  push bp
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
        call puthex
        call crlf
        pop si
        pop bp
        ret

obsb_i: push bp
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
        call puthex2
        call crlf
        pop si
        pop bp
        ret

obsd_i: push bp
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
        mov ax, dx
        call puthex
        pop ax
        call puthex
        call crlf
        pop si
        pop bp
        ret

note_i: push bp
        mov bp, sp
        push si
        mov si, [bp+2]
        call cputs
        mov [bp+2], si
        call crlf
        pop si
        pop bp
        ret

; regs_i: [bp+2] = string; logs the registers as they were at the call
regs_i: pushf
        push bp
        mov bp, sp              ; [bp+2] flags, [bp+4] return
        push ax
        push si
        push ax
        mov si, s_obs
        cmp byte [regs_hdr], 0
        je .nohdr
        call puts
.nohdr: mov si, [bp+4]
        call cputs
        mov [bp+4], si
        mov si, s_rcf
        call puts
        mov ax, [bp+2]
        and al, 1
        add al, '0'
        call putc
        mov si, s_rax
        call puts
        pop ax
        call puthex
        mov si, s_rbx
        call puts
        mov ax, bx
        call puthex
        mov si, s_rcx
        call puts
        mov ax, cx
        call puthex
        mov si, s_rdx
        call puts
        mov ax, dx
        call puthex
        mov si, s_rsi
        call puts
        pop ax                  ; si
        push ax
        call puthex
        mov si, s_rdi
        call puts
        mov ax, di
        call puthex
        mov si, s_res
        call puts
        mov ax, es
        call puthex
        call crlf
        pop si
        pop ax
        pop bp
        popf
        ret

; ---------------------------------------------------------------------------
puts:           ; DS:SI, NUL-terminated
        push ax
.l:     lodsb
        or al, al
        jz .d
        call putc
        jmp .l
.d:     pop ax
        ret

putc_safe:
        cmp al, 20h
        jb .x
        cmp al, 7Eh
        ja .x
        jmp putc
.x:     push ax
        mov al, '\'
        call putc
        pop ax
        jmp puthex2

putc:   push di
        mov di, [logpos]
        cmp di, LOGSIZE
        jae .full
        mov [logbuf+di], al
        inc word [logpos]
.full:  pop di
        ret

crlf:   push ax
        mov al, 13
        call putc
        mov al, 10
        call putc
%ifndef FLUSHALL
        cmp word [logpos], LOGSIZE - 400
        jb .d
%endif
        call flush
.d:     pop ax
        ret

puthex:         ; AX
        push ax
        mov al, ah
        call puthex2
        pop ax
puthex2:        ; AL
        push ax
        push ax
        shr al, 4
        call .n
        pop ax
        call .n
        pop ax
        ret
.n:     and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     jmp putc

; create (truncate) the log file
logcreate:
        push ax
        push bx
        push cx
        push dx
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .d
        mov bx, ax
        mov ah, 3Eh
        int 21h
.d:     pop dx
        pop cx
        pop bx
        pop ax
        ret

; flush: append the buffer to the log file (INT 21h, in either mode)
flush:  pushf
        push ax
        push bx
        push cx
        push dx
        cmp word [logpos], 0
        je .d
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jc .d
        mov bx, ax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        mov cx, [logpos]
        mov dx, logbuf
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov word [logpos], 0
.d:     pop dx
        pop cx
        pop bx
        pop ax
        popf
        ret

; command-tail parameter: AX = the first hex number in the PSP's command
; tail (ES = PSP), 0 if none; CF set if none
tailhex:
        push bx
        push cx
        push si
        xor ax, ax
        xor cx, cx
        mov cl, [es:80h]
        mov si, 81h
.skip:  jcxz .none
        mov bl, [es:si]
        call .digit
        jnc .num
        inc si
        dec cx
        jmp .skip
.num:   mov ah, 0
        xor ax, ax
.nl:    jcxz .done
        mov bl, [es:si]
        call .digit
        jc .done
        shl ax, 4
        or al, bl
        inc si
        dec cx
        jmp .nl
.done:  clc
        jmp .r
.none:  stc
.r:     pop si
        pop cx
        pop bx
        ret
.digit: cmp bl, '0'
        jb .no
        cmp bl, '9'
        jbe .dec
        or bl, 20h
        cmp bl, 'a'
        jb .no
        cmp bl, 'f'
        ja .no
        sub bl, 'a' - 10
        clc
        ret
.dec:   sub bl, '0'
        clc
        ret
.no:    stc
        ret

logname         db LOGNAME, 0
s_obs           db 'OBS ', 0
s_rcf           db '=cf:', 0
s_rax           db ' ax:', 0
s_rbx           db ' bx:', 0
s_rcx           db ' cx:', 0
s_rdx           db ' dx:', 0
s_rsi           db ' si:', 0
s_rdi           db ' di:', 0
s_res           db ' es:', 0
logpos          dw 0
regs_hdr        db 1
%endif
