; vxdrt.inc - macros for w16's VxD modules written in C (win386/vxd;
; ARCHITECTURE.md 12.8). Register block: struct vregs (w16vxd.h): EDI ESI
; EBP ESP EBX EDX ECX EAX (PUSHAD order), EFLAGS, one word of the entry's.

extern vxd_entry_common, vxd_svc_common

; ENTRY label, cfunc: a procedure others call with registers (a control
; procedure, an API procedure, a callback): cfunc(struct vregs *) gets
; them, changes them, and they go back, flags included.
%macro ENTRY 2
global %1
extern %2
%1:     push dword %2
        jmp vxd_entry_common
%endmacro

; SERVICES cfunc, count: the DDB's service table, each entry calling
; cfunc(struct vregs *) with the service number in arg
%macro SERVICES 2
extern %1
svc_table:
%assign i 0
%rep %2
        dd svc_ %+ i
%assign i i+1
%endrep
%assign i 0
%rep %2
svc_ %+ i:
        push dword i
        push dword %1
        jmp vxd_svc_common
%assign i i+1
%endrep
%endmacro

; VXDCALL name, device, service: void name(struct vregs *) in C, a
; dynamic link (INT 20h) to that service with the block's registers
%macro VXDCALL 3
global %1
%1:     push ebx
        push esi
        push edi
        push ebp
        mov ebp, [esp+20]
        push ebp
        mov eax, [ebp+28]
        mov ebx, [ebp+16]
        mov ecx, [ebp+24]
        mov edx, [ebp+20]
        mov esi, [ebp+4]
        mov edi, [ebp+0]
        push dword [ebp+32]
        popfd
        mov ebp, [ebp+8]
        int 20h
        dd ((%2) << 16) | (%3)
        xchg ebp, [esp]
        mov [ebp+28], eax
        mov [ebp+16], ebx
        mov [ebp+24], ecx
        mov [ebp+20], edx
        mov [ebp+4], esi
        mov [ebp+0], edi
        pop dword [ebp+8]
        pushfd
        pop dword [ebp+32]
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
%endmacro
