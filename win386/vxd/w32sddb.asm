; w32sddb.asm - w16's W32S module, Win32s's device (ARCHITECTURE.md
; 12.20): DDB, service table, entry points and dynamic links. DDB as
; Win32s 1.30c's W32S.386's (readexe: "W32S", device 002Dh, 2.00, SDK
; 3.10, init order C0000000h, 2 services, a PM API procedure, no V86
; one), so that WIN32S16.DLL finds it by its ID.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global W32S_DDB
extern _w32s_link, _w32s_fault
W32S_DDB:
        dd 0
        dw 030Ah
        dw 002Dh
        db 2, 0
        dw 0
        db 'W32S    '
        dd 0C0000000h
        dd w32s_ctl
        dd 0                    ; no V86 API
        dd w32s_api             ; PM API
        dd 0, 0
        dd _w32s_link           ; reference data: the VMM's link (w16vxd.h)
        dd svc_table
        dd 2

        ENTRY w32s_ctl, _w32s_control
        ENTRY w32s_api, _w32s_api

        SERVICES _w32s_service, 2

        VXDCALLC _vmm_heap_alloc, 1, 4Fh, 2
        VXDCALLC _vmm_heap_free, 1, 51h, 2
        VXDCALL _vmm_begin_nest_exec, 1, 83h
        VXDCALL _vmm_exec_int, 1, 84h
        VXDCALL _vmm_end_nest_exec, 1, 86h
        VXDCALL _vmm_save_client_state, 1, 8Dh
        VXDCALL _vmm_restore_client_state, 1, 8Eh
        VXDCALL _vmm_hook_pm_fault, 1, 80h
        VXDCALL _vmm_get_profile_hex, 1, 0AFh

; the protected-mode fault hooks' previous handlers (Hook_PM_Fault): a
; fault that is not Win32s's goes on to the one before (w32s.c)
global _w32s_pf_prev, _w32s_pf_entry
_w32s_pf_prev:  dd 0, 0
; w32s_pf_entry_N: w32s_fault(struct vregs *) with arg = N; carry set
; on return: not ours, on to the previous handler
_w32s_pf_entry:
        dd pf_0, pf_1
pf_0:   push dword 0
        jmp pf_common
pf_1:   push dword 1
pf_common:
        push dword _w32s_fault
        jmp .call
.call:
        ; [cfunc][n][ret]: as vxd_svc_common, the number in arg
        xchg eax, [esp]
        xchg eax, [esp+4]
        xchg eax, [esp]
        pushfd
        pushad
        cld
        push esp
        call [esp+4+32+4+4]
        add esp, 4
        popad
        popfd
        jc .prev
        lea esp, [esp+8]
        ret
.prev:  push eax                        ; (the index is at [esp+4])
        mov eax, [esp+4]
        mov eax, [_w32s_pf_prev + eax * 4]
        mov [esp+8], eax                ; the cfunc slot becomes the target
        pop eax
        lea esp, [esp+4]                ; the index gone: [target][ret]
        ret
