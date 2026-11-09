/*
 * msgbox.c - MessageBox, laid out as Windows 3.1 lays it out on VGA with
 * the system font (measured, probe US4's first four screens; client
 * coordinates):
 *   - the icon (IDI_HAND, IDI_QUESTION, IDI_EXCLAMATION, IDI_ASTERISK) at
 *     x 18; the text (DrawText, word-wrapped) at x 68; the taller of the
 *     two at y 16, the other centred on it;
 *   - the client width the larger of the text's right edge + 16 and the
 *     buttons' width + 86; buttons 58 x 28, 16 apart, centred in dialog
 *     units (rounded up), 22 below the icon or text, 10 above the bottom;
 *   - the dialog centred on the screen (the class aligns its left edge to
 *     8 pixels: CS_BYTEALIGNWINDOW).
 *   - the text wrapped so that the box is at most 5/8 of the screen wide
 *     (US4's fourth box: a 297-pixel line, "be" not fitting after it,
 *     in a box 396 wide; UNVERIFIED as the rule).
 * UNVERIFIED: the layout
 * without an icon, the button order and texts for every MB_ type beyond
 * those seen, and the ±1 pixel of the buttons' centring.
 */
#include "user.h"

HWND create_window(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                   void __far *);

static int mb_result;

static BOOL CALLBACK __loadds mb_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_COMMAND && wp >= IDOK && wp <= IDNO) {
        mb_result = wp;
        EndDialog(h, wp);
        return TRUE;
    }
    return msg == WM_INITDIALOG;
}

int WINAPI MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type)
{
    static const char *const names[] = { "OK", "Cancel", "&Abort", "&Retry", "&Ignore", "&Yes", "&No" };
    static const BYTE sets[6][4] = {
        { IDOK, 0 }, { IDOK, IDCANCEL, 0 }, { IDABORT, IDRETRY, IDIGNORE, 0 },
        { IDYES, IDNO, IDCANCEL, 0 }, { IDYES, IDNO, 0 }, { IDRETRY, IDCANCEL, 0 }
    };
    UINT set = type & MB_TYPEMASK, i, n = 0, def;
    LPCSTR iconid = 0;
    HICON icon = 0;
    HWND dlg, c;
    RECT tr;
    int tx, cw, ch, top, by, bx, iy = 16, ty = 16, textw, texth;
    HDC hdc;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    if (set > 5)
        set = 0;
    while (n < 4 && sets[set][n])
        n++;
    def = (type & MB_DEFMASK) >> 8;
    if (def >= n)
        def = 0;
    switch (type & MB_ICONMASK) {
    case MB_ICONHAND: iconid = IDI_HAND; break;
    case MB_ICONQUESTION: iconid = IDI_QUESTION; break;
    case MB_ICONEXCLAMATION: iconid = IDI_EXCLAMATION; break;
    case MB_ICONASTERISK: iconid = IDI_ASTERISK; break;
    }
    if (iconid)
        icon = LoadIcon(0, iconid);
    if (!text)
        text = "";
    if (!caption)
        caption = "Error";              /* UNVERIFIED */
    /* the text's size */
    hdc = GetDC(0);
    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    SetRect(&tr, 0, 0, sysmet[SM_CXSCREEN] * 5 / 8 - 2 * (sysmet[SM_CXDLGFRAME] + sysmet[SM_CXBORDER]) -
            (icon ? 68 : 18) - 16, 0);
    DrawText(hdc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
    ReleaseDC(0, hdc);
    textw = tr.right;
    texth = tr.bottom;
    tx = icon ? 68 : 18;
    if (icon) {
        if (texth > 32)
            iy = 16 + (texth - 32) / 2;
        else
            ty = 16 + (32 - texth) / 2;
    }
    top = 16 + (icon && texth < 32 ? 32 : texth);
    cw = tx + textw + 16;
    {
        int bw = n * 58 + (n - 1) * 16 + 86;
        if (bw > cw)
            cw = bw;
    }
    by = top + 22;
    ch = by + 28 + 10;
    {
        int ww = cw + 2 * (sysmet[SM_CXDLGFRAME] + sysmet[SM_CXBORDER]);
        int wh = ch + sysmet[SM_CYCAPTION] + 2 * sysmet[SM_CYDLGFRAME] + sysmet[SM_CYBORDER];
        int x = (sysmet[SM_CXSCREEN] - ww) / 2, y = (sysmet[SM_CYSCREEN] - wh + 1) / 2;
        dlg = create_window(WS_EX_DLGMODALFRAME, MAKEINTRESOURCE(32770), caption, style,
                            x, y, ww, wh, owner, 0, hInstUser, NULL);
    }
    if (!dlg)
        return 0;
    PW(dlg)->flags |= WF_DLG;
    SetWindowLong(dlg, 4, (LONG)(DLGPROC)mb_proc);         /* DWL_DLGPROC */
    if (icon) {
        c = create_window(0, "Static", NULL, WS_CHILD | WS_VISIBLE | SS_ICON, 18, iy, 32, 32, dlg,
                          (HMENU)-1, hInstUser, NULL);
        if (c)
            send_message(c, STM_SETICON, (WPARAM)icon, 0);
    }
    create_window(0, "Static", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, tx, ty,
                  textw + 1, texth, dlg, (HMENU)-1, hInstUser, NULL);
    bx = (cw / 2 - (n * 29 + (n - 1) * 8) + 1) / 2 * 2;
    for (i = 0; i < n; i++) {
        UINT id = sets[set][i];
        c = create_window(0, "Button", names[id - 1],
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) |
                          (i == 0 ? WS_GROUP : 0),
                          bx + i * 74, by, 58, 28, dlg, (HMENU)id, hInstUser, NULL);
        if (i == def && c)
            SetWindowWord(dlg, 18, id);                     /* the default id */
    }
    if (type & MB_SYSTEMMODAL)
        owner = 0;
    {
        HWND f = GetDlgItem(dlg, sets[set][def]);
        int r;
        extern int dialog_run(HWND dlg, HWND owner, HWND focus);
        mb_result = 0;
        r = dialog_run(dlg, owner, f);
        return r > 0 ? r : (mb_result ? mb_result : sets[set][0]);
    }
}
