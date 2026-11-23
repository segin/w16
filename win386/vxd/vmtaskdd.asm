; vmtaskdd.asm - VMTASK.386, w16's task-manager device (win386/vxd/vmtask.c):
; DDB, entry points and dynamic links (ARCHITECTURE.md 15.12). An external
; VxD of w16's own, with no counterpart in Windows 3.1: "VMTASK", device
; 5716h (w16's choice, not one Microsoft assigned: see vmtask.c), 1.00,
; Undefined_Init_Order, one procedure for the V86 and the PM API, no
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
        dd 80000000h            ; Undefined_Init_Order
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
