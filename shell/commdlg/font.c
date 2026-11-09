/*
 * font.c - w16 COMMDLG: ChooseFont (stub, being written).
 */
#include "commdlg.h"

BOOL CDAPI ChooseFont(LPCHOOSEFONT c)
{
    (void)c;
    cd_seterr(CDERR_DIALOGFAILURE);
    return FALSE;
}

BOOL CDPROC FormatCharDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg; (void)msg; (void)wp; (void)lp;
    return FALSE;
}

int CDPROC FontFamilyEnumProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type; (void)lp;
    return 0;
}

int CDPROC FontStyleEnumProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type; (void)lp;
    return 0;
}
