; comm.asm - w16's COMM.DRV: the Windows 3.1 communications driver for
; 8250/16450/16550 UARTs (COM1-COM4) and parallel ports (LPT1-LPT3).
; drivers/comm/README.md says what it is, where each fact comes from and
; what is verified.
;
; Interface (Windows 3.1 DDK, "Communication Drivers"; ordinals from the
; NE export table of Windows 3.1's COMM.DRV, docs/exports/COMM.txt; the
; bytes each entry takes off the stack measured by CM1, tests/apps/cm1.c):
;   1 inicom(lpDCB)                 open a port               RETF 4
;   2 setcom(lpDCB)                 set its state             RETF 4
;   3 setque(cid, lpQDB)            give it its queues        RETF 6
;   4 reccom(cid)                   read a byte               RETF 2
;   5 sndcom(cid, ch)               queue a byte              RETF 4
;   6 ctx(cid, ch)                  send a byte next          RETF 4
;   7 trmcom(cid)                   close                     RETF 2
;   8 stacom(cid, lpCOMSTAT)        error word and status     RETF 6
;   9 cextfcn(cid, fcn)             escapes (DWORD)           RETF 4
;  10 cflush(cid, queue)            empty a queue             RETF 4
;  11 cevt(cid, mask)               event mask; -> event word RETF 4
;  12 cevtGet(cid, mask)            event word, then clear    RETF 4
;  13 csetbrk(cid)                  break on                  RETF 2
;  14 cclrbrk(cid)                  break off                 RETF 2
;  15 getdcb(cid)                   -> the port's DCB         RETF 2
;  16 WEP(bSystemExit)                                        RETF 2
;  17 SuspendOpenCommPorts()        WINOLDAP, before a DOS program
;  18 ReactivateOpenCommPorts()     WINOLDAP, after it
;  19 CommWriteString(cid, lp, n)   queue n bytes             RETF 8
;  20 ReadCommString(cid, lp, n)    read up to n bytes        RETF 8
; 100 EnableNotification(cid, hwnd, in, out)  WM_COMMNOTIFY   RETF 8
; 101 (no name)
;
; A port id (cid) is 0-3 for COM1-COM4 and 80h-82h for LPT1-LPT3, the
; DCB's Id. Windows 3.1's driver has these and no more (GETMAXCOM 3,
; GETMAXLPT 82h): the bound is the interface's.

        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern GETMODULEHANDLE                  ; KERNEL.47
extern GETPROCADDRESS                   ; KERNEL.50
extern GETPRIVATEPROFILEINT             ; KERNEL.127
extern GETPRIVATEPROFILESTRING          ; KERNEL.128
extern __0040H                          ; KERNEL.193
extern CREATESYSTEMTIMER                ; SYSTEM.2
extern GETSYSTEMMSECCOUNT               ; SYSTEM.6

; ---- the interface's constants (SDK windows.h, DDK) ----
IE_BADID        equ -1
IE_OPEN         equ -2
IE_NOPEN        equ -3
IE_DEFAULT      equ -5
IE_HARDWARE     equ -10
IE_BYTESIZE     equ -11
IE_BAUDRATE     equ -12

CE_RXOVER       equ 0001h
CE_OVERRUN      equ 0002h
CE_RXPARITY     equ 0004h
CE_FRAME        equ 0008h
CE_BREAK        equ 0010h
CE_CTSTO        equ 0020h
CE_DSRTO        equ 0040h
CE_RLSDTO       equ 0080h
CE_TXFULL       equ 0100h
CE_PTO          equ 0200h
CE_IOE          equ 0400h
CE_DNS          equ 0800h
CE_OOP          equ 1000h
CE_MODE         equ 8000h

EV_RXCHAR       equ 0001h
EV_RXFLAG       equ 0002h
EV_TXEMPTY      equ 0004h
EV_CTS          equ 0008h
EV_DSR          equ 0010h
EV_RLSD         equ 0020h
EV_BREAK        equ 0040h
EV_ERR          equ 0080h
EV_RING         equ 0100h
EV_PERR         equ 0200h
EV_CTSS         equ 0400h
EV_DSRS         equ 0800h
EV_RLSDS        equ 1000h
EV_RINGTE       equ 2000h

; COMSTAT status byte
CS_CTSHOLD      equ 01h
CS_DSRHOLD      equ 02h
CS_RLSDHOLD     equ 04h
CS_XOFFHOLD     equ 08h
CS_XOFFSENT     equ 10h
CS_EOF          equ 20h
CS_TXIM         equ 40h

CN_RECEIVE      equ 1
CN_TRANSMIT     equ 2
CN_EVENT        equ 4
WM_COMMNOTIFY   equ 44h

; cextfcn codes
SETXOFF         equ 1
SETXON          equ 2
SETRTS          equ 3
CLRRTS          equ 4
SETDTR          equ 5
CLRDTR          equ 6
RESETDEV        equ 7
GETMAXLPT       equ 8
GETMAXCOM       equ 9
GETBASEIRQ      equ 10

; DCB (SDK tagDCB) offsets
D_ID            equ 0
D_BAUD          equ 1
D_BYTESIZE      equ 3
D_PARITY        equ 4
D_STOPBITS      equ 5
D_RLSTO         equ 6
D_CTSTO         equ 8
D_DSRTO         equ 10
D_FLAGS         equ 12              ; word of bit fields
D_XONCHAR       equ 14
D_XOFFCHAR      equ 15
D_XONLIM        equ 16
D_XOFFLIM       equ 18
D_PECHAR        equ 20
D_EOFCHAR       equ 21
D_EVTCHAR       equ 22
D_TXDELAY       equ 23
DCB_SIZE        equ 25
; D_FLAGS bits
F_BINARY        equ 0001h
F_RTSDISABLE    equ 0002h
F_PARITY        equ 0004h
F_OUTXCTSFLOW   equ 0008h
F_OUTXDSRFLOW   equ 0010h
F_DTRDISABLE    equ 0080h
F_OUTX          equ 0100h
F_INX           equ 0200h
F_PECHAR        equ 0400h
F_NULL          equ 0800h
F_CHEVT         equ 1000h
F_DTRFLOW       equ 2000h
F_RTSFLOW       equ 4000h

; 8250 family registers (offsets from the base port)
U_DATA          equ 0
U_IER           equ 1
U_IIR           equ 2               ; read
U_FCR           equ 2               ; write (16550)
U_LCR           equ 3
U_MCR           equ 4
U_LSR           equ 5
U_MSR           equ 6
; IER
IER_RX          equ 01h
IER_TX          equ 02h
IER_LS          equ 04h
IER_MS          equ 08h
; MCR
MCR_DTR         equ 01h
MCR_RTS         equ 02h
MCR_OUT2        equ 08h
; LSR
LSR_DR          equ 01h
LSR_OE          equ 02h
LSR_PE          equ 04h
LSR_FE          equ 08h
LSR_BI          equ 10h
LSR_THRE        equ 20h
LSR_TEMT        equ 40h
; MSR
MSR_DCTS        equ 01h
MSR_DDSR        equ 02h
MSR_TERI        equ 04h
MSR_DDCD        equ 08h
MSR_CTS         equ 10h
MSR_DSR         equ 20h
MSR_RI          equ 40h
MSR_DCD         equ 80h

NCOM            equ 4               ; COM1-COM4 (GETMAXCOM 3)
NLPT            equ 3               ; LPT1-LPT3 (GETMAXLPT 82h)
NPORT           equ NCOM + NLPT
UART_CLOCK_HI   equ 1               ; 1.8432 MHz / 16 = 115200 = 1:C200h
UART_CLOCK_LO   equ 0C200h

; ---- per port (COM and LPT alike) ----
struc P
.dcb            resb 26             ; the DCB (getdcb's pointer)
.evt            resw 1              ; the event word (cevt's pointer)
.evtmask        resw 1
.base           resw 1              ; I/O base, 0: no port
.irq            resb 1
.isopen         resb 1
.err            resw 1              ; CE_ bits since the last stacom
.stat           resb 1              ; COMSTAT status bits
.msr            resb 1              ; the modem status last read
.mcr            resb 1              ; what the modem control should be
.lcr            resb 1
.fifo           resb 1              ; FIFO enabled
.txon           resb 1              ; transmit interrupt enabled
.txim           resb 1              ; ctx's byte
.sendx          resb 1              ; XON or XOFF to send first (0: none)
.rxflow         resb 1              ; receive flow stopped (XOFF sent or
                                    ; RTS/DTR dropped)
.brk            resb 1              ; break set
.notify         resb 1              ; NS_ notification state
.evnew          resb 1              ; an event newly in the event word
.fresh          resb 1              ; program: a fresh start (open)
.rxq            resd 1
.rxsize         resw 1
.rxcnt          resw 1
.rxin           resw 1
.rxout          resw 1
.txq            resd 1
.txsize         resw 1
.txcnt          resw 1
.txin           resw 1
.txout          resw 1
.hwnd           resw 1
.intrig         resw 1
.outtrig        resw 1
.lastrx         resd 1              ; GetSystemMsecCount at the last byte
.savefifo       resb 1              ; before the open: the FIFO (IIR C0h)
.savemcr        resb 1              ; and the modem control
.size:
endstruc

segment _DATA
ports           times NPORT * P.size db 0
irqref          times 16 db 0       ; open ports per IRQ
oldvec          times 16 dd 0       ; the handlers before ours
oldmask         times 16 db 0       ; the IRQ's PIC mask bit before
postmsg         dd 0                ; USER's PostMessage
usermod         db "USER", 0
postname        db "POSTMESSAGE", 0
s386enh         db "386Enh", 0
ssysini         db "SYSTEM.INI", 0
kbase           db "COM1Base", 0
kirq            db "COM1Irq", 0
kfifo           db "COM1FIFO", 0
empty           db 0
inibuf          times 16 db 0

segment _TEXT

; ---- LibEntry: DS = DGROUP. ----
global libentry
libentry:
        push si
        push di
        ; a system timer for the receive time-out and the notifications
        push word 100                   ; ms
        push cs
        push word timer
        call far CREATESYSTEMTIMER
        mov ax, 1
        pop di
        pop si
        retf

; enter: the usual frame; DS = DGROUP. leave n: undo it, RETF n.
%macro ENTER 0
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
%endmacro
%macro LEAVE 1
        pop di
        pop si
        pop ds
        pop bp
        retf %1
%endmacro

; port: AX = cid -> SI = its block, CF clear; CF set for an id that is
; no port (COM 0-3, LPT 80h-82h).
port:
        cmp ax, NCOM
        jb .com
        sub ax, 80h
        jb .bad
        cmp ax, NLPT
        jae .bad
        add ax, NCOM
.com:   mov si, P.size
        mul si
        add ax, ports
        mov si, ax
        clc
        ret
.bad:   stc
        ret

; ---- 1: int inicom(DCB FAR *) ----
global INICOM
INICOM:
        ENTER
        les di, [bp + 6]
        mov al, [es:di + D_ID]
        xor ah, ah
        mov bx, ax
        call port
        mov ax, IE_BADID
        jc .ret
        ; (an open port is opened again: no IE_OPEN from the driver, CM1;
        ; USER's OpenComm reports that)
        mov ax, bx
        call findport                   ; base and IRQ
        mov ax, IE_HARDWARE
        jc .ret
        cmp bl, 80h
        jae .lpt
        call validate                   ; ES:DI the DCB
        jc .ret
        call copydcb
        xor ax, ax
        mov [si + P.err], ax
        mov [si + P.evt], ax
        mov [si + P.evtmask], ax
        mov [si + P.stat], al
        mov [si + P.txon], al
        mov [si + P.sendx], al
        mov [si + P.rxflow], al
        mov [si + P.brk], al
        mov [si + P.notify], al
        mov [si + P.hwnd], ax
        mov [si + P.rxcnt], ax
        mov [si + P.rxin], ax
        mov [si + P.rxout], ax
        mov [si + P.txcnt], ax
        mov [si + P.txin], ax
        mov [si + P.txout], ax
        cmp byte [si + P.isopen], 0
        jne .again
        ; the UART as it was, for trmcom (CM1: the modem control and FIFO
        ; after a close are those before the open)
        mov dx, [si + P.base]
        add dx, U_IIR
        in al, dx
        and al, 0C0h
        mov [si + P.savefifo], al
        add dx, U_MCR - U_IIR
        in al, dx
        mov [si + P.savemcr], al
        mov byte [si + P.isopen], 1
        call hook
.again: mov byte [si + P.fresh], 1
        call program
        xor ax, ax
        jmp .ret
.lpt:   call copydcb
        xor ax, ax
        mov [si + P.err], ax
        mov [si + P.evt], ax
        mov [si + P.evtmask], ax
        mov [si + P.stat], al
        mov byte [si + P.isopen], 1
.ret:   LEAVE 4

; findport: SI = the block, AX = cid. Sets base and IRQ; CF when the
; port does not exist. COM: the BIOS data area's base, else SYSTEM.INI
; [386Enh] COMnBase (hexadecimal), else 3E8h for COM3; IRQ from COMnIrq,
; else 4, 3, 4, 3. The DDK ("Base Address and IRQ Selection"): COMnBase
; never overrides the BIOS data area; COMnBase and COMnIrq hold in
; Standard mode too; COM3 is 3E8h and IRQ 4 when nothing says otherwise.
; LPT: the BIOS data area's base.
findport:
        push es
        push bx
        push di
        mov bx, ax
        mov ax, __0040H
        mov es, ax
        cmp bx, 80h
        jae .lpt
        mov di, bx
        shl di, 1
        mov ax, [es:di]
        test ax, ax
        jnz .base
        call inikey
        push bx
        mov ax, kbase
        call inihex
        pop bx
        test ax, ax
        jnz .base
        mov ax, [cs:defbase + di]
        test ax, ax
        jz .none
.base:  mov [si + P.base], ax
        call inikey
        push bx
        push ds
        push word s386enh
        push ds
        push word kirq
        mov al, [cs:defirq + bx]
        xor ah, ah
        push ax
        push ds
        push word ssysini
        call far GETPRIVATEPROFILEINT
        pop bx
        and al, 15
        mov [si + P.irq], al
        clc
        jmp .ret
.lpt:   sub bx, 80h
        shl bx, 1
        mov ax, [es:bx + 8]
        test ax, ax
        jz .none
        mov [si + P.base], ax
        clc
        jmp .ret
.none:  stc
.ret:   pop di
        pop bx
        pop es
        ret
; IRQs and bases as Windows 3.1 has them by default (DDK "Base Address and
; IRQ Selection")
defirq          db 4, 3, 4, 3
defbase         dw 0, 0, 3E8h, 0

; inikey: BX = COM index 0-3: the digit in the key names
inikey:
        mov al, bl
        add al, '1'
        mov [kbase + 3], al
        mov [kirq + 3], al
        mov [kfifo + 3], al
        ret

; inihex: AX = a key in [386Enh] of SYSTEM.INI -> AX = its value read as
; hexadecimal (an "h" after the digits allowed), 0 when absent
inihex:
        push si
        push ds
        push word s386enh
        push ds
        push ax
        push ds
        push word empty
        push ds
        push word inibuf
        push word 16
        push ds
        push word ssysini
        call far GETPRIVATEPROFILESTRING
        mov si, inibuf
        xor dx, dx
        cld
.next:  lodsb
        cmp al, '0'
        jb .done
        cmp al, '9'
        jbe .dig
        or al, 20h
        cmp al, 'a'
        jb .done
        cmp al, 'f'
        ja .done
        sub al, 'a' - 10 - '0'
.dig:   sub al, '0'
        shl dx, 4
        or dl, al
        jmp .next
.done:  mov ax, dx
        pop si
        ret

; validate: ES:DI = a DCB for a COM port. CF and AX = the error when it
; is not acceptable. The order (CM1, two fields wrong at a time): the
; baud rate (IE_BAUDRATE), the parity (IE_DEFAULT), the byte size (5-8,
; IE_BYTESIZE), the stop bits (IE_DEFAULT; 1.5 only with 5-bit bytes).
validate:
        mov ax, [es:di + D_BAUD]
        call divisor
        mov ax, IE_BAUDRATE
        jc .bad
        cmp byte [es:di + D_PARITY], 4
        ja .def
        mov al, [es:di + D_BYTESIZE]
        cmp al, 5
        jb .bsz
        cmp al, 8
        ja .bsz
        mov al, [es:di + D_STOPBITS]
        cmp al, 2
        ja .def
        cmp al, 1                       ; 1.5 stop bits: 5-bit bytes only
        jne .ok
        cmp byte [es:di + D_BYTESIZE], 5
        jne .def
.ok:    clc
        ret
.def:   mov ax, IE_DEFAULT
        stc
        ret
.bsz:   mov ax, IE_BYTESIZE
.bad:   stc
        ret

; divisor: AX = DCB BaudRate -> AX = the divisor, DX = the rate the FIFO
; decision uses; CF for a rate the driver does not take. Rates are
; 115200 / BaudRate, truncated (CM1: 110 gives 1047, 134 gives 859);
; 0 and 1 are refused. A high byte of FFh is a CBR_ index (DDK "Baud-Rate
; Indexes"): those below are what Windows 3.1's driver takes (CM1, its
; divisors; CBR_14400 gives 9, not 8); FF00h-FF0Fh give divisor 1 and no
; FIFO; the other indexes are refused.
divisor:
        cmp ah, 0FFh
        je .index
        cmp ax, 2
        jb .bad
        mov cx, ax
        mov dx, UART_CLOCK_HI
        mov ax, UART_CLOCK_LO
        div cx
        mov dx, cx
        clc
        ret
.index: cmp al, 10h
        jb .low
        push bx
        mov bl, al
        sub bl, 10h
        cmp bl, cbr_n
        jae .badi
        xor bh, bh
        shl bx, 1
        mov dx, [cs:cbr_rate + bx]
        mov ax, [cs:cbr_div + bx]
        pop bx
        test ax, ax
        jz .bad
        clc
        ret
.badi:  pop bx
.bad:   stc
        ret
.low:   mov ax, 1
        xor dx, dx
        clc
        ret
; CBR_110 (FF10h) ... CBR_56000 (FF1Fh): the rate, the divisor (0: not
; taken). 115200 / rate, except 14400's 9 (observed, CM1); 128000 and
; 256000 (FF23h, FF27h) are beyond index 1Fh and are refused (CM1).
cbr_rate:
        dw 110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 0, 0, 38400
        dw 0, 0, 0, 56000
cbr_div:
        dw 1047, 384, 192, 96, 48, 24, 12, 9, 6, 0, 0, 3
        dw 0, 0, 0, 2
cbr_n   equ ($ - cbr_div) / 2

; copydcb: ES:DI = DCB -> the block's
copydcb:
        push ds
        push es
        push si
        push di
        mov ax, ds
        push es
        pop ds
        mov es, ax
        xchg si, di                     ; DS:SI the caller's, ES:DI ours
        mov cx, DCB_SIZE
        cld
        rep movsb
        pop di
        pop si
        pop es
        pop ds
        ret

; program: SI = an open COM port: the UART set up from the DCB (line
; control, divisor, FIFO, modem control, interrupts).
program:
        mov dx, [si + P.base]
        cli
        mov ax, [si + P.dcb + D_BAUD]
        call divisor
        push dx                         ; the rate
        mov cx, ax
        ; line control: 5-8 bits, 2 stop bits (1.5 with 5 bits), parity
        mov al, [si + P.dcb + D_BYTESIZE]
        sub al, 5
        cmp byte [si + P.dcb + D_STOPBITS], 2
        jne .p
        or al, 04h
.p:     mov bl, [si + P.dcb + D_PARITY]
        test bl, bl
        jz .lc
        dec bl                          ; 1 odd 08h, 2 even 18h, 3 mark
        shl bl, 4                       ; 28h, 4 space 38h
        or bl, 08h
        or al, bl
.lc:    cmp byte [si + P.brk], 0
        je .nb
        or al, 40h
.nb:    mov [si + P.lcr], al
        mov dx, [si + P.base]
        add dx, U_LCR
        or al, 80h
        out dx, al
        sub dx, U_LCR
        mov al, cl
        out dx, al
        inc dx
        mov al, ch
        out dx, al
        add dx, U_LCR - 1
        mov al, [si + P.lcr]
        out dx, al
        ; FIFO: on at 4800 and more (CM1), when SYSTEM.INI [386Enh]
        ; COMnFIFO is not 0 (DDK "16550a UART FIFO Buffer") and the UART
        ; has one
        pop cx
        sti
        cmp cx, 4800
        jb .nofifo
        mov ax, si
        sub ax, ports
        xor dx, dx
        mov bx, P.size
        div bx
        mov bx, ax
        call inikey
        push ds
        push word s386enh
        push ds
        push word kfifo
        push word 1
        push ds
        push word ssysini
        call far GETPRIVATEPROFILEINT
        test ax, ax
        jz .nofifo
        ; on (and emptied) at an open, or when it was off; left alone
        ; when it is on already: setcom must not lose what the FIFOs
        ; hold (it did, in w16's first version: a byte of a command to
        ; commhost)
        cmp byte [si + P.fresh], 0
        jne .on
        cmp byte [si + P.fifo], 0
        jne .mcr
.on:    mov dx, [si + P.base]
        add dx, U_FCR
        mov al, 0C7h                    ; on, both cleared, 14 bytes
        out dx, al                      ; (the trigger: UNVERIFIED)
        in al, dx
        and al, 0C0h
        cmp al, 0C0h
        mov byte [si + P.fifo], 1
        je .mcr
.nofifo:
        mov byte [si + P.fifo], 0
        mov dx, [si + P.base]
        add dx, U_FCR
        xor al, al
        out dx, al
.mcr:   call setmcr
        cli
        cmp byte [si + P.fresh], 0
        je .ier
        mov byte [si + P.fresh], 0
        mov dx, [si + P.base]
        add dx, U_MSR
        in al, dx
        mov [si + P.msr], al
        sub dx, U_MSR - U_LSR
        in al, dx
        sub dx, U_LSR
        in al, dx                       ; nothing old waiting
.ier:   call setier
        sti
        ret

; setmcr: SI = port: DTR and RTS from the DCB and the receive flow, OUT2
setmcr:
        mov al, MCR_OUT2
        mov bx, [si + P.dcb + D_FLAGS]
        test bx, F_DTRDISABLE
        jnz .r
        test bx, F_DTRFLOW
        jz .d
        cmp byte [si + P.rxflow], 0
        jne .r
.d:     or al, MCR_DTR
.r:     test bx, F_RTSDISABLE
        jnz .o
        test bx, F_RTSFLOW
        jz .s
        cmp byte [si + P.rxflow], 0
        jne .o
.s:     or al, MCR_RTS
.o:     mov [si + P.mcr], al
        mov dx, [si + P.base]
        add dx, U_MCR
        out dx, al
        ret

; setier: SI = port: receive, line and modem status interrupts, and
; transmit when there is something to send (interrupts off)
setier:
        mov al, IER_RX | IER_LS | IER_MS
        cmp byte [si + P.txon], 0
        je .w
        or al, IER_TX
.w:     mov dx, [si + P.base]
        add dx, U_IER
        out dx, al
        ret

; ---- hooking IRQs ----
; hook: SI = a COM port being opened: its IRQ's handler is ours
hook:
        mov bl, [si + P.irq]
        xor bh, bh
        inc byte [irqref + bx]
        cmp byte [irqref + bx], 1
        jne .ret
        push es
        mov al, bl
        call vector
        push ax
        mov ah, 35h
        int 21h
        mov di, bx
        mov bl, [si + P.irq]
        xor bh, bh
        shl bx, 2
        mov [oldvec + bx], di
        mov [oldvec + bx + 2], es
        pop ax
        pop es
        push ds
        mov bl, [si + P.irq]
        xor bh, bh
        shl bx, 1
        mov dx, [cs:isr_table + bx]
        push cs
        pop ds
        mov ah, 25h
        int 21h
        pop ds
        ; unmask at the PIC (and the cascade for IRQ 8-15)
        mov cl, [si + P.irq]
        cli
        cmp cl, 8
        jae .slave
        mov dx, 21h
        jmp .m
.slave: sub cl, 8
        in al, 21h
        and al, ~04h
        out 21h, al
        mov dx, 0A1h
.m:     mov ah, 1
        shl ah, cl
        in al, dx
        mov bl, [si + P.irq]
        xor bh, bh
        mov ch, 0
        test al, ah
        jz .wasun
        mov ch, 1
.wasun: mov [oldmask + bx], ch
        not ah
        and al, ah
        out dx, al
        sti
.ret:   ret

; unhook: SI = a COM port being closed
unhook:
        mov bl, [si + P.irq]
        xor bh, bh
        dec byte [irqref + bx]
        jnz .ret
        cmp byte [oldmask + bx], 0
        je .vec
        mov cl, bl
        mov dx, 21h
        cmp cl, 8
        jb .m
        sub cl, 8
        mov dx, 0A1h
.m:     mov ah, 1
        shl ah, cl
        cli
        in al, dx
        or al, ah
        out dx, al
        sti
.vec:   mov al, bl
        call vector
        shl bx, 2
        push ds
        lds dx, [oldvec + bx]
        mov ah, 25h
        int 21h
        pop ds
.ret:   ret

; vector: AL = IRQ -> AL = its interrupt vector (08h-0Fh, 70h-77h)
vector:
        cmp al, 8
        jae .s
        add al, 8
        ret
.s:     add al, 70h - 8
        ret

; ---- interrupts ----
%macro ISR 1
isr%1:  push ax
        mov al, %1
        jmp isr_common
%endmacro
%assign i 0
%rep 16
ISR i
%assign i i + 1
%endrep
isr_table:
%assign i 0
%rep 16
        dw isr %+ i
%assign i i + 1
%endrep

; every open COM port on the IRQ is serviced until none has anything
; pending (ports sharing an edge-triggered IRQ: an interrupt that comes
; while another port is being serviced would otherwise be lost), then
; end of interrupt
isr_common:
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        cld
        mov bx, DGROUP
        mov ds, bx
        mov ah, al
.pass:  xor di, di                      ; serviced anything this pass
        mov si, ports
        mov cx, NCOM
.next:  cmp byte [si + P.isopen], 0
        je .skip
        cmp [si + P.irq], ah
        jne .skip
        push ax
        push cx
        call service
        pop cx
        pop ax
        or di, bx
.skip:  add si, P.size
        loop .next
        test di, di
        jnz .pass
        mov al, 20h
        cmp ah, 8
        jb .eoi
        out 0A0h, al
.eoi:   out 20h, al
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        iret

; service: SI = an open COM port: everything its UART has pending.
; BX = nonzero when there was something.
service:
        xor bx, bx
.again: mov dx, [si + P.base]
        add dx, U_IIR
        in al, dx
        test al, 1
        jnz .ret
        inc bx
        and al, 0Eh
        cmp al, 6
        je .ls
        cmp al, 4
        je .rx
        cmp al, 0Ch
        je .rx
        cmp al, 2
        je .tx
        call modem
        jmp .again
.ls:    add dx, U_LSR - U_IIR
        in al, dx
        call linestat
        jmp .again
.rx:    call receive
        jmp .again
.tx:    call transmit
        jmp .again
.ret:   ret

; linestat: AL = LSR: errors and break into the error word and events
linestat:
        push bx
        xor bx, bx
        test al, LSR_OE
        jz .1
        or bx, CE_OVERRUN
.1:     test al, LSR_PE
        jz .2
        or bx, CE_RXPARITY
.2:     test al, LSR_FE
        jz .3
        or bx, CE_FRAME
.3:     mov cx, bx
        test bx, bx
        jz .4
        push cx
        mov cx, EV_ERR
        call event
        pop cx
.4:     test al, LSR_BI
        jz .5
        or cx, CE_BREAK
        push cx
        mov cx, EV_BREAK
        call event
        pop cx
.5:     or [si + P.err], cx
        pop bx
        ret

; receive: every byte the UART has
receive:
.loop:  mov dx, [si + P.base]
        add dx, U_LSR
        in al, dx
        test al, LSR_OE | LSR_PE | LSR_FE | LSR_BI
        jz .ok
        push ax
        call linestat
        pop ax
.ok:    test al, LSR_DR
        jz .ret
        mov ah, al                      ; LSR
        sub dx, U_LSR
        in al, dx
        call rxbyte
        jmp .loop
.ret:   ret

; rxbyte: AL = a byte received, AH = the LSR it came with
rxbyte:
        mov bx, [si + P.dcb + D_FLAGS]
        test bx, F_NULL
        jz .1
        test al, al
        jz .ret
.1:     test ah, LSR_PE
        jz .2
        test bx, F_PECHAR
        jz .2
        mov al, [si + P.dcb + D_PECHAR]
.2:     test bx, F_OUTX
        jz .3
        cmp al, [si + P.dcb + D_XOFFCHAR]
        jne .xon
        or byte [si + P.stat], CS_XOFFHOLD
        ret
.xon:   cmp al, [si + P.dcb + D_XONCHAR]
        jne .3
        and byte [si + P.stat], ~CS_XOFFHOLD
        jmp kicktx
        ; the end of the input: outside binary mode the EOF character
        ; ends it; what comes after is refused as an overflow until the
        ; port is opened again (CM1: setcom and cflush do not end it)
.3:     test byte [si + P.stat], CS_EOF
        jz .3b
        or word [si + P.err], CE_RXOVER
        ret
.3b:    test bx, F_BINARY
        jnz .4
        cmp al, [si + P.dcb + D_EOFCHAR]
        jne .4
        or byte [si + P.stat], CS_EOF
        ret
.4:     mov cx, EV_RXCHAR
        cmp al, [si + P.dcb + D_EVTCHAR]
        jne .5
        or cx, EV_RXFLAG                ; (fChEvt or not: CM1)
.5:     call event
        or byte [si + P.notify], NS_RXARM
        mov cx, [si + P.rxcnt]
        cmp cx, [si + P.rxsize]
        jb .store
        or word [si + P.err], CE_RXOVER
        ret
.store: les di, [si + P.rxq]
        add di, [si + P.rxin]
        stosb
        mov ax, [si + P.rxin]
        inc ax
        cmp ax, [si + P.rxsize]
        jb .w
        xor ax, ax
.w:     mov [si + P.rxin], ax
        inc word [si + P.rxcnt]
        push es
        push bx
        call getms
        mov [si + P.lastrx], ax
        mov [si + P.lastrx + 2], dx
        pop bx
        pop es
        ; input flow control: stop the sender when the queue is within
        ; XoffLim of full
        test bx, F_INX | F_RTSFLOW | F_DTRFLOW
        jz .ret
        cmp byte [si + P.rxflow], 0
        jne .ret
        mov cx, [si + P.rxsize]
        sub cx, [si + P.dcb + D_XOFFLIM]
        jb .ret
        cmp [si + P.rxcnt], cx
        jb .ret
        mov byte [si + P.rxflow], 1
        test bx, F_INX
        jz .hw
        mov al, [si + P.dcb + D_XOFFCHAR]
        mov [si + P.sendx], al
        or byte [si + P.stat], CS_XOFFSENT
        call kicktx
.hw:    call setmcr
.ret:   ret

; rxtaken: SI = port, after bytes were taken from the receive queue:
; start the sender again when the queue is down to XonLim
rxtaken:
        cmp byte [si + P.rxflow], 0
        je .ret
        mov ax, [si + P.rxcnt]
        cmp ax, [si + P.dcb + D_XONLIM]
        ja .ret
        mov byte [si + P.rxflow], 0
        test word [si + P.dcb + D_FLAGS], F_INX
        jz .hw
        test byte [si + P.stat], CS_XOFFSENT
        jz .hw
        and byte [si + P.stat], ~CS_XOFFSENT
        mov al, [si + P.dcb + D_XONCHAR]
        mov [si + P.sendx], al
        call kicktx
.hw:    call setmcr
.ret:   ret

; getms: DX:AX = GetSystemMsecCount
getms:
        push bx
        push cx
        call far GETSYSTEMMSECCOUNT
        pop cx
        pop bx
        ret

; modem: the modem status into events and holds
modem:
        mov dx, [si + P.base]
        add dx, U_MSR
        in al, dx
        mov [si + P.msr], al
        xor cx, cx
        test al, MSR_DCTS
        jz .1
        or cx, EV_CTS
.1:     test al, MSR_DDSR
        jz .2
        or cx, EV_DSR
.2:     test al, MSR_DDCD
        jz .3
        or cx, EV_RLSD
.3:     test al, MSR_TERI
        jz .4
        or cx, EV_RINGTE
.4:     test al, MSR_RI
        jz .5
        or cx, EV_RING
        ; EV_CTSS, EV_DSRS, EV_RLSDS: the lines' states at this change
        ; (CM1: set while the line is on)
.5:     and word [si + P.evt], ~(EV_CTSS | EV_DSRS | EV_RLSDS)
        test al, MSR_CTS
        jz .6
        or cx, EV_CTSS
.6:     test al, MSR_DSR
        jz .7
        or cx, EV_DSRS
.7:     test al, MSR_DCD
        jz .8
        or cx, EV_RLSDS
.8:     call event
        call holds
        jmp kicktx

; holds: the CTS/DSR holds from the last modem status
holds:
        mov al, [si + P.msr]
        mov bx, [si + P.dcb + D_FLAGS]
        and byte [si + P.stat], ~(CS_CTSHOLD | CS_DSRHOLD)
        test bx, F_OUTXCTSFLOW
        jz .1
        test al, MSR_CTS
        jnz .1
        or byte [si + P.stat], CS_CTSHOLD
.1:     test bx, F_OUTXDSRFLOW
        jz .2
        test al, MSR_DSR
        jnz .2
        or byte [si + P.stat], CS_DSRHOLD
.2:     ret

; kicktx: SI = port: the transmit interrupt on when there is something
; to send and nothing holds it (interrupts off)
kicktx:
        cmp byte [si + P.sendx], 0
        jne .on
        test byte [si + P.stat], CS_CTSHOLD | CS_DSRHOLD | CS_XOFFHOLD
        jnz .ret
        test byte [si + P.stat], CS_TXIM
        jnz .on
        cmp word [si + P.txcnt], 0
        je .ret
        cmp byte [si + P.brk], 0
        jne .ret
.on:    cmp byte [si + P.txon], 0
        jne .ret
        mov byte [si + P.txon], 1
        call setier
.ret:   ret

; transmit: the UART can take more
transmit:
        cmp byte [si + P.brk], 0
        jne .off
        ; only into an empty holding register (a rewrite of IER can
        ; bring a transmit interrupt with it; a byte written over another
        ; is lost)
        mov dx, [si + P.base]
        add dx, U_LSR
        in al, dx
        test al, LSR_OE | LSR_PE | LSR_FE | LSR_BI
        jz .lsr
        push ax
        call linestat
        pop ax
.lsr:   test al, LSR_THRE
        jz .ret
        ; one byte an interrupt, FIFO or not (CM1: Windows 3.1's driver
        ; has 2 of 300 bytes out right after the write, as one a time
        ; gives); XON and XOFF first and whatever holds the output;
        ; then ctx's byte and the queue, unless the output is held
.loop:  mov al, [si + P.sendx]
        test al, al
        jz .im
        mov byte [si + P.sendx], 0
        jmp .out
.im:    test byte [si + P.stat], CS_CTSHOLD | CS_DSRHOLD | CS_XOFFHOLD
        jnz .off
        test byte [si + P.stat], CS_TXIM
        jz .q
        and byte [si + P.stat], ~CS_TXIM
        mov al, [si + P.txim]
        jmp .out
.q:
        cmp word [si + P.txcnt], 0
        je .empty
        les di, [si + P.txq]
        mov bx, [si + P.txout]
        mov al, [es:di + bx]
        inc bx
        cmp bx, [si + P.txsize]
        jb .w
        xor bx, bx
.w:     mov [si + P.txout], bx
        dec word [si + P.txcnt]
        jnz .out
        mov cx, EV_TXEMPTY
        call event
.out:   mov dx, [si + P.base]
        out dx, al
.ret:   ret
.empty:
.off:   mov byte [si + P.txon], 0
        jmp setier

; ---- the timer: notifications (every 100 ms, interrupt time) ----
timer:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        mov ax, DGROUP
        mov ds, ax
        mov si, ports
        mov cx, NCOM
.next:  cmp byte [si + P.isopen], 0
        je .skip
        cmp word [si + P.hwnd], 0
        je .skip
        push cx
        call notify
        pop cx
.skip:  add si, P.size
        loop .next
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        retf

; notify: SI = an open port with a window: WM_COMMNOTIFY when due
; (CM1, against the DDK's EnableNotification), each code its own
; message. CN_EVENT when an event newly entered the event word (the
; events in the word when notification was enabled do not count).
; CN_TRANSMIT when the transmit queue, having reached the out trigger,
; is below it. CN_RECEIVE when the receive queue reaches the in trigger
; (again only after it was below), or, below the trigger, when bytes came
; and then nothing for RX_TIMEOUT ms (once for those bytes). A trigger
; of -1: never.
NS_RXPOSTED     equ 1                   ; CN_RECEIVE for the trigger sent
NS_RXARM        equ 2                   ; bytes came since the last one
NS_TXARM        equ 4                   ; the queue reached the trigger
RX_TIMEOUT      equ 100                 ; ms
notify:
        cmp byte [si + P.evnew], 0
        je .tx
        mov byte [si + P.evnew], 0
        mov cx, CN_EVENT
        call post
.tx:    test byte [si + P.notify], NS_TXARM
        jz .rx
        mov ax, [si + P.txcnt]
        cmp ax, [si + P.outtrig]
        jae .rx
        and byte [si + P.notify], ~NS_TXARM
        mov cx, CN_TRANSMIT
        call post
.rx:    cmp word [si + P.intrig], -1
        je .ret
        mov ax, [si + P.rxcnt]
        cmp ax, [si + P.intrig]
        jb .below
        test byte [si + P.notify], NS_RXPOSTED
        jnz .ret
        or byte [si + P.notify], NS_RXPOSTED
        and byte [si + P.notify], ~NS_RXARM
        jmp .rxpost
.below: and byte [si + P.notify], ~NS_RXPOSTED
        test byte [si + P.notify], NS_RXARM
        jz .ret
        test ax, ax
        jz .ret
        call getms
        sub ax, [si + P.lastrx]
        sbb dx, [si + P.lastrx + 2]
        jnz .to
        cmp ax, RX_TIMEOUT
        jb .ret
.to:    and byte [si + P.notify], ~NS_RXARM
.rxpost:
        mov cx, CN_RECEIVE
        call post
.ret:   ret

; post: WM_COMMNOTIFY(cid, CX) to the port's window
post:
        call cidof
        push word [si + P.hwnd]
        push word WM_COMMNOTIFY
        push ax
        push word 0
        push cx
        call far [postmsg]
        ret

; event: CX = events -> those in the event mask into the event word;
; a bit newly there is noted for CN_EVENT (CM1: the word holds only
; events in the mask)
event:
        push ax
        push bx
        mov ax, [si + P.evtmask]
        and ax, cx
        jz .r
        mov bx, [si + P.evt]
        not bx
        and bx, ax
        jz .o
        mov byte [si + P.evnew], 1
.o:     or [si + P.evt], ax
.r:     pop bx
        pop ax
        ret

; cidof: SI = block -> AX = cid
cidof:
        mov ax, si
        sub ax, ports
        xor dx, dx
        push bx
        mov bx, P.size
        div bx
        pop bx
        cmp ax, NCOM
        jb .r
        add ax, 80h - NCOM
.r:     ret

; ---- 2: int setcom(DCB FAR *) ----
global SETCOM
SETCOM:
        ENTER
        les di, [bp + 6]
        mov al, [es:di + D_ID]
        xor ah, ah
        mov bx, ax
        call port
        mov ax, IE_BADID
        jc .ret
        cmp bl, 80h
        jae .lpt
        call validate
        jc .ret
        call copydcb
        ; a closed port that was open before is set up all the same, its
        ; interrupts on (CM1: so did Windows 3.1's driver)
        cmp word [si + P.base], 0
        je .ok
        cli
        call holds
        sti
        call program
        cli
        call kicktx
        sti
.ok:    xor ax, ax
        jmp .ret
.lpt:   call copydcb
        xor ax, ax
.ret:   LEAVE 4

; ---- 3: int setque(cid, qdb FAR *) ----
global SETQUE
SETQUE:
        ENTER
        mov ax, [bp + 10]
        call port
        mov ax, 8000h
        jc .ret
        les di, [bp + 6]
        cli
        mov ax, [es:di]
        mov [si + P.rxq], ax
        mov ax, [es:di + 2]
        mov [si + P.rxq + 2], ax
        mov ax, [es:di + 4]
        mov [si + P.rxsize], ax
        mov ax, [es:di + 6]
        mov [si + P.txq], ax
        mov ax, [es:di + 8]
        mov [si + P.txq + 2], ax
        mov ax, [es:di + 10]
        mov [si + P.txsize], ax
        xor ax, ax
        mov [si + P.rxcnt], ax
        mov [si + P.rxin], ax
        mov [si + P.rxout], ax
        mov [si + P.txcnt], ax
        mov [si + P.txin], ax
        mov [si + P.txout], ax
        sti
.ret:   LEAVE 6

; getrx: SI = port -> AL = the next received byte, CF when none
getrx:
        cli
        cmp word [si + P.rxcnt], 0
        je .none
        les di, [si + P.rxq]
        mov bx, [si + P.rxout]
        mov al, [es:di + bx]
        inc bx
        cmp bx, [si + P.rxsize]
        jb .w
        xor bx, bx
.w:     mov [si + P.rxout], bx
        dec word [si + P.rxcnt]
        push ax
        call rxtaken
        pop ax
        sti
        clc
        ret
.none:  sti
        stc
        ret

; ---- 4: int reccom(cid): the byte, -2 for none, -1 for an error ----
global RECCOM
RECCOM:
        ENTER
        mov ax, [bp + 6]
        call port
        mov ax, -1
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        call getrx
        mov ah, 0
        jnc .ret
        call nodata
.ret:   LEAVE 2

; nodata: SI = an open port with an empty receive queue -> AX = what
; reccom returns (CM1): after the EOF character outside binary mode the
; EOF character again, else -1 with an error in the error word, else -2
nodata:
        test byte [si + P.stat], CS_EOF
        jz .err
        test word [si + P.dcb + D_FLAGS], F_BINARY
        jnz .err
        mov al, [si + P.dcb + D_EOFCHAR]
        xor ah, ah
        ret
.err:   mov ax, -1
        cmp word [si + P.err], 0
        jne .r
        dec ax
.r:     ret

; ---- 20: int ReadCommString(cid, LPSTR, n): bytes read ----
global READCOMMSTRING
READCOMMSTRING:
        ENTER
        mov ax, [bp + 12]
        call port
        mov ax, 0
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        mov cx, [bp + 6]
        xor dx, dx
.loop:  jcxz .done
        push cx
        push dx
        call getrx
        jnc .got
        call nodata                     ; at the end of the input: the EOF
        test ah, ah                     ; character for every byte asked
        jz .got                         ; for (CM1)
        pop dx
        pop cx
        jmp .done
.got:   pop dx
        pop cx
        push es
        les di, [bp + 8]
        add di, dx
        mov [es:di], al
        pop es
        inc dx
        dec cx
        jmp .loop
.done:  mov ax, dx
.ret:   LEAVE 8

; linewait: SI = an open COM port, before a byte is queued. With
; CtsTimeout, DsrTimeout or RlsTimeout not 0, the line must be on, or come
; on within that many ms (FFFFh: however long it takes); else CE_CTSTO,
; CE_DSRTO or CE_RLSDTO and CF, and the byte is not queued. Observed (CM1,
; tests/commhost.py's timing): with CTS off, CtsTimeout FFFFh held the
; write until CTS came, 200 dropped the bytes with CE_CTSTO; the status
; showed no CTS hold either way.
linewait:
        push bx
        push cx
        push dx
        push di
        lea di, [si + P.dcb + D_CTSTO]
        mov bl, MSR_CTS
        mov cx, CE_CTSTO
        call .one
        jc .ret
        lea di, [si + P.dcb + D_DSRTO]
        mov bl, MSR_DSR
        mov cx, CE_DSRTO
        call .one
        jc .ret
        lea di, [si + P.dcb + D_RLSTO]
        mov bl, MSR_DCD
        mov cx, CE_RLSDTO
        call .one
.ret:   pop di
        pop dx
        pop cx
        pop bx
        ret
; .one: DI = the timeout's address, BL = the MSR bit, CX = the error
.one:   cmp word [di], 0
        je .ok
        test [si + P.msr], bl
        jnz .ok
        push bp
        push cx
        call getms
        pop cx
        mov bp, sp
        push ax                         ; the start: [bp - 2], [bp - 4]
        push dx
.wait:  test [si + P.msr], bl
        jnz .on
        cmp word [di], -1
        je .wait
        push cx
        call getms
        pop cx
        sub ax, [bp - 2]
        sbb dx, [bp - 4]
        jnz .late
        cmp ax, [di]
        jb .wait
.late:  mov sp, bp
        pop bp
        or [si + P.err], cx
        stc
        ret
.on:    mov sp, bp
        pop bp
.ok:    clc
        ret

; putq: SI = port, AL = a byte for the transmit queue; CF when full
putq:
        cli
        mov cx, [si + P.txcnt]
        cmp cx, [si + P.txsize]
        jae .full
        les di, [si + P.txq]
        add di, [si + P.txin]
        mov [es:di], al
        mov ax, [si + P.txin]
        inc ax
        cmp ax, [si + P.txsize]
        jb .w
        xor ax, ax
.w:     mov [si + P.txin], ax
        inc word [si + P.txcnt]
        mov ax, [si + P.txcnt]
        cmp ax, [si + P.outtrig]
        jb .k
        or byte [si + P.notify], NS_TXARM
.k:     call kicktx
        sti
        clc
        ret
.full:  or word [si + P.err], CE_TXFULL
        sti
        stc
        ret

; ---- 5: int sndcom(cid, ch): 0, else an error ----
global SNDCOM
SNDCOM:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 8000h
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        cmp word [bp + 8], 80h
        jae .lpt
        call linewait
        jc .fail
        mov al, [bp + 6]
        call putq
        mov ax, 0
        jnc .ret
.fail:  mov ax, [si + P.err]            ; the error word: CE_TXFULL for a
        jmp .ret                        ; full queue (CM1), CE_CTSTO ...
.lpt:   mov al, [bp + 6]
        call lptout
.ret:   LEAVE 4

; ---- 19: int CommWriteString(cid, LPSTR, n): bytes queued ----
global COMMWRITESTRING
COMMWRITESTRING:
        ENTER
        mov ax, [bp + 12]
        call port
        mov ax, 0
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        xor dx, dx
.loop:  cmp dx, [bp + 6]
        jae .done
        push es
        les di, [bp + 8]
        add di, dx
        mov al, [es:di]
        pop es
        push dx
        cmp word [bp + 12], 80h
        jae .lpt
        push ax
        call linewait
        pop ax
        jc .r
        call putq
        jmp .r
.lpt:   call lptout
        cmp ax, 1
        cmc
.r:     pop dx
        jc .done
        inc dx
        jmp .loop
.done:  mov ax, dx
.ret:   LEAVE 8

; lptout: SI = an LPT port, AL = a byte, printed through the port
; itself (Windows 3.1's driver reads the status port: CM1 in DOSBox 0.74,
; whose LPT1 status port reads 87h, gave CE_IOE | CE_DNS, which the BIOS
; does not report). AX = 0, or 8000h with the error bits set (and
; EV_PERR) and nothing printed.
lptout:
        push dx
        push bx
        mov bl, al
        call lptstat
        jcxz .print
        or [si + P.err], cx
        mov cx, EV_PERR
        call event
        mov ax, 8000h
        jmp .ret
.print: mov dx, [si + P.base]
        mov al, bl
        out dx, al                      ; the data
        add dx, 2
        in al, dx
        and al, ~1Fh
        or al, 0Ch                      ; not initialising, selected
        mov ah, al
        or al, 1                        ; strobe
        out dx, al
        mov al, ah
        out dx, al
        xor ax, ax
.ret:   pop bx
        pop dx
        ret

; lptstat: SI = an LPT port -> CX = the CE_ bits of its status port,
; waiting while it is busy: CE_IOE (error, bit 3 low), CE_DNS (not
; selected, bit 4 low), CE_OOP (paper out, bit 5), CE_PTO (still busy,
; bit 7 low, after LPT_TIMEOUT ms; UNVERIFIED: the time)
LPT_TIMEOUT     equ 1000
lptstat:
        push ax
        push bx
        push dx
        call getms
        mov bx, ax
.wait:  mov dx, [si + P.base]
        inc dx
        in al, dx
        xor cx, cx
        test al, 08h
        jnz .1
        or cx, CE_IOE
.1:     test al, 10h
        jnz .2
        or cx, CE_DNS
.2:     test al, 20h
        jz .3
        or cx, CE_OOP
.3:     jcxz .busy
        jmp .ret
.busy:  test al, 80h
        jnz .ret
        push cx
        call getms
        pop cx
        sub ax, bx
        cmp ax, LPT_TIMEOUT
        jb .wait
        or cx, CE_PTO
.ret:   pop dx
        pop bx
        pop ax
        ret

; ---- 6: int ctx(cid, ch) ----
global CTX
CTX:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 8000h
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        cmp word [bp + 8], 80h
        jae .lpt
        mov ax, 4000h
        test byte [si + P.stat], CS_TXIM
        jnz .ret
        cli
        mov al, [bp + 6]
        mov [si + P.txim], al
        or byte [si + P.stat], CS_TXIM
        call kicktx
        sti
        xor ax, ax
        jmp .ret
.lpt:   mov al, [bp + 6]
        call lptout
        test ax, ax
        jz .ret
        mov ax, 4000h
.ret:   LEAVE 4

; ---- 7: int trmcom(cid) ----
; The transmit queue is sent first (USER's CloseComm), then the UART
; stops interrupting and DTR and RTS drop.
global TRMCOM
TRMCOM:
        ENTER
        mov ax, [bp + 6]
        call port
        mov ax, 8000h
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ret
        cmp word [bp + 6], 80h
        jae .lpt
        call drain
        push ax
        ; closed as Windows 3.1's driver leaves it (CM1, commhost's
        ; record): no interrupts; the modem control 0, then as it was
        ; before the open without OUT2; the FIFO as it was before the open
        cli
        mov dx, [si + P.base]
        add dx, U_IER
        xor al, al
        out dx, al
        add dx, U_MCR - U_IER
        out dx, al
        mov al, [si + P.savemcr]
        and al, MCR_DTR | MCR_RTS
        out dx, al
        add dx, U_FCR - U_MCR
        xor al, al
        cmp byte [si + P.savefifo], 0C0h
        jne .fcr
        mov al, 1                       ; (trigger level: UNVERIFIED)
.fcr:   out dx, al
        mov byte [si + P.txon], 0
        mov byte [si + P.isopen], 0
        sti
        call unhook
        pop ax
        jmp .ret
.lpt:   mov byte [si + P.isopen], 0
        xor ax, ax
.ret:   LEAVE 2

; drain: SI = an open COM port: wait for the transmit queue to empty and
; the transmitter to finish. AX = 0, or -2 when the queue did not move
; for TX_TIMEOUT ms.
TX_TIMEOUT      equ 10000
drain:
        call getms
        mov bx, ax
        mov cx, dx
        mov di, [si + P.txcnt]
.wait:  mov ax, [si + P.txcnt]
        test ax, ax
        jz .shift
        cmp ax, di
        je .same
        mov di, ax
        push cx
        push bx
        call getms
        pop bx
        pop cx
        mov bx, ax
        mov cx, dx
        jmp .wait
.same:  push cx
        push bx
        call getms
        pop bx
        pop cx
        sub ax, bx
        sbb dx, cx
        jnz .tout
        cmp ax, TX_TIMEOUT
        jb .wait
.tout:  mov ax, -2
        ret
.shift: mov dx, [si + P.base]
        add dx, U_LSR
.t:     in al, dx
        test al, LSR_TEMT
        jz .t
        xor ax, ax
        ret

; ---- 8: int stacom(cid, COMSTAT FAR *): the error word (cleared) ----
global STACOM
STACOM:
        ENTER
        mov ax, [bp + 10]
        call port
        mov ax, 8000h
        jc .ret
        les di, [bp + 6]
        mov ax, es
        or ax, di
        jz .noc
        cli
        mov al, [si + P.stat]
        mov [es:di], al
        mov ax, [si + P.rxcnt]
        mov [es:di + 1], ax
        mov ax, [si + P.txcnt]
        mov [es:di + 3], ax
        sti
.noc:   xor ax, ax
        cli
        xchg ax, [si + P.err]
        sti
.ret:   LEAVE 6

; ---- 9: DWORD cextfcn(cid, fcn) ----
global CEXTFCN
CEXTFCN:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 8000h
        mov dx, [bp + 6]
        jc .ret
        mov bx, [bp + 6]
        xor ax, ax
        xor dx, dx
        cmp word [bp + 8], 80h
        jae .lpt
        cmp bx, GETMAXLPT
        je .maxlpt
        cmp bx, GETMAXCOM
        je .maxcom
        cmp bx, GETBASEIRQ
        je .baseirq
        cmp bx, GETBASEIRQ + 1          ; (11 answers as 10: CM1)
        je .baseirq
        cmp byte [si + P.isopen], 0     ; (Windows 3.1's driver answers
        je .ok                          ; 0 for a closed port: CM1)
        cli
        cmp bx, SETXOFF
        je .xoff
        cmp bx, SETXON
        je .xon
        cmp bx, SETRTS
        je .rts
        cmp bx, CLRRTS
        je .crts
        cmp bx, SETDTR
        je .dtr
        cmp bx, CLRDTR
        je .cdtr
        sti                             ; RESETDEV on a COM port, and
        jmp .ok                         ; codes it does not know: 0 (CM1:
                                        ; 0 and 12; the DDK says negative)
.xoff:  or byte [si + P.stat], CS_XOFFHOLD
        jmp .done
.xon:   and byte [si + P.stat], ~CS_XOFFHOLD
        call kicktx
        jmp .done
.rts:   mov al, MCR_RTS
        jmp .set
.dtr:   mov al, MCR_DTR
.set:   or [si + P.mcr], al
        jmp .mcr
.crts:  mov al, ~MCR_RTS
        jmp .clr
.cdtr:  mov al, ~MCR_DTR
.clr:   and [si + P.mcr], al
.mcr:   mov al, [si + P.mcr]
        mov dx, [si + P.base]
        add dx, U_MCR
        out dx, al
.done:  sti
.ok:    xor ax, ax
        xor dx, dx
        jmp .ret
        ; a parallel port: RESETDEV initialises the printer (the INIT
        ; line); AX the error bits of its status. Every other code gives
        ; 0 for a parallel port (CM1: GETMAXLPT and GETMAXCOM too).
.lptf:  cmp bx, RESETDEV
        jne .ok
        mov dx, [si + P.base]           ; the INIT line low a moment
        add dx, 2
        mov al, 08h
        out dx, al
        mov cx, 100
.dly:   in al, dx
        loop .dly
        mov al, 0Ch
        out dx, al
        call lptstat
        or [si + P.err], cx             ; (CM1: 0C00h, in the error word
        mov ax, cx                      ; too)
        xor dx, dx
        jmp .ret
.lpt:   cmp byte [si + P.isopen], 0
        jne .lptf
        jmp .ret
.maxlpt:
        mov ax, 80h + NLPT - 1
        jmp .ret
.maxcom:
        mov ax, NCOM - 1
        jmp .ret
.baseirq:
        xor ax, ax                      ; LPT ports: 0 (CM1)
        xor dx, dx
        cmp word [bp + 8], 80h
        jae .ret
        mov ax, [bp + 8]
        call port
        mov ax, [bp + 8]
        call findport
        mov ax, -1
        mov dx, -1
        jc .ret
        mov ax, [si + P.base]
        mov dl, [si + P.irq]
        xor dh, dh
.ret:   LEAVE 4

; ---- 10: int cflush(cid, queue): 0 transmit, 1 receive ----
global CFLUSH
CFLUSH:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 8000h
        jc .ret
        cli
        cmp word [bp + 6], 0
        jne .rx
        xor ax, ax
        mov [si + P.txcnt], ax
        mov [si + P.txin], ax
        mov [si + P.txout], ax
        jmp .ok
.rx:    cmp word [bp + 6], 1
        jne .ok
        xor ax, ax
        mov [si + P.rxcnt], ax
        mov [si + P.rxin], ax
        mov [si + P.rxout], ax
        call rxtaken
.ok:    sti
        mov ax, [si + P.err]            ; the error word, as it is (CM1,
.ret:   LEAVE 4                         ; the DDK)

; ---- 11: WORD FAR *cevt(cid, mask) ----
global CEVT
CEVT:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 0
        mov dx, 0
        jc .ret
        cmp word [bp + 8], 80h          ; a parallel port: none (CM1)
        jae .ret
        mov ax, [bp + 6]
        cli
        mov [si + P.evtmask], ax
        and [si + P.evt], ax            ; (events outside the new mask go:
        sti                             ; CM1)
        lea ax, [si + P.evt]
        mov dx, ds
.ret:   LEAVE 4

; ---- 12: WORD cevtGet(cid, mask): the event word; mask's bits cleared ----
global CEVTGET
CEVTGET:
        ENTER
        mov ax, [bp + 8]
        call port
        mov ax, 0
        jc .ret
        mov cx, [bp + 6]
        not cx
        cli
        mov ax, [si + P.evt]
        and [si + P.evt], cx
        sti
.ret:   LEAVE 4

; ---- 13/14: int csetbrk(cid), cclrbrk(cid) ----
global CSETBRK
CSETBRK:
        ENTER
        mov al, 1
        call brk
        LEAVE 2
global CCLRBRK
CCLRBRK:
        ENTER
        mov al, 0
        call brk                        ; (what was queued meanwhile goes
        LEAVE 2                         ; with the next write: CM1)

brk:
        mov cl, al
        mov ax, [bp + 6]
        call port
        mov ax, 8000h
        jc .ret
        cmp byte [si + P.isopen], 0
        je .ok
        cmp word [bp + 6], 80h
        jae .ok
        mov [si + P.brk], cl
        cli
        mov al, [si + P.lcr]
        and al, ~40h
        test cl, cl
        jz .w
        or al, 40h
.w:     mov [si + P.lcr], al
        mov dx, [si + P.base]
        add dx, U_LCR
        out dx, al
        sti
.ok:    mov ax, [si + P.err]            ; the error word, as it is (CM1)
.ret:   ret

; ---- 15: DCB FAR *getdcb(cid) ----
global GETDCB
GETDCB:
        ENTER
        mov ax, [bp + 6]
        call port
        mov ax, 0
        mov dx, 0
        jc .ret
        lea ax, [si + P.dcb]
        mov dx, ds
.ret:   LEAVE 2

; ---- 100: BOOL EnableNotification(cid, hwnd, in, out) ----
global ENABLENOTIFICATION
ENABLENOTIFICATION:
        ENTER
        mov ax, [bp + 12]
        call port
        mov ax, 0                       ; (open or not: CM1)
        jc .ret
        cmp word [postmsg + 2], 0
        jne .have
        push ds
        push word usermod
        call far GETMODULEHANDLE
        push ax
        push ds
        push word postname
        call far GETPROCADDRESS
        mov [postmsg], ax
        mov [postmsg + 2], dx
.have:  cli
        mov ax, [bp + 10]
        mov [si + P.hwnd], ax
        mov ax, [bp + 8]
        mov [si + P.intrig], ax
        mov ax, [bp + 6]
        mov [si + P.outtrig], ax
        mov byte [si + P.notify], 0
        mov byte [si + P.evnew], 0
        sti
        mov ax, 1
.ret:   LEAVE 8

; ---- 17: void SuspendOpenCommPorts(void) ----
; WINOLDAP's call before it runs a DOS program. In Standard mode Windows
; 3.1's driver leaves the ports as they are: the UART kept its
; interrupts, and bytes that came meanwhile were in the queue afterwards
; (CM1). So does this one.
global SUSPENDOPENCOMMPORTS
SUSPENDOPENCOMMPORTS:
        retf

; ---- 18: void ReactivateOpenCommPorts(void) ----
; After the DOS program (which may have used the port itself): each
; open port is set up again from its DCB.
global REACTIVATEOPENCOMMPORTS
REACTIVATEOPENCOMMPORTS:
        ENTER
        mov si, ports
        mov cx, NCOM
.next:  cmp byte [si + P.isopen], 0
        je .skip
        push cx
        cli
        call holds
        sti
        mov byte [si + P.fresh], 1
        call program
        cli
        call kicktx
        sti
        pop cx
.skip:  add si, P.size
        loop .next
        LEAVE 0

; ---- 16: int WEP(int) ----
global WEP
WEP:
        mov ax, 1
        retf 2

; ---- 101: no name in Windows 3.1's driver ----
global COMMORD101
COMMORD101:
        xor ax, ax
        retf
