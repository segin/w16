; memlasm.asm - MEMLOAD.EXE's calls to the XMS driver (tests/dos/memload.c):
; the driver is reached by a far call to the entry INT 2Fh 4310h gives
; (XMS 2.0/3.0 specifications). Large model, Watcom __cdecl.
;   void __cdecl xms_call(struct xr __far *r)    ax bx dx si ds, in and out
;   void __cdecl xms_call32(struct xr32 __far *r) eax ebx ecx edx, in and out
        bits 16
        cpu 386

segment memlasm_TEXT public align=16 class=CODE
global _xms_call, _xms_call32, _xms_entry

_xms_entry dd 0

_xms_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        les di, [bp + 6]
        mov ax, [es:di]
        mov bx, [es:di + 2]
        mov dx, [es:di + 4]
        mov si, [es:di + 6]
        mov ds, [es:di + 8]
        push es
        push di
        call far [cs:_xms_entry]
        pop di
        pop es
        mov [es:di], ax
        mov [es:di + 2], bx
        mov [es:di + 4], dx
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

_xms_call32:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        les di, [bp + 6]
        mov eax, [es:di]
        mov ebx, [es:di + 4]
        mov ecx, [es:di + 8]
        mov edx, [es:di + 12]
        push es
        push di
        call far [cs:_xms_entry]
        pop di
        pop es
        mov [es:di], eax
        mov [es:di + 4], ebx
        mov [es:di + 8], ecx
        mov [es:di + 12], edx
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
