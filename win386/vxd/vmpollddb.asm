; vmpollddb.asm - w16's VMPOLL module (the idle-detection device): DDB,
; service table, entry points and dynamic links (ARCHITECTURE.md 12.12).
; DDB as Windows 3.1's VMPOLL's (readexe): "VMPOLL", device 0018h, 3.10,
; init order 64000000h, 3 services, no API procedures.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VMPOLL_DDB
VMPOLL_DDB:
        dd 0
        dw 030Ah
        dw 0018h
        db 3, 10
        dw 0
        db 'VMPOLL  '
        dd 64000000h
        dd vmpoll_ctl
        dd 0, 0, 0, 0, 0
        dd svc_table
        dd 3

        ENTRY vmpoll_ctl, _vmpoll_control
        ENTRY vmpoll_i16, _vmpoll_int16
        ENTRY vmpoll_ts, _vmpoll_switched
        SERVICES _vmpoll_service, 3

        VXDCALLC _vmm_alloc_cb_area, 1, 0A7h, 2
        VXDCALL _vmm_hook_v86_int, 1, 41h
        VXDCALL _vmm_release_time_slice, 1, 38h
        VXDCALL _vmm_get_sys_vm, 1, 03h
        VXDCALL _vmm_call_when_task_switched, 1, 2Ah
        VXDCALL _vmm_get_next_vm, 1, 3Bh
        VXDCALL _vtd_get_real_time, 5, 07h
        VXDCALL _vdd_get_modtime, 0Ah, 05h
