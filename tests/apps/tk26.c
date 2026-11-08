/*
 * tk26.c - icons from a program's own resources (PROGMAN's minimised
 * groups showed no icon on w16): LoadIcon of an integer and a named icon,
 * the resource calls behind it, and DrawIcon into a memory bitmap,
 * counting the pixels it changed. Run as the shell (tests/dropin.sh, and
 * -r for Windows 3.1). Resources: tk26.rc.
 */
#include <windows.h>

#define TK_REF "tk26ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);

static WORD drawn(HICON h)
{
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, 32, 32), old;
    WORD x, y, n = 0;
    RECT r;
    old = SelectObject(dc, bm);
    SetRect(&r, 0, 0, 32, 32);
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
    DrawIcon(dc, 0, 0, h);
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            if (GetPixel(dc, x, y) != RGB(255, 255, 255))
                n++;
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    ReleaseDC(0, sdc);
    return n;
}

static void one(const char *what, WORD hinst, LPCSTR id)
{
    char n[48];
    HICON h;
    HRSRC r;
    lstrcpy(n, what);
    lstrcat(n, " FindResource(GROUP_ICON) != 0");
    r = FindResource(hinst, id, RT_GROUP_ICON);
    obs(n, r != 0);
    if (r) {
        lstrcpy(n, what);
        lstrcat(n, " SizeofResource(GROUP_ICON)");
        obs(n, (WORD)SizeofResource(hinst, r));
    }
    h = LoadIcon(hinst, id);
    lstrcpy(n, what);
    lstrcat(n, " LoadIcon != 0");
    obs(n, h != 0);
    if (h) {
        lstrcpy(n, what);
        lstrcat(n, " pixels drawn");
        obs(n, drawn(h));
        lstrcpy(n, what);
        lstrcat(n, " GlobalSize(icon)");
        obs(n, (WORD)GlobalSize(h));
        lstrcpy(n, what);
        lstrcat(n, " GlobalFlags(icon)");
        obs(n, GlobalFlags(h));
        GlobalCompact(0xFFFFFFFFUL);    /* discard what can be */
        lstrcpy(n, what);
        lstrcat(n, " GlobalFlags after GlobalCompact(-1)");
        obs(n, GlobalFlags(h));
        lstrcpy(n, what);
        lstrcat(n, " pixels drawn after GlobalCompact(-1)");
        obs(n, drawn(h));
    }
}

LRESULT FAR PASCAL __export __loadds WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(h, msg, wp, lp);
}

/* a minimised window of a class with our icon: pixels on the screen */
static void minimised(WORD hinst)
{
    WNDCLASS wc;
    HWND h;
    RECT r;
    HDC dc;
    WORD x, y, n = 0, k = 0;
    MSG m;
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TK26";
    RegisterClass(&wc);
    h = CreateWindow("TK26", "TK26", WS_OVERLAPPEDWINDOW, 40, 40, 200, 120,
                     0, 0, hinst, NULL);
    ShowWindow(h, SW_SHOWMINIMIZED);
    UpdateWindow(h);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE) && k++ < 100)
        DispatchMessage(&m);
    obs("minimised IsIconic", IsIconic(h) != 0);
    GetWindowRect(h, &r);
    obs("minimised width", r.right - r.left);
    dc = GetDC(0);
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            if (GetPixel(dc, x, y) != GetPixel(dc, 0, 0))
                n++;
    ReleaseDC(0, dc);
    obs("minimised pixels unlike the desktop", n);
    DestroyWindow(h);
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];
    out("TK26 start\r\n");
    K_WAITEVENT(0);
    U_INITAPP(hinst);
    one("int 1", hinst, MAKEINTRESOURCE(1));
    one("named", hinst, "TKICON");
    minimised(hinst);
    one("system IDI_APPLICATION", 0, IDI_APPLICATION);
    out("TK26 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
