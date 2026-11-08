; tk10fp.asm - the fixup sites of TK10 (tests/apps/tk10.c), rewritten
; after linking by tools/nechain.py: four 8-byte sites after the marker
; "CHN!", A-C with an FIDRQQ fixup, D without (it gets B's record).

        bits 16

extern FIDRQQ

segment CHNSEG public align=16 class=CODE use16
global _chn_sites
        db "CHN!"
_chn_sites:
        dw 0D99Bh + FIDRQQ              ; A
        db 0C0h
        times 5 nop
        dw 0D99Bh + FIDRQQ              ; B
        db 0C0h
        times 5 nop
        dw 0D99Bh + FIDRQQ              ; C
        db 0C0h
        times 5 nop
        db 9Bh, 0D9h, 0C0h              ; D
        times 5 nop

; void FAR *chn_site(WORD i)
global _chn_site
_chn_site:
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        shl ax, 3
        add ax, _chn_sites
        mov dx, cs
        pop bp
        retf
