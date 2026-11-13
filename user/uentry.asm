; uentry.asm - w16 USER.EXE: entry point and the register-level interfaces
; (ARCHITECTURE.md 10).
;
; libentry: KERNEL calls a DLL's entry point with DI = hInstance, CX = the
;   local heap size, DS = its data segment, ES:SI = the command line (TK5,
;   ARCHITECTURE.md 9.2); USER makes its local heap and runs user_init.
; call_wndproc: calls a window procedure with AX = the window's hInstance
;   and DS = ES = SS, as Windows does (Pietrek, Windows Internals, ch. 7:
;   DispatchMessage and SendMessage), so an exported procedure without a
;   MakeProcInstance thunk, or one with no prologue at all (w16's
;   WINOLDAP, compiled -zWs), finds its data segment. USER's own class
;   procedures are __loadds.
; keybd_event_entry / mouse_event_entry: the event procedures USER gives
;   the keyboard and mouse drivers (KEYBOARD.Enable, MOUSE.Enable); their
;   arguments come in registers (Windows 3.1 DDK, keyboard and mouse driver
;   interfaces), at interrupt time; every register is kept.
; lh_alloc, lh_realloc, lh_free, lh_size: KERNEL's LocalAlloc, LocalReAlloc,
;   LocalFree and LocalSize on the local heap in another segment (the menu
;   heaps, menu.c): DS is that segment for the call (the Local* functions
;   work on the caller's DS, 3.1 SDK) and USER's own again afterwards.
;   Never at interrupt time.
; at_init, at_add, at_find, at_delete, at_name: KERNEL's InitAtomTable,
;   AddAtom, FindAtom, DeleteAtom and GetAtomName on the atom table of the
;   local heap in another segment (the global atom table's heap, uheap.c),
;   the same way. Never at interrupt time.
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0           ; instance data (Windows' convention)

segment UENTRY_TEXT public align=16 class=CODE use16

extern LOCALINIT                ; KERNEL.4
extern LOCALALLOC, LOCALREALLOC, LOCALFREE, LOCALSIZE
extern INITATOMTABLE, ADDATOM, FINDATOM, DELETEATOM, GETATOMNAME
extern _user_init               ; int __cdecl user_init(WORD hinst) (C, far)
extern KBD_EVENT                ; void far pascal kbd_event(ax, bx, si, di)
extern MOUSE_EVENT_C            ; void far pascal mouse_event_c(ax, bx, cx, dx, si, di)

global libentry
libentry:
        push ds
        push si
        push di
        push es
        jcxz .noheap
        push ds                 ; LocalInit(DS, 0, CX)
        push word 0
        push cx
        call far LOCALINIT
.noheap:
        pop es
        pop di
        push di
        push di                 ; hInstance
        call far _user_init
        add sp, 2
        pop di
        pop si
        pop ds
        retf

; LONG __cdecl call_wndproc(WNDPROC f, WORD ax, HWND, UINT, WPARAM, LPARAM)
global _call_wndproc
_call_wndproc:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push word [bp+12]       ; hwnd
        push word [bp+14]       ; msg
        push word [bp+16]       ; wParam
        push word [bp+20]       ; lParam high
        push word [bp+18]       ; lParam low
        mov ax, [bp+10]
        or ax, ax
        jnz .ax
        mov ax, ss              ; (a timer procedure: AX = SS)
.ax:    mov dx, ss              ; DS = ES = SS: the calling task's data
        mov ds, dx              ; segment, for a procedure that is not
        mov es, dx              ; exported properly (Pietrek ch. 7)
        call far [bp+6]
        pop ds
        pop di
        pop si
        pop bp
        retf

global _keybd_event_entry, KEYBD_EVENT
_keybd_event_entry:
KEYBD_EVENT:
        pushf
        pusha
        push ds
        push es
        push ax
        push bx
        push si
        push di
        call far KBD_EVENT
        pop es
        pop ds
        popa
        popf
        retf

global _mouse_event_entry, MOUSE_EVENT
_mouse_event_entry:
MOUSE_EVENT:
        pushf
        pusha
        push ds
        push es
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        call far MOUSE_EVENT_C
        pop es
        pop ds
        popa
        popf
        retf

; WORD __far __cdecl lh_alloc(WORD sel, UINT flags, UINT size)
global _lh_alloc
_lh_alloc:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]        ; flags
        push word [bp+10]       ; size
        call far LOCALALLOC
        pop ds
        pop bp
        retf

; WORD __far __cdecl lh_realloc(WORD sel, WORD h, UINT size, UINT flags)
global _lh_realloc
_lh_realloc:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]        ; h
        push word [bp+10]       ; size
        push word [bp+12]       ; flags
        call far LOCALREALLOC
        pop ds
        pop bp
        retf

; WORD __far __cdecl lh_free(WORD sel, WORD h)
global _lh_free
_lh_free:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far LOCALFREE
        pop ds
        pop bp
        retf

; UINT __far __cdecl lh_size(WORD sel, WORD h)
global _lh_size
_lh_size:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far LOCALSIZE
        pop ds
        pop bp
        retf

; BOOL __far __cdecl at_init(WORD sel, int n)
global _at_init
_at_init:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far INITATOMTABLE
        pop ds
        pop bp
        retf

; ATOM __far __cdecl at_add(WORD sel, LPCSTR s) (and at_find)
global _at_add
_at_add:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+10]       ; s (segment)
        push word [bp+8]        ; s (offset)
        call far ADDATOM
        pop ds
        pop bp
        retf

global _at_find
_at_find:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+10]
        push word [bp+8]
        call far FINDATOM
        pop ds
        pop bp
        retf

; ATOM __far __cdecl at_delete(WORD sel, ATOM a)
global _at_delete
_at_delete:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]
        call far DELETEATOM
        pop ds
        pop bp
        retf

; UINT __far __cdecl at_name(WORD sel, ATOM a, LPSTR buf, int n)
global _at_name
_at_name:
        push bp
        mov bp, sp
        push ds
        mov ds, [bp+6]
        push word [bp+8]        ; atom
        push word [bp+12]       ; buf (segment)
        push word [bp+10]       ; buf (offset)
        push word [bp+14]       ; n
        call far GETATOMNAME
        pop ds
        pop bp
        retf

; WORD __far __cdecl seg_limit(WORD sel): the segment's limit (LSL), 0 if
; the selector is not one this privilege level may read
global _seg_limit
_seg_limit:
        push bp
        mov bp, sp
        lsl ax, [bp+6]
        jz .ok
        xor ax, ax
.ok:    pop bp
        retf

; the compiler's 32-bit helpers (no C run-time is linked), as in shell/start.asm
; __U4D: the compiler's unsigned 32-bit divide helper. DX:AX / CX:BX:
; quotient in DX:AX, remainder in CX:BX (shift and subtract, 32 steps).
; Preserves every other register.
global __U4D
__U4D:
        push si
        push di
        push bp
        xor si, si              ; SI:DI = running remainder
        xor di, di
        mov bp, 32
.step:  shl ax, 1               ; next dividend bit into the remainder;
        rcl dx, 1               ; the quotient bit lands in AX bit 0
        rcl di, 1
        rcl si, 1
        jc .sub                 ; remainder past 32 bits: surely >= divisor
        cmp si, cx
        jb .next
        ja .sub
        cmp di, bx
        jb .next
.sub:   sub di, bx
        sbb si, cx
        inc ax
.next:  dec bp
        jnz .step
        mov cx, si
        mov bx, di
        pop bp
        pop di
        pop si
        retf

; __U4M / __I4M: the compiler's 32-bit multiply helper (we link no C
; run-time). DX:AX = DX:AX * CX:BX, low 32 bits (same for signed).
; Preserves every register but DX:AX.
global __U4M, __I4M
__U4M:
__I4M:
        push si
        push di
        mov si, dx              ; a_hi
        mov di, ax              ; a_lo
        mul bx                  ; DX:AX = a_lo * b_lo
        push ax
        push dx
        mov ax, si
        mul bx                  ; AX = low(a_hi * b_lo)
        mov si, ax
        mov ax, di
        mul cx                  ; AX = low(a_lo * b_hi)
        add si, ax
        pop dx
        pop ax
        add dx, si
        pop di
        pop si
        retf

; __I4D: signed 32-bit divide, DX:AX / CX:BX: quotient in DX:AX, remainder
; (with the dividend's sign) in CX:BX, through __U4D
global __I4D
__I4D:
        push si
        xor si, si
        test dx, dx
        jns .p1
        neg dx
        neg ax
        sbb dx, 0
        xor si, 3               ; bit 0: quotient negative, bit 1: remainder
.p1:    test cx, cx
        jns .p2
        neg cx
        neg bx
        sbb cx, 0
        xor si, 1
.p2:    push cs
        call __U4D
        test si, 1
        jz .q
        neg dx
        neg ax
        sbb dx, 0
.q:     test si, 2
        jz .r
        neg cx
        neg bx
        sbb cx, 0
.r:     pop si
        retf
