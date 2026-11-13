/*
 * cardwnd.c - w16 Cardfile: the card stack (Card view).
 *
 * Measured on Windows 3.1 (CF1; the System fixed font, 8 x 15 on VGA, is
 * the cards' font, cx by cy below): a card is 40 cx + 3 wide and 12 cy + 5
 * high: a border, the index line (cy high, its text 1 pixel in), a double
 * line (rows cy + 1 and cy + 3), the body of 11 lines of 40 characters
 * (the Edit control, 40 cx + 1 by 11 cy, at 1, cy + 4) and a border. The
 * front card sits cx from the left and cy / 2 above the bottom of the
 * area under the status line; each card behind it is 2 cx to the right
 * and cy + 1 higher, in index order after the front card (wrapping).
 * Pictures are drawn over the card's text (CF6).
 */
#include "cardfile.h"

FARPROC g_oldedit;

static int card_w(void) { return 40 * g_cx + 3; }
static int card_h(void) { return 12 * g_cy + 5; }

/* The front card's top left in g_card's coordinates. The area is the
   frame's client below the status line (the Card window is one pixel
   larger than that). */
static void front_pos(int *x, int *y)
{
    RECT r;
    GetClientRect(g_card, &r);
    *x = g_cx;
    *y = (r.bottom - 1) - g_cy / 2 - card_h();
}

void card_front_rect(RECT *r)
{
    int x, y;
    front_pos(&x, &y);
    SetRect(r, x, y, x + card_w(), y + card_h());
}

/* How many cards the stack draws: each card that can show any part in
   the window (with twelve cards, the seventh shows only a piece of its
   bottom edge at the right; CF10). */
int card_visible(void)
{
    int x, y, n = 0;
    RECT r;
    GetClientRect(g_card, &r);
    front_pos(&x, &y);
    while (n < (int)g_ncards && y + card_h() > 0 && x < r.right) {
        n++;
        x += 2 * g_cx;
        y -= g_cy + 1;
    }
    return n ? n : 1;
}

void card_place_edit(void)
{
    int x, y;
    front_pos(&x, &y);
    MoveWindow(g_edit, x + 1, y + g_cy + 4, 40 * g_cx + 1, 11 * g_cy, TRUE);
}

static void draw_card(HDC dc, int x, int y, LPCSTR index, BOOL front)
{
    int w = card_w();
    Rectangle(dc, x, y, x + w, y + card_h());
    if (front) {               /* the cards behind have no lines (CF1) */
        MoveTo(dc, x, y + g_cy + 1);
        LineTo(dc, x + w, y + g_cy + 1);
        MoveTo(dc, x, y + g_cy + 3);
        LineTo(dc, x + w, y + g_cy + 3);
    }
    TextOut(dc, x + 1, y + 1, index, s_len(index));
}

void card_paint(HDC dc)
{
    int x, y, n, k;
    HFONT old = SelectObject(dc, g_font);
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
    HBRUSH br = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    HPEN opn = SelectObject(dc, pen);
    HBRUSH obr = SelectObject(dc, br);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    front_pos(&x, &y);
    n = card_visible();
    for (k = n - 1; k >= 0; k--) {
        WORD i = (WORD)(((DWORD)g_cur + k) % g_ncards);
        draw_card(dc, x + k * 2 * g_cx, y - k * (g_cy + 1), card_hdr(i)->index, k == 0);
    }
    SelectObject(dc, opn);
    SelectObject(dc, obr);
    SelectObject(dc, old);
    DeleteObject(pen);
    DeleteObject(br);
}

/* the picture on the card body (dc: the Edit control's) */
void pic_draw(HDC dc, int x, int y)
{
    if (g_pic.kind == PIC_BITMAP && g_pic.bmp) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP old = SelectObject(mem, g_pic.bmp);
        SetTextColor(dc, RGB(0, 0, 0));
        SetBkColor(dc, RGB(255, 255, 255));
        /* on 3.1 the picture is a static object whose rectangle is a
           pixel smaller than the bitmap (CF8): drawn stretched into it */
        StretchBlt(dc, x + g_pic.x, y + g_pic.y, g_pic.cx - 1, g_pic.cy - 1, mem, 0, 0,
                   g_pic.cx, g_pic.cy, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    else if (g_pic.kind == PIC_OBJECT) {
        RECT r;
        SetRect(&r, x + g_pic.x, y + g_pic.y, x + g_pic.x + g_pic.cx, y + g_pic.y + g_pic.cy);
        ole_draw(&g_pic, dc, &r);
    }
}

/* ---- turning the cards ---- */

void go_card(WORD i)
{
    if (i >= g_ncards || (i == g_cur && !g_listview))
        return;
    if (!body_store())
        return;
    undo_forget();
    g_cur = i;
    body_load(i);
    SendMessage(g_list, LB_SETCURSEL, i, 0);
    InvalidateRect(g_card, NULL, TRUE);
}

void go_next(int delta)
{
    if (g_ncards < 2)
        return;
    if (delta > 0)
        go_card((WORD)((g_cur + 1) % g_ncards));
    else
        go_card(g_cur ? g_cur - 1 : g_ncards - 1);
}

/* Ctrl+Shift+letter: the first card whose index starts with it.
   UNVERIFIED: the search starts after the front card and wraps. */
void go_letter(char c)
{
    WORD i, k;
    char want[2], got[2];
    want[0] = c;
    want[1] = 0;
    AnsiUpper(want);
    for (k = 1; k <= g_ncards; k++) {
        i = (WORD)(((DWORD)g_cur + k) % g_ncards);
        got[0] = card_hdr(i)->index[0];
        got[1] = 0;
        AnsiUpper(got);
        if (got[0] == want[0]) {
            go_card(i);
            return;
        }
    }
}

/* ---- the Card window ---- */

/* Clicking a card behind the front one brings it to the front. */
static void click(int mx, int my)
{
    int x, y, n, k;
    front_pos(&x, &y);
    n = card_visible();
    for (k = n - 1; k > 0; k--) {
        int cx = x + k * 2 * g_cx, cy = y - k * (g_cy + 1);
        if (mx >= cx && mx < cx + card_w() && my >= cy && my < cy + g_cy + 2) {
            go_card((WORD)(((DWORD)g_cur + k) % g_ncards));
            return;
        }
    }
}

LRESULT CALLBACK __export CardWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            card_paint(dc);
            EndPaint(w, &ps);
        }
        return 0;
    case WM_SIZE:
        card_place_edit();
        InvalidateRect(w, NULL, TRUE);
        break;
    case WM_LBUTTONDOWN:
        click(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_LBUTTONDBLCLK:
        {
            RECT r;
            card_front_rect(&r);
            if ((int)HIWORD(lp) >= r.top && (int)HIWORD(lp) < r.top + g_cy + 2)
                do_command(IDM_INDEX);
        }
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL cardwnd_register(HINSTANCE hinst)
{
    WNDCLASS wc;
    mem_set(&wc, 0, sizeof wc);
    wc.style = CS_BYTEALIGNCLIENT | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;  /* 100Bh (CF11) */
    wc.lpfnWndProc = CardWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "Card";
    return RegisterClass(&wc);
}

/* ---- the card's Edit control ---- */

LRESULT CALLBACK __export EditWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_PRIOR || wp == VK_NEXT) {
            if (GetKeyState(VK_CONTROL) < 0)
                go_card(wp == VK_PRIOR ? 0 : g_ncards - 1);   /* UNVERIFIED */
            else
                go_next(wp == VK_PRIOR ? -1 : 1);
            return 0;
        }
        if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0 &&
            ((wp >= 'A' && wp <= 'Z') || (wp >= '0' && wp <= '9'))) {
            go_letter((char)wp);
            return 0;
        }
        if (g_picmode) {
            pic_key(wp);
            return 0;
        }
        break;
    case WM_CHAR:
        if (g_picmode)
            return 0;
        /* Ctrl+Shift+letter arrives as a control character as well */
        if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0 && wp < ' ')
            return 0;
        break;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        if (g_picmode && pic_mouse(msg, LOWORD(lp), HIWORD(lp)))
            return 0;
        break;
    case WM_PAINT:
        r = CallWindowProc(g_oldedit, w, msg, wp, lp);
        if (g_pic.kind != PIC_NONE) {
            /* the caret goes over the picture, as in the original */
            HDC dc = GetDC(w);
            HideCaret(w);
            pic_draw(dc, 0, 0);
            ShowCaret(w);
            ReleaseDC(w, dc);
        }
        return r;
    }
    return CallWindowProc(g_oldedit, w, msg, wp, lp);
}
