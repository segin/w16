/*
 * olecli.c - w16's OLECLI.DLL: objects, client documents and the OLE 1.0
 * client API (ARCHITECTURE.md 16). The OleXxx exports validate the object
 * and call through its vtbl, as the SDK describes object handlers (an
 * object's vtbl may be a handler's); the vtbl entries are the LExxx
 * exports.
 */
#include "olecli.h"

HINSTANCE g_inst;
UINT cf_native, cf_ownerlink, cf_objectlink, cf_binary, cf_link;
WORD g_number;
static LPDOC g_docs;

/* ---- small helpers ---- */

WORD slen(LPCSTR s)
{
    WORD n = 0;
    while (s && s[n])
        n++;
    return n;
}

HGLOBAL gstr_new(LPCSTR s)
{
    WORD n = slen(s);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    LPSTR p;
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    if (s)
        hmemcpy(p, s, n);
    p[n] = 0;
    GlobalUnlock(h);
    return h;
}

/* a string kept in a global block, locked (callers unlock with
   gstr_unlock); "" for none */
LPSTR gstr(HGLOBAL h)
{
    static char empty[1];
    if (!h)
        return empty;
    return (LPSTR)GlobalLock(h);
}

void gstr_unlock(HGLOBAL h)
{
    if (h)
        GlobalUnlock(h);
}

void gfree(HGLOBAL h)
{
    if (h)
        GlobalFree(h);
}

BOOL is_pbrush(LPCSTR cls)
{
    return lstrcmpi(cls, "PBrush") == 0;
}

/* ---- documents ---- */

LPDOC doc_check(LHCLIENTDOC lh)
{
    LPDOC d = (LPDOC)lh;
    LPDOC p;
    if (!lh)
        return NULL;
    for (p = g_docs; p; p = p->next)
        if (p == d)
            return d;
    return NULL;
}

/* OLECLI.41 */
OLESTATUS OLEAPI OleRegisterClientDoc(LPCSTR cls, LPCSTR name, LONG reserved,
                                      LHCLIENTDOC FAR *out)
{
    HGLOBAL h;
    LPDOC d;
    (void)reserved;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    if (!cls || !name || !*cls || !*name)
        return OLE_ERROR_NAME;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, sizeof(DOC));
    if (!h)
        return OLE_ERROR_MEMORY;
    d = (LPDOC)GlobalLock(h);
    d->magic = DOC_MAGIC;
    d->hself = h;
    d->hclass = gstr_new(cls);
    d->hname = gstr_new(name);
    d->next = g_docs;
    g_docs = d;
    *out = (LHCLIENTDOC)d;
    return OLE_OK;
}

/* OLECLI.42 */
OLESTATUS OLEAPI OleRevokeClientDoc(LHCLIENTDOC lh)
{
    LPDOC d = doc_check(lh), FAR *pp;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (d->first)
        return OLE_ERROR_NOT_EMPTY;
    for (pp = &g_docs; *pp; pp = &(*pp)->next)
        if (*pp == d) {
            *pp = d->next;
            break;
        }
    gfree(d->hclass);
    gfree(d->hname);
    d->magic = 0;
    GlobalUnlock(d->hself);
    GlobalFree(d->hself);
    return OLE_OK;
}

/* OLECLI.43: the objects' DDE names use the new name from now on
   (observed: no DDE traffic for objects that are not open) */
OLESTATUS OLEAPI OleRenameClientDoc(LHCLIENTDOC lh, LPCSTR name)
{
    LPDOC d = doc_check(lh);
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!name || !*name)
        return OLE_ERROR_NAME;
    gfree(d->hname);
    d->hname = gstr_new(name);
    return OLE_OK;
}

/* OLECLI.44 */
OLESTATUS OLEAPI OleRevertClientDoc(LHCLIENTDOC lh)
{
    return doc_check(lh) ? OLE_OK : OLE_ERROR_HANDLE;
}

/* OLECLI.45 */
OLESTATUS OLEAPI OleSavedClientDoc(LHCLIENTDOC lh)
{
    return doc_check(lh) ? OLE_OK : OLE_ERROR_HANDLE;
}

/* OLECLI.47: the objects in the order they were made; *obj NULL starts,
   NULL at the end (observed, with OLE_OK) */
OLESTATUS OLEAPI OleEnumObjects(LHCLIENTDOC lh, LPOLEOBJECT FAR *obj)
{
    LPDOC d = doc_check(lh);
    LPOBJ o;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!obj)
        return OLE_ERROR_ADDRESS;
    if (!*obj)
        o = d->first;
    else {
        o = obj_check(*obj);
        if (!o || o->doc != d)
            return OLE_ERROR_OBJECT;
        o = o->next;
    }
    *obj = (LPOLEOBJECT)o;
    return OLE_OK;
}

/* ---- objects ---- */

LPOBJ obj_check(LPOLEOBJECT p)
{
    LPOBJ o = (LPOBJ)p;
    if (!p || IsBadReadPtr(p, sizeof(OBJ)) || o->magic != OBJ_MAGIC)
        return NULL;
    return o;
}

LPOBJ obj_new(LPOLECLIENT client, LPDOC doc, LPCSTR name)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, sizeof(OBJ));
    LPOBJ o, FAR *pp;
    if (!h)
        return NULL;
    o = (LPOBJ)GlobalLock(h);
    o->lpvtbl = &g_vtbl;
    o->magic = OBJ_MAGIC;
    o->hself = h;
    o->client = client;
    o->doc = doc;
    o->hname = gstr_new(name);
    o->render = olerender_draw;
    o->update = oleupdate_always;
    o->rel_error = OLE_OK;
    /* appended: OleEnumObjects gives the objects in creation order */
    for (pp = &doc->first; *pp; pp = &(*pp)->next)
        ;
    *pp = o;
    return o;
}

void obj_free(LPOBJ o)
{
    LPOBJ FAR *pp;
    if (o->doc)
        for (pp = &o->doc->first; *pp; pp = &(*pp)->next)
            if (*pp == o) {
                *pp = o->next;
                break;
            }
    pres_free(o);
    gfree(o->hnative);
    gfree(o->hclass);
    gfree(o->htopic);
    gfree(o->hitem);
    gfree(o->hname);
    gfree(o->hnet);
    gfree(o->hhost);
    gfree(o->hlinkdata);
    gfree(o->hsteps);
    o->hpres = o->hnative = o->hclass = o->htopic = o->hitem = NULL;
    o->hname = o->hnet = o->hhost = o->hlinkdata = o->hsteps = NULL;
    o->nsteps = o->cur = 0;
    o->wait = 0;
    o->busy = FALSE;
    o->magic = 0;
    if (o->depth) {
        /* deleted from a callback: the code that made the callback still
           reads the object, so its block stays until OleWndProc returns
           (the API refuses it: its magic is gone) */
        o->dead = TRUE;
        return;
    }
    obj_free_self(o);
}

void obj_free_self(LPOBJ o)
{
    HGLOBAL h = o->hself;
    GlobalUnlock(h);
    GlobalFree(h);
}

/* An embedded object's DDE document name, "<doc class>%<doc name>%<object
   name>#<nnn>" (observed: "OC1%OCDOC%obj0#001"): nnn counts the embedded
   objects OLECLI has named (created, loaded or renamed), from 1, three
   digits at least. */
void obj_set_number(LPOBJ o)
{
    o->number = ++g_number;
}

static HGLOBAL embedded_topic(LPOBJ o)
{
    char num[8];
    WORD n = o->number, i = 0, k;
    LPSTR a, b, c, p;
    HGLOBAL h;
    char t[8];
    do {
        t[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i < 3)
        t[i++] = '0';
    for (k = 0; k < i; k++)
        num[k] = t[i - 1 - k];
    num[i] = 0;
    a = gstr(o->doc->hclass);
    b = gstr(o->doc->hname);
    c = gstr(o->hname);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, slen(a) + slen(b) + slen(c) + i + 4);
    if (h) {
        p = (LPSTR)GlobalLock(h);
        lstrcpy(p, a);
        lstrcat(p, "%");
        lstrcat(p, b);
        lstrcat(p, "%");
        lstrcat(p, c);
        lstrcat(p, "#");
        lstrcat(p, num);
        GlobalUnlock(h);
    }
    gstr_unlock(o->doc->hclass);
    gstr_unlock(o->doc->hname);
    gstr_unlock(o->hname);
    return h;
}

/* the DDE topic an embedded object goes by now */
HGLOBAL obj_embedded_topic(LPOBJ o)
{
    return embedded_topic(o);
}

void callback(LPOBJ o, OLE_NOTIFICATION n)
{
    if (!o->dead && o->client && o->client->lpvtbl && o->client->lpvtbl->CallBack)
        o->client->lpvtbl->CallBack(o->client, n, (LPOLEOBJECT)o);
}

/* ---- the API: validate, then the object's vtbl ---- */

#define VALID(p) LPOBJ o = obj_check(p); if (!o) return OLE_ERROR_OBJECT

/* OLECLI.2 */
OLESTATUS OLEAPI OleDelete(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->Delete(p);
}

/* OLECLI.40 */
OLESTATUS OLEAPI OleRelease(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->Release(p);
}

/* OLECLI.3 */
OLESTATUS OLEAPI OleSaveToStream(LPOLEOBJECT p, LPOLESTREAM s)
{
    VALID(p);
    if (!s || IsBadReadPtr(s, sizeof *s))
        return OLE_ERROR_STREAM;
    return o->lpvtbl->SaveToStream(p, s);
}

/* OLECLI.4 */
OLESTATUS OLEAPI OleLoadFromStream(LPOLESTREAM s, LPCSTR protocol, LPOLECLIENT c,
                                   LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out)
{
    LPDOC d = doc_check(lh);
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!s)
        return OLE_ERROR_STREAM;
    if (!protocol || (lstrcmp(protocol, "StdFileEditing") && lstrcmp(protocol, "Static")))
        return OLE_ERROR_PROTOCOL;
    return strm_load(s, c, d, name, out);
}

/* OLECLI.6 */
OLESTATUS OLEAPI OleClone(LPOLEOBJECT p, LPOLECLIENT c, LHCLIENTDOC lh, LPCSTR name,
                          LPOLEOBJECT FAR *out)
{
    VALID(p);
    return o->lpvtbl->Clone(p, c, lh, name, out);
}

/* OLECLI.7 */
OLESTATUS OLEAPI OleCopyFromLink(LPOLEOBJECT p, LPCSTR protocol, LPOLECLIENT c,
                                 LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out)
{
    VALID(p);
    (void)protocol;
    return o->lpvtbl->CopyFromLink(p, c, lh, name, out);
}

/* OLECLI.8 */
OLESTATUS OLEAPI OleEqual(LPOLEOBJECT a, LPOLEOBJECT b)
{
    LPOBJ o = obj_check(a);
    if (!o || !obj_check(b))
        return OLE_ERROR_OBJECT;
    return o->lpvtbl->Equal(a, b);
}

/* OLECLI.13 */
OLESTATUS OLEAPI OleCopyToClipboard(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->CopyToClipboard(p);
}

/* OLECLI.14 */
OLESTATUS OLEAPI OleQueryType(LPOLEOBJECT p, LONG FAR *t)
{
    VALID(p);
    return o->lpvtbl->QueryType(p, t);
}

/* OLECLI.15 */
OLESTATUS OLEAPI OleSetHostNames(LPOLEOBJECT p, LPCSTR app, LPCSTR doc)
{
    VALID(p);
    return o->lpvtbl->SetHostNames(p, app, doc);
}

/* OLECLI.16 */
OLESTATUS OLEAPI OleSetTargetDevice(LPOLEOBJECT p, HGLOBAL h)
{
    VALID(p);
    return o->lpvtbl->SetTargetDevice(p, h);
}

/* OLECLI.17 */
OLESTATUS OLEAPI OleSetBounds(LPOLEOBJECT p, const RECT FAR *r)
{
    VALID(p);
    return o->lpvtbl->SetBounds(p, r);
}

/* OLECLI.18 */
OLESTATUS OLEAPI OleQueryBounds(LPOLEOBJECT p, RECT FAR *r)
{
    VALID(p);
    return o->lpvtbl->QueryBounds(p, r);
}

/* OLECLI.19 */
OLESTATUS OLEAPI OleDraw(LPOLEOBJECT p, HDC dc, const RECT FAR *r, const RECT FAR *wnd,
                         HDC fmt)
{
    VALID(p);
    return o->lpvtbl->Draw(p, dc, r, wnd, fmt);
}

/* OLECLI.20 */
OLESTATUS OLEAPI OleQueryOpen(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->QueryOpen(p);
}

/* OLECLI.21 */
OLESTATUS OLEAPI OleActivate(LPOLEOBJECT p, UINT verb, BOOL show, BOOL focus, HWND w,
                             const RECT FAR *r)
{
    VALID(p);
    return o->lpvtbl->Activate(p, verb, show, focus, w, r);
}

/* OLECLI.22 */
OLESTATUS OLEAPI OleUpdate(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->Update(p);
}

/* OLECLI.23 */
OLESTATUS OLEAPI OleReconnect(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->Reconnect(p);
}

/* OLECLI.24 */
OLESTATUS OLEAPI OleGetLinkUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE FAR *u)
{
    VALID(p);
    return o->lpvtbl->GetLinkUpdateOptions(p, u);
}

/* OLECLI.25 */
OLESTATUS OLEAPI OleSetLinkUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE u)
{
    VALID(p);
    return o->lpvtbl->SetLinkUpdateOptions(p, u);
}

/* OLECLI.26: 0 for an invalid object */
OLECLIPFORMAT OLEAPI OleEnumFormats(LPOLEOBJECT p, OLECLIPFORMAT f)
{
    LPOBJ o = obj_check(p);
    if (!o)
        return 0;
    return o->lpvtbl->EnumFormats(p, f);
}

/* OLECLI.27 */
OLESTATUS OLEAPI OleClose(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->Close(p);
}

/* OLECLI.28 */
OLESTATUS OLEAPI OleGetData(LPOLEOBJECT p, OLECLIPFORMAT f, HANDLE FAR *h)
{
    VALID(p);
    return o->lpvtbl->GetData(p, f, h);
}

/* OLECLI.29 */
OLESTATUS OLEAPI OleSetData(LPOLEOBJECT p, OLECLIPFORMAT f, HANDLE h)
{
    VALID(p);
    return o->lpvtbl->SetData(p, f, h);
}

/* OLECLI.30: OLE_OK for the protocols the object speaks (the vtbl's
   QueryProtocol returns the object, or NULL) */
OLESTATUS OLEAPI OleQueryProtocol(LPOLEOBJECT p, LPCSTR protocol)
{
    VALID(p);
    return o->lpvtbl->QueryProtocol(p, (OLE_LPCSTR)protocol) ? OLE_OK : OLE_ERROR_PROTOCOL;
}

/* OLECLI.31 */
OLESTATUS OLEAPI OleQueryOutOfDate(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->QueryOutOfDate(p);
}

/* OLECLI.32 */
OLESTATUS OLEAPI OleObjectConvert(LPOLEOBJECT p, LPCSTR protocol, LPOLECLIENT c,
                                  LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out)
{
    VALID(p);
    return o->lpvtbl->ObjectConvert(p, (OLE_LPCSTR)protocol, c, lh, (OLE_LPCSTR)name, out);
}

/* OLECLI.35 */
OLESTATUS OLEAPI OleQueryReleaseStatus(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->QueryReleaseStatus(p);
}

/* OLECLI.36 */
OLESTATUS OLEAPI OleQueryReleaseError(LPOLEOBJECT p)
{
    VALID(p);
    return o->lpvtbl->QueryReleaseError(p);
}

/* OLECLI.37 */
OLE_RELEASE_METHOD OLEAPI OleQueryReleaseMethod(LPOLEOBJECT p)
{
    LPOBJ o = obj_check(p);
    if (!o)
        return (OLE_RELEASE_METHOD)OLE_ERROR_OBJECT;
    return o->lpvtbl->QueryReleaseMethod(p);
}

/* OLECLI.46 */
OLESTATUS OLEAPI OleRename(LPOLEOBJECT p, LPCSTR name)
{
    VALID(p);
    return o->lpvtbl->Rename(p, (OLE_LPCSTR)name);
}

/* OLECLI.48 */
OLESTATUS OLEAPI OleQueryName(LPOLEOBJECT p, LPSTR buf, UINT FAR *cb)
{
    VALID(p);
    return o->lpvtbl->QueryName(p, buf, cb);
}

/* OLECLI.49 */
OLESTATUS OLEAPI OleSetColorScheme(LPOLEOBJECT p, const LOGPALETTE FAR *pal)
{
    VALID(p);
    return o->lpvtbl->SetColorScheme(p, (OLE_CONST LOGPALETTE FAR *)pal);
}

/* OLECLI.50 */
OLESTATUS OLEAPI OleRequestData(LPOLEOBJECT p, OLECLIPFORMAT f)
{
    VALID(p);
    return o->lpvtbl->RequestData(p, f);
}

/* OLECLI.56 */
OLESTATUS OLEAPI OleQuerySize(LPOLEOBJECT p, DWORD FAR *n)
{
    VALID(p);
    return o->lpvtbl->QuerySize(p, n);
}

/* OLECLI.57 */
OLESTATUS OLEAPI OleExecute(LPOLEOBJECT p, HGLOBAL h, UINT reserved)
{
    VALID(p);
    return o->lpvtbl->Execute(p, h, reserved);
}

/* OLECLI.59: the version of OLE (observed 0x00001001: 1.10) */
DWORD OLEAPI OleQueryClientVersion(void)
{
    return 0x00001001L;
}

/* OLECLI.60: is the DC a metafile DC (GetDeviceCaps' technology) */
BOOL OLEAPI OleIsDcMeta(HDC dc)
{
    return GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE;
}

#ifdef OLETRACE
void olecli_trace(LPCSTR fmt, ...)
{
    char b[300];
    int n = wvsprintf(b, fmt, (LPSTR)(&fmt + 1));
    HFILE f = _lopen("C:\\OLECLI.LOG", OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\OLECLI.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, b, n);
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}
#endif
