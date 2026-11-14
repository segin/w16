; wswap.asm - WSWAP.EXE for w16: Windows' side of the Standard-mode task
; switcher (ARCHITECTURE.md 9.9).
;
; WIN.COM (Windows 3.1's or w16's, win/win.asm) runs it in standard mode
; with the tail " \x08"; everything else comes from WIN.COM's switcher block
; (INT 2Fh AX=4A05h SI=5). It starts DOSX.EXE (block 189h, its tail at
; 05h), which starts KERNEL. When WINOLDAP asks for a DOS program
; (INT 2Fh AX=4B80h, then INT 21h AH=0Bh with AL=80h, which DOSX passes to
; real mode), WSWAP copies Windows' conventional memory, from the end of
; its own small loader up to the end of the memory arena, into the buffer
; WINOLDAP gave (block 185h), keeps its state in ~WOA100F.TMP in the swap
; directory and ends; WIN.COM then runs DSWAP.EXE for the program and
; WSWAP again, which reads Windows back and returns from that INT 21h
; call. Observed on Windows 3.1 with the SW1 probe (tests/dos/sw1.asm):
; the INT 2Fh 4A05h calls and the block bytes WSWAP writes, the state file
; (its name, 34 bytes, hidden and read-only, deleted when read back), the
; MS-DOS 5 task switcher notifications it sends (create, activate, session
; active, query suspend, suspend; session 100Fh), the exit protocol.
; Microsoft's WSWAP.EXE was never disassembled; how it does what it does
; inside is w16's own (it also hands DOSX its extended memory through an
; XMS interface of its own, which w16's does not: UNVERIFIED that anything
; depends on that).
;
; Layout: one segment (a .COM-format program, as DOS loads any file
; without an MZ header). R, the loader, sits right after the PSP and is
; never saved; T, everything else (hooks, state, stacks, start-up code), is
; the start of the image saved with Windows. At a resume R reads the image
; back over T and jumps into it.
;
; Copyright (c) 2026 The w16 contributors, MIT License (LICENSE).

        bits 16
        cpu 286
        org 100h

%include "swap.inc"

start:  jmp t_init

; ========================================================================
; R: the loader (below the image)
; ========================================================================

r_gdt:      times 48 db 0       ; INT 15h AH=87h descriptor table
r_src:      dd 0                ; xmove: linear source, destination, bytes
r_dst:      dd 0
r_len:      dd 0
r_name:     times PATHMAX db 0  ; the state file
; the state file, 34 bytes (the size Windows 3.1's has; the contents are
; w16's own)
r_state:
rs_magic:   db 'w16S'           ; 00h
rs_mcb:     times 16 db 0       ; 04h our MCB as it was before the switch
rs_linear:  dd 0                ; 14h WINOLDAP's buffer
rs_paras:   dw 0                ; 18h the image's size
rs_tseg:    dw 0                ; 1Ah the image's segment
rs_psp2e:   dd 0                ; 1Ch our PSP's 2Eh (DOS's saved SS:SP)
rs_sum:     dw 0                ; 20h
STATE_LEN   equ $ - r_state
            align 2
            times 256 db 0
r_stack_top:

; xmove: copy r_len bytes (even) from linear r_src to linear r_dst with
; INT 15h AH=87h, 64 KB at a time. DS = CS. CF set on failure.
xmove:  push bx
        push cx
        push dx
        push si
        push es
.l:     mov ax, [r_len]
        or ax, [r_len + 2]
        jz .ok
        mov cx, 8000h                   ; words
        cmp word [r_len + 2], 0
        jne .n
        mov cx, [r_len]
        shr cx, 1
.n:     mov si, r_gdt + 10h
        mov bx, r_src
        call .desc
        mov si, r_gdt + 18h
        mov bx, r_dst
        call .desc
        push cx
        push ds
        pop es
        mov si, r_gdt
        mov ah, 87h
        int 15h
        pop cx
        jc .fail
        test ah, ah
        jnz .fail
        mov ax, cx
        xor dx, dx
        shl ax, 1
        rcl dx, 1
        add [r_src], ax
        adc [r_src + 2], dx
        add [r_dst], ax
        adc [r_dst + 2], dx
        sub [r_len], ax
        sbb [r_len + 2], dx
        jmp .l
.ok:    clc
        jmp .r
.fail:  stc
.r:     pop es
        pop si
        pop dx
        pop cx
        pop bx
        ret
.desc:  mov word [si], 0FFFFh
        mov ax, [bx]
        mov [si + 2], ax
        mov ax, [bx + 2]
        mov [si + 4], al
        mov byte [si + 5], 93h
        mov byte [si + 6], 0
        mov [si + 7], ah
        ret

; Resume: read Windows back (T's start-up code jumps here with SS:SP in R,
; DS = CS, r_name set). Never returns.
r_resume:
        mov dx, r_name
        mov ax, 3D00h
        int 21h
        jc r_fatal
        mov bx, ax
        mov dx, r_state
        mov cx, STATE_LEN
        mov ah, 3Fh
        int 21h
        jc r_fatal
        cmp ax, STATE_LEN
        jne r_fatal
        mov ah, 3Eh
        int 21h
        cmp word [rs_magic], 'w1'
        jne r_fatal
        cmp word [rs_magic + 2], '6S'
        jne r_fatal
        mov ax, cs
        add ax, T_PARA
        cmp ax, [rs_tseg]               ; loaded where it was?
        jne r_fatal
        ; the image, back from WINOLDAP's buffer
        mov ax, [rs_linear]
        mov [r_src], ax
        mov ax, [rs_linear + 2]
        mov [r_src + 2], ax
        mov ax, [rs_tseg]
        call r_seg2lin
        mov [r_dst], ax
        mov [r_dst + 2], dx
        mov ax, [rs_paras]
        call r_seg2lin
        mov [r_len], ax
        mov [r_len + 2], dx
        call xmove
        jc r_fatal
        ; our MCB and DOS's saved SS:SP as they were
        mov ax, cs
        dec ax
        mov es, ax
        xor di, di
        mov si, rs_mcb
        mov cx, 8
        cld
        rep movsw
        mov ax, [rs_psp2e]
        mov [2Eh], ax
        mov ax, [rs_psp2e + 2]
        mov [30h], ax
        ; the state file goes
        mov dx, r_name
        mov ax, 4301h
        xor cx, cx
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
        jmp t_resume

; AX = segment -> DX:AX = linear address
r_seg2lin:
        xor dx, dx
        mov cx, 4
.s:     shl ax, 1
        rcl dx, 1
        loop .s
        ret

r_fatal:
        mov dx, m_fatal
        mov ah, 09h
        int 21h
        mov ax, 4CFFh
        int 21h
m_fatal: db 'Fatal exit! w16 cannot be swapped back', 0Dh, 0Ah, '$'

; ========================================================================
; T: the rest (the start of the image saved with Windows)
; ========================================================================
            align 16
T_START:
T_PARA      equ (T_START - $$ + 100h) / 16     ; T's paragraph in our segment

blk:        dd 0                ; WIN.COM's switcher block
psp:        dw 0
arena_end:  dw 0                ; the segment where the memory arena ends
arena_type: db 0                ; its last MCB's type ('Z', or 'M' before UMBs)
sw_req:     db 0                ; INT 2Fh 4B80h seen
brk:        db 0                ; Ctrl+Break checking at the start
old21:      dd 0
old2f:      dd 0
; at a switch
w_ss:       dw 0                ; the caller's stack (DOSX's)
w_sp:       dw 0
w_psp:      dw 0                ; DOS's current PSP (a Windows task's)
w_dta:      dd 0
w_strat:    dw 0                ; allocation strategy, UMB link
w_umb:      dw 0
w_brk:      db 0
w_pic:      dw 0                ; the PICs' masks (master, slave)
pic0:       dw 0                ; at the start
exit_code:  db 0
; MS-DOS 5 task switcher clients (INT 2Fh 4B01h)
chain:      dd 0
; instance data (INT 2Fh 4B05h): up to NINST items
NINST       equ 16
INSTBUF     equ 512
n_inst:     dw 0
inst_ptr:   times NINST dd 0
inst_len:   times NINST dw 0
inst0:      times INSTBUF db 0  ; their contents at the start
instw:      times INSTBUF db 0  ; and Windows' at a switch
; EXEC
exec_path:  times PATHMAX db 0
exec_tail:  times PATHMAX db 0
fcb1:       times 37 db 0
fcb2:       times 37 db 0
exec_pb:    dw 0, exec_tail, 0, fcb1, 0, fcb2, 0
save_ss:    dw 0
save_sp:    dw 0
            align 2
ivt0:       times 1024 db 0     ; the vector table at the start
ivtw:       times 1024 db 0     ; Windows' at a switch
; switcher version (entry point function 0)
sw_version: dw 3, 0Ah           ; (UNVERIFIED: what Windows 3.1's returns)
            db 'w16', 0

; ---- INT 2Fh: AX=4B80h asks for the switch that the next INT 21h AH=0Bh
; AL=80h makes ----
int2f:  cmp ax, 4B80h
        jne .c
        mov byte [cs:sw_req], 1
.c:     jmp far [cs:old2f]

; ---- INT 21h: AX=0B80h after 4B80h: switch to a DOS program ----
int21:  cmp ax, 0B80h
        jne .c
        cmp byte [cs:sw_req], 0
        jne .sw
.c:     jmp far [cs:old21]
.sw:    mov byte [cs:sw_req], 0
        push ds
        push es
        pusha
        mov [cs:w_ss], ss
        mov [cs:w_sp], sp
        cli
        push cs
        pop ss
        mov sp, sw_stack_top
        sti
        push cs
        pop ds
        cld
        call switch_out                 ; returns only if it could not
        cli
        mov ss, [w_ss]
        mov sp, [w_sp]
        sti
        popa
        pop es
        pop ds
        xor al, al                      ; (AH=0Bh: no key)
        iret

; ---- the switcher's entry point (for MS-DOS 5 task switcher clients):
; AX=0 version, the rest not supported ----
sw_entry:
        test ax, ax
        jnz .ns
        push cs
        pop es
        mov bx, sw_version
        clc
        retf
.ns:    stc
        retf

; ---- INT 2Fh AX=4A05h with SI = the function; keeps all but AX, SI, DI
; (and ES, DX for SI=5) ----
sw_call:
        mov ax, 4A05h
        int 2Fh
        ret

; ES:DI -> the block (keeps the others)
get_blk:
        les di, [blk]
        ret

; MS-DOS 5 task switcher notifications: AX = the function, BX = the
; session; to every client in the chain (built with INT 2Fh 4B01h). Windows
; 3.1's WSWAP: 5 (create), 3 and 4 (activate, session active, CX=1) when
; Windows starts; 1 and 2 (query suspend, suspend) at a switch; 3 and 4
; when it comes back (SW1). Returns AX nonzero if a client refused.
notify: push bx
        push cx
        push dx
        push si
        push di
        push es
        push ax
        mov ax, 4B01h                   ; the chain, afresh each time
        mov cx, cs
        mov dx, sw_entry
        xor bx, bx
        mov es, bx
        int 2Fh
        mov [chain], bx
        mov [chain + 2], es
        pop ax
        xor si, si                      ; any refusal
.l:     les bx, [chain]
        mov dx, es
        or dx, bx
        jz .done
        mov dx, [es:bx]                 ; the next one, first
        mov [chain], dx
        mov dx, [es:bx + 2]
        mov [chain + 2], dx
        push ax
        push si
        push word [es:bx + 6]           ; its notification function
        push word [es:bx + 4]
        mov bp, sp
        push cs                         ; ES:DI -> our entry point
        pop es
        mov di, sw_entry
        mov bx, [cs:n_sess]
        mov cx, [cs:n_cx]
        cmp ax, 2                       ; suspend and activate: interrupts
        je .cli                         ; disabled
        cmp ax, 3
        jne .call
.cli:   cli
.call:  call far [bp]
        sti
        add sp, 4
        mov dx, ax
        pop si
        pop ax
        or si, dx
        push cs
        pop ds
        jmp .l
.done:  mov ax, si
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        ret
n_sess:     dw (SESS_HI << 8) | SESS_WIN
n_cx:       dw 0

; ---- the switch ----
switch_out:
        ; MS-DOS 5 task switcher clients may refuse
        mov word [n_cx], 0
        mov ax, 1
        call notify
        test ax, ax
        jnz .refuse
        mov ax, 2
        call notify
        test ax, ax
        jnz .refuse
        ; DOS's state for the switch
        mov ax, 3300h
        int 21h
        mov [w_brk], dl
        mov ax, 3301h
        xor dl, dl
        int 21h
        mov ax, 5800h
        int 21h
        mov [w_strat], ax
        mov ax, 5802h
        int 21h
        mov [w_umb], ax
        push es
        mov ah, 2Fh
        int 21h
        mov [w_dta], bx
        mov [w_dta + 2], es
        pop es
        mov ah, 51h
        int 21h
        mov [w_psp], bx
        mov bx, [psp]
        mov ah, 50h
        int 21h
        ; WIN.COM: the session to run (17Eh := its record's next)
        mov si, SW_COUNT
        call sw_call
        mov si, SW_NEXT
        call sw_call
        mov si, SW_BLOCK
        call sw_call
        ; room in the swap directory for the program (Windows 3.1's: INT
        ; 21h AH=36h on its drive; UNVERIFIED: how much it wants, w16 the
        ; paragraphs a DOS session gets, block 183h)
        call get_blk
        mov dl, [es:di + B_SWAP1]
        and dl, 0DFh
        sub dl, 'A' - 1
        mov ah, 36h
        int 21h
        cmp ax, 0FFFFh
        je .nodisk
        mul cx                          ; bytes per cluster
        mul bx                          ; DX:AX = free bytes
        push dx
        push ax
        call get_blk
        mov ax, [es:di + B_DOSMEM]
        call r_seg2lin                  ; DX:AX = bytes needed
        pop bx
        pop cx                          ; CX:BX = free
        cmp cx, dx
        ja .roomy
        jb .nodisk
        cmp bx, ax
        jb .nodisk
.roomy: ; the state file
        call state_name
        mov dx, r_name
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .nodisk
        mov [st_h], ax
        ; vectors, PICs, instance data: Windows' kept, the start's back
        cli
        push ds
        push es
        push ds
        pop es
        xor si, si
        mov ds, si
        mov di, ivtw
        mov cx, 512
        rep movsw
        pop es
        pop ds
        in al, 0A1h
        mov ah, al
        in al, 21h
        mov [w_pic], ax
        sti
        mov si, instw
        call inst_save
        ; Windows' memory into WINOLDAP's buffer
        call get_blk
        mov ax, [es:di + B_LINEAR]
        mov [rs_linear], ax
        mov [r_dst], ax
        mov ax, [es:di + B_LINEAR + 2]
        mov [rs_linear + 2], ax
        mov [r_dst + 2], ax
        mov ax, cs
        add ax, T_PARA
        mov [rs_tseg], ax
        call r_seg2lin
        mov [r_src], ax
        mov [r_src + 2], dx
        mov ax, [arena_end]
        sub ax, [rs_tseg]
        mov [rs_paras], ax
        call r_seg2lin
        mov [r_len], ax
        mov [r_len + 2], dx
        ; (the copy happens with the switch already decided: T's state
        ; above must be final)
        mov byte [sw_done], 1
        call xmove
        jc .copyfail
        ; our MCB as it is now, then made to reach the arena's end so that
        ; DOS frees all of it when we end
        push ds
        mov ax, [psp]
        dec ax
        mov ds, ax
        xor si, si
        mov di, rs_mcb
        push cs
        pop es
        mov cx, 8
        rep movsw
        pop ds
        mov ax, [2Eh]
        mov [rs_psp2e], ax
        mov ax, [30h]
        mov [rs_psp2e + 2], ax
        mov bx, [st_h]
        mov dx, r_state
        mov cx, STATE_LEN
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ax, 4301h
        mov cx, 3                       ; hidden, read-only (as 3.1's)
        int 21h
        ; the vectors, PICs and instance data as at the start
        cli
        push es
        xor di, di
        mov es, di
        mov si, ivt0
        mov cx, 512
        rep movsw
        pop es
        mov ax, [pic0]
        out 21h, al
        mov al, ah
        out 0A1h, al
        sti
        mov si, inst0
        call inst_load
        ; the block, as Windows 3.1's WSWAP leaves it: Windows' swap file
        ; number at 16Dh, its records (session 0Fh) marked swapped out
        call get_blk
        mov word [es:di + B_WINSID], (SESS_HI << 8) | SESS_WIN
        lea bx, [di + B_RECS]
        mov cx, 16
.mk:    cmp byte [es:bx + R_KIND], 80h
        je .mkn
        cmp byte [es:bx + R_SESS], SESS_WIN
        jne .mkn
        mov byte [es:bx + R_SWAPPED], 1
.mkn:   add bx, REC_SIZE
        loop .mk
        mov si, SW_COUNT
        call sw_call
        call get_blk
        mov word [es:di + B_SID], (SESS_HI << 8) | SESS_WIN
        mov dl, [w_brk]
        mov ax, 3301h
        int 21h
        ; our block to the end of the arena, and end
        push es
        mov ax, [psp]
        dec ax
        mov es, ax
        mov al, [arena_type]
        mov [es:0], al
        mov ax, [arena_end]
        sub ax, [psp]
        mov [es:3], ax
        pop es
        mov ax, 4C00h
        int 21h

.copyfail:
        ; (WINOLDAP's buffer was not there: undo, as for no room)
        mov byte [sw_done], 0
        mov bx, [st_h]
        mov ah, 3Eh
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
        cli
        push es
        xor di, di
        mov es, di
        mov si, ivtw
        mov cx, 512
        rep movsw
        pop es
        sti
        mov si, instw
        call inst_load
.nodisk:
        ; the switch cannot be made: 172h = 08h for WINOLDAP, DOS's state
        ; as it was (Windows 3.1's WSWAP after WINOLDAP's "Insufficient disk
        ; space" warning, ARCHITECTURE.md 9.8)
        call get_blk
        mov byte [es:di + B_FAIL], 08h
        mov bx, [w_psp]
        mov ah, 50h
        int 21h
        mov dl, [w_brk]
        mov ax, 3301h
        int 21h
.refuse:
        ret

st_h:       dw 0
sw_done:    db 0

; the state file's name: the swap directory (block D7h), "~WOA", Windows'
; swap file number (record 15's bytes 2Ch, 2Bh) in hexadecimal, ".TMP"
state_name:
        push ds
        call get_blk
        mov bx, [es:di + B_RECS + REC_WIN * REC_SIZE + R_SESS]
        lea si, [di + B_SWAP1]
        push es
        pop ds
        push cs
        pop es
        mov di, r_name
.c:     lodsb
        test al, al
        jz .e
        stosb
        jmp .c
.e:     pop ds
        mov al, '~'
        stosb
        mov al, 'W'
        stosb
        mov al, 'O'
        stosb
        mov al, 'A'
        stosb
        mov al, bh
        call hex2
        mov al, bl
        call hex2
        mov si, s_tmp
.t:     lodsb
        stosb
        test al, al
        jnz .t
        ret
s_tmp:  db '.TMP', 0

hex2:   push ax
        shr al, 4
        call .d
        pop ax
        and al, 0Fh
.d:     add al, '0'
        cmp al, '9'
        jbe .s
        add al, 'A' - '9' - 1
.s:     stosb
        ret

; instance data: save the items' contents to DS:SI, load them from DS:SI
inst_save:
        push es
        xor bx, bx
.l:     cmp bx, [n_inst]
        jae .d
        push bx
        shl bx, 2
        push ds
        lds di, [inst_ptr + bx]
        shr bx, 1
        mov cx, [cs:inst_len + bx]
        xchg si, di                     ; DS:SI the item, ES:DI ours
        push cs
        pop es
        rep movsb
        xchg si, di
        pop ds
        pop bx
        inc bx
        jmp .l
.d:     pop es
        ret
inst_load:
        push es
        xor bx, bx
.l:     cmp bx, [n_inst]
        jae .d
        push bx
        shl bx, 2
        les di, [inst_ptr + bx]
        shr bx, 1
        mov cx, [inst_len + bx]
        cli
        rep movsb
        sti
        pop bx
        inc bx
        jmp .l
.d:     pop es
        ret

; ---- back from DOS: R has read the image back (DS = CS, SS:SP in R) ----
t_resume:
        cli
        push cs
        pop ss
        mov sp, sw_stack_top
        push cs
        pop ds
        push cs
        pop es
        cld
        mov byte [sw_done], 0
        ; Windows' vectors, PICs, instance data
        xor di, di
        mov es, di
        mov si, ivtw
        mov cx, 512
        rep movsw
        mov ax, [w_pic]
        out 21h, al
        mov al, ah
        out 0A1h, al
        sti
        mov si, instw
        call inst_load
        ; DOS's state as Windows had it
        mov bx, [w_psp]
        mov ah, 50h
        int 21h
        mov bx, [w_strat]
        mov ax, 5801h
        int 21h
        mov bx, [w_umb]
        xor bh, bh
        mov ax, 5803h
        int 21h
        push ds
        lds dx, [w_dta]
        mov ah, 1Ah
        int 21h
        pop ds
        mov ax, 0C00h                   ; (Windows 3.1's WSWAP empties the
        int 21h                         ; keyboard buffer here)
        mov word [n_cx], 1
        mov ax, 3
        call notify
        mov ax, 4
        call notify
        mov dl, [w_brk]
        mov ax, 3301h
        int 21h
        ; return from the INT 21h call that switched
        cli
        mov ss, [w_ss]
        mov sp, [w_sp]
        sti
        popa
        pop es
        pop ds
        xor al, al
        iret

            align 2
            times 1024 db 0
t_stack_top:                    ; WSWAP's own (it runs DOSX on it)
            times 1024 db 0
sw_stack_top:                   ; for a switch

; ---- start-up ----
t_init: cld
        mov sp, t_stack_top
        mov [psp], cs
        mov si, SW_BLOCK
        mov ax, 4A05h
        int 2Fh
        cmp ax, 4A05h
        je no_switcher
        mov [blk], ax
        mov [blk + 2], dx
        ; started again after a DOS program: Windows comes back (record 15,
        ; Windows itself, has a session then; at a restart WIN.COM has
        ; freed it)
        call get_blk
        cmp byte [es:di + B_RECS + REC_WIN * REC_SIZE + R_SESS], 0
        je fresh
        call state_name
        cli
        mov sp, r_stack_top
        sti
        jmp r_resume

no_switcher:
        mov dx, m_noswitch
        mov ah, 09h
        int 21h
        mov ax, 4CFFh
        int 21h

fresh:  ; the memory arena's end: our block's, or further when there are
        ; MCBs after it below the top of memory (INT 12h)
        int 12h
        mov cl, 6
        shl ax, cl
        mov bx, ax                      ; top of conventional memory
        mov ax, cs
        dec ax
.w:     mov es, ax
        mov dl, [es:0]
        mov cx, ax
        add cx, [es:3]
        inc cx                          ; the end of this block
        cmp dl, 'Z'
        je .last
        cmp cx, bx
        jae .last                       ; (UMBs beyond)
        mov ax, cx
        jmp .w
.last:  mov [arena_end], cx
        mov [arena_type], dl
        ; stale swap files of an earlier session (Windows 3.1's WSWAP looks
        ; for ~WOA1*.* in both swap directories and ~GRB*.* in the second)
        call get_blk
        lea si, [di + B_SWAP2]
        mov bx, s_woa
        call clean
        call get_blk
        lea si, [di + B_SWAP1]
        mov bx, s_woa
        call clean
        call get_blk
        lea si, [di + B_SWAP2]
        mov bx, s_grb
        call clean
        ; the block
        call get_blk
        mov byte [es:di + B_RUNS], 1
        mov ax, [arena_end]
        sub ax, [psp]
        mov [es:di + B_DOSMEM], ax
        mov byte [es:di + B_RECS + REC_WIN * REC_SIZE + R_SESS], SESS_WIN
        mov byte [es:di + B_RECS + REC_WIN * REC_SIZE + R_SESSHI], SESS_HI
        ; the program and its tail (DOSX's), copied out of the block (WINOLDAP
        ; writes programs' there later)
        push ds
        push es
        pop ds
        lea si, [di + B_PROGRAM]
        push cs
        pop es
        mov di, exec_path
        mov cx, PATHMAX - 1
        rep movsb
        pop ds
        call get_blk
        push ds
        push es
        pop ds
        lea si, [di + B_TAIL]
        push cs
        pop es
        mov di, exec_tail
        mov cx, PATHMAX
        rep movsb
        pop ds
        mov byte [exec_tail + PATHMAX - 1], 0Dh
        mov si, exec_tail + 1
        mov di, fcb1
        mov ax, 2901h
        int 21h
        mov di, fcb2
        mov ax, 2901h
        int 21h
        ; instance data (INT 2Fh AX=4B05h: WIN.COM declares the last system
        ; file table block's link)
        xor bx, bx
        mov es, bx
        mov cx, cs
        mov dx, sw_entry
        mov ax, 4B05h
        int 2Fh
.si:    mov ax, es
        or ax, bx
        jz .sid
        push es
        push bx
        les si, [es:bx + 0Eh]           ; its instance data records
.sr:    mov ax, [es:si]
        or ax, [es:si + 2]
        jz .srd
        mov di, [n_inst]
        cmp di, NINST
        jae .srd
        shl di, 2
        mov ax, [es:si]
        mov [inst_ptr + di], ax
        mov ax, [es:si + 2]
        mov [inst_ptr + di + 2], ax
        shr di, 1
        mov ax, [es:si + 4]
        mov [inst_len + di], ax
        inc word [n_inst]
        add si, 6
        jmp .sr
.srd:   pop bx
        pop es
        les bx, [es:bx + 2]             ; the next startup info
        jmp .si
.sid:   ; (all items must fit our buffers)
        xor ax, ax
        xor bx, bx
.sz:    cmp bx, [n_inst]
        jae .szd
        push bx
        shl bx, 1
        add ax, [inst_len + bx]
        pop bx
        inc bx
        jmp .sz
.szd:   cmp ax, INSTBUF
        jbe .szok
        mov word [n_inst], 0
.szok:  push cs
        pop es
        mov si, inst0
        call inst_save
        ; the vectors and PICs as they are now
        cli
        push ds
        xor si, si
        mov ds, si
        mov di, ivt0
        mov cx, 512
        rep movsw
        pop ds
        in al, 0A1h
        mov ah, al
        in al, 21h
        mov [pic0], ax
        sti
        ; keep T only
        mov bx, t_end
        add bx, 15
        mov cl, 4
        shr bx, cl
        mov es, [psp]
        mov ah, 4Ah
        int 21h
        mov ax, 0C00h
        int 21h
        mov ax, 3300h
        int 21h
        mov [brk], dl
        ; our hooks
        mov ax, 3521h
        int 21h
        mov [old21], bx
        mov [old21 + 2], es
        mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        mov dx, int21
        mov ax, 2521h
        int 21h
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        ; MS-DOS 5 task switcher clients: Windows' session is created and
        ; active (5, then 3 and 4 with CX=1, as Windows 3.1's WSWAP)
        mov word [n_cx], 0
        mov ax, 5
        call notify
        mov word [n_cx], 1
        mov ax, 3
        call notify
        mov ax, 4
        call notify
        ; DOSX
        mov ax, [2Ch]
        mov [exec_pb], ax
        mov [exec_pb + 4], cs
        mov [exec_pb + 8], cs
        mov [exec_pb + 12], cs
        mov dx, exec_path
        mov bx, exec_pb
        push cs
        pop es
        mov [save_ss], ss
        mov [save_sp], sp
        mov ax, 4B00h
        int 21h
        cli
        mov ss, [cs:save_ss]
        mov sp, [cs:save_sp]
        sti
        push cs
        pop ds
        push cs
        pop es
        cld
        jnc .ran
        mov dx, m_nodosx
        mov ah, 09h
        int 21h
        mov byte [exit_code], 0FFh
        jmp .end
.ran:   mov ah, 4Dh
        int 21h
        mov [exit_code], al
.end:   ; Windows has ended (Windows 3.1's WSWAP: the MS-DOS 5 clients
        ; told (6, destroy session, CX=1), 16Dh cleared, SI=5, SI=2 -
        ; WIN.COM frees record 15 and counts 17Ch down -, SI=5, 178h bit
        ; 08h, SI=4; Windows' swap file number at 173h, its exit code at
        ; 171h)
        mov word [n_cx], 1
        mov ax, 6
        call notify
        call get_blk
        mov al, [exit_code]
        mov [es:di + B_RC], al
        mov word [es:di + B_WINSID], 0
        mov si, SW_BLOCK
        call sw_call
        mov si, SW_END                  ; (BX=0, CX=10h, DX the block's
        xor bx, bx                      ; segment, as Windows 3.1's)
        mov cx, 10h
        call sw_call
        mov si, SW_BLOCK
        call sw_call
        call get_blk
        or byte [es:di + B_FLAGS], 08h
        mov si, SW_COUNT
        call sw_call
        call get_blk
        mov word [es:di + B_SID], (SESS_HI << 8) | SESS_WIN
        ; our hooks off
        push ds
        lds dx, [old21]
        mov ax, 2521h
        int 21h
        pop ds
        push ds
        lds dx, [old2f]
        mov ax, 252Fh
        int 21h
        pop ds
        mov dl, [brk]
        mov ax, 3301h
        int 21h
        mov al, [exit_code]
        mov ah, 4Ch
        int 21h

; delete the files matching BX (a name pattern) in the directory at ES:SI
clean:  push ds
        push es
        pop ds
        push cs
        pop es
        mov di, r_name
.c:     lodsb
        test al, al
        jz .e
        stosb
        jmp .c
.e:     pop ds
        push di                         ; where the name goes
        mov si, bx
.p:     lodsb
        stosb
        test al, al
        jnz .p
        mov dx, cl_dta
        mov ah, 1Ah
        int 21h
        mov dx, r_name
        mov cx, 3
        mov ah, 4Eh
.f:     int 21h
        jc .done
        pop di
        push di
        mov si, cl_dta + 1Eh
.n:     lodsb
        stosb
        test al, al
        jnz .n
        mov dx, r_name
        mov ax, 4301h
        xor cx, cx
        int 21h
        mov dx, r_name
        mov ah, 41h
        int 21h
        mov ah, 4Fh
        jmp .f
.done:  pop di
        mov dx, 80h
        mov ah, 1Ah
        int 21h
        ret
s_woa:  db '~WOA1*.*', 0
s_grb:  db '~GRB*.*', 0
cl_dta: times 43 db 0

m_noswitch: db 'w16 is not running in standard mode.', 0Dh, 0Ah, '$'
m_nodosx:   db 'Cannot find DOSX.EXE.', 0Dh, 0Ah, '$'

t_end:
