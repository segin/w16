/*
 * mmp.c - MMP.EXE, the MMSYSTEM probe, layer 1 (ARCHITECTURE.md 23;
 * docs/notes/mmsystem-handover.md): MMSYSTEM's core as an application,
 * a driver and a library see it. Run as the shell (tests/mmsystem/mrun.sh)
 * on the reference 3.1 with Microsoft's MMSYSTEM.DLL and with w16's; the
 * logs (C:\MMP.LOG) must be the same. Its library MMC.DLL
 * (tests/mmsystem/mmc.c, mmcs.asm) holds the callbacks and logs from the
 * task mmTaskCreate starts.
 *
 * Sections ("== NAME", the letter in C:\MMP.ARG that selects it; default
 * "vdctskop"): ver [v] (mmsystemGetVersion), drivers [d] (the drivers open
 * at boot, MMSYSTEM's DriverProc through USER, the Drv* functions), dcb
 * [c] (DriverCallback: every type, bad targets, the stack and registers a
 * function gets), time [t] (timeSetEvent's argument checks, a callback's
 * context, timeGetSystemTime's sizes), stack [s] (StackEnter and
 * StackLeave, nested, with marker registers), task [k] (mmGetCurrentTask,
 * mmTaskSignal, mmTaskBlock, mmTaskYield, mmTaskCreate and the task it
 * starts), ods [o] (what OutputDebugStr does, seen from INT 21h and INT
 * 41h), pops [p] (how many bytes of arguments the undocumented
 * MMDRVINSTALL and JOYSETCALIBRATION take; MCIPARSECOMMAND faults on
 * 3.1 with zeros), prestack [S] (StackEnter before anything made the interrupt
 * stack: run alone); the MMIO sections F, D, M, B and E (mmio.c's header;
 * not in the default run); the device sections n, e, w, i, m, j, a, y, x
 * and b (layer 3: mmdev.c's header; not in the default run).
 *
 * Sources: Windows 3.1 SDK, Multimedia Programmer's Guide (the timer
 * services), Programmer's Reference Vol. 1 ch. 25 and Vol. 2 (installable
 * drivers), the argument lists of DriverCallback and the mm* task
 * functions from Wine's MMSYSTEM of before 2002 (X11 licence).
 */
#include <windows.h>
#include <mmsystem.h>
#include <toolhelp.h>
#include "mmc.h"

/* DriverCallback's flags: Wine's mmsystem.h of before 2002 (X11 licence) */
#define DCB_NULL      0
#define DCB_WINDOW    1
#define DCB_TASK      2
#define DCB_FUNCTION  3
#define DCB_NOSWITCH  8

extern WORD getss(void);
#pragma aux getss = "mov ax, ss" value [ax];
extern WORD getsp(void);
#pragma aux getsp = "mov ax, sp" value [ax];

extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */
extern void mmio_sections(char c);              /* mmio.c */
extern void dev_sections(char c);               /* mmdev.c */
extern void mci_section(void);                  /* mmci.c */
struct mmcstate FAR *dev_st;                    /* for mmdev.c */
HWND dev_hwnd;
FARPROC dev_cb;
extern BOOL FAR PASCAL MM_DRIVERCALLBACK(DWORD cb, WORD flags, WORD dev, WORD msg,
                                         DWORD user, DWORD dw1, DWORD dw2); /* .31 */
extern WORD FAR PASCAL MM_TASKCREATE(FARPROC f, WORD FAR *ph, DWORD inst); /* .900 */
extern WORD FAR PASCAL MM_TASKBLOCK(WORD h);    /* .902 */
extern BOOL FAR PASCAL MM_TASKSIGNAL(WORD h);   /* .903 */
extern WORD FAR PASCAL MM_GETCURRENTTASK(void); /* .904 */
extern void FAR PASCAL MM_TASKYIELD(void);      /* .905 */
extern HDRVR FAR PASCAL MM_DRVOPEN(LPCSTR, LPCSTR, LPARAM);       /* .1100 */
extern LRESULT FAR PASCAL MM_DRVCLOSE(HDRVR, LPARAM, LPARAM);     /* .1101 */
extern LRESULT FAR PASCAL MM_DRVSENDMESSAGE(HDRVR, UINT, LPARAM, LPARAM); /* .1102 */
extern HINSTANCE FAR PASCAL MM_DRVGETMODULEHANDLE(HDRVR);         /* .1103 */
extern LRESULT FAR PASCAL MM_DRVDEFDRIVERPROC(DWORD, HDRVR, UINT, LPARAM, LPARAM); /* .1104 */

static const char logname[] = "C:\\MMP.LOG";
char line[600];
static char args[40];
static struct mmcstate FAR *st;
static HWND hwnd;
static HINSTANCE hinst;
static FARPROC cb, taskfn, stackfn, hookfn;
static WORD callsp;

void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

static const char *modof(WORD sel)
{
    static char b[4][20];
    static int k;
    WORD m = sel ? K_GETEXEPTR(sel) : 0;
    MODULEENTRY me;
    char *r = b[k++ & 3];
    if (!m)
        return "0";
    me.dwSize = sizeof me;
    if (!ModuleFindHandle(&me, (HMODULE)m))
        return "?";
    lstrcpy(r, me.szModule);
    return r;
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        if (!m.hwnd)
            lg("  task message %04X wParam %04X lParam %08lX", m.message, m.wParam, m.lParam);
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

LRESULT FAR PASCAL __export MmpWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= WM_USER) {
        lg("  window message %04X wParam %04X lParam %08lX", msg, wp, lp);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---- ver ---- */
static void sec_ver(void)
{
    lg("mmsystemGetVersion = %04X", mmsystemGetVersion());
}

/* ---- drivers ---- */
static const char *drvname(HDRVR h)
{
    static char b[4][40];
    static int k;
    DRIVERINFOSTRUCT d;
    char *r = b[k++ & 3];
    d.length = sizeof d;
    if (!GetDriverInfo(h, &d))
        return "?";
    wsprintf(r, "%s/%s", (LPSTR)modof((WORD)d.hModule), (LPSTR)d.szAliasName);
    return r;
}

static HDRVR mmsysdrv(void)
{
    HDRVR h = 0;
    int k;
    for (k = 0; k < 30; k++) {
        h = GetNextDriver(h, 0);
        if (!h)
            break;
        if (!lstrcmp(modof((WORD)GetDriverModuleHandle(h)), "MMSYSTEM"))
            return h;
    }
    return 0;
}

static void sec_drivers(void)
{
    static const UINT msgs[] = { 0, DRV_QUERYCONFIGURE, DRV_INSTALL, DRV_POWER, DRV_EXITAPPLICATION,
                                 0x000C, 0x000D, 0x0010, 0x0800, 0x0801, 0x0808, 0x0BFF,
                                 0x0C00, 0x4000, 0x4001, 0x7FFF };
    HDRVR h = 0, m, t;
    char *p = line;
    int k;
    p += wsprintf(p, "open:");
    for (k = 0; k < 30; k++) {
        h = GetNextDriver(h, 0);
        if (!h)
            break;
        p += wsprintf(p, " %04X %s", (WORD)h, (LPSTR)drvname(h));
    }
    lg("%s", (LPSTR)line);
    m = mmsysdrv();
    for (k = 0; k < sizeof msgs / sizeof msgs[0]; k++) {
        LRESULT r1 = SendDriverMessage(m, msgs[k], 0x11112222L, 0x33334444L);
        LRESULT r2 = MM_DRVSENDMESSAGE(m, msgs[k], 0, 0);
        lg("MMSYSTEM DriverProc %04X: SendDriverMessage %08lX, DrvSendMessage %08lX",
           msgs[k], r1, r2);
    }
    lg("DrvGetModuleHandle(%04X) = %s, (0) = %04X, (99) = %04X", (WORD)m,
       (LPSTR)modof((WORD)MM_DRVGETMODULEHANDLE(m)), (WORD)MM_DRVGETMODULEHANDLE(0),
       (WORD)MM_DRVGETMODULEHANDLE((HDRVR)99));
    for (k = 0; k < 12; k++)
        lg("DrvDefDriverProc(0, %04X, %d) = %08lX, (0, 99, %d) = %08lX", (WORD)m, k,
           MM_DRVDEFDRIVERPROC(0, m, k, 0, 0), k, MM_DRVDEFDRIVERPROC(0, (HDRVR)99, k, 0, 0));
    t = MM_DRVOPEN("timer", NULL, 0);
    lg("DrvOpen(timer) = %04X %s", (WORD)t, (LPSTR)drvname(t));
    lg("DrvSendMessage(timer, 080C, NULL, 0) = %08lX", MM_DRVSENDMESSAGE(t, 0x080C, 0, 0));
    lg("DrvClose = %08lX", MM_DRVCLOSE(t, 0, 0));
    t = MM_DRVOPEN("nosuchdrv", NULL, 0);
    lg("DrvOpen(nosuchdrv) = %04X", (WORD)t);
}

/* ---- dcb ---- */
static void dumprec(int i)
{
    struct mmcrec FAR *r = &st->rec[i];
    WORD myss;
    myss = getss();
    lg("  cb %d: h %04X msg %04X user %04X%04X dw1 %04X%04X dw2 %04X%04X", i,
       r->w[21], r->w[20], r->w[19], r->w[18], r->w[17], r->w[16], r->w[15], r->w[14]);
    lg("  cb %d: ax %04X bx %04X cx %s dx %04X si %04X di %04X ds %s es %s flags %04X ret %s task %s",
       i, r->w[9], r->w[6],
       (LPSTR)(r->w[8] == r->ss ? "=ss" : r->w[8] == r->w[1] ? "=ds" : "other"),
       r->w[7], r->w[3], r->w[2],
       (LPSTR)modof(r->w[1]), (LPSTR)(r->w[0] == r->ss ? "=ss" : modof(r->w[0])), r->w[10],
       (LPSTR)modof(r->w[13]),
       (LPSTR)(r->task == (WORD)GetCurrentTask() ? "probe" : "other"));
    if (r->ss == myss)
        lg("  cb %d: on the caller's stack, %04X below its SP", i, callsp - (r->w[5] + 6));
    else
        lg("  cb %d: ss size %lX owner %s sp %04X", i, GlobalSize((HGLOBAL)r->ss),
           (LPSTR)modof(r->ss), r->w[5] + 6);
}

static void dcb(const char *what, DWORD target, WORD flags)
{
    WORD before = st->nrec;
    BOOL r;
    MSG m;
    WORD mysp;
    mysp = getsp();
    callsp = mysp;
    r = MM_DRIVERCALLBACK(target, flags, 0x1234, WM_USER + 1, 0xAAAA5555L, 0x11112222L,
                          0x33334444L);
    lg("DriverCallback(%s, flags %04X) = %d, calls %d", (LPSTR)what, flags, r,
       st->nrec - before);
    if (st->nrec > before)
        dumprec(before);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        lg("  posted: %s message %04X wParam %04X lParam %08lX",
           (LPSTR)(m.hwnd == hwnd ? "window" : m.hwnd ? "other window" : "task"),
           m.message, m.wParam, m.lParam);
}

static void sec_dcb(void)
{
    static const WORD fl[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x13, 0x0103, 0x8003 };
    int k;
    char b[40];
    for (k = 0; k < sizeof fl / sizeof fl[0]; k++) {
        wsprintf(b, "flags %04X: window", fl[k]);
        dcb(b, (DWORD)(WORD)hwnd, fl[k]);
    }
    dcb("task", (DWORD)(WORD)GetCurrentTask(), DCB_TASK);
    dcb("function", (DWORD)cb, DCB_FUNCTION);
    dcb("function, noswitch", (DWORD)cb, DCB_FUNCTION | DCB_NOSWITCH);
    dcb("window 0", 0, DCB_WINDOW);
    dcb("window 1234", 0x1234, DCB_WINDOW);
    dcb("window high word", MAKELONG(hwnd, 0x5678), DCB_WINDOW);
    dcb("task 0", 0, DCB_TASK);
    dcb("function NULL", 0, DCB_FUNCTION);
    dcb("function in data", (DWORD)(LPVOID)line, DCB_FUNCTION);
    dcb("function 0001:0000", 0x00010000L, DCB_FUNCTION);
}

/* ---- time ---- */
static void sec_time(void)
{
    MMTIME t;
    UINT r, id;
    int k;
    WORD before;
    DWORD w;
    for (k = 0; k <= 12; k += 1) {
        { int i; for (i = 0; i < (int)sizeof t; i++) ((BYTE *)&t)[i] = 0xEE; }
        r = timeGetSystemTime(&t, k);
        lg("timeGetSystemTime(size %d) = %u, wType %04X", k, r, t.wType);
    }
    id = timeSetEvent(10, 0, NULL, 0, TIME_ONESHOT);
    lg("timeSetEvent(NULL callback) = %04X", id);
    if (id)
        lg("timeKillEvent = %u", timeKillEvent(id));
    id = timeSetEvent(10, 0, (LPTIMECALLBACK)(LPVOID)line, 0, TIME_ONESHOT);
    lg("timeSetEvent(data callback) = %04X", id);
    if (id)
        lg("timeKillEvent = %u", timeKillEvent(id));
    id = timeSetEvent(10, 0, (LPTIMECALLBACK)0x00010000L, 0, TIME_ONESHOT);
    lg("timeSetEvent(0001:0000 callback) = %04X", id);
    if (id)
        lg("timeKillEvent = %u", timeKillEvent(id));
    before = st->nrec;
    id = timeSetEvent(10, 0, (LPTIMECALLBACK)cb, 0x5A5A0001L, TIME_ONESHOT);
    lg("timeSetEvent(10 ms, MMC) = %s", (LPSTR)(id ? "id" : "0"));
    w = GetTickCount();
    while (st->nrec == before && GetTickCount() - w < 2000)
        ;
    lg("callbacks %d", st->nrec - before);
    if (st->nrec > before) {
        dumprec(before);
    }
}

/* ---- stack ---- */
static void sec_stack(void)
{
    static const char *step[] = { "before", "StackEnter", "StackEnter again",
                                  "StackLeave", "StackLeave again" };
    WORD FAR *s = st->stk;
    WORD myss;
    int k;
    myss = getss();
    ((void (FAR PASCAL *)(void))stackfn)();
    for (k = 0; k < 5; k++, s += 12)
        lg("  %s: ss %s size %lX owner %s sp %04X (probe's: below the first) flags %04X ax %04X bx %04X cx %04X dx %04X si %04X di %04X bp %04X ds %s es %s",
           (LPSTR)step[k], (LPSTR)(s[0] == myss ? "probe" : "other"),
           s[0] == myss ? 0L : GlobalSize((HGLOBAL)s[0]),
           (LPSTR)modof(s[0]), s[0] == myss ? st->stk[1] - s[1] : s[1], s[2], s[3], s[4], s[5], s[6], s[7],
           s[8], s[9], (LPSTR)modof(s[10]), (LPSTR)modof(s[11]));
}

/* ---- task ---- */
static void sec_task(void)
{
    MSG m;
    WORD h = 0, r;
    BOOL b;
    int k;
    DWORD w;
    lg("mmGetCurrentTask %s", (LPSTR)(MM_GETCURRENTTASK() == (WORD)GetCurrentTask() ? "= GetCurrentTask" : "other"));
    b = MM_TASKSIGNAL((WORD)GetCurrentTask());
    lg("mmTaskSignal(self) = %d", b);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        lg("  posted: %s message %04X wParam %04X lParam %08lX",
           (LPSTR)(m.hwnd ? "window" : "task"), m.message, m.wParam, m.lParam);
    lg("mmTaskSignal(0) = %d", MM_TASKSIGNAL(0));
    lg("mmTaskSignal(1234) = %d", MM_TASKSIGNAL(0x1234));
    PostMessage(hwnd, WM_USER + 7, 7, 7);
    PostAppMessage(GetCurrentTask(), WM_USER + 8, 8, 8);
    MM_TASKSIGNAL((WORD)GetCurrentTask());
    PostMessage(hwnd, WM_USER + 9, 9, 9);
    lg("mmTaskBlock(self), with a window message, a task message, the signal and another window message queued:");
    r = MM_TASKBLOCK((WORD)GetCurrentTask());
    lg("mmTaskBlock = %04X; left:", r);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        lg("  queued: %s message %04X wParam %04X lParam %08lX",
           (LPSTR)(m.hwnd ? "window" : "task"), m.message, m.wParam, m.lParam);
    MM_TASKYIELD();
    lg("mmTaskYield returned");
    st->taskdone = 0;
    st->parent = (WORD)GetCurrentTask();
    r = MM_TASKCREATE(taskfn, &h, 0x12345678L);
    lg("mmTaskCreate = %04X, task %s", r, (LPSTR)(h && IsTask((HTASK)h) ? "valid" : h ? "not a task" : "0"));
    w = GetTickCount();
    for (k = 0; !st->taskblock && !st->taskdone && GetTickCount() - w < 5000; k++)
        Yield();
    lg("the task is %s", (LPSTR)(st->taskblock ? "blocked" : st->taskdone ? "done" : "not seen"));
    Yield();
    Yield();
    lg("mmTaskSignal(task) = %d", MM_TASKSIGNAL(h));
    w = GetTickCount();
    while (!st->taskdone && GetTickCount() - w < 5000)
        Yield();
    for (k = 0; k < 10; k++)
        Yield();
    lg("after its function returned: %s", (LPSTR)(IsTask((HTASK)h) ? "still a task" : "gone"));
    r = MM_TASKCREATE(taskfn, NULL, 0x0BADL);
    lg("mmTaskCreate(NULL handle pointer) = %04X", r);
    w = GetTickCount();
    while (!st->taskblock && GetTickCount() - w < 3000)
        Yield();
    if (st->taskblock) {
        /* the task waits for a signal: find it and signal it */
        TASKENTRY te;
        te.dwSize = sizeof te;
        if (TaskFirst(&te))
            do {
                if (!lstrcmpi(te.szModule, "MMTASK"))
                    MM_TASKSIGNAL((WORD)te.hTask);
            } while (TaskNext(&te));
        w = GetTickCount();
        while (st->taskblock && GetTickCount() - w < 3000)
            Yield();
    }
    /* a signal sent at once, before the new task's function has run
       (MCIWAVE signals its task right after mmTaskCreate): it must wait
       in the task's queue for its first mmTaskBlock */
    st->taskdone = 0;
    st->taskmsg = 0;
    h = 0;
    r = MM_TASKCREATE(taskfn, &h, 0x51674E41L);
    b = MM_TASKSIGNAL(h);
    lg("mmTaskCreate = %04X, then at once mmTaskSignal = %s", r, (LPSTR)(b ? "nonzero" : "0"));
    w = GetTickCount();
    while (!st->taskdone && GetTickCount() - w < 3000)
        Yield();
    if (st->taskdone)
        lg("the task's mmTaskBlock returned %04X", st->taskmsg);
    else {
        lg("the task's mmTaskBlock did not return in 3 s");
        MM_TASKSIGNAL(h);
        w = GetTickCount();
        while (!st->taskdone && GetTickCount() - w < 3000)
            Yield();
    }
    for (k = 0; k < 10; k++)
        Yield();
    pump();
}

/* ---- pops: how many bytes of arguments the undocumented exports pop
   (10 zero words pushed) ---- */
static void sec_pops(void)
{
    static const WORD ords[] = { 34, 109 };     /* 704 faults on 3.1 */
    typedef DWORD (FAR PASCAL *POPS)(FARPROC, WORD);
    POPS pops = (POPS)GetProcAddress(GetModuleHandle("MMC"), "MMCPOPS");
    HMODULE mm = GetModuleHandle("MMSYSTEM");
    int k;
    for (k = 0; k < 2; k++) {
        DWORD r = pops(GetProcAddress(mm, MAKEINTRESOURCE(ords[k])), 10);
        lg("MMSYSTEM.%u pops %u bytes, answers %04X", ords[k], LOWORD(r), HIWORD(r));
    }
}

/* ---- ods ---- */
static void sec_ods(void)
{
    int k;
    char *p;
    ((void (FAR PASCAL *)(WORD))hookfn)(1);
    OutputDebugStr("MMP\r\n");
    ((void (FAR PASCAL *)(WORD))hookfn)(0);
    p = line;
    p += wsprintf(p, "OutputDebugStr: INT 21h %d:", *st->n21);
    for (k = 0; k < *st->n21 && k < 16; k++)
        p += wsprintf(p, " %04X", st->ax21[k]);
    p += wsprintf(p, "; INT 41h %d:", *st->n41);
    for (k = 0; k < *st->n41 && k < 16; k++)
        p += wsprintf(p, " %04X", st->ax41[k]);
    *p++ = '\r';
    *p++ = '\n';
    put(line, p - line);
    ((void (FAR PASCAL *)(WORD))hookfn)(1);
    OutputDebugString("MMP\r\n");
    ((void (FAR PASCAL *)(WORD))hookfn)(0);
    lg("OutputDebugString (KERNEL), for comparison: INT 21h %d, INT 41h %d", *st->n21, *st->n41);
}

extern WORD __cdecl entry_regs[11];  /* start.asm: ax bx cx dx si di bp sp ds es ss */

int __cdecl __far tmain(void)
{
    HINSTANCE hi = (HINSTANCE)entry_regs[5];   /* DI: the instance */
    WNDCLASS wc;
    HINSTANCE mmc;
    HFILE f;
    const char *a = "vdctskop";
    int n;
    hinst = hi;
    {
        /* USER's InitApp (USER.5), as a program's start-up code calls it:
           the boot drivers ([boot] drivers=) are opened at the shell's */
        typedef BOOL (FAR PASCAL *INITAPP)(HINSTANCE);
        INITAPP ia = (INITAPP)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
        ia(hi);
    }
    f = _lopen("C:\\MMP.ARG", OF_READ);
    if (f != HFILE_ERROR) {
        n = _lread(f, args, sizeof args - 1);
        _lclose(f);
        if (n > 0) {
            args[n] = 0;
            a = args;
        }
    }
    lg("MMP start: sections %s", (LPSTR)a);
    mmc = LoadLibrary("MMC.DLL");
    if ((UINT)mmc < 32) {
        lg("MMC.DLL not loaded (%u)", (UINT)mmc);
        return 0;
    }
    st = ((MMCSTATEPROC)GetProcAddress(mmc, "MMCSTATE"))();
    cb = GetProcAddress(mmc, "MMCCALLBACK");
    taskfn = GetProcAddress(mmc, "MMCTASK");
    stackfn = GetProcAddress(mmc, "MMCSTACK");
    hookfn = GetProcAddress(mmc, "MMCHOOK");
    wc.style = 0;
    wc.lpfnWndProc = MmpWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MmpWnd";
    RegisterClass(&wc);
    hwnd = CreateWindow("MmpWnd", "MMP", WS_OVERLAPPED, 0, 0, 100, 50, 0, 0, hi, NULL);
    dev_st = st;
    dev_hwnd = hwnd;
    dev_cb = cb;
    pump();
    for (; *a; a++) {
        switch (*a) {
        case 'v': lg("== ver"); sec_ver(); break;
        case 'd': lg("== drivers"); sec_drivers(); break;
        case 'c': lg("== dcb"); sec_dcb(); break;
        case 't': lg("== time"); sec_time(); break;
        case 's': lg("== stack"); sec_stack(); break;
        case 'S': lg("== prestack"); sec_stack(); break;
        case 'k': lg("== task"); sec_task(); break;
        case 'o': lg("== ods"); sec_ods(); break;
        case 'p': lg("== pops"); sec_pops(); break;
        case 'F': case 'D': case 'M': case 'B': case 'E': mmio_sections(*a); break;
        case 'n': case 'e': case 'w': case 'i': case 'm': case 'j': case 'a': case 'y':
        case 'x': case 'b': case 'l': case 'z': dev_sections(*a); break;
        case 'Q': lg("== mci"); mci_section(); break;
        }
        pump();
    }
    lg("MMP end");
    DestroyWindow(hwnd);
    return 0;
}
