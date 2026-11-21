/*
 * ssstars.c - SSSTARS.SCR, the Starfield Simulation screen saver, on
 * w16's screen saver library (scrnsave.c).
 *
 * Observed on Windows 3.1's (black-box: screenshots, SNAP.EXE dumps and
 * the CONTROL.INI it writes; tests/scripts/ss, ARCHITECTURE.md 15.8):
 * - The animation: a black screen; stars, white squares, fly out of the
 *   middle. They start together, all at the far depth and spread evenly
 *   over the screen, so for the first moments they grow and spread as
 *   one; a star that leaves the screen comes back at the far depth at a
 *   random place. Positions scale as depth falls at a steady rate (the
 *   star at 1/z): at the default warp speed about 5% of the far depth a
 *   frame of 150 ms. A star is one pixel square while beyond 77% of the
 *   far depth, then 2, 3 (below 52%) and 4 (below 26%). Every star is
 *   white on a 16-colour display.
 * - Setup: "Starfield Simulation Setup". Warp Speed: a scroll bar 0
 *   (Slow) to 10 (Fast), a line or a page a step; Starfield Density: the
 *   number of stars, 10-200, an edit with a spin control (OK and the
 *   spin control disabled while the number is out of range); Password Options: Password
 *   Protected and Set Password... (enabled with the box). OK writes
 *   CONTROL.INI [Screen Saver.Stars] Density, WarpSpeed, PWProtected, in
 *   that order; Cancel writes nothing. Defaults: 25, 5, 0. Help: CONTROL.HLP
 *   topic 9003, from the button and F1.
 * UNVERIFIED: the frame rate and how warp speeds other than 5 map to
 * speed (w16: proportional to the setting + 1); the size steps are fitted
 * to screenshots and may be off by a frame.
 */
#include "../scrnsave/scrnsave.h"
#include "ssstars.h"

#define ZFAR    4096L           /* the far depth */
#define TICK    50              /* ms a frame (UNVERIFIED) */

typedef struct {
    int x0, y0;                 /* position at the far depth, from the middle */
    long z;                     /* depth, 1..ZFAR */
    int sx, sy, size;           /* where it is drawn; size 0: not drawn */
} STAR;

static HGLOBAL hstars;
static STAR FAR *stars;
static int nstars, warp, cx, cy;
static UINT timer;

static void place(STAR FAR *s)
{
    s->x0 = (int)((long)ss_rand() * cx / 32768L) - cx / 2;
    s->y0 = (int)((long)ss_rand() * cy / 32768L) - cy / 2;
    s->z = ZFAR;
}

/* the size for a depth: 1 beyond 77% of the far depth, 2 beyond 52%,
   3 beyond 26%, else 4 (observed) */
static int size_of(long z)
{
    int s = 1 + (int)(((ZFAR - z) * 100 / ZFAR + 2) / 25);
    return s > 4 ? 4 : s;
}

static void frame(HWND w)
{
    HDC dc = GetDC(w);
    long dz = ZFAR * (warp + 1) / 370;
    int i;
    if (dz < 1)
        dz = 1;
    for (i = 0; i < nstars; i++) {
        STAR FAR *s = &stars[i];
        int x, y, size;
        s->z -= dz;
        if (s->z < 1)
            place(s);
        x = cx / 2 + (int)(s->x0 * ZFAR / s->z);
        y = cy / 2 + (int)(s->y0 * ZFAR / s->z);
        size = size_of(s->z);
        if (x < -size || x >= cx || y < -size || y >= cy) {
            place(s);
            x = cx / 2 + s->x0;
            y = cy / 2 + s->y0;
            size = 1;
        }
        if (s->size && (s->sx != x || s->sy != y || s->size != size))
            PatBlt(dc, s->sx, s->sy, s->size, s->size, BLACKNESS);
        PatBlt(dc, x, y, size, size, WHITENESS);
        s->sx = x;
        s->sy = y;
        s->size = size;
    }
    ReleaseDC(w, dc);
}

LRESULT FAR PASCAL __export ScreenSaverProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i;
        warp = ss_get_int(KEY_WARP, DEF_WARP);
        nstars = ss_get_int(KEY_DENSITY, DEF_DENSITY);
        if (warp < WARP_MIN) warp = WARP_MIN;
        if (warp > WARP_MAX) warp = WARP_MAX;
        if (nstars < DENSITY_MIN) nstars = DENSITY_MIN;
        if (nstars > DENSITY_MAX) nstars = DENSITY_MAX;
        cx = GetSystemMetrics(SM_CXSCREEN);
        cy = GetSystemMetrics(SM_CYSCREEN);
        hstars = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)nstars * sizeof(STAR));
        stars = hstars ? (STAR FAR *)GlobalLock(hstars) : NULL;
        if (!stars)
            nstars = 0;
        for (i = 0; i < nstars; i++)
            place(&stars[i]);
        timer = SetTimer(w, 1, TICK, NULL);
        break;
    }
    case WM_TIMER:
        if (nstars)
            frame(w);
        return 0;
    case WM_ERASEBKGND: {
        RECT r;
        int i;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, GetStockObject(BLACK_BRUSH));
        for (i = 0; i < nstars; i++)
            stars[i].size = 0;
        return 1;
    }
    case WM_DESTROY:
        if (timer)
            KillTimer(w, 1);
        if (hstars) {
            GlobalUnlock(hstars);
            GlobalFree(hstars);
        }
        hstars = NULL;
        nstars = 0;
        break;
    }
    return DefScreenSaverProc(w, msg, wp, lp);
}

/* ---- Setup ---- */

static int density_of(HWND dlg, BOOL *ok)
{
    int v = (int)GetDlgItemInt(dlg, IDC_DENSITY, ok, FALSE);
    if (v < DENSITY_MIN || v > DENSITY_MAX)
        *ok = FALSE;
    return v;
}

BOOL FAR PASCAL __export ScreenSaverConfigureDialog(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static int speed;
    if (msg == MyHelpMessage && msg) {
        WinHelp(dlg, HELP_FILE, HELP_CONTEXT, HELP_STARS);
        return TRUE;
    }
    switch (msg) {
    case WM_INITDIALOG: {
        HWND sb = GetDlgItem(dlg, IDC_WARP);
        BOOL pw = ss_get_int(KEY_PROTECTED, 0) != 0;
        int d = ss_get_int(KEY_DENSITY, DEF_DENSITY);
        speed = ss_get_int(KEY_WARP, DEF_WARP);
        if (speed < WARP_MIN) speed = WARP_MIN;
        if (speed > WARP_MAX) speed = WARP_MAX;
        SetScrollRange(sb, SB_CTL, WARP_MIN, WARP_MAX, FALSE);
        SetScrollPos(sb, SB_CTL, speed, TRUE);
        SendDlgItemMessage(dlg, IDC_DENSITY, EM_LIMITTEXT, 3, 0);
        SetDlgItemInt(dlg, IDC_DENSITY, d, FALSE);
        CheckDlgButton(dlg, IDC_PROTECTED, pw);
        EnableWindow(GetDlgItem(dlg, IDC_SETPASSWORD), pw);
        return TRUE;
    }
    case WM_HSCROLL: {
        int v = speed;
        switch (wp) {
        case SB_LINEUP:
        case SB_PAGEUP:        v--; break;
        case SB_LINEDOWN:
        case SB_PAGEDOWN:      v++; break;
        case SB_TOP:           v = WARP_MIN; break;
        case SB_BOTTOM:        v = WARP_MAX; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: v = (int)LOWORD(lp); break;
        default: return TRUE;
        }
        if (v < WARP_MIN) v = WARP_MIN;
        if (v > WARP_MAX) v = WARP_MAX;
        speed = v;
        SetScrollPos((HWND)HIWORD(lp), SB_CTL, speed, TRUE);
        return TRUE;
    }
    case WM_VSCROLL: {          /* the spin control */
        BOOL ok;
        int v = density_of(dlg, &ok);
        if (wp == SB_LINEUP) v++;
        else if (wp == SB_LINEDOWN) v--;
        else return TRUE;
        if (v < DENSITY_MIN) v = DENSITY_MIN;
        if (v > DENSITY_MAX) v = DENSITY_MAX;
        SetDlgItemInt(dlg, IDC_DENSITY, v, FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_DENSITY:
            if (HIWORD(lp) == EN_CHANGE) {
                BOOL ok;
                density_of(dlg, &ok);
                EnableWindow(GetDlgItem(dlg, IDOK), ok);
                EnableWindow(GetDlgItem(dlg, IDC_DENSITYA), ok);
            }
            return TRUE;
        case IDC_PROTECTED: {
            BOOL pw = !IsDlgButtonChecked(dlg, IDC_PROTECTED);
            CheckDlgButton(dlg, IDC_PROTECTED, pw);
            EnableWindow(GetDlgItem(dlg, IDC_SETPASSWORD), pw);
            return TRUE;
        }
        case IDC_SETPASSWORD:
            ss_set_password(dlg);
            return TRUE;
        case IDC_HELP:
            WinHelp(dlg, HELP_FILE, HELP_CONTEXT, HELP_STARS);
            return TRUE;
        case IDOK: {
            BOOL ok;
            int d = density_of(dlg, &ok);
            if (!ok)
                return TRUE;
            ss_put_int(KEY_DENSITY, d);
            ss_put_int(KEY_WARP, speed);
            ss_put_int(KEY_PROTECTED, IsDlgButtonChecked(dlg, IDC_PROTECTED) != 0);
            WinHelp(dlg, HELP_FILE, HELP_QUIT, 0);
            EndDialog(dlg, TRUE);
            return TRUE;
        }
        case IDCANCEL:
            WinHelp(dlg, HELP_FILE, HELP_QUIT, 0);
            EndDialog(dlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL WINAPI RegisterDialogClasses(HINSTANCE h)
{
    ss_register_arrow(h);
    return TRUE;
}
