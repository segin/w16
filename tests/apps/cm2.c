/*
 * cm2.c - CM2: USER's communications functions (OpenComm ... FlushComm,
 * EnableCommNotification: USER.200-215, 245) observed from an application.
 * Run as the shell by tests/commusr.sh, on Windows 3.1's USER and on
 * w16's, normally with COMMREC (tests/apps/commrec.c) in front of the
 * communications driver, which records every driver call USER makes;
 * both logs are compared. COM1 and COM2 are DOSBox nullmodem ports with
 * tests/commhost.py at the far end (COM1's carries out "#...;" commands),
 * COM3 has no UART, COM4 is missing, LPT1 is the emulator's.
 *
 * Logged (OBS lines; "MARK" lines go to COMMREC's log as well, by an
 * escape nothing else uses, so the two logs can be read side by side):
 *   P  every entry called through a thunk: the bytes it takes off the
 *      stack, its result, for ports that are not open;
 *   N  OpenComm's names, queue sizes and results, and the DCB
 *      GetCommState gives for each port opened; a second open;
 *   W  what OpenComm takes from WIN.INI [ports];
 *   B  BuildCommDCB's parsing: result and DCB for many strings;
 *   S  SetCommState, GetCommError, the event mask, the escapes, break,
 *      TransmitCommChar, UngetCommChar, ReadComm and WriteComm at the
 *      queues' limits, FlushComm, EnableCommNotification, CloseComm, on
 *      COM1 with commhost.
 * Pointers the functions return are logged as "~" (layout).
 */
#include <windows.h>
#include <stdarg.h>

#define TK_REF "cm2ref.h"
#include "tklog.h"

extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static WORD args[16];
static WORD res[3];
static FARPROC user[256];
static HWND hwnd;
static char line[200];
static DCB dcb;
static COMSTAT cs;
static char buf[300];
static int notes;
static char notelog[200];

/* call USER ordinal o with n word arguments (Pascal order: as written);
   the bytes it popped in res[0] */
static DWORD uc(int o, int n, ...)
{
    va_list ap;
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
    va_start(ap, n);
    for (i = 16 - n; i < 16; i++)
        args[i] = va_arg(ap, WORD);
    va_end(ap);
    return md_call(user[o], args, res);
}

static void obsl(const char *name, DWORD v)
{
    wsprintf(line, "OBS %s=%04X %04X\r\n", (LPSTR)name, HIWORD(v), LOWORD(v));
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

/* a mark in both logs: COMMREC writes every driver call, this one too
   (escape 1234h on port 7Fh, which no driver has: Windows 3.1's driver
   answers 0 for an id that is no port) */
static void mark(const char *s)
{
    out("MARK ");
    out(s);
    out("\r\n");
    EscapeCommFunction(0x7F, 0x1234);
}

static void pump(DWORD ms)
{
    MSG m;
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms)
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMNOTIFY) {
        char b[24];
        notes++;
        wsprintf(b, "%d:%X;", wp, LOWORD(lp));
        if (lstrlen(notelog) + lstrlen(b) < (int)sizeof notelog)
            lstrcat(notelog, b);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

/* ---- P: the entries, the bytes they pop ---- */
/* the result (AX; DX too for EscapeCommFunction, whose result is a LONG)
   and the bytes popped; then a mark, so COMMREC's lines before it are
   this call's */
static void pops(const char *name, DWORD r)
{
    if (name[0] == 'E' && name[1] == 's')
        wsprintf(line, "OBS P %s=%04X %04X pops %u\r\n", (LPSTR)name, HIWORD(r), LOWORD(r), res[0]);
    else
        wsprintf(line, "OBS P %s=%04X pops %u\r\n", (LPSTR)name, LOWORD(r), res[0]);
    out(line);
    mark(name);
}

static void part_p(void)
{
    static char nm[] = "COM9";
    static char bs[] = "COM1:9600,n,8,1";
    LPSTR b = buf;
    WORD i;
    mark("P");
    for (i = 200; i <= 215; i++)
        user[i] = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(i));
    user[245] = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(245));
    pops("OpenComm COM9", uc(200, 4, SELECTOROF((LPSTR)nm), OFFSETOF((LPSTR)nm), 100, 100));
    for (i = 0; i < 3; i++) {
        WORD cid = i == 0 ? 9 : i == 1 ? 0xFFFF : 0x81;
        char t[40];
        wsprintf(t, "%X ", cid);
        out("OBS P cid ");
        out(t);
        out("\r\n");
        {
            BYTE FAR *p = (BYTE FAR *)&dcb;
            int k;
            for (k = 0; k < (int)sizeof dcb; k++)
                p[k] = 0xCC;
        }
        dcb.Id = (BYTE)cid;
        pops("SetCommState", uc(201, 2, SELECTOROF((LPVOID)&dcb), OFFSETOF((LPVOID)&dcb)));
        pops("GetCommState", uc(202, 3, cid, SELECTOROF((LPVOID)&dcb), OFFSETOF((LPVOID)&dcb)));
        pops("GetCommError", uc(203, 3, cid, SELECTOROF((LPVOID)&cs), OFFSETOF((LPVOID)&cs)));
        pops("GetCommError NULL", uc(203, 3, cid, 0, 0));
        pops("ReadComm", uc(204, 4, cid, SELECTOROF(b), OFFSETOF(b), 10));
        pops("WriteComm", uc(205, 4, cid, SELECTOROF(b), OFFSETOF(b), 10));
        pops("TransmitCommChar", uc(206, 2, cid, 'A'));
        pops("CloseComm", uc(207, 1, cid));
        {
            DWORD r = uc(208, 2, cid, EV_RXCHAR);
            wsprintf(line, "OBS P SetCommEventMask pops %u null %d\r\n", res[0], r == 0);
            out(line);
            mark("SetCommEventMask");
        }
        pops("GetCommEventMask", uc(209, 2, cid, 0xFFFF));
        pops("SetCommBreak", uc(210, 1, cid));
        pops("ClearCommBreak", uc(211, 1, cid));
        pops("UngetCommChar", uc(212, 2, cid, 'A'));
        pops("EscapeCommFunction", uc(214, 2, cid, GETMAXCOM));
        pops("FlushComm", uc(215, 2, cid, 0));
        pops("EnableCommNotification", uc(245, 4, cid, (WORD)hwnd, 10, 10));
    }
    pops("BuildCommDCB", uc(213, 4, SELECTOROF((LPSTR)bs), OFFSETOF((LPSTR)bs),
                            SELECTOROF((LPVOID)&dcb), OFFSETOF((LPVOID)&dcb)));
}

/* ---- N: OpenComm's names ---- */
static void one_open(LPCSTR name, UINT in, UINT outq)
{
    int cid;
    char t[80];
    wsprintf(t, "N open \"%s\" %u %u", name, in, outq);
    mark(t);
    cid = OpenComm(name, in, outq);
    obs(t, cid);
    if (cid >= 0) {
        int r;
        {
            BYTE FAR *p = (BYTE FAR *)&dcb;
            int k;
            for (k = 0; k < (int)sizeof dcb; k++)
                p[k] = 0xCC;
        }
        r = GetCommState(cid, &dcb);
        lstrcat(t, " GetCommState");
        obs(t, r);
        lstrcat(t, " DCB");
        /* (for a parallel port only the Id: Windows 3.1's USER gives
           inicom a DCB with only the Id set, the rest whatever is on its
           stack, and its driver keeps it) */
        bdump(t, (BYTE FAR *)&dcb, (cid & 0x80) ? 1 : sizeof dcb);
        wsprintf(t, "N close \"%s\"", name);
        obs(t, CloseComm(cid));
    }
}

static void part_n(void)
{
    static const char *names[] = {
        "COM1", "com1", "COM1:", "COM1 ", "COM1:9600", "COM2", "COM3", "COM4", "COM5",
        "COM0", "COM", "COM10", "COM11", "LPT1", "LPT1:", "lpt1", "LPT2", "LPT3", "LPT4",
        "LPT0", "AUX", "AUX:", "PRN", "CON", "NUL", "", "C", "COMX", " COM1", 0
    };
    int i, a, b;
    mark("N");
    for (i = 0; names[i]; i++)
        one_open(names[i], 1024, 128);
    one_open("COM1", 0, 0);
    one_open("COM1", 1, 1);
    one_open("COM1", 2, 0);
    one_open("COM1", 0, 2);
    one_open("COM1", 0x7FFF, 0x7FFF);
    one_open("COM1", 0x8000, 16);
    one_open("COM1", 0xFFFF, 16);
    one_open("COM1", 60000U, 60000U);
    one_open("LPT1", 0, 0);
    one_open("LPT1", 1000, 1000);
    /* twice, and both COM and LPT at once */
    mark("N twice");
    a = OpenComm("COM1", 64, 64);
    obs("N COM1 first", a);
    obs("N COM1 second", OpenComm("COM1", 64, 64));
    obs("N com1: second", OpenComm("com1:", 64, 64));
    b = OpenComm("COM2", 64, 64);
    obs("N COM2 with COM1", b);
    obs("N LPT1 with COM1", OpenComm("LPT1", 64, 64));
    obs("N close COM1", CloseComm(a));
    obs("N close COM1 again", CloseComm(a));
    obs("N close COM2", CloseComm(b));
    obs("N close LPT1", CloseComm(0x80));
    obs("N close LPT1 again", CloseComm(0x80));
}

/* ---- W: WIN.INI [ports] ---- */
static void part_w(void)
{
    static const char *vals[] = {
        "1200,e,7,2", "1200,e,7,2,x", "1200,e,7,2,p", "19200,n,8,1", "300,o,5,1.5",
        "9600,m,8,1", "9600,s,8,1", "110,n,8,1", "2400", "", "junk", 0
    };
    int i;
    char t[80];
    mark("W");
    GetProfileString("ports", "COM2:", "(none)", t, sizeof t);
    obs_str("W COM2: as set up", t);
    for (i = 0; vals[i]; i++) {
        WriteProfileString("ports", "COM2:", vals[i]);
        one_open("COM2", 256, 256);
    }
    /* the key without the colon, and none at all */
    WriteProfileString("ports", "COM2:", NULL);
    WriteProfileString("ports", "COM2", "4800,e,7,1");
    one_open("COM2", 256, 256);
    WriteProfileString("ports", "COM2", NULL);
    one_open("COM2", 256, 256);
    WriteProfileString("ports", "COM2:", "9600,n,8,1,x");
}

/* ---- B: BuildCommDCB ---- */
static void part_b(void)
{
    static const char *s[] = {
        "COM1:9600,n,8,1", "COM1:9600,N,8,1", "com2:1200,e,7,2", "COM1:110,o,5,1.5",
        "COM1:9600,n,8,1,x", "COM1:9600,n,8,1,p", "COM1:9600,n,8,1,X", "COM1:9600,n,8,1,z",
        "COM1: 9600, n, 8, 1", "COM1 9600,n,8,1", "COM1:96,n,8,1", "COM1:19,n,8,1",
        "COM1:12,n,8,1", "COM1:11,n,8,1", "COM1:30,n,8,1", "COM1:24,n,8,1", "COM1:48,n,8,1",
        "COM1:9600", "COM1:9600,n", "COM1:9600,n,8", "COM1:", "COM1", "9600,n,8,1",
        "COM5:9600,n,8,1", "COM9:9600,n,8,1", "LPT1:", "LPT1:9600,n,8,1",
        "COM1:9600,m,8,1", "COM1:9600,s,8,1", "COM1:9600,x,8,1", "COM1:9600,n,9,1",
        "COM1:9600,n,4,1", "COM1:9600,n,8,3", "COM1:9600,n,8,2", "COM1:9600,n,7,1.5",
        "COM1:abc", "COM1:9600n81", "COM1:19200,n,8,1", "COM1:38400,n,8,1",
        "COM1:56000,n,8,1", "COM1:115200,n,8,1", "COM1:0,n,8,1", "COM1:1,n,8,1",
        "COM1:65535,n,8,1", "COM1:9600,,8,1", "COM1:9600,n,,1", "COM1:,n,8,1",
        "COM1:9600,n,8,1,", "COM1:9600,n,8,1,x,x", "COM1=9600,n,8,1", "COM1;9600,n,8,1",
        "", " COM1:9600,n,8,1", "COM1:9600 ,n,8,1", "COM1:9600,n ,8,1",
        "COM5:38400,n,8,1", "COM5:", "COM5:abc", "COM1:9600,n,6,1", "COM1:9600,n,5,1",
        "COM1:9600,n,8,", "COM1:110,n,8,", "COM1:110,n,8", "COM1:110,n,8,,",
        "COM1:9600,n,8,1,p,x", "COM1:9600,n,8,1,P", "COM1:15,n,8,1", "COM1:60,n,8,1",
        "COM1:14,n,8,1", "COM1:9,n,8,1", "COM1:9600,n,8x,1", "COM1:9600,nx,8,1",
        "COM1:9600,n,8,1x", "COM1:96xx,n,8,1", "COM1:9600,,,", "COM1:9600,,,,p", "LPT2:",
        "LPT3:", "LPT4:", "LPT1", "PRN:", "PRN", "AUX:9600,n,8,1", "AUX", "COM1:9600;n,8,1",
        "COM1:\t9600", "COM1:  9600", "COM1:9600,  n", "COM1:9600, ,8,1", "lpt1:",
        "COM1:9600,e,7,2,P", "COM12:9600", "COM1:9600,n,8,2.5", "COM1:9600,n,8,1.0",
        "COM1: ", "COM1:9600,n,8,1,p,", "COM1:96,e", "COM1:19,o,7,2", "COM2 1200", "COM2:1200,E,7",
        "COM1:300,n,8,1", "COM1:150,n,8,1", "COM1:600,n,8,1", "COM1:2400,n,8,1", "COM1:4800,n,8,1",
        "COM1:110,n,8,2", "COM1:9600,space,8,1", "COM1:9600,n,8,1, p", "COM1:9600,n,8,1 ,p",
        "COM1:9600,", "COM1:9600, ", "COM1:96,", "COM1:9600,n,", "COM1:9600,n,8,1,p, ",
        "COM1:9600,n,8,1,p,,", "COM1:9600,,,,", "COM1:9600,,,,,", "COM1:9600,n,8,1,,",
        "COM1:9600,n,8,1,,p", "COM1:9600,n,8,1,p x", "COM1:9600,n,8,1 p", "COM1:9600 n 8 1",
        "COM1 9600 n 8 1 p", "PRN:9600", "AUX 9600", "COM1:9600,n,8,1,pp", "COM1:9600,n,8,1,px",
        "COM1:9600,o", "COM1:9600 ", "COM1:9600 ,", "COM1:9600,,", "COM1:110,,", "COM1:1100",
        "COM1:1", "COM1:19", "COM1:9600,n,8,1,p", "COM1:9600,n,8 ,1", "COM1:9600,n,8, 1",
        "COM1:9600,n,8,1 ", "COM1:9600,n,8,1,p ", "COM1:9600,e,7,1,", "COM1:9600,e,7,",
        "COM1:9600,x,", "COM1:9600,n,9,", "COM1:9600,n,8,3,", "COM1:9600,n,8,1,x,",
        "COM1:9600,x , ", "COM1:9600,n,8,1,p,x,y", "COM1:9600,x", "COM1:9600,n,9", 0
    };
    int i, r;
    char t[80];
    mark("B");
    for (i = 0; s[i]; i++) {
        BYTE FAR *p = (BYTE FAR *)&dcb;
        int k;
        for (k = 0; k < (int)sizeof dcb; k++)
            p[k] = 0xCC;
        r = BuildCommDCB(s[i], &dcb);
        wsprintf(t, "B \"%s\"", (LPSTR)s[i]);
        obs(t, r);
        lstrcat(t, " DCB");
        bdump(t, p, sizeof dcb);
    }
    /* over a DCB of zeros, so what it leaves alone shows */
    for (i = 0; i < 6; i++) {
        BYTE FAR *p = (BYTE FAR *)&dcb;
        int k;
        for (k = 0; k < (int)sizeof dcb; k++)
            p[k] = 0;
        r = BuildCommDCB(s[i], &dcb);
        wsprintf(t, "B0 \"%s\"", (LPSTR)s[i]);
        obs(t, r);
        lstrcat(t, " DCB");
        bdump(t, p, sizeof dcb);
    }
}

/* ---- S: the rest on an open port, COM1 with commhost ---- */
static void err(const char *name, int cid)
{
    char t[60];
    int e;
    {
        BYTE FAR *p = (BYTE FAR *)&cs;
        int k;
        for (k = 0; k < (int)sizeof cs; k++)
            p[k] = 0xCC;
    }
    e = GetCommError(cid, &cs);
    wsprintf(t, "%s GetCommError", (LPSTR)name);
    obs(t, e);
    lstrcat(t, " COMSTAT");
    bdump(t, (BYTE FAR *)&cs, sizeof cs);
}

static void send_cmd(int cid, LPCSTR s)
{
    WriteComm(cid, s, lstrlen(s));
}

static void part_s(void)
{
    int cid, r, i;
    char t[60];
    UINT FAR *ev, FAR *ev2;
    mark("S");
    cid = OpenComm("COM1", 64, 32);
    obs("S open COM1 64 32", cid);
    if (cid < 0)
        return;
    GetCommState(cid, &dcb);
    bdump("S DCB at open", (BYTE FAR *)&dcb, sizeof dcb);
    err("S at open", cid);
    /* SetCommState: as it is, another rate, a wrong Id, bad fields */
    mark("S SetCommState");
    obs("S SetCommState same", SetCommState(&dcb));
    dcb.BaudRate = 19200;
    obs("S SetCommState 19200", SetCommState(&dcb));
    GetCommState(cid, &dcb);
    obs("S rate after", dcb.BaudRate);
    dcb.Id = 5;
    obs("S SetCommState Id 5", SetCommState(&dcb));
    dcb.Id = (BYTE)cid;
    dcb.ByteSize = 9;
    obs("S SetCommState ByteSize 9", SetCommState(&dcb));
    dcb.ByteSize = 8;
    dcb.BaudRate = 1;
    obs("S SetCommState rate 1", SetCommState(&dcb));
    dcb.BaudRate = 9600;
    obs("S SetCommState 9600", SetCommState(&dcb));
    GetCommState(cid, &dcb);
    bdump("S DCB after", (BYTE FAR *)&dcb, sizeof dcb);
    /* the event mask */
    mark("S events");
    ev = SetCommEventMask(cid, EV_RXCHAR | EV_TXEMPTY);
    obs("S SetCommEventMask null", ev == 0);
    ev2 = SetCommEventMask(cid, EV_RXCHAR | EV_TXEMPTY | EV_RXFLAG);
    obs("S SetCommEventMask same pointer", ev == ev2);
    obs("S GetCommEventMask 0", GetCommEventMask(cid, 0));
    obs("S GetCommEventMask all", GetCommEventMask(cid, 0xFFFF));
    /* escapes */
    mark("S escapes");
    for (i = 0; i <= 12; i++) {
        wsprintf(t, "S EscapeCommFunction %d", i);
        obsl(t, (DWORD)EscapeCommFunction(cid, i));
    }
    obsl("S EscapeCommFunction 0x7F", (DWORD)EscapeCommFunction(cid, 0x7F));
    obsl("S EscapeCommFunction SETXON again", (DWORD)EscapeCommFunction(cid, SETXON));
    /* break */
    mark("S break");
    obs("S SetCommBreak", SetCommBreak(cid));
    obs("S ClearCommBreak", ClearCommBreak(cid));
    /* writing: a queue of 32 */
    mark("S write");
    for (i = 0; i < 100; i++)
        buf[i] = (char)('a' + i % 26);
    EscapeCommFunction(cid, SETXOFF);       /* the output held: the queue fills */
    r = WriteComm(cid, buf, 20);
    obs("S WriteComm 20 held", r);
    r = WriteComm(cid, buf, 20);
    obs("S WriteComm 20 more held", r);
    err("S after the writes held", cid);
    r = WriteComm(cid, buf, 0);
    obs("S WriteComm 0", r);
    obs("S TransmitCommChar held", TransmitCommChar(cid, 'Z'));
    obs("S TransmitCommChar again", TransmitCommChar(cid, 'Y'));
    err("S after TransmitCommChar", cid);
    obs("S FlushComm 0", FlushComm(cid, 0));
    err("S after FlushComm 0", cid);
    EscapeCommFunction(cid, SETXON);
    pump(300);
    /* reading: commhost sends; a queue of 64 */
    mark("S read");
    send_cmd(cid, "#P100,48;");
    pump(1500);
    err("S 100 bytes into 64", cid);
    r = ReadComm(cid, buf, 10);
    obs("S ReadComm 10", r);
    bdump("S read", (BYTE FAR *)buf, 10);
    obs("S UngetCommChar", UngetCommChar(cid, 'Q'));
    obs("S UngetCommChar second", UngetCommChar(cid, 'R'));
    err("S after UngetCommChar", cid);
    r = ReadComm(cid, buf, 3);
    obs("S ReadComm 3", r);
    bdump("S read 3", (BYTE FAR *)buf, 3);
    r = ReadComm(cid, buf, 0);
    obs("S ReadComm 0", r);
    r = ReadComm(cid, buf, 200);
    obs("S ReadComm 200", r);
    r = ReadComm(cid, buf, 10);
    obs("S ReadComm empty", r);
    err("S read out", cid);
    send_cmd(cid, "#P20,0;");
    pump(800);
    obs("S FlushComm 1", FlushComm(cid, 1));
    err("S after FlushComm 1", cid);
    obs("S FlushComm 2", FlushComm(cid, 2));
    obs("S FlushComm -1", FlushComm(cid, -1));
    /* an error pending: ReadComm's result */
    send_cmd(cid, "#P80,0;");
    pump(1200);
    r = ReadComm(cid, buf, 100);
    obs("S ReadComm with overrun", r);
    err("S after it", cid);
    /* an error pending, a character put back, more than asked for there */
    FlushComm(cid, 1);
    GetCommError(cid, &cs);
    send_cmd(cid, "#P80,0;");
    pump(1200);
    mark("S unget with an error pending");
    obs("S UngetCommChar pending", UngetCommChar(cid, 'U'));
    r = ReadComm(cid, buf, 10);
    obs("S ReadComm 10 after unget, error pending", r);
    bdump("S read after unget", (BYTE FAR *)buf, 10);
    err("S then", cid);
    r = ReadComm(cid, buf, 10);
    obs("S ReadComm 10 then", r);
    obs("S UngetCommChar again", UngetCommChar(cid, 'V'));
    r = ReadComm(cid, buf, 10);
    obs("S ReadComm 10 after unget", r);
    bdump("S read after unget 2", (BYTE FAR *)buf, 11);
    r = ReadComm(cid, buf, 1);
    obs("S ReadComm 1", r);
    obs("S UngetCommChar 3", UngetCommChar(cid, 'W'));
    r = ReadComm(cid, buf, 1);
    obs("S ReadComm 1 after unget", r);
    bdump("S read 1 after unget", (BYTE FAR *)buf, 2);
    obs("S UngetCommChar 4", UngetCommChar(cid, 'X'));
    obs("S FlushComm 1 with one put back", FlushComm(cid, 1));
    r = ReadComm(cid, buf, 5);
    obs("S ReadComm after the flush", r);
    bdump("S read after the flush", (BYTE FAR *)buf, 1);
    err("S after all", cid);
    /* notification */
    mark("S notify");
    notelog[0] = 0;
    obs("S EnableCommNotification 10 5", EnableCommNotification(cid, hwnd, 10, 5));
    obs("S EnableCommNotification -1 -1", EnableCommNotification(cid, hwnd, -1, -1));
    obs("S EnableCommNotification 0 0", EnableCommNotification(cid, hwnd, 0, 0));
    obs("S EnableCommNotification 64 32", EnableCommNotification(cid, hwnd, 64, 32));
    obs("S EnableCommNotification 65 33", EnableCommNotification(cid, hwnd, 65, 33));
    obs("S EnableCommNotification 1000 1000", EnableCommNotification(cid, hwnd, 1000, 1000));
    obs("S EnableCommNotification -2 -2", EnableCommNotification(cid, hwnd, -2, -2));
    obs("S EnableCommNotification 10 5", EnableCommNotification(cid, hwnd, 10, 5));
    send_cmd(cid, "#P30,0;");
    pump(1500);
    ReadComm(cid, buf, 100);
    pump(500);
    obs_str("S notifications", notelog);
    obs("S EnableCommNotification off", EnableCommNotification(cid, 0, 10, 5));
    obs("S EnableCommNotification bad hwnd", EnableCommNotification(cid, (HWND)0x1234, 10, 5));
    obs("S EnableCommNotification closed port", EnableCommNotification(5, hwnd, 10, 5));
    /* closing */
    mark("S close");
    obs("S CloseComm", CloseComm(cid));
    obs("S CloseComm again", CloseComm(cid));
    obs("S ReadComm closed", ReadComm(cid, buf, 10));
    obs("S WriteComm closed", WriteComm(cid, buf, 10));
    obs("S GetCommError closed", GetCommError(cid, &cs));
    obs("S GetCommState closed", GetCommState(cid, &dcb));
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("CM2 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "CM2";
    RegisterClass(&wc);
    hwnd = CreateWindow("CM2", "CM2", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, hi, NULL);
    obs_env("ds", SELECTOROF((LPVOID)&dcb));
    obs_env("hwnd", (WORD)hwnd);
    obs_env("stk", OFFSETOF((LPVOID)&wc));
    /* SYSTEM.DRV's GetSystemMsecCount, which COMM.DRV times its close
       with: does it ever go back or jump? ("~": timing) */
    {
        FARPROC f = GetProcAddress(GetModuleHandle("SYSTEM"), MAKEINTRESOURCE(6));
        DWORD prev = 0, v, t0 = GetTickCount();
        WORD back = 0, jumps = 0, n = 0;
        int k;
        for (k = 0; k < 16; k++)
            args[k] = 0;
        while (f && GetTickCount() - t0 < 100000L && prev < 0x10800L) {
            v = md_call(f, args, res);
            if (n && (long)(v - prev) < 0)
                back++;
            if (n && v - prev > 1000 && (long)(v - prev) > 0)
                jumps++;
            prev = v;
            n++;
        }
        wsprintf(line, "OBS ~msec calls %u back %u jumps %u last %08lX\r\n", n, back, jumps, prev);
        out(line);
    }
    part_p();
    part_n();
    part_w();
    part_b();
    part_s();
    mark("end");
    out("CM2 done\r\n");
    return 0;
}
