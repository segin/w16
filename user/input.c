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

/* The system queue's length: 3.1's holds 120 events, and a key or
   button going down needs two places free, so that the matching up
   always fits (US29 sq: 100 presses and releases with nobody reading
   left 59 downs and 61 ups, the last an up, for keys and the left button
   alike). w16 dropped whatever came once full, ups too, leaving keys and
   buttons held down (USER audit, item 17). */
#define NSYSQ 120

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
static BYTE __far qkeystate[256];     /* GetKeyState's: as of the last message */
POINT __far ptCursor;
WORD lastkey;
void switch_window(HWND cur, BOOL tobottom);                   /* the last key pressed: Alt alone opens the menu */
void start_taskman(int x, int y);                             /* the Task List (defwnd.c) */
HWND hwndCapture, hwndFocus, hwndActive;
HQUEUE __far hqInput;
static HCURSOR hcurCurrent;
static HCURSOR hcurSetByMsg;    /* the cursor the last WM_SETCURSOR left */
int menu_mode;                  /* menu loops running: the arrow stays */
static int cursor_level;        /* ShowCursor's count */
static RECT __far rcClip;
static int __far mthresh1 = 2, __far mthresh2, __far mspeed = 1;   /* acceleration */
/* the buttons swapped (SwapMouseButton): read at interrupt time, so a far
   variable, not sysmet[] in DGROUP (tools/irqcheck.py) */
BOOL __far mouse_swapped;
static int __far cxScreen, __far cyScreen;  /* sysmet's, for interrupt time */

static DWORD lastclick_time;
static POINT lastclick_pt;
static UINT lastclick_msg;
static HWND lastclick_hwnd;
static UINT dblclk_time = 452;  /* WIN.INI DoubleClickSpeed (US2: 01C4h) */

static BOOL in_filter_ok(UINT m, UINT lo, UINT hi)
{
    return (!lo && !hi) || (m >= lo && m <= hi);
}

/* a key or a mouse button going down */
static BOOL is_down(WORD msg)
{
    return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_LBUTTONDOWN ||
           msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
}

/* the MK_ key state now (interrupt time: keystate is far data) */
static WORD mk_state(void)
{
    WORD keys = 0;
    if (keystate[VK_LBUTTON] & 0x80) keys |= MK_LBUTTON;
    if (keystate[VK_RBUTTON] & 0x80) keys |= MK_RBUTTON;
    if (keystate[VK_MBUTTON] & 0x80) keys |= MK_MBUTTON;
    if (keystate[VK_SHIFT] & 0x80) keys |= MK_SHIFT;
    if (keystate[VK_CONTROL] & 0x80) keys |= MK_CONTROL;
    return keys;
}

static void push_event(WORD msg, WORD wp, DWORD lp)
{
    WORD f;
    /* a mouse event keeps the buttons and keys as they were when it
       happened (3.1, US25: a press and a release made while the program
       was busy came as WM_LBUTTONDOWN with MK_LBUTTON, the moves between
       with it, the release and the moves after without; w16 gave them
       all the state at the time they were read) */
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
        wp = mk_state();
    f = irq_off();
    /* a move after a move still queued replaces it: the program gets one
       WM_MOUSEMOVE at the latest point, not one for every place the
       pointer passed while it was busy (3.1, US25: eight moves fed while
       the probe read no messages, one a tick or all in one tick, came as
       one message at the last point; a press or a release in between
       keeps the moves on either side of it). Without this a drag loop
       that draws at each move (the scroll-bar thumb's outline) fell
       behind the pointer */
    if (msg == WM_MOUSEMOVE && sq_count) {
        SYSEV *l = &sysq[(sq_head + sq_count - 1) % NSYSQ];
        if (l->msg == WM_MOUSEMOVE) {
            l->wParam = wp;
            l->lParam = lp;
            l->time = tickcount;
            irq_restore(f);
            if (hqInput)
                set_wakebit(hqInput, QS_MOUSE);
            return;
        }
    }
    if (sq_count < (is_down(msg) ? NSYSQ - 2 : NSYSQ)) {
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
            x = (int)(((DWORD)bx * cxScreen) >> 16);
            y = (int)(((DWORD)cx * cyScreen) >> 16);
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
    /* swapped buttons (SwapMouseButton, WIN.INI SwapMouseButtons): the
       physical left button is the right one from here on */
    if (mouse_swapped)
        ax = (ax & ~0x1E) | ((ax & 6) << 2) | ((ax & 0x18) >> 2);
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

/* WH_JOURNALRECORD (system-wide): each event as it is taken from the
   system queue (US30 jr on 3.1: HC_ACTION with WM_KEYDOWN, WM_KEYUP and
   the virtual key in paramL's low byte, as the messages are removed). The
   EVENTMSG as playback takes it (above); paramH a key's repeat count
   (UNVERIFIED). */
static void journal_record(const SYSEV *e)
{
    extern LRESULT call_hook(int type, int code, WPARAM wp, LPARAM lp);
    EVENTMSG ev;
    if (!hook_set(WH_JOURNALRECORD))
        return;
    ev.message = e->msg;
    if (e->msg >= WM_MOUSEFIRST && e->msg <= WM_MOUSELAST) {
        ev.paramL = LOWORD(e->lParam);
        ev.paramH = HIWORD(e->lParam);
    } else {
        ev.paramL = (BYTE)e->wParam | ((WORD)(BYTE)HIWORD(e->lParam) << 8);
        ev.paramH = LOWORD(e->lParam);
    }
    ev.time = e->time;
    call_hook(WH_JOURNALRECORD, HC_ACTION, 0, (LPARAM)(EVENTMSG __far *)&ev);
}

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
    cxScreen = sysmet[SM_CXSCREEN];
    cyScreen = sysmet[SM_CYSCREEN];
    SetRect(&rcClip, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
    ptCursor.x = sysmet[SM_CXSCREEN] / 2;
    ptCursor.y = sysmet[SM_CYSCREEN] / 2;
    dblclk_time = GetProfileInt("windows", "DoubleClickSpeed", 452);
    mthresh1 = GetProfileInt("windows", "MouseThreshold1", 2);
    mthresh2 = GetProfileInt("windows", "MouseThreshold2", 0);
    mspeed = GetProfileInt("windows", "MouseSpeed", 1);
    mouse_swapped = sysmet[SM_SWAPBUTTON];
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

/* USER.4 and USER.3: WINOLDAP's screen switch around a DOS program in
   Standard mode (Schulman ch. 6; Pietrek ch. 1). US10 on 3.1, Microsoft's
   USER between forwarders: DisableOEMLayer is DISPLAY.SetCursor(NULL),
   MOUSE.Disable, KEYBOARD.Disable, SYSTEM.DisableSystemTimers and GDI's
   Death on the screen's DC (the display driver's Disable: BIOS mode 3);
   EnableOEMLayer is GDI's Resurrection on it (mode 12h again),
   SYSTEM.EnableSystemTimers, KEYBOARD.Enable, MOUSE.Enable, the whole
   screen invalidated (windows then get WM_PAINT, erasing and their frames
   in BeginPaint) and the cursor shape set again. UNVERIFIED: the drivers'
   InternalBroadcastDriverMessage notices Schulman mentions (none of
   w16's drivers needs them) */
void input_disable(void)
{
    D_SETCURSOR(0);
    MS_DISABLE();
    KB_DISABLE();
    S_DISABLESYSTEMTIMERS();
}

void WINAPI DisableOEMLayer(void)
{
    ulog("DisableOEMLayer\r\n");
    input_disable();
    G_DEATH(hdcScreen);
}

void WINAPI EnableOEMLayer(void)
{
    ulog("EnableOEMLayer\r\n");
    G_RESURRECTION(hdcScreen, 0, 0, 0, 0, 0, 0);
    S_ENABLESYSTEMTIMERS();
    KB_ENABLE((FARPROC)keybd_event_entry, keystate);
    MS_ENABLE((FARPROC)mouse_event_entry);
    savebits_forget_all();
    RedrawWindow(hwndDesktop, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    set_cursor_shape(hcurCurrent);
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

/* A caller's POINT, RECT and key state array through gp_copy (gp.asm): a
   bad one is left as it was, nothing set from it (3.1, US27) */
void WINAPI GetCursorPos(POINT __far *p)
{
    gp_copy(p, &ptCursor, sizeof(POINT));
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
    RECT a;
    if (r) {
        if (gp_copy(&a, r, sizeof a) == sizeof a)
            rcClip = a;
    } else
        SetRect(&rcClip, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
}

void WINAPI GetClipCursor(RECT __far *r)
{
    gp_copy(r, &rcClip, sizeof(RECT));
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
    gp_copy(b, qkeystate, 256);
}

void WINAPI SetKeyboardState(BYTE __far *b)
{
    BYTE t[256];
    int i;
    if (gp_copy(t, b, 256) != 256)
        return;
    for (i = 0; i < 256; i++)
        qkeystate[i] = t[i];
}

/* the acceleration, for SystemParametersInfo's SPI_GETMOUSE/SPI_SETMOUSE
   (Control Panel's Mouse Tracking Speed: w16 used to answer 2, 0, 1
   whatever was set and ignore SPI_SETMOUSE, so the bar did nothing) */
void mouse_accel(int __far *get, const int __far *set)
{
    if (get) {
        get[0] = mthresh1;
        get[1] = mthresh2;
        get[2] = mspeed;
    }
    if (set) {
        mthresh1 = set[0];
        mthresh2 = set[1];
        mspeed = set[2];
    }
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
    BOOL was = hwndCapture != 0;
    hwndCapture = 0;
    if (hwndActive && valid_wnd(hwndActive))
        hqInput = PW(hwndActive)->hq;
    else
        hqInput = 0;
    /* a mouse move where the pointer is, so that the window under it
       sets the cursor at once (3.1, US22: after ReleaseCapture, with the
       pointer still, the window got WM_SETCURSOR and WM_NCMOUSEMOVE or
       WM_MOUSEMOVE at that point - over a sizing border the sizing arrow
       came up without a move; the message came after ReleaseCapture had
       returned) */
    if (was)
        pointer_changed(NULL);
}

HWND WINAPI GetCapture(void)
{
    return hwndCapture;
}

static BOOL focus_hooked;       /* SetFocus has called WH_CBT already */

void set_focus(HWND h)
{
    HWND old = hwndFocus;
    if (h == old)
        return;
    /* WH_CBT (US30 cbt on 3.1: C9 new, old for every change, the
       focus taken away by a minimise too); nonzero: no change */
    if (!focus_hooked && cbt_hook(HCBT_SETFOCUS, (WPARAM)h, MAKELONG(old, 0)))
        return;
    focus_hooked = FALSE;
    if (hwndFocus != old)
        return;
    /* GetFocus answers the new window already inside WM_KILLFOCUS (US12
       on 3.1: A's WM_KILLFOCUS saw B, and 0 for SetFocus(NULL)) */
    hwndFocus = h;
    if (old && valid_wnd(old))
        send_message(old, WM_KILLFOCUS, h, 0);
    if (hwndFocus != h)     /* the old window moved it on (UNVERIFIED) */
        return;
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
        /* WH_CBT before the activation it brings (US30 cbt on 3.1, the
           window inactive: C9 b m, then C5 a, C9 a m from the activation,
           and the focus then goes to b with no more calls) */
        if (h != hwndFocus && cbt_hook(HCBT_SETFOCUS, (WPARAM)h, MAKELONG(old, 0)))
            return 0;
        if (!valid_wnd(h))
            return 0;
        if (top != hwndActive)
            activate_window(top, FALSE);
        focus_hooked = TRUE;
    }
    set_focus(h);
    focus_hooked = FALSE;
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
    /* WH_CBT (US30 cbt on 3.1: C5 with fMouse and the active window);
       nonzero: no activation */
    if (h && hook_set(WH_CBT)) {
        struct { BOOL fMouse; HWND hWndActive; } ca;
        ca.fMouse = mouse;
        ca.hWndActive = old;
        if (cbt_hook(HCBT_ACTIVATE, (WPARAM)h, (LPARAM)(void __far *)&ca) || !(w = valid_wnd(h)))
            return;
    }
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
    {   /* the owner's last active popup (GetLastActivePopup) */
        PWND o = w;
        while (o->hwndOwner && valid_wnd(o->hwndOwner))
            o = PW(o->hwndOwner);
        o->hwndLastActive = h;
    }
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
/* The window that gets a mouse event: the topmost one under the pointer,
   except that one of the reading task's windows answering WM_NCHITTEST
   with HTTRANSPARENT passes it to the window below (3.1 SDK, WM_NCHITTEST;
   observed: a group box in front of the controls it frames - Control
   Panel's dialogs - takes no clicks on 3.1). w16 used to give the event to
   the transparent window, which dropped it: every control inside a group
   box was dead to the mouse. */
HWND window_below(HWND above, POINT pt);

static HWND mouse_target(HQUEUE hq, POINT pt, int *hit)
{
    HWND h;
    if (hwndCapture && valid_wnd(hwndCapture)) {
        *hit = HTCLIENT;
        return hwndCapture;
    }
    h = WindowFromPoint(pt);
    /* each step goes to a later sibling or out to a parent: it ends */
    while (h && h != hwndDesktop) {
        PWND w = valid_wnd(h);
        if (!w || w->hq != hq ||
            (int)send_message(h, WM_NCHITTEST, 0, MAKELONG(pt.x, pt.y)) != HTTRANSPARENT)
            break;
        h = window_below(h, pt);
    }
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
            target = mouse_target(hq, pt, &hit);
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
                    m->wParam = e.wParam;   /* the keys then (push_event) */
                    m->lParam = MAKELONG(c.x, c.y);
                }
                /* UNVERIFIED for the mouse (US30 saw it for keys) */
                mouse_hook(HC_NOREMOVE, m->message, pt, target, hit);
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
            journal_record(&e);
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
            /* WH_MOUSE: before the press activates anything (US30
               mouse on 3.1: M0 201 before WM_LBUTTONDOWN, nonzero throws
               the message away) */
            if (mouse_hook(HC_ACTION, hit != HTCLIENT ? e.msg + WM_NCMOUSEMOVE - WM_MOUSEMOVE : e.msg,
                           pt, target, hit) || !valid_wnd(target))
                continue;
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
            /* not while the mouse is captured (3.1 SDK, WM_SETCURSOR; 3.1:
               the arrow stayed while Notepad's scroll-bar thumb was
               dragged, where the capturing frame's class cursor, an
               I-beam, came up on w16) */
            if (valid_wnd(target) && !menu_mode && !hwndCapture) {
                send_message(target, WM_SETCURSOR, target, MAKELONG(hit, e.msg));
                hcurSetByMsg = hcurCurrent;
            }
            m->hwnd = target;
            m->time = e.time;
            m->pt = pt;
            m->wParam = e.wParam;       /* the keys then (push_event) */
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
                /* WH_KEYBOARD at every look (US30 kbd on 3.1: K3 for each
                   PM_NOREMOVE PeekMessage) */
                kbd_hook(HC_NOREMOVE, e.wParam, e.lParam);
                return TRUE;
            }
            {
                BOOL took = consume(at, &e);
                at = (WORD)-1;          /* from the head again either way */
                if (!took)
                    continue;
            }
            journal_record(&e);
            /* WH_KEYBOARD as the key is taken; nonzero: thrown away (US30
               kbd on 3.1) */
            if (kbd_hook(HC_ACTION, e.wParam, e.lParam))
                continue;
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
            /* Ctrl+Esc: the active window gets WM_SYSCOMMAND SC_TASKLIST,
               whatever has the focus (3.1: from Character Map, a dialog,
               and from Notepad's text the Task List came up - probe TM1;
               TM2's window got the SC_TASKLIST; the key itself is not
               delivered here: UNVERIFIED whether 3.1 delivers it too) */
            if (e.msg == WM_KEYDOWN && (BYTE)e.wParam == VK_ESCAPE &&
                (qkeystate[VK_CONTROL] & 0x80) && !(qkeystate[VK_MENU] & 0x80)) {
                if (hwndActive && valid_wnd(hwndActive))
                    send_message(top_level(hwndActive), WM_SYSCOMMAND, SC_TASKLIST, 0);
                else
                    start_taskman(0, 0);
                continue;
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
                        start_taskman(0, 0);
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
    if (GP_BAD(m))                      /* a bad MSG: FALSE (3.1, US27) */
        return FALSE;
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
