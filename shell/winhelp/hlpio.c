/*
 * hlpio.c - w16 Windows Help: the reader's platform side (hlp.h) for
 * Win16: files through KERNEL's _lopen/_llseek/_lread, memory from the
 * global heap (one locked block per allocation).
 */
#include "winhelp.h"

LONG hio_open(const char FAR *path)
{
    HFILE f = _lopen(path, OF_READ | OF_SHARE_DENY_WRITE);
    if (f == HFILE_ERROR)
        f = _lopen(path, OF_READ);
    return f == HFILE_ERROR ? -1L : (LONG)f;
}

void hio_close(LONG fh)
{
    _lclose((HFILE)fh);
}

BOOL hio_read(LONG fh, DWORD off, void FAR *buf, WORD n)
{
    if (_llseek((HFILE)fh, (LONG)off, 0) != (LONG)off)
        return FALSE;
    return _lread((HFILE)fh, buf, n) == n;
}

void FAR *hio_alloc(DWORD n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n ? n : 1);
    if (!h)
        return NULL;
    return GlobalLock(h);
}

void hio_free(void FAR *p)
{
    HGLOBAL h;
    if (!p)
        return;
    h = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
    GlobalUnlock(h);
    GlobalFree(h);
}

void m_set(void FAR *d, int c, WORD n)
{
    BYTE FAR *p = (BYTE FAR *)d;
    while (n--)
        *p++ = (BYTE)c;
}

void m_cpy(void FAR *d, const void FAR *s, WORD n)
{
    BYTE FAR *p = (BYTE FAR *)d;
    const BYTE FAR *q = (const BYTE FAR *)s;
    while (n--)
        *p++ = *q++;
}
