/*
 * osspy.c - OSSPY.DLL, the DDE spy and shared log of the OLESVR probes
 * (black-box observation; see osspy.h).
 *
 * Every line goes to C:\OS.LOG, opened, appended to and closed again for
 * each line: the hooks run in the context of whichever task receives the
 * message, and DOS file handles belong to a task. That also keeps the log
 * when Windows hangs or crashes.
 */
#include <windows.h>
#include <dde.h>
#include <stdarg.h>

static const char logname[] = "C:\\OS.LOG";
static HHOOK hk_get, hk_call;
static HINSTANCE self;

typedef struct { LPARAM lParam; WPARAM wParam; UINT message; HWND hwnd; } CWP;

/* ---- output ---- */

static char line[1024];
static WORD pos;

static void put(char c)
{
    if (pos < sizeof line - 3)
        line[pos++] = c;
}

static void puts_(const char FAR *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        put(*s++);
}

static void puthex(DWORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        put("0123456789ABCDEF"[(WORD)(v >> (4 * i)) & 15]);
}

static void putdec(DWORD v, BOOL neg)
{
    /* no C runtime: no 32-bit division, so subtract powers of ten */
    static const DWORD pw[] = { 1000000000L, 100000000L, 10000000L, 1000000L,
                                100000L, 10000L, 1000L, 100L, 10L, 1L };
    int i;
    BOOL any = FALSE;
    if (neg) {
        put('-');
        v = (DWORD)(-(long)v);
    }
    for (i = 0; i < 10; i++) {
        char d = '0';
        while (v >= pw[i]) {
            v -= pw[i];
            d++;
        }
        if (d != '0' || any || i == 9) {
            put(d);
            any = TRUE;
        }
    }
}

static void putesc(const char FAR *s, WORD max)
{
    WORD i;
    put('"');
    for (i = 0; i < max && s[i]; i++) {
        BYTE c = (BYTE)s[i];
        if (c == '\r') {
            put('\\'); put('r');
        } else if (c == '\n') {
            put('\\'); put('n');
        } else if (c < 32 || c >= 127 || c == '"' || c == '\\') {
            put('\\'); put('x'); puthex(c, 2);
        } else
            put((char)c);
    }
    put('"');
}

static void flush(void)
{
    HFILE f;
    line[pos++] = '\r';
    line[pos++] = '\n';
    f = _lopen(logname, OF_WRITE | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    if (f != HFILE_ERROR) {
        _llseek(f, 0, 2);
        _lwrite(f, line, pos);
        _lclose(f);
    }
    pos = 0;
}

/* ---- names ---- */

#define NWIN 256
static HWND wh[NWIN];
static char wname[NWIN][40];
static WORD nwin;

static void winname(HWND h, char *out, int max)
{
    char cls[32], mod[80];
    HINSTANCE hi;
    int i, n = 0, b = 0;
    if (!h || !IsWindow(h)) {
        out[0] = 0;
        return;
    }
    cls[0] = mod[0] = 0;
    GetClassName(h, cls, sizeof cls);
    hi = (HINSTANCE)GetWindowWord(h, GWW_HINSTANCE);
    if (hi)
        GetModuleFileName(hi, mod, sizeof mod);
    for (i = 0; mod[i]; i++)
        if (mod[i] == '\\' || mod[i] == ':')
            b = i + 1;
    for (i = b; mod[i] && mod[i] != '.' && n < max - 2; i++)
        out[n++] = mod[i];
    out[n++] = ':';
    for (i = 0; cls[i] && n < max - 1; i++)
        out[n++] = cls[i];
    out[n] = 0;
}

static void putwin(HWND h)
{
    char nm[40];
    WORD i;
    if (!h) {
        puts_("0");
        return;
    }
    winname(h, nm, sizeof nm);
    if (!nm[0]) {                       /* destroyed: last name it had */
        for (i = nwin; i-- > 0;)
            if (wh[i] == h) {
                puts_(wname[i]);
                put('#');
                putdec(i, FALSE);
                puts_("(gone)");
                return;
            }
        puts_("?");
        puthex((WORD)h, 4);
        return;
    }
    for (i = nwin; i-- > 0;)
        if (wh[i] == h && !lstrcmp(wname[i], nm))
            break;
    if (i == (WORD)-1) {
        if (nwin < NWIN) {
            i = nwin++;
            wh[i] = h;
            lstrcpy(wname[i], nm);
        } else {
            puts_(nm);
            puts_("#?");
            return;
        }
    }
    puts_(nm);
    put('#');
    putdec(i, FALSE);
}

static void putatom(ATOM a)
{
    char b[128];
    if (!a) {
        puts_("0");
        return;
    }
    if (GlobalGetAtomName(a, b, sizeof b))
        putesc(b, sizeof b);
    else {
        puts_("atom?");
        puthex(a, 4);
    }
}

static void putcf(WORD cf)
{
    char b[64];
    switch (cf) {
    case CF_TEXT: puts_("CF_TEXT"); return;
    case CF_BITMAP: puts_("CF_BITMAP"); return;
    case CF_METAFILEPICT: puts_("CF_METAFILEPICT"); return;
    case CF_DIB: puts_("CF_DIB"); return;
    case CF_PALETTE: puts_("CF_PALETTE"); return;
    case 0: puts_("0"); return;
    }
    if (cf >= 0xC000 && GetClipboardFormatName(cf, b, sizeof b)) {
        puts_(b);
        return;
    }
    puts_("cf");
    puthex(cf, 4);
}

static WORD sum16(const BYTE FAR *p, DWORD n)
{
    WORD s = 0;
    while (n--)
        s = (WORD)((s << 1 | s >> 15) + *p++);
    return s;
}

/* a block of n bytes: length, checksum, and the bytes (text where
   printable) up to a limit */
static void putbytes(const BYTE FAR *p, DWORD n)
{
    DWORD i, m = n > 96 ? 96 : n;
    puts_(" len=");
    putdec(n, FALSE);
    puts_(" sum=");
    puthex(sum16(p, n), 4);
    puts_(" [");
    for (i = 0; i < m; i++) {
        BYTE c = p[(WORD)i];
        if (c >= 32 && c < 127 && c != '\\')
            put((char)c);
        else {
            put('\\');
            puthex(c, 2);
        }
    }
    if (m < n)
        puts_("...");
    put(']');
}

static void dumpdata(WORD cf, HGLOBAL h, WORD off)
{
    BYTE FAR *p;
    DWORD sz;
    if (!h) {
        puts_(" data=0");
        return;
    }
    if (cf == CF_BITMAP && !off) {      /* the value is the bitmap */
        BITMAP bm;
        if (GetObject((HBITMAP)h, sizeof bm, (LPSTR)&bm)) {
            puts_(" bitmap=");
            putdec(bm.bmWidth, FALSE);
            put('x');
            putdec(bm.bmHeight, FALSE);
            put('x');
            putdec(bm.bmPlanes * bm.bmBitsPixel, FALSE);
        } else
            puts_(" bitmap=?");
        return;
    }
    sz = GlobalSize(h);
    p = (BYTE FAR *)GlobalLock(h);
    if (!p) {
        puts_(" data=unlockable");
        return;
    }
    puts_(" size=");
    putdec(sz, FALSE);
    if (sz < off) {
        GlobalUnlock(h);
        return;
    }
    if (cf == CF_METAFILEPICT && off && sz >= off + 2) {
        /* in a DDE block the value is the handle of the METAFILEPICT */
        HGLOBAL hp = *(HGLOBAL FAR *)(p + off);
        puts_(" pict");
        GlobalUnlock(h);
        dumpdata(CF_METAFILEPICT, hp, 0);
        return;
    }
    if (cf == CF_METAFILEPICT && sz >= off + 8) {
        METAFILEPICT FAR *mp = (METAFILEPICT FAR *)(p + off);
        puts_(" mm=");
        putdec((DWORD)(long)mp->mm, mp->mm < 0);
        puts_(" ext=");
        putdec((DWORD)(long)mp->xExt, mp->xExt < 0);
        put(',');
        putdec((DWORD)(long)mp->yExt, mp->yExt < 0);
        if (mp->hMF) {
            BYTE FAR *q = (BYTE FAR *)GlobalLock((HGLOBAL)mp->hMF);
            if (q) {
                /* the metafile header gives the real size in words */
                DWORD words = *(DWORD FAR *)(q + 6);
                puts_(" mf");
                putbytes(q, words * 2 < GlobalSize((HGLOBAL)mp->hMF) ? words * 2
                                                     : GlobalSize((HGLOBAL)mp->hMF));
                GlobalUnlock((HGLOBAL)mp->hMF);
            } else
                puts_(" mf=?");
        } else
            puts_(" mf=0");
    } else if (cf == CF_BITMAP && sz >= off + 2) {
        BITMAP bm;
        HBITMAP hb = *(HBITMAP FAR *)(p + off);
        if (hb && GetObject(hb, sizeof bm, (LPSTR)&bm)) {
            puts_(" bitmap=");
            putdec(bm.bmWidth, FALSE);
            put('x');
            putdec(bm.bmHeight, FALSE);
            put('x');
            putdec(bm.bmPlanes * bm.bmBitsPixel, FALSE);
        } else
            puts_(" bitmap=?");
    } else
        putbytes(p + off, sz - off);
    GlobalUnlock(h);
}

/* ---- the hooks ---- */

#define NEXEC 32
static HGLOBAL execs[NEXEC];

static void ddemsg(BOOL sent, HWND to, UINT msg, WPARAM wp, LPARAM lp)
{
    static WPARAM last_iw;
    static LPARAM last_il;
    WORD lo = LOWORD(lp), hi = HIWORD(lp);
    int i;
    if (msg == WM_DDE_INITIATE) {
        if (wp == last_iw && lp == last_il)
            return;                     /* broadcast: once */
        last_iw = wp;
        last_il = lp;
    } else if (!sent)
        last_iw = 0;
    puts_(sent ? "S " : "P ");
    putwin((HWND)wp);
    puts_(" -> ");
    if (msg == WM_DDE_INITIATE)
        puts_("*");
    else
        putwin(to);
    put(' ');
    switch (msg) {
    case WM_DDE_INITIATE:
        puts_("INITIATE app=");
        putatom(lo);
        puts_(" topic=");
        putatom(hi);
        break;
    case WM_DDE_TERMINATE:
        puts_("TERMINATE");
        break;
    case WM_DDE_ADVISE: {
        WORD FAR *o = lo ? (WORD FAR *)GlobalLock((HGLOBAL)lo) : NULL;
        puts_("ADVISE item=");
        putatom(hi);
        if (o) {
            puts_(" flags=");
            puthex(o[0], 4);
            puts_(" fmt=");
            putcf(o[1]);
            GlobalUnlock((HGLOBAL)lo);
        } else
            puts_(" options=0");
        break;
    }
    case WM_DDE_UNADVISE:
        puts_("UNADVISE item=");
        putatom(hi);
        puts_(" fmt=");
        putcf(lo);
        break;
    case WM_DDE_ACK:
        if (sent) {
            puts_("ACK(initiate) app=");
            putatom(lo);
            puts_(" topic=");
            putatom(hi);
            break;
        }
        puts_("ACK status=");
        puthex(lo, 4);
        for (i = 0; i < NEXEC; i++)
            if (execs[i] && execs[i] == (HGLOBAL)hi)
                break;
        if (i < NEXEC) {
            char FAR *c = (char FAR *)GlobalLock((HGLOBAL)hi);
            execs[i] = 0;
            puts_(" cmds=");
            if (c) {
                putesc(c, 200);
                GlobalUnlock((HGLOBAL)hi);
            } else
                puts_("?");
        } else {
            puts_(" item=");
            putatom(hi);
        }
        break;
    case WM_DDE_DATA:
    case WM_DDE_POKE: {
        WORD FAR *d = lo ? (WORD FAR *)GlobalLock((HGLOBAL)lo) : NULL;
        puts_(msg == WM_DDE_DATA ? "DATA item=" : "POKE item=");
        putatom(hi);
        if (d) {
            WORD flags = d[0], cf = d[1];
            GlobalUnlock((HGLOBAL)lo);
            puts_(" flags=");
            puthex(flags, 4);
            puts_(" fmt=");
            putcf(cf);
            dumpdata(cf, (HGLOBAL)lo, 4);
        } else
            puts_(" data=0");
        break;
    }
    case WM_DDE_REQUEST:
        puts_("REQUEST item=");
        putatom(hi);
        puts_(" fmt=");
        putcf(lo);
        break;
    case WM_DDE_EXECUTE: {
        char FAR *c = hi ? (char FAR *)GlobalLock((HGLOBAL)hi) : NULL;
        puts_("EXECUTE ");
        if (c) {
            putesc(c, 400);
            GlobalUnlock((HGLOBAL)hi);
        } else
            puts_("cmds=0");
        for (i = 0; i < NEXEC; i++)
            if (!execs[i])
                break;
        if (i < NEXEC)
            execs[i] = (HGLOBAL)hi;
        break;
    }
    }
    flush();
}

LRESULT FAR PASCAL __loadds OsGetMsgHook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && lp) {
        MSG FAR *m = (MSG FAR *)lp;
        if (m->message >= WM_DDE_FIRST && m->message <= WM_DDE_LAST)
            ddemsg(FALSE, m->hwnd, m->message, m->wParam, m->lParam);
    }
    return CallNextHookEx(hk_get, code, wp, lp);
}

LRESULT FAR PASCAL __loadds OsCallWndHook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && lp) {
        CWP FAR *c = (CWP FAR *)lp;
        if (c->message == WM_DDE_INITIATE ||
            (c->message == WM_DDE_ACK))
            ddemsg(TRUE, c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(hk_call, code, wp, lp);
}

/* ---- exports ---- */

void FAR PASCAL __loadds OsSpyStart(void)
{
    if (!hk_get)
        hk_get = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)OsGetMsgHook, self, NULL);
    if (!hk_call)
        hk_call = SetWindowsHookEx(WH_CALLWNDPROC, (HOOKPROC)OsCallWndHook, self, NULL);
}

void FAR PASCAL __loadds OsSpyStop(void)
{
    if (hk_get)
        UnhookWindowsHookEx(hk_get);
    if (hk_call)
        UnhookWindowsHookEx(hk_call);
    hk_get = hk_call = NULL;
}

void FAR PASCAL __loadds OsSetQuiet(BOOL q)
{
    (void)q;
}

void FAR PASCAL __loadds OsDumpData(WORD cf, HGLOBAL h, WORD off)
{
    puts_("  data fmt=");
    putcf(cf);
    dumpdata(cf, h, off);
    flush();
}

void FAR __cdecl __loadds OsPrintf(const char FAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        BOOL lng = FALSE;
        if (*fmt != '%') {
            if (*fmt == '\n')
                flush();
            else
                put(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == 'l') {
            lng = TRUE;
            fmt++;
        }
        switch (*fmt) {
        case 's': puts_(va_arg(ap, const char FAR *)); break;
        case 'q': {
            const char FAR *s = va_arg(ap, const char FAR *);
            if (s)
                putesc(s, 400);
            else
                puts_("NULL");
            break;
        }
        case 'd':
            if (lng) {
                long v = va_arg(ap, long);
                putdec((DWORD)v, v < 0);
            } else {
                int v = va_arg(ap, int);
                putdec((DWORD)(long)v, v < 0);
            }
            break;
        case 'u': putdec(lng ? va_arg(ap, DWORD) : va_arg(ap, WORD), FALSE); break;
        case 'x':
            if (lng)
                puthex(va_arg(ap, DWORD), 8);
            else
                puthex(va_arg(ap, WORD), 4);
            break;
        case 'c': put((char)va_arg(ap, int)); break;
        case 'a': putatom((ATOM)va_arg(ap, WORD)); break;
        case 'w': putwin((HWND)va_arg(ap, WORD)); break;
        case '%': put('%'); break;
        default: put('%'); put(*fmt); break;
        }
    }
    va_end(ap);
    if (pos)
        flush();
}

int FAR __cdecl libmain(void)
{
    extern WORD __cdecl lib_regs[12];
    self = (HINSTANCE)lib_regs[5];      /* DI = hInstance */
    return 1;
}

int FAR PASCAL __loadds WEP(int n)
{
    (void)n;
    OsSpyStop();
    return 1;
}
