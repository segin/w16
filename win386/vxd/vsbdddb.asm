; vsbdddb.asm - w16's VSBD.386 (the Sound Blaster's virtual device,
; win386/vxd/vsbd.c): DDB, service table, entry points and dynamic links.
; DDB as Windows 3.1's VSBD.386's (readexe): "VSBD", device 0445h, 3.00,
; Undefined_Init_Order, one procedure for the V86 and PM APIs, 2 services.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VSBD_DDB
VSBD_DDB:
        dd 0
        dw 0300h
        dw 0445h
        db 3, 0
        dw 0
        db 'VSBD    '
        dd 80000000h
        dd vsbd_ctl
        dd vsbd_api             ; V86 API
        dd vsbd_api             ; PM API
        dd 0, 0, 0
        dd svc_table
        dd 2

        ENTRY vsbd_ctl, _vsbd_control
        ENTRY vsbd_api, _vsbd_api_proc
        ENTRY vsbd_io, _vsbd_iohandler
        ENTRY vsbd_hw, _vsbd_hw_int
        ENTRY vsbd_eoi, _vsbd_eoi_proc
        ENTRY vsbd_mask, _vsbd_mask_change
        SERVICES _vsbd_service, 2

        VXDCALL _vmm_install_io, 1, 96h
        VXDCALL _vmm_enable_local, 1, 98h
        VXDCALL _vmm_disable_local, 1, 9Ah
        VXDCALL _vmm_get_sys_vm, 1, 3
        VXDCALL _vmm_profile_hex, 1, 0AFh
        VXDCALL _vmm_profile_dec, 1, 0ABh
        VXDCALL _vpicd_virtualize, 3, 1
        VXDCALL _vpicd_set_req, 3, 2
        VXDCALL _vpicd_clear_req, 3, 3
        VXDCALL _vpicd_phys_eoi, 3, 4
        VXDCALL _vpicd_phys_mask, 3, 8
        VXDCALL _vpicd_phys_unmask, 3, 9
        VXDCALL _vpicd_status, 3, 5
        VXDCALL _shell_message, 17h, 4
