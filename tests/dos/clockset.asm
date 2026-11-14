; clockset.asm - CLOCKSET.COM: sets DOS's date and time to a fixed moment,
; Monday 15 June 1992, 10:00:00, so that screens never show the host's
; clock (tests/dropin.sh runs it before WIN in every scratch boot; a
; Windows 3.1 reference cached one day was compared with a w16 run made
; the next, and Clock's minimised title showed the two dates).
; DOSBox 0.74's shell has no DATE or TIME command and its DOS sets no
; time (INT 21h AH=2Dh): the time of day is set where DOS reads it, the
; BIOS tick count (INT 1Ah AH=01h); INT 21h AH=2Dh as well, for DOSBox-X.
        org 100h
        mov ah, 2Bh             ; set date
        mov cx, 1992
        mov dh, 6
        mov dl, 15
        int 21h
        ; 10:00:00 in BIOS ticks: 10 h x 65543.33 ticks = 655434 = 0A004Ah (655433 read back as 9:59:59)
        mov ah, 01h             ; set the tick count CX:DX
        mov cx, 000Ah
        mov dx, 004Ah
        int 1Ah
        mov ah, 2Dh             ; set time 10:00:00.00
        mov ch, 10
        xor cl, cl
        xor dx, dx
        int 21h
        mov ax, 4C00h
        int 21h
