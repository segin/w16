; win.asm - WIN.COM for w16: start Windows.
;
; w16's WIN.COM, a drop-in replacement for Windows 3.1's (ARCHITECTURE.md
; 9.5 has what it is built from: black-box observation of Windows 3.1's
; WIN.COM with the WC1-WC9 probes, tests/wincom.sh). It began as LOSE.COM,
; the Suite16 loader (lose.asm, Copyright (c) 2024 Suite16 Contributors,
; Kirn Gill II; MIT License, win/LICENSE.lose); w16's changes are
; Copyright (c) 2026 The w16 contributors, MIT License (LICENSE).
; From LOSE.COM: the DOS version check, the memory shrink, saving the
; INT 23h/2Fh vectors, the /R /S /2 /3 /? switches blanked in the command
; tail for KERNEL, the INT 2Fh check for a running Windows, the paths of
; SYSTEM\DOSX.EXE, WSWAP.EXE, WIN386.EXE.
;
; What it does (the order Windows 3.1's WIN.COM was seen to use):
;   1. DOS 3.10 or later; Windows not already running (INT 2Fh 160Ah,
;      1600h, 4680h).
;   2. The switches at the start of the tail: /3, /S or /2, /R, /? (also
;      with "-"); each is blanked in the tail, which goes on to KERNEL.
;   3. The processor, XMS (and extended memory without it), the mode:
;      /S, /3, or by itself 386 enhanced mode on a 386 with 1024 KB of XMS
;      memory free and WIN386.EXE present, else standard mode.
;   4. Standard mode: SYSTEM\DOSX.EXE and WSWAP.EXE; enhanced: WIN386.EXE;
;      searched in WIN.COM's directory, its SYSTEM, the current directory,
;      the PATH, the PATH's SYSTEMs.
;   5. Its INT 2Fh handler: 160Ah (Windows version and mode), 4B20h (the
;      ExitWindowsExec buffer), in standard mode 4A05h (the block WSWAP
;      reads: DOSX's path and command tail, swap directories, the processor)
;      and 4B05h (the DOS 5 task switcher's instance data).
;   6. The start-up logo if nothing but switches was given: a 16-colour
;      BI_RLE4 bitmap appended to WIN.COM (w16's own, assets/boot), shown
;      centred in VGA mode 12h. (Windows 3.1's WIN.COM is WIN.CNF + a
;      .LGO program + a .RLE bitmap; w16's has no .LGO: it draws the
;      bitmap itself, and finds one appended after an .LGO too.)
;   7. The environment for Windows: its own plus "windir=<WIN.COM's
;      directory>" at the end (a lowercase name, which SET cannot make).
;   8. Runs WIN386.EXE, or WSWAP.EXE (which runs DOSX.EXE, which runs
;      KRNL386/KRNL286.EXE), and again while Windows exits with 42h
;      (restart) or 44h (run the ExitWindowsExec program, then restart).
;   9. Exits with Windows' exit code; enhanced mode: restores the video
;      mode it found (unless Windows ended with an error code).
;
; The program is in two parts: the resident part (data, the INT 2Fh
; handler, the run loop) stays in memory while Windows runs, followed by
; the environment it builds; the start-up part after it, and the logo,
; are given back to DOS before Windows starts.

        bits 16
        cpu 8086
        org 100h

start:
        jmp init

; ========================================================================
; resident part
; ========================================================================

; ---- the switcher block (INT 2Fh AX=4A05h SI=5), as WSWAP reads it ----
; Offsets observed in Windows 3.1's (WC4, WC3); meanings as far as known.
SB_ONE      equ 000h        ; 01h
SB_CPU      equ 001h        ; processor: 02h 80286, 04h 80386 or later
SB_2        equ 002h        ; 00h 3Bh 00h (UNVERIFIED: meaning unknown)
SB_TAIL     equ 005h        ; DOSX's command tail: length, text, 0Dh
SB_SWAP1    equ 0D7h        ; swap directory (SwapDisk=, TEMP, a root)
SB_SWAP2    equ 11Bh        ; second directory (TEMP; UNVERIFIED details)
SB_DONE     equ 16Fh        ; set to 1 by WSWAP when it ends
SB_RC       equ 171h        ; Windows' exit code, written by WSWAP
SB_ROWS     equ 177h        ; 19h: 25 (UNVERIFIED: screen rows?)
SB_K1       equ 178h        ; 02h 70h 00h 30h (UNVERIFIED)
SB_COUNT    equ 17Ch        ; counted down by SI=2, read by SI=4
SB_17D      equ 17Dh        ; 1 when WSWAP ended to run a DOS program
SB_17E      equ 17Eh        ; 0Fh; less 2 at each SI=3 (UNVERIFIED meaning)
SB_K2       equ 17Dh        ; 00h 0Fh FFh FFh (UNVERIFIED)
SB_DOSX     equ 189h        ; DOSX.EXE's full path, ASCIIZ
SB_TASKS    equ 1F0h        ; 15 records of 32h bytes (the switcher's
SB_TASKLEN  equ 32h         ;   tasks?): byte 12h = 80h, word 19h = FFFFh,
SB_NTASKS   equ 15          ;   name at 1Bh; the last one is DOSX's
TK_FLAG     equ 12h
TK_HANDLE   equ 19h
TK_NAME     equ 1Bh
SB_TAIL2    equ 4DEh        ; 22h bytes seen after them (UNVERIFIED)
SB_SWAPID   equ 4F2h        ; WSWAP's swap file number, while it runs
SB_SIZE     equ 500h        ; (Windows 3.1's startup info follows)

PATHMAX     equ 128         ; DOS path names (and command tails) are at
                            ; most 128 bytes: a DOS limit

old2f:      dd 0            ; INT 2Fh before ours
f_int23orig: dd 0           ; INT 23h at start (LOSE.COM saved it too)
mode:       db 0            ; 2 standard, 3 386 enhanced
cpu_type:   db 0            ; 0 8086, 2 80286, 3 80386 or later
vid_mode:   db 3            ; video mode at start
exit_code:  db 0
save_ss:    dw 0
save_sp:    dw 0
env_seg:    dw 0            ; the environment we build (in our block)
env_src:    dw 0            ; the one we were given
pblock:     dw 0            ; EXEC parameter block: environment,
pb_tail:    dw 80h, 0       ;   command tail,
pb_fcb1:    dw 5Ch, 0       ;   our own FCBs (as Windows 3.1's WIN.COM)
pb_fcb2:    dw 6Ch, 0
wswap_tail: db 2, ' ', 8, 0Dh   ; WSWAP's tail, as Windows 3.1 gives it
                                ; (UNVERIFIED: what the 08h means)
s_windir:   db 'windir='
windir:     times PATHMAX db 0  ; WIN.COM's directory, no trailing "\"
prog_path:  times PATHMAX db 0  ; WIN386.EXE or WSWAP.EXE
dswap_path: times PATHMAX db 0  ; DSWAP.EXE (standard mode's DOS programs)
; ExitWindowsExec's buffer (INT 2Fh AX=4B20h returns DX:CX = it). What
; KERNEL writes there is UNVERIFIED (WC9 could not make Windows 3.1 call
; it); w16 takes a program path, a blank, its arguments.
exec_buf:   times 2*PATHMAX db 0
exec_tail:  times PATHMAX db 0
exec_pb:    dw 0, exec_tail, 0, 5Ch, 0, 6Ch, 0
; MS-DOS 5 task switcher: our startup info (INT 2Fh AX=4B05h) and its one
; instance data item, the last system file table block's link to the
; next (4 bytes), as Windows 3.1's WIN.COM declares (WC4, WC8).
swstart:    dw 3            ; version
sw_next:    dd 0            ; the chain we were given
            dd 0, 0         ; virtual device file, reference data
            dw sw_inst, 0   ; instance data list
sw_inst:    dd 0            ; -> the last SFT block's link
            dw 4
            dd 0            ; end of list
; INT 2Fh AX=4A05h SI=6/7 return a pointer to the last task record's
; name (Windows 3.1: block + 4C7h, "dosx.exe")
sw_x        equ block + SB_TASKS + (SB_NTASKS - 1) * SB_TASKLEN + TK_NAME

        align 2
block:      times SB_SIZE db 0
; INT 2Fh AX=4A05h SI=2's buffer: CX paragraphs (10h, from WSWAP and
; DSWAP), zeros (WC3 on Windows 3.1: the 64 bytes at the DX:AX it returns
; were 0 for both callers, DX 010Fh in two runs where WIN.COM was at
; 0192h and 0194h: memory of its own, not its code)
SW_BUFP     equ 10h
sw_buf:     times SW_BUFP * 16 db 0

; ---- the INT 2Fh handler ----
int2f:
        cmp ax, 160Ah                   ; Windows version and mode
        jne .n1
        xor ax, ax
        mov bx, 030Ah
        mov cl, [cs:mode]
        xor ch, ch
        iret
.n1:    cmp ax, 4B20h                   ; ExitWindowsExec's buffer
        jne .n2
        xor ax, ax
        mov dx, cs
        mov cx, exec_buf
        iret
.n2:    cmp byte [cs:mode], 2           ; the rest: standard mode only
        jne .chain
        cmp ax, 4A05h
        je sw_api
        cmp ax, 4B05h
        je sw_identify
.chain: jmp far [cs:old2f]

; INT 2Fh AX=4A05h: the switcher's interface, function in SI (observed on
; Windows 3.1 with WC3 and WC5/WC6; the meanings are guesses)
sw_api:
        cmp si, 5
        jne .s2
        mov ax, block                   ; DX:AX -> the block
        mov di, ax
        mov dx, cs
        mov si, 0Ah
        iret
.s2:    cmp si, 2
        jne .s4
        ; DX:AX -> a zeroed buffer. w16 returned a pointer into its own
        ; code. UNVERIFIED: what the callers keep there
        dec byte [cs:block + SB_COUNT]
        mov dx, cs
        mov ax, sw_buf
        mov si, sw_x
        iret
.s4:    cmp si, 3
        jne .s4b
        ; WSWAP about to run a DOS program: Windows 3.1's WIN.COM (WC3)
        ; took block byte 17Eh from 0Fh to 0Dh and answered AX = 020Dh
        ; (AH the counter at 17Ch, AL the new 17Eh: the program's task
        ; record, which WIN.COM marks free again afterwards, .std),
        ; SI = 0B8Fh (UNVERIFIED what that is; w16 gives an offset in its
        ; own code as for SI=2), DX unchanged. UNVERIFIED: the rule for
        ; 17Eh beyond this one case (w16: less 2; taking the highest free
        ; record instead made WSWAP report too little disk space). w16
        ; answered as for no such function, and WSWAP ended Windows
        ; instead of running the program
        sub byte [cs:block + SB_17E], 2
        mov al, [cs:block + SB_17E]
        mov ah, [cs:block + SB_COUNT]
        mov si, sw_stub
        iret
.s4b:   cmp si, 4
        jne .s6
        ; the word at 17Ch: the SI=2/SI=4 counter and the byte after it
        ; (Windows 3.1, WSWAP starting a DOS program: AX = 0102h, block
        ; 17Ch-17Dh 02 01; WC3). It leaves WSWAP's swap-file number at
        ; 4F2h alone: w16 used to clear it here, and WSWAP, back from the
        ; DOS program, ended Windows (it could not find Windows' swap
        ; file); 3.1's block keeps it then. It is cleared before WSWAP is
        ; started again for a restart (.again2 below)
        mov ax, [cs:block + SB_COUNT]
        mov di, block                   ; DI -> the block (3.1: 06C8h, as
        mov si, 8                       ; SI=5's AX; w16 left DI alone)
        iret
.s6:    cmp si, 6
        jne .s7
        push cs
        pop es
.s7:    cmp si, 7
        ja .other
        cmp si, 6
        jb .other
        mov ax, sw_x
        mov si, ax
        mov dx, cs
        iret
.other: shl si, 1                       ; no such function: AX unchanged
        iret

sw_stub:
        retf

; INT 2Fh AX=4B05h: identify instance data. Pass it down the chain first,
; then put ours in front of what came back.
sw_identify:
        pushf
        call far [cs:old2f]
        mov [cs:sw_next], bx
        mov [cs:sw_next + 2], es
        push cs
        pop es
        mov bx, swstart
        xor ax, ax
        iret

; ---- run Windows (and again while it asks to be restarted) ----
; Entered from init (by a jump: init's code is overwritten here) with
; DS = ES = CS. Builds the environment at env_area, gives the rest of
; memory back to DOS, runs Windows.
launch:
        call build_env
        mov bx, di                      ; DI = end of the environment
        add bx, 15
        mov cl, 4
        shr bx, cl
        mov ah, 4Ah                     ; keep the resident part and it
        int 21h
.run:   mov ax, [env_seg]
        mov [pblock], ax
        mov [pb_tail + 2], cs
        mov [pb_fcb1 + 2], cs
        mov [pb_fcb2 + 2], cs
        mov word [pb_tail], 80h
        cmp byte [mode], 2
        jne .exec
        ; standard mode: WSWAP gets " \x08", in our PSP as well (as with
        ; Windows 3.1's WIN.COM); its real tail is in the block
        mov si, wswap_tail
        mov di, 80h
        mov cx, 4
        rep movsb                       ; (the block is left as WSWAP left
                                        ; it, as Windows 3.1's WIN.COM does)
.exec:  mov dx, prog_path
        mov bx, pblock
        call exec
        jnc .ran
        mov si, m_notfound              ; UNVERIFIED: the message when
        call print                      ; EXEC fails
        mov al, 0FFh
        jmp .exit
.ran:   mov ah, 4Dh
        int 21h                         ; AL = the program's exit code
        cmp byte [mode], 2
        je .std
        ; enhanced: restore the video mode found at the start, but only
        ; when Windows ended normally or restarts (after an error Windows
        ; 3.1's WIN.COM leaves the screen as it is; WC1 exit codes)
        test al, al
        jz .vid
        cmp al, 42h
        je .vid
        cmp al, 44h
        jne .code
.vid:   push ax
        mov al, [vid_mode]
        xor ah, ah
        int 10h
        pop ax
        jmp .code
.std:   ; standard mode: a DOS program to run. WSWAP, asked by WINOLDAP
        ; to start one (COMMAND.COM /C ...), swaps Windows out and ends,
        ; leaving the program's path at 189h (where DOSX's was), its tail
        ; at 05h, the counter at 17Ch not 0 (2) and 17Dh 1 (WC3 block
        ; dumps on Windows 3.1: Z:\COMMAND.COM, " /C ECHO HI"); 3.1's
        ; WIN.COM runs it and then WSWAP again, which swaps Windows back
        ; in (its swap-file number kept at 4F2h). w16 took the ending
        ; WSWAP for Windows' end. UNVERIFIED: the exact condition (w16:
        ; the counter not 0 and 17Dh 1) and the environment the program
        ; gets (w16: Windows')
        cmp byte [block + SB_COUNT], 0
        je .noapp
        cmp byte [block + SB_17D], 1
        jne .noapp
        ; 3.1's WIN.COM runs DSWAP.EXE (found in the system directory as
        ; WSWAP is, at the start: dswap_path), with WSWAP's tail " \x08",
        ; and DSWAP sets up the text screen (80x25), shows the MS-DOS
        ; Prompt's banner ("Type EXIT and press ENTER to quit this MS-DOS
        ; prompt ...") and runs the program itself (E10, an INT 21h/10h
        ; recorder: WIN.COM's EXECs are wswap.exe, then dswap.exe " \x08",
        ; which runs Z:\COMMAND.COM, then wswap.exe again;
        ; docs/notes/keyloss-handover.md). w16 ran the program at 189h
        ; itself: no banner, the text mode Windows left. Without DSWAP, as
        ; before.
        cmp byte [dswap_path], 0
        je .direct
        mov dx, dswap_path
        mov bx, pblock
        call exec
        jmp .after                      ; the block as for a program
.direct:
        mov ax, [env_seg]
        mov [exec_pb], ax
        mov word [exec_pb + 2], block + SB_TAIL
        mov [exec_pb + 4], cs
        mov [exec_pb + 8], cs
        mov [exec_pb + 12], cs
        mov dx, block + SB_DOSX
        mov bx, exec_pb
        call exec
        mov word [exec_pb + 2], exec_tail
.after: ; the block as 3.1's WIN.COM leaves it for the next WSWAP (WC3: at
        ; the end of the first run 16Fh 01, 17Ch 02, 17Eh 0Dh; at the start
        ; of the next 02, 01, 0Eh, and the program's task record free
        ; again: bytes 11h-13h 04 00 0E -> 00 80 0F (4F1h too went 0Dh ->
        ; 0Eh: left to WSWAP). 178h
        ; went from 02 to 0Ah as well, but its 08h is WINOLDAP's Ctrl+Esc
        ; (the Task List): not done. UNVERIFIED meanings)
        mov al, [block + SB_17E]
        cmp al, SB_NTASKS - 1
        jae .nrec
        mov bl, SB_TASKLEN
        mul bl
        mov bx, ax
        add bx, block + SB_TASKS
        mov byte [bx + 11h], 0
        mov byte [bx + TK_FLAG], 80h
        inc byte [bx + 13h]
.nrec:  mov byte [block + SB_17E], SB_NTASKS - 1
        inc byte [block + 16Fh]
        dec byte [block + SB_COUNT]
        jmp .run
.noapp: ; WSWAP leaves Windows' exit code in the block; if it did not end
        ; the way it should, Windows 3.1's WIN.COM exits with FFh
        mov al, [block + SB_RC]
        cmp byte [block + SB_DONE], 0
        jne .code
        cmp byte [block + SB_COUNT], 0
        jne .code
        mov al, 0FFh
.code:  cmp al, 42h                     ; EW_RESTARTWINDOWS
        je .again
        cmp al, 44h                     ; ExitWindowsExec
        jne .exit
        call exec_program
.again: cmp byte [mode], 2               ; standard mode: the video mode
        jne .again2                     ; is set again before WSWAP runs
        push ax                         ; again (enhanced: done above)
        mov al, [vid_mode]
        xor ah, ah
        int 10h
        pop ax
.again2:
        ; WSWAP keeps the number of its swap file (~WOA<n>.TMP) at block
        ; 4F2h; on 3.1 a WSWAP started again for a restart finds 0 there
        ; and begins afresh rather than resuming the old file (WC3 block
        ; dumps; UNVERIFIED which of WIN.COM or WSWAP clears it)
        mov word [block + SB_SWAPID], 0
        mov byte [80h], 0               ; restarted Windows gets no tail
        mov byte [81h], 0Dh
        mov byte [block + SB_TAIL], 0
        mov byte [block + SB_TAIL + 1], 0Dh
        jmp .run
.exit:  push ax
        push ds
        lds dx, [old2f]                 ; unhook INT 2Fh
        mov ax, 252Fh
        int 21h
        pop ds
        pop ax
        mov ah, 4Ch
        int 21h

; EXEC DS:DX with the parameter block at ES:BX (CS); keeps SS:SP
exec:
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
        ret

; run the program ExitWindowsExec left in exec_buf: "path args"
exec_program:
        mov si, exec_buf
.p:     lodsb
        test al, al
        jz .nul
        cmp al, ' '
        jne .p
        mov byte [si - 1], 0            ; the path ends here
        mov di, exec_tail + 1           ; the rest is the tail (" args")
        mov byte [di], ' '
        inc di
        xor cx, cx
        inc cx
.t:     lodsb
        test al, al
        jz .te
        cmp cx, PATHMAX - 3             ; a DOS limit: tails are 127 bytes
        jae .te
        stosb
        inc cx
        jmp .t
.nul:   mov di, exec_tail + 1
        xor cx, cx
.te:    mov byte [di], 0Dh
        mov [exec_tail], cl
        cmp byte [exec_buf], 0
        je .r
        mov ax, [env_src]
        mov [exec_pb], ax
        mov [exec_pb + 4], cs
        mov [exec_pb + 8], cs
        mov [exec_pb + 12], cs
        mov dx, exec_buf
        mov bx, exec_pb
        call exec
        mov byte [exec_buf], 0
.r:     ret

; Build the environment for Windows at env_area: ours, then
; "windir=<dir>". Returns DI = its end (offset in our segment).
build_env:
        mov ax, env_area                ; (a paragraph boundary)
        mov cl, 4
        shr ax, cl
        mov bx, cs
        add ax, bx
        mov [env_seg], ax
        mov di, env_area
        mov ax, [2Ch]
        mov [env_src], ax
        test ax, ax
        jz .none
        push ds
        mov ds, ax
        xor si, si
.c:     cmp byte [si], 0                ; copy each string
        je .end
.s:     lodsb
        stosb
        test al, al
        jnz .s
        jmp .c
.end:   pop ds
.none:  mov si, s_windir
.w:     lodsb
        stosb
        test al, al
        jnz .w
        xor al, al                      ; the end of the environment
        stosb
        ret

; write the ASCIIZ string at DS:SI to standard output
print:
        push ax
        push bx
        push cx
        push dx
        mov dx, si
        xor cx, cx
.l:     lodsb
        test al, al
        jz .w
        inc cx
        jmp .l
.w:     mov bx, 1
        mov ah, 40h
        int 21h
        pop dx
        pop cx
        pop bx
        pop ax
        ret

m_notfound: db 'Executable Not Found', 0Dh, 0Ah, 0

        align 2
        times 384 db 0
stack_top:
        align 16
env_area:                               ; the environment goes here, over
                                        ; the start-up code

; ========================================================================
; start-up part (given back to DOS before Windows runs)
; ========================================================================

init:
        mov sp, stack_top
        call check_dos_version
        call get_original_vectors
        mov ah, 0Fh
        int 10h
        mov [vid_mode], al
        call check_win_version          ; exits if Windows is running
        call parse_flags
        call check_cpu
        call get_windir
        cmp byte [want_help], 0
        jne show_help
        cmp byte [want_real], 0
        jne real_mode
        ; /3 needs a 386 (asked for 386 enhanced mode on a 286 or 8086,
        ; Windows 3.1 says this even on an 8086)
        cmp byte [want_std], 0
        jne .not3
        cmp byte [want_386], 0
        je .not3
        cmp byte [cpu_type], 3
        jae .not3
        mov si, m_no386
        jmp fail
.not3:  cmp byte [cpu_type], 0
        jne .cpuok
        mov si, m_cannot
        jmp fail
.cpuok: call check_xms                  ; exits without XMS
        call choose_mode
        cmp byte [mode], 3
        je .enh
        ; standard mode: DOSX.EXE, then WSWAP.EXE
        mov si, n_dosx
        mov di, block + SB_DOSX
        call find_file
        jnc .dosx
        mov si, m_nodosx
        jmp fail
.dosx:  call logo                        ; (Windows 3.1's WIN.COM shows the
        mov si, n_wswap                 ; logo before it looks for WSWAP:
        mov di, prog_path               ; without WSWAP the screen is
        call find_file                  ; cleared by the mode set back)
        jnc .wswap
        call logo_off
        mov si, m_notfound
        jmp fail
.wswap: mov si, n_dswap               ; DSWAP for DOS programs (.std)
        mov di, dswap_path
        call find_file
        jnc .dswap
        mov byte [dswap_path], 0
.dswap: call fill_block
        jmp .hook
.enh:   mov si, n_win386
        mov di, prog_path
        call find_file
        jnc .enhok
        mov si, m_nowin386
        jmp fail
.enhok: call logo
.hook:  mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        push cs
        pop es
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        jmp launch

; the logo, if nothing but switches was given
logo:
        call tail_blank
        jnz .r
        call show_logo
.r:     ret

; back to the video mode we found (Windows 3.1's WIN.COM sets it when
; WSWAP is missing whether or not the logo was shown: the screen clears)
logo_off:
        mov al, [vid_mode]
        xor ah, ah
        int 10h
        ret

; print DS:SI, exit with FFh
fail:
        call print
        mov al, 0FFh
        mov ah, 4Ch
        int 21h

; ---- DOS 3.10 or later (LOSE.COM checked for 3.x) ----
check_dos_version:
        mov ax, 3000h
        int 21h
        cmp al, 3
        ja .ok
        jb .old
        cmp ah, 10
        jae .ok
.old:   test al, al                     ; DOS 1.x: no AH=40h or 4Ch
        jnz .dos2
        mov dx, m_dosver_d
        mov ah, 9
        int 21h
        int 20h
.dos2:  mov si, m_dosver
        jmp fail
.ok:    ret

; LOSE.COM saved the INT 23h vector it would hook; w16 keeps it (Windows
; 3.1's WIN.COM leaves INT 23h alone)
get_original_vectors:
        mov ax, 3523h
        int 21h
        mov [f_int23orig], bx
        mov [f_int23orig + 2], es
        push cs
        pop es
        ret

; ---- is Windows running? (INT 2Fh 160Ah; then 1600h and 4680h) ----
check_win_version:
        mov ax, 160Ah
        xor bx, bx
        xor cx, cx
        int 2Fh
        test ax, ax
        jnz .no160a
        mov si, m_run_std
        cmp cx, 2
        je already_running
        mov si, m_run_enh
        cmp cx, 3
        je already_running
        jmp .generic
.no160a:
        mov ax, 1600h
        int 2Fh
        test al, 7Fh                    ; 00h and 80h: no enhanced mode
        jnz .generic
        mov ax, 4680h
        int 2Fh
        test ax, ax
        jz .generic
        ret
.generic:
        mov si, m_run
already_running:
        call print
        mov si, m_run_tail
        call print
        mov ax, 4C00h
        int 21h

; ---- the switches (LOSE.COM's parse_flags) ----
; Only the switches at the start of the tail are WIN.COM's: it stops at
; the first word that is not one of /3 /S /2 /R /? (with "/" or "-").
; Each one is blanked (both characters) and the tail goes on to KERNEL
; (/B, /D:... and anything else are left for KERNEL or WIN386).
parse_flags:
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        mov bx, si
        add bx, cx                      ; BX = the end of the tail
.skip:  cmp si, bx
        jae .end
        mov al, [si]
        cmp al, ' '
        je .next
        cmp al, 9
        je .next
.sw:    cmp al, '/'
        je .flag
        cmp al, '-'
        jne .end                        ; a word that is not a switch
.flag:  lea di, [si + 1]
        cmp di, bx                      ; nothing after the "/"
        jae .end
        mov al, [si + 1]
        cmp al, '3'
        jne .t2
        mov byte [want_386], 1
        jmp .blank
.t2:    cmp al, '2'
        je .tstd
        and al, 0DFh                    ; upper case
        cmp al, 'S'
        jne .tr
.tstd:  mov byte [want_std], 1
        jmp .blank
.tr:    cmp al, 'R'
        jne .th
        mov byte [want_real], 1
        jmp .blank
.th:    cmp byte [si + 1], '?'
        jne .end                        ; not ours: stop here
        mov byte [want_help], 1
.blank: mov word [si], 2020h            ; replace the switch with blanks
        add si, 2
        cmp si, bx                      ; another switch right after it
        jae .end
        mov al, [si]
        cmp al, '/'
        je .flag
        cmp al, '-'
        je .flag
        jmp .skip
.next:  inc si
        jmp .skip
.end:   ret

; ZF set if the tail is blank (after the switches were blanked)
tail_blank:
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        jcxz .yes
.l:     lodsb
        cmp al, ' '
        je .n
        cmp al, 9
        je .n
        ret                             ; ZF clear
.n:     loop .l
.yes:   xor ax, ax
        ret

; ---- the processor: 8086, 80286 or 80386+ (FLAGS bits 12-15) ----
check_cpu:
        pushf
        xor ax, ax
        push ax
        popf
        pushf
        pop ax
        and ax, 0F000h
        cmp ax, 0F000h                  ; 8086/8088: bits 12-15 stay set
        je .c86
        mov ax, 0F000h
        push ax
        popf
        pushf
        pop ax
        and ax, 0F000h                  ; 80286 (real mode): stay clear
        jz .c286
        mov byte [cpu_type], 3
        jmp .r
.c286:  mov byte [cpu_type], 2
        jmp .r
.c86:   mov byte [cpu_type], 0
.r:     popf
        ret

; ---- XMS (HIMEM.SYS) and how much of it is free ----
check_xms:
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        je .xms
        mov ah, 88h                     ; extended memory without XMS?
        int 15h
        jc .none
        test ax, ax
        jz .none
        mov si, m_himem
        jmp fail
.none:  mov si, m_cannot
        jmp fail
.xms:   mov ax, 4310h
        int 2Fh
        mov [xms_entry], bx
        mov [xms_entry + 2], es
        push cs
        pop es
        mov ah, 08h                     ; query free extended memory
        xor bl, bl
        call far [xms_entry]
        mov [xms_free], dx              ; DX = total free, in KB
        ret

; ---- the mode: /S, /3, or by itself ----
; By itself (observed with WC8: 1020 KB free gives standard mode, 1024 KB
; 386 enhanced): 386 enhanced mode on a 386 or later with 1024 KB of XMS
; memory free and WIN386.EXE to be found; otherwise standard mode.
choose_mode:
        mov byte [mode], 2
        cmp byte [want_std], 0
        jne .r
        mov byte [mode], 3
        cmp byte [want_386], 0
        jne .r
        call default_mode
        mov [mode], al
.r:     ret

; AL = 3 or 2: the mode WIN chooses without switches
default_mode:
        mov al, 2
        cmp byte [cpu_type], 3
        jb .r
        cmp word [xms_free], 1024
        jb .r
        mov si, n_win386
        mov di, prog_path
        call find_file
        mov al, 2
        jc .r
        mov al, 3
.r:     ret

; ---- /? (and the mode WIN would choose here) ----
show_help:
        mov si, m_help
        call print
        cmp byte [cpu_type], 0          ; Windows cannot run here at all:
        je .cannot                      ; no last line, exit code 5
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .cannot
        call check_xms
        call default_mode
        push ax
        mov si, m_help_cfg
        call print
        pop ax
        push ax
        mov si, m_help_enh
        cmp al, 3
        je .p
        mov si, m_help_std
.p:     call print
        pop ax                          ; exit code: the mode, 2 or 3
        mov ah, 4Ch
        int 21h
.cannot:
        mov ax, 4C05h
        int 21h

real_mode:
        mov si, m_real
        jmp fail

; ---- WIN.COM's directory, from the program path after our environment
; (DOS 3+); the current directory if there is none ----
get_windir:
        mov di, windir
        mov ax, [2Ch]
        test ax, ax
        jz .cwd
        push ds
        mov ds, ax
        xor si, si
.f:     cmp word [si], 0
        je .g
        inc si
        jmp .f
.g:     add si, 4                       ; the zeros, the string count
        xor bx, bx                      ; BX = after the last "\"
.c:     lodsb
        cmp di, windir + PATHMAX - 1
        jae .cut
        stosb
        cmp al, '\'
        jne .nb
        mov bx, di
.nb:    test al, al
        jnz .c
.cut:   pop ds
        test bx, bx
        jz .cwd
        mov di, bx
        dec di                          ; drop the last "\" and the name
        cmp di, windir + 2              ; ("C:\" keeps its "\")
        ja .z
        inc di
.z:     mov byte [di], 0
        ret
.cwd:   mov di, windir
        call get_cwd
        ret

; DI = buffer: "D:\DIR" (no trailing "\") of the current directory
get_cwd:
        mov ah, 19h
        int 21h
        add al, 'A'
        stosb
        mov al, ':'
        stosb
        mov al, '\'
        stosb
        mov si, di
        xor dl, dl
        mov ah, 47h
        int 21h
        ret

; ---- find a program: DS:SI = file name (lower case, as Windows 3.1's),
; DI = where to put the full path. Searched (as Windows 3.1's WIN.COM,
; observed with DOSBox-X's file log): WIN.COM's directory, its SYSTEM,
; the current directory (UNVERIFIED: seen only where both were the same),
; each PATH directory, each PATH directory's SYSTEM. CF set if not found.
find_file:
        mov [ff_name], si
        mov [ff_out], di
        mov si, windir                  ; <windir>\name
        xor bx, bx
        call try_dir
        jnc .r
        mov si, windir                  ; <windir>\system\name
        mov bx, n_system
        call try_dir
        jnc .r
        mov di, ff_cwd                  ; <cwd>\name
        call get_cwd
        mov si, ff_cwd
        xor bx, bx
        call try_dir
        jnc .r
        xor bx, bx                      ; <each PATH dir>\name
        call try_path
        jnc .r
        mov bx, n_system                ; <each PATH dir>\system\name
        call try_path
.r:     ret

; each PATH entry (BX = subdirectory or 0)
try_path:
        mov [ff_sub], bx
        call find_path_var              ; ES:SI -> the value, CF if none
        jc .none
.entry: cmp byte [es:si], 0
        je .none
        mov di, ff_dir                  ; copy one entry
.cp:    mov al, [es:si]
        test al, al
        jz .got
        inc si
        cmp al, ';'
        je .got
        cmp di, ff_dir + PATHMAX - 1
        jae .cp
        mov [di], al
        inc di
        jmp .cp
.got:   mov byte [di], 0
        push es
        push si
        push cs
        pop es
        mov si, ff_dir
        mov bx, [ff_sub]
        call try_dir
        pop si
        pop es
        jnc .found
        cmp byte [es:si - 1], 0
        jne .entry
.none:  push cs
        pop es
        stc
        ret
.found: push cs
        pop es
        ret

; ES:SI -> the value of PATH= in our environment; CF set if there is none
find_path_var:
        mov ax, [2Ch]
        test ax, ax
        jz .no
        mov es, ax
        xor si, si
.v:     cmp byte [es:si], 0
        je .no
        cmp word [es:si], 'PA'
        jne .skip
        cmp word [es:si + 2], 'TH'
        jne .skip
        cmp byte [es:si + 4], '='
        jne .skip
        add si, 5
        clc
        ret
.skip:  inc si
        cmp byte [es:si - 1], 0
        jne .skip
        jmp .v
.no:    push cs
        pop es
        stc
        ret

; DS:SI = directory, BX = subdirectory ("system\") or 0: build
; dir\[sub]name at [ff_out] and see whether it opens. CF set if not.
try_dir:
        mov di, [ff_out]
        mov dx, di
        add dx, PATHMAX - 1             ; (the end of the buffer)
.d:     lodsb
        test al, al
        jz .de
        cmp di, dx
        jae .long
        stosb
        jmp .d
.de:    cmp di, [ff_out]
        je .sep
        cmp byte [di - 1], '\'
        je .sub
.sep:   mov al, '\'
        stosb
.sub:   test bx, bx
        jz .name
        mov si, bx
        call .cat
.name:  mov si, [ff_name]
        call .cat
        cmp di, dx
        jae .long
        mov byte [di], 0
        mov dx, [ff_out]
        mov ax, 3D00h                   ; does it open?
        int 21h
        jc .r
        mov bx, ax
        mov ah, 3Eh
        int 21h
        clc
.r:     ret
.long:  stc
        ret
.cat:   lodsb
        test al, al
        jz .ce
        cmp di, dx
        jae .ce
        stosb
        jmp .cat
.ce:    ret

; ---- the switcher block for WSWAP (standard mode) ----
fill_block:
        mov byte [block + SB_ONE], 1
        mov al, 2                       ; WF_CPU286 / WF_CPU386
        cmp byte [cpu_type], 3
        jb .c
        mov al, 4
.c:     mov [block + SB_CPU], al
        mov byte [block + SB_2 + 1], 3Bh
        ; DOSX's tail: "krnl286" and WIN's (blanked) tail
        mov di, block + SB_TAIL + 1
        mov si, n_krnl286
.k:     lodsb
        test al, al
        jz .ke
        stosb
        jmp .k
.ke:    mov al, ' '
        stosb
        mov si, 81h
        xor cx, cx
        mov cl, [80h]
        mov ax, PATHMAX - 10            ; a DOS limit: 127-byte tails
        cmp cx, ax
        jbe .tl
        mov cx, ax
.tl:    rep movsb
        mov ax, di
        sub ax, block + SB_TAIL + 1
        mov [block + SB_TAIL], al
        mov byte [di], 0Dh
        ; 25 (screen rows? Windows 3.1's WIN.COM writes 19h even when the
        ; screen is in a 30-row mode), and the other constants seen there
        mov byte [block + SB_ROWS], 19h
        mov si, sb_k1
        mov di, block + SB_K1
        mov cx, 9
        rep movsb
        ; the task records; the last one named after DOSX.EXE (lower case,
        ; as found)
        mov di, block + SB_TASKS
        mov cx, SB_NTASKS
.tk:    mov byte [di + TK_FLAG], 80h
        mov word [di + TK_HANDLE], 0FFFFh
        add di, SB_TASKLEN
        loop .tk
        mov si, n_dosx
        mov di, sw_x
.tn:    lodsb
        stosb
        test al, al
        jnz .tn
        mov si, sb_tail2
        mov di, block + SB_TAIL2
        mov cx, 22h
        rep movsb
        call swap_dirs
        call sft_link
        mov [swstart + 16], cs          ; (the instance list's segment)
        ret

; the last system file table block's link (DOS's list of lists + 4)
sft_link:
        push es
        mov ah, 52h
        int 21h
        les bx, [es:bx + 4]
.l:     cmp word [es:bx], 0FFFFh
        je .last
        les bx, [es:bx]
        jmp .l
.last:  mov [sw_inst], bx
        mov [sw_inst + 2], es
        pop es
        ret

; The swap directories: SYSTEM.INI [NonWindowsApp] SwapDisk= if it is a
; directory, else the TEMP variable's if it is one, else the root of the
; first local drive; each with a "\" at the end. The second: that drive's
; current directory (ARCHITECTURE 9.5).
;
; The drive: the first from A: that INT 21h 4409h (IOCTL: remote?) accepts
; and 4408h (removable?) does not call removable. Windows 3.1's WIN.COM
; asks 4408h/4409h there and no INT 13h (DOSBox-X's log); in DOSBox,
; where 4409h fails for A: and B:, it chose C:, in DOSBox-X, where 4409h
; answers for A: (no disk; 4408h fails), A:. It used to be INT 13h 08h's
; first hard disk, A: without one, which in DOSBox (no BIOS hard disk)
; gave A: where 3.1 gave C:, and the DOS prompt (WINOLDAP) could not swap.
; UNVERIFIED: the remote and removable tests (neither emulator has one).
swap_dirs:
        push cs
        pop es
        mov bl, 1
.dr:    mov ax, 4409h
        int 21h
        jc .drn
        test dh, 10h                    ; DX bit 12: remote
        jnz .drn
        mov ax, 4408h
        int 21h
        jc .dry                         ; no answer: not removable
        test ax, ax
        jz .drn                         ; 0: removable
.dry:   mov al, bl
        add al, 'A' - 1
        jmp .hd
.drn:   inc bl
        cmp bl, 26
        jbe .dr
        mov al, 'C'
.hd:    mov di, root_dir
        stosb
        mov ax, '\' * 256 + ':'
        stosw
        xor al, al
        stosb
        ; TEMP
        mov byte [temp_dir], 0
        mov si, n_temp
        call find_env_var
        jc .notemp
        mov di, temp_dir
        call copy_dir_es
        mov si, temp_dir
        call is_dir
        jnc .notemp
        mov byte [temp_dir], 0
.notemp:
        ; SwapDisk=
        mov di, swap_dir
        mov byte [di], 0
        call read_swapdisk
        mov byte [swap_ok], 0
        mov si, swap_dir
        cmp byte [si], 0
        je .t
        call is_dir
        jc .t
        mov byte [swap_ok], 1
        jmp .s1
.t:     mov si, temp_dir
        cmp byte [si], 0
        jne .s1
        mov si, root_dir
.s1:    mov di, block + SB_SWAP1
        call copy_slash
        ; the second, when that drive is the current one: its current
        ; directory. Windows 3.1's in DOSBox: C:\WINDOWS\ started from
        ; C:\WINDOWS, C:\TMP\ from C:\TMP, C:\A\ from C:\A, whatever TEMP
        ; is; C:\ when started from Z:. Otherwise (DOSBox-X: the drive is
        ; A:, the current one C:) TEMP's directory (not a root) with the
        ; drive's letter on it and a second "\" when SwapDisk= did not give
        ; the first (C:\TMP: A:\TMP\\, seen with WC4), else the root.
        ; UNVERIFIED: the rule behind both (3.1's buffer after a failed
        ; INT 21h 47h for A: in DOSBox-X?)
        mov ah, 19h
        int 21h
        add al, 'A'
        cmp al, [root_dir]
        jne .nocwd
        mov si, cwd_buf + 3
        xor dl, dl
        mov ah, 47h
        int 21h
        jc .nocwd
        mov ax, [root_dir]
        mov [cwd_buf], ax
        mov byte [cwd_buf + 2], '\'
        mov si, cwd_buf
        mov di, block + SB_SWAP2
        jmp .s2
.nocwd: mov di, block + SB_SWAP2
        mov si, temp_dir
        cmp byte [si], 0
        je .s2root
        cmp byte [si + 3], 0            ; "C:\" alone: the root
        je .s2root
        cmp byte [swap_ok], 0
        jne .s2
        call copy_slash
        mov al, [root_dir]
        mov [block + SB_SWAP2], al
        mov word [di], '\'
        ret
.s2root:
        mov si, root_dir
.s2:    call copy_slash
        ret

; copy DS:SI to DI with a "\" at the end (unless there is one), ASCIIZ;
; at most 64 bytes (the size of the block's fields: DOS paths are 64)
copy_slash:
        mov cx, 63
.l:     lodsb
        test al, al
        jz .e
        stosb
        loop .l
.e:     cmp byte [di - 1], '\'
        je .z
        mov al, '\'
        stosb
.z:     mov byte [di], 0
        ret

; CF clear if DS:SI names a directory (it can be made current: as
; Windows 3.1's WIN.COM tests it, with INT 21h 47h/3Bh)
is_dir:
        push si
        mov di, isdir_save              ; the current directory, to go back
        mov al, '\'
        stosb
        mov si, di
        xor dl, dl
        mov ah, 47h
        int 21h
        pop si
        push si
        mov dx, si
        mov ah, 3Bh
        int 21h
        pushf
        mov dx, isdir_save
        mov ah, 3Bh
        int 21h
        popf
        pop si
        ret

; ES:SI = a variable's value: copy to DS:DI (at most PATHMAX - 1 bytes)
copy_dir_es:
        mov cx, PATHMAX - 1
.l:     mov al, [es:si]
        test al, al
        jz .e
        mov [di], al
        inc si
        inc di
        loop .l
.e:     mov byte [di], 0
        push cs
        pop es
        ret

; ES:SI -> the value of the variable named at DS:SI ("NAME="); CF if none
find_env_var:
        mov dx, si
        mov ax, [2Ch]
        test ax, ax
        jz .no
        mov es, ax
        xor di, di
.v:     cmp byte [es:di], 0
        je .no
        mov si, dx
        mov bx, di
.c:     lodsb
        test al, al
        jz .hit
        cmp al, [es:bx]
        jne .skip
        inc bx
        jmp .c
.hit:   mov si, bx
        clc
        ret
.skip:  inc di
        cmp byte [es:di - 1], 0
        jne .skip
        jmp .v
.no:    push cs
        pop es
        stc
        ret

; SYSTEM.INI (found as the programs are) [NonWindowsApp] SwapDisk= into
; swap_dir. Read a line at a time, so the file may be any size.
read_swapdisk:
        mov si, n_sysini
        mov di, ini_path
        call find_file
        jc .r
        mov dx, ini_path
        mov ax, 3D00h
        int 21h
        jc .r
        mov [ini_h], ax
        mov word [ini_n], 0
        mov word [ini_p], 0
        mov byte [ini_sec], 0
.line:  call ini_getline                ; DS:SI = the line, CF at the end
        jc .close
        call skip_blanks
        cmp byte [si], '['
        jne .key
        inc si
        mov di, n_nonwin
        call match_ci
        mov byte [ini_sec], 0
        jne .line
        cmp byte [si], ']'
        jne .line
        mov byte [ini_sec], 1
        jmp .line
.key:   cmp byte [ini_sec], 0
        je .line
        mov di, n_swapdisk
        call match_ci
        jne .line
        call skip_blanks
        cmp byte [si], '='
        jne .line
        inc si
        call skip_blanks
        mov di, swap_dir
        mov cx, PATHMAX - 1
.cp:    lodsb
        cmp al, ' '
        jbe .cpe
        cmp al, ';'
        je .cpe
        stosb
        loop .cp
.cpe:   mov byte [di], 0
        ; drop a trailing "\" (but not from "C:\")
        cmp di, swap_dir + 3
        jbe .line
        cmp byte [di - 1], '\'
        jne .line
        mov byte [di - 1], 0
        jmp .line
.close: mov bx, [ini_h]
        mov ah, 3Eh
        int 21h
.r:     ret

; the next line of the INI file into ini_line (longer lines are cut: they
; cannot hold a SwapDisk= path anyway); CF at the end of the file
ini_getline:
        mov di, ini_line
        xor dx, dx                      ; DX = bytes in the line
.c:     mov bx, [ini_p]
        cmp bx, [ini_n]
        jb .have
        push di
        push dx
        mov bx, [ini_h]
        mov dx, ini_buf
        mov cx, INIBUF
        mov ah, 3Fh
        int 21h
        pop dx
        pop di
        jc .eof
        test ax, ax
        jz .eof
        mov [ini_n], ax
        mov word [ini_p], 0
        xor bx, bx
.have:  mov al, [ini_buf + bx]
        inc word [ini_p]
        cmp al, 0Ah
        je .eol
        cmp al, 0Dh
        je .c
        cmp al, 1Ah
        je .eof
        cmp dx, INILINE - 1
        jae .c
        mov [di], al
        inc di
        inc dx
        jmp .c
.eol:   mov byte [di], 0
        mov si, ini_line
        clc
        ret
.eof:   mov byte [di], 0
        mov si, ini_line
        test dx, dx
        jnz .last
        stc
        ret
.last:  mov word [ini_n], 0             ; give the last line, then the end
        mov word [ini_p], 0
        clc
        ret

skip_blanks:
.l:     cmp byte [si], ' '
        je .n
        cmp byte [si], 9
        jne .r
.n:     inc si
        jmp .l
.r:     ret

; does DS:SI start with the ASCIIZ word at DI (any case)? ZF set if so,
; SI then after it
match_ci:
        push si
.l:     mov al, [di]
        test al, al
        jz .yes
        mov ah, [si]
        cmp ah, 'a'
        jb .u
        cmp ah, 'z'
        ja .u
        sub ah, 20h
.u:     cmp al, 'a'
        jb .u2
        cmp al, 'z'
        ja .u2
        sub al, 20h
.u2:    cmp al, ah
        jne .no
        inc si
        inc di
        jmp .l
.yes:   pop ax                          ; (drop the saved SI)
        xor ax, ax                      ; ZF set
        ret
.no:    pop si
        or al, 1                        ; ZF clear
        ret

; ---- the start-up logo ----
; A BI_RLE4 bitmap (4 bits a pixel, at most 640 x 480) appended to this
; program: the first "BM" after the program whose file size reaches
; exactly to the end of the file (so a .LGO between, as in Windows 3.1's
; WIN.COM, is skipped, never run). Shown centred in VGA mode 12h with its
; own palette.
show_logo:
        call own_size                   ; AX = our file's size
        jc .r
        mov [file_end], ax
        mov si, program_end
.find:  mov ax, [file_end]
        sub ax, si
        jb .r
        cmp ax, 118                     ; header, info, 16 colours
        jb .r
        cmp word [si], 'BM'
        jne .nx
        cmp [si + 2], ax                ; bfSize = what is left
        jne .nx
        cmp word [si + 4], 0
        je .found
.nx:    inc si
        jmp .find
.found:
        ; validate: BITMAPINFOHEADER, 4 bits, BI_RLE4, fits the screen
        cmp word [si + 14], 40
        jne .r
        cmp word [si + 28], 4
        jne .r
        cmp word [si + 30], 2
        jne .r
        mov ax, [si + 18]               ; width
        test ax, ax
        jz .r
        cmp ax, 640
        ja .r
        cmp word [si + 20], 0
        jne .r
        mov [lg_w], ax
        mov ax, [si + 22]               ; height (bottom-up only)
        test ax, ax
        jz .r
        cmp ax, 480
        ja .r
        cmp word [si + 24], 0
        jne .r
        mov [lg_h], ax
        mov [lg_bmp], si
        ; the BIOS's row count (0040:0084h, rows - 1) is put back after the
        ; mode set: Windows 3.1's WIN.COM leaves the one from before the
        ; logo (observed, a stand-in for WSWAP dumping the BIOS data area:
        ; 18h from 80x25, 31h from 50 lines; the rest as mode 12h sets it).
        ; Windows' display driver reads it when it starts and restores that
        ; text mode when it ends: with mode 12h's 1Dh it gave the 8x8 font
        ; (50 lines) after Windows instead of 80x25
        push es
        mov ax, 40h
        mov es, ax
        mov al, [es:84h]
        mov [bios_rows], al
        mov ax, 0012h                   ; VGA 640 x 480, 16 colours
        int 10h
        mov al, [bios_rows]
        mov [es:84h], al
        pop es
        ; palette registers 0-15 -> DAC 0-15, then the bitmap's colours
        push cs
        pop es
        mov dx, pal_regs
        mov ax, 1002h
        int 10h
        mov si, [lg_bmp]
        add si, 14 + 40
        mov di, dac
        mov cx, 16
.pal:   mov al, [si + 2]                ; RGBQUAD: blue, green, red, 0
        shr al, 1
        shr al, 1
        stosb
        mov al, [si + 1]
        shr al, 1
        shr al, 1
        stosb
        mov al, [si]
        shr al, 1
        shr al, 1
        stosb
        add si, 4
        loop .pal
        mov ax, 1012h
        xor bx, bx
        mov cx, 16
        mov dx, dac
        int 10h
        ; placement
        mov ax, 640
        sub ax, [lg_w]
        shr ax, 1
        mov [lg_x0], ax
        mov ax, 480
        sub ax, [lg_h]
        shr ax, 1
        add ax, [lg_h]
        dec ax
        mov [lg_y], ax                  ; screen row of the bottom line
        call rle4_draw
.r:     ret

; AX = the size of our own file (the program path after our environment);
; CF set if it cannot be read
own_size:
        mov ax, [2Ch]
        test ax, ax
        jz .no
        push ds
        mov ds, ax
        xor si, si
.f:     cmp word [si], 0
        je .g
        inc si
        jmp .f
.g:     lea dx, [si + 4]
        mov ax, 3D00h
        int 21h
        pop ds
        jc .no
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        pushf
        push ax
        push dx
        mov ah, 3Eh
        int 21h
        pop dx
        pop ax
        popf
        jc .no
        test dx, dx                     ; a .COM file is less than 64 KB
        jnz .no
        add ax, 100h                    ; as an offset in memory
        jc .no
        clc
        ret
.no:    stc
        ret

; decode the BI_RLE4 bits into a line at a time and draw each line
rle4_draw:
        mov si, [lg_bmp]
        add si, [si + 10]               ; bfOffBits
        call line_clear
        xor bx, bx                      ; BX = x within the line
        mov word [lg_rows], 0
.next:  mov ax, [lg_rows]
        cmp ax, [lg_h]
        jae .done
        cmp si, [file_end]
        jae .done
        lodsb
        test al, al
        jz .esc
        mov cl, al                      ; encoded run: CL pixels, colours
        xor ch, ch                      ; alternating from the next byte
        lodsb
.run:   call put_hi
        dec cx
        jz .next
        call put_lo
        loop .run
        jmp .next
.esc:   lodsb
        cmp al, 0                       ; end of line
        je .eol
        cmp al, 1                       ; end of bitmap
        je .eob
        cmp al, 2                       ; delta: right, up
        je .delta
        mov cl, al                      ; absolute: CL pixels follow,
        xor ch, ch                      ; in (CL + 1) / 2 bytes, padded
        mov dx, cx                      ; to a whole word
        inc dx
        shr dx, 1
.abs:   lodsb
        call put_hi
        dec cx
        jz .ae
        call put_lo
        loop .abs
.ae:    test dl, 1
        jz .next
        inc si
        jmp .next
.eol:   call line_out
        xor bx, bx
        jmp .next
.delta: lodsb
        xor ah, ah
        add bx, ax
        lodsb
        mov cl, al
        xor ch, ch
        jcxz .next
.dl:    call line_out
        loop .dl
        jmp .next
.eob:   call line_out
.done:  ret

put_hi: push ax
        shr al, 1
        shr al, 1
        shr al, 1
        shr al, 1
        call put_px
        pop ax
        ret
put_lo: push ax
        and al, 0Fh
        call put_px
        pop ax
        ret
; colour AL at x = BX of the line; BX advances
put_px:
        cmp bx, [lg_w]
        jae .r
        push bx
        add bx, [lg_x0]
        mov [line + bx], al
        pop bx
.r:     inc bx
        ret

line_clear:
        push di
        push cx
        push ax
        mov di, line
        mov cx, 640
        xor al, al
        rep stosb
        pop ax
        pop cx
        pop di
        ret

; draw the line at screen row lg_y (planar: 4 planes x 80 bytes), go up
line_out:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push es
        mov ax, [lg_y]
        mov cx, 80
        mul cx
        mov di, ax                      ; A000:DI = the row
        mov ax, 0A000h
        mov es, ax
        mov ah, 1                       ; plane mask
.plane: mov dx, 3C4h                    ; sequencer: map mask
        mov al, 2
        out dx, al
        inc dx
        mov al, ah
        out dx, al
        push di
        mov si, line
        mov cx, 80
.byte:  push cx
        xor bl, bl
        mov cx, 8
.bit:   lodsb
        test al, ah                     ; this plane's bit of the colour
        jz .z
        stc
        jmp .s
.z:     clc
.s:     rcl bl, 1
        loop .bit
        mov [es:di], bl
        inc di
        pop cx
        loop .byte
        pop di
        shl ah, 1
        cmp ah, 10h
        jb .plane
        mov dx, 3C4h                    ; all planes again
        mov al, 2
        out dx, al
        inc dx
        mov al, 0Fh
        out dx, al
        dec word [lg_y]
        inc word [lg_rows]
        call line_clear
        pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---- start-up data ----
want_386:   db 0
want_std:   db 0
want_real:  db 0
want_help:  db 0
xms_entry:  dd 0
xms_free:   dw 0
ff_name:    dw 0
ff_out:     dw 0
ff_sub:     dw 0
ff_dir:     times PATHMAX db 0
ff_cwd:     times PATHMAX db 0
ini_path:   times PATHMAX db 0
root_dir:   times 4 db 0
temp_dir:   times PATHMAX db 0
swap_dir:   times PATHMAX db 0
swap_ok:    db 0
isdir_save: times 68 db 0
cwd_buf:    times 68 db 0
INIBUF      equ 512
INILINE     equ 256
ini_h:      dw 0
ini_n:      dw 0
ini_p:      dw 0
ini_sec:    db 0
ini_buf:    times INIBUF db 0
ini_line:   times INILINE db 0
file_end:   dw 0
lg_bmp:     dw 0
bios_rows:  db 0
lg_w:       dw 0
lg_h:       dw 0
lg_x0:      dw 0
lg_y:       dw 0
lg_rows:    dw 0
pal_regs:   db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0
dac:        times 48 db 0
line:       times 640 db 0
sb_tail2:   db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0FFh, 0FFh, 0FFh, 0FFh, 0, 0
            db 0, 46h, 68h, 0Fh, 0, 0, 0, 0, 0, 0FFh, 0FFh, '0310', 1, 0, 0
sb_k1:      db 02h, 70h, 00h, 30h, 00h, 00h, 0Fh, 0FFh, 0FFh

n_win386:   db 'win386.exe', 0
n_dosx:     db 'dosx.exe', 0
n_wswap:    db 'wswap.exe', 0
n_dswap:    db 'dswap.exe', 0
n_system:   db 'system\', 0
n_sysini:   db 'system.ini', 0
n_krnl286:  db 'krnl286', 0
n_temp:     db 'TEMP=', 0
n_nonwin:   db 'NonWindowsApp', 0
n_swapdisk: db 'SwapDisk', 0

; ---- messages: as Windows 3.1's WIN.COM writes them (WC2, redirected
; output), line ends and all, except that the product is called w16
; (ARCHITECTURE.md 3, branding; tests/wincom.sh maps it back) ----
m_dosver:   db 'Must have MS-DOS version 3.10 or greater', 0Ah, 0Dh, 0
m_dosver_d: db 'Must have MS-DOS version 3.10 or greater', 0Ah, 0Dh, '$'
m_cannot:   db 'Your computer cannot run w16 in either standard or 386', 0Ah, 0Dh
            db 'enhanced mode.', 0Ah, 0Dh, 0
m_no386:    db 'Cannot run in 386 enhanced mode;', 0Ah, 0Dh
            db 'enter WIN to run in standard mode.', 0Ah, 0Dh, 0
m_himem:    db 'Missing HIMEM.SYS;', 0Ah, 0Dh
            db 'Make sure that the file is in your w16 directory and that its', 0Ah, 0Dh
            db 'location is correctly specified in your CONFIG.SYS file.', 0Ah, 0Dh, 0
m_nodosx:   db 'Cannot find DOSX.EXE needed to run in standard mode;', 0Ah, 0Dh
            db 'check to ensure the path is correct or reinstall w16.', 0Ah, 0Dh, 0
m_nowin386: db 'Cannot find WIN386.EXE needed to run in 386 enhanced mode;', 0Ah, 0Dh
            db 'check to ensure the path is correct or reinstall w16.', 0Ah, 0Dh, 0
m_real:     db 0Dh, 0Ah
            db 'This version of w16 does not run in real mode.', 0Dh, 0Ah
            db 0Dh, 0Ah
            db 'To run w16 in standard mode, type win /s.', 0Dh, 0Ah
            db 'To run w16 in 386 enhanced mode, type win /3.', 0Dh, 0Ah, 0
m_run_std:  db 'You are already running Standard mode w16.', 0Dh, 0Ah, 0
m_run_enh:  db 'You are already running Enhanced mode w16.', 0Dh, 0Ah
            db 0FEh, ' Press ALT+ENTER to switch this MS-DOS prompt between a window', 0Dh, 0Ah
            db '  and a full screen.', 0Dh, 0Ah, 0
m_run:      db 'You are already running w16.', 0Dh, 0Ah, 0
m_run_tail: db 0FEh, ' Type Exit and press Enter to quit this MS-DOS prompt and', 0Dh, 0Ah
            db '  return to w16.', 0Dh, 0Ah
            db 0FEh, ' Press ALT+TAB to switch to w16 or another application', 0Dh, 0Ah, 0
m_help:     db 0Dh, 0Ah
            db 'Starts w16.', 0Dh, 0Ah
            db 0Dh, 0Ah
            db 'WIN [/3] [/S] [/B] [/D:[F][S][V][X]]', 0Dh, 0Ah
            db 0Dh, 0Ah
            db '/3    Starts w16 in 386 enhanced mode.', 0Dh, 0Ah
            db '/S    Starts w16 in standard mode.', 0Dh, 0Ah
            db '/B    Creates a file, BOOTLOG.TXT, that records system messages generated', 0Dh, 0Ah
            db '      during system startup (boot).', 0Dh, 0Ah
            db '/D    Used for troubleshooting when w16 does not start correctly.', 0Dh, 0Ah
            db '  :F  Turns off 32-bit disk access.', 0Dh, 0Ah
            db '      Equivalent to SYSTEM.INI file setting: 32BitDiskAccess=FALSE.', 0Dh, 0Ah
            db '  :S  Specifies that w16 should not use ROM address space between', 0Dh, 0Ah
            db '      F000:0000 and 1 MB for a break point.', 0Dh, 0Ah
            db '      Equivalent to SYSTEM.INI file setting: SystemROMBreakPoint=FALSE.', 0Dh, 0Ah
            db '  :V  Specifies that the ROM routine will handle interrupts from the hard', 0Dh, 0Ah
            db '      disk controller.', 0Dh, 0Ah
            db '      Equivalent to SYSTEM.INI file setting: VirtualHDIRQ=FALSE.', 0Dh, 0Ah
            db '  :X  Excludes all of the adapter area from the range of memory that w16', 0Dh, 0Ah
            db '      scans to find unused space.', 0Dh, 0Ah
            db '      Equivalent to SYSTEM.INI file setting: EMMExclude=A000-FFFF.', 0Dh, 0Ah
            db 0Dh, 0Ah, 0
m_help_cfg: db 'In this configuration, WIN without any switches will launch ', 0
m_help_enh: db 'enhanced mode.', 0Dh, 0Ah, 0
m_help_std: db 'standard mode.', 0Dh, 0Ah, 0

program_end:                            ; a logo bitmap may follow
