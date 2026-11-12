/*
 * input.c - keyboard and mouse input, the cursor, focus and activation.
 *
 * The keyboard and mouse drivers call USER's event procedures at interrupt
 * time (KEYBOARD.Enable, MOUSE.Enable: Windows 3.1 DDK). USER records each
 * event in the system queue (a ring here), moves the cursor (DISPLAY's
 * MoveCursor) and wakes the task whose queue gets input. GetMessage and
 * PeekMessage take events from the head of the ring when they belong to
 * their queue, in order, and turn them into window messages.
 */
#include "user.h"

#define NSYSQ 120               /* UNVERIFIED: the system queue's length */

typedef struct SYSEV {
    WORD  msg;                  /* WM_KEYDOWN.. / WM_MOUSEMOVE.. */
    WORD  wParam;               /* virtual key */
    DWORD lParam;               /* key data, or the point */
    DWORD time;
} SYSEV;

/* What the event procedures touch at interrupt time lives in far data
   (class FAR_DATA, a fixed segment: user/user.lnk), not in the data
   segment, which is moveable and moves when the local heap grows; an
   interrupt arriving while it is being copied would write into the old
   copy. Windows 3.1's USER has a small fixed data segment besides its
   moveable one too (segment 44: 10.1). */
static SYSEV __far sysq[NSYSQ];
static volatile WORD __far sq_head, __far sq_count;
WORD __far input_pending;
BYTE __far keystate[256];       /* the driver's async state (KEYBOARD.Enable) */
static BYTE qkeystate[256];     /* GetKeyState's: as of the last message */
POINT __far ptCursor;
WORD lastkey;
void switch_window(HWND cur, BOOL tobottom);                   /* the last key pressed: Alt alone opens the menu */
HWND hwndCapture, hwndFocus, hwndActive;
HQUEUE __far hqInput;
static HCURSOR hcurCurrent;
static HCURSOR hcurSetByMsg;    /* the cursor the last WM_SETCURSOR left */
int menu_mode;                  /* menu loops running: the arrow stays */
static int cursor_level;        /* ShowCursor's count */
static RECT __far rcClip;
static int __far mthresh1 = 2, __far mthresh2, __far mspeed = 1;   /* acceleration */

static DWORD lastclick_time;
static POINT lastclick_pt;
static UINT lastclick_msg;
static HWND lastclick_hwnd;
static UINT dblclk_time = 452;  /* WIN.INI DoubleClickSpeed (US2: 01C4h) */

static BOOL in_filter_ok(UINT m, UINT lo, UINT hi)
{
    return (!lo && !hi) || (m >= lo && m <= hi);
}

static void push_event(WORD msg, WORD wp, DWORD lp)
{
    WORD f = irq_off();
    if (sq_count < NSYSQ) {
        SYSEV *e = &sysq[(sq_head + sq_count) % NSYSQ];
        e->msg = msg;
        e->wParam = wp;
        e->lParam = lp;
        e->time = tickcount;
        sq_count++;
        input_pending = 1;
    }
    irq_restore(f);
    if (hqInput)
        set_wakebit(hqInput, (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ? QS_MOUSE : QS_KEY);
}

/* KEYBOARD's event procedure: AL = virtual key, AH bit 7 = released,
   BL = scan code, BH bit 0 = extended key (DDK) */
void __far __pascal __loadds KBD_EVENT(WORD ax, WORD bx, WORD si, WORD di)
{
    BYTE vk = (BYTE)ax;
    BOOL up = (ax & 0x8000) != 0;
    DWORD lp = 1 | ((DWORD)(bx & 0xFF) << 16) | ((DWORD)(bx & 0x100) << 16);
    BOOL alt = (keystate[VK_MENU] & 0x80) != 0;
    WORD msg;
    (void)si, (void)di;
    if (up) {
        lp |= 0xC0000000L;
        keystate[vk] &= ~0x80;
    } else {
        if (keystate[vk] & 0x80)
            lp |= 0x40000000L;
        else
            keystate[vk] ^= 1;          /* toggle */
        keystate[vk] |= 0x80;
    }
    if (alt)
        lp |= 0x20000000L;
    if (vk == VK_MENU || alt || vk == VK_F10)
        msg = up ? WM_SYSKEYUP : WM_SYSKEYDOWN;
    else
        msg = up ? WM_KEYUP : WM_KEYDOWN;
    if (vk == VK_MENU && up && !(lp & 0x20000000L))
        msg = WM_SYSKEYUP;
    push_event(msg, vk, lp);
}

/* MOUSE's event procedure: AX = status (move 1, left down 2, up 4, right
   down 8, up 10h, middle 20h/40h, absolute 8000h), BX, CX = x, y (mickeys
   or 0-FFFFh absolute), DX = number of buttons (DDK) */
void __far __pascal __loadds MOUSE_EVENT_C(WORD ax, WORD bx, WORD cx, WORD dx, WORD si, WORD di)
{
    int x = ptCursor.x, y = ptCursor.y;
    (void)dx, (void)si, (void)di;
    if (ax & 1) {
        if (ax & 0x8000) {
            x = (int)(((DWORD)bx * sysmet[SM_CXSCREEN]) >> 16);
            y = (int)(((DWORD)cx * sysmet[SM_CYSCREEN]) >> 16);
        } else {
            /* acceleration (WIN.INI MouseThreshold1/2, MouseSpeed: 2, 0
               and 1 on 3.1 as SPI_GETMOUSE reports them, US2): past the
               first threshold the motion doubles, past the second with
               speed 2 it doubles again - each axis by its own motion
               (3.1: File Manager's drag of 5,2 10,2 15,3 15,3 mickeys
               dropped 4 pixels higher than w16's when either axis past
               the threshold doubled both) */
            int dx = (int)bx, dy = (int)cx, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (mspeed >= 1 && ax > mthresh1)
                dx *= (mspeed >= 2 && ax > mthresh2) ? 4 : 2;
            if (mspeed >= 1 && ay > mthresh1)
                dy *= (mspeed >= 2 && ay > mthresh2) ? 4 : 2;
            x += dx;
            y += dy;
        }
        if (x < rcClip.left) x = rcClip.left;
        if (x >= rcClip.right) x = rcClip.right - 1;
        if (y < rcClip.top) y = rcClip.top;
        if (y >= rcClip.bottom) y = rcClip.bottom - 1;
        if (x != ptCursor.x || y != ptCursor.y) {
            ptCursor.x = x;
            ptCursor.y = y;
            D_MOVECURSOR(x, y);
            push_event(WM_MOUSEMOVE, 0, MAKELONG(x, y));
        }
    }
    if (ax & 2) { keystate[VK_LBUTTON] |= 0x80; push_event(WM_LBUTTONDOWN, 0, MAKELONG(x, y)); }
    if (ax & 4) { keystate[VK_LBUTTON] &= ~0x80; push_event(WM_LBUTTONUP, 0, MAKELONG(x, y)); }
    if (ax & 8) { keystate[VK_RBUTTON] |= 0x80; push_event(WM_RBUTTONDOWN, 0, MAKELONG(x, y)); }
    if (ax & 0x10) { keystate[VK_RBUTTON] &= ~0x80; push_event(WM_RBUTTONUP, 0, MAKELONG(x, y)); }
    if (ax & 0x20) { keystate[VK_MBUTTON] |= 0x80; push_event(WM_MBUTTONDOWN, 0, MAKELONG(x, y)); }
    if (ax & 0x40) { keystate[VK_MBUTTON] &= ~0x80; push_event(WM_MBUTTONUP, 0, MAKELONG(x, y)); }
}

/* ---- WH_JOURNALPLAYBACK (3.1 SDK): while such a hook is set, input
   comes from it. HC_GETNEXT fills an EVENTMSG (message, paramL, paramH:
   the screen point, or virtual key and scan code in paramL); a non-zero
   answer is a wait in milliseconds before the event, asked again while
   waiting (US9 and the SHELL probes play their drags so); HC_SKIP moves
   on. Polled at task time from GetMessage/PeekMessage. UNVERIFIED: real
   input is not shut out meanwhile. ---- */
static BOOL jwaiting;
static DWORD jdue;

BOOL journal_active(void)
{
    extern BOOL hook_set(int type);
    return hook_set(WH_JOURNALPLAYBACK);
}

void journal_poll(void)
{
    extern LRESULT call_hook(int type, int code, WPARAM wp, LPARAM lp);
    EVENTMSG ev;
    LRESULT r;
    if (!journal_active()) {
        jwaiting = FALSE;
        return;
    }
    fmemset(&ev, 0, sizeof ev);
    r = call_hook(WH_JOURNALPLAYBACK, HC_GETNEXT, 0, (LPARAM)(EVENTMSG __far *)&ev);
    if (r > 0 && !jwaiting) {
        jwaiting = TRUE;
        jdue = GetTickCount() + r;
        return;
    }
    if (jwaiting && (long)(GetTickCount() - jdue) < 0)
        return;
    jwaiting = FALSE;
    if (ev.message >= WM_MOUSEFIRST && ev.message <= WM_MOUSELAST) {
        int x = (int)ev.paramL, y = (int)ev.paramH;
        if (x < rcClip.left) x = rcClip.left;
        if (x >= rcClip.right) x = rcClip.right - 1;
        if (y < rcClip.top) y = rcClip.top;
        if (y >= rcClip.bottom) y = rcClip.bottom - 1;
        if (x != ptCursor.x || y != ptCursor.y) {
            ptCursor.x = x;
            ptCursor.y = y;
            D_MOVECURSOR(x, y);
            push_event(WM_MOUSEMOVE, 0, MAKELONG(x, y));
        }
        switch (ev.message) {
        case WM_LBUTTONDOWN: keystate[VK_LBUTTON] |= 0x80; break;
        case WM_LBUTTONUP: keystate[VK_LBUTTON] &= ~0x80; break;
        case WM_RBUTTONDOWN: keystate[VK_RBUTTON] |= 0x80; break;
        case WM_RBUTTONUP: keystate[VK_RBUTTON] &= ~0x80; break;
        case WM_MBUTTONDOWN: keystate[VK_MBUTTON] |= 0x80; break;
        case WM_MBUTTONUP: keystate[VK_MBUTTON] &= ~0x80; break;
        }
        if (ev.message != WM_MOUSEMOVE)
            push_event(ev.message, 0, MAKELONG(x, y));
    } else if (ev.message >= WM_KEYFIRST && ev.message <= WM_KEYLAST) {
        BOOL up = ev.message == WM_KEYUP || ev.message == WM_SYSKEYUP;
        KBD_EVENT((ev.paramL & 0xFF) | (up ? 0x8000 : 0), (ev.paramL >> 8) & 0xFF, 0, 0);
    }
    call_hook(WH_JOURNALPLAYBACK, HC_SKIP, 0, 0);
}

void input_init(void)
{
    SetRect(&rcClip, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
    ptCursor.x = sysmet[SM_CXSCREEN] / 2;
    ptCursor.y = sysmet[SM_CYSCREEN] / 2;
    dblclk_time = GetProfileInt("windows", "DoubleClickSpeed", 452);
    mthresh1 = GetProfileInt("windows", "MouseThreshold1", 2);
    mthresh2 = GetProfileInt("windows", "MouseThreshold2", 0);
    mspeed = GetProfileInt("windows", "MouseSpeed", 1);
    /* the display's cursor procedure, as fast as the system timer goes
       (US1: CreateSystemTimer(0, DISPLAY.CheckCursor)) */
    S_CREATESYSTEMTIMER(0, (FARPROC)D_CHECKCURSOR);
}

void input_enable(void)
{

    S_ENABLESYSTEMTIMERS();
    KB_ENABLE((FARPROC)keybd_event_entry, keystate);
    MS_ENABLE((FARPROC)mouse_event_entry);
    D_MOVECURSOR(ptCursor.x, ptCursor.y);
    set_cursor_shape(load_icon_cursor(0, IDC_ARROW, TRUE));
}

/* ---- the cursor ---- */
void set_cursor_shape(HCURSOR h)
{
    hcurCurrent = h;
    if (cursor_level >= 0 && h)
        D_SETCURSOR(GlobalLock(h));
    else
        D_SETCURSOR(0);
    if (h)
        GlobalUnlock(h);
}

/* USER.247 (it was a stub answering 0) */
HCURSOR WINAPI GetCursor(void)
{
    return hcurCurrent;
}

HCURSOR WINAPI SetCursor(HCURSOR h)
{
    HCURSOR old = hcurCurrent;
    if (h != hcurCurrent)
        set_cursor_shape(h);
    return old;
}

int WINAPI ShowCursor(BOOL show)
{
    cursor_level += show ? 1 : -1;
    if ((show && cursor_level == 0) || (!show && cursor_level == -1))
        set_cursor_shape(hcurCurrent);
    return cursor_level;
}

void WINAPI GetCursorPos(POINT __far *p)
{
    *p = ptCursor;
}

/* A window appeared, went or moved where the pointer is: a mouse move
   where it stands, so the window now under it sets the cursor (3.1: the
   I-beam came up over Notepad's new edit control without the mouse
   moving; an hourglass left by a program that had finished stayed until
   the mouse moved). UNVERIFIED: the events 3.1 queues for it. */
void pointer_changed(const RECT *r)
{
    POINT p;
    WORD f = irq_off();
    p.x = ptCursor.x;
    p.y = ptCursor.y;
    irq_restore(f);
    if (!r || PtInRect(r, p))
        push_event(WM_MOUSEMOVE, 0, MAKELONG(p.x, p.y));
}

/* A task about to wait for messages: if the cursor is not what the window
   under it last chose (a program changed it - an hourglass while busy -
   and has gone idle), that window gets to choose again (3.1 showed
   Notepad's I-beam as soon as it was ready, the mouse untouched;
   UNVERIFIED how 3.1 decides when) */
void idle_cursor_check(HQUEUE hq)
{
    HWND w;
    if (hcurCurrent == hcurSetByMsg || hwndCapture || menu_mode)
        return;
    w = WindowFromPoint(ptCursor);
    /* or the desktop, which no task owns: Windows Write turned on its
       ruler with the pointer on the desktop below its window; 3.1 showed
       the arrow again after, w16 kept Write's I-beam */
    if (w && valid_wnd(w) && (PW(w)->hq == hq || w == hwndDesktop)) {
        hcurSetByMsg = hcurCurrent;     /* once per change */
        pointer_changed(NULL);
    }
}

void WINAPI SetCursorPos(int x, int y)
{
    WORD f = irq_off();
    ptCursor.x = x;
    ptCursor.y = y;
    irq_restore(f);
    D_MOVECURSOR(x, y);
    push_event(WM_MOUSEMOVE, 0, MAKELONG(x, y));
}

void WINAPI ClipCursor(const RECT __far *r)
{
    if (r)
        rcClip = *r;
    else
        SetRect(&rcClip, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
}

void WINAPI GetClipCursor(RECT __far *r)
{
    *r = rcClip;
}

/* ---- key state ---- */
int WINAPI GetKeyState(int vk)
{
    BYTE s = qkeystate[vk & 0xFF];
    return (s & 0x80 ? 0xFF80 : 0) | (s & 1);
}

int WINAPI GetAsyncKeyState(int vk)
{
    BYTE s = keystate[vk & 0xFF];
    return s & 0x80 ? 0x8000 : 0;
}

void WINAPI GetKeyboardState(BYTE __far *b)
{
    int i;
    for (i = 0; i < 256; i++)
        b[i] = qkeystate[i];
}

void WINAPI SetKeyboardState(BYTE __far *b)
{
    int i;
    for (i = 0; i < 256; i++)
        qkeystate[i] = b[i];
}

UINT WINAPI GetDoubleClickTime(void)
{
    return dblclk_time;
}

void WINAPI SetDoubleClickTime(UINT t)
{
    dblclk_time = t ? t : 500;
}

/* ---- capture, focus, activation ---- */
HWND WINAPI SetCapture(HWND h)
{
    HWND old = hwndCapture;
    hwndCapture = h;
    if (h && valid_wnd(h))
        hqInput = PW(h)->hq;
    return old;
}

void WINAPI ReleaseCapture(void)
{
    hwndCapture = 0;
    if (hwndActive && valid_wnd(hwndActive))
        hqInput = PW(hwndActive)->hq;
    else
        hqInput = 0;
}

HWND WINAPI GetCapture(void)
{
    return hwndCapture;
}

void set_focus(HWND h)
{
    HWND old = hwndFocus;
    if (h == old)
        return;
    if (old && valid_wnd(old))
        send_message(old, WM_KILLFOCUS, h, 0);
    hwndFocus = h;
    if (h && valid_wnd(h))
        send_message(h, WM_SETFOCUS, old, 0);
}

HWND WINAPI SetFocus(HWND h)
{
    HWND old = hwndFocus, top;
    PWND w;
    if (h) {
        w = valid_wnd(h);
        if (!w || (w->style & (WS_DISABLED | WS_MINIMIZE)) && 0)
            return 0;
        top = top_level(h);
        if (top != hwndActive)
            activate_window(top, FALSE);
    }
    set_focus(h);
    return old;
}

HWND WINAPI GetFocus(void)
{
    return hwndFocus;
}

HWND WINAPI GetActiveWindow(void)
{
    return hwndActive;
}

void activate_window(HWND h, BOOL mouse)
{
    HWND old = hwndActive;
    PWND w = valid_wnd(h);
    if (h == old)
        return;
    if (h && !w)
        return;
    if (old && valid_wnd(old)) {
        send_message(old, WM_NCACTIVATE, FALSE, 0);
        send_message(old, WM_ACTIVATE, WA_INACTIVE, MAKELONG(h, 0));
        if (!h || PW(old)->hq != w->hq)
            send_message(old, WM_ACTIVATEAPP, FALSE, 0);
    }
    hwndActive = h;
    if (!h) {
        hqInput = 0;
        return;
    }
    hqInput = w->hq;
    set_window_pos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!old || !valid_wnd(old) || PW(old)->hq != w->hq)
        send_message(h, WM_ACTIVATEAPP, TRUE, 0);
    send_message(h, WM_NCACTIVATE, TRUE, 0);
    send_message(h, WM_ACTIVATE, mouse ? WA_CLICKACTIVE : WA_ACTIVE,
                 MAKELONG(old, (w->style & WS_MINIMIZE) ? 1 : 0));
    /* the focus stays inside the active window or goes: an icon made
       active keeps none (DefWindowProc gives it the focus only when not
       minimised), and the keys go to the icon as WM_SYSKEYDOWN. It used
       to stay in the window before: with Program Manager minimised and
       Notepad's icon active, Alt+Esc went to Program Manager */
    if (hwndFocus && hwndActive == h && hwndFocus != h && !IsChild(h, hwndFocus))
        set_focus(0);
    (void)mouse;
}

HWND WINAPI SetActiveWindow(HWND h)
{
    HWND old = hwndActive;
    PWND w = valid_wnd(h);
    if (w && (w->style & WS_CHILD))
        return 0;
    activate_window(h, FALSE);
    return old;
}

/* ---- turning events into messages ---- */
static HWND key_target(void)
{
    if (hwndFocus && valid_wnd(hwndFocus))
        return hwndFocus;
    return hwndActive;
}

/* the window the mouse is over, and the hit-test code */
static HWND mouse_target(POINT pt, int *hit)
{
    HWND h;
    if (hwndCapture && valid_wnd(hwndCapture)) {
        *hit = HTCLIENT;
        return hwndCapture;
    }
    h = WindowFromPoint(pt);
    if (!h)
        h = hwndDesktop;
    *hit = HTCLIENT;
    return h;
}

/* would a GetMessage/PeekMessage with this filter take the event (as
   message m1 or m2, for target)? */
static BOOL wanted(UINT m1, UINT m2, HWND target, HWND hf, UINT lo, UINT hi)
{
    if (!in_filter_ok(m1, lo, hi) && !in_filter_ok(m2, lo, hi) &&
        !in_filter_ok(m1 + 2, lo, hi))          /* the double-click form */
        return FALSE;
    if (hf && target != hf && !IsChild(hf, target))
        return FALSE;
    return TRUE;
}

/* take event number at (from the head) if it is still e (a nested
   reader may have taken it), closing the gap; FALSE: it was not */
static BOOL consume(WORD at, const SYSEV *e)
{
    WORD f = irq_off(), i;
    SYSEV *q;
    BOOL ok = at < sq_count;
    if (ok) {
        q = &sysq[(sq_head + at) % NSYSQ];
        ok = q->msg == e->msg && q->time == e->time && q->lParam == e->lParam &&
             q->wParam == e->wParam;
    }
    if (ok) {
        for (i = at; i > 0; i--)        /* the ones before it move up one */
            sysq[(sq_head + i) % NSYSQ] = sysq[(sq_head + i - 1) % NSYSQ];
        sq_head = (sq_head + 1) % NSYSQ;
        sq_count--;
    }
    irq_restore(f);
    return ok;
}

BOOL input_message(HQUEUE hq, HWND hf, UINT lo, UINT hi, BOOL remove, MSG __far *m)
{
    SYSEV e;
    HWND target;
    PWND w;
    int hit = HTCLIENT;
    WORD f, at = 0;             /* the event looked at, from the head */
    for (;; at++) {
        f = irq_off();
        if (at >= sq_count) {
            if (!sq_count)
                input_pending = 0;
            irq_restore(f);
            return FALSE;
        }
        e = sysq[(sq_head + at) % NSYSQ];
        irq_restore(f);
        if (e.msg >= WM_MOUSEFIRST && e.msg <= WM_MOUSELAST) {
            POINT pt;
            pt.x = LOWORD(e.lParam);
            pt.y = HIWORD(e.lParam);
            target = mouse_target(pt, &hit);
            w = valid_wnd(target);
            /* another task's window takes it from there; the desktop's
               (no queue: it belongs to no task) whoever reads first. A
               mouse event over the desktop used to stay at the head for
               ever, and with it all input behind it: the pointer moved,
               nothing else did */
            if (w && w->hq && w->hq != hq) {
                set_wakebit(w->hq, QS_MOUSE);
                return FALSE;
            }
            /* an event the caller's filter does not take stays where it
               is (it used to be thrown away: keys typed while a program
               peeked for mouse messages were lost) */
            if (!wanted(e.msg, e.msg + WM_NCMOUSEMOVE - WM_MOUSEMOVE, target, hf, lo, hi))
                continue;
            /* PM_NOREMOVE: the message it would be, the event left in
               place (it used to be taken: the idle check of a dialog's
               modal loop, a PeekMessage, ate keys and clicks) */
            if (!remove) {
                if (!w)
                    continue;
                if (!hwndCapture)
                    hit = (int)send_message(target, WM_NCHITTEST, 0, e.lParam);
                m->hwnd = target;
                m->time = e.time;
                m->pt = pt;
                if (hit != HTCLIENT) {
                    m->message = e.msg + WM_NCMOUSEMOVE - WM_MOUSEMOVE;
                    m->wParam = hit;
                    m->lParam = e.lParam;
                } else {
                    POINT c = pt;
                    if (valid_wnd(target))
                        ScreenToClient(target, &c);
                    m->message = e.msg;
                    m->wParam = 0;
                    m->lParam = MAKELONG(c.x, c.y);
                }
                return TRUE;
            }
            /* consumed before anything is sent: a window procedure that
               reads messages meanwhile must not get the same event */
            {
                BOOL took = consume(at, &e);
                at = (WORD)-1;          /* from the head again either way */
                if (!took)
                    continue;
            }
            if (w && !hwndCapture) {
                hit = (int)send_message(target, WM_NCHITTEST, 0, e.lParam);
                if (!valid_wnd(target))
                    hit = HTNOWHERE;
            }
            if (!w)
                continue;
            if ((e.msg == WM_LBUTTONDOWN || e.msg == WM_RBUTTONDOWN || e.msg == WM_MBUTTONDOWN) &&
                hit == HTCLIENT) {
                /* a click on a child: WM_PARENTNOTIFY up its parents (3.1
                   SDK; before WM_MOUSEACTIVATE, as File Manager's
                   directory window and frame got it on 3.1 - it starts its
                   drag from there) */
                HWND c = target;
                PWND cw;
                while ((cw = valid_wnd(c)) != 0 && (cw->style & WS_CHILD) &&
                       !(cw->exstyle & WS_EX_NOPARENTNOTIFY) && cw->hwndParent) {
                    HWND p = cw->hwndParent;
                    POINT pc = pt;
                    ScreenToClient(p, &pc);
                    send_message(p, WM_PARENTNOTIFY, e.msg, MAKELONG(pc.x, pc.y));
                    c = p;
                }
                if (!valid_wnd(target))
                    continue;
            }
            {
                /* a disabled window (one a modal dialog keeps from input)
                   gets no mouse messages: the cursor is set, a press
                   beeps (3.1 SDK). Program Manager under its Exit dialog
                   took the second release of a double-click and came back
                   in front of the dialog */
                extern BOOL sysmodal_blocks(HWND h);
                PWND tw = valid_wnd(target);
                PWND tt = tw ? valid_wnd(top_level(target)) : (PWND)0;
                if ((tw && (tw->style & WS_DISABLED)) || (tt && (tt->style & WS_DISABLED)) ||
                    (tw && sysmodal_blocks(top_level(target)))) {
                    if (!menu_mode)
                        send_message(target, WM_SETCURSOR, target, MAKELONG(HTERROR, e.msg));
                    if (e.msg == WM_LBUTTONDOWN || e.msg == WM_RBUTTONDOWN || e.msg == WM_MBUTTONDOWN)
                        MessageBeep(0);
                    continue;
                }
            }
            if (e.msg == WM_LBUTTONDOWN || e.msg == WM_RBUTTONDOWN || e.msg == WM_MBUTTONDOWN) {
                /* activation on a press (not on a release) */
                /* WM_MOUSEACTIVATE whenever the window clicked is not the
                   active one - a child of the active window included (3.1:
                   File Manager's file list passed it up to the directory
                   window and the frame); activation only for another
                   top-level window */
                HWND top = top_level(target);
                if (top && target != hwndActive && top != hwndDesktop) {
                    int r = (int)send_message(target, WM_MOUSEACTIVATE, top,
                                              MAKELONG(hit, e.msg));
                    if (top != hwndActive && r != MA_NOACTIVATE && r != MA_NOACTIVATEANDEAT)
                        activate_window(top, TRUE);
                    if (r == MA_ACTIVATEANDEAT || r == MA_NOACTIVATEANDEAT)
                        continue;
                }
            }
            if (valid_wnd(target) && !menu_mode) {
                send_message(target, WM_SETCURSOR, target, MAKELONG(hit, e.msg));
                hcurSetByMsg = hcurCurrent;
            }
            m->hwnd = target;
            m->time = e.time;
            m->pt = pt;
            {
                WORD keys = 0;
                if (keystate[VK_LBUTTON] & 0x80) keys |= MK_LBUTTON;
                if (keystate[VK_RBUTTON] & 0x80) keys |= MK_RBUTTON;
                if (keystate[VK_MBUTTON] & 0x80) keys |= MK_MBUTTON;
                if (keystate[VK_SHIFT] & 0x80) keys |= MK_SHIFT;
                if (keystate[VK_CONTROL] & 0x80) keys |= MK_CONTROL;
                m->wParam = keys;
            }
            m->message = e.msg;
            if ((e.msg == WM_LBUTTONDOWN || e.msg == WM_RBUTTONDOWN || e.msg == WM_MBUTTONDOWN)) {
                if (e.msg == lastclick_msg && target == lastclick_hwnd &&
                    e.time - lastclick_time <= dblclk_time &&
                    iabs(pt.x - lastclick_pt.x) <= sysmet[SM_CXDOUBLECLK] / 2 &&
                    iabs(pt.y - lastclick_pt.y) <= sysmet[SM_CYDOUBLECLK] / 2 &&
                    ((((PCLS)PW(target)->cls)->style & CS_DBLCLKS) || hit != HTCLIENT)) {
                    m->message = e.msg + 2;     /* ...DBLCLK */
                    lastclick_msg = 0;
                } else {
                    lastclick_msg = e.msg;
                    lastclick_time = e.time;
                    lastclick_pt = pt;
                    lastclick_hwnd = target;
                }
            }
            if (hit != HTCLIENT) {
                m->message += WM_NCMOUSEMOVE - WM_MOUSEMOVE;
                m->wParam = hit;
                m->lParam = e.lParam;
            } else {
                POINT c = pt;
                ScreenToClient(target, &c);
                m->lParam = MAKELONG(c.x, c.y);
            }
        } else {
            target = key_target();
            w = valid_wnd(target);
            if (w && w->hq && w->hq != hq) {
                set_wakebit(w->hq, QS_KEY);
                return FALSE;
            }
            if (!wanted(e.msg, e.msg, w ? target : hwndActive, hf, lo, hi))
                continue;
            if (!remove) {
                m->hwnd = w ? target : hwndActive;
                m->message = e.msg;
                m->wParam = e.wParam;
                m->lParam = e.lParam;
                m->time = e.time;
                m->pt = ptCursor;
                return TRUE;
            }
            {
                BOOL took = consume(at, &e);
                at = (WORD)-1;          /* from the head again either way */
                if (!took)
                    continue;
            }
            {
                BYTE vk = (BYTE)e.wParam;
                if (e.lParam & 0x80000000L)
                    qkeystate[vk] &= ~0x80;
                else {
                    lastkey = vk;
                    if (!(qkeystate[vk] & 0x80))
                        qkeystate[vk] ^= 1;
                    qkeystate[vk] |= 0x80;
                }
            }
            if (!w) {
                if (!hwndActive) {
                    /* nothing active (the active program ended and no
                       window could take over): Alt+Tab and Alt+Esc
                       still switch, Ctrl+Esc still opens the Task List */
                    BYTE vk = (BYTE)e.wParam;
                    if (e.msg == WM_SYSKEYDOWN && (vk == VK_TAB || vk == VK_ESCAPE))
                        switch_window(0, FALSE);
                    else if (e.msg == WM_KEYDOWN && vk == VK_ESCAPE && (qkeystate[VK_CONTROL] & 0x80))
                        WinExec("taskman.exe", SW_SHOWNORMAL);
                    continue;
                }
                target = hwndActive;
                if (e.msg == WM_KEYDOWN)
                    e.msg = WM_SYSKEYDOWN;
                else if (e.msg == WM_KEYUP)
                    e.msg = WM_SYSKEYUP;
            }
            m->hwnd = target;
            m->message = e.msg;
            m->wParam = e.wParam;
            m->lParam = e.lParam;
            m->time = e.time;
            m->pt = ptCursor;
        }
        if (!in_filter_ok(m->message, lo, hi) || (hf && m->hwnd != hf && !IsChild(hf, m->hwnd)))
            continue;                   /* UNVERIFIED: Windows keeps it for later */
        return TRUE;
    }
}

/* ---- TranslateMessage: WM_KEYDOWN -> WM_CHAR through the keyboard driver ---- */
BOOL WINAPI TranslateMessage(const MSG __far *m)
{
    BYTE ks[256];
    DWORD ch = 0;
    int n;
    UINT cm;
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN)
        return FALSE;
    GetKeyboardState(ks);
    n = KB_TOASCII(m->wParam, HIWORD(m->lParam), ks, &ch, 0);
    if (n <= 0)
        return FALSE;
    cm = m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
    post_message(cur_hq(), m->hwnd, cm, (BYTE)ch, m->lParam);
    if (n > 1)
        post_message(cur_hq(), m->hwnd, cm, (BYTE)(ch >> 8), m->lParam);
    return TRUE;
}
