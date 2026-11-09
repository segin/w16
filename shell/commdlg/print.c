/*
 * print.c - w16 COMMDLG: PrintDlg (stub, being written).
 */
#include "commdlg.h"

BOOL CDAPI PrintDlg(LPPRINTDLG p)
{
    (void)p;
    cd_seterr(CDERR_DIALOGFAILURE);
    return FALSE;
}

BOOL CDPROC PrintDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg; (void)msg; (void)wp; (void)lp;
    return FALSE;
}

BOOL CDPROC PrintSetupDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg; (void)msg; (void)wp; (void)lp;
    return FALSE;
}

/* edit controls that take digits only */
LRESULT CDPROC EditIntegerOnly(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CHAR && wp >= ' ' && (wp < '0' || wp > '9')) {
        MessageBeep(0);
        return 0;
    }
    return call_old(w, msg, wp, lp);
}

LRESULT CDPROC dwUpArrowHack(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return call_old(w, msg, wp, lp);
}
