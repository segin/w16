; vidmode.asm - VIDMODE.COM: exits with the BIOS video mode as its exit
; code (INT 10h AH=0Fh), for a batch file's IF ERRORLEVEL (boot-test
; lastexit: the screen after Windows ends)
        org 100h
        mov ah, 0Fh
        int 10h
        and al, 7Fh
        mov ah, 4Ch
        int 21h
