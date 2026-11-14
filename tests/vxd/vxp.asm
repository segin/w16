; vxp.asm - VXP.386, a probe VxD (w16's own, test only; ARCHITECTURE.md 12):
; loaded with "device=vxp.386" in SYSTEM.INI [386Enh], under Windows 3.1's
; WIN386 or w16's, it writes to COM1 (DOSBox-X serial1=file) one line per
; control message the VMM sends it, with EAX, EBX and ESI (and, the first
; time, the command tail ESI points at), and at the first message the
; results of a few VMM services called through INT 20h dynamic links:
;   VMM 0000h Get_VMM_Version, 0001h Get_Cur_VM_Handle,
;   0003h Get_Sys_VM_Handle; VTD 0000h VTD_Get_Version, 0002h
;   VTD_Get_Interrupt_Period.
; Lines start with "VXP ". Built with BADLINK defined it is VXQ.386, which
; first calls service 0 of device 000Fh (not loaded): the dynamic-link
; failure. Sources: the Windows 3.1 DDK's Virtual Device
; Adaptation Guide (Declare_Virtual_Device, the control messages, VMMcall:
; INT 20h followed by a dword, service number in the low word and device
; ID in the high word); the DDB layout as win386/vmm/ddb.asm. Nothing here
; comes from Microsoft's code.

        bits 32

COM1    equ 3F8h

; CALLLOG message, link, EAX, EBX (-1: the System VM), ECX: the call, then
; EAX EBX ECX EDX and the flags (CF bit 0, ZF bit 6)
%macro CALLLOG 5
        mov esi, %1
        call puts
        mov eax, %3
        mov ebx, %4
        cmp ebx, -1
        jne %%nb
        mov ebx, [sysvm_h]
%%nb:   mov ecx, %5
        xor edx, edx
        int 20h
        dd %2
        pushfd
        pushad
        call regdump
        popad
        popfd
%endmacro


segment _TEXT public align=16 class=CODE use32 flat
global VXP_DDB
VXP_DDB:
        dd 0                    ; DDB_Next
        dw 030Ah                ; DDB_SDK_Version
        dw 0                    ; DDB_Req_Device_Number: Undefined_Device_ID
        db 1, 0                 ; DDB_Dev_Major_Version, Minor
        dw 0                    ; DDB_Flags
%ifdef BADLINK
        db 'VXQ     '           ; DDB_Name
%else
        db 'VXP     '           ; DDB_Name
%endif
        dd 80000000h            ; DDB_Init_Order: Undefined_Init_Order
        dd vxp_control          ; DDB_Control_Proc
        dd 0                    ; DDB_V86_API_Proc
        dd 0                    ; DDB_PM_API_Proc
        dd 0                    ; DDB_V86_API_CSIP
        dd 0                    ; DDB_PM_API_CSIP
        dd 0                    ; DDB_Reference_Data
        dd 0                    ; DDB_Service_Table_Ptr
        dd 0                    ; DDB_Service_Table_Size

; the control procedure: EAX = message; carry clear = success
vxp_control:
        pushad
        mov edi, eax
        mov esi, msg_ctl
        call puts
        mov eax, edi
        call puthex
        mov esi, msg_ebx
        call puts
        mov eax, [esp + 16]             ; EBX as it came (PUSHAD)
        call puthex
        mov esi, msg_esi
        call puts
        mov eax, [esp + 4]              ; ESI
        call puthex
        mov esi, msg_edx
        call puts
        mov eax, [esp + 20]             ; EDX
        call puthex
        mov esi, msg_edi
        call puts
        mov eax, [esp]                  ; EDI
        call puthex
        ; the first message (the DDK: Sys_Critical_Init comes first)
        cmp byte [tail_done], 0
        jne .notfirst
        mov byte [tail_done], 1
        ; the command tail: a length byte, then the text
        mov esi, msg_tail
        call puts
        mov esi, [esp + 4]
        test esi, esi
        jz .notail
        movzx ecx, byte [esi]
        cmp ecx, 64
        jbe .tl
        mov ecx, 64
.tl:    mov eax, ecx
        call puthex
        mov al, ' '
        call putc
        mov al, '"'
        call putc
        jecxz .tdone
.tc:    inc esi
        mov al, [esi]
        call putc
        loop .tc
.tdone: mov al, '"'
        call putc
.notail:
        call crlf
        call services
        popad
        clc
        ret
.notfirst:
        call crlf
        cmp edi, 5                      ; System_Exit
        jne .nl
        call late
.nl:    popad
        clc
        ret

services:
%ifdef BADLINK
        ; VXQ.386: a dynamic link to a device that is not loaded: VPD
        ; (000Fh, WfW 3.11's printer device, not in Windows 3.1). (With
        ; an ID no device has, 7FFFh, Windows 3.1 went on as if nothing
        ; happened.)
        int 20h
        dd 000F0000h
%endif
        mov esi, msg_ver
        call puts
        int 20h
        dd 00010000h                    ; VMM, Get_VMM_Version
        push ecx
        call puthex
        mov esi, msg_ecx
        call puts
        pop eax
        call puthex
        call crlf
        mov esi, msg_cur
        call puts
        int 20h
        dd 00010001h                    ; VMM, Get_Cur_VM_Handle
        mov eax, ebx
        call puthex
        call crlf
        mov esi, msg_sys
        call puts
        int 20h
        dd 00010003h                    ; VMM, Get_Sys_VM_Handle
        mov eax, ebx
        call puthex
        call crlf
        ; the control block's first 8 dwords (the DDK names CB_VM_Status,
        ; CB_High_Linear, CB_Client_Pointer, CB_VMID among its fields)
        mov esi, msg_cb
        call puts
        mov ecx, 8
.cb:    mov eax, [ebx]
        call puthex
        mov al, ' '
        call putc
        add ebx, 4
        loop .cb
        call crlf
        ; VTD (device 0005h): 0000h VTD_Get_Version, 0002h
        ; VTD_Get_Interrupt_Period (VTDAPI.386 calls both)
        mov esi, msg_vtdv
        call puts
        int 20h
        dd 00050000h
        push ecx
        push ebx
        call puthex
        mov esi, msg_ebx
        call puts
        pop eax
        call puthex
        mov esi, msg_ecx
        call puts
        pop eax
        call puthex
        call crlf
        mov esi, msg_vtdp
        call puts
        int 20h
        dd 00050002h
        call puthex
        call crlf
        ; the keyboard, mouse and interrupt devices (ARCHITECTURE.md 12.8):
        ; registers and flags after each call
        CALLLOG msg_vkdv, 000D0000h, 0, 0, 0     ; VKD_Get_Version
        CALLLOG msg_vkdo, 000D0008h, 0, 0, 0     ; VKD_Get_Kbd_Owner
        call sysvm
        CALLLOG msg_vkdp, 000D000Dh, 0, -1, 0    ; VKD_Peek_Msg_Key, EBX = System VM
        CALLLOG msg_vmdv, 000C0000h, 0, 0, 0     ; VMD_Get_Version
        CALLLOG msg_vmdo, 000C0002h, 0, 0, 0     ; VMD_Get_Mouse_Owner
        CALLLOG msg_vmdt, 000C0001h, -74h, 0, 4  ; VMD_Set_Mouse_Type: INT 74h, type 4 (as MOUSE.DRV's BX=7404h)
        CALLLOG msg_pcv, 00030000h, 0, 0, 0      ; VPICD_Get_Version
        CALLLOG msg_pcb1, 0003000Bh, 1, 0, 0     ; VPICD_Get_IRQ_Complete_Status IRQ 1
        CALLLOG msg_pcb5, 0003000Bh, 5, 0, 0     ;   IRQ 5 (no device)
        CALLLOG msg_pcb0, 0003000Bh, 0, 0, 0     ;   IRQ 0
        CALLLOG msg_pcbc, 0003000Bh, 12, 0, 0    ;   IRQ 12
        CALLLOG msg_pcd1, 0003000Dh, 1, -1, 0    ; VPICD_Convert_IRQ_To_Int IRQ 1, System VM
        CALLLOG msg_pcdc, 0003000Dh, 12, -1, 0   ;   IRQ 12
        CALLLOG msg_pcdg, 0003000Dh, 16, -1, 0   ;   IRQ 16 (none)
        CALLLOG msg_pce, 0003000Eh, 74h, 0, 0    ; VPICD_Convert_Int_To_IRQ 74h
        CALLLOG msg_pce2, 0003000Eh, 21h, 0, 0   ;   21h
        ret

; at System_Exit: who owns the keyboard and the mouse after the session
late:   call sysvm
        CALLLOG msg_vkdo, 000D0008h, 0, 0, 0
        CALLLOG msg_vmdo, 000C0002h, 0, 0, 0
        CALLLOG msg_pcb1, 0003000Bh, 1, 0, 0
        CALLLOG msg_pcbc, 0003000Bh, 12, 0, 0
        ret

; the System VM's handle into sysvm_h
sysvm:  int 20h
        dd 00010003h
        mov [sysvm_h], ebx
        ret

; regdump: the PUSHAD frame and the flags above it (after the return address)
regdump:
        mov eax, [esp + 4 + 28]         ; EAX
        call puthex
        mov esi, msg_ebx
        call puts
        mov eax, [esp + 4 + 16]
        call puthex
        mov esi, msg_ecx
        call puts
        mov eax, [esp + 4 + 24]
        call puthex
        mov esi, msg_edx
        call puts
        mov eax, [esp + 4 + 20]
        call puthex
        mov esi, msg_fl
        call puts
        mov eax, [esp + 4 + 32]
        and eax, 0041h
        call puthex
        call crlf
        ret

; serial output, polled
putc:   push edx
        push eax
        mov dx, COM1 + 5
.w:     in al, dx
        test al, 20h
        jz .w
        pop eax
        mov dx, COM1
        out dx, al
        pop edx
        ret

puts:   lodsb
        test al, al
        jz .e
        call putc
        jmp puts
.e:     ret

crlf:   mov al, 13
        call putc
        mov al, 10
        jmp putc

; EAX in 8 hex digits
puthex: push ecx
        push eax
        mov ecx, 8
.d:     rol eax, 4
        push eax
        and eax, 15
        mov al, [hexd + eax]
        call putc
        pop eax
        loop .d
        pop eax
        pop ecx
        ret

hexd     db '0123456789ABCDEF'
msg_ctl  db 'VXP control eax=', 0
msg_ebx  db ' ebx=', 0
msg_esi  db ' esi=', 0
msg_edx  db ' edx=', 0
msg_edi  db ' edi=', 0
msg_tail db ' tail=', 0
msg_ver  db 'VXP Get_VMM_Version eax=', 0
msg_ecx  db ' ecx=', 0
msg_cur  db 'VXP Get_Cur_VM_Handle ebx=', 0
msg_sys  db 'VXP Get_Sys_VM_Handle ebx=', 0
msg_cb   db 'VXP sysvm cb=', 0
msg_vtdv db 'VXP VTD_Get_Version eax=', 0
msg_vtdp db 'VXP VTD_Get_Interrupt_Period eax=', 0
msg_vkdv db 'VXP VKD_Get_Version eax=', 0
msg_vkdo db 'VXP VKD_Get_Kbd_Owner eax=', 0
msg_vkdp db 'VXP VKD_Peek_Msg_Key eax=', 0
msg_vmdv db 'VXP VMD_Get_Version eax=', 0
msg_vmdo db 'VXP VMD_Get_Mouse_Owner eax=', 0
msg_vmdt db 'VXP VMD_Set_Mouse_Type eax=', 0
msg_pcv  db 'VXP VPICD_Get_Version eax=', 0
msg_fl   db ' fl=', 0
msg_pcb1 db 'VXP VPICD_Get_IRQ_Complete_Status(1) eax=', 0
msg_pcb5 db 'VXP VPICD_Get_IRQ_Complete_Status(5) eax=', 0
msg_pcb0 db 'VXP VPICD_Get_IRQ_Complete_Status(0) eax=', 0
msg_pcbc db 'VXP VPICD_Get_IRQ_Complete_Status(12) eax=', 0
msg_pcd1 db 'VXP VPICD_Convert_IRQ_To_Int(1) eax=', 0
msg_pcdc db 'VXP VPICD_Convert_IRQ_To_Int(12) eax=', 0
msg_pcdg db 'VXP VPICD_Convert_IRQ_To_Int(16) eax=', 0
msg_pce  db 'VXP VPICD_Convert_Int_To_IRQ(74h) eax=', 0
msg_pce2 db 'VXP VPICD_Convert_Int_To_IRQ(21h) eax=', 0
sysvm_h  dd 0
tail_done db 0
