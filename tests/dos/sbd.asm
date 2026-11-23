; sbd.asm - SBD.COM: Sound Blaster DMA playback the way DOS programs do
; it, for the Virtual DMA Device's probes (tests/vdmadprobe.sh,
; ARCHITECTURE.md 12.8 VDMAD): run in a DOS box under Windows 3.1's
; WIN386 and w16's, with DOSBox-X's sound recorded. Black box: only what
; the card, the DMA controller's ports and the interrupt give back.
; Sources: Creative's Sound Blaster Series Hardware Programming Guide (DSP
; reset, 40h time constant, 41h output rate, 14h single-cycle and 48h/1Ch
; auto-initialised 8-bit output, B6h 16-bit auto-initialised output, DAh/
; D9h exit auto-init, D1h speaker on, E1h version, the interrupt
; acknowledge at base+0Eh/0Fh); the Intel 8237A data sheet and the IBM
; PC/AT technical reference (mask 0Ah/D4h, mode 0Bh/D6h, flip-flop 0Ch/
; D8h, status 08h/D0h, the channels' address and count ports, the page
; registers 87h 83h 81h 82h / 8Bh 89h 8Ah); the 8259A (EOI).
;
;   usage: SBD M IRQ DMA HDMA      (base 220h; one digit each)
;     M  s  8-bit single-cycle: 1 s of 700 Hz, then 1 s of 900 Hz, each
;           one transfer and one interrupt (11025 Hz)
;        a  8-bit auto-initialised: a buffer of two halves of 0.5 s
;           (11025 Hz), refilled after each interrupt; the halves play
;           500, 600, ..., 1200 Hz, then the DSP leaves auto-init
;        w  16-bit auto-initialised the same way (22050 Hz, mono, the
;           high DMA channel): 1300, 1400, ..., 2000 Hz
;        b  8-bit single-cycle, 0.75 s of 1500 Hz, from 8 KB of the
;           program's memory that are not physically contiguous within one
;           DMA page (found with VDS: Lock with no buffer and 64 KB
;           alignment fails there), so that the VDMAD must copy them into
;           its buffer; "SBD b at SSSS al=EE" (none: no such place)
;        r  no sound: channel DMA's and HDMA's registers programmed and
;           read back (address, count, status) as the VM sees them
;        v  no sound: the Virtual DMA Services (INT 4Bh, AH=81h) and the
;           BIOS data byte 40:7Bh's bit 5 that says they are there; one
;           line per call (registers back, the DDS)
; It prints, and appends to SBD.LOG in the current directory, its lines:
;   SBD M dsp=VVVV            the DSP's version (0000: no card)
;   irq N cnt=CCCC st=SS t=T  each interrupt: the channel's count read
;                             back, the status register, BIOS ticks
;   SBD M end irqs=N ticks=T  (timeout: an interrupt did not come in 3 s)
; The tones are triangle waves (wavtones.py finds their frequency).
; With the Virtual DMA Services (40:7Bh bit 5) the buffer of s, a and w
; is put where it is physically contiguous within one DMA page (VDS Lock,
; no buffer, 64 KB or 128 KB alignment), so that the transfer needs no
; copy whatever the box's memory is; else at the first 64 KB boundary
; above the program.
        bits 16
        cpu 386
        org 100h

BASE    equ 220h

start:  mov si, 81h
        call word_
        mov [mode], al
        call word_
        sub al, '0'
        mov [irq], al
        call word_
        sub al, '0'
        mov [dma8], al
        call word_
        sub al, '0'
        mov [dma16], al
        ; a 64 KB-aligned segment above the program for the buffer
        mov ax, cs
        add ax, 1000h + 0FFFh
        and ax, 0F000h
        mov [bufseg], ax
        add ax, 1000h
        cmp ax, [2]
        jbe .mem
        mov si, m_nomem
        call out_s
        jmp exit
.mem:   mov al, [mode]
        mov [l_mode], al
        mov [l_mode2], al
        call dsp_reset
        jc .nod
        mov al, 0E1h
        call dsp_w
        call dsp_r
        mov [dspver + 1], al
        call dsp_r
        mov [dspver], al
.nod:   mov ax, [dspver]
        mov di, l_ver
        call hex4
        mov si, l_start
        call out_s
        cmp byte [mode], 'r'
        je regs
        cmp byte [mode], 'v'
        je vds
        cmp word [dspver], 0
        je done
        call hook
        mov al, 0D1h
        call dsp_w
        cmp byte [mode], 'b'
        je bounce
        cmp byte [mode], 's'
        je single
        cmp byte [mode], 'a'
        je auto8
        cmp byte [mode], 'w'
        je auto16
        jmp done

; ---- s: two single-cycle transfers of 11025 bytes
single: mov cx, 11025
        mov dx, 14h
        call place
        mov al, 40h
        call dsp_w
        mov al, 165                     ; 256 - 1000000/11025
        call dsp_w
        mov word [freq], 700
        call one_block
        jc done
        mov word [freq], 900
        call one_block
        jmp done

one_block:
        mov word [rate], 11025
        mov cx, 11025
        xor di, di
        call fill8
        mov cx, 11025
        mov al, 48h                     ; single, read (to the card)
        call dma8_prog
        mov al, 14h
        call dsp_w
        mov ax, 11025 - 1
        call dsp_w16
        call wait_irq
        ret

; ---- a: 8-bit auto-init, halves of 5512 bytes
HALF8   equ 5512
auto8:  mov cx, 2 * HALF8
        mov dx, 14h
        call place
        mov word [rate], 11025
        mov al, 40h
        call dsp_w
        mov al, 165
        call dsp_w
        mov word [freq], 500
        mov word [fstep], 100
        mov word [half], HALF8
        mov cx, HALF8
        xor di, di
        call fill8
        add word [freq], 100
        mov cx, HALF8
        mov di, HALF8
        call fill8
        add word [freq], 100
        mov cx, 2 * HALF8
        mov al, 58h                     ; auto-init, read
        call dma8_prog
        mov al, 48h
        call dsp_w
        mov ax, HALF8 - 1
        call dsp_w16
        mov al, 1Ch
        call dsp_w
        mov byte [is16], 0
        call auto_loop
        jmp done

; ---- w: 16-bit auto-init, halves of 11025 samples
HALF16  equ 11025
auto16: mov cx, 4 * HALF16
        mov dx, 24h
        call place
        mov word [rate], 22050
        mov byte [is16], 1
        mov al, 41h
        call dsp_w
        mov al, 22050 >> 8
        call dsp_w
        mov al, 22050 & 0FFh
        call dsp_w
        mov word [freq], 1300
        mov word [half], HALF16
        mov cx, HALF16
        xor di, di
        call fill16
        add word [freq], 100
        mov cx, HALF16
        mov di, 2 * HALF16
        call fill16
        add word [freq], 100
        mov cx, 2 * HALF16              ; words
        mov al, 58h
        call dma16_prog
        mov al, 0B6h                    ; 16-bit, auto-init, FIFO
        call dsp_w
        mov al, 10h                     ; signed mono
        call dsp_w
        mov ax, HALF16 - 1
        call dsp_w16
        call auto_loop
        jmp done

; eight halves: after interrupt k, the half just played (k & 1) gets
; the tone of half k+2, silence after the eighth
auto_loop:
        xor bx, bx
.l:     push bx
        call wait_irq
        pop bx
        jc .r
        lea ax, [bx + 2]
        cmp ax, 8
        jb .t
        mov word [freq], 0
.t:     mov cx, [half]
        xor di, di
        test bl, 1
        jz .h0
        mov di, [half]
        cmp byte [is16], 0
        je .h0
        shl di, 1
.h0:    cmp byte [is16], 0
        jne .f16
        call fill8
        jmp .n
.f16:   call fill16
.n:     cmp word [freq], 0
        je .nf
        add word [freq], 100
.nf:    inc bx
        cmp bx, 8
        jb .l
        mov al, 0DAh                    ; exit 8-bit auto-init
        cmp byte [is16], 0
        je .x
        mov al, 0D9h
.x:     call dsp_w
        call wait_irq                   ; the last block
.r:     ret

; ---- r: registers programmed and read back
regs:   mov cx, 1678h + 1
        mov word [bufaddr], 1234h
        mov al, 49h - 1                 ; single, read (channel added)
        call dma8_prog_raw
        mov al, [dma8]
        call rdback8
        mov si, l_rb8
        call out_s
        mov cx, 0345h + 1
        mov al, 48h
        call dma16_prog_raw
        call rdback16
        mov si, l_rb16
        call out_s
        jmp done

; ---- v: the Virtual DMA Services
; vcall: INT 4Bh with AX, BX, CX, DX from vax..vdx, ES:DI the DDS; the
; line: the call, AX BX CX DX SI DI back, the carry, then the DDS (size,
; offset, segment, buffer ID, physical address)
vcall:  mov ax, [vax]
        mov di, l_vfn
        call hex4
        mov ax, [vax]
        mov bx, [vbx]
        mov cx, [vcx]
        mov dx, [vdx]
        push ds
        pop es
        mov di, dds
        xor si, si
        clc
        int 4Bh
        pushf
        push di
        push si
        push dx
        push cx
        push bx
        mov di, l_vax
        call hex4
        pop ax
        mov di, l_vbx
        call hex4
        pop ax
        mov di, l_vcx
        call hex4
        pop ax
        mov di, l_vdx
        call hex4
        pop ax
        mov di, l_vsi
        call hex4
        pop ax
        mov di, l_vdi
        call hex4
        pop ax
        and al, 1
        mov di, l_vcf
        call hex2
        mov si, dds
        mov di, l_vdds
        mov cx, 8
.d:     lodsw                           ; the DDS's 16 bytes, as words
        call hex4
        inc di
        loop .d
        mov si, l_vds
        call out_s
        ret

; vlock: the DDS for SIZE (CX) bytes at bufseg:SI
vdds:   mov word [dds], cx
        mov word [dds + 2], 0
        mov word [dds + 4], si
        mov word [dds + 6], 0
        mov ax, [bufseg]
        mov word [dds + 8], ax
        mov word [dds + 10], 0
        mov word [dds + 12], 0
        mov word [dds + 14], 0
        ret

vds:    push ds
        push 40h
        pop ds
        mov al, [7Bh]
        pop ds
        mov di, l_v7b
        call hex2
        mov si, l_v7bl
        call out_s
        mov word [vax], 8102h           ; version
        call vcall
        mov cx, 1000h                   ; lock: one page
        xor si, si
        call vdds
        mov word [vax], 8103h
        mov word [vdx], 0
        call vcall
        mov word [vax], 8104h           ; unlock it
        call vcall
        mov cx, 3000h                   ; lock: three pages, no buffer
        mov si, 800h
        call vdds
        mov word [vax], 8103h
        mov word [vdx], 4
        call vcall
        mov word [vax], 8104h
        mov word [vdx], 0
        call vcall
        mov cx, 3000h                   ; three pages, a buffer allowed, copied in
        mov si, 800h
        call vdds
        mov word [vax], 8103h
        mov word [vdx], 2
        call vcall
        mov word [vax], 8104h           ; unlock, copied out
        mov word [vdx], 2
        call vcall
        mov cx, 2000h                   ; 64 KB alignment across a boundary
        mov si, 0F000h
        call vdds
        mov word [vax], 8103h
        mov word [vdx], 14h             ; no buffer, 64 KB
        call vcall
        mov word [vax], 8104h
        mov word [vdx], 0
        call vcall
        mov cx, 1000h                   ; request the buffer, then release
        xor si, si
        call vdds
        mov word [vax], 8107h
        mov word [vdx], 0
        call vcall
        mov word [vax], 8107h           ; again
        call vcall
        mov word [vax], 8109h           ; copy into it (BX:CX = 0)
        mov word [vbx], 0
        mov word [vcx], 0
        call vcall
        mov word [vax], 810Ah           ; out of it
        call vcall
        mov word [vax], 8108h
        call vcall
        mov word [vax], 8108h           ; again
        call vcall
        mov word [vax], 810Bh           ; translation, channel 1
        mov word [vbx], 1
        call vcall
        mov word [vax], 810Ch
        call vcall
        mov word [vax], 810Ch
        call vcall
        mov word [vax], 8105h           ; scatter lock: 3 pages, 4 entries
        mov cx, 3000h
        mov si, 800h
        call vdds
        mov word [dds + 12], 4          ; entries available
        mov word [vdx], 0
        call vcall
        mov si, dds + 16                ; its table
        mov di, l_vtab
        mov cx, 16
.t:     lodsw
        call hex4
        inc di
        loop .t
        mov si, l_vtabl
        call out_s
        mov word [vax], 8106h
        call vcall
        mov word [vax], 8100h           ; an unknown function
        call vcall
        jmp done

; ---- placing the buffer with VDS: CX bytes, DX the alignment flag (10h
; 64 KB, 20h 128 KB); bufseg moves up 4 KB at a time (16 tries) to where
; a Lock with no buffer succeeds
place:  push ds
        push 40h
        pop ds
        test byte [7Bh], 20h
        pop ds
        jz .r
        mov [plsize], cx
        or dx, 4
        mov [plflags], dx
        mov bx, 16
.t:     push bx
        mov cx, [plsize]
        xor si, si
        call vdds
        mov ax, 8103h
        mov dx, [plflags]
        push ds
        pop es
        mov di, dds
        int 4Bh
        pop bx
        jnc .ok
        add word [bufseg], 100h
        dec bx
        jnz .t
        sub word [bufseg], 1000h        ; none: where it was
.r:     ret
.ok:    mov ax, 8104h
        xor dx, dx
        mov di, dds
        int 4Bh
        ret

; ---- b: a transfer through VDMAD's buffer
bounce: push ds
        push 40h
        pop ds
        test byte [7Bh], 20h
        pop ds
        jz .none
        mov bx, 64                      ; 64 places, 4 KB apart
.t:     push bx
        mov cx, 2000h
        xor si, si
        call vdds
        mov ax, 8103h
        mov dx, 14h                     ; no buffer, 64 KB
        push ds
        pop es
        mov di, dds
        int 4Bh
        pop bx
        jc .found
        mov ax, 8104h
        xor dx, dx
        mov di, dds
        int 4Bh
.n:     add word [bufseg], 100h
        mov ax, [bufseg]                ; not across a linear 64 KB boundary
        and ax, 0FFFh
        cmp ax, 0F00h
        jae .n
        dec bx
        jnz .t
.none:  mov si, l_bnone
        call out_s
        jmp done
.found: mov di, l_bal
        call hex2
        mov ax, [bufseg]
        mov di, l_bseg
        call hex4
        mov si, l_bat
        call out_s
        mov al, 40h
        call dsp_w
        mov al, 165
        call dsp_w
        mov word [rate], 11025
        mov word [freq], 1500
        mov cx, 2000h
        xor di, di
        call fill8
        mov cx, 2000h
        mov al, 48h
        call dma8_prog
        mov al, 14h
        call dsp_w
        mov ax, 2000h - 1
        call dsp_w16
        call wait_irq
        jmp done

; ---- the end: DSP reset, the vector back, the summary
done:   call unhook
        call dsp_reset
        mov al, [irqs]
        xor ah, ah
        mov di, l_irqs
        call hex4
        call ticks
        sub ax, [t0]
        mov di, l_ticks
        call hex4
        mov si, l_end
        call out_s
exit:   mov ax, 4C00h
        int 21h

; ---- the DMA controller
pagereg db 87h, 83h, 81h, 82h, 8Fh, 8Bh, 89h, 8Ah

; dma8_prog: AL the mode (channel added), CX bytes, the buffer at bufseg:0
dma8_prog:
        mov word [bufaddr], 0
dma8_prog_raw:
        push ax
        movzx bx, byte [dma8]
        mov al, bl
        or al, 4
        out 0Ah, al                     ; masked
        out 0Ch, al                     ; flip-flop
        pop ax
        or al, bl
        out 0Bh, al
        mov dx, bx
        shl dx, 1
        mov ax, [bufseg]
        shl ax, 4
        add ax, [bufaddr]
        out dx, al
        mov al, ah
        out dx, al
        inc dx
        mov ax, cx
        dec ax
        out dx, al
        mov al, ah
        out dx, al
        mov ax, [bufseg]
        shr ax, 12
        mov dl, [pagereg + bx]
        out dx, al
        mov al, bl
        out 0Ah, al                     ; unmasked
        ret

; dma16_prog: AL the mode, CX words
dma16_prog:
        mov word [bufaddr], 0
dma16_prog_raw:
        push ax
        movzx bx, byte [dma16]
        sub bx, 4
        mov al, bl
        or al, 4
        out 0D4h, al
        out 0D8h, al
        pop ax
        or al, bl
        out 0D6h, al
        mov dx, bx
        shl dx, 2
        add dx, 0C0h
        mov ax, [bufseg]                ; the word address: (phys >> 1) & FFFFh
        and ax, 1FFFh
        shl ax, 3
        add ax, [bufaddr]
        out dx, al
        mov al, ah
        out dx, al
        add dx, 2
        mov ax, cx
        dec ax
        out dx, al
        mov al, ah
        out dx, al
        mov ax, [bufseg]
        shr ax, 12
        and al, 0FEh
        mov dl, [pagereg + bx + 4]
        out dx, al
        mov al, bl
        out 0D4h, al
        ret

; rdback8: AL the channel: l_rb8's address, count, status
rdback8:
        movzx dx, al
        shl dx, 1
        out 0Ch, al
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        mov di, l_rb8a
        call hex4
        inc dx
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        mov di, l_rb8c
        call hex4
        in al, 08h
        mov di, l_rb8s
        call hex2
        ret

rdback16:
        movzx dx, byte [dma16]
        sub dx, 4
        shl dx, 2
        add dx, 0C0h
        out 0D8h, al
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        mov di, l_rb16a
        call hex4
        add dx, 2
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        mov di, l_rb16c
        call hex4
        in al, 0D0h
        mov di, l_rb16s
        call hex2
        ret

; the channel's count now (AX) and the status register (BL)
count_now:
        cmp byte [is16], 0
        jne .w
        movzx dx, byte [dma8]
        shl dx, 1
        inc dx
        out 0Ch, al
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        push ax
        in al, 08h
        mov bl, al
        pop ax
        ret
.w:     movzx dx, byte [dma16]
        sub dx, 4
        shl dx, 2
        add dx, 0C2h
        out 0D8h, al
        in al, dx
        mov ah, al
        in al, dx
        xchg al, ah
        push ax
        in al, 0D0h
        mov bl, al
        pop ax
        ret

; ---- the tone: CX samples from bufseg:DI at [freq] Hz (0: silence),
; a triangle wave; the phase in [phase] (16-bit, 65536 a period)
inc_of:                                 ; EAX = freq * 65536 / rate
        movzx eax, word [freq]
        shl eax, 16
        movzx ebx, word [rate]
        xor edx, edx
        div ebx
        ret
tri:                                    ; AX = the sample (-128..126) for [phase]
        mov ax, [phase]
        mov al, ah
        xor ah, ah
        cmp al, 128
        jb .up
        not al
.up:    shl ax, 1
        sub ax, 128
        ret
fill8:  push bx
        push es
        mov es, [bufseg]
        call inc_of
        mov bp, ax
.l:     cmp word [freq], 0
        jne .t
        mov al, 80h
        jmp .s
.t:     call tri
        sar ax, 1
        add al, 80h
.s:     mov [es:di], al
        inc di
        add [phase], bp
        loop .l
        pop es
        pop bx
        ret
fill16: push bx
        push es
        mov es, [bufseg]
        call inc_of
        mov bp, ax
.l:     xor ax, ax
        cmp word [freq], 0
        je .s
        call tri
        shl ax, 7
.s:     mov [es:di], ax
        add di, 2
        add [phase], bp
        loop .l
        pop es
        pop bx
        ret

; ---- the DSP
dsp_reset:                              ; CF if no DSP
        mov dx, BASE + 6
        mov al, 1
        out dx, al
        call ticks
        mov bx, ax
.d:     in al, dx                       ; a few microseconds
        call ticks
        cmp ax, bx
        je .d
        mov al, 0
        out dx, al
        call ticks
        mov bx, ax
        mov dx, BASE + 0Eh
.w:     in al, dx
        test al, 80h
        jnz .r
        call ticks
        sub ax, bx
        cmp ax, 4
        jb .w
        stc
        ret
.r:     mov dx, BASE + 0Ah
        in al, dx
        cmp al, 0AAh
        jne .f
        clc
        ret
.f:     stc
        ret
dsp_w:  push dx
        push ax
        mov dx, BASE + 0Ch
.w:     in al, dx
        test al, 80h
        jnz .w
        pop ax
        out dx, al
        pop dx
        ret
dsp_w16:
        push ax
        call dsp_w
        pop ax
        mov al, ah
        jmp dsp_w
dsp_r:  push dx
        mov dx, BASE + 0Eh
        mov cx, 0
.w:     in al, dx
        test al, 80h
        jnz .r
        loop .w
.r:     mov dx, BASE + 0Ah
        in al, dx
        pop dx
        ret

; ---- the interrupt
hook:   call ticks
        mov [t0], ax
        mov al, [irq]
        add al, 8
        cmp byte [irq], 8
        jb .v
        add al, 70h - 16
.v:     mov [vec], al
        mov ah, 35h
        int 21h
        mov [oldvec], bx
        mov [oldvec + 2], es
        mov dx, isr
        mov al, [vec]
        mov ah, 25h
        int 21h
        mov cl, [irq]
        cmp cl, 8
        jae .s
        mov ah, 1
        shl ah, cl
        not ah
        in al, 21h
        and al, ah
        out 21h, al
        jmp .d
.s:     sub cl, 8
        mov ah, 1
        shl ah, cl
        not ah
        in al, 0A1h
        and al, ah
        out 0A1h, al
        in al, 21h
        and al, 0FBh
        out 21h, al
.d:     mov byte [hooked], 1
        sti
        ret
unhook: cmp byte [hooked], 0
        je .r
        push ds
        mov al, [vec]
        lds dx, [oldvec]
        mov ah, 25h
        int 21h
        pop ds
        mov byte [hooked], 0
.r:     ret

isr:    push ax
        push dx
        mov dx, BASE + 0Eh              ; 8-bit acknowledge
        cmp byte [cs:is16], 0
        je .a
        inc dx
.a:     in al, dx
        inc byte [cs:irqs]
        mov al, 20h
        cmp byte [cs:irq], 8
        jb .m
        out 0A0h, al
.m:     out 20h, al
        pop dx
        pop ax
        iret

; wait for one more interrupt (3 s at most: CF), then log it
wait_irq:
        call ticks
        mov bx, ax
        mov cl, [seen]
.w:     sti
        cmp [irqs], cl
        jne .got
        call ticks
        sub ax, bx
        cmp ax, 55
        jb .w
        mov si, l_tmo
        call out_s
        stc
        ret
.got:   inc byte [seen]
        call count_now
        mov di, l_cnt
        call hex4
        mov al, bl
        mov di, l_st
        call hex2
        mov al, [seen]
        mov di, l_n
        call hex2
        call ticks
        sub ax, [t0]
        mov di, l_t
        call hex4
        mov si, l_irq
        call out_s
        clc
        ret

ticks:  push ds
        push 40h
        pop ds
        mov ax, [6Ch]
        pop ds
        ret

; ---- text
word_:  lodsb                           ; AL: the next word's first letter
        cmp al, ' '
        je word_
        cmp al, 13
        je .e
        push ax
.s:     lodsb
        cmp al, ' '
        je .d
        cmp al, 13
        jne .s
.d:     dec si
        pop ax
        ret
.e:     dec si
        mov al, '0'
        ret
hex4:   push ax
        mov al, ah
        call hex2
        pop ax
hex2:   push ax
        shr al, 4
        call .d
        pop ax
.d:     and al, 15
        add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     mov [di], al
        inc di
        ret

; out_s: the $-ended line at SI to standard output and SBD.LOG
out_s:  push si
        mov dx, si
        mov ah, 9
        int 21h
        pop si
        mov di, si
.l:     cmp byte [di], '$'
        je .e
        inc di
        jmp .l
.e:     sub di, si
        mov cx, di
        push cx
        push si
        mov dx, logname
        mov ax, 3D01h
        int 21h
        jnc .o
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
.o:     mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        pop dx
        pop cx
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        ret

logname db 'SBD.LOG', 0
m_nomem db 'SBD: not enough memory', 13, 10, '$'
l_start db 'SBD '
l_mode  db '? dsp='
l_ver   db '0000', 13, 10, '$'
l_irq   db 'irq '
l_n     db '00 cnt='
l_cnt   db '0000 st='
l_st    db '00 t='
l_t     db '0000', 13, 10, '$'
l_tmo   db 'timeout', 13, 10, '$'
l_end   db 'SBD '
l_mode2 db '? end irqs='
l_irqs  db '0000 ticks='
l_ticks db '0000', 13, 10, '$'
l_rb8   db 'rb8 adr='
l_rb8a  db '0000 cnt='
l_rb8c  db '0000 st='
l_rb8s  db '00', 13, 10, '$'
l_rb16  db 'rb16 adr='
l_rb16a db '0000 cnt='
l_rb16c db '0000 st='
l_rb16s db '00', 13, 10, '$'
l_bat   db 'SBD b at '
l_bseg  db '0000 al='
l_bal   db '00', 13, 10, '$'
l_bnone db 'SBD b none', 13, 10, '$'
plsize  dw 0
plflags dw 0
l_v7bl  db '40:7B='
l_v7b   db '00', 13, 10, '$'
l_vds   db 'vds '
l_vfn   db '0000: ax='
l_vax   db '0000 bx='
l_vbx   db '0000 cx='
l_vcx   db '0000 dx='
l_vdx   db '0000 si='
l_vsi   db '0000 di='
l_vdi   db '0000 cf='
l_vcf   db '00 dds='
l_vdds  db '0000 0000 0000 0000 0000 0000 0000 0000', 13, 10, '$'
l_vtabl db '  table='
l_vtab  times 16 db '0000 '
        db 13, 10, '$'
vax     dw 0
vbx     dw 0
vcx     dw 0
vdx     dw 0
dds     times 16 + 64 db 0

mode    db 0
irq     db 5
dma8    db 1
dma16   db 5
is16    db 0
hooked  db 0
vec     db 0
irqs    db 0
seen    db 0
dspver  dw 0
bufseg  dw 0
bufaddr dw 0
rate    dw 11025
freq    dw 0
fstep   dw 0
half    dw 0
phase   dw 0
t0      dw 0
oldvec  dd 0
