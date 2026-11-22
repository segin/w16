/*
 * util.c - string helpers and message boxes (no C runtime).
 */
#include "progman.h"

/* ---- global memory blocks, addressed by locked far pointers ---- */

LPSTR mem_alloc(DWORD n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n ? n : 1);
    LPSTR p;
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    if (!p)
        GlobalFree(h);
    return p;
}

static HGLOBAL handle_of(LPSTR p)
{
    return (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
}

#ifdef W16_WIN2
LPSTR mem_realloc(LPSTR p, DWORD n)
{
    /* Windows 2.x runs in real mode: while the block is unlocked for
       GlobalReAlloc, a compaction may move it, so a failed call would
       leave the caller's pointer stale. Copy into a new block instead
       (every block of ours stays locked). */
    DWORD have, k;
    LPSTR q;
    if (!p)
        return mem_alloc(n);
    have = GlobalSize(handle_of(p));
    if ((q = mem_alloc(n)) == NULL)
        return NULL;
    for (k = 0; k < have && k < n; k++)
        q[(WORD)k] = p[(WORD)k];
    mem_free(p);
    return q;
}
#else
LPSTR mem_realloc(LPSTR p, DWORD n)
{
    HGLOBAL h, nh;
    if (!p)
        return mem_alloc(n);
    h = handle_of(p);
    GlobalUnlock(h);
    nh = GlobalReAlloc(h, n ? n : 1, GMEM_MOVEABLE | GMEM_ZEROINIT);
    if (!nh) {
        GlobalLock(h);                  /* p stays valid */
        return NULL;
    }
    return (LPSTR)GlobalLock(nh);
}
#endif

void mem_free(LPSTR p)
{
    HGLOBAL h;
    if (!p)
        return;
    h = handle_of(p);
    GlobalUnlock(h);
    GlobalFree(h);
}

LPSTR s_dup(const char FAR *s)
{
    LPSTR d = mem_alloc(s_len(s) + 1);
    if (d)
        s_cpy(d, s, s_len(s) + 1);
    return d;
}

/* Append s to the growable string *buf (which may start out NULL). */
BOOL s_append(LPSTR FAR *buf, const char FAR *s)
{
    /* lengths as WORDs: a string can pass 32K (a DDE group listing) */
    WORD have = *buf ? (WORD)s_len(*buf) : 0, add = (WORD)s_len(s), k;
    LPSTR n;
    if ((DWORD)have + add + 1 > 0xFFF0UL)
        return FALSE;                   /* one segment holds it */
    n = mem_realloc(*buf, (DWORD)have + add + 1);
    if (!n)
        return FALSE;
    for (k = 0; k <= add; k++)
        n[have + k] = s[k];
    *buf = n;
    return TRUE;
}

/* A private-profile string of any length (GetPrivateProfileString returns
   size - 2 when the buffer was too small); NULL if absent or empty. */
LPSTR profile_get(const char FAR *sec, const char FAR *key, const char FAR *file)
{
    WORD size = 128;
    for (;;) {
        LPSTR p = mem_alloc(size);
        int got;
        if (!p)
            return NULL;
        got = GetPrivateProfileString(sec, key, "", p, size, file);
        if (got == 0) {
            mem_free(p);
            return NULL;
        }
        if (got < (int)size - 2 || size >= 0x7FFF)
            return p;
        mem_free(p);
        /* (GetPrivateProfileString's size is an int: at most 7FFFh) */
        size = size >= 0x4000 ? 0x7FFF : size * 2;
    }
}

/* The keys of a private-profile section, each followed by a NUL, the
   list by another; NULL if there are none. */
LPSTR profile_keys(const char FAR *sec, const char FAR *file)
{
    WORD size = 256;
    for (;;) {
        LPSTR p = mem_alloc(size);
        int got;
        if (!p)
            return NULL;
        got = GetPrivateProfileString(sec, NULL, "", p, size, file);
        if (got == 0) {
            mem_free(p);
            return NULL;
        }
        if (got < (int)size - 2 || size >= 0x7FFF)
            return p;
        mem_free(p);
        size = size >= 0x4000 ? 0x7FFF : size * 2;
    }
}

int s_len(const char FAR *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* Copy at most max-1 characters and always terminate. */
void s_cpy(char FAR *d, const char FAR *s, int max)
{
    int i;
    if (max <= 0)
        return;                         /* no room even for the NUL */
    for (i = 0; i < max - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

void s_cat(char FAR *d, const char FAR *s, int max)
{
    int n = s_len(d);
    if (n < max)
        s_cpy(d + n, s, max - n);
}

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

int s_icmp(const char FAR *a, const char FAR *b)
{
    while (*a && up(*a) == up(*b)) {
        a++;
        b++;
    }
    return up(*a) - up(*b);
}

void s_utoa(char FAR *d, unsigned v)
{
    char b[6];
    int i = 5;
    b[5] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    s_cpy(d, b + i, 6);
}

void s_itoa(char FAR *d, int v)
{
    if (v < 0) {
        *d++ = '-';
        v = -v;
    }
    s_utoa(d, (unsigned)v);
}

/* Parse an optionally signed decimal number, skipping leading blanks;
   advances *ps past it. */
int s_atoi(const char FAR **ps)
{
    const char FAR *s = *ps;
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t' || *s == ',')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    *ps = s;
    return neg ? -v : v;
}

/* A message box: string resource `ids` with "%s" replaced by arg, under
   the title string `title_ids`. Returns MessageBox's answer. */
int ask(HWND owner, int title_ids, int ids, const char FAR *arg, UINT flags)
{
    /* our own resource strings: these buffers fit all of them */
    char fmt[200], title[40];
    LPSTR text;
    int i, j = 0, r;
    fmt[0] = title[0] = 0;              /* (if LoadString fails) */
    LoadString(g_hinst, ids, fmt, sizeof fmt);
    LoadString(g_hinst, title_ids, title, sizeof title);
    text = mem_alloc(s_len(fmt) + (arg ? s_len(arg) : 0) + 1);
    if (!text)
        return MessageBox(owner, fmt, title, flags);
    for (i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i + 1] == 's' && arg) {
            int k;
            for (k = 0; arg[k]; k++)
                text[j++] = arg[k];
            i++;
        } else {
            text[j++] = fmt[i];
        }
    }
    text[j] = 0;
    r = MessageBox(owner, text, title, flags);
    mem_free(text);
    return r;
}

#ifndef W16_WIN2
extern int FAR PASCAL SysErrorBox(LPCSTR text, LPCSTR caption, WORD left, WORD middle,
                                  WORD right);
#endif

/* USER's system error box (USER.320; Schulman, Undocumented Windows,
   ch. 6): string `ids` with "%s" replaced by arg, the title inside it,
   buttons Yes (the default) on the left and No on the right. TRUE for
   Yes. Windows 2.x: a system-modal MessageBox with Yes and No. */
BOOL ask_yes_no_system(int title_ids, int ids, const char FAR *arg)
{
    char fmt[200], title[40];
    LPSTR text;
    int i, j = 0;
    BOOL yes;
    fmt[0] = title[0] = 0;
    LoadString(g_hinst, ids, fmt, sizeof fmt);
    LoadString(g_hinst, title_ids, title, sizeof title);
    text = mem_alloc(s_len(fmt) + (arg ? s_len(arg) : 0) + 1);
    if (!text)
        return TRUE;
    for (i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i + 1] == 's' && arg) {
            int k;
            for (k = 0; arg[k]; k++)
                text[j++] = arg[k];
            i++;
        } else {
            text[j++] = fmt[i];
        }
    }
    text[j] = 0;
#ifdef W16_WIN2
    yes = MessageBox(NULL, text, title, MB_YESNO | MB_ICONEXCLAMATION | MB_SYSTEMMODAL) == IDYES;
#else
    yes = SysErrorBox(text, title, 3 | 0x8000, 0, 4) == 1;  /* SEB_YES default, SEB_NO */
#endif
    mem_free(text);
    return yes;
}

void message(HWND owner, int ids, const char FAR *arg)
{
    ask(owner, IDS_APPNAME, ids, arg, MB_OK | MB_ICONEXCLAMATION);
}
