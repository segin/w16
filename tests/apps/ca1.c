/*
 * ca1.c - CA1: drive Calculator with scripted input and record what it
 * displays (black-box observation; tests/calcscenarios.sh).
 *
 * Runs as the Windows shell (tests/refrun.sh) and executes C:\CA.TXT:
 *     run <command line>     WinExec it and wait for its "SciCalc" window
 *     wait <ms>              keep dispatching messages for <ms> ms
 *     ; ...                  comment (not logged)
 *     # ...                  copied to the log as it is (a section label)
 *     anything else          a test line, see below
 * A test line is a sequence of inputs, sent in order to the Calculator
 * window (blanks are ignored):
 *     {n}                    post WM_COMMAND n (a button's ID)
 *     <XX>                   post WM_KEYDOWN + WM_KEYUP, virtual key XX (hex)
 *     [x,y]                  post a left click at client point x,y
 *     'c                     post WM_CHAR c
 *     "text"                 put text on the clipboard (CF_TEXT) and post
 *                            Edit > Paste (WM_COMMAND 301)
 *     any other character    the WM_COMMAND of the button that character
 *                            stands for (table below)
 * After the line, CA1 lets Calculator process everything, then logs
 *     <line>|<414>|<413>|<clipboard after Edit > Copy>|<400>|<401>|<402>|<403>|<after>
 * (the texts of the displays, static 414 for the standard view and 413
 * for the scientific one, read before Copy; of the indicator statics; and
 * of the visible display once more after Copy).
 * A message box Calculator puts up meanwhile is logged ("BOX caption:
 * text") and closed with IDOK. Results go to C:\CA.LOG.
 *
 * The button IDs are Calculator's own, observed: the accelerator table of
 * Windows 3.1's CALC.EXE (resources read as data, tools/resdump.py) maps
 * each key to them, and posting them has the effect of the key.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

/* the character that stands for each button (the ones that are not
   their own key are upper case or punctuation; see calcscenarios.sh) */
static const struct { char c; BYTE id; } keys[] = {
    { '(', 40 }, { ')', 41 }, { '.', 85 }, { '+', 92 }, { '-', 93 },
    { '*', 91 }, { '/', 90 }, { '=', 111 }, { '%', 94 }, { '&', 86 },
    { '|', 87 }, { '^', 88 }, { '~', 97 }, { ';', 96 }, { '!', 105 },
    { '@', 103 }, { '#', 104 }, { 's', 98 }, { 'o', 99 }, { 't', 100 },
    { 'n', 101 }, { 'l', 102 }, { 'r', 106 }, { 'm', 107 }, { 'v', 109 },
    { 'p', 110 }, { 'x', 116 }, { 'y', 95 }, { 'i', 125 }, { 'h', 126 },
    { 'L', 89 }, { 'N', 80 }, { 'c', 81 }, { 'e', 82 }, { 'b', 83 },
    { 'Q', 112 }, { 'R', 113 }, { 'S', 114 }, { 'P', 115 },
    { 'T', 84 }, { 'U', 117 }, { 'V', 118 }, { 'W', 119 }, { 'X', 120 },
    { 'H', 124 }, { 'J', 123 }, { 'K', 122 }, { 'G', 121 },
    { 'w', 127 }, { 'u', 128 }, { 'z', 129 }, { 'Z', 0 }
};

static HFILE logf = HFILE_ERROR;
static char obuf[2048];
static WORD olen;
static HWND calc, mine;

static void flush(void)
{
    if (olen && logf != HFILE_ERROR)
        _lwrite(logf, obuf, olen);
    olen = 0;
}

static void out(const char FAR *s)
{
    while (*s) {
        if (olen == sizeof obuf)
            flush();
        obuf[olen++] = *s++;
    }
}

static void outn(const char FAR *s, int n)
{
    while (n-- > 0 && *s) {
        if (olen == sizeof obuf)
            flush();
        obuf[olen++] = *s++;
    }
}

static void tklog(const char *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0);
}

/* let Calculator work off what was posted to it: Yield runs it while it
   has messages; it yields back from GetMessage when its queue is empty */
static void settle(void)
{
    int i;
    MSG msg;
    for (i = 0; i < 6; i++) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

static WORD num(const char **pp, WORD base)
{
    const char *s = *pp;
    WORD v = 0;
    for (;; s++) {
        WORD d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else
            break;
        v = v * base + d;
    }
    *pp = s;
    return v;
}

static int same(const char *a, const char *b)
{
    while (*b && *a == *b) {
        a++;
        b++;
    }
    return *b == 0;
}

static void post_cmd(WORD id)
{
    PostMessage(calc, WM_COMMAND, id, 0);
    settle();
}

static void put_clip(const char *s, int n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    char FAR *p;
    int i;
    if (!h)
        return;
    p = GlobalLock(h);
    for (i = 0; i < n; i++)
        p[i] = s[i];
    p[n] = 0;
    GlobalUnlock(h);
    if (OpenClipboard(mine)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
    } else
        GlobalFree(h);
}

static void child_text(int id)
{
    char t[80];
    HWND c = GetDlgItem(calc, id);
    t[0] = 0;
    if (c)
        SendMessage(c, WM_GETTEXT, sizeof t, (LPARAM)(LPSTR)t);
    out(t);
}

BOOL CALLBACK __export __loadds find_box(HWND w, LPARAM lp)
{
    char cls[16], cap[64];
    HWND c;
    (void)lp;
    if (!IsWindowVisible(w))
        return TRUE;
    GetClassName(w, cls, sizeof cls);
    if (lstrcmp(cls, "#32770"))
        return TRUE;
    GetWindowText(w, cap, sizeof cap);
    if (!lstrcmp(cap, "Statistics Box"))
        return TRUE;
    out("|BOX ");
    out(cap);
    out(":");
    for (c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        GetClassName(c, cls, sizeof cls);
        if (!lstrcmpi(cls, "Static")) {
            char t[160];
            SendMessage(c, WM_GETTEXT, sizeof t, (LPARAM)(LPSTR)t);
            out(t);
        }
    }
    PostMessage(w, WM_COMMAND, IDOK, 0);
    return TRUE;
}

static FARPROC box_proc;

static void boxes(void)
{
    EnumWindows((WNDENUMPROC)box_proc, 0);
    settle();
}

static void test_line(const char *l)
{
    const char *p = l;
    while (*p) {
        char c = *p++;
        int i;
        if (c == ' ')
            continue;
        if (c == '{') {
            WORD id = num(&p, 10);
            if (*p == '}')
                p++;
            post_cmd(id);
        } else if (c == '<') {
            WORD vk = num(&p, 16);
            if (*p == '>')
                p++;
            PostMessage(calc, WM_KEYDOWN, vk, 1L);
            PostMessage(calc, WM_KEYUP, vk, 0xC0000001L);
            settle();
        } else if (c == '[') {
            WORD x = num(&p, 10), y = 0;
            if (*p == ',') {
                p++;
                y = num(&p, 10);
            }
            if (*p == ']')
                p++;
            PostMessage(calc, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            PostMessage(calc, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            settle();
        } else if (c == '\'' && *p) {
            PostMessage(calc, WM_CHAR, (BYTE)*p++, 1L);
            settle();
        } else if (c == '"') {
            const char *s = p;
            while (*p && *p != '"')
                p++;
            put_clip(s, (int)(p - s));
            if (*p)
                p++;
            post_cmd(301);
            pump(400);
        } else {
            for (i = 0; keys[i].id; i++)
                if (keys[i].c == c)
                    break;
            if (keys[i].id)
                post_cmd(keys[i].id);
            else if (c >= '0' && c <= '9')
                post_cmd(c);
            else if (c >= 'A' && c <= 'F')
                post_cmd(c);
        }
        boxes();
    }
    out(l);
    out("|");
    child_text(414);
    out("|");
    child_text(413);
    out("|");
    post_cmd(300);
    boxes();
    if (OpenClipboard(mine)) {
        HANDLE h = GetClipboardData(CF_TEXT);
        if (h) {
            char FAR *t = GlobalLock(h);
            if (t) {
                outn(t, 200);
                GlobalUnlock(h);
            }
        } else
            out("(none)");
        CloseClipboard();
    } else
        out("(busy)");
    out("|");
    child_text(400);
    out("|");
    child_text(401);
    out("|");
    child_text(402);
    out("|");
    child_text(403);
    out("|");
    child_text(IsWindowVisible(GetDlgItem(calc, 414)) ? 414 : 413);
    out("\r\n");
}

static void run_line(char *l)
{
    if (same(l, "run ")) {
        int i;
        tklog("CA1 run\r\n");
        if (WinExec(l + 4, SW_SHOWNORMAL) < 32) {
            tklog("CA1 run failed\r\n");
            return;
        }
        for (i = 0; i < 100 && !calc; i++) {
            pump(100);
            calc = FindWindow("SciCalc", NULL);
        }
        pump(500);
        tklog(calc ? "CA1 calc up\r\n" : "CA1 no SciCalc window\r\n");
    } else if (same(l, "wait ")) {
        const char *p = l + 5;
        pump(num(&p, 10));
    } else if (*l == '#') {
        out(l);
        out("\r\n");
    } else if (calc) {
        test_line(l);
    }
}

int __cdecl tmain(void)
{
    HFILE f;
    HGLOBAL hs;
    char FAR *script;
    LONG n;
    char line[512];
    LONG i;
    int k;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    box_proc = MakeProcInstance((FARPROC)find_box, (HINSTANCE)init_regs[I_DI]);
    mine = CreateWindow("STATIC", "CA1", WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                        (HINSTANCE)init_regs[I_DI], NULL);
    f = _lopen("C:\\CA.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        tklog("CA1: no C:\\CA.TXT\r\n");
        return 1;
    }
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (n > 65000L)
        n = 65000L;
    hs = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    script = hs ? GlobalLock(hs) : NULL;
    if (!script) {
        tklog("CA1: no memory\r\n");
        return 1;
    }
    n = _lread(f, script, (UINT)n);
    _lclose(f);
    if (n < 0)
        n = 0;
    script[(UINT)n] = 0;
    logf = _lcreat("C:\\CA.LOG", 0);
    i = 0;
    while (i < n) {
        k = 0;
        while (i < n && script[(UINT)i] != '\r' && script[(UINT)i] != '\n') {
            if (k < (int)sizeof line - 1)
                line[k++] = script[(UINT)i];
            i++;
        }
        while (i < n && (script[(UINT)i] == '\r' || script[(UINT)i] == '\n'))
            i++;
        line[k] = 0;
        if (k && line[0] != ';')
            run_line(line);
        if (olen > 1500)
            flush();
    }
    out("CA1 done\r\n");
    flush();
    _lclose(logf);
    tklog("CA1 done\r\n");
    if (calc && IsWindow(calc)) {
        PostMessage(calc, WM_CLOSE, 0, 0);
        pump(300);
    }
    return 0;
}
