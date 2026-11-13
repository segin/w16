/*
 * us21.c - probe US21: Microsoft's Notepad's data segment around its edit
 * control's text (observation for user/edit.c; ARCHITECTURE.md 10.3).
 *
 * Run as the shell. Starts NOTEPAD.EXE C:\NPFILE.TXT, waits for its
 * window, then lists the local heap of the segment the edit control's
 * hInstance names (TOOLHELP's LocalFirst/LocalNext: handle, offset, size,
 * flags, lock count) with EM_GETHANDLE's handle marked; then types 40
 * characters at the end of the text (WM_CHAR) and lists it again. The log
 * (TK.LOG) shows whether the text's block is the heap's last and how it
 * grows.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "us20ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    while (GetTickCount() < end) {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

static void walk(const char *tag, HWND e)
{
    LOCALENTRY le;
    HGLOBAL ds = (HGLOBAL)GetWindowWord(e, GWW_HINSTANCE);
    WORD th = (WORD)SendMessage(e, WM_USER + 13, 0, 0);
    char b[120];
    BOOL ok;
    wsprintf(b, "%s: hInstance %04X, GlobalSize %lu, text handle %04X, length %d\r\n", (LPSTR)tag,
             (WORD)ds, GlobalSize(ds), th, GetWindowTextLength(e));
    out(b);
    le.dwSize = sizeof le;
    for (ok = LocalFirst(&le, ds); ok; ok = LocalNext(&le)) {
        wsprintf(b, "  %04X at %04X size %5u flags %04X lock %u type %u%s\r\n", (WORD)le.hHandle,
                 le.wAddress, le.wSize, le.wFlags, le.wcLock, le.wType,
                 (WORD)le.hHandle == th ? (LPSTR)"  <- text" : (LPSTR)"");
        out(b);
    }
}

int __cdecl tmain(void)
{
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    HWND np = 0, e;
    int i;
    WaitEvent(0);
    InitApp(hinst);
    out("US21 start\r\n");
    WinExec("NOTEPAD.EXE C:\\NPFILE.TXT", SW_SHOWNORMAL);
    for (i = 0; i < 100 && !np; i++) {
        pump(100);
        np = FindWindow("Notepad", NULL);
    }
    pump(2000);
    e = np ? GetWindow(np, GW_CHILD) : 0;
    if (!e) {
        out("no Notepad\r\n");
        return 0;
    }
    walk("loaded", e);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(32767, 32767));
    for (i = 0; i < 40; i++)
        SendMessage(e, WM_CHAR, 'x', 1L);
    walk("typed", e);
    out("US21 end\r\n");
    return 0;
}
