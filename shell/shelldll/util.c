/*
 * util.c - w16 SHELL.DLL: helpers (no C runtime).
 */
#include "shell.h"

void sh_move(void __far *dst, const void __far *src, WORD n)
{
    BYTE __far *d = (BYTE __far *)dst;
    const BYTE __far *s = (const BYTE __far *)src;
    if (d < s || SELECTOROF(d) != SELECTOROF(s)) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
}

void sh_fill(void __far *dst, BYTE v, WORD n)
{
    BYTE __far *d = (BYTE __far *)dst;
    while (n--)
        *d++ = v;
}

/* AnsiUpper of one character (USER.431 takes a character in the low word) */
BYTE sh_upper(BYTE c)
{
    return (BYTE)(DWORD)AnsiUpper((LPSTR)MAKELP(0, c));
}

int sh_same_ci(const char __far *a, const char __far *b, WORD n)
{
    while (n--)
        if (sh_upper(*a++) != sh_upper(*b++))
            return 0;
    return 1;
}

int sh_same(const char __far *a, const char __far *b, WORD n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

WORD sh_len(const char __far *s)
{
    WORD n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

/* copy at most cb - 1 characters and a NUL (nothing when cb is 0) */
void sh_copyn(char __far *dst, const char __far *src, WORD cb)
{
    if (!cb)
        return;
    while (--cb && *src)
        *dst++ = *src++;
    *dst = 0;
}

HGLOBAL sh_alloc(DWORD size)
{
    return GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size ? size : 1);
}

void __far *sh_lock(HGLOBAL h)
{
    return h ? GlobalLock(h) : NULL;
}

/* a copy of s in its own block (*ph; GlobalFree it) */
char __far *sh_strdup(const char __far *s, HGLOBAL *ph)
{
    WORD n = sh_len(s);
    char __far *p;
    *ph = sh_alloc(n + 1);
    p = (char __far *)sh_lock(*ph);
    if (p)
        sh_move(p, s, n + 1);
    return p;
}

/* v with a comma every three digits, as the About box shows memory
   ("15,079"; UNVERIFIED: whether 3.1 uses WIN.INI [intl] sThousand) */
void sh_commas(char __far *out, DWORD v)
{
    char t[16];
    int n = 0, i, k = 0;
    do {
        if (n && n % 3 == 0)
            t[k++] = ',';
        t[k++] = (char)('0' + (int)(v % 10));
        v /= 10;
        n++;
    } while (v);
    for (i = 0; i < k; i++)
        out[i] = t[k - 1 - i];
    out[k] = 0;
}

/* the extension of path's last component (after its last '.'), or NULL */
const char __far *sh_ext(const char __far *path)
{
    const char __far *e = NULL;
    for (; *path; path++) {
        if (*path == '.')
            e = path + 1;
        else if (*path == '\\' || *path == '/' || *path == ':')
            e = NULL;
    }
    return e;
}

/* Is path a program by its extension: WIN.INI [windows] Programs=
   (default "com exe bat pif"), as Program Manager and File Manager use it
   (Windows 3.1 User's Guide; probes SH4, SH6: .PIF, .BAT, .COM and .EXE
   files are their own executables). */
int sh_is_program(const char __far *path)
{
    static char progs[128];
    const char __far *e = sh_ext(path);
    char *p;
    WORD n;
    if (!e || !*e)
        return 0;
    n = sh_len(e);
    GetProfileString("windows", "Programs", "com exe bat pif", progs, sizeof progs);
    for (p = progs; *p;) {
        WORD k = 0;
        while (*p == ' ')
            p++;
        while (p[k] && p[k] != ' ')
            k++;
        if (k == n && sh_same_ci(p, e, n))
            return 1;
        p += k;
    }
    return 0;
}

/* ---- a stack of words in global memory ---- */
int ws_push(WSTACK *s, WORD v)
{
    if (s->n == s->cap) {
        WORD cap = s->cap ? s->cap * 2 : 64;
        HGLOBAL h;
        if (cap < s->cap)               /* past 32K entries: 64K bytes */
            return 0;
        if (s->h) {
            GlobalUnlock(s->h);
            h = GlobalReAlloc(s->h, (DWORD)cap * 2, GMEM_MOVEABLE);
        } else {
            h = sh_alloc((DWORD)cap * 2);
        }
        if (!h) {
            if (s->h)
                s->p = (WORD __far *)GlobalLock(s->h);
            return 0;
        }
        s->h = h;
        s->cap = cap;
        s->p = (WORD __far *)GlobalLock(h);
    }
    s->p[s->n++] = v;
    return 1;
}

WORD ws_pop(WSTACK *s)
{
    return s->n ? s->p[--s->n] : 0;
}

void ws_free(WSTACK *s)
{
    if (s->h) {
        GlobalUnlock(s->h);
        GlobalFree(s->h);
    }
    s->h = NULL;
    s->p = NULL;
    s->n = s->cap = 0;
}
