; vxt.asm - VXT.386, a probe VxD (w16's own, test only): how Windows 3.1
; schedules idle and busy VMs (the VMPOLL work, ARCHITECTURE.md 12.12).
; Loaded with "device=vxt.386" in SYSTEM.INI [386Enh] under Windows 3.1's
; WIN386 (or w16's), it writes to COM1 (DOSBox-X serial1=file), at 115200
; baud, lines starting "VXT ":
;   - at Init_Complete: VMPoll_Get_Version (EAX, carry),
;     Get_Time_Slice_Granularity, and whether each hook went in;
;   - every call of VMPOLL's three services (Hook_Device_Service): the
;     registers in and out, the caller's address;
;   - the control messages for VMs other than the System VM (EAX, EBX);
;   - every 2 seconds (Set_Global_Time_Out): Get_Time_Slice_Info, then one
;     line per VM (Get_Next_VM_Handle from the System VM): its ID, the
;     control block's status, the milliseconds it ran in those 2 seconds
;     (Call_When_Task_Switched and Get_System_Time), how often it was
;     switched to, how many INT 16h AH=00h/10h, AH=01h/11h and other
;     calls, INT 2Fh AX=1680h, INT 28h and INT 21h AH=0Bh it made in V86
;     mode (Hook_V86_Int_Chain, installed at Init_Complete: called before
;     the hooks the devices installed earlier), how often the VMM's
;     Release_Time_Slice was called while it ran, Wake_Up_VM and
;     Adjust_Exec_Priority for it (Hook_Device_Service), and
;     Get_Time_Slice_Priority's EAX, ECX, EDX, ESI.
; Black box (ARCHITECTURE.md 3): only what the services return and what
; callers pass. Sources: the Windows 3.1 DDK's Virtual Device Adaptation
; Guide (the services named, Client_Reg_Struc, the control block's
; CB_VM_Status and CB_VMID), the service numbers as docs/win386-services.md
; lists them, VMPOLL's (0018h) in the DDK's order (Get_Version,
; Enable_Disable, Reset_Detection).

        bits 32

COM1    equ 3F8h
NVM     equ 16
NSVC    equ 3                           ; VMM services counted
NPOLL   equ 3                           ; VMPOLL services logged

segment _TEXT public align=16 class=CODE use32 flat
global VXT_DDB
VXT_DDB:
        dd 0
        dw 030Ah
        dw 0                            ; Undefined_Device_ID
        db 1, 0
        dw 0
        db 'VXT     '
        dd 0F0000000h                   ; after Windows 3.1's devices
        dd vxt_control
        dd 0, 0, 0, 0, 0, 0, 0

vxt_control:
        pushad
        test eax, eax                   ; Sys_Critical_Init: COM1 at 115200 baud
        jnz .nb
        mov dx, COM1 + 3
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
        mov [sysvm_h], ebx
        mov eax, [ebx + 0Ch]
        and eax, NVM - 1
        mov [cur_id], eax
        jmp .r
.nb:    cmp eax, 2                      ; Init_Complete
        jne .nic
        call setup
        jmp .r
.nic:   cmp ebx, [sysvm_h]
        je .r
        cmp eax, 7                      ; VM messages for the other VMs
        jb .r
        cmp eax, 11h
        ja .r
        mov edi, eax
        mov esi, msg_ctl
        call puts
        mov eax, edi
        call puthex
        mov esi, msg_ebx
        call puts
        mov eax, [esp + 16]
        call puthex
        mov esi, msg_id
        call puts
        mov eax, [esp + 16]
        mov eax, [eax + 0Ch]
        call puthex
        call crlf
        cmp edi, 9                      ; VM_Init: V86MMGR's view of the VM
        jne .r
        mov ebx, [esp + 16]
        call mmvm
.r:     popad
        clc
        ret

setup:
        ; V86MMGR (0006h): Get_Version, Get_Mapping_Info, the System VM's
        ; Get_Xlat_Buff_State and Get_EMS_XMS_Limits
        mov esi, msg_mmv
        call puts
        xor eax, eax
        int 20h
        dd 00060000h                    ; V86MMGR_Get_Version
        pushfd
        call puthex
        mov esi, msg_cf
        call puts
        pop eax
        and eax, 1
        call puthex
        call crlf
        mov esi, msg_mmm
        call puts
        xor ecx, ecx
        int 20h
        dd 00060005h                    ; V86MMGR_Get_Mapping_Info
        mov eax, ecx
        call puthex
        call crlf
        mov esi, msg_mmx
        call puts
        mov ebx, [sysvm_h]
        int 20h
        dd 0006000Ah                    ; V86MMGR_Get_Xlat_Buff_State
        push edx
        push ecx
        call puthex
        mov al, ' '
        call putc
        pop eax
        call puthex
        mov al, ' '
        call putc
        pop eax
        call puthex
        call crlf
        mov ebx, [sysvm_h]
        call mmvm
        mov esi, msg_ver
        call puts
        xor eax, eax
        int 20h
        dd 00180000h                    ; VMPoll_Get_Version
        pushfd
        call puthex
        mov esi, msg_cf
        call puts
        pop eax
        and eax, 1
        call puthex
        call crlf
        mov esi, msg_gran
        call puts
        int 20h
        dd 00010034h                    ; Get_Time_Slice_Granularity
        call puthex
        call crlf
        ; V86 interrupt hooks
        mov eax, 16h
        mov esi, h16
        call hookint
        mov eax, 2Fh
        mov esi, h2f
        call hookint
        mov eax, 28h
        mov esi, h28
        call hookint
        mov eax, 21h
        mov esi, h21
        call hookint
        ; service hooks
        xor ebx, ebx
.s:     mov eax, [svc_ids + ebx * 4]
        mov esi, [svc_stubs + ebx * 4]
        int 20h
        dd 00010090h                    ; Hook_Device_Service
        mov [svc_prev + ebx * 4], esi
        pushfd
        mov esi, msg_hook
        call puts
        mov eax, [svc_ids + ebx * 4]
        call puthex
        mov esi, msg_cf
        call puts
        pop eax
        and eax, 1
        call puthex
        call crlf
        inc ebx
        cmp ebx, NSVC + NPOLL
        jb .s
        ; the task-switch callback and the clock
        mov esi, tswitch
        int 20h
        dd 0001002Ah                    ; Call_When_Task_Switched
        int 20h
        dd 0001003Fh                    ; Get_System_Time
        mov [last_t], eax
        mov [period_t], eax
        call arm
        ret

hookint:
        push eax
        int 20h
        dd 00010041h                    ; Hook_V86_Int_Chain
        pushfd
        mov esi, msg_int
        call puts
        mov eax, [esp + 4]
        call puthex
        mov esi, msg_cf
        call puts
        pop eax
        and eax, 1
        call puthex
        call crlf
        pop eax
        ret

arm:    mov eax, 2000
        mov esi, report
        xor edx, edx
        int 20h
        dd 0001003Ch                    ; Set_Global_Time_Out
        ret

; ---- the V86 interrupt hooks: EBX the VM, EBP the client's registers;
; passed on (carry) ----
%macro VMIDX 0
        mov eax, [ebx + 0Ch]
        and eax, NVM - 1
%endmacro

h16:    push eax
        push edx
        VMIDX
        mov dl, [ebp + 1Dh]             ; Client_AH
        and dl, 0EFh                    ; 10h/11h as 00h/01h
        cmp dl, 0
        jne .n0
        inc dword [kb0 + eax * 4]
        jmp .x
.n0:    cmp dl, 1
        jne .no
        inc dword [kb1 + eax * 4]
        jmp .x
.no:    inc dword [kbo + eax * 4]
.x:     pop edx
        pop eax
        stc
        ret

h2f:    cmp word [ebp + 1Ch], 1680h
        jne .x
        push eax
        VMIDX
        inc dword [p80 + eax * 4]
        pop eax
.x:     stc
        ret

h28:    push eax
        VMIDX
        inc dword [i28 + eax * 4]
        pop eax
        stc
        ret

h21:    cmp byte [ebp + 1Dh], 0Bh
        jne .x
        push eax
        VMIDX
        inc dword [d0b + eax * 4]
        pop eax
.x:     stc
        ret

; ---- the task-switch callback: EAX the old VM, EBX the new one ----
tswitch:
        pushad
        mov edi, [ebx + 0Ch]
        and edi, NVM - 1
        int 20h
        dd 0001003Fh                    ; Get_System_Time
        mov edx, eax
        sub edx, [last_t]
        mov [last_t], eax
        mov ecx, [cur_id]
        add [run + ecx * 4], edx
        mov [cur_id], edi
        inc dword [sw + edi * 4]
        popad
        ret

; ---- the counted VMM services: Release_Time_Slice for the VM running,
; Wake_Up_VM and Adjust_Exec_Priority for EBX's ----
%assign i 0
%rep NSVC + NPOLL
svc_stub %+ i:
        push dword i
        jmp svc_common
%assign i i+1
%endrep

svc_common:
        pushfd
        pushad
        mov ecx, [esp + 36]             ; the index
        cmp ecx, NSVC
        jae .poll
        mov eax, [cur_id]
        cmp ecx, 0
        je .c
        mov eax, ebx
        cmp eax, 80000000h              ; a VM handle?
        jb .q
        mov eax, [eax + 0Ch]
        and eax, NVM - 1
.c:     imul edx, ecx, NVM
        add edx, eax
        inc dword [svc_count + edx * 4]
        ; Release_Time_Slice and Wake_Up_VM for a DOS VM: the first 200
        ; logged with the time, the caller and the VM's AH=01h polls so far
        cmp ecx, 2
        jae .q
        cmp eax, 1
        je .q
        cmp dword [nlog], 200
        jae .q
        inc dword [nlog]
        mov edi, eax
        mov esi, msg_rts1
        cmp ecx, 0
        je .l1
        mov esi, msg_wk1
.l1:    call puts
        mov eax, edi
        call puthex
        mov esi, msg_t
        call puts
        int 20h
        dd 0001003Fh                    ; Get_System_Time
        call puthex
        mov esi, msg_from
        call puts
        mov eax, [esp + 40]
        call puthex
        mov esi, msg_polls
        call puts
        mov eax, [kb1 + edi * 4]
        call puthex
        call crlf
.q:     popad
        popfd
        xchg eax, [esp]                 ; the index for EAX
        mov eax, [svc_prev + eax * 4]
        xchg eax, [esp]                 ; the previous procedure for the index
        ret
        ; VMPOLL's: logged with the registers before and after
.poll:  mov esi, msg_poll
        call puts
        mov eax, ecx
        sub eax, NSVC
        call puthex
        mov esi, msg_from
        call puts
        mov eax, [esp + 40]
        call puthex
        call regs
        call crlf
        popad
        popfd
        xchg eax, [esp]
        mov eax, [svc_prev + eax * 4]
        xchg eax, [esp]
        call [esp]
        pushfd
        pushad
        mov esi, msg_pout
        call puts
        call regs
        mov esi, msg_cf
        call puts
        mov eax, [esp + 32]
        and eax, 1
        call puthex
        call crlf
        popad
        popfd
        lea esp, [esp + 4]
        ret

; EAX EBX ECX EDX ESI of the PUSHAD above the caller's return
regs:   mov esi, msg_eax
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
        ret

; ---- every 2 seconds ----
report:
        pushad
        int 20h
        dd 0001003Fh                    ; Get_System_Time
        mov edx, eax
        sub edx, [last_t]
        mov [last_t], eax
        mov ecx, [cur_id]
        add [run + ecx * 4], edx
        mov esi, msg_rep
        call puts
        sub eax, [period_t]
        call puthex
        add eax, [period_t]
        mov [period_t], eax
        int 20h
        dd 00010036h                    ; Get_Time_Slice_Info
        push ecx
        mov esi, msg_sched
        call puts
        call puthex
        mov esi, msg_idle
        call puts
        pop eax
        call puthex
        call crlf
        mov ebx, [sysvm_h]
.vm:    push ebx
        mov edi, [ebx + 0Ch]
        and edi, NVM - 1
        mov esi, msg_vm
        call puts
        mov eax, [ebx + 0Ch]
        call puthex
        mov esi, msg_st
        call puts
        mov eax, [ebx]
        call puthex
        mov esi, msg_run
        call puts
        xor eax, eax
        xchg eax, [run + edi * 4]
        call puthex
        mov esi, msg_sw
        call puts
        xor eax, eax
        xchg eax, [sw + edi * 4]
        call puthex
        mov esi, msg_k
        call puts
        xor eax, eax
        xchg eax, [kb0 + edi * 4]
        call puthex
        mov al, '/'
        call putc
        xor eax, eax
        xchg eax, [kb1 + edi * 4]
        call puthex
        mov al, '/'
        call putc
        xor eax, eax
        xchg eax, [kbo + edi * 4]
        call puthex
        mov esi, msg_p80
        call puts
        xor eax, eax
        xchg eax, [p80 + edi * 4]
        call puthex
        mov esi, msg_i28
        call puts
        xor eax, eax
        xchg eax, [i28 + edi * 4]
        call puthex
        mov esi, msg_d0b
        call puts
        xor eax, eax
        xchg eax, [d0b + edi * 4]
        call puthex
        mov esi, msg_rts
        call puts
        xor eax, eax
        xchg eax, [svc_count + edi * 4]
        call puthex
        mov esi, msg_wk
        call puts
        xor eax, eax
        xchg eax, [svc_count + (NVM + 0) * 4 + edi * 4]
        call puthex
        mov esi, msg_aep
        call puts
        xor eax, eax
        xchg eax, [svc_count + (NVM * 2) * 4 + edi * 4]
        call puthex
        mov esi, msg_tsp
        call puts
        mov ebx, [esp]
        int 20h
        dd 00010032h                    ; Get_Time_Slice_Priority
        call puthex
        mov al, ' '
        call putc
        mov eax, ecx
        call puthex
        mov al, ' '
        call putc
        mov eax, edx
        call puthex
        mov al, ' '
        call putc
        mov eax, esi
        call puthex
        call crlf
        pop ebx
        int 20h
        dd 0001003Bh                    ; Get_Next_VM_Handle
        cmp ebx, [sysvm_h]
        jne .vm
        call arm
        popad
        ret

; V86MMGR_Get_EMS_XMS_Limits for EBX's VM (EAX EDX ESI EDI ECX), then
; V86MMGR_GetPgStatus for its pages A0h-10Fh, where nonzero
mmvm:   pushad
        mov esi, msg_mml
        call puts
        mov eax, [ebx + 0Ch]
        call puthex
        int 20h
        dd 00060003h                    ; V86MMGR_Get_EMS_XMS_Limits
        push ecx
        push edi
        push esi
        push edx
        call puthex
        mov ecx, 4
.l:     mov al, ' '
        call putc
        pop eax
        call puthex
        loop .l
        call crlf
        mov ebx, [esp + 16]
        mov ecx, 0A0h
.p:     push ecx
        push ebx
        int 20h
        dd 00060010h                    ; V86MMGR_GetPgStatus
        pop ebx
        pop ecx
        test eax, eax
        jz .n
        push eax
        mov esi, msg_mmp
        call puts
        mov eax, ecx
        call puthex
        mov al, '='
        call putc
        pop eax
        call puthex
        call crlf
.n:     inc ecx
        cmp ecx, 110h
        jbe .p
        popad
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

puts:   push eax
.l:     lodsb
        test al, al
        jz .e
        call putc
        jmp .l
.e:     pop eax
        ret

crlf:   push eax
        mov al, 13
        call putc
        mov al, 10
        call putc
        pop eax
        ret

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

sysvm_h   dd 0
cur_id    dd 1
last_t    dd 0
period_t  dd 0
run       times NVM dd 0
sw        times NVM dd 0
kb0       times NVM dd 0
kb1       times NVM dd 0
kbo       times NVM dd 0
p80       times NVM dd 0
i28       times NVM dd 0
d0b       times NVM dd 0
svc_ids   dd 00010038h, 00010039h, 0001001Eh
          dd 00180000h, 00180001h, 00180002h
svc_stubs:
%assign i 0
%rep NSVC + NPOLL
          dd svc_stub %+ i
%assign i i+1
%endrep
svc_prev  times NSVC + NPOLL dd 0
svc_count times NSVC * NVM dd 0
msg_ctl   db 'VXT ctl ', 0
msg_mmv   db 'VXT V86MMGR_Get_Version eax=', 0
msg_mmm   db 'VXT V86MMGR_Get_Mapping_Info ecx=', 0
msg_mmx   db 'VXT V86MMGR_Get_Xlat_Buff_State sys eax ecx edx=', 0
msg_mml   db 'VXT V86MMGR limits vm=', 0
msg_mmp   db 'VXT V86MMGR page ', 0
msg_rts1  db 'VXT rts vm=', 0
msg_wk1   db 'VXT wake vm=', 0
msg_t     db ' t=', 0
msg_polls db ' polls=', 0
nlog      dd 0
msg_ebx   db ' ebx=', 0
msg_eax   db ' eax=', 0
msg_ecx   db ' ecx=', 0
msg_edx   db ' edx=', 0
msg_id    db ' id=', 0
msg_cf    db ' cf=', 0
msg_ver   db 'VXT VMPoll_Get_Version eax=', 0
msg_gran  db 'VXT granularity ', 0
msg_int   db 'VXT hook int ', 0
msg_hook  db 'VXT hook svc ', 0
msg_poll  db 'VXT VMPOLL ', 0
msg_from  db ' from=', 0
msg_pout  db 'VXT VMPOLL out', 0
msg_rep   db 'VXT t+', 0
msg_sched db ' scheduled=', 0
msg_idle  db ' idle=', 0
msg_vm    db 'VXT vm=', 0
msg_st    db ' st=', 0
msg_run   db ' run=', 0
msg_sw    db ' sw=', 0
msg_k     db ' k0/1/o=', 0
msg_p80   db ' 1680=', 0
msg_i28   db ' i28=', 0
msg_d0b   db ' d0b=', 0
msg_rts   db ' rts=', 0
msg_wk    db ' wake=', 0
msg_aep   db ' aep=', 0
msg_tsp   db ' tsp=', 0
hexd      db '0123456789ABCDEF'
