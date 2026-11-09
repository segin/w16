; dosx.asm - w16's DOSX.EXE: the Standard-mode DOS extender.
;
; A real-mode MZ program that WIN.COM/WSWAP start as SYSTEM\DOSX.EXE. It
; takes the extended memory from XMS, builds the descriptor tables,
; offers a DPMI 0.9 host for 16-bit clients (INT 2Fh 1687h) and starts
; KERNEL (KRNL386.EXE on a 386 or later if it exists, else KRNL286.EXE)
; as an ordinary real-mode program, which switches to protected mode
; itself. When KERNEL ends, DOSX releases everything and exits with
; KERNEL's exit code.
;
; Behaviour follows Windows 3.1's DOSX as observed black-box with the DX
; probes (tests/dos/dx*.asm, tests/dosxtest.sh; ARCHITECTURE.md 9.6),
; and the published specifications:
;   DPMI Specification 0.9 (INT 2Fh 1687h, INT 31h, the exception frame)
;   XMS Specification 2.0/3.0 (functions 00h, 05h-0Ch, 0Eh)
;   Intel 80286 and 80386 Programmer's Reference (descriptors, gates,
;     the 286 TSS, LMSW/CR0, LGDT/LIDT/LLDT/LTR)
;   IBM PC AT Technical Reference (CMOS shutdown byte 0Fh, code 0Ah:
;     resume through the pointer at 0040:0067 after a reset; reset by
;     the keyboard controller, command 0FEh to port 64h)
;
; Structure (one 64 KB segment, used three ways):
;   real mode     CS = DS = SS = the image segment
;   ring 0        CS = G_HC0, DS = SS = G_HD0 (both based at the image)
;   ring 3 (host) CS = G_HC3, DS = G_HD3, for the default interrupt
;                 handlers and other code a client may call or chain to
; The client runs at ring 3 with IOPL 3. Interrupts are disabled in all
; ring-0 code. Software interrupts 20h-FFh go straight from the IDT to
; the protected-mode handler (the client's, or one of the ring-3 stubs
; here); vectors 00h-1Fh go to ring-0 code that sorts exceptions from
; hardware interrupts (IRQ 0-7, the PIC is not reprogrammed, as with
; Microsoft's DOSX) and software interrupts. Ring-3 stubs enter ring 0
; through a call gate.

        bits 16
        cpu 286                 ; 386 instructions only in "cpu 386" blocks

%include "dosx/defs.inc"

; ---------------------------------------------------------------------------
; MZ header
        section .hdr start=0
mz:     db 'MZ'
        dw (img_file_len + 32) % 512
        dw (img_file_len + 32 + 511) / 512
        dw 0                    ; relocations
        dw 2                    ; header paragraphs
        dw (bss_len + 15) / 16  ; minimum extra paragraphs
        dw 0FFFFh               ; maximum
        dw 0                    ; SS (relative)
        dw stack_init           ; SP
        dw 0                    ; checksum
        dw start                ; IP
        dw 0                    ; CS (relative)
        dw 1Ch                  ; relocation table offset
        dw 0                    ; overlay
        times 32 - ($ - mz) db 0

        section .text follows=.hdr vstart=0
img_start:

%include "dosx/trace.inc"
%include "dosx/init.inc"
%include "dosx/switch.inc"
%include "dosx/ring0.inc"
%include "dosx/ldt.inc"
%include "dosx/mem.inc"
%include "dosx/int31.inc"
%include "dosx/int21.inc"
%include "dosx/int10.inc"
%include "dosx/intbios.inc"
%include "dosx/stubs.inc"
%include "dosx/data.inc"

img_file_end:
img_file_len equ img_file_end - img_start

        absolute img_file_end
bss_start:
%include "dosx/bss.inc"
bss_end:
bss_len equ bss_end - bss_start
