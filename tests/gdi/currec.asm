; currec.asm - CURREC.DRV: a DISPLAY stand-in that counts (tests/gdi/
; currec.spec, tools/mkgdlog.py; tests/currec.sh). It forwards every call
; to the real driver (renamed VGAREAL, as for VGREC) unchanged; a "count"
; export's stub pushes its table index and ordinal and jumps to gdl_cnt,
; which tallies the call and, for SetCursor, MoveCursor and CheckCursor,
; records it:
;     ordinal, BIOS tick count (0040:006C, dword), PIT channel 0 count,
;     the caller's CS and IP, the first two argument words on the stack
;     (MoveCursor: y, x), FLAGS as the caller had them (IF: interrupts on),
;     the caller's SS
; (20 bytes) into a fixed buffer, until it is full. Then a RETF to the
; real entry with the caller's stack and every register as they were.
; Interrupt time: only this module's fixed data is touched, with
; interrupts off for the few instructions of the record.
; CURREC_INFO (ordinal 900) returns a far pointer to the header below;
; CURREC_RESET (901) empties the buffer and the tallies. Test use only: a
; real display driver has neither.

        bits 16
        cpu 286

%define GDLOG_MAIN
%include "gdlog.inc"

extern __0040H                  ; KERNEL.193
extern _gdl_count               ; the stubs' table (count of entries)
extern _gdl_tab

%define RECSIZE 20
%define MAXREC 2800
%define MAXENT 64

segment _NULL
        times 16 db 0

segment _DATA
global cr_hdr
cr_hdr:
cr_n    dw 0                    ; records in the buffer
        dw MAXREC
        dw RECSIZE
cr_nent dw 0                    ; table entries (from _gdl_count at load)
        dw cr_tally             ; offsets in this segment
        dw cr_buf
        dw _gdl_tab             ; the stubs' table: 14 bytes an entry,
                                ; the ordinal first

segment _BSS
cr_tally resd MAXENT
cr_buf  resb RECSIZE * MAXREC

segment _TEXT
; the logging stand-ins' common entry, not used here
global gdl_common
gdl_common:
        retf

; [bp+2] ordinal, [bp+4] table index, [bp+6] IP, [bp+8] CS, [bp+10]...
global gdl_cnt
gdl_cnt:
        push bp
        mov bp, sp
        pushf
        push ds
        push es
        push ax
        push bx
        push si
        cli
        mov ax, DGROUP
        mov ds, ax
        mov si, [bp + 4]
        cmp si, MAXENT
        jae .rec
        shl si, 2
        add word [cr_tally + si], 1
        adc word [cr_tally + si + 2], 0
.rec:   mov ax, [bp + 2]
        cmp ax, 102
        jb .go
        cmp ax, 104
        ja .go
        mov bx, [cr_n]
        cmp bx, MAXREC
        jae .go
        inc word [cr_n]
        imul bx, bx, RECSIZE
        add bx, cr_buf
        mov [bx], ax
        mov ax, __0040H
        mov es, ax
        mov ax, [es:6Ch]
        mov [bx + 2], ax
        mov ax, [es:6Eh]
        mov [bx + 4], ax
        mov al, 0                       ; latch channel 0
        out 43h, al
        in al, 40h
        mov ah, al
        in al, 40h
        xchg al, ah
        mov [bx + 6], ax
        mov ax, [bp + 8]
        mov [bx + 8], ax
        mov ax, [bp + 6]
        mov [bx + 10], ax
        mov ax, [bp + 10]
        mov [bx + 12], ax
        mov ax, [bp + 12]
        mov [bx + 14], ax
        mov ax, [bp - 2]
        mov [bx + 16], ax
        mov [bx + 18], ss
.go:    mov si, [bp + 4]
        shl si, 2
        mov ax, [cs:gdl_realtab + si]
        mov [bp + 2], ax
        mov ax, [cs:gdl_realtab + si + 2]
        mov [bp + 4], ax
        pop si
        pop bx
        pop ax
        pop es
        pop ds
        popf
        pop bp
        retf

; void far *CURREC_INFO(void)
global CURREC_INFO
CURREC_INFO:
        mov ax, cr_hdr
        mov dx, DGROUP
        retf

; void CURREC_RESET(void)
global CURREC_RESET
CURREC_RESET:
        push ds
        push di
        push es
        push cx
        pushf
        cli
        mov ax, DGROUP
        mov ds, ax
        mov es, ax
        mov word [cr_n], 0
        mov di, cr_tally
        mov cx, MAXENT * 2
        xor ax, ax
        cld
        rep stosw
        popf
        pop cx
        pop es
        pop di
        pop ds
        retf

; the DLL's entry point
global gdl_libentry
gdl_libentry:
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov ax, [_gdl_count wrt DGROUP]
        mov [cr_nent], ax
        pop ds
        mov ax, 1
        retf

global WEP
WEP:
        mov ax, 1
        retf 2
