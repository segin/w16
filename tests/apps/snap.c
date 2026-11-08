/*
 * snap.c - scripted screenshots inside a running Windows (test tool).
 *
 * Runs as the Windows shell (tests/refrun.sh) and executes C:\SNAP.TXT,
 * one command per line:
 *     run <command line>     WinExec it (SW_SHOWNORMAL)
 *     wait <ms>              keep dispatching messages for <ms> ms
 *     menu <letter>          open the active window's menu (Alt+letter)
 *     key <vk-hex>           post a key press to the window with the focus
 *     char <c>               post a WM_CHAR to the window with the focus
 *     snap <file.bmp>        save the whole screen as a 4-bit .BMP
 *     dump                   log every window: class, text, style, rect
 * Lines starting with ';' are comments. Progress and errors go to TK.LOG.
 * When the script ends SNAP exits, and because it is the shell, Windows
 * exits too.
 *
 * Used for black-box observation of the reference Windows (the original
 * Program Manager's layout, for UI fidelity) and for comparing w16's own
 * programs against it pixel by pixel. Only documented APIs are used.
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

/* ---- screenshot ---- */

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD pal[16];
} BMI16;

#define BAND 96                         /* scan lines per GetDIBits call */

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
        GetWindowText(w, text, sizeof text);
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
        out(" exstyle=");
        outhex(GetWindowLong(w, GWL_EXSTYLE), 8);
        out(" id=");
        outhex(GetWindowWord(w, GWW_ID), 4);
        out(" rect=");
        outint(r.left); out(",");
        outint(r.top); out(",");
        outint(r.right); out(",");
        outint(r.bottom);
        out("\r\n");
        c = GetWindow(w, GW_CHILD);
        if (c)
            dump_tree(c, depth + 1);
    }
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
    if (!lstrcmp(l, "run")) {
        if (WinExec(arg, SW_SHOWNORMAL) < 32)
            out(" -> failed");
    } else if (!lstrcmp(l, "wait")) {
        pump(parse_num(arg, 10));
    } else if (!lstrcmp(l, "menu")) {
        PostMessage(GetActiveWindow(), WM_SYSCOMMAND, SC_KEYMENU, (BYTE)arg[0]);
        pump(200);
    } else if (!lstrcmp(l, "key")) {
        WORD vk = parse_num(arg, 16);
        PostMessage(target(), WM_KEYDOWN, vk, 1L);
        PostMessage(target(), WM_KEYUP, vk, 0xC0000001L);
        pump(200);
    } else if (!lstrcmp(l, "char")) {
        PostMessage(target(), WM_CHAR, (BYTE)arg[0], 1L);
        pump(200);
    } else if (!lstrcmp(l, "dump")) {
        out("\r\n");
        dump_tree(GetWindow(GetDesktopWindow(), GW_CHILD), 0);
    } else if (!lstrcmp(l, "snap")) {
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
    return 0;
}
