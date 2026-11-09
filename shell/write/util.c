/*
 * util.c - w16 Write: huge memory blocks, strings, message boxes.
 */
#include "write.h"

/* ---- huge memory ----
 * A block is kept locked at its base while it exists; offsets beyond 64K
 * reach the next pieces through KERNEL's __AHINCR (selector increment in
 * protected mode, paragraph increment in real mode: both run here). */

BOOL hm_resize(HMEM *m, DWORD size)
{
    HGLOBAL h;
    if (size == 0)
        size = 16;
    if (!m->h) {
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        if (!h)
            return FALSE;
    } else {
        GlobalUnlock(m->h);
        h = GlobalReAlloc(m->h, size, GMEM_MOVEABLE | GMEM_ZEROINIT);
        if (!h) {
            m->p = (BYTE FAR *)GlobalLock(m->h);
            return FALSE;
        }
    }
    m->h = h;
    m->p = (BYTE FAR *)GlobalLock(h);
    m->size = size;
    return m->p != NULL;
}

void hm_free(HMEM *m)
{
    if (m->h) {
        GlobalUnlock(m->h);
        GlobalFree(m->h);
    }
    m->h = NULL;
    m->p = NULL;
    m->size = 0;
}

BYTE FAR *hm_ptr(const HMEM *m, DWORD off)
{
    WORD sel = SELECTOROF(m->p) + HIWORD(off) * ahincr();
    return (BYTE FAR *)MAKELP(sel, LOWORD(off));
}

WORD hm_span(DWORD off, DWORD want)
{
    DWORD room = 0x10000L - LOWORD(off);
    if (want > room)
        want = room;
    if (want > 0xFFF0L)                 /* a WORD count, and m_cpy's */
        want = 0xFFF0L;
    return (WORD)want;
}

void hm_get(const HMEM *m, DWORD off, void FAR *dst, DWORD n)
{
    BYTE FAR *d = (BYTE FAR *)dst;
    while (n) {
        WORD k = hm_span(off, n);
        m_cpy(d, hm_ptr(m, off), k);
        d += k;
        off += k;
        n -= k;
    }
}

void hm_put(HMEM *m, DWORD off, const void FAR *src, DWORD n)
{
    const BYTE FAR *s = (const BYTE FAR *)src;
    while (n) {
        WORD k = hm_span(off, n);
        m_cpy(hm_ptr(m, off), s, k);
        s += k;
        off += k;
        n -= k;
    }
}

/* overlapping moves within one block */
void hm_move(HMEM *m, DWORD dst, DWORD src, DWORD n)
{
    if (dst == src || !n)
        return;
    if (dst < src) {
        while (n) {
            WORD k = hm_span(src, n), k2 = hm_span(dst, k);
            BYTE FAR *d = hm_ptr(m, dst), FAR *s = hm_ptr(m, src);
            WORD i;
            for (i = 0; i < k2; i++)
                d[i] = s[i];
            dst += k2;
            src += k2;
            n -= k2;
        }
    } else {
        /* from the end: pieces ending at the 64K boundaries below */
        DWORD se = src + n, de = dst + n;
        while (n) {
            WORD ks = LOWORD(se) ? LOWORD(se) : 0xFFF0, kd = LOWORD(de) ? LOWORD(de) : 0xFFF0;
            WORD k = ks < kd ? ks : kd, i;
            BYTE FAR *d, FAR *s;
            if ((DWORD)k > n)
                k = (WORD)n;
            d = hm_ptr(m, de - k);
            s = hm_ptr(m, se - k);
            for (i = k; i-- > 0;)
                d[i] = s[i];
            se -= k;
            de -= k;
            n -= k;
        }
    }
}

/* ---- memory and strings ---- */

void m_set(void FAR *d, BYTE v, WORD n)
{
    BYTE FAR *p = (BYTE FAR *)d;
    while (n--)
        *p++ = v;
}

void m_cpy(void FAR *d, const void FAR *s, WORD n)
{
    BYTE FAR *p = (BYTE FAR *)d;
    const BYTE FAR *q = (const BYTE FAR *)s;
    while (n--)
        *p++ = *q++;
}

int m_cmp(const void FAR *a, const void FAR *b, WORD n)
{
    const BYTE FAR *p = (const BYTE FAR *)a, FAR *q = (const BYTE FAR *)b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p < *q ? -1 : 1;
    return 0;
}

int s_len(const char FAR *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void s_cpy(char FAR *d, const char FAR *s, int max)
{
    while (--max > 0 && *s)
        *d++ = *s++;
    *d = 0;
}

void s_cat(char FAR *d, const char FAR *s, int max)
{
    int n = s_len(d);
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

void s_utoa(char FAR *d, DWORD v)
{
    char b[11];
    int i = 10;
    b[10] = 0;
    do {
        b[--i] = (char)('0' + (WORD)(v % 10));
        v /= 10;
    } while (v);
    s_cpy(d, b + i, 11);
}

void load_str(WORD ids, char *buf, int size)
{
    if (!LoadString(g_hinst, ids, buf, size))
        buf[0] = 0;
}

/* A message box titled "Write" with string ids; "%s" in it is replaced
   by arg (the original's strings put a file name first, "%s\n..."). */
int msgbox(HWND owner, WORD ids, const char FAR *arg, UINT flags)
{
    char fmt[320], title[32];
    static char out[480];
    char *s, *d = out;
    load_str(ids, fmt, sizeof fmt);
    load_str(IDS_WRITE, title, sizeof title);
    for (s = fmt; *s && d < out + sizeof out - 1; s++) {
        if (s[0] == '%' && s[1] == 's' && arg) {
            const char FAR *a = arg;
            while (*a && d < out + sizeof out - 1)
                *d++ = *a++;
            s++;
        } else {
            *d++ = *s;
        }
    }
    *d = 0;
    return MessageBox(owner ? owner : g_frame, out, title, flags);
}
