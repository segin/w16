; sw2.asm - SW2.COM: an MS-DOS 5 task switcher client, resident, for the
; WSWAP/DSWAP audit (docs/notes/swap-audit-triage.md, ARCHITECTURE.md 9.9).
; Black-box: it sees only the public task switcher interface (INT 2Fh
; 4B01h, 4B05h; RBIL, MS-DOS Programmer's Reference) and the BIOS data
; area, and logs what happens into a buffer of its own.
;
;   SW2      install (once, before WIN). It answers INT 2Fh 4B05h with two
;            startup-info structures: the first declares no instance data
;            (instance list 0000:0000, which the interface allows), the
;            second one item, a byte here: 'A' at the start. It joins the
;            notification chain (4B01h) and logs each notification
;            ("N function session CX"). INT 2Fh 4B80h (WINOLDAP asking for
;            a DOS program) sets the byte to 'W' and logs "X"; 4B05h logs
;            "I".
;   SW2 F    install, but every notification returns with the direction
;            flag set (a careless client).
;   SW2 L    log the byte ("V x").
;   SW2 S    set the byte to 'D', log "S".
;   SW2 K    log the BIOS keyboard flags: 0040:0017h, 0018h, 0096h.
;   SW2 D    write the log to C:\SW2.LOG.
;
; Public domain (CC0 1.0, LICENSE).

        bits 16
        cpu 8086
        org 100h

LOGMAX  equ 3072
API     equ 0DE57h              ; INT 2Fh AX: "is SW2 resident?"
MAGIC   equ 5357h               ; BX in, AX out

start:  jmp init

old2f:  dd 0
val:    db 'A'
stdflag: db 0
inst:   dd val                  ; the one instance item: val, 1 byte
        dw 1
        dd 0
s_item: dw 3                    ; startup info: version, next, virtual
        dd 0                    ; device file, reference data, instance
        dd 0, 0                 ; list
        dd inst
s_null: dw 3
        dd s_item
        dd 0, 0
        dd 0                    ; (no instance data)
cbinfo: dd 0                    ; callback info: next, notification
        dd notify               ; function, reserved, API list
        dd 0, 0
logp:   dw 0

int2f:  cmp ax, 4B05h
        je .ident
        cmp ax, 4B01h
        je .build
        cmp ax, 4B80h
        je .sw
        cmp ax, API
        jne .c
        cmp bx, MAGIC
        jne .c
        mov ax, MAGIC
        push cs
        pop es
        iret
.sw:    mov byte [cs:val], 'W'
        push si
        mov si, m_x
        call logs
        pop si
.c:     jmp far [cs:old2f]
.ident: push si
        mov si, m_i
        call logs
        pop si
        pushf
        call far [cs:old2f]
        mov [cs:s_item + 2], bx
        mov [cs:s_item + 4], es
        push cs
        pop es
        mov bx, s_null
        iret
.build: pushf
        call far [cs:old2f]
        mov [cs:cbinfo], bx
        mov [cs:cbinfo + 2], es
        push cs
        pop es
        mov bx, cbinfo
        iret

; the notification function: AX function, BX session, CX; AX=0 back
notify: push si
        push ax
        mov si, m_n
        call logs
        call loghex
        mov ax, bx
        call logsp
        call loghex
        mov ax, cx
        call logsp
        call loghex
        call logcr
        pop ax
        pop si
        xor ax, ax
        cmp byte [cs:stdflag], 0
        je .r
        std
.r:     retf

; append the NUL-ended string CS:SI to the log
logs:   push ax
        pushf
        cli
.l:     mov al, [cs:si]
        test al, al
        jz .d
        call logc
        inc si
        jmp .l
.d:     popf
        pop ax
        ret
logc:   push bx
        mov bx, [cs:logp]
        cmp bx, LOGMAX
        jae .f
        mov [cs:logbuf + bx], al
        inc word [cs:logp]
.f:     pop bx
        ret
logsp:  push ax
        mov al, ' '
        call logc
        pop ax
        ret
logcr:  push ax
        mov al, 0Dh
        call logc
        mov al, 0Ah
        call logc
        pop ax
        ret
loghex: push ax
        push cx
        mov cx, 4
.l:     push cx
        mov cl, 4
        rol ax, cl
        pop cx
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     call logc
        pop ax
        loop .l
        pop cx
        pop ax
        ret
m_x:    db 'X', 0Dh, 0Ah, 0
m_i:    db 'I', 0Dh, 0Ah, 0
m_n:    db 'N ', 0
m_s:    db 'S', 0Dh, 0Ah, 0
m_v:    db 'V ', 0
m_k:    db 'K', 0
logbuf: times LOGMAX db 0
resident_end:

; ---- commands (transient) ----
init:   cld                             ; (3.1's DSWAP starts a program with
        mov si, 81h                     ; DF as SW2 F's notification left it)
.sk:    lodsb
        cmp al, ' '
        je .sk
        and al, 0DFh
        mov [cmd], al
        mov ax, API
        mov bx, MAGIC
        int 2Fh
        cmp ax, MAGIC
        je have
        ; not resident: install (plain or F)
        mov al, [cmd]
        cmp al, 'F'
        je .inst
        cmp al, 0Dh & 0DFh
        jne no_res
.inst:  cmp al, 'F'
        jne .i2
        mov byte [stdflag], 1
.i2:    mov [inst + 2], cs
        mov [s_item + 0Eh + 2], cs
        mov [s_null + 2 + 2], cs
        mov [cbinfo + 4 + 2], cs
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        mov dx, m_inst
        mov ah, 09h
        int 21h
        mov dx, resident_end + 15
        mov cl, 4
        shr dx, cl
        mov ax, 3100h
        int 21h
no_res: mov dx, m_nores
        mov ah, 09h
        int 21h
        mov ax, 4C01h
        int 21h

have:   ; ES = the resident copy
        mov al, [cmd]
        cmp al, 'L'
        je c_l
        cmp al, 'S'
        je c_s
        cmp al, 'K'
        je c_k
        cmp al, 'D'
        je c_d
        mov ax, 4C00h                   ; (installed already)
        int 21h
c_l:    mov si, m_v
        call far_logs
        mov al, [es:val]
        call far_logc
        call far_logcr
        jmp done
c_s:    mov byte [es:val], 'D'
        mov si, m_s
        call far_logs
        jmp done
c_k:    mov si, m_k
        call far_logs
        mov di, 17h
        call kflag
        mov di, 18h
        call kflag
        mov di, 96h
        call kflag
        call far_logcr
        jmp done
kflag:  call far_logsp                  ; " xx", 0040:DI
        push ds
        mov ax, 40h
        mov ds, ax
        mov al, [di]
        pop ds
        jmp far_loghb
c_d:    mov dx, m_file
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc done
        mov bx, ax
        mov cx, [es:logp]
        push ds
        push es
        pop ds
        mov dx, logbuf
        mov ah, 40h
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h
done:   mov ax, 4C00h
        int 21h

; the resident log, through ES (the resident copy's segment)
far_logs:
        mov al, [si]
        test al, al
        jz .d
        call far_logc
        inc si
        jmp far_logs
.d:     ret
far_logc:
        mov bx, [es:logp]
        cmp bx, LOGMAX
        jae .f
        mov [es:logbuf + bx], al
        inc word [es:logp]
.f:     ret
far_logsp:
        mov al, ' '
        jmp far_logc
far_logcr:
        mov al, 0Dh
        call far_logc
        mov al, 0Ah
        jmp far_logc
far_loghb:                              ; AL as two hex digits
        push ax
        mov cl, 4
        shr al, cl
        call .d
        pop ax
        and al, 0Fh
.d:     add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     jmp far_logc

cmd:    db 0
m_file: db 'C:\SW2.LOG', 0
m_inst: db 'SW2 installed', 0Dh, 0Ah, '$'
m_nores: db 'SW2 is not installed', 0Dh, 0Ah, '$'
