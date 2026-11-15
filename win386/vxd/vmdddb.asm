; vmdddb.asm - w16's VMD module (the virtual mouse device): DDB, service
; table, entry points and dynamic links (ARCHITECTURE.md 12.8). DDB as
; Windows 3.1's VMD's (readexe): "VMD", device 000Ch, 3.00, init order
; 34000000h, 3 services, V86 and PM API procedures.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VMD_DDB
VMD_DDB:
        dd 0
        dw 030Ah
        dw 000Ch
        db 3, 0
        dw 0
        db 'VMD     '
        dd 34000000h
        dd vmd_ctl
        dd vmd_api              ; V86 API (UNVERIFIED: no V86 entries in w16's VMM yet)
        dd vmd_api              ; PM API
        dd 0, 0, 0
        dd svc_table
        dd 3

        ENTRY vmd_ctl, _vmd_control
        ENTRY vmd_api, _vmd_api
        ENTRY vmd_hw, _vmd_hw_int
        ENTRY vmd_eoi, _vmd_eoi_proc
        SERVICES _vmd_service, 3

        VXDCALL _vpicd_virtualize_irq, 3, 01h
        VXDCALL _vpicd_set_int_request, 3, 02h
        VXDCALL _vpicd_clear_int_request, 3, 03h
        VXDCALL _vpicd_phys_eoi, 3, 04h
        VXDCALL _vpicd_physically_unmask, 3, 09h
        VXDCALL _vpicd_convert_int_to_irq, 3, 0Eh
