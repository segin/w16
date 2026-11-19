; tk61s.asm - helpers of the freed-selector probe TK61 (tk61.c): calls with
; FS, GS and ES holding a selector, FS and GS read back, DPMI 0000h/0001h,
; and the dead-FS steps. All far cdecl, called with DS = DGROUP. FS and GS
; are touched only on a 386 or later (use386, tk61.c).

        bits 16
        cpu 386

segment _DATA public align=2 class=DATA use16
group DGROUP _DATA
extern _use386

segment _TEXT public align=16 class=CODE use16

; u16 call_regs(void far *fn, u16 arg, u16 sel, u16 far *out): calls the
; Pascal function fn(arg) with FS = GS = ES = sel (FS and GS on a 386
; only); out[0..2] = FS, GS, ES it came back with (FS and GS 0 on a 286);
; FS and GS are 0 afterwards. Returns AX.
global _call_regs
_call_regs:
        push bp
        mov bp, sp
        push es
        push si
        push di
        mov ax, [bp + 12]
        cmp word [_use386 wrt DGROUP], 0
        je .n1
        mov fs, ax
        mov gs, ax
.n1:    mov es, ax
        push word [bp + 10]
        call far [bp + 6]
        mov si, ax
        mov cx, es
        xor bx, bx
        xor dx, dx
        cmp word [_use386 wrt DGROUP], 0
        je .n2
        mov bx, fs
        mov dx, gs
        xor ax, ax
        mov fs, ax
        mov gs, ax
.n2:    les di, [bp + 14]
        mov [es:di], bx
        mov [es:di + 2], dx
        mov [es:di + 4], cx
        mov ax, si
        pop di
        pop si
        pop es
        pop bp
        retf

; u16 get_fs(void), get_gs(void): 0 on a 286
global _get_fs, _get_gs
_get_fs:
        xor ax, ax
        cmp word [_use386 wrt DGROUP], 0
        je .r
        mov ax, fs
.r:     retf
_get_gs:
        xor ax, ax
        cmp word [_use386 wrt DGROUP], 0
        je .r
        mov ax, gs
.r:     retf

; void zero_fsgs(void)
global _zero_fsgs
_zero_fsgs:
        cmp word [_use386 wrt DGROUP], 0
        je .r
        xor ax, ax
        mov fs, ax
        mov gs, ax
.r:     retf

; void set_fsgs(u16 fs, u16 gs)
global _set_fsgs
_set_fsgs:
        push bp
        mov bp, sp
        cmp word [_use386 wrt DGROUP], 0
        je .r
        mov fs, [bp + 6]
        mov gs, [bp + 8]
.r:     pop bp
        retf

; u16 dpmi_alloc1(void): INT 31h 0000h, CX = 1; the selector or 0
global _dpmi_alloc1
_dpmi_alloc1:
        xor ax, ax
        mov cx, 1
        int 31h
        jnc .r
        xor ax, ax
.r:     retf

; void dpmi_free1(u16 sel): INT 31h 0001h
global _dpmi_free1
_dpmi_free1:
        push bp
        mov bp, sp
        mov bx, [bp + 6]
        mov ax, 0001h
        int 31h
        pop bp
        retf

; u16 deadfs(u16 what, void far *fn, u16 h, u16 sel, u16 bios): FS = sel
; (live), then fn(h) (GlobalFree: sel dies), then with FS untouched:
; what 0 = wait until the BIOS tick count (bios:006Ch) has changed 18
; times, interrupts enabled; 1 = INT 21h AH=30h; 2 = INT 31h 0006h of DS.
; Returns the FS it then holds, and zeroes FS.
global _deadfs
_deadfs:
        push bp
        mov bp, sp
        push es
        push si
        mov fs, [bp + 14]
        push word [bp + 12]
        call far [bp + 8]
        mov si, [bp + 6]
        cmp si, 1
        je .i21
        cmp si, 2
        je .i31
        mov es, [bp + 16]
        mov cx, 18
        sti
.t:     mov dx, [es:006Ch]
.w:     cmp dx, [es:006Ch]
        je .w
        loop .t
        jmp .done
.i21:   mov ah, 30h
        int 21h
        jmp .done
.i31:   mov ax, 0006h
        mov bx, ds
        int 31h
.done:  mov ax, fs
        xor dx, dx
        mov fs, dx
        pop si
        pop es
        pop bp
        retf

; void mark(u16 n): INT 31h 0400h (get version: changes nothing) with
; SI = 6100h + n, a marker in w16's DOSXT trace (tools/dosxtrc.py)
global _mark
_mark:
        push bp
        mov bp, sp
        push si
        mov si, [bp + 6]
        add si, 6100h
        mov ax, 0400h
        int 31h
        pop si
        pop bp
        retf

; u16 vendor_api(void): 1 if the DOS extender gives the LDT's own
; selector, writeable, through INT 2Fh 168Ah "MS-DOS" and the entry's
; 0100h (Windows 3.1's DOSX and WIN386 and w16's do; HDPMI16 does not)
global _vendor_api
_vendor_api:
        push bp
        mov bp, sp
        push si
        push di
        push es
        mov si, msdos wrt DGROUP
        mov ax, 168Ah
        int 2Fh
        cmp al, 0
        jne .no
        push es
        push di
        mov ax, 0100h
        call far [bp - 10]
        lea sp, [bp - 6]
        jc .no
        verw ax
        jnz .no
        mov ax, 1
        jmp .r
.no:    xor ax, ax
.r:     pop es
        pop di
        pop si
        pop bp
        retf

segment _DATA
msdos   db "MS-DOS", 0

segment _TEXT
; u16 dpmi_version(void): INT 31h 0400h's AX (DOSX and WIN386: 005Ah)
global _dpmi_version
_dpmi_version:
        mov ax, 0400h
        int 31h
        retf

; u16 host_keeps_fs(void): 1 if INT 31h 0001h of a selector in FS leaves
; it there (Microsoft's DOSX and w16's; HDPMI16 zeroes FS, as DPMI 1.0
; has it); 386 only
global _host_keeps_fs
_host_keeps_fs:
        xor ax, ax
        mov cx, 1
        int 31h
        jc .no
        mov bx, ax
        mov fs, ax
        mov ax, 0001h
        int 31h
        mov ax, fs
        xor cx, cx
        mov fs, cx
        cmp ax, bx
        jne .no
        mov ax, 1
        retf
.no:    xor ax, ax
        retf
