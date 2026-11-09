/*
 * misc.c - USER's links with KERNEL (the signal procedure, IsUserIdle),
 * the stubs' recorder, and small functions.
 *
 * KERNEL finds USER.314 (SignalProc) with GetProcAddress at boot and calls
 * it when a library is loaded (code 40h) or freed (80h); InitApp also makes
 * it the task's signal procedure (SetTaskSignalProc), which KERNEL calls
 * with 20h when the task ends, or 0666h when it is killed after a fault
 * (Pietrek, Windows Internals, ch. 3; US1 saw Windows 3.1's KERNEL call
 * USER.314 with 40h during the boot). USER then destroys the task's
 * windows, timers and queue.
 */
#include "user.h"

void free_window_dcs(HWND h);

/* ---- stubs (user/stubs.asm, tools/mkuserexp.py): record which
   unimplemented ordinals are called ---- */
static BYTE unimpl_seen[64];            /* bit per ordinal 0-511 */
static WORD unimpl_count;

void __far __pascal __loadds UNIMPL(WORD ord)
{
    unimpl_count++;
    if (ord < 512 && !(unimpl_seen[ord >> 3] & (1 << (ord & 7)))) {
        unimpl_seen[ord >> 3] |= 1 << (ord & 7);
        ulogx("unimplemented USER ordinal ", ord);
    }
}

/* ---- task exit ---- */
static void app_exit(HTASK task)
{
    HQUEUE hq = K_GETTASKQUEUE(task);
    HWND h, n;
    if (!hq)
        return;
    /* its top-level windows (and their children) */
    for (h = PW(hwndDesktop)->hwndChild; h; h = n) {
        n = PW(h)->hwndNext;
        if (PW(h)->hq == hq) {
            destroy_window(h);
            n = PW(hwndDesktop)->hwndChild;
        }
    }
    kill_queue_timers(hq);
    if (hqInput == hq)
        hqInput = 0;
    K_SETTASKQUEUE(task, 0);
    destroy_queue(hq);
    G_GDITASKTERMINATION(task);
}

LONG WINAPI U_SIGNALPROC(HANDLE h, WORD code, WORD exitfn, HINSTANCE hinst, HQUEUE hq)
{
    (void)exitfn, (void)hinst, (void)hq;
    ulogx("SignalProc code ", code);
    ulogx("  handle ", (WORD)h);
    if (code == 0x20 || code == 0x666)
        app_exit((HTASK)h);
    return 0;
}

/* KERNEL's idle loop asks this (US1: KERNEL calls USER.333 while no task
   runs) */
BOOL WINAPI U_ISUSERIDLE(void)
{
    return !input_pending;
}

void WINAPI MessageBeep(UINT type)
{
    (void)type;
}

/* ---- the caret (minimal: one caret, drawn by inverting) ---- */
static HWND hwndCaret;
static int caret_x, caret_y, caret_cx, caret_cy, caret_hide, caret_on;
static UINT caret_blink = 530;

static void caret_draw(void)
{
    HDC hdc;
    if (!hwndCaret || !valid_wnd(hwndCaret))
        return;
    hdc = get_dc(hwndCaret, FALSE, 0, 0);
    PatBlt(hdc, caret_x, caret_y, caret_cx, caret_cy, DSTINVERT);
    release_dc(hwndCaret, hdc);
    caret_on ^= 1;
}

void WINAPI CreateCaret(HWND h, HBITMAP bm, int cx, int cy)
{
    DestroyCaret();
    hwndCaret = h;
    caret_cx = cx ? cx : sysmet[SM_CXBORDER];
    caret_cy = cy ? cy : sysmet[SM_CYBORDER];
    caret_hide = 1;
    caret_on = 0;
    (void)bm;
}

void WINAPI DestroyCaret(void)
{
    if (hwndCaret && caret_on)
        caret_draw();
    hwndCaret = 0;
}

void WINAPI HideCaret(HWND h)
{
    if (!hwndCaret || (h && h != hwndCaret))
        return;
    if (!caret_hide++ && caret_on)
        caret_draw();
}

void WINAPI ShowCaret(HWND h)
{
    if (!hwndCaret || (h && h != hwndCaret) || !caret_hide)
        return;
    if (!--caret_hide && !caret_on)
        caret_draw();
}

void WINAPI SetCaretPos(int x, int y)
{
    BOOL was = caret_on;
    if (was)
        caret_draw();
    caret_x = x;
    caret_y = y;
    if (was)
        caret_draw();
}

void WINAPI GetCaretPos(POINT __far *p)
{
    p->x = caret_x;
    p->y = caret_y;
}

void WINAPI SetCaretBlinkTime(UINT t)
{
    caret_blink = t;
}

UINT WINAPI GetCaretBlinkTime(void)
{
    return caret_blink;
}

/* ---- odds and ends ---- */
BOOL WINAPI ExitWindows(DWORD rc, UINT reserved)
{
    HWND h;
    (void)reserved;
    ulogx("ExitWindows ", LOWORD(rc));
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext)
        if (!send_message(h, WM_QUERYENDSESSION, 0, 0))
            return FALSE;
    /* BOOTLOG.TXT gets these from 3.1's USER here (KERNEL calls
       ExitWindows when the shell ends: US1/userboot); UNVERIFIED what 3.1
       asks the TSRs */
    diag("INIT=TSRQuery\r\n");
    diag("INITDONE=TSRQuery\r\n");
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext)
        send_message(h, WM_ENDSESSION, TRUE, 0);
    /* KERNEL ends Windows (ExitKernel, KERNEL.2) with the code */
    {
        extern void __far __pascal K_EXITKERNEL(WORD);
        K_EXITKERNEL(LOWORD(rc));
    }
    return FALSE;
}

BOOL WINAPI SwapMouseButton(BOOL swap)
{
    BOOL old = sysmet[SM_SWAPBUTTON];
    sysmet[SM_SWAPBUTTON] = swap != 0;
    return old;
}

int WINAPI GetKeyboardType(int which)
{
    /* US2 on the reference: type 4 (enhanced 101/102), subtype 0, 12
       function keys; UNVERIFIED that this does not come from KEYBOARD.DRV */
    return which == 0 ? 4 : which == 1 ? 0 : which == 2 ? 12 : 0;
}

BOOL WINAPI SystemParametersInfo(UINT what, UINT ui, void __far *pv, UINT ini)
{
    WORD __far *w = (WORD __far *)pv;
    (void)ui, (void)ini;
    switch (what) {
    case SPI_GETBEEP: *w = 1; return TRUE;
    case SPI_GETMOUSE: w[0] = 2; w[1] = 0; w[2] = 1; return TRUE;     /* US2 */
    case SPI_GETBORDER: *w = sysmet[SM_CXFRAME] - 1; return TRUE;
    case SPI_GETKEYBOARDSPEED: *w = GetProfileInt("windows", "KeyboardSpeed", 31); return TRUE;
    case SPI_GETSCREENSAVETIMEOUT: *w = GetProfileInt("windows", "ScreenSaveTimeOut", 120); return TRUE;
    case SPI_GETSCREENSAVEACTIVE: *w = GetProfileInt("windows", "ScreenSaveActive", 0); return TRUE;
    case SPI_GETGRIDGRANULARITY: *w = GetProfileInt("desktop", "GridGranularity", 0); return TRUE;
    case SPI_GETKEYBOARDDELAY: *w = GetProfileInt("windows", "KeyboardDelay", 2); return TRUE;
    case SPI_ICONHORIZONTALSPACING: if (pv) *w = sysmet[SM_CXICONSPACING]; return TRUE;
    case SPI_ICONVERTICALSPACING: if (pv) *w = sysmet[SM_CYICONSPACING]; return TRUE;
    case SPI_GETICONTITLEWRAP: *w = GetProfileInt("desktop", "IconTitleWrap", 1); return TRUE;
    case SPI_GETMENUDROPALIGNMENT: *w = 0; return TRUE;
    case SPI_GETFASTTASKSWITCH: *w = 1; return TRUE;
    case SPI_GETICONTITLELOGFONT: {
        /* US2 found nothing written with uiParam 0 (UNVERIFIED content) */
        if (ui < sizeof(LOGFONT))
            return TRUE;
        {
            LOGFONT __far *lf = (LOGFONT __far *)pv;
            int i;
            for (i = 0; i < (int)sizeof(LOGFONT); i++)
                ((BYTE __far *)lf)[i] = 0;
            lf->lfHeight = -8;
            lf->lfWeight = FW_NORMAL;
            lstr_cpyn(lf->lfFaceName, "MS Sans Serif", LF_FACESIZE);
        }
        return TRUE;
    }
    }
    return FALSE;
}

int WINAPI WinHelp(HWND h, LPCSTR file, UINT cmd, DWORD data)
{
    (void)h, (void)file, (void)cmd, (void)data;
    return 0;                   /* UNVERIFIED: WINHELP.EXE is not started yet */
}

/* ---- menus: the minimum until menu.c (no menu bar drawn) ---- */
int menu_bar_height(PWND w, int width)
{
    (void)w, (void)width;
    return 0;
}

void menu_draw_bar(HDC hdc, PWND w, int x, int y, int width)
{
    (void)hdc, (void)w, (void)x, (void)y, (void)width;
}
