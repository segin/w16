; dpmiprb.asm - DPMIPRB.COM: a raw DPMI 0.9 client for DOS boxes in 386
; Enhanced mode (ARCHITECTURE.md 12.16), run under Windows 3.1's WIN386
; and w16's (tests/dpmirun.sh). It enters protected mode itself through
; INT 2Fh 1687h, as a 16-bit or a 32-bit client, and logs what the host
; answers: the mode switch and its selectors, INT 2Fh in protected mode,
; INT 31h's descriptor, DOS memory, vector, exception, translation
; (0300h-0306h), version, memory, lock, physical mapping, virtual
; interrupt and vendor functions, interrupt reflection both ways, its own
; INT 21h calls in protected mode, then INT 21h 4Ch.
;
;   DPMIPRB MODE TAG      log DP<TAG>.LOG in the current directory
;   MODE 16, 32   the whole probe as a 16-bit or a 32-bit client
;        N, O     16-bit; then EXECs "DPMIPRB 16 <TAG+1>" (N) or "DPMIPRB
;                 32 <TAG+1>" (O) from protected mode (a nested client) and
;                 checks its own state after
;        G16, G32 the probe, then a general protection fault with no
;                 handler (the host's default action)
;        C16, C32 two boxes at once: 256 KB filled with the tag's pattern,
;                 checked 40 times with a time-slice release between
; Lines starting with INFO hold what depends on the host's own layout
; (entry points, linear addresses, handles); the rest is compared.
; Sources: the DPMI 0.9 specification, the Intel 386 manual. Black box:
; nothing but the published interfaces.
        bits 16
        cpu 386
        org 100h

section .text
start:  mov di, bss_start
        mov cx, stacktop - bss_start
        xor al, al
        rep stosb
        mov sp, stacktop
        mov [psp], cs
        mov ax, [2Ch]
        mov [envseg], ax
        ; 64 KB is all a .COM needs: the rest for DOS (0100h, the child)
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        ; the command tail: MODE TAG
        mov si, 81h
.sk:    lodsb
        cmp al, ' '
        je .sk
        cmp al, 13
        je .usage
        cmp al, 'a'
        jb .up
        and al, 0DFh
.up:    mov [mode], al          ; '1', '3', 'N', 'G', 'C'
        cmp al, '3'
        je .m32
        cmp al, 'G'
        je .mg
        cmp al, 'C'
        jne .mt
.mg:    mov al, [si]
        cmp al, '3'
        jne .mt
.m32:   mov byte [is32], 1
.mt:    lodsb
        cmp al, ' '
        je .tag
        cmp al, 13
        je .usage
        jmp .mt
.tag:   lodsb
        cmp al, ' '
        je .tag
        cmp al, 13
        je .usage
        and al, 0DFh
        mov [tag], al
        mov [logname + 2], al
        mov dx, logname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .usage
        mov [logh], ax
        ; real mode: INT 2Fh 1686h, 1687h
        mov ax, 1686h
        int 2Fh
        mov [rm1686], ax
        mov ax, 1687h
        xor bx, bx
        xor cx, cx
        xor dx, dx
        xor si, si
        xor di, di
        push es
        int 2Fh
        mov [e_ax], ax
        mov [e_bx], bx
        mov [e_cx], cx
        mov [e_dx], dx
        mov [e_si], si
        mov [entry], di
        mov [entry + 2], es
        pop es
        test ax, ax
        jnz .nodpmi
        ; the host's data
        mov bx, [e_si]
        xor ax, ax
        test bx, bx
        jz .nodata
        mov ah, 48h
        int 21h
        jc .nodpmi
.nodata: mov es, ax
        movzx ax, byte [is32]
        call far [entry]
        jc .noswitch
        ; protected mode now
        mov [sel_ds], ds
        mov [sel_cs0], cs
        mov [sel_ss0], ss
        mov [sel_es0], es
        mov [sel_fs0], fs
        mov [sel_gs0], gs
        mov [esp0], esp
        pushfd
        pop dword [fl0]
        cmp byte [is32], 0
        jne .go32
        jmp b16_main

        ; a 32-bit client: a 32-bit code segment over the same memory
.go32:   xor ax, ax
        mov cx, 1
        int 31h
        jc .fail
        mov [sel_c32], ax
        mov ax, 6
        mov bx, cs
        int 31h
        mov ax, 7
        mov bx, [sel_c32]
        int 31h
        mov ax, 8
        xor cx, cx
        mov dx, 0FFFFh
        int 31h
        mov ax, 9
        mov cx, 40FAh           ; code, DPL 3, readable; D
        int 31h
        jc .fail
        push word [sel_c32]
        push word b32_main
        retf
.fail:  mov ax, 4CFFh
        int 21h

.nodpmi: mov dx, m_nodpmi
        mov cx, m_noswitch - m_nodpmi
        jmp .say
.noswitch: mov dx, m_noswitch
        mov cx, m_end - m_noswitch
.say:   mov bx, [logh]
        mov ah, 40h
        int 21h
        mov ax, 4C01h
        int 21h
.usage: mov ax, 4C02h
        int 21h

; ---- real-mode routines (V86), called from protected mode ----
rm_int62:                       ; INT 62h reflected from protected mode
        inc word [cs:rm62cnt]
        add ax, 1
        mov [cs:rm62ds], ds
        iret
rm_int61:                       ; INT 61h reflected: nothing
        iret
rm_args:                        ; 0301h with two words copied
        push bp
        mov bp, sp
        mov ax, [bp + 6]
        mov bx, [bp + 8]
        pop bp
        retf
rm_iret:                        ; 0302h
        mov ax, 0A5A5h
        iret
rm_wait:                        ; 0301h: interrupts on until 4 BIOS ticks
        sti
        push ds
        push word 40h
        pop ds
        mov bx, [6Ch]
        mov ecx, 4000000h
.w:     mov ax, [6Ch]
        sub ax, bx
        cmp ax, 4
        jae .d
        dec ecx
        jnz .w
.d:     pop ds
        retf
rm_cb2:                         ; 0301h: calls the real-mode callback itself
        push word 1111h
        call far [cs:cbaddr]
        pop bx
        retf
rm_raw:                         ; the raw switch to real mode lands here
        mov [cs:raw_ss], ss
        mov [cs:raw_sp], sp
        mov ax, cs
        mov [cs:raw_cs], ax
        mov ah, 30h
        int 21h
        mov [cs:raw_ver], ax
        ; and back (0306h's real-to-protected entry)
        mov ax, [cs:sel_ds]
        mov cx, ax
        mov dx, [cs:sel_ss0]
        mov ebx, [cs:raw_back_sp]
        mov si, [cs:raw_back_cs]
        mov edi, [cs:raw_back_ip]
        jmp far [cs:rm2pm]

%define L(x) b %+ BZ %+ _ %+ x
%define BZ 16
        bits 16
%include "dpmibody.inc"
%undef BZ
%define BZ 32
        bits 32
%include "dpmibody.inc"

section .data
logname db 'DPx.LOG', 0
m_nodpmi db 'no DPMI host', 13, 10
m_noswitch db 'mode switch failed', 13, 10
m_end:
childname db 'DPMIPRB.COM', 0
childtail db 5, ' 32 x', 13
childt16 db 5, ' 16 x', 13
msdos   db 'MS-DOS', 0
tmpname db 'DPPRB.TMP', 0
hexd    db '0123456789ABCDEF'
logpos  dd 0
badsel  dd 0FFF4h

section .bss
bss_start:
oldv62  resd 1
oldv61  resd 1
psp     resw 1
envseg  resw 1
mode    resb 1
is32    resb 1
tag     resb 1
        resb 1
logh    resw 1
rm1686  resw 1
e_ax    resw 1
e_bx    resw 1
e_cx    resw 1
e_dx    resw 1
e_si    resw 1
entry   resd 1
sel_ds  resw 1
sel_cs0 resw 1
sel_ss0 resw 1
sel_es0 resw 1
sel_fs0 resw 1
sel_gs0 resw 1
sel_c32 resw 1
sel_40  resw 1
esp0    resd 1
fl0     resd 1
rm62cnt resw 1
rm62ds  resw 1
cbaddr  resd 1
rm2pm   resd 1
pm2rm   resd 2
raw_ss  resw 1
raw_sp  resw 1
raw_cs  resw 1
raw_ver resw 1
raw_back_sp resd 1
raw_back_cs resw 1
raw_back_ip resd 1
raw_save_esp resd 1
r_eax   resd 1
r_ebx   resd 1
r_ecx   resd 1
r_edx   resd 1
r_esi   resd 1
r_edi   resd 1
r_es    resw 1
r_cf    resb 1
        resb 1
tmp     resd 4
desc    resb 8
rmr     resb 64             ; a real-mode register structure (0300h-0302h)
cbr     resb 64             ; the callback's
logr    resb 64             ; the log's writes
old8    resd 2
oldex   resd 2 * 8          ; the exception handlers replaced (0, 3, 6, 0Dh)
exc_no  resd 1
exc_err resd 1
exc_ip  resd 1
exc_cs  resd 1
exc_ss  resd 1
exc_fl  resd 1
exc_sp  resd 1
exc_hss resd 1
exc_resume resd 1
int61cnt resd 1
int3pm  resd 1
tick_pm resd 1
cb_cnt  resd 1
cb_es   resd 1
cb_edi  resd 1
cb_ds   resd 1
cbh     resd 1024            ; callbacks counted (0303h)
mh      resd 512            ; memory handles (0501h)
st_buf  resb 512            ; 0305h's state
ptab    resb 32             ; the child's EXEC block
logbuf  resb 512
        resb 4096
stacktop:
