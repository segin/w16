/*
 * fmx.c - FMX0.DLL, a File Manager extension that records how File
 * Manager talks to it (probe for layer 5 of shell/winfile, the extension
 * interface; ARCHITECTURE.md 15.7, docs/notes/winfile-handover.md).
 *
 * Listed in WINFILE.INI [AddOns], it logs every FMExtensionProc call to
 * C:\FMX.LOG (one line each, the module name first, so that copies made
 * with tools/nerename.py, FMX1 ... FMX9, share the log; C:\FMX.INI [FMX]
 * Log= names another file). SNAP's "append FILE text" puts the script's
 * own marks between them. Handles are
 * never printed (they differ between runs); what they are is: "ours" for
 * the menu it gave, the class for a window.
 *
 * The interface, from the 3.1 SDK (Programmer's Reference vol. 1 ch. 16,
 * vol. 3 ch. 7, facts only): FMExtensionProc(hwnd, wMsg, lParam); wMsg
 * 1..99 a command of the extension's menu, else an FMEVENT_*; FMS_LOAD
 * { DWORD dwSize; char szMenuName[MENU_TEXT_LEN]; HMENU hMenu; UINT
 * wMenuDelta; }. The SDK gives no numbers: the values below are what the
 * probe looks for, each one confirmed or refuted by the log (the scan
 * command sends a range of WM_USER messages and logs which ones answer).
 *
 * Its menu (FMEVENT_LOAD): name "FMX&n" (n the module name's last
 * character), items 1 &Query (every FM_* query, logged), 2 Refresh &All,
 * 3 Refresh A&ctive (FM_REFRESH_WINDOWS 1, 0), 4 &Reload (posts
 * FM_RELOAD_EXTENSIONS), 5 Su&b (a popup: 6 &Inner, 7 &Grayed), 8 &Scan,
 * 9 Chec&ked (checked at INITMENU), 10 &Uninstall (removes its own
 * [AddOns] key, then posts FM_RELOAD_EXTENSIONS).
 *
 * C:\FMX.INI [module name] changes how it answers: LoadRet= what
 * FMEVENT_LOAD returns (default 1), NoMenu=1 gives no menu (hMenu 0),
 * Name= the menu name, Size= the dwSize it gives, Quiet=1 no FM_* queries on FMEVENT_SELCHANGE
 * (default: the focus, the count and the first and last selected file).
 */
#include <windows.h>
#include "tkdll.h"

/* the FMS_* structures as the SDK gives them */
#define MENU_TEXT_LEN       40          /* (to be confirmed: the log dumps
                                           the structure as given) */
typedef struct {
    DWORD dwSize;
    char  szMenuName[MENU_TEXT_LEN];
    HMENU hMenu;
    WORD  wMenuDelta;
} FMS_LOAD, FAR *LPFMS_LOAD;
typedef struct {
    WORD  wTime, wDate;
    DWORD dwSize;
    BYTE  bAttr;
    char  szName[260];
} FMS_GETFILESEL, FAR *LPFMS_GETFILESEL;
typedef struct {
    DWORD dwTotalSpace, dwFreeSpace;
    char  szPath[260];
    char  szVolume[14];
    char  szShare[128];
} FMS_GETDRIVEINFO, FAR *LPFMS_GETDRIVEINFO;

/* candidates (confirmed by the log) */
#define FMEVENT_LOAD        100
#define FMEVENT_UNLOAD      101
#define FMEVENT_INITMENU    102
#define FMEVENT_USER_REFRESH 103
#define FMEVENT_SELCHANGE   104
#define FM_GETFOCUS         (WM_USER + 0x200)
#define FM_GETDRIVEINFO     (WM_USER + 0x201)
#define FM_GETSELCOUNT      (WM_USER + 0x202)
#define FM_GETSELCOUNTLFN   (WM_USER + 0x203)
#define FM_GETFILESEL       (WM_USER + 0x204)
#define FM_GETFILESELLFN    (WM_USER + 0x205)
#define FM_REFRESH_WINDOWS  (WM_USER + 0x206)
#define FM_RELOAD_EXTENSIONS (WM_USER + 0x207)

static char logname[80] = "C:\\FMX.LOG";
static char ininame[] = "C:\\FMX.INI";
static char mod[9];
static HMENU menu;
static WORD delta;
static HWND fmwnd;
static BOOL quiet;
static char line[512];
static int  len;

static void fill(LPVOID p, BYTE v, int n)
{
    LPBYTE b = p;
    while (n-- > 0)
        *b++ = v;
}

/* ---- the log ---- */

static void put(LPCSTR s)
{
    while (*s && len < (int)sizeof line - 3)
        line[len++] = *s++;
}

static void putf(LPCSTR f, ...)
{
    char b[300];
    wvsprintf(b, f, (LPSTR)(&f + 1));
    put(b);
}

static void begin(void)
{
    len = 0;
    put(mod);
    put(" ");
}

static void flush(void)
{
    HFILE h;
    line[len++] = '\r';
    line[len++] = '\n';
    h = _lopen(logname, OF_WRITE);
    if (h == HFILE_ERROR)
        h = _lcreat(logname, 0);
    if (h != HFILE_ERROR) {
        _llseek(h, 0, 2);
        _lwrite(h, line, len);
        _lclose(h);
    }
    len = 0;
}

static void say(LPCSTR f, ...)
{
    char b[300];
    begin();
    wvsprintf(b, f, (LPSTR)(&f + 1));
    put(b);
    flush();
}

static void put_hex(const BYTE FAR *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        putf("%02x", p[i]);
}

/* a window as its class (and, for the frame, "FM") */
static void put_wnd(HWND w)
{
    char c[40];
    if (!w) {
        put("0");
        return;
    }
    if (!IsWindow(w)) {
        put("(gone)");
        return;
    }
    GetClassName(w, c, sizeof c);
    put(c);
}

/* the menu's items: id, state, text; popups in braces */
static void put_menu(HMENU m, int depth)
{
    int i, n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        char t[64];
        UINT st = GetMenuState(m, i, MF_BYPOSITION);
        HMENU sub = GetSubMenu(m, i);
        GetMenuString(m, i, t, sizeof t, MF_BYPOSITION);
        if (sub) {
            putf(" [%s]{", (LPSTR)t);
            if (depth < 2)
                put_menu(sub, depth + 1);
            put(" }");
        } else if (st & MF_SEPARATOR) {
            put(" -");
        } else {
            putf(" %u:%s", GetMenuItemID(m, i), (LPSTR)t);
            if (st & MF_GRAYED)
                put("/g");
            if (st & MF_DISABLED)
                put("/d");
            if (st & MF_CHECKED)
                put("/c");
        }
    }
}

/* ---- the queries ---- */

static void q_file(WORD msg, LPCSTR what, WORD i)
{
    FMS_GETFILESEL f;
    LONG r;
    fill(&f, 0xCC, sizeof f);
    r = SendMessage(fmwnd, msg, i, (LPARAM)(LPFMS_GETFILESEL)&f);
    begin();
    putf("  %s(%u) -> %ld:", what, i, r);
    if (f.wTime == 0xCCCC && f.wDate == 0xCCCC && f.bAttr == 0xCC) {
        put(" untouched");
    } else {
        f.szName[sizeof f.szName - 1] = 0;
        putf(" time=%04x date=%04x size=%lu attr=%02x name=\"%s\"",
             f.wTime, f.wDate, f.dwSize, f.bAttr, (LPSTR)f.szName);
        /* (3.1 copies more than the name: the bytes after its NUL are
           left from earlier names, WF15; not logged) */
    }
    flush();
}

static void q_drive(void)
{
    FMS_GETDRIVEINFO g;
    LONG r;
    fill(&g, 0xCC, sizeof g);
    r = SendMessage(fmwnd, FM_GETDRIVEINFO, 0, (LPARAM)(LPFMS_GETDRIVEINFO)&g);
    g.szPath[sizeof g.szPath - 1] = 0;
    g.szVolume[sizeof g.szVolume - 1] = 0;
    g.szShare[sizeof g.szShare - 1] = 0;
    begin();
    putf("  GETDRIVEINFO -> %ld: total=%lu free=%lu path=\"%s\" vol=\"%s\" share=\"%s\"",
         r, g.dwTotalSpace, g.dwFreeSpace, (LPSTR)g.szPath, (LPSTR)g.szVolume,
         (LPSTR)g.szShare);
    flush();
    begin();
    put("  volume bytes ");
    put_hex((LPBYTE)g.szVolume, sizeof g.szVolume);
    put(" share[0..3] ");
    put_hex((LPBYTE)g.szShare, 4);
    flush();
}

static void query(BOOL full)
{
    LONG focus = SendMessage(fmwnd, FM_GETFOCUS, 0, 0);
    LONG n = SendMessage(fmwnd, FM_GETSELCOUNT, 0, 0);
    LONG nl = SendMessage(fmwnd, FM_GETSELCOUNTLFN, 0, 0);
    HWND f = GetFocus();
    begin();
    putf("  GETFOCUS -> %ld  GETSELCOUNT -> %ld  GETSELCOUNTLFN -> %ld  focus=", focus, n, nl);
    put_wnd(f);
    if (f) {
        put("<");
        put_wnd(GetParent(f));
    }
    flush();
    if (!full) {
        if (n > 0) {
            q_file(FM_GETFILESEL, "GETFILESEL", 0);
            if (n > 1)
                q_file(FM_GETFILESEL, "GETFILESEL", (WORD)(n - 1));
        }
        return;
    }
    {
        WORD i;
        for (i = 0; i < (WORD)n && i < 40; i++)
            q_file(FM_GETFILESEL, "GETFILESEL", i);
        q_file(FM_GETFILESEL, "GETFILESEL", (WORD)n);
        q_file(FM_GETFILESEL, "GETFILESEL", (WORD)n + 3);
        for (i = 0; i < (WORD)nl && i < 3; i++)
            q_file(FM_GETFILESELLFN, "GETFILESELLFN", i);
    }
    q_drive();
}

/* WM_USER + 0x1F0 ... 0x21F but the reload candidate, to the frame with
   a buffer: which ones answer */
static void scan(void)
{
    static BYTE buf[600];
    WORD m;
    for (m = WM_USER + 0x1F0; m <= WM_USER + 0x21F; m++) {
        LONG r;
        int i, changed = 0;
        if (m == FM_RELOAD_EXTENSIONS)
            continue;
        fill(buf, 0xCC, sizeof buf);
        r = SendMessage(fmwnd, m, 0, (LPARAM)(LPBYTE)buf);
        for (i = 0; i < (int)sizeof buf; i++)
            if (buf[i] != 0xCC)
                changed = i + 1;
        if (r || changed) {
            begin();
            putf("  scan %04x -> %ld, %d bytes: ", m, r, changed);
            put_hex(buf, changed < 24 ? changed : 24);
            flush();
        }
    }
    say("  scan done");
}

static void uninstall(void)
{
    /* remove our own key: the entry whose value names this file */
    static char keys[1024], val[144], self[144];
    LPSTR k;
    GetModuleFileName((HINSTANCE)lib_regs[L_DI], self, sizeof self);
    GetPrivateProfileString("AddOns", NULL, "", keys, sizeof keys, "WINFILE.INI");
    for (k = keys; *k; k += lstrlen(k) + 1) {
        GetPrivateProfileString("AddOns", k, "", val, sizeof val, "WINFILE.INI");
        if (!lstrcmpi(val, self)) {
            WritePrivateProfileString("AddOns", k, NULL, "WINFILE.INI");
            say("  removed [AddOns] %s", (LPSTR)k);
        }
    }
    PostMessage(fmwnd, FM_RELOAD_EXTENSIONS, 0, 0);
}

/* ---- the entry ---- */

static HMENU make_menu(void)
{
    HMENU m = CreatePopupMenu(), s = CreatePopupMenu();
    AppendMenu(m, MF_STRING, 1, "&Query");
    AppendMenu(m, MF_STRING, 2, "Refresh &All");
    AppendMenu(m, MF_STRING, 3, "Refresh A&ctive");
    AppendMenu(m, MF_STRING, 4, "&Reload");
    AppendMenu(s, MF_STRING, 6, "&Inner");
    AppendMenu(s, MF_STRING | MF_GRAYED, 7, "&Grayed");
    AppendMenu(m, MF_POPUP, (UINT)s, "Su&b");
    AppendMenu(m, MF_SEPARATOR, 0, NULL);
    AppendMenu(m, MF_STRING, 8, "&Scan");
    AppendMenu(m, MF_STRING, 9, "Chec&ked");
    AppendMenu(m, MF_STRING, 10, "&Uninstall");
    return m;
}

static int nchildren(HWND frame)
{
    HWND mdi = GetWindow(frame, GW_CHILD), w;
    int n = 0;
    if (mdi)
        for (w = GetWindow(mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
            n++;
    return n;
}

DWORD FAR PASCAL __loadds FMExtensionProc(HWND hwnd, WORD msg, LONG lp)
{
    fmwnd = hwnd;
    switch (msg) {
    case FMEVENT_LOAD: {
        LPFMS_LOAD l = (LPFMS_LOAD)lp;
        DWORD ret = GetPrivateProfileInt(mod, "LoadRet", 1, ininame);
        char name[64];
        begin();
        putf("LOAD(%u) hwnd=", msg);
        put_wnd(hwnd);
        putf(" vis=%d children=%d menuitems=%d in=", IsWindowVisible(hwnd) ? 1 : 0,
             nchildren(hwnd), GetMenuItemCount(GetMenu(hwnd)));
        /* of the structure as given only the delta: the rest is what
           File Manager's stack held (WF15, WF16: zeros the first time,
           then what the extension before wrote, after a reload other
           bytes) */
        putf("delta=%u", l->wMenuDelta);
        flush();
        quiet = GetPrivateProfileInt(mod, "Quiet", 0, ininame);
        delta = l->wMenuDelta;
        wsprintf(name, "FMX&%c", mod[lstrlen(mod) - 1]);
        GetPrivateProfileString(mod, "Name", name, name, sizeof name, ininame);
        l->dwSize = GetPrivateProfileInt(mod, "Size", sizeof *l, ininame);
        lstrcpy(l->szMenuName, name);
        menu = GetPrivateProfileInt(mod, "NoMenu", 0, ininame) ? NULL : make_menu();
        l->hMenu = menu;
        say("  delta=%u name=\"%s\" menu=%d return %lu", delta, (LPSTR)name, menu ? 1 : 0, ret);
        return ret;
    }
    case FMEVENT_UNLOAD:
        begin();
        putf("UNLOAD(%u) lp=%lx hwnd=", msg, lp);
        put_wnd(hwnd);
        putf(" vis=%d", IsWindow(hwnd) && IsWindowVisible(hwnd) ? 1 : 0);
        flush();
        return 0;
    case FMEVENT_INITMENU:
        begin();
        putf("INITMENU(%u) delta=%u menu=%s", msg, LOWORD(lp),
             (LPSTR)((HMENU)HIWORD(lp) == menu ? "ours" : "other"));
        if (HIWORD(lp) && IsMenu((HMENU)HIWORD(lp)))
            put_menu((HMENU)HIWORD(lp), 0);
        flush();
        if (HIWORD(lp))
            CheckMenuItem((HMENU)HIWORD(lp), LOWORD(lp) + 9, MF_BYCOMMAND | MF_CHECKED);
        return 0;
    case FMEVENT_USER_REFRESH:
        say("USER_REFRESH(%u) lp=%lx", msg, lp);
        /* before or after the windows are read again: the selection
           tells */
        if (!quiet)
            query(FALSE);
        return 0;
    case FMEVENT_SELCHANGE:
        say("SELCHANGE(%u) lp=%lx", msg, lp);
        if (!quiet)
            query(FALSE);
        return 0;
    }
    if (msg >= 1 && msg <= 99) {
        say("COMMAND %u lp=%lx", msg, lp);
        switch (msg) {
        case 1: query(TRUE); break;
        /* (the answer means nothing: WF15) */
        case 2: SendMessage(hwnd, FM_REFRESH_WINDOWS, 1, 0); say("  REFRESH_WINDOWS(1) done"); break;
        case 3: SendMessage(hwnd, FM_REFRESH_WINDOWS, 0, 0); say("  REFRESH_WINDOWS(0) done"); break;
        case 4: PostMessage(hwnd, FM_RELOAD_EXTENSIONS, 0, 0); break;
        case 8: scan(); break;
        case 10: uninstall(); break;
        }
        return 0;
    }
    begin();
    putf("MSG %u (%04x) lp=%lx hwnd=", msg, msg, lp);
    put_wnd(hwnd);
    flush();
    return 0;
}

int __cdecl libmain(void)
{
    char path[144];
    LPSTR p, b;
    int i = 0;
    lib_heap();
    GetModuleFileName((HINSTANCE)lib_regs[L_DI], path, sizeof path);
    for (p = b = path; *p; p++)
        if (*p == '\\' || *p == ':')
            b = p + 1;
    while (b[i] && b[i] != '.' && i < 8) {
        mod[i] = b[i];
        i++;
    }
    mod[i] = 0;
    GetPrivateProfileString("FMX", "Log", logname, logname, sizeof logname, ininame);
    say("libmain proc=%d PROC=%d", GetProcAddress((HINSTANCE)lib_regs[L_DI], "FMExtensionProc") ? 1 : 0,
        GetProcAddress((HINSTANCE)lib_regs[L_DI], "FMEXTENSIONPROC") ? 1 : 0);
    return 1;
}

int FAR PASCAL __loadds WEP(int param)
{
    if (param != WEP_SYSTEM_EXIT)
        say("WEP");
    return 1;
}

/* what the entry code recorded (tkdll.h) */
WORD FAR PASCAL __loadds FMXInfo(WORD i)
{
    return lib_info(i);
}
