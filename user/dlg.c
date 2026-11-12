/*
 * dlg.c - dialog boxes: templates, the dialog manager (DefDlgProc, class
 * #32770), modal and modeless dialogs, IsDialogMessage, dialog items.
 *
 * Template format (3.1 SDK, DLGTEMPLATE): style (DWORD), item count
 * (BYTE), x, y, cx, cy (dialog units), menu (0, FFh + ordinal, or a
 * string), class (0 or a string), caption, and with DS_SETFONT a point
 * size and a face name; then each item: x, y, cx, cy, id, style (DWORD),
 * class (80h-85h: Button, Edit, Static, ListBox, ScrollBar, ComboBox, or a
 * string), text (FFh + ordinal or a string), and a count of extra bytes
 * that follow. Dialog units: x * cxChar / 4, y * cyChar / 8 with the
 * dialog font's base units (10.4).
 *
 * #32770's window extra bytes (Windows 3.1: 30, US2; DLGWINDOWEXTRA): w16
 * keeps the result, the dialog procedure, the font, the default id and
 * flags in them; DWL_MSGRESULT (0), DWL_DLGPROC (4) and DWL_USER (8) are
 * the SDK's.
 */
#include "user.h"

#define DW_RESULT   0           /* DWL_MSGRESULT */
#define DW_PROC     4           /* DWL_DLGPROC */
#define DW_USER     8           /* DWL_USER */
#define DW_FONT     12
#define DW_ENDED    14          /* EndDialog was called */
#define DW_ENDCODE  16
#define DW_DEFID    18
#define DW_FOCUS    20          /* the item that had the focus */
#define DW_CX       22          /* base units */
#define DW_CY       24
#define DW_MODAL    26

HBRUSH sys_brush(HBRUSH b);

void button_click(HWND h);

/* ---- base units of a dialog font ---- */
static void font_units(HFONT f, int *cx, int *cy)
{
    static char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    TEXTMETRIC tm;
    HFONT old;
    if (!f) {
        *cx = cxSysChar;
        *cy = cySysChar;
        return;
    }
    old = SelectObject(hdcScreen, f);
    GetTextMetrics(hdcScreen, &tm);
    *cy = tm.tmHeight;
    *cx = (LOWORD(GetTextExtent(hdcScreen, letters, 52)) / 26 + 1) / 2;
    SelectObject(hdcScreen, old);
}

/* v * u / d rounded to the nearest, as MulDiv (US4: a Run dialog with
   base units 7 x 13 puts x 6, y 20 at 11, 33) */
static int du(int v, int u, int d)
{
    long p = (long)v * u;
    return (int)(p >= 0 ? (p + d / 2) / d : -((-p + d / 2) / d));
}

void WINAPI MapDialogRect(HWND h, RECT __far *r)
{
    int cx = GetWindowWord(h, DW_CX), cy = GetWindowWord(h, DW_CY);
    if (!cx)
        cx = cxSysChar, cy = cySysChar;
    r->left = du(r->left, cx, 4);
    r->right = du(r->right, cx, 4);
    r->top = du(r->top, cy, 8);
    r->bottom = du(r->bottom, cy, 8);
}

/* ---- the dialog manager ---- */
static LONG call_dlgproc(HWND h, UINT msg, WPARAM wp, LPARAM lp, BOOL *handled)
{
    DLGPROC f = (DLGPROC)GetWindowLong(h, DW_PROC);
    LONG r = 0;
    *handled = FALSE;
    if (f) {
        PWND w = valid_wnd(h);
        r = call_wndproc((WNDPROC)f, w ? (WORD)w->hInstance : 0, h, msg, wp, lp);
        *handled = LOWORD(r) != 0;
    }
    return r;
}

static HWND first_tab(HWND dlg)
{
    HWND c;
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        LONG s = GetWindowLong(c, GWL_STYLE);
        if ((s & WS_TABSTOP) && (s & WS_VISIBLE) && !(s & WS_DISABLED))
            return c;
    }
    return 0;
}

static BOOL is_button_type(HWND c, UINT type)
{
    char cls[16];
    GetClassName(c, cls, sizeof cls);
    return !lstrcmpi(cls, "Button") && (GetWindowLong(c, GWL_STYLE) & 0x0F) == type;
}

static void set_dlg_focus(HWND dlg, HWND c)
{
    char cls[16];
    if (!c)
        return;
    SetFocus(c);
    GetClassName(c, cls, sizeof cls);
    if (!lstrcmpi(cls, "Edit"))
        SendMessage(c, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
    (void)dlg;
}

/* the default push button follows the focus among push buttons */
static void update_default(HWND dlg, HWND focus)
{
    HWND c, def = GetDlgItem(dlg, GetWindowWord(dlg, DW_DEFID));
    BOOL push = focus && (is_button_type(focus, BS_PUSHBUTTON) ||
                          is_button_type(focus, BS_DEFPUSHBUTTON));
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        BOOL want = push ? c == focus : c == def;
        if (is_button_type(c, want ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON))
            SendMessage(c, BM_SETSTYLE, want ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    }
}

LRESULT WINAPI DefDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    BOOL handled;
    LONG r;
    SetWindowLong(h, DW_RESULT, 0);
    r = call_dlgproc(h, msg, wp, lp, &handled);
    switch (msg) {
    case WM_CTLCOLOR:
    case WM_COMPAREITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_QUERYDRAGICON:
    case WM_INITDIALOG:
        /* without a dialog procedure: TRUE, the focus as usual */
        if (msg == WM_INITDIALOG)
            return GetWindowLong(h, DW_PROC) ? r : TRUE;
        if (handled)
            return r;
        break;
    default:
        if (handled)
            return GetWindowLong(h, DW_RESULT);
    }
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        HBRUSH b = (HBRUSH)(WORD)send_message(h, WM_CTLCOLOR, wp, MAKELONG(h, CTLCOLOR_DLG));
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, b ? b : sysbrush[COLOR_WINDOW]);
        return 1;
    }
    case WM_SETFOCUS: {
        HWND f = (HWND)GetWindowWord(h, DW_FOCUS);
        if (!f || !IsChild(h, f))
            f = first_tab(h);
        set_dlg_focus(h, f);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            if (hwndFocus && IsChild(h, hwndFocus))
                SetWindowWord(h, DW_FOCUS, (WORD)hwndFocus);
        } else if (!HIWORD(lp))
            send_message(h, WM_SETFOCUS, 0, 0);
        return 0;
    case WM_CLOSE: {
        HWND c = GetDlgItem(h, IDCANCEL);
        if (!c || IsWindowEnabled(c))
            PostMessage(h, WM_COMMAND, IDCANCEL, MAKELONG(c, BN_CLICKED));
        return 0;
    }
    case WM_NEXTDLGCTL: {
        HWND c = lp ? (HWND)wp : GetNextDlgTabItem(h, hwndFocus, wp != 0);
        set_dlg_focus(h, c);
        update_default(h, c);
        return 0;
    }
    case DM_GETDEFID:
        return MAKELONG(GetWindowWord(h, DW_DEFID), DC_HASDEFID);
    case DM_SETDEFID:
        SetWindowWord(h, DW_DEFID, wp);
        update_default(h, hwndFocus);
        return 1;
    case WM_GETFONT:
        return GetWindowWord(h, DW_FONT);
    case WM_CTLCOLOR:
        return DefWindowProc(h, msg, wp, lp);
    }
    return DefWindowProc(h, msg, wp, lp);
}

LRESULT WINAPI U_DEFDLGPROC(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefDlgProc(h, msg, wp, lp);
}

/* ---- template parsing ---- */
static const BYTE __far *skip_sz(const BYTE __far *p, LPCSTR *out)
{
    *out = (LPCSTR)p;
    while (*p)
        p++;
    return p + 1;
}

static const char *item_class(BYTE c)
{
    switch (c) {
    case 0x80: return "Button";
    case 0x81: return "Edit";
    case 0x82: return "Static";
    case 0x83: return "ListBox";
    case 0x84: return "ScrollBar";
    case 0x85: return "ComboBox";
    }
    return "Static";
}

HWND create_dialog(HINSTANCE hinst, const BYTE __far *t, HWND owner, DLGPROC proc,
                   LPARAM param, BOOL modal)
{
    DWORD style = *(const DWORD __far *)t;
    int n = t[4], x = ((const int __far *)(t + 5))[0], y = ((const int __far *)(t + 5))[1];
    int cx = ((const int __far *)(t + 5))[2], cy = ((const int __far *)(t + 5))[3];
    const BYTE __far *p = t + 13;
    LPCSTR menu = 0, cls = 0, caption, face = 0;
    int pts = 0, ux, uy, i;
    HFONT font = 0;
    HWND h, focus = 0;
    RECT r;
    HMENU hmenu = 0;
    /* menu */
    if (*p == 0xFF) {
        menu = MAKEINTRESOURCE(*(const WORD __far *)(p + 1));
        p += 3;
    } else if (*p) {
        p = skip_sz(p, &menu);
    } else
        p++;
    /* class */
    if (*p)
        p = skip_sz(p, &cls);
    else
        p++;
    p = skip_sz(p, &caption);
    if (style & DS_SETFONT) {
        pts = *(const WORD __far *)p;
        p = skip_sz(p + 2, &face);
        {
            LOGFONT lf;
            fmemset(&lf, 0, sizeof lf);
            lf.lfHeight = -(pts * GetDeviceCaps(hdcScreen, LOGPIXELSY) / 72);
            lf.lfWeight = FW_BOLD;      /* UNVERIFIED: 3.1 dialog fonts are bold */
            lstr_cpyn(lf.lfFaceName, face, LF_FACESIZE);
            font = CreateFontIndirect(&lf);
        }
    }
    font_units(font, &ux, &uy);
    SetRect(&r, du(x, ux, 4), du(y, uy, 8), 0, 0);
    r.right = r.left + du(cx, ux, 4);
    r.bottom = r.top + du(cy, uy, 8);
    /* the window rectangle around that client area */
    {
        int fx = (style & WS_THICKFRAME) ? sysmet[SM_CXFRAME] :
                 ((style & DS_MODALFRAME) || (style & WS_DLGFRAME)) ?
                 sysmet[SM_CXDLGFRAME] + sysmet[SM_CXBORDER] :
                 (style & WS_BORDER) ? sysmet[SM_CXBORDER] : 0;
        int fy = (style & WS_THICKFRAME) ? sysmet[SM_CYFRAME] :
                 ((style & DS_MODALFRAME) || (style & WS_DLGFRAME)) ?
                 sysmet[SM_CYDLGFRAME] + sysmet[SM_CYBORDER] :
                 (style & WS_BORDER) ? sysmet[SM_CYBORDER] : 0;
        int top = fy;
        if ((style & WS_CAPTION) == WS_CAPTION)
            top += sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER];
        if (menu)
            top += sysmet[SM_CYMENU] + sysmet[SM_CYBORDER];
        /* the template places the client area (US4: a dialog at 10,10
           dialog units has its client area at 20,20 less the alignment) */
        r.left -= fx;
        r.right += fx;
        r.top -= top;
        r.bottom += fy;
    }
    if (!(style & WS_CHILD) && owner) {
        /* position relative to the owner's client area (3.1 SDK) */
        PWND o = valid_wnd(owner);
        if (o && !(style & DS_ABSALIGN))
            OffsetRect(&r, o->rcClient.left, o->rcClient.top);
    }
    /* kept on the screen (US4: the dialog at y 10 units starts at 0;
       Calculator's, 240 x 158 units at 310,65, comes up on 3.1 at
       154,110, against the screen's right and bottom edges. UNVERIFIED:
       w16 puts it at 150,106 - its window is 4 pixels wider and taller
       than 3.1's, unexplained) */
    if (!(style & WS_CHILD) && r.right > sysmet[SM_CXSCREEN])
        OffsetRect(&r, sysmet[SM_CXSCREEN] - r.right, 0);
    if (!(style & WS_CHILD) && r.bottom > sysmet[SM_CYSCREEN])
        OffsetRect(&r, 0, sysmet[SM_CYSCREEN] - r.bottom);
    if (!(style & WS_CHILD) && r.top < 0)
        OffsetRect(&r, 0, -r.top);
    if (!(style & WS_CHILD) && r.left < 0)
        OffsetRect(&r, -r.left, 0);
    if (menu)
        hmenu = LoadMenu(hinst, menu);
    h = create_window((style & DS_MODALFRAME) ? WS_EX_DLGMODALFRAME : 0,
                      cls ? cls : MAKEINTRESOURCE(32770), caption,
                      style & ~(WS_VISIBLE | DS_SETFONT | DS_MODALFRAME | DS_ABSALIGN |
                                DS_SYSMODAL | DS_LOCALEDIT | DS_NOIDLEMSG),
                      r.left, r.top, r.right - r.left, r.bottom - r.top, owner, hmenu,
                      hinst, NULL);
    if (!h)
        return 0;
    PW(h)->flags |= WF_DLG;
    SetWindowLong(h, DW_PROC, (LONG)proc);
    SetWindowWord(h, DW_FONT, (WORD)font);
    SetWindowWord(h, DW_CX, ux);
    SetWindowWord(h, DW_CY, uy);
    SetWindowWord(h, DW_DEFID, IDOK);
    SetWindowWord(h, DW_MODAL, modal | (modal && (style & DS_SYSMODAL) ? 4 : 0));
    /* the items */
    for (i = 0; i < n; i++) {
        int ix = ((const int __far *)p)[0], iy = ((const int __far *)p)[1];
        int icx = ((const int __far *)p)[2], icy = ((const int __far *)p)[3];
        UINT id = ((const WORD __far *)p)[4];
        DWORD is = *(const DWORD __far *)(p + 10);
        LPCSTR icls, text;
        const BYTE __far *q = p + 14;
        HWND c;
        if (*q & 0x80) {
            icls = item_class(*q);
            q++;
        } else
            q = skip_sz(q, &icls);
        if (*q == 0xFF) {
            text = MAKEINTRESOURCE(*(const WORD __far *)(q + 1));
            q += 3;
        } else
            q = skip_sz(q, &text);
        {
            BYTE extra = *q++;
            c = create_window(0, icls, text, is | WS_CHILD, du(ix, ux, 4), du(iy, uy, 8),
                              du(icx, ux, 4), du(icy, uy, 8), h, (HMENU)id, hinst,
                              extra ? (void __far *)q : NULL);
            q += extra;
        }
        p = q;
        if (!c)
            continue;
        if (font)
            send_message(c, WM_SETFONT, (WPARAM)font, FALSE);
        if ((is & 0x0F) == BS_DEFPUSHBUTTON && !lstr_cmpi(icls, "Button"))
            SetWindowWord(h, DW_DEFID, id);
        if (!focus && (is & WS_TABSTOP) && !(is & WS_DISABLED))
            focus = c;
    }
    if (font)
        send_message(h, WM_SETFONT, (WPARAM)font, FALSE);
    /* WM_INITDIALOG goes to the window, so a dialog of its own class
       (Calculator: CLASS "SciCalc", no dialog procedure) gets it in its
       class's procedure; DefDlgProc hands it on to the dialog procedure.
       It was called straight, NULL included: a GP fault in USER */
    if (send_message(h, WM_INITDIALOG, (WPARAM)focus, param) && valid_wnd(h)) {
        /* TRUE: the focus goes to the first tab stop even where the
           procedure set it elsewhere meanwhile (3.1: Program Manager's
           Exit dialog sets it to Cancel and answers TRUE, and OK has
           it) */
        if (focus) {
            SetWindowWord(h, DW_FOCUS, (WORD)focus);
            if (hwndFocus && IsChild(h, hwndFocus))
                set_dlg_focus(h, focus);
        }
    }
    if (!valid_wnd(h))
        return 0;
    if (style & WS_VISIBLE) {
        ShowWindow(h, SW_SHOWNORMAL);
        UpdateWindow(h);
    }
    return h;
}

static const BYTE __far *find_template(HINSTANCE hinst, LPCSTR name, HGLOBAL *g)
{
    HRSRC r = FindResource(hinst, name, RT_DIALOG);
    if (!r)
        return 0;
    *g = LoadResource(hinst, r);
    return (const BYTE __far *)LockResource(*g);
}

HWND WINAPI CreateDialogIndirectParam(HINSTANCE hinst, const void __far *t, HWND owner,
                                      DLGPROC proc, LPARAM param)
{
    return create_dialog(hinst, (const BYTE __far *)t, owner, proc, param, FALSE);
}

HWND WINAPI CreateDialogIndirect(HINSTANCE hinst, const void __far *t, HWND owner, DLGPROC proc)
{
    return create_dialog(hinst, (const BYTE __far *)t, owner, proc, 0, FALSE);
}

HWND WINAPI CreateDialogParam(HINSTANCE hinst, LPCSTR name, HWND owner, DLGPROC proc, LPARAM param)
{
    HGLOBAL g;
    const BYTE __far *t = find_template(hinst, name, &g);
    HWND h;
    if (!t)
        return 0;
    h = create_dialog(hinst, t, owner, proc, param, FALSE);
    GlobalUnlock(g);
    FreeResource(g);
    return h;
}

HWND WINAPI CreateDialog(HINSTANCE hinst, LPCSTR name, HWND owner, DLGPROC proc)
{
    return CreateDialogParam(hinst, name, owner, proc, 0);
}

/* the modal loop: the owner disabled, messages through IsDialogMessage */
static int modal_loop(HWND h, HWND owner)
{
    MSG m;
    int code;
    BOOL ownerwas = FALSE;
    /* DS_SYSMODAL: the system-modal window while it runs (3.1 SDK;
       Program Manager's Exit dialog: a click on Program Manager does
       nothing) */
    BOOL sysmodal = (GetWindowWord(h, DW_MODAL) & 4) != 0;
    HWND oldsm = sysmodal ? SetSysModalWindow(h) : 0;
    if (owner && valid_wnd(owner)) {
        ownerwas = !EnableWindow(owner, FALSE);
    }
    /* bit 1: EndDialog enables the owner again (it was enabled before);
       bit 2: system modal */
    SetWindowWord(h, DW_MODAL, (ownerwas ? 3 : 1) | (sysmodal ? 4 : 0));
    ShowWindow(h, SW_SHOWNORMAL);
    UpdateWindow(h);
    while (valid_wnd(h) && !GetWindowWord(h, DW_ENDED)) {
        if (!GetMessage(&m, 0, 0, 0)) {
            PostQuitMessage(m.wParam);
            break;
        }
        if (!IsDialogMessage(h, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        if (valid_wnd(h) && !GetWindowWord(h, DW_ENDED) && !PeekMessage(&m, 0, 0, 0, PM_NOREMOVE | PM_NOYIELD))
            send_message(owner ? owner : h, WM_ENTERIDLE, MSGF_DIALOGBOX, MAKELONG(h, 0));
    }
    code = valid_wnd(h) ? (int)GetWindowWord(h, DW_ENDCODE) : -1;
    if (sysmodal)
        SetSysModalWindow(oldsm);
    if (owner && valid_wnd(owner) && ownerwas)
        EnableWindow(owner, TRUE);
    if (valid_wnd(h)) {
        if (owner && valid_wnd(owner) && GetActiveWindow() == h)
            SetActiveWindow(owner);
        DestroyWindow(h);
    }
    return code;
}

/* MessageBox's dialog (msgbox.c): shown, focus on the default button */
int dialog_run(HWND dlg, HWND owner, HWND focus)
{
    SetWindowWord(dlg, DW_MODAL, 1);
    if (focus)
        SetWindowWord(dlg, DW_FOCUS, (WORD)focus);
    return modal_loop(dlg, owner);
}

int WINAPI DialogBoxIndirectParam(HINSTANCE hinst, HGLOBAL ht, HWND owner, DLGPROC proc, LPARAM param)
{
    const BYTE __far *t = (const BYTE __far *)GlobalLock(ht);
    HWND h;
    if (!t)
        return -1;
    h = create_dialog(hinst, t, owner, proc, param, TRUE);
    GlobalUnlock(ht);
    return h ? modal_loop(h, owner) : -1;
}

int WINAPI DialogBoxIndirect(HINSTANCE hinst, HGLOBAL ht, HWND owner, DLGPROC proc)
{
    return DialogBoxIndirectParam(hinst, ht, owner, proc, 0);
}

int WINAPI DialogBoxParam(HINSTANCE hinst, LPCSTR name, HWND owner, DLGPROC proc, LPARAM param)
{
    HGLOBAL g;
    const BYTE __far *t = find_template(hinst, name, &g);
    HWND h;
    if (!t)
        return -1;
    h = create_dialog(hinst, t, owner, proc, param, TRUE);
    GlobalUnlock(g);
    FreeResource(g);
    return h ? modal_loop(h, owner) : -1;
}

int WINAPI DialogBox(HINSTANCE hinst, LPCSTR name, HWND owner, DLGPROC proc)
{
    return DialogBoxParam(hinst, name, owner, proc, 0);
}

void WINAPI EndDialog(HWND h, int code)
{
    if (!valid_wnd(h))
        return;
    SetWindowWord(h, DW_ENDED, 1);
    SetWindowWord(h, DW_ENDCODE, code);
    if (!GetWindowWord(h, DW_MODAL))
        return;
    /* the owner enabled before the dialog hides, so the activation goes
       back to it (ShellAbout's dialog left Program Manager inactive and
       deaf otherwise); a dialog its program hid already (Program
       Manager's Run, before WinExec) is not active and gives nothing */
    {
        PWND w = valid_wnd(h);
        if ((GetWindowWord(h, DW_MODAL) & 2) && w && w->hwndOwner && valid_wnd(w->hwndOwner))
            EnableWindow(w->hwndOwner, TRUE);
    }
    ShowWindow(h, SW_HIDE);
    PostMessage(h, WM_NULL, 0, 0);      /* wake the modal loop */
}

/* ---- items ---- */
HWND WINAPI GetDlgItem(HWND h, int id)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetWindowWord(c, GWW_ID) == (WORD)id)
            return c;
    return 0;
}

int WINAPI GetDlgCtrlID(HWND h)
{
    return GetWindowWord(h, GWW_ID);
}

LRESULT WINAPI SendDlgItemMessage(HWND h, int id, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND c = GetDlgItem(h, id);
    return c ? send_message(c, msg, wp, lp) : 0;
}

void WINAPI SetDlgItemText(HWND h, int id, LPCSTR s)
{
    HWND c = GetDlgItem(h, id);
    if (c)
        SetWindowText(c, s);
}

int WINAPI GetDlgItemText(HWND h, int id, LPSTR s, int n)
{
    HWND c = GetDlgItem(h, id);
    if (!c) {
        if (n > 0)
            s[0] = 0;
        return 0;
    }
    return GetWindowText(c, s, n);
}

void WINAPI SetDlgItemInt(HWND h, int id, UINT v, BOOL sign)
{
    char b[8];
    wsprintf(b, sign ? "%d" : "%u", v);
    SetDlgItemText(h, id, b);
}

UINT WINAPI GetDlgItemInt(HWND h, int id, BOOL __far *ok, BOOL sign)
{
    char b[16], *p = b;
    long v = 0;
    BOOL neg = FALSE, any = FALSE;
    GetDlgItemText(h, id, b, sizeof b);
    while (*p == ' ')
        p++;
    if (sign && *p == '-') {
        neg = TRUE;
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        any = TRUE;
    }
    while (*p == ' ')
        p++;
    if (ok)
        *ok = any && !*p && (sign ? v <= 32768L : v <= 65535L);
    return (UINT)(neg ? -v : v);
}

void WINAPI CheckDlgButton(HWND h, int id, UINT c)
{
    SendDlgItemMessage(h, id, BM_SETCHECK, c, 0);
}

UINT WINAPI IsDlgButtonChecked(HWND h, int id)
{
    return (UINT)SendDlgItemMessage(h, id, BM_GETCHECK, 0, 0);
}

void WINAPI CheckRadioButton(HWND h, int first, int last, int check)
{
    int i;
    for (i = first; i <= last; i++)
        SendDlgItemMessage(h, i, BM_SETCHECK, i == check, 0);
}

HWND WINAPI GetNextDlgTabItem(HWND dlg, HWND c, BOOL prev)
{
    HWND s, best = 0, first = 0, last = 0;
    BOOL after = FALSE;
    for (s = GetWindow(dlg, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT)) {
        LONG st = GetWindowLong(s, GWL_STYLE);
        BOOL ok = (st & WS_TABSTOP) && (st & WS_VISIBLE) && !(st & WS_DISABLED);
        if (ok) {
            if (!first)
                first = s;
            last = s;
        }
        if (s == c) {
            if (prev && best)
                return best;
            after = TRUE;
            continue;
        }
        if (!ok)
            continue;
        if (!prev && after)
            return s;
        if (prev)
            best = s;
    }
    return prev ? (best ? best : last) : first;
}

HWND WINAPI GetNextDlgGroupItem(HWND dlg, HWND c, BOOL prev)
{
    /* the items between WS_GROUP marks around c, cycling */
    HWND s, start = 0, end = 0, res;
    HWND list[64];
    int n = 0, i, at = 0;
    for (s = GetWindow(dlg, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT)) {
        if ((GetWindowLong(s, GWL_STYLE) & WS_GROUP) && n) {
            if (start)
                break;
            n = 0;
        }
        if (n < 64)
            list[n++] = s;
        if (s == c)
            start = s;
    }
    (void)end;
    for (i = 0; i < n; i++)
        if (list[i] == c)
            at = i;
    for (i = 1; i <= n; i++) {
        res = list[(at + (prev ? n - i : i)) % n];
        if (IsWindowVisible(res) && IsWindowEnabled(res))
            return res;
    }
    return c;
}

static HWND mnemonic_item(HWND dlg, char ch)
{
    HWND s;
    char text[64], *p;
    ch = (char)(DWORD)AnsiUpper((LPSTR)MAKELP(0, (BYTE)ch));
    for (s = GetWindow(dlg, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT)) {
        if (!IsWindowVisible(s) || !IsWindowEnabled(s))
            continue;
        GetWindowText(s, text, sizeof text);
        for (p = text; *p; p++)
            if (*p == '&' && p[1] != '&' && p[1]) {
                if ((char)(DWORD)AnsiUpper((LPSTR)MAKELP(0, (BYTE)p[1])) == ch)
                    return s;
                break;
            }
    }
    return 0;
}

BOOL WINAPI IsDialogMessage(HWND dlg, MSG __far *m)
{
    UINT code;
    HWND c;
    if (!valid_wnd(dlg) || (m->hwnd != dlg && !IsChild(dlg, m->hwnd)))
        return FALSE;
    if (m->message != WM_KEYDOWN && m->message != WM_CHAR && m->message != WM_SYSCHAR) {
        TranslateMessage(m);
        DispatchMessage(m);
        return TRUE;
    }
    code = (UINT)send_message(m->hwnd, WM_GETDLGCODE, m->wParam, (LPARAM)m);
    if (code & DLGC_WANTMESSAGE) {
        TranslateMessage(m);
        DispatchMessage(m);
        return TRUE;
    }
    if (m->message == WM_KEYDOWN) {
        switch (m->wParam) {
        case VK_TAB:
            if (!(code & DLGC_WANTTAB)) {
                send_message(dlg, WM_NEXTDLGCTL, (GetKeyState(VK_SHIFT) & 0x8000) != 0, 0);
                return TRUE;
            }
            break;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
            if (!(code & DLGC_WANTARROWS)) {
                c = GetNextDlgGroupItem(dlg, m->hwnd, m->wParam == VK_LEFT || m->wParam == VK_UP);
                set_dlg_focus(dlg, c);
                if (send_message(c, WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON)
                    button_click(c);
                return TRUE;
            }
            break;
        case VK_RETURN: {
            DWORD def = send_message(dlg, DM_GETDEFID, 0, 0);
            UINT id = HIWORD(def) == DC_HASDEFID ? LOWORD(def) : IDOK;
            c = GetDlgItem(dlg, id);
            if (!(code & DLGC_WANTALLKEYS)) {
                if (!c || IsWindowEnabled(c))
                    send_message(dlg, WM_COMMAND, id, MAKELONG(c, BN_CLICKED));
                return TRUE;
            }
            break;
        }
        case VK_ESCAPE:
            if (!(code & DLGC_WANTALLKEYS)) {
                c = GetDlgItem(dlg, IDCANCEL);
                if (!c || IsWindowEnabled(c))
                    send_message(dlg, WM_COMMAND, IDCANCEL, MAKELONG(c, BN_CLICKED));
                return TRUE;
            }
            break;
        }
    }
    if ((m->message == WM_CHAR && !(code & (DLGC_WANTCHARS | DLGC_HASSETSEL))) ||
        m->message == WM_SYSCHAR) {
        c = mnemonic_item(dlg, (char)m->wParam);
        if (c) {
            UINT dc = (UINT)send_message(c, WM_GETDLGCODE, 0, 0);
            if (dc & DLGC_STATIC) {     /* a label: the next tab item */
                HWND n = GetWindow(c, GW_HWNDNEXT);
                if (n)
                    set_dlg_focus(dlg, n);
            } else if (dc & DLGC_BUTTON) {
                set_dlg_focus(dlg, c);
                button_click(c);
            } else
                set_dlg_focus(dlg, c);
            return TRUE;
        }
        if (m->message == WM_SYSCHAR)
            return FALSE;
    }
    TranslateMessage(m);
    DispatchMessage(m);
    return TRUE;
}
