/*
 * mmc.c - MMC.DLL, the MMSYSTEM probe's library (tests/mmsystem/mmp.c):
 * the callback DriverCallback and timeSetEvent call, the task function
 * mmTaskCreate starts (which logs into the probe's log itself), and the
 * StackEnter and INT 21h / 41h recorders of mmcs.asm. Code and data
 * fixed: callbacks may come at interrupt time.
 */
#include <windows.h>
#include <toolhelp.h>
#include "mmc.h"

extern WORD __cdecl stk[60];
extern WORD __cdecl n21, n41, ax21[16], ax41[16];
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */
extern WORD FAR PASCAL K_GETCURRENTPDB(void);   /* KERNEL.37 */
extern WORD FAR PASCAL MM_TASKBLOCK(WORD h);    /* MMSYSTEM.902 */
extern WORD FAR PASCAL MM_GETCURRENTTASK(void); /* MMSYSTEM.904 */

static struct mmcstate state;

int __cdecl libmain(void)
{
    return 1;
}
static char line[400];

static void mlg(const char *fmt, ...)
{
    HFILE f;
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    f = _lopen("C:\\MMP.LOG", OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\MMP.LOG", 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, line, n);
    _lclose(f);
}

void __cdecl __far mmc_cb(WORD __far *frame)
{
    int i;
    if (state.nrec < MMC_NREC) {
        struct mmcrec *r = &state.rec[state.nrec];
        for (i = 0; i < 22; i++)
            r->w[i] = frame[i];
        r->task = (WORD)GetCurrentTask();
        r->ss = (WORD)((DWORD)frame >> 16);
    }
    state.nrec++;
}

static const char *modof(WORD sel)
{
    static char b[20];
    WORD m = sel ? K_GETEXEPTR(sel) : 0;
    MODULEENTRY me;
    if (!m)
        return "0";
    me.dwSize = sizeof me;
    if (!ModuleFindHandle(&me, (HMODULE)m))
        return "?";
    lstrcpy(b, me.szModule);
    return b;
}

void __cdecl __far mmc_task(WORD __far *frame)
{
    TASKENTRY te;
    WORD pdb;
    BYTE FAR *p;
    char *q;
    char buf[200];
    int i, n;
    WORD me;
    mlg("  task: dwInst %04X%04X regs ax %04X bx %04X cx %04X dx %04X si %04X di %04X bp0 %04X flags %04X",
        frame[15], frame[14], frame[9], frame[6], frame[8], frame[7], frame[3], frame[2],
        frame[11], frame[10]);
    mlg("  task: ret to %s, sp %04X, beyond: %04X %04X %04X %04X, ds %s es %s",
        (LPSTR)modof(frame[13]), frame[5] + 6, frame[16], frame[17], frame[18], frame[19],
        (LPSTR)modof(frame[1]), (LPSTR)modof(frame[0]));
    te.dwSize = sizeof te;
    if (TaskFindHandle(&te, GetCurrentTask()))
        mlg("  task: module %s, parent %s, stack %04X-%04X min %04X, queue %s",
            (LPSTR)te.szModule,
            (LPSTR)((WORD)te.hTaskParent == state.parent ? "the probe" : "other"),
            te.wStackTop, te.wStackBottom, te.wStackMinimum,
            (LPSTR)(te.hQueue ? "yes" : "no"));
    pdb = K_GETCURRENTPDB();
    p = (BYTE FAR *)MAKELP(pdb, 0x80);
    n = p[0];
    q = buf;
    q += wsprintf(q, "  task: command tail length %d:", n);
    for (i = 1; i <= n && i < 40; i++)
        q += wsprintf(q, " %02X", p[i]);
    mlg("%s", (LPSTR)buf);
    me = MM_GETCURRENTTASK();
    {
        /* the message queue's size: task messages posted until one fails */
        MSG m;
        for (n = 0; n < 200 && PostAppMessage(GetCurrentTask(), WM_USER + 100, n, 0); n++)
            ;
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            ;
        mlg("  task: queue holds %d", n);
    }
    {
        /* its data segment (where mmTaskCreate's values must be): the
           words after KERNEL's 16 bytes, as the function's DS shows them */
        WORD FAR *d = (WORD FAR *)MAKELP(frame[1], 0);
        q = buf;
        q += wsprintf(q, "  task: data from 10h:");
        for (i = 8; i < 14; i++)
            q += wsprintf(q, " %04X", d[i]);
        mlg("%s", (LPSTR)buf);
    }
    mlg("  task: mmGetCurrentTask %s", (LPSTR)(me == (WORD)GetCurrentTask() ? "= GetCurrentTask" : "other"));
    state.taskblock = 1;
    i = MM_TASKBLOCK(me);
    state.taskblock = 0;
    mlg("  task: mmTaskBlock returned %04X", i);
    state.taskdone = 1;
}

/* ---- the "TST " I/O procedure (MMP's MMIO sections): a file of up to
   4 KB in this library's data, every message logged as it comes, with
   the MMIOINFO fields an I/O procedure owns ---- */
static BYTE disk[4096];
static long disklen;
static int iolog = 1;

typedef struct {
    DWORD dwFlags, fccIOProc, pIOProc;
    WORD wErrorRet, htask;
    long cchBuffer;
    DWORD pchBuffer, pchNext, pchEndRead, pchEndWrite;
    long lBufOffset, lDiskOffset;
    DWORD adwInfo[3], dwReserved1, dwReserved2;
    WORD hmmio;
} MINFO;


LRESULT FAR PASCAL __loadds MMCIOPROC(LPSTR lpi, UINT msg, LPARAM l1, LPARAM l2)
{
    MINFO FAR *m = (MINFO FAR *)lpi;
    LRESULT r = 0;
    long n, i;
    BYTE FAR *p;
    if (iolog) {
        if (msg == 3 || msg == 6)
            mlg("    io msg %u l1 \"%s\" l2 %08lX: flags %08lX disk %ld info %08lX %08lX %08lX",
                msg, l1 ? (LPSTR)l1 : (LPSTR)"(null)", l2, m->dwFlags, m->lDiskOffset,
                m->adwInfo[0], m->adwInfo[1], m->adwInfo[2]);
        else
            mlg("    io msg %04X l1 %s%ld l2 %08lX: flags %08lX disk %ld info %08lX %08lX %08lX",
                msg, (LPSTR)(msg <= 1 || msg == 5 ? "(buffer) " : ""), msg <= 1 || msg == 5 ? 0L : l1, l2, m->dwFlags,
                m->lDiskOffset, m->adwInfo[0], m->adwInfo[1], m->adwInfo[2]);
        if (msg <= 1 || msg == 5) {
            /* where the buffer is, relative to MMIO's own buffer */
            if ((DWORD)l1 == m->pchBuffer)
                mlg("      at its buffer");
            else if (m->pchBuffer && SELECTOROF(l1) == SELECTOROF(m->pchBuffer))
                mlg("      at its buffer + %ld", (long)(OFFSETOF(l1) - OFFSETOF(m->pchBuffer)));
            else
                mlg("      at the caller's memory");
        }
    }
    switch (msg) {
    case 3:     /* MMIOM_OPEN */
        if (m->dwFlags & 0x1000L)
            disklen = 0;
        m->lDiskOffset = 0;
        m->adwInfo[0] = 0x0BADF00DL;
        r = 0;
        break;
    case 4:     /* MMIOM_CLOSE */
        r = 0;
        break;
    case 0:     /* MMIOM_READ */
        n = l2;
        if (m->lDiskOffset + n > disklen)
            n = disklen - m->lDiskOffset;
        if (n < 0)
            n = 0;
        p = (BYTE FAR *)l1;
        for (i = 0; i < n; i++)
            p[(WORD)i] = disk[(WORD)(m->lDiskOffset + i)];
        m->lDiskOffset += n;
        r = n;
        break;
    case 1:     /* MMIOM_WRITE */
    case 5:     /* MMIOM_WRITEFLUSH */
        n = l2;
        if (m->lDiskOffset + n > (long)sizeof disk)
            n = sizeof disk - m->lDiskOffset;
        p = (BYTE FAR *)l1;
        for (i = 0; i < n; i++)
            disk[(WORD)(m->lDiskOffset + i)] = p[(WORD)i];
        m->lDiskOffset += n;
        if (m->lDiskOffset > disklen)
            disklen = m->lDiskOffset;
        r = n;
        break;
    case 2:     /* MMIOM_SEEK */
        n = l2 == 0 ? l1 : l2 == 1 ? m->lDiskOffset + l1 : disklen + l1;
        if (n < 0 || n > (long)sizeof disk)
            r = -1;
        else
            r = m->lDiskOffset = n;
        break;
    case 6:     /* MMIOM_RENAME */
        r = 0;
        break;
    default:
        r = (msg >= 0x8000) ? l1 + l2 : 0;
        break;
    }
    if (iolog)
        mlg("      -> %ld, disk %ld", r, m->lDiskOffset);
    return r;
}

/* the I/O procedure's file: length and bytes, for the probe */
long FAR PASCAL __loadds MMCDISK(BYTE FAR * FAR *pb)
{
    *pb = disk;
    return disklen;
}

struct mmcstate FAR * FAR PASCAL __loadds MMCSTATE(void)
{
    state.stk = stk;
    state.n21 = &n21;
    state.n41 = &n41;
    state.ax21 = ax21;
    state.ax41 = ax41;
    return &state;
}
