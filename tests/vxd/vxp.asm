; vxp.asm - VXP.386, a probe VxD (w16's own, test only; ARCHITECTURE.md 12):
; loaded with "device=vxp.386" in SYSTEM.INI [386Enh], under Windows 3.1's
; WIN386 or w16's, it writes to COM1 (DOSBox-X serial1=file) one line per
; control message the VMM sends it, with EAX, EBX and ESI (and, the first
; time, the command tail ESI points at), and at the first message the
; results of a few VMM services called through INT 20h dynamic links:
;   VMM 0000h Get_VMM_Version, 0001h Get_Cur_VM_Handle,
;   0003h Get_Sys_VM_Handle; VTD 0000h VTD_Get_Version, 0002h
;   VTD_Get_Interrupt_Period.
; Lines start with "VXP ". Built with IOPROBE defined it is VXI.386, which
; also installs an I/O handler (Install_IO_Handler) for ports 2F0h and
; 0E0h at Sys_Critical_Init and logs every call the VMM makes to it (the
; I/O type in ECX, port, data, the client's CS:EIP and CX); the first six
; different types it sees are taken as the plain byte/word/dword in/out
; ones (VX7.COM, tests/dos/vx7.asm, makes those first) and answered
; directly (input: 87654300h plus the call count), any other type goes to
; Simulate_IO, which shows how it is broken down. It also virtualises IRQ
; 10 (VPICD_Virtualize_IRQ, every VID procedure its own, logging the IRQ's
; VPICD_Get_Complete_Status) and sets its request (VPICD_Set_Int_Request)
; when the VM writes A5h to port 0E0h: VX7 then sees the interrupt, reads
; the virtual PIC and EOIs, and the log shows the order of the calls (the
; second time VX7's handler waits about a second with interrupts on, for
; the IRET time-out of 500 ms). Built with BADLINK defined it is VXQ.386, which
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
%elifdef IOPROBE
        db 'VXI     '           ; DDB_Name
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
%ifdef IOPROBE
        mov esi, msg_ioi
        call puts
        mov esi, io_handler
        mov edx, 2F0h
        int 20h
        dd 00010096h                    ; VMM, Install_IO_Handler
        pushfd
        pop eax
        and eax, 1
        call puthex
        mov esi, io_handler
        mov edx, 0E0h
        int 20h
        dd 00010096h
        pushfd
        pop eax
        and eax, 1
        call puthex
        mov esi, io_handler             ; again: one handler per port
        mov edx, 2F0h
        int 20h
        dd 00010096h
        pushfd
        pop eax
        and eax, 1
        call puthex
        call crlf
        mov esi, msg_virq
        call puts
        mov edi, irq_desc
        int 20h
        dd 00030001h                    ; VPICD_Virtualize_IRQ
        mov [irq_h], eax
        pushfd
        pop ebx
        and ebx, 1
        call puthex
        mov esi, msg_cf
        call puts
        mov eax, ebx
        call puthex
        call crlf
%endif
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

%ifdef IOPROBE
; the I/O handler: EBX VM, ECX type, EDX port, EBP client registers, EAX
; output data; input data back in EAX
io_handler:
        pushad
        inc dword [io_count]
        mov esi, msg_io
        call puts
        mov eax, ecx
        call puthex
        mov esi, msg_edx
        call puts
        mov eax, edx
        call puthex
        mov esi, msg_iodat
        call puts
        mov eax, [esp + 28]
        call puthex
        mov esi, msg_iocs
        call puts
        movzx eax, word [ebp + 28h]     ; Client_CS
        call puthex
        mov al, ':'
        call putc
        mov eax, [ebp + 24h]            ; Client_EIP
        call puthex
        mov esi, msg_iocx
        call puts
        mov eax, [ebp + 18h]            ; Client_ECX
        call puthex
        mov esi, msg_ioebx
        call puts
        mov eax, ebx
        call puthex
        call crlf
        popad
        ; a type seen among the first six: plain; else Simulate_IO
        push esi
        mov esi, io_types
.t:     cmp esi, io_types + 6 * 4
        jae .new
        cmp dword [esi], -1
        je .add
        cmp [esi], ecx
        je .plain
        add esi, 4
        jmp .t
.add:   mov [esi], ecx
.plain: pop esi
        cmp edx, 0E0h
        jne .r
        cmp ecx, 4
        jne .r
        cmp al, 0A5h
        jne .r
        pushad
        mov esi, msg_set
        call puts
        call irqstat
        mov eax, [irq_h]
        int 20h
        dd 00030002h                    ; VPICD_Set_Int_Request (EBX the VM)
        mov esi, msg_seta
        call puts
        call irqstat
        popad
.r:     mov eax, 87654300h
        add eax, [io_count]
        ret
.new:   pop esi
        int 20h
        dd 00018094h                    ; VMMjmp Simulate_IO
; " status=<VPICD_Get_Complete_Status ECX>" and the end of the line
irqstat:
        pushad
        mov esi, msg_st
        call puts
        mov eax, [irq_h]
        int 20h
        dd 00030005h                    ; VPICD_Get_Complete_Status
        mov eax, ecx
        call puthex
        call crlf
        popad
        ret

; the VID procedures (EAX the handle, EBX the VM)
vxi_hw: ret
vxi_virt:
        pushad
        mov esi, msg_vvirt
        call puts
        call irqstat
        popad
        ret
vxi_eoi:
        pushad
        mov esi, msg_veoi
        call puts
        call irqstat
        mov eax, [irq_h]
        int 20h
        dd 00030003h                    ; VPICD_Clear_Int_Request
        mov eax, [irq_h]
        int 20h
        dd 00030004h                    ; VPICD_Phys_EOI
        mov esi, msg_veoi2
        call puts
        call irqstat
        popad
        ret
vxi_mask:
        pushad
        mov esi, msg_vmask
        call puts
        mov eax, ecx
        call puthex
        call irqstat
        popad
        ret
vxi_iret:
        pushfd
        pushad
        mov esi, msg_viret
        call puts
        mov eax, [esp + 32]
        and eax, 1
        call puthex
        call irqstat
        popad
        popfd
        ret

irq_desc dw 10, 0
         dd vxi_hw, vxi_virt, vxi_eoi, vxi_mask, vxi_iret, 500
irq_h    dd 0
msg_virq db 'VXP VPICD_Virtualize_IRQ(10) eax=', 0
msg_cf   db ' cf=', 0
msg_st   db ' status=', 0
msg_set  db 'VXP set request:', 0
msg_seta db 'VXP after set:', 0
msg_vvirt db 'VXP VID_Virt_Int_Proc:', 0
msg_veoi db 'VXP VID_EOI_Proc:', 0
msg_veoi2 db 'VXP after clear, phys EOI:', 0
msg_vmask db 'VXP VID_Mask_Change_Proc ecx=', 0
msg_viret db 'VXP VID_IRET_Proc cf=', 0
io_count dd 0
io_types dd -1, -1, -1, -1, -1, -1
msg_ioi  db 'VXP Install_IO_Handler cf 2F0h, 0E0h, 2F0h again=', 0
msg_io   db 'VXP io type=', 0
msg_iodat db ' data=', 0
msg_iocs db ' at=', 0
msg_iocx db ' client_ecx=', 0
msg_ioebx db ' ebx=', 0
%endif

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
