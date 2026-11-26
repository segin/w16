/*
 * us37.c - probe US37: where the focus is after CreateDialog of a modeless
 * dialog that is not shown, or shown without activation, and what
 * DM_GETDEFID gives (Microsoft Setup's dialogs, in the Entertainment
 * Pack 4 and Best of setups: shown with SetWindowPos(SWP_SHOWWINDOW |
 * SWP_NOACTIVATE), their caption painted active with WM_NCACTIVATE; on
 * 3.1 Return pressed their default button, on w16 it did nothing,
 * 2026-11-25). Run as the shell (tests/dropin.sh -r for 3.1, -f USER.EXE
 * for w16's USER); writes TK.LOG:
 *   OBS fN.made=F,A    after CreateDialogIndirect: F the focus (the
 *                      control's id, 1 the dialog, 2 the main window, 0
 *                      none, 3 another), A the active window (1 the
 *                      dialog, 2 the main window, 0 none, 3 another)
 *   OBS fN.shown=F,A   after SetWindowPos(SWP_SHOWWINDOW | SWP_NOACTIVATE
 *                      | SWP_NOSIZE | SWP_NOMOVE)
 *   OBS fN.ret=ID      the WM_COMMAND id the dialog got for a Return
 *                      typed then (keybd_event), 0 none
 * for N: 0 the procedure answers TRUE, 1 FALSE, 2 TRUE with WS_VISIBLE in
 * the template, 3 TRUE with no default push button, 4 TRUE with the
 * main window not active (an icon); and
 *   OBS defid.D=HI,LO  DM_GETDEFID of a dialog with (D 1) and without
 *                      (D 0) a DEFPUSHBUTTON
 */
#include <windows.h>

#define TK_REF "us37ref.h"
#include "tklog.h"

extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);

static HINSTANCE hi;
static HWND hMain, hDlg;
static int variant;
static WORD gotcmd;

static void pump(int n)
{
    MSG m;
    while (n-- > 0) {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            if (!hDlg || !IsDialogMessage(hDlg, &m)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        Yield();
    }
}

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)l, (void)h;
    if (m == WM_INITDIALOG)
        return variant != 1;
    if (m == WM_COMMAND && !gotcmd)
        gotcmd = w ? w : 0xFFFF;
    return FALSE;
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

static int who(HWND f)
{
    if (!f)
        return 0;
    if (f == hDlg)
        return 1;
    if (f == hMain)
        return 2;
    if (hDlg && GetParent(f) == hDlg)
        return GetWindowWord(f, GWW_ID);
    return 3;
}

static void obs2(const char *name, int a, int b)
{
    char t[40];
    wsprintf(t, "OBS %s=%d,%d\r\n", (LPSTR)name, a, b);
    out(t);
}

/* a template: two push buttons 11 and 12, 12 the default one if def */
static BYTE *make_template(BYTE *p, DWORD style, BOOL def)
{
    int k;
    *(DWORD *)p = style; p += 4;
    *p++ = 2;
    *(int *)p = 20; p += 2; *(int *)p = 20; p += 2;
    *(int *)p = 100; p += 2; *(int *)p = 60; p += 2;
    *p++ = 0;                                           /* menu */
    *p++ = 0;                                           /* class */
    *p++ = 'D'; *p++ = 0;
    for (k = 11; k <= 12; k++) {
        *(int *)p = 5; p += 2; *(int *)p = 5 + (k - 11) * 16; p += 2;
        *(int *)p = 40; p += 2; *(int *)p = 14; p += 2;
        *(WORD *)p = k; p += 2;
        *(DWORD *)p = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                      (def && k == 12 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON); p += 4;
        *p++ = 0x80;
        *p++ = 'B'; *p++ = 0;
        *p++ = 0;
    }
    return p;
}

int __cdecl tmain(void)
{
    static BYTE t[160];
    WNDCLASS wc;
    char nm[24];
    DWORD r;

    hi = (HINSTANCE)init_regs[5];
    InitApp(hi);
    out("US37 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US37";
    RegisterClass(&wc);
    hMain = CreateWindow("US37", "US37", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 300,
                         0, 0, hi, NULL);
    pump(30);
    for (variant = 0; variant < 5; variant++) {
        make_template(t, WS_POPUP | WS_CAPTION | (variant == 2 ? WS_VISIBLE : 0), variant != 3);
        if (variant == 4) {
            ShowWindow(hMain, SW_SHOWMINNOACTIVE);
            SetActiveWindow(0);
            pump(20);
        }
        hDlg = 0;
        gotcmd = 0;
        hDlg = CreateDialogIndirect(hi, (LPCSTR)t, hMain, (DLGPROC)dlg_proc);
        wsprintf(nm, "f%d.made", variant);
        obs2(nm, who(GetFocus()), who(GetActiveWindow()));
        SetWindowPos(hDlg, 0, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
        pump(30);
        wsprintf(nm, "f%d.shown", variant);
        obs2(nm, who(GetFocus()), who(GetActiveWindow()));
        call_keybd_event(VK_RETURN, 0x1C);
        call_keybd_event(0x8000 | VK_RETURN, 0x1C);
        pump(60);
        wsprintf(nm, "f%d.ret", variant);
        obs2(nm, gotcmd, 0);
        DestroyWindow(hDlg);
        hDlg = 0;
        pump(20);
        ShowWindow(hMain, SW_RESTORE);
        SetActiveWindow(hMain);
        SetFocus(hMain);
        pump(20);
    }
    for (variant = 0; variant < 2; variant++) {
        make_template(t, WS_POPUP | WS_CAPTION, variant);
        hDlg = CreateDialogIndirect(hi, (LPCSTR)t, hMain, (DLGPROC)dlg_proc);
        r = SendMessage(hDlg, DM_GETDEFID, 0, 0);
        wsprintf(nm, "defid.%d", variant);
        obs2(nm, HIWORD(r), LOWORD(r));
        DestroyWindow(hDlg);
        hDlg = 0;
    }
    out("US37 end\r\n");
    return 0;
}
