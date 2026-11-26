/*
 * defs.c - OLECLI's other exports (ARCHITECTURE.md 16.7): the default
 * object-handler functions an object handler DLL calls (DEFxxx), the
 * built-in Paintbrush handler's entries (PBxxx), the methods that refuse
 * (ERRxxx), the network-drive helpers, the library entry and WEP.
 *
 * Nothing in Windows 3.1 imports these (docs/exportdb); their parameters
 * follow the SDK's object-handler interface as recalled and are
 * UNVERIFIED: DefCreateFromClip and DefLoadFromStream carry the object
 * type (and the stream's class atom and format) after OleCreateFromClip's
 * and OleLoadFromStream's parameters.
 */
#include "olecli.h"

/* ---- the library ---- */

int __cdecl __far olecli_init(HINSTANCE inst)
{
    g_inst = inst;
    cf_native = RegisterClipboardFormat("Native");
    cf_ownerlink = RegisterClipboardFormat("OwnerLink");
    cf_objectlink = RegisterClipboardFormat("ObjectLink");
    cf_binary = RegisterClipboardFormat("Binary");
    return dde_register(inst);
}

/* ---- default handler functions ---- */

OLESTATUS OLEAPI DefCreate(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls, LHCLIENTDOC lh,
                           LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                           OLECLIPFORMAT f)
{
    return OleCreate(protocol, c, cls, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefCreateInvisible(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                    LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out,
                                    OLEOPT_RENDER r, OLECLIPFORMAT f, BOOL launch)
{
    return OleCreateInvisible(protocol, c, cls, lh, name, out, r, f, launch);
}

OLESTATUS OLEAPI DefCreateFromTemplate(LPCSTR protocol, LPOLECLIENT c, LPCSTR templ,
                                       LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return OleCreateFromTemplate(protocol, c, templ, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefCreateFromFile(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                   LPCSTR file, LHCLIENTDOC lh, LPCSTR name,
                                   LPOLEOBJECT FAR *out, OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return OleCreateFromFile(protocol, c, cls, file, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefCreateLinkFromFile(LPCSTR protocol, LPOLECLIENT c, LPCSTR cls,
                                       LPCSTR file, LPCSTR item, LHCLIENTDOC lh,
                                       LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return OleCreateLinkFromFile(protocol, c, cls, file, item, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefCreateFromClip(LPCSTR protocol, LPOLECLIENT c, LHCLIENTDOC lh,
                                   LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                                   OLECLIPFORMAT f, LONG type)
{
    if (type == OT_LINK)
        return OleCreateLinkFromClip(protocol, c, lh, name, out, r, f);
    return OleCreateFromClip(protocol, c, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefCreateLinkFromClip(LPCSTR protocol, LPOLECLIENT c, LHCLIENTDOC lh,
                                       LPCSTR name, LPOLEOBJECT FAR *out,
                                       OLEOPT_RENDER r, OLECLIPFORMAT f)
{
    return OleCreateLinkFromClip(protocol, c, lh, name, out, r, f);
}

OLESTATUS OLEAPI DefLoadFromStream(LPOLESTREAM s, LPCSTR protocol, LPOLECLIENT c,
                                   LHCLIENTDOC lh, LPCSTR name, LPOLEOBJECT FAR *out,
                                   LONG type, ATOM cls, OLECLIPFORMAT f)
{
    char cn[128];
    LPDOC d = doc_check(lh);
    (void)protocol;
    (void)f;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!s)
        return OLE_ERROR_STREAM;
    cn[0] = 0;
    if (cls)
        GlobalGetAtomName(cls, cn, sizeof cn);
    return strm_load_rest(s, c, d, name, out, type, cn);
}

/* ---- methods that refuse: a static object's (UNVERIFIED code) ---- */

#define ERR OLE_ERROR_STATIC

OLESTATUS OLEAPI ErrShow(LPOLEOBJECT p, BOOL b) { (void)p; (void)b; return ERR; }
OLESTATUS OLEAPI ErrSetData(LPOLEOBJECT p, OLECLIPFORMAT f, HANDLE h)
{ (void)p; (void)f; (void)h; return ERR; }
OLESTATUS OLEAPI ErrSetHostNames(LPOLEOBJECT p, LPCSTR a, LPCSTR b)
{ (void)p; (void)a; (void)b; return ERR; }
OLESTATUS OLEAPI ErrSetTargetDevice(LPOLEOBJECT p, HGLOBAL h) { (void)p; (void)h; return ERR; }
OLESTATUS OLEAPI ErrSetBounds(LPOLEOBJECT p, const RECT FAR *r) { (void)p; (void)r; return ERR; }
OLESTATUS OLEAPI ErrCopyFromLink(LPOLEOBJECT p, LPOLECLIENT c, LHCLIENTDOC lh, LPCSTR n,
                                 LPOLEOBJECT FAR *o)
{ (void)p; (void)c; (void)lh; (void)n; (void)o; return ERR; }
OLESTATUS OLEAPI ErrQueryOpen(LPOLEOBJECT p) { (void)p; return ERR; }
OLESTATUS OLEAPI ErrActivate(LPOLEOBJECT p, UINT v, BOOL s, BOOL f, HWND w, const RECT FAR *r)
{ (void)p; (void)v; (void)s; (void)f; (void)w; (void)r; return ERR; }
OLESTATUS OLEAPI ErrClose(LPOLEOBJECT p) { (void)p; return ERR; }
OLESTATUS OLEAPI ErrUpdate(LPOLEOBJECT p) { (void)p; return ERR; }
OLESTATUS OLEAPI ErrReconnect(LPOLEOBJECT p) { (void)p; return ERR; }
void FAR * OLEAPI ErrQueryProtocol(LPOLEOBJECT p, LPCSTR s) { (void)p; (void)s; return NULL; }
OLESTATUS OLEAPI ErrQueryOutOfDate(LPOLEOBJECT p) { (void)p; return ERR; }
OLESTATUS OLEAPI ErrObjectConvert(LPOLEOBJECT p, LPCSTR s, LPOLECLIENT c, LHCLIENTDOC lh,
                                  LPCSTR n, LPOLEOBJECT FAR *o)
{ (void)p; (void)s; (void)c; (void)lh; (void)n; (void)o; return ERR; }
OLESTATUS OLEAPI ErrGetUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE FAR *u)
{ (void)p; (void)u; return ERR; }
OLESTATUS OLEAPI ErrSetUpdateOptions(LPOLEOBJECT p, OLEOPT_UPDATE u)
{ (void)p; (void)u; return ERR; }
OLESTATUS OLEAPI ErrExecute(LPOLEOBJECT p, HGLOBAL h, UINT r)
{ (void)p; (void)h; (void)r; return ERR; }
OLESTATUS OLEAPI ErrObjectLong(LPOLEOBJECT p, UINT f, LONG FAR *l)
{ (void)p; (void)f; (void)l; return ERR; }

/* ---- network drives (UNVERIFIED parameters) ---- */

/* the dialog "Connect Network Drive" (the resource CONNECTDLG) */
BOOL OLEAPI ConnectDlgProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (m == WM_INITDIALOG)
        return TRUE;
    if (m == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(w, wp == IDOK);
        return TRUE;
    }
    return FALSE;
}

/* a link's network name: the connection of its file's drive, if any */
OLESTATUS OLEAPI SetNetName(LPOLEOBJECT p)
{
    LPOBJ o = obj_check(p);
    char drive[3], net[128];
    UINT n = sizeof net;
    LPSTR f;
    if (!o || o->type != OT_LINK)
        return OLE_ERROR_OBJECT;
    f = gstr(o->htopic);
    drive[0] = f[0];
    drive[1] = f[0] ? f[1] : 0;         /* an empty name has no second */
    drive[2] = 0;
    gstr_unlock(o->htopic);
    if (drive[1] != ':')
        return OLE_OK;
    if (WNetGetConnection(drive, net, &n) == WN_SUCCESS) {
        gfree(o->hnet);
        o->hnet = gstr_new(net);
    }
    return OLE_OK;
}

OLESTATUS OLEAPI CheckNetDrive(LPOLEOBJECT p, BOOL prompt)
{
    (void)p;
    (void)prompt;
    return OLE_OK;
}

OLESTATUS OLEAPI SetNextNetDrive(LPOLEOBJECT p, int FAR *drive, LPSTR buf)
{
    (void)p;
    (void)drive;
    (void)buf;
    return OLE_ERROR_DRIVE;
}

/* ---- the windows' procedures, exported by name ---- */

LRESULT OLEAPI DocWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return OleWndProc(w, m, wp, lp);
}

LRESULT OLEAPI SrvrWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return OleWndProc(w, m, wp, lp);
}
