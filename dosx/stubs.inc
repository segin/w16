; stubs.inc - entry tables and the ring-3 code a client can call or
; chain to.

; IDT 00h-1Fh (ring 0)
excstub_tab:
%rep 32
        call exc_entry
%endrep

; real-mode interrupt hooks (real mode)
rmhook_tab:
%rep 256
        call rmhook_common
%endrep

; ---------------------------------------------------------------------------
; ring 3 (CS = G_HC3)

; the default protected-mode interrupt handlers: reflect to real mode
refstub_tab:
%rep 256
        call ref_common
%endrep
ref_common:
        cli
        push OP_REFLECT
        call (G_GATE | 3):0
        iret

; the default exception handlers
excstub3_tab:
%rep 32
        call excdef_common
%endrep
excdef_common:
        cli
        push OP_EXCDEF
        call (G_GATE | 3):0
        retf

int21_stub:
        cli
        push 0
        push OP_INT21
        call (G_GATE | 3):0
        iret

int31_stub:
        cli
        push 0
        push OP_INT31
        call (G_GATE | 3):0
        iret

iret_stub:
        iret

; where ring-3 code DOSX called (exception handlers, callbacks, interrupt
; handlers for real-mode interrupts) comes back to
ret3_stub:
        cli                     ; ring 0 runs with interrupts disabled
        call (G_GATER | 3):0

; INT 2Fh in protected mode: 1686h answers 0 (in protected mode), 168Ah
; with DS:SI = "MS-DOS" gives the vendor entry in ES:DI and AX = 0
; (TK18, DX1); everything else goes to real mode
int2f_stub:
        cmp ax, 1686h
        jne .v
        xor ax, ax
        iret
.v:     cmp ax, 168Ah
        jne .chain
        push es
        push di
        push si
        push cx
        cld
        push cs
        pop es
        mov di, s_msdos
        mov cx, 7
        repe cmpsb
        pop cx
        pop si
        pop di
        pop es
        jne .chain
        xor ax, ax
        push cs
        pop es
        mov di, vendor_entry
        iret
.chain: jmp refstub_tab + 2Fh * 3

; the vendor entry: AX = 0100h gives the LDT's alias selector (TK18:
; 009Bh, CF clear). UNVERIFIED: other functions (they fail here).
vendor_entry:
        cmp ax, 0100h
        jne .f
        mov ax, G_LDTA | 3
        clc
        retf
.f:     stc
        retf

s_msdos db 'MS-DOS', 0
