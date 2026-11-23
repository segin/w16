/*
 * ttview.c - TTVIEW.EXE: a TrueType viewer that renders the font itself,
 * with w16's fixed-point rasteriser (gdi/ttf/ttfx.c), not through GDI. It
 * runs on Windows 2.x (Windows/386 2.11, real mode underneath) as well as
 * 3.x and w16: marked as a Windows 2.0 program, 80286 code without
 * floating point (no WIN87EM), only calls Windows 2.x has, memory from
 * GlobalAlloc locked only while in use, and the font file read in 512-byte
 * pages through _llseek/_lread (a .TTF is over 64K).
 *
 *   TTVIEW [/T] [/R n] [FONT.TTF]
 *
 * It draws a page (tests/apps/ttvpage.c: the font's name, a sample line
 * at eleven sizes, the characters 20h-FFh in a grid) into a 640 x 480
 * 1-bit buffer, hands that to a monochrome bitmap with SetBitmapBits (no
 * DIBs before 3.0) and shows it with BitBlt. Without a file name it reads
 * the command line from C:\TTVIEW.CMD if there is one, else asks for a
 * file (File > Open...: a plain dialog, there is no COMMDLG on 2.x).
 *
 * /T (tests/ttview.sh): once the window is shown, write C:\TTVIEW.PG (the
 * page buffer), C:\TTVIEW.SCR (the client area read back from the screen
 * with BitBlt into a monochrome bitmap and GetBitmapBits: a word each of
 * width, height and bytes a row, then the rows; white pixels are 1) and
 * C:\TTVIEW.LOG (the Windows version, the font, glyphs drawn, the render
 * time from GetTickCount), then end the Windows session. /R n renders the
 * page n times for timing.
 */
#include <windows.h>
#include "ttvpage.h"

#define IDM_OPEN 100
#define IDM_EXIT 101
#define IDC_FILE 200

static HINSTANCE g_inst;
static HWND g_wnd;
static HBITMAP g_bm;
static HGLOBAL g_page, g_arena;
static ttfx_font g_font;
static ttfx_u8 g_cache[TTFX_CACHE_BYTES(8)];
static HFILE g_file = HFILE_ERROR;
static char g_path[128], g_name[64];
static int g_test, g_reps = 1, g_glyphs, g_failed;
static DWORD g_ms;
static char g_log[512];
static int g_loglen;

static void lg(const char *s)
{
    while (*s && g_loglen < (int)sizeof g_log - 1)
        g_log[g_loglen++] = *s++;
}

static void lgnum(DWORD v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    lg(b + i);
}

/* ttfx's reads: the file's page at off */
static unsigned readfn(void *user, ttfx_u32 off, void TTFX_FAR *dst, unsigned len)
{
    int n;
    (void)user;
    if (_llseek(g_file, (LONG)off, 0) != (LONG)off)
        return 0;
    n = _lread(g_file, (LPSTR)dst, len);
    return n > 0 ? (unsigned)n : 0;
}

static void close_font(void)
{
    if (g_file != HFILE_ERROR)
        _lclose(g_file);
    g_file = HFILE_ERROR;
    if (g_bm)
        DeleteObject(g_bm);
    g_bm = 0;
}

/* open the font and draw its page into the bitmap */
static int load(HWND w, const char *path)
{
    OFSTRUCT of;
    ttfx_u8 FAR *bits;
    DWORD t;
    int i;
    close_font();
    g_file = OpenFile(path, &of, OF_READ);
    if (g_file == HFILE_ERROR) {
        if (!g_test)
            MessageBox(w, path, "TTVIEW: cannot open", MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    if (!ttfx_InitFont(&g_font, readfn, 0, 0, g_cache, sizeof g_cache, 0)) {
        if (!g_test)
            MessageBox(w, path, "TTVIEW: not a TrueType font", MB_OK | MB_ICONEXCLAMATION);
        close_font();
        return 0;
    }
    bits = (ttfx_u8 FAR *)GlobalLock(g_page);
    ttv_arena = (ttfx_u8 FAR *)GlobalLock(g_arena);
    if (!bits || !ttv_arena) {
        if (bits)
            GlobalUnlock(g_page);
        if (ttv_arena)
            GlobalUnlock(g_arena);
        close_font();
        return 0;
    }
    t = GetTickCount();
    for (i = 0; i < g_reps; i++)
        g_glyphs = ttv_page(&g_font, bits, &g_failed);
    g_ms = GetTickCount() - t;
    g_bm = CreateBitmap(TTV_W, TTV_H, 1, 1, NULL);
    if (g_bm)
        SetBitmapBits(g_bm, (DWORD)TTV_STRIDE * TTV_H, (LPSTR)bits);
    if (g_test) {                       /* the page as drawn */
        HFILE h = _lcreat("C:\\TTVIEW.PG", 0);
        if (h != HFILE_ERROR) {
            _lwrite(h, (LPSTR)bits, TTV_STRIDE * TTV_H);
            _lclose(h);
        }
    }
    GlobalUnlock(g_arena);
    GlobalUnlock(g_page);
    ttv_arena = 0;
    ttv_name(&g_font, g_name, sizeof g_name);
    {
        char title[100];
        int k = 0, j;
        const char *p = "TTVIEW - ";
        for (; *p; p++)
            title[k++] = *p;
        for (j = 0; g_name[j] && k < (int)sizeof title - 1; j++)
            title[k++] = g_name[j];
        title[k] = 0;
        SetWindowText(w, title);
    }
    InvalidateRect(w, NULL, TRUE);
    return 1;
}

/* the client area as the screen shows it, to C:\TTVIEW.SCR */
static void capture(HWND w)
{
    RECT r;
    HDC dc, mdc;
    HBITMAP mono, old;
    HGLOBAL hb;
    LPSTR p;
    WORD hdr[3], cw, ch, stride;
    HFILE h;
    GetClientRect(w, &r);
    cw = (WORD)(r.right < TTV_W ? r.right : TTV_W);
    ch = (WORD)(r.bottom < TTV_H ? r.bottom : TTV_H);
    stride = (WORD)(((cw + 15) / 16) * 2);
    dc = GetDC(w);
    mdc = CreateCompatibleDC(dc);
    mono = CreateBitmap(cw, ch, 1, 1, NULL);
    hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)stride * ch);
    if (mdc && mono && hb) {
        old = SelectObject(mdc, mono);
        BitBlt(mdc, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        p = GlobalLock(hb);
        GetBitmapBits(mono, (DWORD)stride * ch, p);
        h = _lcreat("C:\\TTVIEW.SCR", 0);
        if (h != HFILE_ERROR) {
            hdr[0] = cw;
            hdr[1] = ch;
            hdr[2] = stride;
            _lwrite(h, (LPSTR)hdr, sizeof hdr);
            _lwrite(h, p, stride * ch);
            _lclose(h);
        }
        GlobalUnlock(hb);
    }
    if (hb)
        GlobalFree(hb);
    if (mono)
        DeleteObject(mono);
    if (mdc)
        DeleteDC(mdc);
    ReleaseDC(w, dc);
}

static void write_log(void)
{
    DWORD v = GetVersion();
    HFILE h;
    /* GetVersion's low word, as it is: 3.1 gives 0A03h, 2.11 1102h */
    lg("TTVIEW GetVersion ");
    {
        char b[5];
        int i;
        for (i = 0; i < 4; i++)
            b[i] = "0123456789ABCDEF"[(LOWORD(v) >> (12 - 4 * i)) & 15];
        b[4] = 0;
        lg(b);
    }
    lg("h\r\nfont ");
    lg(g_path);
    lg(" \"");
    lg(g_name);
    lg("\"\r\nglyphs ");
    lgnum(g_glyphs);
    lg(" failed ");
    lgnum(g_failed);
    lg(" arena peak ");
    lgnum(ttv_arena_peak);
    lg("\r\nrender ");
    lgnum(g_reps);
    lg(" page(s) ");
    lgnum(g_ms);
    lg(" ms\r\n");
    h = _lcreat("C:\\TTVIEW.LOG", 0);
    if (h != HFILE_ERROR) {
        _lwrite(h, g_log, g_loglen);
        _lclose(h);
    }
}

BOOL CALLBACK __export OpenDlg(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, IDC_FILE, g_path);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(d, IDC_FILE, g_path, sizeof g_path);
            EndDialog(d, 1);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void open_dialog(HWND w)
{
    FARPROC fp = MakeProcInstance((FARPROC)OpenDlg, g_inst);
    int r = DialogBox(g_inst, "OPEN", w, (DLGPROC)fp);
    FreeProcInstance(fp);
    if (r == 1 && g_path[0])
        load(w, g_path);
}

LRESULT CALLBACK __export TTViewWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (g_bm) {
            HDC mdc = CreateCompatibleDC(dc);
            if (mdc) {
                HBITMAP old = SelectObject(mdc, g_bm);
                /* glyph pixels are set: NOTSRCCOPY makes them the text
                   colour (black), the rest the background (white) */
                BitBlt(dc, 0, 0, TTV_W, TTV_H, mdc, 0, 0, NOTSRCCOPY);
                SelectObject(mdc, old);
                DeleteDC(mdc);
            }
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (wp == IDM_OPEN)
            open_dialog(w);
        else if (wp == IDM_EXIT)
            DestroyWindow(w);
        return 0;
    case WM_USER:                       /* /T: the window is on the screen */
        capture(w);
        write_log();
        ExitWindows(0, 0);
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        close_font();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* the command line's options and file name */
static void parse(const char FAR *t)
{
    int n = 0;
    for (;;) {
        while (*t == ' ' || *t == '\t')
            t++;
        if (!*t || *t == '\r' || *t == '\n')
            break;
        if (t[0] == '/' && (t[1] == 'T' || t[1] == 't')) {
            g_test = 1;
            t += 2;
        } else if (t[0] == '/' && (t[1] == 'R' || t[1] == 'r')) {
            t += 2;
            while (*t == ' ')
                t++;
            g_reps = 0;
            while (*t >= '0' && *t <= '9')
                g_reps = g_reps * 10 + *t++ - '0';
            if (g_reps < 1)
                g_reps = 1;
        } else {
            n = 0;
            while (*t && *t != ' ' && *t != '\t' && *t != '\r' && *t != '\n' && n < (int)sizeof g_path - 1)
                g_path[n++] = *t++;
            g_path[n] = 0;
        }
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HMENU menu;
    g_inst = inst;
    parse(cmd);
    if (!g_path[0] && !g_test) {        /* C:\TTVIEW.CMD: a command line */
        OFSTRUCT of;
        HFILE h = OpenFile("C:\\TTVIEW.CMD", &of, OF_READ);
        if (h != HFILE_ERROR) {
            static char line[160];
            int n = _lread(h, line, sizeof line - 1);
            _lclose(h);
            line[n > 0 ? n : 0] = 0;
            parse(line);
        }
    }
    g_page = GlobalAlloc(GMEM_MOVEABLE, (DWORD)TTV_STRIDE * TTV_H);
    g_arena = GlobalAlloc(GMEM_MOVEABLE, TTV_ARENA);
    if (!g_page || !g_arena)
        return 1;
    if (!prev) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = TTViewWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "TTView";
        if (!RegisterClass(&wc))
            return 1;
    }
    /* the menu made with ChangeMenu: Windows 2.x cannot read the menu
       resources wrc writes (Program Manager's 2.x build does the same,
       shell/progman/compat2.c) */
    {
        HMENU file = CreateMenu();
        menu = CreateMenu();
        ChangeMenu(file, 0, "&Open...", IDM_OPEN, MF_APPEND);
        ChangeMenu(file, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        ChangeMenu(file, 0, "E&xit", IDM_EXIT, MF_APPEND);
        ChangeMenu(menu, 0, "&File", (UINT)file, MF_APPEND | MF_POPUP);
    }
    g_wnd = CreateWindow("TTView", "TTVIEW", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                         NULL, menu, inst, NULL);
    if (!g_wnd)
        return 1;
    ShowWindow(g_wnd, g_test ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(g_wnd);
    if (g_path[0])
        load(g_wnd, g_path);
    else
        open_dialog(g_wnd);
    UpdateWindow(g_wnd);
    if (g_test)
        PostMessage(g_wnd, WM_USER, 0, 0);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    GlobalFree(g_page);
    GlobalFree(g_arena);
    return (int)m.wParam;
}
