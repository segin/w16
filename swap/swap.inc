; swap.inc - what w16's WSWAP.EXE and DSWAP.EXE share: WIN.COM's switcher
; block, the swap files, copying to and from extended memory, the MCB
; chain. Black-box facts from Windows 3.1 (ARCHITECTURE.md 9.9; the SW1
; probe, tests/dos/sw1.asm). Copyright (c) 2026 The w16 contributors, MIT
; License (LICENSE).

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
