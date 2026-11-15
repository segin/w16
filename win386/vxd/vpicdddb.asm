; vpicdddb.asm - w16's VPICD module: its DDB, service table and entry
; points (ARCHITECTURE.md 12.8). DDB fields as Windows 3.1's VPICD's
; (docs/win386-vxds.md, read with readexe): "VPICD", device 0003h, 3.10,
; init order 0C000000h, 21 services, V86 and PM API procedures.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VPICD_DDB
extern _vpicd_link
VPICD_DDB:
        dd 0                    ; DDB_Next
        dw 030Ah                ; DDB_SDK_Version
        dw 0003h                ; DDB_Req_Device_Number
        db 3, 10                ; version 3.10
        dw 0                    ; DDB_Flags
        db 'VPICD   '
        dd 0C000000h            ; DDB_Init_Order
        dd vpicd_ctl            ; DDB_Control_Proc
        dd vpicd_api            ; DDB_V86_API_Proc (UNVERIFIED: w16's VMM has no V86 entries yet)
        dd vpicd_api            ; DDB_PM_API_Proc
        dd 0, 0                 ; DDB_V86_API_CSIP, DDB_PM_API_CSIP
        dd _vpicd_link          ; DDB_Reference_Data: the link with the VMM (w16vxd.h)
        dd svc_table            ; DDB_Service_Table_Ptr
        dd 21                   ; DDB_Service_Table_Size

        ENTRY vpicd_ctl, _vpicd_control
        ENTRY vpicd_api, _vpicd_api
        ENTRY vpicd_io_entry, _vpicd_io
        ENTRY vpicd_tmo, _vpicd_time_out

        VXDCALL _vmm_install_mult_io, 1, 95h
        VXDCALL _vmm_simulate_io, 1, 94h
        VXDCALL _vmm_set_global_time_out, 1, 3Ch
        VXDCALL _vmm_cancel_time_out, 1, 3Eh

; the PIC ports for Install_Mult_IO_Handlers (the DDK's VxD_IO table: a
; word count, then a word port and a dword procedure each)
global _vpicd_io_table
_vpicd_io_table:
        dw 4
        dw 20h
        dd vpicd_io_entry
        dw 21h
        dd vpicd_io_entry
        dw 0A0h
        dd vpicd_io_entry
        dw 0A1h
        dd vpicd_io_entry
        SERVICES _vpicd_service, 21
