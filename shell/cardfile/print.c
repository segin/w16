/*
 * print.c - w16 Cardfile: Print, Print All, Page Setup, Print Setup.
 *
 * Page Setup's defaults are the original's strings 45-50: header "&f",
 * footer "Page &p", margins .75 left and right, 1.00 top and bottom
 * (inches). Header and footer codes (string 51 lists the letters): &f
 * the file name, &p the page number, &d the date, &t the time, &l &c &r
 * align what follows left, centre or right, && an ampersand.
 *
 * UNVERIFIED: the printed layout. No printer output of the original was
 * observed; cards print one under another as on screen (border, index
 * line, double line, text, picture) at the printer's resolution, the
 * page's header and footer centred by default.
 */
#include "cardfile.h"

char g_header[64], g_footer[64];
char g_margin[4][16];                   /* left, right, top, bottom */

void page_defaults(void)
{
    int i;
    LoadString(g_hinst, IDS_DEFHEADER, g_header, sizeof g_header);
    LoadString(g_hinst, IDS_DEFFOOTER, g_footer, sizeof g_footer);
    for (i = 0; i < 4; i++)
        LoadString(g_hinst, IDS_DEFLEFT + i, g_margin[i], sizeof g_margin[i]);
}

/* "1.25" in hundredths of an inch; -1 if not a positive number */
static int hundredths(LPCSTR s)
{
    int v = 0, frac = -1;
    BOOL any = FALSE;
    while (*s == ' ')
        s++;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            any = TRUE;
            if (frac < 0)
                v = v * 10 + (*s - '0');
            else if (frac < 2) {
                v = v * 10 + (*s - '0');
                frac++;
            }
        } else if (*s == '.' && frac < 0) {
            frac = 0;
        } else if (*s != ' ') {
            return -1;
        }
        if (v > 3000)
            return -1;
    }
    if (!any)
        return -1;
    if (frac < 0)
        frac = 0;
    while (frac++ < 2)
        v *= 10;
    return v;
}

BOOL CALLBACK __export PageDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, IDC_HEADER, g_header);
        SetDlgItemText(d, IDC_FOOTER, g_footer);
        for (i = 0; i < 4; i++)
            SetDlgItemText(d, IDC_LEFT + i, g_margin[i]);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            char m[4][16];
            for (i = 0; i < 4; i++) {
                GetDlgItemText(d, IDC_LEFT + i, m[i], sizeof m[i]);
                if (hundredths(m[i]) < 0) {
                    msg_box(IDS_BADMARGINS, NULL, MB_OK | MB_ICONEXCLAMATION);
                    SetFocus(GetDlgItem(d, IDC_LEFT + i));
                    return TRUE;
                }
            }
            GetDlgItemText(d, IDC_HEADER, g_header, sizeof g_header);
            GetDlgItemText(d, IDC_FOOTER, g_footer, sizeof g_footer);
            for (i = 0; i < 4; i++)
                s_cpy(g_margin[i], m[i], sizeof g_margin[i]);
            EndDialog(d, TRUE);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void dlg_pagesetup(void)
{
    FARPROC p = MakeProcInstance((FARPROC)PageDlgProc, g_hinst);
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_PAGESETUP), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
}

/* ---- the printer ---- */

static HGLOBAL dev_mode, dev_names;

void print_setup(void)
{
    PRINTDLG pd;
    mem_set(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_frame;
    pd.hDevMode = dev_mode;
    pd.hDevNames = dev_names;
    pd.Flags = PD_PRINTSETUP;
    if (PrintDlg(&pd)) {
        dev_mode = pd.hDevMode;
        dev_names = pd.hDevNames;
    }
}

static HDC printer_dc(void)
{
    PRINTDLG pd;
    mem_set(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_frame;
    pd.hDevMode = dev_mode;
    pd.hDevNames = dev_names;
    pd.Flags = PD_RETURNDC | PD_RETURNDEFAULT;
    if (!PrintDlg(&pd))
        return NULL;
    dev_mode = pd.hDevMode;
    dev_names = pd.hDevNames;
    return pd.hDC;
}

static BOOL user_abort;
static HWND abort_dlg;

BOOL CALLBACK __export AbortDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetDlgItemText(d, IDC_PRINTFILE, g_path[0] ? base_name(g_path) : load_str(IDS_UNTITLED));
        return TRUE;
    }
    if (msg == WM_COMMAND && wp == IDCANCEL) {
        user_abort = TRUE;
        return TRUE;
    }
    return FALSE;
}

BOOL CALLBACK __export AbortProc(HDC dc, int err)
{
    MSG m;
    (void)dc;
    (void)err;
    while (!user_abort && PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        if (!abort_dlg || !IsDialogMessage(abort_dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    return !user_abort;
}

/* expand the header/footer codes; returns the alignment chosen */
static int expand(LPCSTR in, LPSTR out, int size, int page)
{
    int j = 0, align = 1;               /* 0 left, 1 centre, 2 right */
    char num[12];
    for (; *in && j < size - 1; in++) {
        if (*in != '&') {
            out[j++] = *in;
            continue;
        }
        in++;
        switch (*in) {
        case 'f': case 'F':
            {
                LPCSTR n = g_path[0] ? base_name(g_path) : load_str(IDS_UNTITLED);
                while (*n && j < size - 1)
                    out[j++] = *n++;
            }
            break;
        case 'p': case 'P':
            {
                LPCSTR n = s_utoa(num, page);
                while (*n && j < size - 1)
                    out[j++] = *n++;
            }
            break;
        case 'l': case 'L':
            align = 0;
            break;
        case 'c': case 'C':
            align = 1;
            break;
        case 'r': case 'R':
            align = 2;
            break;
        case 'd': case 'D':
        case 't': case 'T':
            break;                      /* UNVERIFIED: date and time format */
        case '&':
            out[j++] = '&';
            break;
        case 0:
            in--;
            break;
        }
    }
    out[j] = 0;
    return align;
}

static void hf_line(HDC dc, LPCSTR fmt, int y, int left, int right, int page)
{
    char buf[160];
    int align = expand(fmt, buf, sizeof buf, page), w;
    w = LOWORD(GetTextExtent(dc, buf, s_len(buf)));
    TextOut(dc, align == 0 ? left : align == 2 ? right - w : (left + right - w) / 2, y,
            buf, s_len(buf));
}

/* print card i at y; returns its height */
static int print_one(HDC dc, WORD i, int x, int y, int cx, int cy)
{
    int w = 40 * cx + 3, h = 12 * cy + 5;
    HGLOBAL text = NULL;
    WORD cb = 0;
    PICTURE pic;
    LPCARD c = card_hdr(i);
    mem_set(&pic, 0, sizeof pic);
    Rectangle(dc, x, y, x + w, y + h);
    MoveTo(dc, x, y + cy + 1);
    LineTo(dc, x + w, y + cy + 1);
    MoveTo(dc, x, y + cy + 3);
    LineTo(dc, x + w, y + cy + 3);
    TextOut(dc, x + 1, y + 1, c->index, s_len(c->index));
    if (c->where != WHERE_BLANK)
        body_read(body_file(c), c->fmt, c->pos, &text, &cb, &pic);
    if (text) {
        RECT r;
        LPSTR t = GlobalLock(text);
        SetRect(&r, x + 1, y + cy + 4, x + w - 1, y + h - 1);
        DrawText(dc, t, cb, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        GlobalUnlock(text);
        GlobalFree(text);
    }
    if (pic.kind == PIC_BITMAP && pic.bmp) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP old = SelectObject(mem, pic.bmp);
        StretchBlt(dc, x + 1 + MulDiv(pic.x, cx, g_cx), y + cy + 4 + MulDiv(pic.y, cy, g_cy),
                   MulDiv(pic.cx, cx, g_cx), MulDiv(pic.cy, cy, g_cy), mem, 0, 0, pic.cx,
                   pic.cy, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    else if (pic.kind == PIC_OBJECT) {
        RECT r;
        SetRect(&r, x + 1 + MulDiv(pic.x, cx, g_cx), y + cy + 4 + MulDiv(pic.y, cy, g_cy), 0, 0);
        r.right = r.left + MulDiv(pic.cx, cx, g_cx);
        r.bottom = r.top + MulDiv(pic.cy, cy, g_cy);
        ole_draw(&pic, dc, &r);
    }
    pic_free(&pic);
    return h;
}

void print_cards(BOOL all)
{
    HDC dc = printer_dc();
    TEXTMETRIC tm;
    FARPROC ap, dp;
    int ppx, ppy, left, right, top, bottom, y, page = 1, cx, cy;
    WORD i, first, last;
    char doc[40];

    if (!dc) {
        msg_box(IDS_CANTPRINT, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    user_abort = FALSE;
    dp = MakeProcInstance((FARPROC)AbortDlgProc, g_hinst);
    ap = MakeProcInstance((FARPROC)AbortProc, g_hinst);
    abort_dlg = CreateDialog(g_hinst, MAKEINTRESOURCE(IDD_PRINTING), g_frame, (DLGPROC)dp);
    EnableWindow(g_frame, FALSE);
    Escape(dc, SETABORTPROC, 0, (LPSTR)ap, NULL);
    LoadString(g_hinst, IDS_APPNAME, doc, sizeof doc);
    if (Escape(dc, STARTDOC, s_len(doc), doc, NULL) <= 0) {
        msg_box(IDS_CANTPRINT, NULL, MB_OK | MB_ICONEXCLAMATION);
        goto done;
    }
    SelectObject(dc, GetStockObject(SYSTEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    cx = tm.tmAveCharWidth;
    cy = tm.tmHeight + tm.tmExternalLeading;
    ppx = GetDeviceCaps(dc, LOGPIXELSX);
    ppy = GetDeviceCaps(dc, LOGPIXELSY);
    left = MulDiv(hundredths(g_margin[0]), ppx, 100);
    right = GetDeviceCaps(dc, HORZRES) - MulDiv(hundredths(g_margin[1]), ppx, 100);
    top = MulDiv(hundredths(g_margin[2]), ppy, 100);
    bottom = GetDeviceCaps(dc, VERTRES) - MulDiv(hundredths(g_margin[3]), ppy, 100);
    first = all ? 0 : g_cur;
    last = all ? g_ncards - 1 : g_cur;
    y = top;
    hf_line(dc, g_header, top - cy - cy / 2, left, right, page);
    for (i = first; !user_abort; i++) {
        if (y + 12 * cy + 5 > bottom && y > top) {
            hf_line(dc, g_footer, bottom + cy / 2, left, right, page);
            Escape(dc, NEWFRAME, 0, NULL, NULL);
            page++;
            y = top;
            hf_line(dc, g_header, top - cy - cy / 2, left, right, page);
        }
        y += print_one(dc, i, left, y, cx, cy) + cy;
        if (i == last)
            break;
    }
    hf_line(dc, g_footer, bottom + cy / 2, left, right, page);
    if (!user_abort) {
        Escape(dc, NEWFRAME, 0, NULL, NULL);
        Escape(dc, ENDDOC, 0, NULL, NULL);
    }
done:
    EnableWindow(g_frame, TRUE);
    DestroyWindow(abort_dlg);
    abort_dlg = NULL;
    FreeProcInstance(dp);
    FreeProcInstance(ap);
    DeleteDC(dc);
}
