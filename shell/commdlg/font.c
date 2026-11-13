/*
 * font.c - w16 COMMDLG: ChooseFont, the Font dialog (template 1543,
 * FormatCharDlgProc) and its font enumeration callbacks.
 *
 * Observed on Windows 3.1 (tests/scripts/commdlg/font*.txt, ARCHITECTURE.md
 * 17):
 *  - Checks, in order: lStructSize (CDERR_STRUCTSIZE), CF_LIMITSIZE with
 *    nSizeMax < nSizeMin (CFERR_MAXLESSTHANMIN; the LOGFONT untouched);
 *    then a zero lfHeight becomes 10 points (-13 at 96 dpi) and a zero
 *    lfWeight 400 - before the fonts are counted: with none (no
 *    CF_SCREENFONTS/CF_PRINTERFONTS, or nothing passes the filters) a
 *    message box "Fonts" / string 1281 and CFERR_NOFONTS.
 *  - The face list: every family EnumFontFamilies gives, sorted by the
 *    combo box; CF_ANSIONLY keeps ANSI_CHARSET, CF_FIXEDPITCHONLY fixed
 *    pitch, CF_TTONLY TrueType, CF_SCALABLEONLY TrueType and vector,
 *    CF_NOVECTORFONTS drops vector fonts. A TrueType face has a picture
 *    (bitmap 38's first cell, 20x12) at the item's left edge, the name 20
 *    pixels in; items as high as the dialog font.
 *  - The face is chosen only with CF_INITTOLOGFONTSTRUCT and a face that
 *    is listed; the size then comes from lfHeight (positive or negative,
 *    its magnitude: points = pixels * 72 / dpi, rounded), none when
 *    lfHeight is 0; without CF_INITTOLOGFONTSTRUCT the size is 10.
 *    iPointSize is not read.
 *  - Styles, in this order: Regular, Italic, Bold, Bold Italic (strings
 *    1284-1287); a TrueType face has all four; another (raster, vector or
 *    device) face its own style and, simulated, those that add bold or
 *    italic to it (System, which is bold: Bold, Bold Italic), only its own
 *    with CF_NOSIMULATIONS. CF_USESTYLE chooses by lpszStyle's name and
 *    writes the chosen name back. A face typed in full is chosen in the
 *    list (its styles and sizes follow).
 *  - Sizes: TrueType and vector faces 8 9 10 11 12 14 16 18 20 22 24 26
 *    28 36 48 72, raster faces the sizes they have (MS Sans Serif 8 10 12
 *    14 18 24), both within CF_LIMITSIZE's range.
 *  - CF_EFFECTS: Strikeout, Underline and the colour list of the 16 VGA
 *    colours named by strings 1040-1055 (a 24x12 sample with a black
 *    frame, the name after it); without it those are hidden and
 *    disabled. Apply and Help are hidden and disabled without CF_APPLY /
 *    CF_SHOWHELP. The sample "AaBbYyZz" in the chosen font and colour,
 *    centred in the hidden static 0x444's rectangle; nothing without a
 *    face.
 *  - OK: no face - CF_NOFACESEL in Flags (likewise CF_NOSTYLESEL,
 *    CF_NOSIZESEL); a face not in the list with CF_FORCEFONTEXIST -
 *    "Invalid font name..." (1282), not a style (1283); a size not a
 *    number - 1098, outside CF_LIMITSIZE - "Size must be between %d and
 *    %d points." (1099); the dialog stays (exclamation boxes titled with
 *    the dialog's caption).
 *  - Returned: iPointSize in tenths; lfHeight = -(points * dpi / 72),
 *    lfWidth 0; lfWeight 400/700, lfItalic the enumerated face's (255 for
 *    a TrueType italic), 1 when simulated or no face; lfUnderline,
 *    lfStrikeOut 1/0 (CF_EFFECTS); lfCharSet, lfOutPrecision,
 *    lfClipPrecision, lfQuality, lfPitchAndFamily and lfFaceName the
 *    enumerated family's (e.g. Arial 0,3,2,1,22h; MS Sans Serif
 *    0,1,2,1,22h). nFontType: the enumeration's FontType bits (raster 1,
 *    TrueType 4), SCREEN_FONTTYPE, and REGULAR/ITALIC/BOLD (Bold Italic:
 *    both); a simulated style SIMULATED_FONTTYPE | REGULAR_FONTTYPE too
 *    (MS Sans Serif Bold: A501h); no face: the style bits only.
 *  - The hook receives every message, WM_INITDIALOG last of the set-up
 *    (lParam the CHOOSEFONT); no notifications while setting up.
 *  - With CF_PRINTERFONTS and hDC the printer's families come first, the
 *    screen's after; a face both have keeps the printer's LOGFONT and type
 *    with both SCREEN_ and PRINTER_FONTTYPE (PostScript Courier: 6402h);
 *    CF_WYSIWYG keeps only those. A device font has bitmap 38's second
 *    cell. With CF_PRINTERFONTS (even without hDC, as Write passes it) the
 *    line 0x445 under the sample says what the face is: TrueType (1101),
 *    printer (1102), screen (1103).
 *  - Message boxes are titled with the dialog's caption ("Fonts" for 1281)
 *    and have the information icon.
 * UNVERIFIED: when 1100 (an imitated style) is shown; the sample's
 * vertical place when the font is taller than the box; partial face names
 * typed; the colour list's own items' height.
 */
#include "commdlg.h"


#define RASTER_T        0x0001          /* EnumFontFamilies' FontType */
#define DEVICE_T        0x0002
#define TRUETYPE_T      0x0004

#define CELL_W          20              /* bitmap 38's cells */
#define CELL_H          12

/* a face of the list: its enumerated LOGFONT and type (item data) */
typedef struct {
    LOGFONT lf;
    WORD type;                          /* FontType | SCREEN/PRINTER */
} face;

/* a style of the list (item data: weight | italic << 15 | sim << 14) */
#define ST_ITALIC       0x8000
#define ST_SIM          0x4000

/* posted to ourselves when the face's text changes (UNVERIFIED: the
   original's mechanism; it chooses the face only after the combo box
   has handled the typing) */
#define WM_FACETYPED    (WM_USER + 0x7F)

typedef struct {
    LPCHOOSEFONT cf;
    HWND dlg;
    cd_hook hook;
    int dpi;
    HDC screen;
    /* enumeration */
    HWND list;                          /* the combo being filled */
    int sizes[100];                     /* raster sizes (points); a face has
                                           at most one per height a 16-bit
                                           GDI enumerates, far fewer */
    int nsizes;
    BOOL size_overflow;
    int faces;                          /* counted */
    int h0;                             /* the caller's lfHeight */
    WORD device;                        /* SCREEN_ or PRINTER_FONTTYPE:
                                           whose fonts are enumerated */
} fstate;

static HBITMAP marks;                   /* bitmap 38, both key colours set */

static void describe(fstate FAR *st);

/* ---- the pictures ---- */

/* bitmap 38 with its blue on the window colour and magenta on the
   highlight colour */
static HBITMAP load_marks(void)
{
    HRSRC r = FindResource(cd_inst, MAKEINTRESOURCE(BMP_FONTTYPE), RT_BITMAP);
    HGLOBAL h;
    LPBITMAPINFOHEADER b;
    RGBQUAD FAR *pal;
    HBITMAP bm = NULL;
    int ncol, i;
    HDC dc;
    if (!r || !(h = LoadResource(cd_inst, r)))
        return NULL;
    b = (LPBITMAPINFOHEADER)LockResource(h);
    if (b) {
        /* a copy: resources may be shared and read-only */
        DWORD size = SizeofResource(cd_inst, r), k;
        BYTE FAR *c = mem_alloc(size);
        if (c) {
            for (k = 0; k < size; k++)
                c[(WORD)k] = ((BYTE FAR *)b)[(WORD)k];
            b = (LPBITMAPINFOHEADER)c;
            ncol = b->biClrUsed ? (int)b->biClrUsed : 1 << b->biBitCount;
            pal = (RGBQUAD FAR *)((BYTE FAR *)b + (WORD)b->biSize);
            for (i = 0; i < ncol; i++) {
                COLORREF k2 = RGB(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue), n;
                if (k2 == RGB(0, 0, 255))
                    n = GetSysColor(COLOR_WINDOW);
                else if (k2 == RGB(255, 0, 255))
                    n = GetSysColor(COLOR_HIGHLIGHT);
                else
                    continue;
                pal[i].rgbRed = GetRValue(n);
                pal[i].rgbGreen = GetGValue(n);
                pal[i].rgbBlue = GetBValue(n);
            }
            dc = GetDC(NULL);
            bm = CreateDIBitmap(dc, b, CBM_INIT, (BYTE FAR *)pal + ncol * sizeof(RGBQUAD),
                                (LPBITMAPINFO)b, DIB_RGB_COLORS);
            ReleaseDC(NULL, dc);
            mem_free(c);
        }
        UnlockResource(h);
    }
    FreeResource(h);
    return bm;
}

static void draw_mark(HDC dc, int x, int y, int cell, BOOL hi)
{
    HDC mem;
    HBITMAP old;
    if (!marks || !(mem = CreateCompatibleDC(dc)))
        return;
    old = SelectObject(mem, marks);
    BitBlt(dc, x, y, CELL_W, CELL_H, mem, cell * CELL_W, hi ? CELL_H : 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

/* ---- enumeration ---- */

static BOOL wanted(fstate FAR *st, LPLOGFONT lf, int type)
{
    DWORD f = st->cf->Flags;
    if ((f & CF_ANSIONLY) && lf->lfCharSet != ANSI_CHARSET)
        return FALSE;
    if ((f & CF_FIXEDPITCHONLY) && (lf->lfPitchAndFamily & 3) != FIXED_PITCH)
        return FALSE;
    if ((f & CF_TTONLY) && !(type & TRUETYPE_T))
        return FALSE;
    if ((f & CF_SCALABLEONLY) && (type & RASTER_T))
        return FALSE;
    if ((f & CF_NOVECTORFONTS) && !(type & (RASTER_T | TRUETYPE_T | DEVICE_T)))
        return FALSE;
    return TRUE;
}

int CDPROC FontFamilyEnumProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM lp)
{
    fstate FAR *st = (fstate FAR *)lp;
    face FAR *f;
    int i;
    (void)tm;
    if (!wanted(st, lf, type))
        return 1;
    st->faces++;
    if (!st->list)
        return 1;
    i = (int)SendMessage(st->list, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)lf->lfFaceName);
    if (i >= 0) {                       /* a face both devices have: the
                                           printer's, both bits (observed) */
        f = (face FAR *)SendMessage(st->list, CB_GETITEMDATA, i, 0);
        if (f && f != (face FAR *)-1L)
            f->type |= st->device;
        return 1;
    }
    f = mem_alloc(sizeof(face));
    if (!f)
        return 0;
    f->lf = *lf;
    f->type = (WORD)type | st->device;
    i = (int)SendMessage(st->list, CB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    if (i < 0) {
        mem_free(f);
        return 0;
    }
    SendMessage(st->list, CB_SETITEMDATA, i, (LPARAM)(LPVOID)f);
    return 1;
}

/* a raster face's sizes in points, ascending, once each */
int CDPROC FontStyleEnumProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM lp)
{
    fstate FAR *st = (fstate FAR *)lp;
    int pt, i, k;
    (void)lf;
    if (!(type & RASTER_T))
        return 1;
    pt = MulDiv(tm->tmHeight - tm->tmInternalLeading, 72, st->dpi);
    for (i = 0; i < st->nsizes && st->sizes[i] < pt; i++)
        ;
    if (i < st->nsizes && st->sizes[i] == pt)
        return 1;
    if (st->nsizes == sizeof st->sizes / sizeof st->sizes[0]) {
        st->size_overflow = TRUE;
        return 1;
    }
    for (k = st->nsizes; k > i; k--)
        st->sizes[k] = st->sizes[k - 1];
    st->sizes[i] = pt;
    st->nsizes++;
    return 1;
}

/* the faces into st->list (NULL: only counted): the printer's first,
   then the screen's; CF_WYSIWYG keeps those both have */
static void enum_faces(fstate FAR *st)
{
    DWORD fl = st->cf->Flags;
    st->faces = 0;
    if ((fl & CF_PRINTERFONTS) && st->cf->hDC) {
        st->device = PRINTER_FONTTYPE;
        EnumFontFamilies(st->cf->hDC, NULL, (FONTENUMPROC)FontFamilyEnumProc, (LPSTR)st);
    }
    if (fl & CF_SCREENFONTS) {
        st->device = SCREEN_FONTTYPE;
        EnumFontFamilies(st->screen, NULL, (FONTENUMPROC)FontFamilyEnumProc, (LPSTR)st);
    }
    if (st->list && (fl & CF_WYSIWYG)) {
        int i;
        for (i = (int)SendMessage(st->list, CB_GETCOUNT, 0, 0) - 1; i >= 0; i--) {
            face FAR *f = (face FAR *)SendMessage(st->list, CB_GETITEMDATA, i, 0);
            if ((f->type & (SCREEN_FONTTYPE | PRINTER_FONTTYPE)) !=
                (SCREEN_FONTTYPE | PRINTER_FONTTYPE)) {
                mem_free(f);
                SendMessage(st->list, CB_DELETESTRING, i, 0);
            }
        }
    }
}

static int count_faces(fstate FAR *st)
{
    st->list = NULL;
    enum_faces(st);
    return st->faces;
}

/* ---- the lists ---- */

static face FAR *cur_face(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb1);
    char t[LF_FACESIZE + 1];
    int i;
    GetWindowText(cb, t, sizeof t);
    if (!t[0])
        return NULL;
    i = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)t);
    if (i < 0)
        return NULL;
    return (face FAR *)SendMessage(cb, CB_GETITEMDATA, i, 0);
}

static void add_style(HWND cb, UINT ids, WORD data)
{
    LPSTR s = load_str(ids);
    int i;
    if (!s)
        return;
    i = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)s);
    if (i >= 0)
        SendMessage(cb, CB_SETITEMDATA, i, data);
    mem_free(s);
}

/* Styles: a TrueType face all four; another face its own style (its
   enumerated weight and italic) and, simulated, those that add bold or
   italic to it (observed: System, which is bold, offers Bold and Bold
   Italic; MS Sans Serif all four, three simulated); with
   CF_NOSIMULATIONS its own only. */
static void fill_styles(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb2);
    face FAR *f = cur_face(st);
    BOOL tt = !f || (f->type & TRUETYPE_T);
    BOOL bold = !tt && f->lf.lfWeight >= FW_BOLD, ital = !tt && f->lf.lfItalic;
    BOOL nosim = (st->cf->Flags & CF_NOSIMULATIONS) != 0;
    char keep[32];
    int i, k;
    static const UINT ids[4] = { IDS_REGULAR, IDS_ITALIC, IDS_BOLD, IDS_BOLDITALIC };
    GetWindowText(cb, keep, sizeof keep);
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    for (k = 0; k < 4; k++) {
        BOOL kb = k >= 2, ki = k & 1, sim;
        if ((bold && !kb) || (ital && !ki))
            continue;
        sim = !tt && (kb != bold || ki != ital);
        if (sim && nosim)
            continue;
        add_style(cb, ids[k], (kb ? FW_BOLD : FW_NORMAL) | (ki ? ST_ITALIC : 0) |
                  (sim ? ST_SIM : 0));
    }
    i = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)keep);
    SendMessage(cb, CB_SETCURSEL, i < 0 ? 0 : i, 0);
}

static const int tt_sizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72 };

static void add_size(fstate FAR *st, HWND cb, int pt)
{
    char t[8];
    int i;
    if ((st->cf->Flags & CF_LIMITSIZE) && (pt < st->cf->nSizeMin || pt > st->cf->nSizeMax))
        return;
    s_itoa(pt, t);
    i = (int)SendMessage(cb, CB_INSERTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)t);
    if (i >= 0)
        SendMessage(cb, CB_SETITEMDATA, i, pt);
}

/* the sizes of the current face; the size typed stays */
static void fill_sizes(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb3);
    face FAR *f = cur_face(st);
    char keep[8];
    int i;
    GetWindowText(cb, keep, sizeof keep);
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    if (f && (f->type & RASTER_T)) {
        st->nsizes = 0;
        EnumFontFamilies((f->type & SCREEN_FONTTYPE) || !st->cf->hDC ? st->screen : st->cf->hDC,
                         f->lf.lfFaceName, (FONTENUMPROC)FontStyleEnumProc,
                         (LPSTR)st);
        for (i = 0; i < st->nsizes; i++)
            add_size(st, cb, st->sizes[i]);
    } else {
        for (i = 0; i < (int)(sizeof tt_sizes / sizeof tt_sizes[0]); i++)
            add_size(st, cb, tt_sizes[i]);
    }
    i = (int)SendMessage(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)keep);
    if (i >= 0)
        SendMessage(cb, CB_SETCURSEL, i, 0);
    else
        SetWindowText(cb, keep);
}

static const COLORREF colours[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(128, 128, 128),
    RGB(192, 192, 192), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255)
};

static void fill_colours(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb4);
    int i, k, sel = -1;
    for (k = 0; k < 16; k++) {
        LPSTR s = load_str(IDS_COLORNAMES + k);
        if (!s)
            continue;
        i = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)s);
        mem_free(s);
        if (i < 0)
            continue;
        SendMessage(cb, CB_SETITEMDATA, i, colours[k]);
        if (colours[k] == st->cf->rgbColors)
            sel = i;
    }
    SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static void hide(HWND dlg, int id)
{
    HWND w = GetDlgItem(dlg, id);
    if (w) {
        ShowWindow(w, SW_HIDE);
        EnableWindow(w, FALSE);
    }
}

/* points from a LOGFONT height (either sign) */
static int points_of(fstate FAR *st, int h)
{
    if (h < 0)
        h = -h;
    return MulDiv(h, 72, st->dpi);
}

static void init(fstate FAR *st)
{
    LPCHOOSEFONT cf = st->cf;
    HWND dlg = st->dlg;
    DWORD fl = cf->Flags;
    int i;
    char t[8];
    BOOL init_lf = (fl & CF_INITTOLOGFONTSTRUCT) != 0;

    st->list = GetDlgItem(dlg, cmb1);
    enum_faces(st);
    st->list = NULL;
    i = -1;
    if (init_lf && cf->lpLogFont->lfFaceName[0])
        i = (int)SendDlgItemMessage(dlg, cmb1, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                    (LPARAM)(LPSTR)cf->lpLogFont->lfFaceName);
    SendDlgItemMessage(dlg, cmb1, CB_SETCURSEL, i, 0);

    /* the style */
    fill_styles(st);
    if ((fl & CF_USESTYLE) && cf->lpszStyle) {
        i = (int)SendDlgItemMessage(dlg, cmb2, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                    (LPARAM)cf->lpszStyle);
    } else {
        int n = (int)SendDlgItemMessage(dlg, cmb2, CB_GETCOUNT, 0, 0), k;
        WORD want = (cf->lpLogFont->lfWeight >= FW_BOLD ? FW_BOLD : FW_NORMAL) |
                    (cf->lpLogFont->lfItalic ? ST_ITALIC : 0);
        i = 0;
        for (k = 0; k < n; k++)
            if (((WORD)SendDlgItemMessage(dlg, cmb2, CB_GETITEMDATA, k, 0) & ~ST_SIM) == want)
                i = k;
    }
    SendDlgItemMessage(dlg, cmb2, CB_SETCURSEL, i < 0 ? 0 : i, 0);

    /* the size */
    t[0] = 0;
    if (!init_lf)
        s_itoa(10, t);
    else if (st->h0)
        s_itoa(points_of(st, st->h0), t);
    SetDlgItemText(dlg, cmb3, t);
    fill_sizes(st);

    if (fl & CF_EFFECTS) {
        CheckDlgButton(dlg, chx1, cf->lpLogFont->lfStrikeOut != 0);
        CheckDlgButton(dlg, chx2, cf->lpLogFont->lfUnderline != 0);
        fill_colours(st);
    } else {
        hide(dlg, grp1);
        hide(dlg, chx1);
        hide(dlg, chx2);
        hide(dlg, stc4);
        hide(dlg, cmb4);
    }
    describe(st);
    if (!(fl & CF_APPLY))
        hide(dlg, psh3);
    if (!(fl & CF_SHOWHELP))
        hide(dlg, pshHelp);
}

/* ---- the choice ---- */

typedef struct {
    face FAR *f;
    int style;                          /* index, -1: none */
    WORD sdata;
    int pt;                             /* 0: none */
    BOOL pt_bad;
    char face_text[LF_FACESIZE + 1];
    char style_text[32];
} choice;

static void get_choice(fstate FAR *st, choice FAR *c)
{
    HWND dlg = st->dlg;
    char t[16];
    BOOL ok;
    mem_zero(c, sizeof *c);
    GetDlgItemText(dlg, cmb1, c->face_text, sizeof c->face_text);
    c->f = cur_face(st);
    GetDlgItemText(dlg, cmb2, c->style_text, sizeof c->style_text);
    c->style = c->style_text[0]
        ? (int)SendDlgItemMessage(dlg, cmb2, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                  (LPARAM)(LPSTR)c->style_text)
        : -1;
    c->sdata = c->style >= 0 ? (WORD)SendDlgItemMessage(dlg, cmb2, CB_GETITEMDATA, c->style, 0)
                             : FW_NORMAL;
    GetDlgItemText(dlg, cmb3, t, sizeof t);
    if (t[0]) {
        c->pt = s_atoi(t, &ok);
        if (!ok) {
            c->pt = 0;
            c->pt_bad = TRUE;
        }
    }
}

/* the LOGFONT of a choice (the caller's own for what is not chosen) */
static void make_lf(fstate FAR *st, const choice FAR *c, LPLOGFONT lf)
{
    if (c->f) {
        LOGFONT e = c->f->lf;
        lf->lfCharSet = e.lfCharSet;
        lf->lfOutPrecision = e.lfOutPrecision;
        lf->lfClipPrecision = e.lfClipPrecision;
        lf->lfQuality = e.lfQuality;
        lf->lfPitchAndFamily = e.lfPitchAndFamily;
        s_cpy(lf->lfFaceName, e.lfFaceName, LF_FACESIZE);
    }
    if (c->pt)
        lf->lfHeight = -MulDiv(c->pt, st->dpi, 72);
    lf->lfWidth = 0;
    if (c->style >= 0) {
        lf->lfWeight = c->sdata & 0x0FFF;
        if (!(c->sdata & ST_ITALIC))
            lf->lfItalic = 0;
        else if (c->f && (c->f->type & TRUETYPE_T))
            lf->lfItalic = 255;
        else
            lf->lfItalic = 1;
    }
    if (st->cf->Flags & CF_EFFECTS) {
        lf->lfStrikeOut = (BYTE)IsDlgButtonChecked(st->dlg, chx1);
        lf->lfUnderline = (BYTE)IsDlgButtonChecked(st->dlg, chx2);
    }
}

static COLORREF cur_colour(fstate FAR *st)
{
    int i = (int)SendDlgItemMessage(st->dlg, cmb4, CB_GETCURSEL, 0, 0);
    return i < 0 ? st->cf->rgbColors : (COLORREF)SendDlgItemMessage(st->dlg, cmb4, CB_GETITEMDATA, i, 0);
}

static void sample_rect(fstate FAR *st, RECT FAR *r)
{
    HWND s = GetDlgItem(st->dlg, stc5);
    GetWindowRect(s, r);
    ScreenToClient(st->dlg, (LPPOINT)&r->left);
    ScreenToClient(st->dlg, (LPPOINT)&r->right);
}

/* the line under the sample, with CF_PRINTERFONTS only (observed: empty
   with screen fonts alone; Write, with no printer, has it). UNVERIFIED:
   when 1100 (an imitated style) comes before the others. */
static void describe(fstate FAR *st)
{
    face FAR *f;
    UINT ids = 0;
    LPSTR t;
    if (!(st->cf->Flags & CF_PRINTERFONTS))  /* even without hDC (Write) */
        return;
    f = cur_face(st);
    if (f) {
        int i = (int)SendDlgItemMessage(st->dlg, cmb2, CB_GETCURSEL, 0, 0);
        WORD sd = i >= 0 ? (WORD)SendDlgItemMessage(st->dlg, cmb2, CB_GETITEMDATA, i, 0) : 0;
        if (f->type & TRUETYPE_T)
            ids = IDS_FONTTT;
        else if (sd & ST_SIM)
            ids = IDS_FONTSIMULATED;
        else if (f->type & PRINTER_FONTTYPE)
            ids = IDS_FONTPRINTER;
        else
            ids = IDS_FONTSCREEN;
    }
    t = ids ? load_str(ids) : NULL;
    SetDlgItemText(st->dlg, stc6, t ? t : "");
    mem_free(t);
}

static void update_sample(fstate FAR *st)
{
    RECT r;
    describe(st);
    sample_rect(st, &r);
    InvalidateRect(st->dlg, &r, TRUE);
}

static void paint_sample(fstate FAR *st, HDC dc)
{
    choice c;
    LOGFONT lf;
    HFONT f, of;
    RECT r;
    get_choice(st, &c);
    if (!c.f)
        return;
    lf = *st->cf->lpLogFont;
    make_lf(st, &c, &lf);
    if (!c.pt)
        lf.lfHeight = st->cf->lpLogFont->lfHeight;
    f = CreateFontIndirect(&lf);
    if (!f)
        return;
    sample_rect(st, &r);
    of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (st->cf->Flags & CF_EFFECTS) ? cur_colour(st) : RGB(0, 0, 0));
    {   /* centred on the characters' height above the baseline without
           the internal leading, an odd spare pixel below (observed in
           seven fonts, tests/scripts/commdlg/font5.txt) */
        TEXTMETRIC tm;
        int w = LOWORD(GetTextExtent(dc, "AaBbYyZz", 8));
        int body;
        GetTextMetrics(dc, &tm);
        body = tm.tmAscent - tm.tmInternalLeading;
        /* wider than the box: from its left edge (observed) */
        ExtTextOut(dc, r.left + (w < r.right - r.left ? (r.right - r.left - w) / 2 : 0),
                   r.top + (r.bottom - r.top - body + 1) / 2 - tm.tmInternalLeading,
                   ETO_CLIPPED, &r, "AaBbYyZz", 8, NULL);
    }
    SelectObject(dc, of);
    DeleteObject(f);
}

/* OK: check what was chosen, fill in the structure. FALSE: stay. */
static BOOL finish(fstate FAR *st)
{
    LPCHOOSEFONT cf = st->cf;
    HWND dlg = st->dlg;
    choice c;
    WORD type = 0;
    char cap[80];
    GetWindowText(dlg, cap, sizeof cap);
    get_choice(st, &c);
    if (c.pt_bad) {
        cd_msgbox(dlg, cap, IDS_SIZENUMBER, NULL, MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dlg, cmb3));
        return FALSE;
    }
    if (c.pt && (cf->Flags & CF_LIMITSIZE) && (c.pt < cf->nSizeMin || c.pt > cf->nSizeMax)) {
        LPSTR fmt = load_str(IDS_SIZERANGE);
        char text[128];
        if (fmt) {
            wsprintf(text, fmt, cf->nSizeMin, cf->nSizeMax);
            MessageBox(dlg, text, cap, MB_OK | MB_ICONINFORMATION);
            mem_free(fmt);
        }
        SetFocus(GetDlgItem(dlg, cmb3));
        return FALSE;
    }
    /* observed: the Flags bits go in after the size checks, before the
       face checks (they stay when the dialog is then cancelled) */
    cf->Flags &= ~(CF_NOFACESEL | CF_NOSTYLESEL | CF_NOSIZESEL);
    if (!c.face_text[0])
        cf->Flags |= CF_NOFACESEL;
    if (c.style < 0)
        cf->Flags |= CF_NOSTYLESEL;
    if (!c.pt)
        cf->Flags |= CF_NOSIZESEL;
    if (!c.face_text[0])
        cf->lpLogFont->lfFaceName[0] = 0;
    cf->iPointSize = c.pt ? c.pt * 10 : points_of(st, cf->lpLogFont->lfHeight) * 10;
    if (!c.f && (cf->Flags & CF_FORCEFONTEXIST)) {
        cd_msgbox(dlg, cap, IDS_BADFACE, NULL, MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dlg, cmb1));
        return FALSE;
    }
    if (c.style_text[0] && c.style < 0 && (cf->Flags & CF_FORCEFONTEXIST)) {
        cd_msgbox(dlg, cap, IDS_BADSTYLE, NULL, MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dlg, cmb2));
        return FALSE;
    }
    make_lf(st, &c, cf->lpLogFont);
    if (!c.face_text[0])
        cf->lpLogFont->lfFaceName[0] = 0;
    if (c.f)
        type = c.f->type;
    if (c.style >= 0) {
        if (c.sdata & ST_SIM) {         /* and the face's own style */
            type |= SIMULATED_FONTTYPE;
            if (c.f && c.f->lf.lfWeight >= FW_BOLD)
                type |= BOLD_FONTTYPE;
            if (c.f && c.f->lf.lfItalic)
                type |= ITALIC_FONTTYPE;
            if (!c.f || (c.f->lf.lfWeight < FW_BOLD && !c.f->lf.lfItalic))
                type |= REGULAR_FONTTYPE;
        }
        if ((c.sdata & 0x0FFF) >= FW_BOLD)
            type |= BOLD_FONTTYPE;
        if (c.sdata & ST_ITALIC)
            type |= ITALIC_FONTTYPE;
        if (!(type & (BOLD_FONTTYPE | ITALIC_FONTTYPE)))
            type |= REGULAR_FONTTYPE;
    }
    cf->nFontType = type;
    if (cf->Flags & CF_EFFECTS)
        cf->rgbColors = cur_colour(st);
    if ((cf->Flags & CF_USESTYLE) && cf->lpszStyle)
        s_cpy(cf->lpszStyle, c.style_text, LF_FACESIZE);
    return TRUE;
}

/* ---- drawing the owner-drawn lists ---- */

static void draw_list_item(fstate FAR *st, const DRAWITEMSTRUCT FAR *d)
{
    char t[LF_FACESIZE + 8];
    BOOL hi = (d->itemState & ODS_SELECTED) != 0;
    HBRUSH b;
    int x = d->rcItem.left;
    TEXTMETRIC tm;
    (void)st;
    if (d->itemID == (UINT)-1) {
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(d->hDC, &d->rcItem);
        return;
    }
    SendMessage(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)(LPSTR)t);
    b = CreateSolidBrush(GetSysColor(hi ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    FillRect(d->hDC, &d->rcItem, b);
    DeleteObject(b);
    if (d->CtlID == cmb1) {
        face FAR *f = (face FAR *)d->itemData;
        if (f && (f->type & TRUETYPE_T))
            draw_mark(d->hDC, x, d->rcItem.top, 0, hi);
        else if (f && (f->type & DEVICE_T))
            draw_mark(d->hDC, x, d->rcItem.top, 1, hi);
        x += CELL_W;
    } else if (d->CtlID == cmb4) {
        HBRUSH cb = CreateSolidBrush((COLORREF)d->itemData), ob;
        HPEN op = SelectObject(d->hDC, GetStockObject(BLACK_PEN));
        ob = SelectObject(d->hDC, cb);
        Rectangle(d->hDC, x + 1, d->rcItem.top + 1, x + 25, d->rcItem.bottom - 1);
        SelectObject(d->hDC, ob);
        SelectObject(d->hDC, op);
        DeleteObject(cb);
        x += 27;
    } else {
        x += 1;
    }
    GetTextMetrics(d->hDC, &tm);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, GetSysColor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    TextOut(d->hDC, x, d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - tm.tmHeight) / 2,
            t, s_len(t));
    if (d->itemState & ODS_FOCUS)
        DrawFocusRect(d->hDC, &d->rcItem);
}

static void measure(HWND dlg, MEASUREITEMSTRUCT FAR *m)
{
    HDC dc = GetDC(dlg);
    HFONT f = (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0), of = NULL;
    TEXTMETRIC tm;
    if (f)
        of = SelectObject(dc, f);
    GetTextMetrics(dc, &tm);
    if (of)
        SelectObject(dc, of);
    ReleaseDC(dlg, dc);
    /* observed: the selection fields one pixel higher than the items */
    m->itemHeight = tm.tmHeight + (m->itemID == (UINT)-1 ? 1 : 0);
}

/* ---- the dialog procedure ---- */

static void free_faces(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, cmb1);
    int i, n = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);
    for (i = 0; i < n; i++) {
        LPVOID p = (LPVOID)SendMessage(cb, CB_GETITEMDATA, i, 0);
        if (p && p != (LPVOID)-1L)
            mem_free(p);
        SendMessage(cb, CB_SETITEMDATA, i, 0);
    }
}

BOOL CDPROC FormatCharDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    fstate FAR *st;
    if (msg == WM_INITDIALOG) {
        st = (fstate FAR *)lp;
        st->dlg = dlg;
        state_set(dlg, st);
        init(st);
        if (st->hook)
            return (BOOL)call_hook(st->hook, dlg, msg, wp, (LPARAM)st->cf);
        return TRUE;
    }
    if (msg == WM_MEASUREITEM) {
        measure(dlg, (MEASUREITEMSTRUCT FAR *)lp);
        return TRUE;
    }
    st = (fstate FAR *)state_get(dlg);
    if (!st)
        return FALSE;
    if (st->hook) {
        UINT r = call_hook(st->hook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    switch (msg) {
    case WM_DRAWITEM:
        draw_list_item(st, (const DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(dlg, &ps);
        paint_sample(st, ps.hdc);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_FACETYPED: {
        /* a face typed in full: chosen in the list (observed) */
        char t[LF_FACESIZE + 1];
        int i, cur = (int)SendDlgItemMessage(dlg, cmb1, CB_GETCURSEL, 0, 0);
        GetDlgItemText(dlg, cmb1, t, sizeof t);
        i = (int)SendDlgItemMessage(dlg, cmb1, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)t);
        if (i >= 0 && i != cur) {
            DWORD sel = SendDlgItemMessage(dlg, cmb1, CB_GETEDITSEL, 0, 0);
            SendDlgItemMessage(dlg, cmb1, CB_SETCURSEL, i, 0);
            SendDlgItemMessage(dlg, cmb1, CB_SETEDITSEL, 0, sel);
            fill_styles(st);
            fill_sizes(st);
            update_sample(st);
        }
        return TRUE;
    }
    case WM_CHOOSEFONT_GETLOGFONT: {
        choice c;
        LPLOGFONT lf = (LPLOGFONT)lp;
        *lf = *st->cf->lpLogFont;
        get_choice(st, &c);
        make_lf(st, &c, lf);
        return TRUE;
    }
    case WM_DESTROY:
        free_faces(dlg);
        state_remove(dlg);
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            if (finish(st))
                EndDialog(dlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, FALSE);
            return TRUE;
        case pshHelp:
            send_help(dlg, st->cf->hwndOwner, (LPARAM)st->cf);
            return TRUE;
        case cmb1:
            if (HIWORD(lp) == CBN_EDITCHANGE) {
                /* after the combo box's own handling of the change */
                PostMessage(dlg, WM_FACETYPED, 0, 0);
                return TRUE;
            }
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, cmb1, CB_GETCURSEL, 0, 0);
                char t[LF_FACESIZE + 1];
                if (i >= 0) {
                    SendDlgItemMessage(dlg, cmb1, CB_GETLBTEXT, i, (LPARAM)(LPSTR)t);
                    SetDlgItemText(dlg, cmb1, t);
                }
                fill_styles(st);
                fill_sizes(st);
                update_sample(st);
            }
            return TRUE;
        case cmb2:
        case cmb3:
        case cmb4:
            if (HIWORD(lp) == CBN_SELCHANGE || HIWORD(lp) == CBN_EDITCHANGE)
                update_sample(st);
            return TRUE;
        case chx1:
        case chx2:
            update_sample(st);
            return FALSE;
        }
        break;
    }
    return FALSE;
}

BOOL CDAPI ChooseFont(LPCHOOSEFONT cf)
{
    fstate FAR *st;
    cd_template t;
    LPLOGFONT lf;
    int r;
    cd_seterr(0);
    if (!cf) {
        cd_seterr(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (cf->lStructSize != sizeof(CHOOSEFONT)) {
        cd_seterr(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if ((cf->Flags & CF_ENABLEHOOK) && !cf->lpfnHook) {
        cd_seterr(CDERR_NOHOOK);
        return FALSE;
    }
    if ((cf->Flags & CF_LIMITSIZE) && cf->nSizeMax < cf->nSizeMin) {
        cd_seterr(CFERR_MAXLESSTHANMIN);
        return FALSE;
    }
    lf = cf->lpLogFont;
    st = mem_alloc(sizeof(fstate));     /* at offset 0: state_get */
    if (!st || !lf) {
        mem_free(st);
        cd_seterr(st ? CDERR_INITIALIZATION : CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    st->cf = cf;
    st->hook = (cf->Flags & CF_ENABLEHOOK) ? (cd_hook)cf->lpfnHook : NULL;
    st->screen = GetDC(NULL);
    st->dpi = GetDeviceCaps(st->screen, LOGPIXELSY);
    /* observed: the defaults go in before the fonts are counted; the size
       shown comes from the caller's height */
    {
        st->h0 = lf->lfHeight;
        if (!lf->lfHeight)
            lf->lfHeight = -MulDiv(10, st->dpi, 72);
        if (!lf->lfWeight)
            lf->lfWeight = FW_NORMAL;
        if (!count_faces(st)) {
            ReleaseDC(NULL, st->screen);
            mem_free(st);
            {
                LPSTR title = load_str(IDS_FONTS_T);
                cd_msgbox(cf->hwndOwner, title ? title : "", IDS_NOFONTS, NULL,
                          MB_OK | MB_ICONINFORMATION);
                mem_free(title);
            }
            cd_seterr(CFERR_NOFONTS);
            return FALSE;
        }
    }
    if (!marks)
        marks = load_marks();
    t.app = cf->hInstance;
    t.use_handle = (cf->Flags & CF_ENABLETEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (cf->Flags & CF_ENABLETEMPLATE);
    t.name = cf->lpTemplateName;
    t.ours = MAKEINTRESOURCE(DLG_FONT);
    r = cd_modal(&t, cf->hwndOwner, (FARPROC)FormatCharDlgProc, (LPARAM)(LPVOID)st);
    ReleaseDC(NULL, st->screen);
    mem_free(st);
    return r == 1;
}

