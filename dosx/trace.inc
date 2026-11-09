; trace.inc - a debugging aid, built only with -dTRACE (DOSXT.EXE):
; every call of the ring-0 gate (INT 21h, INT 31h, reflected
; interrupts, default exception handlers), its result, every exception
; and IRQ through vectors 00h-1Fh, every real-mode callback or hook goes
; into a ring buffer at the top of extended memory; when DOSX exits, the
; buffer is written to C:\DOSX.TRC (tools/dosxtrc.py prints it).
;
; A record is 16 words: kind, AX, BX, CX, DX, SI, DI, BP, DS, ES, then
; four words that depend on the kind, then a 32-bit sequence number.
;   kind 00xxh  gate operation xx: client IP, CS, FLAGS, the argument
;   kind 80xxh  its result: the same, after
;   kind 01nnh  exception/interrupt nn: IP, CS, error code, FLAGS
;   kind 02nnh  callback (nn = 0) or real-mode hook nn: RM CS:IP, SS, SP

%ifdef TRACE
G_TRC           equ 0E8h
TRC_SIZE        equ 40000h      ; 8192 records
TRC_RECS        equ 8192
%ifndef TRC_BATCH
%define TRC_BATCH 1   ; records to collect before writing (-dTRC_BATCH=n)
%endif

; trc: AX = kind, BP = a gate/exception frame, trc_x = 4 more words
trc:
%ifdef TRC4
        push ds
        push bx
        mov bx, G_HD0
        mov ds, bx
        pop bx
        pop ds
        ret
%endif
%ifdef TRC5
        add word [ss:msgbuf+200], 1
        ret
%endif
%ifdef TRCT
        push cx
        mov cx, TRCT
.dl:    loop .dl
        pop cx
        ret
%endif
%ifdef TRCH
        push ds
        push bx
        mov bx, G_HD0
        mov ds, bx
%ifdef TRCHM
        add word [msgbuf+200], 1
%else
        add word [trc_n], 1
%endif
        pop bx
        pop ds
        ret
%endif
        pusha
        push es
        push ds
        mov bx, G_HD0
        mov ds, bx
        mov bx, [trc_n]
        and bx, TRC_RECS - 1
        mov dx, bx
        shr dx, 11              ; 64 KB pieces: 2048 records each
        and bx, 2047
        shl bx, 5
        push ax
        push di
        mov ax, [trc_lin]
        add dx, [trc_lin+2]
        mov di, G_TRC
        mov cx, 0FFFFh
        call gdt_base_lin
        pop di
        pop ax
        mov cx, G_TRC
        mov es, cx
        mov [es:bx], ax
        mov ax, [bp+F_AX]
        mov [es:bx+2], ax
        mov ax, [bp+F_BX]
        mov [es:bx+4], ax
        mov ax, [bp+F_CX]
        mov [es:bx+6], ax
        mov ax, [bp+F_DX]
        mov [es:bx+8], ax
        mov ax, [bp+F_SI]
        mov [es:bx+10], ax
        mov ax, [bp+F_DI]
        mov [es:bx+12], ax
        mov ax, [bp+F_BP]
        mov [es:bx+14], ax
        mov ax, [bp+F_DS]
        mov [es:bx+16], ax
        mov ax, [bp+F_ES]
        mov [es:bx+18], ax
        mov ax, [trc_x]
        mov [es:bx+20], ax
        mov ax, [trc_x+2]
        mov [es:bx+22], ax
        mov ax, [trc_x+4]
        mov [es:bx+24], ax
        mov ax, [trc_x+6]
        mov [es:bx+26], ax
        mov ax, [trc_n]
        mov [es:bx+28], ax
        mov ax, [trc_n+2]
        mov [es:bx+30], ax
        add word [trc_n], 1
        adc word [trc_n+2], 0
        pop ds
        pop es
        popa
        ret

; trc_gate: before (AX = 0) or after (AX = 8000h) a gate operation
trc_gate:
        push es
        push si
        push bx
        les si, [bp+F_SP3]
        mov bx, [es:si+K_IP]
        mov [trc_x], bx
        mov bx, [es:si+K_CS]
        mov [trc_x+2], bx
        mov bx, [es:si+K_FL]
        mov [trc_x+4], bx
        mov bx, [es:si+K_ARG]
        mov [trc_x+6], bx
        or ax, [es:si+K_OP]
        pop bx
        pop si
        pop es
        call trc
        ret

; trc_flush (ring 0, between gate operations): append the records not
; written yet to C:\DOSX.TRC (created by DOSX at start), through
; real-mode DOS. So a trace survives a hang.
trc_flush:
        cmp word [rmx_depth], 0  ; not while real-mode code (DOS) is
        jne .ret                 ; waiting for this to return
        push es
        push si
        les si, [bp+F_SP3]       ; only after INT 31h: after an interrupt
        mov si, [es:si+K_OP]     ; DOS's caller may be anywhere, after
        cmp si, OP_INT31         ; INT 21h the client may ask for DOS's
        pop si                   ; extended error (59h)
        pop es
        jne .ret
        pusha
        push es
        mov ax, [trc_n]
        mov dx, [trc_n+2]
        sub ax, [trc_done]
        sbb dx, [trc_done+2]
        or dx, dx
        jnz .lots
        cmp ax, [trc_batch]
        jb .d
        cmp ax, TRC_RECS
        jb .some
.lots:  ; more than the ring holds: skip to the oldest one left
        mov ax, [trc_n]
        mov dx, [trc_n+2]
        sub ax, TRC_RECS - 1
        sbb dx, 0
        mov [trc_done], ax
        mov [trc_done+2], dx
.some:  mov ax, [trc_done]
        cmp ax, [trc_n]
        jne .go
        mov ax, [trc_done+2]
        cmp ax, [trc_n+2]
        je .d
.go:    ; one record at a time into trc_stage, 32 per write
        xor cx, cx
        mov di, trc_stage
.rec:   mov ax, [trc_done]
        cmp ax, [trc_n]
        jne .r1
        mov ax, [trc_done+2]
        cmp ax, [trc_n+2]
        je .wr
.r1:    mov bx, [trc_done]
        and bx, TRC_RECS - 1
        mov dx, bx
        shr dx, 11
        and bx, 2047
        shl bx, 5
        push di
        push cx
        mov ax, [trc_lin]
        add dx, [trc_lin+2]
        mov di, G_TRC
        mov cx, 0FFFFh
        call gdt_base_lin
        pop cx
        pop di
        push ds
        push ds
        pop es
        mov ax, G_TRC
        mov ds, ax
        mov si, bx
        push cx
        mov cx, 16
        rep movsw
        pop cx
        pop ds
        add word [trc_done], 1
        adc word [trc_done+2], 0
        inc cx
        cmp cx, 32
        jb .rec
.wr:    jcxz .d
        shl cx, 5
        mov [trc_wlen], cx
        sub sp, R_SIZE
        mov bx, sp
        call trc_rs0
        mov word [bx+R_EAX], 3D01h
        mov word [bx+R_EDX], s_trcname
        call trc_dos
        test word [bx+R_FL], FL_CF
        jnz .x
        mov ax, [bx+R_EAX]
        mov [trc_h], ax
        call trc_rs0
        mov word [bx+R_EAX], 4202h
        mov ax, [trc_h]
        mov [bx+R_EBX], ax
        call trc_dos
        call trc_rs0
        mov word [bx+R_EAX], 4000h
        mov ax, [trc_h]
        mov [bx+R_EBX], ax
        mov ax, [trc_wlen]
        mov [bx+R_ECX], ax
        mov word [bx+R_EDX], trc_stage
        call trc_dos
        call trc_rs0
        mov word [bx+R_EAX], 3E00h
        mov ax, [trc_h]
        mov [bx+R_EBX], ax
        call trc_dos
.x:     add sp, R_SIZE
        jmp .some
.d:     pop es
        popa
.ret:   ret
trc_rs0:
        push es
        push di
        push cx
        push ss
        pop es
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        rep stosw
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        pop cx
        pop di
        pop es
        ret
trc_dos:
        mov ax, 2100h
        xor cx, cx
        jmp r0_rmcall

; trc_create (real mode, at start): an empty C:\DOSX.TRC
trc_create:
        mov dx, s_trcname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .r
        mov bx, ax
        mov ah, 3Eh
        int 21h
.r:     ret

s_trcname       db 'C:\DOSX.TRC', 0
trc_n           dd 0
trc_lin         dd 0
trc_x           dw 0, 0, 0, 0
trc_h           dw 0
trc_done        dd 0
trc_batch       dw TRC_BATCH
trc_wlen        dw 0
trc_stage       times 32 * 32 db 0
%endif

; TRACE_GATE / TRACE_RESULT / TRACE_EXC: nothing without -dTRACE
%macro TRACE_GATE 0
%ifdef TRACE
        push ax
        xor ax, ax
        call trc_gate
        pop ax
%endif
%endmacro
%macro TRACE_RESULT 0
%ifdef TRACE
        push ax
        mov ax, 8000h
        call trc_gate
%ifndef NOFLUSH
        call trc_flush
%endif
        pop ax
%endif
%endmacro
