; gr3.asm - GR3: a recording stand-in for the 386 Enhanced-mode display
; grabber (VGA.3GR), for observing how WINOLDAP (WINOA386.MOD) uses the
; grabber and how the grabber uses the VDD (tests/grabber3.sh,
; docs/notes/grabber-handover.md "VGA.3GR").
;
; Installed as SYSTEM\VGA.3GR: module GRABBER with the grabber's 24 export
; names and ordinals. It imports module GRABTGT: a scratch copy of the
; grabber under test (Microsoft's VGA.3GR or w16's) whose NE module name
; is changed to GRABTGT by tools/nerename.py at test time (header tables
; only; SYSTEM\GRABTGT.DLL). Nothing of it is ever read or copied here.
;
; Every call of an export is recorded and passed on to the same ordinal of
; GRABTGT with the caller's registers and a copy of the top 8 stack words;
; as many bytes as the real entry took off are taken off the caller's
; stack (as KBREC does, tests/apps/kbrec.asm). Recorded: the registers and
; stack words in, DX:AX/flags out, the bytes taken off, and the
; EXTPAINTSTRUC the first argument points at (WINOLDAP's structure: values
; passed across the interface): in full the first time, afterwards the
; bytes that changed, before (changed by WINOLDAP) and after the call
; (changed by the grabber). GetFontList's buffer and SetPaintFnt's two
; result words are recorded after the call.
;
; The VDD's protected-mode API entry: LibEntry hooks protected-mode
; INT 2Fh (DPMI 0204h/0205h); a call AX=1684h BX=000Ah (VDD) gets the real
; entry from the previous handler and is answered with a thunk here, which
; records the registers (32-bit) in and out, and the buffer at ES:DI (up
; to the larger of CX in and CX out, 1000h bytes at most) before and
; after, then returns to the caller with the VDD's registers.
;
; Records go to a buffer in this DLL's fixed data segment and are appended
; to C:\GR3.LOG (DOS file I/O) when no grabber call is active and the DOS
; application is not locked (between GetDisplayUpd and the next
; PaintScreen, UpdateScreen or GrbUnLockApp the VM is held, and the DOS
; box may be inside DOS). tools/gr3dump.py decodes the log.
;
; Record: +0 type (byte), +1 ordinal (byte), +2 sequence, +4 length of the
; whole record, +6 BIOS tick (low word), then by type:
;   1 call:    AX BX CX DX SI DI BP DS ES flags, the 8 stack words above
;              the return address (w0 = last pushed), return IP, CS
;   2 return:  AX DX flags, bytes taken off
;   3 full:    when (0 before, 1 after), offset, selector, size, bytes
;   4 diff:    when, offset, selector, then runs (offset, length, bytes),
;              FFFFh
;   5 memory:  offset, selector, length, bytes
;   6 VDD in:  EAX EBX ECX EDX ESI EDI EBP (dwords), DS ES FS GS flags,
;              return IP, CS, length, the bytes at ES:DI
;   7 VDD out: the same registers after the call, length, the bytes at
;              the ES:DI of the call
;   8 int 2Fh: the real VDD entry offset, selector
;   9 lost:    records dropped (buffer full)
        bits 16
        cpu 386

LOGMAX  equ 0B000h
SMAX    equ 0800h                       ; EXTPAINTSTRUC bytes recorded
ROOM    equ 2600h                       ; the largest record

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern __0040H                          ; KERNEL.193
extern GETMODULEHANDLE                  ; KERNEL.47
extern GETPROCADDRESS                   ; KERNEL.50
extern ALLOCSELECTOR                    ; KERNEL.175
extern SETSELECTORBASE                  ; KERNEL.187
extern SETSELECTORLIMIT                 ; KERNEL.189

segment _DATA
logpos  dw 0
seq     dw 0
depth   dw 0
locked  dw 0
lost    dw 0
rstart  dw 0
old2f   dd 0
trrec   dd 0                            ; USTRACE's TRREC and TRFLUSH, when
trflush dd 0                            ; USTRACE is loaded
usname  db 'USTRACE', 0
mkframe times 33 dw 0                   ; a USTRACE frame for TRREC
retax   times 8 dw 0                    ; AX, DX, bytes taken off
vddreal dd 0
vddes   dw 0
vdddi   dw 0
vddcx   dw 0
vddord  dw 0
vddfn   dw 0                            ; AX of the VDD call
flatsel dw 0                            ; a selector for the VDD's memory
memdumps dw 0                           ; Get_Mem memory dumps so far
shoff   dw 0
shsel   dw 0
shsize  dw 0
fname   db 'C:\GR3.LOG', 0
shadow  times SMAX db 0
logbuf  times LOGMAX db 0

segment _TEXT

; ordinal -> argument bytes (the DDK's prototypes; UpdateScreen (12),
; GrbGetTextColor (21) and TranslateWinToFScreen (23) as observed)
argbytes:
        db 0, 4, 4, 8, 8, 8, 4, 10, 4, 4, 4, 12, 10, 4, 4, 4, 4, 8, 10
        db 10, 10, 8, 4, 4, 2

; LibEntry: the log file is created empty; INT 2Fh is hooked
global libentry
libentry:
        push ds
        push es
        push si
        push di
        mov ax, seg logpos
        mov ds, ax
        mov dx, fname
        mov ah, 3Ch
        xor cx, cx
        int 21h
        jc .nof
        mov bx, ax
        mov ah, 3Eh
        int 21h
.nof:   ; USTRACE (tests/user/ustrace.c), when the grabber under test's
        ; imports are traced too (tests/grabber3.sh -u): the grabber calls
        ; go into its records as module 9, ordinal n (n + 100: the return)
        push ds
        push word usname
        call far GETMODULEHANDLE
        or ax, ax
        jz .nous
        mov si, ax
        push si
        push word 0
        push word 1
        call far GETPROCADDRESS
        mov [trrec wrt DGROUP], ax
        mov [trrec + 2 wrt DGROUP], dx
        push si
        push word 0
        push word 3
        call far GETPROCADDRESS
        mov [trflush wrt DGROUP], ax
        mov [trflush + 2 wrt DGROUP], dx
.nous:  push ds
        call far ALLOCSELECTOR
        mov [flatsel wrt DGROUP], ax
        mov ax, 0204h
        mov bl, 2Fh
        int 31h
        mov [old2f wrt DGROUP], dx
        mov [old2f + 2 wrt DGROUP], cx
        mov ax, 0205h
        mov bl, 2Fh
        mov cx, cs
        mov dx, int2f
        int 31h
        pop di
        pop si
        pop es
        pop ds
        mov ax, 1
        retf

global WEP
WEP:
        push ds
        mov ax, seg logpos
        mov ds, ax
        mov ax, 0205h
        mov bl, 2Fh
        mov dx, [old2f wrt DGROUP]
        mov cx, [old2f + 2 wrt DGROUP]
        int 31h
        call flush
        pop ds
        mov ax, 1
        retf 2

; ---- the protected-mode INT 2Fh hook ----
int2f:
        cmp ax, 1684h
        jne .chain
        cmp bx, 000Ah
        je .vdd
.chain: sub sp, 4
        push bp
        mov bp, sp
        push ds
        push ax
        mov ax, seg logpos
        mov ds, ax
        mov ax, [old2f wrt DGROUP]
        mov [bp + 2], ax
        mov ax, [old2f + 2 wrt DGROUP]
        mov [bp + 4], ax
        pop ax
        pop ds
        pop bp
        retf                            ; on to the previous handler
.vdd:   pushf
        push cs
        push word .back
        jmp .chain
.back:  push ds
        push ax
        mov ax, seg logpos
        mov ds, ax
        mov ax, es
        or ax, di
        jz .none
        mov [vddreal wrt DGROUP], di
        mov [vddreal + 2 wrt DGROUP], es
        pushad
        push es
        push ds
        pop es
        mov ax, 0008h
        call rbegin
        jc .nr
        mov ax, [vddreal wrt DGROUP]
        stosw
        mov ax, [vddreal + 2 wrt DGROUP]
        stosw
        call rend
.nr:    pop es
        popad
        mov di, vddthunk
        push cs
        pop es
.none:  pop ax
        pop ds
        iret

; ---- the VDD entry thunk ----
; [bp+2] slot: the real entry's offset, [bp+4] its selector, [bp+6] .vback,
; [bp+8] CS, [bp+10] the caller's IP, [bp+12] CS
vddthunk:
        push cs
        push word vback
        sub sp, 4
        push bp
        mov bp, sp
        pushfd
        pushad
        push ds
        push es
        push fs
        push gs
        ; [bp-4] eflags, [bp-8] EAX, -12 ECX, -16 EDX, -20 EBX, -24 ESP,
        ; -28 EBP, -32 ESI, -36 EDI, -38 DS, -40 ES, -42 FS, -44 GS
        cld
        mov ax, seg logpos
        mov ds, ax
        mov ax, [vddreal wrt DGROUP]
        mov [bp + 2], ax
        mov ax, [vddreal + 2 wrt DGROUP]
        mov [bp + 4], ax
        inc word [depth wrt DGROUP]
        mov ax, [bp - 40]
        mov [vddes wrt DGROUP], ax
        mov ax, [bp - 36]
        mov [vdddi wrt DGROUP], ax
        mov ax, [bp - 12]
        mov [vddcx wrt DGROUP], ax
        mov ax, [bp - 8]
        mov [vddfn wrt DGROUP], ax
        push ds
        pop es
        mov ax, 0006h
        call rbegin
        jc .nr
        call vddregs
        mov ax, [bp + 10]
        stosw
        mov ax, [bp + 12]
        stosw
        mov cx, [vddcx wrt DGROUP]
        call vddbuf
        call rend
.nr:    pop gs
        pop fs
        pop es
        pop ds
        popad
        popfd
        pop bp
        retf                            ; to the VDD, back at vback
vback:
        push bp
        mov bp, sp
        pushfd
        pushad
        push ds
        push es
        push fs
        push gs
        cld
        mov ax, seg logpos
        mov ds, ax
        push ds
        pop es
        mov ax, 0007h
        call rbegin
        jc .nr
        call vddregs
        mov cx, [bp - 12]               ; CX out
        cmp cx, [vddcx wrt DGROUP]
        jae .big
        mov cx, [vddcx wrt DGROUP]
.big:   call vddbuf
        call rend
        ; Get_Mem (AX=1): the first times, the memory its first three
        ; addresses point at (the DOS box's video memory, as the VDD
        ; gives it), 1000h bytes each
        cmp word [vddfn wrt DGROUP], 1
        jne .nr
        cmp word [memdumps wrt DGROUP], 8
        jae .nr
        inc word [memdumps wrt DGROUP]
        xor si, si
.dm:    push si
        mov es, [vddes wrt DGROUP]
        mov bx, [vdddi wrt DGROUP]
        mov eax, [es:bx + si]
        push ds
        pop es
        or eax, eax
        jz .ndm
        push word [flatsel wrt DGROUP]
        push eax
        call far SETSELECTORBASE
        push word [flatsel wrt DGROUP]
        push word 0
        push word 0FFFFh
        call far SETSELECTORLIMIT
        mov dx, [flatsel wrt DGROUP]
        xor bx, bx
        mov cx, 1000h
        call memrec
.ndm:   pop si
        add si, 4
        cmp si, 12
        jb .dm
.nr:    dec word [depth wrt DGROUP]
        pop gs
        pop fs
        pop es
        pop ds
        popad
        popfd
        pop bp
        retf

; the frame's registers to ES:DI (the frame as built above)
vddregs:
        mov eax, [bp - 8]
        stosd
        mov eax, [bp - 20]
        stosd
        mov eax, [bp - 12]
        stosd
        mov eax, [bp - 16]
        stosd
        mov eax, [bp - 32]
        stosd
        mov eax, [bp - 36]
        stosd
        mov eax, [bp - 28]              ; EBP: the high half as pushed,
        mov ax, [bp]                    ; BP the caller's
        stosd
        mov ax, [bp - 38]
        stosw
        mov ax, [bp - 40]
        stosw
        mov ax, [bp - 42]
        stosw
        mov ax, [bp - 44]
        stosw
        mov ax, [bp - 4]
        stosw
        ret

; CX bytes (1000h at most) at the call's ES:DI to ES:DI: length, bytes
vddbuf:
        cmp cx, 1000h
        jbe .ok
        mov cx, 1000h
.ok:    mov ax, [vddes wrt DGROUP]
        mov bx, [vdddi wrt DGROUP]
        call readable
        mov ax, cx
        stosw
        jcxz .r
        push ds
        mov si, [vdddi wrt DGROUP]
        mov ds, [vddes wrt DGROUP]
        rep movsb
        pop ds
.r:     ret

; readable: AX selector, BX offset, CX wanted -> CX bytes readable there
readable:
        or ax, ax
        jz .bad
        verr ax
        jnz .bad
        push dx
        lsl dx, ax
        jnz .bad2
        cmp bx, dx
        ja .bad2
        sub dx, bx
        inc dx
        jz .all                         ; the whole 64 KB
        cmp cx, dx
        jbe .all
        mov cx, dx
.all:   pop dx
        ret
.bad2:  pop dx
.bad:   xor cx, cx
        ret

; ---- records ----
; rbegin: AL type, AH ordinal; DS = ES = DGROUP. CF set if there is no
; room (counted as lost); else DI points past the header.
rbegin:
        mov di, [logpos wrt DGROUP]
        cmp di, LOGMAX - ROOM
        jb .ok
        inc word [lost wrt DGROUP]
        stc
        ret
.ok:    mov [rstart wrt DGROUP], di
        add di, logbuf
        stosw
        inc word [seq wrt DGROUP]
        mov ax, [seq wrt DGROUP]
        stosw
        xor ax, ax
        stosw                           ; length, set by rend
        push ds
        mov ax, __0040H
        mov ds, ax
        mov ax, [6Ch]
        pop ds
        stosw
        clc
        ret
; rend: DI = the end of the record started by rbegin
rend:
        mov ax, di
        sub ax, logbuf
        mov bx, [rstart wrt DGROUP]
        mov [logpos wrt DGROUP], ax
        sub ax, bx
        mov [logbuf + bx + 4], ax
        ret

; flush: the buffer is appended to the log file (DS = DGROUP)
flush:
        pushad
        push es
        cmp word [trflush + 2 wrt DGROUP], 0
        je .nus
        call far [trflush wrt DGROUP]
.nus:
        cmp word [lost wrt DGROUP], 0
        je .nl
        push ds
        pop es
        mov ax, 0009h
        call rbegin
        jc .nl
        mov ax, [lost wrt DGROUP]
        stosw
        call rend
        mov word [lost wrt DGROUP], 0
.nl:    mov cx, [logpos wrt DGROUP]
        jcxz .r
        mov dx, fname
        mov ax, 3D01h
        int 21h
        jc .r
        mov bx, ax
        push cx
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        pop cx
        mov dx, logbuf
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov word [logpos wrt DGROUP], 0
.r:     pop es
        popad
        ret

; structrec: the EXTPAINTSTRUC at DX:BX (DX selector), AL when (0
; before, 1 after); DS = ES = DGROUP. In full when it is new, else the
; runs of bytes that differ from the shadow copy; the shadow is updated.
structrec:
        push bp
        mov bp, ax                      ; when
        mov ax, dx
        mov cx, SMAX
        call readable
        test cx, cx
        jz near .r
        cmp dx, [shsel wrt DGROUP]
        jne .full
        cmp bx, [shoff wrt DGROUP]
        jne .full
        cmp cx, [shsize wrt DGROUP]
        jne .full
        ; any differences?
        push ds
        mov si, bx
        mov di, shadow
        mov ds, dx
        push cx
        repe cmpsb
        pop cx
        pop ds
        je .r
        mov ax, 0004h
        call rbegin
        jc .r
        mov ax, bp
        stosw
        mov ax, bx
        stosw
        mov ax, dx
        stosw
        ; runs: SI walks the structure, CX left
        xor si, si                      ; offset in the structure
.run:   cmp si, [shsize wrt DGROUP]
        jae .done
        push es
        mov es, dx
        mov al, [es:bx + si]
        pop es
        cmp al, [shadow + si]
        jne .start
        inc si
        jmp .run
.start: mov ax, si
        stosw                           ; offset
        push di
        stosw                           ; length, set below
.byte:  push es
        mov es, dx
        mov al, [es:bx + si]
        pop es
        cmp al, [shadow + si]
        je .endr
        stosb
        mov [shadow + si], al
        inc si
        cmp si, [shsize wrt DGROUP]
        jb .byte
.endr:  pop ax                          ; where the length goes
        push bx
        mov bx, ax
        mov ax, di
        sub ax, bx
        sub ax, 2
        mov [bx], ax
        pop bx
        jmp .run
.done:  mov ax, 0FFFFh
        stosw
        call rend
        jmp .r
.full:  mov [shsel wrt DGROUP], dx
        mov [shoff wrt DGROUP], bx
        mov [shsize wrt DGROUP], cx
        push ds
        mov si, bx
        mov di, shadow
        mov ds, dx
        push cx
        rep movsb
        pop cx
        pop ds
        mov ax, 0003h
        call rbegin
        jc .r
        mov ax, bp
        stosw
        mov ax, bx
        stosw
        mov ax, dx
        stosw
        mov ax, cx
        stosw
        mov si, shadow
        rep movsb
        call rend
.r:     pop bp
        ret

; memrec: CX bytes at DX:BX as a memory record (DS = ES = DGROUP)
memrec:
        mov ax, dx
        call readable
        mov ax, 0005h
        call rbegin
        jc .r
        mov ax, bx
        stosw
        mov ax, dx
        stosw
        mov ax, cx
        stosw
        push ds
        mov ds, dx
        mov si, bx
        rep movsb
        pop ds
        call rend
.r:     ret

; usmark: a record in USTRACE's stream (when it is there): module 9,
; ordinal AX, the frame's registers (the export's frame at BP), stack
; words from the far pointer pushed before the call (8 words). DS =
; DGROUP. Everything but AX is kept.
usmark:
        push bp
        mov bp, sp
        pushad
        push es
        cmp word [trrec + 2 wrt DGROUP], 0
        je .r
        mov bx, ax
        push ds
        pop es
        mov di, mkframe
        push ds
        mov si, [bp]                    ; the export's BP
        sub si, 28                      ; its ES..flags (11 words)
        push ss
        pop ds
        mov cx, 11
        rep movsw
        pop ds
        mov si, [bp]
        mov ax, [ss:si]
        stosw                           ; [11] BP
        xor ax, ax
        stosw
        stosw
        stosw
        mov ax, [ss:si + 4]
        stosw                           ; [15] return IP
        mov ax, [ss:si + 6]
        stosw                           ; [16] CS
        push ds
        lds si, [bp + 4]
        mov cx, 8
        rep movsw                       ; [17..24]
        pop ds
        xor ax, ax
        mov cx, 8
        rep stosw
        push word 9                     ; modid
        push bx                         ; ordinal
        push word 0                     ; pointer mask
        push word 0                     ; argument bytes
        push word 0                     ; no return
        push word 0
        push word 0                     ; stub
        push ds
        push word mkframe
        call far [trrec wrt DGROUP]
.r:     pop es
        popad
        pop bp
        ret 4

; ---- the exports ----
%macro thunk 1
global T_%1
T_%1:
        push word %1
        jmp thcommon
%endmacro
%assign i 1
%rep 23
thunk i
%assign i i+1
%endrep

%assign i 1
%rep 23
extern X_ %+ i
%assign i i+1
%endrep

targets:
        dd 0
%assign i 1
%rep 23
        dw X_ %+ i, seg X_ %+ i
%assign i i+1
%endrep

; [bp+2] ordinal, [bp+4] return IP, [bp+6] return CS, [bp+8..] w0..w7
thcommon:
        push bp
        mov bp, sp
        sub sp, 6                       ; [bp-4] target offset, [bp-2] seg,
                                        ; [bp-6] spare
        pushf
        pusha
        push ds
        push es
        ; [bp-8] flags, [bp-10] AX, -12 CX, -14 DX, -16 BX, -18 SP, -20 BP,
        ; -22 SI, -24 DI, -26 DS, -28 ES
        cld
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov ax, seg logpos
        mov ds, ax
        push ds
        pop es
        mov bx, [bp + 2]
        shl bx, 2
        mov ax, [cs:targets + bx]
        mov [bp - 4], ax
        mov ax, [cs:targets + bx + 2]
        mov [bp - 2], ax
        inc word [depth wrt DGROUP]
        mov al, 1
        mov ah, [bp + 2]
        call rbegin
        jc .norec
        mov ax, [bp - 10]
        stosw                           ; AX
        mov ax, [bp - 16]
        stosw                           ; BX
        mov ax, [bp - 12]
        stosw                           ; CX
        mov ax, [bp - 14]
        stosw                           ; DX
        mov ax, [bp - 22]
        stosw                           ; SI
        mov ax, [bp - 24]
        stosw                           ; DI
        mov ax, [bp]
        stosw                           ; BP
        mov ax, [bp - 26]
        stosw                           ; DS
        mov ax, [bp - 28]
        stosw                           ; ES
        mov ax, [bp - 8]
        stosw                           ; flags
        mov cx, 8
        lea si, [bp + 8]
.args:  mov ax, [ss:si]
        stosw
        add si, 2
        loop .args
        mov ax, [bp + 4]
        stosw
        mov ax, [bp + 6]
        stosw
        call rend
.norec: mov ax, [bp + 2]
        push ss
        lea si, [bp + 8]
        push si
        call usmark
        ; the structure before the call, where the argument count is known
        mov bx, [bp + 2]
        cmp bx, 10                      ; GetFontList: not a structure
        je .nost
        movzx bx, byte [cs:argbytes + bx]
        cmp bx, 0FFh
        je .nost
        mov si, bp
        add si, bx
        mov dx, [ss:si + 8 - 4 + 2]
        mov bx, [ss:si + 8 - 4]
        mov al, 0
        call structrec
.nost:  pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        pop es
        pop ds
        popa
        popf
        ; call the real entry with a copy of the 8 words
        push word [bp + 22]
        push word [bp + 20]
        push word [bp + 18]
        push word [bp + 16]
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push cs
        push word back
        push word [bp - 2]
        push word [bp - 4]
        retf
back:
        pushf
        pusha
        push ds
        push es
        ; [sp] ES, +2 DS, +4 DI, +6 SI, +8 BP, +10 SP, +12 BX, +14 DX,
        ; +16 CX, +18 AX, +20 flags
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        cld
        mov si, sp
        add si, 24
        mov cx, sp
        add cx, 24
        sub cx, bp
        add cx, 44                      ; bytes the real entry took off
        mov [bp - 6], cx
        mov ax, seg logpos
        mov ds, ax
        push ds
        pop es
        mov al, 2
        mov ah, [bp + 2]
        push si
        call rbegin
        pop si
        jc .norec
        mov ax, [ss:si + 18]
        stosw                           ; AX
        mov ax, [ss:si + 14]
        stosw                           ; DX
        mov ax, [ss:si + 20]
        stosw                           ; flags
        mov ax, [bp - 6]
        stosw                           ; bytes taken off
        call rend
.norec: push si
        mov si, [ss:si + 18]
        mov [retax wrt DGROUP], si
        pop si
        push si
        mov si, [ss:si + 14]
        mov [retax + 2 wrt DGROUP], si
        pop si
        mov ax, [bp - 6]
        mov [retax + 4 wrt DGROUP], ax
        mov ax, [bp + 2]
        add ax, 100
        push ds
        push word retax
        call usmark
        mov ax, [bp + 2]
        cmp ax, 10
        jne .nfl
        mov bx, [bp + 8]                ; GetFontList's buffer
        mov dx, [bp + 10]
        mov cx, 200h
        call memrec
        jmp .after
.nfl:   cmp ax, 11
        jne .nsp
        mov bx, [bp + 8]                ; SetPaintFnt: *lpHeight, *lpWidth
        mov dx, [bp + 10]
        mov cx, 2
        call memrec
        mov bx, [bp + 12]
        mov dx, [bp + 14]
        mov cx, 2
        call memrec
.nsp:   mov bx, [bp - 6]
        cmp bx, 4
        jb .after
        mov si, bp
        add si, bx
        mov dx, [ss:si + 8 - 4 + 2]
        mov bx, [ss:si + 8 - 4]
        mov al, 1
        call structrec
.after: ; the lock: GetDisplayUpd takes it; PaintScreen, UpdateScreen and
        ; GrbUnLockApp give it back
        mov ax, [bp + 2]
        cmp ax, 18
        jne .nlk
        mov word [locked wrt DGROUP], 1
.nlk:   cmp ax, 1
        je .unl
        cmp ax, 12
        je .unl
        cmp ax, 22
        jne .nul
.unl:   mov word [locked wrt DGROUP], 0
.nul:   dec word [depth wrt DGROUP]
        jnz .nfls
        cmp word [locked wrt DGROUP], 0
        jne .nfls
        call flush
.nfls:  ; return address to [bp+4+n]; the caller's BP to [bp+2+n]; the
        ; new SP to [bp-4]
        mov cx, [bp - 6]
        mov di, bp
        add di, cx
        mov ax, [bp + 4]
        mov dx, [bp + 6]
        mov [ss:di + 4], ax
        mov [ss:di + 6], dx
        mov ax, [bp]
        mov [ss:di + 2], ax
        add di, 2
        mov [bp - 4], di
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        pop es
        pop ds
        popa
        popf
        mov sp, [bp - 4]
        pop bp
        retf
