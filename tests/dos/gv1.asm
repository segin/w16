; gv1.asm - GV1.COM: a DOS program that sets a video mode and draws a fixed
; screen, for the display-grabber tests (tests/grabber.sh, ARCHITECTURE.md
; 9.11): what the screen looks like after switching away and back shows
; what the grabber saved and restored.
;
;   GV1 mm [p]    mm = the BIOS mode in hex (00-07, 0D-13); with "p" the
;                 palette is changed too (palette registers, the DAC, the
;                 CGA palette, the blink bit)
;
; Text modes: every cell of page 0 (char = row*7+col, attribute = row*9+col)
; with "GV1 mm" on row 0, the cursor at row 10 column 20 with shape 0407h.
; CGA modes 4-6: the two banks with a byte pattern. Planar modes 0Dh-12h:
; each plane its own pattern (map mask). 13h: every pixel. Then "GV1 mm" at
; 0,0 through the BIOS (INT 10h AH=0Eh). It waits for keys: "q" ends it
; (the mode is left as it is), "t" writes a line of text through the BIOS
; (so a screen can change after a switch).

        bits 16
        cpu 8086
        org 100h

start:
        mov si, 81h
        call skip
        call hexbyte
        mov [mode], al
        call skip
        cmp byte [si], 'p'
        jne .nop
        mov byte [pal], 1
.nop:
        call gv_draw
        jmp wait_keys

wait_keys:
        mov ah, 0
        int 16h
        cmp al, 'q'
        je .q
        cmp al, 't'
        jne wait_keys
        mov si, more
.t:     lodsb
        or al, al
        jz wait_keys
        mov ah, 0Eh
        mov bx, 000Eh
        int 10h
        jmp .t
.q:     mov ax, 4C00h
        int 21h

skip:   cmp byte [si], ' '
        jne .e
        inc si
        jmp skip
.e:     ret

hexbyte:
        mov ax, [si]
        mov [title+4], ax
        xor ah, ah
        mov cx, 2
.l:     mov al, [si]
        sub al, '0'
        cmp al, 9
        jbe .d
        and al, 0DFh
        sub al, 7
.d:     shl ah, 1
        shl ah, 1
        shl ah, 1
        shl ah, 1
        or ah, al
        inc si
        loop .l
        mov al, ah
        ret


%include "gvdraw.inc"
