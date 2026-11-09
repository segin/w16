/*
 * stat.c - w16 Calculator: the Statistics Box.
 *
 * Observed on Windows 3.1 (CA1 statistics probes, SNAP dumps): the
 * modeless dialog "SB" ("Statistics Box", list box 407, RET 411, LOAD 410,
 * CD 404, CAD 405, "n=" 409 and the count 408). Sta opens it (in the
 * scientific view only, not in an error state) and "stat" shows in the
 * calculator. Dat adds the display's text as it is (" 1.50", "FF") and
 * selects it; LOAD makes the selected value the current number; CD
 * deletes the selected entry and selects the last; CAD deletes them all;
 * RET returns to the calculator. Closing the box ends statistics.
 * Ave, Sum and s work on the values (engine.c).
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"

HWND g_statbox;

#define MAXSTAT 512
static double vals[MAXSTAT];
static int nvals;

static void show_count(void)
{
    char t[8];
    int n = nvals, i = 0, k;
    char d[8];
    do {
        d[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    for (k = 0; k < i; k++)
        t[k] = d[i - 1 - k];
    t[i] = 0;
    SetDlgItemText(g_statbox, IDD_STATN, t);
}

void ui_stat_add(double v, const char *text)
{
    HWND lb;
    int i;
    if (!g_statbox || nvals >= MAXSTAT)
        return;
    lb = GetDlgItem(g_statbox, IDD_STATLIST);
    vals[nvals++] = v;
    i = (int)SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)text);
    SendMessage(lb, LB_SETCURSEL, i, 0);
    show_count();
}

int ui_stat_count(void)
{
    return nvals;
}

double ui_stat_get(int i)
{
    return vals[i];
}

BOOL CALLBACK __export StatDlgProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND lb = GetDlgItem(w, IDD_STATLIST);
    int sel, i;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDD_RET:
            SetFocus(g_wnd);
            return TRUE;
        case IDD_LOAD:
            sel = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < nvals) {
                calc_load(vals[sel]);
                ui_update();
            }
            return TRUE;
        case IDD_CD:
            sel = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < nvals) {
                SendMessage(lb, LB_DELETESTRING, sel, 0);
                for (i = sel; i + 1 < nvals; i++)
                    vals[i] = vals[i + 1];
                nvals--;
                if (nvals)
                    SendMessage(lb, LB_SETCURSEL, nvals - 1, 0);
                show_count();
            }
            return TRUE;
        case IDD_CAD:
            SendMessage(lb, LB_RESETCONTENT, 0, 0);
            nvals = 0;
            show_count();
            return TRUE;
        case IDCANCEL:
            stat_close();
            return TRUE;
        }
        break;
    case WM_CLOSE:
        stat_close();
        return TRUE;
    }
    return FALSE;
}

static FARPROC stat_proc;

void stat_open(void)
{
    if (g_statbox) {
        SetFocus(g_statbox);
        return;
    }
    if (!stat_proc)
        stat_proc = MakeProcInstance((FARPROC)StatDlgProc, g_hinst);
    g_statbox = CreateDialog(g_hinst, "SB", g_wnd, (DLGPROC)stat_proc);
    if (!g_statbox)
        return;
    nvals = 0;
    show_count();
    g.stat = 1;
    SetDlgItemText(g_wnd, IDD_STATTEXT, "stat");
    ShowWindow(g_statbox, SW_SHOWNORMAL);
}

void stat_close(void)
{
    if (!g_statbox)
        return;
    DestroyWindow(g_statbox);
    g_statbox = NULL;
    g.stat = 0;
    nvals = 0;
    SetDlgItemText(g_wnd, IDD_STATTEXT, "");
    SetFocus(g_wnd);
}
