/*
 * os1.c - OS1.EXE, a logging OLE 1.0 server (black-box probe of OLESVR).
 *
 * Registers the class given by /C:<class> (default "OS1Probe") with
 * OleRegisterServer and implements the server, document and object
 * vtables of the Windows 3.1 SDK. Every callback OLESVR makes is logged
 * through OSSPY.DLL (C:\OS.LOG) with its arguments and what OS1 returns,
 * between the DDE messages the spy logs, so the log shows which message
 * led to which callback.
 *
 * Command line: [/Embedding|-Embedding] [/C:class] [/S] [/R:name=code]...
 * [file]
 *   /S              register OLE_SERVER_SINGLE (default MULTI)
 *   /R:Name=code    make callback Name (e.g. Create, GetObject, Save)
 *                   return OLESTATUS code instead of its normal result
 *   file            register it as an open document (as a server does
 *                   when the user opens a file)
 * After start-up OS1 runs the script C:\OS1.TXT if there is one (one
 * command per line, see step()), from a timer, while serving clients.
 *
 * Behaves like a well-behaved SDK server: Exit and Close revoke, and the
 * program quits on Release of the server once it has been revoked.
 */
#include <windows.h>
#include <ole.h>
#include "osspy.h"

#define P OsPrintf

static HINSTANCE inst;
static HWND mainwnd;
static char cls_name[64] = "OS1Probe";
static LHSERVER lhsrv;
static BOOL revoking, embedding, quitting;

/* ---- forced return codes ---- */

#define NFORCE 16
static struct { char name[24]; int code; } force[NFORCE];
static int nforce;

static int forced(const char *name, int normal)
{
    int i;
    for (i = 0; i < nforce; i++)
        if (!lstrcmpi(force[i].name, name))
            return force[i].code;
    return normal;
}

/* ---- documents and objects ---- */

typedef struct {
    OLESERVERDOC d;
    LHSERVERDOC lh;
    char name[128];
    BOOL used, revoking;
    int n;
} DOC;

typedef struct {
    OLEOBJECT o;
    DOC *doc;
    LPOLECLIENT client;
    char item[64];
    BOOL used;
    int n;
    HGLOBAL native;
} OBJ;

#define NDOC 16
#define NOBJ 32
static DOC docs[NDOC];
static OBJ objs[NOBJ];
static OLESERVER srv;
static OLESERVERVTBL srvvt;
static OLESERVERDOCVTBL docvt;
static OLEOBJECTVTBL objvt;
static int ndoc_made, nobj_made;

static DOC *newdoc(const char FAR *name)
{
    int i;
    for (i = 0; i < NDOC; i++)
        if (!docs[i].used) {
            DOC *d = &docs[i];
            d->used = TRUE;
            d->revoking = FALSE;
            d->d.lpvtbl = &docvt;
            d->lh = 0;
            d->n = ++ndoc_made;
            lstrcpy(d->name, name ? name : "");
            return d;
        }
    return NULL;
}

static int docno(LPOLESERVERDOC p)
{
    return p ? ((DOC *)p)->n : 0;
}

static int objno(LPOLEOBJECT p)
{
    return p ? ((OBJ *)p)->n : 0;
}

static const char *stname(int s)
{
    switch (s) {
    case OLE_OK: return "OK";
    case OLE_WAIT_FOR_RELEASE: return "WAIT_FOR_RELEASE";
    case OLE_BUSY: return "BUSY";
    case OLE_ERROR_MEMORY: return "ERROR_MEMORY";
    case OLE_ERROR_PROTECT_ONLY: return "ERROR_PROTECT_ONLY";
    case OLE_ERROR_FORMAT: return "ERROR_FORMAT";
    case OLE_ERROR_OBJECT: return "ERROR_OBJECT";
    case OLE_ERROR_HANDLE: return "ERROR_HANDLE";
    case OLE_ERROR_GENERIC: return "ERROR_GENERIC";
    case OLE_ERROR_CLASS: return "ERROR_CLASS";
    case OLE_ERROR_NAME: return "ERROR_NAME";
    case OLE_ERROR_SERVER_BLOCKED: return "ERROR_SERVER_BLOCKED";
    case OLE_ERROR_ALREADY_REGISTERED: return "ERROR_ALREADY_REGISTERED";
    case OLE_ERROR_ADDRESS: return "ERROR_ADDRESS";
    case OLE_ERROR_NEW: return "ERROR_NEW";
    case OLE_ERROR_EDIT: return "ERROR_EDIT";
    case OLE_ERROR_OPEN: return "ERROR_OPEN";
    case OLE_ERROR_TEMPLATE: return "ERROR_TEMPLATE";
    case OLE_ERROR_COMMAND: return "ERROR_COMMAND";
    case OLE_ERROR_SHOW: return "ERROR_SHOW";
    case OLE_ERROR_DOVERB: return "ERROR_DOVERB";
    case OLE_ERROR_PROTOCOL: return "ERROR_PROTOCOL";
    case OLE_ERROR_TERMINATE: return "ERROR_TERMINATE";
    case OLE_ERROR_REGISTRATION: return "ERROR_REGISTRATION";
    case OLE_ERROR_TASK: return "ERROR_TASK";
    case OLE_ERROR_DATATYPE: return "ERROR_DATATYPE";
    }
    return "?";
}

#define RET(name, normal) do { int r_ = forced(name, normal); \
    P("  -> %d %s", r_, (const char FAR *)stname(r_)); return r_; } while (0)

/* ---- server vtable ---- */

static OLESTATUS register_doc_result(DOC *d, LHSERVERDOC lh, LPOLESERVERDOC FAR *pp)
{
    d->lh = lh;
    *pp = &d->d;
    return OLE_OK;
}

OLESTATUS CALLBACK __export SrvOpen(LPOLESERVER s, LHSERVERDOC lh, OLE_LPCSTR name,
                                    LPOLESERVERDOC FAR *pp)
{
    DOC *d;
    P("CB Server.Open srv=%s lhdoc=%s name=%q", s == &srv ? "ok" : "?",
      lh ? "nz" : "0", name);
    d = newdoc(name);
    if (forced("Open", 0) || !d)
        RET("Open", OLE_ERROR_OPEN);
    register_doc_result(d, lh, pp);
    P("  doc=%d", d->n);
    RET("Open", OLE_OK);
}

OLESTATUS CALLBACK __export SrvCreate(LPOLESERVER s, LHSERVERDOC lh, OLE_LPCSTR cls,
                                      OLE_LPCSTR name, LPOLESERVERDOC FAR *pp)
{
    DOC *d;
    P("CB Server.Create srv=%s lhdoc=%s class=%q name=%q", s == &srv ? "ok" : "?",
      lh ? "nz" : "0", cls, name);
    d = newdoc(name);
    if (forced("Create", 0) || !d)
        RET("Create", OLE_ERROR_NEW);
    register_doc_result(d, lh, pp);
    P("  doc=%d", d->n);
    RET("Create", OLE_OK);
}

OLESTATUS CALLBACK __export SrvCreateFromTemplate(LPOLESERVER s, LHSERVERDOC lh,
        OLE_LPCSTR cls, OLE_LPCSTR name, OLE_LPCSTR tmpl, LPOLESERVERDOC FAR *pp)
{
    DOC *d;
    P("CB Server.CreateFromTemplate srv=%s lhdoc=%s class=%q name=%q template=%q",
      s == &srv ? "ok" : "?", lh ? "nz" : "0", cls, name, tmpl);
    d = newdoc(name);
    if (forced("CreateFromTemplate", 0) || !d)
        RET("CreateFromTemplate", OLE_ERROR_TEMPLATE);
    register_doc_result(d, lh, pp);
    P("  doc=%d", d->n);
    RET("CreateFromTemplate", OLE_OK);
}

OLESTATUS CALLBACK __export SrvEdit(LPOLESERVER s, LHSERVERDOC lh, OLE_LPCSTR cls,
                                    OLE_LPCSTR name, LPOLESERVERDOC FAR *pp)
{
    DOC *d;
    P("CB Server.Edit srv=%s lhdoc=%s class=%q name=%q", s == &srv ? "ok" : "?",
      lh ? "nz" : "0", cls, name);
    d = newdoc(name);
    if (forced("Edit", 0) || !d)
        RET("Edit", OLE_ERROR_EDIT);
    register_doc_result(d, lh, pp);
    P("  doc=%d", d->n);
    RET("Edit", OLE_OK);
}

OLESTATUS CALLBACK __export SrvExit(LPOLESERVER s)
{
    OLESTATUS r;
    P("CB Server.Exit srv=%s", s == &srv ? "ok" : "?");
    if (forced("Exit", 0))
        RET("Exit", 0);
    revoking = TRUE;
    r = OleRevokeServer(lhsrv);
    P("  OleRevokeServer -> %d %s", r, (const char FAR *)stname(r));
    RET("Exit", OLE_OK);
}

OLESTATUS CALLBACK __export SrvRelease(LPOLESERVER s)
{
    P("CB Server.Release srv=%s revoking=%d", s == &srv ? "ok" : "?", revoking);
    if (revoking && !quitting) {
        quitting = TRUE;
        PostMessage(mainwnd, WM_CLOSE, 0, 0);
    }
    RET("SrvRelease", OLE_OK);
}

OLESTATUS CALLBACK __export SrvExecute(LPOLESERVER s, HGLOBAL h)
{
    LPSTR p = h ? GlobalLock(h) : NULL;
    P("CB Server.Execute srv=%s cmds=%q", s == &srv ? "ok" : "?", (LPCSTR)p);
    if (p)
        GlobalUnlock(h);
    RET("SrvExecute", OLE_OK);
}

/* ---- document vtable ---- */

static void revoke_doc(DOC *d)
{
    OLESTATUS r;
    d->revoking = TRUE;
    r = OleRevokeServerDoc(d->lh);
    P("  OleRevokeServerDoc doc=%d -> %d %s", d->n, r, (const char FAR *)stname(r));
}

OLESTATUS CALLBACK __export DocSave(LPOLESERVERDOC d)
{
    P("CB Doc.Save doc=%d", docno(d));
    RET("Save", OLE_OK);
}

OLESTATUS CALLBACK __export DocClose(LPOLESERVERDOC d)
{
    P("CB Doc.Close doc=%d", docno(d));
    if (!forced("Close", 0))
        revoke_doc((DOC *)d);
    RET("Close", OLE_OK);
}

OLESTATUS CALLBACK __export DocSetHostNames(LPOLESERVERDOC d, OLE_LPCSTR client, OLE_LPCSTR doc)
{
    P("CB Doc.SetHostNames doc=%d client=%q clientdoc=%q", docno(d), client, doc);
    RET("SetHostNames", OLE_OK);
}

OLESTATUS CALLBACK __export DocSetDocDimensions(LPOLESERVERDOC d, OLE_CONST RECT FAR *r)
{
    if (r)
        P("CB Doc.SetDocDimensions doc=%d rect=%d,%d,%d,%d", docno(d), r->left, r->top,
          r->right, r->bottom);
    else
        P("CB Doc.SetDocDimensions doc=%d rect=NULL", docno(d));
    RET("SetDocDimensions", OLE_OK);
}

OLESTATUS CALLBACK __export DocGetObject(LPOLESERVERDOC d, OLE_LPCSTR item,
                                         LPOLEOBJECT FAR *pp, LPOLECLIENT client)
{
    DOC *doc = (DOC *)d;
    int i;
    P("CB Doc.GetObject doc=%d item=%q client=%s", docno(d), item, client ? "nz" : "NULL");
    if (forced("GetObject", 0))
        RET("GetObject", 0);
    /* the same item again: a server may hand out the same object (it must
       then remember every client); OS1 makes a new one each time */
    for (i = 0; i < NOBJ; i++)
        if (!objs[i].used)
            break;
    if (i == NOBJ)
        RET("GetObject", OLE_ERROR_MEMORY);
    objs[i].used = TRUE;
    objs[i].o.lpvtbl = &objvt;
    objs[i].doc = doc;
    objs[i].client = client;
    objs[i].n = ++nobj_made;
    objs[i].native = NULL;
    lstrcpy(objs[i].item, item ? item : "");
    *pp = &objs[i].o;
    P("  obj=%d", objs[i].n);
    RET("GetObject", OLE_OK);
}

OLESTATUS CALLBACK __export DocRelease(LPOLESERVERDOC d)
{
    DOC *doc = (DOC *)d;
    P("CB Doc.Release doc=%d", docno(d));
    doc->used = FALSE;
    RET("DocRelease", OLE_OK);
}

OLESTATUS CALLBACK __export DocSetColorScheme(LPOLESERVERDOC d, OLE_CONST LOGPALETTE FAR *pal)
{
    P("CB Doc.SetColorScheme doc=%d pal=%s n=%d", docno(d), pal ? "nz" : "NULL",
      pal ? pal->palNumEntries : 0);
    RET("DocSetColorScheme", OLE_OK);
}

OLESTATUS CALLBACK __export DocExecute(LPOLESERVERDOC d, HGLOBAL h)
{
    LPSTR p = h ? GlobalLock(h) : NULL;
    P("CB Doc.Execute doc=%d cmds=%q", docno(d), (LPCSTR)p);
    if (p)
        GlobalUnlock(h);
    RET("DocExecute", OLE_OK);
}

/* ---- object vtable ---- */

static OLECLIPFORMAT cf_native, cf_ownerlink, cf_objectlink;

void FAR * CALLBACK __export ObjQueryProtocol(LPOLEOBJECT o, OLE_LPCSTR proto)
{
    P("CB Obj.QueryProtocol obj=%d protocol=%q", objno(o), proto);
    if (forced("QueryProtocol", 0)) {
        P("  -> NULL");
        return NULL;
    }
    P("  -> obj");
    return o;
}

OLESTATUS CALLBACK __export ObjRelease(LPOLEOBJECT o)
{
    OBJ *ob = (OBJ *)o;
    P("CB Obj.Release obj=%d", objno(o));
    ob->used = FALSE;
    if (ob->native)
        GlobalFree(ob->native);
    ob->native = NULL;
    RET("ObjRelease", OLE_OK);
}

OLESTATUS CALLBACK __export ObjShow(LPOLEOBJECT o, BOOL focus)
{
    P("CB Obj.Show obj=%d takefocus=%d", objno(o), focus);
    ShowWindow(mainwnd, SW_SHOWNORMAL);
    RET("Show", OLE_OK);
}

OLESTATUS CALLBACK __export ObjDoVerb(LPOLEOBJECT o, UINT verb, BOOL show, BOOL focus)
{
    P("CB Obj.DoVerb obj=%d verb=%u show=%d takefocus=%d", objno(o), verb, show, focus);
    if (show)
        ShowWindow(mainwnd, SW_SHOWNORMAL);
    RET("DoVerb", OLE_OK);
}

static HGLOBAL make_text(const char FAR *a, const char FAR *b, const char FAR *c, BOOL list)
{
    WORD n = lstrlen(a) + lstrlen(b) + lstrlen(c) + 4;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n);
    LPSTR p = h ? GlobalLock(h) : NULL;
    if (!p)
        return NULL;
    lstrcpy(p, a);
    if (list) {
        p += lstrlen(p) + 1;
        lstrcpy(p, b);
        p += lstrlen(p) + 1;
        lstrcpy(p, c);
    }
    GlobalUnlock(h);
    return h;
}

static HGLOBAL make_picture(void)
{
    HDC dc = CreateMetaFile(NULL);
    HMETAFILE mf;
    HGLOBAL h;
    METAFILEPICT FAR *mp;
    if (!dc)
        return NULL;
    SetWindowOrg(dc, 0, 0);
    SetWindowExt(dc, 100, 50);
    Rectangle(dc, 0, 0, 100, 50);
    mf = CloseMetaFile(dc);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(METAFILEPICT));
    mp = h ? (METAFILEPICT FAR *)GlobalLock(h) : NULL;
    if (!mp)
        return NULL;
    mp->mm = MM_ANISOTROPIC;
    mp->xExt = 2000;
    mp->yExt = 1000;
    mp->hMF = mf;
    GlobalUnlock(h);
    return h;
}

OLESTATUS CALLBACK __export ObjGetData(LPOLEOBJECT o, OLECLIPFORMAT cf, HANDLE FAR *ph)
{
    OBJ *ob = (OBJ *)o;
    char fmt[40];
    HGLOBAL h = NULL;
    fmt[0] = 0;
    if (cf >= 0xC000)
        GetClipboardFormatName(cf, fmt, sizeof fmt);
    P("CB Obj.GetData obj=%d fmt=%u %s", objno(o), cf, (LPCSTR)fmt);
    if (cf == cf_native) {
        if (ob->native) {
            DWORD n = GlobalSize(ob->native);
            BYTE FAR *s = GlobalLock(ob->native), FAR *t;
            h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
            t = h ? GlobalLock(h) : NULL;
            if (t) {
                while (n--)
                    t[(WORD)n] = s[(WORD)n];
                GlobalUnlock(h);
            }
            GlobalUnlock(ob->native);
        } else
            h = make_text("OS1NATIVE", "", "", FALSE);
    } else if (cf == cf_ownerlink || cf == cf_objectlink)
        h = make_text(cls_name, ob->doc ? ob->doc->name : "", ob->item, TRUE);
    else if (cf == CF_METAFILEPICT)
        h = make_picture();
    else if (cf == CF_TEXT)
        h = make_text("OS1 text", "", "", FALSE);
    else
        RET("GetData", OLE_ERROR_FORMAT);
    *ph = h;
    RET("GetData", h ? OLE_OK : OLE_ERROR_MEMORY);
}

OLESTATUS CALLBACK __export ObjSetData(LPOLEOBJECT o, OLECLIPFORMAT cf, HANDLE h)
{
    OBJ *ob = (OBJ *)o;
    char fmt[40];
    fmt[0] = 0;
    if (cf >= 0xC000)
        GetClipboardFormatName(cf, fmt, sizeof fmt);
    P("CB Obj.SetData obj=%d fmt=%u %s h=%s", objno(o), cf, (LPCSTR)fmt, h ? "nz" : "0");
    if (h)
        OsDumpData(cf, (HGLOBAL)h, 0);
    if (cf == cf_native && h) {
        if (ob->native)
            GlobalFree(ob->native);
        ob->native = (HGLOBAL)h;        /* the server owns it now */
    } else if (h && !forced("KeepSetData", 0))
        GlobalFree((HGLOBAL)h);
    RET("SetData", OLE_OK);
}

OLESTATUS CALLBACK __export ObjSetTargetDevice(LPOLEOBJECT o, HGLOBAL h)
{
    P("CB Obj.SetTargetDevice obj=%d h=%s", objno(o), h ? "nz" : "0");
    if (h)
        OsDumpData(0, h, 0);
    RET("SetTargetDevice", OLE_OK);
}

OLESTATUS CALLBACK __export ObjSetBounds(LPOLEOBJECT o, OLE_CONST RECT FAR *r)
{
    if (r)
        P("CB Obj.SetBounds obj=%d rect=%d,%d,%d,%d", objno(o), r->left, r->top, r->right,
          r->bottom);
    else
        P("CB Obj.SetBounds obj=%d rect=NULL", objno(o));
    RET("SetBounds", OLE_OK);
}

OLECLIPFORMAT CALLBACK __export ObjEnumFormats(LPOLEOBJECT o, OLECLIPFORMAT cf)
{
    OLECLIPFORMAT next;
    if (cf == 0)
        next = cf_native;
    else if (cf == cf_native)
        next = cf_ownerlink;
    else if (cf == cf_ownerlink)
        next = cf_objectlink;
    else if (cf == cf_objectlink)
        next = CF_METAFILEPICT;
    else
        next = 0;
    P("CB Obj.EnumFormats obj=%d fmt=%u -> %u", objno(o), cf, next);
    return next;
}

OLESTATUS CALLBACK __export ObjSetColorScheme(LPOLEOBJECT o, OLE_CONST LOGPALETTE FAR *pal)
{
    P("CB Obj.SetColorScheme obj=%d pal=%s n=%d", objno(o), pal ? "nz" : "NULL",
      pal ? pal->palNumEntries : 0);
    RET("ObjSetColorScheme", OLE_OK);
}

/* ---- script ---- */

static char script[4096];
static char *next_line;
static DWORD wait_until;

static int notify(int what)
{
    int i, n = 0;
    for (i = 0; i < NOBJ; i++)
        if (objs[i].used && objs[i].client) {
            int r = objs[i].client->lpvtbl->CallBack(objs[i].client, what, &objs[i].o);
            P("  CallBack obj=%d %d -> %d", objs[i].n, what, r);
            n++;
        }
    return n;
}

static DOC *curdoc(void)
{
    int i;
    for (i = NDOC; i-- > 0;)
        if (docs[i].used && docs[i].lh)
            return &docs[i];
    return NULL;
}

static void step(char *l)
{
    char *arg = l;
    OLESTATUS r;
    DOC *d;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = 0;
    if (!*l || *l == ';')
        return;
    P("OS1 step %s %s%s%s", (LPCSTR)l, (LPCSTR)arg, (LPCSTR)(lstrcmp(cls_name, "OS1Probe") ? " @" : ""),
      (LPCSTR)(lstrcmp(cls_name, "OS1Probe") ? cls_name : ""));
    if (!lstrcmpi(l, "wait")) {
        WORD ms = 0;
        while (*arg >= '0' && *arg <= '9')
            ms = ms * 10 + (*arg++ - '0');
        wait_until = GetTickCount() + ms;
    } else if (!lstrcmpi(l, "regdoc")) {
        LHSERVERDOC lh = 0;
        d = newdoc(arg);
        r = OleRegisterServerDoc(lhsrv, arg, &d->d, &lh);
        d->lh = lh;
        P("  OleRegisterServerDoc %q -> %d %s lh=%s", (LPCSTR)arg, r,
          (const char FAR *)stname(r), lh ? "nz" : "0");
    } else if (!lstrcmpi(l, "revokedoc") && (d = curdoc()) != NULL) {
        revoke_doc(d);
    } else if (!lstrcmpi(l, "renamedoc") && (d = curdoc()) != NULL) {
        r = OleRenameServerDoc(d->lh, arg);
        lstrcpy(d->name, arg);
        P("  OleRenameServerDoc -> %d %s", r, (const char FAR *)stname(r));
    } else if (!lstrcmpi(l, "saveddoc") && (d = curdoc()) != NULL) {
        r = OleSavedServerDoc(d->lh);
        P("  OleSavedServerDoc -> %d %s", r, (const char FAR *)stname(r));
    } else if (!lstrcmpi(l, "revertdoc") && (d = curdoc()) != NULL) {
        r = OleRevertServerDoc(d->lh);
        P("  OleRevertServerDoc -> %d %s", r, (const char FAR *)stname(r));
    } else if (!lstrcmpi(l, "changed")) {
        notify(OLE_CHANGED);
    } else if (!lstrcmpi(l, "saved")) {
        notify(OLE_SAVED);
    } else if (!lstrcmpi(l, "closed")) {
        notify(OLE_CLOSED);
    } else if (!lstrcmpi(l, "renamed")) {
        notify(OLE_RENAMED);
    } else if (!lstrcmpi(l, "revokeobj")) {
        int i;
        for (i = 0; i < NOBJ; i++)
            if (objs[i].used && objs[i].client) {
                r = OleRevokeObject(objs[i].client);
                P("  OleRevokeObject obj=%d -> %d %s", objs[i].n, r,
                  (const char FAR *)stname(r));
            }
    } else if (!lstrcmpi(l, "block")) {
        r = OleBlockServer(lhsrv);
        P("  OleBlockServer -> %d %s", r, (const char FAR *)stname(r));
    } else if (!lstrcmpi(l, "unblock")) {
        BOOL more = TRUE;
        int n = 0;
        do {
            r = OleUnblockServer(lhsrv, &more);
            P("  OleUnblockServer -> %d %s more=%d", r, (const char FAR *)stname(r), more);
        } while (r == OLE_OK && more && ++n < 100);
    } else if (!lstrcmpi(l, "unblock1")) {
        BOOL more = 77;
        r = OleUnblockServer(lhsrv, &more);
        P("  OleUnblockServer -> %d %s more=%d", r, (const char FAR *)stname(r), more);
    } else if (!lstrcmpi(l, "revokesrv")) {
        revoking = TRUE;
        r = OleRevokeServer(lhsrv);
        P("  OleRevokeServer -> %d %s", r, (const char FAR *)stname(r));
    } else if (!lstrcmpi(l, "bad")) {
        /* invalid arguments that cannot fault: null and stale handles */
        LHSERVER lh2 = 0;
        LHSERVERDOC ld2 = 0;
        BOOL more = 77;
        docs[NDOC - 1].d.lpvtbl = docs[NDOC - 2].d.lpvtbl = &docvt;
        docs[NDOC - 1].n = 901;
        docs[NDOC - 2].n = 902;
        r = OleRegisterServer(cls_name, &srv, &lh2, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer same class -> %d %s lh=%s", r, (const char FAR *)stname(r),
          lh2 ? "nz" : "0");
        if (r == OLE_OK)
            P("  OleRevokeServer it -> %d", OleRevokeServer(lh2));
        lh2 = 0;
        r = OleRegisterServer("OS1NotInRegDB", &srv, &lh2, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer unregistered class -> %d %s lh=%s", r,
          (const char FAR *)stname(r), lh2 ? "nz" : "0");
        if (r == OLE_OK)
            P("  OleRevokeServer it -> %d", OleRevokeServer(lh2));
        lh2 = 0;
        r = OleRegisterServer("", &srv, &lh2, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer \"\" -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServer(NULL, &srv, &lh2, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer NULL -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServer(cls_name, NULL, &lh2, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer lpsrv NULL -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServer(cls_name, &srv, NULL, inst, OLE_SERVER_MULTI);
        P("  OleRegisterServer lplh NULL -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServer("OS1Probe", &srv, &lh2, inst, (OLE_SERVER_USE)7);
        P("  OleRegisterServer use=7 -> %d %s", r, (const char FAR *)stname(r));
        if (r == OLE_OK)
            P("  OleRevokeServer it -> %d", OleRevokeServer(lh2));
        P("  OleRevokeServer(0) -> %d", OleRevokeServer(0));
        P("  OleBlockServer(0) -> %d", OleBlockServer(0));
        P("  OleUnblockServer(0) -> %d more=%d", OleUnblockServer(0, &more), more);
        P("  OleUnblockServer(lh, NULL) -> %d", OleUnblockServer(lhsrv, NULL));
        P("  OleRegisterServerDoc(0) -> %d", OleRegisterServerDoc(0, "X", &docs[0].d, &ld2));
        r = OleRegisterServerDoc(lhsrv, NULL, &docs[NDOC - 1].d, &ld2);
        P("  OleRegisterServerDoc name NULL -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServerDoc(lhsrv, "BADDOC", NULL, &ld2);
        P("  OleRegisterServerDoc lpdoc NULL -> %d %s", r, (const char FAR *)stname(r));
        r = OleRegisterServerDoc(lhsrv, "BADDOC", &docs[NDOC - 1].d, NULL);
        P("  OleRegisterServerDoc lplh NULL -> %d %s", r, (const char FAR *)stname(r));
        ld2 = 0;
        r = OleRegisterServerDoc(lhsrv, "", &docs[NDOC - 1].d, &ld2);
        P("  OleRegisterServerDoc \"\" -> %d %s lh=%s", r, (const char FAR *)stname(r),
          ld2 ? "nz" : "0");
        if (r == OLE_OK)
            P("  OleRevokeServerDoc it -> %d", OleRevokeServerDoc(ld2));
        ld2 = 0;
        r = OleRegisterServerDoc(lhsrv, "DUPDOC", &docs[NDOC - 1].d, &ld2);
        P("  OleRegisterServerDoc DUPDOC -> %d lh=%s", r, ld2 ? "nz" : "0");
        {
            LHSERVERDOC ld3 = 0;
            r = OleRegisterServerDoc(lhsrv, "DUPDOC", &docs[NDOC - 2].d, &ld3);
            P("  OleRegisterServerDoc DUPDOC again -> %d lh=%s same=%d", r, ld3 ? "nz" : "0",
              ld3 == ld2);
            if (r == OLE_OK && ld3 != ld2)
                P("  OleRevokeServerDoc it -> %d", OleRevokeServerDoc(ld3));
        }
        if (ld2) {
            P("  OleRevokeServerDoc DUPDOC -> %d", OleRevokeServerDoc(ld2));
            P("  OleRevokeServerDoc DUPDOC again -> %d", OleRevokeServerDoc(ld2));
            P("  OleRenameServerDoc stale -> %d", OleRenameServerDoc(ld2, "Y"));
            P("  OleSavedServerDoc stale -> %d", OleSavedServerDoc(ld2));
            P("  OleRevertServerDoc stale -> %d", OleRevertServerDoc(ld2));
        }
        P("  OleRevokeServerDoc(0) -> %d", OleRevokeServerDoc(0));
        P("  OleRenameServerDoc(0) -> %d", OleRenameServerDoc(0, "Y"));
        P("  OleSavedServerDoc(0) -> %d", OleSavedServerDoc(0));
        P("  OleRevertServerDoc(0) -> %d", OleRevertServerDoc(0));
        P("  OleRevokeObject(NULL) -> %d", OleRevokeObject(NULL));
        if (curdoc()) {
            P("  OleRenameServerDoc NULL name -> %d", OleRenameServerDoc(curdoc()->lh, NULL));
            P("  OleRenameServerDoc same name -> %d",
              OleRenameServerDoc(curdoc()->lh, curdoc()->name));
        }
    } else if (!lstrcmpi(l, "version")) {
        DWORD v = OleQueryServerVersion();
        P("  OleQueryServerVersion -> %lx", v);
    } else if (!lstrcmpi(l, "show")) {
        ShowWindow(mainwnd, SW_SHOWNORMAL);
    } else if (!lstrcmpi(l, "quit")) {
        PostMessage(mainwnd, WM_CLOSE, 0, 0);
    } else
        P("  (not done)");
}

static void run_script(void)
{
    while (next_line && *next_line && (long)(GetTickCount() - wait_until) >= 0) {
        char *l = next_line, *e = l;
        while (*e && *e != '\r' && *e != '\n')
            e++;
        next_line = e;
        while (*next_line == '\r' || *next_line == '\n')
            *next_line++ = 0;
        *e = 0;
        step(l);
    }
}

static void load_script(void)
{
    HFILE f = _lopen("C:\\OS1.TXT", OF_READ);
    int n;
    if (f == HFILE_ERROR)
        return;
    n = _lread(f, script, sizeof script - 1);
    _lclose(f);
    if (n < 0)
        n = 0;
    script[n] = 0;
    next_line = script;
}

/* ---- main window ---- */

LRESULT CALLBACK __export MainWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        run_script();
        return 0;
    case WM_CLOSE:
        if (!revoking) {
            /* the user closes the server: revoke first, quit on Release */
            OLESTATUS r;
            int i;
            for (i = 0; i < NDOC; i++)
                if (docs[i].used && docs[i].lh && !docs[i].revoking)
                    revoke_doc(&docs[i]);
            revoking = TRUE;
            r = OleRevokeServer(lhsrv);
            P("  OleRevokeServer -> %d %s", r, (const char FAR *)stname(r));
            if (r == OLE_WAIT_FOR_RELEASE)
                return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

#define MPI(f) MakeProcInstance((FARPROC)(f), inst)

static void make_vtables(void)
{
    srvvt.Open = (void FAR *)MPI(SrvOpen);
    srvvt.Create = (void FAR *)MPI(SrvCreate);
    srvvt.CreateFromTemplate = (void FAR *)MPI(SrvCreateFromTemplate);
    srvvt.Edit = (void FAR *)MPI(SrvEdit);
    srvvt.Exit = (void FAR *)MPI(SrvExit);
    srvvt.Release = (void FAR *)MPI(SrvRelease);
    srvvt.Execute = (void FAR *)MPI(SrvExecute);
    docvt.Save = (void FAR *)MPI(DocSave);
    docvt.Close = (void FAR *)MPI(DocClose);
    docvt.SetHostNames = (void FAR *)MPI(DocSetHostNames);
    docvt.SetDocDimensions = (void FAR *)MPI(DocSetDocDimensions);
    docvt.GetObject = (void FAR *)MPI(DocGetObject);
    docvt.Release = (void FAR *)MPI(DocRelease);
    docvt.SetColorScheme = (void FAR *)MPI(DocSetColorScheme);
    docvt.Execute = (void FAR *)MPI(DocExecute);
    objvt.QueryProtocol = (void FAR *)MPI(ObjQueryProtocol);
    objvt.Release = (void FAR *)MPI(ObjRelease);
    objvt.Show = (void FAR *)MPI(ObjShow);
    objvt.DoVerb = (void FAR *)MPI(ObjDoVerb);
    objvt.GetData = (void FAR *)MPI(ObjGetData);
    objvt.SetData = (void FAR *)MPI(ObjSetData);
    objvt.SetTargetDevice = (void FAR *)MPI(ObjSetTargetDevice);
    objvt.SetBounds = (void FAR *)MPI(ObjSetBounds);
    objvt.EnumFormats = (void FAR *)MPI(ObjEnumFormats);
    objvt.SetColorScheme = (void FAR *)MPI(ObjSetColorScheme);
    srv.lpvtbl = &srvvt;
}

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static BOOL prefix(const char FAR *s, const char *p)
{
    while (*p) {
        char a = *s++, b = *p++;
        if (a >= 'a' && a <= 'z')
            a -= 32;
        if (b >= 'a' && b <= 'z')
            b -= 32;
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

int FAR __cdecl tmain(void)
{
    char FAR *cmd = (char FAR *)MAKELP(init_regs[6], init_regs[1]);
    static char file[128];
    OLE_SERVER_USE use = OLE_SERVER_MULTI;
    WNDCLASS wc;
    MSG m;
    OLESTATUS r;
    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    P("OS1 start cmd=%q", (LPCSTR)cmd);
    file[0] = 0;
    while (*cmd) {
        char FAR *a;
        while (*cmd == ' ')
            cmd++;
        a = cmd;
        while (*cmd && *cmd != ' ')
            cmd++;
        if (*cmd)
            *cmd++ = 0;
        if (!*a)
            break;
        if (prefix(a, "/EMBEDDING") || prefix(a, "-EMBEDDING"))
            embedding = TRUE;
        else if (prefix(a, "/C:"))
            lstrcpy(cls_name, a + 3);
        else if (prefix(a, "/S"))
            use = OLE_SERVER_SINGLE;
        else if (prefix(a, "/R:") && nforce < NFORCE) {
            char FAR *e = a + 3;
            int i = 0, v = 0;
            while (*e && *e != '=' && i < 23)
                force[nforce].name[i++] = *e++;
            force[nforce].name[i] = 0;
            if (*e == '=')
                e++;
            while (*e >= '0' && *e <= '9')
                v = v * 10 + (*e++ - '0');
            force[nforce++].code = v;
        } else
            lstrcpy(file, a);
    }
    cf_native = RegisterClipboardFormat("Native");
    cf_ownerlink = RegisterClipboardFormat("OwnerLink");
    cf_objectlink = RegisterClipboardFormat("ObjectLink");

    wc.style = 0;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "OS1Main";
    RegisterClass(&wc);
    mainwnd = CreateWindow("OS1Main", cls_name, WS_OVERLAPPEDWINDOW, 300, 200, 200, 120,
                           NULL, NULL, inst, NULL);
    make_vtables();
    r = OleRegisterServer(cls_name, &srv, &lhsrv, inst, use);
    P("OS1 OleRegisterServer %q use=%d -> %d %s lh=%s", (LPCSTR)cls_name, use, r,
      (const char FAR *)stname(r), lhsrv ? "nz" : "0");
    if (r != OLE_OK) {
        DestroyWindow(mainwnd);
        return 1;
    }
    if (file[0]) {
        LHSERVERDOC lh = 0;
        DOC *d = newdoc(file);
        r = OleRegisterServerDoc(lhsrv, file, &d->d, &lh);
        d->lh = lh;
        P("OS1 OleRegisterServerDoc %q -> %d %s lh=%s", (LPCSTR)file, r,
          (const char FAR *)stname(r), lh ? "nz" : "0");
    }
    if (!embedding)
        ShowWindow(mainwnd, SW_SHOWNORMAL);
    load_script();
    SetTimer(mainwnd, 1, 100, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    P("OS1 exit");
    return 0;
}
