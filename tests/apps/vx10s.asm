; vx10s.asm - VX10's spy (tests/apps/vx10.c): a protected-mode INT 2Fh
; handler that records every call (but 1680h/1689h, the idle calls) with
; the registers in and out, and hands out its own entry points for the
; VxD API entries (INT 2Fh 1684h) of the devices DOS boxes use, which
; record each call with the registers in and out and the first words at
; DS:SI and ES:DI, around the real entry. Black box: only
; registers and data the caller passes are recorded, never code bytes:
; a dump from a code segment is zeros (ARCHITECTURE.md 3).
;   void __cdecl __far vx10_setup(WORD bufsel, WORD apimask, WORD inonly)
;   void __cdecl __far vx10_hook(int on)
; The buffer (one 64 KB segment): +0 records taken, +2 slots used, +4
; device of each slot (8 words), +20 the real entry of each slot (8
; dwords), +56 return-area entries in use, the return area at F000h (see
; below), records of 128 bytes from +64:
;   w0 kind (1 INT 2Fh, 2 an API call), w1 device, w2 caller CS, w3 IP,
;   w4-11 in: ax bx cx dx si di ds es, w12-19 out: the same, w20 flags out,
;   w21 BIOS tick, w24-39 16 words at DS:SI in, w40-55 16 words at ES:DI
;   in, w56-63 8 words at ES:DI out (0 where the selector or limit did not
;   allow)

        bits 16
        cpu 386

MAXREC  equ 479
RETAREA equ 0F000h
NRET    equ 256
NSLOT   equ 8

segment _TEXT public align=16 class=CODE use16
global _vx10_setup, _vx10_hook

bufseg  dw 0
old2f   dw 0, 0
apimask dw 0                            ; vx10_setup's second argument: whose entries are
                                        ; replaced: 1 SHELL, 2 VMD, 4 VKD, 8 VDD, 10h others
inonly  dw 0                            ; vx10_setup's third: whose calls are recorded going in
                                        ; only (the return address left alone: with SHELL's
                                        ; changed, 3.1's DOS box started as an icon)

; ---- setup: the buffer's selector and the old INT 2Fh vector into this
; code segment, through a data alias ----
_vx10_setup:
        push bp
        mov bp, sp
        push es
        push bx
        push cx
        push dx
        mov ax, 0204h                   ; the old vector
        mov bl, 2Fh
        int 31h
        push cx
        push dx
        mov ax, 000Ah
        mov bx, cs
        int 31h
        pop dx
        pop cx
        jc .x
        mov es, ax
        mov [es:old2f], dx
        mov [es:old2f+2], cx
        mov ax, [bp+6]
        mov [es:bufseg], ax
        mov ax, [bp+8]
        mov [es:apimask], ax
        mov ax, [bp+10]
        mov [es:inonly], ax
        mov bx, es
        xor ax, ax
        mov es, ax
        mov ax, 0001h
        int 31h
.x:     pop dx
        pop cx
        pop bx
        pop es
        pop bp
        retf

_vx10_hook:
        push bp
        mov bp, sp
        push bx
        push cx
        push dx
        mov bl, 2Fh
        mov ax, 0205h
        cmp word [bp+6], 0
        je .off
        mov cx, cs
        mov dx, int2f
        int 31h
        jmp .x
.off:   mov cx, [cs:old2f+2]
        mov dx, [cs:old2f]
        int 31h
.x:     pop dx
        pop cx
        pop bx
        pop bp
        retf

; ---- the thunks: one per slot ----
%assign i 0
%rep NSLOT
thunk %+ i:
        push word i
        jmp api_common
%assign i i+1
%endrep
thunks:
%assign i 0
%rep NSLOT
        dw thunk %+ i
%assign i i+1
%endrep

; a record: BX its offset in FS's buffer, or 0 when full (uses AX)
alloc:
        mov bx, [fs:0]
        cmp bx, MAXREC
        jae .full
        inc word [fs:0]
        shl bx, 7
        add bx, 64
        ret
.full:  xor bx, bx
        ret

; CX words from DX:SI to FS:DI, zero where DX:SI cannot be read or is
; code (only data a caller passes is recorded, never code bytes:
; ARCHITECTURE.md 3) (uses AX, CX, SI, DI, GS)
dump:
        push cx
        verr dx
        jnz .zero
        lar ax, dx                      ; a code segment: never dumped (clean room)
        test ah, 08h
        jnz .zero
        push bx                         ; video memory (A0000h-BFFFFh): never read
        push cx                         ; (a read loads the VGA's latches under
        push dx                         ; the display driver: the DOS box then
        mov bx, dx                      ; started as an icon, not full screen)
        mov ax, 0006h
        int 31h
        mov ax, cx
        pop dx
        pop cx
        pop bx
        jc .zero
        cmp ax, 000Ah
        je .zero
        cmp ax, 000Bh
        je .zero
        movzx eax, si
        add ax, cx
        add ax, cx                      ; the last byte + 1
        jc .zero
        dec ax
        push ebx
        movzx ebx, dx
        lsl ebx, ebx
        cmp eax, ebx
        pop ebx
        ja .zero
        mov gs, dx
        pop cx
.l:     mov ax, [gs:si]
        mov [fs:di], ax
        add si, 2
        add di, 2
        loop .l
        ret
.zero:  pop cx
.z:     mov word [fs:di], 0
        add di, 2
        loop .z
        ret

; The real handler or entry runs on the caller's own stack, exactly as
; the caller left it (a device may read the caller's stack): the return
; address there is changed to VX10's (.back), the original kept in the
; buffer's return area with the caller's SS:SP, and found again by SS:SP
; when the call returns (calls of several tasks may interleave).

; the frame the entry code builds: bp+0 the caller's BP, then (API) +2
; the slot, +4 IP, +6 CS; (INT) +2 IP, +4 CS, +6 flags
F_FS    equ -2
F_DS    equ -4
F_ES    equ -6
F_AX    equ -8
F_BX    equ -10
F_CX    equ -12
F_DX    equ -14
F_SI    equ -16
F_DI    equ -18
F_REC   equ -20
F_OBP   equ -22
; the frame at .back: the out registers (B_*), then the caller's return
; frame from B_RET (its IP, CS, for INT flags)
B_DI    equ 0
B_SI    equ 2
B_DX    equ 4
B_CX    equ 6
B_BX    equ 8
B_AX    equ 10
B_ES    equ 12
B_DS    equ 14
B_FS    equ 16
B_BP    equ 18
B_FL    equ 20
B_RET   equ 22

; the in part of a record (BX the record; kind; CS:IP at [bp+cs_off], [bp+ip_off])
%macro record_in 3
        mov word [fs:bx], %1
        mov ax, [bp+%2]
        mov [fs:bx+4], ax
        mov ax, [bp+%3]
        mov [fs:bx+6], ax
        mov ax, [bp+F_AX]
        mov [fs:bx+8], ax
        mov ax, [bp+F_BX]
        mov [fs:bx+10], ax
        mov ax, [bp+F_CX]
        mov [fs:bx+12], ax
        mov ax, [bp+F_DX]
        mov [fs:bx+14], ax
        mov ax, [bp+F_SI]
        mov [fs:bx+16], ax
        mov ax, [bp+F_DI]
        mov [fs:bx+18], ax
        mov ax, [bp+F_DS]
        mov [fs:bx+20], ax
        mov ax, [bp+F_ES]
        mov [fs:bx+22], ax
        push ds
        mov ax, 40h
        mov ds, ax
        mov ax, [6Ch]
        pop ds
        mov [fs:bx+42], ax
        mov dx, [bp+F_DS]
        mov si, [bp+F_SI]
        lea di, [bx+48]
        mov cx, 16
        call dump
        mov dx, [bp+F_ES]
        mov si, [bp+F_DI]
        lea di, [bx+80]
        mov cx, 16
        call dump
%endmacro

%macro save_in 0
        push fs
        push ds
        push es
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        sub sp, 4
%endmacro

; the caller's return address at [bp+%1] (its SP there) into the return
; area, replaced by %2; BX the record (uses AX, SI)
%macro track 2
        mov si, [fs:56]
        cmp si, NRET
        jae %%no
        inc word [fs:56]
        shl si, 4
        add si, RETAREA
        mov ax, [bp+%1]
        mov [fs:si], ax
        mov ax, [bp+%1+2]
        mov [fs:si+2], ax
        mov [fs:si+4], bx
        mov [fs:si+6], ss
        lea ax, [bp+%1]
        mov [fs:si+8], ax
        mov word [bp+%1], %2
        mov [bp+%1+2], cs
%%no:
%endmacro

; at the return: %1 the bytes of the caller's return frame (4 API, 6 INT)
%macro save_out 1
        sub sp, %1
        pushf
        push bp
        push fs
        push ds
        push es
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        mov bp, sp
        mov fs, [cs:bufseg]
        call find
%endmacro

%macro record_out 0
        mov ax, [bp+B_AX]
        mov [fs:bx+24], ax
        mov ax, [bp+B_BX]
        mov [fs:bx+26], ax
        mov ax, [bp+B_CX]
        mov [fs:bx+28], ax
        mov ax, [bp+B_DX]
        mov [fs:bx+30], ax
        mov ax, [bp+B_SI]
        mov [fs:bx+32], ax
        mov ax, [bp+B_DI]
        mov [fs:bx+34], ax
        mov ax, [bp+B_DS]
        mov [fs:bx+36], ax
        mov ax, [bp+B_ES]
        mov [fs:bx+38], ax
        mov ax, [bp+B_FL]
        mov [fs:bx+40], ax
        mov dx, [bp+B_ES]
        mov si, [bp+B_DI]
        lea di, [bx+112]
        mov cx, 8
        call dump
%endmacro

%macro restore_out 0
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pop es
        pop ds
        pop fs
        pop bp
        popf
%endmacro

; the return-area entry of this return (SS and the caller's SP, BP+B_RET):
; BX its record (0: none); the caller's IP and CS back into the return
; frame; the entry freed (uses AX, CX, DX, SI)
find:
        mov cx, [fs:56]
        mov si, cx
        shl si, 4
        add si, RETAREA
        lea ax, [bp+B_RET]
.l:     jcxz .none
        sub si, 16
        dec cx
        mov dx, ss
        cmp [fs:si+6], dx
        jne .l
        cmp [fs:si+8], ax
        jne .l
        mov bx, [fs:si+4]
        mov ax, [fs:si]
        mov [bp+B_RET], ax
        mov ax, [fs:si+2]
        mov [bp+B_RET+2], ax
        mov word [fs:si+6], 0           ; free
.pop:   mov si, [fs:56]                 ; free entries off the top
        or si, si
        jz .x
        dec si
        shl si, 4
        cmp word [fs:si+RETAREA+6], 0
        jne .x
        dec word [fs:56]
        jmp .pop
.x:     ret
.none:  jmp .none                       ; not found: cannot happen (stops here)

; ---- an API entry: [sp] the slot, then the caller's IP, CS ----
api_common:
        push bp
        mov bp, sp
        save_in
        mov fs, [cs:bufseg]
        call alloc
        mov [bp+F_REC], bx
        test bx, bx
        jz .call
        mov si, [bp+2]
        shl si, 1
        mov ax, [fs:4+si]
        mov [fs:bx+2], ax
        record_in 2, 6, 4
.call:  mov bx, [bp+F_REC]
        mov si, [bp+2]                  ; the slot's device: in only?
        shl si, 1
        mov ax, [fs:4+si]
        mov cx, 1
        cmp ax, 0017h
        je .m
        mov cl, 2
        cmp ax, 000Ch
        je .m
        mov cl, 4
        cmp ax, 000Dh
        je .m
        mov cl, 8
        cmp ax, 000Ah
        je .m
        mov cl, 10h
.m:     test [cs:inonly], cx
        jnz .go
        track 4, api_back
.go:
        mov ax, [bp]
        mov [bp+F_OBP], ax
        mov si, [bp+2]
        shl si, 2
        mov ax, [fs:20+si]              ; the real entry where BP and the slot were
        mov [bp], ax
        mov ax, [fs:20+si+2]
        mov [bp+2], ax
        mov es, [bp+F_ES]
        mov ds, [bp+F_DS]
        mov fs, [bp+F_FS]
        mov ax, [bp+F_AX]
        mov bx, [bp+F_BX]
        mov cx, [bp+F_CX]
        mov dx, [bp+F_DX]
        mov si, [bp+F_SI]
        mov di, [bp+F_DI]
        mov sp, bp
        mov bp, [bp+F_OBP]
        retf

api_back:
        save_out 4
        test bx, bx
        jz .ret
        record_out
.ret:   restore_out
        retf

; ---- INT 2Fh ----
int2f:
        cmp ax, 1689h
        je .fast
        cmp ax, 1680h
        je .fast
        push bp
        mov bp, sp
        save_in
        mov fs, [cs:bufseg]
        call alloc
        mov [bp+F_REC], bx
        test bx, bx
        jz .call
        mov word [fs:bx+2], 0
        record_in 1, 4, 2
.call:  mov bx, [bp+F_REC]
        track 2, int_back
        mov ax, [bp]
        mov [bp+F_OBP], ax
        mov fs, [bp+F_FS]
        mov ax, [cs:old2f]              ; the old handler where FS and BP were
        mov [bp-2], ax
        mov ax, [cs:old2f+2]
        mov [bp], ax
        mov es, [bp+F_ES]
        mov ds, [bp+F_DS]
        mov ax, [bp+F_AX]
        mov bx, [bp+F_BX]
        mov cx, [bp+F_CX]
        mov dx, [bp+F_DX]
        mov si, [bp+F_SI]
        mov di, [bp+F_DI]
        lea sp, [bp-2]
        mov bp, [bp+F_OBP]
        retf
.fast:  jmp far [cs:old2f]

int_back:
        save_out 6
        mov ax, [bp+B_FL]               ; the caller gets the flags as they came out
        mov [bp+B_RET+4], ax
        test bx, bx
        jz .ret
        record_out
        ; 1684h with an entry for a device a DOS box uses: ours instead
        cmp word [fs:bx+8], 1684h
        jne .ret
        mov ax, [bp+B_ES]
        or ax, [bp+B_DI]
        jz .ret
        mov ax, [fs:bx+10]              ; BX in
        cmp ax, 0005h                   ; VTD, Reboot, VTDAPI: not these
        je .ret
        cmp ax, 0009h
        je .ret
        cmp ax, 0442h
        je .ret
        mov cx, 1
        cmp ax, 0017h
        je .mask
        mov cl, 2
        cmp ax, 000Ch
        je .mask
        mov cl, 4
        cmp ax, 000Dh
        je .mask
        mov cl, 8
        cmp ax, 000Ah
        je .mask
        mov cl, 10h
.mask:  test [cs:apimask], cx
        jz .ret
        xor si, si
.find:  cmp si, [fs:2]
        jae .new
        mov di, si
        shl di, 1
        cmp [fs:4+di], ax
        je .use
        inc si
        jmp .find
.new:   cmp si, NSLOT
        jae .ret
        inc word [fs:2]
        mov di, si
        shl di, 1
        mov [fs:4+di], ax
        shl di, 1
        mov ax, [bp+B_DI]
        mov [fs:20+di], ax
        mov ax, [bp+B_ES]
        mov [fs:20+di+2], ax
.use:   mov di, si
        shl di, 1
        mov ax, [cs:thunks+di]
        mov [bp+B_DI], ax
        mov [bp+B_ES], cs
.ret:   restore_out
        iret
