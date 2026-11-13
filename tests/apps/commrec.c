/*
 * commrec.c - COMMREC: a recording stand-in for the communications
 * driver, for observing how USER's comm functions (OpenComm, WriteComm,
 * ...) use COMM.DRV (tests/commusr.sh).
 *
 * Built as COMM.DRV with the module name COMM, it puts itself in front of
 * the real driver: a copy of Windows 3.1's (or w16's) COMM.DRV whose
 * module name is changed to MCOM (only the name in the NE resident-name
 * table: tests/scripts/nemodname.py), installed as SYSTEM\MCOM.DLL, which
 * this module imports by ordinal. Every entry passes its arguments on to
 * the same ordinal of MCOM unchanged and returns what it returned; before
 * and after, it appends a line to C:\COMMREC.LOG:
 *     ord(arg1,arg2,...)=result
 * in hexadecimal, the arguments in the order the DDK writes them,
 * pointers as seg:off. Only values: never the memory behind a pointer.
 *
 * It imports only KERNEL and MCOM: USER imports COMM, so COMM must not
 * import USER. It is called on its callers' stacks (SS != DS).
 */
#include <windows.h>

extern int FAR PASCAL M_INICOM(LPVOID);
extern int FAR PASCAL M_SETCOM(LPVOID);
extern int FAR PASCAL M_SETQUE(int, LPVOID);
extern int FAR PASCAL M_RECCOM(int);
extern int FAR PASCAL M_SNDCOM(int, int);
extern int FAR PASCAL M_CTX(int, int);
extern int FAR PASCAL M_TRMCOM(int);
extern int FAR PASCAL M_STACOM(int, LPVOID);
extern DWORD FAR PASCAL M_CEXTFCN(int, int);
extern int FAR PASCAL M_CFLUSH(int, int);
extern LPVOID FAR PASCAL M_CEVT(int, WORD);
extern WORD FAR PASCAL M_CEVTGET(int, WORD);
extern int FAR PASCAL M_CSETBRK(int);
extern int FAR PASCAL M_CCLRBRK(int);
extern LPVOID FAR PASCAL M_GETDCB(int);
extern int FAR PASCAL M_SUSPEND(void);
extern int FAR PASCAL M_REACTIVATE(void);
extern int FAR PASCAL M_COMMWRITESTRING(int, LPSTR, int);
extern int FAR PASCAL M_READCOMMSTRING(int, LPSTR, int);
extern BOOL FAR PASCAL M_ENABLENOTIFICATION(int, HWND, int, int);

static char buf[96];
static int n;
static const char hexd[] = "0123456789ABCDEF";

static void put(char c)
{
    if (n < (int)sizeof buf - 3)
        buf[n++] = c;
}

static void hex(WORD v)
{
    int i;
    for (i = 12; i >= 0; i -= 4)
        put(hexd[(v >> i) & 15]);
}

static void start(int ord)
{
    n = 0;
    hex(ord);
    put('(');
}

static void arg(WORD v)
{
    if (buf[n - 1] != '(')
        put(',');
    hex(v);
}

static void parg(const void FAR *p)
{
    arg(SELECTOROF(p));
    put(':');
    hex(OFFSETOF(p));
}

static void flush(void)
{
    HFILE f;
    OFSTRUCT of;
    buf[n++] = '\r';
    buf[n++] = '\n';
    f = OpenFile("C:\\COMMREC.LOG", &of, OF_WRITE);
    if (f == HFILE_ERROR)
        f = OpenFile("C:\\COMMREC.LOG", &of, OF_CREATE | OF_WRITE);
    if (f != HFILE_ERROR) {
        _llseek(f, 0, 2);
        _lwrite(f, buf, n);
        _lclose(f);
    }
}

/* the call written before it is made (it may not come back), and the
   result after */
static void called(void)
{
    put(')');
    flush();
    n = 0;
    put(' ');
    put('=');
}

static int ret(int r)
{
    hex(r);
    flush();
    return r;
}

static DWORD retd(DWORD r)
{
    hex(HIWORD(r));
    put(':');
    hex(LOWORD(r));
    flush();
    return r;
}

int __cdecl libmain(void)
{
    return 1;
}

int FAR PASCAL __loadds INICOM(LPVOID d)
{
    start(1); parg(d); called();
    return ret(M_INICOM(d));
}

int FAR PASCAL __loadds SETCOM(LPVOID d)
{
    start(2); parg(d); called();
    return ret(M_SETCOM(d));
}

int FAR PASCAL __loadds SETQUE(int cid, LPVOID q)
{
    start(3); arg(cid); parg(q); called();
    return ret(M_SETQUE(cid, q));
}

int FAR PASCAL __loadds RECCOM(int cid)
{
    start(4); arg(cid); called();
    return ret(M_RECCOM(cid));
}

int FAR PASCAL __loadds SNDCOM(int cid, int ch)
{
    start(5); arg(cid); arg(ch); called();
    return ret(M_SNDCOM(cid, ch));
}

int FAR PASCAL __loadds CTX(int cid, int ch)
{
    start(6); arg(cid); arg(ch); called();
    return ret(M_CTX(cid, ch));
}

int FAR PASCAL __loadds TRMCOM(int cid)
{
    start(7); arg(cid); called();
    return ret(M_TRMCOM(cid));
}

int FAR PASCAL __loadds STACOM(int cid, LPVOID s)
{
    start(8); arg(cid); parg(s); called();
    return ret(M_STACOM(cid, s));
}

DWORD FAR PASCAL __loadds CEXTFCN(int cid, int f)
{
    start(9); arg(cid); arg(f); called();
    return retd(M_CEXTFCN(cid, f));
}

int FAR PASCAL __loadds CFLUSH(int cid, int q)
{
    start(10); arg(cid); arg(q); called();
    return ret(M_CFLUSH(cid, q));
}

LPVOID FAR PASCAL __loadds CEVT(int cid, WORD mask)
{
    LPVOID r;
    start(11); arg(cid); arg(mask); called();
    r = M_CEVT(cid, mask);
    retd((DWORD)r);
    return r;
}

WORD FAR PASCAL __loadds CEVTGET(int cid, WORD mask)
{
    start(12); arg(cid); arg(mask); called();
    return ret(M_CEVTGET(cid, mask));
}

int FAR PASCAL __loadds CSETBRK(int cid)
{
    start(13); arg(cid); called();
    return ret(M_CSETBRK(cid));
}

int FAR PASCAL __loadds CCLRBRK(int cid)
{
    start(14); arg(cid); called();
    return ret(M_CCLRBRK(cid));
}

LPVOID FAR PASCAL __loadds GETDCB(int cid)
{
    LPVOID r;
    start(15); arg(cid); called();
    r = M_GETDCB(cid);
    retd((DWORD)r);
    return r;
}

int FAR PASCAL __loadds WEP(int x)
{
    (void)x;
    return 1;
}

int FAR PASCAL __loadds SUSPENDOPENCOMMPORTS(void)
{
    start(17); called();
    return ret(M_SUSPEND());
}

int FAR PASCAL __loadds REACTIVATEOPENCOMMPORTS(void)
{
    start(18); called();
    return ret(M_REACTIVATE());
}

int FAR PASCAL __loadds COMMWRITESTRING(int cid, LPSTR s, int len)
{
    start(19); arg(cid); parg(s); arg(len); called();
    return ret(M_COMMWRITESTRING(cid, s, len));
}

int FAR PASCAL __loadds READCOMMSTRING(int cid, LPSTR s, int len)
{
    start(20); arg(cid); parg(s); arg(len); called();
    return ret(M_READCOMMSTRING(cid, s, len));
}

BOOL FAR PASCAL __loadds ENABLENOTIFICATION(int cid, HWND h, int in, int out)
{
    start(100); arg(cid); arg((WORD)h); arg(in); arg(out); called();
    return ret(M_ENABLENOTIFICATION(cid, h, in, out));
}
