/*
 * ocspy.c - OCSPY.DLL: a logging DDE spy (probe OC2, black-box observation
 * of the OLE 1.0 wire protocol; ARCHITECTURE.md 16).
 *
 * System-wide WH_GETMESSAGE and WH_CALLWNDPROC hooks (documented Windows
 * 3.1 SDK interfaces) see every posted and sent WM_DDE_* message of every
 * task. Each one is written to C:\OC.LOG, decoded: the windows as
 * "<module>:<class>#<n>" (n numbers the windows in the order the log first
 * sees them, so that logs of two runs compare), atoms by name, clipboard
 * formats by name, and the global memory objects the messages carry (DDE
 * data, advise options, execute strings, metafiles, bitmaps) by size, a
 * checksum and their first bytes.
 *
 * Also the log for the probe client OC1 (SpyLog), so that the client's
 * calls, its callbacks and the DDE traffic land in one file in order.
 * The file is opened and closed for each line: DOS handles belong to the
 * task that opens them, and the hooks run in every task.
 */
#include <windows.h>
#include <dde.h>
#include <toolhelp.h>

static HHOOK hk_get, hk_call;
static HINSTANCE self;
static BOOL dump_all;           /* SpyAll: also non-DDE messages (unused) */
static HWND seen_w[256];        /* window numbering (a probe's table) */
static WORD n_seen;
static DWORD last_key[4];
static WORD verbose = 32;       /* bytes of each data object shown */
static BOOL times;              /* SpyTimes: each DDE line's time */
static DWORD t0;

extern WORD __cdecl lib_regs[12];       /* dllstart.asm: DI = instance */

int __cdecl __far libmain(void)
{
    self = (HINSTANCE)lib_regs[5];
    return 1;
}

static void append(const char FAR *s, WORD n)
{
    HFILE f = _lopen("C:\\OC.LOG", OF_WRITE | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\OC.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

void FAR PASCAL __loadds SpyLog(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    append(s, n);
}

/* ---- a small line builder ---- */
static char line[600];
static WORD lp_;

static void put(const char FAR *s)
{
    while (*s && lp_ < sizeof line - 4)
        line[lp_++] = *s++;
}

static void putc_(char c)
{
    if (lp_ < sizeof line - 4)
        line[lp_++] = c;
}

static void puthex(DWORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        putc_("0123456789ABCDEF"[(WORD)(v >> (i * 4)) & 15]);
}

static void putdec(DWORD v)
{
    char b[12];
    int i = 11;
    b[11] = 0;
    do {
        DWORD q = v / 10;
        b[--i] = (char)('0' + (WORD)(v - q * 10));
        v = q;
    } while (v);
    put(b + i);
}

static void putq(const char FAR *s, WORD n)  /* quoted, escaped */
{
    WORD i;
    putc_('"');
    for (i = 0; i < n; i++) {
        BYTE c = (BYTE)s[i];
        if (c < 32 || c >= 127 || c == '"' || c == '\\') {
            putc_('\\');
            putc_('x');
            puthex(c, 2);
        } else
            putc_((char)c);
    }
    putc_('"');
}

static void flush(void)
{
    line[lp_++] = '\r';
    line[lp_++] = '\n';
    append(line, lp_);
    lp_ = 0;
}

static void putwin(HWND w)
{
    char b[64];
    WORD i, n;
    if (!w) {
        put("0");
        return;
    }
    if (!IsWindow(w)) {
        put("dead:");
        puthex((WORD)w, 4);
        return;
    }
    for (i = 0; i < n_seen; i++)
        if (seen_w[i] == w)
            break;
    if (i == n_seen && n_seen < sizeof seen_w / sizeof seen_w[0])
        seen_w[n_seen++] = w;
    n = GetModuleFileName((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE), b, sizeof b);
    {   /* base name without extension */
        WORD s = 0, e;
        for (e = 0; e < n; e++)
            if (b[e] == '\\' || b[e] == ':')
                s = e + 1;
        for (e = s; e < n && b[e] != '.'; e++)
            putc_(b[e]);
    }
    putc_(':');
    GetClassName(w, b, sizeof b);
    put(b);
    putc_('#');
    putdec(i);
}

static void putatom(WORD a)
{
    char b[256];
    WORD n;
    if (!a) {
        put("0");
        return;
    }
    n = GlobalGetAtomName(a, b, sizeof b);
    if (!n) {
        put("atom?");
        puthex(a, 4);
        return;
    }
    putq(b, n);
}

static void putfmt(WORD f)
{
    char b[64];
    WORD n;
    switch (f) {
    case CF_TEXT: put("CF_TEXT"); return;
    case CF_BITMAP: put("CF_BITMAP"); return;
    case CF_METAFILEPICT: put("CF_METAFILEPICT"); return;
    case CF_DIB: put("CF_DIB"); return;
    case CF_PALETTE: put("CF_PALETTE"); return;
    case 0: put("0"); return;
    }
    n = GetClipboardFormatName(f, b, sizeof b);
    if (n)
        putq(b, n);
    else {
        put("cf");
        puthex(f, 4);
    }
}

static DWORD sum(const BYTE __huge *p, DWORD n)
{
    DWORD s = 0, i;
    for (i = 0; i < n; i++)
        s = s * 31 + p[i];
    return s;
}

static void putbytes(const BYTE FAR *p, DWORD n)
{
    WORD i, m = n < verbose ? (WORD)n : verbose;
    for (i = 0; i < m; i++)
        puthex(p[i], 2);
    if (m < n)
        put("..");
}

/* a global memory object: size, checksum, first bytes */
static void putmem(HGLOBAL h, WORD skip)
{
    DWORD sz;
    BYTE FAR *p;
    if (!h) {
        put("null");
        return;
    }
    sz = GlobalSize(h);
    p = (BYTE FAR *)GlobalLock(h);
    if (!p) {
        put("badmem:");
        puthex((WORD)h, 4);
        return;
    }
    if (sz < skip)
        skip = (WORD)sz;
    put("size=");
    putdec(sz - skip);
    put(" sum=");
    puthex(sum((const BYTE __huge *)(p + skip), sz - skip), 8);
    put(" ");
    putbytes(p + skip, sz - skip);
    GlobalUnlock(h);
}

/* the metafile a METAFILEPICT holds: its records, by size and checksum
   (a metafile handle is a global memory handle in Windows 3.1) */
static void putmfp(HGLOBAL h)
{
    METAFILEPICT FAR *m;
    if (!h) {
        put("null");
        return;
    }
    m = (METAFILEPICT FAR *)GlobalLock(h);
    if (!m) {
        put("badmfp");
        return;
    }
    put("mm=");
    putdec(m->mm);
    put(" x=");
    putdec((DWORD)(long)m->xExt);
    put(" y=");
    putdec((DWORD)(long)m->yExt);
    put(" mf:");
    putmem((HGLOBAL)m->hMF, 0);
    GlobalUnlock(h);
}

static void putbitmap(HBITMAP b)
{
    BITMAP bm;
    if (!GetObject(b, sizeof bm, &bm)) {
        put("badbitmap");
        return;
    }
    put("bm ");
    putdec(bm.bmWidth);
    putc_('x');
    putdec(bm.bmHeight);
    putc_('x');
    putdec(bm.bmPlanes);
    putc_('x');
    putdec(bm.bmBitsPixel);
    {
        DWORD n = (DWORD)bm.bmWidthBytes * bm.bmHeight * bm.bmPlanes;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
        if (h) {
            BYTE __huge *p = (BYTE __huge *)GlobalLock(h);
            GetBitmapBits(b, n, (void FAR *)p);
            put(" sum=");
            puthex(sum(p, n), 8);
            GlobalUnlock(h);
            GlobalFree(h);
        }
    }
}

/* DDEDATA/DDEPOKE: flags word, format word, then the data (for
   CF_METAFILEPICT, CF_BITMAP, CF_PALETTE a handle) */
static void putdata(HGLOBAL h)
{
    WORD FAR *p;
    WORD fl, cf;
    if (!h) {
        put("null");
        return;
    }
    p = (WORD FAR *)GlobalLock(h);
    if (!p) {
        put("baddata:");
        puthex((WORD)h, 4);
        return;
    }
    fl = p[0];
    cf = p[1];
    put("flags=");
    puthex(fl, 4);
    put(" fmt=");
    putfmt(cf);
    put(" ");
    if (cf == CF_METAFILEPICT)
        putmfp((HGLOBAL)p[2]);
    else if (cf == CF_BITMAP)
        putbitmap((HBITMAP)p[2]);
    else if (cf == CF_PALETTE) {
        put("palette ");
        puthex(p[2], 4);
    } else {
        GlobalUnlock(h);
        putmem(h, 4);
        return;
    }
    GlobalUnlock(h);
}

static BOOL is_atom(WORD a)
{
    char b[4];
    return a >= 0xC000 && GlobalGetAtomName(a, b, sizeof b) != 0;
}

static const char *const names[] = {
    "INITIATE", "TERMINATE", "ADVISE", "UNADVISE", "ACK", "DATA",
    "REQUEST", "POKE", "EXECUTE"
};

static void log_dde(char how, HWND to, UINT msg, WPARAM wp, LPARAM lp)
{
    WORD lo = LOWORD(lp), hi = HIWORD(lp);
    if (times) {                        /* ms since SpyTimes(1) */
        putdec(GetTickCount() - t0);
        put(" ");
    }
    put("DDE ");
    putc_(how);
    putc_(' ');
    put(names[msg - WM_DDE_FIRST]);
    put(" ");
    putwin((HWND)wp);
    put(" -> ");
    putwin(to);
    put(" ");
    switch (msg) {
    case WM_DDE_INITIATE:
        putatom(lo);
        put(" ");
        putatom(hi);
        break;
    case WM_DDE_TERMINATE:
        break;
    case WM_DDE_ADVISE:
        put("item=");
        putatom(hi);
        put(" ");
        if (lo) {
            WORD FAR *o = (WORD FAR *)GlobalLock((HGLOBAL)lo);
            if (o) {
                put("flags=");
                puthex(o[0], 4);
                put(" fmt=");
                putfmt(o[1]);
                GlobalUnlock((HGLOBAL)lo);
            } else
                put("badopt");
        }
        break;
    case WM_DDE_UNADVISE:
    case WM_DDE_REQUEST:
        put("item=");
        putatom(hi);
        put(" fmt=");
        putfmt(lo);
        break;
    case WM_DDE_ACK:
        if (how == 'S') {               /* answer to INITIATE */
            putatom(lo);
            put(" ");
            putatom(hi);
        } else {
            put("status=");
            puthex(lo, 4);
            put(" ");
            if (is_atom(hi)) {
                put("item=");
                putatom(hi);
            } else if (hi) {
                char FAR *s = (char FAR *)GlobalLock((HGLOBAL)hi);
                put("cmd=");
                if (s) {
                    WORD n = 0;
                    while (s[n] && n < 400)
                        n++;
                    putq(s, n);
                    GlobalUnlock((HGLOBAL)hi);
                } else
                    puthex(hi, 4);
            } else
                put("0");
        }
        break;
    case WM_DDE_DATA:
    case WM_DDE_POKE:
        put("item=");
        putatom(hi);
        put(" ");
        putdata((HGLOBAL)lo);
        break;
    case WM_DDE_EXECUTE:
        {
            char FAR *s = (char FAR *)GlobalLock((HGLOBAL)hi);
            if (s) {
                WORD n = 0;
                while (s[n] && n < 400)
                    n++;
                putq(s, n);
                GlobalUnlock((HGLOBAL)hi);
            } else
                put("badcmd");
        }
        break;
    }
    flush();
}

/* The first time the hooks run in a task: its module and command line
   (the PSP's command tail, offset 80h; GetCurrentPDB is KERNEL.37), to see
   how OLE starts servers. */
UINT FAR PASCAL GetCurrentPDB(void);       /* KERNEL.37 */
static HTASK seen_t[64];
static WORD n_seen_t;

static void note_task(HWND w)
{
    HTASK t = GetCurrentTask();
    WORD i;
    char b[80];
    BYTE FAR *psp;
    for (i = 0; i < n_seen_t; i++)
        if (seen_t[i] == t)
            return;
    if (n_seen_t < sizeof seen_t / sizeof seen_t[0])
        seen_t[n_seen_t++] = t;
    put("TASK ");
    {
        WORD n = GetModuleFileName((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE), b, sizeof b);
        putq(b, n);
    }
    psp = (BYTE FAR *)MAKELP(GetCurrentPDB(), 0x80);
    put(" tail=");
    putq((char FAR *)psp + 1, psp[0]);
    flush();
}

LRESULT CALLBACK __loadds SpyGetMsg(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && ((MSG FAR *)lp)->hwnd)
        note_task(((MSG FAR *)lp)->hwnd);
    if (code >= 0) {
        MSG FAR *m = (MSG FAR *)lp;
        if (m->message >= WM_DDE_FIRST && m->message <= WM_DDE_LAST) {
            /* a message seen again unremoved (PeekMessage NOREMOVE) is
               logged once */
            if (last_key[0] != (DWORD)(WORD)m->hwnd || last_key[1] != m->message ||
                last_key[2] != m->wParam || last_key[3] != (DWORD)m->lParam) {
                last_key[0] = (WORD)m->hwnd;
                last_key[1] = m->message;
                last_key[2] = m->wParam;
                last_key[3] = m->lParam;
                log_dde('P', m->hwnd, m->message, m->wParam, m->lParam);
            }
        }
    }
    return CallNextHookEx(hk_get, code, wp, lp);
}

/* Windows 3.1's WH_CALLWNDPROC: lParam points at lParam, wParam, message,
   hwnd (in that order in memory) */
typedef struct {
    LPARAM lParam;
    WPARAM wParam;
    UINT message;
    HWND hwnd;
} CWP;

LRESULT CALLBACK __loadds SpyCallWnd(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0) {
        CWP FAR *c = (CWP FAR *)lp;
        if (c->message >= WM_DDE_FIRST && c->message <= WM_DDE_LAST)
            log_dde('S', c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(hk_call, code, wp, lp);
}

/* the fault hook (ocfault.asm): exception, CS:IP and the module and
   segment number CS belongs to */
void FAR PASCAL SPYFAULTHOOK(void);
static BOOL fault_hooked;

void __cdecl __far spy_fault(WORD no, WORD cs, WORD ip, WORD ax)
{
    GLOBALENTRY ge;
    MODULEENTRY me;
    put("FAULT ");
    putdec(no);
    put(" at ");
    puthex(cs, 4);
    putc_(':');
    puthex(ip, 4);
    put(" ax=");
    puthex(ax, 4);
    ge.dwSize = sizeof ge;
    me.dwSize = sizeof me;
    if (GlobalEntryHandle(&ge, (HGLOBAL)cs)) {
        if (ModuleFindHandle(&me, (HMODULE)ge.hOwner)) {
            put(" ");
            put(me.szModule);
            put(" seg ");
            putdec(ge.wData);
        } else {
            put(" owner ");
            puthex((WORD)ge.hOwner, 4);
        }
    }
    flush();
}

void FAR PASCAL __loadds SpyStart(HINSTANCE h, WORD bytes)
{
    if (!fault_hooked)
        fault_hooked = InterruptRegister(NULL, (FARPROC)SPYFAULTHOOK);
    if (h)
        self = h;
    h = self;
    verbose = bytes;
    if (!hk_get)
        hk_get = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)SpyGetMsg, h, NULL);
    if (!hk_call)
        hk_call = SetWindowsHookEx(WH_CALLWNDPROC, (HOOKPROC)SpyCallWnd, h, NULL);
    (void)dump_all;
}

void FAR PASCAL __loadds SpyStop(void)
{
    if (fault_hooked)
        InterruptUnRegister(NULL);
    fault_hooked = FALSE;
    if (hk_get)
        UnhookWindowsHookEx(hk_get);
    if (hk_call)
        UnhookWindowsHookEx(hk_call);
    hk_get = hk_call = NULL;
}

/* SpyTimes(1): the DDE lines start with the milliseconds since this call
   (to see the timing of a conversation; logs to be compared leave it 0) */
void FAR PASCAL __loadds SpyTimes(WORD on)
{
    times = on != 0;
    t0 = GetTickCount();
}

/* the name OC1 should show for a window, as the DDE lines show it */
void FAR PASCAL __loadds SpyWinName(HWND w, char FAR *out, WORD cb)
{
    WORD i;
    lp_ = 0;
    putwin(w);
    for (i = 0; i < lp_ && i + 1 < cb; i++)
        out[i] = line[i];
    out[i] = 0;
    lp_ = 0;
}
