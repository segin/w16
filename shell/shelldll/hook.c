/*
 * hook.c - w16 SHELL.DLL: RegisterShellHook (SHELL.102) and ShellHookProc
 * (SHELL.103), the WH_SHELL hook Program Manager and WINOLDAP use to
 * hear about top-level windows (ARCHITECTURE.md 9.4: USER calls
 * SetWindowsHook for it while Program Manager handles WM_CREATE).
 *
 * Observed (probe SH9): RegisterShellHook(hwnd, n) with n != 0 adds hwnd
 * to a list, again on every call (a window registered twice gets each
 * message twice); n == 0 takes one registration of hwnd off. It returns
 * 1 always. HSHELL_WINDOWCREATED (1) and HSHELL_WINDOWDESTROYED (2) post
 * the registered messages "OTHERWINDOWCREATED" and "OTHERWINDOWDESTROYED"
 * to every registration, wParam the window, lParam 0. The hook returns 0.
 * SHELL also registers "ACTIVATESHELLWINDOW"; HSHELL_ACTIVATESHELLWINDOW
 * (3) sent nothing to the registered window and activated nothing in SH9
 * (UNVERIFIED: what it is for).
 */
#include "shell.h"

#ifndef WH_SHELL
#define WH_SHELL 10
#endif

static HGLOBAL h_list;
static WORD n_list, cap_list;
static UINT msg_created, msg_destroyed, msg_activate;
static HOOKPROC prev_hook;
static int hooked;

LRESULT SHAPI ShellHookProc(int code, WPARAM wp, LPARAM lp);

/* the messages are registered at the first RegisterShellHook (SH9: their
   numbers came after those of the caller's own window class) */
void hook_init(void)
{
}

static void register_messages(void)
{
    if (msg_created)
        return;
    msg_created = RegisterWindowMessage("OTHERWINDOWCREATED");
    msg_destroyed = RegisterWindowMessage("OTHERWINDOWDESTROYED");
    msg_activate = RegisterWindowMessage("ACTIVATESHELLWINDOW");
}

BOOL SHAPI RegisterShellHook(HWND w, UINT on)
{
    HWND __far *l;
    WORD i;
    register_messages();
    if (on) {
        if (n_list == cap_list) {
            WORD cap = cap_list ? cap_list * 2 : 8;
            HGLOBAL h = h_list ? GlobalReAlloc(h_list, (DWORD)cap * 2, GMEM_MOVEABLE)
                               : GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, (DWORD)cap * 2);
            if (!h || cap < cap_list)
                return TRUE;
            h_list = h;
            cap_list = cap;
        }
        l = (HWND __far *)GlobalLock(h_list);
        l[n_list++] = w;
        GlobalUnlock(h_list);
        if (!hooked) {
            prev_hook = SetWindowsHook(WH_SHELL, (HOOKPROC)ShellHookProc);
            hooked = 1;
        }
        return TRUE;
    }
    if (!h_list)
        return TRUE;
    l = (HWND __far *)GlobalLock(h_list);
    for (i = 0; i < n_list; i++)
        if (l[i] == w) {
            sh_move(l + i, l + i + 1, (n_list - i - 1) * 2);
            n_list--;
            break;
        }
    GlobalUnlock(h_list);
    return TRUE;
}

LRESULT SHAPI ShellHookProc(int code, WPARAM wp, LPARAM lp)
{
    UINT m = code == 1 ? msg_created : code == 2 ? msg_destroyed : 0;
    if (m && h_list) {
        HWND __far *l = (HWND __far *)GlobalLock(h_list);
        WORD i;
        for (i = 0; i < n_list; i++)
            PostMessage(l[i], m, wp, 0);
        GlobalUnlock(h_list);
    }
    if (hooked)
        DefHookProc(code, wp, lp, &prev_hook);
    return 0;
}
