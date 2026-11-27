; vxyddb.asm - VXY.386, the probe VxD that steps the next device's
; Device_Init (w16's own, test only; tests/vxd/vxy.c has what it does):
; its DDB, control procedure and the debug exception's handler. No device
; ID, init order BFFFFFFFh, no services, no API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VXY_DDB, _VXY_DDB, _vxy_hook, _vxy_stepping, _vxy_bp, _vmm_profile_hex, _vxy_thunk, _vxy_next
global _vxy_pf_on, _vxy_pf_armed, _vxy_pf_user
extern _vxy_int1, _vxy_called, _vxy_returned, _vxy_pf
VXY_DDB:
_VXY_DDB:
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
_vxy_pf_user:   dd 0
old_int14:      dd 0
                dw 0

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

; the page fault and the debug exception: from V86 mode or ring 3 (DS
; may be null there) straight on to the VMM's handlers; from ring 0 the C
; side with DS and ES set from SS (the VMM's flat data)
vxy_int14:
        cmp dword [cs:_vxy_pf_user], 0  ; (VXYWatchUser: V86 and ring 3 too)
        jne .any
        test dword [esp+12], 20000h     ; (error code, EIP, CS, EFLAGS)
        jnz .chain
        test byte [esp+8], 3
        jnz .chain
.any:   cmp dword [cs:_vxy_pf_on], 0
        je .chain
        pushad
        push ds
        push es
        mov ax, ss
        mov ds, ax
        mov es, ax
        cld
        lea eax, [esp+8]
        push eax
        call _vxy_pf                    ; (struct pfframe *): 1 ours
        add esp, 4
        pop es
        pop ds
        test eax, eax
        popad
        jz .chain
        add esp, 4                      ; (the error code)
        iretd
.chain: jmp far [cs:old_int14]

vxy_int1:
        cmp dword [cs:_vxy_pf_armed], 0 ; (a watched access, from any mode)
        jne .any
        test dword [esp+8], 20000h      ; (EIP, CS, EFLAGS)
        jnz .chain
        test byte [esp+4], 3
        jnz .chain
.any:
        pushad
        push ds
        push es
        mov ax, ss
        mov ds, ax
        mov es, ax
        cld
        lea eax, [esp+8]
        push eax
        call _vxy_int1                  ; (struct sframe *): 1 handled
        add esp, 4
        pop es
        pop ds
        test eax, eax
        popad
        jz .chain
        iretd
.chain: jmp far [cs:old_int1]

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

; the hooked services (VXYSvc): stub i pushes i and comes to svc_common
; with the caller's return address above it; vxy_svc_called(struct
; svcregs *) logs the call and may point the return at svc_after; then on
; to the previous procedure, the registers and flags as they came
global _vxy_svc_prev, _vxy_svc_stub, _vxy_svc_after_addr, _vmm_hook_service
extern _vxy_svc_called, _vxy_svc_returned
_vxy_svc_prev:  times 256 dd 0
_vxy_svc_stub:
%assign i 0
%rep 256
        dd svc_stub_ %+ i
%assign i i+1
%endrep
_vxy_svc_after_addr: dd svc_after
%assign i 0
%rep 256
svc_stub_ %+ i:
        push dword i
        jmp svc_common
%assign i i+1
%endrep
svc_common:
        pushfd
        pushad
        cld
        push esp
        call _vxy_svc_called
        add esp, 4
        popad
        popfd
        xchg eax, [esp]                 ; (the index; flags kept)
        mov eax, [_vxy_svc_prev + eax * 4]
        xchg eax, [esp]
        ret
svc_after:
        push eax                        ; (the return address)
        pushfd
        pushad
        cld
        lea eax, [esp + 36]             ; where the return address was
        push eax
        lea eax, [esp + 4]
        push eax
        call _vxy_svc_returned
        add esp, 8
        mov [esp+36], eax
        popad
        popfd
        ret

; vu32 vmm_hook_service(vu32 id, vu32 proc): Hook_Device_Service; the
; previous procedure, 0 when it failed
_vmm_hook_service:
        push ebx
        push esi
        push edi
        push ebp
        mov eax, [esp+20]
        mov esi, [esp+24]
        int 20h
        dd 00010090h
        mov eax, esi
        jnc .ok
        xor eax, eax
.ok:    pop ebp
        pop edi
        pop esi
        pop ebx
        ret

; the hooked PM API procedure (VXYApi): vxy_api_in(struct vregs *), the
; previous procedure with the registers as they came, vxy_api_out with
; those it returned
global _vxy_api_prev, _vxy_api_entry, _vmm_hook_pm_api
extern _vxy_api_in, _vxy_api_out
_vxy_api_prev:  dd 0
_vxy_api_entry:
        pushfd
        pushad
        cld
        push esp
        call _vxy_api_in
        add esp, 4
        popad
        popfd
        call [_vxy_api_prev]
        pushfd
        pushad
        cld
        push esp
        call _vxy_api_out
        add esp, 4
        popad
        popfd
        ret

; the hooked protected-mode faults (VXYFaults): stub i pushes i,
; vxy_fault(struct vregs *) logs it, then on to the previous handler
; with the registers and flags as they came
global _vxy_fault_prev, _vxy_fault_stub, _vmm_hook_pm_fault
extern _vxy_fault
_vxy_fault_prev: times 8 dd 0
_vxy_fault_stub:
%assign i 0
%rep 8
        dd fault_stub_ %+ i
%assign i i+1
%endrep
%assign i 0
%rep 8
fault_stub_ %+ i:
        push dword i
        jmp fault_common
%assign i i+1
%endrep
fault_common:
        pushfd
        pushad
        cld
        push esp
        call _vxy_fault
        add esp, 4
        popad
        popfd
        xchg eax, [esp]                 ; (the index; flags kept)
        mov eax, [_vxy_fault_prev + eax * 4]
        xchg eax, [esp]
        ret

; vu32 vmm_hook_pm_fault(vu32 n, vu32 proc): Hook_PM_Fault; the previous
; handler (0 when there was none or it failed)
_vmm_hook_pm_fault:
        push ebx
        push esi
        push edi
        push ebp
        mov eax, [esp+20]
        mov esi, [esp+24]
        int 20h
        dd 00010080h
        mov eax, esi
        jnc .ok
        xor eax, eax
.ok:    pop ebp
        pop edi
        pop esi
        pop ebx
        ret

; vu32 vmm_hook_pm_api(vu32 id, vu32 proc): Hook_Device_PM_API; the
; previous procedure, 0 when it failed
_vmm_hook_pm_api:
        push ebx
        push esi
        push edi
        push ebp
        mov eax, [esp+20]
        mov esi, [esp+24]
        int 20h
        dd 00010092h
        mov eax, esi
        jnc .ok
        xor eax, eax
.ok:    pop ebp
        pop edi
        pop esi
        pop ebx
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
