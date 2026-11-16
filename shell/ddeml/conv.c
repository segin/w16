/*
 * conv.c - DDEML conversations: connecting (DdeConnect, DdeConnectList,
 * DdeReconnect, the server side of WM_DDE_INITIATE), transactions
 * (DdeClientTransaction, DdeAbandonTransaction, DdePostAdvise), the
 * client and server windows, blocking (DdeEnableCallback) and the
 * queries (ARCHITECTURE.md, chapter "DDEML").
 *
 * A conversation is a child window of the instance's DMGClass (client,
 * class DMGClientClass) or of its DMGHoldingClass (server,
 * DMGServerClass); HCONV is MAKELONG(window, sequence number), as 3.1's.
 * Every message on the wire is plain DDE, so a DDEML application talks
 * to raw-DDE applications (Program Manager) and to other DDEML builds.
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

static CONV NEAR *conv_of_window(HWND h)
{
    INST NEAR *in;
    CONV NEAR *c;
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
        nfree(l);
    }
    while (c->queue) {
        QMSG NEAR *q = c->queue;
        c->queue = q->next;
        nfree(q);
    }
    if (c->svcpartner)
        GlobalDeleteAtom(c->svcpartner);
    if (c->topic)
        GlobalDeleteAtom(c->topic);
    if (c->svcreq)
        GlobalDeleteAtom(c->svcreq);
    if (c->hwnd && IsWindow(c->hwnd))
        DestroyWindow(c->hwnd);
    nfree(c);
}

static ATOM addref(ATOM a)
{
    char b[256];
    if (!a || !GlobalGetAtomName(a, b, sizeof b))
        return 0;
    return GlobalAddAtom(b);
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
    HCONV hc;
    if (!c)
        return;
    c->partner = client;
    c->svcreq = addref(app);
    c->topic = addref(topic);
    if (pcc)
        bcopy(&c->cc, pcc, sizeof(CONVCONTEXT));
    c->status = ST_CONNECTED;
    c->convst = XST_CONNECTED;
    hc = conv_handle(c);
    SendMessage(client, WM_DDE_ACK, (WPARAM)c->hwnd, MAKELPARAM(addref(app), addref(topic)));
    /* the client may have terminated it already (a second answer to
       a DdeConnect): the confirmation then names no conversation */
    c = conv_find(hc);
    if (c)
        mon_conv(c, TRUE);
    callback(in, XTYP_CONNECT_CONFIRM, 0, c, hsz(topic), hsz(app), 0, 0, same);
}

void conv_initiate(INST NEAR *in, HWND dmg, ATOM app, ATOM topic, HWND from)
{
    CONV NEAR *cc = local_window(from, CLS_CLIENT) ? conv_of_window(from) : (CONV NEAR *)0;
    BOOL same = cc && cc->inst == in;
    CONVCONTEXT FAR *pcc = cc ? &cc->cc : NULL;
    (void)dmg;
    if (in->flags & (IF_CLIENTONLY | IF_MONITOR | IF_DYING))
        return;
    if (in->afCmd & CBF_FAIL_CONNECTIONS)
        return;
    if (same && (in->afCmd & CBF_FAIL_SELFCONNECTIONS))
        return;
    if (cc && cc->mode == CM_SINGLE && cc->partner)
        return;                         /* that DdeConnect has its server */
    if (in->flags & IF_FILTER) {
        if (app ? !registered(in, app) : !in->names)
            return;
    }
    if (app && topic) {
        if (callback(in, XTYP_CONNECT, 0, NULL, hsz(topic), hsz(app), 0, (DWORD)pcc, same))
            server_accept(in, from, app, topic, same, pcc);
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
                }
                GlobalUnlock(d->h);
            }
            data_free(d, TRUE);
        }
    }
}

/* ---- connecting: the client side ---- */

static void client_connected(CONV NEAR *c, HWND server, ATOM app, ATOM topic)
{
    c->partner = server;
    c->svcpartner = app;
    c->topic = topic;
    c->status |= ST_CONNECTED;
    if (local_window(server, CLS_SERVER))
        c->status |= ST_ISLOCAL;
    c->convst = XST_CONNECTED;
    c->ltype = XTYP_CONNECT;
    c->lfmt = CF_TEXT;
    mon_conv(c, TRUE);
}

static CONV NEAR *client_start(INST NEAR *in, HSZ svc, HSZ topic, CONVCONTEXT FAR *pcc, WORD mode)
{
    CONV NEAR *c = conv_new(in, TRUE, in->dmg);
    if (!c)
        return NULL;
    c->mode = mode;
    c->svcreq = addref(LOWORD(svc));
    if (pcc)
        bcopy(&c->cc, pcc, sizeof(CONVCONTEXT));
    c->convst = XST_INIT1;
    c->initiating = TRUE;
    SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)c->hwnd, MAKELPARAM(LOWORD(svc), LOWORD(topic)));
    c->initiating = FALSE;
    return c;
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
    c = client_start(in, svc, topic, pcc, CM_SINGLE);
    if (!c || !c->partner) {
        if (c)
            conv_free(c);
        seterr(in, DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }
    return conv_handle(c);
}

/* the conversation lists: a hidden window each */
static CLIST NEAR *list_find(HCONVLIST h)
{
    INST NEAR *in;
    CLIST NEAR *l;
    for (in = ddeml_insts; in; in = in->next)
        for (l = in->lists; l; l = l->next)
            if ((HWND)LOWORD(h) == l->hwnd)
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

HCONVLIST DMLAPI DdeConnectList(DWORD id, HSZ svc, HSZ topic, HCONVLIST old, CONVCONTEXT FAR *pcc)
{
    INST NEAR *in = inst_arg(id);
    CLIST NEAR *l, NEAR *ol = old ? list_find(old) : (CLIST NEAR *)0;
    CONV NEAR *c, NEAR *n;
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
    l->hwnd = newwin(in, CLS_LIST, in->dmg, WS_CHILD, 0);
    l->next = in->lists;
    in->lists = l;
    c = client_start(in, svc, topic, pcc, CM_LIST);
    if (c && !c->partner) {
        conv_free(c);
        c = NULL;
    }
    /* the conversations this INITIATE made join the list; those that
       an old list already has (same service and topic) are dropped */
    for (c = in->convs; c; c = n) {
        n = c->next;
        if (c->mode != CM_LIST || c->list || !(c->status & ST_CLIENT))
            continue;
        if (ol) {
            CONV NEAR *o;
            for (o = in->convs; o; o = o->next)
                if (o->list == ol && !o->dead && o->svcpartner == c->svcpartner && o->topic == c->topic)
                    break;
            if (o) {
                c->list = l;
                conv_terminate(c, FALSE);
                continue;
            }
        }
        c->list = l;
        c->status |= ST_INLIST;
    }
    if (ol) {
        for (c = in->convs; c; c = c->next)
            if (c->list == ol)
                c->list = l;
        list_drop(ol);
    }
    return (HCONVLIST)MAKELONG(l->hwnd, 0);
}

HCONV DMLAPI DdeQueryNextServer(HCONVLIST hl, HCONV prev)
{
    CLIST NEAR *l = list_find(hl);
    CONV NEAR *c;
    BOOL after = prev == 0;
    if (!l) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return 0;
    }
    for (c = l->inst->convs; c; c = c->next) {
        if (c->list != l || c->dead || !(c->status & ST_INLIST))
            continue;
        if (after)
            return conv_handle(c);
        if (conv_handle(c) == prev)
            after = TRUE;
    }
    return 0;
}

BOOL DMLAPI DdeDisconnectList(HCONVLIST hl)
{
    CLIST NEAR *l = list_find(hl);
    CONV NEAR *c;
    if (!l) {
        seterr(inst_of_task(), DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    for (c = l->inst->convs; c; c = c->next)
        if (c->list == l && !c->dead)
            conv_terminate(c, FALSE);
    list_drop(l);
    return TRUE;
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
    if (c->partner && IsWindow(c->partner))
        PostMessage(c->partner, WM_DDE_TERMINATE, (WPARAM)c->hwnd, 0);
    mon_conv(c, FALSE);
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
                     CM_SINGLE);
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

static void link_set(CONV NEAR *c, ATOM item, WORD fmt, WORD opts)
{
    LINK NEAR *l;
    for (l = c->links; l; l = l->next)
        if (l->item == item && l->fmt == fmt) {
            l->opts = opts;
            return;
        }
    l = (LINK NEAR *)nalloc(sizeof(LINK));
    if (!l)
        return;
    l->item = item;
    l->fmt = fmt;
    l->opts = opts;
    l->next = c->links;
    c->links = l;
    c->status |= ST_ADVISE;
}

static BOOL link_drop(CONV NEAR *c, ATOM item, WORD fmt)
{
    LINK NEAR * NEAR *pp = &c->links;
    BOOL any = FALSE;
    while (*pp) {
        LINK NEAR *l = *pp;
        if (l->item == item && (!fmt || l->fmt == fmt)) {
            *pp = l->next;
            mon_link(c, l, FALSE);
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
        if (t == XTYP_ADVSTART)
            link_set(c, x->item, x->fmt,
                     0x2000 | ((x->type & XTYPF_NODATA) ? 0x4000 : 0) | ((x->type & XTYPF_ACKREQ) ? 0x8000 : 0));
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
            PostMessage(c->partner, WM_DDE_ACK, (WPARAM)c->hwnd, MAKELPARAM(DDE_FACK, addref(item)));
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
        /* the server ended it */
        c->dead = TRUE;
        c->status &= ~ST_CONNECTED;
        c->status |= ST_TERMINATED;
        mon_conv(c, FALSE);
        callback(c->inst, XTYP_DISCONNECT, 0, c, 0, 0, 0, 0, 0);
        c = conv_of_window(me);
        if (c) {
            DestroyWindow(c->hwnd);
            c->hwnd = NULL;
        }
        if (partner && IsWindow(partner))
            PostMessage(partner, WM_DDE_TERMINATE, (WPARAM)me, 0);
        if (c && !(c->status & ST_INLIST))
            conv_free(c);
        else if (c)
            c->partner = NULL;
        return;
    }
    if (c->status & ST_INLIST) {
        DestroyWindow(c->hwnd);
        c->hwnd = NULL;
        c->partner = NULL;
        if (!c->list)
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
        if (InSendMessage() && c->initiating) {
            if (!c->partner) {
                client_connected(c, (HWND)wp, LOWORD(lp), HIWORD(lp));
                return 0;
            }
            if (c->mode == CM_LIST) {
                CONV NEAR *n = conv_new(c->inst, TRUE, c->inst->dmg);
                if (n) {
                    CONV NEAR *s = local_window((HWND)wp, CLS_SERVER) ? conv_of_window((HWND)wp)
                                 : (CONV NEAR *)0;
                    n->mode = CM_LIST;
                    n->svcreq = addref(c->svcreq);
                    bcopy(&n->cc, &c->cc, sizeof(CONVCONTEXT));
                    client_connected(n, (HWND)wp, LOWORD(lp), HIWORD(lp));
                    if (s)
                        s->partner = n->hwnd;
                }
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
        msg = WM_DDE_REQUEST;
        lp = MAKELPARAM(fmt, addref(a));
        break;
    case XTYP_ADVSTOP:
        msg = WM_DDE_UNADVISE;
        lp = MAKELPARAM(fmt, addref(a));
        break;
    case XTYP_ADVSTART: {
        WORD FAR *o;
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4);
        if (!h) {
            seterr(in, DMLERR_MEMORY_ERROR);
            return 0;
        }
        o = (WORD FAR *)GlobalLock(h);
        o[0] = 0x2000 | ((type & XTYPF_NODATA) ? 0x4000 : 0) | ((type & XTYPF_ACKREQ) ? 0x8000 : 0);
        o[1] = fmt;
        GlobalUnlock(h);
        msg = WM_DDE_ADVISE;
        lp = MAKELPARAM(h, addref(a));
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
            h = data_block(in, pData, cb, a != 0, fmt);
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
            lp = MAKELPARAM(h, addref(a));
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
        if (err)
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
            LINK NEAR *l;
            link_set(c, item, fmt, opts);
            for (l = c->links; l; l = l->next)
                if (l->item == item && l->fmt == fmt)
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
        r = callback(in, XTYP_ADVSTOP, l->fmt, c, hsz(c->topic), hsz(item), 0, 0, (DWORD)hsz(item));
        if (r == CBR_BLOCK)
            return FALSE;
        if (!(c = conv_find(hc)))
            return TRUE;
        link_drop(c, item, fmt);
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
    server_reply(c, WM_DDE_DATA, MAKELPARAM(h, addref(l->item)));
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
        callback(c->inst, XTYP_DISCONNECT, 0, c, 0, 0, 0, 0, 0);
        c = conv_of_window(me);
        if (c) {
            DestroyWindow(c->hwnd);
            c->hwnd = NULL;
        }
        if (partner && IsWindow(partner))
            PostMessage(partner, WM_DDE_TERMINATE, (WPARAM)me, 0);
    }
    if (c)
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
    c->status &= ~ST_BLOCKED;
    run_queue(c, cmd == EC_ENABLEONE);
    c = conv_find(conv_handle(c));
    if (c && cmd == EC_ENABLEONE && c->queue)
        c->status |= ST_BLOCKED;
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
        }
        return 0;
    case WM_DDE_ACK:
        server_ack(c, LOWORD(lp), HIWORD(lp));
        return 0;
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
    ci.wLastError = c->lasterr;
    ci.hConvList = c->list ? (HCONVLIST)MAKELONG(c->list->hwnd, 0) : 0;
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
