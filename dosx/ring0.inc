; ring0.inc - ring-0 entry points: interrupts 00h-1Fh, the call gate,
; starting and ending the client, real-mode callbacks and hooks.

; push FS and GS on a 386, two unused words on a 286
%macro PUSH_FSGS 0
        cmp byte [cs:is386], 0
        je %%n
        cpu 386
        push fs
        push gs
        cpu 286
        jmp %%d
%%n:    sub sp, 4
%%d:
%endmacro
; POP_FSGS: FS and GS back through fsgs_restore (a freed selector in them
; does not end Windows; flags and every other register are kept)
%macro POP_FSGS 0
        cmp byte [cs:is386], 0
        je %%n
        call fsgs_restore
%%n:    add sp, 4
%endmacro


; ---------------------------------------------------------------------------
; IDT vectors 00h-1Fh: excstub_tab (32 x "call exc_entry", ring 0)
;
; Vectors 08h-0Fh are IRQ 0-7 as well as exceptions (Microsoft's DOSX
; leaves the PIC at 08h/70h: INT 31h 0400h DX = 0870h), and every vector
; can be a software INT from ring 3 (the gates have DPL 3). Whether the
; processor pushed an error code shows in the frame's depth: from ring 3
; it switched to the TSS's ring-0 stack and pushed SS, SP, FLAGS, CS and
; IP, and an error code for exceptions 08h and 0Ah-0Eh only (Intel 80286
; and 80386 Programmer's Reference, interrupts and exceptions); an IRQ or
; a software INT never pushes one. Without an error code it is an IRQ if
; its bit is set in the master PIC's in-service register, a software INT
; if CD nn is before the return address (DX5 F00E: INT 0Dh reaches the
; protected-mode handler under Microsoft's DOSX, a general protection
; fault does not, F00F; DX9 0C: INT 09h does too), else an exception.
; (w16 used to guess the error code from the bytes before the return
; address: a fault whose IP happened to be a code selector's value read
; through that selector at ring 0, and ended Windows when it was not
; present (DX9 0B), and one whose bytes there were CD 0D went to the
; interrupt handler with the error code still on the stack (DX9 0A);
; Windows 3.1 gives both to the exception handler.) Vectors 10h-1Fh are
; software interrupts. IRQs and software interrupts go to the
; protected-mode interrupt handler (INT 31h 0205h), exceptions to the
; exception handler (0203h).
exc_entry:
        PUSH_FSGS
        push ds
        push es
        pusha
        mov bp, sp
        mov ax, G_HD0
        mov ds, ax
        mov es, ax
        cld
        mov ax, [bp+E_ERR]      ; the stub's return address
        sub ax, excstub_tab + 3
        mov dl, 3
        div dl
        xor ah, ah
        mov bx, ax              ; BX = vector
        lea ax, [bp+E_ERR+14]
        cmp ax, [tss+2]
        je .drop                ; from ring 3, with an error code
        lea ax, [bp+E_ERR+12]
        cmp ax, [tss+2]
        jne .ring0
        ; from ring 3 without an error code
        cmp bx, 10h
        jae .soft
        cmp bx, 8
        jb .noerr
        ; IRQ 0-7?
        mov cl, bl
        sub cl, 8
        mov al, 0Bh
        out 20h, al
        in al, 20h
        mov ah, al
        mov al, 0Ah
        out 20h, al
        shr ah, cl
        test ah, 1
        jnz .soft
        call is_softint
        jnc .soft
        jmp .noerr
.ring0: ; DOSX's own code (no stack switch): an error code for 08h and
        ; 0Ah-0Eh
        cmp bx, 8
        jb .noerr
        cmp bx, 9
        je .noerr
        cmp bx, 0Eh
        ja .noerr
.drop:  ; drop the error code: move the saved registers and the stub's
        ; return address up by one word, the error code into the slot
        mov dx, [bp+E_ERR+2]
        mov si, bp
        add si, E_ERR
        mov cx, (E_ERR + 2) / 2
.mv:    mov ax, [ss:si]
        mov [ss:si+2], ax
        sub si, 2
        loop .mv
        add bp, 2
        mov sp, bp
        mov [bp+E_ERR], dx
        jmp .exc
.noerr: mov word [bp+E_ERR], 0
.exc:
%ifdef TRACE
        call trc_exc
%endif
        test byte [bp+E_CS], 3
        jz near host_fault
        cmp bx, 0Dh
        jne .dlv
        cmp word [bp+E_ERR], G_HC0B
        jne .dlv
        call dpmi_ret           ; an exception handler's return?
.dlv:   jmp exc_deliver
.soft:
%ifdef TRACE
        mov word [bp+E_ERR], 0FFFFh
        call trc_exc
%endif
        test byte [bp+E_CS], 3
        jz near host_fault
        ; deliver as an interrupt to pmvec[n] on the interrupted stack; a
        ; stack the frame does not fit on gets a stack fault instead
        mov ax, [bp+E_SS]
        mov si, [bp+E_SP]
        sub si, 6
        mov cx, 6
        mov dl, 1
        call uchk
        jnc .fits
        mov bx, 0Ch
        mov word [bp+E_ERR], 0
        jmp exc_deliver
.fits:  les di, [bp+E_SP]
        sub di, 6
        mov ax, [bp+E_IP]
        mov [es:di], ax
        mov ax, [bp+E_CS]
        mov [es:di+2], ax
        mov ax, [bp+E_FL]
        mov [es:di+4], ax
        mov [bp+E_SP], di
        shl bx, 2
        mov ax, [pmvec+bx]
        mov [bp+E_IP], ax
        mov ax, [pmvec+bx+2]
        mov [bp+E_CS], ax
        and word [bp+E_FL], ~(FL_IF | FL_TF)
exc_exit:
        call dses_fix
        popa
        pop es
        pop ds
        POP_FSGS
        add sp, 2
        iret

%ifdef TRACE
trc_exc:
        push ax
        mov ax, [bp+E_IP]
        mov [trc_x], ax
        mov ax, [bp+E_CS]
        mov [trc_x+2], ax
        mov ax, [bp+E_ERR]
        mov [trc_x+4], ax
        mov ax, [bp+E_FL]
        mov [trc_x+6], ax
        mov ax, bx
        or ah, 1
        call trc
        pop ax
        ret
%endif

; is_softint: BX = vector; the frame has no error code: is the instruction
; before CS:IP "INT BL"? CF clear if so.
is_softint:
        mov ax, [bp+E_CS]
        mov si, [bp+E_IP]
        cmp si, 2
        jb .no
        sub si, 2
        mov cx, 2
        xor dl, dl
        call uchk               ; (a code segment readable and present)
        jc .no
        push ds
        mov ds, ax
        cmp byte [si], 0CDh
        jne .no1
        cmp [si+1], bl
        jne .no1
        pop ds
        clc
        ret
.no1:   pop ds
.no:    stc
        ret

; dses_fix: the DS and ES saved in the frame at BP become 0 when the
; processor would not load them any more (a selector freed or made not
; present while the client had it there). Microsoft's DOSX gives 0 in the
; register that held a selector INT 31h 0001h freed (DX9 01, 02); after
; 0502h, 0009h, 000Ch or INT 21h 49h it ends Windows ("Fault in MS-DOS
; Extender": DX9 03-06), where w16's goes on with 0 (the user's ruling
; in the DOSX audit, ARCHITECTURE.md 9.6).
dses_fix:
        mov ax, [bp+F_DS]
        call seg_ok
        jnc .es
        mov word [bp+F_DS], 0
.es:    mov ax, [bp+F_ES]
        call seg_ok
        jnc .r
        mov word [bp+F_ES], 0
.r:     ret
; seg_ok: may ring 0 load AX into DS or ES? (null, or present and
; readable at its RPL) CF if not
seg_ok: test ax, 0FFFCh
        jz .ok
        push dx
        verr ax
        jnz .no
        lar dx, ax
        jnz .no
        test dh, 80h
        jz .no
        pop dx
.ok:    clc
        ret
.no:    pop dx
        stc
        ret

; exc_deliver: an exception from ring 3. BX = vector; the frame has the
; error code at E_ERR. Calls the exception handler (excvec[n]) at ring 3
; on the exception stack with the DPMI 0.9 frame: return address, error
; code, IP, CS, FLAGS, SP, SS. The return address is 00E0:140C, a ring-0
; selector, as Microsoft's (DX5 F00D): the handler's RETF to it faults,
; and that fault is its return (dpmi_ret). The client then continues as
; the frame says, with the registers the handler left (DX5 F00D under
; Microsoft's DOSX). A frame whose CS or SS a ring-3 program could not
; have ends Windows with "Standard Mode: Invalid DPMI return from
; cccc:iiii", the address of the handler's RETF, as Microsoft's DOSX does
; (DX9 24).
exc_deliver:
        ; the frame on the exception stack (EXC_SEL, 4 KB), as Microsoft's
        ; DOSX: SS = 02C7h, SP = 0FECh for the first one (DX5 F00D).
        ; Nested exceptions get the room below, 15 of them.
        cmp word [exc_sp], 110h
        jb near host_fault0
        sub sp, C_SIZE
        mov si, sp              ; the context
        mov ax, [exc_sp]
        push ax
        sub ax, 16
        mov di, ax
        sub word [exc_sp], 100h
        push es
        mov ax, EXC_SEL
        mov es, ax
        mov word [es:di], EXC_RETIP
        mov word [es:di+2], G_HC0B
        mov ax, [bp+E_ERR]
        mov [es:di+4], ax
        mov ax, [bp+E_IP]
        mov [es:di+6], ax
        mov ax, [bp+E_CS]
        mov [es:di+8], ax
        mov ax, [bp+E_FL]
        mov [es:di+10], ax
        mov ax, [bp+E_SP]
        mov [es:di+12], ax
        mov ax, [bp+E_SS]
        mov [es:di+14], ax
        pop es
        mov [si+C_SP], di
        mov word [si+C_SS], EXC_SEL
        shl bx, 2
        mov ax, [excvec+bx]
        mov [si+C_IP], ax
        mov ax, [excvec+bx+2]
        mov [si+C_CS], ax
        mov ax, [bp+E_FL]
        and ax, ~(FL_IF | FL_TF)
        mov [si+C_FL], ax
        call ctx_from_frame
        push word [exc_ctx]
        mov [exc_ctx], si
        mov bx, si
        call r0_call3
        pop word [exc_ctx]
        pop word [exc_sp]
        call ctx_to_frame
        ; the frame where the handler's RETF found it
        les di, [exc_rfr]
        mov ax, [es:di+8]       ; CS: ring-3 code
        call cs3_ok
        jc .inval
        mov [bp+E_CS], ax
        mov ax, [es:di+14]      ; SS: ring-3 writable data
        call ss3_ok
        jc .inval
        mov [bp+E_SS], ax
        mov ax, [es:di+6]
        mov [bp+E_IP], ax
        mov ax, [es:di+10]
        or ax, FL_IOPL          ; as r0_call3 gives ring 3 its flags
        and ax, ~FL_NT
        mov [bp+E_FL], ax
        mov ax, [es:di+12]
        mov [bp+E_SP], ax
        mov sp, bp
        jmp exc_exit
.inval: push ss
        pop es
        mov si, msg_dpmiret
        mov di, msgbuf
.cp:    lodsb
        stosb
        or al, al
        jnz .cp
        dec di
        mov ax, [exc_rcs]
        call hex4
        mov al, ':'
        stosb
        mov ax, [exc_rip]
        call hex4
        mov ax, 0A0Dh
        stosw
        mov al, '$'
        stosb
        mov word [pending_msg], msgbuf
        mov al, 0FFh
        jmp r0_terminate

; cs3_ok / ss3_ok: AX = a selector ring 3 can run in / use as its stack
; (RPL 3, present, DPL 3; code / writable data). CF if not.
cs3_ok: push dx
        mov dh, 18h             ; a code segment
        jmp sel3_ok
ss3_ok: push dx
        mov dh, 12h             ; writable data
sel3_ok:
        push cx
        mov cx, ax
        and cx, 3
        cmp cx, 3
        jne .no
        lar cx, ax
        jnz .no
        test ch, 80h            ; present
        jz .no
        mov dl, ch
        and dl, 60h             ; DPL 3
        cmp dl, 60h
        jne .no
        mov dl, ch
        and dl, dh
        cmp dl, dh
        jne .no
        cmp dh, 12h             ; data: not code
        jne .yes
        test ch, 08h
        jnz .no
.yes:   pop cx
        pop dx
        clc
        ret
.no:    pop cx
        pop dx
        stc
        ret

; dpmi_ret (from exc_entry: a general protection fault with error code
; 00E0h at ring 3): if it is an exception handler's RETF to the return
; address exc_deliver gave it, finish that exception: the handler's
; registers into exc_deliver's context, as ret3_entry does, and back into
; r0_call3's caller. Else return (an ordinary fault).
dpmi_ret:
        cmp word [exc_ctx], 0
        je .no
        mov ax, [bp+E_SS]
        mov si, [bp+E_SP]
        mov cx, 16
        xor dl, dl
        call uchk
        jc .no
        push ds
        mov ds, ax
        cmp word [si], EXC_RETIP
        jne .no1
        cmp word [si+2], G_HC0B
        jne .no1
        pop ds
        mov bx, [tss+2]         ; r0_call3's: [context] [old SP0] ...
        mov si, [ss:bx]
        cmp si, [exc_ctx]
        jne .no
        call ctx_from_frame
        mov ax, [bp+E_SP]
        mov [exc_rfr], ax
        mov ax, [bp+E_SS]
        mov [exc_rfr+2], ax
        mov ax, [bp+E_IP]
        mov [exc_rip], ax
        mov ax, [bp+E_CS]
        mov [exc_rcs], ax
        mov sp, bx
        jmp ret3_entry.back
.no1:   pop ds
.no:    ret

; ctx_from_frame: context SI <- the registers saved at BP (pusha, ES, DS)
ctx_from_frame:
        mov ax, [bp+F_AX]
        mov [si+C_AX], ax
        mov ax, [bp+F_BX]
        mov [si+C_BX], ax
        mov ax, [bp+F_CX]
        mov [si+C_CX], ax
        mov ax, [bp+F_DX]
        mov [si+C_DX], ax
        mov ax, [bp+F_SI]
        mov [si+C_SI], ax
        mov ax, [bp+F_DI]
        mov [si+C_DI], ax
        mov ax, [bp+F_BP]
        mov [si+C_BP], ax
        mov ax, [bp+F_DS]
        mov [si+C_DS], ax
        mov ax, [bp+F_ES]
        mov [si+C_ES], ax
        ret

; ctx_to_frame: the reverse
ctx_to_frame:
        mov ax, [si+C_AX]
        mov [bp+F_AX], ax
        mov ax, [si+C_BX]
        mov [bp+F_BX], ax
        mov ax, [si+C_CX]
        mov [bp+F_CX], ax
        mov ax, [si+C_DX]
        mov [bp+F_DX], ax
        mov ax, [si+C_SI]
        mov [bp+F_SI], ax
        mov ax, [si+C_DI]
        mov [bp+F_DI], ax
        mov ax, [si+C_BP]
        mov [bp+F_BP], ax
        mov ax, [si+C_DS]
        mov [bp+F_DS], ax
        mov ax, [si+C_ES]
        mov [bp+F_ES], ax
        ret

; host_fault: an exception in DOSX's own ring-0 code (frame at BP,
; error code at E_ERR, no SS:SP). Microsoft's DOSX prints "Standard
; Mode: Fault in MS-DOS Extender." with the registers and ends with
; errorlevel 0FFh (DX5 0502h, F006).
host_fault:
        mov ax, [bp+E_ERR]
        mov [fx_ec], ax
        mov ax, [bp+E_CS]
        mov [fx_cs], ax
        mov ax, [bp+E_IP]
        mov [fx_ip], ax
        lea ax, [bp+E_FL+2]
        mov [fx_sp], ax
        mov [fx_ss], ss
        mov si, msg_hostfault
        jmp fault_report

; ---------------------------------------------------------------------------
; the call gate (G_GATE): ring-3 stubs push an argument and an operation
gate_entry:
        cli
        PUSH_FSGS
        push ds
        push es
        pusha
        mov bp, sp
        mov ax, G_HD0
        mov ds, ax
        cld
        pushf                   ; NT as ring 3 left it (a call gate keeps
        pop ax                  ; it): r0_call3's IRET must not be a task
        and ax, ~FL_NT          ; return
        push ax
        popf
        les si, [bp+F_SP3]
        mov bx, [es:si+K_OP]
        cmp bx, OP_COUNT
        jae gate_exit
        shl bx, 1
        TRACE_GATE
        call [optab+bx]
        TRACE_RESULT
gate_exit:
        add word [bp+F_SP3], 4  ; the operation and argument words
        call dses_fix
        popa
        pop es
        pop ds
        POP_FSGS
        retf

; ring3sp: ES:SI = the ring-3 stack at the gate call
ring3sp:     les si, [bp+F_SP3]
        ret

; fsgs_restore (386; near call, the saved GS and FS above the return
; address): FS and GS get their saved values back. A saved selector the
; processor would not load (a freed one: Windows 3.1's KERNEL leaves them
; in FS and GS, docs/notes/quicktime-handover.md) is left in the register
; if the register still holds it (an INT 31h call: Microsoft's DOSX keeps
; FS there, TK61 "deadfs: INT 31h"), else 0 (after a trip to real mode:
; Microsoft's DOSX ends Windows there, "Fault in MS-DOS Extender" at every
; timer tick and INT 21h with a freed selector in FS, TK61 "deadfs: ticks"
; and "INT 21h"; WIN386 loads 0, as here).
fsgs_restore:
        cpu 386
        push bp
        mov bp, sp
        pushf
        push ax
        push dx
        mov ax, [bp+4]          ; the saved GS
        mov dx, gs
        call fsgs_pick
        jc .g
        mov gs, ax
.g:     mov ax, [bp+6]          ; the saved FS
        mov dx, fs
        call fsgs_pick
        jc .f
        mov fs, ax
.f:     pop dx
        pop ax
        popf
        pop bp
        ret
        cpu 286

; fsgs_pick: AX = a saved selector, DX = the register's value now -> CF
; clear: load AX (the selector, or 0 for one that cannot be loaded); CF
; set: leave the register as it is
fsgs_pick:
        test ax, 0FFFCh
        jz .load                ; a null selector loads
        push bx
        lar bx, ax
        jnz .bad
        test bh, 80h            ; present
        jz .bad
        test bh, 10h            ; code or data
        jz .bad
        test bh, 08h
        jz .ok                  ; data
        test bh, 02h            ; readable code
        jz .bad
.ok:    pop bx
.load:  clc
        ret
.bad:   pop bx
        cmp ax, dx
        je .keep
        xor ax, ax
        clc
        ret
.keep:  stc
        ret

; set_cf / clr_cf: the client's carry flag in the interrupt frame
set_cf: push es
        push si
        call ring3sp
        or byte [es:si+K_FL], FL_CF
        pop si
        pop es
        ret
clr_cf: push es
        push si
        call ring3sp
        and byte [es:si+K_FL], ~FL_CF
        pop si
        pop es
        ret

; op_int28: INT 28h from protected mode (int28_stub): every tenth to real
; mode as op_reflect would send it, the others end here
op_int28:
        dec byte [idle28_left]
        jz .go
        ret
.go:    mov byte [idle28_left], 10
        mov ax, 2800h
        jmp r0_reflect

; op_reflect: the default protected-mode handler of vector n: the same
; interrupt in real mode with the client's general registers; the
; arithmetic flags come back in the interrupt frame.
op_reflect:
        mov ax, [es:si+K_ARG]
        sub ax, refstub_tab + 3
        mov dl, 3
        div dl
        mov ah, al
        mov al, 0
        ; fall through: AH = vector
r0_reflect:
        sub sp, R_SIZE
        mov bx, sp
        push ax
        call rs_from_frame
        call ring3sp
        mov ax, [es:si+K_FL]
        mov [bx+R_FL], ax
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov [bx+R_ES], ax
        pop ax
        ; a vector DOSX hooked for the client goes to its previous owner
        push bx
        push si
        mov bl, ah
        xor bh, bh
        call hook_slot
        mov di, si
        pop si
        pop bx
        jc .int
        cmp byte [rmhooked+di], 0
        je .int
        shl di, 2
        mov cx, [rmorig+di]
        mov [bx+R_IP], cx
        mov cx, [rmorig+di+2]
        mov [bx+R_CS], cx
        mov al, 2
.int:   xor cx, cx
        call r0_rmcall
        call rs_to_frame
        call ring3sp
        mov ax, [bx+R_FL]
        and ax, FL_ARITH
        and word [es:si+K_FL], ~FL_ARITH
        or [es:si+K_FL], ax
        add sp, R_SIZE
        ret

; rs_from_frame: structure BX <- the frame's general registers; SS:SP,
; CS:IP, FS, GS and the high words zero
rs_from_frame:
        push di
        push cx
        push ax
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        push es
        push ss
        pop es
        rep stosw
        pop es
        mov ax, [bp+F_AX]
        mov [bx+R_EAX], ax
        mov ax, [bp+F_BX]
        mov [bx+R_EBX], ax
        mov ax, [bp+F_CX]
        mov [bx+R_ECX], ax
        mov ax, [bp+F_DX]
        mov [bx+R_EDX], ax
        mov ax, [bp+F_SI]
        mov [bx+R_ESI], ax
        mov ax, [bp+F_DI]
        mov [bx+R_EDI], ax
        mov ax, [bp+F_BP]
        mov [bx+R_EBP], ax
        pop ax
        pop cx
        pop di
        ret

; rs_to_frame: the frame's general registers <- structure BX
rs_to_frame:
        push ax
        mov ax, [bx+R_EAX]
        mov [bp+F_AX], ax
        mov ax, [bx+R_EBX]
        mov [bp+F_BX], ax
        mov ax, [bx+R_ECX]
        mov [bp+F_CX], ax
        mov ax, [bx+R_EDX]
        mov [bp+F_DX], ax
        mov ax, [bx+R_ESI]
        mov [bp+F_SI], ax
        mov ax, [bx+R_EDI]
        mov [bp+F_DI], ax
        mov ax, [bx+R_EBP]
        mov [bp+F_BP], ax
        pop ax
        ret

; op_excdef: the default exception handler of exception n, called with
; the DPMI frame (ring-3 stack: [4] return IP, [6] return CS, [8] error
; code, [10] IP, [12] CS, [14] FLAGS, [16] SP, [18] SS). Exceptions 06h
; and 08h-0Eh end the client with "Fault outside of MS-DOS Extender"
; (DX5 F000, F002, F00F); the others go to the protected-mode interrupt
; handler of the same number, as if the interrupt had happened there
; (Microsoft's defaults: DX1 excvec lines; DX5 F001).
op_excdef:
        mov ax, [es:si+K_ARG]
        sub ax, excstub3_tab + 3
        mov dl, 3
        div dl
        xor ah, ah
        mov bx, ax
        cmp bx, 6
        je .fatal
        cmp bx, 8
        jb .refl
        cmp bx, 0Eh
        jbe .fatal
.refl:  ; the frame, and room for an interrupt frame on the client's stack
        ; (a stack it does not fit on: the client's fault, as 06h)
        push dx
        push cx
        mov ax, es
        mov cx, 20
        xor dl, dl
        call uchk
        jc .bad
        mov ax, [es:si+18]
        push si
        mov si, [es:si+16]
        sub si, 6
        mov cx, 6
        mov dl, 1
        call uchk
        pop si
.bad:   pop cx
        pop dx
        jc .fatal
        push es
        les di, [es:si+16]      ; the client's SS:SP
        sub di, 6
        pop ds
        mov ax, [si+10]
        mov [es:di], ax
        mov ax, [si+12]
        mov [es:di+2], ax
        mov ax, [si+14]
        mov [es:di+4], ax
        mov [si+16], di
        push ss
        pop es
        shl bx, 2
        mov ax, [es:pmvec+bx]
        mov [si+10], ax
        mov ax, [es:pmvec+bx+2]
        mov [si+12], ax
        and word [si+14], ~(FL_IF | FL_TF)
        push ss
        pop ds
        ret
.fatal: mov ax, [es:si+8]
        mov [fx_ec], ax
        mov ax, [es:si+10]
        mov [fx_ip], ax
        mov ax, [es:si+12]
        mov [fx_cs], ax
        mov ax, [es:si+16]
        mov [fx_sp], ax
        mov ax, [es:si+18]
        mov [fx_ss], ax
        mov si, msg_clientfault
        ; fall through

; fault_report: print message SI with the registers in the frame at BP
; (and fx_*), then end the client with exit code 0FFh
fault_report:
        mov ax, [bp+F_AX]
        mov [fx_ax], ax
        mov ax, [bp+F_BX]
        mov [fx_bx], ax
        mov ax, [bp+F_CX]
        mov [fx_cx], ax
        mov ax, [bp+F_DX]
        mov [fx_dx], ax
        mov ax, [bp+F_SI]
        mov [fx_si], ax
        mov ax, [bp+F_DI]
        mov [fx_di], ax
        mov ax, [bp+F_BP]
        mov [fx_bp], ax
        mov ax, [bp+F_DS]
        mov [fx_ds], ax
        mov ax, [bp+F_ES]
        mov [fx_es], ax
.fmt:   push ss
        pop es
        mov di, msgbuf
.cp:    lodsb
        stosb
        or al, al
        jnz .cp
        dec di
        mov si, fx_names
        mov bx, fx_vals
.reg:   lodsb
        or al, al
        jz .regd
        cmp al, 1
        jne .ch
        mov ax, [bx]
        add bx, 2
        call hex4
        jmp .reg
.ch:    stosb
        jmp .reg
.regd:  mov al, '$'
        stosb
        mov word [pending_msg], msgbuf
        mov al, 0FFh
        jmp r0_terminate

; client_fault0: end the client with message SI when DOSX meets something
; it cannot go on with away from a frame of the client's registers (a
; client structure or stack it cannot reach, its host stacks used up):
; the message and a register line of zeros, exit code 0FFh. Where Windows
; 3.1's DOSX would end with a fault of its own, w16's ends the client
; cleanly (the user's ruling in the DOSX audit, ARCHITECTURE.md 9.6).
client_fault0:
        mov ax, G_HD0
        mov ds, ax
        mov es, ax
        mov di, fx_vals
        mov cx, 14
        xor ax, ax
        rep stosw
        jmp fault_report.fmt
host_fault0:
        mov si, msg_hostfault
        jmp client_fault0

; uchk: may ring 0 use the client's AX:SI for CX bytes (CX of 0 is
; always good)? DL = 0 to read, 1 to write. The selector must be present
; and readable (writable data to write), and every byte within its limit
; and the 64 KB a 16-bit offset reaches (above the limit for an
; expand-down segment). CF if not; registers kept.
uchk:   push ax
        push bx
        push cx
        push dx
        jcxz .ok
        verr ax
        jnz .f
        or dl, dl
        jz .rd
        verw ax
        jnz .f
.rd:    lar bx, ax
        jnz .f
        test bh, 80h            ; present
        jz .f
        dec cx
        add cx, si              ; the last byte
        jc .f
        cmp byte [cs:is386], 0
        je .l16
        cpu 386
        push edx
        lsl edx, ax
        cmp edx, 0FFFFh
        jb .l32
        mov dx, 0FFFFh
.l32:   mov ax, dx
        pop edx
        cpu 286
        jmp .lim
.l16:   lsl ax, ax
.lim:   mov dl, bh
        and dl, 0Ch             ; data, expand-down?
        cmp dl, 04h
        je .xd
        cmp cx, ax
        ja .f
.ok:    pop dx
        pop cx
        pop bx
        pop ax
        clc
        ret
.xd:    cmp si, ax              ; expand-down: offsets above the limit
        ja .ok
.f:     pop dx
        pop cx
        pop bx
        pop ax
        stc
        ret

; hex4: AX as 4 hex digits at ES:DI
hex4:   push cx
        mov cx, 4
.l:     rol ax, 4
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .d
        add al, 7
.d:     stosb
        pop ax
        loop .l
        pop cx
        ret

; r0_print: DX = '$'-terminated message in host data, through real-mode
; INT 21h AH=09h
r0_print:
        sub sp, R_SIZE
        mov bx, sp
        push di
        push cx
        mov di, bx
        mov cx, R_SIZE / 2
        xor ax, ax
        push es
        push ss
        pop es
        rep stosw
        pop es
        pop cx
        pop di
        mov word [bx+R_EAX], 0900h
        mov word [bx+R_FL], 0202h
        mov [bx+R_EDX], dx
        mov ax, [imgseg]
        mov [bx+R_DS], ax
        mov ax, 2100h
        xor cx, cx
        call r0_rmcall
        add sp, R_SIZE
        ret

; The fatal messages are printed by DOSX itself once the client's
; program has ended (pending_msg), as Microsoft's DOSX does: its output
; follows everything the client wrote.
; fatal_nomem: "Insufficient extended memory to run Windows in standard
; mode." (with "w16" for "Windows", ARCHITECTURE.md 3) and exit 0FFh:
; what Microsoft's DOSX does for every INT 31h function it does not have
; (DX5: 000Eh, 0401h, 0504h-050Bh, 0801h, 0C00h, 0E00h, ...)
fatal_nomem:
        mov word [pending_msg], msg_nomem
        mov al, 0FFh
        ; fall through

; r0_terminate: AL = exit code. Undo the client's traces, leave
; protected mode for good and end the client's program with INT 21h
; AH=4Ch in real mode; DOS returns to DOSX's EXEC.
r0_terminate:
        cli
        mov ah, 4Ch
        push ax
        mov ax, G_HD0
        mov ds, ax
        pop ax
        mov [exit_ax], ax
        mov ax, G_HD0
        mov es, ax
        mov ss, ax
        mov sp, hs_stack_top
        mov word [hs_top], hs_stack_top
        mov word [h3_top], H3_SIZE
        mov word [tss+2], hs_stack_top
        mov word [tb_top], tbuf + TB_SIZE
        mov word [exc_ctx], 0
%ifdef TRACE
        ; the trace's last records, on the fresh stacks; once only (a
        ; fault in the flush itself comes back here)
        mov word [rmx_depth], 0
        cmp byte [trc_fin], 0
        jne .nf
        mov byte [trc_fin], 1
        mov word [trc_batch], 1
        call trc_flush.all
.nf:
%endif
        call to_rm
        call client_rm_reset
        ; the client's own PSP current again: KERNEL makes its tasks' PDBs
        ; DOS's current PSP (INT 21h 50h), and DOS ends the current one
        ; and returns to its parent (here a selector, not a segment)
        mov bx, [c_psp]
        mov ah, 50h
        int 21h

        mov ax, [exit_ax]
        int 21h
        ; not reached
        jmp $

; ---------------------------------------------------------------------------
; r0_client_start: the client called pm_entry; its real-mode state is in
; c_*. Set up its protected-mode world and continue it at ring 3.
r0_client_start:
        mov byte [client_active], 1
        call ldt_init
        call arena_init
        ; the exception stack: 4 KB of extended memory (Microsoft's is
        ; a 4 KB block there too, selector 02C7h, base 145020h: DX1, DX5)
        xor cx, cx
        mov bx, 1000h
        call arena_alloc
        add ax, A_HDR
        adc dx, 0
        mov bx, EXC_SEL
        mov cx, 0FFFh
        call desc_lin
        mov word [exc_sp], 0FFCh
        mov word [exc_ctx], 0
        call vectors_init
        mov word [cb_cap], CB_STATIC
        mov word [cb_depth], 0
        push ds
        pop es
        mov di, cb_static
        mov cx, CB_STATIC * 4
        xor ax, ax
        rep stosw
        ; selectors, in Microsoft's order (TK18: DS 027F, environment
        ; 02D7, PSP 02DF, CS 02E7)
        mov ax, [c_ds]
        call segmap
        mov [c_dssel], ax
        mov ax, [c_ss]
        call segmap
        mov [c_sssel], ax
        ; the environment
        mov ax, [c_psp]
        mov di, G_SCR0
        call gdt_base_seg
        mov ax, G_SCR0
        mov es, ax
        mov ax, [es:2Ch]
        mov [c_env], ax
        mov cx, 1
        call ldt_alloc
        mov [c_envsel], ax
        mov bx, ax
        mov ax, [c_env]
        mov cx, 0FFFFh
        mov dl, 0F2h
        call desc_seg
        mov cx, 1
        call ldt_alloc
        mov [c_pspsel], ax
        mov bx, ax
        mov ax, [c_psp]
        mov cx, 100h
        mov dl, 0F2h
        call desc_seg
        mov cx, 1
        call ldt_alloc
        mov [c_cssel], ax
        mov bx, ax
        mov ax, [c_cs]
        mov cx, 0FFFFh
        mov dl, 0FAh
        call desc_seg
        mov ax, G_SCR0
        mov es, ax
        mov ax, [c_envsel]
        mov [es:2Ch], ax
        ; the PM DTA: the PSP's default one
        mov word [dta_off], 80h
        mov ax, [c_pspsel]
        mov [dta_sel], ax
        ; to ring 3
        mov word [tss+2], hs_stack_top
        mov sp, hs_stack_top
        push word [c_sssel]
        push word [c_sp]
        mov ax, [c_fl]
        or ax, FL_IOPL
        and ax, ~FL_NT
        push ax
        push word [c_cssel]
        push word [c_ip]
        mov es, [c_pspsel]
        mov ax, [c_ax]
        mov bx, [c_bx]
        mov cx, [c_cx]
        mov dx, [c_dx]
        mov si, [c_si]
        mov di, [c_di]
        mov bp, [c_bp]
        cmp byte [is386], 0     ; FS and GS null for the client (the
        je .nfs                 ; IRET would null them anyway: DPL 0)
        cpu 386
        push word 0
        pop fs
        push word 0
        pop gs
        cpu 286
.nfs:   mov ds, [c_dssel]
        iret

; vectors_init: the default protected-mode interrupt handlers (the
; ring-3 stubs; INT 10h, 13h, 15h, 21h, 25h, 26h, 2Fh, 31h and 33h are
; DOSX's own; 01h, 03h, 30h and
; 41h just return, as with Microsoft's DOSX: DX1 pmvec lines) and
; exception handlers, and the IDT
vectors_init:
        xor bx, bx
        mov ax, refstub_tab
.pv:    mov [pmvec+bx], ax
        mov word [pmvec+bx+2], G_HC3 | 3
        add ax, 3
        add bx, 4
        cmp bx, 400h
        jb .pv
        mov word [pmvec+10h*4], int10_stub
        mov word [pmvec+15h*4], int15_stub
        mov word [pmvec+13h*4], int13_stub
        mov word [pmvec+25h*4], int25_stub
        mov word [pmvec+26h*4], int26_stub
        mov word [pmvec+33h*4], int33_stub
        mov word [pmvec+4Bh*4], int4b_stub
        mov word [pmvec+21h*4], int21_stub
        mov word [pmvec+2Fh*4], int2f_stub
        mov word [pmvec+28h*4], int28_stub
        mov word [pmvec+31h*4], int31_stub
        mov word [pmvec+01h*4], iret_stub
        mov word [pmvec+03h*4], iret_stub
        mov word [pmvec+30h*4], iret_stub
        mov word [pmvec+41h*4], iret_stub
        xor bx, bx
        mov ax, excstub3_tab
.ev:    mov [excvec+bx], ax
        mov word [excvec+bx+2], G_HC3 | 3
        add ax, 3
        add bx, 4
        cmp bx, 80h
        jb .ev
        ; IDT: 00h-1Fh to ring 0, the rest to the handlers
        xor bx, bx
        mov ax, excstub_tab
        push es
        mov cx, G_IDT
        mov es, cx
.i0:    mov [es:bx], ax
        mov word [es:bx+2], G_HC0
        mov word [es:bx+4], 0E600h
        mov word [es:bx+6], 0
        add ax, 3
        add bx, 8
        cmp bx, 20h * 8
        jb .i0
        pop es
        mov cx, 20h
.i1:    call idt_from_pmvec
        inc cx
        cmp cx, 100h
        jb .i1
        ; nothing hooked in real mode
        mov di, rmhooked
        mov cx, NHOOK
        xor ax, ax
        push es
        push ds
        pop es
        rep stosb
        pop es
        ret

; idt_from_pmvec: CX = vector (20h-FFh): its gate <- pmvec[CX]
idt_from_pmvec:
        push bx
        push si
        push ax
        mov bx, cx
        shl bx, 2
        mov si, cx
        shl si, 3
        push es
        mov ax, G_IDT
        mov es, ax
        mov ax, [pmvec+bx]
        mov [es:si], ax
        mov ax, [pmvec+bx+2]
        mov [es:si+2], ax
        mov word [es:si+4], 0E600h
        mov word [es:si+6], 0
        pop es
        pop ax
        pop si
        pop bx
        ret

; ---------------------------------------------------------------------------
; r0_rmx: from rmx_common, SP = the real-mode structure, then kind,
; index, the saved hs_top
r0_rmx:
        mov bp, sp
        cld
        ; the host stacks: the real-mode/ring-0 one (hs_stack) and the
        ; ring-3 one (G_HD3, H3_CHUNK per nesting level) must have room
        ; for another level, else DOSX ends the client (once overrun they
        ; spilled into the transfer buffer and the client's memory)
        cmp sp, hs_stack + 200h
        jb near host_fault0
        cmp word [h3_top], H3_CHUNK
        jb near host_fault0
%ifdef TRACE
        ; the record: the real-mode registers in a gate frame's layout,
        ; and the real-mode CS:IP, SS:SP
        pusha
        push word [bp+R_DS]
        push word [bp+R_ES]
        push word [bp+R_EAX]
        push word [bp+R_ECX]
        push word [bp+R_EDX]
        push word [bp+R_EBX]
        push word 0
        push word [bp+R_EBP]
        push word [bp+R_ESI]
        push word [bp+R_EDI]
        mov ax, [bp+R_IP]
        mov [trc_x], ax
        mov ax, [bp+R_CS]
        mov [trc_x+2], ax
        mov ax, [bp+R_SS]
        mov [trc_x+4], ax
        mov ax, [bp+R_SP]
        mov [trc_x+6], ax
        mov ax, [bp+R_SIZE+2]
        and ax, 0FFh
        or ax, 0200h
        cmp byte [bp+R_SIZE], 0
        je .tk
        add ax, 0100h           ; 03nnh: hook of vector nn
.tk:    push bp
        mov bp, sp
        add bp, 2
        call trc
        pop bp
        add sp, 20
        popa
%endif
        inc word [rmx_depth]
        cmp byte [bp+R_SIZE], 0
        jne near .hook
        ; a callback: its record
        mov ax, [bp+R_SIZE+2]
        cmp ax, [cb_cap]
        jae near .back
        call cb_rec             ; ES:DI = the record
        mov ax, [es:di+2]
        or ax, ax
        jz near .back
        sub sp, C_SIZE
        mov si, sp
        mov ax, [es:di]
        mov [si+C_IP], ax
        mov ax, [es:di+2]
        mov [si+C_CS], ax
        mov ax, [es:di+4]
        mov [si+C_DI], ax
        mov ax, [es:di+6]
        mov [si+C_ES], ax
        ; copy the real-mode registers to the client's structure (one
        ; ring 0 cannot write: the client's fault)
        push si
        mov si, [si+C_DI]
        mov cx, R_SIZE
        mov dl, 1
        call uchk
        jc near .badcb
        mov es, ax
        mov di, si
        mov si, bp
        mov cx, R_SIZE / 2
        rep movsw
        pop si
        ; DS:SI = the real-mode SS:SP, through a selector of this
        ; nesting level
        call cb_stacksel
        jc near host_fault0
        mov [si+C_DS], ax
        mov ax, [bp+R_SP]
        mov [si+C_SI], ax
        ; the ring-3 stack: an IRET frame back to ret3_stub
        mov ax, [h3_top]
        push ax
        sub word [h3_top], H3_CHUNK
        sub ax, 6
        mov di, ax
        push es
        mov cx, G_HD3
        mov es, cx
        mov word [es:di], ret3_stub
        mov word [es:di+2], G_HC3 | 3
        ; the real-mode caller's flags, but DF clear: Microsoft's DOSX
        ; starts the procedure with DF clear when real mode called with DF
        ; set (DX9 0D)
        mov cx, [bp+R_FL]
        and cx, ~(FL_IF | FL_TF | FL_NT | FL_DF)
        or cx, FL_IOPL
        mov [es:di+4], cx
        pop es
        mov [si+C_SP], di
        mov word [si+C_SS], G_HD3 | 3
        mov [si+C_FL], cx
        mov ax, [bp+R_EAX]
        mov [si+C_AX], ax
        mov [si+C_BX], ax
        mov [si+C_CX], ax
        mov [si+C_DX], ax
        mov [si+C_BP], ax
        inc word [cb_depth]
        mov bx, si
        call r0_call3
        dec word [cb_depth]
        pop word [h3_top]
        ; ES:DI = the structure to continue with
        mov ax, [si+C_ES]
        mov si, [si+C_DI]
        mov cx, R_SIZE
        xor dl, dl
        call uchk
        jc .badcb
        push ds
        mov ds, ax
        push ss
        pop es
        mov di, bp
        mov cx, R_SIZE / 2
        rep movsw
        pop ds
        mov sp, bp
        jmp .back
.badcb: mov si, msg_clientfault
        jmp client_fault0
.hook:  ; a real-mode interrupt the client handles in protected mode
        cmp byte [bp+R_SIZE], 2
        je near .ps2
        sub sp, C_SIZE
        mov si, sp
        mov bx, [bp+R_SIZE+2]
        shl bx, 2
        mov ax, [pmvec+bx]
        mov [si+C_IP], ax
        mov ax, [pmvec+bx+2]
        mov [si+C_CS], ax
        mov ax, [h3_top]
        push ax
        sub word [h3_top], H3_CHUNK
        sub ax, 6
        mov di, ax
        push es
        mov cx, G_HD3
        mov es, cx
        mov word [es:di], ret3_stub
        mov word [es:di+2], G_HC3 | 3
        mov cx, [bp+R_FL]
        and cx, ~(FL_IF | FL_TF | FL_NT)
        or cx, FL_IOPL
        mov [es:di+4], cx
        pop es
        mov [si+C_SP], di
        mov word [si+C_SS], G_HD3 | 3
        mov [si+C_FL], cx
        mov ax, [bp+R_EAX]
        mov [si+C_AX], ax
        mov ax, [bp+R_EBX]
        mov [si+C_BX], ax
        mov ax, [bp+R_ECX]
        mov [si+C_CX], ax
        mov ax, [bp+R_EDX]
        mov [si+C_DX], ax
        mov ax, [bp+R_ESI]
        mov [si+C_SI], ax
        mov ax, [bp+R_EDI]
        mov [si+C_DI], ax
        mov ax, [bp+R_EBP]
        mov [si+C_BP], ax
        xor ax, ax
        mov [si+C_DS], ax
        mov [si+C_ES], ax
        mov bx, si
        call r0_call3
        pop word [h3_top]
        mov ax, [si+C_AX]
        mov [bp+R_EAX], ax
        mov ax, [si+C_BX]
        mov [bp+R_EBX], ax
        mov ax, [si+C_CX]
        mov [bp+R_ECX], ax
        mov ax, [si+C_DX]
        mov [bp+R_EDX], ax
        mov ax, [si+C_SI]
        mov [bp+R_ESI], ax
        mov ax, [si+C_DI]
        mov [bp+R_EDI], ax
        mov ax, [si+C_BP]
        mov [bp+R_EBP], ax
        mov sp, bp
        jmp .back
.ps2:   ; the BIOS called the pointing-device handler (ps2_entry): far
        ; call the client's with the BIOS's four words, on the ring-3 host
        ; stack, interrupts disabled as the BIOS has them. Not while the
        ; client has not yet taken IRQ 12 (INT 74h) in protected mode: then
        ; the packet is dropped. MOUSE.DRV enables the device (C200h) and
        ; sets INT 74h after it; a packet that came in between (mouse
        ; motion during the boot, in real mode, straight to the BIOS)
        ; reached its handler before its INT 74h one was set, and IRQ 12
        ; then stayed in service for good: the pointer never moved again
        ; (1 boot in 5 with motion from second 16, autolock=false; never
        ; with Microsoft's DOSX: tests/dos/ms1.asm, TK49).
%ifndef NOPS2DROP                       ; (a test build without the drop:
        cmp word [pmvec+74h*4+2], G_HC3 | 3 ; DOSXND.EXE, tests/mousestress.sh
        jne .ps2go                      ; -D nd)
        cmp word [pmvec+74h*4], refstub_tab + 74h * 3
        je near .back
%endif
.ps2go: sub sp, C_SIZE
        mov si, sp
        mov ax, [ps2_pm]
        mov [si+C_IP], ax
        mov ax, [ps2_pm+2]
        mov [si+C_CS], ax
        mov ax, [h3_top]
        push ax
        sub word [h3_top], H3_CHUNK
        sub ax, 12
        mov di, ax
        push si
        push ds
        push es
        call cb_stacksel        ; AX = a selector for the real-mode stack
        jc near host_fault0
        mov cx, G_HD3
        mov es, cx
        mov word [es:di], ret3_stub
        mov word [es:di+2], G_HC3 | 3
        mov ds, ax
        mov si, [bp+R_SP]
        add si, 4               ; past the BIOS's return address
        push di
        add di, 4
        mov cx, 4
        rep movsw
        pop di
        pop es
        pop ds
        pop si
        mov cx, [bp+R_FL]
        and cx, ~(FL_IF | FL_TF)
        or cx, FL_IOPL
        mov [si+C_SP], di
        mov word [si+C_SS], G_HD3 | 3
        mov [si+C_FL], cx
        mov ax, [bp+R_EAX]
        mov [si+C_AX], ax
        mov ax, [bp+R_EBX]
        mov [si+C_BX], ax
        mov ax, [bp+R_ECX]
        mov [si+C_CX], ax
        mov ax, [bp+R_EDX]
        mov [si+C_DX], ax
        mov ax, [bp+R_ESI]
        mov [si+C_SI], ax
        mov ax, [bp+R_EDI]
        mov [si+C_DI], ax
        mov ax, [bp+R_EBP]
        mov [si+C_BP], ax
        xor ax, ax
        mov [si+C_DS], ax
        mov [si+C_ES], ax
        inc word [cb_depth]
        mov bx, si
        call r0_call3
        dec word [cb_depth]
        pop word [h3_top]
        mov sp, bp
.back:  mov sp, bp
        dec word [rmx_depth]
        call to_rm
        jmp rmx_resume

; cb_stacksel: AX = a selector for the real-mode stack segment [bp+R_SS],
; one per callback nesting level (allocated when first needed); CF if the
; LDT has none left
cb_stacksel:
        push bx
        push cx
        push dx
        mov bx, [cb_depth]
        shl bx, 1
        cmp bx, CBSEL_MAX * 2
        jae .new
        mov ax, [cbsel+bx]
        or ax, ax
        jnz .set
.new:   mov cx, 1
        call ldt_alloc
        jc .fail
        cmp bx, CBSEL_MAX * 2
        jae .set
        mov [cbsel+bx], ax
.set:   mov bx, ax
        mov ax, [bp+R_SS]
        mov cx, 0FFFFh
        mov dl, 0F2h
        call desc_seg
        mov ax, bx
        clc
.fail:  pop dx
        pop cx
        pop bx
        ret
