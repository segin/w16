/*
 * compat2.c - replacements for calls Windows 2.x lacks (W16_WIN2 build of
 * Clock; see compat2.h). Uses only calls Windows/386 2.11 has.
 */
#include "clock.h"

#ifdef W16_WIN2

/* ---- private profiles ----
 * Enough of GetPrivateProfileString and WritePrivateProfileString for
 * CLOCK.INI (a few hundred bytes): the file is read whole into a buffer;
 * [section] and key names compare without case; a new key goes at the
 * end of its section, a new section at the end of the file, each line
 * ending in CR LF, as Windows 3.1 writes them. The file is found in the
 * Windows directory: where OpenFile finds WIN.INI. */

#define INIMAX 4096
static char ini[INIMAX + 1];

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static BOOL same_n(const char FAR *a, const char FAR *b, int n)
{
    while (n--)
        if (up(*a++) != up(*b++))
            return FALSE;
    return TRUE;
}

static void ini_path(LPCSTR file, char *path, int size)
{
    OFSTRUCT of;
    int n;
    const char FAR *c;
    for (c = file; *c; c++)
        if (*c == '\\' || *c == ':') {
            s_cpy(path, file, size);
            return;
        }
    path[0] = 0;
    if (OpenFile("WIN.INI", &of, OF_EXIST) != HFILE_ERROR) {
        s_cpy(path, of.szPathName, size);
        for (n = s_len(path); n > 0 && path[n - 1] != '\\'; n--)
            ;
        path[n] = 0;
    }
    s_cat(path, file, size);
}

static int ini_read(LPCSTR file)
{
    char path[144];
    OFSTRUCT of;
    HFILE f;
    int n;
    ini_path(file, path, sizeof path);
    f = OpenFile(path, &of, OF_READ);
    if (f == HFILE_ERROR)
        return 0;
    n = _lread(f, ini, INIMAX);
    _lclose(f);
    if (n < 0)
        n = 0;
    ini[n] = 0;
    return n;
}

typedef struct {
    int sec_end;                        /* after the section's last line */
    int line, line_end;                 /* the key's line */
    int val, val_len;
    BOOL have_sec;
} spot;

static void find(int len, LPCSTR sec, LPCSTR key, spot *s)
{
    int i = 0, slen = s_len(sec), klen = s_len(key);
    BOOL in = FALSE;
    s->sec_end = -1;
    s->line = s->val = -1;
    s->have_sec = FALSE;
    while (i < len) {
        int b = i, e, k;
        while (i < len && ini[i] != '\n')
            i++;
        e = i;
        if (e > b && ini[e - 1] == '\r')
            e--;
        if (i < len)
            i++;
        for (k = b; k < e && (ini[k] == ' ' || ini[k] == '\t'); k++)
            ;
        if (k < e && ini[k] == '[') {
            int c = k + 1;
            in = e - c > slen && ini[c + slen] == ']' && same_n(ini + c, sec, slen);
            if (in) {
                s->have_sec = TRUE;
                s->sec_end = i;
            }
            continue;
        }
        if (!in)
            continue;
        if (k < e)
            s->sec_end = i;
        if (e - k > klen && s->line < 0) {
            int c = k + klen;
            while (c < e && (ini[c] == ' ' || ini[c] == '\t'))
                c++;
            if (c < e && ini[c] == '=' && same_n(ini + k, key, klen)) {
                c++;
                while (c < e && (ini[c] == ' ' || ini[c] == '\t'))
                    c++;
                while (e > c && (ini[e - 1] == ' ' || ini[e - 1] == '\t'))
                    e--;
                s->line = b;
                s->line_end = i;
                s->val = c;
                s->val_len = e - c;
            }
        }
    }
}

int FAR ck_GetPrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, int size,
                                   LPCSTR file)
{
    int len = ini_read(file);
    spot s;
    if (size <= 0)
        return 0;
    find(len, sec, key, &s);
    if (s.val >= 0) {
        int n = s.val_len < size - 1 ? s.val_len : size - 1;
        s_cpy(buf, ini + s.val, n + 1);
        return n;
    }
    s_cpy(buf, def ? def : "", size);
    return s_len(buf);
}

UINT FAR ck_GetPrivateProfileInt(LPCSTR sec, LPCSTR key, int def, LPCSTR file)
{
    char buf[16];
    const char FAR *p = buf;
    if (!ck_GetPrivateProfileString(sec, key, "", buf, sizeof buf, file) ||
        !(buf[0] >= '0' && buf[0] <= '9'))
        return def;
    return s_atoi(&p);
}

static char out[INIMAX + 256];

static void put(int *n, const char FAR *s, int len)
{
    while (len-- > 0 && *n < (int)sizeof out)
        out[(*n)++] = *s++;
}

BOOL FAR ck_WritePrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR value, LPCSTR file)
{
    char path[144];
    int len = ini_read(file), n = 0, cut, resume;
    spot s;
    OFSTRUCT of;
    HFILE f;
    find(len, sec, key, &s);
    if (s.line >= 0) {
        cut = s.line;
        resume = s.line_end;
    } else if (s.have_sec) {
        cut = resume = s.sec_end;
    } else {
        cut = resume = len;
    }
    put(&n, ini, cut);
    if (!s.have_sec && value) {
        if (len && ini[len - 1] != '\n')
            put(&n, "\r\n", 2);
        if (len)
            put(&n, "\r\n", 2);
        put(&n, "[", 1);
        put(&n, sec, s_len(sec));
        put(&n, "]\r\n", 3);
    }
    if (value) {
        put(&n, key, s_len(key));
        put(&n, "=", 1);
        put(&n, value, s_len(value));
        put(&n, "\r\n", 2);
    }
    put(&n, ini + resume, len - resume);
    ini_path(file, path, sizeof path);
    f = OpenFile(path, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return FALSE;
    _lwrite(f, out, n);
    _lclose(f);
    return TRUE;
}

/* ---- menus: built from our menu resource ----
 * Windows 2.x cannot load the 3.x menu resource format wrc writes, so the
 * menu is rebuilt with CreateMenu and ChangeMenu from the same resource
 * (as Program Manager's 2.x build does). */

static const BYTE FAR *build_items(HMENU m, const BYTE FAR *p)
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
            p = build_items(sub, p);
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

HMENU ck_load_menu(HINSTANCE inst, LPCSTR name)
{
    HRSRC r = FindResource(inst, name, RT_MENU);
    HGLOBAL h = r ? LoadResource(inst, r) : NULL;
    const BYTE FAR *p = h ? (const BYTE FAR *)LockResource(h) : NULL;
    HMENU m = NULL;
    if (p) {
        m = CreateMenu();
        build_items(m, p + 4 + (p[2] | (p[3] << 8)));
        UnlockResource(h);
    }
    if (h)
        FreeResource(h);
    return m;
}

/* ---- the restored window rectangle (no GetWindowPlacement) ---- */

static RECT restored;
static BOOL have_restored;

void ck_track_rect(HWND w)
{
    if (!IsIconic(w) && !IsZoomed(w)) {
        GetWindowRect(w, &restored);
        have_restored = TRUE;
    }
}

void ck_restored_rect(HWND w, RECT *rc)
{
    ck_track_rect(w);
    if (have_restored)
        *rc = restored;
    else
        GetWindowRect(w, rc);
}

/* ---- colours ---- */

#undef GetSysColor
DWORD ck_GetSysColor(int i)
{
    switch (i) {
    case COLOR_BTNFACE:
        return RGB(192, 192, 192);
    case COLOR_BTNSHADOW:
        return RGB(128, 128, 128);
    case COLOR_BTNTEXT:
        return RGB(0, 0, 0);
    case COLOR_BTNHIGHLIGHT:
        return RGB(255, 255, 255);
    }
    return GetSysColor(i);
}

#undef ExtTextOut
BOOL ck_ExtTextOut(HDC dc, int x, int y, UINT opt, const RECT FAR *rc, LPCSTR s, UINT n,
                   int FAR *dx)
{
    if (opt & ETO_OPAQUE) {
        int mode = SetBkMode(dc, TRANSPARENT);
        BOOL r;
        FillRect(dc, rc, g_brface);
        r = ExtTextOut(dc, x, y, opt & ~ETO_OPAQUE, rc, s, n, dx);
        SetBkMode(dc, mode);
        return r;
    }
    return ExtTextOut(dc, x, y, opt, rc, s, n, dx);
}

/* ---- About (no ShellAbout) ---- */

BOOL FAR PASCAL __export about_proc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(d, TRUE);
        return TRUE;
    }
    return FALSE;
}

void ck_about(HWND w)
{
    FARPROC fp = MakeProcInstance((FARPROC)about_proc, g_hinst);
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_ABOUT2), w, (DLGPROC)fp);
    FreeProcInstance(fp);
}

#endif
