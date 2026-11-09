/*
 * main.c - w16 Calculator: the window, its buttons, keyboard and menus.
 *
 * Observed on Windows 3.1 (SNAP window dumps and screenshots, CA1):
 *  - The window is the dialog template "SC" of class "SciCalc" (style
 *    WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, the system font); the menu
 *    "SM" comes with the class. The template's controls are only the
 *    radio buttons (Hex Dec Oct Bin, and Dword/Word/Byte which become
 *    Deg/Rad/Grad in decimal), the Inv and Hyp check boxes and the
 *    statics: 413 the scientific display, 414 the standard one, 400
 *    "stat", 401 and 402 " M", 403 "(=n". A hidden Edit control (495) is
 *    added; Copy goes through it.
 *  - Calculator draws the buttons itself: rounded rectangles outlined and
 *    labelled in the button's colour, in the system font (draw.c).
 *  - The standard view hides the scientific controls and shrinks the
 *    window: client 259 x 229 pixels, scientific 480 x 297 (VGA).
 *  - Keys come through the accelerator table "SA"; in the standard view
 *    what has no button there is ignored, % is percent and @ square root.
 *  - The view is kept in WIN.INI [SciCalc] layout= (0 scientific, 1
 *    standard).
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"

HINSTANCE g_hinst;
HWND g_wnd;
static HACCEL g_acc;
static const char s_class[] = "SciCalc";
static const char s_section[] = "SciCalc";

/* ---- the display and indicators ---- */

void ui_update(void)
{
    char t[80];
    int disp = g.sci ? IDD_DISPSCI : IDD_DISPSTD;
    if (g.err && g.err != ERR_SILENT) {
        LoadString(g_hinst, g.err, t, sizeof t);
        SetDlgItemText(g_wnd, disp, t);
    } else if (g.err) {
        /* the display keeps its text */
    } else if (g.showparen) {
        int i;
        for (i = 0; i < g.showparen && i < MAXPAREN; i++)
            t[i] = '(';
        t[i] = 0;
        SetDlgItemText(g_wnd, disp, t);
    } else {
        SetDlgItemText(g_wnd, disp, g.disp);
    }
    if (g.sci) {
        SetDlgItemText(g_wnd, IDD_MEMSCI, g.mem != 0 ? " M" : "");
        if (g.depth) {
            t[0] = '(';
            t[1] = '=';
            if (g.depth >= 10) {
                t[2] = (char)('0' + g.depth / 10);
                t[3] = (char)('0' + g.depth % 10);
                t[4] = 0;
            } else {
                t[2] = (char)('0' + g.depth);
                t[3] = 0;
            }
        } else {
            t[0] = 0;
        }
        SetDlgItemText(g_wnd, IDD_PARENS, t);
        CheckDlgButton(g_wnd, IDC_INV, g.inv);
        CheckDlgButton(g_wnd, IDC_HYP, g.hyp);
    } else {
        SetDlgItemText(g_wnd, IDD_MEMSTD, g.mem != 0 ? " M" : "");
    }
}

/* the radio buttons: radix, and word size or angle unit */
static void update_radios(void)
{
    char t[16];
    int i, base = g.radix == 10 ? IDS_DEG : IDS_FIRSTKEY + 61;
    CheckRadioButton(g_wnd, IDC_BIN, IDC_HEX,
                     g.radix == 16 ? IDC_HEX : g.radix == 10 ? IDC_DEC :
                     g.radix == 8 ? IDC_OCT : IDC_BIN);
    for (i = 0; i < 3; i++) {
        LoadString(g_hinst, base + i, t, sizeof t);
        SetDlgItemText(g_wnd, IDC_SIZE1 + i, t);
    }
    CheckRadioButton(g_wnd, IDC_SIZE1, IDC_SIZE3,
                     IDC_SIZE1 + (g.radix == 10 ? g.angle : g.wordsize));
}

/* the standard display's width: text that does not fit is not shown */
int ui_toowide(const char *s)
{
    HWND d = GetDlgItem(g_wnd, IDD_DISPSTD);
    HDC dc;
    RECT r;
    DWORD ext;
    int n = 0;
    if (!d)
        return 0;
    while (s[n])
        n++;
    GetClientRect(d, &r);
    dc = GetDC(d);
    ext = GetTextExtent(dc, s, n);
    ReleaseDC(d, dc);
    /* observed: 176 pixels fit the 186 pixel display, 184 do not;
       UNVERIFIED where between the limit lies */
    return (int)LOWORD(ext) > r.right - 8;
}

void ui_beep(void)
{
    MessageBeep(0);
}

/* ---- the views ---- */

static const int sci_only[] = {
    IDC_HEX, IDC_DEC, IDC_OCT, IDC_BIN, IDC_SIZE1, IDC_SIZE2, IDC_SIZE3,
    IDC_INV, IDC_HYP, IDD_STATTEXT, IDD_MEMSCI, IDD_PARENS, IDD_DISPSCI, 0
};

static void set_view(int sci, int first)
{
    RECT rc;
    int i;
    char t[64];
    HMENU m = GetMenu(g_wnd);
    if (!first) {
        /* the new display starts with the old one's text */
        GetDlgItemText(g_wnd, sci ? IDD_DISPSTD : IDD_DISPSCI, t, sizeof t);
        SetDlgItemText(g_wnd, sci ? IDD_DISPSCI : IDD_DISPSTD, t);
    }
    calc_set_view(sci);
    for (i = 0; sci_only[i]; i++)
        ShowWindow(GetDlgItem(g_wnd, sci_only[i]), sci ? SW_SHOWNA : SW_HIDE);
    ShowWindow(GetDlgItem(g_wnd, IDD_DISPSTD), sci ? SW_HIDE : SW_SHOWNA);
    ShowWindow(GetDlgItem(g_wnd, IDD_MEMSTD), sci ? SW_HIDE : SW_SHOWNA);
    if (!sci && g_statbox)
        stat_close();
    SetRect(&rc, 0, 0, sci ? SCI_CX : STD_CX, sci ? SCI_CY : STD_CY);
    AdjustWindowRect(&rc, GetWindowLong(g_wnd, GWL_STYLE), TRUE);
    SetWindowPos(g_wnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CheckMenuItem(m, IDM_SCIENTIFIC, sci ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_STANDARD, sci ? MF_UNCHECKED : MF_CHECKED);
    update_radios();
    InvalidateRect(g_wnd, NULL, TRUE);
    ui_update();
}

/* ---- commands ---- */

void do_command(int id)
{
    switch (id) {
    case IDM_COPY:
        clip_copy();
        ui_update();
        return;
    case IDM_PASTE:
        clip_paste();
        ui_update();
        return;
    case IDM_ABOUT:
        about();
        return;
    case IDM_SCIENTIFIC:
    case IDM_STANDARD:
        if ((id == IDM_SCIENTIFIC) != g.sci)
            set_view(id == IDM_SCIENTIFIC, 0);
        return;
    case IDM_HELPCONTENTS:
    case IDM_HELPSEARCH:
    case IDM_HELPHELP:
        help_command(id);
        return;
    case IDC_STA:
        if (!g.err && g.sci)
            stat_open();
        return;
    }
    calc_command(id);
    if (id >= IDC_BIN && id <= IDC_SIZE3)
        update_radios();
    ui_update();
}

/* paste: each command goes straight to the engine */
void paste_command(int id, int fromkey)
{
    (void)fromkey;
    if (id == IDC_STA || id >= IDM_COPY)
        do_command(id);
    else
        calc_command(id);
    if (id >= IDC_BIN && id <= IDC_SIZE3)
        update_radios();
}

static void on_command(HWND w, WPARAM wp, LPARAM lp)
{
    int id = (int)wp;
    (void)w;
    if (LOWORD(lp) == 0 && HIWORD(lp) == 1) {
        /* an accelerator */
        if (!g.sci) {
            id = calc_std_key(id);
            if (!id)
                return;
        }
        do_command(id);
        return;
    }
    if (LOWORD(lp) != 0) {
        /* a control: the radio buttons and check boxes */
        if (HIWORD(lp) != BN_CLICKED)
            return;
        if (id == IDC_INV || id == IDC_HYP) {
            if (!g.err) {
                if (id == IDC_INV)
                    g.inv = IsDlgButtonChecked(g_wnd, id);
                else
                    g.hyp = IsDlgButtonChecked(g_wnd, id);
            }
            ui_update();
            SetFocus(g_wnd);
            return;
        }
        do_command(id);
        update_radios();
        SetFocus(g_wnd);
        return;
    }
    do_command(id);
}

LRESULT CALLBACK __export CalcWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        draw_paint(w);
        return 0;
    case WM_LBUTTONDOWN:
        mouse_down(w, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        mouse_move(w, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        mouse_up(w, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_COMMAND:
        on_command(w, wp, lp);
        return 0;
    case WM_INITMENUPOPUP:
        /* Paste only with text on the clipboard (observed) */
        EnableMenuItem((HMENU)wp, IDM_PASTE,
                       IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED);
        break;
    case WM_SETFOCUS:
        /* the focus stays on the window itself (observed: no control
           shows the focus after RET) */
        return 0;
    case WM_MENUSELECT:
        help_menuselect(wp, lp);
        break;
    case WM_CLOSE:
        WriteProfileString(s_section, "layout", g.sci ? "0" : "1");
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        help_quit(w);
        PostQuitMessage(0);
        return 0;
    }
    return DefDlgProc(w, msg, wp, lp);
}

static BOOL register_class(HINSTANCE hinst)
{
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CalcWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon(hinst, "SC");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = "SM";
    wc.lpszClassName = s_class;
    return RegisterClass(&wc);
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show)
{
    MSG msg;
    int sci;
    (void)cmdline;
    g_hinst = hinst;
    if (!hprev && !register_class(hinst))
        return 1;
    fp_init();
    calc_init();
    g_wnd = CreateDialog(hinst, "SC", NULL, NULL);
    if (!g_wnd)
        return 1;
    CreateWindow("Edit", "", WS_CHILD, -270, -15, 270, 10, g_wnd, (HMENU)IDD_EDIT, hinst, NULL);
    g_acc = LoadAccelerators(hinst, "SA");
    sci = GetProfileInt(s_section, "layout", 1) == 0;
    set_view(sci, 1);
    calc_clear();
    ui_update();
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (g_statbox && IsDialogMessage(g_statbox, &msg))
            continue;
        if (TranslateAccelerator(g_wnd, g_acc, &msg))
            continue;
        if (IsDialogMessage(g_wnd, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
