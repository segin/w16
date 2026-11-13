# MD2 / MS3: the "second packet after Enable" of Windows 3.1's MOUSE.DRV

Question (drivers/mouse/README.md): why does the second PS/2 packet after
each `Enable` never reach the event procedure with Microsoft's MOUSE.DRV?

Method, black-box only (no Microsoft code read):
- `tests/dos/ms3c.asm` (MS3C.COM, a TSR loaded before WIN) logs INT 15h
  C2xxh, INT 74h, and every BIOS call of the C207h packet handler.
  It can also give the driver's handler packets from a table that MD2 writes
  into it, in place of the BIOS's packet. MS3 can give several packets in
  one BIOS callback, after a hold-off, or with a minimum gap. When it holds
  a packet back, the handler is not called at all.
- `tests/apps/md2c.c` (MD2C.EXE, the shell) calls the driver's `Enable` and
  `Disable` and records each event procedure call. Each record has
  AX/BX/CX/DX, the BIOS tick and MS3's sequence number at that moment, so
  every event is placed exactly between MS3's records. Each call is then
  passed on to USER's `Mouse_Event`.
- `tests/md2cprobe.sh -e N -o DIR -- <dropin options>` runs it. xdotool
  sends a steady stream of real moves into DOSBox, about 2 IRQ 12 a second,
  which only carry MS3's packets. `tests/md2cview.py LOG [-v]` prints each
  cycle as given packets against events.

Runs, in DOSBox 0.74 with Microsoft's KRNL286, USER and DOSX unless noted.
The logs (copies of TK.LOG) are in `tests/apps/md2logs/c/`:

| log | driver | DOS extender | experiment |
|---|---|---|---|
| ms-e3b.log | Microsoft's | Microsoft's | 3: pass-through (real xdotool packets) |
| ms-e1.log | Microsoft's | Microsoft's | 1: position, content, packets per IRQ, timing |
| ms-e2.log | Microsoft's | Microsoft's | 2: what resets it |
| msx-e1.log | Microsoft's | w16's (`-x`) | 1 |
| w16-e1.log | w16's (as of 024bfd8) | Microsoft's | 1 |

## Findings (Microsoft's driver)

1. **DOSBox delivers the packet.** Packet 2 is passed to the C207h handler
   and the handler returns. In ms-e3b, records #65-#69 are IRQ in, BIOS
   packet 18h/F6h, handler call, handler return, IRQ out. Then no event
   comes, so the packet is lost inside the driver (hypothesis b, a DOSBox
   artefact, is out). USER is not involved either: MD2's procedure comes
   before USER (hypothesis d is out).
2. **The event is sent after the IRQ, not from inside the C207h callback.**
   In steady state each event's MS3 sequence number falls just after the
   "INT 74h out" record of the same IRQ. The handler has returned and the
   BIOS's INT 74h has returned to the DOS extender before the driver calls
   the event procedure (ms-e1 cycle 1: packet at #62, event at q=65 after
   #64).
3. **At most one event per IRQ 12, carrying the last packet.** Earlier
   packets in the same IRQ are overwritten, not summed. With 4 packets per
   callback the events were 4 and 8; with 8 packets in one callback the
   only event was 8 (ms-e1 cycles 7 and 8).
4. **The second IRQ after Enable that brings a packet sends no event. Its
   packet is kept, not discarded.** It is reported at the end of the *next*
   IRQ 12, but only if that IRQ brings no newer packet. If it does, the
   newer packet replaces it, and that is the loss seen in a continuous
   stream. Evidence:
   - With 1 packet per IRQ (ms-e1 cycle 1): events 1,3,4,5,6,7,8. Packet 2,
     given at IRQ #65, produced nothing and was overwritten at #70.
   - With 3 s between packets (ms-e1 cycle 10), MS3 held back the IRQs in
     between, so the handler was not called during them. Packet 2, given at
     IRQ #678, produced no event at its own IRQ. It was reported at the next
     IRQ #683, which brought no packet (q=687). Nothing was lost: events
     1..8.
   - With 4 packets per IRQ (cycle 7): IRQ 1 reported 4 at once. IRQ 2
     (packets 5-8) reported nothing. IRQ 3, with no packet, reported 8.
   - So the count is of IRQs that bring a packet, not of handler calls:
     handler call 2 was in IRQ 1 in cycle 7, and IRQ 1 still reported.
     IRQs without a packet in between do not count (cycle 10, #658-#674).
5. **Content does not matter.** In slot 2, each of these was lost in the
   same way: pure X motion, Y motion, a left press, a right press, a zero
   packet, a repeat of packet 1, and a status byte without bit 3 (ms-e1
   cycles 1, 3, 4, 5, 6, 11, 12). The following packets were never
   affected, so the loss does not move to slot 3 (hypothesis H5 is out).
   Buttons are compared with the last *reported* state. A press that was
   lost and then released makes no event at all (cycle 3). A release lost
   in slot 2 comes out as SF_B1_UP with the next packet's motion (cycle 2:
   event 0005h with x=3).
6. **Timing does not matter.** The loss is the same with the first packet
   10 s after Enable (cycle 9) or 1 s after (ms-e2 cycle 6). Packet 2 was
   still held back when it came 3 s after packet 1 (cycle 10). Only its
   *loss* depends on whether another packet comes before the next IRQ.
   MD2 saw nesting 1, so there was no reentrancy (hypothesis c is out as
   the cause).
7. **Enable resets it; nothing else does** (ms-e2):
   - Arming MS3 again without Disable and Enable: no loss (cycle 2).
   - Enable, no packet, Disable, Enable: packet 2 lost (cycle 4).
   - Enable, one packet, Disable, Enable: packet 2 *after the second
     Enable* lost. The count does not carry over (cycle 5).
   - Enable twice without Disable: the second Enable made no INT 15h calls
     at all, and packet 2 was lost once (cycle 3).
   - With 16 packets, only packet 2 was lost (cycle 7).
8. **The DOS extender does not matter.** With w16's DOSX (msx-e1) every
   cycle gave the same events, with the same placement against the IRQs
   (cycles 7 and 10 checked with -v), as with Microsoft's DOSX.

Side observations (ms-e1, MS3 INT 15h records):
- Microsoft's `Disable` makes C200h BH=0 and then C202h BH=5 (sample rate
  code 5, which is 200 reports/s by RBIL's table). It makes no C207h 0:0.
  w16's `Disable` makes C200h and C207h instead (w16-e1 #90-#92).
- An event with no motion and a button change has SF_MOVEMENT clear (cycle
  2: AX=0002h, 0,0).

## w16's driver (w16-e1)

It drops the *second packet* by count. That matches Microsoft's driver
whenever there is one packet per IRQ (cycles 1-6, 9, 11, 12). It differs
in three cycles:
- cycle 7: w16 reports 1,3,4,5,6,7,8; Microsoft's reports 4,8.
- cycle 8: w16 reports 1,3..8; Microsoft's reports 8.
- cycle 10: w16 loses 2; Microsoft's reports it one IRQ late.
In DOSBox 0.74 the BIOS gives one packet per IRQ, so only cycle 10's case
(no packet in the IRQ after packet 2) can show this with real input.

## Not established

- *Why* the driver holds back the second IRQ's packet. The behaviour is
  measured, but the purpose (for example a check, during the first packets
  after Enable, of whether packets come through the 8042 directly or
  through the BIOS) is not observed and not documented in any local source.
- The 8042 path, real hardware and DOSBox-X: there, xdotool's motion does
  not reach the guest under the harness, and MS3 has no packet source of
  its own.
- 386 Enhanced mode.
