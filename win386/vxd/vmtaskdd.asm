; vmtaskdd.asm - VMTASK.386, w16's task-manager device (win386/vxd/vmtask.c):
; DDB, entry points and dynamic links (ARCHITECTURE.md 15.12). An external
; VxD of w16's own, with no counterpart in Windows 3.1: "VMTASK", device
; 5716h (w16's choice, not one Microsoft assigned: see vmtask.c), 1.00,
; init order 0, one procedure for the V86 and the PM API, no
; services. Every dynamic link is to a service the Windows 3.1 DDK
; documents, so it loads under Microsoft's WIN386 as under w16's.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VMTASK_DDB
VMTASK_DDB:
        dd 0
        dw 030Ah
        dw 5716h
        db 1, 0
        dw 0
        db 'VMTASK  '
        dd 0                    ; init order 0: the first after the VMM, so that
                                ; DDB_Next leads through every other device (function 4)
        dd vmtask_ctl
        dd vmtask_api           ; V86 API
        dd vmtask_api           ; PM API
        dd 0, 0, 0
        dd 0                    ; no services
        dd 0

        ENTRY vmtask_ctl, _vmtask_control
        ENTRY vmtask_api, _vmtask_api_proc
        ENTRY vmtask_ts, _vmtask_on_switch
        ENTRY vmtask_idle, _vmtask_on_idle

        VXDCALL _vmm_get_cur_vm, 1, 01h
        VXDCALL _vmm_get_sys_vm, 1, 03h
        VXDCALL _vmm_map_flat, 1, 1Ch
        VXDCALL _vmm_call_when_task_switched, 1, 2Ah
        VXDCALL _vmm_get_focus, 1, 30h
        VXDCALL _vmm_get_ts_priority, 1, 32h
        VXDCALL _vmm_get_ts_granularity, 1, 34h
        VXDCALL _vmm_call_when_idle, 1, 3Ah
        VXDCALL _vmm_get_next_vm, 1, 3Bh
        VXDCALL _vmm_get_system_time, 1, 3Fh
        VXDCALL _vmm_get_vm_exec_time, 1, 40h
        VXDCALLC _vmm_alloc_cb_area, 1, 0A7h, 2
        VXDCALL _shell_get_vm_info, 17h, 05h
        VXDCALL _vtd_get_real_time, 5, 07h
        VXDCALL _vmm_validate_vm, 1, 05h
        VXDCALL _vmm_nuke_vm, 1, 2Eh
        VXDCALL _vmm_close_vm, 1, 0ECh
        VXDCALL _vmm_get_version, 1, 00h
        VXDCALL _v86mm_get_limits, 6, 03h

; vu32 vmm_vm_pgcount(vu32 vm, vu32 *outside): _GetVMPgCount(VM, 0), the
; DDK's C-convention service (VMM 5Ch): EAX back, EDX into *outside
global _vmm_vm_pgcount
_vmm_vm_pgcount:
        push ebx
        push esi
        push edi
        push ebp
        push dword 0
        push dword [esp + 24]
        int 20h
        dd (1 << 16) | 5Ch
        add esp, 8
        mov ecx, [esp + 24]
        mov [ecx], edx
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
