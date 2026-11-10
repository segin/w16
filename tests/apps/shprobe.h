/*
 * shprobe.h - helpers for the SHELL.DLL probes (SH1, SH2, ...), on top of
 * tklog.h (include that first, with TK_REF defined).
 *
 *   obs_l(name, v)        a LONG: "name" (low word) and, when the high
 *                         word is not 0, "name.hi"
 *   obs_s(name, s)        a string, plus "name.len" and "name.sum" (a
 *                         16-bit checksum) so that obsref.py's tables check
 *                         strings too
 *   obs_b(name, p, n)     n bytes as hex, plus "name.sum"
 *   snapshot(file)        copy the Windows directory's REG.DAT to C:\file
 *                         (black-box observation of the data file)
 *
 * SHELL's functions that shellapi.h does not declare are declared here
 * (names: OpenWatcom's shell.lib and Wine's X11-licensed shell.spec,
 * wine-20020228; ordinals: docs/exports/SHELL.txt).
 */
#include <shellapi.h>

/* task start-up (KERNEL.30, USER.5), not in windows.h */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
HICON FAR PASCAL ExtractAssociatedIcon(HINSTANCE, LPSTR, WORD FAR *);
DWORD FAR PASCAL DoEnvironmentSubst(LPSTR, UINT);
LPSTR FAR PASCAL FindEnvironmentString(LPSTR);
HICON FAR PASCAL InternalExtractIcon(HINSTANCE, LPCSTR, UINT, UINT);
BOOL FAR PASCAL RegisterShellHook(HWND, UINT);

WORD strsum(const char FAR *s, WORD n)
{
    WORD sum = 0, i;
    for (i = 0; i < n; i++)
        sum = (WORD)((sum << 1 | sum >> 15) + (BYTE)s[i]);
    return sum;
}

WORD slen(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    return n;
}

void suffix(char *dst, const char *name, const char *suf)
{
    while (*name)
        *dst++ = *name++;
    while (*suf)
        *dst++ = *suf++;
    *dst = 0;
}

void obs_l(const char *name, LONG v)
{
    char n[64];
    obs(name, LOWORD(v));
    if (HIWORD(v)) {
        suffix(n, name, ".hi");
        obs(n, HIWORD(v));
    }
}

void obs_s(const char *name, const char FAR *s)
{
    char n[64];
    WORD len = slen(s);
    obs_str(name, s);
    suffix(n, name, ".len");
    obs(n, len);
    suffix(n, name, ".sum");
    obs(n, strsum(s, len));
}

void obs_b(const char *name, const BYTE FAR *p, WORD len)
{
    char n[64], b[4];
    WORD i;
    out("OBS ");
    out(name);
    out("=\"");
    for (i = 0; i < len; i++) {
        b[0] = "0123456789ABCDEF"[p[i] >> 4];
        b[1] = "0123456789ABCDEF"[p[i] & 15];
        b[2] = 0;
        out(b);
    }
    out("\"\r\n");
    suffix(n, name, ".sum");
    obs(n, strsum((const char FAR *)p, len));
}

/* Copy WINDOWS\REG.DAT to C:\<file>. */
void snapshot(const char *file)
{
    static char path[160], dst[32], buf[512];
    HFILE in, o;
    UINT n;
    GetWindowsDirectory(path, sizeof path - 10);
    lstrcat(path, "\\REG.DAT");
    lstrcpy(dst, "C:\\");
    lstrcat(dst, file);
    in = _lopen(path, OF_READ);
    if (in == HFILE_ERROR) {
        out("snapshot: no REG.DAT\r\n");
        return;
    }
    o = _lcreat(dst, 0);
    while ((n = _lread(in, buf, sizeof buf)) != 0 && n != (UINT)-1)
        _lwrite(o, buf, n);
    _lclose(o);
    _lclose(in);
    out("snapshot ");
    out(file);
    out("\r\n");
}

/* ---- windows: a dump (class, text, style, id, rectangle relative to
   the top window's client area, font) and a screenshot ---- */

void outint(int v)
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

void dump_win(HWND top, HWND w, int depth)
{
    static char cls[40], text[200];
    RECT r;
    HFONT f;
    LOGFONT lf;
    int i;
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        GetClassName(w, cls, sizeof cls);
        GetWindowText(w, text, sizeof text);
        GetWindowRect(w, &r);
        if (depth)
            MapWindowPoints(NULL, top, (POINT FAR *)&r, 2);
        out("WIN ");
        for (i = 0; i < depth; i++)
            out("  ");
        out(cls);
        out(" \"");
        out(text);
        out("\" style=");
        outhex(HIWORD(GetWindowLong(w, GWL_STYLE)));
        outhex(LOWORD(GetWindowLong(w, GWL_STYLE)));
        out(" id=");
        outhex(GetWindowWord(w, GWW_ID));
        out(" rect=");
        outint(r.left); out(","); outint(r.top); out(",");
        outint(r.right); out(","); outint(r.bottom);
        f = (HFONT)(WORD)SendMessage(w, WM_GETFONT, 0, 0);
        if (f && GetObject(f, sizeof lf, (LPSTR)&lf)) {
            out(" font=");
            out(lf.lfFaceName);
            out(",");
            outint(lf.lfHeight);
            out(",");
            outint(lf.lfWeight);
        }
        out("\r\n");
        if (!depth)
            dump_win(w, GetWindow(w, GW_CHILD), 1);
        else if (GetWindow(w, GW_CHILD))
            dump_win(top, GetWindow(w, GW_CHILD), depth + 1);
        if (!depth)
            break;
    }
}

#define SNAP_BAND 96
typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD pal[16];
} SNAPBMI;

/* the whole screen as a 4-bit .BMP (as tests/apps/snap.c) */
int snap(const char *file)
{
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy);
    HBITMAP old;
    HGLOBAL hbuf = GlobalAlloc(GMEM_MOVEABLE, (WORD)(stride * SNAP_BAND));
    BYTE FAR *buf = hbuf ? (BYTE FAR *)GlobalLock(hbuf) : NULL;
    static SNAPBMI bmi;
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
    bmi.h.biSizeImage = 0;
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;
    f = _lcreat(file, 0);
    if (f == HFILE_ERROR)
        goto done;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    _llseek(f, fh.bfOffBits, 0);
    ok = 1;
    for (y = 0; y < cy; y += SNAP_BAND) {
        int n = cy - y < SNAP_BAND ? cy - y : SNAP_BAND;
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
    out("snap ");
    out(file);
    out(ok ? "\r\n" : " failed\r\n");
    return ok;
}

void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    do {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0);
}
