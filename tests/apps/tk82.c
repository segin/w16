/*
 * tk82.c - a Windows program that ends with INT 21h AH=4Ch while its
 * windows are still there, as a C runtime's exit does: what its window
 * procedures get while USER cleans up after it, and what is left.
 * Adobe Acrobat Reader 2.0, which ends this way after a message box when
 * Adobe Type Manager is missing, faulted w16 there.
 *
 * Run as the shell (no command tail): TK82 starts TK82A.EXE, the same
 * program as a module of its own, once per case (WinExec
 * "C:\TK82A.EXE x"), waits for it to end, and logs what it left. Each case makes a visible top-level window (M), a child
 * of it (C) and an owned popup (P), each of its own class, sets INT 0
 * through DOS3Call AX=2500h (as C runtimes do at their start and put back
 * at their exit) and ends through DOS3Call AX=4C00h:
 *   a   from the main window's procedure, in a posted message
 *   b   from the start-up code after the message loop, windows left
 *   c   from a WM_TIMER while a MessageBox owned by the main window is up
 *   d   as b, the main window hidden first
 * From just before the exit call every message any of its windows gets
 * is logged ("x M 0002": case, window, message).
 * The shell logs per case:
 *   x E 1             the case has ended (its module is gone)
 *   x W M|C|P n       IsWindow of each window after the end (0: gone)
 *   x V same|changed  INT 0's vector against the one before the case
 * and "T done"; C:\TK82.END tells the test to stop.
 * Writes C:\TK82.LOG.
 */
#include <windows.h>
#include <dos.h>

extern WORD __cdecl init_regs[7];       /* InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL INITAPP(HINSTANCE inst);                 /* USER.5 */
extern void FAR PASCAL DOS3CALL(void);                          /* KERNEL.102 */

static WORD hinst;
static char mode;                       /* the case; 0 in the shell */
static int gone;                        /* the exit call is near: log */
static HWND wnd_m, wnd_c, wnd_p;

static void put(const char *s)
{
    HFILE f = _lopen("C:\\TK82.LOG", OF_READWRITE);
    int n = 0;
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK82.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    while (s[n])
        n++;
    _lwrite(f, s, n);
    _lclose(f);
}

static void line3(char a, char b, const char *s, WORD v, int hex)
{
    char t[40];
    int n = 0, i;
    t[n++] = a;
    t[n++] = ' ';
    t[n++] = b;
    if (s) {
        t[n++] = ' ';
        while (*s)
            t[n++] = *s++;
    }
    if (hex >= 0) {
        t[n++] = ' ';
        if (hex)
            for (i = 12; i >= 0; i -= 4)
                t[n++] = "0123456789ABCDEF"[(v >> i) & 15];
        else
            t[n++] = (char)('0' + v);
    }
    t[n++] = '\r';
    t[n++] = '\n';
    t[n] = 0;
    put(t);
}

static void __far __interrupt int0h(void)
{
}

static void set_int0(WORD seg, WORD off);
#pragma aux set_int0 = \
    "push ds" \
    "mov ds, cx" \
    "mov ax, 2500h" \
    "call far ptr DOS3CALL" \
    "pop ds" \
    parm [cx] [dx] modify [ax];

static void dos_exit(void);
#pragma aux dos_exit = \
    "mov ax, 4C00h" \
    "call far ptr DOS3CALL" \
    aborts;

static DWORD get_int0(void);
#pragma aux get_int0 = \
    "push es" \
    "mov ax, 3500h" \
    "int 21h" \
    "mov dx, es" \
    "mov ax, bx" \
    "pop es" \
    value [dx ax] modify [bx];

/* what a C runtime's exit does: put INT 0 back, end the task */
static void crt_exit(void)
{
    void (__far __interrupt *p)(void) = int0h;
    gone = 1;
    put("  exit\r\n");
    set_int0(FP_SEG(p), FP_OFF(p));
    dos_exit();
}

static LRESULT common(char tag, HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (gone)
        line3(mode, tag, NULL, m, 1);
    return DefWindowProc(h, m, w, l);
}

LRESULT FAR PASCAL __export __loadds TK82_M(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (!gone && m == WM_USER + 1 && mode == 'a')
        crt_exit();
    if (!gone && m == WM_TIMER && mode == 'c') {
        KillTimer(h, 1);
        crt_exit();
    }
    return common('M', h, m, w, l);
}

LRESULT FAR PASCAL __export __loadds TK82_C(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return common('C', h, m, w, l);
}

LRESULT FAR PASCAL __export __loadds TK82_P(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return common('P', h, m, w, l);
}

/* the shell's hidden window: a case reports its windows (wParam M, C, P) */
static HWND rep[3];

LRESULT FAR PASCAL __export __loadds TK82_S(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_USER + 2 && w < 3)
        rep[w] = (HWND)LOWORD(l);
    return DefWindowProc(h, m, w, l);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

static void reg(const char *name, WNDPROC p)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = p;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name;
    RegisterClass(&wc);
}

static void run_case(void)
{
    HWND s;
    MSG msg;
    reg("TK82M", (WNDPROC)TK82_M);
    reg("TK82C", (WNDPROC)TK82_C);
    reg("TK82P", (WNDPROC)TK82_P);
    wnd_m = CreateWindow("TK82M", "TK82", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 300, 200,
                         0, 0, (HINSTANCE)hinst, NULL);
    wnd_c = CreateWindow("TK82C", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 100, 50,
                         wnd_m, (HMENU)1, (HINSTANCE)hinst, NULL);
    wnd_p = CreateWindow("TK82P", "TK82 popup", WS_POPUP | WS_CAPTION | WS_VISIBLE, 200, 150, 200, 100,
                         wnd_m, 0, (HINSTANCE)hinst, NULL);
    s = FindWindow("TK82S", NULL);
    if (s) {
        SendMessage(s, WM_USER + 2, 0, (LPARAM)(WORD)wnd_m);
        SendMessage(s, WM_USER + 2, 1, (LPARAM)(WORD)wnd_c);
        SendMessage(s, WM_USER + 2, 2, (LPARAM)(WORD)wnd_p);
    }
    {
        void (__far __interrupt *p)(void) = int0h;
        set_int0(FP_SEG(p), FP_OFF(p) + 0);  /* a start-up's own handler */
    }
    line3(mode, 'S', "start", 0, -1);
    switch (mode) {
    case 'a':
        PostMessage(wnd_m, WM_USER + 1, 0, 0);
        while (GetMessage(&msg, 0, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        break;
    case 'c':
        SetTimer(wnd_m, 1, 200, NULL);
        MessageBox(wnd_m, "TK82", "TK82", MB_OK);
        break;
    case 'd':
        ShowWindow(wnd_m, SW_HIDE);
        /* fall through */
    default:
        pump();
        break;
    }
    crt_exit();
}

static char tailchar(void)
{
    /* the command tail, PSP 80h (InitTask's ES = the PSP) */
    BYTE __far *t = (BYTE __far *)MK_FP(init_regs[6], 0x81);
    int i;
    for (i = 0; i < 16 && t[i] != '\r'; i++)
        if (t[i] >= 'a' && t[i] <= 'z')
            return (char)t[i];
    return 0;
}

int __cdecl tmain(void)
{
    static const char cases[] = "abcd";
    char cmd[20];
    HWND sh;
    int i, k;

    hinst = init_regs[5];
    INITAPP((HINSTANCE)hinst);
    mode = tailchar();
    if (mode) {
        run_case();
        return 0;
    }
    put("T start\r\n");
    reg("TK82S", (WNDPROC)TK82_S);
    sh = CreateWindow("TK82S", "", WS_POPUP, 0, 0, 10, 10, 0, 0, (HINSTANCE)hinst, NULL);
    for (i = 0; cases[i]; i++) {
        DWORD v0 = get_int0(), v1;
        DWORD t;
        lstrcpy(cmd, "C:\\TK82A.EXE x");
        cmd[13] = cases[i];
        rep[0] = rep[1] = rep[2] = 0;
        if (WinExec(cmd, SW_SHOWNORMAL) < 32) {
            line3(cases[i], 'X', "WinExec failed", 0, -1);
            continue;
        }
        /* the case's module goes when it ends; at most about 20 s */
        t = GetTickCount();
        while (GetModuleHandle("TK82A") && GetTickCount() - t < 20000) {
            pump();
            Yield();
        }
        pump();
        line3(cases[i], 'E', NULL, GetModuleHandle("TK82A") == 0, 0);
        for (k = 0; k < 3; k++) {
            char nm[3];
            nm[0] = "MCP"[k];
            nm[1] = 0;
            line3(cases[i], 'W', nm, rep[k] ? IsWindow(rep[k]) != 0 : 9, 0);
        }
        v1 = get_int0();
        line3(cases[i], 'V', v1 == v0 ? "same" : "changed", 0, -1);
    }
    DestroyWindow(sh);
    put("T done\r\n");
    _lclose(_lcreat("C:\\TK82.END", 0));
    return 0;
}
