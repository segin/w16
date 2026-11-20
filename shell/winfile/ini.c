/*
 * ini.c - w16 File Manager: WINFILE.INI.
 *
 * [Settings] as 3.1's File Manager writes it (WF1, WF2, WF4):
 *   Window=left,top,right,bottom, , ,show      the frame (normal place)
 *   dirN=left,top,right,bottom,iconx,icony,show,view,sort,attribs,split,path
 *       a directory window, in MDI client coordinates; the icon's place
 *       -1,-1 when it has none; view, sort and attribs as in winfile.h;
 *       the path with its pattern. dir1 is the bottom window of the MDI
 *       Z order, dirN the top; they are written top first and made again
 *       dir1 first, so the order comes back.
 *   StatusBar, MinOnRun, ConfirmDelete, ConfirmSubDel, ConfirmReplace,
 *   ConfirmMouse, ConfirmFormat: 0 or 1, written when changed;
 *   Face, Size, LowerCase (bit 0 lower case, bit 2 italic), FaceWeight:
 *   the font, written when it is chosen.
 * Window= and the dirN lines are written when File Manager ends with
 * Save Settings on Exit checked.
 */
#include "winfile.h"

static char ini[] = INI_FILE;
static char sec[] = INI_SETTINGS;

static char face[LF_FACESIZE];
static int fsize = 8, fweight = FW_NORMAL, fflags = 1;

static int get_int(LPCSTR key, int def)
{
    return GetPrivateProfileInt(sec, key, def, ini);
}

void ini_write_int(LPCSTR key, int v)
{
    char t[8];
    wsprintf(t, "%d", v);
    WritePrivateProfileString(sec, key, t, ini);
}

void ini_write_str(LPCSTR key, LPCSTR v)
{
    WritePrivateProfileString(sec, key, v, ini);
}

void ini_load(void)
{
    g_statusbar = get_int("StatusBar", 1);
    g_minonrun = get_int("MinOnRun", 0);
    g_saveonexit = get_int("Save Settings", 1);
    g_confdel = get_int("ConfirmDelete", 1);
    g_confsubdel = get_int("ConfirmSubDel", 1);
    g_confreplace = get_int("ConfirmReplace", 1);
    g_confmouse = get_int("ConfirmMouse", 1);
    g_confdisk = get_int("ConfirmFormat", 1);
    GetPrivateProfileString(sec, "Face", "MS Sans Serif", face, sizeof face, ini);
    fsize = get_int("Size", 8);
    fflags = get_int("LowerCase", 1);
    fweight = get_int("FaceWeight", FW_NORMAL);
    g_lower = (fflags & 1) != 0;
}

/* the list font, from Face, Size, FaceWeight and LowerCase's italic bit */
void ini_font(LOGFONT FAR *lf)
{
    HDC dc = GetDC(NULL);
    LPBYTE b = (LPBYTE)lf;
    int i;
    for (i = 0; i < (int)sizeof *lf; i++)
        b[i] = 0;
    lf->lfHeight = -MulDiv(fsize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);
    lf->lfWeight = fweight;
    lf->lfItalic = (BYTE)((fflags & 4) != 0);
    lf->lfCharSet = ANSI_CHARSET;
    lf->lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    s_cpy(lf->lfFaceName, face, sizeof lf->lfFaceName);
}

/* "a,b,c,..." into numbers; blanks give def */
static LPCSTR next_int(LPCSTR p, int *v, int def)
{
    while (*p == ' ')
        p++;
    if ((*p >= '0' && *p <= '9') || *p == '-')
        *v = s_atoi(p);
    else
        *v = def;
    while (*p && *p != ',')
        p++;
    if (*p == ',')
        p++;
    return p;
}

/* the frame's place from Window=; FALSE if there is none */
BOOL ini_frame(RECT FAR *r, int *show)
{
    char t[64];
    LPCSTR p = t;
    int x, y, cx, cy, dummy;
    if (!GetPrivateProfileString(sec, "Window", "", t, sizeof t, ini))
        return FALSE;
    p = next_int(p, &x, 0);
    p = next_int(p, &y, 0);
    p = next_int(p, &cx, 0);
    p = next_int(p, &cy, 0);
    p = next_int(p, &dummy, 0);
    p = next_int(p, &dummy, 0);
    p = next_int(p, show, SW_SHOWNORMAL);
    SetRect(r, x, y, cx, cy);
    return TRUE;
}

/* make the windows dir1, dir2... describe; one for the current
   directory when there are none */
void ini_restore_windows(int cmdshow)
{
    char key[8], t[PATHLEN + 64];
    int n, made = 0;
    (void)cmdshow;
    for (n = 1;; n++) {
        LPCSTR p;
        RECT r;
        POINT ic;
        int show, view, sort, attribs, split, k;
        char path[PATHLEN], spec[PATHLEN];
        wsprintf(key, "dir%d", n);
        if (!GetPrivateProfileString(sec, key, "", t, sizeof t, ini))
            break;
        p = t;
        p = next_int(p, (int *)&r.left, 0);
        p = next_int(p, (int *)&r.top, 0);
        p = next_int(p, (int *)&r.right, 0);
        p = next_int(p, (int *)&r.bottom, 0);
        p = next_int(p, (int *)&ic.x, -1);
        p = next_int(p, (int *)&ic.y, -1);
        p = next_int(p, &show, SW_SHOWNORMAL);
        p = next_int(p, &view, 0);
        p = next_int(p, &sort, SORT_NAME);
        p = next_int(p, &attribs, ATTR_DEFAULT);
        p = next_int(p, &split, 250);
        while (*p == ' ')
            p++;
        if (!p[0] || p[1] != ':')
            continue;
        /* the pattern is the last component */
        s_cpy(path, p, sizeof path);
        k = s_len(path);
        while (k > 0 && path[k - 1] != '\\')
            k--;
        s_cpy(spec, path + k, sizeof spec);
        if (!spec[0])
            s_cpy(spec, "*.*", sizeof spec);
        if (k > 3)
            k--;
        path[k] = 0;
        s_upper(path);
        if (sort < SORT_NAME || sort > SORT_DATE)
            sort = SORT_NAME;
        if (treewin_create(path, spec, (WORD)view, (WORD)sort, (WORD)attribs, split, &r,
                           show, &ic))
            made++;
    }
    if (!made) {
        char path[PATHLEN];
        int drv = dos_getdrive();
        path[0] = (char)('A' + drv);
        path[1] = ':';
        path[2] = '\\';
        if (dos_getcwd(drv + 1, path + 3) != 0)
            path[3] = 0;
        treewin_create(path, "*.*", VIEW_NAMEONLY, SORT_NAME, ATTR_DEFAULT, 250, NULL,
                       SW_SHOWNORMAL, NULL);
    }
}

void ini_save(void)
{
    WINDOWPLACEMENT wp;
    char t[PATHLEN + 80], key[8];
    HWND w;
    int n = 0, k;
    wp.length = sizeof wp;
    GetWindowPlacement(g_frame, &wp);
    wsprintf(t, "%d,%d,%d,%d, , ,%d", wp.rcNormalPosition.left, wp.rcNormalPosition.top,
             wp.rcNormalPosition.right, wp.rcNormalPosition.bottom, wp.showCmd);
    ini_write_str("Window", t);
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        if (dirwin_of(w) && !GetWindow(w, GW_OWNER))
            n++;
    k = n;
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        LPDIRWIN d = dirwin_of(w);
        char path[PATHLEN];
        if (!d || GetWindow(w, GW_OWNER))
            continue;
        wp.length = sizeof wp;
        GetWindowPlacement(w, &wp);
        s_cpy(path, d->path, sizeof path);
        path_add(path, d->spec, sizeof path);
        wsprintf(t, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s",
                 wp.rcNormalPosition.left, wp.rcNormalPosition.top,
                 wp.rcNormalPosition.right, wp.rcNormalPosition.bottom,
                 wp.ptMinPosition.x, wp.ptMinPosition.y,
                 wp.showCmd, d->view, d->sort, d->attribs, d->split, (LPSTR)path);
        wsprintf(key, "dir%d", k--);
        ini_write_str(key, t);
    }
    /* windows that are gone */
    for (k = n + 1;; k++) {
        wsprintf(key, "dir%d", k);
        if (!GetPrivateProfileString(sec, key, "", t, 2, INI_FILE))
            break;
        WritePrivateProfileString(sec, key, NULL, INI_FILE);
    }
}
