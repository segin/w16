/*
 * env.c - w16 SHELL.DLL: DoEnvironmentSubst (SHELL.37) and
 * FindEnvironmentString (SHELL.38), on the DOS environment
 * (GetDOSEnvironment). Behaviour from probe SH7.
 */
#include "shell.h"

/* the value of NAME (n characters, any case) in the environment, or NULL */
static LPSTR env_find(const char __far *name, WORD n)
{
    LPSTR e = GetDOSEnvironment();
    if (!e || !n)
        return NULL;
    while (*e) {
        if (sh_same_ci(e, name, n) && e[n] == '=')
            return e + n + 1;
        e += sh_len(e) + 1;
    }
    return NULL;
}

/* a pointer to the value (past '='), the name matched in any case; NULL
   for a name that is not there, a prefix of one, or one with '=' */
LPSTR SHAPI FindEnvironmentString(LPSTR name)
{
    WORD n = sh_len(name), i;
    for (i = 0; i < n; i++)
        if (name[i] == '=')
            return NULL;
    return env_find(name, n);
}

/* %NAME% becomes NAME's value; %% becomes %; an unknown %NAME%, a lone %
   and an unterminated %NAME stay as they are. When the result and its NUL
   fit in cb, it replaces s and the result is (length, 1); otherwise s is
   unchanged and the result is (length of s, 0) (SH7). UNVERIFIED: after
   an unknown name the scan goes on behind its closing %. */
DWORD SHAPI DoEnvironmentSubst(LPSTR s, UINT cb)
{
    HGLOBAL h;
    char __far *out;
    DWORD len = 0, cap;
    WORD i = 0, n = sh_len(s);
    /* first the length, then the text */
    int pass;
    for (pass = 0; pass < 2; pass++) {
        len = 0;
        i = 0;
        while (i < n) {
            if (s[i] == '%') {
                WORD j = i + 1;
                while (j < n && s[j] != '%')
                    j++;
                if (j < n) {
                    LPSTR v;
                    if (j == i + 1) {
                        if (pass)
                            out[len] = '%';
                        len++;
                        i = j + 1;
                        continue;
                    }
                    v = env_find(s + i + 1, j - i - 1);
                    if (v) {
                        WORD k = sh_len(v);
                        if (pass)
                            sh_move(out + len, v, k);
                        len += k;
                        i = j + 1;
                        continue;
                    }
                    if (pass)
                        sh_move(out + len, s + i, j - i + 1);
                    len += j - i + 1;
                    i = j + 1;
                    continue;
                }
            }
            if (pass)
                out[len] = s[i];
            len++;
            i++;
        }
        if (!pass) {
            if (len + 1 > cb || len > 0xFFFEL)
                return MAKELONG(n, 0);
            cap = len + 1;
            h = sh_alloc(cap);
            out = (char __far *)sh_lock(h);
            if (!out)
                return MAKELONG(n, 0);
        }
    }
    out[len] = 0;
    sh_move(s, out, (WORD)len + 1);
    GlobalUnlock(h);
    GlobalFree(h);
    return MAKELONG((WORD)len, 1);
}
