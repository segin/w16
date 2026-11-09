/*
 * filedlg.c - w16 Cardfile: Open, Save As and Merge (COMMDLG; the 2.x
 * build has its own in compat2.c).
 *
 * Observed (CF2, CF8, CF9): the File Name box starts as "*.crd" in the
 * current directory; Save As offers "Card File (*.CRD)" and "3.0 Card
 * File (*.CRD)" and remembers the choice for the session, and has a
 * "Validate" check box (COMMDLG's read-only box, renamed, checked).
 */
#include "cardfile.h"

#ifndef W16_WIN2
static OPENFILENAME ofn;                /* kept: the filter choice stays */
static char filter[3][160];
static char title[40];
static DWORD save_index = 1;           /* Save As remembers the type */

/* "Name|*.crd|..." with the bars made NULs (COMMDLG's filter form) */
static void make_filter(char *f, int size, UINT a, UINT b, LPCSTR pat_b)
{
    int i;
    s_cpy(f, load_str(a), size);
    s_cat(f, "|*.crd|", size);
    s_cat(f, load_str(b), size);
    s_cat(f, "|", size);
    s_cat(f, pat_b, size);
    s_cat(f, "|", size);
    for (i = 0; f[i]; i++)
        if (f[i] == '|')
            f[i] = 0;
}

UINT CALLBACK __export SaveHook(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetDlgItemText(d, 0x410, load_str(IDS_VALIDATE));
        return TRUE;
    }
    return FALSE;
}

BOOL file_dialog(int which, LPSTR path, int *fmt)
{
    static char file[144];
    static BOOL inited;
    BOOL ok;
    FARPROC hook = NULL;
    if (!inited) {
        inited = TRUE;
        /* all three offer the same two types (CF9) */
        make_filter(filter[FD_OPEN], sizeof filter[0], IDS_FILTERCRD, IDS_FILTER30, "*.crd");
        make_filter(filter[FD_SAVEAS], sizeof filter[0], IDS_FILTERCRD, IDS_FILTER30, "*.crd");
        make_filter(filter[FD_MERGE], sizeof filter[0], IDS_FILTERCRD, IDS_FILTER30, "*.crd");
    }
    file[0] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_frame;
    ofn.hInstance = g_hinst;
    ofn.lpstrFilter = filter[which];
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrDefExt = load_str(IDS_EXT);
    LoadString(g_hinst, which == FD_OPEN ? IDS_OPENTITLE : which == FD_SAVEAS ? IDS_SAVEASTITLE
                                                                                : IDS_MERGETITLE,
               title, sizeof title);
    ofn.lpstrTitle = title;
    if (which == FD_SAVEAS) {
        static char defext[4];
        s_cpy(defext, load_str(IDS_EXT), sizeof defext);
        ofn.lpstrDefExt = defext;
        hook = MakeProcInstance((FARPROC)SaveHook, g_hinst);
        ofn.lpfnHook = (UINT (CALLBACK *)(HWND, UINT, WPARAM, LPARAM))hook;
        ofn.Flags = OFN_ENABLEHOOK | OFN_READONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.nFilterIndex = save_index;
        ok = GetSaveFileName(&ofn);
        save_index = ofn.nFilterIndex;
        *fmt = ofn.nFilterIndex == 2 ? FMT_MGC : -1;
    } else {
        static char defext[4];
        s_cpy(defext, load_str(IDS_EXT), sizeof defext);
        ofn.lpstrDefExt = defext;
        ofn.lpfnHook = NULL;
        ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.nFilterIndex = 1;
        ok = GetOpenFileName(&ofn);
    }
    if (hook)
        FreeProcInstance(hook);
    if (ok)
        s_cpy(path, file, 144);
    return ok;
}
#endif
