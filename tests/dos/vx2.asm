; vx2.asm - VX2.COM: put keystrokes into the BIOS keyboard buffer (INT 16h
; AH=05h) and exit, so that a program run next that waits for a key (e.g.
; WIN386.EXE after a start-up error, "press any key") goes on in a headless
; test. The tail selects the keys: "e" Enter (the default), "y" Y, "n" N,
; "x" Esc, "s" a space; each letter one key, up to 15 (the BIOS buffer).
; Black-box test helper for tests/vxobs.sh (ARCHITECTURE.md 12).

        bits 16
        cpu 8086
        org 100h

start:  xor cx, cx
        mov cl, [80h]
        mov si, 81h
        xor bx, bx                      ; keys stuffed
.n:     jcxz .end
        lodsb
        dec cx
        mov dx, 1C0Dh                   ; Enter
        cmp al, 'e'
        je .k
        mov dx, 1579h                   ; y
        cmp al, 'y'
        je .k
        mov dx, 316Eh                   ; n
        cmp al, 'n'
        je .k
        mov dx, 011Bh                   ; Esc
        cmp al, 'x'
        je .k
        mov dx, 3920h                   ; space
        cmp al, 's'
        je .k
        jmp .n
.k:     push cx
        mov cx, dx
        mov ah, 05h
        int 16h
        pop cx
        inc bx
        jmp .n
.end:   test bx, bx
        jnz .q
        mov cx, 1C0Dh
        mov ah, 05h
        int 16h
.q:     mov ax, 4C00h
        int 21h
