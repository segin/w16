/*
 * ttview.c - TTVIEW.EXE: a TrueType viewer that renders the font itself,
 * with w16's fixed-point rasteriser (gdi/ttf/ttfx.c), not through GDI. It
 * runs on Windows 2.x (Windows/386 2.11, real mode underneath) as well as
 * 3.x and w16: marked as a Windows 2.0 program, 80286 code without
 * floating point (no WIN87EM), only calls Windows 2.x has, memory from
 * GlobalAlloc locked only while in use, and the font file read in 512-byte
 * pages through _llseek/_lread (a .TTF is over 64K).
 *
 *   TTVIEW [/T] [/R n] [/B|/A|/U] [/2] [/G] [FONT.TTF]
 *
 * It draws a page (tests/apps/ttvpage.c: the font's name, a sample line
 * at eleven sizes, the characters 20h-FFh in a grid; with /2 the
 * specimen, a line at every size from 8 to 24 pixels to the em) into a
 * 640 x 480 1-bit buffer, hands that to a monochrome bitmap with
 * SetBitmapBits (no DIBs before 3.0) and shows it with BitBlt. Without a
 * file name it reads the command line from C:\TTVIEW.CMD if there is one,
 * else asks for a file (File > Open...: a plain dialog, there is no
 * COMMDLG on 2.x). The View menu switches what /U, /2 and /G set.
 *
 * Hinting: /B (the default) runs the font's own instructions (fpgm, prep
 * and each glyph's program: gdi/ttf/ttfbc.c, as Windows 3.1 does; a font
 * without them, or a glyph whose program fails, gets the autohinter); /A
 * w16's autohinter (gdi/ttf/ttfhint.c) for every glyph; /U unhinted
 * (ttfx_SetHinting 0). /G (Windows 3.1 and later):
 * show the specimen as Windows draws it, with its own TrueType rasteriser
 * (CreateScalableFontResource, found with GetProcAddress so that TTVIEW
 * still loads on 2.x, writes the font's .FOT; AddFontResource; TextOut of
 * ttv_line's lines in CreateFont's of the font's family at -em), to
 * compare with w16's at the same sizes.
 *
 * /T (tests/ttview.sh): render the page both hinted (/B or /A) and
 * unhinted, writing C:\TTVIEW.PG (hinted) and C:\TTVIEWU.PG (the page buffers), show the
 * one asked for and once the window is shown, write C:\TTVIEW.SCR (the
 * client area read back from the screen with BitBlt into a monochrome
 * bitmap and GetBitmapBits: a word each of width, height and bytes a
 * row, then the rows; white pixels are 1) and C:\TTVIEW.LOG (the Windows
 * version, the font, glyphs drawn, the render times from GetTickCount,
 * what is shown), then end the Windows session. /R n renders each page n
 * times for timing.
 */
#include <windows.h>
#include "ttvpage.h"

#define IDM_OPEN 100
#define IDM_EXIT 101
#define IDM_HINT 102
#define IDM_PAGE2 103
#define IDM_GDI 104
#define IDM_BC 105
#define IDM_AUTO 106
#define IDC_FILE 200

static HINSTANCE g_inst;
static HWND g_wnd;
static HBITMAP g_bm;
static HGLOBAL g_page, g_arena;
static ttfx_font g_font;
static ttfx_u8 g_cache[TTFX_CACHE_BYTES(8)];
static HFILE g_file = HFILE_ERROR;
static char g_path[128], g_name[64], g_family[64];
static int g_test, g_reps = 1, g_glyphs, g_failed;
static int g_unhinted, g_page2, g_gdi, g_fot;   /* /U, /2, /G; FOT added */
static int g_auto;                              /* /A: the autohinter */
static HGLOBAL g_bcmem;                         /* a size's bytecode state */
static DWORD g_ms, g_msu;                       /* hinted, unhinted */
static HMENU g_view;
static char g_fotpath[160];                     /* /G: the .FOT added */

static void check_menu(void)
{
    CheckMenuItem(g_view, IDM_BC, !g_unhinted && !g_auto ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, IDM_AUTO, !g_unhinted && g_auto ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, IDM_HINT, g_unhinted ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, IDM_PAGE2, g_page2 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, IDM_GDI, g_gdi ? MF_CHECKED : MF_UNCHECKED);
}
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
    if (g_fot)
        RemoveFontResource(g_fotpath);
    g_fot = 0;
    if (g_bcmem)
        GlobalFree(g_bcmem);
    g_bcmem = 0;
}

/* /G: the font added to Windows (3.1 and later), through a .FOT made for
   it: 1, else 0 and why in g_fotwhy. As probe FN4 does it: the .TTF in the
   system directory (tests/ttview.sh -g puts a copy there), the .FOT made
   with its bare name and that directory as the current path; else with
   the .TTF's full path. Observed on 3.1: AddFontResource adds the .FOT
   only if it is named as the .TTF is (TTFONT.FOT for TTFONT.TTF; made as
   TTVIEW.FOT, CreateScalableFontResource succeeds and AddFontResource
   returns 0). GDI's and KERNEL's exported names are upper case, as the NE
   format keeps them; both functions are found at run time, so that TTVIEW
   still loads on 2.x */
typedef BOOL (FAR PASCAL *CSFR)(UINT, LPCSTR, LPCSTR, LPCSTR);
typedef UINT (FAR PASCAL *GSD)(LPSTR, UINT);
static const char *g_fotwhy = "";

/* append t to s (no lstrcat before 3.0); s */
static char *scat(char *s, const char *t)
{
    char *e = s;
    while (*e)
        e++;
    while ((*e++ = *t++) != 0)
        ;
    return s;
}

/* make dir\BASE.FOT for the font (dir 0: the .TTF's own) and add it */
static int add_fot(FARPROC csfr, const char *dir, const char *ttf, const char *cur)
{
    const char *name = g_path, *s;
    char *e;
    int c, a;
    OFSTRUCT of;
    for (s = g_path; *s; s++)
        if (*s == '\\' || *s == ':')
            name = s + 1;
    g_fotpath[0] = 0;
    if (dir)
        scat(scat(g_fotpath, dir), "\\");
    else {
        scat(g_fotpath, g_path);
        g_fotpath[name - g_path] = 0;
    }
    scat(g_fotpath, name);
    for (e = g_fotpath; *e; e++)
        ;
    while (e > g_fotpath && e[-1] != '.' && e[-1] != '\\')
        e--;
    if (e > g_fotpath && e[-1] == '.')
        e[-1] = 0;
    scat(g_fotpath, ".FOT");
    OpenFile(g_fotpath, &of, OF_DELETE);
    c = ((CSFR)csfr)(0, g_fotpath, ttf ? ttf : name, cur);
    a = c ? AddFontResource(g_fotpath) : 0;
    lg("add font: ");
    lg(g_fotpath);
    lg(": CreateScalableFontResource ");
    lgnum((DWORD)c);
    lg(", AddFontResource ");
    lgnum((DWORD)(a > 0 ? a : 0));
    lg("\r\n");
    return a > 0;
}

static int add_font(void)
{
    FARPROC p = GetProcAddress(GetModuleHandle("GDI"), "CREATESCALABLEFONTRESOURCE");
    FARPROC gsd = GetProcAddress(GetModuleHandle("KERNEL"), "GETSYSTEMDIRECTORY");
    char sys[144];
    int n;
    if (!p) {
        g_fotwhy = "no CreateScalableFontResource";
        return 0;
    }
    n = gsd ? (int)((GSD)gsd)(sys, sizeof sys - 16) : 0;
    if (n > 0 && n < (int)sizeof sys - 16 && add_fot(p, sys, 0, sys)) {
        g_fotwhy = "Windows' TextOut (the .FOT made the portable way)";
        return 1;
    }
    if (add_fot(p, 0, g_path, NULL)) {
        g_fotwhy = "Windows' TextOut (the .FOT made with the full path)";
        return 1;
    }
    g_fotwhy = "the font not added";
    return 0;
}

/* the page, hinted or not, g_reps times; the time it took */
static DWORD render(ttfx_u8 FAR *bits, int hinted)
{
    DWORD t = GetTickCount();
    int i;
    ttfx_SetHinting(&g_font, hinted ? (g_auto ? TTFX_HINT_DEFAULT : TTFX_HINT_ALL) : 0);
    for (i = 0; i < g_reps; i++)
        g_glyphs = g_page2 ? ttv_sizes(&g_font, bits, &g_failed) : ttv_page(&g_font, bits, &g_failed);
    return GetTickCount() - t;
}

static void write_page(const char *name, ttfx_u8 FAR *bits)
{
    HFILE h = _lcreat(name, 0);
    if (h != HFILE_ERROR) {
        _lwrite(h, (LPSTR)bits, TTV_STRIDE * TTV_H);
        _lclose(h);
    }
}

/* open the font and draw its page into the bitmap */
static int load(HWND w, const char *path)
{
    OFSTRUCT of;
    ttfx_u8 FAR *bits;
    int pass;
    close_font();
    /* /G: shared, for GDI to open it too when Windows adds the font (a
       3.x sharing mode; Windows 2.x has no /G) */
    g_file = OpenFile(path, &of, g_gdi ? OF_READ | OF_SHARE_DENY_NONE : OF_READ);
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
    /* the bytecode's state for a size (each size's fpgm and prep run in
       it): locked while drawing, as the page and the arena are */
    ttv_bcbytes = ttfx_bcBytes(&g_font);
    g_bcmem = ttv_bcbytes ? GlobalAlloc(GMEM_MOVEABLE, ttv_bcbytes) : 0;
    ttv_bcmem = g_bcmem ? GlobalLock(g_bcmem) : 0;
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
    /* /T: both, the one not shown first, each to its file */
    for (pass = g_test ? 0 : 1; pass < 2; pass++) {
        int hinted = pass ? !g_unhinted : g_unhinted;
        DWORD ms = render(bits, hinted);
        if (hinted)
            g_ms = ms;
        else
            g_msu = ms;
        if (g_test)
            write_page(hinted ? "C:\\TTVIEW.PG" : "C:\\TTVIEWU.PG", bits);
    }
    g_bm = CreateBitmap(TTV_W, TTV_H, 1, 1, NULL);
    if (g_bm)
        SetBitmapBits(g_bm, (DWORD)TTV_STRIDE * TTV_H, (LPSTR)bits);
    GlobalUnlock(g_arena);
    GlobalUnlock(g_page);
    ttv_arena = 0;
    if (g_bcmem)
        GlobalUnlock(g_bcmem);
    ttv_bcmem = 0;
    g_font.bc = 0;
    ttv_name(&g_font, g_name, sizeof g_name);
    ttv_nameid(&g_font, 1, g_family, sizeof g_family);
    if (g_gdi)
        g_fot = add_font();
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
    lg(g_page2 ? " specimen page(s): hinted " : " page(s): hinted ");
    lgnum(g_ms);
    lg(" ms, unhinted ");
    lgnum(g_msu);
    lg(" ms\r\nshown ");
    lg(g_gdi ? g_fotwhy
             : g_unhinted ? "unhinted" : g_auto ? "hinted (autohinter)" : "hinted (instructions)");
    lg("\r\n");
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
        if (g_gdi && g_fot) {
            /* the specimen's lines as Windows draws them */
            ttfx_i32 bold = g_font.os2 ? ttfx_U16(&g_font, g_font.os2 + 4) : 400;
            int italic = ttfx_U16(&g_font, g_font.head + 44) & 2, i, em, y;
            char text[80];
            SetTextAlign(dc, TA_BASELINE | TA_LEFT);
            SetBkMode(dc, TRANSPARENT);         /* (opaque, a line's cell would cut the descenders of the line above) */
            for (i = 0; ttv_line(&g_font, i, &em, &y, text, sizeof text); i++) {
                HFONT hf = CreateFont(-em, 0, 0, 0, (int)bold, (BYTE)italic, 0, 0, ANSI_CHARSET,
                                      OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, g_family);
                HFONT old = SelectObject(dc, hf);
                int n = 0;
                while (text[n])
                    n++;
                TextOut(dc, 2, y, text, n);
                SelectObject(dc, old);
                DeleteObject(hf);
            }
        } else if (g_bm) {
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
        else if (wp == IDM_BC || wp == IDM_AUTO || wp == IDM_HINT) {
            g_unhinted = wp == IDM_HINT;
            g_auto = wp == IDM_AUTO;
            check_menu();
            if (g_path[0])
                load(w, g_path);
        } else if (wp == IDM_PAGE2 || wp == IDM_GDI) {
            int *flag = wp == IDM_PAGE2 ? &g_page2 : &g_gdi;
            *flag = !*flag;
            if (wp == IDM_GDI && g_gdi)
                g_page2 = 1;            /* (Windows draws the specimen only) */
            check_menu();
            if (g_path[0])
                load(w, g_path);
        }
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
        } else if (t[0] == '/' && (t[1] == 'U' || t[1] == 'u')) {
            g_unhinted = 1;
            t += 2;
        } else if (t[0] == '/' && (t[1] == 'A' || t[1] == 'a')) {
            g_auto = 1;
            g_unhinted = 0;
            t += 2;
        } else if (t[0] == '/' && (t[1] == 'B' || t[1] == 'b')) {
            g_auto = g_unhinted = 0;
            t += 2;
        } else if (t[0] == '/' && t[1] == '2') {
            g_page2 = 1;
            t += 2;
        } else if (t[0] == '/' && (t[1] == 'G' || t[1] == 'g')) {
            g_gdi = g_page2 = 1;
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
        g_view = CreateMenu();
        ChangeMenu(g_view, 0, "&Font's instructions", IDM_BC, MF_APPEND);
        ChangeMenu(g_view, 0, "&Autohinter", IDM_AUTO, MF_APPEND);
        ChangeMenu(g_view, 0, "&Unhinted", IDM_HINT, MF_APPEND);
        ChangeMenu(g_view, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        ChangeMenu(g_view, 0, "&Specimen, 8-24 pixels", IDM_PAGE2, MF_APPEND);
        ChangeMenu(g_view, 0, "&Windows' rasteriser (specimen)", IDM_GDI, MF_APPEND);
        ChangeMenu(menu, 0, "&View", (UINT)g_view, MF_APPEND | MF_POPUP);
        check_menu();
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
