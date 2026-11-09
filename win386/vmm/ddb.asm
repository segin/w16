; ddb.asm - the VMM's Device Descriptor Block, as in Windows 3.1's WIN386
; module: name "VMM", device ID 0001h, version 3.10, SDK 3.10, init order 0.
; Layout: Windows 3.1 DDK (VxD_Desc_Block), checked against the reference
; (docs/win386-vxds.md). The service table (242 services in 3.1) is still
; empty: ARCHITECTURE.md 12.4.

        bits 32

segment _TEXT public align=16 class=CODE use32 flat
global VMM_DDB
VMM_DDB:
        dd 0                    ; DDB_Next
        dw 030Ah                ; DDB_SDK_Version
        dw 0001h                ; DDB_Req_Device_Number
        db 3, 10                ; DDB_Dev_Major_Version, Minor
        dw 0                    ; DDB_Flags
        db 'VMM     '           ; DDB_Name
        dd 0                    ; DDB_Init_Order
        dd vmm_control          ; DDB_Control_Proc
        dd 0                    ; DDB_V86_API_Proc
        dd 0                    ; DDB_PM_API_Proc
        dd 0                    ; DDB_V86_API_CSIP
        dd 0                    ; DDB_PM_API_CSIP
        dd 0                    ; DDB_Reference_Data
        dd 0                    ; DDB_Service_Table_Ptr
        dd 0                    ; DDB_Service_Table_Size

vmm_control:
        clc
        ret
