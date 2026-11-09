/*
 * timer.c - SetTimer/KillTimer and the tick count.
 *
 * USER drives its timers from one SYSTEM.DRV timer (CreateSystemTimer; the
 * system driver calls it at interrupt time). Each tick counts the timers
 * down; a timer that expires marks itself ready and sets QS_TIMER in its
 * queue. WM_TIMER is made when the task asks for messages, so ready timers
 * never pile up (Pietrek, Windows Internals, ch. 7). The timers are a list
 * in USER's heap: no fixed limit (UNVERIFIED: Windows 3.1 has a limit of
 * its own, to be observed).
 */
#include "user.h"

#define TICK 55                 /* ms; UNVERIFIED: Windows' rate follows its timers */

typedef struct TIMER {
    WORD   next;
    HQUEUE hq;
    HWND   hwnd;
    UINT   id;
    UINT   rate;                /* ms */
    long   left;                /* ms until it fires */
    TIMERPROC proc;
    WORD   ready;
    WORD   sys;                 /* SetSystemTimer: WM_SYSTIMER */
} TIMER;
typedef TIMER __near *PTIMER;

static WORD timers;
volatile DWORD tickcount;
static WORD systimer;

void __far __pascal __loadds timer_tick(void)
{
    PTIMER t;
    tickcount += TICK;
    for (t = (PTIMER)timers; t; t = (PTIMER)t->next) {
        if (t->ready)
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

static UINT set_timer(HWND h, UINT id, UINT rate, TIMERPROC proc, BOOL sys)
{
    PTIMER t;
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
    f = irq_off();
    for (t = (PTIMER)timers; t; t = (PTIMER)t->next)
        if (h && t->hwnd == h && t->id == id && t->sys == sys)
            break;
    irq_restore(f);
    if (!t) {
        t = (PTIMER)LocalAlloc(LPTR, sizeof(TIMER));
        if (!t)
            return 0;
        t->hwnd = h;
        t->id = id;
        t->hq = hq;
        t->sys = sys;
        f = irq_off();
        t->next = timers;
        timers = (WORD)t;
        irq_restore(f);
    }
    f = irq_off();
    if (t->ready) {
        t->ready = 0;
        if (q_of(t->hq)->ntimer)
            q_of(t->hq)->ntimer--;
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
    WORD __far *l;
    for (l = &timers; *l; l = &((PTIMER)*l)->next)
        if (*l == (WORD)t) {
            *l = t->next;
            break;
        }
    if (t->ready && q_of(t->hq)->ntimer)
        q_of(t->hq)->ntimer--;
    irq_restore(f);
    LocalFree((HLOCAL)t);
}

static BOOL kill_timer(HWND h, UINT id, BOOL sys)
{
    PTIMER t;
    HQUEUE hq = cur_hq();
    for (t = (PTIMER)timers; t; t = (PTIMER)t->next)
        if (t->hwnd == h && t->id == id && t->sys == sys && (h || t->hq == hq)) {
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
    PTIMER t, n;
    for (t = (PTIMER)timers; t; t = n) {
        n = (PTIMER)t->next;
        if (t->hq == hq)
            free_timer(t);
    }
}

void kill_window_timers(HWND h)
{
    PTIMER t, n;
    for (t = (PTIMER)timers; t; t = n) {
        n = (PTIMER)t->next;
        if (t->hwnd == h)
            free_timer(t);
    }
}

BOOL timer_message(QUEUE __far *q, HQUEUE hq, HWND hf, UINT lo, UINT hi, BOOL remove, MSG __far *m)
{
    PTIMER t;
    if (!q->ntimer)
        return FALSE;
    for (t = (PTIMER)timers; t; t = (PTIMER)t->next) {
        UINT msg;
        if (t->hq != hq || !t->ready)
            continue;
        msg = t->sys ? WM_SYSTIMER : WM_TIMER;
        if (hf && t->hwnd != hf && !IsChild(hf, t->hwnd))
            continue;
        if ((lo || hi) && (msg < lo || msg > hi))
            continue;
        m->hwnd = t->hwnd;
        m->message = msg;
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
