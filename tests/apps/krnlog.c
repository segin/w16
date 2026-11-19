/*
 * krnlog.c - KRNLOG.DLL: a recording KERNEL for the sound driver
 * (drivers/sound/README.md, "The beep"; tests/soundprobe.sh beep).
 *
 * A scratch copy of the driver under test (Windows 3.1's MMSOUND.DRV or
 * w16's) has its imports of KERNEL renamed to KRNLOG (tools/nerename.py:
 * the names have the same length), so every KERNEL function it calls
 * comes here: GlobalAlloc, GlobalFree, GlobalLock, Yield, GetCurrentTask,
 * GetModuleHandle, GetProcAddress (the ones either driver imports, from
 * their NE import tables) and the constant __WINFLAGS. Each call is
 * recorded (arguments, the string a name argument points to, the result)
 * and made with KERNEL's own function. When GetProcAddress hands the
 * driver MMSYSTEM's sndPlaySound (by name or by ordinal 2) the driver gets
 * KL_SNDPLAYSOUND instead, which records the sound's name and flags and
 * calls MMSYSTEM's. SN1 (tests/apps/sn1.c) reads the records with KLRECS.
 *
 * __WINFLAGS (KERNEL.178) is a constant entry in KERNEL; a DLL linked by
 * wlink cannot export one, so here it is a label at offset WINFLAGS of a
 * segment of its own (krnlogs.asm): the driver's reference to it is fixed
 * up with that offset. WINFLAGS must therefore be the run's GetWinFlags()
 * (0415h: Microsoft's KRNL286 in Standard mode with a coprocessor, as
 * tests/soundprobe.sh runs it); KLRECS hands it back so that SN1 can check.
 *
 * Imports only KERNEL: SOUND is loaded before USER. Black-box observation
 * (ARCHITECTURE.md 3): only what crosses the interface is recorded.
 */
#include <windows.h>

#define NREC 120

/* what was called */
#define KF_GLOBALALLOC  15
#define KF_GLOBALFREE   17
#define KF_GLOBALLOCK   18
#define KF_YIELD        29
#define KF_GETCURRENTTASK 36
#define KF_GETMODULEHANDLE 47
#define KF_GETPROCADDRESS 50
#define KF_SNDPLAYSOUND 1002    /* MMSYSTEM.2, through the wrapper */
#define KF_TIMESETEVENT 1602    /* MMSYSTEM.602 */
#define KF_TIMEKILLEVENT 1603   /* MMSYSTEM.603 */
#define KF_TIMECB       1604    /* the driver's timer callback, called */

struct krec {                   /* 48 bytes */
    WORD fn, seq;
    WORD a1, a2, a3;            /* the arguments (words, left to right) */
    WORD task;                  /* GetCurrentTask at the call */
    DWORD r;                    /* the result, DX:AX */
    WORD flags;                 /* the caller's flags (IF) */
    WORD depth;                 /* calls of this DLL in progress */
    WORD rep;                   /* the same call again right after: Yield,
                                   GetCurrentTask, the timer callback */
    char s[22];                 /* the string a name argument points to */
};

struct khdr {                   /* KLRECS's answer */
    WORD n, max, size, seq, winflags, lost;
    struct krec FAR *recs;
    WORD yields, ticks;         /* Yield calls, timer callbacks (counted) */
};

typedef DWORD (FAR PASCAL *P0)(void);
typedef DWORD (FAR PASCAL *P1)(WORD);
typedef DWORD (FAR PASCAL *PAL)(WORD, DWORD);
typedef DWORD (FAR PASCAL *PNAMEFN)(LPCSTR);
typedef DWORD (FAR PASCAL *PGPA)(WORD, LPCSTR);
typedef DWORD (FAR PASCAL *PSND)(LPCSTR, WORD);
typedef void (FAR PASCAL *TCB)(WORD, WORD, DWORD, DWORD, DWORD);
typedef DWORD (FAR PASCAL *PSET)(WORD, WORD, TCB, DWORD, WORD);

extern WORD kl_flags(void);
#pragma aux kl_flags = "pushf" "pop ax" value [ax];
extern void kl_cli(void);
#pragma aux kl_cli = "cli";
extern void kl_sti(void);
#pragma aux kl_sti = "sti";
extern WORD __cdecl __far kl_winflags(void);    /* krnlogs.asm */

static struct krec recs[NREC];
static struct khdr hdr;
static WORD depth;
static PAL r_alloc;
static P1 r_free, r_lock;
static P0 r_yield, r_task;
static PNAMEFN r_gmh;
static PGPA r_gpa;
static PSND r_snd;              /* MMSYSTEM's sndPlaySound, as last handed out */
static PSET r_set;              /* timeSetEvent */
static P1 r_kill;               /* timeKillEvent */
static TCB r_cb;                /* the driver's timer callback, as last given */

int __cdecl libmain(void)
{
    HMODULE k = GetModuleHandle("KERNEL");
    r_alloc = (PAL)GetProcAddress(k, MAKEINTRESOURCE(15));
    r_free = (P1)GetProcAddress(k, MAKEINTRESOURCE(17));
    r_lock = (P1)GetProcAddress(k, MAKEINTRESOURCE(18));
    r_yield = (P0)GetProcAddress(k, MAKEINTRESOURCE(29));
    r_task = (P0)GetProcAddress(k, MAKEINTRESOURCE(36));
    r_gmh = (PNAMEFN)GetProcAddress(k, MAKEINTRESOURCE(47));
    r_gpa = (PGPA)GetProcAddress(k, MAKEINTRESOURCE(50));
    hdr.max = NREC;
    hdr.size = sizeof(struct krec);
    hdr.winflags = kl_winflags();
    hdr.recs = recs;
    return 1;
}

static struct krec *newrec(WORD fn)
{
    struct krec *t;
    WORD fl = kl_flags();
    kl_cli();
    hdr.seq++;
    if (fn == KF_YIELD) {
        hdr.yields++;               /* counted only */
        t = 0;
    } else if (fn == KF_TIMECB) {
        hdr.ticks++;
        t = 0;
    } else if (fn == KF_GETCURRENTTASK && hdr.n
        && recs[hdr.n - 1].fn == fn && recs[hdr.n - 1].depth == depth
        && ((recs[hdr.n - 1].flags ^ fl) & 0x200) == 0) {
        recs[hdr.n - 1].rep++;
        t = 0;
    } else if (hdr.n < NREC) {
        t = &recs[hdr.n++];
    } else {
        hdr.lost++;
        t = 0;
    }
    if (fl & 0x200)
        kl_sti();
    if (t) {
        t->fn = fn;
        t->seq = hdr.seq;
        t->a1 = t->a2 = t->a3 = 0;
        t->r = 0;
        t->flags = fl;
        t->depth = depth;
        t->rep = 0;
        t->s[0] = 0;
        t->task = LOWORD(r_task());
    }
    return t;
}

/* a name argument: the string when it is one (a selector in the high
   word), else nothing (the low word is logged) */
static void name(struct krec *t, LPCSTR s)
{
    int i;
    if (!t || !SELECTOROF(s))
        return;
    for (i = 0; i < (int)sizeof t->s - 1 && s[i]; i++)
        t->s[i] = s[i];
    t->s[i] = 0;
}

static DWORD done(struct krec *t, DWORD r)
{
    if (t)
        t->r = r;
    depth--;
    return r;
}

DWORD FAR PASCAL __loadds KL_GLOBALALLOC(WORD f, DWORD n)
{
    struct krec *t;
    depth++;
    t = newrec(KF_GLOBALALLOC);
    if (t) {
        t->a1 = f;
        t->a2 = HIWORD(n);
        t->a3 = LOWORD(n);
    }
    return done(t, r_alloc(f, n));
}

DWORD FAR PASCAL __loadds KL_GLOBALFREE(WORD h)
{
    struct krec *t;
    depth++;
    t = newrec(KF_GLOBALFREE);
    if (t)
        t->a1 = h;
    return done(t, r_free(h));
}

DWORD FAR PASCAL __loadds KL_GLOBALLOCK(WORD h)
{
    struct krec *t;
    depth++;
    t = newrec(KF_GLOBALLOCK);
    if (t)
        t->a1 = h;
    return done(t, r_lock(h));
}

DWORD FAR PASCAL __loadds KL_YIELD(void)
{
    struct krec *t;
    depth++;
    t = newrec(KF_YIELD);
    return done(t, r_yield());
}

DWORD FAR PASCAL __loadds KL_GETCURRENTTASK(void)
{
    struct krec *t;
    depth++;
    t = newrec(KF_GETCURRENTTASK);
    return done(t, r_task());
}

DWORD FAR PASCAL __loadds KL_GETMODULEHANDLE(LPCSTR s)
{
    struct krec *t;
    depth++;
    t = newrec(KF_GETMODULEHANDLE);
    if (t) {
        t->a1 = SELECTOROF(s);
        t->a2 = OFFSETOF(s);
    }
    name(t, s);
    return done(t, r_gmh(s));
}

DWORD FAR PASCAL __loadds KL_SNDPLAYSOUND(LPCSTR s, WORD f)
{
    struct krec *t;
    depth++;
    t = newrec(KF_SNDPLAYSOUND);
    if (t) {
        t->a1 = SELECTOROF(s);
        t->a2 = OFFSETOF(s);
        t->a3 = f;
    }
    if (!(f & 0x0004))          /* SND_MEMORY: an image, not a name */
        name(t, s);
    return done(t, r_snd ? r_snd(s, f) : 0);
}

/* the timer callback the driver gave timeSetEvent: called through
   KL_TIMECB, which records each call (at interrupt time) and the calls
   made inside it (depth) */
DWORD FAR PASCAL __loadds KL_TIMECB(WORD id, WORD msg, DWORD user, DWORD d1, DWORD d2)
{
    struct krec *t;
    depth++;
    t = newrec(KF_TIMECB);
    if (t) {
        t->a1 = id;
        t->a2 = msg;
        t->a3 = LOWORD(user);
        t->r = user;
    }
    if (r_cb)
        r_cb(id, msg, user, d1, d2);
    depth--;
    return 0;
}

DWORD FAR PASCAL __loadds KL_TIMESETEVENT(WORD delay, WORD res, TCB cb, DWORD user, WORD flags)
{
    struct krec *t;
    depth++;
    t = newrec(KF_TIMESETEVENT);
    if (t) {
        t->a1 = delay;
        t->a2 = res;
        t->a3 = flags;
        /* the callback (is it the one before?) and dwUser in s */
        t->s[0] = (char)(cb == r_cb ? 1 : r_cb ? 2 : 0);
        *(DWORD *)(t->s + 2) = user;
        *(DWORD *)(t->s + 6) = (DWORD)cb;
    }
    if (cb && SELECTOROF(cb)) {
        r_cb = cb;
        cb = (TCB)KL_TIMECB;
    }
    return done(t, r_set ? r_set(delay, res, cb, user, flags) : 0);
}

DWORD FAR PASCAL __loadds KL_TIMEKILLEVENT(WORD id)
{
    struct krec *t;
    depth++;
    t = newrec(KF_TIMEKILLEVENT);
    if (t)
        t->a1 = id;
    return done(t, r_kill ? r_kill(id) : 0);
}

static int isname(LPCSTR s, const char *n)
{
    for (; *n; s++, n++) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != *n)
            return 0;
    }
    return *s == 0;
}

DWORD FAR PASCAL __loadds KL_GETPROCADDRESS(WORD h, LPCSTR s)
{
    struct krec *t;
    DWORD r;
    depth++;
    t = newrec(KF_GETPROCADDRESS);
    if (t) {
        t->a1 = h;
        t->a2 = SELECTOROF(s);
        t->a3 = OFFSETOF(s);
    }
    name(t, s);
    r = r_gpa(h, s);
    /* MMSYSTEM's sndPlaySound, timeSetEvent and timeKillEvent: handed
       out wrapped */
    if (r && h && h == LOWORD(r_gmh("MMSYSTEM"))) {
        if (SELECTOROF(s) ? isname(s, "SNDPLAYSOUND") : OFFSETOF(s) == 2) {
            r_snd = (PSND)r;
            r = (DWORD)(FARPROC)KL_SNDPLAYSOUND;
        } else if (SELECTOROF(s) ? isname(s, "TIMESETEVENT") : OFFSETOF(s) == 602) {
            r_set = (PSET)r;
            r = (DWORD)(FARPROC)KL_TIMESETEVENT;
        } else if (SELECTOROF(s) ? isname(s, "TIMEKILLEVENT") : OFFSETOF(s) == 603) {
            r_kill = (P1)r;
            r = (DWORD)(FARPROC)KL_TIMEKILLEVENT;
        }
    }
    return done(t, r);
}

/* the records: SN1 reads them through this pointer, and clears them by
   setting n to 0 */
struct khdr FAR * FAR PASCAL __loadds KLRECS(void)
{
    return &hdr;
}

int FAR PASCAL __loadds WEP(int x)
{
    (void)x;
    return 1;
}
