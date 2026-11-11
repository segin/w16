; mevent.asm - call USER's mouse_event (USER.299) the way MOUSE.DRV does:
; arguments in registers (Windows 3.1 DDK, Mouse_Event: AX status, BX x,
; CX y, DX buttons, SI = DI = 0). For probes that feed real mouse input
; through USER's event procedure from a system-timer procedure, i.e. at
; interrupt time like the mouse driver (tests/apps/us6.c).
;   void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons)
extern U_MOUSE_EVENT

segment _TEXT class=CODE
global _call_mouse_event
_call_mouse_event:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp + 6]
        mov bx, [bp + 8]
        mov cx, [bp + 10]
        mov dx, [bp + 12]
        xor si, si
        xor di, di
        call far U_MOUSE_EVENT
        pop di
        pop si
        pop bp
        retf
