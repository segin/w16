; ldrasm.asm - the WIN386 loader's machine-level helpers (16-bit, real mode
; and V86 mode): processor checks, XMS calls, the switch to the VMM, the
; breakpoints and the way back to real mode.
;
; Linked with the loader's C code (Open Watcom, small model, __cdecl).
; Sources: Intel 80386 Programmer's Reference Manual (protected-mode entry
; with paging, the return to real mode, V86 mode), XMS 2.0 specification.

        bits 16
        cpu 386

%include "w386.inc"

group DGROUP _DATA

segment _TEXT public class=CODE use16

global _cpu_is_386, _cpu_msw, _xms_call, _pm_go, _pm_exit, _bp_table, _rm_tramp

; int cpu_is_386(void): 1 on an 80386 or later. FLAGS bits 12-15 are always
; set on an 8086/8088 and always clear in real mode on an 80286.
_cpu_is_386:
        pushf
        pushf
        pop ax
        and ax, 0FFFh
        push ax
        popf
        pushf
        pop ax
        and ax, 0F000h
        cmp ax, 0F000h
        je .no                          ; 8086
        pushf
        pop ax
        or ax, 7000h
        push ax
        popf
        pushf
        pop ax
        test ax, 7000h
        jz .no                          ; 80286
        popf
        mov ax, 1
        ret
.no:    popf
        xor ax, ax
        ret

; unsigned bp_table_off(void), rm_tramp_off(void): offsets in this segment
global _bp_table_off, _rm_tramp_off
_bp_table_off:
        mov ax, _bp_table
        ret
_rm_tramp_off:
        mov ax, _rm_tramp
        ret

; unsigned cpu_msw(void): the machine status word (bit 0: protected or V86)
_cpu_msw:
        smsw ax
        ret

; void xms_call(struct xregs *r): AX, BX, DX, SI from *r (DS:SI addresses a
; move block), the call through _xms_entry, AX, BX, DX back into *r.
_xms_call:
        push bp
        mov bp, sp
        push si
        push di
        push bx
        mov di, [bp+4]
        mov ax, [di]
        mov bx, [di+2]
        mov dx, [di+4]
        mov si, [di+6]
        call far [_xms_entry]
        mov di, [bp+4]
        mov [di], ax
        mov [di+2], bx
        mov [di+4], dx
        pop bx
        pop di
        pop si
        pop bp
        ret

; int pm_go(struct w386_info *info): enter the VMM. Returns 0 - in V86
; mode, as the System VM: the VMM starts the System VM right here.
_pm_go:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov bx, [bp+4]
        mov [cur_info], bx
        mov [bx+I_V86_SS], ss
        mov [bx+I_V86_SP], sp
        mov [bx+I_V86_CS], cs
        mov word [bx+I_V86_IP], .back
        mov [bx+I_V86_DS], ds
        mov [bx+I_V86_ES], es
        pushfd
        pop eax
        mov [bx+I_V86_FLAGS], eax
        cli
        xor eax, eax
        mov ax, ds
        shl eax, 4
        add eax, tgdt
        mov [tgdtr+2], eax
        lgdt [tgdtr]
        mov eax, [bx+I_ENTRY_LIN]
        mov [jmpvec], eax
        mov word [jmpvec+4], 08h
        mov esi, [bx+I_INFO_LIN]
        mov eax, [bx+I_XMS_PHYS]        ; the page directory is the first page
        mov cr3, eax
        mov eax, cr0
        or eax, 80000001h
        mov cr0, eax
        jmp dword far [jmpvec]
.back:  pop es
        pop ds
        pop di
        pop si
        pop bp
        xor ax, ax
        ret

; int pm_exit(int code): leave Windows. Called in V86 mode; the exit
; breakpoint makes the VMM switch back to real mode, which comes back
; through rm_tramp/rm_real below and returns the exit code.
_pm_exit:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        mov ax, [bp+4]
        jmp _bp_table + BP_EXIT

; The breakpoints (W386_BP_*): ARPL opcodes, invalid in V86 mode.
_bp_table:
        times BP_COUNT db 63h
        nop

; The VMM jumps here in 16-bit protected mode (a code descriptor based at
; this segment), paging on (this is identity mapped), interrupts off, data
; segment registers loaded with a 64 KB 16-bit descriptor.
_rm_tramp:
        mov eax, cr0
        and eax, 7FFFFFFFh              ; paging off
        mov cr0, eax
        xor eax, eax
        mov cr3, eax
        mov eax, cr0
        and al, 0FEh                    ; protection off
        mov cr0, eax
        jmp (seg rm_real):rm_real
rm_real:
        mov ax, DGROUP
        mov ds, ax
        mov es, ax
        lidt [rm_idtr]
        mov bx, [cur_info]
        mov ss, [bx+I_RET_SS]
        mov sp, [bx+I_RET_SP]
        mov ax, [bx+I_EXIT_CODE]
        pop es
        pop ds
        pop di
        pop si
        pop bp
        sti
        ret

segment _DATA public class=DATA use16
global _xms_entry
_xms_entry      dd 0
cur_info        dw 0
                align 8
tgdt            dq 0
                dw 0FFFFh, 0, 9A00h, 00CFh      ; 08h: code, flat, 32-bit
                dw 0FFFFh, 0, 9200h, 00CFh      ; 10h: data, flat
tgdtr           dw 3 * 8 - 1
                dd 0
rm_idtr         dw 3FFh
                dd 0
jmpvec          dd 0
                dw 0
