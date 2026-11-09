/*
 * color.c - w16 COMMDLG: ChooseColor (stub, being written).
 */
#include "commdlg.h"

BOOL CDAPI ChooseColor(LPCHOOSECOLOR c)
{
    (void)c;
    cd_seterr(CDERR_DIALOGFAILURE);
    return FALSE;
}

BOOL CDPROC ColorDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg; (void)msg; (void)wp; (void)lp;
    return FALSE;
}

/* the colour boxes take the arrow keys (observed: the basic and custom
   colour controls have this procedure) */
LRESULT CDPROC WantArrows(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTARROWS;
    return call_old(w, msg, wp, lp);
}
