; dllstart.asm - shared entry code for the test DLLs (TKDLL, TKDLB, TKID,
; TKBAD): records the registers KERNEL passes to a DLL's initialisation
; routine (the NE entry point, "LibMain" convention), the instance data
; DS:0000-000F and the first bytes at ES:SI, then calls the DLL's C
; routine libmain() and returns its result in AX.

        bits 16
        cpu 286

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _TEXT public align=16 class=CODE use16

extern _libmain                 ; int __cdecl __far libmain(void)

segment _NULL
        times 16 db 0

segment _DATA
global _lib_regs, _lib_inst, _lib_cmd, _lib_cmdlen, _lib_calls
; {ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, cs}
_lib_regs       times 12 dw 0
_lib_inst       times 8 dw 0    ; DS:0000-000F at entry
_lib_cmd        times 8 db 0    ; ES:SI[0..7] if readable
_lib_cmdlen     dw 0FFFFh       ; bytes copied from ES:SI (FFFFh: none)
_lib_calls      dw 0            ; times the entry point ran

segment _TEXT
global libentry
libentry:
        push ds
        push ax
        mov ax, DGROUP
        mov ds, ax
        pop ax
        mov [_lib_regs + 0 wrt DGROUP], ax
        mov [_lib_regs + 2 wrt DGROUP], bx
        mov [_lib_regs + 4 wrt DGROUP], cx
        mov [_lib_regs + 6 wrt DGROUP], dx
        mov [_lib_regs + 8 wrt DGROUP], si
        mov [_lib_regs + 10 wrt DGROUP], di
        mov [_lib_regs + 12 wrt DGROUP], bp
        mov ax, sp
        add ax, 2
        mov [_lib_regs + 14 wrt DGROUP], ax
        pop ax                          ; DS at entry
        push ax
        mov [_lib_regs + 16 wrt DGROUP], ax
        mov [_lib_regs + 18 wrt DGROUP], es
        mov [_lib_regs + 20 wrt DGROUP], ss
        mov [_lib_regs + 22 wrt DGROUP], cs
        inc word [_lib_calls wrt DGROUP]
        push bx
        push cx
        push dx
        push si
        push di
        push es
        push bp
        ; instance data
        mov bx, 0
.inst:
        mov ax, [bx]
        mov [_lib_inst + bx wrt DGROUP], ax
        add bx, 2
        cmp bx, 16
        jb .inst
        ; ES:SI, only if ES is a readable selector and 8 bytes fit
        mov ax, es
        verr ax
        jnz .nocmd
        lsl dx, ax
        jnz .nocmd
        cmp si, dx
        ja .nocmd
        mov cx, dx
        sub cx, si
        inc cx                          ; bytes from SI to the limit
        jcxz .full                      ; (wrapped: 64K)
        cmp cx, 8
        jbe .copy
.full:
        mov cx, 8
.copy:
        mov [_lib_cmdlen wrt DGROUP], cx
        xor bx, bx
.cmd:
        mov al, [es:si + bx]
        mov [_lib_cmd + bx wrt DGROUP], al
        inc bx
        loop .cmd
.nocmd:
        call far _libmain
        pop bp
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ds
        retf

; void touch_file(const char far *name): create (truncate) the file and
; close it, to leave a trace where a DLL has no other channel
global _touch_file
_touch_file:
        push bp
        mov bp, sp
        push ds
        lds dx, [bp + 6]
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .done
        mov bx, ax
        mov ah, 3Eh
        int 21h
.done:
        pop ds
        pop bp
        retf
