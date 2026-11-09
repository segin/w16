/*
 * msg.c - message queues and the message functions.
 *
 * One queue per task (InitApp creates it; KERNEL keeps it in the TDB:
 * SetTaskQueue/GetTaskQueue). Posted messages wait in a ring in the queue
 * (8 entries unless SetMessageQueue says otherwise: the 3.1 SDK's
 * default); input, WM_PAINT and WM_TIMER are made when GetMessage or
 * PeekMessage looks for them, in that order after posted messages (Pietrek,
 * Windows Internals, ch. 7). A task waits in KERNEL's WaitEvent; whoever
 * gives it something to do sets a wake bit in its queue and PostEvents it.
 * SendMessage to another task's window queues the call there and waits
 * for the reply, handling messages sent to its own queue meanwhile.
 */
#include "user.h"

HQUEUE hqFirst;
HQUEUE hqBoot;

QUEUE __far *q_of(HQUEUE hq)
{
    return (QUEUE __far *)MAKELP(hq, 0);
}

HQUEUE cur_hq(void)
{
    return K_GETTASKQUEUE(0);
}

HQUEUE create_queue(HTASK task, WORD size)
{
    HGLOBAL g;
    QUEUE __far *q;
    if (!size)
        size = 8;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE,
                    sizeof(QUEUE) + (size - 1) * sizeof(QMSG));
    if (!g)
        return 0;
    q = (QUEUE __far *)GlobalLock(g);
    q->task = task;
    q->size = size;
    q->next = hqFirst;
    hqFirst = FP_SEG(q);
    return FP_SEG(q);
}

void destroy_queue(HQUEUE hq)
{
    HQUEUE __far *l;
    WORD f;
    if (!hq)
        return;
    kill_queue_timers(hq);
    f = irq_off();
    for (l = &hqFirst; *l; l = &q_of(*l)->next)
        if (*l == hq) {
            *l = q_of(hq)->next;
            break;
        }
    if (hqInput == hq)
        hqInput = 0;
    irq_restore(f);
    GlobalFree(LOWORD(GlobalHandle(hq)));
}

void set_wakebit(HQUEUE hq, WORD bits)
{
    QUEUE __far *q;
    WORD f;
    if (!hq)
        return;
    q = q_of(hq);
    f = irq_off();
    q->wakebits |= bits;
    q->changebits |= bits;
    irq_restore(f);
    if (q->task)
        K_POSTEVENT(q->task);
}

void inc_paint(HQUEUE hq)
{
    if (hq) {
        q_of(hq)->npaint++;
        set_wakebit(hq, QS_PAINT);
    }
}

void dec_paint(HQUEUE hq)
{
    QUEUE __far *q;
    if (!hq)
        return;
    q = q_of(hq);
    if (q->npaint && !--q->npaint)
        q->wakebits &= ~QS_PAINT;
}

BOOL post_message(HQUEUE hq, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    QUEUE __far *q;
    QMSG __far *m;
    WORD f;
    if (!hq)
        return FALSE;
    q = q_of(hq);
    f = irq_off();
    if (q->count >= q->size) {
        irq_restore(f);
        return FALSE;
    }
    m = &q->msgs[(q->head + q->count) % q->size];
    q->count++;
    irq_restore(f);
    m->msg.hwnd = h;
    m->msg.message = msg;
    m->msg.wParam = wp;
    m->msg.lParam = lp;
    m->msg.time = GetTickCount();
    m->msg.pt = ptCursor;
    m->extra = 0;
    set_wakebit(hq, QS_POSTMESSAGE);
    return TRUE;
}

BOOL WINAPI PostMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w;
    if (h == (HWND)0xFFFF) {
        HWND t;
        for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext)
            post_message(PW(t)->hq, t, msg, wp, lp);
        return TRUE;
    }
    if (!h)
        return post_message(cur_hq(), 0, msg, wp, lp);
    w = valid_wnd(h);
    if (!w)
        return FALSE;
    return post_message(w->hq, h, msg, wp, lp);
}

BOOL WINAPI PostAppMessage(HTASK t, UINT msg, WPARAM wp, LPARAM lp)
{
    return post_message(K_GETTASKQUEUE(t), 0, msg, wp, lp);
}

void WINAPI PostQuitMessage(int code)
{
    HQUEUE hq = cur_hq();
    if (!hq)
        return;
    q_of(hq)->quit = 1;
    q_of(hq)->quitcode = code;
    set_wakebit(hq, QS_POSTMESSAGE);
}

BOOL WINAPI SetMessageQueue(int size)
{
    HQUEUE old = cur_hq(), hq;
    HTASK t = GetCurrentTask();
    if (size <= 0)
        return FALSE;
    /* the old queue's windows move to the new one (3.1 SDK: call it before
       creating windows; its messages are lost) */
    hq = create_queue(t, size);
    if (!hq)
        return FALSE;
    if (old) {
        QUEUE __far *o = q_of(old), __far *n = q_of(hq);
        n->hinst = o->hinst;
        n->expwinver = o->expwinver;
        {
            HWND w;
            for (w = PW(hwndDesktop)->hwndChild; w; w = PW(w)->hwndNext)
                if (PW(w)->hq == old)
                    PW(w)->hq = hq;
        }
        destroy_queue(old);
    }
    K_SETTASKQUEUE(0, hq);
    return TRUE;
}

/* ---- calling window procedures ---- */
LONG call_wnd(PWND w, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return call_wndproc(w->lpfnWndProc, (WORD)w->hInstance, h, msg, wp, lp);
}

LRESULT WINAPI CallWindowProc(FARPROC f, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    return call_wndproc((WNDPROC)f, w ? (WORD)w->hInstance : 0, h, msg, wp, lp);
}

static void receive_sent(HQUEUE hq)
{
    QUEUE __far *q;
    PSMSG s;
    while (hq && (q = q_of(hq))->sent) {
        WORD outer;
        PWND w;
        s = (PSMSG)q->sent;
        q->sent = s->next;
        if (!q->sent)
            q->wakebits &= ~QS_SENDMESSAGE;
        outer = q->insend;
        q->insend = (WORD)s;
        w = valid_wnd(s->hwnd);
        s->result = w ? call_wnd(w, s->hwnd, s->msg, s->wParam, s->lParam) : 0;
        q = q_of(hq);
        if (q->insend == (WORD)s && !s->done) {
            s->done = 1;
            set_wakebit(s->from, QS_SMRESULT);
        }
        q->insend = outer;
    }
}

LONG send_message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    HQUEUE me, to;
    PSMSG s;
    LONG r;
    if (h == (HWND)0xFFFF) {
        HWND t, n;
        for (t = PW(hwndDesktop)->hwndChild; t; t = n) {
            n = PW(t)->hwndNext;
            send_message(t, msg, wp, lp);
            if (n && !valid_wnd(n))
                break;
        }
        return 0;
    }
    if (!w)
        return 0;
    me = cur_hq();
    to = w->hq;
    if (!to || to == me || !me || !booted)
        return call_wnd(w, h, msg, wp, lp);
    /* another task's window */
    s = (PSMSG)LocalAlloc(LPTR, sizeof(SMSG));
    if (!s)
        return 0;
    s->from = me;
    s->hwnd = h;
    s->msg = msg;
    s->wParam = wp;
    s->lParam = lp;
    {
        QUEUE __far *tq = q_of(to);
        if (!tq->sent)
            tq->sent = (WORD)s;
        else {
            PSMSG t = (PSMSG)tq->sent;
            while (t->next)
                t = (PSMSG)t->next;
            t->next = (WORD)s;
        }
    }
    set_wakebit(to, QS_SENDMESSAGE);
    while (!s->done) {
        receive_sent(me);
        if (s->done)
            break;
        if (!valid_wnd(h)) {
            /* the window went away with the call still queued: unlink it */
            QUEUE __far *tq = q_of(to);
            if (tq->sent == (WORD)s) {
                tq->sent = s->next;
                s->done = 1;
            } else {
                PSMSG t;
                for (t = (PSMSG)tq->sent; t; t = (PSMSG)t->next)
                    if (t->next == (WORD)s) {
                        t->next = s->next;
                        s->done = 1;
                        break;
                    }
            }
            if (s->done)
                break;
        }
        q_of(me)->wakemask = QS_SENDMESSAGE | QS_SMRESULT;
        K_DIRECTEDYIELD(q_of(to)->task);
        if (!s->done && !(q_of(me)->wakebits & (QS_SENDMESSAGE | QS_SMRESULT)))
            K_WAITEVENT(0);
        q_of(me)->wakebits &= ~QS_SMRESULT;
    }
    r = s->result;
    LocalFree((HLOCAL)s);
    return r;
}

LRESULT WINAPI SendMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return send_message(h, msg, wp, lp);
}

void WINAPI ReplyMessage(LRESULT r)
{
    HQUEUE hq = cur_hq();
    PSMSG s;
    if (!hq || !q_of(hq)->insend)
        return;
    s = (PSMSG)q_of(hq)->insend;
    if (!s->done) {
        s->result = r;
        s->done = 1;
        set_wakebit(s->from, QS_SMRESULT);
    }
}

BOOL WINAPI InSendMessage(void)
{
    HQUEUE hq = cur_hq();
    return hq && q_of(hq)->insend != 0;
}

/* ---- getting messages ---- */
static BOOL in_filter(UINT m, UINT lo, UINT hi)
{
    if (!lo && !hi)
        return TRUE;
    return m >= lo && m <= hi;
}

static BOOL wnd_match(HWND h, HWND filter)
{
    return !filter || h == filter || IsChild(filter, h);
}

static BOOL posted_message(HQUEUE hq, HWND hf, UINT lo, UINT hi, BOOL remove, MSG __far *out)
{
    QUEUE __far *q = q_of(hq);
    WORD i, k, f;
    for (i = 0; i < q->count; i++) {
        QMSG __far *m = &q->msgs[(q->head + i) % q->size];
        if (!in_filter(m->msg.message, lo, hi) || !wnd_match(m->msg.hwnd, hf))
            continue;
        *out = m->msg;
        q->msgextra = m->extra;
        if (remove) {
            f = irq_off();
            /* close the gap */
            for (k = i; k + 1 < q->count; k++)
                q->msgs[(q->head + k) % q->size] = q->msgs[(q->head + k + 1) % q->size];
            q->count--;
            if (!q->count)
                q->wakebits &= ~QS_POSTMESSAGE;
            irq_restore(f);
        }
        return TRUE;
    }
    return FALSE;
}

static BOOL get_message(MSG __far *m, HWND hf, UINT lo, UINT hi, UINT flags, BOOL wait)
{
    HQUEUE hq = cur_hq();
    QUEUE __far *q;
    BOOL remove = (flags & PM_REMOVE) != 0;
    if (!hq)
        return FALSE;
    for (;;) {
        q = q_of(hq);
        q->changebits = 0;
        receive_sent(hq);
        if (posted_message(hq, hf, lo, hi, remove, m))
            break;
        q = q_of(hq);
        if (q->quit && in_filter(WM_QUIT, lo, hi)) {
            m->hwnd = 0;
            m->message = WM_QUIT;
            m->wParam = q->quitcode;
            m->lParam = 0;
            m->time = GetTickCount();
            m->pt = ptCursor;
            if (remove)
                q->quit = 0;
            break;
        }
        if (input_message(hq, hf, lo, hi, remove, m))
            break;
        if (in_filter(WM_PAINT, lo, hi) && paint_message(hq, hf, m))
            break;
        if (in_filter(WM_TIMER, lo, hi) && timer_message(q_of(hq), hq, hf, lo, hi, remove, m))
            break;
        if (!wait) {
            if (!(flags & PM_NOYIELD))
                K_OLDYIELD();
            return FALSE;
        }
        q = q_of(hq);
        q->wakemask = QS_ALLINPUT | QS_SENDMESSAGE;
        if (!(q->wakebits & (QS_SENDMESSAGE | QS_POSTMESSAGE | QS_TIMER | QS_PAINT)) && !input_pending)
            K_WAITEVENT(0);
        else if (input_pending && !(q->wakebits & (QS_SENDMESSAGE | QS_POSTMESSAGE | QS_TIMER | QS_PAINT)))
            K_OLDYIELD();
    }
    q = q_of(hq);
    q->msgtime = m->time;
    q->msgpos = MAKELONG(m->pt.x, m->pt.y);
    return TRUE;
}

BOOL WINAPI GetMessage(MSG __far *m, HWND h, UINT lo, UINT hi)
{
    get_message(m, h, lo, hi, PM_REMOVE, TRUE);
    return m->message != WM_QUIT;
}

BOOL WINAPI PeekMessage(MSG __far *m, HWND h, UINT lo, UINT hi, UINT flags)
{
    return get_message(m, h, lo, hi, flags, FALSE);
}

void WINAPI WaitMessage(void)
{
    HQUEUE hq = cur_hq();
    QUEUE __far *q;
    MSG m;
    if (!hq)
        return;
    for (;;) {
        receive_sent(hq);
        q = q_of(hq);
        if (q->count || q->quit || q->npaint || q->ntimer || input_pending)
            return;
        if (PeekMessage(&m, 0, 0, 0, PM_NOREMOVE | PM_NOYIELD))
            return;
        q->wakemask = QS_ALLINPUT | QS_SENDMESSAGE;
        K_WAITEVENT(0);
    }
}

LONG WINAPI DispatchMessage(const MSG __far *m)
{
    PWND w;
    LONG r;
    if (m->message == WM_TIMER && m->lParam)
        return call_wndproc((WNDPROC)m->lParam, 0, m->hwnd, m->message, m->wParam,
                            GetTickCount());
    w = valid_wnd(m->hwnd);
    if (!w)
        return 0;
    r = call_wnd(w, m->hwnd, m->message, m->wParam, m->lParam);
    if (m->message == WM_PAINT && (w = valid_wnd(m->hwnd)) != 0 &&
        (w->hrgnUpdate || (w->flags & WF_INTERNALPAINT))) {
        /* the window did not paint: validate it, as Windows does after
           sending it WM_NCPAINT/WM_ERASEBKGND (UNVERIFIED which of those) */
        PAINTSTRUCT ps;
        BeginPaint(m->hwnd, &ps);
        EndPaint(m->hwnd, &ps);
    }
    return r;
}

DWORD WINAPI GetMessagePos(void)
{
    HQUEUE hq = cur_hq();
    return hq ? q_of(hq)->msgpos : 0;
}

LONG WINAPI GetMessageTime(void)
{
    HQUEUE hq = cur_hq();
    return hq ? q_of(hq)->msgtime : 0;
}

LONG WINAPI GetMessageExtraInfo(void)
{
    HQUEUE hq = cur_hq();
    return hq ? q_of(hq)->msgextra : 0;
}

DWORD WINAPI GetQueueStatus(UINT flags)
{
    HQUEUE hq = cur_hq();
    QUEUE __far *q;
    DWORD r;
    if (!hq)
        return 0;
    q = q_of(hq);
    r = MAKELONG(q->changebits & flags, q->wakebits & flags);
    q->changebits &= ~flags;
    return r;
}

BOOL WINAPI GetInputState(void)
{
    return input_pending != 0;
}

UINT WINAPI RegisterWindowMessage(LPCSTR s)
{
    return AddAtom(s);
}

/* InitApp (USER.5): the task's queue */
int WINAPI U_INITAPP(HINSTANCE hinst)
{
    HQUEUE hq = cur_hq();
    if (!hq) {
        hq = create_queue(GetCurrentTask(), 0);
        if (!hq)
            return 0;
        K_SETTASKQUEUE(0, hq);
    }
    {
        extern LONG WINAPI U_SIGNALPROC(HANDLE, WORD, WORD, HINSTANCE, HQUEUE);
        K_SETTASKSIGNALPROC(0, (FARPROC)U_SIGNALPROC);
    }
    q_of(hq)->hinst = hinst;
    q_of(hq)->expwinver = K_GETEXPWINVER(K_GETEXEPTR(hinst));
    return 1;
}
