/*
 * util.c - w16 File Manager: strings, numbers, dates, paths, file kinds,
 * and the arena that holds the lists' records.
 */
#include "winfile.h"

char g_thousand = ',';
static char date_sep = '/', time_sep = ':';
static int date_order;                  /* [intl] iDate: 0 MDY, 1 DMY, 2 YMD */
static int time_24, time_lzero;
static char s1159[8], s2359[8];
static char programs[80];               /* [windows] Programs= */

/* ---- strings ---- */

int s_len(LPCSTR s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void s_cpy(LPSTR d, LPCSTR s, int n)
{
    while (n > 1 && *s) {
        *d++ = *s++;
        n--;
    }
    if (n > 0)
        *d = 0;
}

void s_cat(LPSTR d, LPCSTR s, int n)
{
    int k = s_len(d);
    s_cpy(d + k, s, n - k);
}

int s_icmp(LPCSTR a, LPCSTR b)
{
    return lstrcmpi(a, b);
}

void s_lower(LPSTR s)
{
    AnsiLower(s);
}

void s_upper(LPSTR s)
{
    AnsiUpper(s);
}

int s_atoi(LPCSTR s)
{
    int v = 0, neg = 0;
    while (*s == ' ')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

LPSTR s_utoa(LPSTR buf, DWORD v)
{
    char t[12];
    int i = 0, k = 0;
    do {
        t[i++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    while (i)
        buf[k++] = t[--i];
    buf[k] = 0;
    return buf;
}

/* v with the [intl] thousands separator between groups of three */
LPSTR s_commas(LPSTR buf, DWORD v)
{
    char t[12];
    int n, i, k = 0;
    s_utoa(t, v);
    n = s_len(t);
    for (i = 0; i < n; i++) {
        buf[k++] = t[i];
        if (g_thousand && (n - i - 1) % 3 == 0 && i != n - 1)
            buf[k++] = g_thousand;
    }
    buf[k] = 0;
    return buf;
}

/* the extension of a file name, without the dot ("" if none) */
LPCSTR s_ext(LPCSTR name)
{
    LPCSTR dot = NULL;
    for (; *name; name++)
        if (*name == '.')
            dot = name;
    return dot ? dot + 1 : name;
}

int fmt(LPSTR out, int n, LPCSTR f, ...)
{
    char t[256];
    int k;
    k = wvsprintf(t, f, (LPSTR)(&f + 1));
    s_cpy(out, t, n);
    return k;
}

/* ---- dates and times as File Manager shows them ----
   The year is two characters, '0' + (year - 1900) / 10 and the units:
   3.1's File Manager shows 2026 as "<6" (WF2), and so does this one. */
void fmt_date(LPSTR out, WORD date)
{
    int y = 80 + (date >> 9), m = (date >> 5) & 15, d = date & 31;
    char ys[3];
    char sep[2];
    ys[0] = (char)('0' + y / 10);
    ys[1] = (char)('0' + y % 10);
    ys[2] = 0;
    sep[0] = date_sep;
    sep[1] = 0;
    switch (date_order) {
    case 1:
        wsprintf(out, "%d%s%d%s%s", d, (LPSTR)sep, m, (LPSTR)sep, (LPSTR)ys);
        break;
    case 2:
        wsprintf(out, "%s%s%d%s%d", (LPSTR)ys, (LPSTR)sep, m, (LPSTR)sep, d);
        break;
    default:
        wsprintf(out, "%d%s%d%s%s", m, (LPSTR)sep, d, (LPSTR)sep, (LPSTR)ys);
        break;
    }
}

void fmt_time(LPSTR out, WORD time)
{
    int h = time >> 11, m = (time >> 5) & 63, s = (time & 31) * 2;
    LPCSTR ampm = "";
    if (!time_24) {
        ampm = h >= 12 ? s2359 : s1159;
        h %= 12;
        if (h == 0)
            h = 12;
    }
    wsprintf(out, time_lzero ? "%02d%c%02d%c%02d%s" : "%d%c%02d%c%02d%s",
             h, time_sep, m, time_sep, s, ampm);
}

/* "rhsa" letters of the attributes that are set */
void fmt_attr(LPSTR out, BYTE attr)
{
    if (attr & ATTR_READONLY)
        *out++ = 'R';
    if (attr & ATTR_HIDDEN)
        *out++ = 'H';
    if (attr & ATTR_SYSTEM)
        *out++ = 'S';
    if (attr & ATTR_ARCHIVE)
        *out++ = 'A';
    *out = 0;
}

/* ---- paths ---- */

/* path + "\" + name; "C:\" + name for a root */
void path_add(LPSTR path, LPCSTR name, int n)
{
    int k = s_len(path);
    if (k && path[k - 1] != '\\')
        s_cat(path, "\\", n);
    s_cat(path, name, n);
}

/* drop the last component: "C:\A\B" -> "C:\A", "C:\A" -> "C:\" */
void path_strip(LPSTR path)
{
    int k = s_len(path), cut = -1, i;
    for (i = 0; i < k; i++)
        if (path[i] == '\\')
            cut = i;
    if (cut < 0)
        return;
    if (cut == 2)
        path[3] = 0;
    else
        path[cut] = 0;
}

/* ---- kinds of file ---- */

/* a program: its extension is in [windows] Programs= */
BOOL is_program(LPCSTR name)
{
    LPCSTR e = s_ext(name), p = programs;
    int n = s_len(e);
    if (!n)
        return FALSE;
    while (*p) {
        int k = 0;
        while (p[k] && p[k] != ' ')
            k++;
        if (k == n) {
            int i;
            for (i = 0; i < n; i++) {
                char a = e[i], b = p[i];
                if (a >= 'a' && a <= 'z')
                    a -= 32;
                if (b >= 'a' && b <= 'z')
                    b -= 32;
                if (a != b)
                    break;
            }
            if (i == n)
                return TRUE;
        }
        p += k;
        while (*p == ' ')
            p++;
    }
    return FALSE;
}

/* a document: its extension has a WIN.INI [Extensions] entry or a class
   in the registration database (WF01: .WAV and .MID, which only REG.DAT
   associates, show as documents) */
BOOL is_document(LPCSTR name)
{
    char t[40], key[8];
    LONG cb = sizeof t;
    LPCSTR e = s_ext(name);
    if (!*e)
        return FALSE;
    if (GetProfileString("Extensions", e, "", t, sizeof t) != 0)
        return TRUE;
    key[0] = '.';
    s_cpy(key + 1, e, sizeof key - 1);
    return RegQueryValue(HKEY_CLASSES_ROOT, key, t, &cb) == ERROR_SUCCESS && t[0];
}

/* fill with a system colour (USER's FillRect takes no colour index) */
void fill_sys(HDC dc, const RECT FAR *r, int color)
{
    HBRUSH b = CreateSolidBrush(GetSysColor(color));
    FillRect(dc, r, b);
    DeleteObject(b);
}

void util_init(void)
{
    char t[8];
    GetProfileString("intl", "sThousand", ",", t, sizeof t);
    g_thousand = t[0];
    GetProfileString("intl", "sDate", "/", t, sizeof t);
    date_sep = t[0] ? t[0] : '/';
    GetProfileString("intl", "sTime", ":", t, sizeof t);
    time_sep = t[0] ? t[0] : ':';
    date_order = GetProfileInt("intl", "iDate", 0);
    time_24 = GetProfileInt("intl", "iTime", 0);
    time_lzero = GetProfileInt("intl", "iTLZero", 0);
    GetProfileString("intl", "s1159", "AM", s1159, sizeof s1159);
    GetProfileString("intl", "s2359", "PM", s2359, sizeof s2359);
    GetProfileString("windows", "Programs", "com exe bat pif", programs, sizeof programs);
}

/* ---- the arena ---- */

#define ARENA_BLOCK 8192

typedef struct tagABLOCK {
    HGLOBAL next;
} ABLOCK;

/* Blocks are fixed (their far pointers stay valid); each starts with the
   handle of the one made before it. */
LPVOID arena_alloc(ARENA FAR *a, WORD n)
{
    LPBYTE p;
    n = (n + 1) & ~1;
    if (!a->first || a->used + n > a->size) {
        WORD size = n + sizeof(ABLOCK) > ARENA_BLOCK ? n + sizeof(ABLOCK) : ARENA_BLOCK;
        HGLOBAL h = GlobalAlloc(GMEM_FIXED, size);
        ABLOCK FAR *b;
        if (!h)
            return NULL;
        b = (ABLOCK FAR *)GlobalLock(h);
        b->next = a->first;
        a->first = h;
        a->used = sizeof(ABLOCK);
        a->size = size;
    }
    p = (LPBYTE)GlobalLock(a->first) + a->used;
    GlobalUnlock(a->first);
    a->used += n;
    return p;
}

void arena_free(ARENA FAR *a)
{
    HGLOBAL h = a->first;
    while (h) {
        ABLOCK FAR *b = (ABLOCK FAR *)GlobalLock(h);
        HGLOBAL next = b->next;
        GlobalUnlock(h);
        GlobalUnlock(h);
        GlobalFree(h);
        h = next;
    }
    a->first = NULL;
    a->used = a->size = 0;
}
