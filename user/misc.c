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
    /* then, as 3.1's USER does (US1 trace of ExitWindows, Microsoft's
       USER between forwarders; Pietrek ch. 1): the cursor off
       (DISPLAY.SetCursor(NULL)), MOUSE.Disable, KEYBOARD.Disable,
       SYSTEM.DisableSystemTimers, and GDI's Death on the screen's DC,
       which puts the display back in text mode. w16's USER skipped these
       and the graphics screen stayed up at the DOS prompt */
    D_SETCURSOR(0);
    MS_DISABLE();
    KB_DISABLE();
    S_DISABLESYSTEMTIMERS();
    G_DEATH(hdcScreen);
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

/* WinHelp (probe US7 with tests/apps/thelp.c as WINHELP.EXE on 3.1, and
   the requests of Program Manager and Notepad): the help window is the
   window of class MS_WINHELP; when there is none, WINHELP.EXE is started
   with "-x" - except for HELP_QUIT, which then sends nothing. Each request
   is the registered message "WM_WINHELP" sent to it, wParam the caller's
   window, lParam's low word a shared global block:
     WORD cbData, WORD command, DWORD topic (HELP_CONTEXT's number, else
     0), DWORD reserved (0), WORD offset of the help file's name (16),
     WORD offset of the data (0 if none), the file name, the data
   (a key: the string; HELP_MULTIKEY, HELP_SETWINPOS: the structure, its
   first word its size). UNVERIFIED: the return values (TRUE here), the
   topic word for key commands (3.1 left garbage there). Notepad shows
   "Not enough memory" when HELP_QUIT on exit returns FALSE. */
int WINAPI WinHelp(HWND h, LPCSTR file, UINT cmd, DWORD data)
{
    UINT msg = RegisterWindowMessage("WM_WINHELP");
    HWND hw = FindWindow("MS_WINHELP", NULL);
    WORD nf, nd = 0, cb;
    HGLOBAL g;
    BYTE __far *p;
    if (!hw) {
        if (cmd == HELP_QUIT)
            return TRUE;
        WinExec("winhelp.exe -x", SW_SHOW);
        hw = FindWindow("MS_WINHELP", NULL);
        if (!hw)
            return FALSE;
    }
    nf = file ? lstr_len(file) + 1 : 0;
    switch (cmd) {
    case HELP_KEY:
    case HELP_PARTIALKEY:
    case HELP_COMMAND:
        nd = data ? lstr_len((LPCSTR)data) + 1 : 0;
        break;
    case HELP_MULTIKEY:
    case HELP_SETWINPOS:
        nd = data ? *(const WORD __far *)data : 0;
        break;
    }
    cb = 16 + nf + nd;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, cb);
    if (!g)
        return FALSE;
    p = (BYTE __far *)GlobalLock(g);
    *(WORD __far *)(p + 0) = cb;
    *(WORD __far *)(p + 2) = cmd;
    if (cmd == HELP_CONTEXT || cmd == HELP_SETCONTENTS || cmd == HELP_CONTEXTPOPUP)
        *(DWORD __far *)(p + 4) = data;
    *(WORD __far *)(p + 12) = nf ? 16 : 0;
    *(WORD __far *)(p + 14) = nd ? 16 + nf : 0;
    if (nf)
        fmemcpy(p + 16, file, nf);
    if (nd)
        fmemcpy(p + 16 + nf, (const void __far *)data, nd);
    GlobalUnlock(g);
    SendMessage(hw, msg, (WPARAM)h, MAKELONG(g, 0));
    GlobalFree(g);
    return TRUE;
}


/* SetSysModalWindow/GetSysModalWindow (3.1 SDK): the window recorded.
   UNVERIFIED: 3.1 also keeps input and activation to it (Pietrek ch. 7:
   hQSysModal comes first when USER wakes a queue for input); w16 only
   records it and activates it; mouse input to other top-level windows
   is dropped (a press beeps) while it lasts (input.c). */
static HWND hwndSysModal;

HWND WINAPI SetSysModalWindow(HWND h)
{
    HWND old = hwndSysModal;
    hwndSysModal = valid_wnd(h) ? h : 0;
    if (hwndSysModal)
        SetActiveWindow(top_level(hwndSysModal));
    return old;
}

/* the system-modal window, if any, and whether input to top-level window
   h is kept from it (h is not it and not owned by it) */
BOOL sysmodal_blocks(HWND h)
{
    HWND o;
    if (hwndSysModal && !valid_wnd(hwndSysModal))
        hwndSysModal = 0;
    if (!hwndSysModal || !h)
        return FALSE;
    for (o = h; o && valid_wnd(o); o = PW(o)->hwndOwner)
        if (o == hwndSysModal)
            return FALSE;
    return TRUE;
}

HWND WINAPI GetSysModalWindow(void)
{
    if (hwndSysModal && !valid_wnd(hwndSysModal))
        hwndSysModal = 0;
    return hwndSysModal;
}

/* ---- the network functions (USER.499-532) without a network driver:
   WNetGetCaps 0 for every index, every other one WN_NOT_SUPPORTED (1)
   with its buffers untouched (US8 on Windows 3.1: caps 0-12 and FFFFh,
   WNetGetConnection of A:, C:, D:, Z:, WNetGetUser, WNetGetError,
   WNetGetDirectoryType, Add/Cancel/RestoreConnection, WNetGetErrorText).
   UNVERIFIED for the print-queue and dialog ones. They were stubs
   returning 0, WN_SUCCESS: File Manager took every drive A-Z for a
   connected network drive. WNetEnable/WNetDisable (521, 522; arguments
   unknown) stay stubs. WNetGetUser(szUser, lpSize) pops 8 bytes (3.1 DDK;
   Wine's 12 left US8's stack four bytes off on w16, not on 3.1). ---- */
#ifndef WN_NOT_SUPPORTED
#define WN_NOT_SUPPORTED 1
#endif

WORD WINAPI U_WNETERRORTEXT(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETOPENJOB(DWORD d0, DWORD d1, DWORD d2, WORD w)
{
    (void)d0; (void)d1; (void)d2; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETCLOSEJOB(DWORD d0, DWORD d1, WORD w)
{
    (void)d0; (void)d1; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETABORTJOB(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETHOLDJOB(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETRELEASEJOB(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETCANCELJOB(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETSETJOBCOPIES(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETWATCHQUEUE(DWORD d0, DWORD d1, DWORD d2)
{
    (void)d0; (void)d1; (void)d2;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETUNWATCHQUEUE(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETLOCKQUEUEDATA(DWORD d0, DWORD d1, DWORD d2)
{
    (void)d0; (void)d1; (void)d2;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETUNLOCKQUEUEDATA(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETCONNECTION(DWORD d0, DWORD d1, DWORD d2)
{
    (void)d0; (void)d1; (void)d2;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETCAPS(WORD w)
{
    (void)w;
    return 0;
}

WORD WINAPI U_WNETDEVICEMODE(WORD w)
{
    (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETBROWSEDIALOG(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETUSER(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETADDCONNECTION(DWORD d0, DWORD d1, DWORD d2)
{
    (void)d0; (void)d1; (void)d2;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETCANCELCONNECTION(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETERROR(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETERRORTEXT(DWORD d0, DWORD d1, WORD w)
{
    (void)d0; (void)d1; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETRESTORECONNECTION(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETWRITEJOB(DWORD d0, DWORD d1, WORD w)
{
    (void)d0; (void)d1; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETCONNECTDIALOG(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETDISCONNECTDIALOG(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETCONNECTIONDIALOG(DWORD d0)
{
    (void)d0;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETVIEWQUEUEDIALOG(DWORD d0, WORD w)
{
    (void)d0; (void)w;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETPROPERTYDIALOG(DWORD d0, DWORD d1, DWORD d2)
{
    (void)d0; (void)d1; (void)d2;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETDIRECTORYTYPE(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETDIRECTORYNOTIFY(DWORD d0, DWORD d1)
{
    (void)d0; (void)d1;
    return WN_NOT_SUPPORTED;
}

WORD WINAPI U_WNETGETPROPERTYTEXT(DWORD d0, DWORD d1, DWORD d2, DWORD d3)
{
    (void)d0; (void)d1; (void)d2; (void)d3;
    return WN_NOT_SUPPORTED;
}
