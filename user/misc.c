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
static BYTE __far unimpl_seen[64];            /* bit per ordinal 0-511 */
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
static void app_exit(HTASK task, BOOL fault)
{
    HQUEUE hq = K_GETTASKQUEUE(task);
    HMODULE m;
    HWND h, n;
    if (!hq)
        return;
    /* the module (InitApp's instance): it goes when this is its last
       instance */
    m = K_GETEXEPTR(q_of(hq)->hinst);
    /* nobody waits for its replies, and it waits for none (Pietrek ch. 3,
       AppExit: FlushSentMessages; again in destroy_queue for what comes
       while its windows are destroyed) */
    smsg_queue_gone(hq, 0);
    /* its hooks and the hooks for it alone (AppExit: UnhookHooks) */
    hooks_queue_gone(hq, task);
    /* its top-level windows (and their children) */
    /* from the top again after each (a WM_DESTROY can change the list),
       passing over those already being destroyed: a task that ended
       inside a WM_DESTROY left its window linked, and the loop came back
       to it for ever (USER audit, item 4) */
    for (h = PW(hwndDesktop)->hwndChild; h; h = n) {
        PWND w = PW(h);
        n = w->hwndNext;
        if (w->hq == hq && !(w->flags & WF_DESTROYING)) {
            destroy_window(h);
            n = PW(hwndDesktop)->hwndChild;
        }
    }
    /* and those: out of the tree, freed */
    for (h = PW(hwndDesktop)->hwndChild; h; h = n) {
        n = PW(h)->hwndNext;
        if (PW(h)->hq == hq)
            destroy_abandoned(h);
    }
    /* the last instance: the module's hooks and classes (ModuleUnload;
       the hooks after a fault too) */
    if (m && (fault || GetModuleUsage(m) <= 1))
        hooks_module_gone(m);
    if (m && GetModuleUsage(m) <= 1) {
        purge_classes(m);
        icons_module_gone(m);
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
    if (code == 0x20 || code == 0x666) {
        drivers_task_exit(code == 0x666);
        app_exit((HTASK)h, code == 0x666);
    }
    /* a DLL freed (after its WEP; Pietrek ch. 3, DelModule): its hooks and
       classes go (US29 cl, hk) */
    if (code == 0x80) {
        HMODULE m = K_GETEXEPTR(h);
        if (!m)
            m = (HMODULE)h;
        hooks_module_gone(m);
        purge_classes(m);
        icons_module_gone(m);
    }
    if (code == 0x20)
        user_heaps_shrink();    /* not after a fault (Pietrek ch. 3) */
    return 0;
}

/* KERNEL's idle loop asks this (US1: KERNEL calls USER.333 while no task
   runs) */
BOOL WINAPI U_ISUSERIDLE(void)
{
    return !input_pending;
}

/* ---- MessageBeep: the sound driver's DoBeep (SOUND.17) with the beep's
   type, as Windows 3.1's USER does (its NE imports name SOUND.17; SN1
   with SNDREC in front of the driver saw USER call DoBeep(type) once for
   every MessageBeep, whatever the type, the device open or not: probe
   tests/soundprobe.sh rec). WIN.INI [windows] Beep turns it off (Pietrek,
   Windows Internals ch. 2: USER reads Beep at its start-up, and
   MessageBeep looks at the flag; "yes" by default, UNVERIFIED for a
   missing key); SystemParametersInfo SPI_GETBEEP/SPI_SETBEEP read and set
   the flag. DoBeep is found by ordinal at the first beep: the driver
   module is loaded before USER (Pietrek ch. 1). ---- */
typedef void (FAR PASCAL *DOBEEPFN)(UINT);
static int beep_on = -1;
static DOBEEPFN do_beep;
static int do_beep_found;

static int beep_flag(void)
{
    if (beep_on < 0) {
        char b[8];
        GetProfileString("windows", "Beep", "yes", b, sizeof b);
        beep_on = lstrcmpi(b, "yes") == 0;
    }
    return beep_on;
}

void WINAPI MessageBeep(UINT type)
{
    if (!beep_flag())
        return;
    if (!do_beep_found) {
        HMODULE m = GetModuleHandle("SOUND");
        do_beep = m ? (DOBEEPFN)GetProcAddress(m, MAKEINTRESOURCE(17)) : 0;
        do_beep_found = 1;
    }
    if (do_beep)
        do_beep(type);
}

/* ---- the caret: one, drawn by inverting, blinking ----
   US13 on Windows 3.1: the caret blinks only while its task reads
   messages (a second without them, it stayed as it was) and no message
   reaches the window procedure: a system timer of USER's (SetSystemTimer,
   WM_SYSTIMER whose lParam is USER's own procedure, run by
   DispatchMessage; 3.0 exported it as CaretBlinkProc, USER.311). The
   period is GetCaretBlinkTime rounded up to the 55 ms tick (530: 550 ms;
   200: 220 ms). The phase goes on while the caret is hidden: ShowCaret
   draws nothing until the next "on" (3.1: 194 ms after ShowCaret, on the
   old schedule); SetCaretPos keeps the phase; CreateCaret starts on, and
   ShowCaret then draws at once. WIN.INI [windows] CursorBlinkRate, 530
   by default (3.1 SDK). Nothing of this runs at interrupt time. */
#define CARET_TIMER 0xFFFE      /* UNVERIFIED: 3.1's id for it */
static HWND hwndCaret;
static int caret_x, caret_y, caret_cx, caret_cy, caret_hide, caret_on, caret_phase;
static UINT caret_blink;
static BOOL caret_gray;         /* CreateCaret's (HBITMAP)1: a gray caret */

/* invert the caret's rectangle: black (DSTINVERT), or a gray caret
   through the 50% pattern aligned with the screen (observed: a focused
   scroll bar control's thumb, the pixels where x + y is odd change) */
static void caret_blt(HDC hdc)
{
    if (caret_gray) {
        DWORD org = GetDCOrg(hdc), borg;
        HBRUSH ob;
        UnrealizeObject(hbrGray);
        borg = SetBrushOrg(hdc, (LOWORD(org) + HIWORD(org)) & 1, 0);
        ob = SelectObject(hdc, hbrGray);
        PatBlt(hdc, caret_x, caret_y, caret_cx, caret_cy, PATINVERT);
        SelectObject(hdc, ob);
        SetBrushOrg(hdc, LOWORD(borg), HIWORD(borg));     /* the DC's own back */
    } else
        PatBlt(hdc, caret_x, caret_y, caret_cx, caret_cy, DSTINVERT);
}

static void caret_draw(void)
{
    HDC hdc;
    if (!hwndCaret || !valid_wnd(hwndCaret))
        return;
    hdc = get_dc(hwndCaret, FALSE, 0, DCX_OWNCLIENT);
    caret_blt(hdc);
    release_dc(hwndCaret, hdc);
    caret_on ^= 1;
}

/* shown when in the "on" phase and not hidden */
static void caret_update(void)
{
    int want = caret_phase && !caret_hide;
    if (want != caret_on)
        caret_draw();
}

static UINT blink_time(void)
{
    if (!caret_blink)
        caret_blink = GetProfileInt("windows", "CursorBlinkRate", 530);
    return caret_blink;
}

void __far __pascal __loadds caret_blink_proc(HWND h, UINT msg, UINT id, DWORD t)
{
    (void)msg, (void)id, (void)t;
    if (h && h == hwndCaret) {
        caret_phase ^= 1;
        caret_update();
    }
}

static void caret_timer(BOOL on)
{
    UINT WINAPI U_BEAR11(HWND, UINT, UINT, TIMERPROC);
    BOOL WINAPI U_KILLSYSTEMTIMER(HWND, UINT);
    if (!hwndCaret)
        return;
    if (on)
        U_BEAR11(hwndCaret, CARET_TIMER, blink_time(), (TIMERPROC)caret_blink_proc);
    else
        U_KILLSYSTEMTIMER(hwndCaret, CARET_TIMER);
}

/* for savebits.c: where the caret shows on screen (FALSE: it does not) */
BOOL caret_screen_rect(RECT *r)
{
    PWND w;
    if (!hwndCaret || !caret_on || !(w = valid_wnd(hwndCaret)))
        return FALSE;
    SetRect(r, caret_x, caret_y, caret_x + caret_cx, caret_y + caret_cy);
    OffsetRect(r, w->rcClient.left, w->rcClient.top);
    return TRUE;
}

void caret_toggle(void)
{
    caret_draw();
}

/* the caret inverted again within screen rectangle rc, its state kept */
void caret_invert_in(const RECT *rc)
{
    PWND w;
    HDC hdc;
    int sv;
    if (!hwndCaret || !(w = valid_wnd(hwndCaret)))
        return;
    hdc = get_dc(hwndCaret, FALSE, 0, DCX_OWNCLIENT);
    sv = SaveDC(hdc);
    IntersectClipRect(hdc, rc->left - w->rcClient.left, rc->top - w->rcClient.top,
                      rc->right - w->rcClient.left, rc->bottom - w->rcClient.top);
    caret_blt(hdc);
    RestoreDC(hdc, sv);
    release_dc(hwndCaret, hdc);
}

void WINAPI CreateCaret(HWND h, HBITMAP bm, int cx, int cy)
{
    DestroyCaret();
    hwndCaret = h;
    caret_cx = cx ? cx : sysmet[SM_CXBORDER];
    caret_cy = cy ? cy : sysmet[SM_CYBORDER];
    caret_hide = 1;
    caret_on = 0;
    caret_phase = 1;
    caret_gray = bm == (HBITMAP)1;
    caret_timer(TRUE);
}

void WINAPI DestroyCaret(void)
{
    if (hwndCaret && caret_on)
        caret_draw();
    caret_timer(FALSE);
    hwndCaret = 0;
}

void WINAPI HideCaret(HWND h)
{
    if (!hwndCaret || (h && h != hwndCaret))
        return;
    caret_hide++;
    caret_update();
}

void WINAPI ShowCaret(HWND h)
{
    if (!hwndCaret || (h && h != hwndCaret) || !caret_hide)
        return;
    caret_hide--;
    caret_update();
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

/* the new rate from now on (UNVERIFIED: 3.1 restarted the count, the
   first change 313 ms after the call, then every 220 ms) */
void WINAPI SetCaretBlinkTime(UINT t)
{
    caret_blink = t ? t : 1;
    caret_timer(TRUE);
}

UINT WINAPI GetCaretBlinkTime(void)
{
    return blink_time();
}

/* ---- odds and ends ---- */
BOOL WINAPI ExitWindows(DWORD rc, UINT reserved)
{
    HWND h;
    HQUEUE me = cur_hq();
    (void)reserved;
    ulogx("ExitWindows ", LOWORD(rc));
    /* the top-level windows of the other tasks only (Pietrek ch. 1,
       QueryQuitEnum: "Don't bother with windows in the current
       task/queue"; observed with TK77's own window, boot-test case
       exitexec). w16 asked the caller's windows too: Win32s 1.30c's
       Setup, calling ExitWindowsExec, answered its own WM_QUERYENDSESSION
       with "Please exit the Program normally" and Windows did not end */
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext)
        if (PW(h)->hq != me && !send_message(h, WM_QUERYENDSESSION, 0, 0))
            return FALSE;
    /* BOOTLOG.TXT gets these from 3.1's USER here (KERNEL calls
       ExitWindows when the shell ends: US1/userboot); UNVERIFIED what 3.1
       asks the TSRs */
    diag("INIT=TSRQuery\r\n");
    diag("INITDONE=TSRQuery\r\n");
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext)
        if (PW(h)->hq != me)
            send_message(h, WM_ENDSESSION, TRUE, 0);
    drivers_exit_session();             /* DRV_EXITSESSION ... (driver.c) */
    /* then, as 3.1's USER does (US1 trace of ExitWindows, Microsoft's
       USER between forwarders; Pietrek ch. 1): the cursor off
       (DISPLAY.SetCursor(NULL)), MOUSE.Disable, KEYBOARD.Disable,
       SYSTEM.DisableSystemTimers, and GDI's Death on the screen's DC,
       which puts the display back in text mode. w16's USER skipped these
       and the graphics screen stayed up at the DOS prompt */
    input_disable();
    G_DEATH(hdcScreen);
    /* KERNEL ends Windows (ExitKernel, KERNEL.2) with the code */
    {
        extern void __far __pascal K_EXITKERNEL(WORD);
        K_EXITKERNEL(LOWORD(rc));
    }
    return FALSE;
}

/* ExitWindowsExec (USER.246): end Windows, run a DOS program, start
   Windows again. Pietrek (Windows Internals ch. 1): it asks WIN.COM for
   its buffer (INT 2Fh AX=4B20h, DX:CX a real-mode address), copies the
   program there and ends Windows with 44h, after which WIN.COM runs the
   program and restarts Windows. Observed on Windows 3.1 (TK77,
   tests/apps/tk77.c, boot-test case exitexec): Microsoft's USER ends with
   ExitKernel(44h); its buffer then holds the program as given (a path or
   a bare name), NUL-terminated, the rest of the first 80h bytes zero, and
   at 80h the parameters as a DOS command tail (length, text, CR; NULL
   parameters as 00 00 0Dh; the rest zero), which WIN.COM gives the
   program. w16 does the same. FALSE when no WIN.COM answers or a window refuses to end the
   session (ExitWindows). w16 had no ExitWindowsExec: Win32s 1.30c's Setup
   said "Unable to Restart Windows" at its end. */
static WORD exec_seg, exec_off;         /* INT 2Fh 4B20h: DX:CX */
static void win_exec_buf(void);
#pragma aux win_exec_buf = \
    "mov ax, 4B20h" "xor cx, cx" "xor dx, dx" "int 2Fh" \
    "test ax, ax" "jz l1" "xor dx, dx" "l1:" \
    "mov exec_seg, dx" "mov exec_off, cx" \
    modify [ax bx cx dx si di es];
static WORD seg_to_sel(WORD seg);       /* DPMI 0002h */
#pragma aux seg_to_sel = \
    "mov ax, 0002h" "int 31h" "jnc l1" "xor ax, ax" "l1:" \
    parm [bx] value [ax] modify [ax];

BOOL WINAPI U_EXITWINDOWSEXEC(LPCSTR exe, LPCSTR params)
{
    WORD sel, n;
    char __far *b;

    win_exec_buf();
    ulogx("ExitWindowsExec: WIN.COM's buffer at segment ", exec_seg);
    if (exe) {
        ulog(exe);
        ulog("\r\n");
    }
    if (params) {
        ulog(params);
        ulog("\r\n");
    }
    if (!exec_seg || !exe || !*exe || !(sel = seg_to_sel(exec_seg)))
        return FALSE;
    b = (char __far *)MAKELP(sel, exec_off);
    for (n = 0; n < 0x100; n++)         /* zero-filled, as 3.1 leaves it */
        b[n] = 0;
    for (n = 0; *exe && n < 0x7F; n++)
        b[n] = *exe++;
    b = b + 0x80;                       /* the parameters: a DOS tail */
    n = 0;
    if (params)
        for (; params[n] && n < 126; n++)
            b[1 + n] = params[n];
    else
        b[1 + n++] = 0;                 /* NULL: 00 00 0Dh, as 3.1 */
    b[0] = (char)(params ? n : 0);
    b[1 + n] = 0x0D;
    return ExitWindows(0x44, 0);
}

BOOL WINAPI SwapMouseButton(BOOL swap)
{
    BOOL old = sysmet[SM_SWAPBUTTON];
    sysmet[SM_SWAPBUTTON] = swap != 0;
    mouse_swapped = swap != 0;
    return old;
}

int WINAPI GetKeyboardType(int which)
{
    /* US2 on the reference: type 4 (enhanced 101/102), subtype 0, 12
       function keys; UNVERIFIED that this does not come from KEYBOARD.DRV */
    return which == 0 ? 4 : which == 1 ? 0 : which == 2 ? 12 : 0;
}

/* ---- the screen saver (3.1 SDK, Programmer's Reference vol. 1 ch. 14
   and WM_SYSCOMMAND; Pietrek, Windows Internals ch. 2 and 6, as facts):
   USER reads WIN.INI [windows] ScreenSaveActive and ScreenSaveTimeOut
   (seconds) at its start-up; SPI_SET* change them. A system timer every
   10 seconds checks the time since the last key or mouse event: past the
   time-out, with no button held, no system-modal window and an active
   window that is not a DOS program's in 386 Enhanced mode, the active
   window is posted WM_SYSCOMMAND SC_SCREENSAVE and the idle time starts
   again from the next input. DefWindowProc runs SYSTEM.INI [boot]
   SCRNSAVE.EXE with /s for it. Observed on 3.1 (tests/scripts/ss): a
   15-second time-out started the saver 15-17.5 s after the last key;
   SC_SCREENSAVE posted to Notepad started it. ---- */
static int ss_loaded, ss_active;
static UINT ss_timeout;
#define SS_PERIOD 10000         /* Pietrek: "every 10 seconds" */

static void ss_load(void)
{
    if (ss_loaded)
        return;
    ss_loaded = 1;
    ss_active = GetProfileInt("windows", "ScreenSaveActive", 0) != 0;
    ss_timeout = GetProfileInt("windows", "ScreenSaveTimeOut", 120);
}

static void write_int(LPCSTR key, UINT v)
{
    char b[8];
    wsprintf(b, "%u", v);
    WriteProfileString("windows", key, b);
}

/* ---- the keyboard's typematic delay and rate ----
   Observed on 3.1 with KBREC.DRV in front of its KEYBOARD.DRV
   (tests/scripts/cpl/kbspeed*.txt): at start-up USER asks SetSpeed(-1)
   (0: the driver can) and then gives it WIN.INI [windows] KeyboardDelay
   (0..3, default 2) in bits 5-6 and KeyboardSpeed (0..31, default 31) in
   bits 0-4: 005Fh by default, 0034h for 1 and 20. Each
   SPI_SETKEYBOARDDELAY or SPI_SETKEYBOARDSPEED gives it both again, the
   new value with the other's current one. The driver programs the
   keyboard (command F3h, drivers/keyboard). */
static int kb_delay = -1, kb_speed, kb_can;

static void kb_load(void)
{
    if (kb_delay >= 0)
        return;
    kb_delay = GetProfileInt("windows", "KeyboardDelay", 2);
    kb_speed = GetProfileInt("windows", "KeyboardSpeed", 31);
    if (kb_delay < 0) kb_delay = 0;
    if (kb_delay > 3) kb_delay = 3;
    if (kb_speed < 0) kb_speed = 0;
    if (kb_speed > 31) kb_speed = 31;
}

static void kb_apply(void)
{
    if (kb_can)
        KB_SETSPEED(kb_delay << 5 | kb_speed);
}

/* input_enable, after KEYBOARD.Enable */
void keyboard_speed_init(void)
{
    kb_load();
    kb_can = KB_SETSPEED(-1) == 0;
    kb_apply();
}

void __far __pascal __loadds ss_timer_proc(HWND h, UINT msg, UINT id, DWORD time)
{
    HWND act = hwndActive;
    (void)h, (void)msg, (void)id, (void)time;
    if ((keystate[VK_LBUTTON] | keystate[VK_RBUTTON] | keystate[VK_MBUTTON]) & 0x80)
        return;
    ss_load();
    if (!ss_active || !ss_timeout || !time_last_input || GetSysModalWindow())
        return;
    /* on the clock time_last_input comes from, USER's tick count:
       GetTickCount (SYSTEM.DRV's) ran some 30-70 ms behind it, so a key
       in the last moment before the check came out "in the future", the
       unsigned difference huge, and the saver started again right after
       the key that had ended it (boot-test scrnsave@tree*) */
    if (tickcount - time_last_input <= (DWORD)ss_timeout * 1000)
        return;
    if (!act || !valid_wnd(act))
        return;
    if ((GetWinFlags() & WF_ENHANCED) && K_ISWINOLDAPTASK(GetWindowTask(act)))
        return;
    PostMessage(act, WM_SYSCOMMAND, SC_SCREENSAVE, 0);
    time_last_input = 0;
}

/* the first task's InitApp once Windows is up (the shell's): the timer
   is that task's, as on 3.1 (Pietrek ch. 3: InitApp's SetSystemTimer) */
void screensave_init(void)
{
    UINT WINAPI U_BEAR11(HWND, UINT, UINT, TIMERPROC);
    static BOOL done;
    if (done)
        return;
    done = TRUE;
    ss_load();
    U_BEAR11(0, 0, SS_PERIOD, (TIMERPROC)ss_timer_proc);
}

/* SC_SCREENSAVE: the saver SYSTEM.INI names, full screen */
void screensave_run(void)
{
    char path[160];
    int n;
    n = GetPrivateProfileString("boot", "SCRNSAVE.EXE", "", path, sizeof path - 3, "SYSTEM.INI");
    if (!n || !lstrcmpi(path, "(None)"))
        return;
    lstrcpy(path + n, " /s");
    WinExec(path, SW_SHOWNORMAL);
}

BOOL WINAPI SystemParametersInfo(UINT what, UINT ui, void __far *pv, UINT ini)
{
    WORD __far *w;
    LOGFONT none;
    if (GP_BAD(pv))                     /* a bad pvParam: FALSE (3.1, US27) */
        return FALSE;
    /* a NULL one: TRUE, nothing written (3.1, US27's N cases; the probe's
       emulator does not fault on a null selector's memory, so whether 3.1
       writes nowhere or lets it fault is not known: UNVERIFIED) */
    if (!pv)
        pv = &none;
    w = (WORD __far *)pv;
    switch (what) {
    case SPI_GETBEEP: *w = beep_flag(); return TRUE;
    case SPI_SETBEEP:                   /* (UNVERIFIED: 3.1's writes "yes"/"no") */
        beep_on = ui != 0;
        if (ini & SPIF_UPDATEINIFILE)
            WriteProfileString("windows", "Beep", beep_on ? "yes" : "no");
        return TRUE;
    case SPI_GETMOUSE:      /* WIN.INI's or the last SPI_SETMOUSE (US2: 2, 0, 1) */
        mouse_accel((int __far *)w, NULL);
        return TRUE;
    case SPI_SETMOUSE:      /* thresholds 1 and 2, speed (3.1 SDK) */
        mouse_accel(NULL, (const int __far *)w);
        if (ini & SPIF_UPDATEINIFILE) {
            char b[8];
            wsprintf(b, "%d", w[0]);
            WriteProfileString("windows", "MouseThreshold1", b);
            wsprintf(b, "%d", w[1]);
            WriteProfileString("windows", "MouseThreshold2", b);
            wsprintf(b, "%d", w[2]);
            WriteProfileString("windows", "MouseSpeed", b);
        }
        return TRUE;
    case SPI_GETBORDER: *w = sysmet[SM_CXFRAME] - 1; return TRUE;
    case SPI_GETKEYBOARDSPEED: kb_load(); *w = kb_speed; return TRUE;
    case SPI_SETKEYBOARDSPEED:          /* 0..31 (3.1 SDK) */
        kb_load();
        kb_speed = ui > 31 ? 31 : ui;
        kb_apply();
        if (ini & SPIF_UPDATEINIFILE)
            write_int("KeyboardSpeed", kb_speed);
        return TRUE;
    case SPI_SETKEYBOARDDELAY:          /* 0..3 (3.1 SDK) */
        kb_load();
        kb_delay = ui > 3 ? 3 : ui;
        kb_apply();
        if (ini & SPIF_UPDATEINIFILE)
            write_int("KeyboardDelay", kb_delay);
        return TRUE;
    case SPI_GETSCREENSAVETIMEOUT: ss_load(); *w = ss_timeout; return TRUE;
    case SPI_GETSCREENSAVEACTIVE: ss_load(); *w = ss_active; return TRUE;
    case SPI_SETSCREENSAVETIMEOUT:      /* seconds (3.1 SDK) */
        ss_load();
        ss_timeout = ui;
        if (ini & SPIF_UPDATEINIFILE)
            write_int("ScreenSaveTimeOut", ui);
        return TRUE;
    case SPI_SETSCREENSAVEACTIVE:
        ss_load();
        ss_active = ui != 0;
        if (ini & SPIF_UPDATEINIFILE)
            write_int("ScreenSaveActive", ss_active);
        return TRUE;
    case SPI_GETGRIDGRANULARITY: *w = GetProfileInt("desktop", "GridGranularity", 0); return TRUE;
    case SPI_GETKEYBOARDDELAY: kb_load(); *w = kb_delay; return TRUE;
    case SPI_ICONHORIZONTALSPACING: *w = sysmet[SM_CXICONSPACING]; return TRUE;
    case SPI_ICONVERTICALSPACING: *w = sysmet[SM_CYICONSPACING]; return TRUE;
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
   connected network drive. WNetEnable/WNetDisable (521, 522): no
   arguments, 1 (US11). WNetGetUser(szUser, lpSize) pops 8 bytes (3.1 DDK;
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

/* FlashWindow: the caption (or a minimised window's title) drawn the other
   way, active or inactive, without changing which window is active; FALSE
   puts back how it is. Returns whether it was drawn active before (SDK).
   Write imports it. UNVERIFIED: 3.1 may send WM_NCACTIVATE instead of
   drawing directly */
void redraw_icon_title(HWND h);
BOOL WINAPI FlashWindow(HWND h, BOOL invert)
{
    PWND w = valid_wnd(h);
    BOOL was, now;
    if (!w)
        return FALSE;
    was = (w->flags & WF_ACTIVEFRAME) != 0;
    now = invert ? !was : (h == hwndActive);
    if (now)
        w->flags |= WF_ACTIVEFRAME;
    else
        w->flags &= ~WF_ACTIVEFRAME;
    if (now != was && is_visible(w)) {
        if (w->style & WS_MINIMIZE)
            redraw_icon_title(h);
        else
            nc_paint(h, 0, now);
    }
    return was;
}

/* ---- global atoms (USER.268-271) and the palette front ends (282, 283):
   OLECLI, OLESVR, DDEML and SHELL import them from USER. On 3.1 the
   global atom table is a local atom table kept in a heap of USER's own
   (USER's GlobalAddAtom sets DS to it and calls KERNEL's AddAtom:
   Schulman, Undocumented Windows ch. 5; Pietrek, Windows Internals ch. 1,
   GlobalInitAtom); US15 found it there (a 100h-byte heap at start, which
   GlobalAddAtom grows) and RegisterWindowMessage's and
   RegisterClipboardFormat's atoms not in it (GlobalFindAtom does not find
   them: they are in USER's DGROUP atom table). w16 does the same
   (uheap.c) ---- */
ATOM WINAPI GlobalAddAtom(LPCSTR s)
{
    return ga_add(s);
}

ATOM WINAPI GlobalDeleteAtom(ATOM a)
{
    return ga_delete(a);
}

ATOM WINAPI GlobalFindAtom(LPCSTR s)
{
    return ga_find(s);
}

UINT WINAPI GlobalGetAtomName(ATOM a, LPSTR buf, int n)
{
    return ga_name(a, buf, n);
}

/* UNVERIFIED: 3.1's USER may also track the foreground palette and send
   WM_PALETTECHANGED; on VGA (no RC_PALETTE) nothing is ever mapped */
HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE h, BOOL bk)
{
    return G_GDISELECTPALETTE(hdc, h, bk);
}

UINT WINAPI RealizePalette(HDC hdc)
{
    return LOWORD(G_GDIREALIZEPALETTE(hdc));
}

WORD WINAPI U_WNETENABLE(void)                                  /* 521 */
{
    return 1;
}

WORD WINAPI U_WNETDISABLE(void)                                 /* 522 */
{
    return 1;
}

/* ---- undocumented exports whose argument sizes were not known (they
   were stubs that popped nothing). US11 on Windows 3.1 measured what each
   pops (Schulman, Undocumented Windows ch. 6, for the parameter lists);
   those not here are correctly sized stubs returning 3.1's 0: IsTwoByte-
   CharPrefix (BEAR51, 2 bytes), QuerySendMessage (10), LockInput (6),
   LockMyTask (2), WinOldAppHackOMatic (4), GetUserLocalObjType (2),
   hardware_event (register arguments, pops none), OldExitWindows (none:
   Schulman; not called, it ends Windows). ---- */

/* USER.275: no arguments; 1 (US11). UNVERIFIED that it is more than the
   whole screen painted again. The display driver calls it from its INT 2Fh
   4002h handler in 386 Enhanced mode (the DDK's Display Driver Adaptation
   Guide), which the VDD runs at any point of the System VM's code, USER's
   own included: so it only notes the repaint and wakes a task; the next
   GetMessage, PeekMessage or WaitMessage of any task invalidates the
   screen (repaint_check) */
volatile BYTE __far repaint_due;

BOOL WINAPI U_REPAINTSCREEN(void)
{
    HQUEUE hq = hqInput ? hqInput : hqFirst;
    repaint_due = 1;
    if (hq && q_of(hq)->task)
        K_POSTEVENT(q_of(hq)->task);
    return TRUE;
}

void repaint_check(void)
{
    if (!repaint_due)
        return;
    repaint_due = 0;
    RedrawWindow(hwndDesktop, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

/* USER.323 GetMessage2(lpMsg, hwnd, min, max, wRemove, bWait): the back
   end of GetMessage and PeekMessage (Schulman); 14 bytes (US11: with
   bWait FALSE and a message waiting, 1) */
BOOL WINAPI U_GETMESSAGE2(MSG __far *m, HWND h, UINT lo, UINT hi, UINT remove, BOOL wait)
{
    if (!wait)
        return PeekMessage(m, h, lo, hi, remove);
    return GetMessage(m, h, lo, hi);
}

/* USER.343 GetFilePortName(LPOFSTRUCT): GDI's "Print to File" dialog
   (Schulman); 4 bytes. w16 has no dialog yet: SP_USERABORT (-3), as if
   cancelled. UNVERIFIED beyond the size */
int WINAPI U_GETFILEPORTNAME(OFSTRUCT __far *of)
{
    (void)of;
    return -3;
}

/* USER.372 GetInternalIconHeader(src, dst): 8 bytes; copies the 12 bytes
   at src (an icon's CURSORICONINFO header: hot spot, size, width bytes,
   planes, bits a pixel) to dst (US11: a pattern at src, 12 bytes of it at
   dst, nothing more). Program Manager imports it. UNVERIFIED: the value
   it returns (3.1's DX was src's selector) */
DWORD WINAPI U_GETINTERNALICONHEADER(const BYTE __far *src, BYTE __far *dst)
{
    int i;
    for (i = 0; i < 12; i++)
        dst[i] = src[i];
    return (DWORD)(const void __far *)src;
}
