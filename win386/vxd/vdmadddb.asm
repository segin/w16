; vdmadddb.asm - w16's VDMAD module (the virtual DMA device): DDB, service
; table, entry points and dynamic links (ARCHITECTURE.md 12.8). DDB as
; Windows 3.1's VDMAD's (readexe): "VDMAD", device 0004h, 2.00, init order
; 90000000h, 24 services, no V86 or PM API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VDMAD_DDB
VDMAD_DDB:
        dd 0
        dw 030Ah
        dw 0004h
        db 2, 0
        dw 0
        db 'VDMAD   '
        dd 90000000h
        dd vdmad_ctl
        dd 0                    ; no V86 API (as Windows 3.1's)
        dd 0                    ; no PM API
        dd 0, 0
        dd vdmad_link           ; reference data: the VMM's link (w16vxd.h)
        dd svc_table
        dd 24

        ENTRY vdmad_ctl, _vdmad_control
        ENTRY vdmad_io, _vdmad_iohandler
        ENTRY vdmad_def, _vdmad_default
        ENTRY vdmad_vds, _vdmad_vds_hook
        SERVICES _vdmad_service, 24

        VXDCALL _vmm_install_io, 1, 96h
        VXDCALL _vmm_get_sys_vm, 1, 3
        VXDCALL _vmm_get_cur_vm, 1, 1
        VXDCALL _vmm_crash_cur_vm, 1, 2Fh
        VXDCALL _vmm_hook_v86_int, 1, 41h
        VXDCALLC _vmm_alloc_cb_area, 1, 0A7h, 2

extern _vdmad_link
vdmad_link equ _vdmad_link
