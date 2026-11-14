; rebootdd.asm - w16's REBOOT module: DDB, entry points and dynamic links
; (ARCHITECTURE.md 12.8). DDB as Windows 3.1's (readexe): "Reboot", device
; 0009h, 2.00, init order 24000000h, no services, a PM API procedure.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global REBOOT_DDB
REBOOT_DDB:
        dd 0
        dw 030Ah
        dw 0009h
        db 2, 0
        dw 0
        db 'Reboot  '
        dd 24000000h
        dd rb_ctl
        dd 0
        dd rb_api
        dd 0, 0, 0
        dd 0                    ; no services
        dd 0

        ENTRY rb_ctl, _reboot_control
        ENTRY rb_api, _reboot_api
        ENTRY rb_cad, _reboot_cad

        VXDCALL _vkd_define_hot_key, 0Dh, 01h
        VXDCALL _vkd_force_keys, 0Dh, 07h
        VXDCALL _vmm_begin_nest_v86_exec, 1, 82h
        VXDCALL _vmm_exec_int, 1, 84h
        VXDCALL _vmm_end_nest_exec, 1, 86h
        VXDCALL _vmm_save_client_state, 1, 8Dh
        VXDCALL _vmm_restore_client_state, 1, 8Eh
        VXDCALL _vmm_get_sys_vm, 1, 03h
