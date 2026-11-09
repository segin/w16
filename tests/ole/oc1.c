/*
 * oc1.c - OC1.EXE: a scripted OLE 1.0 client probe (black-box observation
 * of OLECLI.DLL; ARCHITECTURE.md 16).
 *
 * Runs as the Windows shell (tests/refrun.sh) and executes C:\OC.TXT, one
 * command per line, through the documented OLE 1.0 client API (Windows 3.1
 * SDK; declarations from OpenWatcom's ole.h). Every call is logged with its
 * arguments and the OLESTATUS it returns, every client callback with its
 * notification, and every OLESTREAM Get/Put with its size and first bytes,
 * to C:\OC.LOG through OCSPY.DLL, which also logs the DDE conversation
 * (command "spy"). Objects are numbered 0.. by the script; strings are
 * words or "quoted"; "-" is a NULL string.
 *
 *   spy [bytes]                     start the DDE spy (bytes of data shown)
 *   doc <d> <name>                  OleRegisterClientDoc("OC1", name) -> doc d
 *   use <d>                         later objects go into doc d
 *   revokedoc|saveddoc|revertdoc <d>; renamedoc <d> <name>
 *   create <i> <class> <name> <render> <fmt>      OleCreate (StdFileEditing)
 *   createinv <i> <class> <name> <render> <fmt> <launch>
 *   template <i> <class> <file> <name> <render> <fmt>
 *   fromfile <i> <class> <file> <item> <name> <render> <fmt>
 *   linkfile <i> <class> <file> <item> <name> <render> <fmt>
 *   qclip|qlink <protocol> <render> <fmt>
 *   fromclip|linkclip <i> <protocol> <name> <render> <fmt>
 *   save <i> <file>  /  load <i> <file> <protocol> <name>
 *   activate <i> <verb> <show> <focus>; update|close|delete|release|
 *   reconnect|queryopen|outofdate|querytype|bounds|size|qname|copy|
 *   getlinkopt|lock|unlock|status|enumfmts|unlockserver <i>
 *   setlinkopt <i> <opt>; rename <i> <name>; hostnames <i> <app> <obj>
 *   setbounds <i> l t r b; getdata|requestdata <i> <fmt>
 *   setdata <i> <fmt> <file>        (the data from a host file)
 *   execute <i> <command>; equal <i> <j>; clone|copyfromlink <i> <j>
 *   convert <i> <j> <protocol>; colors <i>; target <i>
 *   draw <i> <file.bmp> <w> <h>; drawmeta <i> <w> <h>
 *   enumobjs <d>; version; isdcmeta
 *   clipdump; clipclear
 *   regset <key> <value>; regget <key>
 *   run <cmd>; wait <ms>; waitrel <i> <ms>; autowait 0|1; retry 0|1
 *   menu <class> <item text>; wmclose <class>; wins
 * Lines starting with ';' are comments.
 */
#include <windows.h>
#include <ole.h>
#include <shellapi.h>

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
void FAR PASCAL SpyLog(const char FAR *s);
void FAR PASCAL SpyStart(HINSTANCE h, WORD bytes);
void FAR PASCAL SpyStop(void);
void FAR PASCAL SpyWinName(HWND w, char FAR *out, WORD cb);
void FAR PASCAL hmemcpy(void __huge *, const void __huge *, long);

#define NOBJ 32
#define NDOC 8

static HINSTANCE inst;
static HWND me;
static LPOLEOBJECT obj[NOBJ];
static LHCLIENTDOC docs[NDOC];
static int cur_doc;
static BOOL autowait = TRUE;
static int retry_answer = TRUE;
static WORD cbcount[NOBJ][8];

/* ---- log ---- */
static char line[700];
static WORD lp_;

static void put(const char FAR *s)
{
    while (s && *s && lp_ < sizeof line - 4)
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

static void putdec(long v)
{
    char b[12];
    int i = 11;
    DWORD u = v < 0 ? -v : v;
    b[11] = 0;
    do {
        DWORD q = u / 10;
        b[--i] = (char)('0' + (WORD)(u - q * 10));
        u = q;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    put(b + i);
}

static void putq(const char FAR *s, WORD n)
{
    WORD i;
    if (!s) {
        put("NULL");
        return;
    }
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

static WORD slen(const char FAR *s)
{
    WORD n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void putqs(const char FAR *s)
{
    putq(s, slen(s));
}

static void flush(void)
{
    line[lp_++] = '\r';
    line[lp_++] = '\n';
    line[lp_] = 0;
    SpyLog(line);
    lp_ = 0;
}

static void say(const char *s)
{
    put(s);
    flush();
}

static DWORD sum(const BYTE __huge *p, DWORD n)
{
    DWORD s = 0, i;
    for (i = 0; i < n; i++)
        s = s * 31 + p[i];
    return s;
}

static const char *const st_names[] = {
    "OLE_OK", "OLE_WAIT_FOR_RELEASE", "OLE_BUSY", "OLE_ERROR_PROTECT_ONLY",
    "OLE_ERROR_MEMORY", "OLE_ERROR_STREAM", "OLE_ERROR_STATIC", "OLE_ERROR_BLANK",
    "OLE_ERROR_DRAW", "OLE_ERROR_METAFILE", "OLE_ERROR_ABORT", "OLE_ERROR_CLIPBOARD",
    "OLE_ERROR_FORMAT", "OLE_ERROR_OBJECT", "OLE_ERROR_OPTION", "OLE_ERROR_PROTOCOL",
    "OLE_ERROR_ADDRESS", "OLE_ERROR_NOT_EQUAL", "OLE_ERROR_HANDLE", "OLE_ERROR_GENERIC",
    "OLE_ERROR_CLASS", "OLE_ERROR_SYNTAX", "OLE_ERROR_DATATYPE", "OLE_ERROR_PALETTE",
    "OLE_ERROR_NOT_LINK", "OLE_ERROR_NOT_EMPTY", "OLE_ERROR_SIZE", "OLE_ERROR_DRIVE",
    "OLE_ERROR_NETWORK", "OLE_ERROR_NAME", "OLE_ERROR_TEMPLATE", "OLE_ERROR_NEW",
    "OLE_ERROR_EDIT", "OLE_ERROR_OPEN", "OLE_ERROR_NOT_OPEN", "OLE_ERROR_LAUNCH",
    "OLE_ERROR_COMM", "OLE_ERROR_TERMINATE", "OLE_ERROR_COMMAND", "OLE_ERROR_SHOW",
    "OLE_ERROR_DOVERB", "OLE_ERROR_ADVISE_NATIVE", "OLE_ERROR_ADVISE_PICT",
    "OLE_ERROR_ADVISE_RENAME", "OLE_ERROR_POKE_NATIVE", "OLE_ERROR_REQUEST_NATIVE",
    "OLE_ERROR_REQUEST_PICT", "OLE_ERROR_SERVER_BLOCKED", "OLE_ERROR_REGISTRATION",
    "OLE_ERROR_ALREADY_REGISTERED", "OLE_ERROR_TASK", "OLE_ERROR_OUTOFDATE",
    "OLE_ERROR_CANT_UPDATE_CLIENT", "OLE_ERROR_UPDATE", "OLE_ERROR_SETDATA_FORMAT",
    "OLE_ERROR_STATIC_FROM_OTHER_OS"
};

static void putst(OLESTATUS s)
{
    if ((WORD)s < sizeof st_names / sizeof st_names[0])
        put(st_names[s]);
    else if (s == OLE_WARN_DELETE_DATA)
        put("OLE_WARN_DELETE_DATA");
    else {
        put("status");
        putdec((WORD)s);
    }
}

static const char *const cb_names[] = {
    "OLE_CHANGED", "OLE_SAVED", "OLE_CLOSED", "OLE_RENAMED", "OLE_QUERY_PAINT",
    "OLE_RELEASE", "OLE_QUERY_RETRY"
};

static const char *const rm_names[] = {
    "OLE_NONE", "OLE_DELETE", "OLE_LNKPASTE", "OLE_EMBPASTE", "OLE_SHOW", "OLE_RUN",
    "OLE_ACTIVATE", "OLE_UPDATE", "OLE_CLOSE", "OLE_RECONNECT", "OLE_SETUPDATEOPTIONS",
    "OLE_SERVERUNLAUNCH", "OLE_LOADFROMSTREAM", "OLE_SETDATA", "OLE_REQUESTDATA",
    "OLE_OTHER", "OLE_CREATE", "OLE_CREATEFROMTEMPLATE", "OLE_CREATELINKFROMFILE",
    "OLE_COPYFROMLNK", "OLE_CREATEFROMFILE", "OLE_CREATEINVISIBLE"
};

static int objidx(LPOLEOBJECT o)
{
    int i;
    if (!o)
        return -2;
    for (i = 0; i < NOBJ; i++)
        if (obj[i] == o)
            return i;
    return -1;
}

static void putfmt(OLECLIPFORMAT f)
{
    char b[64];
    int n;
    switch (f) {
    case 0: put("0"); return;
    case CF_TEXT: put("CF_TEXT"); return;
    case CF_BITMAP: put("CF_BITMAP"); return;
    case CF_METAFILEPICT: put("CF_METAFILEPICT"); return;
    case CF_DIB: put("CF_DIB"); return;
    case CF_PALETTE: put("CF_PALETTE"); return;
    case CF_SYLK: put("CF_SYLK"); return;
    case CF_DIF: put("CF_DIF"); return;
    case CF_TIFF: put("CF_TIFF"); return;
    case CF_OEMTEXT: put("CF_OEMTEXT"); return;
    case CF_PENDATA: put("CF_PENDATA"); return;
    case CF_RIFF: put("CF_RIFF"); return;
    case CF_WAVE: put("CF_WAVE"); return;
    case CF_OWNERDISPLAY: put("CF_OWNERDISPLAY"); return;
    case CF_DSPTEXT: put("CF_DSPTEXT"); return;
    case CF_DSPBITMAP: put("CF_DSPBITMAP"); return;
    case CF_DSPMETAFILEPICT: put("CF_DSPMETAFILEPICT"); return;
    }
    n = GetClipboardFormatName(f, b, sizeof b);
    if (n)
        putq(b, n);
    else {
        put("cf");
        puthex(f, 4);
    }
}

/* ---- client and stream ---- */
typedef struct {
    OLECLIENT c;
    int idx;
} CLIENT;

int FAR PASCAL __loadds ClientCallBack(LPOLECLIENT c, OLE_NOTIFICATION n, LPOLEOBJECT o)
{
    int idx = ((CLIENT FAR *)c)->idx;
    put("CB ");
    putdec(idx);
    put(" ");
    if ((WORD)n < 7) {
        put(cb_names[n]);
        cbcount[idx][n]++;
    } else {
        put("notification");
        putdec(n);
    }
    put(" obj=");
    if (obj[idx] == o)
        put("self");
    else if (!obj[idx]) {
        put("new");                     /* before the creating call returns */
    } else
        putdec(objidx(o));
    flush();
    if (n == OLE_QUERY_RETRY)
        return retry_answer;
    return TRUE;
}

static OLECLIENTVTBL client_vtbl;
static CLIENT clients[NOBJ];

typedef struct {
    OLESTREAM s;
    HGLOBAL h;
    DWORD size, pos, cap;
    WORD calls;
} STREAM;

static OLESTREAMVTBL stream_vtbl;
static STREAM stm;
static int log_puts = 1;

static void putbytes(const BYTE FAR *p, DWORD n, WORD max)
{
    WORD i, m = n < max ? (WORD)n : max;
    for (i = 0; i < m; i++)
        puthex(p[i], 2);
    if (m < n)
        put("..");
}

DWORD FAR PASCAL __loadds StreamGet(LPOLESTREAM s, void FAR *buf, DWORD n)
{
    STREAM FAR *t = (STREAM FAR *)s;
    BYTE __huge *p;
    DWORD got = n;
    if (t->pos + n > t->size)
        got = t->size - t->pos;
    p = (BYTE __huge *)GlobalLock(t->h);
    if (got)
        hmemcpy(buf, p + t->pos, got);
    if (log_puts) {
        put("GET ");
        putdec(n);
        put(" @");
        putdec(t->pos);
        if (got != n) {
            put(" short ");
            putdec(got);
        }
        put(" ");
        putbytes((BYTE FAR *)(p + t->pos), got, 32);
        flush();
    }
    GlobalUnlock(t->h);
    t->pos += got;
    t->calls++;
    return got;
}

DWORD FAR PASCAL __loadds StreamPut(LPOLESTREAM s, const void FAR *buf, DWORD n)
{
    STREAM FAR *t = (STREAM FAR *)s;
    BYTE __huge *p;
    if (log_puts > 1) {
        put("PUT> ");
        putdec(n);
        flush();
    }
    if (t->size + n > t->cap) {
        DWORD c = t->cap ? t->cap : 4096;
        HGLOBAL h;
        while (c < t->size + n)
            c += c;
        h = t->h ? GlobalReAlloc(t->h, c, GMEM_MOVEABLE) : GlobalAlloc(GMEM_MOVEABLE, c);
        if (!h)
            return 0;
        t->h = h;
        t->cap = c;
    }
    p = (BYTE __huge *)GlobalLock(t->h);
    if (n)
        hmemcpy(p + t->size, buf, n);
    GlobalUnlock(t->h);
    if (log_puts) {
        put("PUT ");
        putdec(n);
        put(" @");
        putdec(t->size);
        put(" ");
        putbytes((const BYTE FAR *)buf, n, 32);
        flush();
    }
    t->size += n;
    t->calls++;
    return n;
}

/* ---- message pump ---- */
static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    do {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0);
}

static void waitrel(int i, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    OLESTATUS s;
    if (!obj[i])
        return;
    for (;;) {
        s = OleQueryReleaseStatus(obj[i]);
        if (s != OLE_BUSY)
            break;
        if ((long)(GetTickCount() - end) >= 0) {
            say("  (still busy)");
            return;
        }
        if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        } else
            Yield();
    }
    put("  released ");
    putdec(i);
    put(": status=");
    putst(s);
    put(" error=");
    putst(OleQueryReleaseError(obj[i]));
    {
        OLE_RELEASE_METHOD r = OleQueryReleaseMethod(obj[i]);
        put(" method=");
        if ((WORD)r < sizeof rm_names / sizeof rm_names[0])
            put(rm_names[r]);
        else
            putdec(r);
    }
    flush();
}

static OLESTATUS done(int i, OLESTATUS s)
{
    putst(s);
    flush();
    if (s == OLE_WAIT_FOR_RELEASE && autowait && i >= 0)
        waitrel(i, 60000L);
    return s;
}

/* ---- script parsing ---- */
static char *cur;

static char *word(void)
{
    char *w;
    while (*cur == ' ' || *cur == '\t')
        cur++;
    if (!*cur)
        return "";
    if (*cur == '"') {
        w = ++cur;
        while (*cur && *cur != '"')
            cur++;
    } else {
        w = cur;
        while (*cur && *cur != ' ' && *cur != '\t')
            cur++;
    }
    if (*cur)
        *cur++ = 0;
    return w;
}

static char *rest(void)
{
    while (*cur == ' ' || *cur == '\t')
        cur++;
    return cur;
}

static LPSTR str(void)
{
    char *w = word();
    if (w[0] == '-' && !w[1])
        return NULL;
    return w;
}

static long num(void)
{
    char *s = word();
    long v = 0, base = 10, sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            break;
        v = v * base + d;
    }
    return v * sign;
}

static int same(const char FAR *a, const char FAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* text starts with p (case-insensitive) */
static int prefix_ci(const char FAR *text, const char FAR *p)
{
    while (*p) {
        char x = *text, y = *p;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
        text++;
        p++;
    }
    return 1;
}

static OLEOPT_RENDER render(void)
{
    char *w = word();
    if (same(w, "draw"))
        return olerender_draw;
    if (same(w, "format"))
        return olerender_format;
    return olerender_none;
}

static OLECLIPFORMAT fmt(void)
{
    char *w = word();
    if (same(w, "0") || !*w)
        return 0;
    if (same(w, "CF_TEXT"))
        return CF_TEXT;
    if (same(w, "CF_BITMAP"))
        return CF_BITMAP;
    if (same(w, "CF_METAFILEPICT"))
        return CF_METAFILEPICT;
    if (same(w, "CF_DIB"))
        return CF_DIB;
    if (same(w, "CF_PALETTE"))
        return CF_PALETTE;
    return RegisterClipboardFormat(w);
}

static int idx(void)
{
    long i = num();
    if (i < 0 || i >= NOBJ)
        i = 0;
    return (int)i;
}

static LPOLECLIENT client(int i)
{
    clients[i].c.lpvtbl = &client_vtbl;
    clients[i].idx = i;
    return &clients[i].c;
}

static void putobj(int i)
{
    put(" -> obj ");
    putdec(i);
    put(obj[i] ? " set " : " null ");
}

/* ---- files ---- */
static BOOL write_file(const char *name, HGLOBAL h, DWORD n)
{
    HFILE f = _lcreat(name, 0);
    BYTE __huge *p;
    DWORD off = 0;
    if (f == HFILE_ERROR)
        return FALSE;
    p = (BYTE __huge *)GlobalLock(h);
    while (off < n) {
        WORD c = n - off > 0x4000 ? 0x4000 : (WORD)(n - off);
        _lwrite(f, (LPCSTR)(p + off), c);
        off += c;
    }
    GlobalUnlock(h);
    _lclose(f);
    return TRUE;
}

static HGLOBAL read_file(const char *name, DWORD *size)
{
    HFILE f = _lopen(name, OF_READ);
    HGLOBAL h;
    BYTE __huge *p;
    DWORD n, off = 0;
    if (f == HFILE_ERROR)
        return NULL;
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n ? n : 1);
    if (!h) {
        _lclose(f);
        return NULL;
    }
    p = (BYTE __huge *)GlobalLock(h);
    while (off < n) {
        WORD c = n - off > 0x4000 ? 0x4000 : (WORD)(n - off);
        _lread(f, (LPSTR)(p + off), c);
        off += c;
    }
    GlobalUnlock(h);
    _lclose(f);
    *size = n;
    return h;
}

/* ---- data dumps ---- */
static void dump_mem(HGLOBAL h, WORD max)
{
    DWORD n = GlobalSize(h);
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    if (!p) {
        put("badmem");
        return;
    }
    put("size=");
    putdec(n);
    put(" sum=");
    puthex(sum((BYTE __huge *)p, n), 8);
    put(" ");
    putbytes(p, n, max);
    GlobalUnlock(h);
}

static void dump_handle(OLECLIPFORMAT f, HANDLE h)
{
    if (!h) {
        put("null");
        return;
    }
    if (f == CF_METAFILEPICT) {
        METAFILEPICT FAR *m = (METAFILEPICT FAR *)GlobalLock(h);
        if (!m) {
            put("badmfp");
            return;
        }
        put("mm=");
        putdec(m->mm);
        put(" x=");
        putdec(m->xExt);
        put(" y=");
        putdec(m->yExt);
        put(" mf:");
        dump_mem((HGLOBAL)m->hMF, 32);
        GlobalUnlock(h);
    } else if (f == CF_BITMAP) {
        BITMAP bm;
        DWORD dim;
        GetObject(h, sizeof bm, &bm);
        put("bm ");
        putdec(bm.bmWidth);
        put("x");
        putdec(bm.bmHeight);
        put("x");
        putdec(bm.bmPlanes);
        put("x");
        putdec(bm.bmBitsPixel);
        dim = GetBitmapDimension(h);
        put(" dim=");
        putdec(LOWORD(dim));
        put(",");
        putdec(HIWORD(dim));
        {
            DWORD n = (DWORD)bm.bmWidthBytes * bm.bmHeight * bm.bmPlanes;
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
            if (g) {
                BYTE __huge *p = (BYTE __huge *)GlobalLock(g);
                GetBitmapBits(h, n, (void FAR *)p);
                put(" sum=");
                puthex(sum(p, n), 8);
                GlobalUnlock(g);
                GlobalFree(g);
            }
        }
    } else if (f == CF_PALETTE) {
        WORD n = 0;
        GetObject(h, sizeof n, &n);
        put("palette entries=");
        putdec(n);
    } else
        dump_mem(h, 96);
}

static void clipdump(void)
{
    UINT f = 0;
    if (!OpenClipboard(me)) {
        say("clipdump: cannot open");
        return;
    }
    say("clipboard:");
    while ((f = EnumClipboardFormats(f)) != 0) {
        put("  ");
        putfmt(f);
        put(": ");
        dump_handle(f, GetClipboardData(f));
        flush();
    }
    CloseClipboard();
}

/* ---- drawing ---- */
static void save_dib(HDC dc, HBITMAP bm, int cx, int cy, const char *file)
{
    struct {
        BITMAPINFOHEADER h;
        RGBQUAD pal[16];
    } bmi;
    BITMAPFILEHEADER fh;
    DWORD stride = ((cx * 4 + 31) / 32) * 4, n = stride * cy;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    BYTE __huge *p;
    HFILE f;
    if (!g)
        return;
    p = (BYTE __huge *)GlobalLock(g);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = BI_RGB;
    bmi.h.biSizeImage = n;
    bmi.h.biXPelsPerMeter = bmi.h.biYPelsPerMeter = 0;
    bmi.h.biClrUsed = bmi.h.biClrImportant = 0;
    GetDIBits(dc, bm, 0, cy, (void FAR *)p, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
    put(" pixels sum=");
    puthex(sum(p, n), 8);
    f = _lcreat(file, 0);
    if (f != HFILE_ERROR) {
        DWORD off = 0;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof fh + sizeof bmi;
        fh.bfSize = fh.bfOffBits + n;
        fh.bfReserved1 = fh.bfReserved2 = 0;
        _lwrite(f, (LPCSTR)&fh, sizeof fh);
        _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
        while (off < n) {
            WORD c = n - off > 0x4000 ? 0x4000 : (WORD)(n - off);
            _lwrite(f, (LPCSTR)(p + off), c);
            off += c;
        }
        _lclose(f);
    }
    GlobalUnlock(g);
    GlobalFree(g);
}

static void draw(int i, const char *file, int cx, int cy)
{
    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    RECT r;
    OLESTATUS s;
    old = SelectObject(mem, bm);
    PatBlt(mem, 0, 0, cx, cy, WHITENESS);
    SetRect(&r, 0, 0, cx, cy);
    s = OleDraw(obj[i], mem, &r, NULL, NULL);
    SelectObject(mem, old);
    put("OleDraw ");
    putdec(i);
    put(" = ");
    putst(s);
    save_dib(scr, bm, cx, cy, file);
    flush();
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

static void drawmeta(int i, int cx, int cy)
{
    HDC mdc = CreateMetaFile(NULL);
    HMETAFILE mf;
    RECT r;
    OLESTATUS s;
    put("OleIsDcMeta(meta) = ");
    putdec(OleIsDcMeta(mdc));
    flush();
    SetWindowOrg(mdc, 0, 0);
    SetWindowExt(mdc, cx, cy);
    SetRect(&r, 0, 0, cx, cy);
    s = OleDraw(obj[i], mdc, &r, NULL, NULL);
    mf = CloseMetaFile(mdc);
    put("OleDraw(meta) ");
    putdec(i);
    put(" = ");
    putst(s);
    put(" mf:");
    dump_mem((HGLOBAL)mf, 64);
    flush();
    DeleteMetaFile(mf);
}

/* ---- windows ---- */
static int menu_find(HMENU m, const char *text)
{
    int n = GetMenuItemCount(m), k;
    for (k = 0; k < n; k++) {
        char b[80], c[80];
        int i, j = 0;
        HMENU sub = GetSubMenu(m, k);
        if (sub) {
            int r = menu_find(sub, text);
            if (r != -1)
                return r;
            continue;
        }
        GetMenuString(m, k, b, sizeof b, MF_BYPOSITION);
        for (i = 0; b[i] && b[i] != '\t'; i++)
            if (b[i] != '&')
                c[j++] = b[i];
        c[j] = 0;
        if (prefix_ci(c, text))
            return GetMenuItemID(m, k);
    }
    return -1;
}

static HWND find_top(const char *cls)
{
    return FindWindow(cls, NULL);
}

BOOL CALLBACK __export __loadds EnumWin(HWND w, LPARAM lp)
{
    char b[128];
    (void)lp;
    put("  ");
    SpyWinName(w, b, sizeof b);
    put(b);
    put(IsWindowVisible(w) ? " visible " : " hidden ");
    GetWindowText(w, b, sizeof b);
    putqs(b);
    flush();
    return TRUE;
}

/* ---- the window OC1 owns ---- */
LRESULT CALLBACK __export __loadds OcWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, msg, wp, lp);
}

static void cmd(char *l)
{
    char *c;
    int i, j;
    OLESTATUS s;
    for (c = l; *c == ' ' || *c == '\t'; c++)
        ;
    if (!*c || *c == ';')
        return;
    put("> ");
    put(c);
    flush();
    cur = c;
    c = word();

    if (same(c, "spy")) {
        long b = num();
        SpyStart(NULL, b ? (WORD)b : 32);
    } else if (same(c, "wait")) {
        pump(num());
    } else if (same(c, "waitrel")) {
        i = idx();
        waitrel(i, num());
    } else if (same(c, "autowait")) {
        autowait = (BOOL)num();
    } else if (same(c, "retry")) {
        retry_answer = (int)num();
    } else if (same(c, "puts")) {
        log_puts = (int)num();
    } else if (same(c, "run")) {
        put("WinExec = ");
        putdec(WinExec(rest(), SW_SHOWNORMAL));
        flush();
    } else if (same(c, "version")) {
        DWORD v = OleQueryClientVersion();
        put("OleQueryClientVersion = ");
        puthex(v, 8);
        flush();
    } else if (same(c, "isdcmeta")) {
        HDC d = GetDC(NULL);
        put("OleIsDcMeta(screen) = ");
        putdec(OleIsDcMeta(d));
        flush();
        ReleaseDC(NULL, d);
    } else if (same(c, "doc")) {
        int d = (int)num();
        LPSTR name = str();
        put("OleRegisterClientDoc = ");
        s = OleRegisterClientDoc("OC1", name, 0, &docs[d]);
        putst(s);
        put(" lh=");
        puthex(docs[d], 8);
        flush();
        cur_doc = d;
    } else if (same(c, "use")) {
        cur_doc = (int)num();
    } else if (same(c, "revokedoc")) {
        int d = (int)num();
        put("OleRevokeClientDoc = ");
        done(-1, OleRevokeClientDoc(docs[d]));
    } else if (same(c, "saveddoc")) {
        int d = (int)num();
        put("OleSavedClientDoc = ");
        done(-1, OleSavedClientDoc(docs[d]));
    } else if (same(c, "revertdoc")) {
        int d = (int)num();
        put("OleRevertClientDoc = ");
        done(-1, OleRevertClientDoc(docs[d]));
    } else if (same(c, "renamedoc")) {
        int d = (int)num();
        put("OleRenameClientDoc = ");
        done(-1, OleRenameClientDoc(docs[d], str()));
    } else if (same(c, "enumobjs")) {
        int d = (int)num();
        LPOLEOBJECT o = NULL;
        put("OleEnumObjects:");
        while ((s = OleEnumObjects(docs[d], &o)) == OLE_OK && o) {
            put(" ");
            putdec(objidx(o));
        }
        put(" end ");
        putst(s);
        flush();
    } else if (same(c, "create") || same(c, "createinv")) {
        BOOL inv = same(c, "createinv");
        LPSTR cls, name;
        OLEOPT_RENDER r;
        OLECLIPFORMAT f;
        i = idx();
        cls = str();
        name = str();
        r = render();
        f = fmt();
        obj[i] = NULL;
        if (inv) {
            BOOL launch = (BOOL)num();
            put("OleCreateInvisible = ");
            s = OleCreateInvisible("StdFileEditing", client(i), cls, docs[cur_doc], name,
                                   &obj[i], r, f, launch);
        } else {
            put("OleCreate = ");
            s = OleCreate("StdFileEditing", client(i), cls, docs[cur_doc], name, &obj[i], r, f);
        }
        putobj(i);
        done(i, s);
    } else if (same(c, "template")) {
        LPSTR cls, file, name;
        OLEOPT_RENDER r;
        OLECLIPFORMAT f;
        i = idx();
        cls = str();
        file = str();
        name = str();
        r = render();
        f = fmt();
        obj[i] = NULL;
        put("OleCreateFromTemplate = ");
        s = OleCreateFromTemplate("StdFileEditing", client(i), file, docs[cur_doc], name,
                                  &obj[i], r, f);
        (void)cls;
        putobj(i);
        done(i, s);
    } else if (same(c, "fromfile") || same(c, "linkfile")) {
        BOOL link = same(c, "linkfile");
        LPSTR cls, file, item, name;
        OLEOPT_RENDER r;
        OLECLIPFORMAT f;
        i = idx();
        cls = str();
        file = str();
        item = str();
        name = str();
        r = render();
        f = fmt();
        obj[i] = NULL;
        if (link) {
            put("OleCreateLinkFromFile = ");
            s = OleCreateLinkFromFile("StdFileEditing", client(i), cls, file, item,
                                      docs[cur_doc], name, &obj[i], r, f);
        } else {
            put("OleCreateFromFile = ");
            s = OleCreateFromFile("StdFileEditing", client(i), cls, file, docs[cur_doc],
                                  name, &obj[i], r, f);
            (void)item;
        }
        putobj(i);
        done(i, s);
    } else if (same(c, "qclip") || same(c, "qlink")) {
        LPSTR prot = str();
        OLEOPT_RENDER r = render();
        OLECLIPFORMAT f = fmt();
        if (same(c, "qclip")) {
            put("OleQueryCreateFromClip = ");
            s = OleQueryCreateFromClip(prot, r, f);
        } else {
            put("OleQueryLinkFromClip = ");
            s = OleQueryLinkFromClip(prot, r, f);
        }
        done(-1, s);
    } else if (same(c, "fromclip") || same(c, "linkclip")) {
        LPSTR prot, name;
        OLEOPT_RENDER r;
        OLECLIPFORMAT f;
        i = idx();
        prot = str();
        name = str();
        r = render();
        f = fmt();
        obj[i] = NULL;
        if (!OpenClipboard(me)) {
            say("cannot open clipboard");
            return;
        }
        if (same(c, "fromclip")) {
            put("OleCreateFromClip = ");
            s = OleCreateFromClip(prot, client(i), docs[cur_doc], name, &obj[i], r, f);
        } else {
            put("OleCreateLinkFromClip = ");
            s = OleCreateLinkFromClip(prot, client(i), docs[cur_doc], name, &obj[i], r, f);
        }
        CloseClipboard();
        putobj(i);
        done(i, s);
    } else if (same(c, "copy")) {
        i = idx();
        if (!OpenClipboard(me)) {
            say("cannot open clipboard");
            return;
        }
        EmptyClipboard();
        put("OleCopyToClipboard = ");
        s = OleCopyToClipboard(obj[i]);
        CloseClipboard();
        done(i, s);
    } else if (same(c, "clipdump")) {
        clipdump();
    } else if (same(c, "clipclear")) {
        if (OpenClipboard(me)) {
            EmptyClipboard();
            CloseClipboard();
        }
    } else if (same(c, "save")) {
        char *file;
        i = idx();
        file = word();
        if (stm.h)
            GlobalFree(stm.h);
        stm.h = NULL;
        stm.size = stm.pos = stm.cap = 0;
        stm.calls = 0;
        stm.s.lpstbl = &stream_vtbl;
        if (log_puts > 1) {
            stm.s.lpstbl->Put(&stm.s, "test", 4);
            stm.size = 0;
            say("selftest done");
        }
        put("OleSaveToStream = ");
        s = OleSaveToStream(obj[i], &stm.s);
        putst(s);
        put(" bytes=");
        putdec(stm.size);
        put(" puts=");
        putdec(stm.calls);
        if (stm.h) {
            BYTE __huge *p = (BYTE __huge *)GlobalLock(stm.h);
            put(" sum=");
            puthex(sum(p, stm.size), 8);
            GlobalUnlock(stm.h);
            write_file(file, stm.h, stm.size);
        }
        flush();
    } else if (same(c, "load")) {
        char *file;
        LPSTR prot, name;
        DWORD n = 0;
        i = idx();
        file = word();
        prot = str();
        name = str();
        if (stm.h)
            GlobalFree(stm.h);
        stm.h = read_file(file, &n);
        stm.size = n;
        stm.pos = 0;
        stm.cap = n;
        stm.calls = 0;
        stm.s.lpstbl = &stream_vtbl;
        obj[i] = NULL;
        put("OleLoadFromStream = ");
        s = OleLoadFromStream(&stm.s, prot, client(i), docs[cur_doc], name, &obj[i]);
        putobj(i);
        put("read=");
        putdec(stm.pos);
        put(" of ");
        putdec(n);
        put(" ");
        done(i, s);
    } else if (same(c, "activate")) {
        WORD verb;
        BOOL show, focus;
        RECT r;
        i = idx();
        verb = (WORD)num();
        show = (BOOL)num();
        focus = (BOOL)num();
        SetRect(&r, 10, 10, 110, 110);
        put("OleActivate = ");
        done(i, OleActivate(obj[i], verb, show, focus, me, &r));
    } else if (same(c, "update")) {
        i = idx();
        put("OleUpdate = ");
        done(i, OleUpdate(obj[i]));
    } else if (same(c, "close")) {
        i = idx();
        put("OleClose = ");
        done(i, OleClose(obj[i]));
    } else if (same(c, "reconnect")) {
        i = idx();
        put("OleReconnect = ");
        done(i, OleReconnect(obj[i]));
    } else if (same(c, "delete") || same(c, "release")) {
        LPOLEOBJECT o;
        i = idx();
        o = obj[i];
        if (same(c, "delete")) {
            put("OleDelete = ");
            s = OleDelete(o);
        } else {
            put("OleRelease = ");
            s = OleRelease(o);
        }
        putst(s);
        flush();
        if (s == OLE_WAIT_FOR_RELEASE && autowait) {
            /* the object goes away once released: wait for the callback */
            DWORD end = GetTickCount() + 60000L;
            WORD before = cbcount[i][OLE_RELEASE] - 1;
            MSG m;
            (void)before;
            while ((long)(GetTickCount() - end) < 0 &&
                   OleQueryReleaseStatus(o) == OLE_BUSY) {
                if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&m);
                    DispatchMessage(&m);
                } else
                    Yield();
            }
            say("  (released)");
        }
        obj[i] = NULL;
    } else if (same(c, "queryopen")) {
        i = idx();
        put("OleQueryOpen = ");
        done(-1, OleQueryOpen(obj[i]));
    } else if (same(c, "outofdate")) {
        i = idx();
        put("OleQueryOutOfDate = ");
        done(-1, OleQueryOutOfDate(obj[i]));
    } else if (same(c, "status")) {
        i = idx();
        put("OleQueryReleaseStatus = ");
        putst(OleQueryReleaseStatus(obj[i]));
        put(" error=");
        putst(OleQueryReleaseError(obj[i]));
        put(" method=");
        putdec(OleQueryReleaseMethod(obj[i]));
        flush();
    } else if (same(c, "querytype")) {
        LONG t = -1;
        i = idx();
        put("OleQueryType = ");
        s = OleQueryType(obj[i], &t);
        putst(s);
        put(" type=");
        putdec(t);
        flush();
    } else if (same(c, "bounds")) {
        RECT r;
        i = idx();
        SetRect(&r, -1, -1, -1, -1);
        put("OleQueryBounds = ");
        s = OleQueryBounds(obj[i], &r);
        putst(s);
        put(" ");
        putdec(r.left);
        put(",");
        putdec(r.top);
        put(",");
        putdec(r.right);
        put(",");
        putdec(r.bottom);
        flush();
    } else if (same(c, "size")) {
        DWORD n = 0xFFFFFFFFL;
        i = idx();
        put("OleQuerySize = ");
        s = OleQuerySize(obj[i], &n);
        putst(s);
        put(" size=");
        putdec(n);
        flush();
    } else if (same(c, "qname")) {
        char b[256];
        UINT n = sizeof b;
        i = idx();
        b[0] = 0;
        put("OleQueryName = ");
        s = OleQueryName(obj[i], b, &n);
        putst(s);
        put(" n=");
        putdec(n);
        put(" ");
        putqs(b);
        flush();
    } else if (same(c, "rename")) {
        i = idx();
        put("OleRename = ");
        done(i, OleRename(obj[i], str()));
    } else if (same(c, "hostnames")) {
        LPSTR a, b;
        i = idx();
        a = str();
        b = str();
        put("OleSetHostNames = ");
        done(i, OleSetHostNames(obj[i], a, b));
    } else if (same(c, "setbounds")) {
        RECT r;
        i = idx();
        r.left = (int)num();
        r.top = (int)num();
        r.right = (int)num();
        r.bottom = (int)num();
        put("OleSetBounds = ");
        done(i, OleSetBounds(obj[i], &r));
    } else if (same(c, "getlinkopt")) {
        OLEOPT_UPDATE u = 99;
        i = idx();
        put("OleGetLinkUpdateOptions = ");
        s = OleGetLinkUpdateOptions(obj[i], &u);
        putst(s);
        put(" opt=");
        putdec(u);
        flush();
    } else if (same(c, "setlinkopt")) {
        i = idx();
        put("OleSetLinkUpdateOptions = ");
        done(i, OleSetLinkUpdateOptions(obj[i], (OLEOPT_UPDATE)num()));
    } else if (same(c, "enumfmts")) {
        OLECLIPFORMAT f = 0;
        i = idx();
        put("OleEnumFormats:");
        while ((f = OleEnumFormats(obj[i], f)) != 0) {
            put(" ");
            putfmt(f);
        }
        flush();
    } else if (same(c, "getdata")) {
        HANDLE h = NULL;
        OLECLIPFORMAT f;
        i = idx();
        f = fmt();
        put("OleGetData ");
        putfmt(f);
        put(" = ");
        s = OleGetData(obj[i], f, &h);
        putst(s);
        put(" ");
        if (s == OLE_OK || s == OLE_WARN_DELETE_DATA)
            dump_handle(f, h);
        flush();
    } else if (same(c, "requestdata")) {
        OLECLIPFORMAT f;
        i = idx();
        f = fmt();
        put("OleRequestData = ");
        done(i, OleRequestData(obj[i], f));
    } else if (same(c, "setdata")) {
        OLECLIPFORMAT f;
        DWORD n;
        HGLOBAL h;
        i = idx();
        f = fmt();
        h = read_file(word(), &n);
        put("OleSetData = ");
        done(i, OleSetData(obj[i], f, h));
    } else if (same(c, "execute")) {
        char *t;
        HGLOBAL h;
        char FAR *p;
        i = idx();
        t = rest();
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, slen(t) + 1);
        p = (char FAR *)GlobalLock(h);
        for (j = 0; t[j]; j++)
            p[j] = t[j];
        p[j] = 0;
        GlobalUnlock(h);
        put("OleExecute = ");
        done(i, OleExecute(obj[i], h, 0));
    } else if (same(c, "equal")) {
        i = idx();
        j = idx();
        put("OleEqual = ");
        done(-1, OleEqual(obj[i], obj[j]));
    } else if (same(c, "clone") || same(c, "copyfromlink")) {
        i = idx();
        j = idx();
        obj[j] = NULL;
        if (same(c, "clone")) {
            put("OleClone = ");
            s = OleClone(obj[i], client(j), docs[cur_doc], "clone", &obj[j]);
        } else {
            put("OleCopyFromLink = ");
            s = OleCopyFromLink(obj[i], "StdFileEditing", client(j), docs[cur_doc], "copy",
                                &obj[j]);
        }
        putobj(j);
        done(j, s);
    } else if (same(c, "convert")) {
        LPSTR prot;
        i = idx();
        j = idx();
        prot = str();
        obj[j] = NULL;
        put("OleObjectConvert = ");
        s = OleObjectConvert(obj[i], prot, client(j), docs[cur_doc], "conv", &obj[j]);
        putobj(j);
        done(j, s);
    } else if (same(c, "qprotocol")) {
        i = idx();
        put("OleQueryProtocol = ");
        done(-1, OleQueryProtocol(obj[i], str()));
    } else if (same(c, "lock")) {
        LHSERVER h = 0;
        i = idx();
        put("OleLockServer = ");
        s = OleLockServer(obj[i], &h);
        putst(s);
        put(" lh=");
        puthex(h, 8);
        flush();
    } else if (same(c, "draw")) {
        char *file;
        int cx, cy;
        i = idx();
        file = word();
        cx = (int)num();
        cy = (int)num();
        draw(i, file, cx, cy);
    } else if (same(c, "drawmeta")) {
        int cx, cy;
        i = idx();
        cx = (int)num();
        cy = (int)num();
        drawmeta(i, cx, cy);
    } else if (same(c, "regset")) {
        LPSTR k = str(), v = rest();
        put("RegSetValue = ");
        putdec(RegSetValue(HKEY_CLASSES_ROOT, k, REG_SZ, v, slen(v)));
        flush();
    } else if (same(c, "regget")) {
        char b[256];
        LONG n = sizeof b;
        LPSTR k = str();
        LONG r;
        b[0] = 0;
        r = RegQueryValue(HKEY_CLASSES_ROOT, k, b, &n);
        put("RegQueryValue = ");
        putdec(r);
        put(" ");
        putqs(b);
        flush();
    } else if (same(c, "menu")) {
        char *cls = word();
        HWND w = find_top(cls);
        int id;
        if (!w) {
            say("menu: no such window");
            return;
        }
        id = menu_find(GetMenu(w), rest());
        put("menu id=");
        putdec(id);
        flush();
        if (id != -1)
            PostMessage(w, WM_COMMAND, id, 0);
    } else if (same(c, "wmclose")) {
        HWND w = find_top(word());
        if (w)
            PostMessage(w, WM_CLOSE, 0, 0);
        else
            say("wmclose: no such window");
    } else if (same(c, "colors")) {
        static struct {
            WORD ver, n;
            PALETTEENTRY e[2];
        } pal = { 0x300, 2, { { 1, 2, 3, 0 }, { 250, 251, 252, 0 } } };
        i = idx();
        put("OleSetColorScheme = ");
        done(i, OleSetColorScheme(obj[i], (LOGPALETTE FAR *)&pal));
    } else if (same(c, "target")) {
        /* an OLETARGETDEVICE: device, driver, port names and an empty
           DEVMODE, as offsets from otdData */
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 64);
        OLETARGETDEVICE FAR *t = (OLETARGETDEVICE FAR *)GlobalLock(h);
        static const char data[] = "PCL\0HPPCL\0LPT1:\0";
        for (j = 0; j < (int)sizeof data; j++)
            t->otdData[j] = data[j];
        t->otdDeviceNameOffset = 0;
        t->otdDriverNameOffset = 4;
        t->otdPortNameOffset = 10;
        t->otdExtDevmodeOffset = 16;
        t->otdExtDevmodeSize = 0;
        t->otdEnvironmentOffset = 16;
        t->otdEnvironmentSize = 0;
        GlobalUnlock(h);
        i = idx();
        put("OleSetTargetDevice = ");
        done(i, OleSetTargetDevice(obj[i], h));
    } else if (same(c, "snap")) {
        HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
        int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN);
        HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old = SelectObject(mem, bm);
        BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        put("snap");
        save_dib(scr, bm, cx, cy, word());
        flush();
        DeleteObject(bm);
        DeleteDC(mem);
        ReleaseDC(NULL, scr);
    } else if (same(c, "post")) {
        HWND w = find_top(word());
        UINT m = (UINT)num();
        WPARAM wp = (WPARAM)num();
        LPARAM lp = num();
        if (w)
            PostMessage(w, m, wp, lp);
        else
            say("post: no such window");
    } else if (same(c, "key")) {
        /* a key press to the focus window (or the active one) */
        HWND w = GetFocus();
        WORD vk = (WORD)num();
        if (!w)
            w = GetActiveWindow();
        PostMessage(w, WM_KEYDOWN, vk, 1);
        PostMessage(w, WM_KEYUP, vk, 0xC0000001L);
    } else if (same(c, "wins")) {
        say("windows:");
        EnumWindows((WNDENUMPROC)EnumWin, 0);
    } else {
        say("?? unknown command");
    }
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HFILE f;
    HGLOBAL hs;
    char FAR *script;
    long n;
    char *l;
    static char buf[1024];
    long pos = 0;

    WaitEvent(0);
    inst = (HINSTANCE)init_regs[5];
    InitApp(inst);
    client_vtbl.CallBack = ClientCallBack;
    stream_vtbl.Get = StreamGet;
    stream_vtbl.Put = (void FAR *)StreamPut;
    wc.style = 0;
    wc.lpfnWndProc = OcWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "OC1Client";
    RegisterClass(&wc);
    me = CreateWindow("OC1Client", "OC1", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL,
                      inst, NULL);
    f = _lopen("C:\\OC.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        say("OC1: no C:\\OC.TXT");
        return 1;
    }
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    hs = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    script = (char FAR *)GlobalLock(hs);
    _lread(f, script, (UINT)n);
    script[n] = 0;
    _lclose(f);
    while (pos < n) {
        int k = 0;
        while (pos < n && script[pos] != '\n' && script[pos] != '\r') {
            if (k < (int)sizeof buf - 1)
                buf[k++] = script[pos];
            pos++;
        }
        while (pos < n && (script[pos] == '\n' || script[pos] == '\r'))
            pos++;
        buf[k] = 0;
        l = buf;
        cmd(l);
    }
    say("OC1 done");
    SpyStop();
    /* as the shell, end the session explicitly: a plain exit of the shell
       task with OLECLI loaded did not end Windows (observed) */
    ExitWindows(0, 0);
    return 0;
}
