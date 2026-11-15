; gp.asm - w16 USER.EXE: the code that touches a caller's memory, and
; USER's "__GP" table (ARCHITECTURE.md 9.4 and 10, probe US27).
;
; Windows 3.1's USER lets a bad pointer fault and recovers: KERNEL's fault
; handler looks the faulting CS:IP up in the table the module exports as
; "__GP" (resident, USER.484; Schulman, Undocumented Windows ch. 5, _GP
; and HasGPHandler) and resumes at the entry's recovery offset. An entry
; is { code selector, low offset, high offset, recovery offset }, the
; range from low up to (not including) high; a 0 selector ends the table.
; The recovery code is entered with every register as at the fault, SP 4
; lower: [SP] the faulting IP, [SP+2] the error code (TK40;
; kernel/fault.c, gp_handler).
;
; w16's USER does the same, with the primitives below: every load of a
; caller's selector and every access through it is inside a range, whose
; recovery code makes the primitive return how far it got. The C code
; calls them for caller-supplied pointers and returns what Windows 3.1's
; function returns for a bad one (US27). The table is made from the
; GPRANGE markers in this file when it is assembled, never from
; Microsoft's.
        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

; the table, in its own code segment so that its entries stay together
segment UGPTAB_TEXT public align=2 class=CODE use16
global __GP
__GP:

segment UGP_TEXT public align=16 class=CODE use16

; GPRANGE low, high, recovery: a table entry (labels in UGP_TEXT)
%macro GPRANGE 3
segment UGPTAB_TEXT
        dw seg %1, %1, %2, %3
segment UGP_TEXT
%endmacro

; BOOL __cdecl __far gp_sel(const void __far *p)
; Loads p's selector (into ES): 0 if that faults (a freed or not-present
; selector, as US27's F and P cases), else 1. A null selector loads.
; Windows 3.1's functions given such a pointer fault on its first use
; and return without doing anything (US27); w16's check it first.
global _gp_sel
_gp_sel:
        push bp
        mov bp, sp
        push es
        xor ax, ax
.lo:    mov es, [bp + 8]
        inc ax
.hi:    pop es
        pop bp
        retf
.rec:   add sp, 4
        xor ax, ax
        jmp .hi
        GPRANGE .lo, .hi, .rec

; NULL_SEL: memory through a null selector faults on the access (the
; selector itself loads), so the primitives take a null selector for a
; fault at the first byte without trying: the emulators the tests run in
; (DOSBox, DOSBox-X) do not fault there but use linear address 0, the
; real-mode interrupt table (US27: GetKeyboardState(NULL) wrote 256
; bytes over it).

; WORD __cdecl __far gp_copy(void __far *d, const void __far *s, WORD n)
; Copies n bytes forward, one at a time; returns how many were copied
; before a fault (n: all of them). Either pointer may be a caller's.
global _gp_copy
_gp_copy:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push es
        mov cx, [bp + 14]
        xor ax, ax
        cmp [bp + 8], ax                ; a null selector: nothing (NULL_SEL)
        je .out
        cmp [bp + 12], ax
        je .out
        cld
.lo:    lds si, [bp + 10]
        les di, [bp + 6]
        rep movsb
.hi:    mov ax, [bp + 14]
        jmp .out
.rec:   add sp, 4                       ; the faulting IP and error code
        mov ax, [bp + 14]
        sub ax, cx                      ; bytes moved (rep keeps CX current)
.out:   pop es
        pop di
        pop si
        pop ds
        pop bp
        retf
        GPRANGE .lo, .hi, .rec

; WORD __cdecl __far gp_fill(void __far *d, BYTE v, WORD n)
; Stores v in n bytes forward; returns how many before a fault.
global _gp_fill
_gp_fill:
        push bp
        mov bp, sp
        push di
        push es
        mov cx, [bp + 12]
        xor ax, ax
        cmp [bp + 8], ax                ; a null selector: nothing (NULL_SEL)
        je .out
        mov al, [bp + 10]
        cld
.lo:    les di, [bp + 6]
        rep stosb
.hi:    mov ax, [bp + 12]
        jmp .out
.rec:   add sp, 4
        mov ax, [bp + 12]
        sub ax, cx
.out:   pop es
        pop di
        pop bp
        retf
        GPRANGE .lo, .hi, .rec

; DWORD __cdecl __far gp_strlen(LPCSTR s, WORD max)
; The length of the string at s, at most max (max: no NUL in the first max
; bytes); the high word is 1 if reading faulted, the low word then how
; many bytes were read before the fault. Each byte is read with MOVSB into
; a byte on the stack: DOSBox-X checks a segment's limit for MOVS but not
; for SCAS (US27: a name 4 bytes before its segment's end read on).
global _gp_strlen
_gp_strlen:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        push es
        sub sp, 2                       ; [bp-10]: the byte read
        mov cx, [bp + 10]
        xor ax, ax
        xor dx, dx
        jcxz .out
        cmp [bp + 8], ax                ; a null selector: a fault at once
        jne .go                         ; (NULL_SEL)
        inc dx
        jmp .out
.go:    cld
        push ss
        pop es
.lo:    lds si, [bp + 6]
.next:  lea di, [bp - 10]
        movsb
        cmp byte [es:bp - 10], 0
        je .hi
        loop .next
.hi:    mov ax, [bp + 10]               ; CX: the bytes not read (0: max,
        sub ax, cx                      ; no NUL)
        jmp .out
.rec:   add sp, 4
        mov ax, [bp + 10]
        sub ax, cx
        mov dx, 1
.out:   add sp, 2
        pop es
        pop di
        pop si
        pop ds
        pop bp
        retf
        GPRANGE .lo, .hi, .rec

segment UGPTAB_TEXT
        dw 0, 0, 0, 0                   ; the end of the table
