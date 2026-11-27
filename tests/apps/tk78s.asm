; tk78s.asm - TK78's first segment, the start of the loader segment of a
; self-loading program (Windows 3.1 SDK, Programmer's Reference Vol. 1
; ch. 24): the loader data table at offset 0, then BootApp, LoadAppSeg and
; ExitProc (each records its entry registers and calls the C half in
; tk78l.c, which shares this segment), and helpers for tk78l.c: calls
; that record a procedure's result registers, LAR/LSL, and the log file.
; The table's slots that the SDK has KERNEL fill (MyAlloc 10h,
; EntryAddrProc 14h, SetOwner 24h) and every reserved one hold 5A5Ah
; words in the file, so tk78l.c sees each word KERNEL wrote.
        bits 16
        cpu 286
segment TK78L_TEXT public align=16 class=CODE use16

extern _boot_c                  ; tk78l.c
extern _load_c
extern _exit_c

global _ldr_table
_ldr_table:
        db 'A0'                 ; 00 the signature
        dw 5A5Ah                ; 02 reserved
        dw BootApp, seg BootApp         ; 04
        dw LoadAppSeg, seg LoadAppSeg   ; 08
        dw 5A5Ah, 5A5Ah         ; 0C reserved
        dw 5A5Ah, 5A5Ah         ; 10 MyAlloc (KERNEL)
        dw 5A5Ah, 5A5Ah         ; 14 EntryAddrProc (KERNEL)
        dw ExitProc, seg ExitProc       ; 18
        times 6 dw 5A5Ah        ; 1C-27 reserved; 24 SetOwner (KERNEL)
        times 12 dw 5A5Ah       ; 28-3F

; the registers at entry: ax bx cx dx si di ds es ss sp bp flags, then the
; caller's return address (ip cs) and the arguments
%macro SAVEREGS 0
        push bp
        mov bp, sp
        pushf
        push ds
        push es
        push si
        push di
        sub sp, 32
        mov [bp - 42], ax
        mov [bp - 40], bx
        mov [bp - 38], cx
        mov [bp - 36], dx
        mov [bp - 34], si
        mov [bp - 32], di
        mov [bp - 30], ds
        mov [bp - 28], es
        mov [bp - 26], ss
        lea ax, [bp + 2]
        mov [bp - 24], ax       ; SP at entry (the return address)
        mov ax, [bp]
        mov [bp - 22], ax       ; BP
        mov ax, [bp - 2]
        mov [bp - 20], ax       ; flags
        mov ax, [bp + 2]
        mov [bp - 18], ax       ; return IP
        mov ax, [bp + 4]
        mov [bp - 16], ax       ; return CS
        cld
%endmacro
%macro RESTREGS 0
        add sp, 32
        pop di
        pop si
        pop es
        pop ds
        popf
        pop bp
%endmacro

; void FAR PASCAL BootApp(HANDLE hBlock, HANDLE hFile)
BootApp:
        SAVEREGS
        push word [bp + 6]      ; hFile
        push word [bp + 8]      ; hBlock
        push ss
        lea ax, [bp - 42]
        push ax
        call far _boot_c        ; boot_c(regs, hBlock, hFile)
        add sp, 8
        RESTREGS
        retf 4

; WORD FAR PASCAL LoadAppSeg(HANDLE hBlock, HANDLE hFile, WORD wSeg)
LoadAppSeg:
        SAVEREGS
        push word [bp + 6]      ; wSeg
        push word [bp + 8]      ; hFile
        push word [bp + 10]     ; hBlock
        push ss
        lea ax, [bp - 42]
        push ax
        call far _load_c        ; load_c(regs, hBlock, hFile, wSeg)
        add sp, 10
        RESTREGS
        retf 6

; void FAR PASCAL ExitProc(HANDLE hBlock)
ExitProc:
        SAVEREGS
        push word [bp + 6]
        push ss
        lea ax, [bp - 42]
        push ax
        call far _exit_c
        add sp, 6
        RESTREGS
        retf 2

; DWORD __cdecl kcall(DWORD fn, WORD far *out, WORD n, WORD a1, WORD a2,
;                     WORD a3)
; Far-call the Pascal procedure fn with its first n (0-3) word arguments
; a1..a3 (a1 pushed first), BX, CX, SI, DI and ES 6B6Bh before; out[]
; gets AX BX CX DX SI DI ES DS and the flags after. Returns DX:AX.
global _kcall
_kcall:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov cx, [bp + 14]
        lea si, [bp + 16]
.arg:   jcxz .go
        push word [ss:si]
        add si, 2
        dec cx
        jmp .arg
.go:    mov ax, 6B6Bh
        mov bx, ax
        mov cx, ax
        mov si, ax
        mov di, ax
        mov es, [bp - 6]        ; (a valid selector: the caller's DS)
        call far [bp + 6]
        pushf
        push es
        push ds
        push di
        push si
        push bx
        les bx, [bp + 10]
        mov [es:bx + 0], ax
        pop word [es:bx + 2]
        mov [es:bx + 6], dx
        pop word [es:bx + 8]
        pop word [es:bx + 10]
        pop word [es:bx + 14]
        pop word [es:bx + 12]
        pop word [es:bx + 16]
        mov [es:bx + 4], cx
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; WORD __cdecl lar(WORD sel): the access rights byte (LAR's bits 8-15)
; and in the high byte the attributes (bits 16-23 >> 8) if the selector
; is valid; FFFFh if not
global _lar
_lar:
        push bp
        mov bp, sp
        cpu 386
        mov ax, [bp + 6]
        lar eax, eax
        jnz .bad
        shr eax, 8
        jmp .out
.bad:   mov ax, 0FFFFh
.out:   cpu 286
        pop bp
        retf

; WORD __cdecl lsl(WORD sel): the limit, FFFFh if not valid (or 64K)
global _lsl
_lsl:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        lsl ax, ax
        jz .ok
        mov ax, 0FFFFh
.ok:    pop bp
        retf

; void __cdecl log_line(const char far *s, WORD n): append n bytes to
; C:\TK78.LOG (opened and closed each time: BootApp runs before the
; program's data segment exists)
global _log_line
_log_line:
        push bp
        mov bp, sp
        push ds
        push si
        push cs
        pop ds
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jnc .seek
        mov ah, 3Ch
        xor cx, cx
        int 21h
        jc .done
.seek:  mov si, ax
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov bx, si
        mov cx, [bp + 10]
        lds dx, [bp + 6]
        mov ah, 40h
        int 21h
        mov bx, si
        mov ah, 3Eh
        int 21h
.done:  pop si
        pop ds
        pop bp
        retf

; DWORD __cdecl dos_pos(WORD h): INT 21h 4201h (the handle's position);
; FFFFFFFFh with the carry
global _dos_pos
_dos_pos:
        push bp
        mov bp, sp
        mov bx, [bp + 6]
        mov ax, 4201h
        xor cx, cx
        xor dx, dx
        int 21h
        jnc .ok
        mov ax, 0FFFFh
        mov dx, ax
.ok:    pop bp
        retf

; WORD __cdecl get_cs(void)
global _get_cs
_get_cs:
        mov ax, cs
        retf

logname db "C:\TK78.LOG", 0
