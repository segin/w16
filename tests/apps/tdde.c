/*
 * tdde.c - Program Manager DDE probe (black-box observation).
 *
 * Talks to the "PROGMAN" DDE service the way an installer does, with the
 * raw DDE messages of the Windows SDK, and logs every answer to TK.LOG:
 *     DDE ack <n> server class="..." text="..."      replies to INITIATE
 *     DDE exec "<command>" ack=<status word>         WM_DDE_ACK status
 *     DDE data "<item>" flags=<word> fmt=<n> "<text, CR/LF shown as \r\n>"
 * The script C:\DDE.TXT holds one line per step:
 *     exec <command string>        WM_DDE_EXECUTE
 *     request <item>               WM_DDE_REQUEST, CF_TEXT
 *     wait <ms>
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
static char script[4096];

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
