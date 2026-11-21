/*
 * fonts.c - the Fonts applet: the fonts installed in WIN.INI [fonts], a
 * sample of the one selected, Add (a file dialog over a directory's font
 * files), Remove, and the TrueType options.
 *
 * Observed on Windows 3.1 (black box: SNAP window dumps, screenshots and
 * the INI files written; docs/notes/cpl-fonts-ports-handover.md):
 * - The list: the keys of [fonts], sorted, several selectable (extended
 *   selection); none selected at first. Below the sample: "No fonts are
 *   selected.", "This is a screen or plotter font.", "This is a scalable
 *   TrueType font that can be displayed on the screen and printed on your
 *   printer." or, with several selected, "Windows cannot display samples
 *   of more than one font at a time."; and "The size of the font on the
 *   disk is:  N KB." (two blanks), N the sum of each file's size in KB
 *   rounded up, a TrueType font's .FOT and .TTF each counted (Arial Bold:
 *   2 + 65 = 67).
 * - The sample (an owner-drawn list): "AaBbCcXxYyZz 123" once per size of
 *   a raster font file, once at 24 points for a TrueType font, once at its
 *   nominal size for a vector font.
 * - The default button (IDOK) reads "Cancel"; "Close" once a font was
 *   added or removed (from the Remove button on) or a TrueType option
 *   clicked.
 * - Add: COMMDLG's open dialog with a template of its own ("Add Fonts":
 *   List of Fonts, Select All, Copy Fonts to Windows Directory, checked);
 *   the list names each *.TTF and *.FON of the directory by its
 *   description (a .FON's NE description after "FONTRES ...:", a .TTF's
 *   full name and " (TrueType)"), "No fonts found." (disabled) when none.
 *   OK closes it, then each chosen font: if [fonts] has its name already,
 *   "The <name> font is already installed. To install a new version, first
 *   remove the old version." (OK goes on, Cancel stops); else, copied, the
 *   file goes to the System directory under its name, or with its last
 *   letter replaced by 0, 1 ... when that name is taken (EGA40WOA.FON as
 *   EGA40WO0.FON), and [fonts] gets "<name>=<file name>"; not copied, the
 *   full path (C:\NEWFONTS\EGA40WOA.FON). A .TTF gets a .FOT of its name
 *   in the System directory (CreateScalableFontResource), and that is the
 *   value (LIBSANS.FOT copied, C:\WINDOWS\SYSTEM\LIBSANS.FOT not). The keys
 *   are appended to [fonts]; the new names are selected.
 * - Remove: for each selected font a "Remove Font" dialog: "Are you sure
 *   you want to remove the <name> font?", Delete Font File From Disk, Yes,
 *   Yes to All, No, Cancel. Yes takes the key out of [fonts] and the font
 *   out of GDI (RemoveFontResource) and, with the box checked, deletes the
 *   file.
 * - TrueType: Enable TrueType Fonts and Show Only TrueType Fonts in
 *   Applications (disabled while the first is off), WIN.INI [TrueType]
 *   TTEnable and TTOnly, both written on OK (1/0); a change of TTEnable asks
 *   to restart ("System Setting Change").
 * w16's GDI has no TrueType rasterizer: the settings are kept as 3.1 keeps
 * them and take effect only with a GDI that has one.
 * UNVERIFIED: dropping files from File Manager on the dialog (its window
 * has WS_EX_ACCEPTFILES; w16 installs them as Add does without copying);
 * compressed files (*.FO_, *.TT_, which 3.1's dialog lists: w16 skips
 * them); what 3.1 says when a font cannot be added.
 */
#include "maincpl.h"
#include <commdlg.h>
#include <shellapi.h>
#include "cplutil.h"

static char s_fonts[] = "fonts";
static char s_tt[] = "TrueType";
static char s_sample[] = "AaBbCcXxYyZz 123";

static HWND fonts_dlg;

/* ---- reading font files ---- */

#define FD_TYPE   66            /* in a FONTDIR entry, after its ordinal */
#define FD_ITALIC 80
#define FD_WEIGHT 83
#define FD_CHARSET 85
#define FD_HEIGHT 88
#define FD_PITCH  90
#define FD_FIXED  113           /* then the device and face names */

typedef struct {
    int height, weight, avgwidth;
    BYTE italic, charset, pitch, vector;
    char face[LF_FACESIZE];
} FACEINFO;

static WORD rd16(BYTE *p)
{
    return p[0] | (p[1] << 8);
}

static WORD be16(BYTE *p)
{
    return (p[0] << 8) | p[1];
}

static DWORD be32(BYTE *p)
{
    return ((DWORD)be16(p) << 16) | be16(p + 2);
}

/* the font directory (RT_FONTDIR) of an NE font file, in a global block;
   NULL if none */
static HGLOBAL read_fontdir(const char *path, UINT *len)
{
    HFILE f;
    OFSTRUCT of;
    BYTE h[64], t[8], n[12];
    DWORD ne;
    WORD rsrc, shift;
    HGLOBAL g = NULL;
    f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        return NULL;
    if (_lread(f, h, 64) != 64 || h[0] != 'M' || h[1] != 'Z')
        goto done;
    ne = *(DWORD *)(h + 0x3C);
    _llseek(f, ne, 0);
    if (_lread(f, h, 64) != 64 || h[0] != 'N' || h[1] != 'E')
        goto done;
    rsrc = rd16(h + 0x24);
    _llseek(f, ne + rsrc, 0);
    if (_lread(f, h, 2) != 2)
        goto done;
    shift = rd16(h);
    for (;;) {
        WORD type, count;
        if (_lread(f, t, 8) != 8 || !(type = rd16(t)))
            goto done;
        count = rd16(t + 2);
        if (type != 0x8007) {
            _llseek(f, (long)count * 12, 1);
            continue;
        }
        if (!count || _lread(f, n, 12) != 12)
            goto done;
        *len = rd16(n + 2) << shift;
        g = GlobalAlloc(GMEM_MOVEABLE, *len ? *len : 1);
        if (g) {
            BYTE FAR *p = (BYTE FAR *)GlobalLock(g);
            _llseek(f, (long)rd16(n) << shift, 0);
            if (_lread(f, p, *len) != *len) {
                GlobalUnlock(g);
                GlobalFree(g);
                g = NULL;
            } else
                GlobalUnlock(g);
        }
        goto done;
    }
done:
    _lclose(f);
    return g;
}

/* entry k of a font directory; FALSE past the end */
static BOOL fontdir_entry(BYTE FAR *p, UINT len, int k, FACEINFO *fi)
{
    UINT off = 2, i;
    int n = rd16(p);
    if (k >= n)
        return FALSE;
    for (;;) {
        BYTE FAR *e = p + off + 2;      /* after the ordinal */
        UINT s;
        if (off + 2 + FD_FIXED > len)
            return FALSE;
        s = off + 2 + FD_FIXED;
        while (s < len && p[s])         /* the device name */
            s++;
        s++;
        if (!k) {
            fi->height = rd16(e + FD_HEIGHT);
            fi->avgwidth = rd16(e + 91);
            fi->weight = rd16(e + FD_WEIGHT);
            fi->italic = e[FD_ITALIC];
            fi->charset = e[FD_CHARSET];
            fi->pitch = e[FD_PITCH];
            fi->vector = rd16(e + FD_TYPE) & 1;
            for (i = 0; i < LF_FACESIZE - 1 && s + i < len && p[s + i]; i++)
                fi->face[i] = p[s + i];
            fi->face[i] = 0;
            return TRUE;
        }
        while (s < len && p[s])         /* the face name */
            s++;
        off = s + 1;
        k--;
    }
}

/* a TrueType file's full name (name ID 4: Microsoft Unicode, else
   Macintosh Roman), from the TrueType specification's tables */
static BOOL ttf_name(const char *path, char *buf, int size)
{
    HFILE f;
    OFSTRUCT of;
    BYTE h[12], r[16];
    WORD n, i, count, strs;
    DWORD off = 0;
    BOOL ok = FALSE;
    int best = 0;
    DWORD boff = 0;
    WORD blen = 0;
    f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        return FALSE;
    if (_lread(f, h, 12) != 12)
        goto done;
    n = be16(h + 4);
    for (i = 0; i < n; i++) {
        if (_lread(f, r, 16) != 16)
            goto done;
        if (r[0] == 'n' && r[1] == 'a' && r[2] == 'm' && r[3] == 'e')
            off = be32(r + 8);
    }
    if (!off)
        goto done;
    _llseek(f, off, 0);
    if (_lread(f, h, 6) != 6)
        goto done;
    count = be16(h + 2);
    strs = be16(h + 4);
    for (i = 0; i < count; i++) {
        int score = 0;
        if (_lread(f, r, 12) != 12)
            goto done;
        if (be16(r + 6) != 4)
            continue;
        if (be16(r) == 3 && be16(r + 2) == 1)
            score = be16(r + 4) == 0x409 ? 3 : 2;
        else if (be16(r) == 1 && be16(r + 2) == 0)
            score = 1;
        if (score > best) {
            best = score;
            blen = be16(r + 8);
            boff = off + strs + be16(r + 10);
        }
    }
    if (best) {
        int k = 0;
        BYTE c[2];
        _llseek(f, boff, 0);
        while (blen && k < size - 1) {
            if (best >= 2) {
                if (_lread(f, c, 2) != 2)
                    break;
                blen = blen >= 2 ? blen - 2 : 0;
                buf[k++] = c[0] ? '?' : c[1];
            } else {
                if (_lread(f, c, 1) != 1)
                    break;
                blen--;
                buf[k++] = c[0];
            }
        }
        buf[k] = 0;
        ok = k > 0;
    }
done:
    _lclose(f);
    return ok;
}

static BOOL has_ext(const char *name, const char *ext)
{
    int n = lstrlen(name);
    return n > 4 && !lstrcmpi(name + n - 4, ext);
}

/* the name a font file is listed under; FALSE if it is not one */
static BOOL font_desc(const char *path, char *buf, int size)
{
    char d[256], *p;
    if (has_ext(path, ".TTF")) {
        if (!ttf_name(path, d, sizeof d - 12))
            return FALSE;
        lstrcat(d, " (TrueType)");
        lstrcpyn(buf, d, size);
        return TRUE;
    }
    if (!cpl_ne_desc(path, d, sizeof d))
        return FALSE;
    for (p = d; *p && *p != ':'; p++)
        ;
    p = *p ? p + 1 : d;
    while (*p == ' ')
        p++;
    lstrcpyn(buf, p, size);
    return TRUE;
}

/* where a [fonts] value is: OpenFile searches as GDI does (the current,
   Windows and System directories, the PATH) */
static BOOL font_path(const char *value, char *path)
{
    OFSTRUCT of;
    if (OpenFile(value, &of, OF_EXIST) == HFILE_ERROR)
        return FALSE;
    lstrcpy(path, of.szPathName);
    return TRUE;
}

/* the .TTF that goes with a .FOT: the same name beside it, else in the
   System directory (UNVERIFIED: 3.1 may read the name out of the .FOT) */
static BOOL ttf_of(const char *fot, char *ttf)
{
    OFSTRUCT of;
    char *b;
    int n;
    lstrcpy(ttf, fot);
    n = lstrlen(ttf);
    lstrcpy(ttf + n - 4, ".TTF");
    if (OpenFile(ttf, &of, OF_EXIST) != HFILE_ERROR)
        return TRUE;
    for (b = ttf + n; b > ttf && b[-1] != '\\' && b[-1] != ':'; b--)
        ;
    return OpenFile(b, &of, OF_EXIST) != HFILE_ERROR && (lstrcpy(ttf, of.szPathName), TRUE);
}

static long kb_of(const char *path)
{
    long n = cpl_file_size(path);
    return n < 0 ? 0 : (n + 1023) / 1024;
}

/* ---- the sample ---- */

static HGLOBAL sample_fonts;    /* HFONTs, one per item */
static int sample_count;

static void sample_clear(HWND dlg)
{
    if (sample_fonts) {
        HFONT FAR *f = (HFONT FAR *)GlobalLock(sample_fonts);
        int i;
        SendDlgItemMessage(dlg, IDC_FN_SAMPLE, LB_RESETCONTENT, 0, 0);
        for (i = 0; i < sample_count; i++)
            DeleteObject(f[i]);
        GlobalUnlock(sample_fonts);
        GlobalFree(sample_fonts);
        sample_fonts = NULL;
    } else
        SendDlgItemMessage(dlg, IDC_FN_SAMPLE, LB_RESETCONTENT, 0, 0);
    sample_count = 0;
}

static void sample_add(HWND dlg, FACEINFO *fi, BOOL scalable)
{
    HGLOBAL g;
    HFONT f, FAR *a;
    HDC dc;
    int h = fi->height, w = 0;
    if (fi->vector) {
        /* a vector font at its directory's nominal size (UNVERIFIED: fitted
           to 3.1's Modern sample) */
        w = fi->avgwidth;
    } else if (scalable) {
        dc = GetDC(dlg);
        h = -MulDiv(24, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(dlg, dc);
    }
    f = CreateFont(h, w, 0, 0, fi->weight, fi->italic, 0, 0, fi->charset,
                   fi->vector ? OUT_STROKE_PRECIS : OUT_DEFAULT_PRECIS,
                   CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, fi->pitch, fi->face);
    if (!f)
        return;
    g = sample_fonts ? GlobalReAlloc(sample_fonts, (sample_count + 1) * sizeof(HFONT), GMEM_MOVEABLE)
                     : GlobalAlloc(GMEM_MOVEABLE, sizeof(HFONT));
    if (!g) {
        DeleteObject(f);
        return;
    }
    sample_fonts = g;
    a = (HFONT FAR *)GlobalLock(g);
    a[sample_count] = f;
    GlobalUnlock(g);
    /* counted first: WM_MEASUREITEM comes during LB_ADDSTRING */
    sample_count++;
    SendDlgItemMessage(dlg, IDC_FN_SAMPLE, LB_ADDSTRING, 0, (LPARAM)(sample_count - 1));
}

static HFONT sample_font(int i)
{
    HFONT f = NULL, FAR *a;
    if (sample_fonts && i >= 0 && i < sample_count) {
        a = (HFONT FAR *)GlobalLock(sample_fonts);
        f = a[i];
        GlobalUnlock(sample_fonts);
    }
    return f;
}

static void sample_of(HWND dlg, const char *path)
{
    BOOL tt = has_ext(path, ".FOT");
    UINT len;
    HGLOBAL g = read_fontdir(path, &len);
    FACEINFO fi;
    int k;
    if (!g)
        return;
    for (k = 0; fontdir_entry((BYTE FAR *)GlobalLock(g), len, k, &fi); k++) {
        GlobalUnlock(g);
        if (tt)
            fi.vector = 0;      /* a .FOT's entry: drawn at 24 points */
        sample_add(dlg, &fi, tt || fi.vector);
        if (tt || fi.vector)
            break;
    }
    GlobalUnlock(g);
    GlobalFree(g);
}

/* ---- the Fonts dialog ---- */

static void set_close(void)
{
    char t[32];
    LoadString(g_hinst, IDS_CLOSE, t, sizeof t);
    SetDlgItemText(fonts_dlg, IDOK, t);
}

static void show_selection(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FN_LIST);
    int n = (int)SendMessage(list, LB_GETSELCOUNT, 0, 0), i, cnt;
    long kb = 0;
    char name[128], value[160], path[160], ttf[160], t[200], fmt[80];
    int text = IDS_FN_NONE;
    SendDlgItemMessage(dlg, IDC_FN_SAMPLE, WM_SETREDRAW, FALSE, 0);
    sample_clear(dlg);
    cnt = (int)SendMessage(list, LB_GETCOUNT, 0, 0);
    for (i = 0; i < cnt && n > 0; i++) {
        if (SendMessage(list, LB_GETSEL, i, 0) <= 0)
            continue;
        if (SendMessage(list, LB_GETTEXTLEN, i, 0) >= (LONG)sizeof name)
            continue;
        SendMessage(list, LB_GETTEXT, i, (LPARAM)(LPSTR)name);
        GetProfileString(s_fonts, name, "", value, sizeof value);
        if (!value[0] || !font_path(value, path))
            continue;
        kb += kb_of(path);
        if (has_ext(path, ".FOT")) {
            text = IDS_FN_TRUETYPE;
            if (ttf_of(path, ttf))
                kb += kb_of(ttf);
        } else
            text = IDS_FN_SCREEN;
        if (n == 1)
            sample_of(dlg, path);
    }
    if (n > 1)
        text = IDS_FN_MANY;
    /* observed: the sample's first row is the list's selection */
    if (sample_count)
        SendDlgItemMessage(dlg, IDC_FN_SAMPLE, LB_SETCURSEL, 0, 0);
    SendDlgItemMessage(dlg, IDC_FN_SAMPLE, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(GetDlgItem(dlg, IDC_FN_SAMPLE), NULL, TRUE);
    LoadString(g_hinst, text, t, sizeof t);
    SetDlgItemText(dlg, IDC_FN_TEXT, t);
    LoadString(g_hinst, IDS_FN_SIZE, fmt, sizeof fmt);
    wsprintf(t, fmt, kb);
    SetDlgItemText(dlg, IDC_FN_SIZE, t);
    EnableWindow(GetDlgItem(dlg, IDC_FN_REMOVE), n > 0);
}

/* ---- Remove ---- */

static char rm_name[128];
static BOOL rm_delete;

BOOL FAR PASCAL __loadds RemoveProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char fmt[128], t[256];
        LoadString(g_hinst, IDS_FN_REMOVEQ, fmt, sizeof fmt);
        wsprintf(t, fmt, (LPSTR)rm_name);
        SetDlgItemText(dlg, IDC_RF_TEXT, t);
        SendDlgItemMessage(dlg, IDC_RF_ICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        CheckDlgButton(dlg, IDC_RF_DELETE, rm_delete);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDYES:
        case IDC_RF_YESALL:
        case IDNO:
        case IDCANCEL:
            rm_delete = IsDlgButtonChecked(dlg, IDC_RF_DELETE);
            EndDialog(dlg, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void remove_fonts(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FN_LIST);
    int i, all = 0, removed = 0, last = -1;
    char value[160], path[160], ttf[160];
    set_close();
    /* observed: the sample is empty while the Remove Font dialogs are up */
    sample_clear(dlg);
    rm_delete = FALSE;
    for (i = 0; i < (int)SendMessage(list, LB_GETCOUNT, 0, 0); i++) {
        int r;
        BOOL have;
        if (SendMessage(list, LB_GETSEL, i, 0) <= 0)
            continue;
        if (SendMessage(list, LB_GETTEXTLEN, i, 0) >= (LONG)sizeof rm_name)
            continue;
        SendMessage(list, LB_GETTEXT, i, (LPARAM)(LPSTR)rm_name);
        if (!all) {
            r = cpl_dialog(dlg, DLG_REMOVEFONT, RemoveProc);
            if (r == IDCANCEL)
                break;
            if (r == IDNO)
                continue;
            if (r == IDC_RF_YESALL)
                all = 1;
        }
        GetProfileString(s_fonts, rm_name, "", value, sizeof value);
        have = value[0] && font_path(value, path);
        if (value[0])
            RemoveFontResource(have ? path : value);
        WriteProfileString(s_fonts, rm_name, NULL);
        if (have && rm_delete) {
            if (has_ext(path, ".FOT") && ttf_of(path, ttf))
                cpl_delete(ttf);
            cpl_delete(path);
        }
        SendMessage(list, LB_DELETESTRING, i, 0);
        last = i--;
        removed++;
    }
    if (removed) {
        /* observed: the caret on the font before the last one removed,
           the list not scrolled */
        int k = last > 0 ? last - 1 : 0;
        SendMessage(list, LB_SETCARETINDEX, k, 0);
        SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    }
    show_selection(dlg);
}

/* ---- Add ---- */

typedef struct {
    char name[128];
    char file[13];
} ADDITEM;

static HGLOBAL add_items;       /* ADDITEM: the directory's font files */
static int add_count;
static HGLOBAL add_chosen;      /* indexes into add_items */
static int add_nchosen;
static char add_dir[160];
static BOOL add_copy;

static void add_free(void)
{
    if (add_items)
        GlobalFree(add_items);
    if (add_chosen)
        GlobalFree(add_chosen);
    add_items = add_chosen = NULL;
    add_count = add_nchosen = 0;
}

static BOOL add_item(const char *name, const char *file)
{
    HGLOBAL g = add_items ? GlobalReAlloc(add_items, (DWORD)(add_count + 1) * sizeof(ADDITEM), GMEM_MOVEABLE)
                          : GlobalAlloc(GMEM_MOVEABLE, sizeof(ADDITEM));
    ADDITEM FAR *a;
    if (!g)
        return FALSE;
    add_items = g;
    a = (ADDITEM FAR *)GlobalLock(g);
    lstrcpyn(a[add_count].name, name, sizeof a->name);
    lstrcpyn(a[add_count].file, file, sizeof a->file);
    GlobalUnlock(g);
    add_count++;
    return TRUE;
}

/* the font files of the dialog's current directory (stc1 shows it) */
static void add_scan(HWND dlg)
{
    static const char *pats[] = { "*.TTF", "*.FON" };
    HWND list = GetDlgItem(dlg, IDC_AF_LIST);
    char spec[180], path[180], name[128];
    CPLFIND fd;
    int p, i;
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (add_items)
        GlobalFree(add_items);
    add_items = NULL;
    add_count = 0;
    GetDlgItemText(dlg, IDC_AF_DIR, add_dir, sizeof add_dir);
    SendMessage(list, WM_SETREDRAW, FALSE, 0);
    SendMessage(list, LB_RESETCONTENT, 0, 0);
    for (p = 0; p < 2; p++) {
        BOOL more;
        cpl_join(spec, sizeof spec, add_dir, pats[p]);
        for (more = cpl_find(spec, 0, &fd, TRUE); more; more = cpl_find(spec, 0, &fd, FALSE)) {
            cpl_join(path, sizeof path, add_dir, fd.name);
            if (!font_desc(path, name, sizeof name) || !add_item(name, fd.name))
                continue;
            i = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
            if (i >= 0)
                SendMessage(list, LB_SETITEMDATA, i, add_count - 1);
        }
    }
    if (!add_count) {
        char t[64];
        LoadString(g_hinst, IDS_FN_NOTFOUND, t, sizeof t);
        SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
    }
    EnableWindow(list, add_count > 0);
    EnableWindow(GetDlgItem(dlg, IDC_AF_SELALL), add_count > 0);
    SendMessage(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);
    SetCursor(old);
}

#define WM_ADDSCAN (WM_USER + 100)

UINT FAR PASCAL __loadds AddHook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(dlg, IDC_AF_COPY, 1);
        PostMessage(dlg, WM_ADDSCAN, 0, 0);
        return TRUE;
    case WM_ADDSCAN:
        add_scan(dlg);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_AF_DIRS:
            if (HIWORD(lp) == LBN_DBLCLK)
                PostMessage(dlg, WM_ADDSCAN, 0, 0);
            break;
        case IDC_AF_DRIVES:
            if (HIWORD(lp) == CBN_SELCHANGE)
                PostMessage(dlg, WM_ADDSCAN, 0, 0);
            break;
        case IDC_AF_SELALL:
            SendDlgItemMessage(dlg, IDC_AF_LIST, LB_SETSEL, TRUE, -1L);
            return TRUE;
        case IDOK: {
            HWND list = GetDlgItem(dlg, IDC_AF_LIST);
            int n = add_count ? (int)SendMessage(list, LB_GETSELCOUNT, 0, 0) : 0, i, cnt;
            int FAR *c;
            if (n <= 0)
                break;          /* COMMDLG's OK: a directory typed or chosen */
            if (add_chosen)
                GlobalFree(add_chosen);
            add_chosen = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n * sizeof(int));
            add_nchosen = 0;
            if (add_chosen) {
                c = (int FAR *)GlobalLock(add_chosen);
                cnt = (int)SendMessage(list, LB_GETCOUNT, 0, 0);
                for (i = 0; i < cnt && add_nchosen < n; i++)
                    if (SendMessage(list, LB_GETSEL, i, 0) > 0)
                        c[add_nchosen++] = (int)SendMessage(list, LB_GETITEMDATA, i, 0);
                GlobalUnlock(add_chosen);
            }
            add_copy = IsDlgButtonChecked(dlg, IDC_AF_COPY);
            /* the dialog closes before the fonts are installed (3.1: the
               "already installed" box comes over the Fonts dialog) */
            PostMessage(dlg, WM_COMMAND, IDCANCEL, 0);
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

/* a name for path's file in dir that is not taken: the name itself, else
   its last letter before the dot replaced by 0, 1 ... (observed: 0) */
static BOOL free_name(const char *dir, const char *file, char *out, int size)
{
    char name[13], path[180];
    OFSTRUCT of;
    int dot, c;
    lstrcpyn(name, file, sizeof name);
    AnsiUpper(name);
    cpl_join(path, sizeof path, dir, name);
    if (OpenFile(path, &of, OF_EXIST) == HFILE_ERROR) {
        lstrcpyn(out, name, size);
        return TRUE;
    }
    for (dot = 0; name[dot] && name[dot] != '.'; dot++)
        ;
    if (!dot)
        return FALSE;
    for (c = '0'; c <= '9'; c++) {
        name[dot - 1] = (char)c;
        cpl_join(path, sizeof path, dir, name);
        if (OpenFile(path, &of, OF_EXIST) == HFILE_ERROR) {
            lstrcpyn(out, name, size);
            return TRUE;
        }
    }
    return FALSE;
}

/* install one font; 0 done, 1 skipped, -1 stop */
static int install(HWND dlg, const char *name, const char *dir, const char *file, BOOL copy)
{
    char t[300], fmt[200], src[180], sys[160], value[180], target[13], ttf[180], fot[180];
    int k;
    GetProfileString(s_fonts, name, "", t, sizeof t);
    if (t[0]) {
        LoadString(g_hinst, IDS_FN_INSTALLED, fmt, sizeof fmt);
        wsprintf(t, fmt, (LPSTR)name);
        LoadString(g_hinst, IDS_CONTROLPANEL, fmt, sizeof fmt);
        return MessageBox(dlg, t, fmt, MB_OKCANCEL | MB_ICONEXCLAMATION) == IDCANCEL ? -1 : 1;
    }
    cpl_join(src, sizeof src, dir, file);
    AnsiUpper(src);
    cpl_sysdir(sys, sizeof sys);
    if (copy) {
        if (!free_name(sys, file, target, sizeof target))
            return 1;
        cpl_join(ttf, sizeof ttf, sys, target);
        if (!cpl_copy_file(src, ttf))
            return 1;
        /* observed: CONTROL.INI [installed] 3.1=yes once a font file was
           copied */
        WritePrivateProfileString("installed", "3.1", "yes", "CONTROL.INI");
        lstrcpy(value, target);
    } else {
        lstrcpy(value, src);
        lstrcpy(ttf, src);
    }
    if (has_ext(value, ".TTF")) {
        /* the .FOT GDI loads, in the System directory */
        char base[13];
        const char *b = copy ? target : file;
        for (k = 0; b[k] && b[k] != '.' && k < 8; k++)
            base[k] = b[k];
        base[k] = 0;
        AnsiUpper(base);
        lstrcat(base, ".FOT");
        if (!free_name(sys, base, target, sizeof target))
            return 1;
        cpl_join(fot, sizeof fot, sys, target);
        if (!CreateScalableFontResource(0, fot, copy ? value : src, NULL))
            return 1;
        if (copy)
            lstrcpy(value, target);
        else
            lstrcpy(value, fot);
    }
    if (!AddFontResource(value))
        return 1;
    WriteProfileString(s_fonts, name, value);
    k = (int)SendDlgItemMessage(dlg, IDC_FN_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
    if (k >= 0) {
        SendDlgItemMessage(dlg, IDC_FN_LIST, LB_SETSEL, TRUE, k);
        SendDlgItemMessage(dlg, IDC_FN_LIST, LB_SETCARETINDEX, k, 0);
    }
    return 0;
}

static void add_fonts(HWND dlg)
{
    OPENFILENAME o;
    char file[128], filter[64];
    int i, added = 0;
    {
        char *z = (char *)&o;
        for (i = 0; i < (int)sizeof o; i++)
            z[i] = 0;
    }
    file[0] = 0;
    /* COMMDLG's filter edit (hidden) shows the pattern */
    lstrcpy(filter, "Font Files");
    lstrcpy(filter + 11, "*.ttf;*.tt_;*.fon;*.fo_");
    filter[11 + 24] = 0;
    o.lStructSize = sizeof o;
    o.hwndOwner = dlg;
    o.hInstance = g_hinst;
    o.lpstrFilter = filter;
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.Flags = OFN_ENABLETEMPLATE | OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_SHOWHELP;
    o.lpfnHook = AddHook;
    o.lpTemplateName = MAKEINTRESOURCE(DLG_ADDFONTS);
    add_nchosen = 0;
    GetOpenFileName(&o);
    if (add_nchosen && add_chosen && add_items) {
        int FAR *c = (int FAR *)GlobalLock(add_chosen);
        ADDITEM FAR *a = (ADDITEM FAR *)GlobalLock(add_items);
        SendDlgItemMessage(dlg, IDC_FN_LIST, LB_SETSEL, FALSE, -1L);
        for (i = 0; i < add_nchosen; i++) {
            char name[128], f[13];
            int r;
            lstrcpy(name, a[c[i]].name);
            lstrcpy(f, a[c[i]].file);
            r = install(dlg, name, add_dir, f, add_copy);
            if (r < 0)
                break;
            if (!r)
                added++;
        }
        GlobalUnlock(add_chosen);
        GlobalUnlock(add_items);
    }
    add_free();
    if (added) {
        set_close();
        SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    }
    show_selection(dlg);
}

/* files dropped from File Manager: installed where they are (UNVERIFIED) */
static void drop_fonts(HWND dlg, HDROP drop)
{
    int n = DragQueryFile(drop, (UINT)-1, NULL, 0), i, added = 0;
    char path[180], dir[180], name[128];
    SendDlgItemMessage(dlg, IDC_FN_LIST, LB_SETSEL, FALSE, -1L);
    for (i = 0; i < n; i++) {
        char *f;
        DragQueryFile(drop, i, path, sizeof path);
        if (!font_desc(path, name, sizeof name))
            continue;
        lstrcpy(dir, path);
        for (f = dir + lstrlen(dir); f > dir && f[-1] != '\\'; f--)
            ;
        lstrcpy(path, f);
        *f = 0;
        if (install(dlg, name, dir, path, FALSE) == 0)
            added++;
    }
    DragFinish(drop);
    if (added) {
        set_close();
        SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    }
    show_selection(dlg);
}

/* ---- TrueType ---- */

static BOOL tt_enable0;

BOOL FAR PASCAL __loadds TrueTypeProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        tt_enable0 = GetProfileInt(s_tt, "TTEnable", 1) != 0;
        CheckDlgButton(dlg, IDC_TT_ENABLE, tt_enable0);
        CheckDlgButton(dlg, IDC_TT_ONLY, GetProfileInt(s_tt, "TTOnly", 0) != 0);
        EnableWindow(GetDlgItem(dlg, IDC_TT_ONLY), tt_enable0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_TT_ENABLE:
            EnableWindow(GetDlgItem(dlg, IDC_TT_ONLY), IsDlgButtonChecked(dlg, IDC_TT_ENABLE));
            set_close();
            return TRUE;
        case IDC_TT_ONLY:
            set_close();
            return TRUE;
        case IDOK: {
            BOOL en = IsDlgButtonChecked(dlg, IDC_TT_ENABLE);
            WriteProfileString(s_tt, "TTEnable", en ? "1" : "0");
            WriteProfileString(s_tt, "TTOnly", IsDlgButtonChecked(dlg, IDC_TT_ONLY) ? "1" : "0");
            set_close();
            if (en != tt_enable0)
                cpl_restart_prompt(dlg, IDS_RS_TRUETYPE);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_FONTS);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- the dialog ---- */

static void fill_installed(HWND dlg)
{
    HGLOBAL g = cpl_section_keys(NULL, s_fonts);
    HWND list = GetDlgItem(dlg, IDC_FN_LIST);
    char FAR *p;
    if (!g)
        return;
    for (p = GlobalLock(g); *p; p += lstrlen(p) + 1)
        SendMessage(list, LB_ADDSTRING, 0, (LPARAM)p);
    GlobalUnlock(g);
    GlobalFree(g);
}

BOOL FAR PASCAL __loadds FontsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        fonts_dlg = dlg;
        fill_installed(dlg);
        DragAcceptFiles(dlg, TRUE);
        show_selection(dlg);
        return TRUE;
    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT m = (LPMEASUREITEMSTRUCT)lp;
        HDC dc = GetDC(dlg);
        /* by position: items are added in order (3.1's row heights are
           each font's tmHeight) */
        HFONT f = sample_font((int)m->itemID), old;
        TEXTMETRIC tm;
        old = SelectObject(dc, f ? f : GetStockObject(SYSTEM_FONT));
        GetTextMetrics(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(dlg, dc);
        m->itemHeight = tm.tmHeight;
        return TRUE;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        HFONT f, old;
        if (d->CtlID != IDC_FN_SAMPLE || (int)d->itemID < 0)
            return TRUE;
        if (!(d->itemAction & ODA_DRAWENTIRE))
            return TRUE;
        f = sample_font((int)d->itemID);
        old = SelectObject(d->hDC, f ? f : GetStockObject(SYSTEM_FONT));
        SetTextColor(d->hDC, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(d->hDC, GetSysColor(COLOR_WINDOW));
        ExtTextOut(d->hDC, d->rcItem.left, d->rcItem.top, ETO_OPAQUE, &d->rcItem, s_sample,
                   sizeof s_sample - 1, NULL);
        SelectObject(d->hDC, old);
        return TRUE;
    }
    case WM_DROPFILES:
        drop_fonts(dlg, (HDROP)wp);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_FN_LIST:
            if (HIWORD(lp) == LBN_SELCHANGE)
                show_selection(dlg);
            return TRUE;
        case IDC_FN_REMOVE:
            remove_fonts(dlg);
            return TRUE;
        case IDC_FN_ADD:
            add_fonts(dlg);
            return TRUE;
        case IDC_FN_TRUETYPE:
            cpl_dialog(dlg, DLG_TRUETYPE, TrueTypeProc);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_FONTS);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            DragAcceptFiles(dlg, FALSE);
            sample_clear(dlg);
            EndDialog(dlg, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void fonts_applet(HWND parent)
{
    cpl_dialog(parent, DLG_FONTS, FontsProc);
}
