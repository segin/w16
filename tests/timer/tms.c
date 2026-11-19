/*
 * tms.c - TMS.DRV, a logging pass-through timer driver (ARCHITECTURE.md
 * 21; docs/notes/timer-handover.md). SYSTEM.INI [drivers] timer=tms.drv
 * puts it where MMSYSTEM opens its timer driver; it loads the real
 * TIMER.DRV (Microsoft's or w16's) as a library and hands every DriverProc
 * call on to it unchanged, recording the call: message, identifier,
 * handle, both parameters, the 16 bytes behind each parameter that is a
 * readable far pointer (before and after the call), the result, whether
 * interrupts were enabled, the stack, the caller (USER's
 * SendDriverMessage or MMSYSTEM directly) and the nesting depth.
 *
 * This is black-box observation between two Microsoft modules
 * (ARCHITECTURE.md 3): only the values that pass the interface are
 * recorded, nothing of either module's code.
 *
 * The records are kept in this driver's fixed data segment (calls come at
 * interrupt time too); the probe TPR.EXE (tests/timer/tpr.c) asks for
 * them as text with TmsDump, which appends them to its log. What comes
 * after the probe's last dump (Windows' end) is appended to C:\TPR.LOG
 * by DRV_FREE itself.
 */
#include <windows.h>

extern WORD tms_cli(void);
#pragma aux tms_cli = "pushf" "pop ax" "cli" value [ax];
extern void tms_sti(WORD fl);
#pragma aux tms_sti = "push ax" "popf" parm [ax];

typedef LRESULT (FAR PASCAL *DRVPROC)(DWORD, HDRVR, UINT, LPARAM, LPARAM);

extern WORD __cdecl __far tms_peek(const void __far *src, void __far *dst, WORD n);
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */

#define NREC 260
#define PEEK 16

struct args { DWORD l2, l1; WORD msg; WORD h; DWORD id; };

struct trec {
    WORD seq, msg, h, flags, ss, sp, retcs, task;
    DWORD id, l1, l2, r;
    BYTE depth, n1, n2, pad;
    BYTE b1[PEEK], a1[PEEK], b2[PEEK], a2[PEEK];
};

static struct trec recs[NREC];
static WORD nrec, seq, lost, depth;
static WORD quiet;              /* a message counted, not recorded */
static DWORD nquiet;
static HINSTANCE real;
static DRVPROC realproc;
static char line[400];
static const char logname[] = "C:\\TPR.LOG";
static HTASK probetask;

void FAR PASCAL __loadds TmsDumpTo(LPCSTR name);

static int differ(const BYTE *a, const BYTE *b, int n)
{
    while (n--)
        if (*a++ != *b++)
            return 1;
    return 0;
}

int __cdecl libmain(void)
{
    return 1;
}

static int load(void)
{
    if (realproc)
        return 1;
    real = LoadLibrary("TIMER.DRV");
    if ((WORD)real < 32) {
        real = 0;
        return 0;
    }
    realproc = (DRVPROC)GetProcAddress(real, "DriverProc");
    return realproc != 0;
}

DWORD __cdecl __far tms_proc(WORD flags, WORD ss, WORD sp, WORD retip,
                             WORD retcs, struct args __far *a)
{
    struct trec *t = 0;
    DWORD r;
    (void)retip;
    ++depth;
    if (a->msg == quiet && quiet)
        nquiet++;
    else if (nrec < NREC) {
        t = &recs[nrec++];
        t->seq = ++seq;
        t->msg = a->msg;
        t->h = a->h;
        t->id = a->id;
        t->l1 = a->l1;
        t->l2 = a->l2;
        t->flags = flags;
        t->ss = ss;
        t->sp = sp;
        t->retcs = retcs;
        t->task = (flags & 0x200) ? (WORD)GetCurrentTask() : 0;
        t->depth = (BYTE)depth;
        t->n1 = (BYTE)tms_peek((void __far *)a->l1, t->b1, PEEK);
        t->n2 = (BYTE)tms_peek((void __far *)a->l2, t->b2, PEEK);
    } else {
        ++seq;
        ++lost;
    }
    r = load() ? realproc(a->id, (HDRVR)a->h, a->msg, a->l1, a->l2) : 0;
    if (t) {
        t->r = r;
        if (t->n1)
            tms_peek((void __far *)a->l1, t->a1, PEEK);
        if (t->n2)
            tms_peek((void __far *)a->l2, t->a2, PEEK);
    }
    if (a->msg == DRV_FREE)
        TmsDumpTo(logname);
    --depth;
    return r;
}

static void put(HFILE f, const char *s, int n)
{
    _lwrite(f, s, n);
}

static int hex(char *p, const BYTE *b, int n)
{
    static const char x[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < n; i++) {
        *p++ = x[b[i] >> 4];
        *p++ = x[b[i] & 15];
    }
    return 2 * n;
}

/* the caller of DriverProc: USER (SendDriverMessage) or MMSYSTEM itself */
static LPSTR who(WORD cs)
{
    WORD m = K_GETEXEPTR(cs);
    if (m && m == (WORD)GetModuleHandle("USER"))
        return "USER";
    if (m && m == (WORD)GetModuleHandle("MMSYSTEM"))
        return "MMSYSTEM";
    if (m && m == (WORD)GetModuleHandle("TPR"))
        return "TPR";
    return "other";
}

/* appends the records so far to the file and forgets them */
void FAR PASCAL __loadds TmsDumpTo(LPCSTR name)
{
    HFILE f = _lopen(name, OF_WRITE);
    WORD i;
    if (f == HFILE_ERROR)
        f = _lcreat(name, 0);
    else
        _llseek(f, 0, 2);
    if (f == HFILE_ERROR)
        return;
    for (i = 0; i < nrec; i++) {
        struct trec *t = &recs[i];
        int n = wsprintf(line, "  TMS msg=%04X id=%08lX h=%04X l1=%08lX l2=%08lX -> %08lX"
                         " if=%d d=%d from=%s task=%s",
                         t->msg, t->id, t->h, t->l1, t->l2, t->r,
                         (t->flags & 0x200) != 0, t->depth, who(t->retcs),
                         !t->task ? (LPSTR)"-" :
                         (HTASK)t->task == probetask ? (LPSTR)"probe" : (LPSTR)"other");
        if (t->n1) {
            n += wsprintf(line + n, " [l1 ");
            n += hex(line + n, t->b1, PEEK);
            if (differ(t->b1, t->a1, PEEK)) {
                n += wsprintf(line + n, " -> ");
                n += hex(line + n, t->a1, PEEK);
            }
            line[n++] = ']';
        }
        if (t->n2) {
            n += wsprintf(line + n, " [l2 ");
            n += hex(line + n, t->b2, PEEK);
            if (differ(t->b2, t->a2, PEEK)) {
                n += wsprintf(line + n, " -> ");
                n += hex(line + n, t->a2, PEEK);
            }
            line[n++] = ']';
        }
        line[n++] = '\r';
        line[n++] = '\n';
        put(f, line, n);
    }
    if (lost) {
        int n = wsprintf(line, "  TMS lost %u\r\n", lost);
        put(f, line, n);
    }
    {
        /* a record made at interrupt time after the loop's last look (a
           callback's call) stays for the next dump: clearing them all
           here could lose one */
        WORD k, fl = tms_cli();
        for (k = 0; i + k < nrec; k++)
            recs[k] = recs[i + k];
        nrec = k;
        lost = 0;
        nquiet = 0;
        tms_sti(fl);
    }
    _lclose(f);
}

/* the message to count only (0: record everything), the probe's task */
void FAR PASCAL __loadds TmsSet(WORD q, HTASK task)
{
    quiet = q;
    probetask = task;
}

/* the records since the last dump, without printing them */
WORD FAR PASCAL __loadds TmsCount(void)
{
    return nrec;
}
