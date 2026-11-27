; vxwddb.asm - VXW.386, the page directory's probe VxD (w16's own, test
; only; tests/vxd/vxw.c has what it does): its DDB, control procedure and
; dynamic links. No device ID, the last init order, no services, no API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VXW_DDB
VXW_DDB:
        dd 0
        dw 030Ah
        dw 0                    ; Undefined_Device_ID
        db 1, 0
        dw 0
        db 'VXW     '
        dd 0FFFFFFFFh           ; the last
        dd vxw_ctl
        dd 0, 0, 0, 0, 0
        dd 0, 0

        ENTRY vxw_ctl, _vxw_control
        VXDCALLC _vmm_map_phys, 1, 6Ch, 3
        VXDCALLC _vmm_demand_info, 1, 6Fh, 2
        VXDCALL _pageswap_get_version, 7, 0
        VXDCALL _pageswap_test_create, 7, 1
        VXDCALL _pageswap_test_io_valid, 7, 2
