/*
 * os2.c - OS2.EXE, the driver and client of the OLESVR probes (black-box
 * observation).
 *
 * Runs as the Windows shell (tests/refrun.sh), starts the DDE spy
 * (OSSPY.DLL, which logs to C:\OS.LOG) and executes C:\OS2.TXT, one
 * command per line. It is an OLE client through OLECLI (whichever
 * OLECLI.DLL is installed) and a raw DDE client, so a server's OLESVR
 * can be driven both the documented way and with messages OLECLI would
 * never send.
 *
 * Driver:   run <cmdline> | wait <ms> | reg <key>=<value> | regdump <key>
 *           | waitgone <module> (until no task of that module runs)
 * OLECLI:   doc <name> | create <class> | createinv <class> |
 *           template <file> | embfile <class> <file> |
 *           linkfile <class> <file> [item] | activate <verb> |
 *           update | close | delete | release | reconnect | query |
 *           setdata <format> <text> | request <format> | getdata <format> |
 *           hostnames <client> <doc> | bounds <l> <t> <r> <b> |
 *           execute <commands> | savestream | loadstream | colors |
 *           target | rename <name> | lock <class> | unlock | revokedoc |
 *           savedoc | obj <n> (select the n-th object, from 0)
 * Raw DDE:  init <app> <topic> | exec <commands> | req <item> <format> |
 *           poke <item> <format> <text> | advise <item> <format> [flags] |
 *           unadvise <item> <format> | term | ackdata on|off|busy
 * Formats are names ("Native", "OwnerLink", ...) or numbers (1 = CF_TEXT).
 */
#include <windows.h>
#include <dde.h>
#include <ole.h>
#include <shellapi.h>
#include "osspy.h"

#define P OsPrintf

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE inst;
static HWND me;

static const char *stname(int s)
{
    static const char *n[] = {
        "OK", "WAIT_FOR_RELEASE", "BUSY", "ERROR_PROTECT_ONLY", "ERROR_MEMORY",
        "ERROR_STREAM", "ERROR_STATIC", "ERROR_BLANK", "ERROR_DRAW", "ERROR_METAFILE",
        "ERROR_ABORT", "ERROR_CLIPBOARD", "ERROR_FORMAT", "ERROR_OBJECT", "ERROR_OPTION",
        "ERROR_PROTOCOL", "ERROR_ADDRESS", "ERROR_NOT_EQUAL", "ERROR_HANDLE",
        "ERROR_GENERIC", "ERROR_CLASS", "ERROR_SYNTAX", "ERROR_DATATYPE", "ERROR_PALETTE",
        "ERROR_NOT_LINK", "ERROR_NOT_EMPTY", "ERROR_SIZE", "ERROR_DRIVE", "ERROR_NETWORK",
        "ERROR_NAME", "ERROR_TEMPLATE", "ERROR_NEW", "ERROR_EDIT", "ERROR_OPEN",
        "ERROR_NOT_OPEN", "ERROR_LAUNCH", "ERROR_COMM", "ERROR_TERMINATE",
        "ERROR_COMMAND", "ERROR_SHOW", "ERROR_DOVERB", "ERROR_ADVISE_NATIVE",
        "ERROR_ADVISE_PICT", "ERROR_ADVISE_RENAME", "ERROR_POKE_NATIVE",
        "ERROR_REQUEST_NATIVE", "ERROR_REQUEST_PICT", "ERROR_SERVER_BLOCKED",
        "ERROR_REGISTRATION", "ERROR_ALREADY_REGISTERED", "ERROR_TASK",
        "ERROR_OUTOFDATE", "ERROR_CANT_UPDATE_CLIENT", "ERROR_UPDATE",
        "ERROR_SETDATA_FORMAT", "ERROR_STATIC_FROM_OTHER_OS"
    };
    if (s >= 0 && s < (int)(sizeof n / sizeof n[0]))
        return n[s];
    return "?";
}

static void pump(DWORD ms, BOOL *until)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    while ((long)(GetTickCount() - end) < 0 && !(until && *until)) {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    }
}

static WORD num(const char **s)
{
    WORD v = 0;
    while (**s == ' ')
        (*s)++;
    while (**s >= '0' && **s <= '9')
        v = v * 10 + (*(*s)++ - '0');
    return v;
}

static char *word(char **s)
{
    char *w;
    while (**s == ' ')
        (*s)++;
    w = *s;
    while (**s && **s != ' ')
        (*s)++;
    if (**s)
        *(*s)++ = 0;
    return w;
}

static WORD format(const char *s)
{
    if (*s >= '0' && *s <= '9')
        return num(&s);
    return RegisterClipboardFormat(s);
}

/* ---- the OLE client ---- */

#define NOBJ 16
static LPOLEOBJECT objs[NOBJ];
static int nobj, cur;
static LHCLIENTDOC lhdoc;
static LHSERVER locked;
static OLECLIENT client;
static OLECLIENTVTBL clientvt;
static int released;

int CALLBACK __export ClientCallBack(LPOLECLIENT c, OLE_NOTIFICATION n, LPOLEOBJECT o)
{
    static const char *names[] = {
        "CHANGED", "SAVED", "CLOSED", "RENAMED", "QUERY_PAINT", "RELEASE", "QUERY_RETRY"
    };
    int i;
    for (i = 0; i < nobj; i++)
        if (objs[i] == o)
            break;
    if (n == OLE_QUERY_PAINT || n == OLE_QUERY_RETRY)
        return TRUE;
    P("CLI CallBack obj=%d %s", i, (LPCSTR)(n <= 6 ? names[n] : "?"));
    if (n == OLE_RELEASE) {
        released++;
        P("    release status=%d error=%d method=%d", OleQueryReleaseStatus(o),
          OleQueryReleaseError(o), OleQueryReleaseMethod(o));
    }
    (void)c;
    return TRUE;
}

static OLESTATUS waitrel(OLESTATUS r)
{
    int n = 0;
    if (r == OLE_WAIT_FOR_RELEASE && cur < nobj && objs[cur]) {
        while (OleQueryReleaseStatus(objs[cur]) == OLE_BUSY && n++ < 300)
            pump(50, NULL);
        r = OleQueryReleaseError(objs[cur]);
        P("    released -> %d %s", r, (LPCSTR)stname(r));
    }
    return r;
}

static void res(const char *what, OLESTATUS r)
{
    P("CLI %s -> %d %s", (LPCSTR)what, r, (LPCSTR)stname(r));
    waitrel(r);
}

static LPOLEOBJECT FAR *newobj(void)
{
    if (nobj < NOBJ) {
        cur = nobj++;
        objs[cur] = NULL;
        return &objs[cur];
    }
    return NULL;
}

static char objname[16];
static const char *nextname(void)
{
    objname[0] = 'O';
    objname[1] = 'b';
    objname[2] = 'j';
    objname[3] = (char)('0' + nobj);
    objname[4] = 0;
    return objname;
}

/* ---- memory stream ---- */

static HGLOBAL sbuf;
static DWORD slen, spos;

DWORD CALLBACK __export StreamGet(LPOLESTREAM s, void FAR *p, DWORD n)
{
    BYTE FAR *b = GlobalLock(sbuf);
    DWORD i;
    for (i = 0; i < n && spos < slen; i++)
        ((BYTE FAR *)p)[(WORD)i] = b[(WORD)spos++];
    GlobalUnlock(sbuf);
    (void)s;
    return i;
}

DWORD CALLBACK __export StreamPut(LPOLESTREAM s, OLE_CONST void FAR *p, DWORD n)
{
    BYTE FAR *b;
    DWORD i;
    if (slen + n > GlobalSize(sbuf))
        sbuf = GlobalReAlloc(sbuf, slen + n + 1024, GMEM_MOVEABLE);
    b = GlobalLock(sbuf);
    for (i = 0; i < n; i++)
        b[(WORD)slen++] = ((const BYTE FAR *)p)[(WORD)i];
    GlobalUnlock(sbuf);
    (void)s;
    return n;
}

static OLESTREAM stream;
static OLESTREAMVTBL streamvt;

/* ---- raw DDE ---- */

static HWND server;
static BOOL got;
static int ackmode;                     /* 0 ack, 1 no ack, 2 busy */

LRESULT CALLBACK __export ClientWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DDE_ACK:
        if (InSendMessage()) {          /* answer to INITIATE */
            if (!server)
                server = (HWND)wp;
            else
                PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0);
            GlobalDeleteAtom(LOWORD(lp));
            GlobalDeleteAtom(HIWORD(lp));
            return 0;
        }
        got = TRUE;
        return 0;                       /* atoms and blocks: leaked (probe) */
    case WM_DDE_DATA: {
        HGLOBAL h = (HGLOBAL)LOWORD(lp);
        WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
        WORD flags = p ? p[0] : 0;
        if (p)
            GlobalUnlock(h);
        if (flags & 0x8000)
            PostMessage((HWND)wp, WM_DDE_ACK, (WPARAM)w,
                        MAKELPARAM(ackmode == 2 ? 0x4000 : ackmode == 1 ? 0 : 0x8000, HIWORD(lp)));
        if (h && (flags & 0x2000) && ackmode == 0)
            GlobalFree(h);
        got = TRUE;
        return 0;
    }
    case WM_DDE_TERMINATE:
        if ((HWND)wp == server) {
            PostMessage(server, WM_DDE_TERMINATE, (WPARAM)w, 0);
            server = NULL;
        }
        got = TRUE;
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static HGLOBAL ddeblock(WORD flags, WORD cf, const char *text)
{
    WORD n = lstrlen(text) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 4);
    WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
    if (!p)
        return NULL;
    p[0] = flags;
    p[1] = cf;
    lstrcpy((LPSTR)(p + 2), text);
    GlobalUnlock(h);
    return h;
}

static void post_wait(UINT msg, LPARAM lp)
{
    got = FALSE;
    if (!server || !PostMessage(server, msg, (WPARAM)me, lp)) {
        P("RAW post failed");
        return;
    }
    pump(4000, &got);
    pump(200, NULL);
}

/* ---- registration database ---- */

static void regdump(HKEY k, char *path, int depth)
{
    char name[128], val[256];
    DWORD i;
    LONG n;
    HKEY sub;
    n = sizeof val;
    val[0] = 0;
    if (RegQueryValue(k, NULL, val, &n) == ERROR_SUCCESS && val[0])
        P("REG %s = %q", (LPCSTR)path, (LPCSTR)val);
    if (depth > 8)
        return;
    for (i = 0; RegEnumKey(k, i, name, sizeof name) == ERROR_SUCCESS; i++) {
        int l = lstrlen(path);
        if (RegOpenKey(k, name, &sub) != ERROR_SUCCESS)
            continue;
        if (l)
            lstrcat(path, "\\");
        lstrcat(path, name);
        regdump(sub, path, depth + 1);
        path[l] = 0;
        RegCloseKey(sub);
    }
}

/* ---- script ---- */

static BOOL task_of(const char *module)
{
    return GetModuleHandle(module) != NULL;
}

BOOL CALLBACK __export PropProc(HWND h, LPCSTR name, HANDLE v)
{
    if (SELECTOROF(name))
        P("    prop %q = %x", name, (WORD)v);
    else
        P("    prop #%x = %x", OFFSETOF(name), (WORD)v);
    (void)h;
    return TRUE;
}

/* a window's class and instance details (OLESVR's windows: "wins") */
static void winfo(HWND h, int depth)
{
    char t[64];
    HWND k;
    RECT r;
    GetWindowRect(h, &r);
    t[0] = 0;
    GetWindowText(h, t, sizeof t);
    P("WIN %d %w h=%x title=%q style=%lx ex=%lx cls.style=%x cbwnd=%d cbcls=%d id=%x parent=%w "
      "owner=%w visible=%d rect=%d,%d,%d,%d menu=%x", depth, h, (WORD)h, (LPCSTR)t, GetWindowLong(h, GWL_STYLE),
      GetWindowLong(h, GWL_EXSTYLE), GetClassWord(h, GCW_STYLE),
      GetClassWord(h, GCW_CBWNDEXTRA), GetClassWord(h, GCW_CBCLSEXTRA),
      GetWindowWord(h, GWW_ID), GetParent(h), GetWindow(h, GW_OWNER), IsWindowVisible(h),
      r.left, r.top, r.right, r.bottom, GetMenu(h) ? 1 : 0);
    {
        static FARPROC pp;
        if (!pp)
            pp = MakeProcInstance((FARPROC)PropProc, inst);
        EnumProps(h, (PROPENUMPROC)pp);
    }
    for (k = GetWindow(h, GW_CHILD); k; k = GetWindow(k, GW_HWNDNEXT))
        winfo(k, depth + 1);
}

static BOOL israw(const char *l)
{
    static const char *raw[] = { "init", "exec", "req", "poke", "advise", "unadvise",
                                 "term", "ackdata" };
    int i;
    for (i = 0; i < (int)(sizeof raw / sizeof raw[0]); i++)
        if (!lstrcmpi(l, raw[i]))
            return TRUE;
    return FALSE;
}

static void step(char *l)
{
    char *arg = l, *a, *b;
    OLESTATUS r;
    LPOLEOBJECT FAR *pp;
    LPOLEOBJECT o;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = 0;
    if (!*l || *l == ';')
        return;
    P("OS2 %s %s", (LPCSTR)l, (LPCSTR)arg);
    o = cur < nobj ? objs[cur] : NULL;
    if (!lstrcmpi(l, "run")) {
        if (WinExec(arg, SW_SHOWNORMAL) < 32)
            P("    failed");
    } else if (!lstrcmpi(l, "wait")) {
        pump(num((const char **)&arg), NULL);
    } else if (!lstrcmpi(l, "wins")) {
        /* every top-level window of OLESVR's classes, with its children */
        HWND h;
        for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
            char c[32];
            GetClassName(h, c, sizeof c);
            if (!lstrcmp(c, "SrvrWndClass") || !lstrcmp(c, "DocWndClass") ||
                !lstrcmp(c, "ItemWndClass"))
                winfo(h, 0);
        }
    } else if (!lstrcmpi(l, "mkwins")) {
        /* which handles new windows get (USER's heap after OLESVR's
           windows went): N windows with 8 extra bytes, then freed */
        static BOOL reg;
        HWND w[16];
        int i, n = num((const char **)&arg);
        if (!reg) {
            WNDCLASS wc;
            wc.style = 0;
            wc.lpfnWndProc = DefWindowProc;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = 8;
            wc.hInstance = inst;
            wc.hIcon = NULL;
            wc.hCursor = NULL;
            wc.hbrBackground = NULL;
            wc.lpszMenuName = NULL;
            wc.lpszClassName = "OS2Probe8";
            reg = RegisterClass(&wc);
        }
        for (i = 0; i < n && i < 16; i++) {
            w[i] = CreateWindow("OS2Probe8", "Doc", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, inst, NULL);
            P("    window %d h=%x", i, (WORD)w[i]);
        }
        for (i = 0; i < n && i < 16; i++)
            DestroyWindow(w[i]);
    } else if (!lstrcmpi(l, "waitgone")) {
        int n = 0;
        while (task_of(arg) && n++ < 600)
            pump(100, NULL);
        P("    %s", (LPCSTR)(task_of(arg) ? "still there" : "gone"));
    } else if (!lstrcmpi(l, "reg")) {
        char *e = arg;
        while (*e && *e != '=')
            e++;
        if (*e)
            *e++ = 0;
        r = (OLESTATUS)RegSetValue(HKEY_CLASSES_ROOT, arg, REG_SZ, e, lstrlen(e));
        if (r)
            P("    RegSetValue -> %d", r);
    } else if (!lstrcmpi(l, "regdump")) {
        HKEY k;
        static char path[512];
        lstrcpy(path, arg);
        if (RegOpenKey(HKEY_CLASSES_ROOT, arg, &k) == ERROR_SUCCESS) {
            regdump(k, path, 0);
            RegCloseKey(k);
        }
    } else if (!lstrcmpi(l, "doc")) {
        r = OleRegisterClientDoc("OS2", arg, 0, &lhdoc);
        P("CLI OleRegisterClientDoc -> %d %s", r, (LPCSTR)stname(r));
    } else if (!lstrcmpi(l, "revokedoc")) {
        res("OleRevokeClientDoc", OleRevokeClientDoc(lhdoc));
    } else if (!lstrcmpi(l, "savedoc")) {
        res("OleSavedClientDoc", OleSavedClientDoc(lhdoc));
    } else if (!lstrcmpi(l, "create") && (pp = newobj()) != NULL) {
        res("OleCreate", OleCreate("StdFileEditing", &client, arg, lhdoc, nextname(), pp,
                                   olerender_draw, 0));
    } else if (!lstrcmpi(l, "createinv") && (pp = newobj()) != NULL) {
        res("OleCreateInvisible", OleCreateInvisible("StdFileEditing", &client, arg, lhdoc,
                                   nextname(), pp, olerender_draw, 0, FALSE));
    } else if (!lstrcmpi(l, "template") && (pp = newobj()) != NULL) {
        res("OleCreateFromTemplate", OleCreateFromTemplate("StdFileEditing", &client, arg,
                                   lhdoc, nextname(), pp, olerender_draw, 0));
    } else if (!lstrcmpi(l, "embfile") && (pp = newobj()) != NULL) {
        a = word(&arg);
        res("OleCreateFromFile", OleCreateFromFile("StdFileEditing", &client, a, arg, lhdoc,
                                   nextname(), pp, olerender_draw, 0));
    } else if (!lstrcmpi(l, "linkfile") && (pp = newobj()) != NULL) {
        a = word(&arg);
        b = word(&arg);
        res("OleCreateLinkFromFile", OleCreateLinkFromFile("StdFileEditing", &client, a, b,
                                   *arg ? arg : NULL, lhdoc, nextname(), pp, olerender_draw, 0));
    } else if (!lstrcmpi(l, "fromclip") && (pp = newobj()) != NULL) {
        res("OleCreateFromClip", OleCreateFromClip("StdFileEditing", &client, lhdoc,
                                   nextname(), pp, olerender_draw, 0));
    } else if (!lstrcmpi(l, "linkclip") && (pp = newobj()) != NULL) {
        res("OleCreateLinkFromClip", OleCreateLinkFromClip("StdFileEditing", &client, lhdoc,
                                   nextname(), pp, olerender_draw, 0));
    } else if (!lstrcmpi(l, "obj")) {
        cur = num((const char **)&arg);
    } else if (!o || israw(l)) {
        if (!lstrcmpi(l, "init")) {
            ATOM aa, at;
            a = word(&arg);
            aa = *a && lstrcmp(a, "*") ? GlobalAddAtom(a) : 0;
            at = *arg && lstrcmp(arg, "*") ? GlobalAddAtom(arg) : 0;
            server = NULL;
            SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)me, MAKELPARAM(aa, at));
            if (aa)
                GlobalDeleteAtom(aa);
            if (at)
                GlobalDeleteAtom(at);
            P("RAW server=%s", (LPCSTR)(server ? "yes" : "none"));
        } else if (!lstrcmpi(l, "exec")) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(arg) + 1);
            lstrcpy(GlobalLock(h), arg);
            GlobalUnlock(h);
            post_wait(WM_DDE_EXECUTE, MAKELPARAM(0, h));
        } else if (!lstrcmpi(l, "req")) {
            a = word(&arg);
            post_wait(WM_DDE_REQUEST, MAKELPARAM(format(arg), GlobalAddAtom(a)));
        } else if (!lstrcmpi(l, "poke")) {
            WORD cf;
            a = word(&arg);
            cf = format(word(&arg));
            post_wait(WM_DDE_POKE, MAKELPARAM(ddeblock(0x2000, cf, arg), GlobalAddAtom(a)));
        } else if (!lstrcmpi(l, "advise")) {
            WORD cf, fl;
            a = word(&arg);
            cf = format(word(&arg));
            fl = *arg ? num((const char **)&arg) : 0;
            {
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4);
                WORD FAR *p = (WORD FAR *)GlobalLock(h);
                p[0] = fl;
                p[1] = cf;
                GlobalUnlock(h);
                post_wait(WM_DDE_ADVISE, MAKELPARAM(h, GlobalAddAtom(a)));
            }
        } else if (!lstrcmpi(l, "unadvise")) {
            a = word(&arg);
            post_wait(WM_DDE_UNADVISE, MAKELPARAM(format(arg), GlobalAddAtom(a)));
        } else if (!lstrcmpi(l, "term")) {
            got = FALSE;
            if (server) {
                HWND s = server;
                server = NULL;          /* its TERMINATE is the answer */
                PostMessage(s, WM_DDE_TERMINATE, (WPARAM)me, 0);
                pump(3000, &got);
            }
        } else if (!lstrcmpi(l, "ackdata")) {
            ackmode = !lstrcmpi(arg, "off") ? 1 : !lstrcmpi(arg, "busy") ? 2 : 0;
        } else
            P("    no object");
    } else if (!lstrcmpi(l, "activate")) {
        res("OleActivate", OleActivate(o, num((const char **)&arg), TRUE, TRUE, me, NULL));
    } else if (!lstrcmpi(l, "update")) {
        res("OleUpdate", OleUpdate(o));
    } else if (!lstrcmpi(l, "close")) {
        res("OleClose", OleClose(o));
    } else if (!lstrcmpi(l, "reconnect")) {
        res("OleReconnect", OleReconnect(o));
    } else if (!lstrcmpi(l, "delete")) {
        res("OleDelete", OleDelete(o));
        objs[cur] = NULL;
    } else if (!lstrcmpi(l, "release")) {
        res("OleRelease", OleRelease(o));
        objs[cur] = NULL;
    } else if (!lstrcmpi(l, "query")) {
        LONG t = 0;
        RECT rc;
        char nm[128];
        UINT n = sizeof nm;
        P("CLI OleQueryOpen -> %d", OleQueryOpen(o));
        P("CLI OleQueryOutOfDate -> %d", OleQueryOutOfDate(o));
        r = OleQueryType(o, &t);
        P("CLI OleQueryType -> %d type=%ld", r, t);
        nm[0] = 0;
        r = OleQueryName(o, nm, &n);
        P("CLI OleQueryName -> %d %q", r, (LPCSTR)nm);
        r = OleQueryBounds(o, &rc);
        P("CLI OleQueryBounds -> %d %d,%d,%d,%d", r, rc.left, rc.top, rc.right, rc.bottom);
    } else if (!lstrcmpi(l, "setdata")) {
        WORD cf;
        HGLOBAL h;
        a = word(&arg);
        cf = format(a);
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(arg) + 1);
        lstrcpy(GlobalLock(h), arg);
        GlobalUnlock(h);
        res("OleSetData", OleSetData(o, cf, h));
    } else if (!lstrcmpi(l, "request")) {
        res("OleRequestData", OleRequestData(o, format(arg)));
    } else if (!lstrcmpi(l, "getdata")) {
        HANDLE h = NULL;
        WORD cf = format(arg);
        r = OleGetData(o, cf, &h);
        P("CLI OleGetData -> %d %s", r, (LPCSTR)stname(r));
        if (h && cf != CF_METAFILEPICT && cf != CF_BITMAP)
            OsDumpData(cf, (HGLOBAL)h, 0);
        else if (h)
            OsDumpData(cf, (HGLOBAL)h, 0);
    } else if (!lstrcmpi(l, "hostnames")) {
        a = word(&arg);
        res("OleSetHostNames", OleSetHostNames(o, a, arg));
    } else if (!lstrcmpi(l, "bounds")) {
        RECT rc;
        const char *s = arg;
        rc.left = num(&s);
        rc.top = num(&s);
        rc.right = num(&s);
        rc.bottom = num(&s);
        res("OleSetBounds", OleSetBounds(o, &rc));
    } else if (!lstrcmpi(l, "execute")) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(arg) + 1);
        lstrcpy(GlobalLock(h), arg);
        GlobalUnlock(h);
        res("OleExecute", OleExecute(o, h, 0));
    } else if (!lstrcmpi(l, "colors")) {
        static struct { WORD v, n; PALETTEENTRY e[2]; } pal = {
            0x300, 2, { { 255, 0, 0, 0 }, { 0, 0, 255, 0 } }
        };
        res("OleSetColorScheme", OleSetColorScheme(o, (LOGPALETTE FAR *)&pal));
    } else if (!lstrcmpi(l, "target")) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 64);
        BYTE FAR *p = GlobalLock(h);
        static const char data[] = "PSCRIPT\0PSCRIPT\0LPT1:";
        int i;
        for (i = 0; i < 14; i++)
            ((WORD FAR *)p)[i] = 0;
        ((WORD FAR *)p)[0] = 0;         /* device name */
        ((WORD FAR *)p)[1] = 8;         /* driver name */
        ((WORD FAR *)p)[2] = 16;        /* port */
        ((WORD FAR *)p)[3] = 22;        /* devmode */
        ((WORD FAR *)p)[4] = 0;
        ((WORD FAR *)p)[5] = 22;        /* environment */
        ((WORD FAR *)p)[6] = 0;
        for (i = 0; i < (int)sizeof data; i++)
            p[14 + i] = data[i];
        GlobalUnlock(h);
        res("OleSetTargetDevice", OleSetTargetDevice(o, h));
    } else if (!lstrcmpi(l, "rename")) {
        res("OleRename", OleRename(o, arg));
    } else if (!lstrcmpi(l, "savestream")) {
        slen = 0;
        r = OleSaveToStream(o, &stream);
        P("CLI OleSaveToStream -> %d %s", r, (LPCSTR)stname(r));
        {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, slen ? slen : 1);
            BYTE FAR *s = GlobalLock(sbuf), FAR *t = GlobalLock(h);
            DWORD i;
            for (i = 0; i < slen; i++)
                t[(WORD)i] = s[(WORD)i];
            GlobalUnlock(sbuf);
            GlobalUnlock(h);
            P("  stream bytes=%lu", slen);
            OsDumpData(0, h, 0);
            GlobalFree(h);
        }
    } else if (!lstrcmpi(l, "loadstream") && (pp = newobj()) != NULL) {
        spos = 0;
        res("OleLoadFromStream", OleLoadFromStream(&stream, "StdFileEditing", &client, lhdoc,
                                   nextname(), pp));
    } else if (!lstrcmpi(l, "lock")) {
        res("OleLockServer", OleLockServer(o, &locked));
    } else if (!lstrcmpi(l, "unlock")) {
        res("OleUnlockServer", OleUnlockServer(locked));
    } else
        P("    unknown");
}

static char script[8192];

int FAR __cdecl tmain(void)
{
    WNDCLASS wc;
    HFILE f;
    int n;
    char *l, *e;
    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    wc.style = 0;
    wc.lpfnWndProc = ClientWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "OS2Client";
    RegisterClass(&wc);
    me = CreateWindow("OS2Client", "OS2", WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, inst, NULL);
    clientvt.CallBack = (void FAR *)MakeProcInstance((FARPROC)ClientCallBack, inst);
    client.lpvtbl = &clientvt;
    streamvt.Get = (void FAR *)MakeProcInstance((FARPROC)StreamGet, inst);
    streamvt.Put = (void FAR *)MakeProcInstance((FARPROC)StreamPut, inst);
    stream.lpstbl = &streamvt;
    sbuf = GlobalAlloc(GMEM_MOVEABLE, 1024);
    OsSpyStart();
    P("OS2 start");
    f = _lopen("C:\\OS2.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        P("OS2 no script");
        return 1;
    }
    n = _lread(f, script, sizeof script - 1);
    _lclose(f);
    script[n < 0 ? 0 : n] = 0;
    for (l = script; *l; l = e) {
        e = l;
        while (*e && *e != '\r' && *e != '\n')
            e++;
        while (*e == '\r' || *e == '\n')
            *e++ = 0;
        step(l);
        pump(100, NULL);
    }
    P("OS2 end");
    OsSpyStop();
    return 0;
}
