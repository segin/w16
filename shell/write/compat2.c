/*
 * compat2.c - w16 Write: what the Windows 2.x build does instead of the
 * calls Windows/386 2.11 lacks (W16_WIN2; ARCHITECTURE.md, Windows 2.x
 * notes): no COMMDLG, so Open and Save As are our own small dialog (a
 * file name field, as Write 2.x's own dialogs had; the full 3.1-style
 * dialog is the plan, after Program Manager's browse.c).
 */
#include "write.h"

#ifdef W16_WIN2

static char *dlgPath;

BOOL CALLBACK __export File2DlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetDlgItemText(d, 3, dlgPath);
        SendDlgItemMessage(d, 3, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
        return TRUE;
    }
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        if (wp == IDOK)
            GetDlgItemText(d, 3, dlgPath, PATH_MAX_DOS);
        EndDialog(d, wp);
        return TRUE;
    }
    return FALSE;
}

/* ---- the menu ----
 * Windows/386 2.11 does not show a menu loaded from the resource wrc
 * writes (observed: no menu bar, WR10; Program Manager's 2.x build met
 * the same). The menu is built here from that resource with CreateMenu
 * and ChangeMenu: a 4-byte header, then items {WORD flags, WORD id (not
 * for popups), text}, MF_END on each menu's last item. */
static const BYTE FAR *menu_items(HMENU m, const BYTE FAR *p)
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
            p = menu_items(sub, p);
            ChangeMenu(m, 0, text, (UINT)sub, MF_APPEND | MF_POPUP);
        } else if (!text[0] && !id) {
            ChangeMenu(m, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        } else {
            ChangeMenu(m, 0, text, id, MF_APPEND | MF_STRING | (flags & (MF_GRAYED | MF_CHECKED)));
        }
        if (flags & MF_END)
            return p;
    }
}

HMENU menu2_load(LPCSTR name)
{
    HRSRC r = FindResource(g_hinst, name, RT_MENU);
    HGLOBAL h = r ? LoadResource(g_hinst, r) : NULL;
    const BYTE FAR *p = h ? (const BYTE FAR *)LockResource(h) : NULL;
    HMENU m = NULL;
    if (p) {
        m = CreateMenu();
        menu_items(m, p + 4 + (p[2] | (p[3] << 8)));
        UnlockResource(h);
    }
    if (h)
        FreeResource(h);
    return m;
}

BOOL dlg2_file(BOOL save, char *path)
{
    FARPROC fp = MakeProcInstance((FARPROC)File2DlgProc, g_hinst);
    int r;
    (void)save;
    dlgPath = path;
    r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_OPEN2), g_frame, (DLGPROC)fp);
    FreeProcInstance(fp);
    return r == IDOK && path[0];
}

#endif
