/*
 * gd8.c - the GDI audit's last open items (its triage file docs/notes/gdi-audit-triage.md, in git history,
 * "Left open"): what Windows 3.1's GDI does
 * - with the clip region's coordinates: GetClipRgn (GDI.173) after
 *   IntersectClipRect, SelectClipRgn, OffsetClipRgn, and after SetDCOrg
 *   (GDI.117) moves the DC (where the clip region then clips);
 * - with SetTextJustification's error term across GetTextExtent, TextOut
 *   and ExtTextOut (Medium 14: BreakErr);
 * - with SetTextCharacterExtra across mapping changes (L8);
 * - and, for the driver's side (seen in GD1's log, C:\GD.LOG, run with
 *   tests/gdiboot.sh -l -L), the PBITMAP of a bitmap CreateCompatibleBitmap
 *   made for a memory DC (H10: its bmlpPDevice) and the device pointer
 *   Escape gives the driver's Control on a memory DC (Medium 3). Those
 *   parts write a marker line through a GDI call GD2 records
 *   (SetBkColor with a magic colour) so that the log can be read.
 * Checked against 3.1's values (gd8ref.h) where a program sees them.
 */
#include <windows.h>

#define TK_REF "gd8ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

typedef HRGN (FAR PASCAL *PGETCLIPRGN)(HDC);
typedef DWORD (FAR PASCAL *PSETDCORG)(HDC, int, int);
static PGETCLIPRGN getcliprgn;
static PSETDCORG setdcorg;
static HINSTANCE hinst;
static HDC hscr;

static void obs_rect(const char *name, RECT *r)
{
    char n[64];
    int i = 0;
    while (*name && i < 56)
        n[i++] = *name++;
    n[i] = '.'; n[i + 2] = 0;
    n[i + 1] = 'l'; obs(n, r->left);
    n[i + 1] = 't'; obs(n, r->top);
    n[i + 1] = 'r'; obs(n, r->right);
    n[i + 1] = 'b'; obs(n, r->bottom);
}

/* PtVisible: whether visible (checked) and the whole value (not: 3.1's
   high byte varies with the kind of DC and the clip region in no way
   followed up; a fresh memory DC's 1 x 1 bitmap gives 0 at 0,0 on 3.1) */
static void vis(const char *name, WORD v)
{
    char n[64];
    int i = 0;
    while (*name && i < 50)
        n[i++] = *name++;
    n[i] = 0;
    obs_env(n, v);
    lstrcpy(n + i, " visible");
    obs(n, v != 0);
}

/* a marker in GD2's log: SetBkColor with RGB(n, 0x5A, 0xA5) */
static void mark(HDC dc, int n)
{
    SetBkColor(dc, RGB(n, 0x5A, 0xA5));
}

/* ---- the clip region's coordinates ---- */

static void clip_box(HDC dc, const char *name)
{
    HRGN c = getcliprgn ? getcliprgn(dc) : 0;
    RECT rc;
    char n[64];
    int i = 0;
    while (*name && i < 40)
        n[i++] = *name++;
    n[i] = 0;
    SetRectEmpty(&rc);
    if (c)
        GetRgnBox(c, &rc);
    lstrcpy(n + i, " GetClipRgn box");
    obs_rect(n, &rc);
    lstrcpy(n + i, " clip box");
    GetClipBox(dc, &rc);
    obs_rect(n, &rc);
}

/* where a DC draws: the window's pixels black after PatBlt, as a box in
   the window's client coordinates (read through a fresh DC) */
static void drawn_box(HWND w, const char *name)
{
    HDC d = GetDC(w);
    RECT r;
    int x, y;
    r.left = r.top = 32767;
    r.right = r.bottom = -32768;
    for (y = 0; y < 120; y++)
        for (x = 0; x < 200; x++)
            if (GetPixel(d, x, y) == 0) {
                if (x < r.left) r.left = x;
                if (y < r.top) r.top = y;
                if (x + 1 > r.right) r.right = x + 1;
                if (y + 1 > r.bottom) r.bottom = y + 1;
            }
    ReleaseDC(w, d);
    obs_rect(name, &r);
}

static void white(HWND w)
{
    HDC d = GetDC(w);
    PatBlt(d, 0, 0, 300, 200, WHITENESS);
    ReleaseDC(w, d);
}

static void sec_clip(void)
{
    WNDCLASS wc;
    HWND w;
    HDC dc;
    HRGN r;
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GD8";
    RegisterClass(&wc);
    w = CreateWindow("GD8", "", WS_POPUP | WS_VISIBLE, 100, 80, 300, 200, NULL, NULL, hinst, NULL);
    UpdateWindow(w);
    /* a class DC of its own: SetDCOrg sticks until the window moves */
    dc = GetDC(w);
    vis("PtVisible 15,15 no clip", PtVisible(dc, 15, 15));
    obs("IntersectClipRect", IntersectClipRect(dc, 10, 10, 20, 20));
    vis("PtVisible 15,15 clip", PtVisible(dc, 15, 15));
    vis("PtVisible 15,15 clip again", PtVisible(dc, 15, 15));
    PatBlt(dc, 0, 0, 1, 1, WHITENESS);
    vis("PtVisible 15,15 clip, PatBlt", PtVisible(dc, 15, 15));
    clip_box(dc, "after IntersectClipRect");
    vis("PtVisible 15,15 clip, GetClipBox", PtVisible(dc, 15, 15));
    white(w);
    PatBlt(dc, 0, 0, 300, 200, BLACKNESS);
    drawn_box(w, "IntersectClipRect drawn");
    obs("OffsetClipRgn", OffsetClipRgn(dc, 5, 3));
    clip_box(dc, "after OffsetClipRgn");
    white(w);
    PatBlt(dc, 0, 0, 300, 200, BLACKNESS);
    drawn_box(w, "OffsetClipRgn drawn");
    if (setdcorg) {
        DWORD o = setdcorg(dc, 130, 100);
        obs("SetDCOrg old x", LOWORD(o));
        obs("SetDCOrg old y", HIWORD(o));
        clip_box(dc, "after SetDCOrg");
        white(w);
        PatBlt(dc, 0, 0, 300, 200, BLACKNESS);
        drawn_box(w, "SetDCOrg drawn");
        vis("PtVisible 15,13 after SetDCOrg", PtVisible(dc, 15, 13));
        vis("PtVisible 15,13 after SetDCOrg again", PtVisible(dc, 15, 13));
        vis("PtVisible -15,-7 after SetDCOrg", PtVisible(dc, -15, -7));
        SaveDC(dc);
        setdcorg(dc, 100, 80);
        clip_box(dc, "SetDCOrg back");
        white(w);
        PatBlt(dc, 0, 0, 300, 200, BLACKNESS);
        drawn_box(w, "SetDCOrg back drawn");
        RestoreDC(dc, -1);
        clip_box(dc, "RestoreDC");
        setdcorg(dc, 100, 80);
    }
    r = CreateRectRgn(0, 0, 8, 6);
    obs("SelectClipRgn", SelectClipRgn(dc, r));
    DeleteObject(r);
    clip_box(dc, "after SelectClipRgn");
    if (setdcorg) {
        setdcorg(dc, 110, 90);
        clip_box(dc, "SelectClipRgn, SetDCOrg");
        white(w);
        PatBlt(dc, -50, -50, 300, 200, BLACKNESS);
        drawn_box(w, "SelectClipRgn, SetDCOrg drawn");
        setdcorg(dc, 100, 80);
    }
    obs("GetClipRgn same handle", getcliprgn && getcliprgn(dc) == getcliprgn(dc));
    SelectClipRgn(dc, NULL);
    obs("GetClipRgn after SelectClipRgn NULL", getcliprgn && getcliprgn(dc) != 0);
    ReleaseDC(w, dc);
    /* the window moved: the class DC's clip region */
    dc = GetDC(w);
    IntersectClipRect(dc, 10, 10, 20, 20);
    ReleaseDC(w, dc);
    MoveWindow(w, 140, 100, 300, 200, TRUE);
    UpdateWindow(w);
    dc = GetDC(w);
    clip_box(dc, "moved window");
    white(w);
    PatBlt(dc, 0, 0, 300, 200, BLACKNESS);
    drawn_box(w, "moved window drawn");
    SelectClipRgn(dc, NULL);
    ReleaseDC(w, dc);
    DestroyWindow(w);
    /* PtVisible's high byte on other DCs */
    {
        HDC m = CreateCompatibleDC(NULL), d = CreateDC("DISPLAY", NULL, NULL, NULL);
        obs_env("PtVisible memory DC", PtVisible(m, 0, 0));
        IntersectClipRect(m, 0, 0, 1, 1);
        obs_env("PtVisible memory DC clip", PtVisible(m, 0, 0));
        {
            HBITMAP b = CreateBitmap(16, 16, 1, 1, NULL), ob = SelectObject(m, b);
            RECT rc;
            SetRect(&rc, 2, 2, 4, 4);
            SelectClipRgn(m, NULL);
            vis("PtVisible memory DC 16x16", PtVisible(m, 5, 5));
            vis("PtVisible memory DC 16x16 0,0", PtVisible(m, 0, 0));
            obs("RectVisible memory DC 16x16", RectVisible(m, &rc));
            obs("GetClipBox memory DC 16x16", GetClipBox(m, &rc));
            obs_rect("memory DC 16x16 clip box", &rc);
            SelectObject(m, ob);
            DeleteObject(b);
        }
        vis("PtVisible CreateDC", PtVisible(d, 5, 5));
        IntersectClipRect(d, 0, 0, 10, 10);
        vis("PtVisible CreateDC clip", PtVisible(d, 5, 5));
        vis("PtVisible screen DC", PtVisible(hscr, 5, 5));
        DeleteDC(m);
        DeleteDC(d);
    }
}

/* ---- text: justification, character extra ---- */

static HDC tdc;
static HBITMAP tbm, told;

/* the starts of the black runs of columns in row band [0, 16) of tdc */
static void runs(const char *name)
{
    int x, y, k = 0, was = 0;
    for (x = 0; x < 300 && k < 10; x++) {
        int b = 0;
        for (y = 0; y < 16; y++)
            if (GetPixel(tdc, x, y) == 0) {
                b = 1;
                break;
            }
        if (b && !was)
            obsn(name, k++, x);
        was = b;
    }
    obsn(name, 0x7F, k);
}

static void tclear(void)
{
    PatBlt(tdc, 0, 0, 300, 20, WHITENESS);
}

static void sec_text(void)
{
    static const char s[] = "a b c d";
    static int dx[7] = { 9, 9, 9, 9, 9, 9, 9 };
    tdc = CreateCompatibleDC(NULL);
    tbm = CreateBitmap(300, 20, 1, 1, NULL);
    told = SelectObject(tdc, tbm);
    /* justification: 10 extra pixels over 3 breaks */
    obs("SetTextJustification", SetTextJustification(tdc, 10, 3));
    obs("extent 1", LOWORD(GetTextExtent(tdc, s, 7)));
    obs("extent 2", LOWORD(GetTextExtent(tdc, s, 7)));
    obs("extent of \"a b\"", LOWORD(GetTextExtent(tdc, s, 3)));
    obs("extent after \"a b\"", LOWORD(GetTextExtent(tdc, s, 7)));
    tclear();
    TextOut(tdc, 0, 0, s, 7);
    runs("TextOut 1");
    tclear();
    TextOut(tdc, 0, 0, s, 7);
    runs("TextOut 2");
    tclear();
    TextOut(tdc, 0, 0, s, 3);
    TextOut(tdc, 100, 0, s + 3, 4);
    runs("TextOut in two");
    obs("extent after two", LOWORD(GetTextExtent(tdc, s, 7)));
    tclear();
    SetTextAlign(tdc, TA_CENTER);
    TextOut(tdc, 150, 0, s, 7);
    runs("TextOut centred");
    SetTextAlign(tdc, TA_LEFT);
    tclear();
    ExtTextOut(tdc, 0, 0, 0, NULL, s, 7, dx);
    runs("ExtTextOut dx");
    tclear();
    SetTextJustification(tdc, 11, 3);
    TextOut(tdc, 0, 0, s, 7);
    runs("11 over 3");
    tclear();
    SetTextJustification(tdc, 11, 3);
    TextOut(tdc, 0, 0, s, 3);
    TextOut(tdc, 100, 0, s + 3, 4);
    runs("11 over 3 in two");
    obs("11 over 3 extent", LOWORD(GetTextExtent(tdc, s, 7)));
    /* the error term's start for other counts: one break at a time */
    {
        static const char t[] = "a b c d e f";
        static const int cnt[4] = { 1, 2, 4, 5 };
        int i;
        for (i = 0; i < 4; i++) {
            char nm[24];
            lstrcpy(nm, "count  ");
            nm[6] = (char)('0' + cnt[i]);
            SetTextJustification(tdc, 2 * cnt[i] + 1, cnt[i]);
            tclear();
            TextOut(tdc, 0, 0, t, 2 * cnt[i] + 1);
            runs(nm);
        }
    }
    SetTextJustification(tdc, 0, 0);
    obs("extent unjustified", LOWORD(GetTextExtent(tdc, s, 7)));
    /* character extra across mapping changes */
    obs("SetTextCharacterExtra", SetTextCharacterExtra(tdc, 6));
    obs("extent extra 6", LOWORD(GetTextExtent(tdc, "abc", 3)));
    SetMapMode(tdc, MM_ANISOTROPIC);
    SetWindowExt(tdc, 1, 1);
    SetViewportExt(tdc, 2, 2);
    obs("GetTextCharacterExtra after 2:1", GetTextCharacterExtra(tdc));
    obs("extent extra 6, 2:1", LOWORD(GetTextExtent(tdc, "abc", 3)));
    tclear();
    TextOut(tdc, 0, 0, "abc", 3);
    runs("extra 6, 2:1 TextOut");
    obs("SetTextCharacterExtra 2:1", SetTextCharacterExtra(tdc, 6));
    obs("extent extra 6 set at 2:1", LOWORD(GetTextExtent(tdc, "abc", 3)));
    tclear();
    TextOut(tdc, 0, 0, "abc", 3);
    runs("extra 6 set at 2:1 TextOut");
    SetMapMode(tdc, MM_TEXT);
    obs("GetTextCharacterExtra back to MM_TEXT", GetTextCharacterExtra(tdc));
    obs("extent back to MM_TEXT", LOWORD(GetTextExtent(tdc, "abc", 3)));
    tclear();
    TextOut(tdc, 0, 0, "abc", 3);
    runs("back to MM_TEXT TextOut");
    SetTextCharacterExtra(tdc, 0);
    SelectObject(tdc, told);
    DeleteObject(tbm);
    DeleteDC(tdc);
}

/* ---- for GD1's log: a memory DC's compatible bitmap, Escape ---- */

static void sec_driver(void)
{
    HDC m = CreateCompatibleDC(NULL), m2 = CreateCompatibleDC(NULL);
    HBITMAP a = CreateCompatibleBitmap(hscr, 16, 8), b, oa, ob, c, d;
    int q = 5;                          /* GETCOLORTABLE */
    DWORD rgb;
    oa = SelectObject(m, a);
    mark(m, 1);                         /* a colour bitmap in m */
    b = CreateCompatibleBitmap(m, 16, 8);
    ob = SelectObject(m2, b);
    mark(m2, 2);                        /* its compatible bitmap in m2 */
    BitBlt(m, 0, 0, 16, 8, m2, 0, 0, SRCCOPY);
    SelectObject(m2, ob);
    mark(m, 3);                         /* a deleted, b selected again */
    SelectObject(m, oa);
    DeleteObject(a);
    SelectObject(m2, b);
    PatBlt(m2, 0, 0, 16, 8, BLACKNESS);
    BitBlt(hscr, 0, 0, 16, 8, m2, 0, 0, SRCCOPY);
    SelectObject(m2, ob);
    DeleteObject(b);
    c = CreateBitmap(16, 8, 1, 1, NULL);
    SelectObject(m, c);
    mark(m, 4);                         /* a monochrome bitmap's compatible one */
    d = CreateCompatibleBitmap(m, 16, 8);
    SelectObject(m2, d);
    BitBlt(m, 0, 0, 16, 8, m2, 0, 0, SRCCOPY);
    SelectObject(m2, ob);
    DeleteObject(d);
    mark(m, 5);                         /* Escape on the memory DC */
    obs("Escape memory DC QUERYESCSUPPORT", Escape(m, QUERYESCSUPPORT, 2, (LPCSTR)&q, NULL));
    q = 0;
    rgb = 0x55555555L;
    obs("Escape memory DC GETCOLORTABLE", Escape(m, 5, 2, (LPCSTR)&q, &rgb));
    obs("GETCOLORTABLE 0 lo", LOWORD(rgb));
    obs("GETCOLORTABLE 0 hi", HIWORD(rgb));
    mark(hscr, 6);                      /* and on the screen */
    q = 5;
    obs("Escape screen QUERYESCSUPPORT", Escape(hscr, QUERYESCSUPPORT, 2, (LPCSTR)&q, NULL));
    mark(hscr, 7);
    SelectObject(m, oa);
    DeleteObject(c);
    DeleteDC(m);
    DeleteDC(m2);
}

int __cdecl tmain(void)
{
    HMODULE gdi;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD8 start\r\n");
    gdi = GetModuleHandle("GDI");
    getcliprgn = (PGETCLIPRGN)GetProcAddress(gdi, MAKEINTRESOURCE(173));
    setdcorg = (PSETDCORG)GetProcAddress(gdi, MAKEINTRESOURCE(117));
    hscr = GetDC(NULL);
    out("GD8 driver\r\n");
    sec_driver();
    out("GD8 text\r\n");
    sec_text();
    out("GD8 clip\r\n");
    sec_clip();
    ReleaseDC(NULL, hscr);
    out("GD8 done\r\n");
    return failures;
}
