; winenv.asm - WINENV.COM, a test tool: run a program with "windir=DIR"
; added to (a copy of) the environment, as WIN.COM gives Windows its
; Windows directory (observed on Windows 3.1: every task's environment
; holds lowercase "windir=C:\WINDOWS"; COMMAND.COM's SET would upper-case
; the name). Until w16 has its own WIN.COM (or LOSE.COM passes it),
; tests/run-tests.sh starts KERNEL through this.
;
;   usage: WINENV DIR PROGRAM.EXE [arguments]
; Exits with the program's exit code (FFh if it cannot be started).

        bits 16
        cpu 8086
        org 100h

start:
        mov sp, stack_top
        ; keep only this program's memory (64K COM image is far too much)
        mov bx, (end_of_image - start + 100h + 15) / 16
        mov ah, 4Ah
        int 21h
        jc fail

        ; parse: DIR, PROGRAM, rest
        mov si, 81h
        call skip_blanks
        mov [dir_ptr], si
        call skip_word
        mov [dir_end], si
        call skip_blanks
        mov [prog_ptr], si
        call skip_word
        cmp si, [prog_ptr]
        je fail
        mov al, [si]                    ; the separator
        mov byte [si], 0                ; PROGRAM is now ASCIIZ (in our PSP)
        inc si
        ; child command tail: a blank and the rest of ours
        mov di, tail + 1
        xor cx, cx
        cmp al, 0Dh
        je .tailend
        mov al, ' '
        stosb
        inc cx
.tail:
        lodsb
        cmp al, 0Dh
        je .tailend
        stosb
        inc cx
        jmp .tail
.tailend:
        mov al, 0Dh
        stosb
        mov [tail], cl

        ; environment: ours, then "windir=DIR", then the closing NUL
        push ds
        mov ax, [2Ch]
        mov ds, ax
        xor si, si
        mov di, envbuf
.copy:
        cmp byte [si], 0
        je .copied
.str:
        lodsb
        stosb
        or al, al
        jnz .str
        cmp di, envbuf_end - 300        ; leave room for windir
        jb .copy
.copied:
        pop ds
        mov si, s_windir
.wd:
        lodsb
        or al, al
        jz .wddone
        stosb
        jmp .wd
.wddone:
        mov si, [dir_ptr]
        mov cx, [dir_end]
        sub cx, si
        rep movsb
        xor al, al
        stosb
        stosb                           ; end of the environment

        ; EXEC
        mov ax, envbuf
        mov cl, 4
        shr ax, cl
        mov bx, cs
        add ax, bx
        mov [pb_env], ax
        mov [pb_tail + 2], cs
        mov [pb_fcb1 + 2], cs
        mov [pb_fcb2 + 2], cs
        mov dx, [prog_ptr]
        mov bx, pblock
        mov ax, 4B00h
        int 21h
        jc fail
        mov ah, 4Dh
        int 21h
        mov ah, 4Ch
        int 21h
fail:
        mov ax, 4CFFh
        int 21h

skip_blanks:
        cmp byte [si], ' '
        je .next
        cmp byte [si], 9
        jne .done
.next:
        inc si
        jmp skip_blanks
.done:
        ret

skip_word:
        mov al, [si]
        cmp al, ' '
        je .done
        cmp al, 9
        je .done
        cmp al, 0Dh
        je .done
        inc si
        jmp skip_word
.done:
        ret

s_windir        db "windir=", 0
dir_ptr         dw 0
dir_end         dw 0
prog_ptr        dw 0
pblock:
pb_env          dw 0
pb_tail         dw tail, 0
pb_fcb1         dw 5Ch, 0
pb_fcb2         dw 6Ch, 0
tail            times 130 db 0

        align 16
envbuf          times 2048 db 0         ; DOS: an environment is < 32K;
envbuf_end:                             ; 2K is plenty for a test
                times 256 db 0
stack_top:
end_of_image:
