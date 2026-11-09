; defs.inc - constants for DOSX.
;
; GDT layout. The entries a ring-3 program can see (LAR succeeds) are
; where Microsoft's DOSX has them (DX1: gdt and gdtraw lines), so the
; selectors a client may meet have the same values: 0053 for the default
; interrupt handlers and the vendor entry, 009B for the LDT alias, 0038
; for the LDT, 0088 for the TSS. Ring-0 entries are w16's own.

G_GDT   equ 08h         ; the GDT as data (DPL 3)
G_IDT   equ 10h         ; the IDT as data (DPL 3)
G_ZERO  equ 18h         ; data at linear 0, 64 KB (DPL 3)
G_PSP   equ 20h         ; DOSX's PSP (DPL 3)
G_ENV   equ 28h         ; DOSX's environment (DPL 3)
G_BIOS  equ 30h         ; code at F0000h (DPL 3)
G_LDT   equ 38h         ; the LDT (system descriptor, DPL 3)
G_B40   equ 40h         ; data at 400h, the BIOS data area (DPL 3)
G_HD3   equ 48h         ; host data, the image (DPL 3)
G_HC3   equ 50h         ; host code, the image (DPL 3)
G_RC3   equ 58h         ; the image as code once more (DPL 3)
G_HD0   equ 68h         ; host data (DPL 0)
G_HC0   equ 70h         ; host code (DPL 0)
G_RC0   equ 78h         ; host code alias (DPL 0)
G_TSS   equ 88h         ; the 286 TSS
G_TSSA  equ 90h         ; the TSS as data (DPL 3)
G_LDTA  equ 98h         ; the LDT as data (DPL 3): the vendor entry's 009Bh
G_FLAT  equ 0A0h        ; data at 0, 4 GB on a 386 (DPL 3)
G_SCR0  equ 0A8h        ; ring-0 scratch data selectors (DPL 0)
G_SCR1  equ 0B0h
G_SCR2  equ 0B8h
G_SCR3  equ 0C0h
G_GATE  equ 0C8h        ; call gate to ring 0 (DPL 3): gate_entry
G_GATER equ 0D0h        ; call gate to ring 0 (DPL 3): ret3_entry
G_HDS   equ 0D8h        ; the first 256 bytes of host data (DPL 3)
G_HC0B  equ 0E0h        ; host code (DPL 0), as Microsoft's
GDT_ENTRIES equ 24h     ; limit 011Fh, as observed

; LDT layout at a client's start (DX1, DX2; ARCHITECTURE.md 9.6)
LDT_LIMIT       equ 0FFEFh      ; 8190 descriptors
LDT_SEGTOP      equ 4Fh         ; segment descriptors (0002h) from here down
LDT_SCRA        equ 50h         ; 0287h: host scratch (ring 3)
LDT_SCRB        equ 51h         ; 028Fh: host scratch
LDT_RES_END     equ 5Ah         ; 02D7h: the first free descriptor
LDT_COUNT       equ 1FFEh       ; descriptors in the LDT (limit 0FFEFh)

LDT_EXC         equ 58h         ; 02C7h: the exception handlers' stack

; selectors with RPL 3
S_SCRA  equ LDT_SCRA * 8 + 7
S_SCRB  equ LDT_SCRB * 8 + 7
EXC_SEL equ LDT_EXC * 8 + 7

; the DPMI real-mode call structure (DPMI 0.9, INT 31h 0300h)
R_EDI   equ 00h
R_ESI   equ 04h
R_EBP   equ 08h
R_RES   equ 0Ch
R_EBX   equ 10h
R_EDX   equ 14h
R_ECX   equ 18h
R_EAX   equ 1Ch
R_FL    equ 20h
R_ES    equ 22h
R_DS    equ 24h
R_FS    equ 26h
R_GS    equ 28h
R_IP    equ 2Ah
R_CS    equ 2Ch
R_SP    equ 2Eh
R_SS    equ 30h
R_SIZE  equ 32h

; the ring-0 gate frame (gate_entry: push ds, push es, pusha)
F_DI    equ 0
F_SI    equ 2
F_BP    equ 4
F_BX    equ 8
F_DX    equ 10
F_CX    equ 12
F_AX    equ 14
F_ES    equ 16
F_DS    equ 18
F_GS    equ 20              ; FS and GS on a 386, unused words on a 286
F_FS    equ 22
F_IP3   equ 24
F_CS3   equ 26
F_SP3   equ 28
F_SS3   equ 30
; the exception frame in exc_entry: the same registers, then the error
; code (0 if the exception has none) and the processor's frame
E_ERR   equ 24
E_IP    equ 26
E_CS    equ 28
E_FL    equ 30
E_SP    equ 32
E_SS    equ 34
; on the ring-3 stack at the gate call: [0] op, [2] arg, then for the
; interrupt stubs the interrupt frame IP, CS, FLAGS at [4], [6], [8]
K_OP    equ 0
K_ARG   equ 2
K_IP    equ 4
K_CS    equ 6
K_FL    equ 8

; r0_call3 context
C_AX    equ 0
C_BX    equ 2
C_CX    equ 4
C_DX    equ 6
C_SI    equ 8
C_DI    equ 10
C_BP    equ 12
C_DS    equ 14
C_ES    equ 16
C_IP    equ 18
C_CS    equ 20
C_SP    equ 22
C_SS    equ 24
C_FL    equ 26
C_SIZE  equ 28

; gate operations
OP_REFLECT      equ 0   ; arg = stub return address: reflect vector n
OP_INT31        equ 1
OP_INT21        equ 2
OP_EXCDEF       equ 3   ; arg = stub return address: default exception n
OP_INT10        equ 4
OP_INT15        equ 5

FL_CF   equ 0001h
FL_IF   equ 0200h
FL_TF   equ 0100h
FL_DF   equ 0400h
FL_IOPL equ 3000h
FL_NT   equ 4000h
FL_ARITH equ 08D5h          ; OF SF ZF AF PF CF

; stacks and buffers (bytes)
HS_SIZE         equ 1800h   ; ring 0 and real-mode host stack
H3_SIZE         equ 1800h   ; ring-3 host stack (callbacks, exceptions)
H3_CHUNK        equ 400h    ; one nesting level of it
RM_RESERVE      equ 600h    ; real-mode code's room on the host stack
TB_SIZE         equ 1000h   ; real-mode transfer buffer
INIT_STACK      equ 200h
CB_ENTRIES      equ 16      ; real-mode entry points for callbacks
CB_STATIC       equ 16      ; callback records in DOSX's own data

; arena block header (16 bytes before each block, in extended memory)
A_SIZE  equ 0           ; dword: data bytes
A_PREV  equ 4           ; dword: previous header's linear address, 0 first
A_FLAGS equ 8           ; word: bit 0 = in use
A_SEL   equ 0Ah         ; word: first selector (INT 21h AH=48h blocks)
A_NSEL  equ 0Ch         ; word: number of selectors
A_MAGIC equ 0Eh         ; word: 'DX'
A_HDR   equ 16
MAGIC   equ 5844h

; DBG pos, 'c': a character on the text screen, real mode (debugging)
%macro DBG 2
        push es
        push ax
        mov ax, 0B800h
        mov es, ax
        mov byte [es:%1*2], %2
        mov byte [es:%1*2+1], 4Fh
        pop ax
        pop es
%endmacro
