/*
 * ole.c - w16 Write: OLE objects through OLECLI.DLL (OLE 1.0).
 *
 * The document is registered as class "WINWRITE" with its name (or
 * "(Untitled)"); an object's name is its header's dwObjNum as 8 hex
 * digits, so embedded documents are "WINWRITE%<doc>%<num>#nnn" as
 * observed on 3.1 (ARCHITECTURE.md 16.10). An object paragraph's data is
 * OLECLI's stream (16.4); the object is loaded from it when it is first
 * drawn (OleLoadFromStream, protocol "Static" for static objects, else
 * "StdFileEditing") and drawn with OleDraw. When the server reports new
 * data (OLE_CHANGED, OLE_SAVED, and OLE_CHANGED before OLE_CLOSED for a
 * changed object closed by the user: without it a new object would be
 * lost, 16.3) the object is saved into its paragraph again
 * (OleSaveToStream). Insert Object lists the classes of the registration
 * database that have a StdFileEditing server.
 *
 * Calls used are those 3.1's Write imports (ARCHITECTURE.md 15.6).
 * UNVERIFIED: the exact order of Write's own OLECLI calls (an OC1 log of
 * Write against this one is the next check: tests/olescenarios.sh with
 * OLESCEN_FILES=build/dos/WRITE.EXE=WINDOWS/WRITE.EXE).
 */
#include "write.h"
#include <ole.h>
#include <shellapi.h>
#include <commdlg.h>

#define PICHDR 40

typedef struct {
    OLECLIENT cl;                       /* first: OLECLI passes it back */
    DWORD num;                          /* dwObjNum */
    LPOLEOBJECT obj;
    BOOL failed;                        /* could not load: drawn by pict.c */
    BOOL dirty;                         /* new data to store */
} WOBJ;

typedef struct {
    OLESTREAM st;
    DWORD cp, lim;                      /* reading: the document's text */
    HMEM out;                           /* writing */
    DWORD outLen;
} WSTREAM;

static LHCLIENTDOC lhdoc;
static OLECLIENTVTBL clvt;
static OLESTREAMVTBL stvt;
static HLOCAL hobjs;                    /* WOBJ * table, grows as needed */
static int nobjs;
static BOOL inited;
static DWORD lastNum;

/* ---- streams ----
 * OLECLI hands Get and Put blocks that may be larger than 64K (Native
 * data): huge pointers, stepped through in pieces that stay inside one
 * 64K segment of the block. */

static BYTE FAR *huge_add(const void FAR *p, DWORD n)
{
    DWORD lin = OFFSETOF(p) + n;
    return (BYTE FAR *)MAKELP(SELECTOROF(p) + HIWORD(lin) * ahincr(), LOWORD(lin));
}

DWORD CALLBACK __export StreamGet(LPOLESTREAM s, void FAR *buf, DWORD n)
{
    WSTREAM *w = (WSTREAM *)OFFSETOF(s);    /* ours, in our data segment */
    DWORD got = 0;
    while (got < n && w->cp < w->lim) {
        BYTE FAR *d = huge_add(buf, got);
        DWORD k = 0x10000L - OFFSETOF(d);
        if (k > n - got)
            k = n - got;
        if (k > w->lim - w->cp)
            k = w->lim - w->cp;
        if (k > 0xFFF0L)
            k = 0xFFF0L;
        doc_get(&g_doc, w->cp, (char FAR *)d, k);
        w->cp += k;
        got += k;
    }
    return got;
}

DWORD CALLBACK __export StreamPut(LPOLESTREAM s, const void FAR *buf, DWORD n)
{
    WSTREAM *w = (WSTREAM *)OFFSETOF(s);
    DWORD done = 0;
    if (w->outLen + n > w->out.size && !hm_resize(&w->out, w->outLen + n + 1024))
        return 0;
    while (done < n) {
        BYTE FAR *src = huge_add(buf, done);
        DWORD k = 0x10000L - OFFSETOF(src);
        if (k > n - done)
            k = n - done;
        if (k > 0xFFF0L)
            k = 0xFFF0L;
        hm_put(&w->out, w->outLen + done, src, k);
        done += k;
    }
    w->outLen += n;
    return n;
}

/* ---- the object table ---- */

static WOBJ **objs(void)
{
    return hobjs ? (WOBJ **)LocalLock(hobjs) : NULL;
}

static WOBJ *find_obj(DWORD num)
{
    WOBJ **t = objs(), *o = NULL;
    int i;
    for (i = 0; i < nobjs; i++)
        if (t[i]->num == num) {
            o = t[i];
            break;
        }
    if (hobjs)
        LocalUnlock(hobjs);
    return o;
}

static WOBJ *add_obj(DWORD num)
{
    WOBJ *o = (WOBJ NEAR *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(WOBJ));
    HLOCAL h;
    WOBJ **t;
    if (!o)
        return NULL;
    h = hobjs ? LocalReAlloc(hobjs, (nobjs + 1) * sizeof(WOBJ *), LMEM_MOVEABLE)
              : LocalAlloc(LMEM_MOVEABLE, sizeof(WOBJ *));
    if (!h) {
        LocalFree((HLOCAL)o);
        return NULL;
    }
    hobjs = h;
    t = (WOBJ **)LocalLock(hobjs);
    t[nobjs++] = o;
    LocalUnlock(hobjs);
    o->cl.lpvtbl = &clvt;
    o->num = num;
    return o;
}

/* the picture paragraph holding object num: its bounds */
static BOOL find_para(DWORD num, DWORD *pf, DWORD *pl)
{
    DWORD i;
    for (i = 0; i < g_doc.nparas; i++) {
        ATOM32 a;
        PAPX p;
        para_get(&g_doc, i, pf, pl, &a);
        pap_decode(&g_doc, a, &p);
        if (p.fGraphics && *pl <= g_doc.cpMac && *pl >= *pf + PICHDR) {
            WORD mm = 0;
            DWORD n = 0;
            doc_get(&g_doc, *pf, (char FAR *)&mm, 2);
            doc_get(&g_doc, *pf + 24, (char FAR *)&n, 4);
            if (mm == 0xE4 && n == num)
                return TRUE;
        }
    }
    return FALSE;
}

static void obj_name(char *s, DWORD num)
{
    int i;
    for (i = 7; i >= 0; i--, num >>= 4)
        s[i] = "0123456789ABCDEF"[(WORD)num & 15];
    s[8] = 0;
}

/* HIMETRIC to twips by way of the screen's pixels, as the MM_HIMETRIC
   and MM_TWIPS mapping modes do it from HORZSIZE/HORZRES (VERTSIZE/
   VERTRES): the size in pixels (rounded), then those pixels in twips
   (px * mm * 14400 / (254 * res), rounded). Black-box (WR10, WR15, WR16), on VGA (208 x 156 mm, 640 x 480):
   1058 x 529 HIMETRIC gives 33 x 16 pixels, stored as 608 x 295;
   5080 x 2540 gives 2874 x 1437; 16931 x 11693 gives 9600 x 6633. */
static int hm2tw(long hm, BOOL vert)
{
    static int mm[2], res[2];
    long px, ext;
    int i = vert ? 1 : 0;
    if (!res[0]) {
        HDC dc = GetDC(NULL);
        mm[0] = GetDeviceCaps(dc, HORZSIZE);
        mm[1] = GetDeviceCaps(dc, VERTSIZE);
        res[0] = GetDeviceCaps(dc, HORZRES);
        res[1] = GetDeviceCaps(dc, VERTRES);
        ReleaseDC(NULL, dc);
    }
    if (hm < 0)
        hm = -hm;
    if (!mm[i] || !res[i])
        return (int)((hm * 1440 + 1270) / 2540);
    ext = (long)mm[i] * 100;
    px = (hm * res[i] * 2 + ext) / (ext * 2);
    /* px * mm * 14400 / (254 * res) with the fraction reduced, so that
       the product stays in a long */
    {
        long n = 14400, d = 254L * res[i], a = n, b = d, t;
        while (b) {
            t = a % b;
            a = b;
            b = t;
        }
        n /= a;
        d /= a;
        return (int)((px * mm[i] * n * 2 + d) / (d * 2));
    }
}

/* store the object's stream (and size) in its paragraph */
static void store(WOBJ *o)
{
    WSTREAM ws;
    DWORD pf, pl, o2;
    BYTE hdr[PICHDR];
    RECT r;
    LONG type = 0;
    ATOM32 pap, chp, dchp;
    DWORD i, rf, rl;
    if (!o->obj || !find_para(o->num, &pf, &pl))
        return;
    m_set(&ws, 0, sizeof ws);
    ws.st.lpstbl = &stvt;
    if (OleSaveToStream(o->obj, &ws.st) != OLE_OK || !ws.outLen) {
        hm_free(&ws.out);
        return;
    }
    doc_get(&g_doc, pf, (char FAR *)hdr, PICHDR);
    *(DWORD *)(hdr + 16) = ws.outLen;
    if (OleQueryType(o->obj, &type) == OLE_OK)
        *(WORD *)(hdr + 6) = type == OT_STATIC ? 1 : type == OT_LINK ? 3 : 2;
    if (OleQueryBounds(o->obj, &r) == OLE_OK && r.right != r.left) {
        *(WORD *)(hdr + 10) = hm2tw((long)r.right - r.left, FALSE);
        *(WORD *)(hdr + 12) = hm2tw((long)r.bottom - r.top, TRUE);
    }
    i = para_at(&g_doc, pf, &rf, &rl, &pap);
    (void)i;
    run_at(&g_doc, pf, &rf, &rl, &chp);
    run_at(&g_doc, pl > pf + PICHDR ? pf + PICHDR : pf, &rf, &rl, &dchp);
    doc_delete(&g_doc, pf, pl - pf);
    doc_insert_para(&g_doc, pf, (const char FAR *)hdr, PICHDR, chp, pap);
    for (o2 = 0; o2 < ws.outLen;) {
        char buf[512];
        WORD k = (WORD)(ws.outLen - o2 > sizeof buf ? sizeof buf : ws.outLen - o2);
        hm_get(&ws.out, o2, buf, k);
        doc_append_para(&g_doc, pf + PICHDR + o2, buf, k, dchp);
        o2 += k;
    }
    hm_free(&ws.out);
    o->dirty = FALSE;
    /* a caret inside the object goes after it (typing never goes into a
       picture's bytes) */
    {
        DWORD end = pf + PICHDR + ws.outLen;
        if (g_selFirst > pf && g_selFirst < end)
            g_selFirst = end;
        if (g_selLim > pf && g_selLim < end)
            g_selLim = end;
    }
    view_changed(pf);
}

int CALLBACK __export ClientCallBack(LPOLECLIENT c, OLE_NOTIFICATION n, LPOLEOBJECT obj)
{
    WOBJ *o = (WOBJ NEAR *)OFFSETOF(c);   /* ours: in our data segment */
    (void)obj;
    switch (n) {
    case OLE_CHANGED:
    case OLE_SAVED:
        o->dirty = TRUE;
        PostMessage(g_frame, WM_USER + 1, 0, (LPARAM)o->num);
        break;
    case OLE_CLOSED:
        PostMessage(g_frame, WM_USER + 1, 1, (LPARAM)o->num);
        break;
    case OLE_QUERY_PAINT:
    case OLE_QUERY_RETRY:
        return TRUE;
    default:
        break;
    }
    return TRUE;
}

/* the frame's WM_USER + 1: an object's data changed (wp 0) or it closed */
void ole_notify(WPARAM wp, LPARAM lp)
{
    WOBJ *o = find_obj((DWORD)lp);
    (void)wp;
    if (o && o->dirty) {
        store(o);
        g_doc.dirty = TRUE;
    }
}

static void wait_release(LPOLEOBJECT obj, OLESTATUS st)
{
    MSG m;
    if (st != OLE_WAIT_FOR_RELEASE)
        return;
    while (OleQueryReleaseStatus(obj) == OLE_BUSY) {
        if (GetMessage(&m, NULL, 0, 0)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* ---- the client document ---- */

void ole_init(void)
{
    char name[PATH_MAX_DOS];
    if (!inited) {
        *(FARPROC *)&clvt.CallBack = MakeProcInstance((FARPROC)ClientCallBack, g_hinst);
        *(FARPROC *)&stvt.Get = MakeProcInstance((FARPROC)StreamGet, g_hinst);
        *(FARPROC *)&stvt.Put = MakeProcInstance((FARPROC)StreamPut, g_hinst);
        inited = TRUE;
    }
    ole_done();
    if (g_path[0])
        s_cpy(name, g_path, sizeof name);
    else
        load_str(IDS_UNTITLED, name, sizeof name);
    if (OleRegisterClientDoc("WINWRITE", name, 0, &lhdoc) != OLE_OK)
        lhdoc = 0;
}

void ole_done(void)
{
    WOBJ **t = objs();
    int i;
    for (i = 0; i < nobjs; i++) {
        if (t[i]->obj)
            wait_release(t[i]->obj, OleDelete(t[i]->obj));
        LocalFree((HLOCAL)t[i]);
    }
    if (hobjs) {
        LocalUnlock(hobjs);
        LocalFree(hobjs);
    }
    hobjs = NULL;
    nobjs = 0;
    if (lhdoc)
        OleRevokeClientDoc(lhdoc);
    lhdoc = 0;
}

void ole_saved(void)
{
    char name[PATH_MAX_DOS];
    if (!lhdoc)
        return;
    s_cpy(name, g_path, sizeof name);
    OleRenameClientDoc(lhdoc, name);
    OleSavedClientDoc(lhdoc);
}

/* The names the server shows: Write's and the document's (WR10: an
   embedded Paintbrush picture's window reads "Paintbrush Picture in
   (Untitled)"). UNVERIFIED: the client name, and a saved document's name
   as its file name without the path. */
static void host_names(LPOLEOBJECT obj)
{
    char app[16], doc[PATH_MAX_DOS];
    const char *p, *base;
    load_str(IDS_WRITE, app, sizeof app);
    if (g_path[0]) {
        for (p = base = g_path; *p; p++)
            if (*p == '\\' || *p == ':')
                base = p + 1;
        s_cpy(doc, base, sizeof doc);
    } else {
        load_str(IDS_UNTITLED, doc, sizeof doc);
    }
    wait_release(obj, OleSetHostNames(obj, app, doc));
}

/* ---- drawing ---- */

static WOBJ *load(DWORD pf, DWORD pl)
{
    DWORD num = 0;
    WORD type = 0;
    WOBJ *o;
    WSTREAM ws;
    char name[9];
    OLESTATUS st;
    doc_get(&g_doc, pf + 24, (char FAR *)&num, 4);
    doc_get(&g_doc, pf + 6, (char FAR *)&type, 2);
    o = find_obj(num);
    if (o)
        return o;
    o = add_obj(num);
    if (!o || !lhdoc)
        return o;
    m_set(&ws, 0, sizeof ws);
    ws.st.lpstbl = &stvt;
    ws.cp = pf + PICHDR;
    ws.lim = pl;
    obj_name(name, num);
    st = OleLoadFromStream(&ws.st, type == 1 ? "Static" : "StdFileEditing", &o->cl, lhdoc,
                           name, &o->obj);
    if (st == OLE_WAIT_FOR_RELEASE)
        wait_release(o->obj, st);
    else if (st != OLE_OK)
        o->obj = NULL;
    o->failed = o->obj == NULL;
    return o;
}

/* draw the object paragraph [pf, pl) into r; FALSE: draw it otherwise */
BOOL ole_draw(HDC dc, DWORD pf, DWORD pl, const RECT *r)
{
    WOBJ *o;
    if (!lhdoc)
        return FALSE;
    o = load(pf, pl);
    if (!o || !o->obj)
        return FALSE;
    return OleDraw(o->obj, dc, r, NULL, NULL) == OLE_OK;
}

/* double-click: the server opens the object */
void ole_activate(DWORD pf, DWORD pl, const RECT *r)
{
    WOBJ *o = load(pf, pl);
    if (o && o->obj) {
        wait_release(o->obj, OleActivate(o->obj, OLEVERB_PRIMARY, TRUE, TRUE, g_docwnd, r));
        host_names(o->obj);
    }
}

/* ---- Insert Object ---- */

static char newClass[64];

BOOL CALLBACK __export InsertObjDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        HKEY root;
        char key[64], val[128], path[96];
        DWORD i;
        HWND lb = GetDlgItem(d, 265);
        /* the registration database: every class with a StdFileEditing
           server, by its readable name; the class name kept as item data */
        if (RegOpenKey(HKEY_CLASSES_ROOT, NULL, &root) == ERROR_SUCCESS) {
            for (i = 0; RegEnumKey(root, i, key, sizeof key) == ERROR_SUCCESS; i++) {
                LONG cb = sizeof val;
                HKEY k;
                if (key[0] == '.')
                    continue;
                s_cpy(path, key, sizeof path);
                s_cat(path, "\\protocol\\StdFileEditing\\server", sizeof path);
                if (RegOpenKey(HKEY_CLASSES_ROOT, path, &k) != ERROR_SUCCESS)
                    continue;
                RegCloseKey(k);
                if (RegQueryValue(HKEY_CLASSES_ROOT, key, val, &cb) != ERROR_SUCCESS || !val[0])
                    continue;
                {
                    int ix = (int)SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)val);
                    char *c = (char NEAR *)LocalAlloc(LMEM_FIXED, s_len(key) + 1);
                    if (c) {
                        s_cpy(c, key, s_len(key) + 1);
                        SendMessage(lb, LB_SETITEMDATA, ix, (LPARAM)(WORD)c);    /* castcheck: ok (a near pointer as data) */
                    }
                }
            }
            RegCloseKey(root);
        }
        SendMessage(lb, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        HWND lb = GetDlgItem(d, 265);
        if (wp == IDOK || (wp == 265 && HIWORD(lp) == LBN_DBLCLK)) {
            int ix = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
            if (ix >= 0) {
                char *c = (char NEAR *)(WORD)SendMessage(lb, LB_GETITEMDATA, ix, 0);
                s_cpy(newClass, c, sizeof newClass);
            }
            wp = ix >= 0 ? IDOK : IDCANCEL;
        }
        if (wp == IDOK || wp == IDCANCEL) {
            int i, n = (int)SendMessage(lb, LB_GETCOUNT, 0, 0);
            for (i = 0; i < n; i++)
                LocalFree((HLOCAL)(WORD)SendMessage(lb, LB_GETITEMDATA, i, 0));
            EndDialog(d, wp);
            return TRUE;
        }
    }
    return FALSE;
}

/* A new object paragraph at the caret (type 1 static, 2 embedded): its
   table entry, its name in name; the caret goes after it */
static WOBJ *new_obj_para(WORD type, char *name)
{
    DWORD num, cp, pf, pl;
    WOBJ *o;
    BYTE hdr[PICHDR];
    ATOM32 pa, chp;
    PAPX p;
    CHPX c;
    /* the object's number: the clock, as the original's (ARCHITECTURE.md
       16.6), never the same twice */
    num = GetCurrentTime();
    if (num <= lastNum)
        num = lastNum + 1;
    lastNum = num;
    o = add_obj(num);
    if (!o)
        return NULL;
    /* a paragraph of its own at the caret: after a paragraph mark first
       when the caret is inside a paragraph (UNVERIFIED) */
    if (g_selLim > g_selFirst)
        doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
    cp = g_selFirst;
    para_at(&g_doc, cp, &pf, &pl, &pa);
    sel_props(&c);
    chp = chp_atom(&g_doc, &c);
    if (cp != pf) {
        doc_insert(&g_doc, cp, "\r\n", 2, chp);
        cp += 2;
    }
    pap_default(&p);
    p.rhc = 0x10;                       /* a picture (SDK ch. 8, WR7) */
    p.fGraphics = TRUE;
    pa = pap_atom(&g_doc, &p);
    m_set(hdr, 0, sizeof hdr);
    *(WORD *)hdr = 0xE4;
    *(WORD *)(hdr + 6) = type;
    *(DWORD *)(hdr + 24) = num;
    *(WORD *)(hdr + 30) = PICHDR;
    *(WORD *)(hdr + 36) = 1000;
    *(WORD *)(hdr + 38) = 1000;
    /* in the typing properties (WR10: one run with the text around it) */
    doc_insert_para(&g_doc, cp, (const char FAR *)hdr, PICHDR, chp, pa);
    g_doc.ident = 0xBE32;
    g_selFirst = g_selLim = cp + PICHDR;
    view_changed(cp);
    obj_name(name, num);
    return o;
}

void cmd_insertobj(void)
{
    FARPROC fp;
    int r;
    WOBJ *o;
    char name[9];
    if (!lhdoc)
        return;
    fp = MakeProcInstance((FARPROC)InsertObjDlgProc, g_hinst);
    r = DialogBox(g_hinst, "DTCREATE", g_frame, (DLGPROC)fp);
    FreeProcInstance(fp);
    if (r != IDOK || !newClass[0])
        return;
    o = new_obj_para(2, name);
    if (!o)
        return;
    r = OleCreate("StdFileEditing", &o->cl, newClass, lhdoc, name, &o->obj, olerender_draw, 0);
    if (r == OLE_WAIT_FOR_RELEASE)
        wait_release(o->obj, r);
    else if (r != OLE_OK) {
        o->obj = NULL;
        o->failed = TRUE;
    }
    if (o->obj)
        host_names(o->obj);
}

/* Paste: an object on the clipboard (Native and OwnerLink) is embedded;
   a picture alone (a bitmap, a DIB, a metafile picture) becomes a static
   object (WR7: 3.1 stores a pasted picture as one). TRUE when done. */
BOOL ole_paste(void)
{
    WOBJ *o;
    char name[9];
    const char *proto;
    OLESTATUS st;
    if (!lhdoc)
        return FALSE;
    if (OleQueryCreateFromClip("StdFileEditing", olerender_draw, 0) == OLE_OK)
        proto = "StdFileEditing";
    else if (!IsClipboardFormatAvailable(CF_TEXT) &&
             OleQueryCreateFromClip("Static", olerender_draw, 0) == OLE_OK)
        proto = "Static";
    else
        return FALSE;
    o = new_obj_para((WORD)(proto[0] == 'S' && proto[1] == 't' && proto[2] == 'a' ? 1 : 2), name);
    if (!o)
        return TRUE;
    if (!OpenClipboard(g_frame))
        return TRUE;
    st = OleCreateFromClip(proto, &o->cl, lhdoc, name, &o->obj, olerender_draw, 0);
    CloseClipboard();
    if (st == OLE_WAIT_FOR_RELEASE)
        wait_release(o->obj, st);
    else if (st != OLE_OK)
        o->obj = NULL;
    o->failed = o->obj == NULL;
    if (o->obj) {
        o->dirty = TRUE;
        store(o);
    }
    return TRUE;
}

/* ---- links: Paste Link and Edit > Links (WR30) ---- */

/* Paste Link: enabled when the clipboard offers a link (ObjectLink: WR30
   E1); the object a linked one (header type 3, stream format 1) */
BOOL ole_can_pastelink(void)
{
    return lhdoc && OleQueryLinkFromClip("StdFileEditing", olerender_draw, 0) == OLE_OK;
}

void ole_pastelink(void)
{
    WOBJ *o;
    char name[9];
    OLESTATUS st;
    if (!ole_can_pastelink())
        return;
    /* the caret off until it blinks, as after Paste (WR30 L1) */
    view_caret_defer();
    undo_save(IDS_EDITING);
    o = new_obj_para(3, name);
    if (!o || !OpenClipboard(g_frame))
        return;
    st = OleCreateLinkFromClip("StdFileEditing", &o->cl, lhdoc, name, &o->obj, olerender_draw, 0);
    CloseClipboard();
    if (st == OLE_WAIT_FOR_RELEASE)
        wait_release(o->obj, st);
    else if (st != OLE_OK)
        o->obj = NULL;
    o->failed = o->obj == NULL;
    if (o->obj) {
        o->dirty = TRUE;
        store(o);
    }
    g_doc.dirty = TRUE;
}

/* The Links box (the original's DTPROP): one line per linked object,
   "class name<tab>document<tab>item<tab>update", the class's name from
   the registration database ("Paintbrush Picture"), the document without
   its path, the update "Automatic", "Manual" or "Unavailable" (strings
   34-36; WR30 K1). Automatic and Manual set the selected links' update
   option; Update Now updates them (a failure: string 8470, the link
   "Unavailable"); Cancel Link turns them into static pictures; Change
   Link names another document (COMMDLG Open); Activate and Edit open
   them. UNVERIFIED: whether Cancel undoes what the box did (here every
   button acts at once). */
#define LK_LIST     265
#define LK_AUTO     258
#define LK_MANUAL   259
#define LK_UPDATE   1031
#define LK_CANCEL   1032
#define LK_CHANGE   262
#define LK_ACTIVATE 273
#define LK_EDIT     1030

static HLOCAL hlinks;                   /* WOBJ * of each line */
static int nlinks;
static BOOL *unavail;                   /* per line: an update failed */

static WOBJ *link_obj(int i)
{
    WOBJ *o = ((WOBJ **)LocalLock(hlinks))[i];
    LocalUnlock(hlinks);
    return o;
}

static UINT cf_objectlink(void)
{
    return RegisterClipboardFormat("ObjectLink");
}

/* the object's class, document and item (ObjectLink) */
static BOOL link_names(WOBJ *o, char *cls, char *doc, char *item)
{
    HANDLE h = NULL;
    char FAR *p;
    cls[0] = doc[0] = item[0] = 0;
    if (!o->obj || OleGetData(o->obj, cf_objectlink(), &h) != OLE_OK || !h)
        return FALSE;
    p = GlobalLock(h);
    if (!p)
        return FALSE;
    s_cpy(cls, p, 64);
    p += s_len(p) + 1;
    s_cpy(doc, p, PATH_MAX_DOS);
    p += s_len(p) + 1;
    s_cpy(item, p, 64);
    GlobalUnlock(h);
    return TRUE;
}

static void link_line(HWND d, int i, BOOL replace)
{
    WOBJ *o = link_obj(i);
    char cls[65], doc[PATH_MAX_DOS], item[65], name[80], line[300], *b;
    LONG cb = sizeof name;
    OLEOPT_UPDATE opt = oleupdate_always;
    link_names(o, cls, doc, item);
    if (RegQueryValue(HKEY_CLASSES_ROOT, cls, name, &cb) != ERROR_SUCCESS || !name[0])
        s_cpy(name, cls, sizeof name);
    for (b = doc; *b; b++)
        ;
    while (b > doc && b[-1] != '\\' && b[-1] != ':')
        b--;
    s_cpy(line, name, sizeof line);
    s_cat(line, "\t", sizeof line);
    s_cat(line, b, sizeof line);
    s_cat(line, "\t", sizeof line);
    s_cat(line, item, sizeof line);
    s_cat(line, "\t", sizeof line);
    if (o->obj)
        OleGetLinkUpdateOptions(o->obj, &opt);
    load_str(unavail[i] ? IDS_UNAVAILABLE : opt == oleupdate_always ? IDS_AUTOMATIC : IDS_MANUAL,
             line + s_len(line), (int)(sizeof line - s_len(line)));
    if (replace) {
        BOOL sel = (BOOL)SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0);
        SendDlgItemMessage(d, LK_LIST, LB_DELETESTRING, i, 0);
        SendDlgItemMessage(d, LK_LIST, LB_INSERTSTRING, i, (LPARAM)(LPSTR)line);
        SendDlgItemMessage(d, LK_LIST, LB_SETSEL, sel, MAKELPARAM(i, 0));
    } else {
        SendDlgItemMessage(d, LK_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)line);
    }
}

/* the radio buttons show the first selected link's option; with no
   link selected they and the buttons for links are grayed (WR30 K3) */
static void link_radios(HWND d)
{
    static const int ids[] = { LK_AUTO, LK_MANUAL, LK_UPDATE, LK_CANCEL, LK_CHANGE,
                               LK_ACTIVATE, LK_EDIT };
    int i, any = 0;
    for (i = 0; i < nlinks; i++)
        if (SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0))
            any = 1;
    for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
        EnableWindow(GetDlgItem(d, ids[i]), any);
    for (i = 0; i < nlinks; i++)
        if (SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0)) {
            WOBJ *o = link_obj(i);
            OLEOPT_UPDATE opt = oleupdate_always;
            if (o->obj)
                OleGetLinkUpdateOptions(o->obj, &opt);
            CheckRadioButton(d, LK_AUTO, LK_MANUAL, opt == oleupdate_always ? LK_AUTO : LK_MANUAL);
            return;
        }
    CheckRadioButton(d, LK_AUTO, LK_MANUAL, 0);
}

static ATOM32 chp_plain(DOC *d, ATOM32 a, WORD unused)
{
    CHPX c;
    (void)a;
    (void)unused;
    chp_default(&c);
    return chp_atom(d, &c);
}

/* a linked object made a static one (Cancel Link); its characters lose
   their properties (WR30C: the run's FOD FFFFh) */
static void link_cancel(WOBJ *o)
{
    LPOLEOBJECT st = NULL;
    char name[9];
    OLESTATUS r;
    DWORD pf, pl;
    if (!o->obj)
        return;
    obj_name(name, o->num);
    r = OleObjectConvert(o->obj, "Static", &o->cl, lhdoc, name, &st);
    if (r == OLE_WAIT_FOR_RELEASE)
        wait_release(st, r);
    else if (r != OLE_OK)
        return;
    if (!st)
        return;
    wait_release(o->obj, OleDelete(o->obj));
    o->obj = st;
    if (find_para(o->num, &pf, &pl)) {
        WORD one = 1;
        doc_put(&g_doc, pf + 6, &one, 2);
    }
    o->dirty = TRUE;
    store(o);
    if (find_para(o->num, &pf, &pl))
        doc_set_chp(&g_doc, pf, pl, chp_plain, 0);
}

static void link_each(HWND d, WORD id)
{
    int i;
    for (i = 0; i < nlinks; i++) {
        WOBJ *o;
        OLESTATUS r;
        if (!SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0))
            continue;
        o = link_obj(i);
        if (!o->obj)
            continue;
        switch (id) {
        case LK_AUTO:
        case LK_MANUAL:
            r = OleSetLinkUpdateOptions(o->obj, id == LK_AUTO ? oleupdate_always : oleupdate_oncall);
            wait_release(o->obj, r);
            o->dirty = TRUE;
            store(o);
            break;
        case LK_UPDATE:
            r = OleUpdate(o->obj);
            wait_release(o->obj, r);
            if (r != OLE_OK && r != OLE_WAIT_FOR_RELEASE) {
                msgbox(d, IDS_NOSOURCE, NULL, MB_OK | MB_ICONEXCLAMATION);
                unavail[i] = TRUE;
            } else {
                unavail[i] = FALSE;
                o->dirty = TRUE;
                store(o);
            }
            break;
        default:                        /* Activate, Edit */
            wait_release(o->obj, OleActivate(o->obj, OLEVERB_PRIMARY, TRUE, TRUE, g_docwnd, NULL));
            break;
        }
        if (id != LK_ACTIVATE && id != LK_EDIT)
            link_line(d, i, TRUE);
    }
    g_doc.dirty = TRUE;
}

static void link_change(HWND d)
{
    OPENFILENAME ofn;
    char path[PATH_MAX_DOS];
    int i;
    path[0] = 0;
    m_set(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = d;
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof path;
    ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
    if (!GetOpenFileName(&ofn))
        return;
    for (i = 0; i < nlinks; i++) {
        WOBJ *o;
        char cls[65], doc[PATH_MAX_DOS], item[65];
        HGLOBAL h;
        char FAR *p;
        int n;
        if (!SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0))
            continue;
        o = link_obj(i);
        if (!link_names(o, cls, doc, item))
            continue;
        n = s_len(cls) + s_len(path) + s_len(item) + 4;
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n);
        if (!h)
            continue;
        p = GlobalLock(h);
        s_cpy(p, cls, n);
        s_cpy(p + s_len(cls) + 1, path, n);
        s_cpy(p + s_len(cls) + s_len(path) + 2, item, n);
        GlobalUnlock(h);
        wait_release(o->obj, OleSetData(o->obj, cf_objectlink(), h));
        wait_release(o->obj, OleUpdate(o->obj));
        o->dirty = TRUE;
        store(o);
        link_line(d, i, TRUE);
    }
    g_doc.dirty = TRUE;
}

BOOL CALLBACK __export LinksDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 0; i < nlinks; i++)
            link_line(d, i, FALSE);
        /* every link selected (WR30 K1: the one there highlighted) */
        if (nlinks)
            SendDlgItemMessage(d, LK_LIST, LB_SELITEMRANGE, TRUE, MAKELPARAM(0, nlinks - 1));
        link_radios(d);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case LK_LIST:
            if (HIWORD(lp) == LBN_SELCHANGE)
                link_radios(d);
            return TRUE;
        case LK_AUTO:
        case LK_MANUAL:
            CheckRadioButton(d, LK_AUTO, LK_MANUAL, wp);
            link_each(d, wp);
            return TRUE;
        case LK_UPDATE:
        case LK_ACTIVATE:
        case LK_EDIT:
            link_each(d, wp);
            return TRUE;
        case LK_CANCEL:
            for (i = nlinks - 1; i >= 0; i--)
                if (SendDlgItemMessage(d, LK_LIST, LB_GETSEL, i, 0)) {
                    WOBJ **t = (WOBJ **)LocalLock(hlinks);
                    WOBJ *o = t[i];
                    int k;
                    for (k = i; k + 1 < nlinks; k++) {
                        t[k] = t[k + 1];
                        unavail[k] = unavail[k + 1];
                    }
                    LocalUnlock(hlinks);
                    nlinks--;
                    SendDlgItemMessage(d, LK_LIST, LB_DELETESTRING, i, 0);
                    link_cancel(o);
                }
            g_doc.dirty = TRUE;
            link_radios(d);
            return TRUE;
        case LK_CHANGE:
            link_change(d);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(d, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void cmd_links(void)
{
    DWORD i, pf, pl;
    ATOM32 a;
    PAPX p;
    FARPROC fp;
    if (!lhdoc)
        return;
    nlinks = 0;
    hlinks = LocalAlloc(LMEM_MOVEABLE, sizeof(WOBJ *));
    if (!hlinks)
        return;
    for (i = 0; i < g_doc.nparas; i++) {
        WORD type = 0, mm = 0;
        WOBJ *o;
        HLOCAL h;
        para_get(&g_doc, i, &pf, &pl, &a);
        pap_decode(&g_doc, a, &p);
        if (!p.fGraphics || pl < pf + PICHDR || pl > g_doc.cpMac)
            continue;
        doc_get(&g_doc, pf, (char FAR *)&mm, 2);
        doc_get(&g_doc, pf + 6, (char FAR *)&type, 2);
        if (mm != 0xE4 || type != 3)
            continue;
        o = load(pf, pl);
        if (!o)
            continue;
        h = LocalReAlloc(hlinks, (nlinks + 1) * sizeof(WOBJ *), LMEM_MOVEABLE);
        if (!h)
            break;
        hlinks = h;
        ((WOBJ **)LocalLock(hlinks))[nlinks++] = o;
        LocalUnlock(hlinks);
    }
    unavail = (BOOL *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (nlinks + 1) * sizeof(BOOL));
    if (unavail) {
        undo_save(IDS_EDITING);
        fp = MakeProcInstance((FARPROC)LinksDlgProc, g_hinst);
        DialogBox(g_hinst, "DTPROP", g_frame, (DLGPROC)fp);
        FreeProcInstance(fp);
        LocalFree((HLOCAL)unavail);
        unavail = NULL;
    }
    LocalFree(hlinks);
    hlinks = NULL;
    nlinks = 0;
}
