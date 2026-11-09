/*
 * print.c - w16 Notepad: Print, Page Setup, Print Setup.
 *
 * Observed on Windows 3.1 (no printer installed in the reference copy):
 *   - Print is grayed while WIN.INI [windows] device= names no printer.
 *   - Print Setup is COMMDLG's PrintDlg(PD_PRINTSETUP); with no printer
 *     COMMDLG itself says "No default printer..." under "Print Setup".
 *   - Page Setup is Notepad's own dialog (template 14 of the original,
 *     laid out the same here): Header "&f", Footer "Page &p", margins
 *     .75 .75 1 1 (inches); margins that are not numbers: "The Margin
 *     values are not correct..." (IDS_BADMARGIN). The settings last while
 *     Notepad runs (WIN.INI is not written).
 *   - Header and footer codes (the original's string 42 lists the code
 *     letters "fFpPtTdDcCrRlL"): &f file name, &p page number, &t time,
 *     &d date, &l &c &r left/centre/right alignment of what follows,
 *     && an ampersand.
 * UNVERIFIED (needs a printer driver in a scratch install): the printed
 * page itself (font, where header and footer sit, default alignment), the
 * Printing dialog's text and the /P flow's messages.
 */
#include "notepad.h"
#ifndef W16_WIN2
#include <commdlg.h>
#endif

#define STR_MAX     64

static char header[STR_MAX], footer[STR_MAX];
static char margin[4][16];              /* left, right, top, bottom */
static BOOL setup_loaded;

#ifndef W16_WIN2
static HGLOBAL dev_mode, dev_names;     /* from Print Setup */
#endif

static void load_setup(void)
{
    int i;
    if (setup_loaded)
        return;
    setup_loaded = TRUE;
    LoadString(g_hinst, IDS_HEADER, header, sizeof header);
    LoadString(g_hinst, IDS_FOOTER, footer, sizeof footer);
    for (i = 0; i < 4; i++)
        LoadString(g_hinst, IDS_LEFT + i, margin[i], sizeof margin[i]);
}

/* A margin in thousandths of an inch: digits with at most one point,
   blanks around them; -1 if it is not such a number. UNVERIFIED: which
   values the original accepts beyond these (it also checks that the
   margins fit the page). */
static long margin_mils(const char *s)
{
    long in = 0, mils = 0;
    int scale = 100;
    BOOL point = FALSE, digits = FALSE;
    while (*s == ' ')
        s++;
    for (; *s && *s != ' '; s++) {
        if (*s == '.' && !point) {
            point = TRUE;
        } else if (*s >= '0' && *s <= '9') {
            digits = TRUE;
            if (!point) {
                in = in * 10 + (*s - '0');
                if (in > 30)          /* MulDiv needs ints */
                    return -1;
            } else {
                mils += (*s - '0') * scale;
                scale /= 10;
            }
        } else {
            return -1;
        }
    }
    while (*s == ' ')
        s++;
    if (*s || !digits)
        return -1;
    return in * 1000 + mils;
}

BOOL FAR PASCAL __export PageSetupDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_HEADER, header);
        SetDlgItemText(dlg, IDC_FOOTER, footer);
        for (i = 0; i < 4; i++)
            SetDlgItemText(dlg, IDC_LEFT + i, margin[i]);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            char m[4][16];
            for (i = 0; i < 4; i++) {
                GetDlgItemText(dlg, IDC_LEFT + i, m[i], sizeof m[i]);
                if (margin_mils(m[i]) < 0) {
                    msg_box(dlg, IDS_BADMARGIN, NULL, MB_OK | MB_ICONEXCLAMATION);
                    SetFocus(GetDlgItem(dlg, IDC_LEFT + i));
                    SendDlgItemMessage(dlg, IDC_LEFT + i, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
                    return TRUE;
                }
            }
            GetDlgItemText(dlg, IDC_HEADER, header, sizeof header);
            GetDlgItemText(dlg, IDC_FOOTER, footer, sizeof footer);
            for (i = 0; i < 4; i++)
                s_cpy(margin[i], m[i], sizeof margin[i]);
            EndDialog(dlg, TRUE);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void page_setup(void)
{
    FARPROC p = MakeProcInstance((FARPROC)PageSetupDlgProc, g_hinst);
    load_setup();
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_PAGESETUP), g_main, (DLGPROC)p);
    FreeProcInstance(p);
}

/* ---- the printer ---- */

/* WIN.INI [windows] device=name,driver,port */
static BOOL default_printer(char *buf, int size, char **name, char **drv, char **port)
{
    char *p;
    if (!GetProfileString("windows", "device", "", buf, size) || !buf[0])
        return FALSE;
    *name = buf;
    for (p = buf; *p && *p != ','; p++)
        ;
    if (!*p)
        return FALSE;
    *p++ = 0;
    *drv = p;
    for (; *p && *p != ','; p++)
        ;
    if (!*p)
        return FALSE;
    *p++ = 0;
    *port = p;
    return TRUE;
}

BOOL can_print(void)
{
    char buf[128], *n, *d, *p;
#ifndef W16_WIN2
    if (dev_names)
        return TRUE;
#endif
    return default_printer(buf, sizeof buf, &n, &d, &p);
}

static HDC printer_dc(void)
{
    char buf[128], *n, *d, *p;
#ifndef W16_WIN2
    if (dev_names) {
        DEVNAMES FAR *dn = (DEVNAMES FAR *)GlobalLock(dev_names);
        LPSTR base = (LPSTR)dn;
        HDC dc = CreateDC(base + dn->wDriverOffset, base + dn->wDeviceOffset,
                          base + dn->wOutputOffset,
                          dev_mode ? GlobalLock(dev_mode) : NULL);
        if (dev_mode)
            GlobalUnlock(dev_mode);
        GlobalUnlock(dev_names);
        return dc;
    }
#endif
    if (!default_printer(buf, sizeof buf, &n, &d, &p))
        return NULL;
    return CreateDC(d, n, p, NULL);
}

void print_setup(void)
{
#ifndef W16_WIN2
    PRINTDLG pd;
    BYTE FAR *z = (BYTE FAR *)&pd;
    unsigned i;
    for (i = 0; i < sizeof pd; i++)
        z[i] = 0;
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_main;
    pd.hDevMode = dev_mode;
    pd.hDevNames = dev_names;
    pd.Flags = PD_PRINTSETUP;
    if (PrintDlg(&pd)) {
        dev_mode = pd.hDevMode;
        dev_names = pd.hDevNames;
    } else {
        dev_mode = pd.hDevMode;         /* PrintDlg may have made them */
        dev_names = pd.hDevNames;
    }
#endif
}

/* ---- printing ---- */

static BOOL aborted;
static HWND abort_dlg;

BOOL FAR PASCAL __export AbortProc(HDC dc, int code)
{
    MSG msg;
    (void)dc;
    (void)code;
    while (!aborted && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        if (!abort_dlg || !IsDialogMessage(abort_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    return !aborted;
}

BOOL FAR PASCAL __export AbortDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_PRINTFILE, g_file[0] ? s_basename(g_file) : "");
        return TRUE;
    case WM_COMMAND:
        if (wp == IDCANCEL) {
            aborted = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Expand a header/footer into its left, centre and right parts. */
static void expand_hf(const char *src, int page, char part[3][STR_MAX * 2])
{
    int where = 1, n[3] = { 0, 0, 0 };   /* UNVERIFIED: centred unless told */
    char tmp[80];
    const char FAR *add;
    for (; *src; src++) {
        add = NULL;
        tmp[0] = *src;
        tmp[1] = 0;
        if (*src == '&' && src[1]) {
            src++;
            switch (*src) {
            case 'f': case 'F':
                add = g_file[0] ? s_basename(g_file) : "";
                break;
            case 'p': case 'P': {
                int v = page, k = 0, j;
                char rev[8];
                do {
                    rev[k++] = (char)('0' + v % 10);
                    v /= 10;
                } while (v);
                for (j = 0; j < k; j++)
                    tmp[j] = rev[k - 1 - j];
                tmp[k] = 0;
                add = tmp;
                break;
            }
            case 't': case 'T':
                time_part(tmp);
                add = tmp;
                break;
            case 'd': case 'D':
                date_part(tmp);
                add = tmp;
                break;
            case 'l': case 'L':
                where = 0;
                continue;
            case 'c': case 'C':
                where = 1;
                continue;
            case 'r': case 'R':
                where = 2;
                continue;
            default:                    /* "&&" and unknown codes: the char */
                tmp[0] = *src;
                add = tmp;
                break;
            }
        } else {
            add = tmp;
        }
        s_cpy(part[where] + n[where], add, STR_MAX * 2 - n[where]);
        n[where] = s_len(part[where]);
    }
}

static void print_hf(HDC dc, const char *spec, int page, int y, int left, int right)
{
    char part[3][STR_MAX * 2];
    int i;
    for (i = 0; i < 3; i++)
        part[i][0] = 0;
    expand_hf(spec, page, part);
    for (i = 0; i < 3; i++) {
        int len = s_len(part[i]), w, x;
        if (!len)
            continue;
        w = LOWORD(GetTextExtent(dc, part[i], len));
        x = i == 0 ? left : i == 1 ? (left + right - w) / 2 : right - w;
        TextOut(dc, x, y, part[i], len);
    }
}

void print_file(void)
{
    HDC dc;
    WORD len, pos = 0;
    LPSTR text;
    TEXTMETRIC tm;
    int lh, dpx, dpy, left, right, top, bottom, y, page = 1;
    FARPROC abortp, dlgp;
    char docname[DOS_PATH];

    load_setup();
    dc = printer_dc();
    if (!dc) {
        msg_box(g_main, IDS_CANTPRINT, g_file, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    text = mem_alloc((DWORD)(len = (WORD)SendMessage(g_edit, WM_GETTEXTLENGTH, 0, 0)) + 1);
    if (!text) {
        DeleteDC(dc);
        msg_box(g_main, IDS_PRINTMEM, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    GetWindowText(g_edit, text, len + 1);
    GetTextMetrics(dc, &tm);
    lh = tm.tmHeight + tm.tmExternalLeading;
    dpx = GetDeviceCaps(dc, LOGPIXELSX);
    dpy = GetDeviceCaps(dc, LOGPIXELSY);
    left = MulDiv((int)margin_mils(margin[0]), dpx, 1000);
    right = GetDeviceCaps(dc, HORZRES) - MulDiv((int)margin_mils(margin[1]), dpx, 1000);
    top = MulDiv((int)margin_mils(margin[2]), dpy, 1000);
    bottom = GetDeviceCaps(dc, VERTRES) - MulDiv((int)margin_mils(margin[3]), dpy, 1000);

    aborted = FALSE;
    EnableWindow(g_main, FALSE);
    dlgp = MakeProcInstance((FARPROC)AbortDlgProc, g_hinst);
    abort_dlg = CreateDialog(g_hinst, MAKEINTRESOURCE(IDD_PRINTING), g_main, (DLGPROC)dlgp);
    abortp = MakeProcInstance((FARPROC)AbortProc, g_hinst);
    Escape(dc, SETABORTPROC, 0, (LPSTR)abortp, NULL);
    s_cpy(docname, g_file[0] ? s_basename(g_file) : "", sizeof docname);
    if (Escape(dc, STARTDOC, s_len(docname), docname, NULL) > 0) {
        while (!aborted) {
            print_hf(dc, header, page, top - 2 * lh, left, right);
            for (y = top; y + lh <= bottom && pos < len;) {
                WORD e = pos;
                int n;
                while (e < len && text[e] != '\r' && text[e] != '\n')
                    e++;
                n = e - pos;
                /* UNVERIFIED: long lines are cut at the right margin */
                while (n > 0 && LOWORD(GetTextExtent(dc, text + pos, n)) > right - left)
                    n--;
                if (n == 0 && e > pos)
                    n = 1;
                TabbedTextOut(dc, left, y, text + pos, n, 0, NULL, left);
                y += lh;
                pos += n;
                if (pos == e) {
                    if (pos < len && text[pos] == '\r')
                        pos++;
                    if (pos < len && text[pos] == '\n')
                        pos++;
                }
            }
            print_hf(dc, footer, page, bottom + lh, left, right);
            if (Escape(dc, NEWFRAME, 0, NULL, NULL) < 0)
                break;
            if (pos >= len)
                break;
            page++;
        }
        if (!aborted)
            Escape(dc, ENDDOC, 0, NULL, NULL);
        else
            Escape(dc, ABORTDOC, 0, NULL, NULL);
    } else {
        msg_box(g_main, IDS_CANTPRINT, g_file, MB_OK | MB_ICONEXCLAMATION);
    }
    EnableWindow(g_main, TRUE);
    DestroyWindow(abort_dlg);
    abort_dlg = NULL;
    FreeProcInstance(abortp);
    FreeProcInstance(dlgp);
    DeleteDC(dc);
    mem_free(text);
}
