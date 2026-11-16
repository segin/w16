; tk17s.asm - descriptor helpers for the selector probe TK17 (tk17.c):
; the protected-mode instructions LAR, LSL, VERR and VERW, and DPMI 000Bh
; (get descriptor). All far cdecl, called with DS = DGROUP. The 32-bit
; forms are used when use386 (tk17.c) is set; on an 80286 (tests/refrun.sh
; REFRUN_CPU=286) the 16-bit ones.

        bits 16
        cpu 386

segment _DATA public align=2 class=DATA use16
group DGROUP _DATA
extern _use386

segment _TEXT public align=16 class=CODE use16

; u16 sel_ok(u16 sel): 1 if LAR accepts the selector (ZF), else 0
global _sel_ok
_sel_ok:
        push bp
        mov bp, sp
        xor ax, ax
        lar dx, [bp + 6]
        jnz .no
        inc ax
.no:
        pop bp
        retf

; u16 sel_ar(u16 sel): descriptor bytes 5 (low) and 6 (high, limit bits
; masked) as LAR (32-bit) returns them; 0 if LAR fails
global _sel_ar
_sel_ar:
        push bp
        mov bp, sp
        cmp word [_use386 wrt DGROUP], 0
        jne .w32
        xor ax, ax
        lar ax, [bp + 6]
        jz .ok16
        xor ax, ax
.ok16:
        mov al, ah
        xor ah, ah
        pop bp
        retf
.w32:
        xor eax, eax
        movzx edx, word [bp + 6]
        lar eax, edx
        jz .ok
        xor eax, eax
.ok:
        and eax, 00F0FF00h              ; as a 386 does (DOSBox does not mask)
        shr eax, 8
        pop bp
        retf

; u32 sel_lsl(u16 sel): 32-bit segment limit (LSL), 0 if LSL fails
global _sel_lsl
_sel_lsl:
        push bp
        mov bp, sp
        cmp word [_use386 wrt DGROUP], 0
        jne .w32
        xor ax, ax
        lsl ax, [bp + 6]
        jz .ok16
        xor ax, ax
.ok16:
        xor dx, dx
        pop bp
        retf
.w32:
        xor eax, eax
        movzx edx, word [bp + 6]
        lsl eax, edx
        jz .ok
        xor eax, eax
.ok:
        mov edx, eax
        shr edx, 16
        pop bp
        retf

; u16 sel_rw(u16 sel): bit 0 = VERR, bit 1 = VERW
global _sel_rw
_sel_rw:
        push bp
        mov bp, sp
        xor ax, ax
        verr word [bp + 6]
        jnz .w
        or al, 1
.w:
        verw word [bp + 6]
        jnz .done
        or al, 2
.done:
        pop bp
        retf

; u16 get_desc(u16 sel, u8 far *buf): DPMI 000Bh into buf (8 bytes);
; returns 1 on success
global _get_desc
_get_desc:
        push bp
        mov bp, sp
        push di
        push es
        mov bx, [bp + 6]
        les di, [bp + 8]
        mov ax, 000Bh
        int 31h
        mov ax, 0
        jc .done
        inc ax
.done:
        pop es
        pop di
        pop bp
        retf

; u16 cur_cs(void), cur_ds(void)
global _cur_cs, _cur_ds
_cur_cs:
        mov ax, cs
        retf
_cur_ds:
        mov ax, ds
        retf

; u16 ret_ax(void far *fn, u16 arg): call the Pascal void function fn(arg)
; and return what it left in AX (for APIs documented as void)
global _ret_ax
_ret_ax:
        push bp
        mov bp, sp
        push word [bp + 10]
        xor ax, ax
        call far [bp + 6]
        pop bp
        retf

; u16 call_es(void far *fn, u16 arg): calls fn(arg) (one word, Pascal)
; with ES = arg; returns the ES it came back with
global _call_es
_call_es:
        push bp
        mov bp, sp
        push es
        push word [bp + 10]
        mov es, [bp + 10]
        call far [bp + 6]
        mov ax, es
        pop es
        pop bp
        retf

; u16 call_with_ds(void far *fn, u16 arg): calls fn(arg) (one word, Pascal)
; with DS = arg; returns the DS it came back with
global _call_with_ds
_call_with_ds:
        push bp
        mov bp, sp
        push ds
        push word [bp + 10]
        mov ds, [bp + 10]
        call far [bp + 6]
        mov ax, ds
        pop ds
        pop bp
        retf
