; vxhddb.asm - VXH.386, the memory and paging services' probe VxD (w16's
; own, test only; tests/vxd/vxh.c has what it does): its DDB, entry points
; and dynamic links. Device ID 7E16h (a number of w16's own, for its V86
; API: INT 2Fh 1684h), the last init order, no services.

        bits 32
        cpu 386
%include "vxdrt.inc"

segment _TEXT public align=16 class=CODE use32 flat
global VXH_DDB
VXH_DDB:
        dd 0
        dw 030Ah
        dw 7E16h
        db 1, 0
        dw 0
        db 'VXH     '
        dd 0FFFFFFFFh           ; the last
        dd vxh_ctl
        dd vxh_v86              ; V86 API
        dd 0                    ; PM API
        dd 0, 0, 0
        dd 0, 0

        ENTRY vxh_ctl, _vxh_control
        ENTRY vxh_v86, _vxh_api
        ENTRY vxh_hook, _vxh_hook_proc
        ENTRY vxh_hook2, _vxh_hook_proc2

; the V86 fault hook (Hook_V86_Fault, fault 06h): EBX the VM, EBP the
; client registers; handled (vxh_ud_check nonzero): return; else chain to
; the previous handler with every register as it came
global vxh_ud
extern _vxh_ud_check, _vxh_ud_prev
vxh_ud: pushad
        push ebp
        push ebx
        call _vxh_ud_check
        add esp, 8
        test eax, eax
        popad
        jz .chain
        ret
.chain: cmp dword [_vxh_ud_prev], 0
        je .none
        jmp [_vxh_ud_prev]
.none:  ret

; C-convention services whose EDX matters too: vu32 name(args...), EDX
; and the flags after the call kept in vxh_edx and vxh_fl
extern _vxh_edx, _vxh_fl
%macro CCALL 3
global %1
%1:     push ebx
        push esi
        push edi
        push ebp
%rep %3
        push dword [esp + 20 + 4 * (%3 - 1)]
%endrep
        int 20h
        dd (1 << 16) | (%2)
        add esp, 4 * %3
        mov [_vxh_edx], edx
        pushfd
        pop dword [_vxh_fl]
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
%endmacro

        CCALL __PageAllocate, 53h, 8
        CCALL __PageReAllocate, 54h, 3
        CCALL __PageFree, 55h, 2
        CCALL __PageLock, 56h, 4
        CCALL __PageUnLock, 57h, 4
        CCALL __PageGetSizeAddr, 58h, 2
        CCALL __PageGetAllocInfo, 59h, 1
        CCALL __GetFreePageCount, 5Ah, 1
        CCALL __GetSysPageCount, 5Bh, 1
        CCALL __GetVMPgCount, 5Ch, 2
        CCALL __MapIntoV86, 5Dh, 6
        CCALL __PhysIntoV86, 5Eh, 5
        CCALL __TestGlobalV86Mem, 5Fh, 3
        CCALL __ModifyPageBits, 60h, 7
        CCALL __CopyPageTable, 61h, 4
        CCALL __LinMapIntoV86, 62h, 5
        CCALL __LinPageLock, 63h, 3
        CCALL __LinPageUnLock, 64h, 3
        CCALL __SetResetV86Pageable, 65h, 4
        CCALL __GetV86PageableArray, 66h, 3
        CCALL __PageCheckLinRange, 67h, 3
        CCALL __PageOutDirtyPages, 68h, 2
        CCALL __PageDiscardPages, 69h, 4
        CCALL __GetNulPageHandle, 6Ah, 0
        CCALL __GetFirstV86Page, 6Bh, 0
        CCALL __MapPhysToLinear, 6Ch, 3
        CCALL __SelectorMapFlat, 6Eh, 3
        CCALL __GetDemandPageInfo, 6Fh, 2
        CCALL __GetSetPageOutCount, 70h, 2
        CCALL __Assign_Device_V86_Pages, 72h, 4
        CCALL __DeAssign_Device_V86_Pages, 73h, 4
        CCALL __Get_Device_V86_Pages_Array, 74h, 3
        CCALL __Allocate_Device_CB_Area, 0A7h, 2
        CCALL __Allocate_Global_V86_Data_Area, 0A8h, 2
        CCALL __Allocate_Temp_V86_Data_Area, 0A9h, 2
        CCALL __Free_Temp_V86_Data_Area, 0AAh, 0
        CCALL __AddFreePhysPage, 0D2h, 3
        CCALL __GetLastV86Page, 0D5h, 0

; register services: void name(struct vregs *)
        VXDCALL _Get_Cur_VM_Handle, 1, 1
        VXDCALL _Get_Sys_VM_Handle, 1, 3
        VXDCALL _Map_Lin_To_VM_Addr, 1, 1Dh
        VXDCALL _Get_System_Time, 1, 3Fh
        VXDCALL _Hook_V86_Page, 1, 71h
        VXDCALL _Hook_V86_Fault, 1, 7Fh
        VXDCALL _Get_Profile_Decimal_Int, 1, 0ABh
