/*
 * track.c - moving and sizing windows with the mouse or the keyboard
 * (DefWindowProc's SC_MOVE and SC_SIZE).
 *
 * A press on the caption or a sizing border sends WM_SYSCOMMAND SC_MOVE
 * or SC_SIZE with the hit-test code in the low four bits (3.1 SDK: "the
 * four low-order bits of wParam are used internally"); from the system
 * menu they come without it and the keyboard drives: the pointer goes to
 * the caption's middle (moving) or the lower right corner (sizing) and
 * the arrow keys move it. Meanwhile a gray frame (the 50% gray pattern,
 * inverted onto the screen) shows where the window would go; the button
 * released or Enter puts it there, Esc leaves it. The window is moved
 * once, at the end (tests/scripts/input/dragpm.txt, screenshots taken
 * mid-drag on 3.1). UNVERIFIED: the keyboard step (8 pixels) and start
 * points.
 */
#include "user.h"

extern HBRUSH hbrGray;
extern HDC hdcScreen;
void minmax(PWND w, MINMAXINFO __far *mm);

static void xor_frame(const RECT *r, int t)
{
    HBRUSH old = SelectObject(hdcScreen, hbrGray);
    int w = r->right - r->left, ht = r->bottom - r->top;
    PatBlt(hdcScreen, r->left, r->top, w, t, PATINVERT);
    PatBlt(hdcScreen, r->left, r->bottom - t, w, t, PATINVERT);
    PatBlt(hdcScreen, r->left, r->top + t, t, ht - 2 * t, PATINVERT);
    PatBlt(hdcScreen, r->right - t, r->top + t, t, ht - 2 * t, PATINVERT);
    SelectObject(hdcScreen, old);
}

/* the rectangle for the pointer having moved by dx, dy from the start */
static void new_rect(RECT *r, const RECT *r0, UINT hit, int dx, int dy, POINT mn, POINT mx)
{
    *r = *r0;
    if (hit == HTCAPTION) {
        OffsetRect(r, dx, dy);
        return;
    }
    if (hit == HTLEFT || hit == HTTOPLEFT || hit == HTBOTTOMLEFT)
        r->left += dx;
    if (hit == HTRIGHT || hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT)
        r->right += dx;
    if (hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT)
        r->top += dy;
    if (hit == HTBOTTOM || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT)
        r->bottom += dy;
    /* the minimum and maximum tracking sizes (WM_GETMINMAXINFO) */
    if (r->right - r->left < mn.x) {
        if (r->left != r0->left) r->left = r->right - mn.x; else r->right = r->left + mn.x;
    }
    if (r->bottom - r->top < mn.y) {
        if (r->top != r0->top) r->top = r->bottom - mn.y; else r->bottom = r->top + mn.y;
    }
    if (r->right - r->left > mx.x) {
        if (r->left != r0->left) r->left = r->right - mx.x; else r->right = r->left + mx.x;
    }
    if (r->bottom - r->top > mx.y) {
        if (r->top != r0->top) r->top = r->bottom - mx.y; else r->bottom = r->top + mx.y;
    }
}

void track_window(HWND h, UINT hit, BOOL keyboard)
{
    PWND w = valid_wnd(h);
    RECT r0, r, shown;
    POINT p0, mn, mx;
    MINMAXINFO mm;
    MSG m;
    int t;
    BOOL done = FALSE, ok = FALSE, drawn = FALSE;
    if (!w || (w->style & (WS_MAXIMIZE | WS_MINIMIZE)) && hit != HTCAPTION)
        return;
    if (w->style & WS_MAXIMIZE)
        return;
    r0 = w->rcWindow;
    /* the frame as thick as the window's border less its black line
       (dragging Program Manager with BorderWidth=3: 3 pixels; UNVERIFIED
       for windows without a sizing border) */
    t = (w->style & WS_THICKFRAME) ? sysmet[SM_CXFRAME] - sysmet[SM_CXBORDER] : sysmet[SM_CXBORDER];
    minmax(w, &mm);
    send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
    if (!(w = valid_wnd(h)))
        return;
    mn = mm.ptMinTrackSize;
    mx = mm.ptMaxTrackSize;
    if (keyboard) {
        POINT c;
        if (hit == HTCAPTION) {
            c.x = (r0.left + r0.right) / 2;
            c.y = r0.top + sysmet[SM_CYFRAME] + sysmet[SM_CYCAPTION] / 2;
        } else {
            hit = HTBOTTOMRIGHT;
            c.x = r0.right - 1;
            c.y = r0.bottom - 1;
        }
        SetCursorPos(c.x, c.y);
    }
    GetCursorPos(&p0);
    SetCapture(h);
    shown = r0;
    xor_frame(&shown, t);
    drawn = TRUE;
    while (!done) {
        POINT p;
        if (!GetMessage(&m, 0, 0, 0)) {
            PostQuitMessage(m.wParam);
            break;
        }
        switch (m.message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_NCMOUSEMOVE:
            GetCursorPos(&p);
            new_rect(&r, &r0, hit, p.x - p0.x, p.y - p0.y, mn, mx);
            if (!EqualRect(&r, &shown)) {
                xor_frame(&shown, t);
                shown = r;
                xor_frame(&shown, t);
            }
            if (m.message == WM_LBUTTONUP)
                done = ok = TRUE;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            GetCursorPos(&p);
            switch (m.wParam) {
            case VK_ESCAPE: done = TRUE; break;
            case VK_RETURN: done = ok = TRUE; break;
            case VK_LEFT: SetCursorPos(p.x - 8, p.y); break;
            case VK_RIGHT: SetCursorPos(p.x + 8, p.y); break;
            case VK_UP: SetCursorPos(p.x, p.y - 8); break;
            case VK_DOWN: SetCursorPos(p.x, p.y + 8); break;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_NCLBUTTONDOWN:
            break;                      /* taken by the tracking */
        default:
            /* other messages (timers, posted ones) go on; painting waits
               until the frame is gone */
            if (m.message != WM_PAINT) {
                if (drawn)
                    xor_frame(&shown, t);
                DispatchMessage(&m);
                xor_frame(&shown, t);
            }
            break;
        }
        if (!valid_wnd(h))
            return;
    }
    if (drawn)
        xor_frame(&shown, t);
    ReleaseCapture();
    if (ok && !EqualRect(&shown, &r0) && (w = valid_wnd(h)) != 0) {
        RECT n = shown;
        if (w->style & WS_CHILD) {
            PWND pw = PW(w->hwndParent);
            OffsetRect(&n, -pw->rcClient.left, -pw->rcClient.top);
        }
        SetWindowPos(h, 0, n.left, n.top, n.right - n.left, n.bottom - n.top,
                     SWP_NOZORDER | SWP_NOACTIVATE |
                     (hit == HTCAPTION ? SWP_NOSIZE : 0));
    }
}
