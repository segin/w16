; us29s.asm - call USER's keybd_event (USER.289) the way KEYBOARD.DRV
; does: arguments in registers (Windows 3.1 DDK: AL virtual key, AH bit 7
; set for a release, BL scan code, BH bit 0 an extended key, SI = DI = 0).
; For probe US29 (tests/apps/us29.c).
;   void __cdecl call_keybd_event(WORD ax, WORD bx)
extern U_KEYBD_EVENT

segment _TEXT class=CODE
global _call_keybd_event
_call_keybd_event:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp + 6]
        mov bx, [bp + 8]
        xor si, si
        xor di, di
        call far U_KEYBD_EVENT
        pop di
        pop si
        pop bp
        retf
