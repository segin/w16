/*
 * comm.c - the communications functions (USER.200-215, 245: OpenComm ...
 * FlushComm, EnableCommNotification) over the communications driver,
 * COMM.DRV (its interface: the Windows 3.1 DDK; w16's driver and how it
 * was measured: drivers/comm/README.md).
 *
 * Everything here is as Windows 3.1's USER does it, observed with probe
 * CM2 (tests/apps/cm2.c, run by tests/commusr.sh) and COMMREC
 * (tests/apps/commrec.c), a recording stand-in in front of the driver
 * that logs every call USER makes to it (its arguments' values only):
 *   - which driver entry each function calls, with what: most pass their
 *     arguments straight on, the application's pointers included
 *     (GetCommError's COMSTAT, SetCommState's DCB, ReadComm's and
 *     WriteComm's buffers); ReadComm and WriteComm call the driver's
 *     ReadCommString and CommWriteString (ordinals 20 and 19, which USER
 *     finds at run time), EnableCommNotification its ordinal 100;
 *   - what USER checks itself: an id below 0 (every function fails
 *     without a call: -1, or 0 for GetCommError, GetCommEventMask and
 *     EnableCommNotification, a null pointer for SetCommEventMask);
 *     a port it has not opened (GetCommState and SetCommState IE_NOPEN,
 *     ReadComm 0, WriteComm, CloseComm and UngetCommChar -1; the others
 *     go to the driver, which answers for itself); a window handle that
 *     is no window (EnableCommNotification 0);
 *   - OpenComm: the names COM1-COM9, AUX, LPT1-LPT3 and PRN, any case,
 *     with or without a colon and nothing else (IE_BADID, no call); a
 *     port open already IE_OPEN; for a COM port, either queue size 0
 *     IE_MEMORY; then setque (its result not looked at) and inicom with
 *     a DCB of USER's own on the stack: 9600 baud, 7 data bits, even
 *     parity, 1 stop bit, binary, XON 11h, XOFF 13h, both limits 10,
 *     everything else 0, whatever WIN.INI [ports] says (a parallel
     port's: the Id, the rest left as it was on USER's stack); inicom's error
 *     is the result, else the id (COM n is n - 1, LPT n 80h + n - 1);
 *     an LPT port gets no queues;
 *   - CloseComm: trmcom, its result;
 *   - GetCommState: getdcb, the 25 bytes copied;
 *   - WriteComm: CommWriteString's count, negative when not all of it
 *     went (20 asked, 12 taken: -12);
 *   - ReadComm: a byte put back (UngetCommChar, one at a time) goes into
 *     the buffer's first byte and ReadCommString reads the full count
 *     after it, and the result is ReadCommString's (so the buffer gets
 *     one byte more than asked for, and the byte put back is not
 *     counted: 3.1's own); 0 bytes asked: 0, no call; FlushComm leaves a
 *     byte put back alone;
 *   - BuildCommDCB: no driver call (parse_dcb below).
 * Not known: what 3.1 does with a port still open when its task ends
 * (here nothing; the queues are USER's, GMEM_SHARE, so the driver never
 * writes into freed memory).
 */
#include "user.h"

/* COMM.DRV's entries (USER imports 1-15; 19, 20 and 100 are looked up) */
extern int FAR PASCAL C_INICOM(BYTE FAR *dcb);
extern int FAR PASCAL C_SETCOM(const BYTE FAR *dcb);
extern int FAR PASCAL C_SETQUE(int cid, BYTE FAR *qdb);
extern int FAR PASCAL C_RECCOM(int cid);
extern int FAR PASCAL C_SNDCOM(int cid, int ch);
extern int FAR PASCAL C_CTX(int cid, int ch);
extern int FAR PASCAL C_TRMCOM(int cid);
extern int FAR PASCAL C_STACOM(int cid, COMSTAT FAR *cs);
extern LONG FAR PASCAL C_CEXTFCN(int cid, int fcn);
extern int FAR PASCAL C_CFLUSH(int cid, int q);
extern UINT FAR * FAR PASCAL C_CEVT(int cid, UINT mask);
extern UINT FAR PASCAL C_CEVTGET(int cid, UINT mask);
extern int FAR PASCAL C_CSETBRK(int cid);
extern int FAR PASCAL C_CCLRBRK(int cid);
extern BYTE FAR * FAR PASCAL C_GETDCB(int cid);

typedef int (FAR PASCAL *STRFN)(int, LPSTR, int);
typedef BOOL (FAR PASCAL *NOTEFN)(int, HWND, int, int);

#define DCB_SIZE 25
#define NPORTS   12             /* COM1-COM9, LPT1-LPT3 */

typedef struct {
    BYTE    open;
    BYTE    ungot;              /* a byte put back */
    char    ungotc;
    HGLOBAL hin, hout;          /* the queues */
    BYTE    qdb[24];            /* the queue block given to setque */
} PORT;

static PORT ports[NPORTS];
static STRFN pWriteString, pReadString;
static NOTEFN pNotify;
static BYTE looked;

static void lookup(void)
{
    HMODULE m;
    if (looked)
        return;
    looked = 1;
    m = GetModuleHandle("COMM");
    if (m) {
        pWriteString = (STRFN)GetProcAddress(m, MAKEINTRESOURCE(19));
        pReadString = (STRFN)GetProcAddress(m, MAKEINTRESOURCE(20));
        pNotify = (NOTEFN)GetProcAddress(m, MAKEINTRESOURCE(100));
    }
}

/* the port record of an id: COM ids 0-8, LPT 80h-82h; 0 for others */
static PORT *port_of(int cid)
{
    if (cid >= 0 && cid < 9)
        return &ports[cid];
    if (cid >= 0x80 && cid < 0x83)
        return &ports[9 + cid - 0x80];
    return 0;
}

static PORT *open_port(int cid)
{
    PORT *p = port_of(cid);
    return p && p->open ? p : 0;
}

static char up(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

/* s starts with word w (any case) */
static BOOL starts(LPCSTR s, const char *w)
{
    while (*w)
        if (up(*s++) != *w++)
            return FALSE;
    return TRUE;
}

/* a port name at s: its id and its length, -1 for none. COMn (1-9) and
   AUX (COM1), LPTn (1-3) and PRN (LPT1) */
static int port_name(LPCSTR s, int *len)
{
    if (starts(s, "COM") && s[3] >= '1' && s[3] <= '9') {
        *len = 4;
        return s[3] - '1';
    }
    if (starts(s, "AUX")) {
        *len = 3;
        return 0;
    }
    if (starts(s, "LPT") && s[3] >= '1' && s[3] <= '3') {
        *len = 4;
        return 0x80 + s[3] - '1';
    }
    if (starts(s, "PRN")) {
        *len = 3;
        return 0x80;
    }
    return -1;
}

static void dcb_word(BYTE FAR *d, int off, WORD v)
{
    d[off] = LOBYTE(v);
    d[off + 1] = HIBYTE(v);
}

/* the DCB OpenComm gives inicom, and the fields BuildCommDCB fills in
   once it has a rate: binary, XON 11h, XOFF 13h, limits 10 */
static void dcb_defaults(BYTE FAR *d)
{
    d[12] = 0x01;               /* fBinary */
    d[14] = 0x11;
    d[15] = 0x13;
    dcb_word(d, 16, 10);
    dcb_word(d, 18, 10);
}

int WINAPI OpenComm(LPCSTR name, UINT in, UINT out)
{
    BYTE dcb[DCB_SIZE];
    int len, id, r, i;
    PORT *p;
    id = port_name(name, &len);
    if (id < 0 || (name[len] == ':' ? name[len + 1] : name[len]) != 0)
        return IE_BADID;
    p = port_of(id);
    if (p->open)
        return IE_OPEN;
    if (id < 0x80) {
        BYTE FAR *q = p->qdb;
        if (!in || !out)
            return IE_MEMORY;
        p->hin = GlobalAlloc(GMEM_FIXED | GMEM_SHARE, in);
        p->hout = p->hin ? GlobalAlloc(GMEM_FIXED | GMEM_SHARE, out) : 0;
        if (!p->hout) {
            if (p->hin)
                GlobalFree(p->hin);
            p->hin = 0;
            return IE_MEMORY;
        }
        for (i = 0; i < (int)sizeof p->qdb; i++)
            q[i] = 0;
        /* the queue block (DDK: QInAddr, QInSize, QOutAddr, QOutSize) */
        *(LPSTR FAR *)(q + 0) = (LPSTR)GlobalLock(p->hin);
        dcb_word(q, 4, in);
        *(LPSTR FAR *)(q + 6) = (LPSTR)GlobalLock(p->hout);
        dcb_word(q, 10, out);
        C_SETQUE(id, q);
    }
    /* a parallel port's DCB: only its Id (3.1's has whatever its stack
       held after the Id; w16's is zeros) */
    for (i = 0; i < DCB_SIZE; i++)
        dcb[i] = 0;
    dcb[0] = (BYTE)id;
    if (id < 0x80) {
        dcb_word(dcb, 1, 9600);
        dcb[3] = 7;
        dcb[4] = EVENPARITY;
        dcb[5] = ONESTOPBIT;
        dcb_defaults(dcb);
    }
    r = C_INICOM((BYTE FAR *)dcb);
    if (r < 0) {
        if (p->hin) {
            GlobalFree(p->hin);
            GlobalFree(p->hout);
        }
        p->hin = p->hout = 0;
        return r;
    }
    p->open = 1;
    p->ungot = 0;
    return id;
}

int WINAPI CloseComm(int cid)
{
    PORT *p;
    int r;
    if (cid < 0 || (p = open_port(cid)) == 0)
        return -1;
    r = C_TRMCOM(cid);
    if (p->hin) {
        GlobalFree(p->hin);
        GlobalFree(p->hout);
    }
    p->hin = p->hout = 0;
    p->open = 0;
    p->ungot = 0;
    return r;
}

int WINAPI GetCommState(int cid, DCB FAR *dcb)
{
    BYTE FAR *d;
    int i;
    if (cid < 0)
        return -1;
    if (!open_port(cid))
        return IE_NOPEN;
    d = C_GETDCB(cid);
    if (!d)
        return -1;
    for (i = 0; i < DCB_SIZE; i++)
        ((BYTE FAR *)dcb)[i] = d[i];
    return 0;
}

int WINAPI SetCommState(const DCB FAR *dcb)
{
    if (!open_port(((const BYTE FAR *)dcb)[0]))
        return IE_NOPEN;
    return C_SETCOM((const BYTE FAR *)dcb);
}

/* a byte put back counts in the receive queue (CM2: 55 after 54 and
   UngetCommChar) */
int WINAPI GetCommError(int cid, COMSTAT FAR *cs)
{
    PORT *p;
    int r;
    if (cid < 0)
        return 0;
    r = C_STACOM(cid, cs);
    if (cs && (p = open_port(cid)) != 0 && p->ungot)
        cs->cbInQue++;
    return r;
}

/* the driver's ReadCommString and CommWriteString, or byte by byte */
static int read_string(int cid, LPSTR buf, int n)
{
    int k, c;
    lookup();
    if (pReadString)
        return pReadString(cid, buf, n);
    for (k = 0; k < n && (c = C_RECCOM(cid)) >= 0; k++)
        buf[k] = (char)c;
    return k;
}

static int write_string(int cid, LPCSTR buf, int n)
{
    int k;
    lookup();
    if (pWriteString)
        return pWriteString(cid, (LPSTR)buf, n);
    for (k = 0; k < n && !C_SNDCOM(cid, (BYTE)buf[k]); k++)
        ;
    return k;
}

int WINAPI ReadComm(int cid, void FAR *buf, int n)
{
    PORT *p;
    LPSTR b = (LPSTR)buf;
    if (cid < 0)
        return -1;
    if ((p = open_port(cid)) == 0 || n == 0)
        return 0;
    if (p->ungot) {
        p->ungot = 0;
        b[0] = p->ungotc;
        return read_string(cid, b + 1, n);
    }
    return read_string(cid, b, n);
}

int WINAPI WriteComm(int cid, const void FAR *buf, int n)
{
    int r;
    if (cid < 0 || !open_port(cid))
        return -1;
    r = write_string(cid, (LPCSTR)buf, n);
    return r == n ? r : -r;
}

int WINAPI TransmitCommChar(int cid, char c)
{
    if (cid < 0)
        return -1;
    return C_CTX(cid, (BYTE)c);
}

int WINAPI UngetCommChar(int cid, char c)
{
    PORT *p;
    if (cid < 0 || (p = open_port(cid)) == 0 || p->ungot)
        return -1;
    p->ungot = 1;
    p->ungotc = c;
    return 0;
}

UINT FAR * WINAPI SetCommEventMask(int cid, UINT mask)
{
    if (cid < 0)
        return 0;
    return C_CEVT(cid, mask);
}

UINT WINAPI GetCommEventMask(int cid, int mask)
{
    if (cid < 0)
        return 0;
    return C_CEVTGET(cid, mask);
}

int WINAPI SetCommBreak(int cid)
{
    if (cid < 0)
        return -1;
    return C_CSETBRK(cid);
}

int WINAPI ClearCommBreak(int cid)
{
    if (cid < 0)
        return -1;
    return C_CCLRBRK(cid);
}

LONG WINAPI EscapeCommFunction(int cid, int fcn)
{
    if (cid < 0)
        return -1;
    return C_CEXTFCN(cid, fcn);
}

int WINAPI FlushComm(int cid, int q)
{
    if (cid < 0)
        return -1;
    return C_CFLUSH(cid, q);
}

BOOL WINAPI EnableCommNotification(int cid, HWND h, int in, int out)
{
    if (cid < 0 || (h && !IsWindow(h)))
        return FALSE;
    lookup();
    return pNotify ? pNotify(cid, h, in, out) : FALSE;
}

/* ---- BuildCommDCB ----
   The device-control string as DOS's MODE takes it, measured with CM2
   (part B, about 170 strings) on Windows 3.1:
   - the DCB is cleared first; then a port name at the very start: COMn
     (1-9) or AUX, then ':' or a blank (nothing else, not even the end);
     LPTn (1-3) or PRN, and that is all (success, the Id set, the rest of
     the string not read). No port: -1, the DCB all zeros; a COM port's
     Id is set even when what follows fails;
   - the rate, after blanks: its first two characters name it (11 110,
     15 150, 30 300, 60 600, 12 1200, 24 2400, 48 4800, 96 9600, 19
     19200; so 115200 is 110, and 38400, 14 or a single digit fail with
     only the Id set); with it the fields OpenComm's DCB has (binary,
     XON/XOFF, the limits);
   - then up to four more fields, each after a ',' or a blank and any
     blanks: parity (n e o m s, any case: 0-4), data bits (7, 8), stop
     bits (1, 2: 1.5 is 1), and 'p' (any case: the three time-outs
     FFFFh, MODE's retry). Of each field only its first character
     counts, the rest up to the next ',' or blank is passed over. An
     empty field (a ',' straight away) means MODE's default: even
     parity, 7 data bits, 1 stop bit; an empty fifth field fails;
   - a field's value is stored only when the string goes on after it: a
     ',' or blank with nothing after it drops it ("9600,n,8," leaves the
     data bits 0, "...,p," no time-outs), the end of the string straight
     after it keeps it; a bad value fails then (-1, what was stored before
     it kept); after the fifth field nothing more is read. */
#define F_NONE  0               /* nothing pending */
#define F_SET   1               /* a value to store */
#define F_BAD   2               /* fails when it would be stored */

static int baud_of(LPCSTR s)
{
    static const struct { char a, b; WORD rate; } r[] = {
        { '1', '1', 110 }, { '1', '5', 150 }, { '3', '0', 300 }, { '6', '0', 600 },
        { '1', '2', 1200 }, { '2', '4', 2400 }, { '4', '8', 4800 }, { '9', '6', 9600 },
        { '1', '9', 19200 }
    };
    int i;
    for (i = 0; i < (int)(sizeof r / sizeof r[0]); i++)
        if (s[0] == r[i].a && s[1] == r[i].b)
            return r[i].rate;
    return 0;
}

static LPCSTR skip_blanks(LPCSTR s)
{
    while (*s == ' ')
        s++;
    return s;
}

static LPCSTR skip_value(LPCSTR s)
{
    while (*s && *s != ',' && *s != ' ')
        s++;
    return s;
}

int WINAPI BuildCommDCB(LPCSTR s, DCB FAR *dcbp)
{
    BYTE FAR *d = (BYTE FAR *)dcbp;
    int i, len, id, rate, field, state = F_NONE, off = 0;
    WORD val = 0;
    for (i = 0; i < DCB_SIZE; i++)
        d[i] = 0;
    id = port_name(s, &len);
    if (id < 0)
        return -1;
    d[0] = (BYTE)id;
    if (id >= 0x80)
        return 0;
    s += len;
    if (*s != ':' && *s != ' ')
        return -1;
    s = skip_blanks(s + 1);
    if (!*s || (rate = baud_of(s)) == 0)
        return -1;
    dcb_word(d, 1, rate);
    dcb_defaults(d);
    s = skip_value(s + 2);
    for (field = 2;; field++) {
        if (!*s)
            break;                      /* the end after a value: kept */
        s = skip_blanks(s + 1);         /* past the ',' or blank */
        if (!*s)
            return 0;                   /* nothing after it: dropped */
        if (state == F_BAD)
            return -1;
        if (state == F_SET) {
            if (off == 6) {
                dcb_word(d, 6, 0xFFFF);
                dcb_word(d, 8, 0xFFFF);
                dcb_word(d, 10, 0xFFFF);
            } else
                d[off] = (BYTE)val;
        }
        state = F_NONE;
        if (field > 5)
            return 0;                   /* the rest is not read */
        if (*s == ',') {
            /* empty: the default */
            state = field == 5 ? F_BAD : F_SET;
            off = field == 2 ? 4 : field == 3 ? 3 : 5;
            val = field == 2 ? EVENPARITY : field == 3 ? 7 : ONESTOPBIT;
            continue;
        }
        state = F_SET;
        switch (field) {
        case 2: {
            static const char par[] = "NOEMS";
            char c = up(*s);
            off = 4;
            for (val = 0; par[val] && par[val] != c; val++)
                ;
            if (!par[val])
                state = F_BAD;
            break;
        }
        case 3:
            off = 3;
            val = *s - '0';
            if (*s != '7' && *s != '8')
                state = F_BAD;
            break;
        case 4:
            off = 5;
            val = *s == '2' ? TWOSTOPBITS : ONESTOPBIT;
            if (*s != '1' && *s != '2')
                state = F_BAD;
            break;
        default:
            off = 6;
            if (up(*s) != 'P')
                state = F_BAD;
            break;
        }
        s = skip_value(s);
    }
    if (state == F_BAD)
        return -1;
    if (state == F_SET) {
        if (off == 6) {
            dcb_word(d, 6, 0xFFFF);
            dcb_word(d, 8, 0xFFFF);
            dcb_word(d, 10, 0xFFFF);
        } else
            d[off] = (BYTE)val;
    }
    return 0;
}
