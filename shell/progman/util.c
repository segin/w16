/*
 * util.c - string helpers and message boxes (no C runtime).
 */
#include "progman.h"

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

/* Show string resource `ids` (with "%s" replaced by arg) in a message box. */
void message(HWND owner, int ids, const char FAR *arg)
{
    char fmt[128], text[256], title[40];
    int i, j = 0;
    LoadString(g_hinst, ids, fmt, sizeof fmt);
    LoadString(g_hinst, IDS_APPNAME, title, sizeof title);
    for (i = 0; fmt[i] && j < (int)sizeof text - 1; i++) {
        if (fmt[i] == '%' && fmt[i + 1] == 's' && arg) {
            int k;
            for (k = 0; arg[k] && j < (int)sizeof text - 1; k++)
                text[j++] = arg[k];
            i++;
        } else {
            text[j++] = fmt[i];
        }
    }
    text[j] = 0;
    MessageBox(owner, text, title, MB_OK | MB_ICONEXCLAMATION);
}
