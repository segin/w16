/*
 * util.c - strings and the icon frame (no C runtime).
 */
#include "clock.h"

int s_len(const char FAR *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* copy at most n - 1 characters and terminate */
void s_cpy(char FAR *d, const char FAR *s, int n)
{
    while (n > 1 && *s) {
        *d++ = *s++;
        n--;
    }
    if (n > 0)
        *d = 0;
}

void s_cat(char FAR *d, const char FAR *s, int n)
{
    int l = s_len(d);
    s_cpy(d + l, s, n - l);
}

int s_utoa(char *buf, unsigned v)
{
    char t[6];
    int i = 0, n = 0;
    do {
        t[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        buf[n++] = t[--i];
    buf[n] = 0;
    return n;
}

/* the next decimal number in *p, skipping what comes before it; -1 and
   *p unchanged at the end of the string */
int s_atoi(const char FAR **p)
{
    const char FAR *s = *p;
    int v = 0, neg = 0;
    while (*s && !(*s >= '0' && *s <= '9') && *s != '-')
        s++;
    if (!*s)
        return -1;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    *p = s;
    return neg ? -v : v;
}

/* The frame round the minimized clock (observed, both faces): a black
   square (its corners left out), a red one inside it and a black one
   inside that, 36 x 36 pixels, drawn after the face. */
void icon_frame(HDC dc)
{
    HPEN old = SelectObject(dc, GetStockObject(BLACK_PEN));
    MoveTo(dc, 0, 1);
    LineTo(dc, 0, 35);
    MoveTo(dc, 1, 35);
    LineTo(dc, 35, 35);
    MoveTo(dc, 35, 34);
    LineTo(dc, 35, 0);
    MoveTo(dc, 34, 0);
    LineTo(dc, 0, 0);
    MoveTo(dc, 2, 2);
    LineTo(dc, 2, 33);
    LineTo(dc, 33, 33);
    LineTo(dc, 33, 2);
    LineTo(dc, 2, 2);
    SelectObject(dc, g_pnred);
    MoveTo(dc, 1, 1);
    LineTo(dc, 1, 34);
    LineTo(dc, 34, 34);
    LineTo(dc, 34, 1);
    LineTo(dc, 1, 1);
    SelectObject(dc, old);
}
