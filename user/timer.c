/*
 * timer.c - SetTimer/KillTimer and the tick count.
 *
 * USER drives its timers from one SYSTEM.DRV timer (CreateSystemTimer; the
 * system driver calls it at interrupt time). Each tick counts the timers
 * down; a timer that expires marks itself ready and sets QS_TIMER in its
 * queue. WM_TIMER is made when the task asks for messages, so ready timers
 * never pile up (Pietrek, Windows Internals, ch. 7).
 *
 * The tick runs at interrupt time, so nothing it reads may move or be half
 * changed under it: the timers are not in USER's local heap (they were,
 * and an interrupt landing while USER's heap was being changed walked a
 * list through a block that was no timer - the heap's end sentinel as a
 * queue selector: a fault, or wild writes and lost wake-ups, depending on
 * where the heap's blocks fell), but in fixed global blocks of TCHUNK
 * timers each, chained, never moved or freed; the tick touches only those
 * and the queues (locked global blocks). A slot is filled before it is
 * marked used and marked unused before it is reused, with interrupts off.
 * Chunks are added as needed: no fixed limit (UNVERIFIED: Windows 3.1 has
 * a limit of its own, to be observed).
 */
#include "user.h"

#define TICK 55                 /* ms; UNVERIFIED: Windows' rate follows its timers */
#define TCHUNK 16

typedef struct TIMER {
    WORD   used;
    WORD   seq;                 /* newer timers first, as the list had them */
    HQUEUE hq;
    HWND   hwnd;
    UINT   id;
    UINT   rate;                /* ms */
    long   left;                /* ms until it fires */
    TIMERPROC proc;
    WORD   ready;
    WORD   sys;                 /* SetSystemTimer: WM_SYSTIMER */
} TIMER;
typedef TIMER __far *PTIMER;

typedef struct CHUNK {
    struct CHUNK __far *next;
    TIMER  slot[TCHUNK];
} CHUNK;

static CHUNK __far * __far chunks;      /* in USER's fixed data, as tickcount */
static WORD __far nextseq;
volatile DWORD __far tickcount;
static WORD systimer;

#define FOR_TIMERS(c, i, t) \
    for (c = chunks; c; c = c->next) \
        for (i = 0, t = c->slot; i < TCHUNK; i++, t++)

/* at interrupt time: DS is not loaded; only far (fixed) data */
void __far __pascal timer_tick(void)
{
    CHUNK __far *c;
    PTIMER t;
    int i;
    tickcount += TICK;
    FOR_TIMERS(c, i, t) {
        if (!t->used || t->ready)
            continue;
        t->left -= TICK;
        if (t->left <= 0) {
            t->ready = 1;
            q_of(t->hq)->ntimer++;
            set_wakebit(t->hq, QS_TIMER);
        }
    }
}

void timer_init(void)
{
    systimer = S_CREATESYSTEMTIMER(TICK, (FARPROC)timer_tick);
}

DWORD WINAPI GetTickCount(void)
{
    return S_GETSYSTEMMSECCOUNT();
}

DWORD WINAPI GetCurrentTime(void)
{
    return S_GETSYSTEMMSECCOUNT();
}

DWORD WINAPI GetTimerResolution(void)
{
    return 1000;                /* US2: 03E8h */
}

/* a ready timer of queue hq made not ready (interrupts off): one fewer
   to deliver, and QS_TIMER off with the last. Killing or resetting a
   ready timer took the count down and left QS_TIMER on: GetMessage, with
   the bit on and nothing to give, neither waited nor yielded and went
   round again until some other message came. Program
   Manager's blinking caret did it when File > Run's dialog went away
   with the caret timer ready: WINOLDAP, started from that dialog, got no
   turn for its switch timer until later keys came (boot-test woados,
   intermittent since master 8690c87) */
static void unready(HQUEUE hq)
{
    QUEUE __far *q = q_of(hq);
    if (q->ntimer && !--q->ntimer)
        q->wakebits &= ~QS_TIMER;
}

/* a free slot, a chunk added if none (NULL: no memory) */
static PTIMER free_slot(void)
{
    CHUNK __far *c;
    PTIMER t;
    int i;
    HGLOBAL g;
    WORD f;
    FOR_TIMERS(c, i, t)
        if (!t->used)
            return t;
    g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE, sizeof(CHUNK));
    if (!g)
        return 0;
    c = (CHUNK __far *)GlobalLock(g);
    f = irq_off();
    c->next = chunks;
    chunks = c;
    irq_restore(f);
    return c->slot;
}

static UINT set_timer(HWND h, UINT id, UINT rate, TIMERPROC proc, BOOL sys)
{
    CHUNK __far *c;
    PTIMER t = 0, s;
    int i;
    HQUEUE hq;
    WORD f;
    PWND w = 0;
    if (h) {
        w = valid_wnd(h);
        if (!w)
            return 0;
        hq = w->hq;
    } else
        hq = cur_hq();
    if (!hq)
        return 0;
    if (!h) {
        /* a new id for a window-less timer (3.1 SDK) */
        static UINT nextid = 1;
        id = nextid++;
        if (!nextid)
            nextid = 1;
    }
    if (h)
        FOR_TIMERS(c, i, s)
            if (s->used && s->hwnd == h && s->id == id && s->sys == sys) {
                t = s;
                goto found;
            }
found:
    if (!t) {
        t = free_slot();
        if (!t)
            return 0;
        /* filled while not in use: the tick passes over it */
        t->hwnd = h;
        t->id = id;
        t->hq = hq;
        t->sys = sys;
        t->ready = 0;
        t->rate = rate < TICK ? TICK : rate;
        t->left = t->rate;
        t->proc = proc;
        t->seq = ++nextseq;
        f = irq_off();
        t->used = 1;
        irq_restore(f);
        return h ? id : t->id;
    }
    f = irq_off();
    if (t->ready) {
        t->ready = 0;
        unready(t->hq);
    }
    t->rate = rate < TICK ? TICK : rate;
    t->left = t->rate;
    t->proc = proc;
    irq_restore(f);
    return h ? id : t->id;
}

UINT WINAPI SetTimer(HWND h, UINT id, UINT rate, TIMERPROC proc)
{
    return set_timer(h, id, rate, proc, FALSE);
}

UINT WINAPI U_BEAR11(HWND h, UINT id, UINT rate, TIMERPROC proc)   /* SetSystemTimer */
{
    return set_timer(h, id, rate, proc, TRUE);
}

static void free_timer(PTIMER t)
{
    WORD f = irq_off();
    if (t->used && t->ready)
        unready(t->hq);
    t->used = 0;
    irq_restore(f);
}

static BOOL kill_timer(HWND h, UINT id, BOOL sys)
{
    CHUNK __far *c;
    PTIMER t;
    int i;
    HQUEUE hq = cur_hq();
    FOR_TIMERS(c, i, t)
        if (t->used && t->hwnd == h && t->id == id && t->sys == sys && (h || t->hq == hq)) {
            free_timer(t);
            return TRUE;
        }
    return FALSE;
}

BOOL WINAPI KillTimer(HWND h, UINT id)
{
    return kill_timer(h, id, FALSE);
}

BOOL WINAPI U_KILLSYSTEMTIMER(HWND h, UINT id)
{
    return kill_timer(h, id, TRUE);
}

void kill_queue_timers(HQUEUE hq)
{
    CHUNK __far *c;
    PTIMER t;
    int i;
    FOR_TIMERS(c, i, t)
        if (t->used && t->hq == hq)
            free_timer(t);
}

void kill_window_timers(HWND h)
{
    CHUNK __far *c;
    PTIMER t;
    int i;
    FOR_TIMERS(c, i, t)
        if (t->used && t->hwnd == h)
            free_timer(t);
}

BOOL timer_message(QUEUE __far *q, HQUEUE hq, HWND hf, UINT lo, UINT hi, BOOL remove, MSG __far *m)
{
    CHUNK __far *c;
    PTIMER t, best = 0;
    int i;
    UINT msg;
    if (!q->ntimer) {
        q->wakebits &= ~QS_TIMER;
        return FALSE;
    }
    /* the newest ready timer first (the order of the list it was) */
    FOR_TIMERS(c, i, t) {
        if (!t->used || t->hq != hq || !t->ready)
            continue;
        msg = t->sys ? WM_SYSTIMER : WM_TIMER;
        if (hf && t->hwnd != hf && !IsChild(hf, t->hwnd))
            continue;
        if ((lo || hi) && (msg < lo || msg > hi))
            continue;
        if (!best || (WORD)(t->seq - best->seq) < 0x8000)
            best = t;
    }
    if (best) {
        t = best;
        m->hwnd = t->hwnd;
        m->message = t->sys ? WM_SYSTIMER : WM_TIMER;
        m->wParam = t->id;
        m->lParam = (LPARAM)t->proc;
        m->time = GetTickCount();
        m->pt = ptCursor;
        if (remove) {
            WORD f = irq_off();
            t->ready = 0;
            t->left = t->rate;
            if (q->ntimer && !--q->ntimer)
                q->wakebits &= ~QS_TIMER;
            irq_restore(f);
        }
        return TRUE;
    }
    q->ntimer = 0;
    q->wakebits &= ~QS_TIMER;
    return FALSE;
}
