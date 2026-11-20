/*
 * data.c - DDEML data handles (ARCHITECTURE.md, chapter "DDEML").
 *
 * A data handle names a record in DDEML's heap holding a global block in
 * DDE's own layout: WORD flags, WORD format, then the data (the block
 * that goes into WM_DDE_DATA and WM_DDE_POKE as it is). A handle made
 * with no item name has no header: it is an EXECUTE command block. Its
 * size is the block's GlobalSize less the header, as on 3.1.
 */
#include "ddeml.h"

static WORD ds_sel(void)
{
    return SELECTOROF((void FAR *)&ddeml_insts);
}

DATA NEAR *data_find(HDDEDATA h)
{
    INST NEAR *in;
    DATA NEAR *d;
    if (HIWORD(h) != ds_sel())
        return NULL;
    for (in = ddeml_insts; in; in = in->next)
        for (d = in->datas; d; d = d->next)
            if ((WORD)d == LOWORD(h))
                return d;
    return NULL;
}

HDDEDATA data_handle(DATA NEAR *d)
{
    return d ? (HDDEDATA)MAKELONG((WORD)d, ds_sel()) : 0;
}

static WORD hdr(DATA NEAR *d)
{
    return (d->flags & DF_NOHEADER) ? 0 : 4;
}

static DWORD dsize(DATA NEAR *d)
{
    DWORD n = d->h ? GlobalSize(d->h) : 0;
    if (d->size)
        return d->size;
    return n >= hdr(d) ? n - hdr(d) : 0;
}

DATA NEAR *data_new(INST NEAR *in, HGLOBAL h, WORD flags, ATOM item, WORD fmt)
{
    DATA NEAR *d = (DATA NEAR *)nalloc(sizeof(DATA));
    if (!d)
        return NULL;
    d->inst = in;
    d->h = h;
    d->flags = flags;
    d->item = item;
    d->fmt = fmt;
    d->next = in->datas;
    in->datas = d;
    return d;
}

void data_free(DATA NEAR *d, BOOL block)
{
    DATA NEAR * NEAR *pp;
    for (pp = &d->inst->datas; *pp; pp = &(*pp)->next)
        if (*pp == d) {
            *pp = d->next;
            break;
        }
    if (block && d->h)
        GlobalFree(d->h);
    nfree(d);
}

void data_freeall(INST NEAR *in)
{
    while (in->datas)
        data_free(in->datas, !(in->datas->flags & DF_TEMP));
}

/* a new DDE block: header (flags 0, format) unless an EXECUTE block;
   fixed, as 3.1's (its blocks' handles are selectors: xmon.dm as xmon+x) */
HGLOBAL data_block(INST NEAR *in, const void FAR *p, DWORD cb, BOOL header, WORD fmt)
{
    HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_DDESHARE | GMEM_ZEROINIT, cb + (header ? 4 : 0));
    BYTE FAR *b;
    (void)in;
    if (!h)
        return NULL;
    b = (BYTE FAR *)GlobalLock(h);
    if (header) {
        ((WORD FAR *)b)[0] = 0;
        ((WORD FAR *)b)[1] = fmt;
        b += 4;
    }
    if (p && cb)
        hcopy(b, p, cb);
    GlobalUnlock(h);
    return h;
}

/* the block to post for a handle given to DDEML, its header flags or'ed
   with FLAGS: an application-owned handle's block is copied (the
   application keeps it), any other handle is used up */
HGLOBAL data_take(DATA NEAR *d, WORD flags)
{
    HGLOBAL h;
    WORD df = d->flags;
    if (df & DF_APPOWNED) {
        DWORD n = GlobalSize(d->h);
        h = GlobalAlloc(GMEM_FIXED | GMEM_DDESHARE, n);
        if (h) {
            hcopy(GlobalLock(h), GlobalLock(d->h), n);
            GlobalUnlock(d->h);
            GlobalUnlock(h);
        }
        d->flags |= DF_USED;
    } else {
        h = d->h;
        d->h = NULL;
        data_free(d, FALSE);
    }
    if (h && !(df & DF_NOHEADER) && flags) {
        WORD FAR *p = (WORD FAR *)GlobalLock(h);
        p[0] |= flags;
        GlobalUnlock(h);
    }
    return h;
}

/* a block that came in a message, lent to a callback */
HDDEDATA data_wrap(INST NEAR *in, HGLOBAL h, BOOL header, ATOM item)
{
    DATA NEAR *d;
    WORD fmt = 0;
    if (!h)
        return 0;
    if (header) {
        WORD FAR *p = (WORD FAR *)GlobalLock(h);
        if (p) {
            fmt = p[1];
            GlobalUnlock(h);
        }
    }
    d = data_new(in, h, DF_TEMP | DF_USED | (header ? 0 : DF_NOHEADER), item, fmt);
    return data_handle(d);
}

/* ---- the DMLAPI ---- */

HDDEDATA DMLAPI DdeCreateDataHandle(DWORD id, LPVOID p, DWORD cb, DWORD off, HSZ item, UINT fmt,
                                 UINT afCmd)
{
    INST NEAR *in = inst_find(id);
    HGLOBAL h;
    DATA NEAR *d;
    if (!in) {
        seterr(NULL, id ? DMLERR_INVALIDPARAMETER : DMLERR_DLL_NOT_INITIALIZED);
        return 0;
    }
    if (afCmd & ~HDATA_APPOWNED) {
        seterr(in, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    h = data_block(in, p ? hadd(p, off) : NULL, cb, LOWORD(item) != 0, fmt);
    if (!h) {
        seterr(in, DMLERR_MEMORY_ERROR);
        return 0;
    }
    d = data_new(in, h, (afCmd & HDATA_APPOWNED ? DF_APPOWNED : 0) | (LOWORD(item) ? 0 : DF_NOHEADER),
                 LOWORD(item), fmt);
    if (!d) {
        GlobalFree(h);
        seterr(in, DMLERR_MEMORY_ERROR);
        return 0;
    }
    return data_handle(d);
}

HDDEDATA DMLAPI DdeAddData(HDDEDATA hd, LPVOID p, DWORD cb, DWORD off)
{
    DATA NEAR *d = data_find(hd);
    DWORD need;
    BYTE FAR *b;
    if (!d) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    if (d->flags & DF_USED) {
        seterr(d->inst, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    need = off + cb + hdr(d);
    if (need > GlobalSize(d->h)) {
        HGLOBAL h = GlobalReAlloc(d->h, need, GMEM_MOVEABLE | GMEM_ZEROINIT);
        if (!h) {
            seterr(d->inst, DMLERR_MEMORY_ERROR);
            return 0;
        }
        d->h = h;
    }
    b = (BYTE FAR *)GlobalLock(d->h);
    if (p && cb)
        hcopy(hadd(b, hdr(d) + off), p, cb);
    GlobalUnlock(d->h);
    return hd;
}

DWORD DMLAPI DdeGetData(HDDEDATA hd, LPVOID p, DWORD max, DWORD off)
{
    DATA NEAR *d = data_find(hd);
    DWORD size, n;
    BYTE FAR *b;
    if (!d) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    size = dsize(d);
    if (!p)
        return size;
    if (off > size) {
        seterr(d->inst, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    n = size - off;
    if (n > max)
        n = max;
    b = (BYTE FAR *)GlobalLock(d->h);
    if (n)
        hcopy(p, hadd(b, hdr(d) + off), n);
    GlobalUnlock(d->h);
    return n;
}

/* the OpenWatcom header declares a near result: exported under the
   SDK's name with the SDK's far result (DDEACCESSDATA, ddeml.lnk) */
BYTE FAR * DMLAPI DmlAccessData(HDDEDATA hd, DWORD FAR *pcb)
{
    DATA NEAR *d = data_find(hd);
    BYTE FAR *b;
    if (!d) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return NULL;
    }
    b = (BYTE FAR *)GlobalLock(d->h);
    if (!b) {
        seterr(d->inst, DMLERR_INVALIDPARAMETER);
        return NULL;
    }
    d->flags |= DF_LOCKED;
    if (pcb)
        *pcb = dsize(d);
    return b + hdr(d);
}

BOOL DMLAPI DdeUnaccessData(HDDEDATA hd)
{
    DATA NEAR *d = data_find(hd);
    if (!d) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    if (d->flags & DF_LOCKED) {
        GlobalUnlock(d->h);
        d->flags &= ~DF_LOCKED;
    }
    return TRUE;
}

BOOL DMLAPI DdeFreeDataHandle(HDDEDATA hd)
{
    DATA NEAR *d;
    if (!hd || hd == (HDDEDATA)1)
        return TRUE;
    d = data_find(hd);
    if (!d) {
        seterr(NULL, DMLERR_INVALIDPARAMETER);
        return FALSE;
    }
    if (d->flags & DF_TEMP)             /* a callback's: DDEML frees it */
        return TRUE;
    if (d->flags & DF_LOCKED)
        GlobalUnlock(d->h);
    data_free(d, TRUE);
    return TRUE;
}
