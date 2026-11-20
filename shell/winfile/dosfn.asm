; dosfn.asm - w16 File Manager: the DOS calls shell/start.asm does not have.
; Each goes through DOS3Call (KERNEL.102), as Windows programs' INT 21h
; calls should.

        bits 16
        cpu 286

segment _TEXT public align=16 class=CODE use16

extern DOS3CALL                 ; KERNEL.102

; int dos_freespace(int drive, DWORD far *freeb, DWORD far *totalb):
; drive 1 = A:; the free and total bytes (AH=36h); 0 ok, -1 no such drive
global _dos_freespace
_dos_freespace:
        push bp
        mov bp, sp
        push si
        push di
        mov dl, [bp + 6]
        mov ah, 36h
        call far DOS3CALL
        cmp ax, 0FFFFh
        je .bad
        mov si, dx              ; total clusters
        mul cx                  ; bytes a cluster: AX = sectors * bytes
        mov cx, ax
        mov ax, bx              ; free clusters
        mul cx
        les di, [bp + 8]
        mov [es:di], ax
        mov [es:di + 2], dx
        mov ax, si
        mul cx
        les di, [bp + 12]
        mov [es:di], ax
        mov [es:di + 2], dx
        xor ax, ax
        jmp .out
.bad:   mov ax, -1
.out:   pop di
        pop si
        pop bp
        retf
