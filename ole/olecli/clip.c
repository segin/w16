/*
 * clip.c - OLE objects and the clipboard (ARCHITECTURE.md 16.6): the
 * registered formats "Native", "OwnerLink", "ObjectLink" (strings
 * "class\0document\0item\0\0") and pictures. The client opens and empties
 * the clipboard itself (SDK).
 */
#include "olecli.h"

/* OleCopyToClipboard: observed formats and order: an embedded object
   "Native", "OwnerLink", CF_METAFILEPICT; a link "OwnerLink" (holding the
   link's class, file and item), "Native", CF_METAFILEPICT */
OLESTATUS clip_copy(LPOBJ o)
{
    HANDLE pic = NULL;
    WORD picfmt = 0;
    if (o->busy)
        return OLE_BUSY;
    if (o->pb && o->hnative) {
        HANDLE h;
        if (pres_getdata(o, CF_METAFILEPICT, &h) == OLE_WARN_DELETE_DATA) {
            pic = h;
            picfmt = CF_METAFILEPICT;
        }
    } else if (o->hpres && o->pres != PRES_GEN) {
        pic = pres_copy(o->presfmt, o->hpres);
        picfmt = o->presfmt;
    }
    if (o->type == OT_EMBEDDED) {
        if (o->hnative)
            SetClipboardData(cf_native, copy_global(o->hnative));
        SetClipboardData(cf_ownerlink, link_data(o));
    } else if (o->type == OT_LINK) {
        SetClipboardData(cf_ownerlink, link_data(o));
        if (o->hnative)
            SetClipboardData(cf_native, copy_global(o->hnative));
    }
    if (pic)
        SetClipboardData(picfmt, pic);
    return OLE_OK;
}

static BOOL has_picture(void)
{
    return IsClipboardFormatAvailable(CF_METAFILEPICT) || IsClipboardFormatAvailable(CF_DIB)
        || IsClipboardFormatAvailable(CF_BITMAP);
}

static OLESTATUS query(LPCSTR protocol, OLEOPT_RENDER r, OLECLIPFORMAT f, BOOL link)
{
    (void)r;
    (void)f;
    if (!protocol)
        return OLE_ERROR_PROTOCOL;
    if (!lstrcmp(protocol, "Static"))
        return !link && has_picture() ? OLE_OK : OLE_ERROR_FORMAT;
    if (lstrcmp(protocol, "StdFileEditing"))
        return OLE_ERROR_PROTOCOL;
    if (link)
        return IsClipboardFormatAvailable(cf_objectlink) ? OLE_OK : OLE_ERROR_FORMAT;
    return IsClipboardFormatAvailable(cf_native) && IsClipboardFormatAvailable(cf_ownerlink)
               ? OLE_OK : OLE_ERROR_FORMAT;
}

/* OLECLI.10 */
OLESTATUS OLEAPI OleQueryCreateFromClip(LPCSTR protocol, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return query(protocol, r, f, FALSE);
}

/* OLECLI.9 */
OLESTATUS OLEAPI OleQueryLinkFromClip(LPCSTR protocol, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return query(protocol, r, f, TRUE);
}

/* the three strings of an OwnerLink or ObjectLink block */
static BOOL parse_link(HANDLE h, LPSTR cls, LPSTR file, LPSTR item, WORD cb)
{
    LPSTR p;
    DWORD n;
    WORD i = 0, k;
    LPSTR outs[3];
    if (!h)
        return FALSE;
    n = GlobalSize(h);
    p = (LPSTR)GlobalLock(h);
    if (!p)
        return FALSE;
    outs[0] = cls;
    outs[1] = file;
    outs[2] = item;
    for (k = 0; k < 3; k++) {
        WORD j = 0;
        while (i < n && p[i]) {
            if (j + 1 < cb)
                outs[k][j++] = p[i];
            i++;
        }
        outs[k][j] = 0;
        i++;
    }
    GlobalUnlock(h);
    return cls[0] != 0;
}

/* the clipboard's picture for an object rendered "draw" (metafile first) */
static void take_picture(LPOBJ o, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    WORD cf = 0;
    HANDLE h;
    if (r == olerender_none)
        return;
    if (r == olerender_format)
        cf = f;
    else if (IsClipboardFormatAvailable(CF_METAFILEPICT))
        cf = CF_METAFILEPICT;
    else if (IsClipboardFormatAvailable(CF_DIB))
        cf = CF_DIB;
    else if (IsClipboardFormatAvailable(CF_BITMAP))
        cf = CF_BITMAP;
    if (!cf || cf == cf_native)
        return;
    h = GetClipboardData(cf);
    if (!h)
        return;
    h = pres_copy(cf, h);
    if (h && cf == CF_DIB) {
        /* a DIB without a resolution gets the screen's (3.1's OLECLI:
           3780 pixels a metre on VGA in Write's pasted pictures and
           links, WR16, WR30, from SNAP's DIBs with 0; UNVERIFIED: how it
           comes by it) */
        BITMAPINFOHEADER FAR *b = (BITMAPINFOHEADER FAR *)GlobalLock(h);
        if (b) {
            HDC dc = GetDC(NULL);
            if (!b->biXPelsPerMeter)
                b->biXPelsPerMeter = MulDiv(GetDeviceCaps(dc, LOGPIXELSX), 10000, 254);
            if (!b->biYPelsPerMeter)
                b->biYPelsPerMeter = MulDiv(GetDeviceCaps(dc, LOGPIXELSY), 10000, 254);
            ReleaseDC(NULL, dc);
            GlobalUnlock(h);
        }
    }
    pres_set(o, cf, h);
}

/* OLECLI.12 */
OLESTATUS OLEAPI OleCreateFromClip(LPCSTR protocol, LPOLECLIENT c, LHCLIENTDOC lh,
                                   LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                                   OLECLIPFORMAT f)
{
    LPDOC d = doc_check(lh);
    LPOBJ o;
    char cls[128], file[260], item[260];
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!protocol)
        return OLE_ERROR_PROTOCOL;
    if (!lstrcmp(protocol, "Static")) {
        if (!has_picture())
            return OLE_ERROR_CLIPBOARD;
        o = obj_new(c, d, name);
        if (!o)
            return OLE_ERROR_MEMORY;
        o->type = OT_STATIC;
        take_picture(o, olerender_draw, 0);
        o->hclass = gstr_new(o->pres == PRES_MF ? "METAFILEPICT" : o->pres == PRES_BM
                             ? "BITMAP" : "DIB");
        *out = (LPOLEOBJECT)o;
        return OLE_OK;
    }
    if (lstrcmp(protocol, "StdFileEditing"))
        return OLE_ERROR_PROTOCOL;
    if (!IsClipboardFormatAvailable(cf_native) ||
        !parse_link(GetClipboardData(cf_ownerlink), cls, file, item, sizeof file))
        return OLE_ERROR_CLIPBOARD;
    o = obj_new(c, d, name);
    if (!o)
        return OLE_ERROR_MEMORY;
    o->type = OT_EMBEDDED;
    o->hclass = gstr_new(cls);
    o->render = r;
    o->renderfmt = f;
    o->pb = is_pbrush(cls) && r == olerender_draw;
    o->hnative = copy_global(GetClipboardData(cf_native));
    if (!o->pb)
        take_picture(o, r, f);
    obj_set_number(o);
    o->htopic = obj_embedded_topic(o);
    *out = (LPOLEOBJECT)o;
    return OLE_OK;
}

/* OLECLI.11 */
OLESTATUS OLEAPI OleCreateLinkFromClip(LPCSTR protocol, LPOLECLIENT c, LHCLIENTDOC lh,
                                       LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    LPDOC d = doc_check(lh);
    char cls[128], file[260], item[260];
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!protocol || lstrcmp(protocol, "StdFileEditing"))
        return OLE_ERROR_PROTOCOL;
    if (!parse_link(GetClipboardData(cf_objectlink), cls, file, item, sizeof file))
        return OLE_ERROR_CLIPBOARD;
    if (r == olerender_draw && has_picture()) {
        /* the clipboard's picture is the presentation (le.c link_new) */
        LPOBJ o = link_new(d, c, cls, file, item, name, r, f);
        if (!o)
            return OLE_ERROR_MEMORY;
        take_picture(o, r, f);
        *out = (LPOLEOBJECT)o;
        return link_connect_running(o);
    }
    return link_from_clip(d, c, cls, file, item, name, out, r, f);
}
