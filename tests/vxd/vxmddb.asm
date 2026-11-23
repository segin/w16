; vxmddb.asm - VXM.386, the Virtual DMA Device's probe VxD (w16's own,
; test only; tests/vxd/vxm.c has what it does): its DDB, entry points and
; dynamic links. Undefined device ID and init order, no services, no API.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VXM_DDB
VXM_DDB:
        dd 0
        dw 030Ah
        dw 0                    ; Undefined_Device_ID
        db 1, 0
        dw 0
        db 'VXM     '
        dd 80000000h            ; Undefined_Init_Order
        dd vxm_ctl
        dd 0, 0, 0, 0, 0
        dd 0, 0

        ENTRY vxm_ctl, _vxm_control
        ENTRY vxm_cb, _vxm_callback
        ENTRY vxm_io, _vxm_iohandler
        ENTRY vxm_tmo, _vxm_timeout

; VMM
        VXDCALL _vmm_install_io, 1, 96h
        VXDCALL _vmm_get_sys_vm, 1, 3
        VXDCALL _vmm_set_global_time_out, 1, 3Ch
; VDMAD (0004h), every service by number
%assign i 0
%rep 24
        VXDCALL _vdmad_ %+ i, 4, i
%assign i i+1
%endrep
