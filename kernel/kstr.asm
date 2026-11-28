; kstr.asm - lstrcpy, lstrcat, lstrlen and lstrcpyn (KERNEL.88-90, 353)
; in assembly, with no more of the caller's stack than Windows 3.1's.
;
; Observed on Windows 3.1 (TK84, tests/apps/tk84.c, Standard and 386
; Enhanced mode): below its return address lstrcpy and lstrcat write 6
; bytes of the caller's stack, lstrlen 2, lstrcpyn 8. Programs rely on
; the rest staying as it was: Adobe Type Manager 3.0's installer loads
; an entry's name with LoadString into a buffer 22h bytes below its
; stack pointer (a returned function's) and then copies it with
; lstrcpy; w16's lstrcpy, C behind the __saveregs prologue and its
; checks, used 4Ah bytes and copied stack garbage, and the installer
; said "The Installer Configuration file is missing the following
; entry: ':*'". These use no more stack than 3.1's: the registers the
; callers keep (BX, CX, ES besides SI, DI, BP and DS: TK24, TK31;
; lstrcpyn ES not) are kept in the argument words once those are read.
;
; Behaviour otherwise as the C versions had it (TK4, TK39, US27):
;   - lstrcpy(d, s) and lstrcat(d, s) return d; NULL d gives 0; a NULL s
;     reads as "" (3.1 reads a scratch string there: not copied); a
;     selector that is not valid, a string with no NUL before its
;     segment's end, a destination too small, or a copy that overwrote
;     its own source give 0 (3.1 faults and recovers through its "__GP"
;     table); w16 checks first and writes nothing then, except that a
;     copy over its own source has written what it copied
;   - lstrlen(s): the length, 0 for NULL or a bad string
;   - lstrcpyn(d, s, n): at most n - 1 characters and a NUL; n = 0
;     returns d untouched; where 3.1 would fault, what was copied stays
;     and the result is 0
; The highest offset a selector reaches is its limit (FFFFh above 64K,
; and for an expand-down data segment: TK62); on a 386 the 32-bit LSL
; gives a limit above 64K (str386, set by str_init in selector.c).
        bits 16
        cpu 286                         ; (KRNL286 runs on an 80286: no 0F 8x jumps)

segment _TEXT public align=16 class=CODE use16

global LSTRCPY, LSTRCAT, LSTRLEN, LSTRCPYN
global _str386

; 1 on a 386 or later (str_init, selector.c, writes it through an alias)
_str386: db 0

; TOP SEL, WRITE, FAIL: AX = the highest offset SEL (a register other than
; AX and DX) can be read at (WRITE 0) or written at (WRITE 1), else jump to
; FAIL. Changes AX and DX (EAX and EDX on a 386) and the flags.
%macro TOP 3
        lar ax, %1
        jnz %3
        test ah, 80h                    ; present
        jz %3
        test ah, 10h                    ; code or data, not a system one
        jz %3
        test ah, 08h
        jz %%data
%if %2
        jmp %3                          ; code is never writable
%else
        test ah, 02h                    ; readable code
        jz %3
        jmp %%lim
%endif
%%data:
%if %2
        test ah, 02h                    ; writable data
        jz %3
%endif
        and ah, 0Ch
        cmp ah, 04h                     ; expand-down: every offset
        jne %%lim
        mov ax, 0FFFFh
        jmp %%done
%%lim:
        cmp byte [cs:_str386], 0
        je %%l16
        cpu 386
        movzx edx, %1
        lsl eax, edx
        cpu 286
        jnz %3
        cpu 386
        cmp eax, 0FFFFh
        cpu 286
        jbe %%done
        mov ax, 0FFFFh
        jmp %%done
%%l16:
        lsl ax, %1
        jnz %3
%%done:
%endmacro

; ---- lstrcpy(d, s): far pascal; [bp+6] s, [bp+10] d ----
LSTRCPY:
        push bp
        mov bp, sp
        push ds
        push si
        ; the pointers to registers, the caller's ES, DI, CX, BX to the
        ; argument words: s = CX:SI, d = BX:DI
        mov si, [bp+6]
        mov ax, [bp+8]
        mov [bp+6], es
        mov [bp+8], di
        mov di, [bp+10]
        mov dx, [bp+12]
        mov [bp+10], cx
        mov [bp+12], bx
        mov cx, ax
        mov bx, dx
        or dx, di
        jz .zero                        ; d NULL
        mov ax, cx
        or ax, si
        jnz .src
        ; s NULL: an empty string, if d has a byte to write
        TOP bx, 1, .zero
        cmp di, ax
        ja .zero
        mov es, bx
        mov byte [es:di], 0
        jmp .retd
.src:   TOP cx, 0, .zero
        cmp si, ax
        ja .zero
        mov ds, cx
        mov dx, si
.scan:  cmp byte [si], 0
        je .found
        cmp si, ax
        je .zero                        ; no NUL before the segment's end
        inc si
        jmp .scan
.found: sub si, dx                      ; n
        cmp si, 0FFFFh
        je .zero
        mov cx, si
        mov si, dx
        TOP bx, 1, .zero
        mov dx, di
        add dx, cx
        jc .zero
        cmp dx, ax
        ja .zero                        ; d + n past the segment's end
        mov es, bx
        mov dx, di
        inc cx
        cld
        rep movsb
        cmp byte [es:di-1], 0
        jne .zero                       ; the copy overwrote its source
        mov ax, dx
        mov dx, bx
        jmp .out
.retd:  mov ax, di
        mov dx, bx
        jmp .out
.zero:  xor ax, ax
        xor dx, dx
.out:   mov bx, [bp+12]
        mov cx, [bp+10]
        mov di, [bp+8]
        mov es, [bp+6]
        pop si
        pop ds
        pop bp
        retf 8

; DSCAN FAIL: d = BX:DI, a string to read: ES = its selector, BX its
; offset, DI its NUL, AX the highest offset readable (FAIL: not a string)
%macro DSCAN 1
        TOP bx, 0, %1
        cmp di, ax
        ja %1
        mov es, bx
        mov bx, di
%%scan: cmp byte [es:di], 0
        je %%found
        cmp di, ax
        je %1
        inc di
        jmp %%scan
%%found:
%endmacro

; ---- lstrcat(d, s): far pascal; [bp+6] s, [bp+10] d ----
LSTRCAT:
        push bp
        mov bp, sp
        push ds
        push si
        mov si, [bp+6]
        mov ax, [bp+8]
        mov [bp+6], es
        mov [bp+8], di
        mov di, [bp+10]
        mov dx, [bp+12]
        mov [bp+10], cx
        mov [bp+12], bx
        mov cx, ax
        mov bx, dx
        or dx, di
        jz .zero                        ; d NULL
        mov ax, cx
        or ax, si
        jnz .src
        ; s NULL: d is returned if it is a string
        DSCAN .zero
        jmp .retd
.src:   TOP cx, 0, .zero
        cmp si, ax
        ja .zero
        mov ds, cx
        mov dx, si
.scan:  cmp byte [si], 0
        je .found
        cmp si, ax
        je .zero
        inc si
        jmp .scan
.found: sub si, dx
        mov cx, si                      ; sn
        mov si, dx
        DSCAN .zero                     ; dn = DI - BX
        mov dx, es
        verw dx
        jnz .zero                       ; d not writable
        mov dx, di
        add dx, cx
        jc .zero
        cmp dx, ax
        ja .zero                        ; d + dn + sn past the segment's end
        sub dx, bx
        cmp dx, 0FFFFh
        je .zero                        ; dn + sn + 1 over 64K
        inc cx
        cld
        rep movsb
        cmp byte [es:di-1], 0
        jne .zero                       ; the copy overwrote its source
.retd:  mov ax, bx
        mov dx, es
        jmp .out
.zero:  xor ax, ax
        xor dx, dx
.out:   mov bx, [bp+12]
        mov cx, [bp+10]
        mov di, [bp+8]
        mov es, [bp+6]
        pop si
        pop ds
        pop bp
        retf 8

; ---- lstrlen(s): far pascal; [bp+6] s. Keeps everything but AX, DX ----
LSTRLEN:
        push bp
        mov bp, sp
        xchg bx, [bp+8]                 ; BX = s's selector, the slot BX
        mov ax, bx
        or ax, [bp+6]
        jz .zero                        ; NULL
        TOP bx, 0, .zero
        cmp [bp+6], ax
        ja .zero
        mov dx, es                      ; the caller's ES, in DX meanwhile
        mov es, bx
        mov bx, [bp+6]
.scan:  cmp byte [es:bx], 0
        je .found
        cmp bx, ax
        je .none
        inc bx
        jmp .scan
.found: sub bx, [bp+6]
        mov ax, bx
        mov es, dx
        jmp .out
.none:  mov es, dx
.zero:  xor ax, ax
.out:   mov bx, [bp+8]
        pop bp
        retf 4

; ---- lstrcpyn(d, s, n): far pascal; [bp+6] n, [bp+8] s, [bp+12] d.
; Keeps BX and CX (not ES) ----
LSTRCPYN:
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov si, [bp+8]
        mov ax, [bp+10]
        mov [bp+8], bx                  ; the caller's BX and CX
        mov [bp+10], cx
        mov di, [bp+12]
        mov cx, [bp+6]
        test cx, cx
        jz .retd                        ; n = 0: d, nothing checked
        dec cx                          ; m = n - 1 characters at most
        ; how many bytes of s can be read (at most FFFFh; 0: none)
        mov bx, ax
        TOP bx, 0, .rd0
        cmp si, ax
        ja .rd0
        sub ax, si
        cmp ax, 0FFFFh
        je .rdok
        inc ax
        jmp .rdok
.rd0:   xor ax, ax
.rdok:  test ax, ax
        jz .rd1
        mov ds, bx
.rd1:   cmp ax, cx
        jae .wr
        mov cx, ax                      ; lim = min(m, rd, wr)
.wr:    mov bx, [bp+14]
        TOP bx, 1, .wr0
        cmp di, ax
        ja .wr0
        sub ax, di
        cmp ax, 0FFFFh
        je .wrok
        inc ax
        jmp .wrok
.wr0:   xor ax, ax
.wrok:  mov dx, ax                      ; wr
        test ax, ax
        jz .wr1
        mov es, bx
.wr1:   cmp dx, cx
        jae .lim
        mov cx, dx
.lim:   mov bx, cx                      ; lim
        test cx, cx
        jz .atlim
.copy:  mov al, [si]
        test al, al
        jz .nul
        mov [es:di], al
        inc si
        inc di
        loop .copy
.atlim: ; i = lim: m reached with room for the NUL, else 3.1 faulted
        mov ax, [bp+6]
        dec ax
        cmp bx, ax
        jne .zero
        cmp dx, ax
        jbe .zero
.nul:   mov byte [es:di], 0             ; (i < lim <= wr at a NUL)
.retd:  mov ax, [bp+12]
        mov dx, [bp+14]
        jmp .out
.zero:  xor ax, ax
        xor dx, dx
.out:   mov bx, [bp+8]
        mov cx, [bp+10]
        pop di
        pop si
        pop ds
        pop bp
        retf 10
