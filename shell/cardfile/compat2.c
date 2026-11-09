/*
 * compat2.c - w16 Cardfile on Windows 2.x (W16_WIN2; see compat2.h):
 * the menu, file dialogs and Help, with calls Windows/386 2.11 has.
 */
#include "cardfile.h"

#ifdef W16_WIN2

BOOL FAR cf_WinHelp(HWND w, LPCSTR file, UINT cmd, DWORD data)
{
    (void)w;
    (void)file;
    (void)data;
    if (cmd != HELP_QUIT)
        msg_box(IDS_NOHELP, NULL, MB_OK | MB_ICONEXCLAMATION);
    return TRUE;
}

/* ---- the menu ----
 * Windows 2.x cannot load the 3.x menu resource format (its 4-byte
 * header), so the menu is built with CreateMenu and ChangeMenu from the
 * same resource: after the header, items { WORD flags; WORD id (not for
 * popups); text }, MF_END on the last item of each level. */
static const BYTE FAR *build(HMENU m, const BYTE FAR *p)
{
    for (;;) {
        WORD flags = p[0] | (p[1] << 8), id = 0;
        const char FAR *text;
        p += 2;
        if (!(flags & MF_POPUP)) {
            id = p[0] | (p[1] << 8);
            p += 2;
        }
        text = (const char FAR *)p;
        p += s_len(text) + 1;
        if (flags & MF_POPUP) {
            HMENU sub = CreateMenu();
            p = build(sub, p);
            ChangeMenu(m, 0, text, (UINT)sub, MF_APPEND | MF_POPUP);
        } else if (!text[0] && !id) {
            ChangeMenu(m, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        } else {
            ChangeMenu(m, 0, text, id,
                       MF_APPEND | MF_STRING | (flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED)));
        }
        if (flags & MF_END)
            return p;
    }
}

HMENU cf_load_menu(HINSTANCE inst, LPCSTR name)
{
    HRSRC r = FindResource(inst, name, RT_MENU);
    HGLOBAL h = r ? LoadResource(inst, r) : NULL;
    const BYTE FAR *p = h ? (const BYTE FAR *)LockResource(h) : NULL;
    HMENU m = NULL;
    if (p) {
        m = CreateMenu();
        build(m, p + 4 + (p[2] | (p[3] << 8)));
        UnlockResource(h);
    }
    if (h)
        FreeResource(h);
    return m;
}

/* ---- Open, Save As, Merge: our own boxes (no COMMDLG) ---- */

static int fd_which;
static char fd_name[144];

static void fd_fill(HWND d)
{
    char spec[144];
    s_cpy(spec, "*.CRD", sizeof spec);
    DlgDirList(d, spec, IDC_FLIST, IDC_FPATH, 0x4010);  /* files, drives, dirs */
}

BOOL CALLBACK __export FileDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowText(d, load_str(fd_which == FD_OPEN ? IDS_OPENTITLE :
                                  fd_which == FD_SAVEAS ? IDS_SAVEASTITLE : IDS_MERGETITLE));
        if (fd_which != FD_SAVEAS) {
            fd_fill(d);
            SetDlgItemText(d, IDC_FNAME, "*.CRD");
        } else {
            SetDlgItemText(d, IDC_FNAME, fd_name);
            DlgDirList(d, "", 0, IDC_FPATH, 0);
        }
        SendDlgItemMessage(d, IDC_FNAME, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_FLIST:
            if (HIWORD(lp) == LBN_SELCHANGE || HIWORD(lp) == LBN_DBLCLK) {
                char sel[144];
                if (DlgDirSelect(d, sel, IDC_FLIST)) {
                    /* a directory or drive: go there */
                    if (HIWORD(lp) == LBN_DBLCLK) {
                        s_cat(sel, "*.CRD", sizeof sel);
                        DlgDirList(d, sel, IDC_FLIST, IDC_FPATH, 0x4010);
                        SetDlgItemText(d, IDC_FNAME, "*.CRD");
                    }
                } else {
                    SetDlgItemText(d, IDC_FNAME, sel);
                    if (HIWORD(lp) == LBN_DBLCLK)
                        PostMessage(d, WM_COMMAND, IDOK, 0);
                }
            }
            return TRUE;
        case IDOK:
            {
                char t[144];
                int i;
                BOOL wild = FALSE;
                GetDlgItemText(d, IDC_FNAME, t, sizeof t);
                for (i = 0; t[i]; i++)
                    if (t[i] == '*' || t[i] == '?')
                        wild = TRUE;
                if (wild && fd_which != FD_SAVEAS) {
                    DlgDirList(d, t, IDC_FLIST, IDC_FPATH, 0x4010);
                    return TRUE;
                }
                if (!t[0])
                    return TRUE;
                /* no extension: .CRD */
                {
                    const char FAR *b = base_name(t);
                    BOOL dot = FALSE;
                    for (; *b; b++)
                        if (*b == '.')
                            dot = TRUE;
                    if (!dot)
                        s_cat(t, ".CRD", sizeof t);
                }
                s_cpy(fd_name, t, sizeof fd_name);
                EndDialog(d, TRUE);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(d, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL file_dialog(int which, LPSTR path, int *fmt)
{
    FARPROC p = MakeProcInstance((FARPROC)FileDlgProc, g_hinst);
    BOOL r;
    fd_which = which;
    s_cpy(fd_name, which == FD_SAVEAS ? base_name(path) : "", sizeof fd_name);
    r = DialogBox(g_hinst, MAKEINTRESOURCE(which == FD_SAVEAS ? IDD_SAVE2 : IDD_OPEN2), g_frame,
                  (DLGPROC)p) == TRUE;
    FreeProcInstance(p);
    if (r)
        s_cpy(path, fd_name, 144);
    *fmt = FMT_MGC;                     /* Windows 2.x: 3.0-format files */
    return r;
}

#endif
