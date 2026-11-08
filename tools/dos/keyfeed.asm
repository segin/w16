; keyfeed.asm - KEYFEED.COM: run a DOS program with scripted keyboard input
; and record its text screens. A test tool for driving interactive setup
; programs headlessly (black-box observation; see tests/w386setup.sh).
;
;   KEYFEED stream.kfs screens.txt C:\PATH\PROGRAM.EXE [arguments]
;
; stream.kfs comes from tools/kfcomp.py: little-endian words, each a BIOS
; key (scan << 8 | ASCII), FFFFh = record the screen, FFFEh n = hold keys
; back for n timer ticks. KEYFEED hooks INT 16h, runs the program, and
; answers its keyboard reads from the stream. When the stream is used up
; the program is ended: directly (INT 21h AH=4Ch, return code FEh) when
; DOS is not busy, otherwise by answering Ctrl-C, which DOS turns into a
; termination. Afterwards KEYFEED writes every recorded screen, plus the
; final one, to screens.txt as plain text, and exits with the program's
; return code.
;
; A key is reported by a status call (AH=01h/11h) only on the second
; consecutive one. DOS's "flush input" (INT 21h AH=0Ch) polls once and
; stops, so it does not swallow scripted keys; a program that polls in a
; loop sees them on its next poll.
;
; Build: nasm -f bin -o KEYFEED.COM keyfeed.asm

        bits 16
        cpu 8086
        org 100h

start:
        mov sp, stack_top
        mov bx, (prog_end - $$ + 100h + 15) / 16  ; keep just this program
        mov ah, 4Ah
        int 21h

        ; ---- command line ----
        mov si, 81h
        mov di, fn_stream
        call token
        jc usage
        mov di, fn_out
        call token
        jc usage
        mov di, fn_prog
        call token
        jc usage
        mov di, child_tail + 1          ; the rest is the child's tail
        xor cx, cx
.rest:  lodsb
        cmp al, 0Dh
        je .restend
        stosb
        inc cx
        jmp .rest
.restend:
        mov al, 0Dh
        stosb
        mov [child_tail], cl

        ; ---- load the key stream ----
        mov ax, 3D00h
        mov dx, fn_stream
        int 21h
        jc err_stream
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        or dx, dx
        jnz err_stream                  ; a stream is far below 64K
        mov [stream_len], ax
        mov ax, 4200h
        xor cx, cx
        xor dx, dx
        int 21h
        push bx
        mov bx, [stream_len]
        mov cl, 4
        shr bx, cl
        inc bx
        mov ah, 48h
        int 21h
        pop bx
        jc err_mem
        mov [stream_seg], ax
        push ds
        mov cx, [stream_len]
        mov ds, ax
        xor dx, dx
        mov ah, 3Fh
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h

        ; ---- room for every recorded screen, plus the final one ----
        mov es, [stream_seg]
        xor di, di
        mov dx, 1
.count: cmp di, [stream_len]
        jae .counted
        mov ax, [es:di]
        add di, 2
        cmp ax, 0FFFFh
        jne .notsnap
        inc dx
        jmp .count
.notsnap:
        cmp ax, 0FFFEh
        jne .count
        add di, 2
        jmp .count
.counted:
        mov [snap_max], dx
        mov ax, SNAP_PARAS
        mul dx
        or dx, dx
        jnz err_mem
        mov bx, ax
        mov ah, 48h
        int 21h
        jc err_mem
        mov [snap_seg], ax

        ; ---- hook INT 16h and run the program ----
        mov ah, 34h                     ; where DOS keeps its InDOS flag
        int 21h
        mov [indos], bx
        mov [indos + 2], es
        mov ax, 3516h
        int 21h
        mov [old16], bx
        mov [old16 + 2], es
        mov ax, 2516h
        mov dx, int16_handler
        int 21h

        mov [pb_tail + 2], cs
        mov [pb_fcb1 + 2], cs
        mov [pb_fcb2 + 2], cs
        mov [save_sp], sp
        push cs
        pop es
        mov bx, param_block
        mov dx, fn_prog
        mov ax, 4B00h
        int 21h
        cli
        mov bx, cs
        mov ss, bx
        mov sp, [cs:save_sp]
        sti
        push cs
        pop ds
        mov byte [retcode], 0FFh
        jc .ran
        mov ah, 4Dh
        int 21h
        mov [retcode], al
.ran:
        push ds
        lds dx, [old16]
        mov ax, 2516h
        int 21h
        pop ds

        call do_snap                    ; the final screen
        call write_screens
        mov al, [retcode]
        mov ah, 4Ch
        int 21h

usage:
        mov dx, msg_usage
        jmp fail
err_stream:
        mov dx, msg_stream
        jmp fail
err_mem:
        mov dx, msg_mem
fail:
        mov ah, 9
        int 21h
        mov ax, 4CFFh
        int 21h

; Copy the next blank-separated word from DS:SI to DS:DI, NUL-terminated.
; CF=1 if there is none.
token:
.skip:  lodsb
        cmp al, ' '
        je .skip
        cmp al, 9
        je .skip
        cmp al, 0Dh
        je .none
.copy:  stosb
        lodsb
        cmp al, ' '
        je .end
        cmp al, 9
        je .end
        cmp al, 0Dh
        jne .copy
.end:   dec si                          ; leave the separator
        xor al, al
        stosb
        clc
        ret
.none:  dec si
        stc
        ret

; ---- the INT 16h hook ----
int16_handler:
        cmp ah, 00h
        je h_read
        cmp ah, 10h
        je h_read
        cmp ah, 01h
        je h_peek
        cmp ah, 11h
        je h_peek
chain:  jmp far [cs:old16]

h_read:                                 ; blocking read
        sti
        push ax
        push dx
.again: call prepare
        cmp al, 2
        je .key
        cmp al, 0
        je .end
        hlt                             ; holding: wait for a timer tick
        jmp .again
.key:   add word [cs:pos], 2
        mov byte [cs:pollflag], 0
        mov ax, dx
        pop dx
        add sp, 2
        iret
.end:   call stream_end                 ; returns only if DOS is busy
        pop dx
        add sp, 2
        mov ax, CTRL_C
        iret

h_peek:                                 ; status: ZF=1 none, else AX = key
        push ax
        push dx
        cmp byte [cs:pollflag], 0
        jne .commit
        mov dx, [cs:pos]
        cmp dx, [cs:stream_len]
        jae .end
        mov byte [cs:pollflag], 1
        jmp .none
.commit:
        call prepare
        cmp al, 2
        je .key
        cmp al, 0
        je .end
.none:  pop dx
        pop ax
        cmp ax, ax                      ; ZF=1
        sti
        retf 2
.key:   mov ax, dx                      ; never 0: ZF=0 below
        pop dx
        add sp, 2
        or ax, ax
        sti
        retf 2
.end:   call stream_end
        pop dx
        add sp, 2
        mov ax, CTRL_C
        or ax, ax
        sti
        retf 2

; The stream is used up: end the program. Inside a DOS call (InDOS set)
; it is not safe to call DOS, so return and let the caller answer Ctrl-C.
CTRL_C equ 2E03h
stream_end:
        push bx
        push es
        les bx, [cs:indos]
        cmp byte [es:bx], 0
        pop es
        pop bx
        jne .busy
        mov ax, 4CFEh                   ; ends the program, back to KEYFEED
        int 21h
.busy:  ret

; Process screen and wait markers at the stream position.
; Out: AL = 0 stream done, 1 holding (waiting), 2 key in DX (not consumed).
prepare:
        push si
        push es
        mov es, [cs:stream_seg]
.loop:  mov si, [cs:pos]
        cmp si, [cs:stream_len]
        jae .done
        mov dx, [es:si]
        cmp dx, 0FFFFh
        jne .notsnap
        call do_snap
        add word [cs:pos], 2
        jmp .loop
.notsnap:
        cmp dx, 0FFFEh
        jne .key
        push ds
        mov ax, 40h
        mov ds, ax
        mov ax, [6Ch]                   ; BIOS tick count, low word
        pop ds
        cmp byte [cs:waiting], 0
        jne .check
        add ax, [es:si + 2]
        mov [cs:wait_until], ax
        mov byte [cs:waiting], 1
        jmp .hold
.check: sub ax, [cs:wait_until]
        js .hold
        mov byte [cs:waiting], 0
        add word [cs:pos], 4
        jmp .loop
.hold:  mov al, 1
        jmp .out
.done:  mov al, 0
        jmp .out
.key:   mov al, 2
.out:   pop es
        pop si
        ret

; Record the 80x25 text screen (page 0).
SNAP_PARAS equ 250                      ; 4000 bytes
do_snap:
        push ax
        push cx
        push dx
        push si
        push di
        push ds
        push es
        mov ax, [cs:nsnap]
        cmp ax, [cs:snap_max]
        jae .full
        inc word [cs:nsnap]
        mov cx, SNAP_PARAS
        mul cx
        add ax, [cs:snap_seg]
        mov es, ax
        xor di, di
        mov ax, 40h
        mov ds, ax
        mov ax, 0B800h
        cmp byte [49h], 7               ; monochrome text mode
        jne .color
        mov ax, 0B000h
.color: mov ds, ax
        xor si, si
        mov cx, 2000
        cld
        rep movsw
.full:  pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop ax
        ret

; ---- output: every screen as 25 trimmed lines of text ----
write_screens:
        mov ah, 3Ch
        xor cx, cx
        mov dx, fn_out
        int 21h
        jc .ret
        mov [outh], ax
        mov word [cur], 0
.screen:
        mov ax, [cur]
        cmp ax, [nsnap]
        jae .close
        mov dx, msg_hdr
        mov cx, msg_hdr_len
        call write
        mov ax, [cur]
        call write_dec
        mov dx, msg_hdr2
        mov cx, msg_hdr2_len
        call write
        mov ax, [cur]
        mov cx, SNAP_PARAS
        mul cx
        add ax, [snap_seg]
        mov [cur_seg], ax
        xor si, si
        mov word [row], 25
.row:   push cs
        pop es
        mov di, linebuf
        mov cx, 80
        push ds
        mov ds, [cs:cur_seg]
.ch:    lodsw
        or al, al
        jnz .put
        mov al, ' '
.put:   stosb
        loop .ch
        pop ds
.trim:  cmp di, linebuf
        je .eol
        cmp byte [di - 1], ' '
        jne .eol
        dec di
        jmp .trim
.eol:   mov ax, 0A0Dh
        stosw
        mov cx, di
        sub cx, linebuf
        mov dx, linebuf
        push si
        call write
        pop si
        dec word [row]
        jnz .row
        inc word [cur]
        jmp .screen
.close: mov bx, [outh]
        mov ah, 3Eh
        int 21h
.ret:   ret

write:                                  ; DS:DX, CX bytes
        mov bx, [outh]
        mov ah, 40h
        int 21h
        ret

write_dec:                              ; AX
        mov di, numbuf + 5
        mov bx, 10
.d:     xor dx, dx
        div bx
        add dl, '0'
        dec di
        mov [di], dl
        or ax, ax
        jnz .d
        mov dx, di
        mov cx, numbuf + 5
        sub cx, di
        jmp write

; ---- data ----
msg_usage   db "usage: KEYFEED stream.kfs screens.txt program [args]", 13, 10, "$"
msg_stream  db "KEYFEED: cannot read the key stream", 13, 10, "$"
msg_mem     db "KEYFEED: not enough memory", 13, 10, "$"
msg_hdr     db "=== screen "
msg_hdr_len equ $ - msg_hdr
msg_hdr2    db " ===", 13, 10
msg_hdr2_len equ $ - msg_hdr2

old16       dw 0, 0
indos       dw 0, 0
pos         dw 0
pollflag    db 0
waiting     db 0
wait_until  dw 0
stream_seg  dw 0
stream_len  dw 0
snap_seg    dw 0
snap_max    dw 0
nsnap       dw 0
save_sp     dw 0
retcode     db 0
outh        dw 0
cur         dw 0
cur_seg     dw 0
row         dw 0

param_block:
pb_env      dw 0
pb_tail     dw child_tail, 0
pb_fcb1     dw 5Ch, 0
pb_fcb2     dw 6Ch, 0

numbuf      times 6 db 0
fn_stream   times 80 db 0
fn_out      times 80 db 0
fn_prog     times 80 db 0
child_tail  times 130 db 0
linebuf     times 84 db 0

            align 2
            times 512 db 0
stack_top:
prog_end:
