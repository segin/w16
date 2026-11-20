/*
 * conv.c - DDEML conversations: connecting (DdeConnect, DdeConnectList,
 * DdeReconnect, the server side of WM_DDE_INITIATE), transactions
 * (DdeClientTransaction, DdeAbandonTransaction, DdePostAdvise), the
 * client and server windows, blocking (DdeEnableCallback) and the
 * queries (ARCHITECTURE.md, chapter "DDEML").
 *
 * A conversation is a child window of the instance's DMGClass (client,
 * class DMGClientClass), of a conversation list's DMGHoldingClass window
 * (a client in a list) or of the instance's own DMGHoldingClass (server,
 * DMGServerClass); HCONV is MAKELONG(window, sequence number), as 3.1's.
 * Every message on the wire is plain DDE, so a DDEML application talks
 * to raw-DDE applications (Program Manager) and to other DDEML builds.
 *
 * A client's INITIATE goes to every top-level window in turn
 * (EnumWindows, InitEnum), as 3.1's does (tests/ddeml/scripts/xconn.dm:
 * raw windows past the first server never see a DdeConnect's): a
 * DdeConnect stops at the first window that answers.
 */
#include "ddeml.h"

static WORD ds_sel(void)
{
    return SELECTOROF((void FAR *)&ddeml_insts);
}

BOOL local_window(HWND h, LPCSTR cls)
{
    char b[32];
    if (!h || !IsWindow(h) || (HINSTANCE)GetWindowWord(h, GWW_HINSTANCE) != ddeml_inst)
        return FALSE;
    GetClassName(h, b, sizeof b);
    return !lstrcmpi(b, cls);
}

/* an instance window of any DDEML library */
BOOL is_dmgclass(HWND h)
{
    char b[32];
    if (!h || !IsWindow(h))
        return FALSE;
    GetClassName(h, b, sizeof b);
    return !lstrcmpi(b, CLS_DMG);
}

static CONV NEAR *conv_of_window(HWND h)
{
    INST NEAR *in;
    CONV NEAR *c;
    if (!h)
        return NULL;
    for (in = ddeml_insts; in; in = in->next)
        for (c = in->convs; c; c = c->next)
            if (c->hwnd == h)
                return c;
    return NULL;
}

CONV NEAR *conv_find(HCONV h)
{
    CONV NEAR *c = conv_of_window((HWND)LOWORD(h));
    return c && c->seq == HIWORD(h) ? c : (CONV NEAR *)0;
}

HCONV conv_handle(CONV NEAR *c)
{
    return (HCONV)MAKELONG(c->hwnd, c->seq);
}

/* the partner's handle as 3.1's monitor blocks give it: its window
   and the window's third word (a DDEML window's number; whatever the
   word holds for another window); in a link block a partner that is
   not a window of this library has number 0 (xmon.dm as xmon+x) */
HCONV partner_handle(CONV NEAR *c, BOOL inlink)
{
    HWND p = c->partner;
    if (!p)
        return 0;
    if (inlink) {
        CONV NEAR *o = (local_window(p, CLS_CLIENT) || local_window(p, CLS_SERVER))
                           ? conv_of_window(p) : (CONV NEAR *)0;
        return o ? conv_handle(o) : (HCONV)MAKELONG(p, 0);
    }
    return (HCONV)MAKELONG(p, IsWindow(p) ? GetWindowWord(p, 4) : 0);
}

/* a client window of this library initiating for a list */
BOOL conv_listing(HWND client)
{
    CONV NEAR *c = local_window(client, CLS_CLIENT) ? conv_of_window(client) : (CONV NEAR *)0;
    return c && c->mode == CM_LIST;
}

HWND newwin(INST NEAR *in, LPCSTR cls, HWND parent, DWORD style, WORD seq)
{
    HWND h = CreateWindow(cls, "", style, 0, 0, 0, 0, parent, NULL, ddeml_inst, NULL);
    (void)in;
    (void)seq;
    return h;
}

static CONV NEAR *conv_new(INST NEAR *in, BOOL client, HWND parent)
{
    CONV NEAR *c = (CONV NEAR *)nalloc(sizeof(CONV)), NEAR * NEAR *pp;
    if (!c)
        return NULL;
    c->inst = in;
    c->seq = ddeml_seq++;
    c->hwnd = newwin(in, client ? CLS_CLIENT : CLS_SERVER, parent, WS_CHILD, c->seq);
    if (!c->hwnd) {
        nfree(c);
        return NULL;
    }
    SetWindowWord(c->hwnd, 0, (WORD)c);
    SetWindowWord(c->hwnd, 2, ds_sel());
    SetWindowWord(c->hwnd, 4, c->seq);
    c->status = client ? ST_CLIENT : 0;
    c->cc.cb = sizeof(CONVCONTEXT);
    c->cc.iCodePage = CP_WINANSI;
    for (pp = &in->convs; *pp; pp = &(*pp)->next)
        ;
    *pp = c;
    return c;
}

void conv_free(CONV NEAR *c)
{
    CONV NEAR * NEAR *pp;
    INST NEAR *in = c->inst;
    for (pp = &in->convs; *pp; pp = &(*pp)->next)
        if (*pp == c) {
            *pp = c->next;
            break;
        }
    while (c->xacts) {
        XACT NEAR *x = c->xacts;
        c->xacts = x->next;
        nfree(x);
    }
    while (c->links) {
        LINK NEAR *l = c->links;
        c->links = l->next;
        hsz_dec(l->item);
        nfree(l);
    }
    while (c->queue) {
        QMSG NEAR *q = c->queue;
        c->queue = q->next;
        nfree(q);
    }
    /* the names it holds, as 3.1 lets them go (MH_DECATOM: service,
       service, topic) */
    hsz_dec(c->svcref);
    hsz_dec(c->svcpartner);
    hsz_dec(c->svcreq);
    hsz_dec(c->topic);
    if (c->hwnd && IsWindow(c->hwnd))
        DestroyWindow(c->hwnd);
    nfree(c);
}

static HSZ hsz(ATOM a)
{
    return (HSZ)MAKELONG(a, 0);
}

/* ---- connecting: the server side ---- */

static BOOL registered(INST NEAR *in, ATOM a)
{
    NAME NEAR *n;
    for (n = in->names; n; n = n->next)
        if (n->atom == a)
            return TRUE;
    return FALSE;
}

static HWND holding(INST NEAR *in)
{
    if (!in->hold || !IsWindow(in->hold)) {
        WORD seq = ddeml_seq++;
        in->hold = newwin(in, CLS_HOLD, in->dmg, WS_CHILD, seq);
        if (in->hold) {
            SetWindowWord(in->hold, 0, 0);
            SetWindowWord(in->hold, 2, 0);
            SetWindowWord(in->hold, 4, seq);
            SetWindowWord(in->hold, 6, 0);
        }
    }
    return in->hold;
}

static void server_accept(INST NEAR *in, HWND client, ATOM app, ATOM topic, BOOL same,
                          CONVCONTEXT FAR *pcc)
{
    CONV NEAR *c = conv_new(in, FALSE, holding(in));
    HWND sw;
    HCONV hc;
    ATOM a1, a2;
    if (!c)
        return;
    c->partner = client;
    if (pcc)
        bcopy(&c->cc, pcc, sizeof(CONVCONTEXT));
    c->status = ST_CONNECTED;
    c->convst = XST_CONNECTED;
    /* as 3.1: the monitors hear of it first, then it takes its names
       (service twice, topic) and the two for the ACK */
    c->svcreq = app;
    c->topic = topic;
    mon_conv(c, TRUE);
    c->svcref = hsz_inc(app);
    hsz_inc(app);
    hsz_inc(topic);
    a1 = hsz_inc(app);
    a2 = hsz_inc(topic);
    sw = c->hwnd;
    SendMessage(client, WM_DDE_ACK, (WPARAM)sw, MAKELPARAM(a1, a2));
    /* the client may have terminated it already (a second answer to
       a DdeConnect; another library's list asking again): 3.1 names
       whatever window has the handle now, by its third word, or no
       conversation when there is none (xconn.dm as xconn+x) */
    hc = IsWindow(sw) ? (HCONV)MAKELONG(sw, GetWindowWord(sw, 4)) : 0;
    c = conv_find(hc);
    callback_hc(in, XTYP_CONNECT_CONFIRM, 0, c, hc, hsz(topic), hsz(app), 0, 0, same);
}

/* an INITIATE to an instance: TRUE if it made a conversation */
BOOL conv_initiate(INST NEAR *in, HWND dmg, ATOM app, ATOM topic, HWND from)
{
    CONV NEAR *cc = local_window(from, CLS_CLIENT) ? conv_of_window(from) : (CONV NEAR *)0;
    BOOL same = cc && cc->inst == in;
    CONVCONTEXT FAR *pcc = cc ? &cc->cc : NULL;
    BOOL made = FALSE;
    (void)dmg;
    if (in->flags & (IF_CLIENTONLY | IF_MONITOR | IF_DYING))
        return FALSE;
    if (in->afCmd & CBF_FAIL_CONNECTIONS)
        return FALSE;
    if (same && (in->afCmd & CBF_FAIL_SELFCONNECTIONS))
        return FALSE;
    if (cc && cc->mode == CM_SINGLE && cc->partner)
        return FALSE;                   /* that DdeConnect has its server */
    if (in->flags & IF_FILTER) {
        if (app ? !registered(in, app) : !in->names)
            return FALSE;
    }
    if (app && topic) {
        if (callback(in, XTYP_CONNECT, 0, NULL, hsz(topic), hsz(app), 0, (DWORD)pcc, same)) {
            server_accept(in, from, app, topic, same, pcc);
            made = TRUE;
        }
    } else {
        HDDEDATA r = callback(in, XTYP_WILDCONNECT, 0, NULL, hsz(topic), hsz(app), 0, (DWORD)pcc,
                              same);
        DATA NEAR *d = data_find(r);
        if (d) {
            HSZPAIR FAR *p = (HSZPAIR FAR *)GlobalLock(d->h);
            DWORD n = GlobalSize(d->h) - ((d->flags & DF_NOHEADER) ? 0 : 4);
            WORD i, k = (WORD)(n / sizeof(HSZPAIR));
            if (p) {
                if (!(d->flags & DF_NOHEADER))
                    p = (HSZPAIR FAR *)((BYTE FAR *)p + 4);
                for (i = 0; i < k && (p[i].hszSvc || p[i].hszTopic); i++) {
                    if (cc && cc->mode == CM_SINGLE && !IsWindow(from))
                        break;
                    server_accept(in, from, LOWORD(p[i].hszSvc), LOWORD(p[i].hszTopic), same, pcc);
                    made = TRUE;
                }
                GlobalUnlock(d->h);
            }
            data_free(d, TRUE);
        }
    }
    return made;
}

/* ---- connecting: the client side ---- */

static void client_connected(CONV NEAR *c, HWND server, ATOM app, ATOM topic)
{
    c->partner = server;
    c->svcpartner = app;
    c->topic = topic;
    c->status |= ST_CONNECTED;
    if (local_window(server, CLS_SERVER)) {
        CONV NEAR *s = conv_of_window(server);
        c->status |= ST_ISLOCAL;
        if (s && s->inst == c->inst) {  /* status 0115 on 3.1 (conn.dm) */
            c->status |= ST_ISSELF;
            s->status |= ST_ISSELF;
        }
    }
    c->convst = XST_CONNECTED;
    c->ltype = XTYP_CONNECT;
    c->lfmt = CF_TEXT;
    mon_conv(c, TRUE);
    c->svcreq = hsz_inc(c->svcask);
}

typedef struct {
    HCONV hc;
    ATOM app, topic;
} IENUM;

/* EnumWindows: the INITIATE to one top-level window. A monitor hears of
   one to another library's instance window after it (the hook leaves
   such sends of this library's clients out), unless the DdeConnect ends
   there (tests/ddeml/scripts/xmon2.dm) */
BOOL FAR PASCAL __loadds InitEnum(HWND w, LPARAM lp)
{
    IENUM FAR *e = (IENUM FAR *)lp;
    CONV NEAR *c = conv_find(e->hc);
    BOOL other;
    if (!c)
        return FALSE;
    c->enumwin = w;
    other = is_dmgclass(w) && !local_window(w, CLS_DMG);
    SendMessage(w, WM_DDE_INITIATE, (WPARAM)c->hwnd, MAKELPARAM(e->app, e->topic));
    c = conv_find(e->hc);
    if (!c || (c->mode == CM_SINGLE && c->partner))
        return FALSE;
    if (other)
        mon_msg(FALSE, w, WM_DDE_INITIATE, (WPARAM)c->hwnd, MAKELPARAM(e->app, e->topic));
    return TRUE;
}

static CONV NEAR *client_start(INST NEAR *in, HSZ svc, HSZ topic, CONVCONTEXT FAR *pcc, WORD mode,
                               HWND parent)
{
    CONV NEAR *c = conv_new(in, TRUE, parent);
    IENUM e;
    if (!c)
        return NULL;
    c->mode = mode;
    c->svcask = LOWORD(svc);
    if (pcc)
        bcopy(&c->cc, pcc, sizeof(CONVCONTEXT));
    c->convst = XST_INIT1;
    c->initiating = TRUE;
    e.hc = conv_handle(c);
    e.app = LOWORD(svc);
    e.topic = LOWORD(topic);
    EnumWindows((WNDENUMPROC)InitEnum, (LPARAM)(IENUM FAR *)&e);
    c = conv_find(e.hc);
    if (c)
        c->initiating = FALSE;
    return c;
}

/* a further answer to a list's INITIATE. A server window of this
   library: a new client window for it. Any other server: 3.1 hangs up
   at once and asks that same top-level window again, for that service
   and topic, from a new client window (xconn.dm as xconn+x) */
static void list_answer(CONV NEAR *c, HWND srv, ATOM app, ATOM topic)
{
    INST NEAR *in = c->inst;
    HWND parent = GetParent(c->hwnd);
    CONV NEAR *n;
    if (local_window(srv, CLS_SERVER)) {
        CONV NEAR *s = conv_of_window(srv);
        n = conv_new(in, TRUE, parent);
        if (!n)
            return;
        n->mode = CM_LIST;
        n->svcask = c->svcask;
        bzero(&n->cc, sizeof n->cc);    /* 3.1: all zero (xconn2.dm) */
        client_connected(n, srv, app, topic);
        if (s)
            s->partner = n->hwnd;
        return;
    }
    {
        HCONV hc = conv_handle(c), nh;
        HWND top = c->enumwin;
        CONVCONTEXT cc;
        bcopy(&cc, &c->cc, sizeof cc);
        SendMessage(srv, WM_DDE_TERMINATE, (WPARAM)c->hwnd, 0);
        if (conv_find(hc)) {
            n = conv_new(in, TRUE, parent);
            if (n) {
                n->mode = CM_LIST;
                n->svcask = app;
                bcopy(&n->cc, &cc, sizeof cc);
                n->convst = XST_INIT1;
                n->initiating = TRUE;
                n->enumwin = top;
                nh = conv_handle(n);
                SendMessage(top, WM_DDE_INITIATE, (WPARAM)n->hwnd, MAKELPARAM(app, topic));
                n = conv_find(nh);
                if (n) {
                    n->initiating = FALSE;
                    if (!n->partner)
                        conv_free(n);
                }
            }
        }
        GlobalDeleteAtom(app);
        GlobalDeleteAtom(topic);
    }
}

static INST NEAR *inst_arg(DWORD id)
{
    INST NEAR *in = inst_find(id);
    if (!in)
        seterr(NULL, id ? DMLERR_INVALIDPARAMETER : DMLERR_DLL_NOT_INITIALIZED);
    return in;
}

HCONV DMLAPI DdeConnect(DWORD id, HSZ svc, HSZ topic, CONVCONTEXT FAR *pcc)
{
    INST NEAR *in = inst_arg(id);
    CONV NEAR *c;
    if (!in)
        return 0;
    c = client_start(in, svc, topic, pcc, CM_SINGLE, in->dmg);
    if (!c || !c->partner) {
        if (c)
            conv_free(c);
        seterr(in, DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    return conv_handle(c);
}

/* ---- conversation lists ---- */

static HCONVLIST list_handle(CLIST NEAR *l)
{
    return (HCONVLIST)MAKELONG(l->hwnd, l->seq);
}

static CLIST NEAR *list_find(HCONVLIST h)
{
    INST NEAR *in;
    CLIST NEAR *l;
    if (!h)
        return NULL;
    for (in = ddeml_insts; in; in = in->next)
        for (l = in->lists; l; l = l->next)
            if ((WORD)l->hwnd == LOWORD(h) && l->seq == HIWORD(h))
                return l;
    return NULL;
}

static CLIST NEAR *list_of(CONV NEAR *c)
{
    CLIST NEAR *l;
    HWND p = c->hwnd ? GetParent(c->hwnd) : NULL;
    for (l = c->inst->lists; l; l = l->next)
        if (l->hwnd == p)
            return l;
    return NULL;
}

static void list_drop(CLIST NEAR *l)
{
    CLIST NEAR * NEAR *pp;
    for (pp = &l->inst->lists; *pp; pp = &(*pp)->next)
        if (*pp == l) {
            *pp = l->next;
            break;
        }
    if (IsWindow(l->hwnd))
        DestroyWindow(l->hwnd);
    nfree(l);
}

/* does the list have a live conversation with this one's service and
   topic */
static BOOL list_has(CLIST NEAR *l, CONV NEAR *c)
{
    HWND h;
    for (h = GetWindow(l->hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        CONV NEAR *o = conv_of_window(h);
        if (o && !o->dead && o->svcpartner == c->svcpartner && o->topic == c->topic)
            return TRUE;
    }
    return FALSE;
}

/* A list is a DMGHoldingClass child of the client's DMGClass (words:
   instance, 0, number, 0); its conversations are its children, in the
   order USER keeps them (made later, further down). With an old list,
   the new conversations that duplicate one of its conversations are
   terminated, then the old list's windows move into the new one (each
   to the top: their order turns round) and the old window goes. With
   no conversation at all: 0 and no error, and the window stays (3.1,
   tests/ddeml/scripts/xconn2.dm). */
HCONVLIST DMLAPI DdeConnectList(DWORD id, HSZ svc, HSZ topic, HCONVLIST old, CONVCONTEXT FAR *pcc)
{
    INST NEAR *in = inst_arg(id);
    CLIST NEAR *l, NEAR *ol = old ? list_find(old) : (CLIST NEAR *)0;
    CONV NEAR *c;
    HWND h, nx;
    if (!in)
        return 0;
    if (old && !ol) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    l = (CLIST NEAR *)nalloc(sizeof(CLIST));
    if (!l) {
        seterr(in, DMLERR_MEMORY_ERROR);
        return 0;
    }
    l->inst = in;
    l->seq = ddeml_seq++;
    l->hwnd = newwin(in, CLS_HOLD, in->dmg, WS_CHILD, l->seq);
    if (!l->hwnd) {
        nfree(l);
        seterr(in, DMLERR_SYS_ERROR);
        return 0;
    }
    SetWindowWord(l->hwnd, 0, (WORD)in);
    SetWindowWord(l->hwnd, 2, 0);
    SetWindowWord(l->hwnd, 4, l->seq);
    SetWindowWord(l->hwnd, 6, 0);
    l->next = in->lists;
    in->lists = l;
    c = client_start(in, svc, topic, pcc, CM_LIST, l->hwnd);
    if (c && !c->partner)
        conv_free(c);
    for (h = GetWindow(l->hwnd, GW_CHILD); h; h = nx) {
        nx = GetWindow(h, GW_HWNDNEXT);
        c = conv_of_window(h);
        if (!c)
            continue;
        c->status |= ST_INLIST;
        if (ol && list_has(ol, c))
            conv_terminate(c, FALSE);
    }
    if (ol) {
        for (h = GetWindow(ol->hwnd, GW_CHILD); h; h = nx) {
            nx = GetWindow(h, GW_HWNDNEXT);
            SetParent(h, l->hwnd);
        }
        list_drop(ol);
    }
    if (!GetWindow(l->hwnd, GW_CHILD)) {
        /* nobody answered: the window is left behind (it goes with
           the instance) */
        CLIST NEAR * NEAR *pp;
        for (pp = &in->lists; *pp; pp = &(*pp)->next)
            if (*pp == l) {
                *pp = l->next;
                break;
            }
        nfree(l);
        return 0;
    }
    return list_handle(l);
}

HCONV DMLAPI DdeQueryNextServer(HCONVLIST hl, HCONV prev)
{
    CLIST NEAR *l = list_find(hl);
    HWND h;
    BOOL after = prev == 0;
    if (!l) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return 0;
    }
    for (h = GetWindow(l->hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        CONV NEAR *c = conv_of_window(h);
        if (!c || c->dead)
            continue;
        if (after)
            return conv_handle(c);
        if (conv_handle(c) == prev)
            after = TRUE;
    }
    return 0;
}

/* every conversation of the list is terminated (in its windows' order);
   the window stays, its fourth word 2000, until a call finds it empty
   (xconn2.dm) */
BOOL DMLAPI DdeDisconnectList(HCONVLIST hl)
{
    CLIST NEAR *l = list_find(hl);
    HWND h, nx;
    if (!l) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    if (!GetWindow(l->hwnd, GW_CHILD)) {
        list_drop(l);
        return TRUE;
    }
    for (h = GetWindow(l->hwnd, GW_CHILD); h; h = nx) {
        CONV NEAR *c = conv_of_window(h);
        nx = GetWindow(h, GW_HWNDNEXT);
        if (c && !c->dead)
            conv_terminate(c, FALSE);
    }
    l->ended = TRUE;
    SetWindowWord(l->hwnd, 6, 0x2000);
    return TRUE;
}

/* the answer to a partner's TERMINATE, posted before XTYP_DISCONNECT;
   while the partner's queue is full (8 messages) 3.1 yields and tries
   again (tests/ddeml/scripts/xun4.dm: the ninth answer waits until the
   client has taken one) */
static void post_term(HWND to, HWND from)
{
    DWORD end = GetCurrentTime() + 5000;
    while (to && IsWindow(to) && !PostMessage(to, WM_DDE_TERMINATE, (WPARAM)from, 0)) {
        if ((long)(GetCurrentTime() - end) >= 0)
            break;
        Yield();
    }
}

/* DdeUninitialize: every conversation of the instance, in the order of
   its windows (the DMGClass's children, a holding window's children
   where it comes: xun2.dm, xun3.dm) */
static void term_children(HWND parent)
{
    HWND h, nx;
    for (h = GetWindow(parent, GW_CHILD); h; h = nx) {
        CONV NEAR *c = conv_of_window(h);
        nx = GetWindow(h, GW_HWNDNEXT);
        if (c) {
            if (!c->dead)
                conv_terminate(c, FALSE);
        } else
            term_children(h);
    }
}

void conv_terminate_all(INST NEAR *in)
{
    CONV NEAR *c;
    if (in->dmg && IsWindow(in->dmg))
        term_children(in->dmg);
    for (c = in->convs; c; c = c->next)
        if (!c->dead)
            conv_terminate(c, FALSE);
}

/* TERMINATE: posted, the conversation dead; its window goes when the
   partner's TERMINATE comes back */
void conv_terminate(CONV NEAR *c, BOOL wait)
{
    (void)wait;
    if (c->dead)
        return;
    c->dead = TRUE;
    c->status &= ~ST_CONNECTED;
    c->status |= ST_TERMINATED;
    c->convst = XST_NULL;
    mon_conv(c, FALSE);
    /* a full queue: 3.1 waits for room here too (xun4.dm: eleven
       conversations with one server all end) */
    post_term(c->partner, c->hwnd);
}

BOOL DMLAPI DdeDisconnect(HCONV hc)
{
    CONV NEAR *c = conv_find(hc);
    if (!c || c->dead) {
        seterr(c ? c->inst : inst_of_task(), DMLERR_NO_CONV_ESTABLISHED);
        return FALSE;
    }
    conv_terminate(c, FALSE);
    if (!c->partner || !IsWindow(c->partner))
        conv_free(c);
    return TRUE;
}

HCONV DMLAPI DdeReconnect(HCONV hc)
{
    CONV NEAR *c = conv_find(hc), NEAR *n;
    if (!c || !(c->status & ST_CLIENT)) {
        seterr(c ? c->inst : inst_of_task(), DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    n = client_start(c->inst, hsz(c->svcreq ? c->svcreq : c->svcpartner), hsz(c->topic), &c->cc,
                     CM_SINGLE, c->inst->dmg);
    if (!n || !n->partner) {
        if (n)
            conv_free(n);
        seterr(c->inst, DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    return conv_handle(n);
}

static void run_queue(CONV NEAR *c, BOOL one);
static BOOL blocked(CONV NEAR *c);

/* ---- transactions: the client side ---- */

static WORD mask_status(DWORD r)
{
    return (WORD)r & 0xC00F;            /* 8012 -> 8002, 4155 -> 4005 (observed) */
}

static WORD timeout_err(UINT type)
{
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_POKE:     return DMLERR_POKEACKTIMEOUT;
    case XTYP_EXECUTE:  return DMLERR_EXECACKTIMEOUT;
    case XTYP_ADVSTART: return DMLERR_ADVACKTIMEOUT;
    case XTYP_ADVSTOP:  return DMLERR_UNADVACKTIMEOUT;
    }
    return DMLERR_DATAACKTIMEOUT;
}

static void xact_drop(CONV NEAR *c, XACT NEAR *x)
{
    XACT NEAR * NEAR *pp;
    for (pp = &c->xacts; *pp; pp = &(*pp)->next)
        if (*pp == x) {
            *pp = x->next;
            break;
        }
    nfree(x);
}

static XACT NEAR *xact_find(DWORD id, CONV NEAR **pc)
{
    INST NEAR *in;
    CONV NEAR *c;
    XACT NEAR *x;
    for (in = ddeml_insts; in; in = in->next)
        for (c = in->convs; c; c = c->next)
            for (x = c->xacts; x; x = x->next)
                if ((WORD)x == LOWORD(id) && x->id == HIWORD(id) && x->async) {
                    if (pc)
                        *pc = c;
                    return x;
                }
    return NULL;
}

/* a link: a new one holds its item (MH_INCATOM) */
static LINK NEAR *link_set(CONV NEAR *c, ATOM item, WORD fmt, WORD opts)
{
    LINK NEAR *l;
    for (l = c->links; l; l = l->next)
        if (l->item == item && l->fmt == fmt) {
            l->opts = opts;
            return NULL;
        }
    l = (LINK NEAR *)nalloc(sizeof(LINK));
    if (!l)
        return NULL;
    l->item = hsz_inc(item);
    l->fmt = fmt;
    l->opts = opts;
    l->next = c->links;
    c->links = l;
    c->status |= ST_ADVISE;
    return l;
}

static BOOL link_drop(CONV NEAR *c, ATOM item, WORD fmt)
{
    LINK NEAR * NEAR *pp = &c->links;
    BOOL any = FALSE;
    while (*pp) {
        LINK NEAR *l = *pp;
        if (l->item == item && (!fmt || l->fmt == fmt)) {
            /* 3.1: the server tells the monitors, then lets the item
               go; the client the other way round (xmon.dm) */
            *pp = l->next;
            if (c->status & ST_CLIENT) {
                hsz_dec(l->item);
                mon_link(c, l, FALSE);
            } else {
                mon_link(c, l, FALSE);
                hsz_dec(l->item);
            }
            nfree(l);
            any = TRUE;
            if (fmt)
                break;
        } else
            pp = &l->next;
    }
    if (!c->links)
        c->status &= ~ST_ADVISE;
    return any;
}

/* a transaction is over: a synchronous one ends its modal loop, an
   asynchronous one gets XTYP_XACT_COMPLETE */
static void xact_done(CONV NEAR *c, XACT NEAR *x, HGLOBAL data)
{
    if (x->abandoned) {
        if (data)
            GlobalFree(data);
        xact_drop(c, x);
        return;
    }
    if (!x->async) {
        if (data) {
            DATA NEAR *d = data_new(c->inst, data, DF_USED, x->item, x->fmt);
            x->result = data_handle(d);
        }
        x->state = XS_DONE;
        return;
    }
    {
        HDDEDATA hd = data ? data_wrap(c->inst, data, TRUE, x->item)
                    : x->err ? 0
                    : data_handle(data_new(c->inst, NULL, DF_TEMP | DF_USED, x->item, x->fmt));
        DWORD id = MAKELONG((WORD)x, x->id);
        UINT type = x->type;
        WORD status = x->status;
        ATOM item = x->item;
        UINT fmt = x->fmt;
        HCONV hc = conv_handle(c);
        x->state = XS_DONE;
        callback(c->inst, XTYP_XACT_COMPLETE, fmt, c, hsz(c->topic),
                 hsz((type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) == XTYP_EXECUTE ? 0 : item), hd, id,
                 status);
        if (hd && hd != (HDDEDATA)1) {
            DATA NEAR *d = data_find(hd);
            if (d)
                data_free(d, TRUE);
        }
        c = conv_find(hc);
        if (c)
            xact_drop(c, x);
    }
}

/* an answer for the oldest transaction it fits */
static XACT NEAR *xact_match(CONV NEAR *c, UINT msg, ATOM item, HGLOBAL h, WORD fmt)
{
    XACT NEAR *x;
    for (x = c->xacts; x; x = x->next) {
        UINT t = x->type & ~(XTYPF_NODATA | XTYPF_ACKREQ);
        if (x->state != XS_WAIT)
            continue;
        if (msg == WM_DDE_DATA) {
            if (t == XTYP_REQUEST && x->item == item && x->fmt == fmt)
                return x;
        } else if (t == XTYP_EXECUTE) {
            if (x->sent == h)
                return x;
        } else if (x->item == item)
            return x;
    }
    return NULL;
}

static void client_ack(CONV NEAR *c, WORD status, WORD hi)
{
    XACT NEAR *x = xact_match(c, WM_DDE_ACK, hi, (HGLOBAL)hi, 0);
    UINT t;
    if (!x) {
        /* an advise DATA's acknowledgment we did not ask for, or late */
        return;
    }
    t = x->type & ~(XTYPF_NODATA | XTYPF_ACKREQ);
    x->status = status;
    if (status & DDE_FACK)
        x->err = 0;
    else
        x->err = (status & DDE_FBUSY) ? DMLERR_BUSY : DMLERR_NOTPROCESSED;
    if (t == XTYP_EXECUTE) {
        if (x->sent)
            GlobalFree(x->sent);
    } else {
        if (!(status & DDE_FACK) && x->sent)
            GlobalFree(x->sent);        /* a refused POKE or ADVISE */
        GlobalDeleteAtom(hi);
    }
    x->sent = NULL;
    if (status & DDE_FACK) {
        if (t == XTYP_ADVSTART) {
            LINK NEAR *l = link_set(c, x->item, x->fmt,
                                    0x2000 | ((x->type & XTYPF_NODATA) ? 0x4000 : 0) |
                                    ((x->type & XTYPF_ACKREQ) ? 0x8000 : 0));
            if (l)
                mon_link(c, l, TRUE);
        }
        else if (t == XTYP_ADVSTOP)
            link_drop(c, x->item, x->fmt);
    }
    xact_done(c, x, NULL);
}

static BOOL blocked(CONV NEAR *c)
{
    return (c->status & ST_BLOCKED) || c->inst->blockall;
}

static void queue_msg(CONV NEAR *c, UINT msg, WPARAM wp, LPARAM lp, BOOL front)
{
    QMSG NEAR *q = (QMSG NEAR *)nalloc(sizeof(QMSG)), NEAR * NEAR *pp;
    if (!q)
        return;
    q->msg = msg;
    q->wp = wp;
    q->lp = lp;
    if (front) {
        q->next = c->queue;
        c->queue = q;
    } else {
        for (pp = &c->queue; *pp; pp = &(*pp)->next)
            ;
        *pp = q;
    }
}

/* a DATA message to a client: a request's answer or advise data */
static BOOL client_data(CONV NEAR *c, WPARAM wp, LPARAM lp)
{
    HGLOBAL h = (HGLOBAL)LOWORD(lp);
    ATOM item = HIWORD(lp);
    WORD flags = 0, fmt = 0;
    WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
    (void)wp;
    if (p) {
        flags = p[0];
        fmt = p[1];
        GlobalUnlock(h);
    }
    if (flags & 0x1000) {               /* fResponse: a REQUEST's answer */
        XACT NEAR *x = xact_match(c, WM_DDE_DATA, item, h, fmt);
        if (flags & 0x8000)
            PostMessage(c->partner, WM_DDE_ACK, (WPARAM)c->hwnd,
                        MAKELPARAM(DDE_FACK, hsz_inc_quiet(item)));
        GlobalDeleteAtom(item);
        if (!x) {
            if (flags & 0x2000)
                GlobalFree(h);
            return TRUE;
        }
        x->status = 0;                  /* XACT_COMPLETE's dwData2 (3.1) */
        x->err = 0;
        xact_done(c, x, h);
        return TRUE;
    }
    /* advise data */
    {
        LINK NEAR *l;
        HDDEDATA hd;
        DWORD r;
        HCONV hc = conv_handle(c);
        if (blocked(c))
            return FALSE;
        for (l = c->links; l; l = l->next)
            if (l->item == item && (!h || l->fmt == fmt))
                break;
        if (!h && l)
            fmt = l->fmt;
        hd = h ? data_wrap(c->inst, h, TRUE, item) : 0;
        r = (DWORD)callback(c->inst, XTYP_ADVDATA, fmt, c, hsz(c->topic), hsz(item), hd, 0, 0);
        c = conv_find(hc);
        if (r == (DWORD)CBR_BLOCK && c) {
            if (hd) {
                DATA NEAR *d = data_find(hd);
                if (d)
                    data_free(d, FALSE);
            }
            c->status |= ST_BLOCKED;
            return FALSE;
        }
        if (hd) {
            DATA NEAR *d = data_find(hd);
            if (d)
                data_free(d, FALSE);
        }
        if (c && (!h || (flags & 0x8000)))
            PostMessage(c->partner, WM_DDE_ACK, (WPARAM)c->hwnd, MAKELPARAM(mask_status(r), item));
        else
            GlobalDeleteAtom(item);
        if (h && (flags & 0x2000))
            GlobalFree(h);
        return TRUE;
    }
}

static void client_term(CONV NEAR *c)
{
    HWND partner = c->partner;
    HWND me = c->hwnd;
    if (!c->dead) {
        /* the server ended it: DISCONNECT, the conversation goes (its
           names: MH_DECATOM), then the answer */
        c->dead = TRUE;
        c->status &= ~ST_CONNECTED;
        c->status |= ST_TERMINATED;
        mon_conv(c, FALSE);
        post_term(partner, me);
        c = conv_of_window(me);
        if (c)
            callback(c->inst, XTYP_DISCONNECT, 0, c, 0, 0, 0, 0, 0);
        c = conv_of_window(me);
        if (c)
            conv_free(c);
        return;
    }
    conv_free(c);
}

LRESULT FAR PASCAL __loadds ClientWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    CONV NEAR *c = conv_of_window(w);
    if (!c)
        return DefWindowProc(w, msg, wp, lp);
    switch (msg) {
    case WM_DDE_ACK:
        /* an answer to its INITIATE (sent; directly when the server
           is in this task) */
        if (c->initiating) {
            if (!c->partner) {
                client_connected(c, (HWND)wp, LOWORD(lp), HIWORD(lp));
                return 0;
            }
            if (c->mode == CM_LIST) {
                list_answer(c, (HWND)wp, LOWORD(lp), HIWORD(lp));
                return 0;
            }
            /* a second server for one DdeConnect */
            SendMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0);
            GlobalDeleteAtom(LOWORD(lp));
            GlobalDeleteAtom(HIWORD(lp));
            return 0;
        }
        if (!InSendMessage())
            client_ack(c, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_DDE_DATA:
        if (c->dead) {
            HGLOBAL h = (HGLOBAL)LOWORD(lp);
            if (h)
                GlobalFree(h);
            GlobalDeleteAtom(HIWORD(lp));
            return 0;
        }
        {
            HGLOBAL h = (HGLOBAL)LOWORD(lp);
            WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
            BOOL response = p && (p[0] & 0x1000);
            if (p)
                GlobalUnlock(h);
            if (response) {
                if (!client_data(c, wp, lp))
                    queue_msg(c, msg, wp, lp, FALSE);
                return 0;
            }
            /* advise data: held, and given to the callback once the
               DATA messages already posted have been taken (as 3.1) */
            queue_msg(c, msg, wp, lp, FALSE);
            if (!c->runposted && PostMessage(w, WM_DMG_RUN, 0, 0))
                c->runposted = TRUE;
        }
        return 0;
    case WM_DMG_RUN:
        c->runposted = FALSE;
        if (!blocked(c))
            run_queue(c, FALSE);
        return 0;
    case WM_DDE_TERMINATE:
        if ((HWND)wp == c->partner)
            client_term(c);
        return 0;
    case WM_TIMER: {
        XACT NEAR *x;
        KillTimer(w, wp);
        for (x = c->xacts; x; x = x->next)
            if ((WORD)x == (WORD)wp && x->state == XS_WAIT && !x->async) {
                x->err = timeout_err(x->type);
                x->abandoned = TRUE;
                x->state = XS_DONE;
            }
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

static BOOL task_in_sync(void)
{
    INST NEAR *in;
    HTASK t = GetCurrentTask();
    for (in = ddeml_insts; in; in = in->next)
        if (in->task == t && (in->flags & 0x8000))
            return TRUE;
    return FALSE;
}

static void set_sync(BOOL on)
{
    INST NEAR *in;
    HTASK t = GetCurrentTask();
    for (in = ddeml_insts; in; in = in->next)
        if (in->task == t) {
            if (on)
                in->flags |= 0x8000;
            else
                in->flags &= ~0x8000;
        }
}

/* the CBF_FAIL_ flags of a server in this library, seen from its client */
static BOOL local_refuses(CONV NEAR *c, UINT t)
{
    CONV NEAR *s;
    DWORD f;
    if (!(c->status & ST_ISLOCAL) || !(s = conv_of_window(c->partner)))
        return FALSE;
    f = s->inst->afCmd;
    switch (t) {
    case XTYP_REQUEST:  return (f & CBF_FAIL_REQUESTS) != 0;
    case XTYP_POKE:     return (f & CBF_FAIL_POKES) != 0;
    case XTYP_EXECUTE:  return (f & CBF_FAIL_EXECUTES) != 0;
    case XTYP_ADVSTART:
    case XTYP_ADVSTOP:  return (f & CBF_FAIL_ADVISES) != 0;
    }
    return FALSE;
}

HDDEDATA DMLAPI DdeClientTransaction(LPVOID pData, DWORD cb, HCONV hc, HSZ item, UINT fmt, UINT type,
                                  DWORD timeout, DWORD FAR *pres)
{
    CONV NEAR *c = conv_find(hc);
    INST NEAR *in;
    UINT t = type & ~(XTYPF_NODATA | XTYPF_ACKREQ);
    XACT NEAR *x;
    BOOL async = timeout == TIMEOUT_ASYNC, ok;
    ATOM a = LOWORD(item);
    UINT msg;
    LPARAM lp;
    HGLOBAL h = NULL;
    if (!c) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return 0;
    }
    in = c->inst;
    if (in->flags & IF_MONITOR) {
        seterr(in, DMLERR_DLL_USAGE);
        return 0;
    }
    if (c->dead || !(c->status & ST_CONNECTED)) {
        seterr(in, DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    if ((t != XTYP_REQUEST && t != XTYP_POKE && t != XTYP_EXECUTE && t != XTYP_ADVSTART &&
         t != XTYP_ADVSTOP) || (t != XTYP_EXECUTE && !a)) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    if (!async && task_in_sync()) {
        seterr(in, DMLERR_REENTRANCY);
        return 0;
    }
    if (local_refuses(c, t))
        return 0;
    switch (t) {
    case XTYP_REQUEST:
        /* 3.1 reports this one reference to monitors (MH_INCATOM), not
           those of POKE, ADVISE and UNADVISE (xmon.dm) */
        msg = WM_DDE_REQUEST;
        lp = MAKELPARAM(fmt, hsz_inc(a));
        break;
    case XTYP_ADVSTOP:
        msg = WM_DDE_UNADVISE;
        lp = MAKELPARAM(fmt, hsz_inc_quiet(a));
        break;
    case XTYP_ADVSTART: {
        WORD FAR *o;
        h = GlobalAlloc(GMEM_FIXED | GMEM_DDESHARE, 4);
        if (!h) {
            seterr(in, DMLERR_MEMORY_ERROR);
            return 0;
        }
        o = (WORD FAR *)GlobalLock(h);
        o[0] = 0x2000 | ((type & XTYPF_NODATA) ? 0x4000 : 0) | ((type & XTYPF_ACKREQ) ? 0x8000 : 0);
        o[1] = fmt;
        GlobalUnlock(h);
        msg = WM_DDE_ADVISE;
        lp = MAKELPARAM(h, hsz_inc_quiet(a));
        break;
    }
    default: {
        DATA NEAR *d = cb == (DWORD)-1 ? data_find((HDDEDATA)pData) : (DATA NEAR *)0;
        if (cb == (DWORD)-1 && !d) {
            seterr(in, DMLERR_INVALIDPARAMETER);
            return 0;
        }
        if (d)
            h = data_take(d, t == XTYP_POKE ? 0x2000 : 0);
        else {
            /* no data at all still makes a block (3.1: 32 bytes, not cleared) */
            h = data_block(in, pData, cb ? cb : 1, a != 0, fmt);
            if (h && a) {
                WORD FAR *p = (WORD FAR *)GlobalLock(h);
                p[0] = 0x2000;
                GlobalUnlock(h);
            }
        }
        if (!h) {
            seterr(in, DMLERR_MEMORY_ERROR);
            return 0;
        }
        if (t == XTYP_POKE) {
            msg = WM_DDE_POKE;
            lp = MAKELPARAM(h, hsz_inc_quiet(a));
        } else {
            msg = WM_DDE_EXECUTE;
            lp = MAKELPARAM(0, h);
        }
    }
    }
    x = (XACT NEAR *)nalloc(sizeof(XACT));
    if (!x) {
        seterr(in, DMLERR_MEMORY_ERROR);
        return 0;
    }
    x->conv = c;
    x->type = type;
    x->fmt = fmt;
    x->item = a;
    x->async = async;
    x->sent = h;
    x->id = async ? ++c->xcount : 0;
    {
        XACT NEAR * NEAR *pp;
        for (pp = &c->xacts; *pp; pp = &(*pp)->next)
            ;
        *pp = x;
    }
    c->ltype = type;
    c->lfmt = fmt;
    c->litem = a;
    if (!PostMessage(c->partner, msg, (WPARAM)c->hwnd, lp)) {
        xact_drop(c, x);
        seterr(in, DMLERR_POSTMSG_FAILED);
        return 0;
    }
    if (async) {
        if (pres)
            *pres = MAKELONG((WORD)x, x->id);
        return (HDDEDATA)1;
    }
    {
        HCONV keep = hc;
        BOOL done = FALSE;
        HDDEDATA r;
        WORD err, status;
        set_sync(TRUE);
        SetTimer(c->hwnd, (WORD)x, timeout > 0xFFFFL ? 0xFFFF : (WORD)timeout, NULL);
        while (!done) {
            MSG m;
            c = conv_find(keep);
            if (!c || x->state == XS_DONE)
                break;
            if (!GetMessage(&m, NULL, 0, 0)) {
                PostQuitMessage((int)m.wParam);
                break;
            }
            if (!CallMsgFilter(&m, MSGF_DDEMGR)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
        set_sync(FALSE);
        c = conv_find(keep);
        if (!c) {
            seterr(in, DMLERR_SERVER_DIED);
            return 0;
        }
        KillTimer(c->hwnd, (WORD)x);
        err = x->err;
        status = x->status;
        r = x->result;
        ok = x->state == XS_DONE && !err;
        if (x->abandoned) {
            /* timed out: the record stays to swallow a late answer */
            x->state = XS_WAIT;
        } else
            xact_drop(c, x);
        if (err == DMLERR_NOTPROCESSED || err == DMLERR_BUSY)
            seterr_quiet(in, err);      /* 3.1: no MF_ERRORS event (monitor.dm) */
        else if (err)
            seterr(in, err);
        if (pres && !(err && x->abandoned) && !(t == XTYP_REQUEST && ok))
            *(WORD FAR *)pres = status;
        if (!ok)
            return 0;
        return t == XTYP_REQUEST ? r : (HDDEDATA)1;
    }
}

BOOL DMLAPI DdeAbandonTransaction(DWORD id, HCONV hc, DWORD xid)
{
    INST NEAR *in = inst_arg(id);
    CONV NEAR *c;
    XACT NEAR *x;
    if (!in)
        return FALSE;
    if (xid) {
        x = xact_find(xid, &c);
        if (!x || (hc && conv_handle(c) != hc)) {
            seterr(in, DMLERR_UNFOUND_QUEUE_ID);
            return FALSE;
        }
        x->abandoned = TRUE;
        return TRUE;
    }
    for (c = in->convs; c; c = c->next)
        if (!hc || conv_handle(c) == hc)
            for (x = c->xacts; x; x = x->next)
                if (x->async)
                    x->abandoned = TRUE;
    return TRUE;
}

/* ---- the server side ---- */

static void server_reply(CONV NEAR *c, UINT msg, LPARAM lp)
{
    if (c->partner && IsWindow(c->partner))
        PostMessage(c->partner, msg, (WPARAM)c->hwnd, lp);
}

static HGLOBAL reply_block(HDDEDATA r, WORD flags)
{
    DATA NEAR *d = data_find(r);
    if (!d)
        return NULL;
    return data_take(d, flags);
}

/* one client message: FALSE if the callback blocked it */
static BOOL server_msg(CONV NEAR *c, UINT msg, WPARAM wp, LPARAM lp)
{
    INST NEAR *in = c->inst;
    DWORD f = in->afCmd;
    ATOM item = HIWORD(lp);
    HCONV hc = conv_handle(c);
    HDDEDATA r;
    (void)wp;
    switch (msg) {
    case WM_DDE_REQUEST:
        if (f & CBF_FAIL_REQUESTS) {
            server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
            return TRUE;
        }
        r = callback(in, XTYP_REQUEST, LOWORD(lp), c, hsz(c->topic), hsz(item), 0, 0, 0);
        if (r == CBR_BLOCK)
            return FALSE;
        if (!(c = conv_find(hc)))
            return TRUE;
        if (r) {
            HGLOBAL h = reply_block(r, 0x3000);
            if (h) {
                server_reply(c, WM_DDE_DATA, MAKELPARAM(h, item));
                return TRUE;
            }
        }
        server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
        return TRUE;
    case WM_DDE_POKE:
    case WM_DDE_EXECUTE: {
        BOOL poke = msg == WM_DDE_POKE;
        HGLOBAL h = poke ? (HGLOBAL)LOWORD(lp) : (HGLOBAL)HIWORD(lp);
        HDDEDATA hd;
        WORD st, flags = 0;
        if (f & (poke ? CBF_FAIL_POKES : CBF_FAIL_EXECUTES)) {
            server_reply(c, WM_DDE_ACK, poke ? MAKELPARAM(0, item) : MAKELPARAM(0, h));
            return TRUE;
        }
        if (poke && h) {
            WORD FAR *p = (WORD FAR *)GlobalLock(h);
            flags = p ? p[0] : 0;
            if (p)
                GlobalUnlock(h);
        }
        hd = data_wrap(in, h, poke, poke ? item : 0);
        r = callback(in, poke ? XTYP_POKE : XTYP_EXECUTE, poke ? ((WORD FAR *)GlobalLock(h))[1] : 0,
                     c, hsz(c->topic), poke ? hsz(item) : 0, hd, 0, 0);
        if (poke)
            GlobalUnlock(h);
        if (hd) {
            DATA NEAR *d = data_find(hd);
            if (d)
                data_free(d, FALSE);
        }
        if (r == CBR_BLOCK)
            return FALSE;
        if (!(c = conv_find(hc)))
            return TRUE;
        st = mask_status((DWORD)r);
        if (poke && (st & DDE_FACK) && (flags & 0x2000))
            GlobalFree(h);
        server_reply(c, WM_DDE_ACK, poke ? MAKELPARAM(st, item) : MAKELPARAM(st, h));
        return TRUE;
    }
    case WM_DDE_ADVISE: {
        HGLOBAL h = (HGLOBAL)LOWORD(lp);
        WORD opts = 0, fmt = 0;
        WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
        if (p) {
            opts = p[0];
            fmt = p[1];
            GlobalUnlock(h);
        }
        if (f & CBF_FAIL_ADVISES) {
            server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
            return TRUE;
        }
        r = callback(in, XTYP_ADVSTART, fmt, c, hsz(c->topic), hsz(item), 0, 0, 0);
        if (r == CBR_BLOCK)
            return FALSE;
        if (!(c = conv_find(hc)))
            return TRUE;
        if (r) {
            LINK NEAR *l = link_set(c, item, fmt, opts);
            if (l)
                mon_link(c, l, TRUE);
            if (h)
                GlobalFree(h);
            server_reply(c, WM_DDE_ACK, MAKELPARAM(DDE_FACK, item));
        } else
            server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
        return TRUE;
    }
    case WM_DDE_UNADVISE: {
        WORD fmt = LOWORD(lp);
        LINK NEAR *l;
        if (f & CBF_FAIL_ADVISES) {
            server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
            return TRUE;
        }
        for (l = c->links; l; l = l->next)
            if (l->item == item && (!fmt || l->fmt == fmt))
                break;
        if (!l) {
            server_reply(c, WM_DDE_ACK, MAKELPARAM(0, item));
            return TRUE;
        }
        {
            /* 3.1 drops the link (the monitors see it go) before
               XTYP_ADVSTOP (xmon.dm) */
            WORD lfmt = l->fmt, lopts = l->opts;
            link_drop(c, item, fmt);
            r = callback(in, XTYP_ADVSTOP, lfmt, c, hsz(c->topic), hsz(item), 0, 0,
                         (DWORD)hsz(item));
            if (r == CBR_BLOCK) {
                /* held: the link is back for when it runs again */
                if ((c = conv_find(hc)) != NULL) {
                    LINK NEAR *k = (LINK NEAR *)nalloc(sizeof(LINK));
                    if (k) {
                        k->item = hsz_inc_quiet(item);
                        k->fmt = lfmt;
                        k->opts = lopts;
                        k->next = c->links;
                        c->links = k;
                        c->status |= ST_ADVISE;
                    }
                }
                return FALSE;
            }
        }
        if (!(c = conv_find(hc)))
            return TRUE;
        server_reply(c, WM_DDE_ACK, MAKELPARAM(DDE_FACK, item));
        return TRUE;
    }
    }
    return TRUE;
}

/* advise data for one link */
static void advise_send(CONV NEAR *c, LINK NEAR *l, WORD count)
{
    HGLOBAL h = NULL;
    HCONV hc = conv_handle(c);
    if (!(l->opts & 0x4000)) {
        HDDEDATA r = callback(c->inst, XTYP_ADVREQ, l->fmt, c, hsz(c->topic), hsz(l->item), 0, count, 0);
        if (!(c = conv_find(hc)) || !r || r == CBR_BLOCK)
            return;
        h = reply_block(r, 0x2000 | (l->opts & 0x8000));
        if (!h)
            return;
    }
    if (l->opts & 0x8000)
        l->pending = TRUE;
    server_reply(c, WM_DDE_DATA, MAKELPARAM(h, hsz_inc(l->item)));
}

BOOL DMLAPI DdePostAdvise(DWORD id, HSZ topic, HSZ item)
{
    INST NEAR *in = inst_arg(id);
    CONV NEAR *c, NEAR *s, NEAR *order[1];
    (void)order;
    if (!in)
        return FALSE;
    if (in->flags & IF_CLIENTONLY) {
        seterr(in, DMLERR_DLL_USAGE);
        return FALSE;
    }
    /* the server conversations newest first, each one's links newest
       first; ADVREQ's count is how many links with the same item and
       format are still to come */
    {
        WORD n = 0, i;
        CONV NEAR * NEAR *all;
        HLOCAL big;
        for (c = in->convs; c; c = c->next)
            n++;
        big = LocalAlloc(LMEM_FIXED, (n ? n : 1) * sizeof(CONV NEAR *));
        if (!big)
            return FALSE;
        all = (CONV NEAR * NEAR *)big;
        i = 0;
        for (c = in->convs; c; c = c->next)
            all[i++] = c;
        for (i = n; i-- > 0;) {
            LINK NEAR *l;
            s = all[i];
            if ((s->status & ST_CLIENT) || s->dead)
                continue;
            if (LOWORD(topic) && s->topic != LOWORD(topic))
                continue;
            for (l = s->links; l; l = l->next) {
                WORD count = 0, j;
                LINK NEAR *m;
                if (LOWORD(item) && l->item != LOWORD(item))
                    continue;
                if (l->pending) {
                    l->late = TRUE;
                    continue;
                }
                for (m = l->next; m; m = m->next)
                    if (m->item == l->item && m->fmt == l->fmt)
                        count++;
                for (j = i; j-- > 0;) {
                    CONV NEAR *o = all[j];
                    if ((o->status & ST_CLIENT) || o->dead || o->topic != s->topic)
                        continue;
                    for (m = o->links; m; m = m->next)
                        if (m->item == l->item && m->fmt == l->fmt)
                            count++;
                }
                advise_send(s, l, count);
            }
        }
        LocalFree(big);
    }
    return TRUE;
}

static void server_ack(CONV NEAR *c, WORD status, ATOM item)
{
    LINK NEAR *l;
    (void)status;
    for (l = c->links; l; l = l->next)
        if (l->item == item && l->pending)
            break;
    GlobalDeleteAtom(item);
    if (!l)
        return;
    l->pending = FALSE;
    if (l->late) {
        l->late = FALSE;
        advise_send(c, l, CADV_LATEACK);
    }
}

static void server_term(CONV NEAR *c)
{
    HWND partner = c->partner, me = c->hwnd;
    if (!c->dead) {
        c->dead = TRUE;
        c->status &= ~ST_CONNECTED;
        c->status |= ST_TERMINATED;
        mon_conv(c, FALSE);
        post_term(partner, me);
        c = conv_of_window(me);
        if (c)
            callback(c->inst, XTYP_DISCONNECT, 0, c, 0, 0, 0, 0, 0);
        c = conv_of_window(me);
        if (c)
            conv_free(c);
        return;
    }
    /* the answer to its own TERMINATE: 3.1 queues a message of its own
       here (not a DDE message: no spy sees it), one place in the task's
       queue of 8 (tests/ddeml/scripts/xun2.dm, conn.dm's DdeUninitialize
       with conversations to itself); w16 posts a message that does
       nothing */
    if (c->inst->dmg && IsWindow(c->inst->dmg))
        PostMessage(c->inst->dmg, WM_DMG_NOP, 0, 0);
    conv_free(c);
}

/* process what was held for a conversation */
static void run_queue(CONV NEAR *c, BOOL one)
{
    HCONV hc = conv_handle(c);
    while ((c = conv_find(hc)) != NULL && c->queue && !(c->status & ST_BLOCKED) &&
           !c->inst->blockall) {
        QMSG NEAR *q = c->queue;
        BOOL ok;
        c->queue = q->next;
        if (c->status & ST_CLIENT)
            ok = client_data(c, q->wp, q->lp);
        else
            ok = server_msg(c, q->msg, q->wp, q->lp);
        c = conv_find(hc);
        if (!ok && c) {
            q->next = c->queue;
            c->queue = q;
            c->status |= ST_BLOCKED;
            break;
        }
        nfree(q);
        if (one)
            break;
    }
}

void conv_unblock(CONV NEAR *c, UINT cmd)
{
    if (cmd == EC_DISABLE) {
        c->status |= ST_BLOCKED;
        return;
    }
    {
        /* EC_ENABLEONE: one transaction, then the conversation is
           disabled again (3.1: a new one after it is held) */
        HCONV hc = conv_handle(c);
        BOOL had = c->queue != NULL;
        c->status &= ~(ST_BLOCKED | ST_BLOCKNEXT);
        run_queue(c, cmd == EC_ENABLEONE);
        c = conv_find(hc);
        if (c && cmd == EC_ENABLEONE)
            c->status |= had ? ST_BLOCKED : ST_BLOCKNEXT;
    }
}

BOOL conv_waiting(CONV NEAR *c)
{
    return c->queue != NULL;
}

LRESULT FAR PASCAL __loadds ServerWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    CONV NEAR *c = conv_of_window(w);
    if (!c)
        return DefWindowProc(w, msg, wp, lp);
    switch (msg) {
    case WM_DDE_REQUEST:
    case WM_DDE_POKE:
    case WM_DDE_EXECUTE:
    case WM_DDE_ADVISE:
    case WM_DDE_UNADVISE:
        if (c->dead)
            return 0;
        if (blocked(c) || c->queue) {
            queue_msg(c, msg, wp, lp, FALSE);
            return 0;
        }
        if (!server_msg(c, msg, wp, lp)) {
            c = conv_of_window(w);
            if (c) {
                queue_msg(c, msg, wp, lp, TRUE);
                c->status |= ST_BLOCKED;
            }
        } else if ((c = conv_of_window(w)) != NULL && (c->status & ST_BLOCKNEXT)) {
            c->status &= ~ST_BLOCKNEXT;
            c->status |= ST_BLOCKED;
        }
        return 0;
    case WM_DDE_ACK:
        server_ack(c, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_TIMER:
        /* a synchronous transaction a server made on its own
           conversation (3.1 posts it, and it times out) */
        return ClientWndProc(w, msg, wp, lp);
    case WM_DDE_TERMINATE:
        if ((HWND)wp == c->partner)
            server_term(c);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

LRESULT FAR PASCAL __loadds ConvListWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- queries ---- */

BOOL DMLAPI DdeEnableCallback(DWORD id, HCONV hc, UINT cmd)
{
    INST NEAR *in = inst_arg(id);
    CONV NEAR *c;
    if (!in)
        return FALSE;
    if (hc) {
        c = conv_find(hc);
        if (!c || c->inst != in) {
            seterr(in, DMLERR_INVALIDPARAMETER);
            return FALSE;
        }
        if (cmd == EC_QUERYWAITING)
            return conv_waiting(c);
        conv_unblock(c, cmd);
        return TRUE;
    }
    if (cmd == EC_QUERYWAITING) {
        for (c = in->convs; c; c = c->next)
            if (conv_waiting(c))
                return TRUE;
        return FALSE;
    }
    in->blockall = cmd == EC_DISABLE;
    for (c = in->convs; c; c = c->next) {
        HCONV h = conv_handle(c);
        CONV NEAR *n = c->next;
        conv_unblock(c, cmd);
        if (!conv_find(h))
            c = NULL;
        if (!c) {
            c = n;
            if (!c)
                break;
        }
    }
    return TRUE;
}

UINT DMLAPI DdeQueryConvInfo(HCONV hc, DWORD xid, CONVINFO FAR *pci)
{
    CONV NEAR *c = conv_find(hc);
    CONVINFO ci;
    XACT NEAR *x = NULL;
    UINT n;
    if (!c || !pci) {
        seterr(c ? c->inst : inst_of_task(), DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    if (xid != QID_SYNC) {
        CONV NEAR *xc;
        x = xact_find(xid, &xc);
        if (!x || xc != c) {
            seterr(c->inst, DMLERR_UNFOUND_QUEUE_ID);
            return 0;
        }
    }
    bzero(&ci, sizeof ci);
    ci.cb = sizeof ci;
    ci.hUser = x ? x->user : c->user;
    if ((c->status & ST_ISLOCAL) && conv_of_window(c->partner))
        ci.hConvPartner = conv_handle(conv_of_window(c->partner));
    ci.hszSvcPartner = hsz(c->svcpartner);
    ci.hszServiceReq = hsz(c->svcreq);
    ci.hszTopic = hsz(c->topic);
    ci.hszItem = hsz(x ? x->item : c->litem);
    ci.wFmt = x ? x->fmt : c->lfmt;
    ci.wType = x ? x->type : c->ltype;
    ci.wStatus = c->status;
    ci.wConvst = c->convst;
    if (x && x->state == XS_WAIT) {   /* the transaction's own state (3.1) */
        UINT t = x->type & ~(XTYPF_NODATA | XTYPF_ACKREQ);
        ci.wConvst = t == XTYP_REQUEST ? XST_REQSENT : t == XTYP_POKE ? XST_POKESENT
                   : t == XTYP_EXECUTE ? XST_EXECSENT : t == XTYP_ADVSTART ? XST_ADVSENT
                   : XST_UNADVSENT;
    }
    ci.wLastError = c->lasterr;
    {
        CLIST NEAR *l = list_of(c);
        ci.hConvList = l ? list_handle(l) : 0;
    }
    bcopy(&ci.ConvCtxt, &c->cc, sizeof(CONVCONTEXT));
    n = (UINT)(pci->cb < sizeof ci ? pci->cb : sizeof ci);
    bcopy(pci, &ci, n);
    return n;
}

BOOL DMLAPI DdeSetUserHandle(HCONV hc, DWORD xid, DWORD user)
{
    CONV NEAR *c = conv_find(hc);
    if (!c) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    if (xid == QID_SYNC) {
        c->user = user;
        return TRUE;
    }
    {
        CONV NEAR *xc;
        XACT NEAR *x = xact_find(xid, &xc);
        if (!x || xc != c) {
            seterr(c->inst, DMLERR_UNFOUND_QUEUE_ID);
            return FALSE;
        }
        x->user = user;
    }
    return TRUE;
}
