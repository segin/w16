; sn1s.asm - SN1's port-level helpers (tests/apps/sn1.c,
; drivers/sound/README.md): the speaker's state as the hardware shows it.
        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

; WORD __cdecl __far spk_state(void): AL = port 61h, AH = PIT channel 2's
; status byte (8254 read-back command E8h: status only, counter 2): bit 7
; the OUT pin, bits 5-4 the access mode, bits 3-1 the mode, bit 0 BCD
global _spk_state
_spk_state:
        pushf
        cli
        mov al, 0E8h
        out 43h, al
        in al, 42h
        mov ah, al
        in al, 61h
        popf
        retf

; WORD __cdecl __far pit2_count(void): channel 2's count, latched
global _pit2_count
_pit2_count:
        pushf
        cli
        mov al, 80h
        out 43h, al
        in al, 42h
        mov ah, al
        in al, 42h
        xchg al, ah
        popf
        retf

; void __cdecl __far spk_on(WORD divisor): channel 2 square wave (mode 3)
; at 1193182 / divisor Hz, the speaker on (port 61h bits 0 and 1): SN1's
; marker tones, which put the parts of the run on the capture's time line
global _spk_on
_spk_on:
        push bp
        mov bp, sp
        pushf
        cli
        mov al, 0B6h
        out 43h, al
        mov ax, [bp + 6]
        out 42h, al
        mov al, ah
        out 42h, al
        in al, 61h
        or al, 3
        out 61h, al
        popf
        pop bp
        retf

; void __cdecl __far spk_off(void): the speaker off (port 61h bits 0, 1)
global _spk_off
_spk_off:
        pushf
        cli
        in al, 61h
        and al, 0FCh
        out 61h, al
        popf
        retf

; WORD __cdecl __far pit0_count(void): channel 0's count, latched (its
; largest value over a while is the reload value: is the timer
; reprogrammed while notes play?)
global _pit0_count
_pit0_count:
        pushf
        cli
        xor al, al
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        popf
        retf

; WORD __cdecl __far pit0_status(void): channel 0's status byte
; (read-back E2h: status only, counter 0)
global _pit0_status
_pit0_status:
        pushf
        cli
        mov al, 0E2h
        out 43h, al
        in al, 40h
        xor ah, ah
        popf
        retf
