/*
 * desktop.c - the Desktop applet: pattern, fast Alt+Tab, screen saver,
 * wallpaper, icon spacing and title wrap, sizing grid, border width,
 * cursor blink rate; and the Edit Pattern dialog.
 *
 * Observed on Windows 3.1 (tests/scripts/cpl/dk*.txt):
 * - Keys: WIN.INI [windows] CoolSwitch, ScreenSaveActive,
 *   ScreenSaveTimeOut, BorderWidth, CursorBlinkRate; WIN.INI [Desktop]
 *   Pattern, Wallpaper, IconSpacing, IconTitleWrap, TileWallPaper,
 *   GridGranularity; SYSTEM.INI [boot] SCRNSAVE.EXE.
 * - OK with nothing changed writes CoolSwitch=1 ([windows]), IconSpacing=75
 *   and TileWallPaper=1 ([Desktop]; appended in that order when absent:
 *   the defaults) and SCRNSAVE.EXE=(None); IconTitleWrap only when changed
 *   (written between IconSpacing and TileWallPaper). The other keys are
 *   rewritten with their values:
 *   Pattern (the bits, "170 85 ...", or "(None)"), Wallpaper (the file
 *   name as listed, lower case), ScreenSaveActive (1 with a saver),
 *   ScreenSaveTimeOut (seconds), GridGranularity, BorderWidth,
 *   CursorBlinkRate. SCRNSAVE.EXE is the saver's full path
 *   (C:\WINDOWS\SCRNSAVE.SCR).
 * - Ranges, checked on OK with a "Control Panel" message (information):
 *   border width 1-50, icon spacing 32-512, granularity 0-49; a delay of
 *   0 minutes is taken.
 * - Cursor blink rate 200-1200 ms, 20 per step of the bar, 250 per page;
 *   Home is the Fast end (200), End the Slow one, as on the Mouse
 *   applet's bars. The bar starts at WIN.INI CursorBlinkRate (530 when
 *   absent), whatever the caret's rate is; every move of it sets the
 *   caret's rate at once (SetCaretBlinkTime), and Cancel leaves the last
 *   one (dkblk*.txt). The sample left of the bar is inverted at that rate:
 *   yellow on a navy window, black on white (dkcolors.txt).
 * - Patterns: CONTROL.INI [Patterns] (name=8 numbers), "(None)" first,
 *   sorted. Screen savers: the Windows directory's *.SCR files, listed by
 *   the name in their module description ("SCRNSAVE : Blank Screen").
 *   Wallpapers: its *.BMP files, lower case, after "(None)".
 * UNVERIFIED: the order in which absent keys other than the three above
 * are appended; the saver's Test and Setup command lines (/s, /c).
 */
#include "maincpl.h"
#include "cplutil.h"

static char s_win[] = "windows";
static char s_desk[] = "Desktop";
static char s_none[] = "(None)";
static char s_cpl_ini[] = "CONTROL.INI";
static char s_patterns[] = "Patterns";

static int blink;
static BOOL wrap0;

/* ---- helpers ---- */

static int windir(char *buf, int size)
{
    int n = GetWindowsDirectory(buf, size - 14);
    if (n && buf[n - 1] != '\\')
        buf[n++] = '\\';
    buf[n] = 0;
    return n;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s += 'a' - 'A';
}

/* a screen saver's name: its NE module description after "SCRNSAVE :"
   (cpl_ne_desc: the non-resident name table's first entry) */
static BOOL saver_name(const char *path, char *name, int size)
{
    char desc[80], *p;
    if (!cpl_ne_desc(path, desc, sizeof desc))
        return FALSE;
    for (p = desc; *p && *p != ':'; p++)
        ;
    if (!*p)
        return FALSE;
    for (p++; *p == ' '; p++)
        ;
    lstrcpyn(name, p, size);
    return TRUE;
}

/* ---- the lists ---- */

static HLOCAL section_keys(const char *sec)
{
    UINT size = 512;
    for (;;) {
        HLOCAL h = LocalAlloc(LMEM_FIXED, size);
        int n;
        if (!h)
            return NULL;
        n = GetPrivateProfileString(sec, NULL, "", LocalLock(h), size, s_cpl_ini);
        LocalUnlock(h);
        if ((UINT)n < size - 2 || size >= 0x8000)
            return h;
        LocalFree(h);
        size *= 2;
    }
}

/* the text of a window, or of item i of a combo box, in a local block
   (LMEM_FIXED) the caller frees; NULL if there is none. CONTROL.INI's
   pattern names have no length limit (cpl audit H1: a 124-character name
   is listed, chosen and edited on 3.1). */
static HLOCAL win_text(HWND w)
{
    int n = GetWindowTextLength(w);
    HLOCAL h = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n + 1);
    if (h) {
        GetWindowText(w, LocalLock(h), n + 1);
        LocalUnlock(h);
    }
    return h;
}

static HLOCAL item_text(HWND cb, int i)
{
    int n = (int)SendMessage(cb, CB_GETLBTEXTLEN, i, 0);
    HLOCAL h;
    if (n < 0)
        return NULL;
    h = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n + 1);
    if (h) {
        SendMessage(cb, CB_GETLBTEXT, i, (LPARAM)(LPSTR)LocalLock(h));
        LocalUnlock(h);
    }
    return h;
}

/* a pattern's bits from CONTROL.INI, "(None)" for item "(None)" or none */
static void pattern_bits(HWND cb, int i, char *bits, int size)
{
    HLOCAL h = i >= 0 ? item_text(cb, i) : NULL;
    lstrcpy(bits, s_none);
    if (h) {
        const char *name = LocalLock(h);
        if (lstrcmpi(name, s_none))
            GetPrivateProfileString(s_patterns, name, s_none, bits, size, s_cpl_ini);
        LocalUnlock(h);
        LocalFree(h);
    }
}

static void fill_patterns(HWND cb, const char *current)
{
    HLOCAL keys;
    char bits[64], v[64];
    int sel = -1, none;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)s_none);
    if ((keys = section_keys(s_patterns)) != NULL) {
        const char *k;
        for (k = (const char *)LocalLock(keys); *k; k += lstrlen(k) + 1) {
            if (!lstrcmpi(k, s_none))
                continue;
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)k);
        }
        LocalFree(keys);
    }
    /* "(None)" where the sorting put it (a name may sort before it) */
    none = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)s_none);
    /* the current pattern: WIN.INI holds its bits; find the name */
    GetProfileString(s_desk, "Pattern", s_none, bits, sizeof bits);
    if (current) {
        if (*current)
            sel = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)current);
    } else if (!lstrcmpi(bits, s_none))
        sel = none;
    else {
        int i, n = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);
        for (i = 0; i < n && sel < 0; i++) {
            if (i == none)
                continue;
            pattern_bits(cb, i, v, sizeof v);
            if (!lstrcmp(v, bits))
                sel = i;
        }
    }
    SendMessage(cb, CB_SETCURSEL, sel >= 0 ? sel : none, 0);
}

static void fill_savers(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_DK_SAVER);
    char path[160], cur[160], name[60];
    int n = windir(path, sizeof path), sel = 0, i;
    BOOL more;
    CPLFIND fd;
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)s_none);
    GetPrivateProfileString("boot", "SCRNSAVE.EXE", "", cur, sizeof cur, "SYSTEM.INI");
    lstrcpy(path + n, "*.SCR");
    /* one pass, each file read as found: cpl_find keeps its own DTA and
       puts Control Panel's back after every call */
    for (more = cpl_find(path, 0, &fd, TRUE); more; more = cpl_find(path, 0, &fd, FALSE)) {
        char full[160];
        lstrcpy(full, path);
        lstrcpy(full + n, fd.name);
        if (saver_name(full, name, sizeof name)) {
            i = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
            /* the item data: an atom-free copy of the path (none when the
               list is full: CB_ERR, CB_ERRSPACE) */
            if (i >= 0) {
                HLOCAL h = LocalAlloc(LMEM_FIXED, lstrlen(full) + 1);
                if (h) {
                    lstrcpy(LocalLock(h), full);
                    LocalUnlock(h);
                }
                SendMessage(cb, CB_SETITEMDATA, i, MAKELPARAM(h, 0));
            }
        }
        lstrcpy(path + n, "*.SCR");
    }
    /* "(None)" where the sorting put it: a saver's name may sort before
       it (observed with "!Starfield Simulatio": the list starts with that
       saver, "(None)" second and chosen; dksvorder.txt) */
    sel = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)s_none);
    if (sel < 0)
        sel = 0;
    if (GetProfileInt(s_win, "ScreenSaveActive", 0) && cur[0]) {
        int k, m = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);
        for (k = 0; k < m; k++) {
            HLOCAL h = (HLOCAL)LOWORD(SendMessage(cb, CB_GETITEMDATA, k, 0));
            if (h && !lstrcmpi(LocalLock(h), cur))
                sel = k;
            if (h)
                LocalUnlock(h);
        }
    }
    SendMessage(cb, CB_SETCURSEL, sel, 0);
    /* 3.1's: Test and Setup by the position, not the name: off for the
       first entry even when that is a saver, on for "(None)" second
       (dksvorder.txt, dksvorder2.txt) */
    EnableWindow(GetDlgItem(dlg, IDC_DK_TEST), sel > 0);
    EnableWindow(GetDlgItem(dlg, IDC_DK_SETUP), sel > 0);
}

static void free_savers(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_DK_SAVER);
    int k, m = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);
    for (k = 0; k < m; k++) {       /* "(None)"'s data is 0 */
        HLOCAL h = (HLOCAL)LOWORD(SendMessage(cb, CB_GETITEMDATA, k, 0));
        if (h)
            LocalFree(h);
    }
}

static void fill_wallpapers(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_DK_WALLPAPER);
    char path[160], cur[160];
    int n = windir(path, sizeof path), i;
    BOOL more;
    CPLFIND fd;
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)s_none);
    lstrcpy(path + n, "*.BMP");
    for (more = cpl_find(path, 0, &fd, TRUE); more; more = cpl_find(path, 0, &fd, FALSE)) {
        char name[16];
        lstrcpy(name, fd.name);
        lower(name);
        SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
    }
    GetProfileString(s_desk, "Wallpaper", s_none, cur, sizeof cur);
    i = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)cur);
    if (i >= 0)
        SendMessage(cb, CB_SETCURSEL, i, 0);
    else
        SetWindowText(cb, cur);
}

/* ---- the blink bar and its caret ---- */

#define BLINK_MIN 200
#define BLINK_MAX 1200
#define BLINK_STEP 20

static void set_blink_bar(HWND dlg)
{
    SetScrollPos(GetDlgItem(dlg, IDC_DK_BLINK), SB_CTL, (BLINK_MAX - blink) / BLINK_STEP, TRUE);
    KillTimer(dlg, 1);
    SetTimer(dlg, 1, blink, NULL);
}

/* the blink sample (an empty static, IDC_DK_BLINKER): inverted and back
   at the rate, as a caret is drawn (an empty static paints nothing of its
   own: showing and hiding it showed nothing). A repaint of the static
   only moves the phase. */
static void blink_sample(HWND dlg)
{
    HWND b = GetDlgItem(dlg, IDC_DK_BLINKER);
    HDC dc = GetDC(b);
    RECT r;
    GetClientRect(b, &r);
    InvertRect(dc, &r);
    ReleaseDC(b, dc);
}

/* ---- validation ---- */

static BOOL check(HWND dlg, int id, int lo, int hi, UINT ids, int *v)
{
    BOOL ok;
    int n = (int)GetDlgItemInt(dlg, id, &ok, TRUE);
    if (!ok || n < lo || n > hi) {
        char t[40], f[100], m[140];
        LoadString(g_hinst, IDS_CONTROLPANEL, t, sizeof t);
        LoadString(g_hinst, ids, f, sizeof f);
        wsprintf(m, f, lo, hi);
        MessageBox(dlg, m, t, MB_OK | MB_ICONINFORMATION);     /* observed */
        SetFocus(GetDlgItem(dlg, id));
        SendDlgItemMessage(dlg, id, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }
    *v = n;
    return TRUE;
}

static void put_int(const char *file, const char *sec, const char *key, int v)
{
    char b[12];
    wsprintf(b, "%d", v);
    if (file)
        WritePrivateProfileString(sec, key, b, file);
    else
        WriteProfileString(sec, key, b);
}

static BOOL apply(HWND dlg)
{
    int border, spacing, gran, delay, i;
    char s[160], bits[64];
    BOOL wrap = IsDlgButtonChecked(dlg, IDC_DK_WRAP);
    BOOL tile = IsDlgButtonChecked(dlg, IDC_DK_TILE);
    BOOL cool = IsDlgButtonChecked(dlg, IDC_DK_COOLSWITCH);
    HWND sv = GetDlgItem(dlg, IDC_DK_SAVER);
    if (!check(dlg, IDC_DK_BORDER, 1, 50, IDS_DK_BORDER, &border) ||
        !check(dlg, IDC_DK_SPACING, 32, 512, IDS_DK_SPACING, &spacing) ||
        !check(dlg, IDC_DK_GRAN, 0, 49, IDS_DK_GRAN, &gran) ||
        !check(dlg, IDC_DK_DELAY, 0, 99, IDS_DK_DELAY, &delay))    /* UNVERIFIED: 99 */
        return FALSE;

    put_int(NULL, s_win, "CoolSwitch", cool);
    SystemParametersInfo(SPI_SETFASTTASKSWITCH, cool, NULL, 0);
    /* pattern */
    i = (int)SendDlgItemMessage(dlg, IDC_DK_PATTERN, CB_GETCURSEL, 0, 0);
    pattern_bits(GetDlgItem(dlg, IDC_DK_PATTERN), i, bits, sizeof bits);
    WriteProfileString(s_desk, "Pattern", bits);
    /* screen saver */
    /* observed: SCRNSAVE.EXE is the chosen entry's path ("(None)" for
       "(None)"), ScreenSaveActive whether it is not the first entry: the
       first a saver sorting before "(None)" gives its path and 0, "(None)"
       second gives (None) and 1 (dksvorder.txt, dksvorder2.txt) */
    i = (int)SendMessage(sv, CB_GETCURSEL, 0, 0);
    {
        HLOCAL h = i >= 0 ? (HLOCAL)LOWORD(SendMessage(sv, CB_GETITEMDATA, i, 0)) : NULL;
        lstrcpy(s, h ? (char *)LocalLock(h) : s_none);
        if (h)
            LocalUnlock(h);
    }
    WritePrivateProfileString("boot", "SCRNSAVE.EXE", s, "SYSTEM.INI");
    put_int(NULL, s_win, "ScreenSaveActive", i > 0);
    put_int(NULL, s_win, "ScreenSaveTimeOut", delay * 60);
    SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, i > 0, NULL, 0);
    SystemParametersInfo(SPI_SETSCREENSAVETIMEOUT, delay * 60, NULL, 0);
    /* wallpaper */
    GetDlgItemText(dlg, IDC_DK_WALLPAPER, s, sizeof s);
    if (!s[0])
        lstrcpy(s, s_none);
    WriteProfileString(s_desk, "Wallpaper", s);
    /* icons */
    put_int(NULL, s_desk, "IconSpacing", spacing);
    SystemParametersInfo(SPI_ICONHORIZONTALSPACING, spacing, NULL, 0);
    if (wrap != wrap0) {
        put_int(NULL, s_desk, "IconTitleWrap", wrap);
        SystemParametersInfo(SPI_SETICONTITLEWRAP, wrap, NULL, 0);
    }
    put_int(NULL, s_desk, "TileWallPaper", tile);
    /* sizing grid, border, cursor */
    put_int(NULL, s_desk, "GridGranularity", gran);
    SystemParametersInfo(SPI_SETGRIDGRANULARITY, gran, NULL, 0);
    put_int(NULL, s_win, "BorderWidth", border);
    SystemParametersInfo(SPI_SETBORDER, border, NULL, 0);
    put_int(NULL, s_win, "CursorBlinkRate", blink);
    SetCaretBlinkTime(blink);
    /* observed (wnchange): WM_WININICHANGE for "Desktop", then for
       "Windows", then the WM_SYSCOLORCHANGE that 3.1's USER sends when
       the desktop is set: pattern and wallpaper last */
    SendMessage(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)(LPSTR)s_desk);
    SendMessage(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)(LPSTR)"Windows");
    SystemParametersInfo(SPI_SETDESKPATTERN, (UINT)-1, NULL, 0);
    SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (LPVOID)(LPSTR)s, 0);
    return TRUE;
}

/* ---- spin buttons ---- */

static void spin(HWND dlg, int id, int delta, int lo, int hi)
{
    BOOL ok;
    /* in a long: 32767 and up is not -32768 (cpl audit L7) */
    long v = (long)(int)GetDlgItemInt(dlg, id, &ok, TRUE) + delta;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    SetDlgItemInt(dlg, id, (int)v, TRUE);
    /* observed (dkspin): the new value selected */
    SendDlgItemMessage(dlg, id, EM_SETSEL, 0, MAKELPARAM(0, -1));
}

/* ---- the Edit Pattern dialog ---- */

static BYTE pat[8];
static HLOCAL pat_name;     /* the name chosen (win_text, item_text) */

static void set_pat_name(HLOCAL h)
{
    if (pat_name)
        LocalFree(pat_name);
    pat_name = h;
}

/* the patterns again, pat_name's chosen (NULL: by WIN.INI's bits) */
static void fill_patterns_named(HWND cb)
{
    fill_patterns(cb, pat_name ? (const char *)LocalLock(pat_name) : NULL);
    if (pat_name)
        LocalUnlock(pat_name);
}

static void pat_parse(const char *s)
{
    int i;
    for (i = 0; i < 8; i++) {
        int v = 0;
        while (*s == ' ')
            s++;
        while (*s >= '0' && *s <= '9')
            v = v * 10 + *s++ - '0';
        pat[i] = (BYTE)v;
    }
}

static void pat_format(char *s)
{
    int i, n = 0;
    for (i = 0; i < 8; i++)
        n += wsprintf(s + n, i ? " %d" : "%d", pat[i]);
}

static void pat_paint(HWND dlg, HDC dc)
{
    RECT g, r;
    int x, y, cw, ch;
    HBITMAP bm;
    HBRUSH br;
    WORD bits[8];
    /* observed (dkcolors.txt): the set bits in the window text colour,
       the others in the desktop's, in the grid and in the sample */
    HBRUSH fg = CreateSolidBrush(GetSysColor(COLOR_WINDOWTEXT));
    HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_BACKGROUND));
    GetWindowRect(GetDlgItem(dlg, IDC_EP_GRID), &g);
    ScreenToClient(dlg, (POINT FAR *)&g.left);
    ScreenToClient(dlg, (POINT FAR *)&g.right);
    InflateRect(&g, -1, -1);
    /* observed: the cells share the whole frame, cell k from k/8 of it
       to (k+1)/8 (156 pixels wide on VGA: 19 and 20 in turn), as the
       clicks are mapped (WM_LBUTTONDOWN) */
    cw = g.right - g.left;
    ch = g.bottom - g.top;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            SetRect(&r, g.left + x * cw / 8, g.top + y * ch / 8,
                    g.left + (x + 1) * cw / 8, g.top + (y + 1) * ch / 8);
            FillRect(dc, &r, pat[y] & (0x80 >> x) ? fg : bg);
        }
    DeleteObject(fg);
    DeleteObject(bg);
    for (y = 0; y < 8; y++)
        bits[y] = (WORD)(BYTE)~pat[y];
    bm = CreateBitmap(8, 8, 1, 1, bits);
    br = CreatePatternBrush(bm);
    GetWindowRect(GetDlgItem(dlg, IDC_EP_SAMPLE), &r);
    ScreenToClient(dlg, (POINT FAR *)&r.left);
    ScreenToClient(dlg, (POINT FAR *)&r.right);
    InflateRect(&r, -1, -1);
    /* the brush's 0 bits (pat's 1 bits) in the text colour */
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkColor(dc, GetSysColor(COLOR_BACKGROUND));
    FillRect(dc, &r, br);
    DeleteObject(br);
    DeleteObject(bm);
}

static void pat_load(HWND dlg)
{
    char v[64];
    HLOCAL h = win_text(GetDlgItem(dlg, IDC_EP_NAME));
    lstrcpy(v, "0 0 0 0 0 0 0 0");
    if (h) {
        GetPrivateProfileString(s_patterns, LocalLock(h), v, v, sizeof v, s_cpl_ini);
        LocalUnlock(h);
        LocalFree(h);
    }
    pat_parse(v);
    InvalidateRect(dlg, NULL, FALSE);
}

BOOL FAR PASCAL __loadds EditPatternProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND cb = GetDlgItem(dlg, IDC_EP_NAME);
        int i;
        fill_patterns_named(cb);
        /* observed (zzdisc, dkcolors): "(None)" is never listed here;
           opened on (None), the name is empty */
        i = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)s_none);
        if (i >= 0) {
            BOOL none = SendMessage(cb, CB_GETCURSEL, 0, 0) == i;
            SendMessage(cb, CB_DELETESTRING, i, 0);
            if (none)
                SendMessage(cb, CB_SETCURSEL, (WPARAM)-1, 0);
        }
        /* observed: Add and Change disabled at first, Remove with a
           pattern chosen (zzdisc, dkcolors) */
        EnableWindow(GetDlgItem(dlg, IDC_EP_ADD), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_EP_CHANGE), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_EP_REMOVE),
                     SendMessage(cb, CB_GETCURSEL, 0, 0) >= 0);
        pat_load(dlg);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(dlg, &ps);
        pat_paint(dlg, ps.hdc);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        RECT g;
        POINT pt;
        GetWindowRect(GetDlgItem(dlg, IDC_EP_GRID), &g);
        ScreenToClient(dlg, (POINT FAR *)&g.left);
        ScreenToClient(dlg, (POINT FAR *)&g.right);
        InflateRect(&g, -1, -1);
        pt.x = LOWORD(lp);
        pt.y = HIWORD(lp);
        if (PtInRect(&g, pt)) {
            /* the cells as pat_paint draws them */
            int x = (pt.x - g.left) * 8 / (g.right - g.left);
            int y = (pt.y - g.top) * 8 / (g.bottom - g.top);
            pat[y] ^= (BYTE)(0x80 >> x);
            EnableWindow(GetDlgItem(dlg, IDC_EP_CHANGE), TRUE);
            InvalidateRect(dlg, NULL, FALSE);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_EP_NAME:
            /* a name typed: Add (UNVERIFIED: when 3.1 enables it) */
            if (HIWORD(lp) == CBN_EDITCHANGE)
                EnableWindow(GetDlgItem(dlg, IDC_EP_ADD),
                             GetWindowTextLength(GetDlgItem(dlg, IDC_EP_NAME)) > 0);
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_EP_NAME, CB_GETCURSEL, 0, 0);
                HLOCAL h = item_text(GetDlgItem(dlg, IDC_EP_NAME), i);
                if (h) {
                    SetDlgItemText(dlg, IDC_EP_NAME, LocalLock(h));
                    LocalUnlock(h);
                    LocalFree(h);
                }
                pat_load(dlg);
                EnableWindow(GetDlgItem(dlg, IDC_EP_REMOVE), TRUE);
            }
            return TRUE;
        case IDC_EP_ADD:
        case IDC_EP_CHANGE: {
            char v[64];
            HLOCAL h = win_text(GetDlgItem(dlg, IDC_EP_NAME));
            const char *name;
            if (!h)
                return TRUE;
            name = LocalLock(h);
            if (name[0]) {
                pat_format(v);
                WritePrivateProfileString(s_patterns, name, v, s_cpl_ini);
                if (SendDlgItemMessage(dlg, IDC_EP_NAME, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                       (LPARAM)(LPSTR)name) < 0)
                    SendDlgItemMessage(dlg, IDC_EP_NAME, CB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
                LocalUnlock(h);
                set_pat_name(h);
            } else {
                LocalUnlock(h);
                LocalFree(h);
            }
            return TRUE;
        }
        case IDC_EP_REMOVE: {
            char t[40], f[100];
            HLOCAL h = win_text(GetDlgItem(dlg, IDC_EP_NAME)), hm;
            const char *name;
            if (!h)
                return TRUE;
            name = LocalLock(h);
            LoadString(g_hinst, IDS_CONTROLPANEL, t, sizeof t);
            LoadString(g_hinst, IDS_DK_REMOVEPAT, f, sizeof f);
            /* the message as long as the name needs (wsprintf's own
               limit is 1024 bytes) */
            hm = LocalAlloc(LMEM_FIXED, lstrlen(f) + lstrlen(name) + 1);
            if (hm) {
                char *m = LocalLock(hm);
                if (lstrlen(f) + lstrlen(name) < 1000)
                    wsprintf(m, f, (LPSTR)name);
                else
                    lstrcpy(m, name);
                if (MessageBox(dlg, m, t, MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
                    int i = (int)SendDlgItemMessage(dlg, IDC_EP_NAME, CB_FINDSTRINGEXACT,
                                                    (WPARAM)-1, (LPARAM)(LPSTR)name);
                    WritePrivateProfileString(s_patterns, name, NULL, s_cpl_ini);
                    if (i >= 0)
                        SendDlgItemMessage(dlg, IDC_EP_NAME, CB_DELETESTRING, i, 0);
                }
                LocalUnlock(hm);
                LocalFree(hm);
            }
            LocalUnlock(h);
            LocalFree(h);
            return TRUE;
        }
        case IDOK:
            set_pat_name(win_text(GetDlgItem(dlg, IDC_EP_NAME)));
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_DESKTOP);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- the Desktop dialog ---- */

BOOL FAR PASCAL __loadds DesktopProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND sb = GetDlgItem(dlg, IDC_DK_BLINK);
        int tile;
        fill_patterns(GetDlgItem(dlg, IDC_DK_PATTERN), NULL);
        CheckDlgButton(dlg, IDC_DK_COOLSWITCH, GetProfileInt(s_win, "CoolSwitch", 1) != 0);
        fill_savers(dlg);
        SetDlgItemInt(dlg, IDC_DK_DELAY, GetProfileInt(s_win, "ScreenSaveTimeOut", 120) / 60, TRUE);
        fill_wallpapers(dlg);
        tile = GetProfileInt(s_desk, "TileWallPaper", 1);
        CheckRadioButton(dlg, IDC_DK_CENTER, IDC_DK_TILE, tile ? IDC_DK_TILE : IDC_DK_CENTER);
        SetDlgItemInt(dlg, IDC_DK_SPACING, GetProfileInt(s_desk, "IconSpacing", 75), TRUE);
        wrap0 = GetProfileInt(s_desk, "IconTitleWrap", 1) != 0;
        CheckDlgButton(dlg, IDC_DK_WRAP, wrap0);
        SetDlgItemInt(dlg, IDC_DK_GRAN, GetProfileInt(s_desk, "GridGranularity", 0), TRUE);
        SetDlgItemInt(dlg, IDC_DK_BORDER, GetProfileInt(s_win, "BorderWidth", 3), TRUE);
        blink = GetProfileInt(s_win, "CursorBlinkRate", 530);    /* observed */
        if (blink < BLINK_MIN) blink = BLINK_MIN;
        if (blink > BLINK_MAX) blink = BLINK_MAX;
        SetScrollRange(sb, SB_CTL, 0, (BLINK_MAX - BLINK_MIN) / BLINK_STEP, FALSE);
        set_blink_bar(dlg);
        return TRUE;
    }
    case WM_TIMER:          /* the blink sample */
        blink_sample(dlg);
        return TRUE;
    case WM_HSCROLL: {
        int v = blink;
        switch (wp) {
        case SB_LINEUP:        v += BLINK_STEP; break;
        case SB_LINEDOWN:      v -= BLINK_STEP; break;
        case SB_PAGEUP:        v += 250; break;    /* observed: 530 -> 280 */
        case SB_PAGEDOWN:      v -= 250; break;
        case SB_TOP:           v = BLINK_MIN; break;
        case SB_BOTTOM:        v = BLINK_MAX; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: v = BLINK_MAX - (int)LOWORD(lp) * BLINK_STEP; break;
        default: return TRUE;
        }
        if (v < BLINK_MIN) v = BLINK_MIN;
        if (v > BLINK_MAX) v = BLINK_MAX;
        blink = v;
        set_blink_bar(dlg);
        SetCaretBlinkTime(blink);       /* observed: at once, not on OK */
        return TRUE;
    }
    case WM_VSCROLL: {
        int id = GetDlgCtrlID((HWND)HIWORD(lp)), d = wp == SB_LINEUP ? 1 : -1;
        switch (id) {
        case IDC_DK_DELAYA:   spin(dlg, IDC_DK_DELAY, d, 0, 99); break;
        case IDC_DK_SPACINGA: spin(dlg, IDC_DK_SPACING, d, 32, 512); break;
        case IDC_DK_GRANA:    spin(dlg, IDC_DK_GRAN, d, 0, 49); break;
        case IDC_DK_BORDERA:  spin(dlg, IDC_DK_BORDER, d, 1, 50); break;
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_DK_SAVER:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_DK_SAVER, CB_GETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(dlg, IDC_DK_TEST), i > 0);
                EnableWindow(GetDlgItem(dlg, IDC_DK_SETUP), i > 0);
            }
            return TRUE;
        case IDC_DK_TEST:
        case IDC_DK_SETUP: {
            HWND sv = GetDlgItem(dlg, IDC_DK_SAVER);
            int i = (int)SendMessage(sv, CB_GETCURSEL, 0, 0);
            HLOCAL h = i > 0 ? (HLOCAL)LOWORD(SendMessage(sv, CB_GETITEMDATA, i, 0)) : NULL;
            char cmd[180];
            if (h) {
                wsprintf(cmd, "%s %s", (LPSTR)LocalLock(h), (LPSTR)(wp == IDC_DK_TEST ? "/s" : "/c"));
                LocalUnlock(h);
                WinExec(cmd, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        case IDC_DK_CENTER:
        case IDC_DK_TILE:
            CheckRadioButton(dlg, IDC_DK_CENTER, IDC_DK_TILE, wp);
            return TRUE;
        case IDC_DK_EDITPAT: {
            int i = (int)SendDlgItemMessage(dlg, IDC_DK_PATTERN, CB_GETCURSEL, 0, 0);
            set_pat_name(item_text(GetDlgItem(dlg, IDC_DK_PATTERN), i));
            if (DialogBox(g_hinst, MAKEINTRESOURCE(DLG_EDITPATTERN), dlg, EditPatternProc) == IDOK)
                fill_patterns_named(GetDlgItem(dlg, IDC_DK_PATTERN));
            set_pat_name(NULL);
            return TRUE;
        }
        case IDOK:
            if (apply(dlg)) {
                KillTimer(dlg, 1);
                free_savers(dlg);
                EndDialog(dlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            KillTimer(dlg, 1);
            free_savers(dlg);
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_DESKTOP);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void desktop_applet(HWND parent)
{
    cpl_dialog(parent, DLG_DESKTOP, DesktopProc);
}
