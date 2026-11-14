; vxp.asm - VXP.386, a probe VxD (w16's own, test only; ARCHITECTURE.md 12):
; loaded with "device=vxp.386" in SYSTEM.INI [386Enh], under Windows 3.1's
; WIN386 or w16's, it writes to COM1 (DOSBox-X serial1=file) one line per
; control message the VMM sends it, with EAX, EBX and ESI (and, the first
; time, the command tail ESI points at), and at the first message the
; results of a few VMM services called through INT 20h dynamic links:
;   VMM 0000h Get_VMM_Version, 0001h Get_Cur_VM_Handle,
;   0003h Get_Sys_VM_Handle; VTD 0000h VTD_Get_Version, 0002h
;   VTD_Get_Interrupt_Period.
; Lines start with "VXP ". Sources: the Windows 3.1 DDK's Virtual Device
; Adaptation Guide (Declare_Virtual_Device, the control messages, VMMcall:
; INT 20h followed by a dword, service number in the low word and device
; ID in the high word); the DDB layout as win386/vmm/ddb.asm. Nothing here
; comes from Microsoft's code.

        bits 32

COM1    equ 3F8h

segment _TEXT public align=16 class=CODE use32 flat
global VXP_DDB
VXP_DDB:
        dd 0                    ; DDB_Next
        dw 030Ah                ; DDB_SDK_Version
        dw 0                    ; DDB_Req_Device_Number: Undefined_Device_ID
        db 1, 0                 ; DDB_Dev_Major_Version, Minor
        dw 0                    ; DDB_Flags
        db 'VXP     '           ; DDB_Name
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
        popad
        clc
        ret

services:
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
tail_done db 0
