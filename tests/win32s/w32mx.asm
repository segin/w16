; w32mx.asm - W32M.EXE's assembler part (tests/win32s/w32m.c): an
; exception frame of its own on FS:[0] around a probe procedure, and the
; probes that need instructions C does not give.

        bits 32
        cpu 386

segment _TEXT public align=16 class=CODE use32 flat
segment _DATA public align=16 class=DATA use32 flat
group DGROUP _DATA

segment _DATA
global _x_esp, _x_ebp, _x_resume
_x_esp:         dd 0
_x_ebp:         dd 0
_x_resume:      dd 0

segment _TEXT
extern _handler
global _run_guarded, _p_int3, _p_div0, _p_cli, _p_hlt

; void __cdecl run_guarded(void (*f)(DWORD), DWORD arg): f(arg) with the
; frame {previous, handler} on FS:[0]; an exception the handler takes
; resumes at .done with ESP and EBP as they were set here
_run_guarded:
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push dword _handler
        push dword [fs:0]
        mov [fs:0], esp
        mov [_x_esp], esp
        mov [_x_ebp], ebp
        mov dword [_x_resume], .done
        push dword [ebp+12]
        call [ebp+8]
.done:  mov esp, [_x_esp]
        mov ebp, [_x_ebp]
        pop dword [fs:0]
        add esp, 4
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret

_p_int3:
        int 3
        ret
_p_div0:
        xor edx, edx
        mov eax, 1
        mov ecx, [esp+4]
        div ecx
        ret
_p_cli:
        cli
        sti
        ret
_p_hlt:
        hlt
        ret
