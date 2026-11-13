# w16 communications driver (`COMM.DRV`, module `COMM`)

w16's own serial and parallel port driver for Windows 3.1. Like every w16
driver it is a genuine Windows 3.1 driver (ARCHITECTURE.md 6.7): it
implements the communications-driver interface of the Windows 3.1 DDK and
nothing w16-specific, and replaces Windows 3.1's `SYSTEM\COMM.DRV` as it
is (`SYSTEM.INI` `[boot] comm.drv=comm.drv`, as Setup leaves it).

`comm.asm` (NASM) and `comm.lnk` (wlink); `make` builds
`build/dos/COMM.DRV` (one fixed code segment, one fixed data segment,
expected Windows version 3.10) and copies it into the install tree.
8250, 16450 and 16550(A) UARTs on COM1-COM4, LPT1-LPT3 through the BIOS.
Standard mode; 386 Enhanced mode is not verified (below).

Applications never call the driver: USER's OpenComm, ReadComm, WriteComm,
... do (DDK "About the Communications Driver"). Windows 3.1's USER imports
ordinals 1-15 and looks for 19, 20 and 100 at run time; WINOLDAP imports
17 and 18; nothing imports 16 and 101 (`tools/neimports.py --importers
COMM`). w16's own USER does not implement the comm functions yet (its
stubs return 0): everything here was measured and is tested with
Microsoft's USER.

## Interface

Module name, ordinals and residency are those of Windows 3.1's COMM.DRV
(its NE tables: `tools/neinfo.py`, `docs/exports/COMM.txt`). The
signatures are the DDK's ("Communication-Driver Functions"); the bytes
each entry takes off the stack were measured (CM1, part A: every entry
called through a thunk that compares SP before and after).

| Ord | Name | Pops | What it does here |
|---|---|---|---|
| 1 | `inicom(DCB FAR *)` | 4 | opens the port the DCB's Id names; 0 or an IE_ error |
| 2 | `setcom(DCB FAR *)` | 4 | the port set up from the DCB; 0 or an IE_ error |
| 3 | `setque(cid, qdb FAR *)` | 6 | the receive and transmit queues (USER's memory) |
| 4 | `reccom(cid)` | 2 | a received byte; -2 none, -1 none with an error pending |
| 5 | `sndcom(cid, ch)` | 4 | a byte queued; 0, else the error word |
| 6 | `ctx(cid, ch)` | 4 | a byte sent next (TransmitCommChar); 0, 4000h busy |
| 7 | `trmcom(cid)` | 2 | sends what is queued, closes; 0, -2 time-out |
| 8 | `stacom(cid, COMSTAT FAR *)` | 6 | the error word (then cleared), the status |
| 9 | `cextfcn(cid, fcn)` | 4 | the escapes (DWORD) |
| 10 | `cflush(cid, queue)` | 4 | empties the transmit (0) or receive (1) queue |
| 11 | `cevt(cid, mask)` | 4 | the event mask; returns the event word's address |
| 12 | `cevtGet(cid, mask)` | 4 | the event word, then the mask's bits cleared in it |
| 13 | `csetbrk(cid)` | 2 | break on |
| 14 | `cclrbrk(cid)` | 2 | break off |
| 15 | `getdcb(cid)` | 2 | the port's DCB's address |
| 16 | `WEP(int)` | 2 | returns 1 (resident name) |
| 17 | `SuspendOpenCommPorts()` | 0 | nothing (below) |
| 18 | `ReactivateOpenCommPorts()` | 0 | every open port set up again |
| 19 | `CommWriteString(cid, LPSTR, n)` | 8 | bytes queued; their count |
| 20 | `ReadCommString(cid, LPSTR, n)` | 8 | bytes read; their count |
| 100 | `EnableNotification(cid, hwnd, in, out)` | 8 | WM_COMMNOTIFY; TRUE |
| 101 | (no name, not flagged exported) | 0 | returns 0 |

A port id is 0-3 (COM1-COM4) or 80h-82h (LPT1-LPT3): Windows 3.1's driver
takes no others (inicom IE_BADID; GETMAXCOM 3, GETMAXLPT 82h), so neither
does this one: the bound is the interface's. For an id that is no port:
inicom and setcom -1 (IE_BADID), reccom -1, setque, sndcom, ctx, trmcom,
stacom, cextfcn, cflush, csetbrk and cclrbrk 8000h (cextfcn's DX the
code), cevt and getdcb a null pointer, cevtGet, CommWriteString,
ReadCommString and EnableNotification 0.

### Opening, the DCB, the UART

- **Where the port is** (DDK "Base Address and IRQ Selection"): the BIOS
  data area's address (0040:0000 for COM, 0040:0008 for LPT); for a COM
  port it has none of, `SYSTEM.INI [386Enh] COMnBase` (hexadecimal), else
  3E8h for COM3; the IRQ from `COMnIrq`, else 4, 3, 4, 3. CM1: in DOSBox
  (COM1 3F8h, COM2 2F8h, nothing for COM3 and COM4) COM3 opens at 3E8h,
  IRQ 4, with no UART there; COM4 is IE_HARDWARE (-10), as are LPT2 and
  LPT3 (GETBASEIRQ: 4/3F8h, 3/2F8h, 4/3E8h, -1/-1, LPT 0/0). There is no
  presence test.
- **Validation**, in this order (CM1, fields wrong two at a time): baud
  rate (IE_BAUDRATE -12), parity 0-4 (IE_DEFAULT -5), byte size 5-8
  (IE_BYTESIZE -11), stop bits 0-2, 1 (1.5) only with 5-bit bytes
  (IE_DEFAULT). setcom checks the same and changes nothing on an error.
  inicom on a port already open succeeds (USER's OpenComm is what returns
  IE_OPEN); CM1 does not do it, because with Windows 3.1's driver SETXON
  and trmcom did not return afterwards.
- **Baud rate**: the divisor is 115200 / BaudRate, truncated (110 gives
  1047, 134 gives 859); 0 and 1 are refused. With a high byte of FFh it
  is an index (DDK "Baud-Rate Indexes"): CBR_110 ... CBR_19200 (FF10h-
  FF18h), CBR_38400 (FF1Bh) and CBR_56000 (FF1Fh) are taken, the others
  (FF19h, FF1Ah, FF1Ch, CBR_128000, CBR_256000, FF28h ... FFFFh) refused;
  FF00h-FF0Fh give divisor 1. All computed as 115200 / rate except
  CBR_14400, which Windows 3.1's driver sets to divisor 9 (12800 baud):
  observed, and done the same. The DCB keeps BaudRate as given.
- **Line control**: byte size - 5, stop bit 04h when StopBits is 2 (1.5
  stop bits with 5-bit bytes set nothing), parity 08h/18h/28h/38h for
  odd/even/mark/space whether fParity is set or not.
- **FIFO**: a 16550's FIFO is on (FCR C7h: 14-byte trigger, UNVERIFIED,
  the register cannot be read back) when the rate is 4800 or more and
  `[386Enh] COMnFIFO` is not 0 (DDK "16550a UART FIFO Buffer"); off
  otherwise (CM1: off at 3600 and below and for FF00h/FF0Fh, on from
  4800 and CBR_4800).
- **Modem control**: DTR and RTS on unless fDtrDisable/fRtsDisable; OUT2
  on. Interrupts: receive, line status, modem status (IER 0Dh); transmit
  while there is something to send.
- **The DCB** is kept exactly as given (dummy bits too); getdcb returns
  its address in the driver's data segment, also for a port not open.
- **Closing** (trmcom) waits for the queue to go and the transmitter to
  empty (at 2 baud that takes Windows 3.1's driver 5.5 s too), then: IER
  0, the FIFO off, OUT2 off, DTR and RTS left as they were. A queue that
  does not move for 10 s gives -2 (UNVERIFIED: the time).

### Data, flow control, errors

- **Transmit**: one byte per transmit interrupt, FIFO or not (CM1: right
  after a 300-byte write Windows 3.1's driver had 298 left). XON/XOFF go
  first; ctx's byte next, then the queue; ctx's byte waits for an XOFF
  hold like the queue (status 48h until released; a second ctx 4000h).
- **XON/XOFF out** (fOutX): XOFF received holds the output (status 08h),
  XON releases it; neither is stored. SETXOFF/SETXON (escapes 1, 2) do
  the same. **CTS/DSR out** (fOutxCtsFlow, fOutxDsrFlow): the output
  waits while the line is off (status 01h, 02h).
- **CtsTimeout, DsrTimeout, RlsTimeout** (CM1 with commhost's timing):
  when not 0, a byte is queued only if the line is on or comes on within
  that many ms; FFFFh waits however long it takes (the write itself
  waits), any other value drops the byte with CE_CTSTO/CE_DSRTO/CE_RLSDTO.
  No hold shows in the status.
- **Input flow** (fInX, fRtsflow, fDtrflow): when the receive queue
  holds size - XoffLim bytes, XOFF goes out (status 10h) and/or RTS and
  DTR drop; when it is down to XonLim, XON and the lines again.
- **Receive**: fNull drops NULs; fPeChar replaces bytes with parity
  errors by PeChar; XON/XOFF with fOutX are not stored; a full queue
  drops the byte with CE_RXOVER (the first `size` bytes stay). Outside
  binary mode the EofChar ends the input: not stored, status 20h, and
  from then on every byte is refused as CE_RXOVER until the port is
  opened again (setcom, cflush do not end it), and with the queue empty
  reccom returns the EofChar, ReadCommString fills the buffer with it
  (Windows 3.1's driver does all that; ReadComm then never returns 0).
- **Errors**: overrun CE_OVERRUN, parity CE_RXPARITY, framing CE_FRAME
  (each also EV_ERR), break CE_BREAK and EV_BREAK (its NUL is stored);
  a full transmit queue CE_TXFULL (sndcom returns the error word, 0100h;
  CommWriteString the count it took). stacom returns and clears the
  word; cflush, csetbrk and cclrbrk return it as it is.
- **Break**: csetbrk sets LCR bit 6 and holds the output; cclrbrk clears
  it, and what was queued meanwhile goes with the next write (so did
  Windows 3.1's driver).

### Events and notification

- The event word holds only events in the mask (cevt). EV_RXCHAR for
  every byte stored; EV_RXFLAG for EvtChar (fChEvt or not); EV_TXEMPTY
  when the queue's last byte goes; EV_CTS, EV_DSR, EV_RLSD on a change,
  EV_RING with the ring line on, EV_RingTe on its trailing edge; EV_CTSS,
  EV_DSRS, EV_RLSDS the lines' states at each change (CM1: CTS off and
  DSR on gave 080Ch with EV_TXEMPTY, both on 0C1Ch); EV_ERR, EV_BREAK
  above; EV_PERR for printer errors.
- **WM_COMMNOTIFY** (EnableNotification; wParam the id, lParam the
  code, one message per code; checked from a SYSTEM.DRV timer every 100
  ms, `CreateSystemTimer` as Windows 3.1's driver imports it):
  CN_EVENT when an event newly enters the event word (nothing for the
  events already there when notification was enabled); CN_TRANSMIT when
  the transmit queue, having reached the out trigger, falls below it
  (nothing on enabling with an empty queue); CN_RECEIVE when the receive
  queue reaches the in trigger (again only after it was below it), or
  below the trigger once for bytes that came followed by 100 ms of
  silence (UNVERIFIED: the time). A trigger of -1 means never.
  EnableNotification answers TRUE for a port that is not open too.
  USER's PostMessage is found when notification is first enabled.

### Escapes (cextfcn)

COM: SETXOFF 1, SETXON 2, SETRTS 3, CLRRTS 4, SETDTR 5, CLRDTR 6: 0;
RESETDEV 7: 0, nothing done; GETMAXLPT 8: 82h; GETMAXCOM 9: 3;
GETBASEIRQ 10 (and 11, which answers the same on 3.1): DX the IRQ, AX
the base, -1/-1 for no port; anything else 0 (the DDK says negative; CM1
saw 0 for 0 and 12). A closed port gets 0 and nothing done (Windows
3.1's driver acts on it and leaves AX as it was: not compared). LPT: 0
for every code except RESETDEV (INT 17h AH=1; AX the error bits).

### Parallel ports

Output byte by byte through the BIOS (INT 17h AH=0); its status gives
CE_PTO (time-out), CE_IOE (I/O error), CE_OOP (paper out), CE_DNS (not
selected), EV_PERR and an error result (sndcom 8000h, ctx 4000h,
CommWriteString the count printed). Nothing is queued. CM1 in DOSBox
0.74 (an LPT1 address in the BIOS data area, no printer): Windows 3.1's
driver gave CE_IOE | CE_DNS (0C00h) for a write, 4000h for
TransmitCommChar, ReadComm 0, SetCommBreak and FlushComm the error word,
as this one does. UNVERIFIED: whether Windows 3.1's driver uses the BIOS
or the ports, and its behaviour with a working printer (DOSBox-X's
`parallel1=file`, `tests/commprobe.sh -x`, below).

### Leaving Windows for a DOS program

WINOLDAP calls SuspendOpenCommPorts before a DOS program and
ReactivateOpenCommPorts after (DDK). In Standard mode Windows 3.1's
driver changed nothing at Suspend: the UART kept its interrupts and
bytes that came meanwhile were in the queue after Reactivate (CM1,
part C12). This driver does nothing at Suspend either, and at Reactivate
sets each open port up again from its DCB (in case the DOS program
changed it).

## How it was observed (CM1)

`tests/commprobe.sh` runs CM1 (`tests/apps/cm1.c`) as the shell of
Windows 3.1 (Microsoft's KRNL286, DOSX, USER) in headless DOSBox, once
with Windows 3.1's COMM.DRV and once with w16's, and compares the logs.

- **The serial link, headless**: DOSBox's `nullmodem` serial device
  connects to a TCP port (`serial1=nullmodem server:127.0.0.1 port:N`,
  given through `tests/dropin.sh`'s `DROPIN_CONF`). Its far end is
  `tests/commhost.py`, a TCP server on the host: it records every byte
  and every line change the guest makes, and on in-band commands from
  CM1 ("#P200,5;" send 200 bytes, "#X13;" send XOFF, "#C2;" set the lines
  CM1 sees as CTS/DSR, "#B300;" a break, "#W1500;" wait) sends data,
  changes the lines and breaks, so data flows both ways, flow control is
  exercised from both sides and the host's record of what came down the
  wire is compared too. The nullmodem protocol (not transparent): data
  bytes as they are, FFh doubled, FFh then a state byte (bit 0 RTS, bit 1
  DTR, bit 2 break); found by driving the UART from a DOS program. COM2
  is a second nullmodem port with a recording-only commhost. Carrier
  detect and ring cannot be signalled this way.
- **Logged**: only what the driver returns (AX, DX, the bytes popped),
  what the interface defines behind its pointers (the DCB, the COMSTAT,
  the event word, data read), the UART's registers read by CM1 itself
  (IER, LCR, MCR, the divisor, the FIFO bits of IIR), and the far end's
  record. Never other memory behind a pointer: getdcb's pointer for a
  parallel port does not point at a DCB on Windows 3.1, so only its Id
  is logged.
- **Part A**: the entries, the bytes they pop, the answers for ids that
  are no port. **B**: COM2 directly: every baud rate, byte size, parity,
  stop bits, flag, id, the DCB and UART after each; setcom; the escapes;
  break; close. **C**: COM1 through USER with commhost: data both ways,
  UngetCommChar, events, XON/XOFF both ways, CTS/DSR handshaking and
  time-outs, RTS/DTR by escape and DCB, break, input flow thresholds
  byte by byte, overflow, fNull, EOF, WM_COMMNOTIFY, CommWriteString /
  ReadCommString / sndcom / ctx / reccom directly, Suspend/Reactivate,
  closing with output held. **D**: LPT1. **E**: fParity, setcom on a
  closed port, ordinal 101.
- Things Windows 3.1's driver did that CM1 avoids (and this driver does
  not copy): trmcom on a port not open did not return; after inicom on an
  open port, SETXON and trmcom did not return; without cevt's event word
  first, SETXON did not return (UNVERIFIED which of these was the
  cause); inicom and setcom take 220 ms each (4 timer ticks: purpose
  unknown; this driver does not wait).

## Verified

- `tests/commprobe.sh`: CM1's log (OBS lines but the "~" ones: timing,
  addresses) and commhost's records for COM1 and COM2 identical with w16's
  driver and Windows 3.1's.
- `make boot-test` case `comm`: CM1's part C (the exchange through USER)
  with w16's COMM.DRV against Windows 3.1's.
- The install tree (`build/w16/SYSTEM/COMM.DRV`).

## Not verified / not done

- 386 Enhanced mode: there VCD virtualises the ports for DOS boxes, and
  COMM.DRV talks to it (`[386Enh] COMnBuffer`, COMBUFF.386); not run.
- Carrier detect and ring (EV_RLSD, EV_RING, EV_RingTe, RlsTimeout with
  RLSD on, CS_RLSDHOLD): DOSBox's nullmodem cannot signal them; parity,
  framing and overrun errors other than a break's.
- The FIFO trigger level; the receive time-out of CN_RECEIVE; the order
  of CN_ codes posted in the same 100 ms; trmcom's time-out.
- Parallel ports with a printer that works; RESETDEV; whether 3.1's
  driver uses INT 17h.
- `[386Enh] COMnBase`, `COMnIrq` and `COMnFIFO` (read, not exercised).
- More than one port on an IRQ at once (handled: every open port of the
  IRQ is serviced until none has anything pending).
