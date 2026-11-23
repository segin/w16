; dx9.asm - DX9.COM: the DOSX audit's probe (ARCHITECTURE.md 9.6; its
; triage is in git history). One behaviour per run, chosen by the command tail (WIN /S
; XX: the first hex number in KERNEL's tail), as DX5: several of them end
; the extender, so each gets a DOSBox run of its own; what DOSX prints
; and WIN's errorlevel are in the run's output (tests/dosxtest.sh -a XX
; DX9; the boot-test case dosxaudit runs them all under Microsoft's DOSX
; and w16's).
;
; Black-box observation (ARCHITECTURE.md section 3). Lines "OBS ~name"
; are informational (selector values, linear addresses), not compared.
;
;   01  INT 31h 0001h freeing the selector that is in ES
;   02  INT 31h 0001h freeing the selector that is in DS
;   03  INT 31h 0502h freeing the block whose handle selector is in ES
;   04  INT 21h 49h with the block's selector in DS as well as in ES
;   05  INT 31h 0009h making the descriptor in ES not present
;   06  INT 31h 000Ch writing a not-present descriptor for the one in DS
;   07  INT 21h AH=00h from protected mode
;   08  FS and GS in real-mode code: at a 0301h procedure's entry (the
;       client's FS = its data selector, GS = 0), across timer ticks while
;       the client hooks IRQ 0 (0205h 08h) and while it does not, and the
;       client's FS and GS after each call; whether the hook is called
;   09  a general protection fault whose IP is the value of a code
;       selector with a small limit (error code FFF4h), with a 0203h
;       handler; a 0205h handler for 0Dh as well
;   0A  the same, the code selector's bytes at the error code - 2 being
;       CD 0Dh ("INT 0Dh")
;   0B  the same as 09 with the code selector not present
;   0C  software INT 09h with a 0205h handler for 09h
;   0D  a real-mode callback (0303h) entered with DF set
;   0E  INT 13h AH=02h of 1, 8, 9, 80h and 81h sectors from drive 0 (the
;       floppy image tests/dosxtest.sh mounts: sector n begins with the
;       word n) into extended memory; AH=03h of 9 and read back
;   0F  INT 25h/26h of 1, 8, 9, 10h, 80h and 81h sectors, and packets
;       of 10h and 80h, from/to drive C: into extended memory
;   10  INT 21h AH=09h of a string of 6000 bytes
;   11  INT 21h 3Fh/40h of 2000h bytes from offset F000h of a 64 KB
;       selector, and from offset 7000h of one with limit 7FFFh
;   12  INT 21h 3Fh of 2000h bytes at offset F000h of a selector with
;       limit 1FFFFh
;   13  INT 10h 4F00h, 4F01h, 1C00h and 1C01h with buffers in extended
;       memory
;   14  INT 21h 4B01h (load, do not run) of the probe itself: what the
;       parameter block holds afterwards at 0Eh-15h (SS:SP, CS:IP)
;   15  DOS errors with output buffers: 47h of drive Y:, 5F02h of entry
;       1000, 38h of country FFFEh (the buffers filled with 5Ah before);
;       3Fh of 0 bytes from handle 99; 32h of drive Y: (DS afterwards);
;       11h (FCB search) of a name that is not there (the DTA, filled with
;       5Ah before)
;   16  INT 31h 0002h for 100 segments, then the 10th and the 90th again;
;       0008h with the limit 00100000h (the descriptor's bytes)
;   17  INT 25h of 1, 9 and 80h sectors and a packet of 80h from drive A:
;       (the floppy image: under DOSBox-X, tests/dosxtest.sh -x, whose
;       INT 25h reads FAT drives), and INT 26h back
;   20  INT 21h AH=0Ah with DS:DX past DS's limit
;   21  INT 25h and 26h packets whose buffer selector is not present
;   23  INT 31h 0000h with CX = F000h (how long it takes, in ticks)
;   24  an exception handler (0203h) that returns with CS = a GDT
;       ring-0 selector (RPL 0)

        bits 16
        cpu 386
        org 100h

%define LOGNAME 'C:\DX9.LOG'
%include "dxlib.inc"

start:  mov ax, cs
        mov ds, ax
        mov es, ax
        cli
        mov ss, ax
        mov sp, stack_top
        sti
        cld
        mov [psp], ax
        mov bx, 1000h
        mov ah, 4Ah
        int 21h
        call logcreate
        NOTE 'DX9 probe'
        call tailhex
        mov [fn], ax
        OBS 'fn', ax
        call flush
        mov ax, 1687h
        int 2Fh
        mov [pment], di
        mov [pment+2], es
        push ds
        pop es
        or si, si
        jz .nohd
        mov bx, si
        mov ah, 48h
        int 21h
        mov es, ax
.nohd:  xor ax, ax
        call far [pment]
        jnc pm
        NOTE 'DX9 switch failed'
        call flush
        mov ax, 4C01h
        int 21h

pm:     push ds
        pop es
        mov [datasel], ds
        ; our own linear base
        mov bx, ds
        mov ax, 0006h
        int 31h
        mov [ourlin], dx
        mov [ourlin+2], cx
        NOTE 'DX9 protected mode'
        call flush
        mov bx, [fn]
        cmp bx, (jtend - jt) / 2
        jae .none
        shl bx, 1
        jmp [jt+bx]
.none:  NOTE 'DX9 unknown test'
        jmp done

jt:     dw done, t01, t02, t03, t04, t05, t06, t07
        dw t08, t09, t0a, t0b, t0c, t0d, t0e, t0f
        dw t10, t11, t12, t13, t14, t15, t16, t17
        dw done, done, done, done, done, done, done, done
        dw t20, t21, done, t23, t24
jtend:

; ---------------------------------------------------------------------------
; helpers

; newsel: a selector with base [nb], limit [nl], access rights [nr]
; (0009h first: on a 386 it writes the granularity bit that 0008h sets)
; -> AX; CF on failure
newsel: push bx
        push cx
        push dx
        mov cx, 1
        xor ax, ax
        int 31h
        jc .r
        mov bx, ax
        mov cx, [nr]
        mov ax, 0009h
        int 31h
        mov dx, [nb]
        mov cx, [nb+2]
        mov ax, 0007h
        int 31h
        mov dx, [nl]
        mov cx, [nl+2]
        mov ax, 0008h
        int 31h
        mov ax, bx
        clc
.r:     pop dx
        pop cx
        pop bx
        ret

; blk64: a block of 11000h bytes (0501h) -> [blklin]; [s64] a data
; selector over it with limit FFFFh. CF on failure.
blk64:  mov bx, 1
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        jc .r
        mov [blklin], cx
        mov [blklin+2], bx
        mov [blkh], di
        mov [blkh+2], si
        mov [nb], cx
        mov [nb+2], bx
        mov word [nl], 0FFFFh
        mov word [nl+2], 0
        mov word [nr], 00F2h
        call newsel
        jc .r
        mov [s64], ax
.r:     ret

; fill64: ES = [s64], 8000h words of AX
fill64: push es
        push cx
        push di
        mov es, [s64]
        xor di, di
        mov cx, 8000h
        rep stosw
        pop di
        pop cx
        pop es
        ret

; rmcall: the real-mode procedure at our segment:AX with INT 31h 0301h
; (DS = ES = our segment there), on DOSX's stack; registers from rmregs
rmcall: push es
        mov [rm_ip], ax
        mov ax, [psp]
        mov [rm_cs], ax
        mov [rm_ds], ax
        mov [rm_es], ax
        xor ax, ax
        mov [rm_ss], ax
        mov [rm_sp], ax
        mov word [rm_fl], 0202h
        push ds
        pop es
        mov di, rmregs
        xor bx, bx
        xor cx, cx
        mov ax, 0301h
        int 31h
        pop es
        ret

; ticks: AX = the BIOS tick count's low word
ticks:  push es
        push bx
        mov bx, 0040h
        mov ax, 0002h
        int 31h
        mov es, ax
        mov ax, [es:6Ch]
        pop bx
        pop es
        ret

; mkfile: C:\DX9.DAT, 3000h bytes, the word at offset 2i being i -> BX
; open, at its start (CF on failure)
mkfile: mov di, patbuf
        xor ax, ax
        mov cx, 1800h
.p:     stosw
        inc ax
        loop .p
        mov dx, s_dat
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .r
        mov bx, ax
        mov dx, patbuf
        mov cx, 3000h
        mov ah, 40h
        int 21h
        jc .r
        xor cx, cx
        xor dx, dx
        mov ax, 4200h
        int 21h
.r:     ret

; ---------------------------------------------------------------------------
; 01-06: a selector freed or spoilt while a segment register holds it

t01:    xor ax, ax
        mov cx, 1
        int 31h
        mov [t1], ax
        OBS '~sel', ax
        call flush
        mov es, ax
        mov bx, ax
        mov ax, 0001h
        int 31h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_es], es
        push ds
        pop es
        jmp seg_report

t02:    xor ax, ax
        mov cx, 1
        int 31h
        mov [t1], ax
        OBS '~sel', ax
        call flush
        mov bx, ax
        mov ax, 0001h
        mov ds, bx
        int 31h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ds], ds
        push ss
        pop ds
        jmp seg_report

t03:    xor bx, bx
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        jc near fail
        mov [t1], si
        OBS '~handle', si
        lar ax, si
        jnz near fail
        call flush
        mov es, si
        mov ax, 0502h
        int 31h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_es], es
        push ds
        pop es
        jmp seg_report

t04:    mov bx, 100h
        mov ah, 48h
        int 21h
        jc near fail
        mov [t1], ax
        OBS '~sel', ax
        call flush
        mov es, ax
        mov ds, ax
        mov ah, 49h
        int 21h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ds], ds
        mov [ss:r_es], es
        push ss
        pop ds
        push ds
        pop es
        jmp seg_report

t05:    xor ax, ax
        mov cx, 1
        int 31h
        mov [t1], ax
        call flush
        mov es, ax
        mov bx, ax
        mov cx, 0072h           ; data, DPL 3, not present
        mov ax, 0009h
        int 31h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_es], es
        push ds
        pop es
        jmp seg_report

t06:    xor ax, ax
        mov cx, 1
        int 31h
        mov [t1], ax
        call flush
        mov di, npdesc
        mov bx, ax
        mov ax, 000Ch
        mov ds, bx
        int 31h
        mov [ss:r_ax], ax
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ds], ds
        push ss
        pop ds
        ; fall through

; seg_report: what came back. DS and ES are logged as "the selector", 0
; or something else; r_ds/r_es are FFFFh where not taken.
seg_report:
        mov ax, [r_fl]
        and ax, 1
        OBS 'cf', ax
        OBS '~ax', [r_ax]
        mov ax, [r_ds]
        call segrel
        OBS 'ds', ax
        mov ax, [r_es]
        call segrel
        OBS 'es', ax
        mov ax, [t1]
        lar dx, ax
        mov ax, 0
        jnz .nl
        mov ax, dx
.nl:    OBS 'lar_after', ax
        jmp done
; AX = a segment register's value -> AX = 5E15h (the probe's selector), 0,
; FFFFh (not taken) or 0BADh (something else)
segrel: cmp ax, 0FFFFh
        je .r
        cmp ax, [t1]
        je .s
        or ax, ax
        jz .r
        mov ax, 0BADh
        ret
.s:     mov ax, 5E15h
.r:     ret

; ---------------------------------------------------------------------------
; 07: INT 21h AH=00h from protected mode
t07:    NOTE 'int 21h 00h next'
        call flush
        mov ah, 00h
        int 21h
        mov ds, [cs:datasel]
        NOTE 'after int 21h 00h'
        jmp done

; ---------------------------------------------------------------------------
; 08: FS and GS in real mode
t08:    mov fs, [datasel]
        xor ax, ax
        mov gs, ax
        mov ax, 0FFFFh
        mov [rm_fs0], ax
        mov [rm_gs0], ax
        mov [rm_fs1], ax
        mov [rm_gs1], ax
        mov ax, rm_t08
        call rmcall
        mov ax, fs
        call segrel_fs
        OBS 'pm_fs_after1', ax
        mov ax, gs
        OBS 'pm_gs_after1', ax
        OBS 'plain_fs0', [rm_fs0]
        OBS 'plain_gs0', [rm_gs0]
        OBS 'plain_fs1', [rm_fs1]
        OBS 'plain_gs1', [rm_gs1]
        OBS 'plain_waited', [rm_ok]
        call flush
        ; IRQ 0 hooked in protected mode
        mov bl, 08h
        mov ax, 0204h
        int 31h
        mov [old08], dx
        mov [old08+2], cx
        mov bl, 08h
        mov cx, cs
        mov dx, pm08
        mov ax, 0205h
        int 31h
        mov word [cnt08], 0
        mov ax, 0FFFFh
        mov [rm_fs0], ax
        mov [rm_gs0], ax
        mov [rm_fs1], ax
        mov [rm_gs1], ax
        mov ax, rm_t08
        call rmcall
        mov ax, [cnt08]
        mov [t1], ax
        mov ax, fs
        call segrel_fs
        mov [t2], ax
        mov ax, gs
        mov [t3], ax
        mov bl, 08h
        mov dx, [old08]
        mov cx, [old08+2]
        mov ax, 0205h
        int 31h
        OBS 'hook_fs0', [rm_fs0]
        OBS 'hook_gs0', [rm_gs0]
        OBS 'hook_fs1', [rm_fs1]
        OBS 'hook_gs1', [rm_gs1]
        OBS 'hook_waited', [rm_ok]
        mov ax, [t1]
        cmp ax, 2
        jb .few
        mov ax, 2               ; "at least 2"
.few:   OBS '~pm_ticks_seen', ax  ; (Windows 3.1's DOSX: 0, w16's 2)
        OBS 'pm_fs_after2', [t2]
        OBS 'pm_gs_after2', [t3]
        jmp done
; segrel_fs: AX = FS's value -> 5E15h if it is our data selector
segrel_fs:
        cmp ax, [datasel]
        jne .r
        mov ax, 5E15h
.r:     ret

pm08:   push ds
        mov ds, [cs:datasel]
        inc word [cnt08]
        pop ds
        jmp far [cs:old08]

; real mode (DS = our segment): FS and GS at entry, then 1234h/5678h
; across three timer ticks
rm_t08: mov ax, fs
        mov [rm_fs0], ax
        mov ax, gs
        mov [rm_gs0], ax
        mov ax, 1234h
        mov fs, ax
        mov ax, 5678h
        mov gs, ax
        mov word [rm_ok], 0
        push es
        mov ax, 40h
        mov es, ax
        mov bx, [es:6Ch]
        mov ecx, 100000000
        sti
.w:     mov ax, [es:6Ch]
        sub ax, bx
        cmp ax, 3
        jae .ok
        dec ecx
        jnz .w
        jmp .t
.ok:    mov word [rm_ok], 1
.t:     cli
        pop es
        mov ax, fs
        mov [rm_fs1], ax
        mov ax, gs
        mov [rm_gs1], ax
        retf

; ---------------------------------------------------------------------------
; 09, 0A, 0B: a general protection fault (mov es, FFF7h: error code
; FFF4h) at IP = the value of a code selector A
t09:    mov word [nr], 00FAh    ; readable code, present
        mov ax, [ourlin]
        mov [nb], ax
        mov ax, [ourlin+2]
        mov [nb+2], ax
        mov word [nl], 0Fh
        mov word [nl+2], 0
        call newsel
        jc near fail
        jmp gp_at
t0b:    mov word [nr], 007Ah    ; readable code, not present
        mov ax, [ourlin]
        mov [nb], ax
        mov ax, [ourlin+2]
        mov [nb+2], ax
        mov word [nl], 0Fh
        mov word [nl+2], 0
        call newsel
        jc near fail
        jmp gp_at
t0a:    call blk64
        jc near fail
        ; CD 0D at the block's start, A based 0FFF2h below it
        push es
        mov es, [s64]
        mov word [es:0], 0DCDh
        pop es
        mov eax, [blklin]
        sub eax, 0FFF2h
        mov [nb], eax
        mov word [nl], 0FFFFh
        mov word [nl+2], 0
        mov word [nr], 00FAh
        call newsel
        jc near fail
        ; fall through
; gp_at: AX = A. A code selector B maps gp_code at offset A; the 0203h
; and 0205h handlers for 0Dh are set; then far call to B:A.
gp_at:  mov [sela], ax
        OBS '~sela', ax
        movzx eax, ax
        mov edx, [ourlin]
        add edx, gp_code
        sub edx, eax
        mov [nb], edx
        mov word [nl], 0FFFFh
        mov word [nl+2], 0
        mov word [nr], 00FAh
        call newsel
        jc near fail
        mov [selb], ax
        mov bl, 0Dh
        mov cx, cs
        mov dx, gph
        mov ax, 0203h
        int 31h
        mov bl, 0Dh
        mov cx, cs
        mov dx, gpint
        mov ax, 0205h
        int 31h
        mov word [hit], 0
        NOTE 'fault next'
        call flush
        mov bx, 0FFF7h
        push word [selb]
        push word [sela]
        mov bp, sp
        call far [bp]
        add sp, 4
        mov ds, [cs:datasel]
        push ds
        pop es
        OBS 'exc_hit', [hit]
        OBS 'f_err', [fr+4]
        mov ax, [fr+6]
        sub ax, [sela]
        OBS 'f_ip_minus_a', ax
        mov ax, [fr+8]
        sub ax, [selb]
        OBS 'f_cs_minus_b', ax
        jmp done

; the faulting code, reached as B:A
gp_code:
        mov es, bx
        retf

; gph: the 0203h handler for 0Dh: logs the frame, skips "mov es, bx"
gph:    push bp
        mov bp, sp
        push ds
        push ax
        push si
        mov ds, [cs:datasel]
        inc word [hit]
        xor si, si
.cp:    mov ax, [ss:bp+2+si]
        mov [fr+si], ax
        add si, 2
        cmp si, 18
        jb .cp
        add word [ss:bp+2+6], 2
        pop si
        pop ax
        pop ds
        pop bp
        retf

; gpint: the 0205h handler for 0Dh: reached by the fault, it reports and
; ends (the frame cannot be returned through)
gpint:  mov ds, [cs:datasel]
        mov bp, sp
        NOTE 'int 0Dh handler reached'
        OBS '~w0', [ss:bp]
        OBS '~w2', [ss:bp+2]
        OBS '~w4', [ss:bp+4]
        OBS '~w6', [ss:bp+6]
        NOTE 'DX9 end'
        call flush
        mov ax, 4C02h
        int 21h

; ---------------------------------------------------------------------------
; 0C: software INT 09h
t0c:    mov bl, 09h
        mov ax, 0204h
        int 31h
        mov [old08], dx
        mov [old08+2], cx
        mov bl, 09h
        mov cx, cs
        mov dx, h09
        mov ax, 0205h
        int 31h
        mov word [hit], 0
        NOTE 'int 09h next'
        call flush
        int 09h
        mov ds, [cs:datasel]
        mov ax, [hit]
        mov [t1], ax
        mov bl, 09h
        mov dx, [old08]
        mov cx, [old08+2]
        mov ax, 0205h
        int 31h
        OBS 'int09_handler_hit', [t1]
        jmp done
h09:    push ds
        mov ds, [cs:datasel]
        inc word [hit]
        pop ds
        iret

; ---------------------------------------------------------------------------
; 0D: a real-mode callback entered with DF set
t0d:    push es
        mov ax, ds
        mov es, ax
        mov di, cbregs
        push ds
        mov ax, cs
        mov ds, ax
        mov si, cbproc
        mov ax, 0303h
        int 31h
        pop ds
        pop es
        jc near fail
        mov [cbaddr], dx
        mov [cbaddr+2], cx
        mov word [hit], 0
        mov word [c0ddone], 0
        NOTE 'callback next'
        call flush
        mov ax, rm_t0d
        call rmcall
        mov ds, [cs:datasel]
        cld
        OBS 'cb_hit', [hit]
        OBS 'cb_df', [cbdf]
        OBS 'rm_done', [c0ddone]
        OBS 'rm_df_after', [c0ddf]
        jmp done

; real mode: STD, far call the callback, note DF after it
rm_t0d: std
        call far [cbaddr]
        pushf
        pop ax
        and ax, 0400h
        mov [c0ddf], ax
        cld
        mov word [c0ddone], 1
        retf

; the callback's procedure: DS:SI the real-mode stack, ES:DI the
; structure
cbproc: pushf
        pop ax
        and ax, 0400h
        push ds
        mov ds, [cs:datasel]
        mov [cbdf], ax
        inc word [hit]
        pop ds
        cld
        lodsw
        mov [es:di+2Ah], ax
        lodsw
        mov [es:di+2Ch], ax
        add word [es:di+2Eh], 4
        iret

; ---------------------------------------------------------------------------
; 0E: INT 13h from drive 0
t0e:    call blk64
        jc near fail
        mov byte [sr_info], 1
        mov si, n0e
.l:     lodsb
        or al, al
        jz .w
        mov [t1], al
        push si
        mov ax, 0EEEEh
        call fill64
        xor ax, ax              ; reset
        xor dx, dx
        int 13h
        mov ah, 02h
        mov al, [t1]
        mov cx, 0105h           ; cylinder 1, sector 5: LBA 40 (28h)
        xor dx, dx
        mov es, [s64]
        xor bx, bx
        int 13h
        mov ds, [cs:datasel]
        push ds
        pop es
        call cfax
        OBSB 'n', [t1]
        call sect_report
        call flush
        pop si
        jmp .l
.w:     ; write 9 sectors (sector k of them holding 1000h + k), read back
        push es
        mov es, [s64]
        xor di, di
        mov ax, 1000h
.wf:    mov [es:di], ax
        inc ax
        add di, 200h
        cmp di, 9 * 200h
        jb .wf
        pop es
        mov ax, 0309h
        mov cx, 0105h
        xor dx, dx
        mov es, [s64]
        xor bx, bx
        int 13h
        mov ds, [cs:datasel]
        push ds
        pop es
        call cfax
        mov ax, 0EEEEh
        call fill64
        mov ax, 0209h
        mov cx, 0105h
        xor dx, dx
        mov es, [s64]
        xor bx, bx
        int 13h
        mov ds, [cs:datasel]
        push ds
        pop es
        call cfax
        mov byte [t1], 9
        call sect_report
        jmp done

; cfax: logs CF and AX as they are
cfax:   pushf
        push ax
        mov ax, 0
        adc ax, 0
        OBS 'cf', ax
        pop ax
        OBS 'ax', ax
        popf
        ret

; sect_report: [t1] sectors read into s64: the first word of the first,
; the 9th and the last, and the word past them (EEEEh if untouched)
sect_report:
        push es
        mov es, [s64]
        OBS 's0', [es:0]
        movzx bx, byte [t1]
        cmp bx, 9
        jb .s
        OBS 's8', [es:8*200h]
        cmp bx, 21h
        jb .s
        cmp byte [sr_info], 0
        jne .i
        OBS 's31', [es:31*200h]
        OBS 's32', [es:32*200h]
        jmp .s
.i:     ; (INT 13h past 32 sectors: Windows 3.1's DOSX gives sectors from
        ; elsewhere, w16's the disk's; logged, not compared)
        OBS '~s31', [es:31*200h]
        OBS '~s32', [es:32*200h]
        cmp bx, 80h
        ja .e
        dec bx
        shl bx, 9
        OBS '~slast', [es:bx]
        jmp .e
.s:     cmp bx, 80h
        ja .e
        dec bx
        shl bx, 9
        OBS 'slast', [es:bx]
        add bx, 200h
        jz .e
        OBS 'past', [es:bx]
.e:     pop es
        ret

; ---------------------------------------------------------------------------
; 0F: INT 25h/26h on drive C:
t0f:    call blk64
        jc near fail
        mov si, n0f
.l:     lodsw
        or ax, ax
        jz .pk
        mov [t1], ax
        push si
        mov al, 2
        mov cx, [t1]
        xor dx, dx
        mov ds, [s64]
        xor bx, bx
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        OBS 'n25', [t1]
        call cf_r
        mov al, 2
        mov cx, [t1]
        xor dx, dx
        mov ds, [s64]
        xor bx, bx
        int 26h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        OBS 'n26', [t1]
        call cf_r
        call flush
        pop si
        jmp .l
.pk:    ; packets: sector 0, the count, the buffer s64:0
        mov si, n0fp
.pl:    lodsw
        or ax, ax
        jz near done
        push si
        mov [pkt+4], ax
        mov dword [pkt], 0
        mov word [pkt+6], 0
        mov ax, [s64]
        mov [pkt+8], ax
        OBS 'pkt', [pkt+4]
        mov ax, 1102h           ; (AH: a known value)
        mov cx, 0FFFFh
        mov bx, pkt
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        call cf_r
        call flush
        pop si
        jmp .pl
cf_r:   mov ax, [r_fl]
        and ax, 1
        OBS 'cf', ax
        OBS 'ax', [r_ax]
        ret

; ---------------------------------------------------------------------------
; 10: INT 21h AH=09h, 6000 bytes: 75 lines of 78 characters and CR LF,
; then "END$"
t10:    mov di, patbuf
        mov dx, 75
.ln:    mov cx, 78
        mov al, 'a'
.ch:    stosb
        inc al
        cmp al, 'z' + 1
        jb .n
        mov al, 'a'
.n:     loop .ch
        mov ax, 0A0Dh
        stosw
        dec dx
        jnz .ln
        mov dword [di], 'END$'
        NOTE 'string next'
        call flush
        mov dx, patbuf
        mov ah, 09h
        int 21h
        mov ds, [cs:datasel]
        NOTE 'after the string'
        jmp done

; ---------------------------------------------------------------------------
; 11: 3Fh/40h past a segment's end
t11:    call blk64
        jc near fail
        call mkfile
        jc near fail
        mov [fh], bx
        ; (a) read 2000h at s64:F000h
        mov ax, 0EEEEh
        call fill64
        mov ax, 3F00h
        mov cx, 2000h
        mov dx, 0F000h
        mov ds, [s64]
        int 21h
        mov ds, [cs:datasel]
        call cfax
        push es
        mov es, [s64]
        OBS 'r_0000', [es:0]
        OBS 'r_0FFE', [es:0FFEh]
        OBS 'r_1000', [es:1000h]
        OBS 'r_F000', [es:0F000h]
        OBS 'r_FFFE', [es:0FFFEh]
        pop es
        call flush
        ; (b) through a selector of limit 7FFFh at 7000h
        mov eax, [blklin]
        mov [nb], eax
        mov word [nl], 7FFFh
        mov word [nl+2], 0
        mov word [nr], 00F2h
        call newsel
        jc near fail
        mov [t1], ax
        mov ax, 0EEEEh
        call fill64
        mov bx, [fh]
        xor cx, cx
        xor dx, dx
        mov ax, 4200h
        int 21h
        mov ax, 3F00h
        mov cx, 2000h
        mov dx, 7000h
        mov ds, [t1]
        int 21h
        mov ds, [cs:datasel]
        call cfax
        push es
        mov es, [s64]
        OBS 's_0000', [es:0]
        OBS 's_7000', [es:7000h]
        OBS 's_7FFE', [es:7FFEh]
        OBS 's_8000', [es:8000h]
        OBS 's_8FFE', [es:8FFEh]
        pop es
        call flush
        mov bx, [fh]
        mov ah, 3Eh
        int 21h
        ; (c) write 2000h from s64:F000h, (d) from [t1]:7000h
        push es
        mov es, [s64]
        xor di, di
        xor ax, ax
        mov cx, 8000h
.f:     stosw
        inc ax
        loop .f
        pop es
        mov si, [s64]
        mov di, 0F000h
        call wtest
        mov si, [t1]
        mov di, 7000h
        call wtest
        jmp done
; wtest: write 2000h bytes from SI:DI to C:\DX9W.DAT; log the result,
; the file's size and its words at 0, FFEh, 1000h and 1FFEh
wtest:  mov dx, s_datw
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc near fail
        mov bx, ax
        mov ax, 4000h
        mov cx, 2000h
        mov dx, di
        mov ds, si
        int 21h
        mov ds, [cs:datasel]
        call cfax
        xor cx, cx
        xor dx, dx
        mov ax, 4202h
        int 21h
        OBS 'w_size', ax
        xor cx, cx
        xor dx, dx
        mov ax, 4200h
        int 21h
        mov di, patbuf
        mov cx, 2000h / 2
        xor ax, ax
        rep stosw
        mov dx, patbuf
        mov cx, 2000h
        mov ah, 3Fh
        int 21h
        OBS 'w_0000', [patbuf]
        OBS 'w_0FFE', [patbuf+0FFEh]
        OBS 'w_1000', [patbuf+1000h]
        OBS 'w_1FFE', [patbuf+1FFEh]
        mov ah, 3Eh
        int 21h
        call flush
        ret

; ---------------------------------------------------------------------------
; 12: 3Fh at F000h of a selector with limit 1FFFFh
t12:    mov bx, 2
        mov cx, 1000h
        mov ax, 0501h
        int 31h
        jc near fail
        mov [blklin], cx
        mov [blklin+2], bx
        mov [nb], cx
        mov [nb+2], bx
        mov word [nl], 0FFFFh
        mov word [nl+2], 1
        mov word [nr], 00F2h
        call newsel
        jc near fail
        mov [t1], ax
        mov word [nl+2], 0
        call newsel
        jc near fail
        mov [s64], ax
        mov eax, [blklin]
        add eax, 10000h
        mov [nb], eax
        call newsel
        jc near fail
        mov [selb], ax
        mov ax, 0EEEEh
        call fill64
        push es
        mov es, [selb]
        xor di, di
        mov cx, 8000h
        rep stosw
        pop es
        call mkfile
        jc near fail
        mov ax, 3F00h
        mov cx, 2000h
        mov dx, 0F000h
        mov ds, [t1]
        int 21h
        mov ds, [cs:datasel]
        call cfax
        push es
        mov es, [s64]
        OBS 'lo_0000', [es:0]
        OBS 'lo_F000', [es:0F000h]
        OBS 'lo_FFFE', [es:0FFFEh]
        mov es, [selb]
        OBS 'hi_0000', [es:0]
        OBS 'hi_0FFE', [es:0FFEh]
        OBS 'hi_1000', [es:1000h]
        pop es
        jmp done

; ---------------------------------------------------------------------------
; 13: INT 10h with pointers DOSX may not translate
t13:    call blk64
        jc near fail
        xor ax, ax
        call fill64
        mov ax, 4F00h
        mov es, [s64]
        xor di, di
        int 10h
        mov ds, [cs:datasel]
        push ds
        pop es
        OBS 'v4f00_ax', ax
        push es
        mov es, [s64]
        OBS 'v4f00_b0', [es:0]
        OBS 'v4f00_b2', [es:2]
        pop es
        call flush
        mov ax, 4F01h
        mov cx, 0101h
        mov es, [s64]
        mov di, 200h
        int 10h
        mov ds, [cs:datasel]
        push ds
        pop es
        OBS 'v4f01_ax', ax
        push es
        mov es, [s64]
        OBS 'v4f01_b0', [es:200h]
        OBS 'v4f01_b10', [es:210h]
        pop es
        call flush
        mov ax, 1C00h
        mov cx, 7
        int 10h
        OBS 'v1c00_ax', ax
        OBS 'v1c00_bx', bx
        call flush
        mov ax, 1C01h
        mov cx, 7
        mov es, [s64]
        mov bx, 1000h
        int 10h
        mov ds, [cs:datasel]
        push ds
        pop es
        OBS 'v1c01_ax', ax
        push es
        mov es, [s64]
        OBS 'v1c01_b0', [es:1000h]
        OBS 'v1c01_b2', [es:1002h]
        pop es
        jmp done

; ---------------------------------------------------------------------------
; 14: INT 21h 4B01h of the probe itself (C:\WINDOWS\SYSTEM\KRNL286.EXE)
t14:    mov ah, 62h
        int 21h
        mov [t2], bx
        mov di, epb
        mov cx, 22
        mov al, 0A5h
        rep stosb
        mov word [epb], 0
        mov word [epb+2], tail0
        mov [epb+4], ds
        mov word [epb+6], fcb0
        mov [epb+8], ds
        mov word [epb+10], fcb0
        mov [epb+12], ds
        mov dx, s_self
        mov bx, epb
        mov ax, 4B01h
        int 21h
        call cfax
        mov ah, 62h
        int 21h
        mov [t3], bx
        OBS 'sp', [epb+0Eh]
        mov ax, [epb+10h]
        call kind
        OBS 'ss_kind', ax
        OBS 'ip', [epb+12h]
        mov ax, [epb+14h]
        call kind
        OBS 'cs_kind', ax
        mov ax, [epb+10h]
        cmp ax, [epb+14h]
        mov ax, 0
        jne .ne
        inc ax
.ne:    OBS 'ss_is_cs', ax
        mov ax, [t3]
        cmp ax, [t2]
        mov ax, 0
        je .same
        inc ax
.same:  OBS 'psp_changed', ax
        mov bx, [t2]            ; our PSP back as DOS's current one
        mov ah, 50h
        int 21h
        jmp done
; kind: AX = a segment word -> 0 for 0, 7 for an LDT selector of RPL 3
; (TI and RPL bits set), 1 otherwise
kind:   or ax, ax
        jz .r
        and ax, 7
        cmp ax, 7
        je .r
        mov ax, 1
.r:     ret

; ---------------------------------------------------------------------------
; 15: DOS errors and output buffers
t15:    call fill5a
        mov dl, 25              ; Y: (Z: is DOSBox's own drive)
        mov si, patbuf
        mov ah, 47h
        int 21h
        call cfax
        OBS 'b47', [patbuf]
        call flush
        call fill5a
        mov bx, 1000
        mov si, patbuf
        push ds
        pop es
        mov di, patbuf + 100h
        mov ax, 5F02h
        int 21h
        call cfax
        OBS 'b5f_si', [patbuf]
        OBS 'b5f_di', [patbuf+100h]
        call flush
        call fill5a
        mov ax, 38FEh
        mov bx, 0FFFEh
        mov dx, patbuf
        int 21h
        call cfax
        OBS '~b38', [patbuf]
        call flush
        mov bx, 99
        xor cx, cx
        mov dx, patbuf
        mov ah, 3Fh
        int 21h
        call cfax
        call flush
        mov dl, 25
        mov ah, 32h
        push ds
        int 21h
        mov [ss:r_ds], ds
        pop ds
        call cfax
        mov ax, [r_ds]
        cmp ax, [datasel]
        jne .o
        mov ax, 5E15h
        jmp .d
.o:     mov ax, 0BADh
.d:     OBS 'ds_32', ax
        call flush
        ; 11h (FCB search) of a name that is not there, the DTA full of
        ; 5Ah: what DOS's AL=FFh leaves in it
        call fill5a
        mov dx, patbuf
        mov ah, 1Ah
        int 21h
        mov si, fcbnone
        mov di, patbuf + 100h
        push es
        push ds
        pop es
        mov cx, 37
        rep movsb
        pop es
        mov dx, patbuf + 100h
        mov ah, 11h
        int 21h
        OBSB 'fcb11_al', al
        OBS '~fcb11_dta', [patbuf]
        mov ax, 0               ; written over (with what is in the DOS
        cmp word [patbuf], 5A5Ah ; extender's buffer), or left?
        je .kept
        inc ax
.kept:  OBS 'fcb11_dta_written', ax
        jmp done
fill5a: push es
        push ds
        pop es
        mov di, patbuf
        mov cx, 200h
        mov al, 5Ah
        rep stosb
        pop es
        ret

; ---------------------------------------------------------------------------
; 16: 0002h past the 79 descriptors kept for it
t16:    xor si, si
.l:     lea bx, [si+2000h]
        mov ax, 0002h
        int 31h
        jc .f
        mov bx, si
        shl bx, 1
        mov [patbuf+bx], ax
        inc si
        cmp si, 100
        jb .l
.f:     OBS 'mapped', si
        mov bx, 2000h + 10
        mov ax, 0002h
        int 31h
        cmp ax, [patbuf+20]
        mov ax, 0
        jne .n1
        inc ax
.n1:    OBS 'again10_same', ax
        mov bx, 2000h + 90
        mov ax, 0002h
        int 31h
        mov dx, ax
        cmp ax, [patbuf+180]
        mov ax, 0
        jne .n2
        inc ax
.n2:    OBS 'again90_same', ax
        mov ax, [patbuf+180]
        OBS '~sel90', ax
        OBS '~sel90again', dx
        mov ax, [patbuf+156]
        OBS '~sel78', ax
        mov ax, [patbuf+158]
        OBS '~sel79', ax
        ; 0008h with a limit above 1 MB that is not a page's end
        ; (00100000h): what the descriptor's limit bytes become
        xor ax, ax
        mov cx, 1
        int 31h
        mov bx, ax
        mov cx, 0010h
        xor dx, dx
        mov ax, 0008h
        int 31h
        call cfax
        push es
        push ds
        pop es
        mov di, patbuf
        mov ax, 000Bh
        int 31h
        pop es
        OBS 'lim_w0', [patbuf]
        mov al, [patbuf+6]
        OBSB 'lim_b6', al
        jmp done

; ---------------------------------------------------------------------------
; 17: INT 25h/26h from drive A: (DOSBox-X)
t17:    call blk64
        jc near fail
        mov si, n17
.l:     lodsw
        or ax, ax
        jz .pk
        mov [t1], ax
        push si
        mov ax, 0EEEEh
        call fill64
        mov al, 0
        mov cx, [t1]
        mov dx, 40
        mov ds, [s64]
        xor bx, bx
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        OBS 'n', [t1]
        call cf_r
        call sect_report
        call flush
        pop si
        jmp .l
.pk:    mov ax, 0EEEEh
        call fill64
        mov dword [pkt], 40
        mov word [pkt+4], 80h
        mov word [pkt+6], 0
        mov ax, [s64]
        mov [pkt+8], ax
        mov ax, 1100h           ; (AH: a known value)
        mov cx, 0FFFFh
        mov bx, pkt
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        mov word [t1], 80h
        NOTE 'packet'
        call cf_r
        call sect_report
        call flush
        ; INT 26h: 9 sectors (1000h + k) at 40, read back
        push es
        mov es, [s64]
        xor di, di
        mov ax, 1000h
.wf:    mov [es:di], ax
        inc ax
        add di, 200h
        cmp di, 9 * 200h
        jb .wf
        pop es
        mov al, 0
        mov cx, 9
        mov dx, 40
        mov ds, [s64]
        xor bx, bx
        int 26h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        NOTE 'write 9'
        call cf_r
        mov ax, 0EEEEh
        call fill64
        mov al, 0
        mov cx, 9
        mov dx, 40
        mov ds, [s64]
        xor bx, bx
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        mov word [t1], 9
        call cf_r
        call sect_report
        jmp done

; ---------------------------------------------------------------------------
; 20-24: malformed calls (w16's DOSX must survive them)
t20:    mov eax, [ourlin]
        mov [nb], eax
        mov dword [nl], 0Fh
        mov word [nr], 00F2h
        call newsel
        jc near fail
        mov [t1], ax
        mov ah, 05h             ; a CR in the keyboard buffer, in case
        mov cx, 1C0Dh           ; the call reads a line
        int 16h
        NOTE 'int 21h 0Ah next'
        call flush
        mov dx, 100h
        mov ah, 0Ah
        mov ds, [t1]
        int 21h
        mov ds, [cs:datasel]
        call cfax
        jmp done

t21:    call blk64
        jc near fail
        mov eax, [blklin]
        mov [nb], eax
        mov dword [nl], 0FFFFh
        mov word [nr], 0072h    ; not present
        call newsel
        jc near fail
        mov [pkt+8], ax
        mov dword [pkt], 0
        mov word [pkt+4], 1
        mov word [pkt+6], 0
        NOTE 'int 25h packet, buffer not present, next'
        call flush
        mov al, 2
        mov cx, 0FFFFh
        mov bx, pkt
        int 25h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        call cf_r
        NOTE 'int 26h packet, buffer not present, next'
        call flush
        mov al, 2
        mov cx, 0FFFFh
        mov bx, pkt
        int 26h
        pushf
        pop word [ss:r_fl]
        mov [ss:r_ax], ax
        add sp, 2
        mov ds, [cs:datasel]
        call cf_r
        jmp done

t23:    call ticks
        mov [t1], ax
        mov cx, 0F000h
        xor ax, ax
        int 31h
        pushf
        push ax
        call ticks
        sub ax, [t1]
        mov dx, ax
        pop ax
        popf
        call cfax
        cmp dx, 18
        mov ax, 0
        jb .q
        mov ax, 1
.q:     OBS 'a_second_or_more', ax
        jmp done

t24:    mov bl, 0Dh
        mov cx, cs
        mov dx, gph24
        mov ax, 0203h
        int 31h
        NOTE 'fault next'
        call flush
        mov bx, 0FFF7h
        mov es, bx
        mov ds, [cs:datasel]
        NOTE 'after the fault'
        jmp done
gph24:  push bp
        mov bp, sp
        mov word [ss:bp+2+8], 0070h     ; CS = a GDT ring-0 selector
        pop bp
        retf

; ---------------------------------------------------------------------------
fail:   mov ds, [cs:datasel]
        NOTE 'DX9 set-up failed'
done:   mov ds, [cs:datasel]
        NOTE 'DX9 end'
        call flush
        mov ax, 4C00h
        int 21h

%include "dxlib.inc"

datasel dw 0
hit     dw 0
cbdf    dw 0
sr_info db 0
fr      times 9 dw 0
r_ax    dw 0
r_fl    dw 0
r_ds    dw 0FFFFh
r_es    dw 0FFFFh
s_dat   db 'C:\DX9.DAT', 0
s_datw  db 'C:\DX9W.DAT', 0
n0e     db 1, 8, 9, 80h, 81h, 0
n0f     dw 1, 8, 9, 10h, 80h, 81h, 0
n0fp    dw 10h, 80h, 0
n17     dw 1, 9, 80h, 0
s_self  db 'C:\WINDOWS\SYSTEM\KRNL286.EXE', 0
tail0   db 0, 13
fcbnone db 0, 'NOSUCH  XYZ'
        times 25 db 0
fcb0    times 16 db 0
; a not-present data descriptor (000Ch): limit FFFFh, base 0, access 72h
npdesc  dw 0FFFFh, 0, 7200h, 0
old08   dd 0
cbaddr  dd 0
rm_fs0  dw 0
rm_gs0  dw 0
rm_fs1  dw 0
rm_gs1  dw 0
rm_ok   dw 0
c0ddone dw 0
c0ddf   dw 0
cnt08   dw 0

        section .bss
psp     resw 1
fn      resw 1
t1      resw 1
t2      resw 1
t3      resw 1
epb     resb 22
fh      resw 1
sela    resw 1
selb    resw 1
s64     resw 1
ourlin  resd 1
blklin  resd 1
blkh    resd 1
nb      resd 1
nl      resd 1
nr      resw 1
pkt     resb 10
pment   resd 1
rmregs:
rm_di   resd 1
rm_si   resd 1
rm_bp   resd 1
        resd 1
rm_bx   resd 1
rm_dx   resd 1
rm_cx   resd 1
rm_ax   resd 1
rm_fl   resw 1
rm_es   resw 1
rm_ds   resw 1
rm_fs   resw 1
rm_gs   resw 1
rm_ip   resw 1
rm_cs   resw 1
rm_sp   resw 1
rm_ss   resw 1
cbregs  resb 32h
logbuf  resb LOGSIZE
        resb 1024
stack_top:
        alignb 16
patbuf  resb 3000h
