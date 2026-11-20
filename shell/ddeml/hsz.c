/*
 * hsz.c - DDEML string handles (ARCHITECTURE.md, chapter "DDEML").
 *
 * A string handle is the string's global atom (LOWORD, HIWORD 0), as on
 * 3.1: two instances, or two tasks, get the same handle for the same
 * string in any case. An instance-specific service name is the base
 * name's atom with the instance's DMGFrame window in the high word; its
 * string is "name:(hwnd)" (lower-case hex).
 *
 * Each instance keeps one entry per DdeCreateStringHandle and
 * DdeKeepStringHandle, appended; DdeFreeStringHandle takes the first
 * entry of the string out and moves the last into its place, and
 * DdeUninitialize frees the first entry until none is left: the order
 * 3.1's monitors see MH_CLEANUP in (tests/ddeml/scripts/xstr.dm).
 *
 * The library's own references (a conversation's names, a link's item,
 * an item sent in some messages) are reported to monitors as 3.1 does:
 * MH_INCATOM / MH_DECATOM (6 and 7), and an atom made for a new string
 * handle as MH_ADDATOM (5) before MH_CREATE.
 */
#include "ddeml.h"

static void hsz_event(ATOM a, WORD action)
{
    if (a)
        mon_hsz(NULL, (HSZ)MAKELONG(a, 0), action);
}

/* one more reference to an atom, without telling the monitors (the
   atoms 3.1 adds for POKE, ADVISE and UNADVISE) */
ATOM hsz_inc_quiet(ATOM a)
{
    char b[256];
    if (!a || !GlobalGetAtomName(a, b, sizeof b))
        return 0;
    return GlobalAddAtom(b);
}

ATOM hsz_inc(ATOM a)
{
    ATOM r = hsz_inc_quiet(a);
    hsz_event(r, MH_INCATOM);
    return r;
}

void hsz_dec(ATOM a)
{
    if (!a)
        return;
    hsz_event(a, MH_DECATOM);
    GlobalDeleteAtom(a);
}

static void str_add(INST NEAR *in, ATOM a)
{
    if (!in || !a)
        return;
    if (in->nstrs == in->maxstrs) {
        WORD n = in->maxstrs ? in->maxstrs * 2 : 8;
        ATOM NEAR *p = (ATOM NEAR *)nalloc(n * sizeof(ATOM));
        if (!p)
            return;
        if (in->strs) {
            bcopy(p, in->strs, in->nstrs * sizeof(ATOM));
            nfree(in->strs);
        }
        in->strs = p;
        in->maxstrs = n;
    }
    in->strs[in->nstrs++] = a;
}

static void str_take(INST NEAR *in, ATOM a)
{
    WORD i;
    if (!in)
        return;
    for (i = 0; i < in->nstrs; i++)
        if (in->strs[i] == a) {
            in->strs[i] = in->strs[--in->nstrs];
            return;
        }
}

void str_freeall(INST NEAR *in)
{
    while (in->nstrs) {
        ATOM a = in->strs[0];
        hsz_event(a, MH_CLEANUP);
        hsz_dec(a);
        in->strs[0] = in->strs[--in->nstrs];
    }
    nfree(in->strs);
    in->strs = NULL;
    in->maxstrs = 0;
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
    hsz_event(a, MH_ADDATOM);
    str_add(in, a);
    hsz_event(a, MH_CREATE);
    return (HSZ)MAKELONG(a, 0);
}

BOOL DMLAPI DdeFreeStringHandle(DWORD id, HSZ h)
{
    INST NEAR *in = inst_arg(id);
    if (!in)
        return FALSE;
    if (!LOWORD(h))
        return TRUE;
    str_take(in, LOWORD(h));
    hsz_event(LOWORD(h), MH_DELETE);
    hsz_dec(LOWORD(h));
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
    hsz_event(LOWORD(h), MH_KEEP);
    a = hsz_inc(LOWORD(h));
    str_add(in, a);
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
