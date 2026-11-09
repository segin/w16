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
    log_write(p, n);
    out("]");
    GlobalUnlock(h);
    GlobalFree(h);
}

/* ---- script ---- */

static char script[16384];

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
