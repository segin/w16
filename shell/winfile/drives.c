/*
 * drives.c - w16 File Manager: the drive bar (class WFS_Drives, child 1 of
 * a directory window, as wide as its client).
 *
 * Observed (WF1, VGA, MS Sans Serif 8): a drive every 44 pixels from x 4,
 * its picture (27 x 14) at y 4 and its letter, lower case, 4 pixels after
 * it; the current drive in a 1-pixel highlight-coloured frame from a pixel
 * after the drive's start, 42 x 20 at y 1; to the right of the last drive,
 * centred in the rest of the bar, the current drive's letter and volume
 * label ("C: [C_DRIVE]"). Ctrl+letter, a click, and the arrow keys with
 * the bar focused choose a drive.
 */
#include "winfile.h"

int g_ndrives;
BYTE g_drivelist[26];                   /* drive numbers, 0 = A: */
BYTE g_drivepic[26];                    /* BM_DRV* of each */

static char cls_drives[] = "WFS_Drives";

static int g_dxdrive = 44;

int drive_index(int drive)
{
    int i;
    for (i = 0; i < g_ndrives; i++)
        if (g_drivelist[i] == drive)
            return i;
    return -1;
}

/* the drives Windows knows (GetDriveType), A to Z */
void drives_scan(void)
{
    int d;
    g_ndrives = 0;
    for (d = 0; d < 26; d++) {
        UINT t = GetDriveType(d);
        BYTE pic;
        if (t == 0 || t == 1)
            continue;
        switch (t) {
        case DRIVE_REMOVABLE:
            pic = BM_DRVFLOPPY;
            break;
        case DRIVE_REMOTE:
            pic = BM_DRVNET;
            break;
        default:
            pic = BM_DRVHARD;
            break;
        }
        g_drivelist[g_ndrives] = (BYTE)d;
        g_drivepic[g_ndrives] = pic;
        g_ndrives++;
    }
}

int drives_height(void)
{
    return (g_dytext > CY_DRIVEBM ? g_dytext : CY_DRIVEBM) + 9;
}

static void drive_rect(int i, RECT FAR *r)
{
    r->left = i * g_dxdrive + 1;
    r->top = 1;
    r->right = r->left + g_dxdrive - 2;
    r->bottom = r->top + drives_height() - 3;
}

/* the volume label of a drive, "" if none; FALSE if it cannot be read */
static BOOL volume_label(int drive, LPSTR out, int n)
{
    FINDBUF f;
    char spec[8];
    out[0] = 0;
    spec[0] = (char)('A' + drive);
    s_cpy(spec + 1, ":\\*.*", sizeof spec - 1);
    if (dos_findfirst(spec, ATTR_VOLUME, &f) == 0 && (f.attr & ATTR_VOLUME)) {
        int i, k = 0;
        for (i = 0; f.name[i] && k < n - 1; i++)
            if (f.name[i] != '.')           /* the label has no dot */
                out[k++] = f.name[i];
        out[k] = 0;
    }
    return TRUE;
}

static void paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    LPDIRWIN d = dirwin_of(GetParent(w));
    RECT rc, r;
    HFONT of = SelectObject(dc, g_font);
    HBRUSH frame;
    int i;
    char label[24], t[48];
    GetClientRect(w, &rc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    for (i = 0; i < g_ndrives; i++) {
        char letter[2];
        int x = i * g_dxdrive + 4;
        bm_draw_drive(dc, x, 4, g_drivepic[i]);
        letter[0] = (char)('a' + g_drivelist[i]);
        letter[1] = 0;
        /* the case the font had when the window was made (WF05: upper
           case from the start; WF07: still lower after Font) */
        if (d && !d->lowerdrives)
            s_upper(letter);
        TextOut(dc, x + CX_DRIVEBM + 2, 4, letter, 1);
        if (d && GetFocus() == w && i == d->drivefocus && g_drivelist[i] != d->drive) {
            /* the bar's keyboard place on another drive: a dotted
               rectangle round its cell (WF06 S06) */
            drive_rect(i, &r);
            InflateRect(&r, 1, 1);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            DrawFocusRect(dc, &r);
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        }
        if (d && g_drivelist[i] == d->drive) {
            drive_rect(i, &r);
            if (GetFocus() == w && i == d->drivefocus) {
                /* focused: the whole cell filled, the focus rectangle on
                   its edge, the letter in the highlight text colour (WF5
                   E03) */
                InflateRect(&r, 1, 1);
                fill_sys(dc, &r, COLOR_HIGHLIGHT);
                bm_draw_drive(dc, x, 4, g_drivepic[i]);
                SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                TextOut(dc, x + CX_DRIVEBM + 2, 4, letter, 1);
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                DrawFocusRect(dc, &r);
                SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            } else {
                frame = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                FrameRect(dc, &r, frame);
                DeleteObject(frame);
            }
        }
    }
    if (d) {
        volume_label(d->drive, label, sizeof label);
        if (label[0])
            fmt(t, sizeof t, load_str(IDS_DRIVELABEL), 'A' + d->drive, (LPSTR)label);
        else
            fmt(t, sizeof t, load_str(IDS_DRIVENOLABEL), 'A' + d->drive);
        /* centred between the last drive and the right edge, 6 down */
        r = rc;
        r.left = g_ndrives * g_dxdrive;
        r.top = 6;
        DrawText(dc, t, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, of);
    EndPaint(w, &ps);
}

static int hit(HWND w, int x)
{
    int i = x / g_dxdrive;
    (void)w;
    return i < g_ndrives ? i : -1;
}

LRESULT CALLBACK _export DrivesWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d;
    int i;
    switch (msg) {
    case WM_PAINT:
        paint(w);
        return 0;
    case WM_LBUTTONDOWN:
        i = hit(w, LOWORD(lp));
        if (i >= 0 && (d = dirwin_of(GetParent(w))) != NULL) {
            d->drivefocus = i;
            treewin_set_drive(d, g_drivelist[i]);
        }
        return 0;
    case WM_SETFOCUS:
        d = dirwin_of(GetParent(w));
        if (d) {
            d->drivefocus = drive_index(d->drive);
            d->lastpane = ID_DRIVES;
        }
        InvalidateRect(w, NULL, TRUE);
        break;
    case WM_KILLFOCUS:
        InvalidateRect(w, NULL, TRUE);
        break;
    case 0x022B:                        /* WM_QUERYDROPOBJECT */
        return wp ? 0 : dnd_query(w, lp);
    case 0x022A:                        /* WM_DROPOBJECT */
        return dnd_drop(w, lp);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_KEYDOWN:
        d = dirwin_of(GetParent(w));
        if (!d)
            break;
        i = drive_index(d->drive);
        if (wp == VK_TAB) {
            /* tree, list, drive bar, tree... (WF5) */
            HWND next;
            if (GetKeyState(VK_SHIFT) < 0)
                next = d->panes == PANES_TREE ? d->treelb : d->dirlb;
            else
                next = d->panes == PANES_DIR ? d->dirlb : d->treelb;
            SetFocus(next);
            return 0;
        }
        /* the arrows move the keyboard place; Enter or Space takes its
           drive (WF06 S06, S07) */
        i = d->drivefocus;
        if (wp == VK_LEFT && i > 0)
            d->drivefocus = i - 1;
        else if (wp == VK_RIGHT && i + 1 < g_ndrives)
            d->drivefocus = i + 1;
        else if ((wp == VK_RETURN || wp == VK_SPACE) && i >= 0 && i < g_ndrives)
            treewin_set_drive(d, g_drivelist[i]);
        else
            break;
        InvalidateRect(w, NULL, TRUE);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL drives_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = DrivesWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_drives;
    return RegisterClass(&wc);
}
