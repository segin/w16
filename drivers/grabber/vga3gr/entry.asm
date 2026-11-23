; entry.asm - w16's VGA.3GR: the library entry, WEP, and the call into the
; VDD's protected-mode API for the C code (vga3gr.c; grab.h).
;
; The VDD's API entry is found as for any VxD (INT 2Fh AX=1684h with the
; device ID in BX, here 000Ah for VDD; the DDK's "Virtual-Display Device
; Services" for the grabber). It is called with the function in EAX and
; its arguments in EBX (the VM handle), ECX and ES:EDI (a buffer and its
; size); it returns in the registers and the buffer (observed with the
; recorder GR3: docs/notes/grabber-handover.md "VGA.3GR").

        bits 16
        cpu 386

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment _NULL
        times 16 db 0                   ; the instance data KERNEL keeps here

segment _DATA
global _hInst
_hInst   dw 0
vddentry dd 0

extern _grab_init                       ; int __cdecl far grab_init(void)
extern _grab_exit                       ; void __cdecl far grab_exit(void)

segment ENTRY_TEXT public align=16 class=CODE use16

global grab_libentry
grab_libentry:
        push ds
        push si
        push di
        push es
        mov [_hInst wrt DGROUP], di
        call far _grab_init
        pop es
        pop di
        pop si
        pop ds
        retf

; WEP(nExitType): free the selectors grab_init allocated
global WEP
WEP:
        push ds
        push si
        push di
        push es
        mov ax, seg _hInst
        mov ds, ax
        call far _grab_exit
        pop es
        pop di
        pop si
        pop ds
        mov ax, 1
        retf 2

; int __cdecl far vdd_find(void): the VDD's API entry (nonzero: found)
global _vdd_find
_vdd_find:
        push ds
        push es
        push di
        push bx
        mov ax, seg vddentry
        mov ds, ax
        xor di, di
        mov es, di
        mov ax, 1684h
        mov bx, 000Ah
        int 2Fh
        mov [vddentry wrt DGROUP], di
        mov [vddentry + 2 wrt DGROUP], es
        mov ax, es
        or ax, di
        pop bx
        pop di
        pop es
        pop ds
        retf

; void __cdecl far vdd_call(VREGS far *r): the registers in r (EAX, EBX,
; ECX, EDX, ESI, EDI, ES) to the VDD, its registers and flags back to r
global _vdd_call
_vdd_call:
        push bp
        mov bp, sp
        push ds
        push es
        push esi
        push edi
        push ebx
        push ecx
        push edx
        mov ax, seg vddentry
        mov ds, ax
        cmp word [vddentry + 2 wrt DGROUP], 0
        je .none
        push dword [vddentry wrt DGROUP] ; the entry, for the call below
        mov bx, sp
        lds si, [bp + 6]
        mov es, [si + 24]
        mov eax, [si]
        mov ebx, [si + 4]
        mov ecx, [si + 8]
        mov edx, [si + 12]
        mov edi, [si + 20]
        mov esi, [si + 16]
        push bp
        mov bp, sp
        call far [ss:bp + 2]
        pop bp
        add sp, 4
        pushf
        push ds
        push esi
        lds si, [bp + 6]
        mov [si], eax
        mov [si + 4], ebx
        mov [si + 8], ecx
        mov [si + 12], edx
        mov [si + 20], edi
        pop dword [si + 16]
        pop ax                          ; DS after the call: dropped
        pop word [si + 26]              ; flags
.none:  pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        pop es
        pop ds
        pop bp
        retf
