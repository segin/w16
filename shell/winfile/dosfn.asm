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

; the calls that take one path in DS:DX and return CF: 0 ok, else the DOS
; error code
%macro PATHCALL 2
global %1
%1:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov ah, %2
        call far DOS3CALL
        pop ds
        jc .err
        xor ax, ax
.err:   pop bp
        retf
%endmacro

; int dos_mkdir(const char far *path)          AH=39h
PATHCALL _dos_mkdir, 39h
; int dos_rmdir(const char far *path)          AH=3Ah
PATHCALL _dos_rmdir, 3Ah
; int dos_unlink(const char far *path)         AH=41h
PATHCALL _dos_unlink, 41h

; int dos_rename(const char far *from, const char far *to): AH=56h
global _dos_rename
_dos_rename:
        push bp
        mov bp, sp
        push ds
        push di
        lds dx, [bp + 6]
        les di, [bp + 10]
        mov ah, 56h
        call far DOS3CALL
        pop di
        pop ds
        jc .err
        xor ax, ax
.err:   pop bp
        retf

; int dos_getattr(const char far *path): the attributes, or -1 (4300h)
global _dos_getattr
_dos_getattr:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov ax, 4300h
        call far DOS3CALL
        pop ds
        mov ax, cx
        jnc .ok
        mov ax, -1
.ok:    pop bp
        retf

; int dos_setattr(const char far *path, int attr): 0 ok (4301h)
global _dos_setattr
_dos_setattr:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov cx, [bp + 10]
        mov ax, 4301h
        call far DOS3CALL
        pop ds
        jc .err
        xor ax, ax
.err:   pop bp
        retf

; int dos_open(const char far *path, int mode): a handle or -1 (3Dh)
global _dos_open
_dos_open:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov al, [bp + 10]
        mov ah, 3Dh
        call far DOS3CALL
        pop ds
        jnc .ok
        mov ax, -1
.ok:    pop bp
        retf

; int dos_create(const char far *path, int attr): a handle or -1 (3Ch)
global _dos_create
_dos_create:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov cx, [bp + 10]
        mov ah, 3Ch
        call far DOS3CALL
        pop ds
        jnc .ok
        mov ax, -1
.ok:    pop bp
        retf

; int dos_read(int h, void far *buf, unsigned n): bytes, or -1 (3Fh)
; int dos_write(int h, const void far *buf, unsigned n): (40h)
%macro RWCALL 2
global %1
%1:
        push bp
        mov bp, sp
        push ds
        mov bx, [bp + 6]
        lds dx, [bp + 8]
        mov cx, [bp + 12]
        mov ah, %2
        call far DOS3CALL
        pop ds
        jnc .ok
        mov ax, -1
.ok:    pop bp
        retf
%endmacro
RWCALL _dos_read, 3Fh
RWCALL _dos_write, 40h

; void dos_close(int h) (3Eh)
global _dos_close
_dos_close:
        push bp
        mov bp, sp
        mov bx, [bp + 6]
        mov ah, 3Eh
        call far DOS3CALL
        pop bp
        retf

; DWORD dos_getftime(int h): date high, time low (5700h)
global _dos_getftime
_dos_getftime:
        push bp
        mov bp, sp
        mov bx, [bp + 6]
        mov ax, 5700h
        call far DOS3CALL
        mov ax, cx
        pop bp
        retf

; void dos_setftime(int h, DWORD datetime) (5701h)
global _dos_setftime
_dos_setftime:
        push bp
        mov bp, sp
        mov bx, [bp + 6]
        mov cx, [bp + 8]
        mov dx, [bp + 10]
        mov ax, 5701h
        call far DOS3CALL
        pop bp
        retf
