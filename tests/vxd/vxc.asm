; vxc.asm - VXC.386, a probe VxD (w16's own, test only): the VMM services
; Visual C++ 1.52's devices (DOSXNT.386, CVW1.386) use to hook protected-
; mode interrupts and to get memory (ARCHITECTURE.md 12.7; tests/
; msvcvxd.sh vxc). Loaded with "device=vxc.386" under Windows 3.1's WIN386
; or w16's, it writes lines starting "VXC " to COM1 (DOSBox-X
; serial1=file):
;   - at Sys_Critical_Init: Get_PM_Int_Vector for INT 10h, 21h, 2Fh and
;     31h (CX:EDX and ZF, the default), and ZF alone for 21 more vectors;
;     whether INT 10h and 31h share a selector; Log_Proc_Call with known
;     registers and carry set (which change); Allocate_PM_Call_Back twice
;     (carry, whether the two share a selector, how far apart their
;     offsets are); Set_PM_Int_Vector of INT 2Fh to the first, and
;     Get_PM_Int_Vector again (the address back, ZF); INT 21h and 31h to
;     callbacks of their own too; _PageAllocate of 2 pages PG_VM for the
;     System VM with PageZeroInit and PageLocked (a handle and an address,
;     the memory zero, written and read back), _PageGetSizeAddr of it,
;     _PageFree; _MapPhysToLinear of 0A0000h for 64 KB (failure or not);
;   - at the first protected-mode INT 21h, 2Fh or 31h: the GDT's
;     descriptor 40h (base, limit, access), and the real-mode INT 21h
;     vector whenever a hooked call finds it changed;
;   - every protected-mode INT 2Fh with AX 4400h-44FFh or 8600h-86FFh
;     (CodeView for Windows, CVW.EXE, makes such calls), and every INT
;     21h, 2Fh and 31h from its first AX=8600h until AX=4415h is back:
;     the registers the call came with, then, through a second callback
;     the hook returns to (an interrupt frame of its own pushed with
;     Simulate_Push before it jumps on to the previous vector with
;     Simulate_Far_Jmp), what came back (Simulate_Iret then pops the
;     caller's frame, the returned flags kept);
;   - at System_Exit: how many INT 2Fh calls the hook saw, and how many
;     with AX=1689h (KERNEL's idle call).
; Built with DOSVM defined it is VXCD.386, which logs every INT 21h, 2Fh
; and 31h of the DOS VMs instead (a DOS extender's calls in a box).
; Built with UNKNOWN defined it is VXCU.386, which first calls
; In_Debug_Chr, a VMM service w16 does not have: w16 must end with a
; message naming it.
; Its init order is 82000000h: after CVW1.386's (81000000h), so its hooks
; are the outer ones and see what CVW1's and the VMM's give back.
; Black box (ARCHITECTURE.md 3): only what the services return, what
; callers pass and the processor's descriptor tables. Sources: the Windows
; 3.1 DDK's Virtual Device Adaptation Guide (the services,
; Client_Reg_Struc, the page flags), the service numbers as
; docs/win386-services.md lists them, the Intel 386 manual (descriptors).

        bits 32

COM1    equ 3F8h

; Client_Reg_Struc (the DDK)
C_EBX   equ 10h
C_EDX   equ 14h
C_ECX   equ 18h
C_EAX   equ 1Ch
C_EIP   equ 24h
C_CS    equ 28h
C_FL    equ 2Ch
C_ES    equ 38h
C_DS    equ 3Ch

%macro VMMCALL 1
        int 20h
        dd 00010000h + %1
%endmacro

segment _TEXT public align=16 class=CODE use32 flat
global VXC_DDB
VXC_DDB:
        dd 0                    ; DDB_Next
        dw 030Ah                ; DDB_SDK_Version
        dw 0                    ; DDB_Req_Device_Number: Undefined_Device_ID
        db 1, 0                 ; DDB_Dev_Major_Version, Minor
        dw 0                    ; DDB_Flags
        db 'VXC     '           ; DDB_Name
        dd 82000000h            ; DDB_Init_Order
        dd vxc_control          ; DDB_Control_Proc
        dd 0, 0, 0, 0, 0, 0, 0  ; APIs, reference data, services

vxc_control:
        pushad
        test eax, eax
        jnz .n0
        call init
        jmp .done
.n0:    cmp eax, 5                      ; System_Exit
        jne .done
        mov esi, m_exit
        call puts
        mov eax, [n2f]
        call puthex
        mov esi, m_1689
        call puts
        mov eax, [n1689]
        call puthex
        call crlf
.done:  popad
        clc
        ret

; one line: "VXC name ecx=.. edx=.. zf=." for Get_PM_Int_Vector of EAX
pmvec:  push eax
        mov esi, m_get
        call puts
        pop eax
        push eax
        call puthex
        pop eax
        VMMCALL 44h                     ; Get_PM_Int_Vector
        pushfd
        push edx
        push ecx
        mov esi, m_ecx
        call puts
        pop eax
        call puthex
        mov esi, m_edx
        call puts
        pop eax
        call puthex
        mov esi, m_zf
        call puts
        pop eax
        shr eax, 6
        and eax, 1
        call puthex
        call crlf
        ret

init:
%ifdef UNKNOWN
        ; VXCU.386: In_Debug_Chr, a VMM service w16 does not have (no
        ; debugger): w16 must end Windows with a message naming it
        VMMCALL 0C4h
%endif
        mov eax, 10h
        call pmvec
        mov [sel10], cx
        mov eax, 21h
        call pmvec
        mov [old_sel], ecx
        mov [old_off], edx
        mov eax, 2Fh
        call pmvec
        mov [old_sel + 4], ecx
        mov [old_off + 4], edx
        mov eax, 31h
        call pmvec
        mov [old_sel + 8], ecx
        mov [old_off + 8], edx
        ; which vectors are the default (ZF), for a range of them
        push ecx
        mov esi, m_zfs
        call puts
        mov ebx, zfvecs
.zl:    movzx eax, byte [ebx]
        cmp al, 0FFh
        je .zd
        push eax
        VMMCALL 44h
        setz cl
        pop eax
        shl eax, 4
        or al, cl
        call puthex
        mov al, ' '
        call putc
        inc ebx
        jmp .zl
.zd:    call crlf
        pop ecx
        mov esi, m_same
        call puts
        xor eax, eax
        cmp cx, [sel10]
        sete al
        call puthex
        call crlf
        ; Log_Proc_Call: every register as it was?
        mov esi, m_log
        call puts
        mov eax, 11111111h
        mov ebx, 22222222h
        mov ecx, 33333333h
        mov edx, 44444444h
        mov esi, 55555555h
        mov edi, 66666666h
        stc
        VMMCALL 0CBh                    ; Log_Proc_Call
        pushfd
        pop dword [lpc_fl]
        cmp eax, 11111111h
        setne byte [lpc_ch]
        cmp ebx, 22222222h
        setne byte [lpc_ch + 1]
        cmp ecx, 33333333h
        setne byte [lpc_ch + 2]
        cmp edx, 44444444h
        setne byte [lpc_ch + 3]
        cmp esi, 55555555h
        setne byte [lpc_ch + 4]
        cmp edi, 66666666h
        setne byte [lpc_ch + 5]
        xor eax, eax                    ; bit n: register n changed
        mov ecx, 6
.lb:    shl eax, 1
        or al, [lpc_ch + ecx - 1]
        loop .lb
        call puthex
        mov esi, m_cf
        call puts
        mov eax, [lpc_fl]
        and eax, 1
        call puthex
        call crlf
        ; two callbacks
        mov esi, m_cb
        call puts
        mov edx, 2Fh
        mov esi, cb_hook
        VMMCALL 0Ch                     ; Allocate_PM_Call_Back
        mov [cb1], eax
        pushfd
        pop eax
        and eax, 1
        call puthex
        mov edx, 2
        mov esi, cb_back
        VMMCALL 0Ch
        mov [cb2], eax
        pushfd
        pop eax
        and eax, 1
        push eax
        mov al, ' '
        call putc
        pop eax
        call puthex
        mov esi, m_samesel
        call puts
        mov eax, [cb1]
        mov ebx, [cb2]
        shr eax, 16
        shr ebx, 16
        cmp eax, ebx
        sete al
        movzx eax, al
        call puthex
        mov esi, m_delta
        call puts
        mov eax, [cb2]
        sub ax, [cb1]
        movzx eax, ax
        call puthex
        call crlf
        ; INT 2Fh to the first
        mov eax, 2Fh
        movzx ecx, word [cb1 + 2]
        movzx edx, word [cb1]
        VMMCALL 45h                     ; Set_PM_Int_Vector
        mov eax, 2Fh
        VMMCALL 44h
        pushfd
        mov esi, m_set
        call puts
        xor eax, eax
        cmp cx, [cb1 + 2]
        jne .ns
        cmp dx, [cb1]
        jne .ns
        mov al, 1
.ns:    call puthex
        mov esi, m_zf
        call puts
        pop eax
        shr eax, 6
        and eax, 1
        call puthex
        call crlf
        ; INT 21h and 31h to callbacks of their own (the same procedure)
        mov edx, 21h
        mov esi, cb_hook
        VMMCALL 0Ch
        movzx ecx, ax
        shr eax, 16
        xchg eax, ecx
        mov edx, eax
        mov eax, 21h
        VMMCALL 45h
        mov edx, 31h
        mov esi, cb_hook
        VMMCALL 0Ch
        movzx ecx, ax
        shr eax, 16
        xchg eax, ecx
        mov edx, eax
        mov eax, 31h
        VMMCALL 45h
        ; _PageAllocate(2, PG_VM, System VM, 0, 0, 0, 0, PageZeroInit | PageLocked)
        VMMCALL 3                       ; Get_Sys_VM_Handle
        push dword 81h
        push dword 0
        push dword 0
        push dword 0
        push dword 0
        push ebx
        push dword 0
        push dword 2
        VMMCALL 53h                     ; _PageAllocate
        add esp, 32
        mov [ph], eax
        mov [pa], edx
        mov esi, m_pa
        call puts
        xor ecx, ecx
        test eax, eax
        setnz cl
        mov eax, ecx
        call puthex
        mov esi, m_zero
        call puts
        xor eax, eax
        mov edi, [pa]
        test edi, edi
        jz .nz
        mov ecx, 2048
        repe scasd
        sete al
        mov edi, [pa]
        mov dword [edi + 4092], 0A5A55A5Ah
        cmp dword [edi + 4092], 0A5A55A5Ah
        jne .nz
        or al, 2
.nz:    call puthex
        ; _PageGetSizeAddr(h, 0)
        push dword 0
        push dword [ph]
        VMMCALL 58h
        add esp, 8
        push edx
        push eax
        mov esi, m_gsa
        call puts
        pop eax
        call puthex
        pop edx
        xor eax, eax
        cmp edx, [pa]
        sete al
        push eax
        mov al, ' '
        call putc
        pop eax
        call puthex
        ; _PageFree(h, 0)
        push dword 0
        push dword [ph]
        VMMCALL 55h
        add esp, 8
        push eax
        mov esi, m_free
        call puts
        pop eax
        xor ecx, ecx
        test eax, eax
        setnz cl
        mov eax, ecx
        call puthex
        call crlf
        ; _MapPhysToLinear(0A0000h, 10000h, 0)
        push dword 0
        push dword 10000h
        push dword 0A0000h
        VMMCALL 6Ch
        add esp, 12
        mov esi, m_mp
        call puts
        xor ecx, ecx
        cmp eax, -1
        setne cl
        mov eax, ecx
        call puthex
        call crlf
        ret

; the hooks of INT 21h, 2Fh and 31h: EBX the VM, EDX the vector, EBP the
; client registers. Logged: INT 2Fh with AH 44h or 86h always; every call
; of the three from CVW's first INT 2Fh AX=8600h until AX=4415h comes back
; (at most 1000 lines); IVT 21h whenever it has changed
cb_hook:
        mov edi, 0                      ; the vector's index in old_sel/old_off
        cmp edx, 21h
        je .ix
        mov edi, 4
        cmp edx, 2Fh
        je .ix
        mov edi, 8
.ix:    cmp byte [gdt_done], 0
        jne .gd
        mov byte [gdt_done], 1
        call gdt40
.gd:    mov eax, [84h]                  ; the current VM's IVT 21h
        cmp eax, [ivt21]
        je .iv
        mov [ivt21], eax
        push eax
        mov esi, m_ivt
        call puts
        pop eax
        call puthex
        call crlf
.iv:    mov eax, [ebp + C_EAX]
        cmp edx, 2Fh
        jne .n2
        inc dword [n2f]
        cmp ax, 1689h
        jne .n1
        inc dword [n1689]
.n1:    cmp ax, 8600h
        jne .n3
        mov byte [logon], 1
.n3:    cmp ah, 44h
        je .log
        cmp ah, 86h
        je .log
.n2:
%ifdef DOSVM
        ; VXCD.386: every call a DOS VM makes (its DPMI client's), up to
        ; 40000 lines (tests/msvcvxd.sh cl with MSVCVXD_VXCD=1)
        VMMCALL 4                       ; Test_Sys_VM_Handle (EBX)
        je .chain
        cmp dword [nlog], 40000
        jae .chain
        jmp .log
%endif
        cmp byte [logon], 0
        je .chain
        cmp dword [nlog], 1000
        jae .chain
.log:   inc dword [nlog]
        mov esi, m_in
        call puts
        mov eax, edx
        call puthex
        mov al, ' '
        call putc
        call cregs
        ; an interrupt frame back to cb_back, then on to the old vector
        mov eax, [ebp + C_FL]
        VMMCALL 4Dh                     ; Simulate_Push
        movzx eax, word [cb2 + 2]
        VMMCALL 4Dh
        movzx eax, word [cb2]
        VMMCALL 4Dh
.chain: mov ecx, [old_sel + edi]
        mov edx, [old_off + edi]
        VMMCALL 49h                     ; Simulate_Far_Jmp
        ret

; the GDT's descriptor 40h (index 8), which programs use as the BIOS data
; area's selector: base, limit (bytes, with granularity) and access byte
gdt40:  sub esp, 8
        sgdt [esp + 2]
        mov ebx, [esp + 4]              ; the GDT's base
        add esp, 8
        add ebx, 40h
        mov esi, m_gdt
        call puts
        mov al, [ebx + 7]
        shl eax, 24
        mov ecx, [ebx + 2]
        and ecx, 00FFFFFFh
        or eax, ecx
        call puthex
        mov esi, m_lim
        call puts
        movzx eax, byte [ebx + 6]
        and eax, 0Fh
        shl eax, 16
        mov ax, [ebx]
        test byte [ebx + 6], 80h
        jz .bg
        shl eax, 12
        or eax, 0FFFh
.bg:    call puthex
        mov esi, m_acc
        call puts
        movzx eax, byte [ebx + 5]
        call puthex
        call crlf
        ret

; back from the chain with what it returned
cb_back:
        mov esi, m_out
        call puts
        call cregs
        mov eax, [ebp + C_EAX]
        cmp ax, 4415h
        jne .k
        mov byte [logon], 0
.k:     mov edi, [ebp + C_FL]
        VMMCALL 47h                     ; Simulate_Iret: the caller's frame
        and edi, 08D5h                  ; the returned arithmetic flags kept
        and dword [ebp + C_FL], ~08D5h
        or [ebp + C_FL], edi
        ret

; AX BX CX DX ES DS CS:IP flags of the client, then a line end
cregs:  mov eax, [ebp + C_EAX]
        call puthex
        mov esi, m_ebx
        call puts
        mov eax, [ebp + C_EBX]
        call puthex
        mov esi, m_ecx
        call puts
        mov eax, [ebp + C_ECX]
        call puthex
        mov esi, m_edx
        call puts
        mov eax, [ebp + C_EDX]
        call puthex
        mov esi, m_esi
        call puts
        mov eax, [ebp + 4]              ; Client_ESI
        call puthex
        mov esi, m_edi
        call puts
        mov eax, [ebp]                  ; Client_EDI
        call puthex
        mov esi, m_es
        call puts
        movzx eax, word [ebp + C_ES]
        call puthex
        mov esi, m_ds
        call puts
        movzx eax, word [ebp + C_DS]
        call puthex
        mov esi, m_fl
        call puts
        mov eax, [ebp + C_FL]
        and eax, 08D5h
        call puthex
        call crlf
        ret

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
m_get    db 'VXC Get_PM_Int_Vector ', 0
m_ecx    db ' ecx=', 0
m_edx    db ' edx=', 0
m_ebx    db ' ebx=', 0
m_es     db ' es=', 0
m_esi    db ' esi=', 0
m_edi    db ' edi=', 0
m_ds     db ' ds=', 0
m_fl     db ' fl=', 0
m_zf     db ' zf=', 0
m_cf     db ' cf=', 0
zfvecs   db 08h, 09h, 13h, 15h, 16h, 1Ch, 23h, 24h, 25h, 26h, 28h, 33h, 41h, 4Bh, 5Ch, 60h, 67h, 70h, 74h, 0FEh, 0FFh
m_zfs    db 'VXC Get_PM_Int_Vector ZF (vector*10h + ZF): ', 0
m_same   db 'VXC INT 31h and 10h share a selector: ', 0
m_log    db 'VXC Log_Proc_Call changed (EAX 1 EBX 2 ECX 4 EDX 8 ESI 10 EDI 20)=', 0
m_cb     db 'VXC Allocate_PM_Call_Back cf=', 0
m_samesel db ' same selector=', 0
m_delta  db ' offset delta=', 0
m_set    db 'VXC Set_PM_Int_Vector 2Fh, Get gives it: ', 0
m_pa     db 'VXC _PageAllocate ok=', 0
m_zero   db ' zero/written=', 0
m_gsa    db ' _PageGetSizeAddr pages=', 0
m_free   db ' _PageFree ok=', 0
m_mp     db 'VXC _MapPhysToLinear A0000h ok=', 0
m_in     db 'VXC in  INT ', 0
m_out    db 'VXC out          eax=', 0
m_ivt    db 'VXC IVT 21h ', 0
m_gdt    db 'VXC GDT 40h base=', 0
m_lim    db ' limit=', 0
m_acc    db ' access=', 0
m_exit   db 'VXC System_Exit INT 2Fh calls=', 0
m_1689   db ' 1689h=', 0

sel10      dw 0
           dw 0
old_sel    dd 0, 0, 0
old_off    dd 0, 0, 0
ivt21      dd 0
nlog       dd 0
logon      db 0, 0, 0, 0
gdt_done   db 0, 0, 0, 0
cb1        dd 0
cb2        dd 0
ph         dd 0
pa         dd 0
n2f        dd 0
n1689      dd 0
lpc_fl     dd 0
lpc_ch     db 0, 0, 0, 0, 0, 0, 0, 0
