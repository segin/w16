/*
 * util.c - OLESVR's memory, string, list and DDE data helpers, and the
 * parser of DDE EXECUTE command strings (ARCHITECTURE.md 16.2).
 */
#include "olesvr.h"

HINSTANCE svr_inst;
SRV FARP *svr_list;
WORD cf_native, cf_ownerlink, cf_objectlink, cf_binary;

/* ---- memory: fixed, shared global blocks (they outlive no task: each
   server's are freed when it is revoked) ---- */

void FARP *mem_alloc(DWORD n)
{
    HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE, n ? n : 1);
    return h ? GlobalLock(h) : NULL;
}

void mem_free(void FARP *p)
{
    if (p) {
        HGLOBAL h = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
        GlobalUnlock(h);
        GlobalFree(h);
    }
}

int str_len(const char FARP *s)
{
    int n = 0;
    if (s)
        while (s[n])
            n++;
    return n;
}

void str_cpy(char FARP *d, const char FARP *s)
{
    if (s)
        while (*s)
            *d++ = *s++;
    *d = 0;
}

char FARP *str_dup(const char FARP *s)
{
    char FARP *d = mem_alloc(str_len(s) + 1);
    if (d)
        str_cpy(d, s);
    return d;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

BOOL str_ieq(const char FARP *a, const char FARP *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return upper(*a) == upper(*b);
}

/* ---- conversations ---- */

CONV FARP *conv_find(CONV FARP *l, HWND client)
{
    for (; l; l = l->next)
        if (l->client == client)
            return l;
    return NULL;
}

CONV FARP *conv_add(CONV FARP **l, HWND client)
{
    CONV FARP *c = conv_find(*l, client), FARP *FARP *pp;
    if (c)
        return c;
    c = mem_alloc(sizeof *c);
    if (!c)
        return NULL;
    c->client = client;
    for (pp = l; *pp; pp = &(*pp)->next)      /* keep the order of arrival */
        ;
    *pp = c;
    return c;
}

void conv_remove(CONV FARP **l, HWND client)
{
    CONV FARP *FARP *pp;
    for (pp = l; *pp; pp = &(*pp)->next)
        if ((*pp)->client == client) {
            CONV FARP *c = *pp;
            *pp = c->next;
            mem_free(c);
            return;
        }
}

/* ---- DDE data ---- */

extern WORD __cdecl svr_ahincr;

/* copies n bytes between blocks that may be larger than 64K */
void huge_copy(void FAR *dst, const void FAR *src, DWORD n)
{
    BYTE FAR *d = dst;
    const BYTE FAR *s = src;
    while (n) {
        DWORD rd = 0x10000L - OFFSETOF(d), rs = 0x10000L - OFFSETOF(s), k = n, i;
        if (k > rd)
            k = rd;
        if (k > rs)
            k = rs;
        if (k > 0x8000L)
            k = 0x8000L;
        for (i = 0; i < k; i++)
            d[(WORD)i] = s[(WORD)i];
        n -= k;
        if (k == rd)
            d = (BYTE FAR *)MAKELP(SELECTOROF(d) + svr_ahincr, 0);
        else
            d += (WORD)k;
        if (k == rs)
            s = (const BYTE FAR *)MAKELP(SELECTOROF(s) + svr_ahincr, 0);
        else
            s += (WORD)k;
    }
}

/* formats whose DDE value is a handle rather than the bytes */
static BOOL by_handle(WORD cf)
{
    return cf == CF_METAFILEPICT || cf == CF_BITMAP || cf == CF_DIB || cf == CF_PALETTE;
}

/* A WM_DDE_DATA block for the data h the server handed over: flags, cf,
   then the bytes of h (GlobalSize of them, as Windows 3.1's OLESVR: its
   blocks are 4 bytes more than the server's) or, for the picture
   formats, the handle itself. The server's block is freed. */
HGLOBAL data_block(WORD flags, WORD cf, HGLOBAL h)
{
    DWORD n = by_handle(cf) ? 2 : (h ? GlobalSize(h) : 0);
    /* zero-filled: Windows 3.1's are (the rounding of the block) */
    HGLOBAL b = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n + 4);
    BYTE FAR *p = b ? (BYTE FAR *)GlobalLock(b) : NULL;
    if (!p)
        return NULL;
    ((WORD FAR *)p)[0] = flags;
    ((WORD FAR *)p)[1] = cf;
    if (by_handle(cf))
        ((WORD FAR *)p)[2] = (WORD)h;
    else if (h) {
        const BYTE FAR *s = (const BYTE FAR *)GlobalLock(h);
        if (s) {
            huge_copy(p + 4, s, n);
            GlobalUnlock(h);
        }
        GlobalFree(h);
    }
    GlobalUnlock(b);
    return b;
}

/* The value of a POKE or DATA block as a block of its own, what
   SetData gets: a copy of the bytes after the 4-byte header (GlobalSize
   less 4, as observed), or the handle the block carries. */
HGLOBAL data_value(HGLOBAL block, WORD cf)
{
    const BYTE FAR *p = (const BYTE FAR *)GlobalLock(block);
    DWORD n = GlobalSize(block);
    HGLOBAL h;
    BYTE FAR *d;
    if (!p)
        return NULL;
    if (by_handle(cf)) {
        h = n >= 6 ? (HGLOBAL)((const WORD FAR *)p)[2] : NULL;
        GlobalUnlock(block);
        return h;
    }
    n = n > 4 ? n - 4 : 0;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n ? n : 1);
    d = h ? (BYTE FAR *)GlobalLock(h) : NULL;
    if (d) {
        huge_copy(d, p + 4, n);
        GlobalUnlock(h);
    }
    GlobalUnlock(block);
    return h;
}

void ack(HWND to, HWND from, WORD status, WORD hi)
{
    if (!PostMessage(to, WM_DDE_ACK, (WPARAM)from, MAKELPARAM(status, hi)))
        if (hi && status != 0)          /* item atoms: ours to delete now */
            ;                           /* UNVERIFIED: a failed post */
}

