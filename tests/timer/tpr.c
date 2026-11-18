/*
 * tpr.c - TPR.EXE, the timer probe (ARCHITECTURE.md 21; docs/notes/
 * timer-handover.md): MMSYSTEM's time* functions in every mode, and what
 * they ask of the timer driver. Run as the shell (tests/timer/trun.sh) on
 * the reference 3.1 with Microsoft's MMSYSTEM over Microsoft's TIMER.DRV
 * and over w16's, with or without the logging driver TMS.DRV
 * (tests/timer/tms.c) between them; its records (lines "  TMS ...")
 * follow the calls that caused them. The timer callback is TPC.DLL's
 * (tests/timer/tpc.c), which records each call.
 *
 * Lines starting with "~" are measurements ("~ NAME: n n ..."); the
 * comparison (tests/timer/tcmp.py) checks them within limits measured
 * over repeated runs of Microsoft's; every other line must be identical.
 *
 * Sections ("== NAME", the letter that selects it in brackets): boot
 * (the driver's messages from Windows' start), caps [c]
 * (timeGetDevCaps), stime [s] (timeGetSystemTime, timeGetTime), rate [r]
 * (the millisecond count against the BIOS ticks, its steps), period [p]
 * (timeBeginPeriod, timeEndPeriod, and the 8254's period they give),
 * event [e] (timeSetEvent's and timeKillEvent's arguments), ids [i]
 * (event IDs, how many at once, their count's wrap), oneshot [o] and
 * periodic [P] (when the callbacks come), context [x] (the callback's
 * registers, stack and task), inside [I] (StackEnter and StackLeave;
 * time* calls from a callback), nest [N] (a callback that takes long:
 * reentrancy), tod [t] (the BIOS time of day, DOS's clock, GetTickCount
 * and WM_TIMER while the timer runs at 1 ms), child [h] (a task that ends
 * with events set and a period begun), flags [f] (the flags' bits), msgs
 * [m] (the other driver messages, through USER), end (an event and a
 * period left when Windows ends: TMS's record of DRV_FREE). Not in the
 * default run: trace [T] and kin [K] (when the callbacks come, counted in
 * timer interrupts, and channel 0's count at each: what the handover's
 * timing notes come from), W (wait 7 s first), k (kin again after 7 s),
 * null [n] (NULL pointers, which may fault).
 * C:\TPR.ARG (the shell has no arguments) holds the letters to run.
 *
 * Sources: Windows 3.1 SDK, Multimedia Programmer's Guide ch. 6 (timer
 * services), Programmer's Reference Vol. 2 (time* functions), Vol. 3
 * (TIMECAPS, MMTIME); the 8254 and CMOS ports as on any PC/AT.
 */
#include <windows.h>
#include <mmsystem.h>
#include "tpc.h"

extern WORD __cdecl entry_regs[11];  /* start.asm: ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl __far get_0040h(void);
extern WORD __cdecl __far pitcount(void);
extern WORD __cdecl __far cmos(WORD reg);
extern WORD __cdecl __far pitstat(void);
extern DWORD __cdecl __far dostime(void);
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */

typedef void (FAR PASCAL *DUMPPROC)(LPCSTR);
typedef void (FAR PASCAL *SETPROC)(WORD, HTASK);

static const char logname[] = "C:\\TPR.LOG";
static char line[600];
static char args[40];
static volatile DWORD FAR *ticks;
static struct tpcstate FAR *st;
static DUMPPROC tmsdump;
static HTASK me;
static WORD myss;

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

/* the logging driver's records since the last dump (none without it) */
static void shim(void)
{
    if (tmsdump)
        tmsdump(logname);
}

static SETPROC tmsset;

/* the message the logging driver counts instead of recording */
static void quiet(WORD msg)
{
    if (tmsset)
        tmsset(msg, me);
}

static int want(char c)
{
    int i;
    if (!args[0])
        return c != 'n' && c != 'W' && c != 'k' && c != 'T' && c != 'K';
    for (i = 0; args[i]; i++)
        if (args[i] == c)
            return 1;
    return 0;
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

/* wait n ms by the BIOS ticks (54.9 ms each; at least one), yielding */
static void waitticks(WORD n)
{
    DWORD t0 = *ticks;
    while (*ticks - t0 < n) {
        pump();
        Yield();
    }
}

/* the start of a BIOS tick */
static void tickedge(void)
{
    DWORD t0 = *ticks;
    while (*ticks == t0)
        ;
}

/* wait until timeGetTime has advanced n ms, or 2 s more of BIOS ticks */
static void waitms(DWORD n)
{
    DWORD t0 = timeGetTime(), b0 = *ticks;
    while (timeGetTime() - t0 < n && *ticks - b0 < (WORD)n / 54 + 38) {
        pump();
        Yield();
    }
}

/* wait until slot s has been called n times, at most about 3 s: 3.1's
   driver sometimes leaves a new event uncalled for a second or so
   (docs/notes/timer-handover.md), and these sections are about what the
   callback sees, not when it comes */
static void waitfor(int s, WORD n)
{
    DWORD b0 = *ticks;
    while (st->slots[s].count < n && *ticks - b0 < 55) {
        pump();
        Yield();
    }
}

static void fill(void FAR *d, int c, unsigned n)
{
    BYTE FAR *p = d;
    while (n--)
        *p++ = (BYTE)c;
}

static char *hexb(const BYTE FAR *b, int n)
{
    static char s[80];
    static const char x[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < n; i++) {
        s[2 * i] = x[b[i] >> 4];
        s[2 * i + 1] = x[b[i] & 15];
    }
    s[2 * n] = 0;
    return s;
}

/* which module a selector belongs to */
static LPSTR owner(WORD sel)
{
    WORD m;
    if (!sel)
        return "0";
    m = K_GETEXEPTR(sel);
    if (!m)
        return "none";
    if (m == (WORD)GetModuleHandle("MMSYSTEM"))
        return "MMSYSTEM";
    if (m == (WORD)GetModuleHandle("TIMER"))
        return "TIMER";
    if (m == (WORD)GetModuleHandle("TPC"))
        return "TPC";
    if (m == (WORD)GetModuleHandle("TPR"))
        return "TPR";
    if (m == (WORD)GetModuleHandle("TMS"))
        return "TMS";
    if (m == (WORD)GetModuleHandle("KERNEL"))
        return "KERNEL";
    if (m == (WORD)GetModuleHandle("USER"))
        return "USER";
    if (m == (WORD)GetModuleHandle("SYSTEM"))
        return "SYSTEM";
    return "other";
}

static void slot(int s, WORD mode, WORD param)
{
    struct slot FAR *sl = &st->slots[s];
    fill(sl, 0, sizeof *sl);
    sl->mode = mode;
    sl->param = param;
}

static UINT setev(UINT delay, UINT res, int s, UINT flags)
{
    return timeSetEvent(delay, res, (LPTIMECALLBACK)TpcCallback,
                        0x5A5A0000L | s, flags);
}

/* the largest count channel 0 shows over about 3 BIOS ticks: its period */
static int enh;          /* 386 Enhanced mode: the 8254 is virtual, not read */

static WORD pitmax(void)
{
    WORD m = 0, c;
    DWORD t0 = *ticks;
    if (enh)
        return 0;
    while (*ticks - t0 < 3) {
        c = pitcount();
        if (c > m)
            m = c;
    }
    return m;
}

/* the smallest and largest step timeGetTime takes over about 220 ms */
static void steps(const char *name)
{
    DWORD t0 = timeGetTime(), p = t0, t;
    DWORD b0 = *ticks;
    DWORD mn = 0xFFFFFFFFL, mx = 0;
    WORD n = 0;
    while (*ticks - b0 < 4) {
        t = timeGetTime();
        if (t != p) {
            if (t - p < mn)
                mn = t - p;
            if (t - p > mx)
                mx = t - p;
            n++;
            p = t;
        }
    }
    lg("~ %s steps: min %lu max %lu changes %u in %lu", (LPSTR)name, mn, mx, n, p - t0);
}

static void sec_caps(void)
{
    static const WORD sizes[] = { 0, 1, 2, 3, 4, 6, 8, 100 };
    BYTE b[8];
    int i;
    UINT r;
    lg("== caps");
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        fill(b, 0xEE, 8);
        r = timeGetDevCaps((TIMECAPS FAR *)b, sizes[i]);
        lg("timeGetDevCaps(size %u) = %u: %s", sizes[i], r, (LPSTR)hexb(b, 8));
        shim();
    }
}

static void sec_stime(void)
{
    static const WORD sizes[] = { 0, 2, 4, 5, 6, 8, 100 };
    static const WORD types[] = { 0, TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, 3, 0x10, 0xFFFF };
    BYTE b[8];
    int i;
    UINT r;
    DWORD t0, t1, v;
    lg("== stime");
    quiet(0);
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        fill(b, 0xEE, 8);
        ((MMTIME FAR *)b)->wType = TIME_MS;
        t0 = timeGetTime();
        r = timeGetSystemTime((MMTIME FAR *)b, sizes[i]);
        t1 = timeGetTime();
        v = *(DWORD *)(b + 2);
        lg("timeGetSystemTime(TIME_MS, size %u) = %u: type %04X ms %s%s", sizes[i], r,
           *(WORD *)b, v >= t0 && v <= t1 ? (LPSTR)"now" : (LPSTR)hexb(b + 2, 4),
           (LPSTR)(b[6] == 0xEE && b[7] == 0xEE ? "" : " (bytes 6-7 written)"));
        shim();
    }
    for (i = 0; i < sizeof types / sizeof types[0]; i++) {
        fill(b, 0xEE, 8);
        ((MMTIME FAR *)b)->wType = types[i];
        t0 = timeGetTime();
        r = timeGetSystemTime((MMTIME FAR *)b, sizeof(MMTIME));
        t1 = timeGetTime();
        v = *(DWORD *)(b + 2);
        lg("timeGetSystemTime(type %04X) = %u: type %04X ms %s", types[i], r,
           *(WORD *)b, v >= t0 && v <= t1 ? (LPSTR)"now" : (LPSTR)hexb(b + 2, 6));
        shim();
    }
    t0 = timeGetTime();
    lg("timeGetTime: %s", (LPSTR)(t0 ? "nonzero" : "0"));
    shim();
    quiet(0x0808);
    lg("~ stime timeGetTime - GetTickCount: %ld", (long)(timeGetTime() - GetTickCount()));
}

static void sec_rate(void)
{
    DWORD t0, t1, g0, g1, b0;
    lg("== rate");
    tickedge();
    b0 = *ticks;
    t0 = timeGetTime();
    g0 = GetTickCount();
    waitticks(36);
    tickedge();
    t1 = timeGetTime();
    g1 = GetTickCount();
    lg("~ rate: ticks %lu timeGetTime %lu GetTickCount %lu", *ticks - b0, t1 - t0, g1 - g0);
    steps("rate");
    shim();
}

static void sec_period(void)
{
    static const WORD v[] = { 0, 1, 2, 5, 10, 54, 55, 56, 60, 64, 65, 99, 100, 1000, 65535 };
    int i;
    UINT r;
    lg("== period");
    lg("~ period none pit: %u", pitmax());
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        r = timeBeginPeriod(v[i]);
        lg("timeBeginPeriod(%u) = %u", v[i], r);
        shim();
        if (!r) {
            lg("~ period %u pit: %u", v[i], pitmax());
            steps("period");
        }
        r = timeEndPeriod(v[i]);
        lg("timeEndPeriod(%u) = %u", v[i], r);
        shim();
    }
    lg("timeEndPeriod(7) unmatched = %u", timeEndPeriod(7));
    shim();
    lg("timeBeginPeriod(10) = %u", timeBeginPeriod(10));
    lg("timeBeginPeriod(1) = %u", timeBeginPeriod(1));
    lg("timeBeginPeriod(1) = %u", timeBeginPeriod(1));
    shim();
    lg("~ period 10+1+1 pit: %u", pitmax());
    lg("timeEndPeriod(1) = %u", timeEndPeriod(1));
    shim();
    lg("~ period 10+1 pit: %u", pitmax());
    lg("timeEndPeriod(1) = %u", timeEndPeriod(1));
    shim();
    lg("~ period 10 pit: %u", pitmax());
    lg("timeEndPeriod(10) = %u", timeEndPeriod(10));
    shim();
    lg("~ period after pit: %u", pitmax());
    lg("timeEndPeriod(10) again = %u", timeEndPeriod(10));
    shim();
}

static void sec_event(void)
{
    static const WORD d[] = { 0, 1, 2, 10, 1000, 32767, 65535 };
    static const WORD res[] = { 0, 1, 10, 1000, 65535 };
    static const WORD fl[] = { TIME_ONESHOT, TIME_PERIODIC, 2, 3, 0x100, 0xFFFF };
    int i;
    UINT id, r;
    lg("== event");
    slot(1, 0, 0);
    for (i = 0; i < sizeof d / sizeof d[0]; i++) {
        id = setev(d[i], 0, 1, TIME_ONESHOT);
        lg("timeSetEvent(%u, 0, oneshot) = %s", d[i], (LPSTR)(id ? "id" : "0"));
        shim();
        if (id) {
            r = timeKillEvent(id);
            lg("timeKillEvent = %u", r);
            shim();
        }
    }
    for (i = 0; i < sizeof res / sizeof res[0]; i++) {
        id = setev(5000, res[i], 1, TIME_PERIODIC);
        lg("timeSetEvent(5000, %u, periodic) = %s", res[i], (LPSTR)(id ? "id" : "0"));
        shim();
        if (id) {
            lg("timeKillEvent = %u", timeKillEvent(id));
            shim();
        }
    }
    for (i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        id = setev(5000, 10, 1, fl[i]);
        lg("timeSetEvent(5000, 10, flags %04X) = %s", fl[i], (LPSTR)(id ? "id" : "0"));
        shim();
        if (id) {
            lg("timeKillEvent = %u", timeKillEvent(id));
            shim();
        }
    }
    id = timeSetEvent(5000, 0, NULL, 0, TIME_ONESHOT);
    lg("timeSetEvent(callback NULL) = %s", (LPSTR)(id ? "id" : "0"));
    shim();
    if (id) {
        lg("timeKillEvent = %u", timeKillEvent(id));
        shim();
    }
    id = setev(5000, 0, 1, TIME_ONESHOT);
    lg("timeKillEvent(id) = %u", timeKillEvent(id));
    lg("timeKillEvent(id) again = %u", timeKillEvent(id));
    lg("timeKillEvent(0) = %u", timeKillEvent(0));
    lg("timeKillEvent(id + 1) = %u", timeKillEvent(id + 1));
    lg("timeKillEvent(FFFF) = %u", timeKillEvent(0xFFFF));
    shim();
    lg("slot 1 calls: %u", st->slots[1].count);
}

static void sec_ids(void)
{
    static UINT ids[80];
    static char b[400];
    int i, n = 0;
    char *p;
    UINT first;
    lg("== ids");
    slot(2, 0, 0);
    for (i = 0; i < 80; i++) {
        ids[i] = setev(30000, 0, 2, TIME_PERIODIC);
        if (!ids[i])
            break;
        n++;
    }
    lg("events at once: %d", n);
    first = ids[0];
    p = b;
    *p = 0;
    for (i = 0; i < n && p < b + sizeof b - 8; i++)
        p += wsprintf(p, " %d", ids[i] - first);
    lg("ids - first:%s", (LPSTR)b);
    lg("~ ids first: %u", first);
    shim();
    for (i = n - 1; i >= 0; i--)
        if (timeKillEvent(ids[i]))
            lg("timeKillEvent(%d) failed", i);
    shim();
    ids[0] = setev(30000, 0, 2, TIME_ONESHOT);
    ids[1] = setev(30000, 0, 2, TIME_ONESHOT);
    lg("after killing all: %d %d (from the first before)", ids[0] - first, ids[1] - first);
    timeKillEvent(ids[0]);
    ids[0] = setev(30000, 0, 2, TIME_ONESHOT);
    lg("one killed and set again: %d", ids[0] - first);
    {
        /* the IDs over 4100 events set and killed one after another */
        UINT a, prev = ids[0];
        WORD i, jumps = 0;
        for (i = 0; i < 4100; i++) {
            if (i == 4090 - 2048)
                shim();                 /* (TMS records the IDs from 8000h on) */
            a = setev(30000, 0, 2, TIME_ONESHOT);
            if (a != prev + 16 && a != prev + 8 && jumps < 6) {
                lg("ids: %04X after %04X (event %u)", a, prev, i);
                jumps++;
            }
            if (!a)
                break;
            timeKillEvent(a);
            prev = a;
        }
        lg("ids: 4100 set and killed, the last %04X", prev);
    }
    timeKillEvent(ids[0]);
    timeKillEvent(ids[1]);
    shim();
}

static void sec_oneshot(void)
{
    static const WORD d[] = { 1, 2, 3, 5, 10, 20, 50, 100, 200, 500 };
    int i;
    UINT id;
    DWORD t0;
    lg("== oneshot");
    for (i = 0; i < sizeof d / sizeof d[0]; i++) {
        slot(3, CB_REC, 0);
        st->n = 0;
        t0 = timeGetTime();
        id = setev(d[i], 0, 3, TIME_ONESHOT);
        waitms(d[i] + 150);
        lg("~ oneshot %u: calls %u id %s after %ld kill %u", d[i], st->slots[3].count,
           (LPSTR)(st->n && st->rec[0].id == id ? "same" : "other"),
           st->n ? (long)(st->rec[0].ms - t0) : -1L, timeKillEvent(id));
        shim();
    }
    /* coarse resolution */
    slot(3, CB_REC, 0);
    st->n = 0;
    t0 = timeGetTime();
    id = setev(100, 100, 3, TIME_ONESHOT);
    waitms(400);
    lg("~ oneshot 100 res 100: calls %u after %ld", st->slots[3].count,
       st->n ? (long)(st->rec[0].ms - t0) : -1L);
    shim();
}

static void sec_periodic(void)
{
    static const WORD d[] = { 1, 2, 5, 10, 25, 55, 100 };
    int i;
    UINT id;
    WORD c;
    DWORD t0;
    lg("== periodic");
    for (i = 0; i < sizeof d / sizeof d[0]; i++) {
        slot(4, 0, 0);
        t0 = timeGetTime();
        id = setev(d[i], 0, 4, TIME_PERIODIC);
        waitms(1000);
        timeKillEvent(id);
        c = st->slots[4].count;
        lg("~ periodic %u: calls %u first %ld interval min %lu max %lu", d[i], c,
           (long)(st->slots[4].first - t0), st->slots[4].minitv, st->slots[4].maxitv);
        waitms(150);
        lg("periodic %u: %s after the kill", d[i],
           (LPSTR)(st->slots[4].count == c ? "no calls" : "more calls"));
        shim();
    }
    /* two at once */
    slot(4, 0, 0);
    slot(5, 0, 0);
    id = setev(10, 0, 4, TIME_PERIODIC);
    c = setev(15, 0, 5, TIME_PERIODIC);
    waitms(900);
    timeKillEvent(id);
    timeKillEvent(c);
    lg("~ periodic 10 and 15 at once: calls %u %u", st->slots[4].count, st->slots[5].count);
    shim();
}

static void sec_context(void)
{
    struct cbrec FAR *r;
    UINT id;
    int i;
    lg("== context");
    for (i = 0; i < 2; i++) {
        slot(6, CB_REC, 0);
        st->n = 0;
        id = setev(10, 0, 6, i ? TIME_PERIODIC : TIME_ONESHOT);
        waitfor(6, i ? 2 : 1);
        if (i)
            timeKillEvent(id);
        else
            timeKillEvent(id);
        if (!st->n) {
            lg("no call");
            continue;
        }
        r = &st->rec[0];
        lg("%s: id %s msg %04X user %08lX dw1 %08lX dw2 %08lX", (LPSTR)(i ? "periodic" : "oneshot"),
           (LPSTR)(r->id == id ? "same" : "other"), r->msg, r->user, r->dw1, r->dw2);
        lg("  flags %04X (IF %d DF %d) ss %s, es %s, ds %s caller %s task %s depth %u",
           r->flags, (r->flags & 0x200) != 0, (r->flags & 0x400) != 0,
           (LPSTR)(r->ss == myss ? "the probe's" : "another"),
           (LPSTR)(r->es == r->ss ? "= ss" : "other"),
           owner(r->ds), owner(r->retcs),
           (LPSTR)(r->task == (WORD)me ? "probe" : r->task ? "other" : "0"), r->depth);
        lg("~ context sp: %04X ax %04X bx %04X cx %04X dx %04X si %04X di %04X", r->sp,
           r->ax, r->bx, r->cx, r->dx, r->si, r->di);
        lg("~ context selectors: ss %04X ds %04X es %04X", r->ss, r->ds, r->es);
        lg("  PIC in service %02X mask %02X", r->isr & 0xFF, r->isr >> 8);
        {
            char fn[80];
            WORD m = K_GETEXEPTR(r->ss);
            fn[0] = 0;
            if (m)
                GetModuleFileName((HINSTANCE)m, fn, sizeof fn);
            lg("~ context the stack's owner: %s", (LPSTR)(m ? fn : "none"));
        }
        if (i && st->n > 1)
            lg("  second call: ss %s sp %s", (LPSTR)(st->rec[1].ss == r->ss ? "same" : "other"),
               (LPSTR)(st->rec[1].sp == r->sp ? "same" : "other"));
        shim();
    }
}

/* MMSYSTEM's StackEnter and StackLeave, called by TPC (tpcs.asm) with
   marker values in the registers */
static void stackprobe(void)
{
    WORD FAR *s = TpcStack();
    static const char *nm[] = { "AX", "BX", "CX", "DX", "SI", "DI", "BP", "SP", "DS", "ES", "SS", "FL" };
    static const WORD mark[] = { 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777 };
    static char b[300];
    char *p;
    int i, j;
    for (j = 0; j < 2; j++) {
        p = b;
        for (i = 0; i < 7; i++)
            if (s[12 * j + i] != mark[i])
                p += wsprintf(p, " %s %04X", (LPSTR)nm[i], s[12 * j + i]);
        *p = 0;
        lg("%s: registers changed:%s; SP %04X, SS %s, flags %04X", (LPSTR)(j ? "after StackLeave" : "after StackEnter"),
           (LPSTR)(b[0] ? b : " none"), s[12 * j + 7], (LPSTR)(s[12 * j + 10] == s[24] ? "the probe's" : "another"),
           s[12 * j + 11]);
    }
    lg("~ stack before: SS %04X SP %04X; after StackEnter SS %04X %s", s[24], s[25], s[10], owner(s[10]));
}

static void sec_inside(void)
{
    UINT id;
    lg("== inside");
    stackprobe();
    slot(7, CB_KILL | CB_REC, 3);
    st->n = 0;
    id = setev(10, 0, 7, TIME_PERIODIC);
    waitfor(7, 3);
    waitms(100);
    lg("kill itself at call 3: calls %u, timeKillEvent = %u", st->slots[7].count, st->slots[7].res);
    lg("timeKillEvent afterwards = %u", timeKillEvent(id));
    shim();
    slot(8, CB_SET, 20);
    slot(9, CB_REC, 0);
    st->n = 0;
    id = setev(10, 0, 8, TIME_ONESHOT);
    waitfor(8, 1);
    waitfor(9, 1);
    lg("set from a callback: %s, its calls %u", (LPSTR)(st->slots[8].res ? "id" : "0"),
       st->slots[9].count);
    shim();
    slot(10, CB_STIME, 0);
    id = setev(10, 0, 10, TIME_ONESHOT);
    waitfor(10, 1);
    lg("timeGetSystemTime from a callback = %u, type %04X", st->slots[10].res,
       *(WORD FAR *)st->slots[10].mmt);
    shim();
    slot(10, CB_BEGIN, 1);
    id = setev(10, 0, 10, TIME_ONESHOT);
    waitfor(10, 1);
    lg("timeBeginPeriod(1) from a callback = %u", st->slots[10].res);
    if (!st->slots[10].res)
        lg("timeEndPeriod(1) = %u", timeEndPeriod(1));
    shim();
}

static void sec_nest(void)
{
    UINT a, b;
    lg("== nest");
    slot(11, CB_SPIN, 40);
    slot(12, 0, 0);
    st->maxdepth = 0;
    a = setev(1, 0, 11, TIME_PERIODIC);
    waitms(100);
    timeKillEvent(a);
    lg("periodic 1 ms, first call waits 40 ms: depth %u", st->slots[11].maxdepth);
    lg("~ nest calls during the wait: %u", st->slots[11].count);
    shim();
    slot(11, CB_SPIN, 40);
    slot(12, 0, 0);
    a = setev(5, 0, 11, TIME_ONESHOT);
    b = setev(10, 0, 12, TIME_PERIODIC);
    waitms(150);
    timeKillEvent(b);
    lg("~ nest another event while a callback waits: depth %u", st->slots[12].maxdepth);
    lg("~ nest other calls: %u", st->slots[12].count);
    /* (3.1's has not called the one-shot event either by now: its kill
       frees it, so that later events get the same slots on both) */
    quiet(0x0800);
    lg("~ nest the waiting one-shot event: calls %u, killed %u", st->slots[11].count, timeKillEvent(a));
    quiet(0x0808);
    shim();
}

/* hundredths of a second from DOS clock a to b (less than an hour) */
static int csdiff(DWORD a, DWORD b)
{
    int m = (int)(b >> 16 & 0xFF) - (int)(a >> 16 & 0xFF);
    int sec = (int)(b >> 8 & 0xFF) - (int)(a >> 8 & 0xFF);
    if (m < 0)
        m += 60;
    return (m * 60 + sec) * 100 + (int)(b & 0xFF) - (int)(a & 0xFF);
}

static void sec_tod(void)
{
    DWORD b0, t0, g0, d0, d1, b1, t1, g1;
    WORD c0, s0, s1, n = 0, tid;
    UINT id;
    MSG m;
    lg("== tod");
    lg("timeBeginPeriod(1) = %u", timeBeginPeriod(1));
    slot(13, 0, 0);
    id = setev(1, 0, 13, TIME_PERIODIC);
    tid = SetTimer(0, 0, 100, NULL);
    /* from a CMOS second's edge to another, 6 s later */
    s0 = cmos(0);
    while ((c0 = cmos(0)) == s0)
        ;
    b0 = *ticks;
    t0 = timeGetTime();
    g0 = GetTickCount();
    d0 = dostime();
    s1 = c0;
    while (1) {
        WORD s = cmos(0);
        if (s != s1) {
            s1 = s;
            if (++n == 6)
                break;
        }
        while (PeekMessage(&m, 0, WM_TIMER, WM_TIMER, PM_REMOVE))
            st->slots[14].count++;
    }
    b1 = *ticks;
    t1 = timeGetTime();
    g1 = GetTickCount();
    d1 = dostime();
    KillTimer(0, tid);
    timeKillEvent(id);
    lg("timeEndPeriod(1) = %u", timeEndPeriod(1));
    lg("~ tod 6 CMOS seconds: BIOS ticks %lu timeGetTime %lu GetTickCount %lu callbacks %u WM_TIMER %u",
       b1 - b0, t1 - t0, g1 - g0, st->slots[13].count, st->slots[14].count);
    lg("~ tod DOS clock: %d", csdiff(d0, d1));
    shim();
}

static void child(void)
{
    UINT a, b;
    slot(15, 0, 0);
    lg("child: timeBeginPeriod(1) = %u", timeBeginPeriod(1));
    a = setev(20, 0, 15, TIME_PERIODIC);
    b = setev(2000, 0, 15, TIME_ONESHOT);
    lg("child: events %s %s", (LPSTR)(a ? "set" : "0"), (LPSTR)(b ? "set" : "0"));
    WritePrivateProfileString("tpr", "child", "done", "SYSTEM.INI");
}

static void sec_child(void)
{
    WORD i, c;
    lg("== child");
    GlobalAddAtom("TPRCHILD");
    lg("WinExec >= 32: %d", WinExec("C:\\TPR.EXE", SW_SHOWNORMAL) >= 32);
    for (i = 0; i < 2000 && GetModuleUsage(GetModuleHandle("TPR")) > 1; i++) {
        pump();
        Yield();
    }
    lg("child gone: %d", GetModuleUsage(GetModuleHandle("TPR")) <= 1);
    shim();
    c = st->slots[15].count;
    waitms(300);
    lg("child's periodic event after its end: %s", (LPSTR)(st->slots[15].count > c + 5 ? "still called" :
       st->slots[15].count == c ? "not called" : "a few calls"));
    lg("~ child pit: %u", pitmax());
    waitms(2200);
    lg("~ child calls: %u", st->slots[15].count);
    shim();
}

/* channel 0's status (read-back command): its mode in bits 1-3 */
static WORD pitstatus(void)
{
    if (enh)
        return 0;
    return pitstat();
}

/* one timing scenario: timeBeginPeriod(per) (0: none), an event, the
   channel 0 period while it is pending, and each call: timeGetTime, the
   BIOS ticks and channel 0's count against the moment it was set */
static void trace(const char *name, WORD per, WORD delay, WORD res, WORD flags, WORD ms)
{
    struct cbrec FAR *r;
    DWORD t0, b0;
    WORD p0, i, n, pm;
    UINT id;
    if (per)
        timeBeginPeriod(per);
    slot(3, CB_REC, 0);
    st->n = 0;
    tickedge();
    b0 = *ticks;
    p0 = pitcount();
    t0 = timeGetTime();
    id = setev(delay, res, 3, flags);
    pm = pitmax();
    waitms(ms);
    if (id)
        timeKillEvent(id);
    n = st->n;
    lg("trace %s: begun %u, event %u res %u flags %u: %u calls, pit %u status %02X",
       (LPSTR)name, per, delay, res, flags, n, pm, pitstatus());
    for (i = 0; i < n && i < 40; i++) {
        r = &st->rec[i];
        lg("~ trace %s %u: ms %ld ticks %ld pit %u", (LPSTR)name, i,
           (long)(r->ms - t0), (long)(r->tick - (WORD)b0), r->pit);
    }
    if (per)
        timeEndPeriod(per);
    (void)p0;
}

static void sec_trace(void)
{
    lg("== trace");
    lg("~ trace idle pit: %u status %02X", pitmax(), pitstatus());
    trace("pend1000", 0, 1000, 0, TIME_ONESHOT, 10);
    trace("pend1000r1000", 0, 1000, 1000, TIME_PERIODIC, 10);
    trace("pend1000r5", 0, 1000, 5, TIME_ONESHOT, 10);
    trace("pend1000p10", 10, 1000, 0, TIME_ONESHOT, 10);
    trace("per3", 0, 3, 0, TIME_PERIODIC, 100);
    trace("per3r3", 0, 3, 3, TIME_PERIODIC, 100);
    trace("per7p7", 7, 7, 0, TIME_PERIODIC, 150);
    trace("per100", 0, 100, 0, TIME_PERIODIC, 700);
    trace("per100r100", 0, 100, 100, TIME_PERIODIC, 700);
    trace("one30a", 0, 30, 0, TIME_ONESHOT, 200);
    trace("one30b", 0, 30, 0, TIME_ONESHOT, 200);
    trace("one30c", 0, 30, 0, TIME_ONESHOT, 200);
    trace("one30r30", 0, 30, 30, TIME_ONESHOT, 200);
    trace("one30p1", 1, 30, 0, TIME_ONESHOT, 200);
    trace("one30p1b", 1, 30, 0, TIME_ONESHOT, 200);
    trace("one200", 0, 200, 0, TIME_ONESHOT, 400);
    trace("one200r50", 0, 200, 50, TIME_ONESHOT, 400);
    lg("~ trace after pit: %u status %02X", pitmax(), pitstatus());
}

/* when the calls come, counted in timer interrupts (TPC's INT 8 hook in
   front of the driver's handler) from the moment the event was set;
   mid: set halfway between two interrupts of 55 ms instead of just after
   one */
static void kin(const char *name, WORD per, WORD delay, WORD res, WORD flags, WORD ms, int mid)
{
    static char b1[300], b2[300];
    char *p1 = b1, *p2 = b2;
    DWORD i0, t0;
    WORD i, n, c1, s1;
    WORD FAR *il;
    UINT id;
    if (per)
        timeBeginPeriod(per);
    slot(3, CB_REC, 0);
    st->n = 0;
    tickedge();
    if (mid && !enh)
        while (pitcount() > 32768)
            ;
    il = TpcArm();
    i0 = TpcIrq();
    t0 = timeGetTime();
    id = setev(delay, res, 3, flags);
    c1 = enh ? 0 : pitcount();
    s1 = pitstatus();
    waitms(ms);
    if (id && (flags & 1))
        timeKillEvent(id);
    n = st->n;
    for (i = 0; i < n && i < 30; i++) {
        p1 += wsprintf(p1, " %ld", (long)(st->rec[i].irq - i0));
        p2 += wsprintf(p2, " %ld", (long)(st->rec[i].ms - t0));
    }
    *p1 = *p2 = 0;
    lg("~ kin %s k:%s", (LPSTR)name, (LPSTR)b1);
    lg("~ kin %s ms:%s", (LPSTR)name, (LPSTR)b2);
    p1 = b1;
    for (i = 0; i < 12; i++)
        p1 += wsprintf(p1, " %u", il[i]);
    lg("~ kin %s after set: %u %02X; at the interrupts:%s", (LPSTR)name, c1, s1, (LPSTR)b1);
    if (per)
        timeEndPeriod(per);
}

static void sec_kin(void)
{
    static const WORD d55[] = { 1, 10, 30, 50, 54, 55, 56, 60, 100, 109, 110, 111, 150, 200, 250 };
    static const WORD d10[] = { 1, 5, 9, 10, 11, 15, 20, 30, 45 };
    static const WORD d1[] = { 1, 2, 5, 30 };
    char nm[20];
    int i;
    lg("== kin");
    TpcHook(1);
    for (i = 0; i < sizeof d55 / sizeof d55[0]; i++) {
        wsprintf(nm, "p0d%u", d55[i]);
        kin(nm, 0, d55[i], 0, TIME_ONESHOT, 300, 0);
    }
    kin("p0d30mid", 0, 30, 0, TIME_ONESHOT, 300, 1);
    kin("p0d60mid", 0, 60, 0, TIME_ONESHOT, 300, 1);
    kin("p0d100mid", 0, 100, 0, TIME_ONESHOT, 300, 1);
    for (i = 0; i < sizeof d10 / sizeof d10[0]; i++) {
        wsprintf(nm, "p10d%u", d10[i]);
        kin(nm, 10, d10[i], 0, TIME_ONESHOT, 200, 0);
    }
    for (i = 0; i < sizeof d1 / sizeof d1[0]; i++) {
        wsprintf(nm, "p1d%u", d1[i]);
        kin(nm, 1, d1[i], 0, TIME_ONESHOT, 100, 0);
    }
    kin("p10d30r5", 10, 30, 5, TIME_ONESHOT, 200, 0);
    kin("p0d30r5", 0, 30, 5, TIME_ONESHOT, 200, 0);
    kin("p0per100", 0, 100, 0, TIME_PERIODIC, 900, 0);
    kin("p10per30", 10, 30, 0, TIME_PERIODIC, 400, 0);
    kin("p0per30r10", 0, 30, 10, TIME_PERIODIC, 400, 0);
    kin("p0per7", 0, 7, 0, TIME_PERIODIC, 200, 0);
    TpcHook(0);
}

/* the flags' bits: which make an event periodic */
static void sec_flags(void)
{
    static const WORD fl[] = { 0, 1, 2, 3, 4, 0x10, 0x100, 0x8000, 0xFFFE, 0xFFFF };
    int i;
    UINT id;
    lg("== flags");
    for (i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        slot(5, 0, 0);
        id = setev(20, 0, 5, fl[i]);
        waitms(200);
        lg("flags %04X: %s, calls %s", fl[i], (LPSTR)(id ? "id" : "0"),
           (LPSTR)(st->slots[5].count == 0 ? "none" : st->slots[5].count == 1 ? "one" : "several"));
        if (id)
            lg("  timeKillEvent = %u", timeKillEvent(id));
        shim();
    }
}

/* other messages to the timer driver, through MMSYSTEM's open handle */
static void sec_msgs(void)
{
    static const WORD m[] = { 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0x7FF,
        0x801, 0x802, 0x803, 0x805, 0x806, 0x807, 0x809, 0x80A, 0x80B, 0x80D, 0x80E,
        0x80F, 0x811, 0x812, 0x813, 0x815, 0x816, 0x817, 0x818, 0x81C, 0x820, 0x900,
        0x4000, 0x4001 };
    HDRVR h = 0;
    int i;
    lg("== msgs");
    while ((h = GetNextDriver(h, 0)) != 0)
        if (!lstrcmp(owner((WORD)GetDriverModuleHandle(h)), "TIMER") ||
            !lstrcmp(owner((WORD)GetDriverModuleHandle(h)), "TMS"))
            break;
    lg("the timer driver's handle: %s", (LPSTR)(h ? "found" : "none"));
    if (!h)
        return;
    for (i = 0; i < sizeof m / sizeof m[0]; i++)
        lg("SendDriverMessage(%04X, 0, 0) = %08lX", m[i], SendDriverMessage(h, m[i], 0, 0));
    lg("SendDriverMessage(0800, 0, 0) = %08lX", SendDriverMessage(h, 0x800, 0, 0));
    lg("SendDriverMessage(0810, 0, 0) = %08lX", SendDriverMessage(h, 0x810, 0, 0));
    lg("SendDriverMessage(0814, 0, 0) = %08lX", SendDriverMessage(h, 0x814, 0, 0));
    lg("SendDriverMessage(0808, 0, 0) = %s", (LPSTR)(SendDriverMessage(h, 0x808, 0, 0) ? "nonzero" : "0"));
    shim();
}

static void sec_null(void)
{
    lg("== null");
    lg("timeGetDevCaps(NULL, 4) = %u", timeGetDevCaps(NULL, 4));
    shim();
    lg("timeGetSystemTime(NULL, 6) = %u", timeGetSystemTime(NULL, 6));
    shim();
}

int __cdecl __far tmain(void)
{
    ATOM ch = GlobalFindAtom("TPRCHILD");
    HINSTANCE tms;
    me = GetCurrentTask();
    enh = (GetWinFlags() & WF_ENHANCED) != 0;
    _asm { mov myss, ss }
    {
        typedef BOOL (FAR PASCAL *INITAPP)(HINSTANCE);
        INITAPP ia = (INITAPP)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
        ia((HINSTANCE)entry_regs[5]);
    }
    ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    st = TpcState();
    tms = GetModuleHandle("TMS");
    if (tms) {
        tmsset = (SETPROC)GetProcAddress(tms, MAKEINTRESOURCE(4));
        tmsdump = (DUMPPROC)GetProcAddress(tms, MAKEINTRESOURCE(3));
        if (!ch)
            quiet(0x0808);
    }
    if (ch) {
        GlobalDeleteAtom(ch);
        child();
        return 0;
    }
    {
        HFILE f = _lopen("C:\\TPR.ARG", OF_READ);
        if (f != HFILE_ERROR) {
            int n = _lread(f, args, sizeof args - 1);
            args[n > 0 ? n : 0] = 0;
            _lclose(f);
        }
    }
    lg("TPR start (%s)", (LPSTR)(GetWinFlags() & WF_ENHANCED ? "386 Enhanced" :
                               GetWinFlags() & WF_STANDARD ? "Standard" : "other"));
    lg("TIMER %s, TMS %s", (LPSTR)(GetModuleHandle("TIMER") ? "loaded" : "not loaded"),
       (LPSTR)(tms ? "loaded" : "not loaded"));
    lg("== boot");
    shim();
    if (want('W')) { lg("(waiting 7 s)"); waitticks(128); }
    if (want('c')) sec_caps();
    if (want('s')) sec_stime();
    if (want('r')) sec_rate();
    if (want('p')) sec_period();
    if (want('e')) sec_event();
    if (want('i')) sec_ids();
    if (want('o')) sec_oneshot();
    if (want('P')) sec_periodic();
    if (want('x')) sec_context();
    if (want('I')) sec_inside();
    if (want('N')) sec_nest();
    if (want('T')) sec_trace();
    if (want('K')) sec_kin();
    if (want('k')) { lg("(waiting 7 s)"); waitticks(128); sec_kin(); }
    if (want('t')) sec_tod();
    if (want('h')) sec_child();
    if (want('f')) sec_flags();
    if (want('m')) sec_msgs();
    if (want('n')) sec_null();
    lg("== end");
    slot(1, 0, 0);
    lg("timeBeginPeriod(5) left = %u", timeBeginPeriod(5));
    lg("periodic event left: %s", (LPSTR)(setev(100, 0, 1, TIME_PERIODIC) ? "set" : "0"));
    shim();
    lg("TPR end");
    return 0;
}
