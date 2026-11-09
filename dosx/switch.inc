; switch.inc - mode switches and the transitions between real mode,
; ring 0 and ring 3.
;
; Real mode to protected mode: LMSW with PE set (80286 and later).
; Protected mode to real mode: on a 386 or later, clear PE in CR0 (Intel
; 80386 Programmer's Reference, "Switching back to real-address mode");
; on an 80286, which cannot leave protected mode, reset the processor
; through the keyboard controller with CMOS shutdown code 0Ah, so the
; BIOS resumes at the address in 0040:0067 without reinitialising
; anything (IBM PC AT Technical Reference, shutdown byte 0Fh).
;
; The host stack (hs_stack) serves ring 0 and real mode alike: the
; segment registers change, the memory and SP do not. hs_top is where
; the next nested use starts.

; to_pm: real mode -> ring 0. IF = 0, DS = image. Near call; returns at
; ring 0 with DS = ES = SS = G_HD0, SP unchanged. AX changed.
to_pm:
        lgdt [gdtr]
        lidt [idtr]
        smsw ax
        or al, 1
        lmsw ax
        jmp G_HC0:.pm
.pm:    mov ax, G_HD0
        mov ss, ax
        mov ds, ax
        mov es, ax
        push word 3002h         ; IOPL 3 (ring 3 gets these flags back from
                                ; the gate), no NT (IRET would switch tasks), IF, DF
        popf
        mov ax, G_LDT
        lldt ax
        and byte [gdt + G_TSS + 5], 0FDh        ; not busy
        mov ax, G_TSS
        ltr ax
        ret

; to_rm: ring 0 -> real mode. IF = 0, DS = G_HD0. Near call; returns in
; real mode with DS = ES = SS = the image, SP unchanged. AX changed.
to_rm:
        cmp byte [is386], 0
        je to_rm_286
        cpu 386
        push eax
        mov ax, G_HD0
        mov es, ax
        mov fs, ax
        mov gs, ax
        lidt [rmidtr]
        mov eax, cr0
        and al, 0FEh
        mov cr0, eax
        cpu 286
        db 0EAh                 ; jmp far image:.rm
        dw .rm
.seg:   dw 0
.rm:    mov ax, cs
        mov ds, ax
        mov es, ax
        mov ss, ax
        cpu 386
        pop eax
        cpu 286
        ret

; the 80286 way: reset with shutdown code 0Ah
to_rm_286:
        mov [r286_sp], sp
        mov ax, G_B40
        mov es, ax
        mov word [es:67h], to_rm_286.resume
        mov ax, [imgseg]
        mov [es:69h], ax
        mov al, 8Fh             ; CMOS register 0Fh, NMI off
        out 70h, al
        mov al, 0Ah
        out 71h, al
.kw:    in al, 64h
        test al, 2
        jnz .kw
        mov al, 0FEh            ; pulse the reset line
        out 64h, al
.halt:  hlt
        jmp .halt
.resume:
        cli
        mov ax, cs
        mov ss, ax
        mov sp, [cs:r286_sp]
        mov ds, ax
        mov es, ax
        mov al, 8Fh
        out 70h, al
        xor al, al
        out 71h, al
        mov al, 0Dh             ; NMI on again
        out 70h, al
        in al, 71h
        ret

; ---------------------------------------------------------------------------
; r0_rmcall: run real-mode code from ring 0. IF = 0, DS = SS = G_HD0.
;   BX = an R_SIZE structure in host memory (offset from the image)
;   AL = 0: INT AH (through the real-mode vector)
;        1: far call to CS:IP, returning with RETF
;        2: far call to CS:IP with an IRET frame
;   CX = words to copy to the real-mode stack from ES:SI
; The real-mode code starts with the structure's registers and flags (IF
; and TF clear for 0 and 2) on its SS:SP, or on the host stack if SS:SP
; is 0. Afterwards the structure holds the general registers, the flags,
; DS and ES it returned with; SS:SP and CS:IP are left as they were.
; The structure's reserved dword is used meanwhile.
r0_rmcall:
        push bp
        push si
        push di
        push dx
        push es
        push word [rmc_cur]
        push word [hs_top]
        mov [rmc_cur], bx
        mov dx, [bx+R_SP]
        mov di, [bx+R_SS]
        mov bp, sp
        sub bp, 20h
        push ax
        mov ax, di
        or ax, dx
        jnz .given
        mov di, [imgseg]
        mov dx, bp
        sub bp, RM_RESERVE
.given: and bp, 0FFFEh
        mov [hs_top], bp
        jcxz .nocopy
        ; copy CX words from ES:SI to DI:DX - 2*CX
        mov ax, cx
        shl ax, 1
        sub dx, ax
        push cx
        push di
        mov ax, di
        mov di, G_SCR0
        call gdt_base_seg
        pop di
        pop cx
        push ds
        push es
        push di
        push es
        pop ds
        mov ax, G_SCR0
        mov es, ax
        mov di, dx
        cld
        rep movsw
        pop di
        pop es
        pop ds
.nocopy:
        pop ax
        push ax                 ; [sp+4] mode, vector
        push di                 ; [sp+2] real-mode SS
        push dx                 ; [sp]   real-mode SP
        mov [bx+R_RES], sp
        call to_rm
        ; ---- real mode: DS = SS = image
        mov bp, sp
        mov si, [rmc_cur]
        mov dx, [bp]
        mov cx, [bp+2]
        mov ax, [bp+4]
        mov ss, cx
        mov sp, dx
        cmp al, 1
        je .nofl
        push word [si+R_FL]
.nofl:  push cs
        push word rm_ret
        cmp al, 0
        jne .proc
        mov bl, ah
        xor bh, bh
        shl bx, 2
        xor cx, cx
        mov es, cx
        push word [es:bx+2]
        push word [es:bx]
        jmp .tgt
.proc:  push word [si+R_CS]
        push word [si+R_IP]
.tgt:   mov dx, [si+R_FL]
        cmp al, 1
        je .fl
        and dx, ~(FL_IF | FL_TF)
        cmp al, 2
        jne .fl
        mov dx, 3002h           ; 0302h: the procedure starts with these
                                ; flags whatever the structure says (DX2
                                ; rm302_given_ed7_seen)
.fl:    push dx
        push word [si+R_ES]
        push word [si+R_ESI]
        mov ax, [si+R_EAX]
        mov bx, [si+R_EBX]
        mov cx, [si+R_ECX]
        mov dx, [si+R_EDX]
        mov di, [si+R_EDI]
        mov bp, [si+R_EBP]
        mov ds, [si+R_DS]
        pop si
        pop es
        popf
        retf

; rm_ret: where the real-mode code returns to (real mode)
rm_ret: pushf
        push ds
        push si
        mov si, cs
        mov ds, si
        mov si, [rmc_cur]
        pop word [si+R_ESI]
        pop word [si+R_DS]
        pop word [si+R_FL]
        mov [si+R_EAX], ax
        mov [si+R_EBX], bx
        mov [si+R_ECX], cx
        mov [si+R_EDX], dx
        mov [si+R_EDI], di
        mov [si+R_EBP], bp
        mov [si+R_ES], es
        cli
        cld
        mov ax, cs
        mov ss, ax
        mov sp, [si+R_RES]
        add sp, 6
        call to_pm
        mov bx, [rmc_cur]
        pop word [hs_top]
        pop word [rmc_cur]
        pop es
        pop dx
        pop di
        pop si
        pop bp
        ret

; gdt_base_seg (ring 0): GDT descriptor DI gets base AX * 16, limit FFFFh
gdt_base_seg:
        push ax
        push dx
        mov dx, ax
        shl ax, 4
        shr dx, 12
        mov [gdt+di+2], ax
        mov [gdt+di+4], dl
        mov byte [gdt+di+7], 0
        mov word [gdt+di], 0FFFFh
        mov byte [gdt+di+6], 0
        pop dx
        pop ax
        ret

; gdt_base_lin (ring 0): GDT descriptor DI gets base DX:AX, limit CX
gdt_base_lin:
        mov [gdt+di], cx
        mov [gdt+di+2], ax
        mov [gdt+di+4], dl
        mov byte [gdt+di+6], 0
        mov [gdt+di+7], dh
        cmp byte [is386], 0
        jne .d
        mov byte [gdt+di+7], 0
.d:     ret

; ---------------------------------------------------------------------------
; Real mode -> protected mode for the client: real-mode callbacks
; (INT 31h 0303h) and real-mode interrupts the client handles in
; protected mode.
;
; Callback c = 16 i + e is entry e of cb_entries (16 entries, one per
; paragraph, each "call cb_c0") reached as (cb_seg + e - i):(cb_off +
; 16 i): the offset tells i, then the segment tells e. Every one of
; those addresses is DOSX's own code, so the number of callbacks is
; bounded only by how many segment:offset pairs reach these 16 entries:
; 16 x min(4096, cb_seg + 1) (the offset must stay below 10000h, the
; segment at or above 0), 36000 or so where DOSX is loaded.
        align 16
cb_entries:
%rep CB_ENTRIES
        call cb_c0
        align 16
%endrep
cb_c0:  push cs
        db 0EAh                 ; jmp far image:cb_common
        dw cb_common
.seg:   dw 0

cb_common:
        pushf
        cli
        mov [cs:x_ax], ax
        pop word [cs:x_fl]
        pop word [cs:x_cs]      ; the segment it was called with
        pop ax                  ; the return offset: cb_off + 16 i + 3
        sub ax, [cs:cb_off]
        sub ax, 3
        shr ax, 4
        mov [cs:x_i], ax
        add ax, [cs:x_cs]       ; e = CS - cb_seg + i
        sub ax, [cs:cb_seg]
        push ax
        mov ax, [cs:x_i]
        shl ax, 4
        mov [cs:x_idx], ax
        pop ax
        add [cs:x_idx], ax
        mov byte [cs:x_kind], 0
        jmp rmx_common

; rmhook_tab: 256 entries "call rmhook_common"; IVT[n] points at entry n
; while the client handles real-mode interrupt n in protected mode
rmhook_common:
        pushf
        cli
        mov [cs:x_ax], ax
        pop word [cs:x_fl]
        pop ax                  ; return address: the entry after n's
        sub ax, rmhook_tab + 3
        push dx
        mov dl, 3
        div dl
        pop dx
        xor ah, ah
        mov [cs:x_idx], ax
        mov byte [cs:x_kind], 1
        ; fall through

; rmx_common: save the real-mode state in a structure on the host stack,
; switch to ring 0 and continue in r0_rmx
rmx_common:
        mov [cs:x_ss], ss
        mov [cs:x_sp], sp
        mov ax, cs
        mov ss, ax
        mov sp, [cs:hs_top]
        sub sp, R_SIZE + 8
        push bp
        mov bp, sp
        add bp, 2
        mov [bp+R_EBX], bx
        mov [bp+R_ECX], cx
        mov [bp+R_EDX], dx
        mov [bp+R_ESI], si
        mov [bp+R_EDI], di
        pop word [bp+R_EBP]
        mov [bp+R_DS], ds
        mov [bp+R_ES], es
        mov ax, cs
        mov ds, ax
        mov ax, [x_ax]
        mov [bp+R_EAX], ax
        mov ax, [x_fl]
        mov [bp+R_FL], ax
        mov ax, [x_ss]
        mov [bp+R_SS], ax
        mov ax, [x_sp]
        mov [bp+R_SP], ax
        xor ax, ax
        mov [bp+R_EAX+2], ax
        mov [bp+R_EBX+2], ax
        mov [bp+R_ECX+2], ax
        mov [bp+R_EDX+2], ax
        mov [bp+R_ESI+2], ax
        mov [bp+R_EDI+2], ax
        mov [bp+R_EBP+2], ax
        mov [bp+R_RES], ax
        mov [bp+R_RES+2], ax
        mov [bp+R_FS], ax
        mov [bp+R_GS], ax
        mov [bp+R_IP], ax
        mov [bp+R_CS], ax
        mov al, [x_kind]
        mov [bp+R_SIZE], ax     ; kind
        mov ax, [x_idx]
        mov [bp+R_SIZE+2], ax   ; index
        mov ax, [hs_top]
        mov [bp+R_SIZE+4], ax   ; to restore
        lea ax, [bp-10h]
        mov [hs_top], ax
        mov sp, bp
        call to_pm
        jmp r0_rmx

; rmx_resume (real mode, from r0_rmx): SP = the structure. Callbacks
; continue at the structure's CS:IP with its SS:SP and flags; interrupt
; hooks return with IRET on the original stack.
rmx_resume:
        mov si, sp              ; DS = image
        mov ax, [si+R_SIZE+4]
        mov [hs_top], ax
        mov ss, [si+R_SS]
        mov sp, [si+R_SP]
        cmp byte [si+R_SIZE], 0
        jne .hook
        push word [si+R_FL]
        push word [si+R_CS]
        push word [si+R_IP]
.hook:  push word [si+R_DS]
        push word [si+R_ESI]
        mov ax, [si+R_EAX]
        mov bx, [si+R_EBX]
        mov cx, [si+R_ECX]
        mov dx, [si+R_EDX]
        mov di, [si+R_EDI]
        mov bp, [si+R_EBP]
        mov es, [si+R_ES]
        pop si
        pop ds
        iret

; ---------------------------------------------------------------------------
; r0_call3: run ring-3 code from ring 0 and come back when it enters the
; G_GATER gate (ret3_stub). BX = a context (C_*) in host memory: the
; registers, CS:IP, SS:SP and flags to start with; afterwards it holds
; the general registers, DS and ES that ring 3 had at the gate.
; IF = 0, DS = G_HD0.
r0_call3:
        pusha
        push ds
        push es
        push word [tss+2]
        push bx
        mov [tss+2], sp
        push word [bx+C_SS]
        push word [bx+C_SP]
        mov ax, [bx+C_FL]
        or ax, FL_IOPL
        and ax, ~FL_NT
        push ax
        push word [bx+C_CS]
        push word [bx+C_IP]
        mov ax, [bx+C_AX]
        mov cx, [bx+C_CX]
        mov dx, [bx+C_DX]
        mov si, [bx+C_SI]
        mov di, [bx+C_DI]
        mov bp, [bx+C_BP]
        mov es, [bx+C_ES]
        push word [bx+C_DS]
        mov bx, [bx+C_BX]
        pop ds
        iret

; ret3_entry: ring 3 entered G_GATER: SP = the TSS's SP0 - 8
ret3_entry:
        cli
        add sp, 8               ; [sp] = context, [sp+2] = old SP0
        push bx
        push bp
        mov bp, sp
        mov bx, [ss:bp+4]
        mov [ss:bx+C_AX], ax
        mov [ss:bx+C_CX], cx
        mov [ss:bx+C_DX], dx
        mov [ss:bx+C_SI], si
        mov [ss:bx+C_DI], di
        pop word [ss:bx+C_BP]
        pop word [ss:bx+C_BX]
        mov [ss:bx+C_DS], ds
        mov [ss:bx+C_ES], es
        mov ax, G_HD0
        mov ds, ax
        pop bx
        pop word [tss+2]
        pop es
        pop ds
        popa
        ret
