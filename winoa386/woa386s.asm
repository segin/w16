; woa386s.asm - w16's WINOA386.MOD: the register-level calls (MIT licence,
; LICENSE): the VxDs' protected-mode API entries (INT 2Fh AX=1684h, BX the
; device: SHELL 0017h, VKD 000Dh, VMD 000Ch; the DDK's Virtual Device
; Adaptation Guide), a call into one with 32-bit registers, and SHELL's
; event procedure (winoa386.c, "SHELL's protocol").

        bits 16
        cpu 386

segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS

segment WOAFIX_TEXT public align=16 class=CODE use16

; DWORD __cdecl far vxd_find(WORD id): the API entry (selector:offset; 0:
; none)
global _vxd_find
_vxd_find:
        push bp
        mov bp, sp
        push es
        push di
        push bx
        xor di, di
        mov es, di
        mov ax, 1684h
        mov bx, [bp + 6]
        int 2Fh
        mov ax, di
        mov dx, es
        pop bx
        pop di
        pop es
        pop bp
        retf

; void __cdecl far vxd_call(DWORD entry, VREGS far *r): EAX EBX ECX EDX ESI
; EDI ES from r to the entry, the registers and flags back to r
; (VREGS: six dwords, then ES and the flags: winoa386.h)
global _vxd_call
_vxd_call:
        push bp
        mov bp, sp
        push ds
        push es
        push esi
        push edi
        push ebx
        push ecx
        push edx
        push dword [bp + 6]             ; the entry, for the call below
        lds si, [bp + 10]
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
        lds si, [bp + 10]
        mov [si], eax
        mov [si + 4], ebx
        mov [si + 8], ecx
        mov [si + 12], edx
        mov [si + 20], edi
        pop dword [si + 16]
        pop ax                          ; DS after the call: dropped
        pop word [si + 26]              ; flags
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        pop es
        pop ds
        pop bp
        retf

; SHELL's event procedure (SHELL function 9): SHELL calls it far in the
; System VM, on its own locked stack, at any moment, with (hwnd, message,
; wParam, lParam) pushed in that order and expects them popped (PASCAL).
; It posts them to the window, which acts on them in its own time
; (PostMessage may be called at such times: the SDK).
extern POSTMESSAGE
global woa_event
woa_event:
        push bp
        mov bp, sp
        push ds
        push es
        pushad                          ; (DS stays the caller's: PostMessage
                                        ; needs none of ours, and each
                                        ; instance has its own)
        push word [bp + 14]             ; hwnd
        push word [bp + 12]             ; message
        push word [bp + 10]             ; wParam
        push word [bp + 8]              ; lParam, high
        push word [bp + 6]              ; low
        call far POSTMESSAGE
        popad
        pop es
        pop ds
        pop bp
        retf 10

; DWORD __cdecl far woa_eventproc(void): its address
global _woa_eventproc
_woa_eventproc:
        mov ax, woa_event
        mov dx, cs
        retf
