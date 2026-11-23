/*
 * vmth.c - VMTH.EXE: a Windows program that will not close, for the VM
 * Task Manager's End Task (ARCHITECTURE.md 15.12; tests/vmtaskend.sh). Its
 * window ("VMTH - will not close") answers WM_CLOSE by doing nothing, as a
 * program that has stopped responding to it would; it goes on taking
 * messages, so Windows itself goes on. Only TerminateApp ends it. It
 * writes "VMTH start", each WM_CLOSE ("VMTH WM_CLOSE ignored") and, every
 * second, "VMTH alive N" to TK.LOG, so a test sees when it ended.
 * Black box: the published API only.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static int alive;

static void out(const char *s)
{
    log_write(s, lstrlen(s));
}

LRESULT FAR PASCAL __export HungProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[40];
    switch (msg) {
    case WM_CLOSE:
        out("VMTH WM_CLOSE ignored\r\n");
        return 0;
    case WM_TIMER:
        wsprintf(t, "VMTH alive %d\r\n", ++alive);
        out(t);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    MSG m;
    HWND w;
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    wc.style = 0;
    wc.lpfnWndProc = HungProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "VMTH";
    if (!RegisterClass(&wc)) return 1;
    w = CreateWindow("VMTH", "VMTH - will not close", WS_OVERLAPPEDWINDOW, 300, 330, 300, 100,
                     NULL, NULL, hi, NULL);
    if (!w) return 1;
    ShowWindow(w, SW_SHOWNOACTIVATE);
    SetTimer(w, 1, 1000, NULL);
    out("VMTH start\r\n");
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
