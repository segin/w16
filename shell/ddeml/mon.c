/*
 * mon.c - DDEML monitors (APPCLASS_MONITOR): XTYP_MONITOR with the
 * MONHSZSTRUCT, MONMSGSTRUCT, MONCBSTRUCT, MONCONVSTRUCT, MONLINKSTRUCT
 * and MONERRSTRUCT blocks of the SDK (Programmer's Reference Vol. 3), and
 * the system-wide hooks (DdeSendHookProc, DdePostHookProc) that see DDE
 * messages for MF_SENDMSGS and MF_POSTMSGS.
 *
 * UNVERIFIED: 3.1's monitor blocks could not be read back by the probe
 * (DdeAccessData gave the same bytes for every event), so their contents
 * and the exact set of events are the documentation's
 * (ARCHITECTURE.md, chapter "DDEML").
 */
#include "ddeml.h"

static HHOOK hk_send, hk_post;
static BOOL inmon;

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
        h = data_block(in, p, n, TRUE, 0);
        if (!h)
            continue;
        d = data_new(in, h, DF_USED, 0, 0);
        hd = data_handle(d);
        {
            DWORD a[2];
            a[0] = ev;
            a[1] = (DWORD)hd;
            SendMessage(in->dmg, WM_DMG_MONITOR, 0, (LPARAM)(DWORD FAR *)a);
        }
        d = data_find(hd);
        if (d)
            data_free(d, TRUE);
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
    b.m.cb = sizeof(MONHSZSTRUCT);
    b.m.fsAction = action;
    b.m.dwTime = GetCurrentTime();
    b.m.hsz = h;
    b.m.hTask = GetCurrentTask();
    GlobalGetAtomName(LOWORD(h), b.m.str, 256);
    deliver(MF_HSZ_INFO, &b, sizeof b.m + lstrlen(b.m.str));
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
    if (type == XTYP_MONITOR || (in && (in->flags & IF_MONITOR)) || !(wanted() & MF_CALLBACKS))
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
    m.hszSvc = (HSZ)MAKELONG(c->svcpartner ? c->svcpartner : c->svcreq, 0);
    m.hszTopic = (HSZ)MAKELONG(c->topic, 0);
    if (c->status & ST_CLIENT)
        m.hConvClient = conv_handle(c);
    else
        m.hConvServer = conv_handle(c);
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
    m.hszSvc = (HSZ)MAKELONG(c->svcreq, 0);
    m.hszTopic = (HSZ)MAKELONG(c->topic, 0);
    m.hszItem = (HSZ)MAKELONG(l->item, 0);
    m.wFmt = l->fmt;
    m.fServer = !(c->status & ST_CLIENT);
    if (m.fServer)
        m.hConvServer = conv_handle(c);
    else
        m.hConvClient = conv_handle(c);
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
        if (c->message >= WM_DDE_FIRST && c->message <= WM_DDE_LAST)
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
