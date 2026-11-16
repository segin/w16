/*
 * hsz.c - DDEML string handles (ARCHITECTURE.md, chapter "DDEML").
 *
 * A string handle is the string's global atom (LOWORD, HIWORD 0), as on
 * 3.1: two instances, or two tasks, get the same handle for the same
 * string in any case. An instance-specific service name is the base
 * name's atom with the instance's DMGFrame window in the high word; its
 * string is "name:(hwnd)" (lower-case hex). Each instance counts the
 * references it holds, and DdeUninitialize deletes those left.
 */
#include "ddeml.h"

void str_ref(INST NEAR *in, ATOM a, int delta)
{
    SREF NEAR *s, NEAR * NEAR *pp;
    if (!in || !a)
        return;
    for (pp = &in->strs; (s = *pp) != NULL; pp = &s->next)
        if (s->atom == a)
            break;
    if (delta > 0) {
        if (!s) {
            s = (SREF NEAR *)nalloc(sizeof(SREF));
            if (!s)
                return;
            s->atom = a;
            s->next = in->strs;
            in->strs = s;
        }
        s->count++;
    } else if (s && s->count) {
        if (--s->count == 0) {
            *pp = s->next;
            nfree(s);
        }
    }
}

void str_freeall(INST NEAR *in)
{
    while (in->strs) {
        SREF NEAR *s = in->strs;
        in->strs = s->next;
        while (s->count--) {
            mon_hsz(in, (HSZ)MAKELONG(s->atom, 0), MH_CLEANUP);
            GlobalDeleteAtom(s->atom);
        }
        nfree(s);
    }
}

HSZ inst_hsz(INST NEAR *in, ATOM a)
{
    return (HSZ)MAKELONG(a, in->frame);
}

/* the instance an id names, with the error for a bad one */
static INST NEAR *inst_arg(DWORD id)
{
    INST NEAR *in = inst_find(id);
    if (!in)
        seterr(NULL, id ? DMLERR_INVALIDPARAMETER : DMLERR_DLL_NOT_INITIALIZED);
    return in;
}

HSZ DMLAPI DdeCreateStringHandle(DWORD id, LPSTR s, int cp)
{
    INST NEAR *in = inst_find(id);
    ATOM a;
    if (!in && id) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    if (!in)                            /* 3.1 makes it anyway */
        seterr(NULL, DMLERR_DLL_NOT_INITIALIZED);
    if (cp != 0 && cp != CP_WINANSI && cp != (int)ddeml_kbcp) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    if (!s || !*s)
        return 0;
    a = GlobalAddAtom(s);
    if (!a) {
        seterr(in, DMLERR_SYS_ERROR);
        return 0;
    }
    str_ref(in, a, 1);
    mon_hsz(in, (HSZ)MAKELONG(a, 0), MH_CREATE);
    return (HSZ)MAKELONG(a, 0);
}

BOOL DMLAPI DdeFreeStringHandle(DWORD id, HSZ h)
{
    INST NEAR *in = inst_arg(id);
    if (!in)
        return FALSE;
    if (!LOWORD(h))
        return TRUE;
    str_ref(in, LOWORD(h), -1);
    mon_hsz(in, h, MH_DELETE);
    GlobalDeleteAtom(LOWORD(h));
    return TRUE;
}

BOOL DMLAPI DdeKeepStringHandle(DWORD id, HSZ h)
{
    INST NEAR *in = inst_arg(id);
    char b[256];
    ATOM a;
    if (!in)
        return FALSE;
    if (!LOWORD(h))
        return TRUE;
    if (!GlobalGetAtomName(LOWORD(h), b, sizeof b)) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    a = GlobalAddAtom(b);
    str_ref(in, a, 1);
    mon_hsz(in, h, MH_KEEP);
    return (BOOL)a;                     /* 3.1 returns the atom */
}

DWORD DMLAPI DdeQueryString(DWORD id, HSZ h, LPCSTR cbuf, DWORD max, int cp)
{
    LPSTR buf = (LPSTR)cbuf;           /* the header has it const */
    INST NEAR *in = inst_arg(id);
    char b[256], t[300];
    WORD n, i;
    (void)cp;
    if (!in)
        return 0;
    if (!LOWORD(h)) {
        if (buf && max)
            buf[0] = 0;
        return 0;
    }
    if (!GlobalGetAtomName(LOWORD(h), b, sizeof b)) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    if (HIWORD(h))
        wsprintf(t, "%s:(%04x)", (LPSTR)b, HIWORD(h));
    else
        lstrcpy(t, b);
    n = (WORD)lstrlen(t);
    if (!buf)
        return n;
    if (!max)
        return 0;
    if (max - 1 < n)
        n = (WORD)(max - 1);
    for (i = 0; i < n; i++)
        buf[i] = t[i];
    buf[n] = 0;
    return n;
}

int DMLAPI DdeCmpStringHandles(HSZ h1, HSZ h2)
{
    /* by value; 0 is the smallest */
    if ((DWORD)h1 == (DWORD)h2)
        return 0;
    return (DWORD)h1 < (DWORD)h2 ? -1 : 1;
}
