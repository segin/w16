; wcb.asm - WCB.COM: writes the BIOS data area (0040:0000-00FFh) to
; C:\BDA.LOG and exits with 0. Run in a batch file after WIN (boot test
; winprompt: the text mode Windows leaves; 49h the video mode, 4Ah the
; columns, 84h the rows less one, 85h the character height) or in WSWAP's
; place (what WIN.COM leaves: 3.1's puts back the row count from before
; its logo). Black-box probe for w16's WIN.COM (ARCHITECTURE.md 9.5).

        bits 16
        cpu 8086
        org 100h

        mov ah, 3Ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc .x
        mov bx, ax
        push ds
        mov ax, 40h
        mov ds, ax
        xor dx, dx
        mov cx, 100h
        mov ah, 40h
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h
.x:     mov ax, 4C00h
        int 21h

fname:  db 'C:\BDA.LOG', 0
