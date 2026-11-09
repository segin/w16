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
        mov esp, _kstack_top
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

segment _DATA public align=4 class=DATA use32 flat
global _trap_stubs
_trap_stubs:
%assign i 0
%rep 256
        dd stub_ %+ i
%assign i i+1
%endrep

segment _BSS public align=16 class=BSS use32 flat
global _kstack_top
        resb 16384
_kstack_top:

group DGROUP _DATA _BSS
