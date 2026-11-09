; woasm.asm - w16 WINOLDAP.MOD: the register-level calls (ARCHITECTURE.md 9.7).
;
; WINOLDAP talks to the Standard-mode task switcher (WIN.COM's INT 2Fh
; handler, WSWAP.EXE, DSWAP.EXE, DOSX.EXE) through INT 2Fh and DPMI; these
; are the calls, each observed on Windows 3.1 with the WO2 probe
; (tests/apps/wo2.c) watching Microsoft's WINOLDAP.MOD.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

; DWORD woa_block(void): INT 2Fh AX=4A05h SI=0005h returns DX:AX = WIN.COM's
; switcher block (a real-mode address); 0 when nobody answers (no switcher).
global _woa_block
_woa_block:
        push si
        push di
        mov ax, 4A05h
        mov si, 5
        xor dx, dx
        int 2Fh
        cmp ax, 4A05h
        jne .ok
        xor ax, ax
        xor dx, dx
.ok:    pop di
        pop si
        retf

; WORD woa_segsel(WORD seg): a selector for a real-mode segment (DPMI 0002h)
global _woa_segsel
_woa_segsel:
        push bp
        mov bp, sp
        push bx
        mov bx, [bp + 6]
        mov ax, 0002h
        int 31h
        jnc .ok
        xor ax, ax
.ok:    pop bx
        pop bp
        retf

; DWORD woa_selbase(WORD sel): the linear base of a selector (DPMI 0006h)
global _woa_selbase
_woa_selbase:
        push bp
        mov bp, sp
        push bx
        push cx
        mov bx, [bp + 6]
        mov ax, 0006h
        int 31h
        mov ax, dx
        mov dx, cx
        jnc .ok
        xor ax, ax
        xor dx, dx
.ok:    pop cx
        pop bx
        pop bp
        retf

; void woa_switch(void): ask for the switch (INT 2Fh AX=4B80h, DOSX takes
; it to WSWAP), then make a DOS call: the switch to the DOS session happens
; inside it, and it returns when Windows is swapped back in. Windows 3.1's
; WINOLDAP makes AH=0Bh (check input status) with AL still 80h.
global _woa_switch
_woa_switch:
        push si
        push di
        mov ax, 4B80h
        int 2Fh
        mov ah, 0Bh
        int 21h
        pop di
        pop si
        retf

; WORD woa_memtop(void): conventional memory in paragraphs (BIOS INT 12h)
global _woa_memtop
_woa_memtop:
        int 12h
        mov cl, 6
        shl ax, cl
        retf

; DWORD woa_diskfree(int drive): free bytes on a drive (1 = A:), INT 21h
; AH=36h; 0 if the drive is invalid
extern DOS3CALL
global _woa_diskfree
_woa_diskfree:
        push bp
        mov bp, sp
        push bx
        push cx
        mov dl, [bp + 6]
        mov ah, 36h
        call far DOS3CALL
        cmp ax, 0FFFFh
        je .bad
        mul cx                          ; sectors a cluster * bytes a sector
        mul bx                          ; * free clusters (fits for 3.1's sizes)
        jmp .r
.bad:   xor ax, ax
        xor dx, dx
.r:     pop cx
        pop bx
        pop bp
        retf
