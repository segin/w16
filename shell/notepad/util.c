/*
 * util.c - w16 Notepad: strings and memory (no C runtime).
 */
#include "notepad.h"

/* ---- global memory, addressed by locked far pointers ---- */

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

void mem_free(LPSTR p)
{
    HGLOBAL h;
    if (!p)
        return;
    h = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
    GlobalUnlock(h);
    GlobalFree(h);
}

/* ---- strings ---- */

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

/* The file name part of a path. */
const char FAR *s_basename(const char FAR *path)
{
    const char FAR *b = path;
    for (; *path; path++)
        if (*path == '\\' || *path == '/' || *path == ':')
            b = path + 1;
    return b;
}

/* String resource `ids`, as long as it is. */
LPSTR load_str(int ids)
{
    int size = 128, n;
    for (;;) {
        LPSTR s = mem_alloc(size);
        if (!s)
            return NULL;
        n = LoadString(g_hinst, ids, s, size);
        if (n < size - 1)
            return s;
        mem_free(s);
        size *= 2;
    }
}

/* String resource `ids` with each "%%" replaced by `arg` (the original's
   strings use "%%" as their one placeholder). */
LPSTR format_msg(int ids, const char FAR *arg)
{
    LPSTR fmt = load_str(ids), out;
    int n = 0, a = s_len(arg), i, k;
    if (!fmt)
        return NULL;
    for (i = 0; fmt[i]; i++)
        n += (fmt[i] == '%' && fmt[i + 1] == '%') ? (i++, a) : 1;
    out = mem_alloc((DWORD)n + 1);
    if (out) {
        for (i = k = 0; fmt[i]; i++) {
            if (fmt[i] == '%' && fmt[i + 1] == '%') {
                s_cpy(out + k, arg, a + 1);
                k += a;
                i++;
            } else {
                out[k++] = fmt[i];
            }
        }
        out[k] = 0;
    }
    mem_free(fmt);
    return out;
}
