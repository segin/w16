; vxdrt.asm - the run-time part shared by w16's VxD modules written in C
; (win386/vxd; ARCHITECTURE.md 12.8): the common paths of ENTRY and
; SERVICES (vxdrt.inc), and calling a procedure with a register block.

        bits 32
        cpu 386

segment _TEXT public align=16 class=CODE use32 flat
global vxd_entry_common, vxd_svc_common, _vxd_call_proc

; [cfunc][ret] on the stack
vxd_entry_common:
        push dword 0                    ; arg
        pushfd
        pushad
        cld
        push esp
        call [esp+4+32+4+4]             ; cfunc, above arg
        add esp, 4
        popad
        popfd
        lea esp, [esp+8]                ; arg, cfunc (LEA keeps the flags)
        ret

; [cfunc][number][ret] on the stack
vxd_svc_common:
        xchg eax, [esp]                 ; EAX = cfunc, the slot keeps EAX
        xchg eax, [esp+4]               ; EAX = number, the slot keeps cfunc
        xchg eax, [esp]                 ; EAX back, [esp] = number
        ; now [number][cfunc][ret]: arg is the number
        pushfd
        pushad
        cld
        push esp
        call [esp+4+32+4+4]             ; cfunc
        add esp, 4
        popad
        popfd
        lea esp, [esp+8]
        ret

; void vxd_call_proc(u32 proc, struct vregs *r): call proc with r's
; registers (EBP included); r gets them back, flags included
_vxd_call_proc:
        push ebx
        push esi
        push edi
        push ebp
        mov ebp, [esp+24]
        push ebp
        push dword [esp+24]             ; proc
        mov eax, [ebp+28]
        mov ebx, [ebp+16]
        mov ecx, [ebp+24]
        mov edx, [ebp+20]
        mov esi, [ebp+4]
        mov edi, [ebp+0]
        push dword [ebp+32]
        popfd
        mov ebp, [ebp+8]
        call [esp]
        lea esp, [esp+4]
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
