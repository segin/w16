/*
 * util.c - w16 Cardfile: strings, memory and file helpers (no C runtime).
 */
#include "cardfile.h"

int s_len(const char FAR *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* copy at most size - 1 characters and terminate */
void s_cpy(char FAR *d, const char FAR *s, int size)
{
    int i;
    for (i = 0; i < size - 1 && s[i]; i++)
        d[i] = s[i];
    if (size > 0)
        d[i] = 0;
}

void s_cat(char FAR *d, const char FAR *s, int size)
{
    int n = s_len(d);
    if (n < size)
        s_cpy(d + n, s, size - n);
}

void mem_cpy(void FAR *d, const void FAR *s, WORD n)
{
    char FAR *dd = d;
    const char FAR *ss = s;
    if (dd < ss || SELECTOROF(dd) != SELECTOROF(ss)) {
        while (n--)
            *dd++ = *ss++;
    } else {                            /* overlapping, upwards */
        dd += n;
        ss += n;
        while (n--)
            *--dd = *--ss;
    }
}

void mem_set(void FAR *d, int c, WORD n)
{
    char FAR *dd = d;
    while (n--)
        *dd++ = (char)c;
}

int s_cmpn(const char FAR *a, const char FAR *b, int n)
{
    for (; n > 0; n--, a++, b++)
        if (*a != *b || !*a)
            return (BYTE)*a - (BYTE)*b;
    return 0;
}

LPSTR s_utoa(LPSTR d, DWORD v)
{
    char b[12];
    int i = 11;
    b[11] = 0;
    do {
        b[--i] = (char)('0' + (WORD)(v % 10));
        v /= 10;
    } while (v);
    s_cpy(d, b + i, 12);
    return d;
}

const char FAR *base_name(const char FAR *path)
{
    const char FAR *b = path;
    for (; *path; path = AnsiNext(path))
        if (*path == '\\' || *path == ':' || *path == '/')
            b = path + 1;
    return b;
}

BOOL f_read(HFILE f, void FAR *buf, WORD n)
{
    return n == 0 || _lread(f, buf, n) == n;
}

BOOL f_write(HFILE f, const void FAR *buf, WORD n)
{
    return n == 0 || _lwrite(f, buf, n) == n;
}

/* copy n bytes from the current position of one file to another's */
BOOL f_copy(HFILE from, HFILE to, DWORD n)
{
    static char buf[512];
    while (n) {
        WORD k = n > sizeof buf ? sizeof buf : (WORD)n;
        if (!f_read(from, buf, k) || !f_write(to, buf, k))
            return FALSE;
        n -= k;
    }
    return TRUE;
}

/* The original's message strings mark the place of the file name with
   "%%" (and a few with "%s"): put arg there. */
void fmt_msg(LPSTR out, int size, LPCSTR fmt, LPCSTR arg)
{
    int j = 0;
    for (; *fmt && j < size - 1; fmt++) {
        if (*fmt == '%' && (fmt[1] == '%' || fmt[1] == 's')) {
            const char FAR *a = arg ? arg : "";
            while (*a && j < size - 1)
                out[j++] = *a++;
            fmt++;
        } else {
            out[j++] = *fmt;
        }
    }
    out[j] = 0;
}

#ifdef CF_DEBUG
/* a trace line appended to C:\CFDBG.TXT (debugging builds only) */
void dbg(const char FAR *s)
{
    OFSTRUCT of;
    HFILE f = OpenFile("C:\\CFDBG.TXT", &of, OF_WRITE);
    if (f == HFILE_ERROR)
        f = OpenFile("C:\\CFDBG.TXT", &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, s_len(s));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}
#endif
