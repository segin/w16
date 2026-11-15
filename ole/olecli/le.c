/*
 * le.c - the methods of OLECLI's objects (the OLEOBJECTVTBL the OleXxx
 * calls go through; exported as LExxx) and the functions that make
 * objects (ARCHITECTURE.md 16). Behaviour as observed on Windows 3.1 with
 * probe OC1 (tests/ole/obs); UNVERIFIED marks what was not observed.
 */
#include "olecli.h"

/* the format a server is asked for as the presentation (0: none) */
WORD obj_advise_pres(LPOBJ o)
{
    if (o->type == OT_STATIC)
        return 0;
    if (o->render == olerender_format)
        return o->renderfmt == cf_native ? 0 : o->renderfmt;
    if (o->render == olerender_none || o->pb)
        return 0;
    return CF_METAFILEPICT;
}

/* ---- making objects ---- */

static OLESTATUS check_new(LPCSTR protocol, LHCLIENTDOC lh, LPOLEOBJECT FAR *out,
                           LPDOC FAR *d)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    *d = doc_check(lh);
    if (!*d)
        return OLE_ERROR_HANDLE;
    if (!protocol || lstrcmp(protocol, "StdFileEditing"))
        return OLE_ERROR_PROTOCOL;
    return OLE_OK;
}

static void set_render(LPOBJ o, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    LPSTR cls = gstr(o->hclass);
    o->render = r;
    o->renderfmt = f;
    o->pb = is_pbrush(cls) && r == olerender_draw;
    gstr_unlock(o->hclass);
}

/* the steps that advise an embedded object's data: /Save then /Close, for
   Native and the presentation (observed order) */
static void add_embedded_advises(LPOBJ o)
{
    WORD pf = obj_advise_pres(o);
    dde_add(o, ST_ADVISE, cf_native, NULL, IT_SAVE);
    if (pf)
        dde_add(o, ST_ADVISE, pf, NULL, IT_SAVE);
    dde_add(o, ST_ADVISE, cf_native, NULL, IT_CLOSE);
    if (pf)
        dde_add(o, ST_ADVISE, pf, NULL, IT_CLOSE);
}

static void add_requests(LPOBJ o, WORD item)
{
    WORD pf = obj_advise_pres(o);
    dde_add(o, ST_REQUEST, cf_native, NULL, item);
    if (pf)
        dde_add(o, ST_REQUEST, pf, NULL, item);
    dde_add(o, ST_DONE_CHANGED, 0, NULL, 0);
}

static OLESTATUS start_or_fail(LPOBJ o, LPOLEOBJECT FAR *out)
{
    OLESTATUS s;
    *out = (LPOLEOBJECT)o;
    s = dde_run(o);
    if (s != OLE_OK && s != OLE_WAIT_FOR_RELEASE) {
        dde_close_all(o);
        obj_free(o);
        *out = NULL;
    }
    return s;
}

static OLESTATUS create(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls, LHCLIENTDOC lh,
                        LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                        OLECLIPFORMAT f, BOOL show, BOOL launch, LPCSTR templ)
{
    LPDOC d;
    LPOBJ o;
    OLESTATUS s = check_new(protocol, lh, out, &d);
    HGLOBAL cmd;
    LPSTR topic, clsn;
    if (s)
        return s;
    if (!cls || !reg_class_known(cls))
        return OLE_ERROR_CLASS;
    o = obj_new(c, d, name);
    if (!o)
        return OLE_ERROR_MEMORY;
    o->type = OT_EMBEDDED;
    o->hclass = gstr_new(cls);
    set_render(o, r, f);
    obj_set_number(o);
    o->htopic = obj_embedded_topic(o);
    if (!launch) {
        *out = (LPOLEOBJECT)o;
        return OLE_OK;
    }
    dde_start(o, templ ? OLE_CREATEFROMTEMPLATE : show ? OLE_CREATE : OLE_CREATEINVISIBLE);
    dde_add(o, ST_CONNECT_SYS, 0, NULL, SW_SHOWNORMAL);
    topic = gstr(o->htopic);
    clsn = gstr(o->hclass);
    if (templ)                          /* UNVERIFIED: not observed */
        cmd = dde_exec_str3("StdNewFromTemplate", clsn, topic, templ);
    else
        cmd = dde_exec_str("StdNewDocument", clsn, topic);
    gstr_unlock(o->hclass);
    gstr_unlock(o->htopic);
    dde_add(o, ST_SYS_EXEC, 0, cmd, templ ? OLE_ERROR_TEMPLATE : OLE_ERROR_NEW);
    dde_add(o, ST_DOC_CONNECT, 0, NULL, 0);
    add_embedded_advises(o);
    dde_add(o, ST_FINISH_OPEN, 0, NULL, show);
    return start_or_fail(o, out);
}

/* OLECLI.34 */
OLESTATUS OLEAPI OleCreate(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls, LHCLIENTDOC lh,
                           LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                           OLECLIPFORMAT f)
{
    return create(protocol, c, cls, lh, name, out, r, f, TRUE, TRUE, NULL);
}

/* OLECLI.58 */
OLESTATUS OLEAPI OleCreateInvisible(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                    LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out,
                                    OLEOPT_RENDER r, OLECLIPFORMAT f, BOOL launch)
{
    return create(protocol, c, cls, lh, name, out, r, f, FALSE, launch, NULL);
}

/* OLECLI.33: the class is the template file's (UNVERIFIED: not observed) */
OLESTATUS OLEAPI OleCreateFromTemplate(LPCSTR protocol, LPOLECLIENT c, LPCSTR templ,
                                       LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    char cls[128];
    if (!templ || !reg_class_of_file(templ, cls, sizeof cls))
        return OLE_ERROR_CLASS;
    return create(protocol, c, cls, lh, name, out, r, f, TRUE, TRUE, templ);
}

/* OLECLI.38: an embedded object from a file: the server opens the file
   (started hidden), gives its Native data and presentation, and is let
   go (observed) */
OLESTATUS OLEAPI OleCreateFromFile(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                   LPCSTR file, LHCLIENTDOC lh, LPCSTR name,
                                   LPOLEOBJECT FAR *out, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    LPDOC d;
    LPOBJ o;
    char cbuf[128];
    OLESTATUS s = check_new(protocol, lh, out, &d);
    if (s)
        return s;
    if (!file || !*file)
        return OLE_ERROR_NAME;
    if (!cls || !*cls) {
        if (!reg_class_of_file(file, cbuf, sizeof cbuf))
            return OLE_ERROR_CLASS;
        cls = cbuf;
    }
    if (!reg_class_known(cls))
        return OLE_ERROR_CLASS;
    o = obj_new(c, d, name);
    if (!o)
        return OLE_ERROR_MEMORY;
    o->type = OT_EMBEDDED;
    o->hclass = gstr_new(cls);
    set_render(o, r, f);
    obj_set_number(o);
    o->htopic = gstr_new(file);
    dde_start(o, OLE_CREATEFROMFILE);
    dde_add(o, ST_CONNECT_DOC, 0, NULL, SW_HIDE);
    add_requests(o, IT_NONE);
    dde_add(o, ST_MAYBE_TERMINATE, 0, NULL, 0);
    dde_add(o, ST_BECOME_EMBEDDED, 0, NULL, 0);
    return start_or_fail(o, out);
}

/* the steps that connect a link and get its data */
static void add_link_advises(LPOBJ o)
{
    WORD pf = obj_advise_pres(o);
    dde_add(o, ST_ADVISE, cf_native, NULL, IT_LINK);
    if (pf)                             /* UNVERIFIED: order for presentations */
        dde_add(o, ST_ADVISE, pf, NULL, IT_LINK);
    dde_add(o, ST_ADVISE, cf_binary, NULL, IT_DOCNAME);
}

static OLESTATUS make_link(LPDOC d, LPOLECLIENT c, LPCSTR cls, LPCSTR file, LPCSTR item,
                           LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                           OLECLIPFORMAT f, OLE_RELEASE_METHOD m)
{
    LPOBJ o = obj_new(c, d, name);
    if (!o)
        return OLE_ERROR_MEMORY;
    o->type = OT_LINK;
    o->hclass = gstr_new(cls);
    set_render(o, r, f);
    o->htopic = gstr_new(file);
    o->hitem = gstr_new(item);
    dde_start(o, m);
    dde_add(o, ST_CONNECT_DOC, 0, NULL, SW_HIDE);
    add_link_advises(o);
    add_requests(o, IT_LINK);
    dde_add(o, ST_MAYBE_TERMINATE, 0, NULL, 0);
    return start_or_fail(o, out);
}

/* OLECLI.39 */
OLESTATUS OLEAPI OleCreateLinkFromFile(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                       LPCSTR file, LPCSTR item, LHCLIENTDOC lh,
                                       LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    LPDOC d;
    char cbuf[128];
    OLESTATUS s = check_new(protocol, lh, out, &d);
    if (s)
        return s;
    if (!file || !*file)
        return OLE_ERROR_NAME;
    if (!cls || !*cls) {
        if (!reg_class_of_file(file, cbuf, sizeof cbuf))
            return OLE_ERROR_CLASS;
        cls = cbuf;
    }
    if (!reg_class_known(cls))
        return OLE_ERROR_CLASS;
    return make_link(d, c, cls, file, item, name, out, r, f, OLE_CREATELINKFROMFILE);
}

/* OLECLI.11 (from clip.c's parse of ObjectLink) */
OLESTATUS link_from_clip(LPDOC d, LPOLECLIENT c, LPCSTR cls, LPCSTR file, LPCSTR item,
                         LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                         OLECLIPFORMAT f)
{
    return make_link(d, c, cls, file, item, name, out, r, f, OLE_LNKPASTE);
}

/* a link just loaded from a stream: if its server has the document open,
   connect (hot link, observed); a server is not started for it
   (UNVERIFIED) */
OLESTATUS link_loaded(LPOBJ o)
{
    if (o->update != oleupdate_always)
        return OLE_OK;
    dde_start(o, OLE_LOADFROMSTREAM);
    dde_add(o, ST_CONNECT_RUNNING, 0, NULL, 0);
    add_link_advises(o);
    add_requests(o, IT_LINK);
    dde_add(o, ST_MAYBE_TERMINATE, 0, NULL, 0);
    return dde_run(o);
}

/* ---- the vtbl ---- */

#define OBJ_OF(p) LPOBJ o = (LPOBJ)(p)

void FAR * OLEAPI LeQueryProtocol(LPOLEOBJECT p, OLE_LPCSTR protocol)
{
    OBJ_OF(p);
    if (!protocol)
        return NULL;
    if (!lstrcmp(protocol, "Static"))
        return p;
    if (!lstrcmp(protocol, "StdFileEditing") && o->type != OT_STATIC)
        return p;
    if (!lstrcmp(protocol, "StdExecute") && o->type != OT_STATIC) {
        char path[128];
        LPSTR cls = gstr(o->hclass);
        BOOL ok = reg_server(cls, "StdExecute", path, sizeof path);
        gstr_unlock(o->hclass);
        return ok ? p : NULL;
    }
    return NULL;
}

static OLESTATUS busy_check(LPOBJ o)
{
    return o->busy ? OLE_BUSY : OLE_OK;
}

/* the end of an object: conversations closed first (Delete closes an open
   embedded object's document, Release just ends the conversations) */
static OLESTATUS finish(LPOBJ o, BOOL del)
{
    OLESTATUS s;
    if (o->busy)
        return OLE_BUSY;
    if (o->hdoc && o->open) {
        dde_start(o, del ? OLE_DELETE : OLE_NONE);
        if (del && o->type == OT_EMBEDDED) {
            dde_add(o, ST_EXEC, 0, dde_exec_str("StdCloseDocument", NULL, NULL), 0);
            dde_add(o, ST_WAIT_DOCTERM, 0, NULL, 0);
        } else
            dde_add(o, ST_TERMINATE, 0, NULL, 0);
        dde_add(o, ST_CLOSE_SYS, 0, NULL, 0);
        o->free_after = TRUE;
        s = dde_run(o);
        if (s == OLE_WAIT_FOR_RELEASE)
            return s;
    }
    dde_close_all(o);
    obj_free(o);
    return OLE_OK;
}

OLESTATUS OLEAPI LeRelease(LPOLEOBJECT p)
{
    return finish((LPOBJ)p, FALSE);
}

OLESTATUS OLEAPI LeDelete(LPOLEOBJECT p)
{
    return finish((LPOBJ)p, TRUE);
}

/* Show is DoVerb's primary verb (UNVERIFIED) */
OLESTATUS OLEAPI LeShow(LPOLEOBJECT p, BOOL take_focus)
{
    return LeActivate(p, OLEVERB_PRIMARY, TRUE, take_focus, NULL, NULL);
}

OLESTATUS OLEAPI LeDoVerb(LPOLEOBJECT p, UINT verb, BOOL show, BOOL focus)
{
    return LeActivate(p, verb, show, focus, NULL, NULL);
}

OLESTATUS OLEAPI LeActivate(LPOLEOBJECT p, UINT verb, BOOL show, BOOL focus,
                                       HWND w, OLE_CONST RECT FAR *r)
{
    OBJ_OF(p);
    char cmd[300];
    LPSTR item;
    HGLOBAL h;
    (void)w;
    (void)r;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    dde_start(o, OLE_ACTIVATE);
    if (o->type == OT_EMBEDDED && !o->open) {
        LPSTR topic = gstr(o->htopic);
        dde_add(o, ST_CONNECT_SYS, 0, NULL, SW_SHOWNORMAL);
        dde_add(o, ST_SYS_EXEC, 0, dde_exec_str("StdEditDocument", topic, NULL),
                OLE_ERROR_EDIT);
        gstr_unlock(o->htopic);
        dde_add(o, ST_DOC_CONNECT, 0, NULL, 0);
        if (o->hhost)                   /* UNVERIFIED: where host names go */
            dde_add(o, ST_POKE, cf_binary, copy_global(o->hhost), IT_HOST);
        if (o->hnative)
            dde_add(o, ST_POKE, cf_native, poke_block(cf_native, o->hnative), IT_NONE);
        add_embedded_advises(o);
    } else if (o->type == OT_LINK && !o->open) {
        /* observed: a server that had the file open is just told the
           verb; one started for it is advised and asked for data first */
        WORD at;
        dde_add(o, ST_CONNECT_DOC, 0, NULL, SW_SHOWNORMAL);
        at = o->nsteps;
        dde_add(o, ST_SKIP_UNLESS_LAUNCHED, 0, NULL, 0);
        add_link_advises(o);
        add_requests(o, IT_LINK);
        dde_set_arg(o, at, o->nsteps - at - 1);
    }
    /* "[StdDoVerbItem("<item>",<verb>,<show>,<!focus>)]": the last
       argument is the opposite of fTakeFocus (observed both ways) */
    item = gstr(o->hitem);
    wsprintf(cmd, "[StdDoVerbItem(\"%s\",%u,%s,%s)]", item, verb,
             (LPSTR)(show ? "TRUE" : "FALSE"), (LPSTR)(focus ? "FALSE" : "TRUE"));
    gstr_unlock(o->hitem);
    h = gstr_new(cmd);
    dde_add(o, ST_EXEC, 0, h, OLE_ERROR_DOVERB);
    return dde_run(o);
}

OLESTATUS OLEAPI LeGetData(LPOLEOBJECT p, OLECLIPFORMAT f, HANDLE FAR *h)
{
    OBJ_OF(p);
    if (!h)
        return OLE_ERROR_ADDRESS;
    *h = NULL;
    if (f == cf_native) {
        if (o->type == OT_STATIC)
            return OLE_ERROR_FORMAT;
        if (!o->hnative)
            return OLE_ERROR_BLANK;
        *h = o->hnative;
        return OLE_OK;
    }
    if (f == cf_ownerlink || f == cf_objectlink) {
        if (o->type == OT_STATIC)
            return OLE_ERROR_FORMAT;
        if ((f == cf_ownerlink) != (o->type == OT_EMBEDDED))
            return OLE_ERROR_FORMAT;
        gfree(o->hlinkdata);
        o->hlinkdata = link_data(o);
        *h = o->hlinkdata;
        return *h ? OLE_OK : OLE_ERROR_MEMORY;
    }
    return pres_getdata(o, f, h);
}

/* the data of a POKE: DDEPOKE's flags (0: the client frees it, observed),
   the format, then a copy of the data */
HGLOBAL poke_block(WORD cf, HANDLE h)
{
    DWORD n = 0;
    HGLOBAL b;
    BYTE FAR *p;
    if (cf == CF_METAFILEPICT || cf == CF_BITMAP || cf == CF_PALETTE)
        n = 2;
    else if (h)
        n = GlobalSize(h);
    b = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n + 4);
    if (!b)
        return NULL;
    p = (BYTE FAR *)GlobalLock(b);
    ((WORD FAR *)p)[0] = 0;
    ((WORD FAR *)p)[1] = cf;
    if (n == 2 && (cf == CF_METAFILEPICT || cf == CF_BITMAP || cf == CF_PALETTE))
        *(HANDLE FAR *)(p + 4) = pres_copy(cf, h);
    else if (n)
        hmemcpy(p + 4, GlobalLock(h), n), GlobalUnlock(h);
    GlobalUnlock(b);
    return b;
}

static OLESTATUS poke(LPOBJ o, WORD item, WORD cf, HGLOBAL data)
{
    if (!data)
        return OLE_ERROR_MEMORY;
    dde_start(o, OLE_OTHER);
    dde_add(o, ST_POKE, cf, data, item);
    return dde_run(o);
}

OLESTATUS OLEAPI LeSetData(LPOLEOBJECT p, OLECLIPFORMAT f, HANDLE h)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (!o->open || !o->hsrvdoc) {
        /* UNVERIFIED: an object that is not open takes Native as is */
        if (f == cf_native) {
            HGLOBAL c = copy_global(h);
            if (!c)
                return OLE_ERROR_MEMORY;
            gfree(o->hnative);
            o->hnative = c;
            return OLE_OK;
        }
        return OLE_ERROR_NOT_OPEN;
    }
    dde_start(o, OLE_SETDATA);
    dde_add(o, ST_POKE, f, poke_block(f, h), o->type == OT_LINK ? IT_LINK : IT_NONE);
    return dde_run(o);
}

/* StdHostNames: WORD offset of the client's name, WORD offset of the
   document's, then both strings (observed) */
OLESTATUS OLEAPI LeSetHostNames(LPOLEOBJECT p, OLE_LPCSTR app, OLE_LPCSTR doc)
{
    OBJ_OF(p);
    WORD na = slen(app), nd = slen(doc);
    HGLOBAL h;
    BYTE FAR *b;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4 + na + 1 + nd + 1);
    if (!h)
        return OLE_ERROR_MEMORY;
    b = (BYTE FAR *)GlobalLock(h);
    ((WORD FAR *)b)[0] = 0;
    ((WORD FAR *)b)[1] = na + 1;
    hmemcpy(b + 4, app ? app : "", na + 1);
    hmemcpy(b + 4 + na + 1, doc ? doc : "", nd + 1);
    GlobalUnlock(h);
    gfree(o->hhost);
    o->hhost = h;
    if (o->type != OT_EMBEDDED || !o->open)
        return OLE_OK;
    return poke(o, IT_HOST, cf_binary, poke_block(cf_binary, h));
}

OLESTATUS OLEAPI LeSetTargetDevice(LPOLEOBJECT p, HGLOBAL h)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (!h)
        return OLE_ERROR_HANDLE;
    if (!o->open)
        return OLE_OK;
    return poke(o, IT_TARGET, cf_binary, poke_block(cf_binary, h));
}

/* StdDocDimensions: observed for (0,0,100,50) the words 100,-50,100,-50;
   UNVERIFIED for rectangles not at the origin */
OLESTATUS OLEAPI LeSetBounds(LPOLEOBJECT p, OLE_CONST RECT FAR *r)
{
    OBJ_OF(p);
    HGLOBAL h;
    int FAR *b;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (!r)
        return OLE_ERROR_ADDRESS;
    if (busy_check(o))
        return OLE_BUSY;
    o->bounds = *r;
    o->hasbounds = TRUE;
    if (!o->open)
        return OLE_OK;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4 + sizeof(RECT));
    if (!h)
        return OLE_ERROR_MEMORY;
    b = (int FAR *)GlobalLock(h);
    b[0] = 0;
    b[1] = cf_binary;
    b[2] = r->right - r->left;
    b[3] = -(r->bottom - r->top);
    b[4] = b[2];
    b[5] = b[3];
    GlobalUnlock(h);
    return poke(o, IT_DIM, cf_binary, h);
}

OLECLIPFORMAT OLEAPI LeEnumFormat(LPOLEOBJECT p, OLECLIPFORMAT f)
{
    return pres_enum((LPOBJ)p, f);
}

OLESTATUS OLEAPI LeSetColorScheme(LPOLEOBJECT p, OLE_CONST LOGPALETTE FAR *pal)
{
    OBJ_OF(p);
    HGLOBAL h;
    BYTE FAR *b;
    WORD n;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (!pal)
        return OLE_ERROR_PALETTE;
    if (busy_check(o))
        return OLE_BUSY;
    if (!o->open)
        return OLE_OK;
    n = 4 + pal->palNumEntries * sizeof(PALETTEENTRY);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4 + n);
    if (!h)
        return OLE_ERROR_MEMORY;
    b = (BYTE FAR *)GlobalLock(h);
    ((WORD FAR *)b)[0] = 0;
    ((WORD FAR *)b)[1] = cf_binary;
    hmemcpy(b + 4, pal, n);
    GlobalUnlock(h);
    return poke(o, IT_COLOR, cf_binary, h);
}

OLESTATUS OLEAPI LeSaveToStream(LPOLEOBJECT p, LPOLESTREAM s)
{
    return strm_save((LPOBJ)p, s);
}

/* a copy of the object's data in another (or the same) document */
static OLESTATUS copy_obj(LPOBJ o, LPOLECLIENT c, LHCLIENTDOC lh, LPCSTR name,
                          LPOLEOBJECT FAR *out, LONG type)
{
    LPDOC d = doc_check(lh);
    LPOBJ n;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!d)
        return OLE_ERROR_HANDLE;
    n = obj_new(c, d, name);
    if (!n)
        return OLE_ERROR_MEMORY;
    n->type = type;
    n->hclass = copy_global(o->hclass);
    n->hitem = copy_global(o->hitem);
    n->hnet = copy_global(o->hnet);
    n->hnative = copy_global(o->hnative);
    n->render = o->render;
    n->renderfmt = o->renderfmt;
    n->pb = o->pb;
    n->update = o->update;
    n->pres = o->pres;
    n->presfmt = o->presfmt;
    n->hpres = o->hpres ? pres_copy(o->presfmt, o->hpres) : (HANDLE)0;
    if (type == OT_EMBEDDED) {
        obj_set_number(n);
        n->htopic = obj_embedded_topic(n);
    } else
        n->htopic = copy_global(o->htopic);
    *out = (LPOLEOBJECT)n;
    return OLE_OK;
}

OLESTATUS OLEAPI LeClone(LPOLEOBJECT p, LPOLECLIENT c, LHCLIENTDOC lh,
                                    OLE_LPCSTR name, LPOLEOBJECT FAR *out)
{
    OBJ_OF(p);
    if (busy_check(o))
        return OLE_BUSY;
    return copy_obj(o, c, lh, name, out, o->type);
}

/* UNVERIFIED: an embedded copy of a link's current data */
OLESTATUS OLEAPI LeCopyFromLink(LPOLEOBJECT p, LPOLECLIENT c, LHCLIENTDOC lh,
                                           OLE_LPCSTR name, LPOLEOBJECT FAR *out)
{
    OBJ_OF(p);
    if (o->type != OT_LINK)
        return OLE_ERROR_NOT_LINK;
    if (busy_check(o))
        return OLE_BUSY;
    return copy_obj(o, c, lh, name, out, OT_EMBEDDED);
}

static BOOL same_global(HGLOBAL a, HGLOBAL b)
{
    DWORD n, i;
    BYTE __huge *x, __huge *y;
    BOOL r = TRUE;
    if (!a || !b)
        return a == b;
    n = GlobalSize(a);
    if (n != GlobalSize(b))
        return FALSE;
    x = (BYTE __huge *)GlobalLock(a);
    y = (BYTE __huge *)GlobalLock(b);
    for (i = 0; i < n; i++)
        if (x[i] != y[i]) {
            r = FALSE;
            break;
        }
    GlobalUnlock(a);
    GlobalUnlock(b);
    return r;
}

static BOOL same_str(HGLOBAL a, HGLOBAL b)
{
    BOOL r = !lstrcmpi(gstr(a), gstr(b));
    gstr_unlock(a);
    gstr_unlock(b);
    return r;
}

/* equal: same kind, class, and (embedded) Native data or (link) document
   and item (UNVERIFIED beyond the equal embedded copies observed) */
OLESTATUS OLEAPI LeEqual(LPOLEOBJECT a, LPOLEOBJECT b)
{
    LPOBJ x = (LPOBJ)a, y = (LPOBJ)b;
    if (x->type != y->type || !same_str(x->hclass, y->hclass))
        return OLE_ERROR_NOT_EQUAL;
    if (x->type == OT_LINK)
        return same_str(x->htopic, y->htopic) && same_str(x->hitem, y->hitem)
                   ? OLE_OK : OLE_ERROR_NOT_EQUAL;
    if (x->type == OT_EMBEDDED)
        return same_global(x->hnative, y->hnative) ? OLE_OK : OLE_ERROR_NOT_EQUAL;
    return same_global(x->hpres, y->hpres) ? OLE_OK : OLE_ERROR_NOT_EQUAL;
}

OLESTATUS OLEAPI LeCopyToClipboard(LPOLEOBJECT p)
{
    return clip_copy((LPOBJ)p);
}

OLESTATUS OLEAPI LeDraw(LPOLEOBJECT p, HDC dc, OLE_CONST RECT FAR *r,
                                   OLE_CONST RECT FAR *wnd, HDC fmt)
{
    (void)wnd;
    if (!r)
        return OLE_ERROR_ADDRESS;
    return pres_draw((LPOBJ)p, dc, r, fmt);
}

OLESTATUS OLEAPI LeExecute(LPOLEOBJECT p, HGLOBAL h, UINT reserved)
{
    OBJ_OF(p);
    HGLOBAL c;
    (void)reserved;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (!o->open || !o->hsrvdoc)
        return OLE_ERROR_NOT_OPEN;
    c = copy_global(h);
    if (!c)
        return OLE_ERROR_MEMORY;
    dde_start(o, OLE_OTHER);
    dde_add(o, ST_EXEC, 0, c, 0);       /* a refusal is not an error (observed) */
    return dde_run(o);
}

/* Close: an embedded object's server closes the document (and goes, when
   OLE started it); a link just ends the conversation (observed) */
OLESTATUS OLEAPI LeClose(LPOLEOBJECT p)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (!o->open || !o->hdoc)
        return OLE_OK;
    dde_start(o, OLE_CLOSE);
    if (o->type == OT_EMBEDDED) {
        dde_add(o, ST_EXEC, 0, dde_exec_str("StdCloseDocument", NULL, NULL), 0);
        dde_add(o, ST_WAIT_DOCTERM, 0, NULL, 0);
        dde_add(o, ST_WAIT_SYSTERM, 0, NULL, 0);
    } else
        dde_add(o, ST_TERMINATE, 0, NULL, 0);
    return dde_run(o);
}

OLESTATUS OLEAPI LeUpdate(LPOLEOBJECT p)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (o->type == OT_EMBEDDED) {
        if (!o->open)                   /* UNVERIFIED */
            return OLE_OK;
        dde_start(o, OLE_UPDATE);
        add_requests(o, IT_NONE);
        return dde_run(o);
    }
    dde_start(o, OLE_UPDATE);
    if (!o->open) {
        dde_add(o, ST_CONNECT_DOC, 0, NULL, SW_HIDE);
        add_requests(o, IT_LINK);
        dde_add(o, ST_MAYBE_TERMINATE, 0, NULL, 0);
    } else
        add_requests(o, IT_LINK);
    return dde_run(o);
}

/* UNVERIFIED for a link that is not open (observed: an open one, OLE_OK) */
OLESTATUS OLEAPI LeReconnect(LPOLEOBJECT p)
{
    OBJ_OF(p);
    if (o->type != OT_LINK)
        return o->type == OT_STATIC ? OLE_ERROR_STATIC : OLE_ERROR_NOT_LINK;
    if (busy_check(o))
        return OLE_BUSY;
    if (o->open)
        return OLE_OK;
    dde_start(o, OLE_RECONNECT);
    dde_add(o, ST_CONNECT_RUNNING, 0, NULL, 0);
    add_link_advises(o);
    add_requests(o, IT_LINK);
    return dde_run(o);
}

/* to a static object ("Static" protocol) */
OLESTATUS OLEAPI LeObjectConvert(LPOLEOBJECT p, OLE_LPCSTR protocol,
                                            LPOLECLIENT c, LHCLIENTDOC lh, OLE_LPCSTR name,
                                            LPOLEOBJECT FAR *out)
{
    OBJ_OF(p);
    OLESTATUS s;
    if (!protocol || lstrcmp(protocol, "Static"))
        return OLE_ERROR_PROTOCOL;
    if (!pres_has_picture(o))
        return OLE_ERROR_BLANK;
    s = copy_obj(o, c, lh, name, out, OT_STATIC);
    if (s == OLE_OK)
        pres_make_static((LPOBJ)*out);
    return s;
}

OLESTATUS OLEAPI LeGetUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE FAR *u)
{
    OBJ_OF(p);
    if (o->type != OT_LINK)
        return o->type == OT_STATIC ? OLE_ERROR_STATIC : OLE_ERROR_OBJECT;
    if (!u)
        return OLE_ERROR_ADDRESS;
    *u = o->update;
    return OLE_OK;
}

OLESTATUS OLEAPI LeSetUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE u)
{
    OBJ_OF(p);
    if (o->type != OT_LINK)
        return o->type == OT_STATIC ? OLE_ERROR_STATIC : OLE_ERROR_OBJECT;
    if ((WORD)u > oleupdate_oncall)
        return OLE_ERROR_OPTION;
    if (busy_check(o))
        return OLE_BUSY;
    o->update = u;
    return OLE_OK;
}

/* a new name; an embedded object gets a new DDE name number (observed:
   the number after a rename had moved on by one) */
OLESTATUS OLEAPI ObjRename(LPOLEOBJECT p, OLE_LPCSTR name)
{
    OBJ_OF(p);
    if (!name || !*name)
        return OLE_ERROR_NAME;
    gfree(o->hname);
    o->hname = gstr_new(name);
    if (o->type == OT_EMBEDDED) {
        obj_set_number(o);
        if (!o->open) {
            gfree(o->htopic);
            o->htopic = obj_embedded_topic(o);
        }
    }
    return OLE_OK;
}

/* the name and its length; *cb is the buffer size going in */
OLESTATUS OLEAPI ObjQueryName(LPOLEOBJECT p, LPSTR buf, UINT FAR *cb)
{
    OBJ_OF(p);
    LPSTR s;
    WORD n;
    if (!buf || !cb)
        return OLE_ERROR_ADDRESS;
    s = gstr(o->hname);
    n = slen(s);
    if (*cb) {
        WORD k = n < *cb - 1 ? n : *cb - 1;
        hmemcpy(buf, s, k);
        buf[k] = 0;
        *cb = k;
    }
    gstr_unlock(o->hname);
    return OLE_OK;
}

OLESTATUS OLEAPI ObjQueryType(LPOLEOBJECT p, LONG FAR *t)
{
    OBJ_OF(p);
    if (!t)
        return OLE_ERROR_ADDRESS;
    *t = o->type;
    return OLE_OK;
}

OLESTATUS OLEAPI LeQueryBounds(LPOLEOBJECT p, RECT FAR *r)
{
    if (!r)
        return OLE_ERROR_ADDRESS;
    return pres_bounds((LPOBJ)p, r);
}

/* the bytes OleSaveToStream would write (observed equal) */
OLESTATUS OLEAPI ObjQuerySize(LPOLEOBJECT p, DWORD FAR *n)
{
    OBJ_OF(p);
    if (!n)
        return OLE_ERROR_ADDRESS;
    *n = strm_size(o);
    return *n ? OLE_OK : OLE_ERROR_BLANK;
}

OLESTATUS OLEAPI LeQueryOpen(LPOLEOBJECT p)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    return o->open && o->hsrvdoc && IsWindow(o->hsrvdoc) ? OLE_OK : OLE_ERROR_NOT_OPEN;
}

OLESTATUS OLEAPI LeQueryOutOfDate(LPOLEOBJECT p)
{
    (void)p;
    return OLE_OK;
}

OLESTATUS OLEAPI LeQueryReleaseStatus(LPOLEOBJECT p)
{
    return ((LPOBJ)p)->busy ? OLE_BUSY : OLE_OK;
}

OLESTATUS OLEAPI LeQueryReleaseError(LPOLEOBJECT p)
{
    return ((LPOBJ)p)->rel_error;
}

OLE_RELEASE_METHOD OLEAPI LeQueryReleaseMethod(LPOLEOBJECT p)
{
    return ((LPOBJ)p)->rel_method;
}

OLESTATUS OLEAPI LeRequestData(LPOLEOBJECT p, OLECLIPFORMAT f)
{
    OBJ_OF(p);
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (busy_check(o))
        return OLE_BUSY;
    if (!o->open || !o->hsrvdoc)
        return OLE_ERROR_NOT_OPEN;
    dde_start(o, OLE_REQUESTDATA);
    dde_add(o, ST_REQUEST, f, NULL, o->type == OT_LINK ? IT_LINK : IT_NONE);
    dde_add(o, ST_DONE_CHANGED, 0, NULL, 0);
    return dde_run(o);
}

/* UNVERIFIED: the private "object long" slot (a LONG per object kept for
   handlers; flags OF_SET, OF_GET) */
OLESTATUS OLEAPI LeObjectLong(LPOLEOBJECT p, UINT flags, LONG FAR *l)
{
    OBJ_OF(p);
    LONG old = o->objlong;
    if (!l)
        return OLE_ERROR_ADDRESS;
    if (flags & OF_SET)
        o->objlong = *l;
    if (flags & OF_GET)
        *l = old;
    return OLE_OK;
}

/* UNVERIFIED: replace the Native data (handlers) */
OLESTATUS OLEAPI LeChangeData(LPOLEOBJECT p, HANDLE h, LPOLECLIENT c, BOOL del)
{
    OBJ_OF(p);
    (void)c;
    if (!h)
        return OLE_ERROR_HANDLE;
    if (del) {
        gfree(o->hnative);
        o->hnative = h;
    } else {
        HGLOBAL n = copy_global(h);
        if (!n)
            return OLE_ERROR_MEMORY;
        gfree(o->hnative);
        o->hnative = n;
    }
    return OLE_OK;
}

/* the CreateInvisible entry of the vtbl family (UNVERIFIED: unused by the
   vtbl in ole.h; exported as LECREATEINVISIBLE) */
OLESTATUS OLEAPI LeCreateInvisible(LPOLEOBJECT p, BOOL launch)
{
    (void)p;
    (void)launch;
    return OLE_OK;
}

OLESTATUS OLEAPI LeCopy(LPOLEOBJECT p)
{
    return clip_copy((LPOBJ)p);
}

OLEOBJECTVTBL g_vtbl = {
    LeQueryProtocol,
    LeRelease,
    LeShow,
    LeDoVerb,
    LeGetData,
    LeSetData,
    LeSetTargetDevice,
    LeSetBounds,
    LeEnumFormat,
    LeSetColorScheme,
    LeDelete,
    LeSetHostNames,
    LeSaveToStream,
    LeClone,
    LeCopyFromLink,
    LeEqual,
    LeCopyToClipboard,
    LeDraw,
    LeActivate,
    LeExecute,
    LeClose,
    LeUpdate,
    LeReconnect,
    LeObjectConvert,
    LeGetUpdateOptions,
    LeSetUpdateOptions,
    ObjRename,
    ObjQueryName,
    ObjQueryType,
    LeQueryBounds,
    ObjQuerySize,
    LeQueryOpen,
    LeQueryOutOfDate,
    LeQueryReleaseStatus,
    LeQueryReleaseError,
    LeQueryReleaseMethod,
    LeRequestData,
    LeObjectLong,
    LeChangeData
};

/* OLECLI.54, 55 (UNVERIFIED: not observed; a lock keeps the server's
   system conversation) */
OLESTATUS OLEAPI OleLockServer(LPOLEOBJECT p, LHSERVER FAR *lh)
{
    LPOBJ o = obj_check(p);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!lh)
        return OLE_ERROR_ADDRESS;
    *lh = (LHSERVER)o;
    o->locked++;
    return OLE_OK;
}

OLESTATUS OLEAPI OleUnlockServer(LHSERVER lh)
{
    LPOBJ o = obj_check((LPOLEOBJECT)lh);
    if (!o || !o->locked)
        return OLE_ERROR_HANDLE;
    o->locked--;
    return OLE_OK;
}
