/*
 * us23a.c - US23A.EXE, the helper task of probe US23 (tests/apps/us23.c).
 * Command tail "0": it only starts and ends. "1": it makes 1500 menus
 * with a 40-character item each and 400 windows, destroys them all, and
 * ends: USER's heaps have grown and are empty again when the task ends.
 * It writes nothing.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    const char FAR *tail = (const char FAR *)MAKELP(init_regs[6], 0x81);
    static HMENU m[1500];
    static HWND w[400];
    WNDCLASS wc;
    HWND top;
    int i;
    WaitEvent(0);
    InitApp(hi);
    while (*tail == ' ')
        tail++;
    if (*tail != '1')
        return 0;
    for (i = 0; i < 1500; i++) {
        m[i] = CreatePopupMenu();
        AppendMenu(m[i], MF_STRING, 1, "0123456789012345678901234567890123456789");
    }
    for (i = 0; i < 1500; i++)
        DestroyMenu(m[i]);
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US23A";
    RegisterClass(&wc);
    top = CreateWindow("US23A", "", WS_OVERLAPPED, 0, 0, 50, 50, NULL, NULL, hi, NULL);
    for (i = 0; i < 400; i++)
        w[i] = CreateWindow("US23A", "", WS_CHILD, 0, 0, 5, 5, top, (HMENU)i, hi, NULL);
    DestroyWindow(top);
    return 0;
}
