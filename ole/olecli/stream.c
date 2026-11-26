/*
 * stream.c - OleSaveToStream and OleLoadFromStream (ARCHITECTURE.md 16.4).
 *
 * The layout, as Microsoft's OLECLI writes it (probe OC1's OLESTREAM, each
 * field one Put call, in this order; the published MS-OLEDS document
 * describes the same "OLE 1.0 format"):
 *
 *   DWORD version 0x00000501, DWORD format: 1 link, 2 embedded, 3 static
 *   string class                     (DWORD length with the NUL, bytes;
 *                                      an empty string is just DWORD 0)
 *   link:     string topic (the file), string item, string network name,
 *             DWORD 0, DWORD update option
 *   embedded: string topic, string item (both empty), DWORD size, Native
 *             (size = the Native block's GlobalSize)
 *   then the presentation:
 *   DWORD 0x0501, DWORD 0 (none) or 5, string class:
 *     "METAFILEPICT": DWORD xExt, DWORD -yExt, DWORD size (8 + bits),
 *                     8 bytes METAFILEPICT (mm, xExt, yExt, hMF as they
 *                     are in memory), the metafile (its GlobalSize)
 *     "BITMAP", "DIB": the same three DWORDs, then the data (UNVERIFIED)
 *     other: generic: DWORD clipboard format (0: a registered format whose
 *            name follows as a string), DWORD size, data
 *   (a static object has no Native part: its own header is the
 *   presentation's, UNVERIFIED)
 */
#include "olecli.h"

#define OLE_VERSION 0x00000501L

typedef struct {
    LPOLESTREAM s;              /* NULL: only count */
    DWORD n;
    BOOL err;
} OUT;

static void put(OUT FAR *o, const void FAR *p, DWORD n)
{
    if (o->err)
        return;
    if (o->s) {
        if (o->s->lpstbl->Put(o->s, (void FAR *)p, n) != n) {
            o->err = TRUE;
            return;
        }
    }
    o->n += n;
}

static void put_dw(OUT FAR *o, DWORD v)
{
    put(o, &v, 4);
}

static void put_str(OUT FAR *o, LPCSTR s)
{
    WORD n = slen(s);
    if (!n) {
        put_dw(o, 0);
        return;
    }
    put_dw(o, n + 1);
    put(o, s, n + 1);
}

static void put_gstr(OUT FAR *o, HGLOBAL h)
{
    put_str(o, gstr(h));
    gstr_unlock(h);
}

static void put_global(OUT FAR *o, HGLOBAL h)
{
    DWORD n = h ? GlobalSize(h) : 0;
    put_dw(o, n);
    if (n) {
        put(o, GlobalLock(h), n);
        GlobalUnlock(h);
    }
}

static void put_fmtname(OUT FAR *o, WORD cf)
{
    char b[80];
    if (cf >= 0xC000 && GetClipboardFormatName(cf, b, sizeof b)) {
        put_dw(o, 0);
        put_str(o, b);
    } else
        put_dw(o, cf);
}

/* the presentation part */
static void save_pres(OUT FAR *o, LPOBJ ob)
{
    put_dw(o, OLE_VERSION);
    if (ob->pb && ob->type == OT_LINK && ob->hnative) {
        /* a Paintbrush link keeps its picture as a generic presentation:
           the class, "Native", the DIB file (observed) */
        put_dw(o, 5);
        put_gstr(o, ob->hclass);
        put_fmtname(o, cf_native);
        put_global(o, ob->hnative);
        return;
    }
    if (!ob->hpres || ob->pres == PRES_NONE) {
        put_dw(o, 0);
        return;
    }
    put_dw(o, 5);
    pres_save_body(o, ob, TRUE);
}

/* the part of a presentation after its format id: the class and data */
void pres_save_body(void FAR *out, LPOBJ ob, BOOL with_class)
{
    OUT FAR *o = (OUT FAR *)out;
    switch (ob->pres) {
    case PRES_MF:
        {
            METAFILEPICT FAR *m = (METAFILEPICT FAR *)GlobalLock(ob->hpres);
            HGLOBAL mf = (HGLOBAL)m->hMF;
            DWORD n = GlobalSize(mf);
            WORD hdr[4];
            if (with_class)
                put_str(o, "METAFILEPICT");
            put_dw(o, (LONG)m->xExt);
            put_dw(o, -(LONG)m->yExt);
            put_dw(o, n + 8);
            hdr[0] = m->mm;
            hdr[1] = m->xExt;
            hdr[2] = m->yExt;
            hdr[3] = (WORD)m->hMF;
            put(o, hdr, 8);
            put(o, GlobalLock(mf), n);
            GlobalUnlock(mf);
            GlobalUnlock(ob->hpres);
        }
        break;
    case PRES_DIB:
        {
            BITMAPINFOHEADER FAR *b = (BITMAPINFOHEADER FAR *)GlobalLock(ob->hpres);
            RECT r;
            pres_bounds(ob, &r);
            if (with_class)
                put_str(o, "DIB");
            put_dw(o, r.right);
            put_dw(o, r.bottom);
            GlobalUnlock(ob->hpres);
            (void)b;
            put_global(o, ob->hpres);
        }
        break;
    case PRES_BM:
        {
            BITMAP bm;
            DWORD n;
            HGLOBAL h;
            RECT r;
            GetObject(ob->hpres, sizeof bm, &bm);
            n = (DWORD)bm.bmWidthBytes * bm.bmHeight * bm.bmPlanes;
            /* the bits first: without them nothing is written (a record
               that promises bits it lacks would spoil the stream) */
            h = GlobalAlloc(GMEM_MOVEABLE, n ? n : 1);
            if (!h) {
                o->err = TRUE;
                break;
            }
            pres_bounds(ob, &r);
            if (with_class)
                put_str(o, "BITMAP");
            put_dw(o, r.right);
            put_dw(o, r.bottom);
            put_dw(o, n + 10);
            put(o, &bm, 10);
            {
                void FAR *p = GlobalLock(h);
                GetBitmapBits(ob->hpres, n, p);
                put(o, p, n);
                GlobalUnlock(h);
                GlobalFree(h);
            }
        }
        break;
    default:
        if (with_class)
            put_gstr(o, ob->hclass);
        put_fmtname(o, ob->presfmt);
        put_global(o, ob->hpres);
        break;
    }
}

static OLESTATUS save(OUT FAR *o, LPOBJ ob)
{
    put_dw(o, OLE_VERSION);
    put_dw(o, ob->type);
    if (ob->type == OT_STATIC) {
        if (!ob->hpres)
            return OLE_ERROR_BLANK;
        pres_save_body(o, ob, TRUE);
        return o->err ? OLE_ERROR_STREAM : OLE_OK;
    }
    put_gstr(o, ob->hclass);
    if (ob->type == OT_LINK) {
        put_gstr(o, ob->htopic);
        put_gstr(o, ob->hitem);
        put_gstr(o, ob->hnet);
        put_dw(o, 0);
        put_dw(o, ob->update);
    } else {
        /* the topic and item of an embedded object are not saved */
        put_dw(o, 0);
        put_dw(o, 0);
        if (!ob->hnative)
            return o->err ? OLE_ERROR_STREAM : OLE_ERROR_BLANK;
        put_global(o, ob->hnative);
    }
    save_pres(o, ob);
    return o->err ? OLE_ERROR_STREAM : OLE_OK;
}

OLESTATUS strm_save(LPOBJ ob, LPOLESTREAM s)
{
    OUT o;
    o.s = s;
    o.n = 0;
    o.err = FALSE;
    if (ob->busy)
        return OLE_BUSY;
    return save(&o, ob);
}

DWORD strm_size(LPOBJ ob)
{
    OUT o;
    o.s = NULL;
    o.n = 0;
    o.err = FALSE;
    if (save(&o, ob) != OLE_OK)
        return 0;
    return o.n;
}

/* ---- load ---- */

typedef struct {
    LPOLESTREAM s;
    BOOL err;
} IN;

static void get(IN FAR *i, void FAR *p, DWORD n)
{
    if (i->err || !n)
        return;
    if (i->s->lpstbl->Get(i->s, p, n) != n)
        i->err = TRUE;
}

static DWORD get_dw(IN FAR *i)
{
    DWORD v = 0;
    get(i, &v, 4);
    return v;
}

/* a string into a new global block (NULL for empty) */
static HGLOBAL get_str(IN FAR *i)
{
    DWORD n = get_dw(i);
    HGLOBAL h;
    LPSTR p;
    if (i->err || !n)
        return NULL;
    if (n >= 0xFFFF) {                  /* no class, topic or item is */
        i->err = TRUE;
        return NULL;
    }
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    if (!h) {
        i->err = TRUE;
        return NULL;
    }
    p = (LPSTR)GlobalLock(h);
    get(i, p, n);
    p[n] = 0;
    GlobalUnlock(h);
    if (i->err) {
        GlobalFree(h);
        return NULL;
    }
    return h;
}

static HGLOBAL get_global(IN FAR *i, DWORD n)
{
    HGLOBAL h;
    if (i->err)
        return NULL;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n ? n : 1);
    if (!h) {
        i->err = TRUE;
        return NULL;
    }
    get(i, GlobalLock(h), n);
    GlobalUnlock(h);
    return h;
}

static WORD get_fmt(IN FAR *i)
{
    DWORD f = get_dw(i);
    if (!f) {
        HGLOBAL n = get_str(i);
        WORD cf = 0;
        if (n) {
            cf = RegisterClipboardFormat(gstr(n));
            gstr_unlock(n);
            GlobalFree(n);
        }
        return cf;
    }
    return (WORD)f;
}

/* a presentation's body after its class name */
static void load_body(IN FAR *i, LPOBJ ob, LPCSTR cls)
{
    if (!lstrcmp(cls, "METAFILEPICT")) {
        DWORD n;
        WORD hdr[4];
        HGLOBAL bits, mfp;
        METAFILEPICT FAR *m;
        get_dw(i);
        get_dw(i);
        n = get_dw(i);
        if (n < 8) {
            i->err = TRUE;
            return;
        }
        get(i, hdr, 8);
        bits = get_global(i, n - 8);
        if (i->err) {
            gfree(bits);
            return;
        }
        mfp = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(METAFILEPICT));
        if (!mfp) {
            GlobalFree(bits);
            i->err = TRUE;
            return;
        }
        m = (METAFILEPICT FAR *)GlobalLock(mfp);
        m->mm = hdr[0];
        m->xExt = hdr[1];
        m->yExt = hdr[2];
        m->hMF = SetMetaFileBits(bits);
        if (!m->hMF)                    /* not a metafile: still ours */
            GlobalFree(bits);
        GlobalUnlock(mfp);
        pres_set(ob, CF_METAFILEPICT, mfp);
    } else if (!lstrcmp(cls, "DIB")) {
        DWORD n;
        get_dw(i);
        get_dw(i);
        n = get_dw(i);
        pres_set(ob, CF_DIB, get_global(i, n));
    } else if (!lstrcmp(cls, "BITMAP")) {
        DWORD n;
        BITMAP bm;
        HGLOBAL bits;
        get_dw(i);
        get_dw(i);
        n = get_dw(i);
        if (n < 10) {
            i->err = TRUE;
            return;
        }
        get(i, &bm, 10);
        if (!i->err) {
            /* the bits CreateBitmap reads (its own row width: word
               aligned) must all be in the record */
            DWORD wb;
            if (bm.bmWidth <= 0 || bm.bmHeight <= 0 || !bm.bmPlanes || !bm.bmBitsPixel) {
                i->err = TRUE;
                return;
            }
            wb = (((DWORD)bm.bmWidth * bm.bmBitsPixel + 15) >> 4) << 1;
            if ((n - 10) / bm.bmPlanes / bm.bmHeight < wb) {
                i->err = TRUE;
                return;
            }
        }
        bits = get_global(i, n - 10);
        if (!i->err) {
            HBITMAP b = CreateBitmap(bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel,
                                     GlobalLock(bits));
            GlobalUnlock(bits);
            if (b)
                pres_set(ob, CF_BITMAP, b);
        }
        gfree(bits);
    } else {
        WORD cf = get_fmt(i);
        DWORD n = get_dw(i);
        HGLOBAL h = get_global(i, n);
        if (i->err) {
            gfree(h);
            return;
        }
        if (cf == cf_native && ob->type == OT_LINK) {
            /* a Paintbrush link's picture is its Native data */
            gfree(ob->hnative);
            ob->hnative = h;
            ob->pb = TRUE;
        } else
            pres_set(ob, cf, h);
    }
}

/* after the version, the format and the class (DefLoadFromStream starts
   here: an object handler has read those) */
OLESTATUS strm_load_rest(LPOLESTREAM s, LPOLECLIENT c, LPDOC doc, LPCSTR name,
                         LPOLEOBJECT FAR *out, LONG fmt, LPCSTR clsname)
{
    IN i;
    LPOBJ ob;
    LPSTR cn;
    i.s = s;
    i.err = FALSE;
    if (fmt != OT_LINK && fmt != OT_EMBEDDED && fmt != OT_STATIC)
        return OLE_ERROR_STREAM;
    ob = obj_new(c, doc, name);
    if (!ob)
        return OLE_ERROR_MEMORY;
    ob->type = fmt;
    ob->hclass = gstr_new(clsname);
    if (fmt == OT_STATIC)
        load_body(&i, ob, clsname);
    else {
        if (fmt == OT_LINK) {
            ob->htopic = get_str(&i);
            ob->hitem = get_str(&i);
            ob->hnet = get_str(&i);
            get_dw(&i);
            ob->update = (OLEOPT_UPDATE)get_dw(&i);
        } else {
            gfree(get_str(&i));
            gfree(get_str(&i));
            ob->hnative = get_global(&i, get_dw(&i));
        }
        /* the presentation */
        get_dw(&i);
        if (get_dw(&i) == 5) {
            HGLOBAL pc = get_str(&i);
            if (!i.err) {
                cn = gstr(pc);
                load_body(&i, ob, cn);
                gstr_unlock(pc);
            }
            gfree(pc);
        } else {
            ob->pb = is_pbrush(clsname);
            if (!ob->pb)
                ob->render = olerender_none;
        }
    }
    if (i.err) {
        obj_free(ob);
        return OLE_ERROR_STREAM;
    }
    if (ob->type == OT_EMBEDDED) {
        obj_set_number(ob);
        ob->htopic = obj_embedded_topic(ob);
    }
    *out = (LPOLEOBJECT)ob;
    if (ob->type == OT_LINK)
        return link_loaded(ob);
    return OLE_OK;
}

OLESTATUS strm_load(LPOLESTREAM s, LPOLECLIENT c, LPDOC doc, LPCSTR name,
                    LPOLEOBJECT FAR *out)
{
    IN i;
    DWORD fmt;
    HGLOBAL cls;
    OLESTATUS r;
    i.s = s;
    i.err = FALSE;
    get_dw(&i);                         /* the OLE version */
    fmt = get_dw(&i);
    if (i.err || (fmt != OT_LINK && fmt != OT_EMBEDDED && fmt != OT_STATIC))
        return OLE_ERROR_STREAM;
    cls = get_str(&i);
    if (i.err) {
        gfree(cls);
        return OLE_ERROR_STREAM;
    }
    r = strm_load_rest(s, c, doc, name, out, fmt, gstr(cls));
    gstr_unlock(cls);
    gfree(cls);
    return r;
}
