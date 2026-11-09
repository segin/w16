/*
 * fontdlg.c - Settings > Set Font... (digital clock).
 *
 * Observed on Windows 3.1: the original calls ChooseFont with its own
 * template (resource 100: face list, OK, Cancel, sample; the style and
 * size controls are placed off the dialog), offering the screen fonts.
 * It starts from the font it last made (the size list shows its height)
 * and keeps the whole LOGFONT ChooseFont returns (face, family, pitch and
 * precision bits all reach its CreateFontIndirect calls), then writes
 * sFont=<face> to CLOCK.INI at once.
 *
 * The 2.x build (no COMMDLG) shows the same layout itself: an edit
 * control over a list of the screen fonts' faces (EnumFonts), and keeps
 * only the face name.
 */
#include "clock.h"
#ifndef W16_WIN2
#include <commdlg.h>

BOOL font_choose(HWND owner)
{
    CHOOSEFONT cf;
    LOGFONT lf = g_lf;
    int i;
    char FAR *p = (char FAR *)&cf;
    for (i = 0; i < (int)sizeof cf; i++)
        p[i] = 0;
    cf.lStructSize = sizeof cf;
    cf.hwndOwner = owner;
    cf.lpLogFont = &lf;
    /* the faces offered: no OEM or symbol character sets (observed: no
       Terminal, Modern, Roman, Script, Symbol, Wingdings) */
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_ENABLETEMPLATE | CF_ANSIONLY;
    cf.hInstance = g_hinst;
    cf.lpTemplateName = MAKEINTRESOURCE(IDD_FONT);
    if (!ChooseFont(&cf))
        return FALSE;
    g_lf = lf;
    return TRUE;
}

#else

#define IDC_FACEEDIT 1152

int CALLBACK __export add_face(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                      int type, LPARAM lp)
{
    HWND list = (HWND)LOWORD(lp);
    (void)tm;
    (void)type;
    SendMessage(list, LB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);   /* one call per face */
    return 1;
}

static HFONT sample;

static void show_sample(HWND d)
{
    char face[LF_FACESIZE];
    LOGFONT lf = g_lf;
    GetDlgItemText(d, IDC_FACEEDIT, face, sizeof face);
    s_cpy(lf.lfFaceName, face, LF_FACESIZE);
    lf.lfHeight = -21;
    if (sample)
        DeleteObject(sample);
    sample = CreateFontIndirect(&lf);
    SendDlgItemMessage(d, IDC_SAMPLE, WM_SETFONT, (WPARAM)sample, TRUE);
}

BOOL FAR PASCAL __export font_proc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HDC dc = GetDC(d);
        HWND list = GetDlgItem(d, IDC_FONTNAME);
        FARPROC fp = MakeProcInstance((FARPROC)add_face, g_hinst);
        EnumFonts(dc, NULL, (FONTENUMPROC)fp, (char FAR *)MAKELPARAM(list, 0));
        FreeProcInstance(fp);
        ReleaseDC(d, dc);
        SetDlgItemText(d, IDC_FACEEDIT, g_lf.lfFaceName);
        SendMessage(list, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)g_lf.lfFaceName);
        show_sample(d);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDC_FONTNAME && HIWORD(lp) == LBN_SELCHANGE) {
            char face[LF_FACESIZE];
            HWND list = GetDlgItem(d, IDC_FONTNAME);
            int i = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            if (i >= 0) {
                SendMessage(list, LB_GETTEXT, i, (LPARAM)(LPSTR)face);
                SetDlgItemText(d, IDC_FACEEDIT, face);
                show_sample(d);
            }
        } else if (wp == IDOK || wp == IDCANCEL) {
            if (wp == IDOK) {
                char face[LF_FACESIZE];
                GetDlgItemText(d, IDC_FACEEDIT, face, sizeof face);
                s_cpy(g_lf.lfFaceName, face, LF_FACESIZE);
            }
            if (sample)
                DeleteObject(sample);
            sample = NULL;
            EndDialog(d, wp == IDOK);
        }
        return TRUE;
    }
    (void)lp;
    return FALSE;
}

BOOL font_choose(HWND owner)
{
    FARPROC fp = MakeProcInstance((FARPROC)font_proc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_FONT), owner, (DLGPROC)fp);
    FreeProcInstance(fp);
    return r == 1;
}

#endif
