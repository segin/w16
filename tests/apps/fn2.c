/*
 * fn2.c - a window showing every character of the raster fonts, for
 * screenshots of w16's own fonts as Windows draws them (tests/fonts.sh,
 * with SNAP.EXE). Run it only with w16's fonts installed: Microsoft's
 * glyphs are not to be rendered for viewing (ARCHITECTURE.md 6.9).
 *
 * Command tail: the page, 1 (default) System, Fixedsys, Terminal, MS Sans
 * Serif 8 and 10 pt; 2 MS Sans Serif 12, 14 and 18 pt; 3 MS Sans Serif
 * 24 pt. Each font gets a caption line (in the System font) and then
 * codes 20h-FFh (Terminal: 01h-FEh) drawn one by one, wrapped at the
 * window's edge. The window fills the screen; the program ends on WM_CLOSE
 * or after 60 seconds.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];      /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

typedef struct {
    const char *label;
    const char *face;
    int height;
    BYTE charset;
} sample;

static const sample page1[] = {
    { "System (VGASYS.FON)", "System", 16, ANSI_CHARSET },
    { "Fixedsys (VGAFIX.FON)", "Fixedsys", 15, ANSI_CHARSET },
    { "Terminal (VGAOEM.FON)", "Terminal", 12, OEM_CHARSET },
    { "MS Sans Serif 8 (SSERIFE.FON)", "MS Sans Serif", 13, ANSI_CHARSET },
    { "MS Sans Serif 10", "MS Sans Serif", 16, ANSI_CHARSET },
    { 0, 0, 0, 0 }
};
static const sample page2[] = {
    { "MS Sans Serif 12", "MS Sans Serif", 20, ANSI_CHARSET },
    { "MS Sans Serif 14", "MS Sans Serif", 24, ANSI_CHARSET },
    { "MS Sans Serif 18", "MS Sans Serif", 29, ANSI_CHARSET },
    { 0, 0, 0, 0 }
};
static const sample page3[] = {
    { "MS Sans Serif 24", "MS Sans Serif", 37, ANSI_CHARSET },
    { 0, 0, 0, 0 }
};
static const sample *page = page1;

static int len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void paint(HDC dc, RECT *r)
{
    int y = 2, i;
    for (i = 0; page[i].label; i++) {
        const sample *s = &page[i];
        HFONT f = CreateFont(s->height, 0, 0, 0, 0, 0, 0, 0, s->charset, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, s->face);
        HFONT old;
        TEXTMETRIC tm;
        int x = 4, c, first, last;
        char ch;
        SetTextColor(dc, RGB(0, 0, 128));
        TextOut(dc, 4, y, s->label, len(s->label));
        SetTextColor(dc, RGB(0, 0, 0));
        y += 18;
        old = SelectObject(dc, f);
        GetTextMetrics(dc, &tm);
        first = s->charset == OEM_CHARSET ? 1 : 32;
        last = s->charset == OEM_CHARSET ? 254 : 255;
        for (c = first; c <= last; c++) {
            int w;
            ch = (char)c;
            GetCharWidth(dc, c, c, &w);
            if (x + w > r->right - 4) {
                x = 4;
                y += tm.tmHeight;
            }
            TextOut(dc, x, y, &ch, 1);
            x += w;
        }
        y += tm.tmHeight + 4;
        SelectObject(dc, old);
        DeleteObject(f);
    }
}

LRESULT FAR PASCAL __export __loadds WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        BeginPaint(h, &ps);
        GetClientRect(h, &r);
        paint(ps.hdc, &r);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[I_DI];
    WNDCLASS wc;
    HWND h;
    MSG m;
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);   /* ES:BX */

    WaitEvent(0);
    InitApp(hi);
    while (*tail == ' ')
        tail++;
    if (*tail == '2')
        page = page2;
    else if (*tail == '3')
        page = page3;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "W16FontSheet";
    RegisterClass(&wc);
    h = CreateWindow("W16FontSheet", "w16 fonts", WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,
                     0, 0, 640, 480, 0, 0, hi, 0);
    ShowWindow(h, SW_SHOWMAXIMIZED);
    UpdateWindow(h);
    SetTimer(h, 1, 60000, 0);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
