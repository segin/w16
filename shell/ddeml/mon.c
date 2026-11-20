/*
 * mon.c - DDEML monitors (APPCLASS_MONITOR): XTYP_MONITOR with the
 * MONHSZSTRUCT, MONMSGSTRUCT, MONCBSTRUCT, MONCONVSTRUCT, MONLINKSTRUCT
 * and MONERRSTRUCT blocks of the SDK (Programmer's Reference Vol. 3), and
 * the system-wide hooks (DdeSendHookProc, DdePostHookProc) that see DDE
 * messages for MF_SENDMSGS and MF_POSTMSGS.
 *
 * The events and their order are 3.1's as its monitors see them
 * (tests/ddeml/scripts/monitor.dm, xmon.dm, xmon2.dm, xstr.dm;
 * ARCHITECTURE.md, chapter "DDEML"): the blocks are data handles
 * without a DDE header (their size: GlobalSize); string events include
 * the library's own atom references (MH_ADDATOM, MH_INCATOM,
 * MH_DECATOM); conversation and link blocks name both sides.
 */
#include "ddeml.h"

static HHOOK hk_send, hk_post;
static BOOL inmon;

/* the events go out in one block per monitor instance, made with the
   instance: 3.1 makes no global block per event (its event handles
   point into one block made at DdeInitialize), so the handles other
   blocks get stay as they would be (monitor.dm as monitor+x). The
   size a monitor sees is the event's rounded up to 32 bytes, as 3.1's */
#define MONBLK 320                      /* the largest event: a string's */

void mon_init(INST NEAR *in)
{
    in->monblk = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, MONBLK);
}

static HGLOBAL mon_block(INST NEAR *in, const void FAR *p, WORD n)
{
    void FAR *q;
    if (!in->monblk || n > MONBLK || (q = GlobalLock(in->monblk)) == NULL)
        return NULL;
    bcopy(q, p, n);
    GlobalUnlock(in->monblk);
    return in->monblk;
}

static DWORD wanted(void)
{
    INST NEAR *in;
    DWORD f = 0;
    for (in = ddeml_insts; in; in = in->next)
        if (in->flags & IF_MONITOR)
            f |= in->afCmd & MF_MASK;
    return f;
}

static void deliver(DWORD ev, const void FAR *p, WORD n)
{
    INST NEAR *in;
    if (inmon || !(wanted() & ev))
        return;
    inmon = TRUE;
    for (in = ddeml_insts; in; in = in->next) {
        HGLOBAL h;
        HDDEDATA hd;
        DATA NEAR *d;
        if (!(in->flags & IF_MONITOR) || !(in->afCmd & ev) || !in->dmg)
            continue;
        h = mon_block(in, p, n);
        if (!h)
            continue;
        d = data_new(in, h, DF_USED | DF_NOHEADER, 0, 0);
        if (d)
            d->size = ((DWORD)n + 31) & ~31L;
        hd = data_handle(d);
        {
            DWORD a[2];
            a[0] = ev;
            a[1] = (DWORD)hd;
            SendMessage(in->dmg, WM_DMG_MONITOR, 0, (LPARAM)(DWORD FAR *)a);
        }
        d = data_find(hd);
        if (d)
            data_free(d, FALSE);        /* the block stays */
    }
    inmon = FALSE;
}

LRESULT FAR PASCAL __loadds MonitorWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, msg, wp, lp);
}

void mon_hsz(INST NEAR *in, HSZ h, WORD action)
{
    struct {
        MONHSZSTRUCT m;
        char more[256];
    } b;
    (void)in;
    if (!(wanted() & MF_HSZ_INFO))
        return;
    bzero(&b, sizeof b);
    b.m.fsAction = action;
    b.m.dwTime = GetCurrentTime();
    b.m.hsz = h;
    b.m.hTask = GetCurrentTask();
    GlobalGetAtomName(LOWORD(h), b.m.str, 256);
    /* 3.1's cb: the structure with its word padding (18) and the
       string's length */
    b.m.cb = 18 + lstrlen(b.m.str);
    deliver(MF_HSZ_INFO, &b, b.m.cb);
}

void mon_msg(BOOL post, HWND to, UINT msg, WPARAM wp, LPARAM lp)
{
    MONMSGSTRUCT m;
    bzero(&m, sizeof m);
    m.cb = sizeof m;
    m.hwndTo = to;
    m.dwTime = GetCurrentTime();
    m.hTask = GetCurrentTask();
    m.wMsg = msg;
    m.wParam = wp;
    m.lParam = lp;
    deliver(post ? MF_POSTMSGS : MF_SENDMSGS, &m, sizeof m);
}

void mon_cb(INST NEAR *in, UINT type, UINT fmt, HCONV c, HSZ h1, HSZ h2, HDDEDATA d,
            DWORD d1, DWORD d2, DWORD ret)
{
    MONCBSTRUCT m;
    (void)in;
    if (type == XTYP_MONITOR || !(wanted() & MF_CALLBACKS))
        return;
    bzero(&m, sizeof m);
    m.cb = sizeof m;
    m.dwTime = GetCurrentTime();
    m.hTask = GetCurrentTask();
    m.dwRet = ret;
    m.wType = type;
    m.wFmt = fmt;
    m.hConv = c;
    m.hsz1 = h1;
    m.hsz2 = h2;
    m.hData = d;
    m.dwData1 = d1;
    m.dwData2 = d2;
    deliver(MF_CALLBACKS, &m, sizeof m);
}

void mon_conv(CONV NEAR *c, BOOL connect)
{
    MONCONVSTRUCT m;
    if (!(wanted() & MF_CONV))
        return;
    bzero(&m, sizeof m);
    m.cb = sizeof m;
    m.fConnect = connect;
    m.dwTime = GetCurrentTime();
    m.hTask = GetCurrentTask();
    m.hszSvc = (HSZ)MAKELONG(c->svcpartner ? c->svcpartner : c->svcreq ? c->svcreq : c->svcask, 0);
    m.hszTopic = (HSZ)MAKELONG(c->topic, 0);
    if (c->status & ST_CLIENT) {
        m.hConvClient = conv_handle(c);
        m.hConvServer = partner_handle(c, FALSE);
    } else {
        m.hConvServer = conv_handle(c);
        m.hConvClient = partner_handle(c, FALSE);
    }
    deliver(MF_CONV, &m, sizeof m);
}

void mon_link(CONV NEAR *c, LINK NEAR *l, BOOL est)
{
    MONLINKSTRUCT m;
    if (!(wanted() & MF_LINKS))
        return;
    bzero(&m, sizeof m);
    m.cb = sizeof m;
    m.dwTime = GetCurrentTime();
    m.hTask = GetCurrentTask();
    m.fEstablished = est;
    m.fNoData = (l->opts & 0x4000) != 0;
    m.hszSvc = (HSZ)MAKELONG(c->svcpartner ? c->svcpartner : c->svcreq, 0);
    m.hszTopic = (HSZ)MAKELONG(c->topic, 0);
    m.hszItem = (HSZ)MAKELONG(l->item, 0);
    m.wFmt = l->fmt;
    m.fServer = !(c->status & ST_CLIENT);
    if (m.fServer) {
        m.hConvServer = conv_handle(c);
        m.hConvClient = partner_handle(c, TRUE);
    } else {
        m.hConvClient = conv_handle(c);
        m.hConvServer = partner_handle(c, TRUE);
    }
    deliver(MF_LINKS, &m, sizeof m);
}

void mon_err(INST NEAR *in, WORD e)
{
    MONERRSTRUCT m;
    (void)in;
    if (!(wanted() & MF_ERRORS))
        return;
    bzero(&m, sizeof m);
    m.cb = sizeof m;
    m.wLastError = e;
    m.dwTime = GetCurrentTime();
    m.hTask = GetCurrentTask();
    deliver(MF_ERRORS, &m, sizeof m);
}

typedef struct { LPARAM lParam; WPARAM wParam; UINT message; HWND hwnd; } CWP;

LRESULT FAR PASCAL __loadds DdeSendHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && lp) {
        CWP FAR *c = (CWP FAR *)lp;
        /* an INITIATE to an instance window is told after it was
           handled, by that window (this library's) or by the client
           (this library's client, another library's window): 3.1
           (tests/ddeml/scripts/xmon2.dm) */
        if (c->message == WM_DDE_INITIATE && is_dmgclass(c->hwnd) &&
            (local_window(c->hwnd, CLS_DMG) || local_window((HWND)c->wParam, CLS_CLIENT)))
            ;
        else if (c->message >= WM_DDE_FIRST && c->message <= WM_DDE_LAST)
            mon_msg(FALSE, c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(hk_send, code, wp, lp);
}

LRESULT FAR PASCAL __loadds DdePostHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && lp) {
        MSG FAR *m = (MSG FAR *)lp;
        if (m->message >= WM_DDE_FIRST && m->message <= WM_DDE_LAST)
            mon_msg(TRUE, m->hwnd, m->message, m->wParam, m->lParam);
    }
    return CallNextHookEx(hk_post, code, wp, lp);
}

/* the hooks are there while some monitor wants messages */
void mon_update(void)
{
    DWORD f = wanted();
    if ((f & MF_SENDMSGS) && !hk_send)
        hk_send = SetWindowsHookEx(WH_CALLWNDPROC, (HOOKPROC)DdeSendHookProc, ddeml_inst, NULL);
    if (!(f & MF_SENDMSGS) && hk_send) {
        UnhookWindowsHookEx(hk_send);
        hk_send = NULL;
    }
    if ((f & MF_POSTMSGS) && !hk_post)
        hk_post = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)DdePostHookProc, ddeml_inst, NULL);
    if (!(f & MF_POSTMSGS) && hk_post) {
        UnhookWindowsHookEx(hk_post);
        hk_post = NULL;
    }
}
