/*
 * dlg.c - the PIF Editor's views (the Standard-mode and the 386
 * Enhanced-mode window, each a child dialog filling the frame above the
 * status line) and the Advanced Options dialog (ARCHITECTURE.md 15.11).
 *
 * As observed on 3.1 (PE1-PE15): the check boxes and radio buttons are
 * plain ones the program sets itself; the numbers are checked when the
 * focus leaves their box and again before a save: out of range, a
 * message box says what is allowed, the box gets its last good value back
 * and the focus. The priorities (Advanced, 1-10000) are checked when OK
 * is chosen: a bad one silently gets its last good value back and the
 * dialog stays; when the focus leaves one, with the message first.
 */
#include "pifedit.h"
#include <string.h>

PIFVIEW g_v;
static BOOL validating;

#define WM_VALIDATE (WM_USER + 1)

/* check box ID -> (flag word, bit) in g_v */
typedef struct {
    WORD id;
    BYTE which;         /* 0 f63, 1 f16f, 2 std286, 3 flags386, 4 video386 */
    BYTE inverted;
    WORD bit;
} FLAGBOX;

/* Standard mode (PE7) */
static const FLAGBOX std_boxes[] = {
    { IDC_COM1, 0, 0, 0x80 }, { IDC_COM2, 0, 0, 0x40 },
    { IDC_COM3, 2, 0, 0x4000 }, { IDC_COM4, 2, 0, 0x8000 },
    { IDC_KEYBOARD, 1, 0, 0x10 },
    { IDC_NOEXCHANGE, 0, 0, 0x08 }, { IDC_NOSWITCH, 0, 0, 0x04 },
    { IDC_CLOSEEXIT, 0, 0, 0x10 }, { IDC_NOSAVE, 2, 0, 0x20 },
    { IDC_ALTTAB, 2, 0, 0x01 }, { IDC_ALTESC, 2, 0, 0x02 },
    { IDC_ALTPRTSC, 2, 0, 0x04 }, { IDC_PRTSC, 2, 0, 0x08 },
    { IDC_CTRLESC, 2, 0, 0x10 },
    { 0 }
};

/* 386 Enhanced mode's window (PE8) */
static const FLAGBOX enh_boxes[] = {
    { IDC_BACKGROUND, 3, 0, 0x0002 }, { IDC_EXCLUSIVE, 3, 0, 0x0004 },
    { IDC_CLOSEEXIT, 0, 0, 0x10 },
    { 0 }
};

/* the Advanced dialog (PE8, PE12) */
static const FLAGBOX adv_boxes[] = {
    { IDC_IDLE, 3, 0, 0x1000 }, { IDC_EMSLOCK, 3, 0, 0x8000 },
    { IDC_XMSLOCK, 4, 0, 0x0100 }, { IDC_HMA, 3, 1, 0x2000 },
    { IDC_LOCKMEM, 4, 0, 0x0400 },
    { IDC_MONTEXT, 4, 1, 0x02 }, { IDC_MONLOW, 4, 1, 0x04 }, { IDC_MONHIGH, 4, 1, 0x08 },
    { IDC_EMULATE, 4, 0, 0x01 }, { IDC_RETAIN, 4, 0, 0x80 },
    { IDC_FASTPASTE, 4, 0, 0x0200 }, { IDC_ALLOWCLOSE, 3, 0, 0x0001 },
    { IDC_ALTTAB, 3, 0, 0x0020 }, { IDC_ALTESC, 3, 0, 0x0040 },
    { IDC_ALTSPACE, 3, 0, 0x0080 }, { IDC_ALTENTER, 3, 0, 0x0100 },
    { IDC_ALTPRTSC, 3, 0, 0x0200 }, { IDC_PRTSC, 3, 0, 0x0400 },
    { IDC_CTRLESC, 3, 0, 0x0800 },
    { 0 }
};

static WORD FAR *flagword(PIFVIEW FAR *v, int which)
{
    switch (which) {
    case 0: return &v->f63;
    case 1: return &v->f16f;
    case 2: return &v->std286;
    case 3: return &v->flags386;
    default: return &v->video386;
    }
}

static void boxes_fill(HWND dlg, const FLAGBOX *b, PIFVIEW FAR *v)
{
    for (; b->id; b++) {
        BOOL on = (*flagword(v, b->which) & b->bit) != 0;
        CheckDlgButton(dlg, b->id, on != (BOOL)b->inverted);
    }
}

static void boxes_read(HWND dlg, const FLAGBOX *b, PIFVIEW FAR *v)
{
    for (; b->id; b++) {
        BOOL on = IsDlgButtonChecked(dlg, b->id) != 0;
        WORD FAR *w = flagword(v, b->which);
        if (on != (BOOL)b->inverted)
            *w |= b->bit;
        else
            *w &= ~b->bit;
    }
}

static const FLAGBOX *box_find(const FLAGBOX *b, WORD id)
{
    for (; b->id; b++)
        if (b->id == id)
            return b;
    return NULL;
}

static void setnum(HWND dlg, int id, int n)
{
    char buf[12];
    wsprintf(buf, "%d", n);
    SetDlgItemText(dlg, id, buf);
}

/* a box's number: FALSE when it is not one (a sign and digits) */
static BOOL getnum(HWND dlg, int id, int *n)
{
    char buf[16];
    int i = 0;
    long v = 0;
    BOOL neg = FALSE;
    GetDlgItemText(dlg, id, buf, sizeof buf);
    if (buf[0] == '-')
        neg = TRUE, i++;
    if (!buf[i])
        return FALSE;
    for (; buf[i]; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return FALSE;
        v = v * 10 + (buf[i] - '0');
        if (v > 99999L)
            return FALSE;
    }
    *n = (int)(neg ? -v : v);
    return TRUE;
}

/* the numbers' ranges and messages (PE10, PE11) */
typedef struct {
    WORD id;
    BYTE enh;           /* 0 Standard view, 1 386 view, 2 both */
    BYTE minus1;        /* -1 allowed */
    int max;
    WORD msg;
} RANGE;

static const RANGE ranges[] = {
    { IDC_MEMREQ, 2, 1, 640, IDS_MEMREQ640 },
    { IDC_MEMDES, 1, 1, 640, IDS_MEMDES640 },
    { IDC_EMSREQ, 1, 0, 16384, IDS_REQ16384 },
    { IDC_EMSLIM, 1, 1, 16384, IDS_LIM16384 },
    { IDC_XMSREQ, 2, 0, 16384, IDS_REQ16384 },
    { IDC_XMSLIM, 2, 1, 16384, IDS_LIM16384 },
    { 0 }
};

static int FAR *numfield(WORD id)
{
    switch (id) {
    case IDC_MEMREQ: return &g_v.memreq;
    case IDC_MEMDES: return &g_v.memdes;
    case IDC_EMSREQ: return &g_v.emsreq;
    case IDC_EMSLIM: return &g_v.emslim;
    case IDC_XMSREQ: return &g_v.xmsreq;
    case IDC_XMSLIM: return &g_v.xmslim;
    case IDC_FGPRIO: return &g_v.fgprio;
    default: return &g_v.bgprio;
    }
}

/* check one number box of the view; a bad number: the message, its last
   good value back and the focus, the caret before it (PE10, PE11, PF07) */
static BOOL check_range(HWND dlg, const RANGE *r)
{
    int n;
    if (getnum(dlg, r->id, &n) && ((n == -1 && r->minus1) || (n >= 0 && n <= r->max))) {
        *numfield(r->id) = n;
        return TRUE;
    }
    validating = TRUE;
    msgbox(g_frame, r->msg, MB_OK | MB_ICONEXCLAMATION);
    setnum(dlg, r->id, *numfield(r->id));
    SetFocus(GetDlgItem(dlg, r->id));
    validating = FALSE;
    return FALSE;
}

static const RANGE *range_find(WORD id)
{
    const RANGE *r;
    for (r = ranges; r->id; r++)
        if (r->id == id && (r->enh == 2 || r->enh == (BYTE)g_enh))
            return r;
    return NULL;
}

/* ctl NULL: every number of the view, in order */
BOOL view_validate(HWND ctl)
{
    const RANGE *r;
    if (!g_view)
        return TRUE;
    for (r = ranges; r->id; r++) {
        if (!(r->enh == 2 || r->enh == (BYTE)g_enh))
            continue;
        if (ctl && GetDlgCtrlID(ctl) != (int)r->id)
            continue;
        if (!check_range(g_view, r))
            return FALSE;
    }
    return TRUE;
}

/* the view's controls from g_v */
void view_fill(void)
{
    HWND d = g_view;
    SetDlgItemText(d, IDC_PROGRAM, g_v.program);
    SetDlgItemText(d, IDC_TITLE, g_v.title);
    SetDlgItemText(d, IDC_PARAMS, g_v.params);
    SetDlgItemText(d, IDC_DIR, g_v.dir);
    setnum(d, IDC_MEMREQ, g_v.memreq);
    setnum(d, IDC_XMSREQ, g_v.xmsreq);
    setnum(d, IDC_XMSLIM, g_v.xmslim);
    if (!g_enh) {
        CheckRadioButton(d, IDC_TEXT, IDC_GRAPHICS, (g_v.f63 & 0x02) ? IDC_GRAPHICS : IDC_TEXT);
        boxes_fill(d, std_boxes, &g_v);
    } else {
        int vid = (g_v.video386 & 0x40) ? IDC_VHIGH : (g_v.video386 & 0x20) ? IDC_VLOW : IDC_VTEXT;
        CheckRadioButton(d, IDC_VTEXT, IDC_VHIGH, vid);
        setnum(d, IDC_MEMDES, g_v.memdes);
        setnum(d, IDC_EMSREQ, g_v.emsreq);
        setnum(d, IDC_EMSLIM, g_v.emslim);
        CheckRadioButton(d, IDC_WINDOWED, IDC_FULLSCREEN,
                         (g_v.flags386 & 0x08) ? IDC_FULLSCREEN : IDC_WINDOWED);
        boxes_fill(d, enh_boxes, &g_v);
    }
}

/* g_v from the view's controls; validate: the numbers checked first */
BOOL view_commit(BOOL validate)
{
    HWND d = g_view;
    if (!d)
        return TRUE;
    if (validate && !view_validate(NULL))
        return FALSE;
    GetDlgItemText(d, IDC_PROGRAM, g_v.program, sizeof g_v.program);
    GetDlgItemText(d, IDC_TITLE, g_v.title, sizeof g_v.title);
    GetDlgItemText(d, IDC_PARAMS, g_v.params, sizeof g_v.params);
    GetDlgItemText(d, IDC_DIR, g_v.dir, sizeof g_v.dir);
    if (!g_enh) {
        if (IsDlgButtonChecked(d, IDC_GRAPHICS))
            g_v.f63 |= 0x02;
        else
            g_v.f63 &= ~0x02;
        boxes_read(d, std_boxes, &g_v);
    } else {
        g_v.video386 &= ~0x70;
        g_v.video386 |= IsDlgButtonChecked(d, IDC_VHIGH) ? 0x40 :
                        IsDlgButtonChecked(d, IDC_VLOW) ? 0x20 : 0x10;
        if (IsDlgButtonChecked(d, IDC_FULLSCREEN))
            g_v.flags386 |= 0x08;
        else
            g_v.flags386 &= ~0x08;
        boxes_read(d, enh_boxes, &g_v);
    }
    return TRUE;
}

/* the boxes' lengths (EM_LIMITTEXT; PE19) */
static void limits(HWND d)
{
    SendDlgItemMessage(d, IDC_PROGRAM, EM_LIMITTEXT, 62, 0);
    SendDlgItemMessage(d, IDC_TITLE, EM_LIMITTEXT, 30, 0);
    SendDlgItemMessage(d, IDC_PARAMS, EM_LIMITTEXT, 63, 0);
    SendDlgItemMessage(d, IDC_DIR, EM_LIMITTEXT, 63, 0);
    SendDlgItemMessage(d, IDC_MEMREQ, EM_LIMITTEXT, 3, 0);
    SendDlgItemMessage(d, IDC_MEMDES, EM_LIMITTEXT, 3, 0);
    SendDlgItemMessage(d, IDC_EMSREQ, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(d, IDC_EMSLIM, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(d, IDC_XMSREQ, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(d, IDC_XMSLIM, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(d, IDC_FGPRIO, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(d, IDC_BGPRIO, EM_LIMITTEXT, 5, 0);
}

static BOOL is_number_box(WORD id)
{
    return range_find(id) != NULL;
}

/* a priority box of Advanced lost the focus (PE19): out of 1-10000, the
   message, its last good value back, the focus */
static BOOL check_prio(HWND d, WORD id, BOOL quiet)
{
    int n;
    if (getnum(d, id, &n) && n >= 1 && n <= 10000)
        return TRUE;
    validating = TRUE;
    if (!quiet)
        msgbox(d, IDS_PRIORITY, MB_OK | MB_ICONEXCLAMATION);
    setnum(d, id, *numfield(id));
    SetFocus(GetDlgItem(d, id));
    validating = FALSE;
    return FALSE;
}

BOOL CALLBACK view_proc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        limits(d);
        return FALSE;
    case WM_VALIDATE:
        /* a number box lost the focus to another control of the view */
        if (!validating && GetFocus() && GetParent(GetFocus()) == d)
            view_validate((HWND)wp);
        return TRUE;
    case WM_COMMAND: {
        WORD id = (WORD)wp, code = HIWORD(lp);
        const FLAGBOX *b;
        if (code == EN_CHANGE) {
            g_dirty = TRUE;
            return TRUE;
        }
        if (code == EN_KILLFOCUS) {
            if (is_number_box(id) && !validating)
                PostMessage(d, WM_VALIDATE, (WPARAM)LOWORD(lp), 0);
            return TRUE;
        }
        if (code != BN_CLICKED && code != BN_DOUBLECLICKED)
            return FALSE;
        switch (id) {
        case IDC_TEXT: case IDC_GRAPHICS:
            CheckRadioButton(d, IDC_TEXT, IDC_GRAPHICS, id);
            g_dirty = TRUE;
            return TRUE;
        case IDC_VTEXT: case IDC_VLOW: case IDC_VHIGH:
            CheckRadioButton(d, IDC_VTEXT, IDC_VHIGH, id);
            g_dirty = TRUE;
            return TRUE;
        case IDC_WINDOWED: case IDC_FULLSCREEN:
            CheckRadioButton(d, IDC_WINDOWED, IDC_FULLSCREEN, id);
            g_dirty = TRUE;
            return TRUE;
        case IDC_ADVANCED:
            adv_run();
            return TRUE;
        }
        b = box_find(g_enh ? enh_boxes : std_boxes, id);
        if (b) {
            if (id == IDC_CLOSEEXIT)
                g_v.closeset = TRUE;
            CheckDlgButton(d, id, !IsDlgButtonChecked(d, id));
            g_dirty = TRUE;
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

/* the view for the mode: made, put at the frame's client origin, the frame
   sized for it (PE1, PE2: client = the template's width less a pixel,
   its height, and the 25-pixel status line; the frame a pixel taller than
   AdjustWindowRect makes it, the menu bar's line) */
void view_create(void)
{
    RECT r;
    HWND old = g_view;
    int cx, cy;
    g_view = CreateDialog(g_inst, MAKEINTRESOURCE(g_enh ? DLG_ENHANCED : DLG_STANDARD),
                          g_frame, (DLGPROC)view_proc);
    if (old)
        DestroyWindow(old);
    GetWindowRect(g_view, &r);
    cx = r.right - r.left - 1;
    cy = r.bottom - r.top;
    SetWindowPos(g_view, NULL, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    r.left = r.top = 0;
    r.right = cx;
    r.bottom = cy + 25;
    AdjustWindowRect(&r, GetWindowLong(g_frame, GWL_STYLE), TRUE);
    SetWindowPos(g_frame, NULL, 0, 0, r.right - r.left, r.bottom - r.top + 1,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    view_fill();
    g_dirty = FALSE;
}

/* ---- Advanced Options ---- */

static HWND adv_dlg;

static void adv_fill(HWND d)
{
    setnum(d, IDC_BGPRIO, g_v.bgprio);
    setnum(d, IDC_FGPRIO, g_v.fgprio);
    boxes_fill(d, adv_boxes, &g_v);
    hotkey_set(GetDlgItem(d, IDC_HOTKEY), g_v.hkscan, g_v.hkmods, g_v.hotkey);
}

/* OK: the priorities (1-10000; a bad one silently back to its last good
   value, PE15), the shortcut key (PE12), then the boxes */
static BOOL adv_ok(HWND d)
{
    static const WORD prio[] = { IDC_BGPRIO, IDC_FGPRIO };
    int i;
    WORD scan, mods;
    BOOL on;
    for (i = 0; i < 2; i++)
        if (!check_prio(d, prio[i], TRUE))
            return FALSE;
    switch (hotkey_get(GetDlgItem(d, IDC_HOTKEY), &scan, &mods, &on)) {
    case 0:
        msgbox(d, IDS_BADHOTKEY, MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(d, IDC_HOTKEY));
        return FALSE;
    case 1:
        g_v.hkscan = scan;
        g_v.hkmods = mods;
        g_v.hotkey = on;
        break;
    }
    getnum(d, IDC_BGPRIO, &g_v.bgprio);
    getnum(d, IDC_FGPRIO, &g_v.fgprio);
    boxes_read(d, adv_boxes, &g_v);
    return TRUE;
}

BOOL CALLBACK adv_proc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        /* 12 and 13 pixels from the frame's client origin, x down to a
           multiple of 8 as the dialog class's CS_BYTEALIGNWINDOW would
           (PE19: the frame at 0,0, 12,48 and 100,30) */
        POINT p;
        p.x = p.y = 0;
        ClientToScreen(g_frame, &p);
        SetWindowPos(d, NULL, (p.x + 12) & ~7, p.y + 13, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        adv_dlg = g_adv = d;
        limits(d);
        adv_fill(d);
        return TRUE;
    }
    case WM_VALIDATE:
        if (!validating && GetFocus() && GetParent(GetFocus()) == d)
            check_prio(d, (WORD)wp, FALSE);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(d, &ps);
        status_paint(d, dc);
        EndPaint(d, &ps);
        return TRUE;
    }
    case WM_COMMAND: {
        WORD id = (WORD)wp, code = HIWORD(lp);
        if (id == IDOK) {
            PIFVIEW before;
            before = g_v;
            if (adv_ok(d)) {
                if (_fmemcmp(&before, &g_v, sizeof g_v) != 0)
                    g_dirty = TRUE;
                EndDialog(d, IDOK);
            }
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
        if (code == EN_KILLFOCUS && (id == IDC_BGPRIO || id == IDC_FGPRIO)) {
            if (!validating)
                PostMessage(d, WM_VALIDATE, id, 0);
            return TRUE;
        }
        if (code == BN_CLICKED || code == BN_DOUBLECLICKED) {
            if (id == IDC_ALLOWCLOSE && !IsDlgButtonChecked(d, id)) {
                /* PE12, PF08: checked, then the warning; Cancel clears it */
                CheckDlgButton(d, id, TRUE);
                if (msgbox(d, IDS_ALLOWCLOSE, MB_OKCANCEL | MB_ICONSTOP) != IDOK)
                    CheckDlgButton(d, id, FALSE);
                return TRUE;
            }
            if (box_find(adv_boxes, id)) {
                CheckDlgButton(d, id, !IsDlgButtonChecked(d, id));
                return TRUE;
            }
        }
        return FALSE;
    }
    }
    return FALSE;
}

/* Advanced... (386 Enhanced mode): its owner the frame; while it is up
   the view's Advanced button is disabled (PE2) */
void adv_run(void)
{
    HWND b = GetDlgItem(g_view, IDC_ADVANCED);
    EnableWindow(b, FALSE);
    DialogBox(g_inst, MAKEINTRESOURCE(DLG_ADVANCED), g_frame, (DLGPROC)adv_proc);
    adv_dlg = g_adv = NULL;
    EnableWindow(b, TRUE);
    SetFocus(b);
}
