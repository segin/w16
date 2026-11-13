/*
 * cm1.c - CM1: the communications driver (COMM.DRV) observed
 * (drivers/comm/README.md). Run as the shell by tests/commprobe.sh, once
 * with Windows 3.1's COMM.DRV and once with w16's, on Microsoft's KERNEL,
 * DOSX and USER, and compare the TK.LOGs and the far end's logs.
 *
 * COM1 is a DOSBox "nullmodem" port whose other end is tests/commhost.py
 * (it records what arrives and carries out "#...;" commands CM1 sends:
 * send bytes, set the lines CM1 sees as CTS and DSR, a break, a wait).
 * COM2 is a nullmodem port too, its commhost only recording; COM3 and
 * COM4 are missing. LPT1 is whatever the emulator gives.
 *
 * Logged (only what the driver returns and what the UART's registers
 * show; never memory behind pointers other than the DCB, COMSTAT, queue
 * and event word the interface defines):
 *   A  every entry point, how many bytes each takes off the stack and
 *      what it returns for a port that does not exist;
 *   B  inicom's validation on COM2: one DCB field changed at a time, the
 *      result, the DCB getdcb gives back, the UART (line control,
 *      divisor, interrupt enable, modem control, FIFO); setcom the same;
 *      the errors of closed and open ports; cextfcn's escapes;
 *   C  COM1 through USER (OpenComm, WriteComm, ...): data both ways,
 *      the events and the event word, XON/XOFF both ways, CTS and DSR
 *      handshaking, RTS and DTR set by the escapes and as receive flow
 *      control, break, TransmitCommChar, flushing, overflow,
 *      WM_COMMNOTIFY, ReadCommString/CommWriteString, Suspend/
 *      ReactivateOpenCommPorts, closing with output held;
 *   D  LPT1.
 */
#include <windows.h>
#include <stdarg.h>

#define TK_REF "cm1ref.h"
#include "tklog.h"

extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern WORD __cdecl get_0040h(void);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static WORD args[16];
static WORD res[2];
static FARPROC drv[102];
static HWND hwnd;
static char line[160];

/* port I/O (Standard mode lets an application do it) */
BYTE inb(WORD port);
#pragma aux inb = "in al, dx" parm [dx] value [al];
void outb(WORD port, BYTE v);
#pragma aux outb = "out dx, al" parm [dx] [al];
void cli(void);
#pragma aux cli = "cli";
void sti(void);
#pragma aux sti = "sti";

/* call COMM ordinal o with n word arguments (Pascal order: as written) */
static DWORD dc(int o, int n, ...)
{
    va_list ap;
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
    va_start(ap, n);
    for (i = 16 - n; i < 16; i++)
        args[i] = va_arg(ap, WORD);
    va_end(ap);
    {
        DWORD t = GetTickCount(), r = md_call(drv[o], args, res);
        t = GetTickCount() - t;
        if (t >= 200) {     /* slow calls are noted (timing: "~") */
            wsprintf(line, "OBS ~slow ordinal %d: %u ms\r\n", o, (WORD)t);
            out(line);
        }
        return r;
    }
}

static void say(const char *s)
{
    out(s);
    wsprintf(line, "\r\nOBS ~t=%lu\r\n", GetTickCount());
    out(line);
}

static void obs2(const char *name, DWORD r)
{
    wsprintf(line, "OBS %s=%04X %04X\r\n", (LPSTR)name, HIWORD(r), LOWORD(r));
    out(line);
}

static void bdump(const char *name, const BYTE FAR *b, WORD n)
{
    WORD i;
    out("OBS ");
    out(name);
    out("=");
    for (i = 0; i < n; i++) {
        wsprintf(line, "%02X%s", b[i], (LPSTR)(i + 1 < n ? " " : ""));
        out(line);
    }
    out("\r\n");
}

static void dcbdump(const char *name, const DCB FAR *d)
{
    bdump(name, (const BYTE FAR *)d, sizeof(DCB));
}

/* the UART at base: interrupt enable, line control, modem control, the
   divisor; the FIFO bits of IIR only when no transmit interrupt is
   enabled (reading IIR clears a pending one) */
static void uart(const char *name, WORD base)
{
    BYTE ier, lcr, mcr, dll, dlm, iir = 0xEE;
    cli();
    ier = inb(base + 1);
    lcr = inb(base + 3);
    mcr = inb(base + 4);
    outb(base + 3, lcr | 0x80);
    dll = inb(base);
    dlm = inb(base + 1);
    outb(base + 3, lcr);
    if (!(ier & 2))
        iir = inb(base + 2) & 0xC0;
    sti();
    wsprintf(line, "OBS %s uart=ier %02X lcr %02X mcr %02X div %02X%02X fifo %02X\r\n",
             (LPSTR)name, ier, lcr, mcr, dlm, dll, iir);
    out(line);
}

static WORD pops(const char *name, DWORD r)
{
    wsprintf(line, "OBS %s=%04X popped %u\r\nOBS ~%s DX=%04X\r\n", (LPSTR)name, LOWORD(r),
             res[0], (LPSTR)name, HIWORD(r));
    out(line);
    return res[0];
}

static int nmsg;
static struct { WORD wp, lp; } msgs[64];

LRESULT CALLBACK __export CmProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMNOTIFY) {
        if (nmsg < 64) {
            msgs[nmsg].wp = wp;
            msgs[nmsg].lp = LOWORD(lp) | (HIWORD(lp) ? 0x8000 : 0);
        }
        nmsg++;
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD t = GetTickCount();
    MSG msg;
    while (GetTickCount() - t < ms) {
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&msg);
        Yield();
    }
}

/* the WM_COMMNOTIFY messages since the last call, consecutive equal ones
   counted once (how many come depends on timing) */
static void notes(const char *name)
{
    int i, n = nmsg < 64 ? nmsg : 64;
    out("OBS ");
    out(name);
    out(" notify=");
    for (i = 0; i < n; i++) {
        if (i && msgs[i].wp == msgs[i - 1].wp && msgs[i].lp == msgs[i - 1].lp)
            continue;
        wsprintf(line, "%s%X:%X", (LPSTR)(i ? " " : ""), msgs[i].wp, msgs[i].lp);
        out(line);
    }
    out("\r\n");
    nmsg = 0;
}

static int cid = -1;

static void stat(const char *name)
{
    COMSTAT cs;
    int e;
    cs.status = 0xEE;
    cs.cbInQue = cs.cbOutQue = 0xEEEE;
    e = GetCommError(cid, &cs);
    wsprintf(line, "OBS %s stat=err %04X st %02X in %u out %u\r\n", (LPSTR)name, e, cs.status,
             cs.cbInQue, cs.cbOutQue);
    out(line);
}

static void evts(const char *name)
{
    wsprintf(line, "OBS %s events=%04X\r\n", (LPSTR)name, GetCommEventMask(cid, 0xFFFF));
    out(line);
}

/* commands for commhost; each string starts with a wait, so that all of
   it has arrived before commhost does anything that holds CM1's output
   (an XOFF, CTS or DSR dropped in the middle of a command string held
   the rest of it) */
static void host(const char *cmd)
{
    WORD n = 0;
    while (cmd[n])
        n++;
    WriteComm(cid, "#W150;", 6);
    WriteComm(cid, cmd, n);
}

/* wait (pumping messages) until n bytes are in the receive queue or
   ms milliseconds have gone by */
static void waitin(WORD n, DWORD ms)
{
    DWORD t = GetTickCount();
    COMSTAT cs;
    MSG msg;
    while (GetTickCount() - t < ms) {
        GetCommError(cid, &cs);
        if (cs.cbInQue >= n)
            break;
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&msg);
        Yield();
    }
}

static void waitout(DWORD ms)
{
    DWORD t = GetTickCount();
    COMSTAT cs;
    while (GetTickCount() - t < ms) {
        GetCommError(cid, &cs);
        if (cs.cbOutQue == 0)
            break;
        Yield();
    }
}

static BYTE buf[2048];

/* read everything there is: count and a checksum */
static void readall(const char *name)
{
    int n, total = 0, calls = 0;
    WORD sum = 0, i;
    BYTE first = 0, last = 0;
    /* (at most 40 calls: after an EOF character, ReadComm on Windows 3.1
       went on returning data, see "calls") */
    while (calls++ < 40 && (n = ReadComm(cid, buf, 256)) > 0) {
        if (!total)
            first = buf[0];
        for (i = 0; i < n; i++)
            sum = sum * 31 + buf[i];
        last = buf[n - 1];
        total += n;
    }
    wsprintf(line, "OBS %s read=%d (last %d) sum %04X first %02X last %02X calls %d\r\n",
             (LPSTR)name, total, n, sum, first, last, calls);
    out(line);
}

/* data CM1 sends: never '#' (commhost would take it for a command) */
static WORD pattern(BYTE FAR *b, WORD n, BYTE seed)
{
    WORD i;
    for (i = 0; i < n; i++) {
        b[i] = (BYTE)(seed + i * 7);
        if (b[i] == '#')
            b[i] = '$';
    }
    return n;
}

static void base(DCB *d, BYTE id)
{
    WORD i;
    for (i = 0; i < sizeof *d; i++)
        ((BYTE *)d)[i] = 0;
    d->Id = id;
    d->BaudRate = 9600;
    d->ByteSize = 8;
    d->fBinary = 1;
    d->XonChar = 0x11;
    d->XoffChar = 0x13;
    d->XonLim = 10;
    d->XoffLim = 10;
    d->EofChar = 0x1A;
    d->EvtChar = 'E';
}

static WORD combase[4];

/* ---- A: entry points ---- */
static void part_a(void)
{
    static const WORD ords[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                19, 20, 100, 101, 0};
    HMODULE hm = GetModuleHandle("COMM");
    DCB d;
    BYTE qb[16];
    WORD i;
    obs("module", hm != 0);
    for (i = 0; ords[i]; i++) {
        drv[ords[i]] = GetProcAddress(hm, MAKEINTRESOURCE(ords[i]));
        wsprintf(line, "entry %u", ords[i]);
        obs(line, drv[ords[i]] != NULL);
    }
    obs("WEP by name", GetProcAddress(hm, "WEP") == drv[16]);
    for (i = 0; i < 4; i++) {
        combase[i] = *(WORD FAR *)MAKELP(get_0040h(), 2 * i);
        wsprintf(line, "OBS ~bda com%u=%04X lpt%u=%04X\r\n", i + 1, combase[i], i + 1,
                 *(WORD FAR *)MAKELP(get_0040h(), 8 + 2 * i));
        out(line);
    }
    /* a port that does not exist (COM10 and LPT5 in the id space) */
    base(&d, 9);
    pops("inicom(9)", dc(1, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d)));
    pops("setcom(9)", dc(2, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d)));
    for (i = 0; i < 16; i++)
        qb[i] = 0;
    pops("setque(9)", dc(3, 3, 9, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb)));
    pops("reccom(9)", dc(4, 1, 9));
    pops("sndcom(9)", dc(5, 2, 9, 'x'));
    pops("ctx(9)", dc(6, 2, 9, 'x'));
    pops("trmcom(9)", dc(7, 1, 9));
    pops("stacom(9)", dc(8, 3, 9, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb)));
    bdump("stacom(9) comstat", qb, 5);
    for (i = 0; i <= 12; i++) {
        char n[24];
        wsprintf(n, "cextfcn(9,%u)", i);
        pops(n, dc(9, 2, 9, i));
    }
    pops("cflush(9,0)", dc(10, 2, 9, 0));
    pops("cevt(9)", dc(11, 2, 9, 0));
    pops("cevtGet(9)", dc(12, 2, 9, 0));
    pops("csetbrk(9)", dc(13, 1, 9));
    pops("cclrbrk(9)", dc(14, 1, 9));
    pops("getdcb(9)", dc(15, 1, 9));
    pops("CommWriteString(9)", dc(19, 4, 9, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb), 3));
    pops("ReadCommString(9)", dc(20, 4, 9, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb), 3));
    pops("EnableNotification(9)", dc(100, 4, 9, 0, 0, 0));
    pops("SuspendOpenCommPorts", dc(17, 0) & 0);
    pops("ReactivateOpenCommPorts", dc(18, 0) & 0);
    /* LPT ids */
    pops("reccom(84)", dc(4, 1, 0x84));
    pops("stacom(84)", dc(8, 3, 0x84, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb)));
    pops("cextfcn(84,8)", dc(9, 2, 0x84, 8));
    pops("trmcom(84)", dc(7, 1, 0x84));
    pops("getdcb(84)", dc(15, 1, 0x84));
}

/* ---- B: COM2 (nothing attached) directly ---- */
static void try_dcb(const char *name, DCB *d)
{
    char n[48];
    DWORD r, g;
    WORD port = combase[1];
    r = dc(1, 2, SELECTOROF((LPVOID)d), OFFSETOF((LPVOID)d));
    wsprintf(n, "inicom %s", (LPSTR)name);
    obs(n, LOWORD(r));
    if ((int)LOWORD(r) < 0)
        return;
    g = dc(15, 1, 1);
    if (LOWORD(g) || HIWORD(g))
        dcbdump(n, (const DCB FAR *)MAKELP(HIWORD(g), LOWORD(g)));
    else
        say("OBS getdcb=0");
    if (port)
        uart(n, port);
    wsprintf(n, "trmcom %s", (LPSTR)name);
    obs(n, LOWORD(dc(7, 1, 1)));
}

static void part_b(void)
{
    static const WORD bauds[] = {0, 1, 2, 3, 50, 75, 110, 134, 150, 300, 600, 1200, 1800, 2400,
                                 3600, 4800, 7200, 9600, 14400, 19200, 28800, 38400, 56000,
                                 57600, 0xE100, 0xFEFF, 0xFF00, 0xFF0F, 0xFF10, 0xFF11, 0xFF12,
                                 0xFF13, 0xFF14, 0xFF15, 0xFF16, 0xFF17, 0xFF18, 0xFF19, 0xFF1A,
                                 0xFF1B, 0xFF1C, 0xFF1F, 0xFF23, 0xFF27, 0xFF28, 0xFFFF, 0xFFFE};
    static const BYTE ids[] = {0, 2, 3, 4, 5, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0xFF};
    DCB d, d2;
    char n[48];
    BYTE qb[16];
    WORD i;
    DWORD r, g;
    WORD port = combase[1];
    say("B: COM2");
    if (port)
        uart("COM2 before", port);
    for (i = 0; i < sizeof bauds / sizeof bauds[0]; i++) {
        base(&d, 1);
        d.BaudRate = bauds[i];
        wsprintf(n, "baud %04X", bauds[i]);
        try_dcb(n, &d);
    }
    for (i = 0; i <= 10; i++) {
        base(&d, 1);
        d.ByteSize = (BYTE)i;
        wsprintf(n, "bytesize %u", i);
        try_dcb(n, &d);
    }
    for (i = 0; i <= 6; i++) {
        base(&d, 1);
        d.Parity = (BYTE)i;
        wsprintf(n, "parity %u", i);
        try_dcb(n, &d);
    }
    for (i = 0; i <= 4; i++) {
        base(&d, 1);
        d.StopBits = (BYTE)i;
        wsprintf(n, "stopbits %u", i);
        try_dcb(n, &d);
        d.ByteSize = 5;
        wsprintf(n, "stopbits %u bytesize 5", i);
        try_dcb(n, &d);
    }
    base(&d, 1);
    d.ByteSize = 3;
    d.BaudRate = 1;
    try_dcb("bytesize 3 baud 1", &d);
    base(&d, 1);
    d.Parity = 7;
    d.ByteSize = 9;
    try_dcb("parity 7 bytesize 9", &d);
    base(&d, 1);
    d.Parity = 7;
    d.BaudRate = 1;
    try_dcb("parity 7 baud 1", &d);
    base(&d, 1);
    d.StopBits = 3;
    d.ByteSize = 9;
    try_dcb("stopbits 3 bytesize 9", &d);
    base(&d, 1);
    d.StopBits = 3;
    d.BaudRate = 1;
    try_dcb("stopbits 3 baud 1", &d);
    base(&d, 1);
    d.ByteSize = 4;
    d.StopBits = 1;
    try_dcb("stopbits 1 bytesize 4", &d);
    base(&d, 1);
    d.ByteSize = 6;
    d.StopBits = 1;
    try_dcb("stopbits 1 bytesize 6", &d);
    base(&d, 1);
    d.fRtsDisable = 1;
    d.fDtrDisable = 1;
    try_dcb("rts dtr disabled", &d);
    base(&d, 1);
    d.fDtrDisable = 1;
    try_dcb("dtr disabled", &d);
    base(&d, 1);
    d.fRtsflow = 1;
    d.fDtrflow = 1;
    d.fOutX = 1;
    d.fInX = 1;
    d.fOutxCtsFlow = 1;
    d.fOutxDsrFlow = 1;
    d.fNull = 1;
    d.fPeChar = 1;
    d.fChEvt = 1;
    d.CtsTimeout = 0xFFFF;
    d.DsrTimeout = 1000;
    d.RlsTimeout = 2000;
    d.TxDelay = 5;
    d.fDummy = 3;
    d.fDummy2 = 1;
    try_dcb("all flags", &d);
    for (i = 0; i < sizeof ids; i++) {
        base(&d, ids[i]);
        wsprintf(n, "id %02X", ids[i]);
        r = dc(1, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d));
        obs(n, LOWORD(r));
        if (!LOWORD(r)) {
            wsprintf(n, "id %02X trmcom", ids[i]);
            obs(n, LOWORD(dc(7, 1, ids[i])));
        }
    }

    /* closed and open */
    base(&d, 1);
    /* trmcom of a closed port: not called (with Windows 3.1's driver it
       did not return) */
    obs2("closed getdcb", dc(15, 1, 1) & 0xFFFF0000UL ? 1 : 0);
    obs("closed reccom", LOWORD(dc(4, 1, 1)));
    obs("closed cevtGet", LOWORD(dc(12, 2, 1, 0xFFFF)));
    obs2("closed cevt", dc(11, 2, 1, 1) & 0xFFFF0000UL ? 1 : 0);
    for (i = 0; i <= 12; i++) {
        wsprintf(n, "closed cextfcn %u", i);
        obs2(n, dc(9, 2, 1, i));
    }
    obs("closed EnableNotification", LOWORD(dc(100, 4, 1, 0, 0, 0)));

    for (i = 0; i < 6; i++) {
        static const WORD c[] = {0, 1, 2, 3, 0x80, 0x81};
        wsprintf(n, "GETBASEIRQ %X", c[i]);
        obs2(n, dc(9, 2, c[i], 10));
    }
    /* COM3: no UART */
    base(&d, 2);
    obs("COM3 inicom", LOWORD(dc(1, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d))));
    dc(7, 1, 2);
    base(&d, 1);
    obs("open", LOWORD(dc(1, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d))));
    obs("open again", LOWORD(dc(1, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d))));
    /* the event word first, as USER's OpenComm (without it SETXON did not
       return with Windows 3.1's driver) */
    obs("open cevt", dc(11, 2, 1, 0) != 0);
    /* setcom on the open port, one change at a time */
    for (i = 0; i < 8; i++) {
        base(&d2, 1);
        switch (i) {
        case 0: d2.BaudRate = 1200; break;
        case 1: d2.BaudRate = 0xFF18; break;
        case 2: d2.BaudRate = 1; break;
        case 3: d2.ByteSize = 9; break;
        case 4: d2.ByteSize = 7; d2.Parity = 2; d2.StopBits = 2; break;
        case 5: d2.Parity = 9; break;
        case 6: d2.Id = 0; break;
        case 7: d2.fRtsDisable = 1; d2.fDtrDisable = 1; break;
        }
        wsprintf(n, "setcom %u", i);
        obs(n, LOWORD(dc(2, 2, SELECTOROF((LPVOID)&d2), OFFSETOF((LPVOID)&d2))));
        g = dc(15, 1, 1);
        dcbdump(n, (const DCB FAR *)MAKELP(HIWORD(g), LOWORD(g)));
        if (port)
            uart(n, port);
    }
    base(&d2, 1);
    dc(2, 2, SELECTOROF((LPVOID)&d2), OFFSETOF((LPVOID)&d2));
    /* without queues */
    r = dc(8, 3, 1, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb));
    obs("noqueue stacom", LOWORD(r));
    bdump("noqueue comstat", qb, 5);
    obs("noqueue reccom", LOWORD(dc(4, 1, 1)));
    /* (sndcom without queues: not called; on Windows 3.1 it returned
       CE_TXFULL, and SETXON and trmcom did not return afterwards) */
    /* queues (with none, SETXON did not return with Windows 3.1's
       driver) */
    {
        WORD qsel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 512);
        WORD q[6];
        q[0] = 0;
        q[1] = qsel;
        q[2] = 256;
        q[3] = 256;
        q[4] = qsel;
        q[5] = 256;
        obs("setque", LOWORD(dc(3, 3, 1, SELECTOROF((LPVOID)q), OFFSETOF((LPVOID)q))));
    }
    r = dc(8, 3, 1, SELECTOROF((LPVOID)qb), OFFSETOF((LPVOID)qb));
    obs("queue stacom", LOWORD(r));
    bdump("queue comstat", qb, 5);
    for (i = 0; i <= 12; i++) {
        /* RESETDEV; SETXOFF and SETXON did not return with Windows 3.1's
           driver here (on COM2 opened directly; through USER on COM1 they
           did, part C) */
        if (i == 7 || i == 1 || i == 2)
            continue;
        wsprintf(n, "open cextfcn %u", i);
        obs2(n, dc(9, 2, 1, i));
        if (port && i >= 3 && i <= 6)
            uart(n, port);
    }
    obs("open csetbrk", LOWORD(dc(13, 1, 1)));
    if (port)
        uart("csetbrk", port);
    obs("open cclrbrk", LOWORD(dc(14, 1, 1)));
    if (port)
        uart("cclrbrk", port);
    r = dc(11, 2, 1, 0x3FFF);
    obs("cevt nonzero", r != 0);
    g = dc(11, 2, 1, 0);
    obs("cevt same pointer", r == g);
    obs("cevtGet", LOWORD(dc(12, 2, 1, 0xFFFF)));
    /* RTS and DTR on again (with them off, trmcom did not return with
       Windows 3.1's driver) */
    dc(9, 2, 1, SETRTS);
    dc(9, 2, 1, SETDTR);
    obs("trmcom", LOWORD(dc(7, 1, 1)));
    if (port)
        uart("COM2 closed", port);
}

/* ---- C: COM1 through USER, commhost at the other end ---- */
static void setstate(DCB *d)
{
    int r = SetCommState(d);
    if (r)
        obs("SetCommState", r);
}

static void part_c(void)
{
    DCB d;
    char n[48];
    WORD i;
    int r;
    UINT FAR *ev;
    DWORD t;
    WORD port = combase[0];

    say("C: COM1");
    obs("OpenComm COM3", OpenComm("COM3", 1024, 1024));
    obs("OpenComm COM5", OpenComm("COM5", 1024, 1024));
    cid = OpenComm("COM1", 1024, 1024);
    obs("OpenComm COM1", cid);
    if (cid < 0)
        return;
    obs("OpenComm COM1 again", OpenComm("COM1", 1024, 1024));
    GetCommState(cid, &d);
    dcbdump("COM1 from WIN.INI", &d);
    if (port)
        uart("COM1 open", port);
    stat("open");
    base(&d, (BYTE)cid);
    setstate(&d);
    if (port)
        uart("COM1 set", port);
    ev = SetCommEventMask(cid, 0x3FFF);
    obs("event word", ev != 0);
    obs("event word = cevt", ev == (UINT FAR *)dc(11, 2, cid, 0x3FFF));
    pump(300);
    evts("start");

    /* 1: data both ways */
    say("C1 data");
    host("#M1;#W300;#P200,5;");
    waitin(200, 4000);
    pump(200);
    stat("C1 in");
    evts("C1");
    readall("C1");
    stat("C1 after read");
    r = WriteComm(cid, buf, pattern(buf, 300, 1));
    obs("C1 WriteComm", r);
    stat("C1 written");
    waitout(4000);
    pump(300);
    stat("C1 sent");
    evts("C1 sent");
    /* UngetCommChar */
    obs("UngetCommChar", UngetCommChar(cid, 'u'));
    obs("UngetCommChar again", UngetCommChar(cid, 'v'));
    stat("unget");
    readall("unget");

    /* 2: events and the lines */
    say("C2 events");
    host("#M2;#X45;");
    pump(500);
    evts("evtchar");
    host("#C2;");
    pump(500);
    evts("cts off");
    stat("cts off");
    host("#C0;");
    pump(500);
    evts("dsr off");
    stat("dsr off");
    host("#C3;");
    pump(500);
    evts("both on");
    host("#B300;");
    pump(800);
    evts("break");
    stat("break");
    readall("break");
    stat("break read");
    evts("break read");
    d.fChEvt = 1;
    setstate(&d);
    host("#X4541;");
    pump(500);
    evts("evtchar fChEvt");
    readall("evtchar fChEvt");
    d.fChEvt = 0;
    setstate(&d);

    /* 3: XON/XOFF on output */
    say("C3 xon/xoff out");
    d.fOutX = 1;
    setstate(&d);
    host("#M3;#X13;#W1500;#X11;");
    pump(400);
    stat("xoff in");
    WriteComm(cid, buf, pattern(buf, 50, 3));
    pump(500);
    stat("held");
    pump(1500);
    waitout(3000);
    stat("released");
    host("#X13;");
    pump(400);
    obs("ctx held", TransmitCommChar(cid, 'T'));
    obs("ctx again", TransmitCommChar(cid, 'U'));
    stat("ctx held");
    obs2("EscapeCommFunction SETXON", EscapeCommFunction(cid, SETXON));
    pump(500);
    stat("after SETXON");
    obs2("EscapeCommFunction SETXOFF", EscapeCommFunction(cid, SETXOFF));
    WriteComm(cid, buf, pattern(buf, 20, 5));
    pump(500);
    stat("after SETXOFF");
    obs("FlushComm tx", FlushComm(cid, 0));
    stat("flushed tx");
    obs2("EscapeCommFunction SETXON 2", EscapeCommFunction(cid, SETXON));
    host("#X11;");
    pump(500);
    stat("xon");
    readall("C3");
    d.fOutX = 0;
    setstate(&d);

    /* 4: CTS and DSR handshaking on output */
    say("C4 cts/dsr");
    d.fOutxCtsFlow = 1;
    setstate(&d);
    host("#M4;#C2;#W1500;#C3;");
    pump(400);
    WriteComm(cid, buf, pattern(buf, 40, 7));
    pump(500);
    stat("cts held");
    pump(1500);
    waitout(3000);
    stat("cts released");
    d.fOutxCtsFlow = 0;
    d.CtsTimeout = 0xFFFF;
    setstate(&d);
    host("#C2;#W1500;#C3;");
    pump(400);
    WriteComm(cid, buf, pattern(buf, 40, 9));
    pump(500);
    stat("ctstimeout held");
    pump(1500);
    waitout(3000);
    stat("ctstimeout released");
    d.CtsTimeout = 0;
    d.fOutxDsrFlow = 1;
    setstate(&d);
    host("#C1;#W1500;#C3;");
    pump(400);
    WriteComm(cid, buf, pattern(buf, 40, 11));
    pump(500);
    stat("dsr held");
    pump(1500);
    waitout(3000);
    stat("dsr released");
    d.fOutxDsrFlow = 0;
    d.CtsTimeout = 200;
    setstate(&d);
    host("#C2;#W1500;#C3;");
    pump(400);
    WriteComm(cid, buf, pattern(buf, 40, 13));
    pump(800);
    stat("ctstimeout 200");
    pump(1200);
    waitout(3000);
    stat("ctstimeout 200 after");
    d.CtsTimeout = 0;
    setstate(&d);
    readall("C4");

    /* 5: RTS and DTR by escape and by the DCB */
    say("C5 rts/dtr");
    host("#M5;");
    for (i = 3; i <= 6; i++) {
        wsprintf(n, "escape %u", i);
        obs2(n, EscapeCommFunction(cid, i));
        pump(300);
        if (port)
            uart(n, port);
    }
    EscapeCommFunction(cid, SETRTS);
    EscapeCommFunction(cid, SETDTR);
    d.fRtsDisable = 1;
    setstate(&d);
    pump(300);
    if (port)
        uart("fRtsDisable", port);
    d.fRtsDisable = 0;
    d.fDtrDisable = 1;
    setstate(&d);
    pump(300);
    if (port)
        uart("fDtrDisable", port);
    d.fDtrDisable = 0;
    setstate(&d);
    pump(300);
    if (port)
        uart("both enabled", port);
    host("#M5b;");
    pump(300);

    /* 6: SetCommBreak */
    say("C6 break");
    obs("SetCommBreak", SetCommBreak(cid));
    WriteComm(cid, "abc", 3);
    pump(400);
    stat("in break");
    obs("ClearCommBreak", ClearCommBreak(cid));
    waitout(2000);
    pump(300);
    stat("break cleared");
    host("#M6;");

    /* 7: input flow control, a 1024-byte queue */
    say("C7 input flow");
    d.fInX = 1;
    d.XonLim = 100;
    d.XoffLim = 200;
    setstate(&d);
    /* where XOFF goes: queue 1024, XoffLim 200 */
    host("#M7;#P822,0;");
    waitin(822, 4000);
    pump(300);
    stat("C7 822");
    for (i = 823; i <= 826; i++) {
        host("#P1,0;");
        pump(400);
        wsprintf(n, "C7 %u", i);
        stat(n);
    }
    host("#P74,0;");
    waitin(900, 4000);
    pump(500);
    stat("C7 full");
    for (i = 0; i < 7; i++) {
        r = ReadComm(cid, buf, 100);
        wsprintf(n, "C7 read %u", i);
        obs(n, r);
        pump(200);
        stat(n);
    }
    /* where XON goes: XonLim 100 */
    ReadComm(cid, buf, 98);
    pump(300);
    stat("C7 102");
    for (i = 0; i < 4; i++) {
        r = ReadComm(cid, buf, 1);
        wsprintf(n, "C7 read one %u", i);
        pump(300);
        stat(n);
    }
    readall("C7 rest");
    host("#M7b;");
    d.fInX = 0;
    d.fRtsflow = 1;
    d.fDtrflow = 1;
    setstate(&d);
    host("#P900,0;");
    waitin(900, 4000);
    pump(500);
    stat("C7 rts full");
    if (port)
        uart("C7 rts full", port);
    for (i = 0; i < 9; i++) {
        r = ReadComm(cid, buf, 100);
        pump(200);
        wsprintf(n, "C7 rts read %u", i);
        stat(n);
        if (port)
            uart(n, port);
    }
    d.fRtsflow = 0;
    d.fDtrflow = 0;
    setstate(&d);
    host("#M7c;");

    /* 8: overflow */
    say("C8 overflow");
    host("#M8;#P1100,0;");
    waitin(1024, 4000);
    pump(1000);
    stat("C8 over");
    evts("C8");
    stat("C8 again");
    readall("C8");
    host("#P20,0;");
    pump(500);
    obs("FlushComm rx", FlushComm(cid, 1));
    stat("C8 flushed");
    obs("FlushComm 2", FlushComm(cid, 2));

    /* 9: null stripping, parity replacement, EOF */
    say("C9 fNull/fBinary");
    d.fNull = 1;
    setstate(&d);
    host("#M9;#X410042000043;");
    pump(500);
    readall("fNull");
    d.fNull = 0;
    d.fBinary = 0;
    setstate(&d);
    host("#X41421A4344;");
    pump(500);
    stat("eof");
    /* the driver directly (ReadComm did not return here on Windows 3.1) */
    for (i = 0; i < 4; i++) {
        wsprintf(n, "eof reccom %u", i);
        obs(n, LOWORD(dc(4, 1, cid)));
    }
    r = (int)LOWORD(dc(20, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 20));
    obs("eof ReadCommString", r);
    stat("eof read");
    d.fBinary = 1;
    setstate(&d);
    stat("binary set");
    host("#X4546;");
    pump(500);
    stat("binary again");
    for (i = 0; i < 4; i++) {
        wsprintf(n, "binary reccom %u", i);
        obs(n, LOWORD(dc(4, 1, cid)));
    }
    readall("binary again");
    obs("FlushComm rx eof", FlushComm(cid, 1));
    stat("eof flushed");
    host("#X4748;");
    pump(500);
    stat("eof flushed rx");
    readall("eof flushed");
    /* (after the EOF character nothing more is taken until the port is
       opened again, on Windows 3.1) */
    obs("eof CloseComm", CloseComm(cid));
    cid = OpenComm("COM1", 1024, 1024);
    obs("eof reopen", cid);
    base(&d, (BYTE)cid);
    setstate(&d);
    SetCommEventMask(cid, 0x3FFF);
    stat("eof reopened");
    host("#X4950;");
    pump(500);
    readall("eof reopened");

    /* 10: WM_COMMNOTIFY */
    say("C10 notify");
    nmsg = 0;
    obs("EnableCommNotification", EnableCommNotification(cid, hwnd, 50, 20));
    pump(500);
    notes("enabled");
    host("#M10;#P10,0;");
    pump(1000);
    notes("10 bytes");
    stat("10 bytes");
    host("#P60,0;");
    pump(1000);
    notes("60 more");
    readall("C10");
    pump(500);
    notes("read");
    WriteComm(cid, buf, pattern(buf, 300, 2));
    pump(1500);
    notes("written");
    SetCommEventMask(cid, EV_RXFLAG);
    host("#X45;");
    pump(800);
    notes("rxflag");
    evts("rxflag");
    pump(500);
    notes("rxflag got");
    readall("rxflag");
    SetCommEventMask(cid, 0x3FFF);
    obs("EnableCommNotification -1", EnableCommNotification(cid, hwnd, -1, -1));
    host("#P10,0;");
    pump(1000);
    notes("-1 triggers");
    readall("-1 triggers");
    obs("EnableCommNotification off", EnableCommNotification(cid, 0, 50, 20));
    host("#P10,0;");
    pump(800);
    notes("off");
    readall("off");
    obs("EnableNotification direct", LOWORD(dc(100, 4, cid, hwnd, 1, 1)));
    host("#P3,0;");
    pump(800);
    notes("direct");
    dc(100, 4, cid, 0, 0, 0);
    readall("direct");

    /* 11: the direct string calls */
    say("C11 strings");
    host("#M11;#P30,0;");
    waitin(30, 2000);
    pump(200);
    r = (int)LOWORD(dc(20, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 20));
    obs("ReadCommString 20", r);
    bdump("ReadCommString data", buf, 20);
    r = (int)LOWORD(dc(20, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 20));
    obs("ReadCommString rest", r);
    r = (int)LOWORD(dc(20, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 20));
    obs("ReadCommString none", r);
    pattern(buf, 40, 17);
    r = (int)LOWORD(dc(19, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 40));
    obs("CommWriteString 40", r);
    pattern(buf, 1500, 19);
    r = (int)LOWORD(dc(19, 4, cid, SELECTOROF((LPVOID)buf), OFFSETOF((LPVOID)buf), 1500));
    obs("CommWriteString 1500", r);
    stat("after 1500");
    r = WriteComm(cid, buf, 1500);
    obs("WriteComm 1500", r);
    stat("WriteComm 1500");
    waitout(5000);
    pump(300);
    r = (int)LOWORD(dc(5, 2, cid, 'q'));
    obs("sndcom", r);
    r = (int)LOWORD(dc(6, 2, cid, 'r'));
    obs("ctx", r);
    pump(300);
    host("#X7A;");
    pump(300);
    r = (int)LOWORD(dc(4, 1, cid));
    obs("reccom", r);
    r = (int)LOWORD(dc(4, 1, cid));
    obs("reccom none", r);
    stat("reccom");
    host("#X7B;");
    pump(500);
    r = (int)LOWORD(dc(4, 1, cid));
    obs("reccom clean", r);
    r = (int)LOWORD(dc(4, 1, cid));
    obs("reccom clean none", r);

    /* 12: suspended across a DOS program */
    say("C12 suspend");
    host("#M12;");
    pump(300);
    dc(17, 0);
    if (port)
        uart("suspended", port);
    stat("suspended");
    host("#X3132;");
    pump(500);
    stat("suspended rx");
    dc(18, 0);
    if (port)
        uart("reactivated", port);
    pump(500);
    stat("reactivated");
    readall("reactivated");
    host("#X3334;");
    pump(500);
    readall("after reactivate");

    /* 13: closing with output held */
    say("C13 close");
    host("#M13;#X13;");
    pump(400);
    d.fOutX = 1;
    setstate(&d);
    WriteComm(cid, buf, pattern(buf, 10, 23));
    pump(200);
    t = GetTickCount();
    r = CloseComm(cid);
    t = GetTickCount() - t;
    obs("CloseComm held", r);
    obs("CloseComm held secs", ((WORD)t + 500) / 1000);
    if (port)
        uart("closed", port);
    cid = OpenComm("COM1", 1024, 1024);
    obs("reopen", cid);
    GetCommState(cid, &d);
    dcbdump("reopened", &d);
    base(&d, (BYTE)cid);
    setstate(&d);
    stat("reopened");
    host("#X11;");
    pump(500);
    stat("reopened xon");
    obs("CloseComm", CloseComm(cid));
    obs("CloseComm again", CloseComm(cid));
    cid = OpenComm("COM1", 1024, 1024);
    host("#M14;#Q;");
    pump(500);
    CloseComm(cid);
}

/* ---- D: LPT1 ---- */
static void part_d(void)
{
    DCB d;
    int r;
    say("D: LPT1");
    cid = OpenComm("LPT1", 0, 0);
    obs("OpenComm LPT1", cid);
    if (cid < 0)
        return;
    obs("OpenComm LPT1 again", OpenComm("LPT1", 0, 0));
    r = GetCommState(cid, &d);
    obs("GetCommState LPT1", r);
    dcbdump("LPT1 dcb", &d);
    stat("LPT1");
    r = WriteComm(cid, "Hello, LPT1\r\n", 13);
    obs("WriteComm LPT1", r);
    stat("LPT1 written");
    obs("TransmitCommChar LPT1", TransmitCommChar(cid, 'x'));
    stat("LPT1 ctx");
    obs2("LPT1 GETMAXLPT", EscapeCommFunction(cid, GETMAXLPT));
    obs2("LPT1 GETMAXCOM", EscapeCommFunction(cid, GETMAXCOM));
    obs2("LPT1 SETDTR", EscapeCommFunction(cid, SETDTR));
    obs2("LPT1 RESETDEV", EscapeCommFunction(cid, RESETDEV));
    obs("LPT1 ReadComm", ReadComm(cid, buf, 10));
    obs("LPT1 SetCommBreak", SetCommBreak(cid));
    obs("LPT1 FlushComm", FlushComm(cid, 0));
    obs("LPT1 SetCommEventMask", SetCommEventMask(cid, EV_PERR) != 0);
    obs("LPT1 GetCommEventMask", GetCommEventMask(cid, 0xFFFF));
    obs("LPT1 SetCommState", SetCommState(&d));
    obs("CloseComm LPT1", CloseComm(cid));
    cid = OpenComm("LPT2", 0, 0);
    obs("OpenComm LPT2", cid);
    if (cid >= 0)
        CloseComm(cid);
    cid = OpenComm("LPT4", 0, 0);
    obs("OpenComm LPT4", cid);
    if (cid >= 0)
        CloseComm(cid);
}

/* ---- E: fParity (last: with Windows 3.1's driver a run hung here) ---- */
static void part_e(void)
{
    DCB d;
    say("E: fParity");
    base(&d, 1);
    d.fParity = 1;
    d.Parity = 2;
    try_dcb("fParity even", &d);
    base(&d, 1);
    d.fParity = 1;
    try_dcb("fParity none", &d);
    /* setcom on a port that is not open (with Windows 3.1's driver the
       trmcom after it did not return) */
    base(&d, 1);
    obs("closed setcom", LOWORD(dc(2, 2, SELECTOROF((LPVOID)&d), OFFSETOF((LPVOID)&d))));
    if (combase[1])
        uart("closed setcom", combase[1]);
    /* (trmcom on a port that is not open did not return with Windows
       3.1's driver: not called) */
    /* ordinal 101, no name: what it takes off the stack */
    pops("ordinal 101", dc(101, 0));
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    WORD parts;
    WaitEvent(0);
    InitApp(hi);
    out("CM1 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = CmProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "CM1";
    RegisterClass(&wc);
    hwnd = CreateWindow("CM1", "CM1", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hi, NULL);
    /* [w16] cm1parts in SYSTEM.INI: which parts (bit 0 A ... bit 4 E;
       A always, it finds the entry points) */
    parts = GetPrivateProfileInt("w16", "cm1parts", 31, "SYSTEM.INI");
    part_a();
    if (parts & 2)
        part_b();
    if (parts & 4)
        part_c();
    if (parts & 8)
        part_d();
    if (parts & 16)
        part_e();
    out("CM1 done\r\n");
    return 0;
}
