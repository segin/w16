/*
 * us7.c - probe US7: how WinHelp reaches WINHELP.EXE. Run as the shell
 * on Windows 3.1 with tests/apps/thelp.c standing in for WINHELP.EXE
 * (REFRUN_FILES=build/dos/THELP.EXE=WINDOWS/WINHELP.EXE): it logs what
 * WinHelp returns for HELP_QUIT before any help window exists, then for
 * HELP_INDEX and HELP_QUIT, whether a window of class MS_WINHELP exists
 * before and after, and the number RegisterWindowMessage gives for
 * "WM_WINHELP" (UNVERIFIED recall of the name: the stand-in logs the
 * number of the message it receives, and the two must agree).
 */
#include <windows.h>

#define TK_REF "us7ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    HWND w;
    WaitEvent(0);
    InitApp(hi);
    out("US7 start\r\n");
    obs("quit-before", (WORD)WinHelp(NULL, "TEST.HLP", HELP_QUIT, 0L));
    obs("findwindow-before", FindWindow("MS_WINHELP", NULL) != 0);
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US7";
    RegisterClass(&wc);
    w = CreateWindow("US7", "US7", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, hi, NULL);
    obs("index", (WORD)WinHelp(w, "TEST.HLP", HELP_INDEX, 0L));
    obs("findwindow-after", FindWindow("MS_WINHELP", NULL) != 0);
    obs("msg WM_WINHELP", (WORD)RegisterWindowMessage("WM_WINHELP"));
    obs("quit", (WORD)WinHelp(w, "TEST.HLP", HELP_QUIT, 0L));
    DestroyWindow(w);
    out("US7 end\r\n");
    return 0;
}
