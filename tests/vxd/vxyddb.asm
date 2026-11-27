; vxyddb.asm - VXY.386, the probe VxD that steps the next device's
; Device_Init (w16's own, test only; tests/vxd/vxy.c has what it does):
; its DDB, control procedure and the debug exception's handler. No device
; ID, init order BFFFFFFFh, no services, no API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VXY_DDB, _vxy_hook, _vxy_stepping, _vxy_bp, _vmm_profile_hex, _vxy_thunk, _vxy_next
global _vxy_pf_on, _vxy_pf_armed
extern _vxy_step, _vxy_break, _vxy_called, _vxy_returned, _vxy_pf, _vxy_pf_step
VXY_DDB:
        dd 0
        dw 030Ah
        dw 0                    ; Undefined_Device_ID
        db 1, 0
        dw 0
        db 'VXY     '
        dd 0BFFFFFFFh           ; just before W32S.386's C0000000h
        dd vxy_ctl
        dd 0, 0, 0, 0, 0
        dd 0, 0

        ENTRY vxy_ctl, _vxy_control
        VXDCALLC _vmm_map_phys, 1, 6Ch, 3

_vxy_stepping:  dd 0
_vxy_bp:        dd 0
old_int1:       dd 0            ; the old gate's offset and selector (JMP FAR)
                dw 0
idtr:           dw 0
                dd 0

_vxy_pf_on:     dd 0
_vxy_pf_armed:  dd 0
old_int14:      dd 0
                dw 0
pf_err:         dd 0

; vxy_hook(): the debug exception's and the page fault's IDT gates to
; vxy_int1 and vxy_int14, the old ones kept
_vxy_hook:
        push ebx
        sidt [idtr]
        mov ebx, [idtr+2]
        cmp dword [old_int1], 0
        jne .done
        push ebx
        add ebx, 8                      ; gate 1
        mov ecx, vxy_int1
        mov edx, old_int1
        call .gate
        pop ebx
        add ebx, 14 * 8                 ; gate 14
        mov ecx, vxy_int14
        mov edx, old_int14
        call .gate
.done:  pop ebx
        ret
.gate:  movzx eax, word [ebx+6]
        shl eax, 16
        mov ax, [ebx]
        mov [edx], eax
        mov ax, [ebx+2]
        mov [edx+4], ax
        mov eax, ecx
        mov [ebx], ax
        shr eax, 16
        mov [ebx+6], ax
        mov ax, cs
        mov [ebx+2], ax
        ret

vxy_int14:
        cmp dword [_vxy_pf_on], 0
        je .chain
        pop dword [pf_err]
        pushad
        cld
        push dword [pf_err]
        lea eax, [esp+4]
        push eax
        call _vxy_pf
        add esp, 8
        test eax, eax
        popad
        jz .no
        iretd
.no:    push dword [pf_err]
.chain: jmp far [old_int14]

vxy_int1:
        cmp dword [_vxy_pf_armed], 0
        je .st
        pushad
        cld
        push esp
        call _vxy_pf_step
        add esp, 4
        popad
        iretd
.st:    cmp dword [_vxy_stepping], 0
        je .bp
        pushad
        cld
        push esp
        call _vxy_step
        add esp, 4
        popad
        iretd
.bp:    cmp dword [_vxy_bp], 0
        je .chain
        push eax
        mov eax, dr6
        test al, 1                      ; DR0's
        pop eax
        jz .chain
        pushad
        cld
        push esp
        call _vxy_break
        add esp, 4
        popad
        iretd
.chain: jmp far [old_int1]

; a procedure pointer's new target (VXYHook): vxy_called(struct vregs *)
; with the registers and flags as they came, then on to vxy_next
_vxy_thunk:
        pushfd
        pushad
        cld
        push esp
        call _vxy_called
        add esp, 4
        test eax, eax                   ; (the flags: POPFD below)
        jz .go
        mov dword [esp+36], vxy_after   ; its return comes to vxy_after
.go:    popad
        popfd
        jmp [_vxy_next]
_vxy_next:      dd 0

; the procedure's return: vxy_returned(struct vregs *) with its registers
; gives the address it was to return to
vxy_after:
        push eax                        ; (the return address)
        pushfd
        pushad
        cld
        push esp
        call _vxy_returned
        add esp, 4
        mov [esp+36], eax
        popad
        popfd
        ret

; vu32 vmm_profile_hex(vu32 deflt, const char *key): Get_Profile_Hex_Int
; in [386Enh]
_vmm_profile_hex:
        push ebx
        push esi
        push edi
        push ebp
        mov eax, [esp+20]
        mov edi, [esp+24]
        xor esi, esi
        int 20h
        dd 000100AFh
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
