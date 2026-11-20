/*
 * dialogs.c - w16 File Manager: View > Partial Details, View > By File
 * Type, Options > Confirmation, Options > Font.
 *
 * The templates (winfile.rc) are the original's dialogs as observed
 * running (obs WF2: SNAP dump, tools/snapdlg.py). What they do (WF4): Partial
 * Details sets the view's detail bits (size 2, date 4, time 8, attributes
 * 16) for the active window; By File Type its pattern (the title follows)
 * and file kinds; Confirmation writes all five Confirm* keys at OK;
 * Font is COMMDLG's ChooseFont with a hook and a template carrying a
 * "&Lowercase" box (ID 1042) where Strikeout would be, and writes Face,
 * Size, LowerCase (bit 0 lower case, bit 2 italic) and FaceWeight.
 */
#include "winfile.h"

#define IDD_SIZE        203
#define IDD_DATE        204
#define IDD_TIME        205
#define IDD_FLAGS       206
#define IDD_NAME        201
#define IDD_DIRS        101
#define IDD_PROGS       210
#define IDD_DOCS        211
#define IDD_OTHER       212
#define IDD_HIDDEN      250
#define IDD_CDEL        219
#define IDD_CSUBDEL     220
#define IDD_CREPLACE    221
#define IDD_CMOUSE      249
#define IDD_CDISK       251
#define IDD_HELP        254
#define IDD_LOWER       1042

static LPDIRWIN dlg_dir;

static void check(HWND h, int id, BOOL on)
{
    CheckDlgButton(h, id, on ? 1 : 0);
}

static BOOL checked(HWND h, int id)
{
    return IsDlgButtonChecked(h, id) != 0;
}

static void center_help(HWND h, int topic)
{
    (void)h;
    WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + topic);  /* UNVERIFIED: as F1 on the menu item */
}

/* ---- Partial Details ---- */

BOOL CALLBACK _export OtherDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = dlg_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        check(h, IDD_SIZE, (d->view & VIEW_SIZE) != 0);
        check(h, IDD_DATE, (d->view & VIEW_DATE) != 0);
        check(h, IDD_TIME, (d->view & VIEW_TIME) != 0);
        check(h, IDD_FLAGS, (d->view & VIEW_FLAGS) != 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            WORD v = 0;
            if (checked(h, IDD_SIZE))
                v |= VIEW_SIZE;
            if (checked(h, IDD_DATE))
                v |= VIEW_DATE;
            if (checked(h, IDD_TIME))
                v |= VIEW_TIME;
            if (checked(h, IDD_FLAGS))
                v |= VIEW_FLAGS;
            EndDialog(h, 1);
            if (v != (d->view & VIEW_DETAILMASK)) {
                BOOL relist = ((d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY) != (v == VIEW_NAMEONLY);
                d->view = (WORD)((d->view & ~VIEW_DETAILMASK) | v);
                if (relist) {
                    dir_set_view(d);
                } else {
                    dir_columns(d);
                    InvalidateRect(d->dirlb, NULL, TRUE);
                }
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            center_help(h, 403);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- By File Type ---- */

BOOL CALLBACK _export IncludeDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = dlg_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(h, IDD_NAME, d->spec);
        check(h, IDD_DIRS, (d->attribs & ATTR_DIR) != 0);
        check(h, IDD_PROGS, (d->attribs & ATTR_PROGS) != 0);
        check(h, IDD_DOCS, (d->attribs & ATTR_DOCS) != 0);
        check(h, IDD_OTHER, (d->attribs & ATTR_OTHER) != 0);
        check(h, IDD_HIDDEN, (d->attribs & ATTR_HS) != 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            WORD a = 0;
            char spec[PATHLEN];
            GetDlgItemText(h, IDD_NAME, spec, sizeof spec);
            if (!spec[0])
                s_cpy(spec, "*.*", sizeof spec);
            s_upper(spec);
            if (checked(h, IDD_DIRS))
                a |= ATTR_DIR;
            if (checked(h, IDD_PROGS))
                a |= ATTR_PROGS;
            if (checked(h, IDD_DOCS))
                a |= ATTR_DOCS;
            /* Other Files is 0x400 with 0x40, 0x20 and 0x01 at first; off
               takes all four away, on gives back 0x400 alone (WF4: 1905
               to 768; WF07: 1808) */
            if (checked(h, IDD_OTHER))
                a |= (d->attribs & ATTR_OTHER) ? (d->attribs & ATTR_OTHERS) : ATTR_OTHER;
            if (checked(h, IDD_HIDDEN))
                a |= ATTR_HS;
            EndDialog(h, 1);
            s_cpy(d->spec, spec, sizeof d->spec);
            d->attribs = a;
            treewin_title(d);
            dir_read(d);
            status_update();
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            center_help(h, 409);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Confirmation ---- */

BOOL CALLBACK _export ConfirmDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        check(h, IDD_CDEL, g_confdel);
        check(h, IDD_CSUBDEL, g_confsubdel);
        check(h, IDD_CREPLACE, g_confreplace);
        check(h, IDD_CMOUSE, g_confmouse);
        check(h, IDD_CDISK, g_confdisk);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            g_confdel = checked(h, IDD_CDEL);
            g_confsubdel = checked(h, IDD_CSUBDEL);
            g_confreplace = checked(h, IDD_CREPLACE);
            g_confmouse = checked(h, IDD_CMOUSE);
            g_confdisk = checked(h, IDD_CDISK);
            ini_write_int("ConfirmDelete", g_confdel);
            ini_write_int("ConfirmSubDel", g_confsubdel);
            ini_write_int("ConfirmReplace", g_confreplace);
            ini_write_int("ConfirmMouse", g_confmouse);
            ini_write_int("ConfirmFormat", g_confdisk);
            EndDialog(h, 1);
            return TRUE;
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            center_help(h, 501);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Font ---- */

static BOOL font_lower;

UINT CALLBACK _export FontHookProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        check(h, IDD_LOWER, g_lower);
        ShowWindow(GetDlgItem(h, 1092), SW_HIDE);  /* hidden on 3.1 (obs WF2) */
        return TRUE;
    }
    if (msg == WM_COMMAND && wp == IDOK)
        font_lower = checked(h, IDD_LOWER);
    return FALSE;
}

static int dialog(LPCSTR name, FARPROC proc)
{
    FARPROC p = MakeProcInstance(proc, g_hinst);
    int r = DialogBox(g_hinst, name, g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    return r;
}

void dlg_other(LPDIRWIN d)
{
    dlg_dir = d;
    dialog("DLG_OTHER", (FARPROC)OtherDlgProc);
}

void dlg_include(LPDIRWIN d)
{
    dlg_dir = d;
    dialog("DLG_INCLUDE", (FARPROC)IncludeDlgProc);
}

void dlg_confirm(void)
{
    dialog("DLG_CONFIRM", (FARPROC)ConfirmDlgProc);
}

void dlg_font(void)
{
    CHOOSEFONT cf;
    LOGFONT lf;
    LPBYTE b = (LPBYTE)&cf;
    int i, flags;
    char t[8];
    FARPROC hook = MakeProcInstance((FARPROC)FontHookProc, g_hinst);
    for (i = 0; i < (int)sizeof cf; i++)
        b[i] = 0;
    ini_font(&lf);
    cf.lStructSize = sizeof cf;
    cf.hwndOwner = g_frame;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_SHOWHELP | CF_ENABLEHOOK |
               CF_ENABLETEMPLATE | CF_ANSIONLY;  /* no OEM fonts listed (obs WF2) */
    cf.lpfnHook = (UINT (CALLBACK *)(HWND, UINT, WPARAM, LPARAM))hook;
    cf.lpTemplateName = "DLG_FONT";
    cf.hInstance = g_hinst;
    font_lower = g_lower;
    if (ChooseFont(&cf)) {
        flags = (font_lower ? 1 : 0) | (lf.lfItalic ? 4 : 0);
        ini_write_str("Face", lf.lfFaceName);
        wsprintf(t, "%d", cf.iPointSize / 10);
        ini_write_str("Size", t);
        ini_write_int("LowerCase", flags);
        ini_write_int("FaceWeight", lf.lfWeight);
        ini_load();
        frame_newfont();
    }
    FreeProcInstance(hook);
}
