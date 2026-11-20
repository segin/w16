/*
 * inst.c - DDEML instances: DdeInitialize, DdeUninitialize,
 * DdeGetLastError, DdeNameService, the instance windows (DMGClass,
 * DMGFrame) and calling an application's DDE callback.
 * (ARCHITECTURE.md, chapter "DDEML"; behaviour as observed on 3.1 with
 * tests/ddeml/dm.c.)
 */
#include "ddeml.h"

HINSTANCE ddeml_inst;
INST NEAR *ddeml_insts;
WORD ddeml_seq = 2;                     /* the first DdeInitialize gets 2 */
WORD ddeml_lasterr = DMLERR_DLL_NOT_INITIALIZED;
WORD ddeml_kbcp;
int ddeml_syncs;

/* ---- memory ---- */

void NEAR *nalloc(WORD n)
{
    HLOCAL h = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n);
    return (void NEAR *)h;
}

void nfree(void *p)
{
    if (p)
        LocalFree((HLOCAL)p);
}

void bzero(void FAR *p, WORD n)
{
    BYTE FAR *b = (BYTE FAR *)p;
    while (n--)
        *b++ = 0;
}

void bcopy(void FAR *d, const void FAR *s, WORD n)
{
    BYTE FAR *t = (BYTE FAR *)d;
    const BYTE FAR *f = (const BYTE FAR *)s;
    while (n--)
        *t++ = *f++;
}

void hcopy(void FAR *d, const void FAR *s, DWORD n)
{
    hmemcpy(d, s, (long)n);
}

/* a far pointer OFF bytes into a (huge) block, without the compiler's
   huge-pointer helpers */
void FAR *hadd(void FAR *p, DWORD off)
{
    DWORD lin = (DWORD)OFFSETOF(p) + off;
    return MAKELP(SELECTOROF(p) + (WORD)(lin >> 16) * ddeml_ahincr, (WORD)lin);
}

/* a modal loop until *until (a synchronous transaction, waiting for
   TERMINATEs): messages are dispatched, the message filter hook sees
   them as MSGF_DDEMGR */
void modal(BOOL *until)
{
    MSG m;
    while (!*until) {
        if (!GetMessage(&m, NULL, 0, 0)) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (!CallMsgFilter(&m, MSGF_DDEMGR)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* ---- instances ---- */

INST NEAR *inst_find(DWORD id)
{
    INST NEAR *in;
    for (in = ddeml_insts; in; in = in->next)
        if ((WORD)in == LOWORD(id) && in->seq == HIWORD(id))
            return in;
    return NULL;
}

DWORD inst_id(INST NEAR *in)
{
    return MAKELONG((WORD)in, in->seq);
}

INST NEAR *inst_of_task(void)
{
    INST NEAR *in;
    HTASK t = GetCurrentTask();
    for (in = ddeml_insts; in; in = in->next)
        if (in->task == t)
            return in;
    return NULL;
}

void seterr_quiet(INST NEAR *in, WORD e)
{
    if (in)
        in->lasterr = e;
    else
        ddeml_lasterr = e;
}

void seterr(INST NEAR *in, WORD e)
{
    seterr_quiet(in, e);
    if (e)
        mon_err(in, e);
}

/* the CBF_SKIP_* notifications an instance does not want */
BOOL filtered(INST NEAR *in, UINT type)
{
    DWORD f = in->afCmd;
    switch (type) {
    case XTYP_CONNECT_CONFIRM: return (f & CBF_SKIP_CONNECT_CONFIRMS) != 0;
    case XTYP_REGISTER:        return (f & CBF_SKIP_REGISTRATIONS) != 0;
    case XTYP_UNREGISTER:      return (f & CBF_SKIP_UNREGISTRATIONS) != 0;
    case XTYP_DISCONNECT:      return (f & CBF_SKIP_DISCONNECTS) != 0;
    }
    return FALSE;
}

HDDEDATA callback(INST NEAR *in, UINT type, UINT fmt, CONV NEAR *c, HSZ h1, HSZ h2,
                  HDDEDATA d, DWORD d1, DWORD d2)
{
    return callback_hc(in, type, fmt, c, c ? conv_handle(c) : 0, h1, h2, d, d1, d2);
}

/* the callback with the conversation handle given (XTYP_CONNECT_CONFIRM
   names what the server window's handle reads after its ACK) */
HDDEDATA callback_hc(INST NEAR *in, UINT type, UINT fmt, CONV NEAR *c, HCONV hc, HSZ h1, HSZ h2,
                     HDDEDATA d, DWORD d1, DWORD d2)
{
    HDDEDATA r;
    if (!in || !in->cb || filtered(in, type))
        return 0;
    if (c)
        c->incb = TRUE;
    r = in->cb(type, fmt, hc, h1, h2, d, d1, d2);
    if (c)
        c->incb = FALSE;
    mon_cb(in, type, fmt, hc, h1, h2, d, d1, d2, (DWORD)r);
    return r;
}

/* a callback made in another task: its DMGClass window runs it */
typedef struct {
    UINT type, fmt;
    HSZ h1, h2;
    DWORD d1, d2;
} CBARGS;

static void remote_callback(INST NEAR *in, UINT type, HSZ h1, HSZ h2)
{
    CBARGS a;
    a.type = type;
    a.fmt = 0;
    a.h1 = h1;
    a.h2 = h2;
    a.d1 = a.d2 = 0;
    if (in->dmg && IsWindow(in->dmg))
        SendMessage(in->dmg, WM_DMG_CALLBACK, 0, (LPARAM)(CBARGS FAR *)&a);
}

/* XTYP_REGISTER / XTYP_UNREGISTER to every instance, in the z-order of
   their DMGClass windows (newest first), as 3.1 does */
static void broadcast_reg(INST NEAR *from, UINT type, ATOM a, BOOL self)
{
    HWND h;
    HSZ base = (HSZ)MAKELONG(a, 0), spec = inst_hsz(from, a);
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        INST NEAR *in;
        if (!local_window(h, CLS_DMG))
            continue;
        in = (INST NEAR *)GetWindowWord(h, 0);
        for (in = ddeml_insts; in; in = in->next)
            if (in->dmg == h)
                break;
        if (!in || (in == from && !self) || (in->flags & IF_DYING && in != from))
            continue;
        remote_callback(in, type, base, spec);
    }
}

LRESULT FAR PASCAL __loadds DmgWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    INST NEAR *in;
    for (in = ddeml_insts; in; in = in->next)
        if (in->dmg == w)
            break;
    switch (msg) {
    case WM_DMG_CALLBACK:
        if (in) {
            /* REGISTER / UNREGISTER: the name is held (MH_INCATOM)
               around the callback, in the receiving task, as 3.1 */
            CBARGS FAR *a = (CBARGS FAR *)lp;
            ATOM n = hsz_inc(LOWORD(a->h1));
            LRESULT r = (LRESULT)callback(in, a->type, a->fmt, NULL, a->h1, a->h2, 0, a->d1, a->d2);
            hsz_dec(n);
            return r;
        }
        return 0;
    case WM_DMG_MONITOR:
        if (in) {
            DWORD FAR *a = (DWORD FAR *)lp;
            return (LRESULT)callback(in, XTYP_MONITOR, 0, NULL, 0, 0, (HDDEDATA)a[1], 0, a[0]);
        }
        return 0;
    case WM_DDE_INITIATE: {
        /* a monitor sees an INITIATE to this window after it was
           handled (the hook leaves it out), unless it made a
           conversation for anything but a list of this library
           (tests/ddeml/scripts/xmon2.dm) */
        BOOL made = FALSE;
        if (in && !(in->flags & IF_DYING))
            made = conv_initiate(in, w, LOWORD(lp), HIWORD(lp), (HWND)wp);
        if (!made || conv_listing((HWND)wp))
            mon_msg(FALSE, w, WM_DDE_INITIATE, wp, lp);
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

LRESULT FAR PASCAL __loadds SubframeWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the DMLAPI ---- */

static void register_classes(void);

UINT DMLAPI DdeInitialize(DWORD FAR *pid, PFNCALLBACK cb, DWORD afCmd, DWORD res)
{
    INST NEAR *in;
    (void)res;
    if (!pid || !cb)
        return DMLERR_INVALIDPARAMETER;
    if (*pid) {
        /* again: new filters for the instance */
        in = inst_find(*pid);
        if (!in)
            return DMLERR_INVALIDPARAMETER;
        in->cb = cb;
        in->afCmd = (in->afCmd & (APPCLASS_MASK)) | (afCmd & ~APPCLASS_MASK);
        if (afCmd & APPCMD_CLIENTONLY)
            in->flags |= IF_CLIENTONLY;
        else
            in->flags &= ~IF_CLIENTONLY;
        return DMLERR_NO_ERROR;
    }
    if (!ddeml_kbcp) {
        ddeml_kbcp = (WORD)GetKBCodePage();
        register_classes();
        /* 3.1's DDEML adds these three global atoms at its first
           DdeInitialize (seen with tests/ddeml/scripts/xatoms.dm: they
           appear then, in this order, and stay); their place in USER's
           heap decides the values of every atom made after them */
        GlobalAddAtom("Progman");
        GlobalAddAtom("Who");
        GlobalAddAtom("Made me?");
    }
    in = (INST NEAR *)nalloc(sizeof(INST));
    if (!in)
        return DMLERR_SYS_ERROR;
    in->seq = ddeml_seq++;
    in->task = GetCurrentTask();
    in->cb = cb;
    in->afCmd = afCmd;
    in->flags = IF_FILTER;
    if ((afCmd & APPCLASS_MASK) == APPCLASS_MONITOR)
        in->flags |= IF_MONITOR;
    if (afCmd & APPCMD_CLIENTONLY)
        in->flags |= IF_CLIENTONLY;
    in->next = ddeml_insts;
    ddeml_insts = in;
    /* an overlapped window and a popup: USER makes their styles
       04C00000 and 84000000, as 3.1's are */
    in->dmg = CreateWindow(CLS_DMG, "", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL,
                           ddeml_inst, NULL);
    if (in->dmg) {
        SetWindowWord(in->dmg, 0, (WORD)in);
        SetWindowWord(in->dmg, 2, 0);
        SetWindowWord(in->dmg, 4, 0);
    }
    if (!(in->flags & IF_MONITOR)) {
        in->frame = CreateWindow(CLS_FRAME, "", WS_POPUP, 0, 0, 0, 0, NULL, NULL,
                                 ddeml_inst, NULL);
        if (in->frame) {
            SetWindowWord(in->frame, 0, (WORD)in);
            SetWindowWord(in->frame, 2, 0);
        }
    } else if (afCmd & MF_MASK) {
        /* a monitor's second window: DMGMonitorClass, overlapped, 4
           bytes (tests/ddeml/scripts/xmon.dm); a monitor asking for no
           event has none (flags.dm: the windows made later get 3.1's
           handles only so) */
        in->mon = CreateWindow(CLS_MON, "", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL,
                               ddeml_inst, NULL);
        mon_init(in);
        if (in->mon) {
            SetWindowWord(in->mon, 0, (WORD)in);
            SetWindowWord(in->mon, 2, 0);
        }
    }
    *pid = inst_id(in);
    mon_update();
    return DMLERR_NO_ERROR;
}

BOOL DMLAPI DdeUninitialize(DWORD id)
{
    INST NEAR *in = inst_find(id), NEAR * NEAR *pp;
    NAME NEAR *n;
    if (!in) {
        seterr(NULL, id ? DMLERR_INVALIDPARAMETER : DMLERR_DLL_NOT_INITIALIZED);
        return FALSE;
    }
    in->flags |= IF_DYING;
    /* the names go first (the others are told), then the conversations
       (in the order they were made), then everything else */
    while ((n = in->names) != NULL) {
        broadcast_reg(in, XTYP_UNREGISTER, n->atom, FALSE);
        in->names = n->next;
        hsz_dec(n->atom);
        nfree(n);
    }
    {
        CONV NEAR *c;
        conv_terminate_all(in);
        /* wait for the partners' answers: until every partner window
           has gone (a partner's TERMINATE may still be queued then).
           As 3.1 (tests/ddeml/scripts/xun*.dm): the others go first,
           and the task lets them run again after each TERMINATE one of
           its client windows takes */
        {
            DWORD end = GetCurrentTime() + 10000;
            MSG m;
            Yield();
            for (;;) {
                BOOL all = TRUE;
                for (c = in->convs; c; c = c->next)
                    if (c->partner && IsWindow(c->partner) && c->hwnd && IsWindow(c->hwnd))
                        all = FALSE;
                if (all || (long)(GetCurrentTime() - end) >= 0)
                    break;
                if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
                    BOOL cl = m.message == WM_DDE_TERMINATE && local_window(m.hwnd, CLS_CLIENT);
                    TranslateMessage(&m);
                    DispatchMessage(&m);
                    if (cl)
                        Yield();
                } else
                    Yield();
            }
            while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
        while (in->convs)
            conv_free(in->convs);
    }
    while (in->lists) {
        CLIST NEAR *l = in->lists;
        in->lists = l->next;
        if (IsWindow(l->hwnd))
            DestroyWindow(l->hwnd);
        nfree(l);
    }
    data_freeall(in);
    str_freeall(in);
    if (in->hold && IsWindow(in->hold))
        DestroyWindow(in->hold);
    if (in->frame && IsWindow(in->frame))
        DestroyWindow(in->frame);
    if (in->mon && IsWindow(in->mon))
        DestroyWindow(in->mon);
    if (in->monblk)
        GlobalFree(in->monblk);
    if (in->dmg && IsWindow(in->dmg))
        DestroyWindow(in->dmg);
    for (pp = (INST NEAR * NEAR *)&ddeml_insts; *pp; pp = &(*pp)->next)
        if (*pp == in) {
            *pp = in->next;
            break;
        }
    nfree(in);
    mon_update();
    return TRUE;
}

UINT DMLAPI DdeGetLastError(DWORD id)
{
    INST NEAR *in = inst_find(id);
    UINT e;
    if (!in)                            /* as 3.1 */
        return id ? DMLERR_INVALIDPARAMETER : ddeml_lasterr;
    e = in->lasterr;
    in->lasterr = 0;
    return e;
}

HDDEDATA DMLAPI DdeNameService(DWORD id, HSZ hsz1, HSZ hsz2, UINT cmd)
{
    INST NEAR *in = inst_find(id);
    ATOM a = LOWORD(hsz1);
    (void)hsz2;
    if (!in) {
        seterr(NULL, id ? DMLERR_INVALIDPARAMETER : DMLERR_DLL_NOT_INITIALIZED);
        return 0;
    }
    if (in->flags & IF_CLIENTONLY) {
        seterr(in, DMLERR_DLL_USAGE);
        return 0;
    }
    if (cmd & DNS_FILTERON)
        in->flags |= IF_FILTER;
    if (cmd & DNS_FILTEROFF)
        in->flags &= ~IF_FILTER;
    if (cmd & DNS_REGISTER) {
        NAME NEAR *n;
        if (!a) {
            seterr(in, DMLERR_INVALIDPARAMETER);
            return 0;
        }
        n = (NAME NEAR *)nalloc(sizeof(NAME));
        if (!n) {
            seterr(in, DMLERR_MEMORY_ERROR);
            return 0;
        }
        n->atom = hsz_inc(a);
        n->next = in->names;
        in->names = n;
        broadcast_reg(in, XTYP_REGISTER, a, TRUE);
        return (HDDEDATA)1;
    }
    if (cmd & DNS_UNREGISTER) {
        NAME NEAR * NEAR *pp = &in->names;
        while (*pp) {
            NAME NEAR *n = *pp;
            if (!a || n->atom == a) {
                *pp = n->next;
                broadcast_reg(in, XTYP_UNREGISTER, n->atom, TRUE);
                hsz_dec(n->atom);
                nfree(n);
                if (a)
                    break;
            } else
                pp = &n->next;
        }
        return (HDDEDATA)1;
    }
    return 0;
}

/* the termination dialog (TERMDIALOG); w16's DdeUninitialize waits without the
   dialog (UNVERIFIED when 3.1 shows it) */
BOOL FAR PASCAL __loadds TermDlgProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == 3 || wp == 4 || wp == 5)) {
        EndDialog(w, wp);
        return TRUE;
    }
    return FALSE;
}


static void reg(LPCSTR name, WNDPROC proc, int extra)
{
    WNDCLASS wc;
    bzero(&wc, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.cbWndExtra = extra;
    wc.hInstance = ddeml_inst;
    wc.lpszClassName = name;
    RegisterClass(&wc);
}

/* the library's entry (entry.asm): its window classes. 3.1's windows
   show five; the handles its later windows get (tests/ddeml/scripts/
   xatoms.dm) put exactly one class more in USER's heap (each moves them
   0x44 bytes): the sixth is the monitors' class, DMGMonitorClass (a
   monitor instance's second window shows it) */
static void register_classes(void)
{
    reg(CLS_FRAME, SubframeWndProc, 4);
    reg(CLS_DMG, DmgWndProc, 6);
    reg(CLS_CLIENT, ClientWndProc, 6);
    reg(CLS_SERVER, ServerWndProc, 6);
    reg(CLS_HOLD, SubframeWndProc, 8);
    reg(CLS_MON, MonitorWndProc, 4);
}

/* the library's entry (entry.asm). The classes are registered at the
   first DdeInitialize, not here: with another library's instance made
   first, 3.1's window handles show its classes were not yet in USER's
   heap (tests/ddeml/scripts/raw.dm as raw+x) */
int FAR __cdecl ddeml_init(WORD hinst)
{
    ddeml_inst = (HINSTANCE)hinst;
    return 1;
}
