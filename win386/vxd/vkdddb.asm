; vkdddb.asm - w16's VKD module (the virtual keyboard device): DDB,
; service table, entry points and dynamic links (ARCHITECTURE.md 12.8).
; DDB as Windows 3.1's VKD's (readexe): "VKD", device 000Dh, 2.00, init
; order 38000000h, 15 services, a PM API procedure.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VKD_DDB
VKD_DDB:
        dd 0
        dw 030Ah
        dw 000Dh
        db 2, 0
        dw 0
        db 'VKD     '
        dd 38000000h
        dd vkd_ctl
        dd 0                    ; no V86 API (as Windows 3.1's)
        dd vkd_api
        dd 0, 0, 0
        dd svc_table
        dd 15

        ENTRY vkd_ctl, _vkd_control
        ENTRY vkd_api, _vkd_api
        ENTRY vkd_hw, _vkd_hw_int
        SERVICES _vkd_service, 15

        VXDCALL _vpicd_virtualize_irq, 3, 01h
        VXDCALL _vpicd_set_int_request, 3, 02h
        VXDCALL _vpicd_phys_eoi, 3, 04h
