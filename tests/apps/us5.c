/*
 * us5.c - probe US5: where Windows 3.1 puts menu-bar items.
 *
 * Program Manager's and w16's Program Manager's bars (same strings, same
 * text widths) are laid out differently by 3.1's USER (ARCHITECTURE.md
 * 10.4). US5 shows windows whose bars differ in one way each and saves
 * the screen as C:\US5.BMP; tests/userscreens.sh-style comparison or
 * tools/pxdump.py then shows where each item's text begins:
 *   A: the menu from a resource (us5.rc), as CreateWindow's class menu
 *   B: the same, then ModifyMenu on an item of its first popup
 *   C: the same bar built with CreateMenu/AppendMenu
 *   D: A, created 200 wide and then widened with MoveWindow
 *   E: A, with CheckMenuItem on an item of the third popup
 *   F: A, created hidden; ModifyMenu; then shown (as w16's Program Manager)
 *   G: A, with CheckMenuItem by command on the bar handle
 *   H, I: A in a CS_BYTEALIGNCLIENT class, asked for x = 10 and 333 (over E)
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hinst;

LRESULT FAR PASCAL __export __loadds wp5(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

static void snapit(void)
{
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)stride * 32);
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    static struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } bmi;
    BITMAPFILEHEADER fh;
    HFILE f;
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;
    f = _lcreat("C:\\US5.BMP", 0);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    _llseek(f, fh.bfOffBits, 0);
    for (y = 0; y < cy; y += 32) {
        GetDIBits(scr, bm, y, 32, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
        _lwrite(f, buf, stride * 32);
    }
    _llseek(f, 0, 0);
    _lwrite(f, (LPCSTR)&fh, sizeof fh);
    _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
    _lclose(f);
}

static HWND make(const char *title, int y, int cx, BOOL show)
{
    HWND h = CreateWindow("US5", title, WS_OVERLAPPEDWINDOW, 10, y, cx, 70, NULL, NULL,
                          hinst, NULL);
    if (show)
        ShowWindow(h, SW_SHOWNORMAL);
    return h;
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND h;
    MSG m;
    DWORD end;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    wc.style = 0;
    wc.lpfnWndProc = wp5;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(1);
    wc.lpszClassName = "US5";
    RegisterClass(&wc);
    wc.style = CS_BYTEALIGNCLIENT;      /* Program Manager's frame class has it */
    wc.lpszClassName = "US5B";
    RegisterClass(&wc);
    make("A", 0, 512, TRUE);
    h = make("B", 75, 512, TRUE);
    ModifyMenu(GetSubMenu(GetMenu(h), 0), 2, MF_BYCOMMAND | MF_STRING, 2, "E&xit Windows...");
    DrawMenuBar(h);
    h = make("C", 150, 512, TRUE);
    {
        HMENU bar = CreateMenu(), p;
        static const char *names[] = { "&File", "&Options", "&Window", "&Help" };
        int i;
        for (i = 0; i < 4; i++) {
            p = CreatePopupMenu();
            AppendMenu(p, MF_STRING, 10 + i, "Item");
            AppendMenu(bar, MF_POPUP, (UINT)p, names[i]);
        }
        DestroyMenu(SetMenu(h, bar) ? 0 : bar);
    }
    h = make("D", 225, 200, TRUE);
    MoveWindow(h, 10, 225, 512, 70, TRUE);
    h = make("E", 300, 512, TRUE);
    CheckMenuItem(GetSubMenu(GetMenu(h), 2), 4, MF_BYCOMMAND | MF_CHECKED);
    DrawMenuBar(h);
    /* G: CheckMenuItem by command on the bar itself (Program Manager does
       so for an Options item) */
    h = make("G", 375, 512, TRUE);
    CheckMenuItem(GetMenu(h), 3, MF_BYCOMMAND | MF_CHECKED);
    DrawMenuBar(h);
    /* H, I: a CS_BYTEALIGNCLIENT class, asked for x = 10 and x = 13 */
    CreateWindow("US5B", "H", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 300, 300, 70, NULL, NULL, hinst, NULL);
    CreateWindow("US5B", "I", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 333, 300, 300, 70, NULL, NULL, hinst, NULL);
    h = make("F", 420, 512, FALSE);
    ModifyMenu(GetSubMenu(GetMenu(h), 0), 2, MF_BYCOMMAND | MF_STRING, 2, "E&xit Windows...");
    ShowWindow(h, SW_SHOWNORMAL);
    end = GetTickCount() + 1500;
    while ((long)(GetTickCount() - end) < 0)
        if (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&m);
    snapit();
    return 0;
}
