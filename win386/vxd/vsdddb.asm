; vsdddb.asm - w16's VSD module (the virtual sound device): DDB, service
; table, entry points and dynamic links (ARCHITECTURE.md 12.8). DDB as
; Windows 3.1's VSD's (readexe, docs/win386-services.md): "VSD", device
; 000Bh, 2.00, init order 2C000000h, 2 services, no V86 or PM API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VSD_DDB
VSD_DDB:
        dd 0
        dw 030Ah
        dw 000Bh
        db 2, 0
        dw 0
        db 'VSD     '
        dd 2C000000h
        dd vsd_ctl
        dd 0                    ; no V86 API (as Windows 3.1's)
        dd 0                    ; no PM API
        dd 0, 0, 0
        dd svc_table
        dd 2

        ENTRY vsd_ctl, _vsd_control
        ENTRY vsd_tmo, _vsd_time_out
        SERVICES _vsd_service, 2

        VXDCALL _vmm_set_global_time_out, 1, 3Ch
        VXDCALL _vmm_cancel_time_out, 1, 3Eh
        VXDCALLC _vmm_alloc_cb_area, 1, 0A7h, 2
