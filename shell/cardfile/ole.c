/*
 * ole.c - w16 Cardfile: pictures as OLE 1.0 objects (OLECLI.DLL).
 *
 * Cardfile 3.1 keeps every picture as an OLE object: a pasted bitmap
 * becomes a static object of class "BITMAP" (CF6), a bitmap from a 3.0
 * file one too (CF8), objects from other programs are embedded or linked.
 * In the file an object is what OleSaveToStream writes, preceded by its
 * number and followed by the character size, the rectangle and the type
 * (crd.c). Its length is not stored; ole_skip finds it by parsing the
 * stream as KB Q99340 describes the OLE 1.0 stream (facts only).
 *
 * OLECLI calls made (for checking against w16's own OLECLI, ARCHITECTURE.md
 * 16): OleRegisterClientDoc, OleRenameClientDoc, OleSavedClientDoc,
 * OleRevokeClientDoc, OleLoadFromStream, OleSaveToStream, OleDraw,
 * OleQueryBounds, OleQueryType, OleCreateFromClip, OleCreateLinkFromClip,
 * OleQueryCreateFromClip, OleQueryLinkFromClip, OleCreate, OleClone,
 * OleCopyToClipboard, OleActivate, OleUpdate, OleClose, OleDelete,
 * OleRelease, OleQueryReleaseStatus, OleGetLinkUpdateOptions,
 * OleSetLinkUpdateOptions, OleQueryOpen, OleSetHostNames, OleRename.
 */
#include "cardfile.h"

#ifndef W16_WIN2

static LHCLIENTDOC doc;
static char s_stdfile[] = "StdFileEditing";
static char s_static[] = "Static";

/* ---- the client: one for all objects ---- */

static OLECLIENTVTBL client_vtbl;
static OLECLIENT client;

int CALLBACK __export ClientCallBack(LPOLECLIENT c, OLE_NOTIFICATION n, LPOLEOBJECT o)
{
    (void)c;
    switch (n) {
    case OLE_CHANGED:
    case OLE_SAVED:
    case OLE_CLOSED:
        if (g_pic.kind == PIC_OBJECT && g_pic.obj == o) {
            RECT r;
            if (n != OLE_CLOSED && OleQueryBounds(o, &r) == OLE_OK) {
                HDC dc = GetDC(NULL);
                int w = r.right - r.left, h = r.bottom - r.top;
                if (w < 0)
                    w = -w;
                if (h < 0)
                    h = -h;
                g_pic.cx = MulDiv(w, GetDeviceCaps(dc, LOGPIXELSX), 2540);
                g_pic.cy = MulDiv(h, GetDeviceCaps(dc, LOGPIXELSY), 2540);
                ReleaseDC(NULL, dc);
            }
            g_carddirty = TRUE;
            InvalidateRect(g_edit, NULL, TRUE);
        }
        return TRUE;
    case OLE_QUERY_PAINT:
    case OLE_QUERY_RETRY:
        return TRUE;
    default:
        return TRUE;
    }
}

/* wait for an asynchronous operation of o to end */
static OLESTATUS wait_for(LPOLEOBJECT o, OLESTATUS st)
{
    MSG m;
    if (st != OLE_WAIT_FOR_RELEASE)
        return st;
    while (OleQueryReleaseStatus(o) == OLE_BUSY) {
        if (!GetMessage(&m, NULL, 0, 0))
            break;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return OleQueryReleaseError(o);
}

/* ---- streams: a file, or memory ---- */

typedef struct {
    OLESTREAM s;
    HFILE f;                            /* or HFILE_ERROR for memory */
    char FAR *mem;
    DWORD at, len;
} STREAM;

static OLESTREAMVTBL stream_vtbl;

DWORD CALLBACK __export StreamGet(LPOLESTREAM s, void FAR *buf, DWORD n)
{
    STREAM FAR *st = (STREAM FAR *)s;
    if (st->f != HFILE_ERROR) {
        if (n > 0xFFFFu)
            return 0;
        return _lread(st->f, buf, (WORD)n) == (WORD)n ? n : 0;
    }
    if (st->at + n > st->len)
        return 0;
    mem_cpy(buf, st->mem + (WORD)st->at, (WORD)n);
    st->at += n;
    return n;
}

DWORD CALLBACK __export StreamPut(LPOLESTREAM s, OLE_CONST void FAR *buf, DWORD n)
{
    STREAM FAR *st = (STREAM FAR *)s;
    if (n > 0xFFFFu)
        return 0;
    return _lwrite(st->f, buf, (WORD)n) == (WORD)n ? n : 0;
}

/* ---- set-up ---- */

BOOL ole_init(void)
{
    client_vtbl.CallBack = (int (CALLBACK *)(LPOLECLIENT, OLE_NOTIFICATION, LPOLEOBJECT))
        MakeProcInstance((FARPROC)ClientCallBack, g_hinst);
    client.lpvtbl = &client_vtbl;
    stream_vtbl.Get = (DWORD (CALLBACK *)(LPOLESTREAM, void FAR *, DWORD))
        MakeProcInstance((FARPROC)StreamGet, g_hinst);
    stream_vtbl.Put = (DWORD (CALLBACK *)(LPOLESTREAM, OLE_CONST void FAR *, DWORD))
        MakeProcInstance((FARPROC)StreamPut, g_hinst);
    {
        char app[40];
        LoadString(g_hinst, IDS_APPNAME, app, sizeof app);
        OleRegisterClientDoc(app, load_str(IDS_UNTITLED), 0, &doc);
    }
    return TRUE;
}

void ole_done(void)
{
    if (doc)
        OleRevokeClientDoc(doc);
    doc = 0;
}

void ole_rename(LPCSTR path)
{
    if (doc)
        OleRenameClientDoc(doc, path);
}

void ole_saved(void)
{
    if (doc)
        OleSavedClientDoc(doc);
}

/* the name an object gets: "Object #n" (string 776) */
static LPSTR obj_name(DWORD id)
{
    static char name[24];
    wsprintf(name, load_str(IDS_OBJECTN), (int)id);
    return name;
}

static void obj_type(PICTURE *p)
{
    LONG t = 0;
    OleQueryType(p->obj, &t);
    p->otype = t == OT_LINK ? 1 : t == OT_EMBEDDED ? 0 : 2;
}

/* size of the object in pixels (OleQueryBounds gives HIMETRIC) */
static void obj_size(PICTURE *p)
{
    RECT r;
    if (OleQueryBounds(p->obj, &r) == OLE_OK) {
        HDC dc = GetDC(NULL);
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w < 0)
            w = -w;
        if (h < 0)
            h = -h;
        p->cx = MulDiv(w, GetDeviceCaps(dc, LOGPIXELSX), 2540);
        p->cy = MulDiv(h, GetDeviceCaps(dc, LOGPIXELSY), 2540);
        ReleaseDC(NULL, dc);
    }
}

/* ---- loading and saving ---- */

BOOL ole_load(HFILE f, PICTURE *p, DWORD FAR *pos)
{
    STREAM st;
    OLESTATUS r;
    st.s.lpstbl = &stream_vtbl;
    st.f = f;
    _llseek(f, *pos, 0);
    r = OleLoadFromStream(&st.s, s_stdfile, &client, doc, obj_name(p->id), &p->obj);
    if (r != OLE_OK && r != OLE_WAIT_FOR_RELEASE) {
        _llseek(f, *pos, 0);
        r = OleLoadFromStream(&st.s, s_static, &client, doc, obj_name(p->id), &p->obj);
    }
    if (r == OLE_WAIT_FOR_RELEASE)
        r = wait_for(p->obj, r);
    if (r != OLE_OK || !p->obj) {
        p->obj = NULL;
        return FALSE;
    }
    *pos = _llseek(f, 0, 1);
    p->kind = PIC_OBJECT;
    return TRUE;
}

BOOL ole_save(HFILE f, PICTURE *p)
{
    STREAM st;
    st.s.lpstbl = &stream_vtbl;
    st.f = f;
    return OleSaveToStream(p->obj, &st.s) == OLE_OK;
}

/* ---- parsing a stored object (Q99340) ---- */

static BOOL rd_long(HFILE f, DWORD *v)
{
    return f_read(f, v, 4);
}

static BOOL skip(HFILE f, DWORD n)
{
    return _llseek(f, n, 1) != -1;
}

/* a length-prefixed string; its first bytes go to name if given */
static BOOL rd_string(HFILE f, char *name, int size)
{
    DWORD n;
    if (!rd_long(f, &n))
        return FALSE;
    if (name) {
        WORD k = n < (DWORD)size - 1 ? (WORD)n : size - 1;
        if (!f_read(f, name, k))
            return FALSE;
        name[k] = 0;
        return skip(f, n - k);
    }
    return skip(f, n);
}

static BOOL skip_sized(HFILE f)
{
    DWORD n;
    return rd_long(f, &n) && skip(f, n);
}

static BOOL is_picture_class(const char *c)
{
    return !lstrcmp(c, "METAFILEPICT") || !lstrcmp(c, "BITMAP") || !lstrcmp(c, "DIB");
}

static BOOL skip_presentation(HFILE f)
{
    DWORD ver, id, cf;
    char cls[16];
    if (!rd_long(f, &ver) || !rd_long(f, &id))
        return FALSE;
    if (id == 0)
        return TRUE;                    /* no presentation */
    if (!rd_string(f, cls, sizeof cls))
        return FALSE;
    if (is_picture_class(cls))
        return skip(f, 8) && skip_sized(f);
    if (!rd_long(f, &cf))
        return FALSE;
    if (cf == 0 && !rd_string(f, NULL, 0))
        return FALSE;
    return skip_sized(f);
}

/* the end of the object stored at pos, or 0 */
DWORD ole_skip(HFILE f, DWORD pos)
{
    DWORD ver, fmt;
    if (_llseek(f, pos, 0) != (LONG)pos || !rd_long(f, &ver) || !rd_long(f, &fmt) ||
        !rd_string(f, NULL, 0))
        return 0;
    switch (fmt) {
    case 3:                             /* static */
        if (!skip(f, 8) || !skip_sized(f))
            return 0;
        break;
    case 2:                             /* embedded */
        if (!rd_string(f, NULL, 0) || !rd_string(f, NULL, 0) || !skip_sized(f) ||
            !skip_presentation(f))
            return 0;
        break;
    case 1:                             /* linked */
        if (!rd_string(f, NULL, 0) || !rd_string(f, NULL, 0) || !rd_string(f, NULL, 0) ||
            !skip(f, 8) || !skip_presentation(f))
            return 0;
        break;
    default:
        return 0;
    }
    return _llseek(f, 0, 1);
}

/* ---- drawing, copying, deleting ---- */

void ole_draw(PICTURE *p, HDC dc, RECT *r)
{
    if (p->obj)
        OleDraw(p->obj, dc, r, NULL, NULL);
}

void ole_free(PICTURE *p)
{
    if (p->obj)
        wait_for(p->obj, OleRelease(p->obj));
    p->obj = NULL;
}

BOOL ole_clone(PICTURE *from, PICTURE *to)
{
    *to = *from;
    to->id = g_lastobj++;
    if (wait_for(from->obj, OleClone(from->obj, &client, doc, obj_name(to->id), &to->obj)) != OLE_OK) {
        to->kind = PIC_NONE;
        to->obj = NULL;
        return FALSE;
    }
    return TRUE;
}

void ole_copy(PICTURE *p)
{
    if (!OpenClipboard(g_frame))
        return;
    EmptyClipboard();
    OleCopyToClipboard(p->obj);
    CloseClipboard();
}

void ole_activate(PICTURE *p, int verb)
{
    RECT r;
    OLESTATUS st;
    if (p->otype == 2) {
        msg_box(IDS_NOTOLE, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    GetClientRect(g_edit, &r);
    st = wait_for(p->obj, OleActivate(p->obj, verb, TRUE, TRUE, g_frame, &r));
    if (st != OLE_OK)
        msg_box(IDS_NOSERVER, NULL, MB_OK | MB_ICONEXCLAMATION);
}

/* ---- pasting and inserting ---- */

BOOL ole_can_paste(void)
{
    return OleQueryCreateFromClip(s_stdfile, olerender_draw, 0) == OLE_OK ||
           OleQueryCreateFromClip(s_static, olerender_draw, 0) == OLE_OK;
}

BOOL ole_can_link(void)
{
    return OleQueryLinkFromClip(s_stdfile, olerender_draw, 0) == OLE_OK;
}

/* the new picture replaces the card's (at the top left) */
static BOOL new_object(LPOLEOBJECT o, DWORD id)
{
    if (!o)
        return FALSE;
    pic_free(&g_pic);
    g_pic.kind = PIC_OBJECT;
    g_pic.obj = o;
    g_pic.id = id;
    g_pic.x = g_pic.y = 0;
    obj_type(&g_pic);
    obj_size(&g_pic);
    return TRUE;
}

BOOL ole_paste(BOOL link)
{
    LPOLEOBJECT o = NULL;
    OLESTATUS st;
    DWORD id = g_lastobj;
    if (!OpenClipboard(g_frame)) {
        msg_box(IDS_NOCLIP, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (link)
        st = OleCreateLinkFromClip(s_stdfile, &client, doc, obj_name(id), &o, olerender_draw, 0);
    else {
        st = OleCreateFromClip(s_stdfile, &client, doc, obj_name(id), &o, olerender_draw, 0);
        if (st != OLE_OK && st != OLE_WAIT_FOR_RELEASE)
            st = OleCreateFromClip(s_static, &client, doc, obj_name(id), &o, olerender_draw, 0);
    }
    CloseClipboard();
    if (st == OLE_WAIT_FOR_RELEASE)
        st = wait_for(o, st);
    if (st != OLE_OK) {
        msg_box(IDS_NOPICCLIP, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    g_lastobj++;
    return new_object(o, id);
}

/* Insert Object: the classes registered for StdFileEditing */
static char sel_class[64];

BOOL CALLBACK __export InsertDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        {
            char key[64], sub[100], name[80];
            DWORD i;
            for (i = 0; RegEnumKey(HKEY_CLASSES_ROOT, i, key, sizeof key) == ERROR_SUCCESS; i++) {
                LONG n = sizeof name;
                HKEY h;
                if (key[0] == '.')
                    continue;
                s_cpy(sub, key, sizeof sub);
                s_cat(sub, "\\protocol\\StdFileEditing\\server", sizeof sub);
                if (RegOpenKey(HKEY_CLASSES_ROOT, sub, &h) != ERROR_SUCCESS)
                    continue;
                RegCloseKey(h);
                if (RegQueryValue(HKEY_CLASSES_ROOT, key, name, &n) == ERROR_SUCCESS && name[0]) {
                    int at = (int)SendDlgItemMessage(d, IDC_TYPELIST, LB_ADDSTRING, 0,
                                                     (LPARAM)(LPSTR)name);
                    /* remember the class behind each name */
                    SendDlgItemMessage(d, IDC_TYPELIST, LB_SETITEMDATA, at,
                                       (LPARAM)(LPSTR)AnsiNext(name) - (LPARAM)(LPSTR)name);
                }
            }
            SendDlgItemMessage(d, IDC_TYPELIST, LB_SETCURSEL, 0, 0);
        }
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || (wp == IDC_TYPELIST && HIWORD(lp) == LBN_DBLCLK)) {
            char name[80], key[64];
            DWORD i;
            int sel = (int)SendDlgItemMessage(d, IDC_TYPELIST, LB_GETCURSEL, 0, 0);
            if (sel < 0)
                return TRUE;
            SendDlgItemMessage(d, IDC_TYPELIST, LB_GETTEXT, sel, (LPARAM)(LPSTR)name);
            /* find the class with this readable name */
            sel_class[0] = 0;
            for (i = 0; RegEnumKey(HKEY_CLASSES_ROOT, i, key, sizeof key) == ERROR_SUCCESS; i++) {
                char v[80];
                LONG n = sizeof v;
                if (key[0] != '.' && RegQueryValue(HKEY_CLASSES_ROOT, key, v, &n) == ERROR_SUCCESS &&
                    !lstrcmp(v, name)) {
                    s_cpy(sel_class, key, sizeof sel_class);
                    break;
                }
            }
            EndDialog(d, sel_class[0] != 0);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ole_insert(void)
{
    FARPROC p = MakeProcInstance((FARPROC)InsertDlgProc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_INSERTOBJECT), g_frame, (DLGPROC)p);
    LPOLEOBJECT o = NULL;
    OLESTATUS st;
    DWORD id = g_lastobj;
    FreeProcInstance(p);
    if (r != TRUE)
        return;
    st = OleCreate(s_stdfile, &client, sel_class, doc, obj_name(id), &o, olerender_draw, 0);
    if (st == OLE_WAIT_FOR_RELEASE)
        st = wait_for(o, st);
    if (st != OLE_OK) {
        msg_box(IDS_NOSERVER2, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    g_lastobj++;
    new_object(o, id);
    g_carddirty = TRUE;
    InvalidateRect(g_edit, NULL, TRUE);
}

/* ---- Links ---- */

BOOL CALLBACK __export LinksDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        {
            OLEOPT_UPDATE u = oleupdate_always;
            OleGetLinkUpdateOptions(g_pic.obj, &u);
            CheckRadioButton(d, IDC_LINKAUTO, IDC_LINKMANUAL,
                             u == oleupdate_always ? IDC_LINKAUTO : IDC_LINKMANUAL);
        }
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_LINKAUTO:
        case IDC_LINKMANUAL:
            CheckRadioButton(d, IDC_LINKAUTO, IDC_LINKMANUAL, wp);
            wait_for(g_pic.obj, OleSetLinkUpdateOptions(g_pic.obj, wp == IDC_LINKAUTO
                                                                    ? oleupdate_always
                                                                    : oleupdate_oncall));
            g_carddirty = TRUE;
            return TRUE;
        case IDC_UPDATENOW:
            wait_for(g_pic.obj, OleUpdate(g_pic.obj));
            g_carddirty = TRUE;
            InvalidateRect(g_edit, NULL, TRUE);
            return TRUE;
        case IDC_ACTIVATE:
        case IDC_EDITLINK:
            EndDialog(d, TRUE);
            ole_activate(&g_pic, wp == IDC_EDITLINK ? 1 : 0);
            return TRUE;
        case IDC_CANCELLINK:
            {
                LPOLEOBJECT o = NULL;
                if (wait_for(g_pic.obj, OleObjectConvert(g_pic.obj, s_static, &client, doc,
                                                         obj_name(g_pic.id), &o)) == OLE_OK && o) {
                    ole_free(&g_pic);
                    g_pic.obj = o;
                    g_pic.otype = 2;
                    g_carddirty = TRUE;
                }
                EndDialog(d, TRUE);
            }
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(d, wp == IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ole_links(void)
{
    FARPROC p;
    if (g_pic.kind != PIC_OBJECT || g_pic.otype != 1)
        return;
    p = MakeProcInstance((FARPROC)LinksDlgProc, g_hinst);
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_LINKS), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
}

/* ---- Paste Special ---- */

BOOL CALLBACK __export PasteSpecialDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        if (ole_can_paste())
            SendDlgItemMessage(d, IDC_TYPELIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)load_str(IDS_OBJECT));
        if (IsClipboardFormatAvailable(CF_BITMAP))
            SendDlgItemMessage(d, IDC_TYPELIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)load_str(IDS_BITMAP));
        if (IsClipboardFormatAvailable(CF_METAFILEPICT))
            SendDlgItemMessage(d, IDC_TYPELIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)load_str(IDS_PICTURE));
        SendDlgItemMessage(d, IDC_TYPELIST, LB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(d, IDC_PSLINK), ole_can_link());
        return TRUE;
    case WM_COMMAND:
        if (wp == IDC_PSPASTE || wp == IDC_PSLINK || wp == IDOK) {
            EndDialog(d, wp == IDC_PSLINK ? 2 : 1);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ole_paste_special(void)
{
    FARPROC p = MakeProcInstance((FARPROC)PasteSpecialDlgProc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_PASTESPECIAL), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    if (r > 0 && ole_paste(r == 2)) {
        g_carddirty = TRUE;
        InvalidateRect(g_edit, NULL, TRUE);
    }
}

/* ---- the Edit menu's object items ---- */

static char obj_class[64];

/* Edit > Object: "&Object" when there is no object or it has no verbs;
   else the object's verbs (strings 784-786: "<class> &Object" with a
   popup of verbs, or "<verb> <class> &Object" for one). UNVERIFIED: the
   exact texts, beyond the strings' shape. */
void ole_menu(HMENU edit)
{
    BOOL obj = !g_listview && g_pic.kind == PIC_OBJECT;
    EnableMenuItem(edit, IDM_PASTELINK, MF_BYCOMMAND |
                   (!g_listview && g_picmode && ole_can_link() ? MF_ENABLED : MF_GRAYED));
    /* grayed with an empty clipboard (dialogs scenario); UNVERIFIED: in
       Text mode with a picture on the clipboard */
    EnableMenuItem(edit, IDM_PASTESPECIAL, MF_BYCOMMAND |
                   (!g_listview && (ole_can_paste() || IsClipboardFormatAvailable(CF_BITMAP) ||
                                    IsClipboardFormatAvailable(CF_METAFILEPICT))
                        ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(edit, IDM_LINKS, MF_BYCOMMAND |
                   (obj && g_pic.otype == 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(edit, IDM_OBJECT, MF_BYCOMMAND | (obj && g_pic.otype != 2 ? MF_ENABLED : MF_GRAYED));
    /* Picture mode only (grayed in Text mode, enabled in Picture mode) */
    EnableMenuItem(edit, IDM_INSERTOBJECT, MF_BYCOMMAND | (!g_listview && g_picmode ? MF_ENABLED : MF_GRAYED));
    (void)obj_class;
}

void ole_command(WORD id)
{
    switch (id) {
    case IDM_PASTELINK:
        if (ole_paste(TRUE)) {
            g_carddirty = TRUE;
            InvalidateRect(g_edit, NULL, TRUE);
        }
        break;
    case IDM_PASTESPECIAL:
        ole_paste_special();
        break;
    case IDM_LINKS:
        ole_links();
        break;
    case IDM_OBJECT:
        if (g_pic.kind == PIC_OBJECT)
            ole_activate(&g_pic, 0);
        break;
    case IDM_INSERTOBJECT:
        ole_insert();
        break;
    default:
        if (id >= IDM_VERB0 && id < IDM_VERB0 + 16 && g_pic.kind == PIC_OBJECT)
            ole_activate(&g_pic, id - IDM_VERB0);
        break;
    }
}

#endif
