/*
 * snap.c - scripted screenshots inside a running Windows (test tool).
 *
 * Runs as the Windows shell (tests/refrun.sh) and executes C:\SNAP.TXT,
 * one command per line:
 *     run <command line>     WinExec it (SW_SHOWNORMAL)
 *     wait <ms>              keep dispatching messages for <ms> ms
 *     menu <letter>          open the active window's menu (Alt+letter)
 *     close                  post WM_SYSCOMMAND SC_CLOSE to the active window
 *     childcmd <hex>         post WM_SYSCOMMAND to the active MDI child
 *     cmd <decimal id>       post WM_COMMAND id to the active window (for
 *                            testing our own programs, whose IDs we know)
 *     key <vk-hex>           post a key press to the window with the focus
 *     char <c>               post a WM_CHAR to the window with the focus
 *     alt <c>                Alt+c (WM_SYSCHAR), for dialog mnemonics
 *     type <text>            post a WM_CHAR for each character of <text>
 *     snap <file.bmp>        save the whole screen as a 4-bit .BMP
 *     dump                   log every window: class, text, style, rect
 *     focus                  log the window with the focus
 *     blinktime <ms>         SetCaretBlinkTime (65535: carets keep their
 *                            phase, for repeatable screenshots)
 *     clickid <hex id>       click (posted) the middle of a control of the
 *                            active dialog
 *     move <x> <y> <cx> <cy> MoveWindow the active top-level window
 *     mdown <x> <y>          real (journalled) mouse input at screen
 *     mmove <x> <y>          position x,y: left button down, a move, the
 *     mup <x> <y>            button up. Moves are played in steps from
 *                            the last position, so drag loops see them.
 *     mclick <x> <y>         a click (down and up in one go)
 *     kdown <vk-hex>         a real (journalled) key down / up, so that
 *     kup <vk-hex>           GetKeyState sees it (Shift+click etc.)
 *     cursor                 log the current cursor: arrow, no, or other
 *     copyfile <from> <to>   copy a file (to catch it mid-session)
 *     exitwin                ExitWindows; logs it if an application refused
 *     blank <l> <t> <r> <b>  paint a screen rectangle black, to keep
 *                            changing content (free memory) out of the
 *                            next snap
 *     shellabout <file> <app>|<other>   call SHELL's ShellAbout with these
 *                            strings (no icon; "-" for a NULL other),
 *                            dump its windows, save the screen as <file>
 *                            and close it with OK (3.x only)
 *     associcon <path>       log what SHELL's ExtractAssociatedIcon gives
 *                            for <path>: icon file, index (3.x only)
 *     drag x1 y1 x2 y2       press the left button at screen (x1,y1), move
 *                            to (x2,y2) and release it (journal playback)
 *     click x y / dblclick x y   left-button clicks at screen (x,y)
 *     dragitem n x y         drag item n of the list box with the focus
 *                            (else the active window's rightmost one) to
 *                            screen (x,y); dblitem n double-clicks it
 *     place CLASS x y cx cy  move the top-level window of that class
 *     settime hh:mm:ss[.cc]  set the DOS time (INT 21h AH=2Dh)
 *     setdate mm/dd/yyyy     set the DOS date (INT 21h AH=2Bh)
 *     freeze hh:mm:ss <ms>   keep setting the DOS time to hh:mm:ss.00 every
 *                            50 ms for <ms> ms: a clock shows a known time
 *     syscmd <hex>           post WM_SYSCOMMAND <hex> to the active window
 *     movewin x y cx cy      MoveWindow the active window (decimal)
 *     post <msg> <wp> <lp>   post a message (hex) to the active window
 *     send <msg> <wp> <lp>   send it and log the result
 *     timers                 take every timer USER has left (and log how
 *                            many), to see what a program does without one
 *     freetimers             give them back
 *     info                   log the active top-level window in detail:
 *                            class data, client rectangle, children
 *                            relative to it (font, edit state), scroll
 *                            bars, every menu item's state
 *     text                   log the whole text of the focus window
 *     dumpmenu               log the active window's menu bar and system
 *                            menu: each item's ID, state flags and text
 *     mark <n>               label the next observations of THELP.EXE
 * Lines starting with ';' are comments. Progress and errors go to TK.LOG.
 * When the script ends SNAP exits, and because it is the shell, Windows
 * exits too. A script named on the command line (SNAP.EXE FILE.TXT) runs
 * instead of C:\SNAP.TXT, the same way (if it cannot be opened, the 3.1
 * build says so in its interactive window). A script is read as it runs:
 * its length is not bounded; a line is cut at LINE_MAX (64K) characters.
 *
 * Interactive mode (the Windows 3.1 build): with no script, i.e. no
 * command line and no C:\SNAP.TXT (started from Program Manager or File
 * > Run), SNAP opens a window, title "SNAP", made of standard controls (a
 * dialog template of class SnapWindow, snap.rc; icon tools/snapicon.py):
 *   - Command: an edit line; Enter or Execute runs it through the same
 *     dispatcher as a script line (run_line). While a command or script
 *     runs, Execute reads Stop (it also cuts a wait short; so does Esc).
 *   - Target: the window "the active window" means to the commands: the
 *     one that was active before SNAP's window was activated (not one of
 *     SNAP's own), or one picked with Pick (click it; Esc cancels). Before
 *     a command that acts on a window SNAP activates the target, as a
 *     click would, and leaves it active (to see what the command did);
 *     after info, text, dumpmenu and snap it takes the activation back.
 *     run, wait, settime, setdate, freeze, timers, freetimers, mark and
 *     dump leave the activation alone. With no target the commands act
 *     on SNAP's own window.
 *   - Snap (the whole screen to SNAPnnnn.BMP in the current directory,
 *     the first number not taken, never overwriting; past 9999 the digits
 *     take the prefix's letters, up to the 8.3 name), Dump, Info, Run...
 *     (COMMDLG's Open dialog; the program runs as "run <path>").
 *     SNAP's window hides while any snap command takes the screen.
 *   - The log pane: what each command logs, the lines script mode writes
 *     to TK.LOG (no TK.LOG is written in this mode). The whole log is kept
 *     in global memory (bound: memory); the pane shows the newest 32000
 *     characters (PANE_MAX: its edit control holds one segment).
 *   - File: Open Script... (runs a script file, progress in the log; the
 *     target is activated first), Save Log... (all of it), Clear Log,
 *     Exit. Help: Commands (their syntax, into the log), About SNAP...
 *     (SHELL's ShellAbout). COMMDLG and SHELL are loaded when used, so
 *     script mode does not import them.
 * The command line takes what its edit control holds (EM_LIMITTEXT 0: no
 * bound below the control's own).
 *
 * Used for black-box observation of the reference Windows (the original
 * Program Manager's layout, for UI fidelity) and for comparing w16's own
 * programs against it pixel by pixel. Only documented APIs are used.
 *
 * SNAP_WIN2 builds SNAP2.EXE for Windows 2.x (tests/w386run.sh): programs
 * start through LoadModule, screenshots are device-dependent bitmaps
 * (GetBitmapBits, no DIBs before 3.0) written as .DDB files that
 * tools/ddb2png.py converts, and the session ends with ExitWindows when
 * the script is done. SNAP2 is script mode only (no interactive window);
 * it carries the icon for w16's 2.x Program Manager, which reads it
 * itself (Windows 2.x's LoadIcon cannot).
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
/* Task start-up calls every Windows program makes (KERNEL.30, USER.5);
   not declared in windows.h. */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

static int g_stop;                      /* GUI: Stop pressed (never set in script mode) */
#ifndef SNAP_WIN2
#include <commdlg.h>
#include "snap.h"
static int g_gui;                       /* the interactive window is up */
static HWND g_dlg;
static void gui_emit(const char FAR *s, WORD n);
static void gui_flush(void);

/* WM_NULL does nothing, so the interactive window's loops drop it rather
   than dispatch it. Seen on w16's USER: EndDialog posts WM_NULL to wake
   the modal loop of a common dialog, the dialog is destroyed before it is
   read, and dispatching it later to the freed handle (which IsWindow
   still took) destroyed SNAP's window. */
static int gui_skip(const MSG FAR *m)
{
    return m->message == WM_NULL;
}
#endif

/* Everything a command logs: TK.LOG in script mode, the log pane in the
   interactive window. */
static void emit(const char FAR *s, WORD n)
{
#ifndef SNAP_WIN2
    if (g_gui) {
        gui_emit(s, n);
        return;
    }
#endif
    log_write(s, n);
}

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    emit(s, n);
}

static void pump(DWORD ms)
{
    DWORD end;
    MSG msg;
#ifndef SNAP_WIN2
    if (g_gui)
        gui_flush();                    /* show what the command logged so far */
#endif
    end = GetTickCount() + ms;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
#ifndef SNAP_WIN2
            if (g_gui && gui_skip(&msg))
                continue;
            if (g_gui && g_dlg && IsDialogMessage(g_dlg, &msg))
                continue;
#endif
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0 && !g_stop);
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

#ifdef SNAP_WIN2
/* WinExec for Windows 2.x: LoadModule with a parameter block */
static UINT start_program(char *cmd)
{
    static char tail[130];
    static WORD show[2] = { 2, SW_SHOWNORMAL };
    struct { WORD env; LPSTR tail; WORD FAR *show; DWORD res; } lp;
    char *a = cmd;
    int n = 0;
    while (*a && *a != ' ')
        a++;
    if (*a)
        *a++ = 0;
    while (a[n] && n < 126) {
        tail[1 + n] = a[n];
        n++;
    }
    tail[0] = (char)n;
    tail[1 + n] = '\r';
    lp.env = 0;
    lp.tail = tail;
    lp.show = show;
    lp.res = 0;
    return (UINT)LoadModule(cmd, &lp);
}
#else
#define start_program(cmd) WinExec(cmd, SW_SHOWNORMAL)
#endif

static WORD parse_num(const char *s, WORD base)
{
    WORD v = 0;
    for (;; s++) {
        WORD d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else
            return v;
        v = v * base + d;
    }
}

static HWND target(void)
{
    HWND h = GetFocus();
    return h ? h : GetActiveWindow();
}

/* The top-level window of the focus: Windows 2.x answers GetActiveWindow
   per task, and SNAP has no window of its own (observed). */
static HWND top_window(void)
{
    HWND h = GetFocus(), p;
    if (!h)
        h = GetActiveWindow();
    if (!h)                             /* Windows 2.x: not our task's */
        return FindWindow("Progman", NULL);
    /* up to the first window that is not a child: GetParent of a dialog
       is its owner, which is not where its commands go */
    while ((GetWindowLong(h, GWL_STYLE) & WS_CHILD) && (p = GetParent(h)) != NULL)
        h = p;
    return h;
}

#ifndef SNAP_WIN2
/* ---- screenshot ---- */

#define BAND 96                         /* scan lines per call */

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD pal[16];
} BMI16;


static int snap(const char *file)
{
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy);
    HBITMAP old;
    HGLOBAL hbuf = GlobalAlloc(GMEM_MOVEABLE, (WORD)(stride * BAND));
    BYTE FAR *buf = hbuf ? (BYTE FAR *)GlobalLock(hbuf) : NULL;
    static BMI16 bmi;
    BITMAPFILEHEADER fh;
    HFILE f;
    int y, ok = 0;

    if (!mem || !bm || !buf)
        goto done;
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
    SelectObject(mem, old);

    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = BI_RGB;
    bmi.h.biSizeImage = 0;              /* no C runtime: no 32-bit multiply */
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;

    f = _lcreat(file, 0);
    if (f == HFILE_ERROR)
        goto done;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    /* the palette is filled by the first GetDIBits; write headers after */
    _llseek(f, fh.bfOffBits, 0);
    ok = 1;
    for (y = 0; y < cy; y += BAND) {    /* DIB scan 0 is the bottom line */
        int n = cy - y < BAND ? cy - y : BAND;
        if (GetDIBits(scr, bm, y, n, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS) != n ||
            _lwrite(f, buf, stride * n) != stride * n)
            ok = 0;
    }
    _llseek(f, 0, 0);
    _lwrite(f, (LPCSTR)&fh, sizeof fh);
    _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
    _lclose(f);
done:
    if (buf)
        GlobalUnlock(hbuf);
    if (hbuf)
        GlobalFree(hbuf);
    if (bm)
        DeleteObject(bm);
    if (mem)
        DeleteDC(mem);
    ReleaseDC(NULL, scr);
    return ok;
}

#endif

/* ---- window dump ---- */

static void outhex(DWORD v, int digits)
{
    char b[9];
    int i;
    for (i = digits - 1; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[(WORD)v & 15];
        v >>= 4;
    }
    b[digits] = 0;
    out(b);
}

static void outint(int v)
{
    char b[7];
    int i = 6;
    WORD u = v < 0 ? -v : v;
    b[6] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    out(b + i);
}

static void dump_tree(HWND w, int depth)
{
    char cls[64], text[64];
    RECT r;
    int i;
    HWND c;
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) && depth == 0)
            continue;
        GetClassName(w, cls, sizeof cls);
        /* WM_GETTEXT, unlike GetWindowText, reaches another task's edit
           controls (16-bit Windows shares one address space) */
        SendMessage(w, WM_GETTEXT, sizeof text, (LPARAM)(LPSTR)text);
        GetWindowRect(w, &r);
        out("WIN ");
        for (i = 0; i < depth; i++)
            out("  ");
        out("class=\"");
        out(cls);
        out("\" text=\"");
        out(text);
        out("\" style=");
        outhex(GetWindowLong(w, GWL_STYLE), 8);
#ifndef SNAP_WIN2                       /* no extended styles before 3.0 */
        out(" exstyle=");
        outhex(GetWindowLong(w, GWL_EXSTYLE), 8);
#endif
        out(" id=");
        outhex(GetWindowWord(w, GWW_ID), 4);
        out(" rect=");
        outint(r.left); out(",");
        outint(r.top); out(",");
        outint(r.right); out(",");
        outint(r.bottom);
        out("\r\n");
#ifndef SNAP_WIN2
        {   /* the entries of list and combo boxes */
            BOOL combo = !lstrcmpi(cls, "ComboBox"), list = !lstrcmpi(cls, "ListBox");
            if (combo || list) {
                LONG n = SendMessage(w, combo ? CB_GETCOUNT : LB_GETCOUNT, 0, 0), k;
                LONG cur = SendMessage(w, combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0);
                for (k = 0; k < n; k++) {
                    if (SendMessage(w, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, (WPARAM)k, 0) <
                        (LONG)sizeof text) {
                        SendMessage(w, combo ? CB_GETLBTEXT : LB_GETTEXT, (WPARAM)k,
                                    (LPARAM)(LPSTR)text);
                        out("WIN ");
                        for (i = 0; i <= depth; i++)
                            out("  ");
                        out(k == cur ? "* entry \"" : "  entry \"");
                        out(text);
                        out("\"\r\n");
                    }
                }
            }
        }
#endif
        c = GetWindow(w, GW_CHILD);
        if (c)
            dump_tree(c, depth + 1);
    }
}

#ifdef SNAP_WIN2
#define BAND 96
/* The screen as device-dependent bitmap bits, in bands of BAND lines:
   header "DDB2" cx cy band planes bitspixel widthbytes (words), then
   each band's GetBitmapBits data. */
static int snap(const char *file)
{
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), y, ok = 0;
    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, BAND), old;
    BITMAP info;
    HGLOBAL hbuf = NULL;
    BYTE FAR *buf = NULL;
    WORD hdr[7], size = 0;
    HFILE f;
    OFSTRUCT of;

    if (!mem || !bm)
        goto done;
    GetObject(bm, sizeof info, (LPSTR)&info);
    size = info.bmWidthBytes * info.bmPlanes * BAND;
    hbuf = GlobalAlloc(GMEM_MOVEABLE, size);
    buf = hbuf ? (BYTE FAR *)GlobalLock(hbuf) : NULL;
    if (!buf)
        goto done;
    f = OpenFile(file, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        goto done;
    hdr[0] = 'D' | ('D' << 8);
    hdr[1] = 'B' | ('2' << 8);
    hdr[2] = cx;
    hdr[3] = cy;
    hdr[4] = BAND;
    hdr[5] = info.bmPlanes | (info.bmBitsPixel << 8);
    hdr[6] = info.bmWidthBytes;
    _lwrite(f, (LPCSTR)hdr, sizeof hdr);
    ok = 1;
    old = SelectObject(mem, bm);
    for (y = 0; y < cy; y += BAND) {
        BitBlt(mem, 0, 0, cx, BAND, scr, 0, y, SRCCOPY);
        SelectObject(mem, old);
        if (GetBitmapBits(bm, size, (LPSTR)buf) != (LONG)size || _lwrite(f, (LPCSTR)buf, size) != size)
            ok = 0;
        SelectObject(mem, bm);
    }
    SelectObject(mem, old);
    _lclose(f);
done:
    if (buf)
        GlobalUnlock(hbuf);
    if (hbuf)
        GlobalFree(hbuf);
    if (bm)
        DeleteObject(bm);
    if (mem)
        DeleteDC(mem);
    ReleaseDC(NULL, scr);
    return ok;
}
#endif

/* ---- real input: a journal playback hook ----
 * Mouse and key events go through WH_JOURNALPLAYBACK (documented in the
 * SDK), so they reach the system as hardware input does: the cursor
 * moves, drag loops (File Manager's) and GetKeyState see them. The hook
 * is installed for one command's events and removed when they are done. */

#define MAXEV 32
static EVENTMSG evq[MAXEV];
static int evn, evat;
static BOOL ev_done;
static int mouse_x = 320, mouse_y = 240;
static HOOKPROC old_hook;

DWORD FAR PASCAL __export __loadds JournalProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_GETNEXT) {
        EVENTMSG FAR *e = (EVENTMSG FAR *)lp;
        if (evat < evn) {
            *e = evq[evat];
            e->time = GetTickCount();
            return 0;
        }
        /* all played: no input yet (a delay), so that a program's
           PeekMessage loop finds none and yields to SNAP, which removes
           the hook */
        *e = evq[evn - 1];
        e->time = GetTickCount();
        return 2;
    }
    if (code == HC_SKIP) {
        if (++evat >= evn)
            ev_done = TRUE;
        return 0;
    }
    return DefHookProc(code, wp, lp, &old_hook);
}

static void ev_add(UINT msg, WORD l, WORD h)
{
    if (evn < MAXEV) {
        evq[evn].message = msg;
        evq[evn].paramL = l;
        evq[evn].paramH = h;
        evq[evn].time = 0;
        evn++;
    }
}

static void ev_play(void)
{
    DWORD end = GetTickCount() + 5000;
    if (!evn)
        return;
    evat = 0;
    ev_done = FALSE;
    old_hook = SetWindowsHook(WH_JOURNALPLAYBACK, (HOOKPROC)JournalProc);
    while (!ev_done && (long)(GetTickCount() - end) < 0)
        pump(20);
    UnhookWindowsHook(WH_JOURNALPLAYBACK, (HOOKPROC)JournalProc);
    if (!ev_done)
        out(" -> not all played");
    evn = 0;
}

static void mouse_to(int x, int y)       /* moves in steps from the last */
{
    int i, x0 = mouse_x, y0 = mouse_y;
    for (i = 1; i <= 8; i++)
        ev_add(WM_MOUSEMOVE, x0 + (x - x0) * i / 8, y0 + (y - y0) * i / 8);
    mouse_x = x;
    mouse_y = y;
}

static void mouse_args(const char *arg, int *x, int *y)
{
    *x = parse_num(arg, 10);
    while (*arg && *arg != ' ')
        arg++;
    while (*arg == ' ')
        arg++;
    *y = parse_num(arg, 10);
}

#ifndef SNAP_WIN2
/* ShellAbout, looked up at run time; the box is modal, so a timer dumps
   and photographs it and presses OK */
typedef int (FAR PASCAL *SHELLABOUT)(HWND, LPCSTR, LPCSTR, HICON);
static char about_file[64];

void FAR PASCAL __export __loadds AboutTimer(HWND w, UINT m, UINT id, DWORD t)
{
    HWND a = GetActiveWindow();
    (void)w; (void)m; (void)t;
    KillTimer(NULL, id);
    out("\r\n");
    dump_tree(GetWindow(GetDesktopWindow(), GW_CHILD), 0);
    if (!snap(about_file))
        out("SNAP shellabout snap failed\r\n");
    if (a)
        PostMessage(a, WM_COMMAND, IDOK, 0);
}

static void shell_about(char *arg)
{
    HINSTANCE lib = LoadLibrary("SHELL.DLL");
    SHELLABOUT fn = (UINT)lib > 32 ? (SHELLABOUT)GetProcAddress(lib, "ShellAbout") : NULL;
    char *app = arg, *other;
    int i = 0;
    while (*app && *app != ' ')
        about_file[i++] = *app++;
    about_file[i] = 0;
    while (*app == ' ')
        app++;
    for (other = app; *other && *other != '|'; other++)
        ;
    if (*other)
        *other++ = 0;
    if (!fn) {
        out(" -> no ShellAbout");
        return;
    }
    SetTimer(NULL, 0, 1500, (TIMERPROC)AboutTimer);
    fn(NULL, app, same(other, "-") ? NULL : other, NULL);
    FreeLibrary(lib);
}

typedef HICON (FAR PASCAL *EXTRACTASSOC)(HINSTANCE, LPSTR, UINT FAR *);

static void assoc_icon(char *arg)
{
    static char path[260];
    HINSTANCE lib = LoadLibrary("SHELL.DLL");
    EXTRACTASSOC fn = (UINT)lib > 32 ? (EXTRACTASSOC)GetProcAddress(lib, "ExtractAssociatedIcon") : NULL;
    UINT index = 0;
    HICON h;
    lstrcpy(path, arg);
    if (!fn) {
        out(" -> no ExtractAssociatedIcon");
        return;
    }
    h = fn((HINSTANCE)init_regs[I_DI], path, &index);
    out(" -> \"");
    out(path);
    out("\" index ");
    outint(index);
    out(h ? " icon" : " no icon");
    if (h)
        DestroyIcon(h);
    FreeLibrary(lib);
}
#endif

static void copy_file(char *arg)
{
    static char buf[512];
    char *to = arg;
    HFILE a, b;
    int n;
    while (*to && *to != ' ')
        to++;
    if (*to)
        *to++ = 0;
    a = _lopen(arg, OF_READ);
    b = a == HFILE_ERROR ? HFILE_ERROR : _lcreat(to, 0);
    if (b == HFILE_ERROR) {
        out(" -> failed");
    } else {
        while ((n = _lread(a, buf, sizeof buf)) > 0)
            _lwrite(b, buf, n);
        _lclose(b);
    }
    if (a != HFILE_ERROR)
        _lclose(a);
}

/* ---- script ---- */
#ifndef SNAP_WIN2
/* ---- mouse input: a WH_JOURNALPLAYBACK hook (SDK 3.1) plays back
   button and move events at screen coordinates ---- */

#define NEV 64
static EVENTMSG ev[NEV];
static int nev, cur;
static HOOKPROC hk, thunk;
static int playing;

static int waited = -1;

LRESULT CALLBACK __export Playback(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_GETNEXT) {
        EVENTMSG FAR *e = (EVENTMSG FAR *)lp;
        *e = ev[cur];
        e->time = GetTickCount();
        /* USER asks again for the same event while it waits: the delay
           counts only the first time */
        if (cur && cur != waited && ev[cur].message == WM_MOUSEMOVE) {
            waited = cur;
            return 50;
        }
        return 0;
    }
    if (code == HC_SKIP) {
        if (++cur >= nev) {
            UnhookWindowsHook(WH_JOURNALPLAYBACK, thunk);
            playing = 0;
        }
        return 0;
    }
    return DefHookProc(code, wp, lp, &hk);
}

static void add(UINT m, int x, int y)
{
    if (nev < NEV) {
        ev[nev].message = m;
        ev[nev].paramL = x;
        ev[nev].paramH = y;
        ev[nev].time = 0;
        nev++;
    }
}

static void play(void)
{
    DWORD end = GetTickCount() + 10000;
    cur = 0;
    waited = -1;
    playing = 1;
    if (!thunk)
        thunk = (HOOKPROC)MakeProcInstance((FARPROC)Playback, (HINSTANCE)init_regs[I_DI]);
    hk = SetWindowsHook(WH_JOURNALPLAYBACK, thunk);
    while (playing && (long)(GetTickCount() - end) < 0)
        pump(50);
    nev = 0;
}

static void drag(int x1, int y1, int x2, int y2)
{
    int i;
    add(WM_MOUSEMOVE, x1, y1);
    add(WM_LBUTTONDOWN, x1, y1);
    /* a few small steps first, as a hand moves: programs start a drag
       once the mouse has left a small square around the press */
    add(WM_MOUSEMOVE, x1 + 1, y1);
    add(WM_MOUSEMOVE, x1 + 3, y1 + 1);
    add(WM_MOUSEMOVE, x1 + 6, y1 + 3);
    for (i = 1; i <= 16; i++)
        add(WM_MOUSEMOVE, x1 + 6 + (x2 - x1 - 6) * i / 16, y1 + 3 + (y2 - y1 - 3) * i / 16);
    add(WM_MOUSEMOVE, x2, y2);
    add(WM_LBUTTONUP, x2, y2);
    play();
}

static void click(int x, int y, int twice)
{
    add(WM_MOUSEMOVE, x, y);
    add(WM_LBUTTONDOWN, x, y);
    add(WM_LBUTTONUP, x, y);
    if (twice) {
        add(WM_LBUTTONDOWN, x, y);
        add(WM_LBUTTONUP, x, y);
    }
    play();
}

/* the visible list box furthest to the right in a window (File
   Manager's directory pane) */
static HWND best_lb;
static int best_x;

BOOL CALLBACK __export FindLb(HWND w, LPARAM lp)
{
    char cls[16];
    RECT r;
    (void)lp;
    GetClassName(w, cls, sizeof cls);
    GetWindowRect(w, &r);
    if (!lstrcmpi(cls, "ListBox") && IsWindowVisible(w) && (!best_lb || r.left > best_x)) {
        best_lb = w;
        best_x = r.left;
    }
    return TRUE;
}

/* a point in item n of the list box with the focus, or else of the
   active window's rightmost list box: 20 pixels in, half way down */
static int item_point(int n, POINT *pt)
{
    HWND lb = GetFocus();
    RECT r;
    char cls[16];
    cls[0] = 0;
    if (lb)
        GetClassName(lb, cls, sizeof cls);
    if (lstrcmpi(cls, "ListBox")) {
        best_lb = NULL;
        EnumChildWindows(top_window(), (WNDENUMPROC)MakeProcInstance((FARPROC)FindLb,
                         (HINSTANCE)init_regs[I_DI]), 0);
        lb = best_lb;
    }
    if (!lb || SendMessage(lb, LB_GETITEMRECT, n, (LPARAM)(RECT FAR *)&r) == LB_ERR)
        return 0;
    pt->x = r.left + 20;
    pt->y = (r.top + r.bottom) / 2;
    ClientToScreen(lb, pt);
    return 1;
}

static char *word(char *p, int *v)
{
    int neg = 0;
    while (*p == ' ')
        p++;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    *v = (int)parse_num(p, 10);
    if (neg)
        *v = -*v;
    while (*p && *p != ' ')
        p++;
    return p;
}
#endif

/* ---- time, messages, timers (for Clock's scenarios) ---- */

static WORD dos_settime(WORD hm, WORD sc);
#pragma aux dos_settime = "mov ah, 2Dh" "int 21h" "xor ah, ah" parm [cx] [dx] value [ax];
static WORD dos_setdate(WORD y, WORD md);
#pragma aux dos_setdate = "mov ah, 2Bh" "int 21h" "xor ah, ah" parm [cx] [dx] value [ax];

/* the next number in *s (decimal or hex), skipping what is not a digit */
static WORD next_num(char **s, WORD base)
{
    char *p = *s;
    WORD v;
    while (*p && !((*p >= '0' && *p <= '9') ||
                   (base == 16 && ((*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')))))
        p++;
    v = parse_num(p, base);
    while ((*p >= '0' && *p <= '9') ||
           (base == 16 && ((*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f'))))
        p++;
    *s = p;
    return v;
}

static WORD set_time(char **s)
{
    WORD h = next_num(s, 10), m = next_num(s, 10), sec = next_num(s, 10), cs = 0;
    if (**s == '.')
        cs = next_num(s, 10);
    return dos_settime((h << 8) | m, (sec << 8) | cs);
}

#ifndef SNAP_WIN2                       /* 3.x calls: GetMenuItemCount... */
static void dump_menu(HMENU m, int depth)
{
    char text[64];
    int i, k, n;
    if (!m)
        return;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        UINT st = GetMenuState(m, i, MF_BYPOSITION);
        text[0] = 0;
        GetMenuString(m, i, text, sizeof text, MF_BYPOSITION);
        out("MENU ");
        for (k = 0; k < depth; k++)
            out("  ");
        out("id=");
        outhex(GetMenuItemID(m, i), 4);
        out(" state=");
        outhex(st, 4);
        out(" \"");
        out(text);
        out("\"\r\n");
        if (st & MF_POPUP)
            dump_menu(GetSubMenu(m, i), depth + 1);
    }
}
#endif

#define MAXTIMERS 64
static UINT timer_ids[MAXTIMERS];
static int ntimers;
/* ---- info: one window in detail ---- */

static void outkv(const char *k, int v)
{
    out(" ");
    out(k);
    out("=");
    outint(v);
}

static void info_font(HWND w)
{
    HFONT f = (HFONT)SendMessage(w, WM_GETFONT, 0, 0);
    LOGFONT lf;
    if (!f) {
        out(" font=system");
        return;
    }
    GetObject(f, sizeof lf, (LPSTR)&lf);
    out(" font=\"");
    out(lf.lfFaceName);
    out("\"");
    outkv("h", lf.lfHeight);
    outkv("w", lf.lfWidth);
    outkv("wt", lf.lfWeight);
    outkv("pf", lf.lfPitchAndFamily);
}

static void info_menu(HMENU m, int depth)
{
    int n = GetMenuItemCount(m), k, i;
    char t[64];
    for (k = 0; k < n; k++) {
        UINT id = GetMenuItemID(m, k);
        UINT st = GetMenuState(m, k, MF_BYPOSITION);
        GetMenuString(m, k, t, sizeof t, MF_BYPOSITION);
        out("MENU ");
        for (i = 0; i < depth; i++)
            out("  ");
        out("\"");
        out(t);
        out("\"");
        outkv("id", (int)id);
        out(" state=");
        outhex(st, 4);
        out("\r\n");
        if (id == (UINT)-1)
            info_menu(GetSubMenu(m, k), depth + 1);
    }
}

static void info(void)
{
    HWND w = top_window(), c;
    RECT r;
    POINT p;
    char cls[64];
    int lo, hi;
    if (!w) {
        out(" -> no window");
        return;
    }
    out("\r\n");
    GetClassName(w, cls, sizeof cls);
    out("INFO class=\"");
    out(cls);
    out("\" clsstyle=");
    outhex(GetClassWord(w, GCW_STYLE), 4);
    outkv("wndextra", GetClassWord(w, GCW_CBWNDEXTRA));
    outkv("clsextra", GetClassWord(w, GCW_CBCLSEXTRA));
    out(" brush=");
    outhex(GetClassWord(w, GCW_HBRBACKGROUND), 4);
    out(" icon=");
    outhex(GetClassWord(w, GCW_HICON), 4);
    out(" cursor=");
    outhex(GetClassWord(w, GCW_HCURSOR), 4);
    out(" hinst=");
    outhex(GetWindowWord(w, GWW_HINSTANCE), 4);
    GetClientRect(w, &r);
    outkv("clientw", r.right);
    outkv("clienth", r.bottom);
    p.x = p.y = 0;
    ClientToScreen(w, &p);
    outkv("clientx", p.x);
    outkv("clienty", p.y);
    GetScrollRange(w, SB_VERT, &lo, &hi);
    outkv("vmin", lo);
    outkv("vmax", hi);
    outkv("vpos", GetScrollPos(w, SB_VERT));
    GetScrollRange(w, SB_HORZ, &lo, &hi);
    outkv("hmin", lo);
    outkv("hmax", hi);
    outkv("hpos", GetScrollPos(w, SB_HORZ));
    info_font(w);
    out("\r\n");
    for (c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        GetClassName(c, cls, sizeof cls);
        GetWindowRect(c, &r);
        ScreenToClient(w, (POINT FAR *)&r.left);
        ScreenToClient(w, (POINT FAR *)&r.right);
        out("INFO   child class=\"");
        out(cls);
        out("\" id=");
        outhex(GetWindowWord(c, GWW_ID), 4);
        out(" style=");
        outhex(GetWindowLong(c, GWL_STYLE), 8);
        outkv("x", r.left);
        outkv("y", r.top);
        outkv("w", r.right - r.left);
        outkv("h", r.bottom - r.top);
        out(" hinst=");
        outhex(GetWindowWord(c, GWW_HINSTANCE), 4);
        info_font(c);
        if (!lstrcmpi(cls, "Edit")) {
            DWORD sel = SendMessage(c, EM_GETSEL, 0, 0);
            SendMessage(c, EM_GETRECT, 0, (LPARAM)(RECT FAR *)&r);
            outkv("fl", r.left);
            outkv("ft", r.top);
            outkv("fr", r.right);
            outkv("fb", r.bottom);
            outkv("lines", (int)SendMessage(c, EM_GETLINECOUNT, 0, 0));
            outkv("selstart", LOWORD(sel));
            outkv("selend", HIWORD(sel));
            outkv("modify", (int)SendMessage(c, EM_GETMODIFY, 0, 0));
            outkv("len", (int)SendMessage(c, WM_GETTEXTLENGTH, 0, 0));
#ifndef SNAP_WIN2
            outkv("first", (int)SendMessage(c, EM_GETFIRSTVISIBLELINE, 0, 0));
#endif
            outkv("canundo", (int)SendMessage(c, EM_CANUNDO, 0, 0));
            {   /* EM_GETTHUMB (WM_USER+14): "internal" in the 3.1 DDK's
                   WINDOWS.H (Schulman, Undocumented Windows, ch. 7) */
                DWORD th = SendMessage(c, WM_USER + 14, 0, 0);
                outkv("vthumb", LOWORD(th));
                outkv("hthumb", HIWORD(th));
                /* the same code as a scroll request */
                outkv("vsthumb", (int)SendMessage(c, WM_VSCROLL, WM_USER + 14, 0));
                outkv("hsthumb", (int)SendMessage(c, WM_HSCROLL, WM_USER + 14, 0));
            }
        }
        out("\r\n");
    }
    if (GetMenu(w))
        info_menu(GetMenu(w), 0);
}

/* The whole text of the focus window, into the log. */
static void text_of_focus(void)
{
    HWND w = target();
    WORD n = w ? (WORD)SendMessage(w, WM_GETTEXTLENGTH, 0, 0) : 0;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n + 1);
    LPSTR p = h ? GlobalLock(h) : NULL;
    if (!p) {
        out(" -> failed");
        return;
    }
    SendMessage(w, WM_GETTEXT, n + 1, (LPARAM)p);
    out(" len=");
    outint(n);
    out("\r\nTEXT[");
    emit(p, n);
    out("]");
    GlobalUnlock(h);
    GlobalFree(h);
}

/* ---- commands ---- */

/* One command: the dispatcher shared by script mode and the interactive
   window. The line is split in place. */
static void run_line(char *l)
{
    char *arg = l;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = 0;
    out("SNAP ");
    out(l);
    out(" ");
    out(arg);
    if (same(l, "run")) {
        if (start_program(arg) < 32)
            out(" -> failed");
    } else if (same(l, "wait")) {
        pump(parse_num(arg, 10));
    } else if (same(l, "menu")) {
        PostMessage(top_window(), WM_SYSCOMMAND, SC_KEYMENU, (BYTE)arg[0]);
        pump(200);
    } else if (same(l, "cmd")) {
        HWND w = top_window();
        char cls[32];
        cls[0] = 0;
        if (w)
            GetClassName(w, cls, sizeof cls);
        out(" -> ");
        out(cls);
        if (!PostMessage(w, WM_COMMAND, parse_num(arg, 10), 0))
            out(" (post failed)");
        pump(300);
    } else if (same(l, "close")) {
        /* WM_SYSCOMMAND SC_CLOSE to the active top-level window */
        HWND w = top_window();
        if (w)
            PostMessage(w, WM_SYSCOMMAND, SC_CLOSE, 0);
        else
            out(" -> no window");
        pump(500);
    } else if (same(l, "childcmd")) {
        /* WM_SYSCOMMAND to the active MDI child (its window menu), e.g.
           F030 maximize, F060 close */
        HWND frame = top_window(), client = frame ? GetWindow(frame, GW_CHILD) : NULL;
        HWND child = client ? (HWND)LOWORD(SendMessage(client, WM_MDIGETACTIVE, 0, 0)) : NULL;
        if (child)
            PostMessage(child, WM_SYSCOMMAND, parse_num(arg, 16), 0);
        else
            out(" -> no MDI child");
        pump(500);
    } else if (same(l, "key")) {
        WORD vk = parse_num(arg, 16);
        PostMessage(target(), WM_KEYDOWN, vk, 1L);
        PostMessage(target(), WM_KEYUP, vk, 0xC0000001L);
        pump(200);
    } else if (same(l, "type")) {
        char *c;
        for (c = arg; *c; c++) {
            PostMessage(target(), WM_CHAR, (BYTE)*c, 1L);
            pump(30);
        }
        pump(200);
    } else if (same(l, "char")) {
        PostMessage(target(), WM_CHAR, (BYTE)arg[0], 1L);
        pump(200);
    } else if (same(l, "mark")) {
        /* label the next observations of tests/apps/thelp.c (its log
           shows "HELP msg 0477 wp=<n>") */
        HWND h = FindWindow("MS_WINHELP", NULL);
        if (h)
            PostMessage(h, WM_USER + 0x77, parse_num(arg, 10), 0);
        pump(200);
    } else if (same(l, "alt")) {
        /* Alt+letter, as a dialog's mnemonics see it: WM_SYSCHAR with
           the context bit (29) set */
        PostMessage(target(), WM_SYSCHAR, (BYTE)arg[0], 0x20000001L);
        pump(200);
    } else if (same(l, "mdown") || same(l, "mmove") || same(l, "mup")) {
        int x, y;
        mouse_args(arg, &x, &y);
        mouse_to(x, y);
        if (same(l, "mdown"))
            ev_add(WM_LBUTTONDOWN, x, y);
        else if (same(l, "mup"))
            ev_add(WM_LBUTTONUP, x, y);
        ev_play();
        pump(300);
    } else if (same(l, "mclick")) {
        int x, y;
        mouse_args(arg, &x, &y);
        mouse_to(x, y);
        ev_add(WM_LBUTTONDOWN, x, y);
        ev_add(WM_LBUTTONUP, x, y);
        ev_play();
        pump(300);
    } else if (same(l, "kdown") || same(l, "kup")) {
        WORD vk = parse_num(arg, 16);
        /* paramL: virtual key, scan code in the high byte (Shift 2A,
           Ctrl 1D; others 0) */
        WORD scan = vk == VK_SHIFT ? 0x2A : vk == VK_CONTROL ? 0x1D : 0;
        ev_add(same(l, "kdown") ? WM_KEYDOWN : WM_KEYUP, vk | (scan << 8), 1);
        ev_play();
        pump(100);
    } else if (same(l, "move")) {
        int v[4], i;
        const char *a = arg;
        HWND w = top_window();
        for (i = 0; i < 4; i++) {
            v[i] = parse_num(a, 10);
            while (*a && *a != ' ')
                a++;
            while (*a == ' ')
                a++;
        }
        if (w)
            MoveWindow(w, v[0], v[1], v[2], v[3], TRUE);
        pump(300);
#ifndef SNAP_WIN2
    } else if (same(l, "shellabout")) {
        shell_about(arg);
    } else if (same(l, "associcon")) {
        assoc_icon(arg);
#endif
    } else if (same(l, "blank")) {
        int v[4], i;
        const char *a = arg;
        HDC dc = GetDC(NULL);
        for (i = 0; i < 4; i++) {
            v[i] = parse_num(a, 10);
            while (*a && *a != ' ')
                a++;
            while (*a == ' ')
                a++;
        }
        PatBlt(dc, v[0], v[1], v[2] - v[0], v[3] - v[1], BLACKNESS);
        ReleaseDC(NULL, dc);
    } else if (same(l, "exitwin")) {
        if (!ExitWindows(0, 0))
            out(" -> refused");
    } else if (same(l, "copyfile")) {
        copy_file(arg);
    } else if (same(l, "cursor")) {
        HCURSOR c = GetCursor();
        POINT pt;
        GetCursorPos(&pt);
        out(" -> at ");
        outint(pt.x);
        out(",");
        outint(pt.y);
        out(" ");
        if (c == LoadCursor(NULL, IDC_ARROW))
            out("arrow");
        else if (!c)
            out("none");
        else {
            out("other ");
            outhex((WORD)c, 4);
        }
#ifndef SNAP_WIN2
    } else if (same(l, "drag") || same(l, "click") || same(l, "dblclick") ||
               same(l, "dragitem") || same(l, "dblitem")) {
        int a, b, c, d;
        char *q = word(arg, &a);
        q = word(q, &b);
        q = word(q, &c);
        word(q, &d);
        if (same(l, "drag")) {
            drag(a, b, c, d);
        } else if (same(l, "click") || same(l, "dblclick")) {
            click(a, b, same(l, "dblclick"));
        } else {
            POINT pt;
            if (!item_point(a, &pt))
                out(" -> no list box item");
            else if (same(l, "dragitem"))
                drag(pt.x, pt.y, b, c);
            else
                click(pt.x, pt.y, 1);
        }
        pump(500);
    } else if (same(l, "place")) {
        /* place CLASS x y cx cy: move a top-level window */
        char *cls = arg;
        int a, b, c, d;
        char *q = arg;
        HWND w;
        while (*q && *q != ' ')
            q++;
        if (*q)
            *q++ = 0;
        q = word(q, &a);
        q = word(q, &b);
        q = word(q, &c);
        word(q, &d);
        w = FindWindow(cls, NULL);
        if (w) {
            ShowWindow(w, SW_SHOWNORMAL);
            MoveWindow(w, a, b, c, d, TRUE);
        } else {
            out(" -> no window");
        }
        pump(500);
#endif
    } else if (same(l, "settime")) {
        char *s = arg;
        if (set_time(&s))
            out(" -> failed");
    } else if (same(l, "setdate")) {
        char *s = arg;
        WORD m = next_num(&s, 10), d = next_num(&s, 10), y = next_num(&s, 10);
        if (dos_setdate(y, (m << 8) | d))
            out(" -> failed");
    } else if (same(l, "freeze")) {
        char *s = arg;
        WORD i, n;
        set_time(&s);
        n = next_num(&s, 10) / 50;
        for (i = 0; i < n; i++) {
            s = arg;
            set_time(&s);
            pump(50);
        }
        s = arg;
        set_time(&s);
    } else if (same(l, "syscmd")) {
        PostMessage(top_window(), WM_SYSCOMMAND, parse_num(arg, 16), 0);
        pump(300);
    } else if (same(l, "movewin")) {
        char *s = arg;
        int x = next_num(&s, 10), y = next_num(&s, 10);
        int cx = next_num(&s, 10), cy = next_num(&s, 10);
        MoveWindow(top_window(), x, y, cx, cy, TRUE);
        pump(300);
    } else if (same(l, "post") || same(l, "send")) {
        char *s = arg;
        WORD msg = next_num(&s, 16), wp = next_num(&s, 16), lh, ll;
        LONG r;
        ll = next_num(&s, 16);
        lh = 0;
        if (*s == ':') {                /* lp as hi:lo */
            lh = ll;
            ll = next_num(&s, 16);
        }
        if (l[0] == 'p') {
            PostMessage(top_window(), msg, wp, MAKELONG(ll, lh));
        } else {
            r = SendMessage(top_window(), msg, wp, MAKELONG(ll, lh));
            out(" -> ");
            outhex(r, 8);
        }
        pump(300);
    } else if (same(l, "timers")) {
        UINT id;
        while (ntimers < MAXTIMERS && (id = SetTimer(NULL, 0, 60000u, NULL)) != 0)
            timer_ids[ntimers++] = id;
        out(" -> ");
        outint(ntimers);
    } else if (same(l, "freetimers")) {
        while (ntimers)
            KillTimer(NULL, timer_ids[--ntimers]);
#ifndef SNAP_WIN2
    } else if (same(l, "dumpmenu")) {
        /* the active window's menu bar and system menu: each item's
           ID, state flags and text */
        HWND w = top_window();
        out("\r\n");
        dump_menu(GetMenu(w), 0);
        out("MENU system\r\n");
        dump_menu(GetSystemMenu(w, FALSE), 1);
#endif
    } else if (same(l, "info")) {
        info();
    } else if (same(l, "text")) {
        text_of_focus();
    } else if (same(l, "blinktime")) {
        /* blinktime <ms>: SetCaretBlinkTime for the session, e.g. 65535 so
           a caret keeps the phase it starts in (screenshots that do not
           depend on the moment they are taken) */
        SetCaretBlinkTime(parse_num(arg, 10));
    } else if (same(l, "focus")) {
        /* which window has the focus: class, ID, text */
        HWND f = GetFocus();
        char cls[32], text[64];
        cls[0] = text[0] = 0;
        if (f) {
            GetClassName(f, cls, sizeof cls);
            SendMessage(f, WM_GETTEXT, sizeof text, (LPARAM)(LPSTR)text);
        }
        out(" -> ");
        out(cls);
        out(" id=");
        outhex(f ? GetWindowWord(f, GWW_ID) : 0, 4);
        out(" \"");
        out(text);
        out("\"");
    } else if (same(l, "clickid")) {
        /* clickid <hex id>: a left click (posted) in the middle of that
           control of the active dialog (group boxes do not get in the way) */
        HWND top = top_window(), w = top ? GetDlgItem(top, parse_num(arg, 16)) : NULL;
        if (w) {
            RECT r;
            LPARAM lp;
            GetClientRect(w, &r);
            lp = MAKELPARAM(r.right / 2, r.bottom / 2);
            PostMessage(w, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            PostMessage(w, WM_LBUTTONUP, 0, lp);
        } else {
            out(" -> no such control");
        }
        pump(300);
    } else if (same(l, "dump")) {
        out("\r\n");
        dump_tree(GetWindow(GetDesktopWindow(), GW_CHILD), 0);
    } else if (same(l, "snap")) {
        if (!snap(arg))
            out(" -> failed");
    } else {
        out(" -> unknown command");
    }
    out("\r\n");
}

/* ---- script files ---- */

#ifndef SNAP_WIN2
static void gui_line(char *l);
#endif

/* A script line: blank lines and ';' comments are skipped. */
static void do_line(char *l)
{
    if (!*l || *l == ';')
        return;
#ifndef SNAP_WIN2
    if (g_gui) {
        gui_line(l);
        return;
    }
#endif
    run_line(l);
}

/* Run every line of a script file, one command per line (CR, LF or both
   end a line; a NUL byte ends the script). The file is read as it runs,
   so its size is not bounded; a line is kept in global memory that grows
   as needed, up to one segment: a line longer than LINE_MAX characters is
   cut there. Returns 0 if the file cannot be opened. */
#define LINE_MAX 0xFFF0u
static char chunk[512];                 /* read buffer, not a bound */

static int run_file(const char FAR *path)
{
    HFILE f = _lopen(path, OF_READ);
    HGLOBAL h;
    char FAR *l;
    WORD len = 0, cap = 256;
    int n, i, end = 0;
    if (f == HFILE_ERROR)
        return 0;
    h = GlobalAlloc(GMEM_MOVEABLE, cap);
    l = h ? (char FAR *)GlobalLock(h) : NULL;
    if (!l) {
        if (h)
            GlobalFree(h);
        _lclose(f);
        out("SNAP: out of memory\r\n");
        return 1;
    }
    while (!end && !g_stop) {
        n = (int)_lread(f, chunk, sizeof chunk);
        if (n <= 0)
            break;
        for (i = 0; i < n && !g_stop; i++) {
            char c = chunk[i];
            if (c == 0) {
                end = 1;
                break;
            }
            if (c == '\r' || c == '\n') {
                l[len] = 0;
                do_line(l);
                len = 0;
                continue;
            }
            if (len + 1 >= cap && cap < LINE_MAX + 1) {
                WORD ncap = cap < LINE_MAX / 2 ? cap * 2 : LINE_MAX + 1;
                HGLOBAL g;
                GlobalUnlock(h);
                g = GlobalReAlloc(h, ncap, GMEM_MOVEABLE);
                if (g) {
                    h = g;
                    cap = ncap;
                }
                l = (char FAR *)GlobalLock(h);
            }
            if (len + 1 < cap)
                l[len++] = c;
        }
    }
    if (len && !g_stop) {
        l[len] = 0;
        do_line(l);
    }
    GlobalUnlock(h);
    GlobalFree(h);
    _lclose(f);
    return 1;
}

#ifndef SNAP_WIN2
/* ---- the interactive window (GUI mode, Windows 3.1 build) ----

   A dialog template of class SnapWindow (snap.rc) is the main window, so
   the dialog manager gives it Tab, Enter (Execute) and the mnemonics. The
   commands run through run_line, as in script mode; what they log goes to
   the log pane instead of TK.LOG. Commands that act on "the active
   window" act on the target: the window that was active before SNAP's
   window was (or one picked with Pick), which SNAP activates before a
   command that acts on a window (see the header comment for which, and
   when SNAP takes the activation back). SNAP's window hides while a snap
   command takes the screen. */

extern WORD __cdecl get_ahincr(void);   /* start.asm: KERNEL's __AHINCR */

static HINSTANCE g_inst;
static HWND g_cmd, g_log, g_target;
static int g_busy, g_quit, g_picking;

/* The whole log, in global memory that grows as needed (no bound but
   memory; if it runs out, the rest is still shown but not kept). The log
   pane shows the most recent part: its edit control keeps the text in
   one segment, so past PANE_MAX characters the oldest lines leave the
   pane. Save Log writes the whole log. */
#define PANE_MAX 32000u
static HGLOBAL g_logmem;
static DWORD g_loglen, g_logcap;
static int g_lognomem;
static char pend[512];                  /* text on its way to the pane */
static WORD npend;

static void log_keep(const char FAR *s, WORD n)
{
    char FAR *b;
    DWORD off;
    if (g_lognomem || !n)
        return;
    if (g_loglen + n > g_logcap) {
        DWORD cap = g_logcap ? g_logcap : 8192;
        HGLOBAL h;
        while (cap < g_loglen + n)
            cap += cap;
        h = g_logmem ? GlobalReAlloc(g_logmem, cap, GMEM_MOVEABLE) :
                       GlobalAlloc(GMEM_MOVEABLE, cap);
        if (!h) {
            g_lognomem = 1;
            return;
        }
        g_logmem = h;
        g_logcap = cap;
    }
    b = (char FAR *)GlobalLock(g_logmem);
    if (!b)
        return;
    /* the huge address of the end: selector tiles __AHINCR apart */
    off = g_loglen;
    hmemcpy((void __huge *)MAKELP(SELECTOROF(b) + HIWORD(off) * get_ahincr(), LOWORD(off)),
            (const void __huge *)s, n);
    GlobalUnlock(g_logmem);
    g_loglen += n;
}

static void gui_flush(void)
{
    WORD len;
    if (!npend || !g_log)
        return;
    pend[npend] = 0;
    len = (WORD)GetWindowTextLength(g_log);
    if ((DWORD)len + npend > PANE_MAX) {
        /* make room: whole lines from the top, a quarter of the pane more
           than needed so this is not done for every line */
        WORD cut = len + npend - PANE_MAX + PANE_MAX / 4;
        LONG k;
        if (cut > len)
            cut = len;
        k = SendMessage(g_log, EM_LINEINDEX,
                        (WPARAM)SendMessage(g_log, EM_LINEFROMCHAR, cut, 0) + 1, 0);
        if (k < 0 || k > (LONG)len)
            k = len;
        SendMessage(g_log, EM_SETSEL, 0, MAKELONG(0, (WORD)k));
        SendMessage(g_log, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"");
        len = (WORD)GetWindowTextLength(g_log);
    }
    SendMessage(g_log, EM_SETSEL, 0, MAKELONG(len, len));
    SendMessage(g_log, EM_REPLACESEL, 0, (LPARAM)(LPSTR)pend);
    npend = 0;
}

static void gui_emit(const char FAR *s, WORD n)
{
    WORD i;
    log_keep(s, n);
    for (i = 0; i < n; i++) {
        if (npend >= sizeof pend - 1)
            gui_flush();
        if (npend >= sizeof pend - 1)   /* no pane yet: keep the newest */
            npend = 0;
        pend[npend++] = s[i] ? s[i] : ' ';      /* the pane's text ends at a NUL */
    }
}

/* the first word of a command line is v */
static int verb_is(const char FAR *l, const char *v)
{
    while (*v && *l == *v) {
        l++;
        v++;
    }
    return !*v && (!*l || *l == ' ');
}

/* the commands that do not act on a window: the target stays as it is
   (run: the program started becomes the active window, as from Program
   Manager) */
static int no_window(const char FAR *l)
{
    static const char *const v[] = { "run", "wait", "settime", "setdate", "freeze",
                                     "timers", "freetimers", "mark", "dump" };
    int i;
    for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++)
        if (verb_is(l, v[i]))
            return 1;
    return 0;
}

/* the commands that only look at the target: SNAP takes the activation
   back afterwards, so what they logged is in view (the others leave the
   target active, to see what they did and go on with it) */
static int looks(const char FAR *l)
{
    return verb_is(l, "info") || verb_is(l, "text") || verb_is(l, "dumpmenu") ||
           verb_is(l, "snap");
}

static HWND top_of(HWND h)
{
    HWND p;
    while (h && (GetWindowLong(h, GWL_STYLE) & WS_CHILD) && (p = GetParent(h)) != NULL)
        h = p;
    return h;
}

static int ours(HWND h)
{
    return GetWindowTask(h) == GetCurrentTask();
}

/* The window commands act on; NULL: SNAP's own (the active one). */
static void set_target(HWND h)
{
    char text[80], buf[160];
    h = top_of(h);
    if (h && (!IsWindow(h) || ours(h) || h == GetDesktopWindow()))
        h = NULL;
    g_target = h;
    if (!h) {
        lstrcpy(buf, "(none: SNAP's own window)");
    } else {
        /* only this label is shortened: the class name and up to 79
           characters of the window's text */
        GetClassName(h, buf, 64);
        text[0] = 0;
        SendMessage(h, WM_GETTEXT, sizeof text, (LPARAM)(LPSTR)text);
        lstrcat(buf, " \"");
        lstrcat(buf, text);
        lstrcat(buf, "\"");
    }
    SetDlgItemText(g_dlg, IDC_TARGET, buf);
}

/* activate the target, as a click on it would */
static void to_target(void)
{
    if (g_target && !IsWindow(g_target))
        set_target(NULL);
    if (g_target && GetActiveWindow() != g_target) {
        SetActiveWindow(g_target);
        pump(100);
    }
}

static void set_busy(int b)
{
    static const int ids[] = { IDC_PICK, IDC_SNAP, IDC_DUMP, IDC_INFO, IDC_RUN };
    HMENU m = GetMenu(g_dlg);
    int i;
    g_busy = b;
    if (b)
        g_stop = 0;
    for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
        EnableWindow(GetDlgItem(g_dlg, ids[i]), !b);
    EnableMenuItem(m, IDM_OPEN, b ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(m, IDM_CLEAR, b ? MF_GRAYED : MF_ENABLED);
    SetDlgItemText(g_dlg, IDOK, b ? "&Stop" : "E&xecute");
}

static void not_busy(void)
{
    gui_flush();
    set_busy(0);
    if (g_quit)
        DestroyWindow(g_dlg);
}

/* one command, typed or from a script: SNAP stays out of its own
   screenshots */
static void gui_line(char *l)
{
    int hide = verb_is(l, "snap") && IsWindowVisible(g_dlg);
    HWND act = GetActiveWindow();
    if (hide) {
        ShowWindow(g_dlg, SW_HIDE);
        pump(500);                      /* the windows beneath repaint */
    }
    run_line(l);
    if (hide) {
        ShowWindow(g_dlg, SW_SHOWNA);
        /* the activation stays where it was (a script's next line acts
           on it) */
        if (act && act != g_dlg && IsWindow(act) && GetActiveWindow() != act)
            SetActiveWindow(act);
    }
    gui_flush();
}

/* Run one command line (on a copy: run_line splits it in place). */
static void gui_command(const char FAR *cmd)
{
    WORD n = 0;
    HGLOBAL h;
    char FAR *l;
    int back;
    if (g_busy)
        return;
    while (cmd[n])
        n++;
    h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n + 1);
    l = h ? (char FAR *)GlobalLock(h) : NULL;
    if (!l) {
        if (h)
            GlobalFree(h);
        out("SNAP: out of memory\r\n");
        gui_flush();
        return;
    }
    hmemcpy((void __huge *)l, (const void __huge *)cmd, (long)n + 1);
    set_busy(1);
    if (!no_window(l))
        to_target();
    back = looks(l);
    do_line(l);
    if (back && g_dlg && IsWindowVisible(g_dlg))
        SetActiveWindow(g_dlg);
    GlobalUnlock(h);
    GlobalFree(h);
    not_busy();
}

/* the command line's text, run */
static void gui_execute(void)
{
    WORD n = (WORD)GetWindowTextLength(g_cmd);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n + 1);
    char FAR *l = h ? (char FAR *)GlobalLock(h) : NULL;
    if (!l) {
        if (h)
            GlobalFree(h);
        return;
    }
    GetWindowText(g_cmd, l, n + 1);
    gui_command(l);
    GlobalUnlock(h);
    GlobalFree(h);
    if (g_dlg)
        SendMessage(g_cmd, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));     /* Enter repeats it */
}

static void gui_script(const char FAR *path)
{
    if (g_busy)
        return;
    set_busy(1);
    out("SNAP script ");
    out(path);
    out("\r\n");
    to_target();
    if (!run_file(path)) {
        out("SNAP: cannot open ");
        out(path);
        out("\r\n");
    } else {
        out(g_stop ? "SNAP stopped\r\n" : "SNAP done\r\n");
    }
    not_busy();
}

/* SNAPnnnn.BMP in the current directory, the first name not taken: the
   number is kept as decimal text and counts up; past 9999 it takes
   letters of the prefix (SNA12345.BMP), up to 8 digits (the 8.3 name). */
static char snapnum[9] = "0000";

static int next_snap_name(char *name)
{
    for (;;) {
        int k = lstrlen(snapnum), i;
        HFILE f;
        for (i = k - 1; i >= 0 && snapnum[i] == '9'; i--)
            snapnum[i] = '0';
        if (i >= 0) {
            snapnum[i]++;
        } else if (k < 8) {
            for (i = k + 1; i > 0; i--)
                snapnum[i] = snapnum[i - 1];
            snapnum[0] = '1';
        } else {
            return 0;
        }
        k = lstrlen(snapnum);
        lstrcpy(name, "SNAP");
        name[8 - k < 4 ? 8 - k : 4] = 0;
        lstrcat(name, snapnum);
        lstrcat(name, ".BMP");
        f = _lopen(name, OF_READ);
        if (f == HFILE_ERROR)
            return 1;
        _lclose(f);
    }
}

/* COMMDLG and SHELL are loaded when needed, so that script mode imports
   neither (it runs where they may be missing, or w16's own). */
typedef BOOL (FAR PASCAL *FILEDLGPROC)(OPENFILENAME FAR *);
typedef int (FAR PASCAL *ABOUTPROC)(HWND, LPCSTR, LPCSTR, HICON);

#define PATH_MAX 260                    /* DOS paths are shorter */
static char g_path[PATH_MAX];

static int file_dialog(int save, const char *title, const char *filter, const char *ext)
{
    HINSTANCE lib = LoadLibrary("COMMDLG.DLL");
    FILEDLGPROC f;
    OPENFILENAME o;
    BYTE *z = (BYTE *)&o;
    int ok = 0, i;
    if ((UINT)lib < 32) {
        out("SNAP: no COMMDLG.DLL\r\n");
        gui_flush();
        return 0;
    }
    f = (FILEDLGPROC)GetProcAddress(lib, save ? "GetSaveFileName" : "GetOpenFileName");
    if (f) {
        for (i = 0; i < (int)sizeof o; i++)
            z[i] = 0;
        o.lStructSize = sizeof o;
        o.hwndOwner = g_dlg;
        o.lpstrFilter = filter;
        o.nFilterIndex = 1;
        o.lpstrFile = g_path;
        o.nMaxFile = sizeof g_path;
        o.lpstrTitle = title;
        o.lpstrDefExt = ext;
        o.Flags = OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        ok = f(&o);
    }
    FreeLibrary(lib);
    return ok;
}

static void save_log(void)
{
    HFILE f;
    char FAR *b;
    lstrcpy(g_path, "SNAP.LOG");
    if (!file_dialog(1, "Save Log", "Logs (*.log)\0*.log\0All Files (*.*)\0*.*\0", "log"))
        return;
    f = _lcreat(g_path, 0);
    b = g_logmem ? (char FAR *)GlobalLock(g_logmem) : NULL;
    if (f == HFILE_ERROR || (g_loglen && (!b || _hwrite(f, (const void __huge *)b,
                                                       (long)g_loglen) != (long)g_loglen))) {
        out("SNAP: cannot write ");
        out(g_path);
        out("\r\n");
    }
    if (b)
        GlobalUnlock(g_logmem);
    if (f != HFILE_ERROR)
        _lclose(f);
    gui_flush();
}

static void about(void)
{
    HINSTANCE lib = LoadLibrary("SHELL.DLL");
    ABOUTPROC f = (UINT)lib >= 32 ? (ABOUTPROC)GetProcAddress(lib, "ShellAbout") : NULL;
    if (f)
        f(g_dlg, "SNAP", "w16's test driver: scripted input and screenshots",
          LoadIcon(g_inst, "SNAP"));
    else
        out("SNAP: no SHELL.DLL\r\n");
    if ((UINT)lib >= 32)
        FreeLibrary(lib);
    gui_flush();
}

static const char *const help[] = {
    "run <command line>       start a program (WinExec)",
    "wait <ms>                keep dispatching messages for <ms> ms",
    "menu <letter>            open the window's menu (Alt+letter)",
    "close                    WM_SYSCOMMAND SC_CLOSE to the window",
    "childcmd <hex>           WM_SYSCOMMAND to its active MDI child",
    "cmd <decimal id>         post WM_COMMAND id to the window",
    "key <vk-hex>             a key press to the focus window",
    "char <c>                 a WM_CHAR to the focus window",
    "alt <c>                  Alt+c (WM_SYSCHAR): dialog mnemonics",
    "type <text>              a WM_CHAR for each character",
    "snap <file.bmp>          save the whole screen as a 4-bit .BMP",
    "dump                     every window: class, text, style, rect",
    "dumpmenu                 the window's menu bar and system menu",
    "info                     the window in detail",
    "text                     the whole text of the focus window",
    "drag x1 y1 x2 y2         drag with the left button (screen)",
    "click x y / dblclick x y left-button clicks (screen)",
    "dragitem n x y / dblitem n   list box item n",
    "place CLASS x y cx cy    move the top-level window of that class",
    "movewin x y cx cy        move the window",
    "syscmd <hex>             post WM_SYSCOMMAND <hex> to the window",
    "post <msg> <wp> <lp>     post a message (hex; lp also hi:lo)",
    "send <msg> <wp> <lp>     send it and log the result",
    "settime hh:mm:ss[.cc]    set the DOS time",
    "setdate mm/dd/yyyy       set the DOS date",
    "freeze hh:mm:ss <ms>     hold the DOS time there for <ms> ms",
    "timers / freetimers      take every timer USER has / give back",
    "mark <n>                 label the next THELP observations",
    "; text                   a comment",
    "\"The window\" is the target: the one active before SNAP's",
    "(or picked with Pick); SNAP activates it before each command.",
};

static void list_commands(void)
{
    int i;
    out("SNAP commands:\r\n");
    for (i = 0; i < (int)(sizeof help / sizeof help[0]); i++) {
        out("  ");
        out(help[i]);
        out("\r\n");
    }
    gui_flush();
}

static void end_pick(void)
{
    if (g_picking) {
        g_picking = 0;
        ReleaseCapture();
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
}

/* Positions of the controls for the client size: dialog units through
   the dialog's font, as in the template. */
static void layout(int cx, int cy)
{
    RECT u;
    int bx, by, m, bw, bh, eh, right, ex;
    u.left = u.top = 0;
    u.right = 4;
    u.bottom = 8;
    MapDialogRect(g_dlg, &u);
    bx = u.right;                       /* pixels per 4 dialog units */
    by = u.bottom;                      /* pixels per 8 */
    m = bx;                             /* 4 units */
    bw = bx * 13;                       /* 52 units */
    bh = by * 14 / 8;
    eh = by * 12 / 8;
    ex = bx * 21 / 2;                   /* 42 units */
    right = cx - m - bw;
    if (right < bx * 24)
        right = bx * 24;
#define MOVE(id, x, y, w, h) MoveWindow(GetDlgItem(g_dlg, id), x, y, (w) > 0 ? (w) : 0, h, TRUE)
    MOVE(IDC_CMDLABEL, m, by * 6 / 8, bx * 9, by);
    MOVE(IDC_CMD, ex, by * 4 / 8, right - bx - ex, eh);
    MOVE(IDOK, right, by * 3 / 8, bw, bh);
    MOVE(IDC_TGTLABEL, m, by * 22 / 8, bx * 9, by);
    MOVE(IDC_TARGET, ex, by * 22 / 8, right - bx - ex, by);
    MOVE(IDC_PICK, right, by * 19 / 8, bw, bh);
    MOVE(IDC_SNAP, m, by * 37 / 8, bx * 23 / 2, bh);
    MOVE(IDC_DUMP, bx * 27 / 2, by * 37 / 8, bx * 23 / 2, bh);
    MOVE(IDC_INFO, bx * 26, by * 37 / 8, bx * 23 / 2, bh);
    MOVE(IDC_RUN, bx * 77 / 2, by * 37 / 8, bx * 23 / 2, bh);
    MOVE(IDC_LOG, m, by * 55 / 8, cx - 2 * m, cy - by * 55 / 8 - m);
#undef MOVE
}

/* The log pane's text lives in a 64K segment of its own (SDK:
   CreateWindow; Petzold, ch. 6), not in SNAP's data segment; without
   one, SNAP's. */
static HINSTANCE edit_ds(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x10000L);
    LPSTR p = h ? (LPSTR)GlobalLock(h) : NULL;
    if (!p) {
        if (h)
            GlobalFree(h);
        return g_inst;
    }
    if (!LocalInit(SELECTOROF(p), 16, 0xFFFE)) {
        GlobalUnlock(h);
        GlobalFree(h);
        return g_inst;
    }
    return (HINSTANCE)SELECTOROF(p);
}

BOOL CALLBACK __export SnapDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && g_log)
            layout(LOWORD(lp), HIWORD(lp));
        return FALSE;
    case WM_ACTIVATE:
        /* the window SNAP's took over from becomes the target (not one
           of SNAP's own: COMMDLG's dialogs, the About box) */
        if (wp != WA_INACTIVE && LOWORD(lp) && !g_picking && !ours((HWND)LOWORD(lp)))
            set_target((HWND)LOWORD(lp));
        return FALSE;
    case WM_SETCURSOR:
        if (g_picking) {
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            SetWindowLong(h, DWL_MSGRESULT, TRUE);
            return TRUE;
        }
        return FALSE;
    case WM_LBUTTONDOWN:
        if (g_picking) {
            POINT pt;
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            ClientToScreen(h, &pt);
            end_pick();
            set_target(WindowFromPoint(pt));
            out("SNAP target ");
            if (g_target) {
                char cls[64];
                GetClassName(g_target, cls, sizeof cls);
                out(cls);
            } else {
                out("(none)");
            }
            out("\r\n");
            gui_flush();
            return TRUE;
        }
        return FALSE;
    case WM_CANCELMODE:
        end_pick();
        return FALSE;
    case WM_CLOSE:
        if (g_busy)                     /* ends when the command returns */
            g_stop = g_quit = 1;
        else
            DestroyWindow(h);
        return TRUE;
    case WM_DESTROY:
        g_gui = 0;
        g_dlg = NULL;
        PostQuitMessage(0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            if (g_busy)
                g_stop = 1;
            else
                gui_execute();
            return TRUE;
        case IDCANCEL:                  /* Escape */
            if (g_picking)
                end_pick();
            else if (g_busy)
                g_stop = 1;
            return TRUE;
        }
        if (g_busy)
            return TRUE;
        switch (wp) {
        case IDC_PICK:
            g_picking = 1;
            SetCapture(h);
            SetCursor(LoadCursor(NULL, IDC_CROSS));
            out("SNAP pick: click the window commands are to act on (Esc cancels)\r\n");
            gui_flush();
            return TRUE;
        case IDC_SNAP: {
            char cmd[20];
            lstrcpy(cmd, "snap ");
            if (next_snap_name(cmd + 5)) {
                gui_command(cmd);
            } else {
                out("SNAP: no free SNAPnnnn.BMP name\r\n");
                gui_flush();
            }
            return TRUE;
        }
        case IDC_DUMP:
            gui_command("dump");
            return TRUE;
        case IDC_INFO:
            gui_command("info");
            return TRUE;
        case IDC_RUN:
            g_path[0] = 0;
            if (file_dialog(0, "Run",
                            "Programs\0*.exe;*.com;*.pif;*.bat\0All Files (*.*)\0*.*\0",
                            NULL)) {
                static char cmd[PATH_MAX + 4];
                lstrcpy(cmd, "run ");
                lstrcat(cmd, g_path);
                SetWindowText(g_cmd, cmd);
                gui_command(cmd);
            }
            return TRUE;
        case IDM_OPEN:
            g_path[0] = 0;
            if (file_dialog(0, "Open Script", "Scripts (*.txt)\0*.txt\0All Files (*.*)\0*.*\0",
                            NULL))
                gui_script(g_path);
            return TRUE;
        case IDM_SAVE:
            save_log();
            return TRUE;
        case IDM_CLEAR:
            SetWindowText(g_log, "");
            g_loglen = 0;
            g_lognomem = 0;
            return TRUE;
        case IDM_EXIT:
            PostMessage(h, WM_CLOSE, 0, 0);
            return TRUE;
        case IDM_COMMANDS:
            list_commands();
            return TRUE;
        case IDM_ABOUT:
            about();
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

/* SnapWindow: a dialog class of SNAP's own, for its icon */
LRESULT CALLBACK __export SnapWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefDlgProc(h, msg, wp, lp);
}

static int gui(const char *why)
{
    WNDCLASS wc;
    MSG m;
    RECT r;
    HWND prev = GetActiveWindow();
    int show = init_regs[3];            /* DX from InitTask: nCmdShow */

    g_inst = (HINSTANCE)init_regs[I_DI];
    wc.style = 0;
    wc.lpfnWndProc = SnapWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = g_inst;
    wc.hIcon = LoadIcon(g_inst, "SNAP");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "SnapWindow";
    if (!RegisterClass(&wc))
        return 1;
    g_dlg = CreateDialog(g_inst, "SNAP", NULL,
                         (DLGPROC)MakeProcInstance((FARPROC)SnapDlgProc, g_inst));
    if (!g_dlg)
        return 1;
    g_gui = 1;
    g_cmd = GetDlgItem(g_dlg, IDC_CMD);
    SendMessage(g_cmd, EM_LIMITTEXT, 0, 0);     /* the control's own bound only */
    g_log = CreateWindow("Edit", "",
                         WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                         ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                         0, 0, 0, 0, g_dlg, (HMENU)IDC_LOG, edit_ds(), NULL);
    if (g_log) {
        SendMessage(g_log, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FIXED_FONT), 0);
        SendMessage(g_log, EM_LIMITTEXT, 0, 0);
    }
    GetClientRect(g_dlg, &r);
    layout(r.right, r.bottom);
    set_target(prev);
    out("SNAP interactive: type a command and press Enter (Help > Commands)\r\n");
    if (why) {
        out("SNAP: no ");
        out(why);
        out("\r\n");
    }
    ShowWindow(g_dlg, show ? show : SW_SHOWNORMAL);
    UpdateWindow(g_dlg);
    gui_flush();
    SetFocus(g_cmd);
    while (GetMessage(&m, NULL, 0, 0)) {
        if (gui_skip(&m))
            continue;
        if (!g_dlg || !IsDialogMessage(g_dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    return 0;
}
#endif

int __cdecl tmain(void)
{
    static char tail[128];              /* a DOS command tail: 127 at most */
    LPSTR t = (LPSTR)MAKELP(init_regs[6], init_regs[1]);       /* ES:BX */
    int n = 0;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    while (*t == ' ' || *t == '\t')
        t++;
    while (t[n] && t[n] != '\r' && n < (int)sizeof tail - 1) {
        tail[n] = t[n];
        n++;
    }
    while (n && (tail[n - 1] == ' ' || tail[n - 1] == '\t'))
        n--;
    tail[n] = 0;
    /* a script named on the command line, else C:\SNAP.TXT; with neither,
       the interactive window */
    if (!run_file(n ? tail : "C:\\SNAP.TXT")) {
#ifndef SNAP_WIN2
        if (!n)
            return gui(NULL);
        return gui(tail);
#else
        out("SNAP: no ");
        out(n ? tail : "C:\\SNAP.TXT");
        out("\r\n");
        return 1;
#endif
    }
    out("SNAP done\r\n");
#ifdef SNAP_WIN2
    ExitWindows(0, 0);                  /* not the shell on 2.x: end it */
#endif
    return 0;
}
