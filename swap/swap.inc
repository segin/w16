; swap.inc - what w16's WSWAP.EXE and DSWAP.EXE share: WIN.COM's switcher
; block, the swap files, copying to and from extended memory, the MCB
; chain. Black-box facts from Windows 3.1 (ARCHITECTURE.md 9.9; the SW1
; probe, tests/dos/sw1.asm). Public domain (CC0 1.0, LICENSE).

; ---- WIN.COM's switcher block (INT 2Fh AX=4A05h SI=5), offsets ----
B_CPU       equ 001h        ; 02h 80286, 04h 80386 or later
B_TAIL      equ 005h        ; the program's command tail (length, text, CR)
B_GRABBER   equ 087h        ; the 286 grabber's path
B_SWAP1     equ 0D7h        ; the swap directory, with a trailing "\"
B_SWAP2     equ 11Bh        ; the second directory
B_WINSID    equ 16Dh        ; Windows' swap file number while it is out
B_RUNS      equ 16Fh        ; 1 once WSWAP ran, one more for each program
B_RC        equ 171h        ; Windows' exit code
B_FAIL      equ 172h        ; 08h: WSWAP could not switch
B_SID       equ 173h        ; the session WIN.COM runs next (word, 0: new)
B_FLAGS     equ 178h        ; 08h: a session ended (WSWAP, DSWAP)
B_COUNT     equ 17Ch        ; sessions besides Windows' own (FFh: none)
B_CUR       equ 17Eh        ; the record of the session to run
B_DOSMEM    equ 183h        ; paragraphs a DOS session gets
B_LINEAR    equ 185h        ; WINOLDAP's buffer for Windows' low memory
B_PROGRAM   equ 189h        ; the program's path (DOSX's at the start)
B_RECS      equ 1D9h        ; sixteen task records
REC_SIZE    equ 32h
R_NAME      equ 00h         ; the title
R_FLAGS     equ 28h         ; 08h: show the MS-DOS Prompt's banner
R_KIND      equ 29h         ; 80h free, 00h DOS program, 28h Windows
                            ; application, 68h Windows itself
R_NEXT      equ 2Ah         ; the next record in switching order
R_SESS      equ 2Bh         ; its session (swap file number, low byte)
R_SESSHI    equ 2Ch         ; 10h (swap file number, high byte)
R_SWAPPED   equ 2Dh         ; 1 once the session has been swapped out
R_HWND      equ 30h
REC_WIN     equ 15          ; record 15 is Windows itself
SESS_WIN    equ 0Fh         ; Windows' session
SESS_HI     equ 10h

; INT 2Fh AX=4A05h functions (WIN.COM's switcher interface)
SW_END      equ 2           ; the session at 17Eh has ended: free its record
SW_COUNT    equ 4           ; AX = word at 17Ch
SW_NEXT     equ 3           ; 17Eh := its record's next (a program to run)
SW_BLOCK    equ 5           ; DX:AX -> the block
SW_PEEK     equ 6           ; ES:AX -> the name of the record BX away
SW_TAKE     equ 7           ; make that record the current one

PATHMAX     equ 128

; ---- instance data (MS-DOS 5 task switcher, INT 2Fh AX=4B05h): items
; that each session keeps its own copy of. Windows 3.1's WSWAP asks at its
; start, its DSWAP at the start of each DOS session, and a DOS session's
; values stay its own across switches (SW2, tests/scripts/swap/client.txt).
; INST_DATA in T: the items (up to NINST, INSTBUF bytes together), their
; contents at the start (inst0) and the session's at a switch (instx).
; INST_CODE: inst_scan finds them (DS = CS; a structure with no list,
; 0000:0000, has none; too many bytes: none kept); inst_save copies their
; contents to DS:SI, inst_load back from DS:SI.
NINST       equ 16
INSTBUF     equ 512
%macro INST_DATA 0
n_inst:     dw 0
inst_ptr:   times NINST dd 0
inst_len:   times NINST dw 0
inst0:      times INSTBUF db 0
instx:      times INSTBUF db 0
%endmacro
%macro INST_CODE 0
inst_scan:
        push es
        xor bx, bx
        mov es, bx
        mov cx, cs
        mov dx, sw_entry
        mov ax, 4B05h
        int 2Fh
.si:    mov ax, es                      ; each startup info structure
        or ax, bx
        jz .sid
        push es
        push bx
        les si, [es:bx + 0Eh]           ; its instance data records
        mov ax, es
        or ax, si
        jz .srd
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
        les bx, [es:bx + 2]             ; the next one
        jmp .si
.sid:   xor ax, ax                      ; (all must fit our buffers)
        xor bx, bx
.sz:    cmp bx, [n_inst]
        jae .szd
        push bx
        shl bx, 1
        add ax, [inst_len + bx]
        pop bx
        jc .szbig                       ; (64 KB or more)
        inc bx
        jmp .sz
.szd:   cmp ax, INSTBUF
        jbe .szok
.szbig: mov word [n_inst], 0
.szok:  pop es
        ret
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
%endmacro

; INT 21h AH=36h's answer (AX sectors a cluster, BX free clusters, CX bytes
; a sector) -> DX:AX the free bytes, FFFFFFFFh when more (a 64 KB cluster
; does not fit a word). Keeps BX; CX lost.
%macro FREE_BYTES 0
        mul cx                          ; DX:AX = bytes a cluster
        push dx
        mul bx                          ; its low word * clusters
        pop cx
        push ax
        push dx
        xchg ax, cx                     ; its high word * clusters, << 16
        mul bx
        pop cx
        test dx, dx
        jnz %%sat
        add cx, ax
        jc %%sat
        pop ax
        mov dx, cx
        jmp %%done
%%sat:  pop ax
        mov ax, 0FFFFh
        mov dx, ax
%%done:
%endmacro
