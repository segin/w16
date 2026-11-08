; start.asm - start-up code for the w16 shell programs (no C runtime).
;
; The usual Windows task start-up: InitTask, WaitEvent(0), InitApp, then
; WinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow), and exit with
; WinMain's return value. 8086 code so the same object serves the
; Windows 2.x build.

        bits 16
        cpu 8086

; DGROUP starts with 16 bytes reserved for the Windows instance data.
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern INITTASK                 ; KERNEL.91
extern WAITEVENT                ; KERNEL.30
extern INITAPP                  ; USER.5
extern WINMAIN                  ; int PASCAL WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
extern DOS3CALL                 ; KERNEL.102

segment _NULL
        times 16 db 0

segment _BSS
hinst   resw 1
hprev   resw 1
cmdoff  resw 1
cmdseg  resw 1
cmdshow resw 1

segment _TEXT
; The compiler references _wstart_ (its C run-time start-up) from any
; module that defines WinMain; this start-up stands in for it.
global start, _wstart_
start:
_wstart_:
        call far INITTASK
        or ax, ax
        jz .fail
        mov [hinst wrt DGROUP], di
        mov [hprev wrt DGROUP], si
        mov [cmdoff wrt DGROUP], bx
        mov [cmdseg wrt DGROUP], es
        mov [cmdshow wrt DGROUP], dx
        xor ax, ax
        push ax
        call far WAITEVENT
        push word [hinst wrt DGROUP]
        call far INITAPP
        or ax, ax
        jz .fail
        push word [hinst wrt DGROUP]
        push word [hprev wrt DGROUP]
        push word [cmdseg wrt DGROUP]
        push word [cmdoff wrt DGROUP]
        push word [cmdshow wrt DGROUP]
        call far WINMAIN
        mov ah, 4Ch
        int 21h
.fail:
        mov ax, 4CFFh
        int 21h

; int dos_chdir(const char far *path): 0 on success (AH=3Bh)
global _dos_chdir
_dos_chdir:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        mov ah, 3Bh
        call far DOS3CALL
        pop ds
        mov ax, 0
        jnc .ok
        dec ax
.ok:
        pop bp
        retf

; void dos_setdrive(int drive): 0 = A: (AH=0Eh)
global _dos_setdrive
_dos_setdrive:
        push bp
        mov bp, sp
        mov dl, [bp + 6]
        mov ah, 0Eh
        call far DOS3CALL
        pop bp
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
