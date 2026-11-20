/*
 * us29a.c - US29A.EXE, the helper task of probe US29 (tests/apps/us29.c).
 * Its command tail says what it does; it reports to US29's window (class
 * US29P) and writes nothing:
 *   c  registers US29LOC (local) and US29GLB (CS_GLOBALCLASS), sends US29
 *      WM_USER+10 (wParam: bit 0, 1 each registered; lParam: its
 *      instance) and ends
 *   w  the same, then makes a window of US29LOC, sends WM_USER+12 (lParam
 *      high word: the window) and runs until the window is closed
 *   s  makes a window and sends US29 WM_USER+1 with it; US29 sends it
 *      WM_USER+2 back while handling that, and the task ends there (DOS
 *      function 4Ch) inside the nested message
 *   r  makes a window, sends US29 WM_USER+13 with it and waits in
 *      GetMessage; it ends (DOS function 4Ch) on WM_USER+2
 *   h  loads US29D.DLL, sets its system-wide WH_GETMESSAGE hook and a
 *      task-local one, sends WM_USER+11 and ends without unhooking
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

typedef WORD (FAR PASCAL *FN1)(WORD);

void dos_exit(void);
#pragma aux dos_exit = "mov ax, 4C05h" "int 21h" aborts;

/* no __loadds: two instances share it, and it uses no data */
LRESULT FAR PASCAL __export a_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_USER + 2)
        dos_exit();
    if (m == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProc(h, m, w, l);
}

static WORD reg(HINSTANCE hi, const char *name, UINT style)
{
    WNDCLASS wc;
    wc.style = style;
    wc.lpfnWndProc = (WNDPROC)a_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name;
    return RegisterClass(&wc) != 0;
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    const char FAR *tail = (const char FAR *)MAKELP(init_regs[6], 0x81);
    HWND p, h;
    WORD bits;
    MSG m;
    WaitEvent(0);
    InitApp(hi);
    while (*tail == ' ')
        tail++;
    p = FindWindow("US29P", NULL);
    if (!p)
        return 1;
    switch (*tail) {
    case 'c':
    case 'w':
        bits = reg(hi, "US29LOC", 0) | (reg(hi, "US29GLB", CS_GLOBALCLASS) << 1);
        if (*tail == 'c') {
            SendMessage(p, WM_USER + 10, bits, MAKELONG(hi, 0));
            return 0;
        }
        h = CreateWindow("US29LOC", "w", WS_POPUP, 0, 0, 10, 10, 0, 0, hi, NULL);
        if (!h) {
            reg(hi, "US29W", 0);
            h = CreateWindow("US29W", "w", WS_POPUP, 0, 0, 10, 10, 0, 0, hi, NULL);
        }
        SendMessage(p, WM_USER + 12, bits, MAKELONG(hi, h));
        while (GetMessage(&m, 0, 0, 0))
            DispatchMessage(&m);
        return 0;
    case 'r':
        reg(hi, "US29C", 0);
        h = CreateWindow("US29C", "r", WS_POPUP, 0, 0, 10, 10, 0, 0, hi, NULL);
        SendMessage(p, WM_USER + 13, 0, MAKELONG(h, 0));
        while (GetMessage(&m, 0, 0, 0))
            DispatchMessage(&m);
        return 0;
    case 's':
        reg(hi, "US29C", 0);
        h = CreateWindow("US29C", "s", WS_POPUP, 0, 0, 10, 10, 0, 0, hi, NULL);
        SendMessage(p, WM_USER + 1, 0, MAKELONG(h, 0));
        return 0;
    case 'h': {
        HINSTANCE d = LoadLibrary("US29D.DLL");
        FN1 seta, setb;
        if (d < 32)
            return 1;
        seta = (FN1)GetProcAddress(d, "US29D_SETA");
        setb = (FN1)GetProcAddress(d, "US29D_SETB");
        seta(0);
        setb((WORD)GetCurrentTask());
        SendMessage(p, WM_USER + 11, 0, 0);
        return 0;
    }
    }
    return 0;
}
