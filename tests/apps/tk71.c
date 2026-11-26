/*
 * tk71.c - the registers USER calls a window procedure with. StarOffice
 * 3.1's StarView (SV312MW.DLL) faulted on w16's USER while it handled its
 * own message 046Eh: the value it loaded as a selector was the AX its
 * window procedure was called with. TK71's window procedure starts in
 * tk71s.asm, which notes AX BX CX DX SI DI ES DS at entry; for each
 * message below the log says what each register held, against the
 * message's hWnd, msg, wParam and lParam, the program's DS, its hInstance
 * and 0 (registers matching none: "other", the value not logged).
 * Run as the shell (boot-test case wpregs): CreateWindow (WM_CREATE and
 * the rest sent during it), SendMessage of WM_USER+1, PostMessage of
 * WM_USER+2 delivered by DispatchMessage, SendMessage from the procedure
 * itself (WM_USER+3, nested), CallWindowProc (WM_USER+4), and a second
 * window of the same (global) class made with USER's hInstance
 * (WM_USER+5: "inst2DS" is that instance's data segment); then the
 * messages CreateWindow itself sends ("S" lines) to hidden and visible
 * children, a visible overlapped window and popups.
 * Writes C:\TK71.LOG.
 */
#include <windows.h>

extern WORD __cdecl tk71_regs[8];       /* ax bx cx dx si di es ds */
extern WORD __cdecl init_regs[7];       /* InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL INITAPP(HINSTANCE inst);                 /* USER.5 */
extern LRESULT FAR PASCAL TK71_ENTRY(HWND, UINT, WPARAM, LPARAM);

static char line[300];
static int ln;
static WORD ds_self, hinst, inst2;
static HWND main_wnd;

static void put(const char *s)
{
    while (*s)
        line[ln++] = *s++;
}

static void hex(WORD v)
{
    int i;
    for (i = 3; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK71.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK71.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

/* what a register's value is, against the call */
static void what(const char *name, WORD v, HWND h, UINT m, WPARAM w, LPARAM l)
{
    put(" ");
    put(name);
    put("=");
    if (v == (WORD)h)
        put("hwnd");
    else if (v == m)
        put("msg");
    else if (v == w && w)
        put("wParam");
    else if (v == LOWORD(l) && LOWORD(l))
        put("lParamLo");
    else if (v == HIWORD(l) && HIWORD(l))
        put("lParamHi");
    else if (v == ds_self)
        put("DS");
    else if (v == hinst)
        put("hInst");
    else if (v == (inst2 | 1))
        put("inst2DS");
    else if (v == inst2)
        put("inst2");
    else if (v == 0)
        put("0");
    else
        put("other");
}

static int logging;
static int seqlog;              /* every message's number, one line */

static void seq_flush(const char *what)
{
    put(" | ");
    put(what);
    flush();
}

LRESULT FAR PASCAL __loadds TK71_PROC(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WORD r[8];
    int i;
    for (i = 0; i < 8; i++)
        r[i] = tk71_regs[i];
    if (seqlog) {
        put(" ");
        hex(m);
        if (ln > 250) {
            put(" +");
            flush();
        }
    }
    if (logging && (m == WM_CREATE || m == WM_NCCREATE || m == WM_SIZE ||
                    m == WM_SHOWWINDOW || m >= WM_USER)) {
        put("M ");
        hex(m);
        what("AX", r[0], h, m, w, l);
        what("BX", r[1], h, m, w, l);
        what("CX", r[2], h, m, w, l);
        what("DX", r[3], h, m, w, l);
        what("SI", r[4], h, m, w, l);
        what("DI", r[5], h, m, w, l);
        what("ES", r[6], h, m, w, l);
        what("DS", r[7], h, m, w, l);
        flush();
    }
    if (m == WM_USER + 1)
        SendMessage(h, WM_USER + 3, 0x3333, 0x33334444L);
    if (m >= WM_USER)
        return 0;
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    MSG msg;
    int i;

    hinst = init_regs[5];
    ds_self = hinst | 1;
    INITAPP((HINSTANCE)hinst);
    _asm {
        mov ax, ds
        mov ds_self, ax
    }
    put("T start");
    flush();
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = (WNDPROC)TK71_ENTRY;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 4;
    wc.hInstance = (HINSTANCE)hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TK71";
    RegisterClass(&wc);
    logging = 1;
    main_wnd = CreateWindow("TK71", "TK71", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0,
                            (HINSTANCE)hinst, NULL);
    put("T created");
    flush();
    ShowWindow(main_wnd, SW_SHOWNORMAL);
    SendMessage(main_wnd, WM_USER + 1, 0x1111, 0x11112222L);
    PostMessage(main_wnd, WM_USER + 2, 0x2222, 0x22223333L);
    for (i = 0; i < 200 && PeekMessage(&msg, 0, 0, 0, PM_REMOVE); i++)
        DispatchMessage(&msg);
    CallWindowProc((FARPROC)TK71_ENTRY, main_wnd, WM_USER + 4, 0x4444, 0x44445555L);
    /* a window of another instance (USER's) with the same procedure */
    inst2 = (WORD)LoadLibrary("USER.EXE");
    {
        HWND w2 = CreateWindow("TK71", "TK71b", WS_OVERLAPPEDWINDOW, 0, 0, 100, 50, 0, 0,
                               (HINSTANCE)inst2, NULL);
        put("T second");
        flush();
        SendMessage(w2, WM_USER + 5, 0x5555, 0x55556666L);
        logging = 0;
        DestroyWindow(w2);
    }
    logging = 0;
    /* the messages CreateWindow sends: a child, hidden and visible, of
       the main window; a visible top-level window; a hidden popup */
    {
        static const struct { DWORD style; int child; const char *name; } k[] = {
            { WS_CHILD, 1, "child hidden" },
            { WS_CHILD | WS_VISIBLE, 1, "child visible" },
            { WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, "overlapped visible" },
            { WS_POPUP, 0, "popup hidden" },
            { WS_POPUP | WS_VISIBLE, 0, "popup visible" },
        };
        for (i = 0; i < 5; i++) {
            HWND w;
            put("S");
            seqlog = 1;
            w = CreateWindow("TK71", "x", k[i].style, 5, 6, 50, 40, k[i].child ? main_wnd : 0,
                             (HMENU)(k[i].child ? 7 : 0), (HINSTANCE)hinst, NULL);
            seqlog = 0;
            seq_flush(k[i].name);
            DestroyWindow(w);
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
                DispatchMessage(&msg);
        }
    }
    /* WM_MDICREATE: the messages the new child gets in it, for a child
       style of 0, WS_VISIBLE and WS_MAXIMIZE */
    {
        CLIENTCREATESTRUCT ccs;
        MDICREATESTRUCT mc;
        HWND cl;
        static const DWORD st[3] = { 0, WS_VISIBLE, WS_MAXIMIZE };
        static const char *const sn[3] = { "mdi 0", "mdi visible", "mdi maximize" };
        ShowWindow(main_wnd, SW_SHOWNORMAL);
        ccs.hWindowMenu = 0;
        ccs.idFirstChild = 100;
        cl = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                          0, 0, 150, 60, main_wnd, (HMENU)1, (HINSTANCE)hinst,
                          (LPSTR)&ccs);
        for (i = 0; i < 3; i++) {
            HWND c;
            mc.szClass = "TK71";
            mc.szTitle = "c";
            mc.hOwner = (HINSTANCE)hinst;
            mc.x = mc.y = mc.cx = mc.cy = (int)CW_USEDEFAULT;
            mc.style = st[i];
            mc.lParam = 0;
            put("S");
            seqlog = 1;
            c = (HWND)LOWORD(SendMessage(cl, WM_MDICREATE, 0, (LPARAM)(LPSTR)&mc));
            seqlog = 0;
            put(" | ");
            put(sn[i]);
            put(IsWindowVisible(c) ? " visible" : " hidden");
            put((HWND)LOWORD(SendMessage(cl, WM_MDIGETACTIVE, 0, 0)) == c ? " active" : " not active");
            flush();
            SendMessage(cl, WM_MDIDESTROY, (WPARAM)c, 0);
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
                DispatchMessage(&msg);
        }
        DestroyWindow(cl);
    }
    DestroyWindow(main_wnd);
    put("T done");
    flush();
    return 0;
}
