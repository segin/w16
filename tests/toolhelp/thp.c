/*
 * thp.c - THP.EXE, the TOOLHELP.DLL probe (ARCHITECTURE.md 22): calls
 * every function of TOOLHELP.DLL in states the probe makes (its own
 * blocks, local heaps, classes, frames; a child task, THC.EXE, waiting,
 * faulting, ending; a library, THD.DLL, loaded and freed) and logs what
 * each call returned to C:\THP.LOG. Run as the shell of Windows 3.1 with
 * Microsoft's TOOLHELP.DLL and with w16's, and on w16's install tree
 * (tests/toolhelp/thrun.sh); the logs are compared section by section
 * (tests/toolhelp/thcmp.py).
 *
 * Log lines: "== X name" starts section X; "key: value" lines are
 * compared; values are symbolic where the number depends on where things
 * are (handles and selectors are named after what they are: "dg.s" the
 * probe's DGROUP selector, "M:USER" USER's module ...; "?" an unnamed
 * one). Lines starting with "%" hold the raw numbers as well: compared
 * only between two runs on the same system (Microsoft's TOOLHELP against
 * w16's on one KERNEL). Lines starting with "#" are notes, never compared.
 *
 * Sections (C:\THP.ARG, letters; default all): e entry checks (dwSize,
 * NULL pointers, GlobalHandleToSel), g global heap, h the global heap grown
 * (2400 blocks: KRNL386 moves its arenas), l local heaps, m
 * modules, t tasks, s stack traces, c classes, i information
 * (SystemHeapInfo, MemManInfo, TimerCount), r MemoryRead/Write, n
 * notifications, x interrupts and faults, k task control (TaskGetCSIP,
 * TaskSetCSIP, TaskSwitch, TerminateApp).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 8, Vol. 2
 * (the functions), Vol. 3 (the structures); OpenWatcom's toolhelp.h for
 * the constants. TDB offsets read here (00h next, 02h SS:SP, 1Ch
 * hInstance, 60h PDB) are Pietrek's, Windows Internals (1993) Table 3-2,
 * as kernel/task.c has them.
 */
#include <windows.h>
#include <toolhelp.h>

static const char logname[] = "C:\\THP.LOG";
static char line[400];
static HINSTANCE th;                    /* TOOLHELP */
static HMODULE hk, hu, hg, hme;
static HTASK me;
static WORD dsel;                       /* our DGROUP */
static char arg[40] = "eghmltscirnxkd";

/* ---- logging (no C runtime: -zl) ---- */
/* the log is written in pieces of up to 4 KB (lflush before anything
   that could end the probe) */
static char lbuf[4096];
static WORD lpos, bulk;                 /* bulk: no flush at each line */

static void lflush(void)
{
    HFILE f;
    if (!lpos)
        return;
    f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, lbuf, lpos);
    _lclose(f);
    lpos = 0;
}

static void put(const char FAR *s, int n)
{
    if (lpos + n > sizeof lbuf)
        lflush();
    while (n--)
        lbuf[lpos++] = *s++;
}

/* fill a structure with EEh before a call: fields a function leaves
   alone show as EEEE */
static void pat(void FAR *p, WORD n)
{
    BYTE FAR *b = p;
    while (n--)
        *b++ = 0xEE;
}

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
    if (!bulk)
        lflush();
}

/* ---- names for handles and selectors ---- */
typedef struct { WORD v; char n[14]; } NAMED;
static NAMED names[160];
static int nnames;

static void nameit(WORD v, const char *n)
{
    int i;
    if (!v)
        return;
    for (i = 0; i < nnames; i++)
        if (names[i].v == v)
            return;                     /* the first name stays */
    if (nnames < 160) {
        names[nnames].v = v;
        lstrcpy(names[nnames].n, n);
        nnames++;
    }
}

static void unname(WORD v)
{
    int i;
    for (i = 0; i < nnames; i++)
        if (names[i].v == v) {
            names[i] = names[--nnames];
            return;
        }
}

static const char *hn(WORD v)
{
    int i;
    if (!v)
        return "0";
    for (i = 0; i < nnames; i++)
        if (names[i].v == v)
            return names[i].n;
    return "?";
}

/* a block's handle and selector under one base name */
static void nameblk(WORD h, const char *base)
{
    char b[14];
    DWORD gh = GlobalHandle(h);
    lstrcpy(b, base);
    lstrcat(b, ".h");
    nameit(LOWORD(gh), b);
    lstrcpy(b, base);
    lstrcat(b, ".s");
    nameit(HIWORD(gh), b);
}

static void namemod(const char *mod)
{
    HMODULE h = GetModuleHandle(mod);
    char b[14];
    if (!h)
        return;
    lstrcpy(b, "M:");
    lstrcat(b, mod);
    nameit(h, b);
}

/* ---- TOOLHELP's functions, by ordinal ---- */
static FARPROC thfA[84], thfB[84];
static FARPROC *thf = thfA;     /* d: thfB, a second TOOLHELP */
static BYTE dual_st;            /* st_walk: compare the two */
static const char *const thname[84] = {
    0, "WEP", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    "GlobalHandleToSel", "GlobalFirst", "GlobalNext", "GlobalInfo",
    "GlobalEntryHandle", "GlobalEntryModule", "LocalInfo", "LocalFirst",
    "LocalNext", "ModuleFirst", "ModuleNext", "ModuleFindName",
    "ModuleFindHandle", "TaskFirst", "TaskNext", "TaskFindHandle",
    "StackTraceFirst", "StackTraceCSIPFirst", "StackTraceNext", "ClassFirst",
    "ClassNext", "SystemHeapInfo", "MemManInfo", "NotifyRegister",
    "NotifyUnRegister", "InterruptRegister", "InterruptUnRegister",
    "TerminateApp", "MemoryRead", "MemoryWrite", "TimerCount", "TaskSetCSIP",
    "TaskGetCSIP", "TaskSwitch"
};

typedef BOOL (FAR PASCAL *P1)(void FAR *);
typedef BOOL (FAR PASCAL *P2)(void FAR *, WORD);
typedef BOOL (FAR PASCAL *P3)(void FAR *, WORD, WORD);
#define F1(o)   ((P1)thf[o])
#define F2(o)   ((P2)thf[o])
#define F3(o)   ((P3)thf[o])
#define pGlobalHandleToSel ((WORD (FAR PASCAL *)(WORD))thf[50])
#define pGlobalFirst    F2(51)
#define pGlobalNext     F2(52)
#define pGlobalInfo     F1(53)
#define pGlobalEntryHandle F2(54)
#define pGlobalEntryModule F3(55)
#define pLocalInfo      F2(56)
#define pLocalFirst     F2(57)
#define pLocalNext      F1(58)
#define pModuleFirst    F1(59)
#define pModuleNext     F1(60)
#define pModuleFindName ((WORD (FAR PASCAL *)(void FAR *, LPCSTR))thf[61])
#define pModuleFindHandle ((WORD (FAR PASCAL *)(void FAR *, WORD))thf[62])
#define pTaskFirst      F1(63)
#define pTaskNext       F1(64)
#define pTaskFindHandle F2(65)
#define pStackTraceFirst F2(66)
#define pStackTraceCSIPFirst ((BOOL (FAR PASCAL *)(void FAR *, WORD, WORD, WORD, WORD))thf[67])
#define pStackTraceNext F1(68)
#define pClassFirst     F1(69)
#define pClassNext      F1(70)
#define pSystemHeapInfo F1(71)
#define pMemManInfo     F1(72)
#define pNotifyRegister ((BOOL (FAR PASCAL *)(WORD, FARPROC, WORD))thf[73])
#define pNotifyUnRegister ((BOOL (FAR PASCAL *)(WORD))thf[74])
#define pInterruptRegister ((BOOL (FAR PASCAL *)(WORD, FARPROC))thf[75])
#define pInterruptUnRegister ((BOOL (FAR PASCAL *)(WORD))thf[76])
#define pTerminateApp   ((void (FAR PASCAL *)(WORD, WORD))thf[77])
#define pMemoryRead     ((DWORD (FAR PASCAL *)(WORD, DWORD, void FAR *, DWORD))thf[78])
#define pMemoryWrite    ((DWORD (FAR PASCAL *)(WORD, DWORD, void FAR *, DWORD))thf[79])
#define pTimerCount     F1(80)
#define pTaskSetCSIP    ((DWORD (FAR PASCAL *)(WORD, WORD, WORD))thf[81])
#define pTaskGetCSIP    ((DWORD (FAR PASCAL *)(WORD))thf[82])
#define pTaskSwitch     ((BOOL (FAR PASCAL *)(WORD, DWORD))thf[83])

/* ---- KERNEL helpers ---- */
extern WORD __cdecl __far get_ahincr(void);     /* start.asm */
/* start.asm: far-call a Pascal function with DS = ds and nargs words */
extern DWORD __cdecl __far call_ds(WORD ds, void FAR *fn, WORD nargs, WORD a1, WORD a2, WORD a3);

static FARPROC kp(WORD ord)
{
    return GetProcAddress(hk, MAKEINTRESOURCE(ord));
}
#define kGetCurrentPDB  ((WORD (FAR PASCAL *)(void))kp(37))
#define kGlobalMasterHandle ((DWORD (FAR PASCAL *)(void))kp(28))
#define kGetExePtr      ((WORD (FAR PASCAL *)(WORD))kp(133))
#define kIsTask         ((BOOL (FAR PASCAL *)(WORD))kp(320))
#define kGetTaskQueue   ((WORD (FAR PASCAL *)(WORD))kp(35))
#define kGetHeapSpaces  ((DWORD (FAR PASCAL *)(WORD))kp(138))

static WORD peekw(WORD sel, WORD off)
{
    return *(WORD FAR *)MAKELP(sel, off);
}

static WORD lsl(WORD sel)
{
    WORD r = 0;
    _asm {
        xor ax, ax
        lsl ax, sel
        mov r, ax
    }
    return r;
}

/* a code selector's module name (GetExePtr, GetModuleFileName) */
static const char *modof(WORD sel)
{
    static char b[4][14];
    static int k;
    char path[144], *p, *q, *r = b[k++ & 3];
    WORD h;
    if (!sel || !lsl(sel)) {
        lstrcpy(r, sel ? "bad" : "0");
        return r;
    }
    h = kGetExePtr(LOWORD(GlobalHandle(sel)));
    if (!h || !GetModuleFileName(h, path, sizeof path)) {
        lstrcpy(r, "?");
        return r;
    }
    for (p = q = path; *p; p++)
        if (*p == '\\')
            q = p + 1;
    for (p = q; *p && *p != '.'; p++)
        ;
    *p = 0;
    lstrcpy(r, q);
    return r;
}

/* the Windows directory as %W% in a path */
static char windir[80];
static const char *wp(const char FAR *p)
{
    static char b[4][160];
    static int k;
    char *r = b[k++ & 3];
    int n = lstrlen(windir), i;
    for (i = 0; i < n && (p[i] | 0x20) == (windir[i] | 0x20); i++)
        ;
    if (i == n && (p[n] == '\\' || !p[n])) {
        lstrcpy(r, "%W%");
        lstrcpyn(r + 3, p + n, 150);
    } else
        lstrcpyn(r, p, 150);
    return r;
}

/* sorting lines that come in an order of addresses */
static char FAR *sbuf;                  /* 32 KB */
static WORD soff, scount;
static WORD sidx[600];

static void sadd(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    if (scount >= 600 || soff + n + 1 > 32000)
        return;
    sidx[scount++] = soff;
    lstrcpy(sbuf + soff, line);
    soff += n + 1;
}

static void sflush(const char *head)
{
    WORD i, j, t;
    for (i = 1; i < scount; i++)
        for (j = i; j > 0 && lstrcmp(sbuf + sidx[j - 1], sbuf + sidx[j]) > 0; j--) {
            t = sidx[j];
            sidx[j] = sidx[j - 1];
            sidx[j - 1] = t;
        }
    for (i = 0; i < scount; i++)
        lg("%s%s", (LPSTR)head, sbuf + sidx[i]);
    scount = soff = 0;
}

/* ---- the child ---- */
static HTASK child;
static HMODULE hchild;

static WORD task_of_inst(HINSTANCE hi)
{
    /* the task list from THHOOK (KERNEL.332) 0Eh, HeadTDB (ARCHITECTURE.md 9.7) */
    WORD t = ((WORD FAR *)kp(332))[7], n = 0;
    while (t && n++ < 100) {
        if ((peekw(t, 0x1C) | 1) == ((WORD)hi | 1))
            return t;
        t = peekw(t, 0);
    }
    return 0;
}

static void spin(WORD n)
{
    while (n--)
        Yield();
}

static HTASK start_child(const char *cmd)
{
    char c[60];
    HINSTANCE hi;
    lstrcpy(c, "C:\\THC.EXE ");
    lstrcat(c, cmd);
    lflush();
    hi = (HINSTANCE)WinExec(c, SW_SHOWNORMAL);
    if ((WORD)hi < 32) {
        lg("# WinExec(%s) = %u", (LPSTR)c, (WORD)hi);
        return 0;
    }
    spin(20);
    child = task_of_inst(hi);
    hchild = GetModuleHandle("THC");
    if (child) {
        nameit(child, "T:child");
        nameit(peekw(child, 0x60), "child.pdb");
        nameblk((WORD)hi, "child.dg");
        namemod("THC");
    }
    return child;
}

static int wait_gone(HTASK t)
{
    WORD i;
    for (i = 0; i < 3000 && kIsTask(t); i++)
        Yield();
    return !kIsTask(t);
}

static void end_child(WORD code)
{
    if (!child || !kIsTask(child))
        return;
    PostAppMessage(child, WM_QUIT, code, 0);
    lg("child ended: %d", wait_gone(child));
    unname(child);
    child = 0;
}

/* a module's code segments by number (GetCodeHandle(MAKELONG(n, hModule)),
   Schulman ch. 5; segments 1-4) */
static void namesegs(WORD hmod, const char *base)
{
    DWORD (FAR PASCAL *gch)(DWORD) = (DWORD (FAR PASCAL *)(DWORD))kp(93);
    WORD n;
    char b[14];
    for (n = 1; n <= 4; n++) {
        DWORD r = gch(MAKELONG(n, hmod));
        wsprintf(b, "%s%u", (LPSTR)base, n);
        if (HIWORD(r))
            nameit(HIWORD(r), b);
    }
}

/* ==== e: entry checks ==== */
static void t_entry(void)
{
    static const WORD sz[] = { 53, sizeof(GLOBALINFO), 51, sizeof(GLOBALENTRY),
        56, sizeof(LOCALINFO), 57, sizeof(LOCALENTRY), 59, sizeof(MODULEENTRY),
        63, sizeof(TASKENTRY), 69, sizeof(CLASSENTRY), 71, sizeof(SYSHEAPINFO),
        72, sizeof(MEMMANINFO), 80, sizeof(TIMERINFO), 67, sizeof(STACKTRACEENTRY), 0 };
    static BYTE big[300];
    WORD i, ds = dsel, h;
    lg("== e entry checks");
    for (i = 0; sz[i]; i += 2) {
        int d;
        for (d = -2; d <= 2; d++) {
            BOOL r;
            DWORD FAR *p = (DWORD FAR *)big;
            *p = sz[i + 1] + d;
            switch (sz[i]) {
            case 51: r = pGlobalFirst(p, GLOBAL_ALL); break;
            case 56: r = pLocalInfo(p, ds); break;
            case 57: r = pLocalFirst(p, ds); break;
            case 67: r = pStackTraceCSIPFirst(p, ds, SELECTOROF((void FAR *)t_entry), 0, 0); break;
            default: r = F1(sz[i])(p); break;
            }
            lg("%s dwSize %d: %d", (LPSTR)thname[sz[i]], d, r != 0);
        }
        {
            BOOL r;
            switch (sz[i]) {
            case 51: r = pGlobalFirst(NULL, GLOBAL_ALL); break;
            case 56: r = pLocalInfo(NULL, ds); break;
            case 57: r = pLocalFirst(NULL, ds); break;
            case 67: r = pStackTraceCSIPFirst(NULL, ds, 0, 0, 0); break;
            default: r = F1(sz[i])(NULL); break;
            }
            lg("%s NULL: %d", (LPSTR)thname[sz[i]], r != 0);
        }
    }
    {
        /* the Next functions with a wrong dwSize, after a good First */
        GLOBALENTRY ge;
        MODULEENTRY me2;
        TASKENTRY te;
        CLASSENTRY ce;
        pat(&ge, sizeof ge);
        ge.dwSize = sizeof ge;
        pGlobalFirst(&ge, GLOBAL_ALL);
        ge.dwSize--;
        lg("GlobalNext dwSize -1: %d", pGlobalNext(&ge, GLOBAL_ALL) != 0);
        pat(&me2, sizeof me2);
        me2.dwSize = sizeof me2;
        pModuleFirst(&me2);
        me2.dwSize++;
        lg("ModuleNext dwSize +1: %d", pModuleNext(&me2) != 0);
        pat(&te, sizeof te);
        te.dwSize = sizeof te;
        pTaskFirst(&te);
        te.dwSize = 0;
        lg("TaskNext dwSize 0: %d", pTaskNext(&te) != 0);
        pat(&ce, sizeof ce);
        ce.dwSize = sizeof ce;
        pClassFirst(&ce);
        ce.dwSize--;
        lg("ClassNext dwSize -1: %d", pClassNext(&ce) != 0);
        me2.dwSize = sizeof me2 - 1;
        lg("ModuleFindName dwSize -1: %d", pModuleFindName(&me2, "USER") != 0);
        lg("ModuleFindHandle dwSize -1: %d", pModuleFindHandle(&me2, hu) != 0);
        te.dwSize = sizeof te - 1;
        lg("TaskFindHandle dwSize -1: %d", pTaskFindHandle(&te, me) != 0);
        lg("ModuleFindName NULL: %d", pModuleFindName(NULL, "USER") != 0);
        pat(&me2, sizeof me2);
        me2.dwSize = sizeof me2;
        lg("ModuleFindName name NULL: %d", pModuleFindName(&me2, NULL) != 0);
        lg("ModuleFindHandle NULL: %d", pModuleFindHandle(NULL, hu) != 0);
        lg("TaskFindHandle NULL: %d", pTaskFindHandle(NULL, me) != 0);
        lg("StackTraceFirst NULL: %d", pStackTraceFirst(NULL, me) != 0);
        /* GlobalNext(NULL) faults in 3.1's TOOLHELP under KRNL286: the
           Next functions are not called with NULL */
    }
    {
        /* GlobalHandleToSel on handles, selectors and others */
        HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, 100), hf = GlobalAlloc(GMEM_FIXED, 100);
        WORD v[14], k;
        static const char *const vn[14] = { "0", "1", "2", "3", "7", "8", "FFFF",
            "moveable handle", "moveable selector", "fixed handle", "own DS",
            "own CS", "task", "module" };
        v[0] = 0; v[1] = 1; v[2] = 2; v[3] = 3; v[4] = 7; v[5] = 8; v[6] = 0xFFFF;
        v[7] = (WORD)hm;
        v[8] = HIWORD(GlobalHandle((WORD)hm));
        v[9] = (WORD)hf;
        v[10] = dsel;
        v[11] = SELECTOROF((void FAR *)t_entry);
        v[12] = (WORD)me;
        v[13] = hme;
        for (k = 0; k < 14; k++) {
            WORD r;
            h = v[k];
            r = pGlobalHandleToSel(h);
            lg("GlobalHandleToSel(%s): %s", (LPSTR)vn[k],
               (LPSTR)(r == h ? "same" : r == (h | 1) ? "h|1" : r == (h & ~1) ? "h&~1" :
                       r == 0 ? "0" : r == h - 1 ? "h-1" : "other"));
            lg("%%GlobalHandleToSel %04X: %04X", h, r);
        }
        GlobalFree(hm);
        GlobalFree(hf);
    }
}

/* ==== g: the global heap ==== */
static const char *const gtn[] = { "UNKNOWN", "DGROUP", "DATA", "CODE", "TASK",
    "RESOURCE", "MODULE", "FREE", "INTERNAL", "SENTINEL", "BURGERMASTER" };

static const char *gt(WORD t)
{
    static char b[8];
    if (t < 11)
        return gtn[t];
    wsprintf(b, "t%u", t);
    return b;
}

static void ge_line(const char *what, BOOL ok, GLOBALENTRY FAR *ge)
{
    if (!ok) {
        lg("%s: 0", (LPSTR)what);
        return;
    }
    lg("%s: 1 %s data=%u size=%lu lock=%u plock=%u flags=%04X heap=%u block=%s owner=%s",
       (LPSTR)what, (LPSTR)gt(ge->wType), ge->wData, ge->dwBlockSize, ge->wcLock,
       ge->wcPageLock, ge->wFlags, ge->wHeapPresent, (LPSTR)hn(ge->hBlock),
       (LPSTR)hn(ge->hOwner));
    lg("%s: addr==base %d", (LPSTR)what, ge->hBlock && lsl(ge->hBlock | 1) &&
       ge->dwAddress == GetSelectorBase(ge->hBlock | 1));
    lg("%%%s: addr=%08lX blk=%04X own=%04X next=%08lX alt=%08lX", (LPSTR)what,
       ge->dwAddress, ge->hBlock, ge->hOwner, ge->dwNext, ge->dwNextAlt);
}

static void geh(const char *what, WORD h)
{
    GLOBALENTRY ge;
    char w[60];
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    lstrcpy(w, "GlobalEntryHandle(");
    lstrcat(w, what);
    lstrcat(w, ")");
    ge_line(w, pGlobalEntryHandle(&ge, h), &ge);
}

static void gem(const char *what, WORD hmod, WORD seg)
{
    GLOBALENTRY ge;
    char w[60];
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    wsprintf(w, "GlobalEntryModule(%s,%u)", (LPSTR)what, seg);
    ge_line(w, pGlobalEntryModule(&ge, hmod, seg), &ge);
}

static BOOL ours(WORD owner)
{
    const char *n = hn(owner);
    return !lstrcmp(n, "M:THP") || !lstrcmp(n, "pdb") || !lstrcmp(n, "T:self");
}

static void walk(WORD flags, const char *name, WORD expect)
{
    GLOBALENTRY ge;
    WORD n = 0, types[12], i, sent = 0, nohandle = 0, locked = 0, heaps = 0, pdbf = 0;
    WORD firstt = 0xFFFF, lastt = 0xFFFF, badsize = 0, baddr = 0;
    BOOL more;
    for (i = 0; i < 12; i++)
        types[i] = 0;
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    bulk = 1;
    for (more = pGlobalFirst(&ge, flags); more && n < 4000; more = pGlobalNext(&ge, flags)) {
        n++;
        if (firstt == 0xFFFF)
            firstt = ge.wType;
        lastt = ge.wType;
        types[ge.wType < 11 ? ge.wType : 11]++;
        if (!ge.hBlock)
            nohandle++;
        if (ge.wcLock)
            locked++;
        if (ge.wHeapPresent)
            heaps++;
        if (ge.wFlags & GF_PDB_OWNER)
            pdbf++;
        if (ge.hBlock && lsl(ge.hBlock | 1) && ge.wType != GT_FREE &&
            ge.wType != GT_SENTINEL && GlobalSize(ge.hBlock) != ge.dwBlockSize)
            badsize++;
        if (ge.hBlock && lsl(ge.hBlock | 1) && ge.wType != GT_FREE &&
            ge.dwAddress != GetSelectorBase(ge.hBlock | 1))
            baddr++;
        if (flags == GLOBAL_ALL && (ours(ge.hOwner) || !lstrcmp(hn(ge.hOwner), "M:THD") ||
                                    !lstrcmp(hn(ge.hOwner), "M:THC")))
            sadd("%s data=%u size=%lu lock=%u plock=%u flags=%04X heap=%u block=%s owner=%s",
                 (LPSTR)gt(ge.wType), ge.wData, ge.dwBlockSize, ge.wcLock, ge.wcPageLock,
                 ge.wFlags, ge.wHeapPresent, (LPSTR)hn(ge.hBlock), (LPSTR)hn(ge.hOwner));
        lg("%%%s %08lX %08lX %04X %u %u %04X %u %04X %u %u %08lX %08lX", (LPSTR)name,
           ge.dwAddress, ge.dwBlockSize, ge.hBlock, ge.wcLock, ge.wcPageLock, ge.wFlags,
           ge.wHeapPresent, ge.hOwner, ge.wType, ge.wData, ge.dwNext, ge.dwNextAlt);
        (void)sent;
    }
    bulk = 0;
    lg("walk %s: count==info %d", (LPSTR)name, n == expect);
    lg("walk %s: first %s last %s", (LPSTR)name, (LPSTR)gt(firstt), (LPSTR)gt(lastt));
    lg("walk %s: size!=GlobalSize %u, address!=base %u", (LPSTR)name, badsize, baddr);
    lg("%%walk %s: count %u", (LPSTR)name, n);
    if (flags == GLOBAL_ALL) {
        for (i = 0; i < 12; i++)
            if (i == GT_SENTINEL || i == GT_BURGERMASTER || i == 11)
                lg("walk ALL: %s %u", (LPSTR)(i < 11 ? gtn[i] : "other"), types[i]);
            else
                lg("%%walk ALL: %s %u", (LPSTR)gtn[i], types[i]);
        lg("walk ALL: TASK blocks==tasks %d", types[GT_TASK] == GetNumTasks());
        lg("%%walk ALL: no handle %u locked %u heaps %u pdb-owned %u", nohandle, locked,
           heaps, pdbf);
        sflush("ours: ");
    } else {
        lg("walk %s: types other than %s: %u", (LPSTR)name,
           (LPSTR)(flags == GLOBAL_FREE ? "FREE" : "-"),
           flags == GLOBAL_FREE ? n - types[GT_FREE] : 0);
    }
}

static HGLOBAL ga[10];

static void need_ga(void)
{
    WORD i;
    if (ga[0])
        return;
    ga[0] = GlobalAlloc(GMEM_ZEROINIT | GMEM_FIXED, 100);
    ga[1] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, 5000);
    ga[2] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DISCARDABLE, 300);
    GlobalLock(ga[2]);
    ga[3] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DISCARDABLE, 300);
    GlobalDiscard(ga[3]);
    ga[4] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_SHARE, 40);
    ga[5] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, 70000L);
    ga[6] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, 64);
    GlobalFix(ga[6]);
    ga[7] = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DISCARDABLE, 64);
    ga[8] = GlobalAlloc(GMEM_ZEROINIT | GMEM_FIXED | GMEM_DISCARDABLE, 64);
    for (i = 0; i < 9; i++) {
        char b[6];
        wsprintf(b, "ga%u", i);
        nameblk((WORD)ga[i], b);
    }
}

static void t_global(void)
{
    GLOBALINFO gi;
    GLOBALENTRY ge;
    WORD i, nmod = 0;
    BOOL more;
    HRSRC r;
    HGLOBAL hr;
    lg("== g global heap");
    need_ga();
    pat(&gi, sizeof gi);
    gi.dwSize = sizeof gi;
    lg("GlobalInfo: %d", pGlobalInfo(&gi) != 0);
    lg("%%GlobalInfo: items %u free %u lru %u", gi.wcItems, gi.wcItemsFree, gi.wcItemsLRU);
    walk(GLOBAL_ALL, "ALL", gi.wcItems);
    walk(GLOBAL_LRU, "LRU", gi.wcItemsLRU);
    walk(GLOBAL_FREE, "FREE", gi.wcItemsFree);
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    lg("GlobalFirst(3): %d", pGlobalFirst(&ge, 3) != 0);
    lg("GlobalFirst(FFFF): %d", pGlobalFirst(&ge, 0xFFFF) != 0);
    /* every module table is a GT_MODULE block */
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    for (more = pGlobalFirst(&ge, GLOBAL_ALL); more; more = pGlobalNext(&ge, GLOBAL_ALL))
        if (ge.wType == GT_MODULE)
            nmod++;
    {
        MODULEENTRY m;
        WORD n = 0;
        pat(&m, sizeof m);
        m.dwSize = sizeof m;
        for (more = pModuleFirst(&m); more && n < 200; more = pModuleNext(&m))
            n++;
        lg("walk ALL: MODULE blocks==modules %d", n == nmod);
    }
    /* the LRU list: discardable blocks, ours among them */
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    {
        WORD n2 = 0, mine = 0;
        for (more = pGlobalFirst(&ge, GLOBAL_LRU); more && n2 < 4000; more = pGlobalNext(&ge, GLOBAL_LRU)) {
            n2++;
            if (ge.hBlock == ga[7] || ge.hBlock == ga[2] || ge.hBlock == ga[8])
                sadd("LRU has %s", (LPSTR)hn(ge.hBlock));
            (void)mine;
        }
        sflush("");
    }
    /* entries by handle */
    for (i = 0; i < 9; i++) {
        char b[6];
        wsprintf(b, "ga%u", i);
        geh(b, (WORD)ga[i]);
        wsprintf(b, "ga%u|1", i);
        geh(b, (WORD)ga[i] | 1);
    }
    geh("tile 2", HIWORD(GlobalHandle((WORD)ga[5])) + get_ahincr());
    geh("own DS", dsel);
    geh("own DS handle", LOWORD(GlobalHandle(dsel)));
    geh("own CS", SELECTOROF((void FAR *)t_global));
    geh("own module", hme);
    geh("own task", (WORD)me);
    geh("own PDB", kGetCurrentPDB());
    geh("BurgerMaster", LOWORD(kGlobalMasterHandle()));
    geh("USER", hu);
    geh("USER DS", (WORD)GetWindowWord(GetDesktopWindow(), GWW_HINSTANCE));
    geh("KERNEL", hk);
    geh("0", 0);
    geh("1", 1);
    geh("FFFF", 0xFFFF);
    {
        HGLOBAL f = GlobalAlloc(GMEM_MOVEABLE, 10);
        GlobalFree(f);
        geh("freed", (WORD)f);
    }
    {
        WORD a = AllocSelector(dsel);
        geh("alias", a);
        FreeSelector(a);
    }
    /* resources */
    r = FindResource(GetModuleHandle("THP"), MAKEINTRESOURCE(1), RT_RCDATA);
    hr = r ? LoadResource(GetModuleHandle("THP"), r) : 0;
    nameblk((WORD)hr, "rcdata");
    geh("RCDATA", (WORD)hr);
    if (hr) {
        LockResource(hr);
        geh("RCDATA locked", (WORD)hr);
        UnlockResource(hr);
        FreeResource(hr);
    }
    {
        HMENU m = LoadMenu(GetModuleHandle("THP"), MAKEINTRESOURCE(2));
        lg("LoadMenu: %d", m != 0);
        r = FindResource(GetModuleHandle("THP"), MAKEINTRESOURCE(2), RT_MENU);
        hr = r ? LoadResource(GetModuleHandle("THP"), r) : 0;
        nameblk((WORD)hr, "menu");
        geh("MENU", (WORD)hr);
        if (hr)
            FreeResource(hr);
        if (m)
            DestroyMenu(m);
    }
    {
        char s[20];
        LoadString(GetModuleHandle("THP"), 1, s, sizeof s);
        r = FindResource(GetModuleHandle("THP"), MAKEINTRESOURCE(1), RT_STRING);
        hr = r ? LoadResource(GetModuleHandle("THP"), r) : 0;
        nameblk((WORD)hr, "string");
        geh("STRING", (WORD)hr);
        if (hr)
            FreeResource(hr);
    }
    /* by module and segment number */
    gem("own", hme, 0);
    gem("own", hme, 1);
    gem("own", hme, 2);
    gem("own", hme, 3);
    gem("own", hme, 4);
    gem("own", hme, 5);
    gem("USER", hu, 1);
    gem("KERNEL", hk, 1);
    gem("GDI", hg, 1);
    gem("own DS", dsel, 1);
    gem("own task", (WORD)me, 1);
    gem("0", 0, 1);
    gem("FFFF", 0xFFFF, 1);
    {
        /* a font module's resource table in its module table (whose
           resources TOOLHELP types RESOURCE): raw, for the notes */
        WORD m = GetModuleHandle("FONTS"), t, i;
        if (m) {
            m |= 1;
            t = peekw(m, 0x24);
            lg("%%FONTS table: rsrc %04X resnames %04X limit %04X", t, peekw(m, 0x26), lsl(m));
            for (i = 0; i < 48 && t + i * 2 + 2 <= lsl(m); i += 8)
                lg("%%FONTS rsrc+%02X: %04X %04X %04X %04X %04X %04X %04X %04X", i * 2,
                   peekw(m, t + i * 2), peekw(m, t + i * 2 + 2), peekw(m, t + i * 2 + 4),
                   peekw(m, t + i * 2 + 6), peekw(m, t + i * 2 + 8), peekw(m, t + i * 2 + 10),
                   peekw(m, t + i * 2 + 12), peekw(m, t + i * 2 + 14));
        }
    }
    /* a walk from one entry on */
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    if (pGlobalEntryHandle(&ge, (WORD)ga[1])) {
        GLOBALENTRY g2 = ge;
        lg("GlobalNext ALL after EntryHandle: %d", pGlobalNext(&ge, GLOBAL_ALL) != 0);
        lg("%%... next %04X", ge.hBlock);
        lg("GlobalNext LRU after EntryHandle (not LRU): %d", pGlobalNext(&g2, GLOBAL_LRU) != 0);
        lg("%%... next %04X", g2.hBlock);
    }
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    if (pGlobalEntryHandle(&ge, (WORD)ga[7])) {
        lg("GlobalNext LRU after EntryHandle (LRU): %d", pGlobalNext(&ge, GLOBAL_LRU) != 0);
        lg("%%... next %04X", ge.hBlock);
    }
    /* the end of a walk */
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    for (more = pGlobalFirst(&ge, GLOBAL_ALL); more; more = pGlobalNext(&ge, GLOBAL_ALL))
        ;
    lg("GlobalNext past the end: %d", pGlobalNext(&ge, GLOBAL_ALL) != 0);
    /* changes seen by GlobalInfo */
    {
        GLOBALINFO g1, g3;
        HGLOBAL x;
        g1.dwSize = g3.dwSize = sizeof gi;
        pGlobalInfo(&g1);
        x = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 1000);
        pGlobalInfo(&g3);
        lg("GlobalInfo after alloc: items %d lru %d", g3.wcItems - g1.wcItems,
           g3.wcItemsLRU - g1.wcItemsLRU);
        GlobalFree(x);
        pGlobalInfo(&g3);
        lg("GlobalInfo after free: items %d lru %d", g3.wcItems - g1.wcItems,
           g3.wcItemsLRU - g1.wcItemsLRU);
    }
}

/* ==== l: local heaps ==== */
static const char *lhandle(WORD h, WORD heapsel, HLOCAL FAR *known, int n)
{
    static char b[2][12];
    static int k;
    int i;
    char *r = b[k++ & 1];
    for (i = 0; i < n; i++)
        if (known[i] && (WORD)known[i] == h) {
            wsprintf(r, "lh%d", i);
            return r;
        }
    (void)heapsel;
    return h ? "?" : "0";
}

static void lwalk(const char *what, WORD heap, HLOCAL FAR *known, int nk, int all)
{
    LOCALINFO li;
    LOCALENTRY le;
    WORD n = 0, types[40], i, other = 0, ht = 0xFFFF;
    BOOL more;
    pat(&li, sizeof li);
    li.dwSize = sizeof li;
    lg("LocalInfo(%s): %d", (LPSTR)what, pLocalInfo(&li, heap) != 0);
    lg("%%LocalInfo(%s): %u", (LPSTR)what, li.wcItems);
    for (i = 0; i < 40; i++)
        types[i] = 0;
    pat(&le, sizeof le);
    le.dwSize = sizeof le;
    bulk = !all;
    for (more = pLocalFirst(&le, heap); more && n < 2000; more = pLocalNext(&le)) {
        n++;
        if (ht == 0xFFFF)
            ht = le.wHeapType;
        if (le.wType < 39)
            types[le.wType]++;
        else if (le.wType == LT_FREE)
            types[39]++;
        else
            other++;
        if (all)
            lg("%s: handle=%s addr=%s size=%u flags=%u lock=%u type=%u heap=%s htype=%u",
               (LPSTR)what, (LPSTR)lhandle((WORD)le.hHandle, heap, known, nk),
               (LPSTR)(le.wFlags == LF_FREE ? "free" : (WORD)le.hHandle == le.wAddress ? "=handle" :
                       le.wFlags & LF_MOVEABLE && le.wAddress == *(WORD FAR *)MAKELP(heap | 1, (WORD)le.hHandle) ? "=*handle" : "other"),
               le.wSize, le.wFlags, le.wcLock, le.wType, (LPSTR)hn(le.hHeap), le.wHeapType);
        lg("%%%s %04X %04X %04X %u %u %u %04X %u %04X", (LPSTR)what, le.hHandle,
           le.wAddress, le.wSize, le.wFlags, le.wcLock, le.wType, le.hHeap, le.wHeapType, le.wNext);
    }
    bulk = 0;
    if (all)
        lg("walk %s: count==info %d (%u)", (LPSTR)what, n == li.wcItems, n);
    else {
        lg("walk %s: count==info %d", (LPSTR)what, n == li.wcItems);
        lg("walk %s: heap type %u", (LPSTR)what, ht);
    }
    lg("%%%s after the end: %04X %04X %04X %u %u %u %04X %u %04X", (LPSTR)what, le.hHandle,
       le.wAddress, le.wSize, le.wFlags, le.wcLock, le.wType, le.hHeap, le.wHeapType, le.wNext);
    more = pLocalNext(&le);
    lg("LocalNext past the end(%s): %d", (LPSTR)what, more != 0);
    lg("%%%s past the end: %04X %04X %04X %u %u %u %04X %u %04X", (LPSTR)what, le.hHandle,
       le.wAddress, le.wSize, le.wFlags, le.wcLock, le.wType, le.hHeap, le.wHeapType, le.wNext);
    for (i = 0; i < 40; i++)
        if (types[i])
            lg("%%walk %s: type %u: %u", (LPSTR)what, i == 39 ? 0xFF : i, types[i]);
}

/* Section h: walks of the global heap before and after it has grown by
   2400 blocks (more than KRNL386's first room for arenas: w16's KERNEL
   then moves the Burgermaster, its selector staying) and after they are
   freed. w16's TOOLHELP had kept the Burgermaster's linear address from
   the first walk and read freed pages afterwards: the VM Task Manager's
   page fault (the user's report, 2026-11-27). */
#define HGROW 2400
static WORD own_blocks(WORD *n)
{
    GLOBALENTRY ge;
    WORD own = 0;
    BOOL more;
    *n = 0;
    pat(&ge, sizeof ge);
    ge.dwSize = sizeof ge;
    bulk = 1;
    for (more = pGlobalFirst(&ge, GLOBAL_ALL); more && *n < 8000; more = pGlobalNext(&ge, GLOBAL_ALL)) {
        (*n)++;
        if ((WORD)ge.hOwner == (WORD)me && ge.wType != GT_FREE)
            own++;
    }
    bulk = 0;
    return own;
}

static void t_grow(void)
{
    HGLOBAL FAR *h = (HGLOBAL FAR *)sbuf;   /* (not in DGROUP: its size is in section g) */
    WORD i, got = 0, n0, n1, n2, own0, own1, own2, found = 0;
    GLOBALENTRY ge;
    lg("== h the heap grown");
    own0 = own_blocks(&n0);
    for (i = 0; i < HGROW; i++)
        if ((h[i] = GlobalAlloc(GMEM_MOVEABLE, 32)) != 0)
            got++;
    lg("allocated: %d", got == HGROW);
    own1 = own_blocks(&n1);
    lg("walk after: ours %d", own1 == own0 + got);
    for (i = 0; i < HGROW; i++) {
        pat(&ge, sizeof ge);
        ge.dwSize = sizeof ge;
        if (h[i] && pGlobalEntryHandle(&ge, (WORD)h[i]) && ge.hBlock == h[i] && ge.dwBlockSize >= 32)
            found++;
    }
    lg("GlobalEntryHandle: each found %d", found == got);
    for (i = 0; i < HGROW; i++)
        if (h[i])
            GlobalFree(h[i]);
    own2 = own_blocks(&n2);
    lg("walk after freeing: ours as before %d", own2 == own0);
    lg("# h: blocks %u, %u, %u; ours %u, %u, %u", n0, n1, n2, own0, own1, own2);
}

static WORD gdi_types(WORD heap, WORD FAR *t)
{
    LOCALENTRY le;
    WORD n = 0, i;
    BOOL more;
    for (i = 0; i < 16; i++)
        t[i] = 0;
    pat(&le, sizeof le);
    le.dwSize = sizeof le;
    for (more = pLocalFirst(&le, heap); more && n < 4000; more = pLocalNext(&le)) {
        n++;
        t[le.wType < 15 ? le.wType : 15]++;
    }
    return n;
}

static void t_local(void)
{
    HLOCAL lh[8];
    HGLOBAL hb;
    WORD sel, i;
    SYSHEAPINFO sh;
    lg("== l local heaps");
    need_ga();
    for (i = 0; i < 8; i++)
        lh[i] = 0;
    lh[0] = LocalAlloc(LMEM_FIXED, 10);
    lh[1] = LocalAlloc(LMEM_MOVEABLE, 20);
    lh[2] = LocalAlloc(LMEM_MOVEABLE, 30);
    LocalLock(lh[2]);
    lh[3] = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 40);
    lh[4] = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 50);
    LocalDiscard(lh[4]);
    lh[5] = LocalAlloc(LMEM_FIXED, 60);
    lh[6] = LocalAlloc(LMEM_FIXED, 16);
    LocalFree(lh[5]);
    lh[5] = 0;
    nameblk(dsel, "dg");
    lwalk("own DS", dsel, lh, 8, 1);
    lwalk("own DS handle", LOWORD(GlobalHandle(dsel)), lh, 8, 0);
    /* a heap of its own */
    hb = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 2048);
    sel = HIWORD(GlobalHandle((WORD)hb));
    nameblk((WORD)hb, "lhb");
    lg("LocalInit: %d", LocalInit(sel, 16, 2000) != 0);
    {
        HLOCAL k[3];
        k[0] = (HLOCAL)call_ds(sel, (void FAR *)LocalAlloc, 2, LMEM_FIXED, 24, 0);
        k[1] = (HLOCAL)call_ds(sel, (void FAR *)LocalAlloc, 2, LMEM_MOVEABLE, 40, 0);
        k[2] = 0;
        lwalk("block heap", sel, k, 3, 1);
    }
    {
        GLOBALENTRY ge;
        pat(&ge, sizeof ge);
        ge.dwSize = sizeof ge;
        pGlobalEntryHandle(&ge, (WORD)hb);
        lg("GlobalEntryHandle(block heap) heap=%u", ge.wHeapPresent);
        pat(&ge, sizeof ge);
        ge.dwSize = sizeof ge;
        pGlobalEntryHandle(&ge, dsel);
        lg("GlobalEntryHandle(own DS) heap=%u", ge.wHeapPresent);
        pat(&ge, sizeof ge);
        ge.dwSize = sizeof ge;
        pGlobalEntryHandle(&ge, (WORD)ga[1]);
        lg("GlobalEntryHandle(no heap) heap=%u", ge.wHeapPresent);
        {
            /* what a block without a heap has where a heap's pointers are */
            WORD s = (WORD)ga[1] | 1, p = peekw(s, 6), k;
            lg("%%no heap: 06h %04X", p);
            if (p < 0x1300)
                for (k = 0; k < 0x2A; k += 2)
                    lg("%%no heap: p+%02X %04X", k, peekw(s, p + k));
        }
    }
    GlobalFree(hb);
    {
        LOCALINFO li;
        LOCALENTRY le;
        li.dwSize = le.dwSize = sizeof li;
        pat(&le, sizeof le);
        le.dwSize = sizeof le;
        lg("LocalInfo(no heap): %d", pLocalInfo(&li, (WORD)ga[1]) != 0);
        lg("LocalFirst(no heap): %d", pLocalFirst(&le, (WORD)ga[1]) != 0);
        lg("LocalFirst(own CS): %d", pLocalFirst(&le, SELECTOROF((void FAR *)t_local)) != 0);
        lg("LocalFirst(0): %d", pLocalFirst(&le, 0) != 0);
        lg("LocalFirst(FFFF): %d", pLocalFirst(&le, 0xFFFF) != 0);
    }
    /* USER's and GDI's */
    pat(&sh, sizeof sh);
    sh.dwSize = sizeof sh;
    pSystemHeapInfo(&sh);
    nameblk(sh.hUserSegment, "userdg");
    nameblk(sh.hGDISegment, "gdidg");
    lwalk("USER", sh.hUserSegment, NULL, 0, 0);
    lwalk("GDI", sh.hGDISegment, NULL, 0, 0);
    {
        WORD a[16], b[16], c[16];
        HPEN p1, p2, p3;
        HBRUSH b1, b2;
        HFONT f1;
        HRGN r1;
        HBITMAP bm;
        HDC dc;
        LOGFONT lf;
        gdi_types(sh.hGDISegment, a);
        p1 = CreatePen(PS_SOLID, 1, RGB(1, 2, 3));
        p2 = CreatePen(PS_DOT, 1, RGB(4, 5, 6));
        p3 = CreatePen(PS_SOLID, 3, RGB(7, 8, 9));
        b1 = CreateSolidBrush(RGB(10, 11, 12));
        b2 = CreateHatchBrush(HS_CROSS, RGB(13, 14, 15));
        for (i = 0; i < sizeof lf; i++)
            ((BYTE *)&lf)[i] = 0;
        lf.lfHeight = 13;
        lstrcpy(lf.lfFaceName, "Helv");
        f1 = CreateFontIndirect(&lf);
        r1 = CreateRectRgn(1, 2, 30, 40);
        bm = CreateBitmap(8, 8, 1, 1, NULL);
        dc = CreateCompatibleDC(NULL);
        gdi_types(sh.hGDISegment, b);
        for (i = 0; i < 16; i++)
            if (b[i] != a[i])
                lg("GDI heap type %u: %d", i, b[i] - a[i]);
        DeleteDC(dc);
        DeleteObject(bm);
        DeleteObject(r1);
        DeleteObject(f1);
        DeleteObject(b2);
        DeleteObject(b1);
        DeleteObject(p3);
        DeleteObject(p2);
        DeleteObject(p1);
        lg("GDI objects deleted");
        gdi_types(sh.hGDISegment, c);
        for (i = 0; i < 16; i++)
            if (c[i] != a[i])
                lg("GDI heap type %u after delete: %d", i, c[i] - a[i]);
    }
    /* not tried: USER's heap walked again after a window was made (it
       ended the probe under 3.1 with Microsoft's TOOLHELP) */
    for (i = 0; i < 8; i++)
        if (lh[i])
            LocalFree(lh[i]);
}

/* ==== m: modules ==== */
static void me_line(const char *what, WORD r, MODULEENTRY FAR *m)
{
    if (!r) {
        lg("%s: 0", (LPSTR)what);
        return;
    }
    lg("%s: %s name=%s usage=%u path=%s", (LPSTR)what, (LPSTR)hn(r), (LPSTR)m->szModule,
       m->wcUsage, (LPSTR)wp(m->szExePath));
    lg("%s: hModule==ret %d usage==GetModuleUsage %d", (LPSTR)what, m->hModule == r,
       m->wcUsage == GetModuleUsage(m->hModule));
}

static void t_modules(void)
{
    MODULEENTRY m;
    WORD n = 0, prev = 0, nextok = 0;
    BOOL more;
    lg("== m modules");
    pat(&m, sizeof m);
    m.dwSize = sizeof m;
    for (more = pModuleFirst(&m); more && n < 200; more = pModuleNext(&m)) {
        n++;
        if (prev && prev != m.hModule)
            nextok++;
        lg("module %s usage=%u path=%s", (LPSTR)m.szModule, m.wcUsage, (LPSTR)wp(m.szExePath));
        lg("module %s: handle==GetModuleHandle %d next=%s", (LPSTR)m.szModule,
           m.hModule == GetModuleHandle(m.szModule), (LPSTR)(m.wNext ? "set" : "0"));
        lg("%%module %s %04X next %04X", (LPSTR)m.szModule, m.hModule, m.wNext);
        prev = m.wNext;
    }
    lg("ModuleNext past the end: %d", pModuleNext(&m) != 0);
    pat(&m, sizeof m);
    m.dwSize = sizeof m;
    me_line("ModuleFindName(USER)", pModuleFindName(&m, "USER"), &m);
    if (pModuleFindName(&m, "USER")) {
        lg("ModuleNext after FindName(USER): %d %s", pModuleNext(&m) != 0, (LPSTR)m.szModule);
    }
    me_line("ModuleFindName(user)", pModuleFindName(&m, "user"), &m);
    me_line("ModuleFindName(THP)", pModuleFindName(&m, "THP"), &m);
    me_line("ModuleFindName(KERNEL)", pModuleFindName(&m, "KERNEL"), &m);
    me_line("ModuleFindName()", pModuleFindName(&m, ""), &m);
    me_line("ModuleFindName(NOSUCH)", pModuleFindName(&m, "NOSUCH"), &m);
    me_line("ModuleFindName(USER.EXE)", pModuleFindName(&m, "USER.EXE"), &m);
    me_line("ModuleFindName(US)", pModuleFindName(&m, "US"), &m);
    me_line("ModuleFindHandle(USER)", pModuleFindHandle(&m, hu), &m);
    if (pModuleFindHandle(&m, hu))
        lg("ModuleNext after FindHandle(USER): %d %s", pModuleNext(&m) != 0, (LPSTR)m.szModule);
    me_line("ModuleFindHandle(own)", pModuleFindHandle(&m, hme), &m);
    me_line("ModuleFindHandle(own|1)", pModuleFindHandle(&m, hme | 1), &m);
    me_line("ModuleFindHandle(own DS)", pModuleFindHandle(&m, dsel), &m);
    me_line("ModuleFindHandle(own task)", pModuleFindHandle(&m, (WORD)me), &m);
    me_line("ModuleFindHandle(0)", pModuleFindHandle(&m, 0), &m);
    me_line("ModuleFindHandle(FFFF)", pModuleFindHandle(&m, 0xFFFF), &m);
    me_line("ModuleFindHandle(KERNEL)", pModuleFindHandle(&m, hk), &m);
    {
        HINSTANCE d = LoadLibrary("C:\\THD.DLL");
        lg("LoadLibrary(THD): %d", (WORD)d >= 32);
        namemod("THD");
        me_line("ModuleFindName(THD)", pModuleFindName(&m, "THD"), &m);
        lg("last module: %s", (LPSTR)"see list");
        pat(&m, sizeof m);
        m.dwSize = sizeof m;
        n = 0;
        for (more = pModuleFirst(&m); more && n < 200; more = pModuleNext(&m))
            n++;
        lg("modules with THD: %s last", (LPSTR)m.szModule);
        unname(GetModuleHandle("THD"));
        FreeLibrary(d);
    }
}

/* ==== t: tasks ==== */
static void te_line(const char *what, BOOL ok, TASKENTRY FAR *t)
{
    WORD dg;
    if (!ok) {
        lg("%s: 0", (LPSTR)what);
        return;
    }
    dg = t->hInst;
    lg("%s: 1 task=%s parent=%s inst=%s module=%s name=%s events=%u psp=%s",
       (LPSTR)what, (LPSTR)hn(t->hTask), (LPSTR)hn(t->hTaskParent), (LPSTR)hn(t->hInst),
       (LPSTR)hn(t->hModule), (LPSTR)t->szModule, t->wcEvents, (LPSTR)hn(t->wPSPOffset));
    lg("%s: ss==inst %d queue==GetTaskQueue %d next=%s", (LPSTR)what, t->wSS == t->hInst,
       t->hQueue == kGetTaskQueue(t->hTask), (LPSTR)hn(t->hNext));
    if (dg && lsl(dg | 1) > 0x10)
        lg("%s: stack top/min/bottom == DGROUP 0A/0C/0E %d %d %d", (LPSTR)what,
           t->wStackTop == peekw(dg | 1, 0x0A), t->wStackMinimum == peekw(dg | 1, 0x0C),
           t->wStackBottom == peekw(dg | 1, 0x0E));
    if (t->hTask != me)
        lg("%s: ss:sp==TDB 02h %d", (LPSTR)what, t->wSP == peekw(t->hTask, 2) &&
           t->wSS == peekw(t->hTask, 4));
    lg("%%%s: %04X %04X %04X %04X ss %04X sp %04X %04X %04X %04X q %04X next %04X", (LPSTR)what,
       t->hTask, t->hTaskParent, t->hInst, t->hModule, t->wSS, t->wSP, t->wStackTop,
       t->wStackMinimum, t->wStackBottom, t->hQueue, t->hNext);
}

static void t_tasks(void)
{
    TASKENTRY t;
    WORD n = 0;
    BOOL more;
    lg("== t tasks");
    if (!start_child("w"))
        lg("child: none");
    pat(&t, sizeof t);
    t.dwSize = sizeof t;
    for (more = pTaskFirst(&t); more && n < 50; more = pTaskNext(&t)) {
        n++;
        te_line(t.szModule, 1, &t);
    }
    lg("tasks: %u == GetNumTasks %d", n, n == GetNumTasks());
    lg("TaskNext past the end: %d", pTaskNext(&t) != 0);
    pat(&t, sizeof t);
    t.dwSize = sizeof t;
    te_line("TaskFindHandle(own)", pTaskFindHandle(&t, (WORD)me), &t);
    te_line("TaskFindHandle(child)", pTaskFindHandle(&t, (WORD)child), &t);
    if (pTaskFindHandle(&t, (WORD)child))
        lg("TaskNext after FindHandle(child): %d %s", pTaskNext(&t) != 0, (LPSTR)t.szModule);
    te_line("TaskFindHandle(0)", pTaskFindHandle(&t, 0), &t);
    te_line("TaskFindHandle(module)", pTaskFindHandle(&t, hme), &t);
    te_line("TaskFindHandle(own DS)", pTaskFindHandle(&t, dsel), &t);
    te_line("TaskFindHandle(FFFF)", pTaskFindHandle(&t, 0xFFFF), &t);
    te_line("TaskFindHandle(own|1)", pTaskFindHandle(&t, (WORD)me | 1), &t);
    end_child(7);
}

/* ==== s: stack traces ==== */
extern void __cdecl __far st_run(void);
extern void __cdecl __far st_addrs(WORD FAR *);

static void ste_line(const char *what, int i, BOOL ok, STACKTRACEENTRY FAR *s, WORD FAR *ra)
{
    const char *ip = "?";
    int k;
    if (!ok) {
        lg("%s %d: 0", (LPSTR)what, i);
        return;
    }
    if (ra && SELECTOROF((void FAR *)st_run) == s->wCS)
        for (k = 0; k < 4; k++)
            if (ra[k] == s->wIP)
                ip = k == 0 ? "ret st_run" : k == 1 ? "ret st_far_even" : k == 2 ? "ret st_near" : "here";
    lg("%s %d: 1 task=%s ss=%s cs=%s(%s) ip=%s module=%s seg=%u flags=%u", (LPSTR)what, i,
       (LPSTR)hn(s->hTask), (LPSTR)hn(s->wSS), (LPSTR)hn(s->wCS), (LPSTR)modof(s->wCS),
       (LPSTR)ip, (LPSTR)hn(s->hModule), s->wSegment, s->wFlags);
    lg("%%%s %d: %04X:%04X bp %04X", (LPSTR)what, i, s->wCS, s->wIP, s->wBP);
}

static WORD ra[4];

static void dual_walk(WORD ss, WORD cs, WORD ip, WORD bp);

void __cdecl __far st_walk(WORD ss, WORD cs, WORD ip, WORD bp)
{
    STACKTRACEENTRY st;
    int n = 0;
    BOOL more;
    if (dual_st) {
        dual_walk(ss, cs, ip, bp);
        return;
    }
    pat(&st, sizeof st);
    st.dwSize = sizeof st;
    for (more = pStackTraceCSIPFirst(&st, ss, cs, ip, bp); n < 12; more = pStackTraceNext(&st)) {
        ste_line("CSIP trace", n, more, &st, ra);
        n++;
        if (!more)
            break;
    }
}

static void t_stack(void)
{
    STACKTRACEENTRY st;
    int n;
    BOOL more;
    lg("== s stack traces");
    {
        /* where a task's chain of frames starts: BP after InitTask */
        extern WORD __cdecl init_bpsp[2], entry_regs[11];
        WORD bp = init_bpsp[0], sp = init_bpsp[1];
        lg("InitTask: BP %s SP, at entry BP=%04X", (LPSTR)(bp == 0 ? "0," : bp > sp ? "above" : "below"),
           entry_regs[6]);
        if (bp > sp)
            lg("InitTask: BP - SP = %u, [BP] %04X, [BP+2] %s", bp - sp, peekw(dsel, bp),
               (LPSTR)(peekw(dsel, bp + 4) == SELECTOROF((void FAR *)st_run) ? "our CS" : "?"));
        lg("%%InitTask: BP %04X SP %04X", bp, sp);
    }
    st_addrs(ra);
    namesegs(hme, "seg");
    st_run();
    pat(&st, sizeof st);
    st.dwSize = sizeof st;
    lg("StackTraceFirst(own): %d", pStackTraceFirst(&st, (WORD)me) != 0);
    lg("StackTraceFirst(0): %d", pStackTraceFirst(&st, 0) != 0);
    lg("StackTraceFirst(module): %d", pStackTraceFirst(&st, hme) != 0);
    lg("StackTraceCSIPFirst(cs 0): %d", pStackTraceCSIPFirst(&st, dsel, 0, 0, 0) != 0);
    if (start_child("w")) {
        pat(&st, sizeof st);
        st.dwSize = sizeof st;
        n = 0;
        for (more = pStackTraceFirst(&st, (WORD)child); n < 20; more = pStackTraceNext(&st)) {
            ste_line("child trace", n, more, &st, NULL);
            n++;
            if (!more)
                break;
        }
        end_child(7);
    }
}

/* ==== c: classes ==== */
LRESULT CALLBACK __export wproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

static void t_classes(void)
{
    CLASSENTRY ce;
    WNDCLASS wc;
    WORD n = 0;
    BOOL more;
    HINSTANCE hi = (HINSTANCE)dsel;
    lg("== c classes");
    {
        /* what ClassFirst builds on: UserSeeUserDo (USER.216) */
        DWORD (FAR PASCAL *usud)(WORD, WORD, WORD, WORD) = (DWORD (FAR PASCAL *)(WORD, WORD, WORD, WORD))
            GetProcAddress(hu, MAKEINTRESOURCE(216));
        DWORD r;
        SYSHEAPINFO sh;
        pat(&sh, sizeof sh);
        sh.dwSize = sizeof sh;
        pSystemHeapInfo(&sh);
        nameblk(sh.hUserSegment, "userdg");
        r = usud(4, 0, 0, 0);
        lg("UserSeeUserDo(4): %s hi=%04X", (LPSTR)hn(LOWORD(r)), HIWORD(r));
        r = usud(5, 0, 0, 0);
        lg("UserSeeUserDo(5): %s hi=%04X", (LPSTR)(LOWORD(r) ? "set" : "0"), HIWORD(r));
        r = usud(6, 0, 0, 0);
        lg("UserSeeUserDo(6): %08lX", r);
        r = usud(0, 0, 0, 0);
        lg("UserSeeUserDo(0): %08lX", r);
    }
    wc.style = 0;
    wc.lpfnWndProc = wproc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "ThpClassA";
    lg("RegisterClass A: %d", RegisterClass(&wc) != 0);
    wc.lpszClassName = "ThpClassB";
    wc.style = CS_GLOBALCLASS;
    wc.cbClsExtra = 6;
    lg("RegisterClass B: %d", RegisterClass(&wc) != 0);
    pat(&ce, sizeof ce);
    ce.dwSize = sizeof ce;
    for (more = pClassFirst(&ce); more && n < 200; more = pClassNext(&ce)) {
        n++;
        lg("class %s inst=%s", (LPSTR)ce.szClassName, (LPSTR)hn(ce.hInst));
        lg("%%class %s %04X next %04X", (LPSTR)ce.szClassName, ce.hInst, ce.wNext);
        if (n == 1 || !ce.wNext)
            lg("class %s next %s", (LPSTR)ce.szClassName, (LPSTR)(ce.wNext ? "set" : "0"));
    }
    lg("classes: %u", n);
    lg("ClassNext past the end: %d", pClassNext(&ce) != 0);
    UnregisterClass("ThpClassA", hi);
    n = 0;
    pat(&ce, sizeof ce);
    ce.dwSize = sizeof ce;
    for (more = pClassFirst(&ce); more && n < 200; more = pClassNext(&ce))
        if (!lstrcmp(ce.szClassName, "ThpClassA"))
            n++;
    lg("A after UnregisterClass: %u", n);
    UnregisterClass("ThpClassB", hi);
}

/* ==== i: information ==== */
static void t_info(void)
{
    SYSHEAPINFO sh;
    MEMMANINFO mm;
    TIMERINFO ti, t2;
    DWORD tick, v;
    WORD i, minstep = 0xFFFF;
    lg("== i information");
    pat(&sh, sizeof sh);
    sh.dwSize = sizeof sh;
    lg("SystemHeapInfo: %d", pSystemHeapInfo(&sh) != 0);
    lg("SystemHeapInfo: user seg=%s gdi seg=%s", (LPSTR)hn(sh.hUserSegment), (LPSTR)hn(sh.hGDISegment));
    {
        WORD u = (WORD)GetWindowWord(GetDesktopWindow(), GWW_HINSTANCE);
        lg("SystemHeapInfo: user seg vs USER's DS: %s", (LPSTR)(sh.hUserSegment == u ? "same" :
           (sh.hUserSegment | 1) == (u | 1) ? "handle" : "other"));
    }
    lg("SystemHeapInfo: user %% == GetFreeSystemResources(USER) %d",
       sh.wUserFreePercent == GetFreeSystemResources(GFSR_USERRESOURCES));
    lg("SystemHeapInfo: gdi %% == GetFreeSystemResources(GDI) %d",
       sh.wGDIFreePercent == GetFreeSystemResources(GFSR_GDIRESOURCES));
    lg("%%SystemHeapInfo: %u %u %04X %04X", sh.wUserFreePercent, sh.wGDIFreePercent,
       sh.hUserSegment, sh.hGDISegment);
    v = kGetHeapSpaces(hu);
    lg("%%GetHeapSpaces(USER) %u/%u", LOWORD(v), HIWORD(v));
    pat(&mm, sizeof mm);
    mm.dwSize = sizeof mm;
    i = pMemManInfo(&mm) != 0;
    lg("MemManInfo: %d page=%u", i, mm.wPageSize);
    lg("MemManInfo: largest/page==maxpages %d", mm.dwMaxPagesAvailable == mm.dwLargestFreeBlock / 4096);
    lg("MemManInfo: largest==GlobalCompact(0)+? %s", (LPSTR)(mm.dwLargestFreeBlock ? "nonzero" : "0"));
    lg("%%MemManInfo: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
       mm.dwLargestFreeBlock, mm.dwMaxPagesAvailable, mm.dwMaxPagesLockable,
       mm.dwTotalLinearSpace, mm.dwTotalUnlockedPages, mm.dwFreePages, mm.dwTotalPages,
       mm.dwFreeLinearSpace, mm.dwSwapFilePages);
    pat(&ti, sizeof ti);
    ti.dwSize = sizeof ti;
    tick = GetTickCount();
    lg("TimerCount: %d", pTimerCount(&ti) != 0);
    lg("TimerCount: this VM == since start %d", ti.dwmsThisVM == ti.dwmsSinceStart);
    lg("TimerCount: since start - GetTickCount in 0..60 %d",
       ti.dwmsSinceStart >= tick && ti.dwmsSinceStart - tick <= 60);
    lg("%%TimerCount: %lu %lu tick %lu", ti.dwmsSinceStart, ti.dwmsThisVM, tick);
    pat(&t2, sizeof t2);
    t2.dwSize = sizeof t2;
    for (i = 0; i < 2000; i++) {
        pTimerCount(&t2);
        if (t2.dwmsSinceStart < ti.dwmsSinceStart)
            minstep = 0;
        else if (t2.dwmsSinceStart != ti.dwmsSinceStart) {
            if (t2.dwmsSinceStart - ti.dwmsSinceStart < minstep)
                minstep = (WORD)(t2.dwmsSinceStart - ti.dwmsSinceStart);
            ti = t2;
        }
    }
    lg("TimerCount: smallest step <= 5 ms %d", minstep <= 5);
    lg("%%TimerCount: smallest step %u", minstep);
}

/* ==== r: MemoryRead and MemoryWrite ==== */

/* a byte of a huge block at a 32-bit offset */
static BYTE FAR *hp(WORD sel, DWORD off)
{
    return (BYTE FAR *)MAKELP(sel + HIWORD(off) * get_ahincr(), LOWORD(off));
}

extern BYTE __far __cdecl code_area[]; /* thps.asm: in our code segment */

static void t_memory(void)
{
    static BYTE b[64];
    BYTE FAR *p;
    WORD hs, i;
    DWORD r;
    lg("== r MemoryRead and MemoryWrite");
    need_ga();
    p = (BYTE FAR *)GlobalLock(ga[5]);
    hs = SELECTOROF(p);
    for (i = 0; i < 16; i++) {
        p[i] = (BYTE)i;
        *hp(hs, 65530L + i) = (BYTE)(0x80 + i);
    }
    r = pMemoryRead(hs, 0, b, 16);
    lg("MemoryRead(huge,0,16): %lu data %d", r, b[5] == 5);
    r = pMemoryRead(hs, 65530L, b, 16);
    lg("MemoryRead(huge,65530,16): %lu data %d %d", r, b[0] == 0x80, b[15] == 0x8F);
    r = pMemoryRead(hs, 69990L, b, 32);
    lg("MemoryRead(huge,69990,32): %lu", r);
    r = pMemoryRead(hs, 70100L, b, 4);
    lg("MemoryRead(huge,70100,4): %lu", r);
    r = pMemoryRead(hs + get_ahincr(), 0, b, 4);
    lg("MemoryRead(tile 2,0,4): %lu", r);
    r = pMemoryRead(LOWORD(GlobalHandle(hs)), 0, b, 4);
    lg("MemoryRead(handle,0,4): %lu", r);
    r = pMemoryRead(dsel, 0x10, b, 0);
    lg("MemoryRead(DS,10,0): %lu", r);
    r = pMemoryRead(dsel, lsl(dsel) - 3, b, 8);
    lg("MemoryRead(DS,limit-3,8): %lu", r);
    r = pMemoryRead(dsel, lsl(dsel) + 1, b, 8);
    lg("MemoryRead(DS,limit+1,8): %lu", r);
    r = pMemoryRead(0, 0, b, 4);
    lg("MemoryRead(0): %lu", r);
    r = pMemoryRead(0xFFFF, 0, b, 4);
    lg("MemoryRead(FFFF): %lu", r);
    {
        WORD a = AllocSelector(dsel);
        r = pMemoryRead(a, 0, b, 4);
        lg("MemoryRead(alias): %lu", r);
        FreeSelector(a);
    }
    r = pMemoryRead(SELECTOROF((void FAR *)t_memory), 0, b, 8);
    lg("MemoryRead(own CS,0,8): %lu same %d", r, b[0] == *(BYTE FAR *)MAKELP(SELECTOROF((void FAR *)t_memory), 0));
    r = pMemoryRead((WORD)ga[3], 0, b, 4);
    lg("MemoryRead(discarded): %lu", r);
    r = pMemoryRead(LOWORD(kGlobalMasterHandle()), 0, b, 4);
    lg("MemoryRead(BurgerMaster,0,4): %lu", r);
    for (i = 0; i < 8; i++)
        b[i] = (BYTE)(0xA0 + i);
    r = pMemoryWrite(hs, 65532L, b, 8);
    lg("MemoryWrite(huge,65532,8): %lu data %d %d", r,
       *hp(hs, 65532L) == 0xA0, *hp(hs, 65539L) == 0xA7);
    r = pMemoryWrite(dsel, lsl(dsel) - 1, b, 4);
    lg("MemoryWrite(DS,limit-1,4): %lu", r);
    {
        /* a code segment: read-only through CS */
        WORD cs = SELECTOROF((void FAR *)code_area), off = OFFSETOF((void FAR *)code_area);
        r = pMemoryWrite(cs, off, b, 4);
        lg("MemoryWrite(own CS): %lu data %d", r, code_area[0] == 0xA0 && code_area[3] == 0xA3);
    }
    r = pMemoryWrite(0, 0, b, 4);
    lg("MemoryWrite(0): %lu", r);
    GlobalUnlock(ga[5]);
}

/* ==== n: notifications ==== */
typedef struct { WORD id, lo, hi, task, a, b, c, d, e; char s[24]; } NREC;
static NREC nrec[120];
static WORD ncount, nstop;

BOOL __far __pascal __loadds __export ncb(WORD id, DWORD data)
{
    NREC FAR *e;
    if (ncount >= 120)
        return FALSE;
    e = &nrec[ncount++];
    e->id = id;
    e->lo = LOWORD(data);
    e->hi = HIWORD(data);
    e->task = (WORD)GetCurrentTask();
    e->a = e->b = e->c = e->d = e->e = 0;
    e->s[0] = 0;
    switch (id) {
    case NFY_LOADSEG: {
        NFYLOADSEG FAR *p = (NFYLOADSEG FAR *)data;
        e->a = (WORD)p->dwSize;
        e->b = p->wSelector;
        e->c = p->wSegNum;
        e->d = p->wType;
        e->e = p->wcInstance;
        lstrcpyn(e->s, p->lpstrModuleName, 20);
        break;
    }
    case NFY_STARTDLL: {
        NFYSTARTDLL FAR *p = (NFYSTARTDLL FAR *)data;
        e->a = (WORD)p->dwSize;
        e->b = p->hModule;
        e->c = p->wCS;
        e->d = p->wIP;
        break;
    }
    case NFY_OUTSTR:
        lstrcpyn(e->s, (LPCSTR)data, 20);
        if (nstop && e->s[0] == '!')
            return TRUE;
        break;
    case NFY_RIP: {
        NFYRIP FAR *p = (NFYRIP FAR *)data;
        e->a = (WORD)p->dwSize;
        e->b = p->wCS;
        e->c = p->wIP;
        e->d = p->wExitCode;
        break;
    }
    case NFY_LOGERROR: {
        NFYLOGERROR FAR *p = (NFYLOGERROR FAR *)data;
        e->a = (WORD)p->dwSize;
        e->b = p->wErrCode;
        e->c = OFFSETOF(p->lpInfo);
        e->d = SELECTOROF(p->lpInfo);
        break;
    }
    case NFY_LOGPARAMERROR: {
        NFYLOGPARAMERROR FAR *p = (NFYLOGPARAMERROR FAR *)data;
        e->a = (WORD)p->dwSize;
        e->b = p->wErrCode;
        e->c = OFFSETOF(p->lpfnErrorAddr);
        e->d = SELECTOROF(p->lpfnErrorAddr);
        e->e = OFFSETOF(p->lpBadParam);
        break;
    }
    case NFY_INCHAR:
        return 'i';
    }
    return FALSE;
}

static const char *const nfyn[] = { "UNKNOWN", "LOADSEG", "FREESEG", "STARTDLL",
    "STARTTASK", "EXITTASK", "DELMODULE", "RIP", "TASKIN", "TASKOUT", "INCHAR",
    "OUTSTR", "LOGERROR", "LOGPARAMERROR" };

static void ndump(const char *what)
{
    WORD i;
    lg("%s: %u notifications", (LPSTR)what, ncount);
    for (i = 0; i < ncount; i++) {
        NREC FAR *e = &nrec[i];
        const char *n = e->id < 14 ? nfyn[e->id] : "?";
        switch (e->id) {
        case NFY_LOADSEG:
            lg("%s %u: LOADSEG size=%u sel=%s seg=%u type=%u inst=%u module=%s task=%s",
               (LPSTR)what, i, e->a, (LPSTR)hn(e->b), e->c, e->d, e->e, (LPSTR)e->s,
               (LPSTR)hn(e->task));
            break;
        case NFY_STARTDLL:
            lg("%s %u: STARTDLL size=%u module=%s cs=%s(%s) task=%s", (LPSTR)what, i, e->a,
               (LPSTR)hn(e->b), (LPSTR)hn(e->c), (LPSTR)modof(e->c), (LPSTR)hn(e->task));
            lg("%%%s %u: %04X:%04X", (LPSTR)what, i, e->c, e->d);
            break;
        case NFY_OUTSTR:
            lg("%s %u: OUTSTR \"%s\" task=%s", (LPSTR)what, i, (LPSTR)e->s, (LPSTR)hn(e->task));
            break;
        case NFY_FREESEG:
        case NFY_DELMODULE: {
            /* a segment a LOADSEG of this dump named */
            char sn[20];
            WORD j;
            lstrcpy(sn, hn(e->lo));
            for (j = 0; j < ncount && sn[0] == '?'; j++)
                if (nrec[j].id == NFY_LOADSEG && nrec[j].b == e->lo)
                    wsprintf(sn, "%s.seg%u", (LPSTR)nrec[j].s, nrec[j].c);
            lg("%s %u: %s %s task=%s", (LPSTR)what, i, (LPSTR)n, (LPSTR)sn,
               (LPSTR)hn(e->task));
        }
            lg("%%%s %u: %04X %04X", (LPSTR)what, i, e->lo, e->hi);
            break;
        case NFY_EXITTASK:
            lg("%s %u: EXITTASK code=%u hi=%u task=%s", (LPSTR)what, i, e->lo & 0xFF,
               e->hi, (LPSTR)hn(e->task));
            lg("%%%s %u: %04X %04X", (LPSTR)what, i, e->lo, e->hi);
            break;
        case NFY_STARTTASK:
            lg("%s %u: STARTTASK cs=%s(%s) task=%s", (LPSTR)what, i, (LPSTR)hn(e->hi),
               (LPSTR)modof(e->hi), (LPSTR)hn(e->task));
            lg("%%%s %u: %04X:%04X", (LPSTR)what, i, e->hi, e->lo);
            break;
        case NFY_RIP:
            lg("%s %u: RIP size=%u cs=%s code=%04X task=%s", (LPSTR)what, i, e->a,
               (LPSTR)modof(e->b), e->d, (LPSTR)hn(e->task));
            break;
        case NFY_LOGERROR:
            lg("%s %u: LOGERROR size=%u code=%04X info=%04X:%04X task=%s", (LPSTR)what, i,
               e->a, e->b, e->d, e->c, (LPSTR)hn(e->task));
            break;
        case NFY_LOGPARAMERROR:
            lg("%s %u: LOGPARAMERROR size=%u code=%04X addr=%s param=%04X task=%s",
               (LPSTR)what, i, e->a, e->b, (LPSTR)modof(e->d), e->e, (LPSTR)hn(e->task));
            break;
        default:
            lg("%s %u: %s task=%s", (LPSTR)what, i, (LPSTR)n, (LPSTR)hn(e->task));
            lg("%%%s %u: %04X %04X", (LPSTR)what, i, e->lo, e->hi);
        }
    }
    ncount = 0;
}

static void n_script(const char *what)
{
    HINSTANCE d;
    FARPROC f;
    WORD hthd;
    ncount = 0;
    OutputDebugString("THP one\r\n");
    OutputDebugString("THP two");
    d = LoadLibrary("C:\\THD.DLL");
    namemod("THD");
    f = GetProcAddress(d, "THDFAR");
    if (f)
        ((void (FAR PASCAL *)(void))f)();
    hthd = GetModuleHandle("THD");
    FreeLibrary(d);
    {
        char b[40];
        lstrcpy(b, what);
        lstrcat(b, " library");
        ndump(b);
    }
    unname(hthd);
    if (start_child("x")) {
        wait_gone(child);
        unname(child);
        child = 0;
    }
    {
        char b[40];
        lstrcpy(b, what);
        lstrcat(b, " task");
        ndump(b);
    }
}

static void t_notify(void)
{
    FARPROC cb = MakeProcInstance((FARPROC)ncb, (HINSTANCE)dsel);
    lg("== n notifications");
    {
        /* what KERNEL itself reports while THD is loaded and freed
           (ToolHelpHook, KERNEL.341, with a recording procedure) */
        extern WORD __cdecl raw_count, raw_rec[64 * 4];
        extern void __cdecl __far raw_hook(void);
        DWORD (FAR PASCAL *thh)(FARPROC) = (DWORD (FAR PASCAL *)(FARPROC))kp(341);
        DWORD old;
        HINSTANCE d;
        WORD i, hthd;
        raw_count = 0;
        old = thh((FARPROC)raw_hook);
        d = LoadLibrary("C:\\THD.DLL");
        namemod("THD");
        hthd = GetModuleHandle("THD");
        FreeLibrary(d);
        thh((FARPROC)old);
        lg("raw: %u calls", raw_count);
        for (i = 0; i < raw_count; i++) {
            WORD FAR *r = &raw_rec[i * 4], j;
            char sn[12];
            /* a freed segment by the number its 50h gave (BX + 1) */
            lstrcpy(sn, r[0] == 0x50 ? "-" : hn(r[1]));
            for (j = 0; j < raw_count && r[0] == 0x52; j++)
                if (raw_rec[j * 4] == 0x50 && raw_rec[j * 4 + 2] == r[1])
                    wsprintf(sn, "seg%u", raw_rec[j * 4 + 1] + 1);
            lg("raw %u: %02X bx=%s cx=%s es=%s", i, r[0], (LPSTR)sn,
               (LPSTR)(r[0] == 0x50 ? "-" : hn(r[2])), (LPSTR)hn(r[3]));
            lg("%%raw %u: %04X %04X %04X %04X", i, r[0], r[1], r[2], r[3]);
        }
        unname(hthd);
    }
    lg("NotifyUnRegister(none): %d", pNotifyUnRegister(0) != 0);
    lg("NotifyRegister(NULL proc): %d", pNotifyRegister(0, NULL, NF_NORMAL) != 0);
    pNotifyUnRegister(0);
    lg("NotifyRegister: %d", pNotifyRegister(0, cb, NF_NORMAL) != 0);
    lg("NotifyRegister again: %d", pNotifyRegister(0, cb, NF_NORMAL) != 0);
    n_script("normal");
    /* LogError, LogParamError (KERNEL.324, 325) */
    ncount = 0;
    ((void (FAR PASCAL *)(WORD, void FAR *))kp(324))(0x1234, (void FAR *)MAKELP(0x5678, 0x9ABC));
    ((void (FAR PASCAL *)(WORD, FARPROC, void FAR *))kp(325))(0x6001, (FARPROC)t_notify,
        (void FAR *)MAKELP(0x1111, 0x2222));
    ndump("normal log");
    lg("NotifyUnRegister: %d", pNotifyUnRegister(0) != 0);
    lg("NotifyUnRegister again: %d", pNotifyUnRegister(0) != 0);
    lg("NotifyRegister(TASKSWITCH): %d", pNotifyRegister(0, cb, NF_TASKSWITCH) != 0);
    n_script("taskswitch");
    pNotifyUnRegister(0);
    lg("NotifyRegister(RIP): %d", pNotifyRegister(0, cb, NF_RIP) != 0);
    ncount = 0;
    ((void (FAR PASCAL *)(WORD, void FAR *))kp(324))(0x1234, (void FAR *)MAKELP(0x5678, 0x9ABC));
    ((void (FAR PASCAL *)(WORD, FARPROC, void FAR *))kp(325))(0x6001, (FARPROC)t_notify,
        (void FAR *)MAKELP(0x1111, 0x2222));
    OutputDebugString("THP rip\r\n");
    ndump("rip log");
    pNotifyUnRegister(0);
    /* two callbacks: ours first, then the child's (THC n) */
    lg("NotifyRegister: %d", pNotifyRegister(0, cb, NF_NORMAL) != 0);
    if (start_child("n")) {
        ncount = 0;
        nstop = 1;
        OutputDebugString("plain\r\n");
        OutputDebugString("!stop\r\n");
        nstop = 0;
        spin(5);
        ndump("chain");
        end_child(7);
        ndump("chain end");
    }
    /* not tried: a callback whose task ended without NotifyUnRegister
       (THC m): under Windows 3.1 the next notification ended the probe
       (Microsoft's TOOLHELP still called it); w16's drops an ended
       task's callbacks */
    pNotifyUnRegister(0);
}

/* ==== x: interrupts and faults ==== */

extern WORD __cdecl int_mode, int_skip, int_count, int_rec[16 * 16], int_tag;
extern DWORD __cdecl int_task_fn, int_term_fn;
extern void __cdecl __far int_cb(void);
extern void __cdecl __far do_fault(WORD kind);
extern WORD __cdecl __far fault_len(WORD kind);
extern WORD __cdecl __far fault_ip(WORD kind);

static void idump(const char *what)
{
    WORD i;
    lg("%s: %u callbacks", (LPSTR)what, int_count);
    for (i = 0; i < int_count; i++) {
        WORD FAR *r = &int_rec[i * 16];
        const char *ip = "?";
        WORD k;
        static const char kinds[] = "dgnu3si";
        for (k = 0; kinds[k]; k++)
            if (r[6] == fault_ip(kinds[k]))
                ip = "fault";
            else if (r[6] == fault_ip(kinds[k]) + 1 && kinds[k] == '3')
                ip = "after int3";
        lg("%s %u: number=%04X ax=%04X fault=%s(%s):%s flags=%04X task=%s entry-ax=%s",
           (LPSTR)what, i, r[4], r[3], (LPSTR)hn(r[7]), (LPSTR)modof(r[7]),
           (LPSTR)ip, r[8] & 0x0FD5, (LPSTR)hn(r[14]), (LPSTR)hn(r[0]));
        lg("%%%s %u: ret %04X:%04X handle %04X fault %04X:%04X flags %04X ss:sp %04X:%04X %04X:%04X",
           (LPSTR)what, i, r[2], r[1], r[5], r[7], r[6], r[8], r[12], r[13], r[10], r[9]);
    }
    int_count = 0;
}

static void x_child(const char *what, char kind, WORD mode)
{
    char c[8];
    lflush();
    c[0] = 'f';
    c[1] = kind;
    c[2] = 0;
    int_mode = mode;
    int_skip = fault_len(kind);
    int_count = 0;
    ncount = 0;
    if (start_child(c)) {
        namesegs(hchild, "c.seg");
        wait_gone(child);
        unname(child);
        child = 0;
    }
    idump(what);
    ndump(what);
}

static void t_interrupt(void)
{
    FARPROC cb = (FARPROC)int_cb;
    FARPROC ncbp = MakeProcInstance((FARPROC)ncb, (HINSTANCE)dsel);
    lg("== x interrupts and faults");
    int_task_fn = (DWORD)GetProcAddress(hk, MAKEINTRESOURCE(36));
    int_term_fn = (DWORD)thf[77];
    lg("InterruptUnRegister(none): %d", pInterruptUnRegister(0) != 0);
    lg("InterruptRegister(NULL): %d", pInterruptRegister(0, NULL) != 0);
    pInterruptUnRegister(0);
    lg("InterruptRegister: %d", pInterruptRegister(0, cb) != 0);
    lg("InterruptRegister again: %d", pInterruptRegister(0, cb) != 0);
    pNotifyRegister(0, ncbp, NF_NORMAL);
    /* the child's: restarted, ended (TerminateApp), passed on to the
       child's own callback (THC f.c registers one that ends it) */
    x_child("child div restarted", 'd', 1);
    x_child("child gp restarted", 'g', 1);
    x_child("child null restarted", 'n', 1);
    x_child("child opcode restarted", 'u', 1);
    x_child("child int3 restarted", '3', 1);
    x_child("child stack restarted", 's', 1);
    x_child("child gp terminated", 'g', 2);
    x_child("child div terminated", 'd', 2);
    x_child("child gp chained", 'G', 0);
    x_child("child div chained", 'D', 0);
    /* our own faults, restarted, one at a time */
    {
        static const char kinds[] = "d3";
        WORD i;
        namesegs(hme, "seg");
        for (i = 0; kinds[i]; i++) {
            lg("own fault %c", kinds[i]);
            int_mode = 1;
            int_count = 0;
            int_skip = fault_len(kinds[i]);
            do_fault(kinds[i]);
            idump("own restarted");
        }
    }
    ncount = 0;
    pNotifyUnRegister(0);
    lg("InterruptUnRegister: %d", pInterruptUnRegister(0) != 0);
    lg("InterruptUnRegister again: %d", pInterruptUnRegister(0) != 0);
}

/* ==== k: task control ==== */
extern DWORD __cdecl sw_back;
extern WORD __cdecl sw_count, sw_rec[8 * 4];
extern void __cdecl __far sw_routine(void);

static void swdump(const char *what)
{
    WORD i;
    lg("%s: %u runs", (LPSTR)what, sw_count);
    for (i = 0; i < sw_count; i++)
        lg("%s %u: task=%s ss=%s ds=%s", (LPSTR)what, i, (LPSTR)hn(sw_rec[i * 4]),
           (LPSTR)hn(sw_rec[i * 4 + 1]), (LPSTR)hn(sw_rec[i * 4 + 3]));
    sw_count = 0;
}

static void t_taskctl(void)
{
    DWORD csip, old, r;
    FARPROC ncbp = MakeProcInstance((FARPROC)ncb, (HINSTANCE)dsel);
    WORD rcs = SELECTOROF((void FAR *)sw_routine), rip = OFFSETOF((void FAR *)sw_routine);
    lg("== k task control");
    int_task_fn = (DWORD)GetProcAddress(hk, MAKEINTRESOURCE(36));
    namesegs(hme, "seg");
    pNotifyRegister(0, ncbp, NF_NORMAL);
    lg("TaskGetCSIP(own): %08lX", pTaskGetCSIP((WORD)me));
    /* TaskGetCSIP(0) gave 00800000 under 3.1's KRNL286 and a call with a
       module handle ended the probe there: not made; nor TaskSetCSIP and
       TaskSwitch on the running task */
    if (start_child("w")) {
        /* the frame the scheduler left on the child's stack (TDB 02h) */
        {
            WORD ss = peekw(child, 4), sp = peekw(child, 2), i, w;
            WORD ds = peekw(child, 0x1C);
            nameit(peekw(child, 0x1E), "M:THC");
            for (i = 0; i < 12; i++) {
                w = peekw(ss, sp + 2 * i);
                lg("child frame +%02X: %s%s%s", 2 * i, (LPSTR)hn(w),
                   (LPSTR)(w == (ds | 1) || w == (ds & ~1) || w == ds ? " (its DS)" : ""),
                   (LPSTR)(w && lsl(w) && (lsl(w) > 0x40) && !lstrcmp(modof(w), "KERNEL") ? " (KERNEL)" : ""));
                lg("%%child frame +%02X: %04X", 2 * i, w);
            }
        }
        csip = pTaskGetCSIP((WORD)child);
        lg("TaskGetCSIP(child): cs=%s ip==TDB frame %d", (LPSTR)modof(HIWORD(csip)),
           csip == *(DWORD FAR *)MAKELP(peekw(child, 4), peekw(child, 2) + 0x12));
        lg("%%TaskGetCSIP(child): %08lX", csip);
        old = pTaskSetCSIP((WORD)child, rcs, rip);
        lg("TaskSetCSIP(child): returns the old %d", old == csip);
        r = pTaskGetCSIP((WORD)child);
        lg("TaskGetCSIP after Set: ours %d", r == MAKELONG(rip, rcs));
        r = pTaskSetCSIP((WORD)child, HIWORD(csip), LOWORD(csip));
        lg("TaskSetCSIP back: returns ours %d", r == MAKELONG(rip, rcs));
        /* the child runs our routine when it is next run */
        sw_back = csip;
        sw_count = 0;
        pTaskSetCSIP((WORD)child, rcs, rip);
        PostAppMessage(child, WM_USER, 0, 0);
        spin(10);
        swdump("TaskSetCSIP run");
        /* TaskSwitch */
        sw_count = 0;
        csip = pTaskGetCSIP((WORD)child);
        sw_back = csip;
        ncount = 0;
        r = pTaskSwitch((WORD)child, (DWORD)(FARPROC)sw_routine);
        lg("TaskSwitch(child): %d", r != 0);
        swdump("TaskSwitch run");
        lg("TaskGetCSIP after TaskSwitch: same %d", pTaskGetCSIP((WORD)child) == csip);
        ndump("TaskSwitch");
        /* TerminateApp */
        ncount = 0;
        pTerminateApp((WORD)child, NO_UAE_BOX);
        lg("TerminateApp(child) returned; child gone %d", wait_gone(child));
        ndump("TerminateApp");
        unname(child);
        child = 0;
    }
    pNotifyUnRegister(0);
}

/* ==== d: two TOOLHELPs side by side ====
   TOOLHELP.DLL (A) and C:\TOOLHLPM.DLL (B: another TOOLHELP under the
   module name TOOLHLPM, made at test time by tests/toolhelp/thrun.sh)
   asked the same questions in the same state, every structure compared
   byte for byte; the first differences are logged with both structures
   (byte A/B where they differ). */
static WORD dmis;
static char dname[80];

static void dshow(const void FAR *a, const void FAR *b, WORD n)
{
    const BYTE FAR *x = a, *y = b;
    static char t[400];
    WORD i, k;
    if (++dmis > 80)
        return;
    k = wsprintf(t, "d %s differs:", (LPSTR)dname);
    for (i = 0; i < n && k < 380; i++)
        k += x[i] == y[i] ? wsprintf(t + k, " %02X", x[i]) : wsprintf(t + k, " %02X/%02X", x[i], y[i]);
    lg("%s", (LPSTR)t);
}

static void dcmp(BOOL ra, BOOL rb, void FAR *a, void FAR *b, WORD n)
{
    const BYTE FAR *x = a, *y = b;
    WORD i;
    if ((ra != 0) != (rb != 0)) {
        lstrcat(dname, ra ? " (A only)" : " (B only)");
        dshow(a, b, n);
        return;
    }
    if (!ra)
        return;
    for (i = 0; i < n; i++)
        if (x[i] != y[i]) {
            dshow(a, b, n);
            return;
        }
}

static void dprep(void FAR *a, void FAR *b, WORD n)
{
    pat(a, n);
    pat(b, n);
    *(DWORD FAR *)a = n;
    *(DWORD FAR *)b = n;
}

#define USEA    (thf = thfA)
#define USEB    (thf = thfB)

static WORD dblk[1200];

static WORD dwalkg(WORD flags, const char *name)
{
    GLOBALENTRY a, b;
    BOOL ra, rb;
    WORD n = 0;
    dprep(&a, &b, sizeof a);
    USEA; ra = pGlobalFirst(&a, flags);
    USEB; rb = pGlobalFirst(&b, flags);
    USEA;
    while ((ra || rb) && n < 4000) {
        wsprintf(dname, "Global %s %u", (LPSTR)name, n);
        dcmp(ra, rb, &a, &b, sizeof a);
        if (flags == GLOBAL_ALL && n < 1200)
            dblk[n] = ra ? a.hBlock : 0;
        n++;
        if (!ra || !rb)
            break;
        USEA; ra = pGlobalNext(&a, flags);
        USEB; rb = pGlobalNext(&b, flags);
        USEA;
    }
    lg("d walk %s: %u entries", (LPSTR)name, n);
    return n;
}

static void dlocal(WORD h)
{
    LOCALINFO la, lb;
    LOCALENTRY ea, eb;
    BOOL ra, rb;
    WORD m = 0;
    dprep(&la, &lb, sizeof la);
    USEA; ra = pLocalInfo(&la, h);
    USEB; rb = pLocalInfo(&lb, h);
    USEA;
    wsprintf(dname, "LocalInfo(%04X)", h);
    dcmp(ra, rb, &la, &lb, sizeof la);
    dprep(&ea, &eb, sizeof ea);
    USEA; ra = pLocalFirst(&ea, h);
    USEB; rb = pLocalFirst(&eb, h);
    USEA;
    while ((ra || rb) && m < 3000) {
        wsprintf(dname, "Local(%04X) %u", h, m);
        dcmp(ra, rb, &ea, &eb, sizeof ea);
        m++;
        if (!ra || !rb)
            break;
        USEA; ra = pLocalNext(&ea);
        USEB; rb = pLocalNext(&eb);
        USEA;
    }
}

static void dual_walk(WORD ss, WORD cs, WORD ip, WORD bp)
{
    STACKTRACEENTRY a, b;
    BOOL ra, rb;
    WORD n = 0;
    dprep(&a, &b, sizeof a);
    USEA; ra = pStackTraceCSIPFirst(&a, ss, cs, ip, bp);
    USEB; rb = pStackTraceCSIPFirst(&b, ss, cs, ip, bp);
    USEA;
    while ((ra || rb) && n < 20) {
        wsprintf(dname, "StackTrace %u", n);
        dcmp(ra, rb, &a, &b, sizeof a);
        n++;
        if (!ra || !rb)
            break;
        USEA; ra = pStackTraceNext(&a);
        USEB; rb = pStackTraceNext(&b);
        USEA;
    }
    lg("d stack frames: %u", n);
}

static void t_dual(void)
{
    BOOL ra, rb;
    WORD i, n, k, nm = 0;
    WORD hm[100];
    HINSTANCE hb = LoadLibrary("C:\\TOOLHLPM.DLL");
    lg("== d two TOOLHELPs");
    if ((WORD)hb < 32) {
        lg("d: no second TOOLHELP (%u)", (WORD)hb);
        return;
    }
    for (i = 1; i < 84; i++)
        thfB[i] = GetProcAddress(hb, MAKEINTRESOURCE(i));
    need_ga();
    dmis = 0;
    bulk = 1;
    {
        GLOBALINFO a, b;
        dprep(&a, &b, sizeof a);
        USEA; ra = pGlobalInfo(&a);
        USEB; rb = pGlobalInfo(&b);
        USEA;
        lstrcpy(dname, "GlobalInfo");
        dcmp(ra, rb, &a, &b, sizeof a);
    }
    n = dwalkg(GLOBAL_ALL, "ALL");
    dwalkg(GLOBAL_LRU, "LRU");
    dwalkg(GLOBAL_FREE, "FREE");
    for (i = 0; i < n && i < 1200; i++) {
        GLOBALENTRY a, b;
        if (!dblk[i])
            continue;
        for (k = 0; k < 2; k++) {
            WORD h = k ? dblk[i] | 1 : dblk[i];
            dprep(&a, &b, sizeof a);
            USEA; ra = pGlobalEntryHandle(&a, h);
            USEB; rb = pGlobalEntryHandle(&b, h);
            USEA;
            wsprintf(dname, "GlobalEntryHandle(%04X)", h);
            dcmp(ra, rb, &a, &b, sizeof a);
            if (ra && rb && a.wHeapPresent && !k)
                dlocal(h);
        }
    }
    lg("d GlobalEntryHandle and the local heaps: done");
    {
        MODULEENTRY a, b;
        dprep(&a, &b, sizeof a);
        USEA; ra = pModuleFirst(&a);
        USEB; rb = pModuleFirst(&b);
        USEA;
        while (ra || rb) {
            wsprintf(dname, "Module %u", nm);
            dcmp(ra, rb, &a, &b, sizeof a);
            if (!ra || !rb || nm >= 100)
                break;
            hm[nm++] = a.hModule;
            USEA; ra = pModuleNext(&a);
            USEB; rb = pModuleNext(&b);
            USEA;
        }
        lg("d modules: %u", nm);
    }
    for (i = 0; i < nm; i++) {
        GLOBALENTRY a, b;
        MODULEENTRY ma, mb;
        WORD r1, r2;
        dprep(&ma, &mb, sizeof ma);
        USEA; r1 = pModuleFindHandle(&ma, hm[i]);
        USEB; r2 = pModuleFindHandle(&mb, hm[i]);
        USEA;
        wsprintf(dname, "ModuleFindHandle(%04X) %04X/%04X", hm[i], r1, r2);
        dcmp(r1, r2, &ma, &mb, sizeof ma);
        for (k = 0; k < 70; k++) {
            dprep(&a, &b, sizeof a);
            USEA; ra = pGlobalEntryModule(&a, hm[i], k);
            USEB; rb = pGlobalEntryModule(&b, hm[i], k);
            USEA;
            wsprintf(dname, "GlobalEntryModule(%04X,%u)", hm[i], k);
            dcmp(ra, rb, &a, &b, sizeof a);
            if (!ra && !rb && k > 1)
                break;
        }
    }
    {
        TASKENTRY a, b;
        WORD nt = 0;
        dprep(&a, &b, sizeof a);
        USEA; ra = pTaskFirst(&a);
        USEB; rb = pTaskFirst(&b);
        USEA;
        while (ra || rb) {
            wsprintf(dname, "Task %u", nt);
            dcmp(ra, rb, &a, &b, sizeof a);
            if (!ra || !rb || ++nt > 50)
                break;
            USEA; ra = pTaskNext(&a);
            USEB; rb = pTaskNext(&b);
            USEA;
        }
        lg("d tasks: %u", nt);
    }
    {
        CLASSENTRY a, b;
        WORD nc = 0;
        dprep(&a, &b, sizeof a);
        USEA; ra = pClassFirst(&a);
        USEB; rb = pClassFirst(&b);
        USEA;
        while (ra || rb) {
            wsprintf(dname, "Class %u", nc);
            dcmp(ra, rb, &a, &b, sizeof a);
            if (!ra || !rb || ++nc > 300)
                break;
            USEA; ra = pClassNext(&a);
            USEB; rb = pClassNext(&b);
            USEA;
        }
        lg("d classes: %u", nc);
    }
    {
        SYSHEAPINFO a, b;
        MEMMANINFO ma, mb;
        dprep(&a, &b, sizeof a);
        USEA; ra = pSystemHeapInfo(&a);
        USEB; rb = pSystemHeapInfo(&b);
        USEA;
        lstrcpy(dname, "SystemHeapInfo");
        dcmp(ra, rb, &a, &b, sizeof a);
        dprep(&ma, &mb, sizeof ma);
        USEA; ra = pMemManInfo(&ma);
        USEB; rb = pMemManInfo(&mb);
        USEA;
        lstrcpy(dname, "MemManInfo");
        dcmp(ra, rb, &ma, &mb, sizeof ma);
    }
    {
        static const WORD v[] = { 0, 1, 2, 7, 8, 0xFFFF, 0x1234 };
        for (i = 0; i < 7; i++) {
            WORD a2, b2;
            USEA; a2 = pGlobalHandleToSel(v[i]);
            USEB; b2 = pGlobalHandleToSel(v[i]);
            USEA;
            if (a2 != b2)
                lg("d GlobalHandleToSel(%04X): %04X/%04X", v[i], a2, b2);
        }
    }
    dual_st = 1;
    st_run();
    dual_st = 0;
    bulk = 0;
    lg("d: %u difference(s)", dmis);
}

/* ==== w: Dr. Watson (DRWATSON.EXE, which registers notification and
   interrupt callbacks with TOOLHELP and asks it for tasks, modules and
   the system heaps) watching a program start and end, then closed; its
   log, C:\WINDOWS\DRWATSON.LOG, is left for tests/toolhelp/thwatson.sh.
   (A fault under Dr. Watson was tried: under DOSBox 3.1 itself showed
   nothing and went no further, so it is not part of the test.) ==== */
BOOL CALLBACK __export dw_find(HWND w, LPARAM l)
{
    *(HWND FAR *)l = w;
    return FALSE;
}

static void t_watson(void)
{
    HINSTANCE h;
    HWND w = 0;
    WORD t, i;
    lg("== w Dr. Watson");
    lflush();
    h = (HINSTANCE)WinExec("DRWATSON.EXE", SW_SHOWMINIMIZED);
    lg("WinExec(DRWATSON): %d", (WORD)h >= 32);
    spin(50);
    if (start_child("x")) {
        lg("child gone: %d", wait_gone(child));
        unname(child);
        child = 0;
    }
    t = task_of_inst(h);
    lg("Dr. Watson's task: %d", t != 0);
    if (t) {
        EnumTaskWindows((HTASK)t, (WNDENUMPROC)MakeProcInstance((FARPROC)dw_find, (HINSTANCE)dsel),
                        (LPARAM)(HWND FAR *)&w);
        lg("Dr. Watson's window: %d", w != 0);
        PostAppMessage((HTASK)t, WM_QUIT, 0, 0);
        lg("Dr. Watson ended: %d", wait_gone(t));
    }
    for (i = 0; i < 50; i++)
        Yield();
}

static void t_load(void)
{
    WORD i, n = 0;
    th = LoadLibrary("TOOLHELP.DLL");
    lg("TOOLHELP loaded: %d", (WORD)th >= 32);
    for (i = 1; i < 84; i++) {
        thfA[i] = th >= (HINSTANCE)32 ? GetProcAddress(th, MAKEINTRESOURCE(i)) : NULL;
        if (thname[i] && !thf[i])
            lg("missing %u %s", i, (LPSTR)thname[i]);
        if (thf[i])
            n++;
        if (!thname[i] && thf[i])
            lg("extra ordinal %u", i);
    }
    lg("exports: %u", n);
}

int __cdecl __far tmain(void)
{
    HFILE f;
    char *a;
    SetHandleCount(30);
    f = _lopen("C:\\THP.ARG", OF_READ);
    if (f != HFILE_ERROR) {
        int n = _lread(f, arg, sizeof arg - 1);
        arg[n < 0 ? 0 : n] = 0;
        _lclose(f);
    }
    f = _lcreat(logname, 0);
    _lclose(f);
    sbuf = (char FAR *)GlobalLock(GlobalAlloc(GMEM_FIXED, 32768L));
    GetWindowsDirectory(windir, sizeof windir);
    hk = GetModuleHandle("KERNEL");
    hu = GetModuleHandle("USER");
    hg = GetModuleHandle("GDI");
    hme = GetModuleHandle("THP");
    me = GetCurrentTask();
    _asm { mov dsel, ds }
    {
        static const char *const mods[] = { "KERNEL", "USER", "GDI", "SYSTEM", "KEYBOARD",
            "MOUSE", "DISPLAY", "SOUND", "COMM", "MMSYSTEM", "TIMER", "WIN87EM", "TOOLHELP",
            "THP", "VER", "SHELL", "LZEXPAND", 0 };
        WORD i;
        for (i = 0; mods[i]; i++)
            namemod(mods[i]);
    }
    nameit((WORD)me, "T:self");
    nameit(kGetCurrentPDB(), "pdb");
    nameblk(dsel, "dg");
    nameit(LOWORD(kGlobalMasterHandle()), "bm");
    lg("THP start; Windows %s, %s", (LPSTR)((GetWinFlags() & WF_ENHANCED) ? "386 Enhanced" : "Standard"),
       (LPSTR)(GetModuleHandle("KERNEL") && GetModuleUsage(hk) ? "" : ""));
    t_load();
    if ((WORD)th < 32)
        return 1;
    namemod("TOOLHELP");
    for (a = arg; *a; a++) {
        switch (*a) {
        case 'e': t_entry(); break;
        case 'g': t_global(); break;
        case 'h': t_grow(); break;
        case 'l': t_local(); break;
        case 'm': t_modules(); break;
        case 't': t_tasks(); break;
        case 's': t_stack(); break;
        case 'c': t_classes(); break;
        case 'i': t_info(); break;
        case 'r': t_memory(); break;
        case 'n': t_notify(); break;
        case 'x': t_interrupt(); break;
        case 'k': t_taskctl(); break;
        case 'd': t_dual(); break;
        case 'w': t_watson(); break;
        }
    }
    lg("THP end");
    lflush();
    return 0;
}
