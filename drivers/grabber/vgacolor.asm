; vgacolor.asm - w16's VGACOLOR.2GR, the Standard-mode display grabber for
; VGA (SYSTEM.INI [boot] 286grabber=vgacolor.2gr). See README.md and
; ARCHITECTURE.md 9.11.
;
; The interface is the Windows 3.1 DDK's ("Display Grabbers", the
; standard-mode grabber): a binary image (no header) that DSWAP loads and
; calls in real mode with CS = DS = its segment, through a table of 3-byte
; near jumps at offset 0, each function ending with a far return:
;   00h InquireGrab   AX 1/2: grab buffer sizes; AX FFF4h-FFFFh: the
;                     extended functions (GetBlock FFF8h, GetVersion FFFAh,
;                     DisableSave FFFBh, EnableSave FFFCh, SetSwapDrive
;                     FFFDh, GetInfo FFFEh; the rest obsolete)
;   03h-09h           obsolete
;   0Ch InquireSave   DX:AX = the save buffer's size
;   0Fh SaveScreen    the display's state into ES:0 (AX bytes)
;   12h RestoreScreen the state at ES:0 back
;   15h InitScreen    a text mode with AX lines (DSWAP looks for the jump)
; How DSWAP uses them, the values Windows 3.1's grabber answers and what it
; does with the BIOS were observed black-box (tests/dos/gr1.asm, the
; recording stand-in under DSWAP; tests/dos/grt.asm, which calls a grabber
; directly; tests/grabber.sh): Microsoft's grabber was never disassembled.
; How this one saves the display is its own: every VGA register, the DAC
; and the BIOS's video fields in the save buffer; the video memory, read as
; four planes, in a file of its own beside the swap files (~GRBxxxx.TMP in
; the directory of SetSwapDrive's template, the name WSWAP clears at start
; with the swap files); without a file, the text of a text mode in the save
; buffer.

        bits 16
        cpu 8086
        org 0

ERR_UNSUPPORTED equ 0FFh
ERR_BOUNDARY    equ 0FEh
GRB_VERSION     equ 0201h       ; what GetVersion answers (as 3.1's)
GRAB_TEXT       equ 100Ah       ; InquireGrab(1), (2) (as 3.1's)
GRAB_GRAPH      equ 4010h

; ---- the save buffer (ES:0 at SaveScreen/RestoreScreen)
SV_MAGIC equ 0                  ; 'w16G'
SV_MODE  equ 4                  ; the BIOS mode (0040:0049)
SV_FLAGS equ 5                  ; 1: the planes are in SV_FILE
SV_FILE  equ 6                  ; the planes' file (ASCIIZ)
SV_MISC  equ SV_FILE + 80
SV_SEQ   equ SV_MISC + 1        ; 5
SV_CRTC  equ SV_SEQ + 5         ; 25
SV_GC    equ SV_CRTC + 25       ; 9
SV_ATTR  equ SV_GC + 9          ; 21
SV_PEL   equ SV_ATTR + 21
SV_DAC   equ SV_PEL + 1         ; 768
SV_BDA1  equ SV_DAC + 768       ; 0040:0049-0066
SV_BDA2  equ SV_BDA1 + 30       ; 0040:0084-008A
SV_TEXT  equ SV_BDA2 + 7        ; the text page when there is no file
TEXTMAX  equ 8000               ; (80 x 50 x 2)
SV_SIZE  equ SV_TEXT + TEXTMAX

; ---- the dispatch table
        jmp near inquire_grab   ; 00h
        jmp near obsolete       ; 03h
        jmp near obsolete       ; 06h
        jmp near obsolete       ; 09h
        jmp near inquire_save   ; 0Ch
        jmp near save_screen    ; 0Fh
        jmp near restore_screen ; 12h
        jmp near init_screen    ; 15h

ext_table:                      ; FFF4h-FFFFh
        dw obsolete, obsolete, obsolete, obsolete
        dw get_block, obsolete, get_version, disable_save
        dw enable_save, set_swap_drive, get_info, obsolete

; ---- data
swap_drive: db 0FFh
swap_tpl:   times 80 db 0       ; SetSwapDrive's template
fname:      times 80 db 0
crtc:       dw 3D4h

obsolete:
        xor ax, ax
        clc
        retf

inquire_grab:
        cmp ax, 0FFF4h
        jae .ext
        cmp ax, 1
        jne .g2
        mov ax, GRAB_TEXT
        xor dx, dx
        clc
        retf
.g2:    cmp ax, 2
        jne obsolete
        mov ax, GRAB_GRAPH
        xor dx, dx
        clc
        retf
.ext:   push bx
        mov bx, ax
        sub bx, 0FFF4h
        shl bx, 1
        mov bx, [cs:ext_table+bx]
        xchg bx, [cs:ext_jump]
        pop bx
        jmp [cs:ext_jump]       ; (the target ends with retf)

ext_jump: dw 0

inquire_save:
        mov ax, SV_SIZE
        xor dx, dx
        clc
        retf

get_version:
        mov ax, GRB_VERSION
        clc
        retf

; nothing to hook: the state is read from the VGA itself at SaveScreen
enable_save:
disable_save:
        clc
        retf

; BL: the swap drive's letter (FFh none); ES:DI the swap files' template
set_swap_drive:
        push si
        push di
        push cx
        push ds
        push es
        mov [cs:swap_drive], bl
        push es
        pop ds
        push cs
        pop es
        mov si, di
        mov di, swap_tpl
        mov cx, 79
.c:     lodsb
        stosb
        or al, al
        loopnz .c
        mov byte [es:di], 0
        pop es
        pop ds
        pop cx
        pop di
        pop si
        clc
        retf

; ---- InitScreen: text mode 3 with AX lines (25, 43 or 50, rounded down)
init_screen:
        push ax
        push bx
        push cx
        push dx
        mov dx, ax
        mov ax, 1202h           ; 400 scan lines: 25 or 50 lines
        cmp dx, 50
        jae .s
        cmp dx, 43
        jb .s
        mov al, 01h             ; 350: 43 lines
.s:     mov bl, 30h
        int 10h
        mov ax, 0003h
        int 10h
        cmp dx, 43
        jb .c
        mov ax, 1112h           ; the 8 x 8 font
        xor bl, bl
        int 10h
.c:     mov ax, 0100h
        mov cx, 0607h
        int 10h
        pop dx
        pop cx
        pop bx
        pop ax
        clc
        retf

; ---- the current mode's kind: AL mode, CF set for a graphics mode
mode_kind:
        push ds
        mov ax, 40h
        mov ds, ax
        mov al, [49h]
        pop ds
        and al, 7Fh
        cmp al, 4
        jb .t
        cmp al, 7
        je .t
        stc
        ret
.t:     clc
        ret

; ---- GetInfo: GRABINFO (the documented 10 bytes, then INFOCONTEXT's
; further fields in the reserved part, as 3.1's grabber fills them) at
; ES:DI; AX 1 in a text mode, 0 in a graphics mode
get_info:
        push bx
        push cx
        push si
        push di
        push ds
        push cs
        pop ds
        push di
        mov cx, 24
        xor ax, ax
        rep stosw
        pop di
        push ds
        mov ax, 40h
        mov ds, ax
        mov al, [49h]
        mov ah, [84h]
        pop ds
        and al, 7Fh
        mov [rows_bios], ah
        mov si, info_table
.f:     mov bl, [si]
        cmp bl, 0FFh
        je .last                ; (unknown modes: the last entry)
        cmp bl, al
        je .got
        add si, INFO_ENTRY
        jmp .f
.last:  sub si, INFO_ENTRY
.got:   inc si
        mov cx, INFO_ENTRY - 1
        push di
        rep movsb
        pop di
        mov al, [es:di+1]       ; giScrType
        or al, al
        jnz .g
        mov al, [rows_bios]     ; text: the rows the BIOS has
        or al, al
        jz .t
        inc al
        mov [es:di+7], al
.t:     mov ax, 1
        jmp .e
.g:     xor ax, ax
.e:     pop ds
        pop di
        pop si
        pop cx
        pop bx
        clc
        retf

rows_bios: db 0

; per mode, as observed from Windows 3.1's grabber on a VGA (GRT, the GetInfo
; record of each mode): display id, screen type, size x/y (0.1 mm), columns,
; rows, mouse scale x/y, pixels x/y, bytes a line, bits a pixel, planes,
; interlace shift/mask, the screen's address, the page's length.
; Modes 0, 2, 5, 0Fh and 11h were not observed: 0 and 2 as 1 and 3, 5 as 4
; (UNVERIFIED), 0Fh as 10h and 11h as 12h with one plane (UNVERIFIED).
%macro INFO 18
        db %1, %2, %3
        dw %4, %5
        db %6, %7, %8, %9
        dw %10, %11, %12
        db %13, %14, %15, %16
        dw 0, %17
        dw %18
%endmacro
INFO_ENTRY equ 27
info_table:
        INFO 00h, 1, 0, 2400, 1800, 40, 25, 10h, 20h, 320, 200, 80, 1, 1, 0, 0, 0B800h, 0800h
        INFO 01h, 1, 0, 2400, 1800, 40, 25, 10h, 20h, 320, 200, 80, 1, 1, 0, 0, 0B800h, 0800h
        INFO 02h, 1, 0, 2400, 1800, 80, 25, 20h, 20h, 640, 200, 160, 1, 1, 0, 0, 0B800h, 1000h
        INFO 03h, 1, 0, 2400, 1800, 80, 25, 20h, 20h, 640, 200, 160, 1, 1, 0, 0, 0B800h, 1000h
        INFO 07h, 1, 0, 2400, 1800, 80, 25, 20h, 20h, 720, 350, 160, 1, 1, 0, 0, 0B000h, 1000h
        INFO 04h, 1, 1, 2400, 1800, 40, 25, 10h, 20h, 320, 200, 80, 2, 1, 1, 1, 0B800h, 4000h
        INFO 05h, 1, 1, 2400, 1800, 40, 25, 10h, 20h, 320, 200, 80, 2, 1, 1, 1, 0B800h, 4000h
        INFO 06h, 1, 1, 2400, 1800, 80, 25, 20h, 20h, 640, 200, 80, 1, 1, 1, 1, 0B800h, 4000h
        INFO 0Dh, 1, 3, 2400, 1800, 40, 25, 10h, 20h, 320, 200, 80, 1, 4, 0, 0, 0A000h, 2000h
        INFO 0Eh, 1, 3, 2400, 1800, 80, 25, 20h, 20h, 640, 200, 80, 1, 4, 0, 0, 0A000h, 4000h
        INFO 0Fh, 1, 3, 2400, 1800, 80, 25, 20h, 12h, 640, 350, 80, 1, 1, 0, 0, 0A000h, 0A000h
        INFO 10h, 1, 3, 2400, 1800, 80, 25, 20h, 12h, 640, 350, 80, 1, 4, 0, 0, 0A000h, 0A000h
        INFO 11h, 1, 3, 2400, 1800, 80, 30, 20h, 15h, 640, 480, 80, 1, 1, 0, 0, 0A000h, 0A000h
        INFO 12h, 1, 3, 2400, 1800, 80, 30, 20h, 15h, 640, 480, 80, 1, 4, 0, 0, 0A000h, 0A000h
        INFO 13h, 1, 3, 2400, 1800, 40, 25, 10h, 12h, 320, 200, 320, 8, 4, 0, 0, 0A000h, 2000h
        db 0FFh

; ---- GetBlock: ES:DI the GRABREQUEST (documented: grlpData dd, grXorg,
; grYorg, grXext, grYext db, grStyle db). Text modes only (3.1's answers
; ERR_UNSUPPORTED in every graphics mode). Clipboard format (grStyle bit
; 7): GRABST's GT_TEXT, gbSize, then the rows' characters, CR LF between
; rows, two NULs at the end (as 3.1's); native: the character/attribute
; pairs. AX = the bytes (or would be, when grlpData is 0).
GR_DATA  equ 0
GR_XORG  equ 4
GR_YORG  equ 5
GR_XEXT  equ 6
GR_YEXT  equ 7
GR_STYLE equ 8

get_block:
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        call mode_kind
        jnc .text
        cmp al, 7               ; the CGA modes: a bitmap of the screen
        jae .unsup
        test byte [es:di+GR_STYLE], 80h
        jz .unsup
        cmp word [es:di+GR_XORG], 0
        jne .unsup
        cmp word [es:di+GR_XEXT], 0
        jne .unsup
        jmp cga_bitmap
.unsup: mov ax, ERR_UNSUPPORTED
        jmp .err
.text:  mov bx, 0B800h
        cmp al, 7
        jne .seg
        mov bx, 0B000h
.seg:   mov ax, 40h
        mov ds, ax
        mov cx, [4Ah]           ; columns
        mov dl, [84h]           ; rows - 1
        inc dl
        mov si, [4Eh]           ; the page's start
        push cs
        pop ds
        mov [gb_cols], cx
        mov [gb_rows], dl
        mov [gb_seg], bx
        mov [gb_page], si
        ; the block
        mov al, [es:di+GR_XEXT]
        or al, al
        jnz .x1
        mov al, cl
        sub al, [es:di+GR_XORG]
        jbe .bound
.x1:    mov [gb_w], al
        mov al, [es:di+GR_YEXT]
        or al, al
        jnz .y1
        mov al, dl
        sub al, [es:di+GR_YORG]
        jbe .bound
.y1:    mov [gb_h], al
        mov al, [es:di+GR_XORG]
        add al, [gb_w]
        jc .bound
        cmp al, cl
        ja .bound
        mov al, [es:di+GR_YORG]
        add al, [gb_h]
        jc .bound
        cmp al, dl
        ja .bound
        ; its size
        mov al, [gb_w]
        mul byte [gb_h]         ; AX = cells
        test byte [es:di+GR_STYLE], 80h
        jnz .clip
        shl ax, 1
        jmp .size
.clip:  mov bl, [gb_h]
        xor bh, bh
        shl bx, 1
        add ax, bx              ; CR LF between rows, two NULs at the end
        add ax, 4
.size:  mov [gb_size], ax
        les bx, [es:di+GR_DATA]
        mov dx, es
        or dx, bx
        jnz .copy
        jmp .ok
.copy:  ; the source: the page's start + (yorg * cols + xorg) * 2
        pop es
        push es
        mov al, [es:di+GR_YORG]
        xor ah, ah
        mul word [gb_cols]
        mov dl, [es:di+GR_XORG]
        xor dh, dh
        add ax, dx
        shl ax, 1
        add ax, [gb_page]
        mov si, ax
        mov dl, [es:di+GR_STYLE]
        les di, [es:di+GR_DATA]
        mov bp, [gb_cols]
        shl bp, 1               ; a screen row's bytes
        mov ds, [gb_seg]
        test dl, 80h
        jz .native
        mov ax, 1               ; GT_TEXT
        stosw
        mov ax, [cs:gb_size]
        sub ax, 4
        stosw
        mov dh, [cs:gb_h]
.tr:    push si
        mov cl, [cs:gb_w]
        xor ch, ch
.tc:    lodsw
        stosb
        loop .tc
        pop si
        add si, bp
        dec dh
        jz .tend
        mov ax, 0A0Dh
        stosw
        jmp .tr
.tend:  xor ax, ax
        stosw
        jmp .ok
.native:
        mov dh, [cs:gb_h]
.nr:    push si
        mov cl, [cs:gb_w]
        xor ch, ch
        rep movsw
        pop si
        add si, bp
        dec dh
        jnz .nr
.ok:    mov ax, [cs:gb_size]
        pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        clc
        retf
.bound: mov ax, ERR_BOUNDARY
.err:   pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        stc
        retf

; a CGA mode (4-6) in the clipboard format: GT_NEWBITMAP, 640 x 200, one
; plane, one bit a pixel (the screen's bytes as they are, the two banks'
; lines taken in turn), as 3.1's grabber gives it (GRT: header and bits
; compared); registers as get_block pushed them
CGA_BITS equ 80 * 200
cga_bitmap:
        les bx, [es:di+GR_DATA]
        mov ax, es
        or ax, bx
        jz .ok
        mov di, bx
        cld
        mov ax, 3               ; GT_NEWBITMAP
        stosw
        mov ax, CGA_BITS + 12   ; gbSize
        stosw
        mov ax, 640
        stosw
        mov ax, 200
        stosw
        mov ax, 1               ; planes, bits a pixel
        stosw
        stosw
        mov ax, 4800            ; width and height in 0.1 mm (as 3.1's)
        stosw
        mov ax, 1800
        stosw
        mov ax, 0B800h
        mov ds, ax
        xor si, si
        mov dx, 100             ; line pairs
.l:     mov cx, 40
        rep movsw
        add si, 2000h - 80
        mov cx, 40
        rep movsw
        sub si, 2000h
        dec dx
        jnz .l
.ok:    mov ax, CGA_BITS + 16
        pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        clc
        retf

gb_cols: dw 0
gb_seg:  dw 0
gb_page: dw 0
gb_size: dw 0
gb_rows: db 0
gb_w:    db 0
gb_h:    db 0

; ---- the planes kept, for mode AL: all four, 64 KB each, whatever the
; mode (pages, fonts and what a program keeps off the screen included; a
; text mode's 32 KB window does not reach all of what its planes hold)
plane_sizes:
        mov byte [pl_all], 1
        mov byte [pl_font], 0
        ret

pl_len:  dw 0, 0, 0, 0
pl_all:  db 0                   ; 1: 64 KB of each plane
pl_font: db 0                   ; 1: 64 KB of plane 2 as well

; set the VGA for raw plane access (no chaining, no odd/even, A000h 64 KB,
; read mode 0, write mode 0, all bits, no set/reset, no rotation)
planar_on:
        mov dx, 3C4h
        mov ax, 0604h
        out dx, ax
        mov dx, 3CEh
        mov ax, 0005h
        out dx, ax
        mov ax, 0506h
        out dx, ax
        mov ax, 0001h
        out dx, ax
        mov ax, 0003h
        out dx, ax
        mov ax, 0FF08h
        out dx, ax
        ret

; BX = file, plane CL: write the plane to the file (AH 40h) or read it from
; the file (AH 3Fh), DX bytes from A000:0 (0: 64 KB, in two halves); CF on
; failure. CX kept.
plane_io:
        push cx
        push di
        mov di, dx              ; the length
        push ax
        cmp ah, 40h
        jne .rd
        mov dx, 3CEh            ; read map select
        mov al, 4
        mov ah, cl
        jmp .sel
.rd:    mov dx, 3C4h            ; map mask
        mov al, 2
        mov ah, 1
        shl ah, cl
.sel:   out dx, ax
        pop ax
        mov cx, di
        or cx, cx
        jnz .one
        mov cx, 8000h
        mov dx, 0A000h
        call chunk
        jc .e
        mov dx, 0A800h
.one:   call chunk_dx
.e:     pop di
        pop cx
        ret

; AH 3Fh/40h, BX handle, CX bytes at A000:0 (chunk) or DX:0 (chunk_dx)
chunk:
chunk_dx:
        push ax
        push cx
        push ds
        cmp cx, di              ; (a single piece: from A000:0)
        jne .s
        mov dx, 0A000h
.s:     mov ds, dx
        xor dx, dx
        int 21h
        pop ds
        pop cx
        jc .f
        cmp ax, cx
        jne .f
        pop ax
        clc
        ret
.f:     pop ax
        stc
        ret

; ---- SaveScreen: ES:DI (DI = 0) the buffer
save_screen:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        push cs
        pop ds
        cld
        mov word [es:SV_MAGIC], 'w1'
        mov word [es:SV_MAGIC+2], '6G'
        mov byte [es:SV_FLAGS], 0
        mov byte [es:SV_FILE], 0
        ; the BIOS's fields
        push ds
        mov ax, 40h
        mov ds, ax
        mov al, [49h]
        mov [es:SV_MODE], al
        mov si, 49h
        mov di, SV_BDA1
        mov cx, 30
        rep movsb
        mov si, 84h
        mov cx, 7
        rep movsb
        pop ds
        ; the registers
        mov dx, 3CCh
        in al, dx
        mov [es:SV_MISC], al
        mov bx, 3D4h
        test al, 1
        jnz .c
        mov bx, 3B4h
.c:     mov [crtc], bx
        mov di, SV_SEQ
        mov dx, 3C4h
        mov cx, 5
        call read_idx
        mov di, SV_CRTC
        mov dx, [crtc]
        mov cx, 25
        call read_idx
        mov di, SV_GC
        mov dx, 3CEh
        mov cx, 9
        call read_idx
        mov di, SV_ATTR
        xor cx, cx
.at:    mov dx, [crtc]
        add dx, 6
        in al, dx               ; the flip-flop to index
        mov dx, 3C0h
        mov al, cl
        out dx, al
        inc dx
        in al, dx
        stosb
        inc cx
        cmp cx, 21
        jb .at
        call attr_on
        mov dx, 3C6h
        in al, dx
        mov [es:SV_PEL], al
        mov dx, 3C7h
        xor al, al
        out dx, al
        mov dx, 3C9h
        mov di, SV_DAC
        mov cx, 768
.d:     in al, dx
        stosb
        loop .d
        ; the memory
        mov al, [es:SV_MODE]
        and al, 7Fh
        call plane_sizes
        call make_file
        jc .nofile
        mov bp, ax              ; the handle
        call planar_on
        xor cx, cx
.p:     mov bx, cx
        shl bx, 1
        mov dx, [pl_len+bx]
        cmp byte [pl_all], 0
        jne .all
        cmp cx, 2
        jne .p1
        cmp byte [pl_font], 0
        je .pn
.all:   xor dx, dx              ; 64 KB
        jmp .p2
.p1:    or dx, dx
        jz .pn
.p2:    mov bx, bp
        mov ah, 40h
        call plane_io
        jc .ferr
.pn:    inc cx
        cmp cx, 4
        jb .p
        mov bx, bp
        mov ah, 3Eh
        int 21h
        or byte [es:SV_FLAGS], 1
        call regs_back
        jmp .done
.ferr:  mov bx, bp
        mov ah, 3Eh
        int 21h
        call drop_file
        mov byte [es:SV_FILE], 0
        call regs_back
.nofile:
        ; no file: a text mode's page in the buffer
        call mode_kind
        jc .done
        push ds
        mov ax, 40h
        mov ds, ax
        mov cx, [4Ch]
        mov si, [4Eh]
        pop ds
        cmp cx, TEXTMAX
        jbe .tl
        mov cx, TEXTMAX
.tl:    mov ax, 0B800h
        cmp byte [es:SV_MODE], 7
        jne .ts
        mov ax, 0B000h
.ts:    push ds
        mov ds, ax
        mov di, SV_TEXT
        rep movsb
        pop ds
.done:  pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        clc
        retf

; CX registers from index 0 of the pair at DX into ES:DI
read_idx:
        xor bx, bx
.l:     mov al, bl
        out dx, al
        inc dx
        in al, dx
        dec dx
        stosb
        inc bx
        loop .l
        ret

; the attribute controller's flip-flop reset and the display on (index 20h)
attr_on:
        mov dx, [crtc]
        add dx, 6
        in al, dx
        mov dx, 3C0h
        mov al, 20h
        out dx, al
        ret

; the sequencer, graphics controller registers planar_on changed, back
; from the buffer at ES:0
regs_back:
        mov dx, 3C4h
        mov al, 2
        mov ah, [es:SV_SEQ+2]
        out dx, ax
        mov al, 4
        mov ah, [es:SV_SEQ+4]
        out dx, ax
        mov dx, 3CEh
        mov si, SV_GC
        xor bx, bx
.g:     mov al, bl
        mov ah, [es:si+bx]
        out dx, ax
        inc bx
        cmp bx, 9
        jb .g
        ret

; a new file for the planes in the template's directory, its name in
; ES:SV_FILE; AX = handle, CF if none can be made
make_file:
        cmp byte [swap_drive], 0FFh
        je .no
        cmp byte [swap_tpl], 0
        je .no
        ; the template's directory
        mov si, swap_tpl
        mov di, SV_FILE
        mov bx, di              ; after the last '\'
.c:     lodsb
        stosb
        cmp al, '\'
        jne .c1
        mov bx, di
.c1:    or al, al
        jnz .c
        push ds
        mov ax, 40h
        mov ds, ax
        mov dx, [6Ch]           ; a number to start from: the time
        pop ds
        mov bp, 16              ; tries
.t:     mov di, bx
        mov ax, '~G'
        stosw
        mov ax, 'RB'
        stosw
        mov ax, dx
        call hex4
        mov ax, '.T'
        stosw
        mov ax, 'MP'
        stosw
        xor al, al
        stosb
        push ds
        push dx
        push es
        pop ds
        mov dx, SV_FILE
        xor cx, cx
        mov ah, 5Bh             ; create a new file
        int 21h
        pop dx
        pop ds
        jnc .ok
        inc dx
        dec bp
        jnz .t
.no:    stc
        ret
.ok:    clc
        ret

drop_file:
        push ds
        push es
        pop ds
        mov dx, SV_FILE
        mov ah, 41h
        int 21h
        pop ds
        ret

; AX as four hex digits at ES:DI
hex4:   mov cx, 4
.l:     push cx
        mov cl, 4
        rol ax, cl
        pop cx
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .d
        add al, 7
.d:     stosb
        pop ax
        loop .l
        ret

; ---- RestoreScreen: ES:DI (DI = 0) the buffer SaveScreen filled
restore_screen:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        push cs
        pop ds
        cld
        cmp word [es:SV_MAGIC], 'w1'
        jne .bad
        cmp word [es:SV_MAGIC+2], '6G'
        je .go
.bad:   jmp .done
.go:    ; the mode through the BIOS (memory left alone), then everything else
        mov al, [es:SV_MODE]
        or al, 80h
        xor ah, ah
        push es
        int 10h
        pop es
        mov al, [es:SV_MISC]
        mov bx, 3D4h
        test al, 1
        jnz .c
        mov bx, 3B4h
.c:     mov [crtc], bx
        test byte [es:SV_FLAGS], 1
        jz .nofile
        ; the planes from the file
        push ds
        push es
        pop ds
        mov dx, SV_FILE
        mov ax, 3D00h
        int 21h
        pop ds
        jc .nofile
        mov bp, ax
        mov al, [es:SV_MODE]
        and al, 7Fh
        call plane_sizes
        call planar_on
        xor cx, cx
.p:     mov bx, cx
        shl bx, 1
        mov dx, [pl_len+bx]
        cmp byte [pl_all], 0
        jne .all
        cmp cx, 2
        jne .p1
        cmp byte [pl_font], 0
        je .pn
.all:   xor dx, dx
        jmp .p2
.p1:    or dx, dx
        jz .pn
.p2:    mov bx, bp
        mov ah, 3Fh
        call plane_io
        jc .pe
.pn:    inc cx
        cmp cx, 4
        jb .p
.pe:    mov bx, bp
        mov ah, 3Eh
        int 21h
        call drop_file
        jmp .regs
.nofile:
        call text_back
.regs:  call regs_all
        ; the BIOS's fields
        push es
        pop ds
        mov ax, 40h
        mov es, ax
        mov si, SV_BDA1
        mov di, 49h
        mov cx, 30
        rep movsb
        mov si, SV_BDA2
        mov di, 84h
        mov cx, 7
        rep movsb
.done:  pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        clc
        retf

; a text mode's page from the buffer (the mode as the BIOS set it)
text_back:
        mov al, [es:SV_MODE]
        and al, 7Fh
        cmp al, 4
        jb .t
        cmp al, 7
        jne .e
.t:     mov bx, 0B800h
        cmp al, 7
        jne .s
        mov bx, 0B000h
.s:     mov cx, [es:SV_BDA1 + 4Ch - 49h]
        mov di, [es:SV_BDA1 + 4Eh - 49h]
        cmp cx, TEXTMAX
        jbe .l
        mov cx, TEXTMAX
.l:     push ds
        push es
        push es
        pop ds
        mov es, bx
        mov si, SV_TEXT
        rep movsb
        pop es
        pop ds
.e:     ret

; every register from the buffer at ES:0
regs_all:
        ; sequencer, with the miscellaneous output register under a reset
        mov dx, 3C4h
        mov ax, 0100h
        out dx, ax
        mov dx, 3C2h
        mov al, [es:SV_MISC]
        out dx, al
        mov dx, 3C4h
        mov bx, 1
.s:     mov al, bl
        mov ah, [es:SV_SEQ+bx]
        out dx, ax
        inc bx
        cmp bx, 5
        jb .s
        mov al, 0
        mov ah, [es:SV_SEQ]
        out dx, ax
        ; CRTC: registers 0-7 unprotected first
        mov dx, [crtc]
        mov al, 11h
        mov ah, [es:SV_CRTC+11h]
        and ah, 7Fh
        out dx, ax
        xor bx, bx
.cr:    mov al, bl
        mov ah, [es:SV_CRTC+bx]
        cmp bl, 11h
        jne .cr1
        and ah, 7Fh
.cr1:   out dx, ax
        inc bx
        cmp bx, 25
        jb .cr
        mov al, 11h
        mov ah, [es:SV_CRTC+11h]
        out dx, ax
        ; graphics controller
        mov dx, 3CEh
        xor bx, bx
.g:     mov al, bl
        mov ah, [es:SV_GC+bx]
        out dx, ax
        inc bx
        cmp bx, 9
        jb .g
        ; attribute controller
        xor bx, bx
.a:     mov dx, [crtc]
        add dx, 6
        in al, dx
        mov dx, 3C0h
        mov al, bl
        out dx, al
        mov al, [es:SV_ATTR+bx]
        out dx, al
        inc bx
        cmp bx, 21
        jb .a
        call attr_on
        ; the DAC
        mov dx, 3C6h
        mov al, [es:SV_PEL]
        out dx, al
        mov dx, 3C8h
        xor al, al
        out dx, al
        inc dx
        mov si, SV_DAC
        mov cx, 768
.d:     mov al, [es:si]
        inc si
        out dx, al
        loop .d
        ret
