; ldrasm.asm - the WIN386 loader's machine-level helpers (16-bit, real mode
; and V86 mode): processor checks, XMS calls, the switch to the VMM, the
; breakpoints and the way back to real mode.
;
; Linked with the loader's C code (Open Watcom, small model, __cdecl).
; Sources: Intel 80386 Programmer's Reference Manual (protected-mode entry
; with paging, the return to real mode, V86 mode), XMS 2.0 and 3.0
; specifications.

        bits 16
        cpu 386

%include "w386.inc"

group DGROUP _DATA

segment _TEXT public class=CODE use16

global _cpu_is_386, _cpu_msw, _xms_call, _xms_call32, _pm_go, _pm_exit, _bp_table, _rm_tramp

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

; void xms_call32(struct xregs32 *r): EAX, EBX, ECX, EDX from *r, the call
; through _xms_entry, the four back into *r. For XMS 3.0's functions that
; count in 32 bits (88h, 89h, 8Eh: XMS 3.0 specification).
_xms_call32:
        push bp
        mov bp, sp
        push esi
        push edi
        push ebx
        mov di, [bp+4]
        mov eax, [di]
        mov ebx, [di+4]
        mov ecx, [di+8]
        mov edx, [di+12]
        call far [_xms_entry]
        mov di, [bp+4]
        mov [di], eax
        mov [di+4], ebx
        mov [di+8], ecx
        mov [di+12], edx
        pop ebx
        pop edi
        pop esi
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

; unsigned vm_stub_off(void): the offset of vm_stub in this segment
global _vm_stub_off
_vm_stub_off:
        mov ax, vm_stub
        ret

; A DOS VM's first code (V86 mode; ARCHITECTURE.md 12.10). The VMM starts
; a new VM here with DS = SS = P, the first paragraph of the VM's free
; memory (P - 1 its free arena's MCB), SP inside P's first 8 KB, and the
; parameters at P:100h: the program's path (ASCIIZ, 100h), the command
; tail (180h), the drive (200h, 0 = A:) and directory (201h, ASCIIZ, empty:
; none), DOS's EXEC parameter block (280h), two FCBs (300h, 310h), the
; length of a message for standard output (3FEh) and the message (400h).
; It takes those 8 KB from DOS (the first fit: P itself), makes them a PSP
; of their own, the current one (the program's parent: its saved stack
; and handles are this VM's, never the loader's, which the System VM
; uses), then does what Windows 3.1's DOS VM did before it ran the program
; (VXS.386's INT 21h hook, the WIN386 handover note): flush the keyboard
; (0C06h), set the drive and directory, write the message (the MS-DOS
; Prompt's box), parse the tail into the FCBs (2901h, twice), EXEC the
; program, and report its exit code at the VMEXIT breakpoint (carry set:
; the EXEC failed, AX DOS's error). Sources: MS-DOS 4.00 INT 21h 48h, 55h,
; 50h, 0Ch, 0Eh, 3Bh, 40h, 29h, 4B00h, 4Dh.
VS_BLOCK        equ 200h                ; paragraphs: PSP, parameters, stacks
vm_stub:
        mov ah, 48h
        mov bx, VS_BLOCK
        int 21h
        jc .fail
        mov dx, ds                      ; a PSP at P:0 (a copy of the current
        mov si, dx                      ; one: handles, environment)
        add si, VS_BLOCK
        mov ah, 55h
        int 21h
        mov ax, ds                      ; the block is its own
        dec ax
        mov es, ax
        mov ax, ds
        mov [es:1], ax
        mov bx, ds
        mov ah, 50h
        int 21h
        mov ax, 0C06h                   ; the keyboard's buffer flushed
        mov dl, 0FFh
        int 21h
        mov dl, [200h]
        mov ah, 0Eh
        int 21h
        cmp byte [201h], 0
        je .nodir
        mov dx, 201h
        mov ah, 3Bh
        int 21h
.nodir: mov cx, [3FEh]
        jcxz .nomsg
        mov dx, 400h
        mov bx, 1
        mov ah, 40h
        int 21h
.nomsg: push ds
        pop es
        mov si, 181h                    ; the tail's text into the FCBs
        mov di, 300h
        mov ax, 2901h
        int 21h
        mov di, 310h
        mov ax, 2901h
        int 21h
        mov dx, 100h
        mov bx, 280h
        mov ax, 4B00h
        int 21h
        jc .fail
        mov ax, ss
        mov ds, ax
        mov ah, 4Dh
        int 21h
        xor ah, ah
        clc
        jmp _bp_table + BP_VMEXIT
.fail:  stc
        jmp _bp_table + BP_VMEXIT

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
