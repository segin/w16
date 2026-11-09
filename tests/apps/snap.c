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
 * Lines starting with ';' are comments. Progress and errors go to TK.LOG.
 * When the script ends SNAP exits, and because it is the shell, Windows
 * exits too.
 *
 * Used for black-box observation of the reference Windows (the original
 * Program Manager's layout, for UI fidelity) and for comparing w16's own
 * programs against it pixel by pixel. Only documented APIs are used.
 *
 * SNAP_WIN2 builds SNAP2.EXE for Windows 2.x (tests/w386run.sh): programs
 * start through LoadModule, screenshots are device-dependent bitmaps
 * (GetBitmapBits, no DIBs before 3.0) written as .DDB files that
 * tools/ddb2png.py converts, and the session ends with ExitWindows when
 * the script is done.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
/* Task start-up calls every Windows program makes (KERNEL.30, USER.5);
   not declared in windows.h. */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0);
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

static char script[4096];

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

int __cdecl tmain(void)
{
    HFILE f;
    int n;
    char *p, *e;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    f = _lopen("C:\\SNAP.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        out("SNAP: no C:\\SNAP.TXT\r\n");
        return 1;
    }
    n = _lread(f, script, sizeof script - 1);
    _lclose(f);
    if (n < 0)
        n = 0;
    script[n] = 0;
    for (p = script; *p; p = e) {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        if (*p && *p != ';')
            run_line(p);
    }
    out("SNAP done\r\n");
#ifdef SNAP_WIN2
    ExitWindows(0, 0);                  /* not the shell on 2.x: end it */
#endif
    return 0;
}
