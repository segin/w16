; vmmasm.asm - w16 VMM: entry, interrupt stubs, resuming a VM, the way out
; to real mode, and processor instructions C cannot express.
; Sources: Intel 80386 Programmer's Reference Manual (interrupt and trap
; frames from ring 3 and from V86 mode, IRETD to V86 mode, leaving
; protected mode with paging on).

        bits 32
        cpu 386

SEL_DATA    equ 10h
SEL_RMCODE  equ 20h
SEL_RMDATA  equ 28h

; struct cregs offsets (vmm.h)
R_EDI   equ 0
R_ESI   equ 4
R_EBP   equ 8
R_EBX   equ 12
R_EDX   equ 16
R_ECX   equ 20
R_EAX   equ 24
R_EIP   equ 28
R_EFL   equ 32
R_ESP   equ 36
R_CS    equ 40
R_SS    equ 44
R_DS    equ 48
R_ES    equ 52
R_FS    equ 56
R_GS    equ 60

segment _TEXT public align=16 class=CODE use32 flat

global _vmm_entry, _resume, _outb, _inb, _lgdt, _lidt, _lldt, _ltr
global _k_setjmp, _k_longjmp
global _to_realmode, _get_cr0, _get_cr2, _get_cr3, _set_cr3, _hlt
extern _vmm_main, _trap

; The loader jumps here: 32-bit protected mode, paging on, CS = 08h of its
; temporary GDT (flat), interrupts off, ESI = linear address of w386_info.
_vmm_entry:
        mov ax, SEL_DATA
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, _kstack_top
        push dword 2                    ; flags clean: NT (IRETD) and IF off
        popfd
        cld
        push esi
        call _vmm_main
.hang:  hlt
        jmp .hang

; interrupt and exception stubs: an error code (0 where the processor
; pushes none), the vector, then the common path
%macro STUB 1
stub_%1:
%if (%1 == 8) || (%1 >= 10 && %1 <= 14) || (%1 == 17)
%else
        push dword 0
%endif
        push dword %1
        jmp trap_common
%endmacro
%assign i 0
%rep 256
        STUB i
%assign i i+1
%endrep

trap_common:
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, SEL_DATA
        mov ds, ax
        mov es, ax
        cld
        push esp
        call _trap
        add esp, 4
        pop gs
        pop fs
        pop es
        pop ds
        popad
        add esp, 8
        iretd

; void resume(struct cregs *r): run the client - V86 mode or ring 3 - from
; the top of the ring-0 stack. Does not return.
_resume:
        mov esi, [esp+4]
        mov esp, [_kesp0]
        test dword [esi+R_EFL], 20000h
        jz .pm
        push dword [esi+R_GS]
        push dword [esi+R_FS]
        push dword [esi+R_DS]
        push dword [esi+R_ES]
        push dword [esi+R_SS]
        push dword [esi+R_ESP]
        push dword [esi+R_EFL]
        push dword [esi+R_CS]
        push dword [esi+R_EIP]
        push dword 0
        push dword 0
        push dword 0
        push dword 0
        jmp .load
.pm:    push dword [esi+R_SS]
        push dword [esi+R_ESP]
        push dword [esi+R_EFL]
        push dword [esi+R_CS]
        push dword [esi+R_EIP]
        push dword [esi+R_DS]
        push dword [esi+R_ES]
        push dword [esi+R_FS]
        push dword [esi+R_GS]
.load:  mov edi, [esi+R_EDI]
        mov ebp, [esi+R_EBP]
        mov ebx, [esi+R_EBX]
        mov edx, [esi+R_EDX]
        mov ecx, [esi+R_ECX]
        mov eax, [esi+R_EAX]
        mov esi, [esi+R_ESI]
        pop gs
        pop fs
        pop es
        pop ds
        iretd

; void to_realmode(u32 tramp_off): jump to the loader's return code (16-bit
; protected mode, SEL_RMCODE is based at its segment) with every data
; segment register holding a real-mode-like 64 KB descriptor.
_to_realmode:
        cli
        mov eax, [esp+4]
        mov [rmvec], eax
        mov word [rmvec+4], SEL_RMCODE
        mov ax, SEL_RMDATA
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        jmp far [cs:rmvec]

; int k_setjmp(u32 jb[6]) / void k_longjmp(u32 jb[6], int v): nested V86
; execution (exec_v86) waits for the client with these
_k_setjmp:
        mov eax, [esp+4]
        mov [eax], ebx
        mov [eax+4], esi
        mov [eax+8], edi
        mov [eax+12], ebp
        lea ecx, [esp+4]
        mov [eax+16], ecx
        mov ecx, [esp]
        mov [eax+20], ecx
        xor eax, eax
        ret
_k_longjmp:
        mov edx, [esp+4]
        mov eax, [esp+8]
        mov ebx, [edx]
        mov esi, [edx+4]
        mov edi, [edx+8]
        mov ebp, [edx+12]
        mov esp, [edx+16]
        jmp [edx+20]

_outb:  mov edx, [esp+4]
        mov eax, [esp+8]
        out dx, al
        ret
_inb:   mov edx, [esp+4]
        xor eax, eax
        in al, dx
        ret
_lgdt:  mov eax, [esp+4]
        lgdt [eax]
        ret
_lidt:  mov eax, [esp+4]
        lidt [eax]
        ret
_lldt:  mov eax, [esp+4]
        lldt ax
        ret
_ltr:   mov eax, [esp+4]
        ltr ax
        ret
_get_cr0:
        mov eax, cr0
        ret
_get_cr2:
        mov eax, cr2
        ret
_get_cr3:
        mov eax, cr3
        ret
_set_cr3:
        mov eax, [esp+4]
        mov cr3, eax
        ret
_hlt:   sti
        hlt
        cli
        ret

rmvec   dd 0
        dw 0

; VxD services (vxd.c): each service's stub pushes its ID (device << 16 |
; service) and jumps here; svc_dispatch gets the registers (PUSHAD order,
; then the flags, then the ID) and changes them; they come back as the
; service's results, flags included.
global _svc_common
extern _svc_dispatch
_svc_common:
        pushfd
        pushad
        cld
        push esp
        call _svc_dispatch
        add esp, 4
        popad
        popfd
        lea esp, [esp+4]                ; the ID (LEA: the flags stay)
        ret

; u32 vmm_call_proc(proc, struct svcregs *r): call proc (a service or a
; VxD's procedure) with r's registers; r gets them back with the flags
global _vmm_call_proc
_vmm_call_proc:
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

; VTD's VID_Hw_Int_Proc (vxd.c vtd_hw): the registers as a struct svcregs
global _vtd_hw_entry
extern _vtd_hw
_vtd_hw_entry:
        push dword 0                    ; the ID word
        pushfd
        pushad
        cld
        push esp
        call _vtd_hw
        add esp, 4
        popad
        popfd
        lea esp, [esp+4]
        ret

; VTD's VID_EOI_Proc (vxd.c vtd_eoi), the same way
global _vtd_eoi_entry
extern _vtd_eoi
_vtd_eoi_entry:
        push dword 0
        pushfd
        pushad
        cld
        push esp
        call _vtd_eoi
        add esp, 4
        popad
        popfd
        lea esp, [esp+4]
        ret

; u32 vxd_control(proc, msg, ebx, esi, edx, edi, ebp): call a VxD's
; control procedure with EAX = msg and those registers; returns its carry
global _vxd_control
_vxd_control:
        push ebp
        push ebx
        push esi
        push edi
        mov eax, [esp+20+4]
        mov ebx, [esp+20+8]
        mov esi, [esp+20+12]
        mov edx, [esp+20+16]
        mov edi, [esp+20+20]
        mov ebp, [esp+20+24]
        push dword [esp+20]             ; the procedure
        pop ecx
        call ecx
        mov eax, 0
        setc al
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret

segment _DATA public align=4 class=DATA use32 flat
global _trap_stubs
_trap_stubs:
%assign i 0
%rep 256
        dd stub_ %+ i
%assign i i+1
%endrep

global _kesp0
_kesp0  dd _kstack_top                  ; the ring-0 stack top in use (= TSS.ESP0)

segment _BSS public align=16 class=BSS use32 flat
global _kstack_top
        resb 65536
_kstack_top:

group DGROUP _DATA _BSS
