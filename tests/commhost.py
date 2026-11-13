#!/usr/bin/env python3
"""commhost - the far end of an emulated serial port, for the COMM.DRV
probes (tests/commprobe.sh, tests/apps/cm1.c; drivers/comm/README.md).

DOSBox's and DOSBox-X's "nullmodem" serial device connects to a TCP
socket (serial1=nullmodem server:127.0.0.1 port:N). commhost listens on
that port and is the other end of the cable: it records everything the
guest sends, and does what the guest asks of it in band.

The link (DOSBox's nullmodem, not transparent; observed with a DOS
program driving the UART directly, build notes in drivers/comm/README.md):
data bytes go as they are, FFh doubled; FFh followed by another byte is
the sender's line state: bit 0 RTS, bit 1 DTR, bit 2 break. The guest's
RTS arrives here as such, and what this end sends as RTS and DTR becomes
the guest's CTS and DSR (a null-modem cable). Carrier detect and ring
cannot be signalled this way.

Commands, in the data the guest sends: "#" letter [argument] ";".
They are carried out one after the other, in a worker thread, so
reception goes on meanwhile.
  #P<n>,<a>;  send n bytes a, a+1, ... (mod 256; a decimal, default 0)
  #X<hex>;    send these bytes (hex digits, two a byte)
  #C<n>;      set this end's line state to n (bit 0 the guest's CTS,
              bit 1 its DSR); state 3 at the start
  #B<ms>;     a break of ms milliseconds (default 200)
  #W<ms>;     wait ms milliseconds before the next command
  #M<text>;   a mark in the log
  #Q;         end: close the connection and exit
The log (-l FILE) has one line per run of data bytes (D <hex>, in the
order received; a run ends at a line change, a command or a mark),
"L rts dtr brk" for each change of the guest's lines, and "#..." for
each command as received.

-T FILE: also every chunk received ("<") and sent (">", the link's own
encoding) with the seconds since the start, for reading timing.

usage: commhost.py [-l LOG] [-T FILE] [-t SECS] PORT
"""
import argparse
import socket
import sys
import threading
import time
import queue


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port', type=int)
    ap.add_argument('-l', '--log')
    ap.add_argument('-t', '--timeout', type=float, default=600)
    ap.add_argument('-T', '--timed')
    a = ap.parse_args()
    log = open(a.log, 'w') if a.log else sys.stdout
    tlf = open(a.timed, 'w') if a.timed else None
    t0 = time.time()

    def tlog(s):
        if tlf:
            tlf.write('%8.3f %s\n' % (time.time() - t0, s))
            tlf.flush()
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', a.port))
    srv.listen(1)
    srv.settimeout(a.timeout)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        print('commhost: nobody connected', file=sys.stderr)
        return 1
    conn.settimeout(None)
    lock = threading.Lock()
    state = {'out': 3}
    run = []                    # data bytes not yet logged

    def flush():
        if run:
            log.write('D ' + ' '.join('%02X' % b for b in run) + '\n')
            del run[:]

    def note(s):
        with lock:
            flush()
            log.write(s + '\n')
            log.flush()

    def send(b):
        with lock:
            conn.sendall(b)
            tlog('> ' + b.hex())

    def sendctl(n):
        send(bytes([0xFF, n & 0xFF]))

    def senddata(data):
        out = bytearray()
        for b in data:
            out.append(b)
            if b == 0xFF:
                out.append(0xFF)
        send(bytes(out))

    cmds = queue.Queue()

    def worker():
        while True:
            c = cmds.get()
            if c is None:
                return
            k, arg = c[0], c[1:]
            try:
                if k == 'P':
                    n, _, s = arg.partition(',')
                    s = int(s or 0)
                    senddata(bytes((s + i) & 0xFF for i in range(int(n))))
                elif k == 'X':
                    senddata(bytes.fromhex(arg))
                elif k == 'C':
                    state['out'] = int(arg) & 3
                    sendctl(state['out'])
                elif k == 'B':
                    sendctl(state['out'] | 4)
                    time.sleep(int(arg or 200) / 1000)
                    sendctl(state['out'])
                elif k == 'W':
                    time.sleep(int(arg) / 1000)
                elif k == 'Q':
                    conn.shutdown(socket.SHUT_RDWR)
                    return
            except (ValueError, OSError) as e:
                note('! %s: %s' % (c, e))

    threading.Thread(target=worker, daemon=True).start()
    sendctl(state['out'])
    lines = None
    esc = False
    cmd = None                  # a command being received
    t_end = time.time() + a.timeout
    conn.settimeout(1)
    while time.time() < t_end:
        try:
            d = conn.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not d:
            break
        tlog('< ' + d.hex())
        for b in d:
            if esc:
                esc = False
                if b != 0xFF:
                    st = (b & 1, (b >> 1) & 1, (b >> 2) & 1)
                    if st != lines:
                        lines = st
                        note('L rts=%d dtr=%d brk=%d' % st)
                    continue
            elif b == 0xFF:
                esc = True
                continue
            if cmd is not None:
                if b == ord(';'):
                    note('#' + cmd)
                    if cmd[:1] != 'M':
                        cmds.put(cmd)
                    if cmd == 'Q':
                        t_end = 0
                    cmd = None
                elif len(cmd) < 600:
                    cmd += chr(b)
                continue
            if b == ord('#'):
                cmd = ''
                continue
            with lock:
                run.append(b)
    with lock:
        flush()
    log.flush()
    cmds.put(None)
    return 0


if __name__ == '__main__':
    sys.exit(main())
