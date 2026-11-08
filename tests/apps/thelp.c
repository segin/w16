/*
 * thelp.c - a stand-in WINHELP.EXE that logs what it is asked (black-box
 * observation of how programs call WinHelp).
 *
 * Copied over WINHELP.EXE in a scratch copy of the reference install
 * (tests/refrun.sh REFRUN_FILES=...=WINDOWS/WINHELP.EXE). It logs its
 * command line, then every message its window receives, to TK.LOG:
 *     HELP cmdline "<text>"
 *     HELP msg <msg> wp=<word> lp=<hi>:<lo>
 *     HELP block <handle> size=<n>: <bytes in hex>      if lp names one
 *     HELP text "<string>"                               strings in it
 * The window class is the one USER looks for (UNVERIFIED recall:
 * "MS_WINHELP"; the log shows whether messages arrive). It runs until
 * WM_CLOSE or the end of the session: a second copy would start the log
 * afresh.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outhex(WORD v, int digits)
{
    char b[5];
    int i;
    for (i = digits - 1; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[digits] = 0;
    out(b);
}

/* Printable runs of 3 or more characters in a block, as strings. */
static void strings(const BYTE FAR *p, WORD n)
{
    WORD i = 0, j;
    while (i < n) {
        for (j = i; j < n && p[j] >= 0x20 && p[j] < 0x7F; j++)
            ;
        if (j - i >= 3 && (j == n || p[j] == 0)) {
            char c[2];
            WORD k;
            out("HELP text @");
            outhex(i, 4);
            out(" \"");
            c[1] = 0;
            for (k = i; k < j; k++) {
                c[0] = p[k];
                out(c);
            }
            out("\"\r\n");
        }
        i = j + 1;
    }
}

static void block(WORD h)
{
    DWORD size;
    const BYTE FAR *p;
    WORD i, n;
    if (!h)
        return;
    size = GlobalSize((HGLOBAL)h);
    if (!size)
        return;
    p = (const BYTE FAR *)GlobalLock((HGLOBAL)h);
    if (!p)
        return;
    n = size > 256 ? 256 : (WORD)size;          /* enough to see the request */
    out("HELP block ");
    outhex(h, 4);
    out(" size=");
    outhex((WORD)size, 4);
    out(":");
    for (i = 0; i < n; i++) {
        out(" ");
        outhex(p[i], 2);
    }
    out("\r\n");
    strings(p, n);
    GlobalUnlock((HGLOBAL)h);
}

LRESULT CALLBACK __export HelpWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE: case WM_CREATE: case WM_NCCALCSIZE: case WM_GETMINMAXINFO:
    case WM_PAINT: case WM_NCPAINT: case WM_ERASEBKGND: case WM_TIMER:
    case WM_NCHITTEST: case WM_SETCURSOR: case WM_MOUSEMOVE:
        break;                          /* noise */
    default:
        out("HELP msg ");
        outhex(msg, 4);
        out(" wp=");
        outhex(wp, 4);
        out(" lp=");
        outhex(HIWORD(lp), 4);
        out(":");
        outhex(LOWORD(lp), 4);
        out("\r\n");
        if (msg >= WM_USER || msg < 0x100) {
            block(LOWORD(lp));
            if (HIWORD(lp) && HIWORD(lp) != LOWORD(lp))
                block(HIWORD(lp));
        }
        if (msg == WM_CLOSE) {
            DestroyWindow(w);
            return 0;
        }
        if (msg == WM_DESTROY)
            PostQuitMessage(0);
        break;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE inst;
    MSG m;
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);     /* ES:BX */

    WaitEvent(0);
    inst = (HINSTANCE)init_regs[5];
    InitApp(inst);
    out("HELP cmdline \"");
    out(tail);
    out("\"\r\n");
    wc.style = 0;
    wc.lpfnWndProc = HelpWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MS_WINHELP";
    RegisterClass(&wc);
    CreateWindow("MS_WINHELP", "Help", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                 NULL, NULL, inst, NULL);       /* hidden */
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
