/*
 * tk56.c - OutputDebugString (KERNEL.115) with no debugger: where the
 * text goes. Run as the shell (tests/dropin.sh, -r for Microsoft's
 * KRNL286; tests/refrun.sh for KRNL386) with DOSBox's COM1 captured to a
 * file (DROPIN_CONF='[serial]' 'serial1=file file:...'): the screen, WIN's
 * redirected output (C:\OUT.TXT) and COM1 tell. It then waits for ever,
 * so the screen can be compared (boot-test case odstring); with the
 * command tail "x" it ends after 3 seconds, with "q" it calls nothing.
 */
#include <windows.h>

#define TK_REF "tk56ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static char s1[] = "TK56 OutputDebugString one\r\n";
static char s2[] = "TK56 two, no line end";

int __cdecl tmain(void)
{
    MSG m;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("TK56 start\r\n");
    {
        /* command tail "q": no OutputDebugString (the screen to compare
           w16's default with: boot-test odstring) */
        char FAR *t = (char FAR *)MAKELP(init_regs[6], init_regs[1]);
        if (t[0] != 'q') {
            OutputDebugString(s1);
            OutputDebugString(s2);
            OutputDebugString("\r\n");
        }
    }
    out("TK56 called\r\n");
    {
        /* with "x" as the command tail: end after 3 seconds (Windows then
           ends: the files are complete); else wait for ever */
        char FAR *tail = (char FAR *)MAKELP(init_regs[6], init_regs[1]);
        if (tail[0] == 'x')
            SetTimer(0, 1, 3000, NULL);
    }
    while (GetMessage(&m, 0, 0, 0)) {
        if (m.message == WM_TIMER)
            break;
        DispatchMessage(&m);
    }
    return 0;
}
