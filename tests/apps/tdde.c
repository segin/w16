/*
 * tdde.c - Program Manager DDE probe (black-box observation).
 *
 * Talks to the "PROGMAN" DDE service the way an installer does, with the
 * raw DDE messages of the Windows SDK, and logs every answer to TK.LOG:
 *     DDE ack <n> server class="..." text="..."      replies to INITIATE
 *     DDE exec "<command>" ack=<status word> block="<the command block
 *         as the ACK returned it, NULs as \x00>"   WM_DDE_ACK
 *     DDE data "<item>" flags=<word> fmt=<n> "<text, CR/LF shown as \r\n>"
 * The script C:\DDE.TXT holds one line per step:
 *     exec <command string>        WM_DDE_EXECUTE
 *     request <item>               WM_DDE_REQUEST, CF_TEXT
 *     wait <ms>
 *     windows                  log the visible top-level windows (class,
 *                              text) other than TDDE's, every child of a
 *                              dialog (message boxes) and Program
 *                              Manager's groups (its MDI children)
 *     press <id>               WM_COMMAND id (decimal) to the first
 *                              visible dialog (a message box's IDOK 1,
 *                              IDCANCEL 2, IDYES 6, IDNO 7), then a wait
 *     ini <file>|<sec>|<key>[|<value>]   WritePrivateProfileString;
 *                              without a value the key is deleted (before
 *                              "run PROGMAN.EXE": its PROGMAN.INI)
 *     exists <file>            log whether the file is there (OpenFile)
 * Run as the shell (tests/refrun.sh) after starting Program Manager:
 * the first script line can be "run PROGMAN.EXE".
 */
#include <windows.h>
#include <dde.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HWND me, server;
static int acks;
static BOOL got_ack, got_data, got_term;
static WORD exec_len;                   /* the command block's string length */
static char script[16384];

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outhex(WORD v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[4] = 0;
    out(b);
}

/* Text with control characters shown as \r, \n, \t, \xNN. */
static void out_escaped(const char FAR *s, WORD n)
{
    char b[5];
    WORD i;
    for (i = 0; i < n && s[i]; i++) {
        BYTE c = (BYTE)s[i];
        if (c == '\r')
            out("\\r");
        else if (c == '\n')
            out("\\n");
        else if (c == '\t')
            out("\\t");
        else if (c < 32 || c == '\\' || c == '"') {
            b[0] = '\\';
            b[1] = 'x';
            b[2] = "0123456789ABCDEF"[c >> 4];
            b[3] = "0123456789ABCDEF"[c & 15];
            b[4] = 0;
            out(b);
        } else {
            b[0] = (char)c;
            b[1] = 0;
            out(b);
        }
    }
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

LRESULT CALLBACK __export ClientWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    char cls[64], text[64];
    switch (msg) {
    case WM_DDE_ACK:
        if (!server) {                  /* answer to INITIATE (sent) */
            acks++;
            server = (HWND)wp;
            GetClassName((HWND)wp, cls, sizeof cls);
            GetWindowText((HWND)wp, text, sizeof text);
            out("DDE ack ");
            outhex(acks);
            out(" server class=\"");
            out(cls);
            out("\" text=\"");
            out(text);
            out("\" app=\"");
            GlobalGetAtomName(LOWORD(lp), text, sizeof text);
            out(text);
            out("\" topic=\"");
            GlobalGetAtomName(HIWORD(lp), text, sizeof text);
            out(text);
            out("\"\r\n");
            GlobalDeleteAtom(LOWORD(lp));
            GlobalDeleteAtom(HIWORD(lp));
            return 0;
        }
        if ((HWND)wp != server) {       /* a second server answered */
            acks++;
            PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0);
            return 0;
        }
        out(" ack=");
        outhex(LOWORD(lp));
        if (HIWORD(lp) && exec_len) {
            /* the command block as it came back (3.1's Program Manager
               writes into it), NULs shown as \x00 */
            const char FAR *b = (const char FAR *)GlobalLock((HGLOBAL)HIWORD(lp));
            if (b) {
                WORD i = 0;
                out(" block=\"");
                while (i < exec_len) {
                    WORD k = 0;
                    while (i + k < exec_len && b[i + k])
                        k++;
                    out_escaped(b + i, k);
                    i += k;
                    if (i < exec_len) {
                        out("\\x00");
                        i++;
                    }
                }
                out("\"");
                GlobalUnlock((HGLOBAL)HIWORD(lp));
            }
        }
        exec_len = 0;
        if (HIWORD(lp))
            GlobalFree((HGLOBAL)HIWORD(lp));    /* the command block */
        out("\r\n");
        got_ack = TRUE;
        return 0;
    case WM_DDE_DATA: {
        HGLOBAL h = (HGLOBAL)LOWORD(lp);
        const BYTE FAR *p = h ? (const BYTE FAR *)GlobalLock(h) : NULL;
        WORD flags = p ? p[0] | (p[1] << 8) : 0;
        out(" flags=");
        outhex(flags);
        if (p) {
            out(" fmt=");
            outhex(p[2] | (p[3] << 8));
            out(" \"");
            out_escaped((const char FAR *)p + 4, (WORD)GlobalSize(h) - 4);
            out("\"");
            GlobalUnlock(h);
        }
        out("\r\n");
        if (flags & 0x8000)             /* fAckReq */
            PostMessage((HWND)wp, WM_DDE_ACK, (WPARAM)w, MAKELPARAM(0x8000, HIWORD(lp)));
        else
            GlobalDeleteAtom(HIWORD(lp));
        if (h && (flags & 0x2000))      /* fRelease */
            GlobalFree(h);
        got_data = TRUE;
        return 0;
    }
    case WM_DDE_TERMINATE:
        if ((HWND)wp == server)
            got_term = TRUE;
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* Log one window: class and text (another task's too: WM_GETTEXT). */
static void out_window(HWND w, const char *indent)
{
    char cls[64], text[300];
    GetClassName(w, cls, sizeof cls);
    text[0] = 0;
    SendMessage(w, WM_GETTEXT, sizeof text, (LPARAM)(LPSTR)text);
    out(indent);
    out("class=\"");
    out(cls);
    out("\" text=\"");
    out_escaped(text, sizeof text);
    out("\"");
    if (indent[2]) {
        out(" id=");
        outhex(GetWindowWord(w, GWW_ID));
    }
    if (lstrcmp(cls, "PMGroup") && lstrcmp(cls, "Progman")) {
        RECT r;                         /* a dialog and its controls */
        GetWindowRect(w, &r);
        out(" rect=");
        outhex(r.left);
        out(",");
        outhex(r.top);
        out(",");
        outhex(r.right);
        out(",");
        outhex(r.bottom);
        out(" style=");
        outhex(HIWORD(GetWindowLong(w, GWL_STYLE)));
        outhex(LOWORD(GetWindowLong(w, GWL_STYLE)));
    }
    out("\r\n");
}

static void windows(void)
{
    HWND w, c;
    char cls[64];
    out("WINDOWS\r\n");
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (w == me || !IsWindowVisible(w))
            continue;
        out_window(w, "  ");
        GetClassName(w, cls, sizeof cls);
        if (!lstrcmp(cls, "#32770"))
            for (c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
                out_window(c, "    ");
        if (!lstrcmp(cls, "Progman")) {     /* its MDI client's groups */
            HWND mdi = GetWindow(w, GW_CHILD);
            for (c = mdi ? GetWindow(mdi, GW_CHILD) : NULL; c; c = GetWindow(c, GW_HWNDNEXT)) {
                GetClassName(c, cls, sizeof cls);
                if (!lstrcmp(cls, "PMGroup"))
                    out_window(c, "    ");
            }
        }
    }
}

static WORD number(const char *s)
{
    WORD v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

static void step(char *l)
{
    char *arg = l;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = 0;
    if (!lstrcmp(l, "run")) {
        WinExec(arg, SW_SHOWNORMAL);
        pump(3000, NULL);
    } else if (!lstrcmp(l, "wait")) {
        WORD ms = 0;
        while (*arg >= '0' && *arg <= '9')
            ms = ms * 10 + (*arg++ - '0');
        pump(ms, NULL);
    } else if (!lstrcmp(l, "windows")) {
        windows();
    } else if (!lstrcmp(l, "press")) {
        HWND w;
        char cls[64];
        for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
            GetClassName(w, cls, sizeof cls);
            if (IsWindowVisible(w) && !lstrcmp(cls, "#32770"))
                break;
        }
        out("press ");
        out(arg);
        out(w ? "\r\n" : " (no dialog)\r\n");
        if (w)
            PostMessage(w, WM_COMMAND, number(arg), MAKELPARAM(GetDlgItem(w, number(arg)), 0));
        pump(1500, NULL);
    } else if (!lstrcmp(l, "ini")) {
        char *f[4];
        int k = 0;
        f[0] = arg;
        f[1] = f[2] = f[3] = NULL;
        for (; *arg && k < 3; arg++)
            if (*arg == '|') {
                *arg = 0;
                f[++k] = arg + 1;
            }
        if (f[2])
            WritePrivateProfileString(f[1], f[2], f[3], f[0]);
    } else if (!lstrcmp(l, "exists")) {
        OFSTRUCT of;
        out("exists ");
        out(arg);
        out(OpenFile(arg, &of, OF_EXIST) != HFILE_ERROR ? " yes\r\n" : " no\r\n");
    } else if (!lstrcmp(l, "connect")) {
        ATOM app = GlobalAddAtom("PROGMAN"), topic = GlobalAddAtom("PROGMAN");
        server = NULL;
        acks = 0;
        SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)me, MAKELPARAM(app, topic));
        GlobalDeleteAtom(app);
        GlobalDeleteAtom(topic);
        if (!server)
            out("DDE no server\r\n");
    } else if (!lstrcmp(l, "exec") && server) {
        int n = lstrlen(arg);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
        LPSTR p = h ? GlobalLock(h) : NULL;
        if (!p)
            return;
        lstrcpy(p, arg);
        GlobalUnlock(h);
        out("DDE exec \"");
        out_escaped(arg, n);
        out("\"");
        got_ack = FALSE;
        exec_len = n;
        if (!PostMessage(server, WM_DDE_EXECUTE, (WPARAM)me, MAKELPARAM(0, h)))
            out(" post failed\r\n");
        else {
            pump(5000, &got_ack);
            if (!got_ack)
                out(" (no ack)\r\n");
        }
    } else if (!lstrcmp(l, "request") && server) {
        ATOM item = GlobalAddAtom(arg);
        out("DDE data \"");
        out(arg);
        out("\"");
        got_data = got_ack = FALSE;
        PostMessage(server, WM_DDE_REQUEST, (WPARAM)me, MAKELPARAM(CF_TEXT, item));
        pump(5000, &got_data);
        if (!got_data) {
            if (got_ack)
                out(" (negative ack)\r\n");     /* printed " ack=" above */
            else
                out(" (no answer)\r\n");
        }
    } else if (!lstrcmp(l, "disconnect") && server) {
        got_term = FALSE;
        PostMessage(server, WM_DDE_TERMINATE, (WPARAM)me, 0);
        pump(3000, &got_term);
        out(got_term ? "DDE terminated\r\n" : "DDE no terminate reply\r\n");
        server = NULL;
    }
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HFILE f;
    int n;
    char *p, *e;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    wc.style = 0;
    wc.lpfnWndProc = ClientWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)init_regs[5];
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TDDEClient";
    RegisterClass(&wc);
    me = CreateWindow("TDDEClient", "TDDE", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL,
                      (HINSTANCE)init_regs[5], NULL);
    f = _lopen("C:\\DDE.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        out("TDDE: no C:\\DDE.TXT\r\n");
        return 1;
    }
    n = _lread(f, script, sizeof script - 1);
    _lclose(f);
    script[n > 0 ? n : 0] = 0;
    for (p = script; *p; p = e) {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        if (*p && *p != ';')
            step(p);
    }
    out("TDDE done\r\n");
    return 0;
}
