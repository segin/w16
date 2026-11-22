; vxs.asm - VXS.386, a probe VxD (w16's own, test only; the WIN386
; handover note, step 6): what happens around a DOS box. Loaded with
; "device=vxs.386" in SYSTEM.INI [386Enh] under Windows 3.1's WIN386 (or
; w16's), it writes to COM1 (DOSBox-X serial1=file):
;   - every control message (EAX, EBX, ECX, EDX, ESI, EDI);
;   - every call of the protected-mode APIs of SHELL (0017h), VDD (000Ah),
;     VKD (000Dh) and VMD (000Ch), hooked with Hook_Device_PM_API (VMM
;     0092h): the client's registers before and after, and 96 bytes at
;     the client's ES:DI (before and after) and DS:SI (before); for
;     SHELL's function 3 (create a VM) also at the three pointers of its
;     block (the program, its command tail, its directory);
;   - built with SVCHOOK defined (VXSH.386), the calls of a few VMM
;     services, hooked with Hook_Device_Service (VMM 0090h), with the
;     caller's address, the registers before and after and, when EBP looks
;     like a client register structure, the client's CS:EIP:
;     System_Control, Set_Execution_Focus, Suspend_VM, Resume_VM from
;     every caller; Simulate_Far_Call, Simulate_Push, Begin_Nest_Exec,
;     Resume_Exec, End_Nest_Exec, Simulate_Far_Jmp only from SHELL (within
;     3000h bytes of its API procedure); each at most 600 times. (A first
;     version that logged more of them from every caller stopped the boot:
;     too much for the serial port.)
; Lines start with "VXS ". Black box: only what the callers pass and get
; back (ARCHITECTURE.md 3). Sources: the Windows 3.1 DDK's Virtual Device
; Adaptation Guide (Declare_Virtual_Device, VMMcall, Hook_Device_PM_API,
; Hook_Device_Service, Map_Flat, Client_Reg_Struc, the control messages);
; Hook_Device_PM_API's number, 0092h, between Hook_Device_Service (0090h)
; and System_Control (0093h) in the VMM's table (docs/win386-services.md),
; checked by its carry and result under Windows 3.1.

        bits 32

COM1    equ 3F8h
NSVC    equ 10                          ; hooked services
SVCALL  equ 4                           ; logged from every caller; the rest from SHELL only
NAPI    equ 4                           ; hooked APIs
SVCMAX  equ 600

segment _TEXT public align=16 class=CODE use32 flat
global VXS_DDB
VXS_DDB:
        dd 0
        dw 030Ah
        dw 0                            ; Undefined_Device_ID
        db 1, 0
        dw 0
        db 'VXS     '
        dd 0                            ; init order 0: before the others
        dd vxs_control
        dd 0, 0, 0, 0, 0, 0, 0

vxs_control:
        pushad
        test eax, eax                   ; Sys_Critical_Init: COM1 at 115200 baud
        jnz .nb                         ; (left at the BIOS's rate the log
        mov dx, COM1 + 3                ; held Windows up for minutes)
        mov al, 80h
        out dx, al
        mov dx, COM1
        mov al, 1
        out dx, al
        inc dx
        xor al, al
        out dx, al
        mov dx, COM1 + 3
        mov al, 3
        out dx, al
        mov eax, [esp + 28]
.nb:    mov edi, eax
        mov esi, msg_ctl
        call puts
        mov eax, edi
        call puthex
        mov esi, msg_ebx
        call puts
        mov eax, [esp + 16]
        call puthex
        mov esi, msg_ecx
        call puts
        mov eax, [esp + 24]
        call puthex
        mov esi, msg_edx
        call puts
        mov eax, [esp + 20]
        call puthex
        mov esi, msg_esi
        call puts
        mov eax, [esp + 4]
        call puthex
        mov esi, msg_edi
        call puts
        mov eax, [esp]
        call puthex
        call crlf
        cmp edi, 1                      ; Device_Init: every device is there
        jne .r
        call hooks
.r:     popad
        clc
        ret

; ---- install the hooks ----
hooks:
        mov eax, 21h                    ; DOS calls in the DOS VMs
        mov esi, int21_hook
        int 20h
        dd 00010041h                    ; Hook_V86_Int_Chain
        int 20h
        dd 00010003h                    ; Get_Sys_VM_Handle
        mov [sysvm_h], ebx
        xor ebx, ebx
.a:     mov eax, [api_ids + ebx * 4]
        mov esi, [api_stubs + ebx * 4]
        int 20h
        dd 00010092h                    ; Hook_Device_PM_API
        pushfd
        mov [api_prev + ebx * 4], esi
        mov esi, msg_hpm
        call puts
        mov eax, [api_ids + ebx * 4]
        call puthex
        mov esi, msg_cf
        call puts
        pop eax
        and eax, 1
        call puthex
        mov esi, msg_esi
        call puts
        mov eax, [api_prev + ebx * 4]
        call puthex
        call crlf
        inc ebx
        cmp ebx, NAPI
        jb .a
%ifdef SVCHOOK
        xor ebx, ebx
.s:     mov eax, [svc_ids + ebx * 4]
        mov esi, [svc_stubs + ebx * 4]
        int 20h
        dd 00010090h                    ; Hook_Device_Service
        pushfd
        mov [svc_prev + ebx * 4], esi
        pop eax
        test eax, 1
        jz .sok
        mov esi, msg_hsf
        call puts
        mov eax, [svc_ids + ebx * 4]
        call puthex
        call crlf
.sok:   inc ebx
        cmp ebx, NSVC
        jb .s
%endif
        ret

; ---- INT 21h in V86 mode in a VM other than the System VM (VXSS.386,
; built with ALLVM: in every VM): AX, and for
; AH=40h (write) the text at DS:DX (CX bytes, at most 1000), for AH=4Bh
; (EXEC) and 3Bh (directory) the string at DS:DX; passed on (carry) ----
int21_hook:
%ifndef ALLVM
        cmp ebx, [sysvm_h]
        je .pass
%endif
        pushad
        mov esi, msg_i21
        call puts
        mov eax, [ebp + 1Ch]            ; Client_EAX
        call puthex
        mov esi, msg_vm
        call puts
        mov eax, ebx
        call puthex
        mov al, [ebp + 1Dh]             ; AH
        cmp al, 40h
        je .str
        cmp al, 4Bh
        je .asciz
        cmp al, 3Bh
        je .asciz
        cmp al, 0Eh
        jne .done
        mov esi, msg_edx
        call puts
        mov eax, [ebp + 14h]
        call puthex
        jmp .done
.asciz: mov ecx, 80
        jmp .dump
.str:   movzx ecx, word [ebp + 18h]     ; Client_CX
        cmp ecx, 1000
        jbe .dump
        mov ecx, 1000
.dump:  jecxz .done
        mov ax, 3C14h                   ; Client_DS : Client_EDX
        int 20h
        dd 0001001Ch                    ; Map_Flat
        cmp eax, -1
        je .done
        mov esi, eax
        mov al, ' '
        call putc
        mov al, '"'
        call putc
.c:     lodsb
        cmp byte [ebp + 1Dh], 40h
        je .pc
        test al, al
        jz .q
.pc:    cmp al, 20h
        jb .hex
        cmp al, 7Eh
        ja .hex
        call putc
        loop .c
        jmp .q
.hex:   push eax
        mov al, 5Ch
        call putc
        pop eax
        call puthex2
        loop .c
.q:     mov al, '"'
        call putc
.done:  call crlf
        popad
.pass:  stc
        ret

; ---- the PM API hooks: EBX the VM, EBP the client registers ----
%macro APISTUB 1
api_stub%1:
        push dword %1
        jmp api_common
%endmacro
APISTUB 0
APISTUB 1
APISTUB 2
APISTUB 3

api_common:
        pushad
        mov esi, msg_api
        call puts
        mov eax, [esp + 32]
        mov eax, [api_ids + eax * 4]
        call puthex
        mov esi, msg_in
        call puts
        call clientregs
        mov esi, msg_vm
        call puts
        mov eax, [esp + 16]
        call puthex
        call stamp
        call crlf
        mov esi, msg_esdi
        mov ax, 3800h                   ; Client_ES : Client_EDI
        call dump
        mov esi, msg_dssi
        mov ax, 3C04h                   ; Client_DS : Client_ESI
        call dump
        cmp dword [esp + 32], 0         ; SHELL's: also DS:CX and DS:BX
        jne .nb
        mov esi, msg_dscx
        mov ax, 3C18h
        call dump
        mov esi, msg_dsbx
        mov ax, 3C10h
        call dump
.nb:    popad
        push eax
        mov eax, [esp + 4]
        mov eax, [api_prev + eax * 4]
        mov [api_target], eax
        pop eax
        call [api_target]
        pushfd
        pushad
        mov esi, msg_api
        call puts
        mov eax, [esp + 36]
        mov eax, [api_ids + eax * 4]
        call puthex
        mov esi, msg_out
        call puts
        call clientregs
        call crlf
        mov esi, msg_esdi
        mov ax, 3800h
        call dump
        ; the VDD's Get_Mem (AX 1): 64 bytes at the last pointer of its
        ; answer (+24h), what the grabber reads there
        cmp dword [esp + 36], 1
        jne .ng
        cmp word [ebp + 1Ch], 1
        jne .ng
        mov ax, 3800h
        int 20h
        dd 0001001Ch                    ; Map_Flat
        cmp eax, -1
        je .ng
        mov edi, eax
        mov ebx, 24h
        mov esi, msg_ptr
        call puts
        mov esi, [edi + ebx]
        mov eax, esi
        call puthex
        mov al, ':'
        call putc
        mov ecx, 64
.gb:    mov al, ' '
        call putc
        lodsb
        call puthex2
        loop .gb
        call crlf
.ng:
        ; SHELL's function 3 (DX): the three pointers of its block, which
        ; SHELL has made linear (+8, +0Eh, +14h)
        cmp dword [esp + 36], 0
        jne .o
        cmp word [ebp + 14h], 3
        jne .o
        mov ax, 3800h
        int 20h
        dd 0001001Ch                    ; Map_Flat
        cmp eax, -1
        je .o
        mov edi, eax
        mov ebx, 8
.p:     mov esi, msg_ptr
        call puts
        mov esi, [edi + ebx]
        mov eax, esi
        call puthex
        mov al, ':'
        call putc
        mov ecx, 96
.pb:    mov al, ' '
        call putc
        lodsb
        call puthex2
        loop .pb
        call crlf
        add ebx, 6
        cmp ebx, 1Ah
        jb .p
.o:     popad
        popfd
        lea esp, [esp + 4]
        ret

; " ax=.. bx=.. cx=.. dx=.. si=.. di=.. ds=.. es=.. fl=.. cs:ip" of EBP's client
clientregs:
        push ebx
        xor ebx, ebx
.l:     mov esi, [cr_names + ebx * 8]
        call puts
        mov eax, [cr_names + ebx * 8 + 4]
        mov eax, [ebp + eax]
        cmp ebx, 6
        jb .f
        and eax, 0FFFFh
.f:     call puthex
        inc ebx
        cmp ebx, 9
        jb .l
        mov esi, msg_at
        call puts
        movzx eax, word [ebp + 28h]
        call puthex
        mov al, ':'
        call putc
        mov eax, [ebp + 24h]
        call puthex
        pop ebx
        ret

; dump 64 bytes at the client's AH segment : AL offset (Map_Flat), with
; the line's tag in ESI; nothing if the segment register is 0 or invalid
dump:
        push eax
        call puts
        pop eax
        movzx ecx, ah
        cmp word [ebp + ecx], 0
        je .none
        int 20h
        dd 0001001Ch                    ; Map_Flat
        cmp eax, -1
        je .none
        push eax
        call puthex
        mov al, ':'
        call putc
        pop esi
        mov ecx, 96
.b:     mov al, ' '
        call putc
        lodsb
        call puthex2
        loop .b
.none:  jmp crlf

; ---- the service hooks ----
%assign i 0
%rep NSVC
svc_stub %+ i:
        push dword i
        jmp svc_common
%assign i i+1
%endrep

svc_common:
        pushfd
        pushad
        mov eax, [esp + 36]             ; the index
        inc dword [svc_count + eax * 4]
        cmp dword [svc_count + eax * 4], SVCMAX
        ja .q1
        cmp eax, SVCALL                 ; the first SVCALL: every caller
        jb .lg
        mov ecx, [esp + 40]             ; the others: SHELL's calls only (near
        sub ecx, [api_prev]             ; its API procedure)
        add ecx, 3000h
        cmp ecx, 6000h
        jae .q1
.lg:    or dword [esp + 36], 80000000h  ; logged: the return is too
        mov esi, msg_svc
        call puts
        mov eax, [esp + 36]
        and eax, 7FFFFFFFh
        mov eax, [svc_ids + eax * 4]
        call puthex
        mov esi, msg_from
        call puts
        mov eax, [esp + 40]             ; the caller's return address
        call puthex
        call svcregs
        ; Simulate_Push of a ring-0 address (SHELL's event record for
        ; WINOA386's event procedure): its 72 bytes
        mov eax, [esp + 36]
        and eax, 7FFFFFFFh
        cmp dword [svc_ids + eax * 4], 0001004Dh
        jne .q1
        mov eax, [esp + 28]             ; EAX as it came
        cmp eax, 80000000h
        jb .q1
        cmp eax, 0C0000000h
        jae .q1
        mov esi, msg_rec
        call puts
        mov esi, eax
        mov ecx, 72
.rb:    mov al, ' '
        call putc
        lodsb
        call puthex2
        loop .rb
        call crlf
.q1:    popad
        popfd
        push eax
        mov eax, [esp + 4]
        and eax, 7FFFFFFFh
        mov eax, [svc_prev + eax * 4]
        mov [svc_target], eax
        pop eax
        call [svc_target]
        pushfd
        pushad
        mov eax, [esp + 36]
        test eax, 80000000h
        jz .q2
        mov esi, msg_svc
        call puts
        mov eax, [esp + 36]
        and eax, 7FFFFFFFh
        mov eax, [svc_ids + eax * 4]
        call puthex
        mov esi, msg_ret
        call puts
        call svcregs
.q2:    popad
        popfd
        lea esp, [esp + 4]
        ret

; the PUSHAD frame above our return address and the flags above it
svcregs:
        mov esi, msg_eax
        call puts
        mov eax, [esp + 4 + 28]
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
        mov esi, msg_esi
        call puts
        mov eax, [esp + 4 + 4]
        call puthex
        mov esi, msg_edi
        call puts
        mov eax, [esp + 4]
        call puthex
        mov esi, msg_fl
        call puts
        mov eax, [esp + 4 + 32]
        and eax, 0241h
        call puthex
        mov eax, [esp + 4 + 8]          ; EBP: the client registers?
        cmp eax, 80000000h
        jb .nc
        mov esi, msg_ccs
        call puts
        push eax
        movzx eax, word [eax + 28h]
        call puthex
        mov al, ':'
        call putc
        pop eax
        mov eax, [eax + 24h]
        call puthex
.nc:    call crlf
        mov eax, [esp + 4 + 36]         ; the index (above our return, PUSHAD, PUSHFD)
        and eax, 7FFFFFFFh
        mov eax, [svc_ids + eax * 4]
        cmp eax, 00010083h
        jne .n85
        mov esi, [esp + 4 + 20]         ; EDX
        cmp esi, 80000000h
        jb .r
        push esi
        mov esi, msg_evt
        call puts
        pop esi
        mov ecx, 32
.e:     mov al, ' '
        call putc
        lodsb
        call puthex2
        loop .e
        jmp crlf
.n85:   cmp eax, 00010085h
        jne .r
        int 20h
        dd 00010001h                    ; Get_Cur_VM_Handle
        mov ebx, [ebx + 8]              ; CB_Client_Pointer
        mov esi, msg_cip
        call puts
        movzx eax, word [ebx + 28h]
        call puthex
        mov al, ':'
        call putc
        mov eax, [ebx + 24h]
        call puthex
        mov al, ' '
        call putc
        movzx eax, word [ebx + 34h]
        call puthex
        mov al, ':'
        call putc
        mov eax, [ebx + 30h]
        call puthex
        call crlf
.r:     ret

; " t=<ms>" (VMM Get_System_Time)
stamp:  push eax
        push esi
        mov esi, msg_t
        call puts
        int 20h
        dd 0001003Fh                    ; Get_System_Time
        call puthex
        pop esi
        pop eax
        ret

; ---- serial output, polled ----
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

puthex2:
        push eax
        push eax
        shr al, 4
        and eax, 15
        mov al, [hexd + eax]
        call putc
        pop eax
        and eax, 15
        mov al, [hexd + eax]
        call putc
        pop eax
        ret

api_ids   dd 0017h, 000Ah, 000Dh, 000Ch
api_stubs dd api_stub0, api_stub1, api_stub2, api_stub3
api_prev  dd 0, 0, 0, 0
api_target dd 0
svc_ids   dd 00010093h, 00010031h, 0001002Bh, 0001002Ch
          dd 00010048h, 0001004Dh, 00010083h, 00010085h, 00010086h, 00010049h
svc_stubs:
%assign i 0
%rep NSVC
          dd svc_stub %+ i
%assign i i+1
%endrep
svc_prev  times NSVC dd 0
svc_count times NSVC dd 0
svc_target dd 0
; Client_Reg_Struc offsets (the DDK): EAX 1Ch, EBX 10h, ECX 18h, EDX 14h,
; ESI 4, EDI 0, DS 3Ch, ES 38h, EFlags 2Ch
cr_names  dd n_ax, 1Ch, n_bx, 10h, n_cx, 18h, n_dx, 14h, n_si, 4, n_di, 0
          dd n_ds, 3Ch, n_es, 38h, n_fl, 2Ch
n_ax      db ' eax=', 0
n_bx      db ' ebx=', 0
n_cx      db ' ecx=', 0
n_dx      db ' edx=', 0
n_si      db ' esi=', 0
n_di      db ' edi=', 0
n_ds      db ' ds=', 0
n_es      db ' es=', 0
n_fl      db ' fl=', 0
hexd      db '0123456789ABCDEF'
msg_ctl   db 'VXS control eax=', 0
msg_eax   db ' eax=', 0
msg_ebx   db ' ebx=', 0
msg_ecx   db ' ecx=', 0
msg_edx   db ' edx=', 0
msg_esi   db ' esi=', 0
msg_edi   db ' edi=', 0
msg_fl    db ' fl=', 0
msg_cf    db ' cf=', 0
msg_hpm   db 'VXS Hook_Device_PM_API ', 0
msg_hsf   db 'VXS Hook_Device_Service failed ', 0
msg_api   db 'VXS api ', 0
msg_in    db ' in', 0
msg_out   db ' out', 0
msg_vm    db ' vm=', 0
msg_at    db ' at=', 0
msg_esdi  db 'VXS   es:di ', 0
msg_dssi  db 'VXS   ds:si ', 0
msg_ptr   db 'VXS   ptr ', 0
msg_dscx  db 'VXS   ds:cx ', 0
msg_dsbx  db 'VXS   ds:bx ', 0
msg_svc   db 'VXS svc ', 0
msg_rec   db 'VXS   record', 0
msg_from  db ' from=', 0
msg_ret   db ' ret', 0
msg_ccs   db ' client=', 0
msg_i21   db 'VXS int21 ax=', 0
sysvm_h   dd 0
msg_evt   db 'VXS   event', 0
msg_cip   db 'VXS   resume at ', 0
msg_t     db ' t=', 0
