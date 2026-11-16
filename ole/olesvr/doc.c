/*
 * doc.c - OLESVR's documents and objects: OleRegisterServerDoc and the
 * other document APIs, OleRevokeObject, the objects' OLECLIENT
 * (ITEMCALLBACK) and the document topics (DocWndClass windows) of the
 * OLE 1.0 DDE protocol (ARCHITECTURE.md 16.2, observed on Windows 3.1
 * with tests/olesvr).
 */
#include "olesvr.h"

extern BOOL WINAPI IsBadReadPtr(const void FAR *, UINT);
extern BOOL WINAPI IsBadWritePtr(void FAR *, UINT);

static OLECLIENTVTBL item_vtbl = { ITEMCALLBACK };

/* ---- lists ---- */

DOC FARP *doc_from(LHSERVERDOC lh)
{
    SRV FARP *s;
    DOC FARP *d;
    if (!lh)
        return NULL;
    for (s = svr_list; s; s = s->next)
        for (d = s->docs; d; d = d->next)
            if ((LHSERVERDOC)d == lh && !d->released)
                return d;
    return NULL;
}

/* by atom, as Windows 3.1 does: a name that is no atom finds the
   document named "" (observed) */
DOC FARP *doc_find(SRV FARP *s, const char FARP *name)
{
    ATOM a = name && *name ? GlobalFindAtom(name) : 0;
    DOC FARP *d;
    for (d = s->docs; d; d = d->next)
        if (d->atom == a && !d->revoking)
            return d;
    return NULL;
}

static ITEM FARP *item_from(LPOLECLIENT cl)
{
    SRV FARP *s;
    DOC FARP *d;
    ITEM FARP *it;
    if (!cl)
        return NULL;
    for (s = svr_list; s; s = s->next)
        for (d = s->docs; d; d = d->next)
            for (it = d->items; it; it = it->next)
                if (&it->cl == cl)
                    return it;
    return NULL;
}

DOC FARP *doc_new(SRV FARP *s, const char FARP *name, LPOLESERVERDOC lpdoc)
{
    DOC FARP *d = mem_alloc(sizeof *d), FARP *FARP *pp;
    if (!d)
        return NULL;
    d->magic = DOC_MAGIC;
    d->srv = s;
    d->lpdoc = lpdoc;
    d->name = str_dup(name ? name : "");
    d->atom = name && *name ? GlobalAddAtom(name) : 0;
    /* UNVERIFIED: the window's title and style (it is never shown) */
    /* a child of the server's window titled "Doc" (observed), which
       passes it the INITIATEs for document topics (in the order of the
       documents: observed, the oldest answers first) */
    d->hwnd = CreateWindow("DocWndClass", "Doc", WS_CHILD, 0, 0, 0, 0, s->hwnd, NULL,
                           svr_inst, NULL);
    if (!d->name || !d->hwnd) {
        if (d->hwnd)
            DestroyWindow(d->hwnd);
        if (d->atom)
            GlobalDeleteAtom(d->atom);
        mem_free(d->name);
        mem_free(d);
        return NULL;
    }
    SetWindowLong(d->hwnd, 0, (LONG)d);
    for (pp = &s->docs; *pp; pp = &(*pp)->next)   /* in order of creation */
        ;
    *pp = d;
    return d;
}

static void adv_free_all(ADV FARP *FARP *l)
{
    while (*l) {
        ADV FARP *a = *l;
        *l = a->next;
        mem_free(a);
    }
}

static void adv_drop_client(ADV FARP *FARP *l, HWND client)
{
    while (*l) {
        if ((*l)->client == client) {
            ADV FARP *a = *l;
            *l = a->next;
            mem_free(a);
        } else
            l = &(*l)->next;
    }
}

static void item_free(ITEM FARP *it)
{
    if (it->hwnd && IsWindow(it->hwnd)) {
        SetWindowLong(it->hwnd, 0, 0);
        DestroyWindow(it->hwnd);
    }
    adv_free_all(&it->advs);
    while (it->users)
        conv_remove(&it->users, it->users->client);
    mem_free(it);
}

/* Frees a document: Release (unless the document never got one from the
   server, or the server is gone), its window, atom and lists. */
void doc_destroy(DOC FARP *d)
{
    SRV FARP *s = d->srv;
    DOC FARP *FARP *pp;
    if (d->lpdoc && !d->released) {
        d->released = TRUE;
        d->lpdoc->lpvtbl->Release(d->lpdoc);
    }
    d->released = TRUE;
    for (pp = &s->docs; *pp; pp = &(*pp)->next)
        if (*pp == d) {
            *pp = d->next;
            break;
        }
    while (d->items) {
        ITEM FARP *it = d->items;
        d->items = it->next;
        item_free(it);
    }
    adv_free_all(&d->renames);
    while (d->convs)
        conv_remove(&d->convs, d->convs->client);
    if (d->target)
        GlobalFree(d->target);
    if (d->atom)
        GlobalDeleteAtom(d->atom);
    if (d->hwnd && IsWindow(d->hwnd)) {
        SetWindowLong(d->hwnd, 0, 0);
        DestroyWindow(d->hwnd);
    }
    mem_free(d->name);
    mem_free(d);
}

/* a revoked document whose conversations are all gone */
static void doc_finish(DOC FARP *d)
{
    SRV FARP *s = d->srv;
    doc_destroy(d);
    if (s->busy)
        return;
    if (s->revoking)
        srv_try_finish(s);
    else
        srv_check_release(s);
}

void conv_post_terms(HWND from, CONV FARP *l)
{
    for (; l; l = l->next)
        if (l->terminating && !l->term_posted) {
            l->term_posted = TRUE;
            PostMessage(l->client, WM_DDE_TERMINATE, (WPARAM)from, 0);
        }
}

/* Revokes a document: its objects are released at once, its clients get
   WM_DDE_TERMINATE once the message being handled is done (observed: the
   ACK of a StdCloseDocument comes first), and the document is released
   when the last one has answered. */
OLESTATUS doc_revoke(DOC FARP *d)
{
    CONV FARP *c;
    d->revoking = TRUE;
    while (d->items) {
        ITEM FARP *it = d->items;
        d->items = it->next;
        if (it->obj)
            it->obj->lpvtbl->Release(it->obj);
        item_free(it);
    }
    for (c = d->convs; c; c = c->next)
        c->terminating = TRUE;
    if (d->convs) {
        PostMessage(d->hwnd, WM_SVR_TERMINATE, 0, 0);
        return OLE_WAIT_FOR_RELEASE;
    }
    doc_finish(d);
    return OLE_OK;
}

/* ---- objects ---- */

ITEM FARP *item_get(DOC FARP *d, const char FARP *name, HWND client, OLESTATUS FARP *st)
{
    ITEM FARP *it, FARP *FARP *pp;
    LPOLEOBJECT obj = NULL;
    for (it = d->items; it; it = it->next)
        if (str_ieq(it->name, name))
            break;
    if (!it) {
        if (!d->lpdoc) {
            *st = OLE_ERROR_OBJECT;
            return NULL;
        }
        it = mem_alloc(sizeof *it + str_len(name));
        if (!it) {
            *st = OLE_ERROR_MEMORY;
            return NULL;
        }
        it->cl.lpvtbl = &item_vtbl;
        it->doc = d;
        str_cpy(it->name, name);
        *st = d->lpdoc->lpvtbl->GetObject(d->lpdoc, (LPSTR)name, &obj, &it->cl);
        if (*st != OLE_OK || !obj) {
            if (*st == OLE_OK)
                *st = OLE_ERROR_OBJECT;
            mem_free(it);
            return NULL;
        }
        it->obj = obj;
        /* each object has a child window of its document's, "ITEM"
           (observed) */
        it->hwnd = CreateWindow("ItemWndClass", "ITEM", WS_CHILD, 0, 0, 0, 0, d->hwnd, NULL,
                                svr_inst, NULL);
        if (it->hwnd)
            SetWindowLong(it->hwnd, 0, (LONG)it);
        for (pp = &d->items; *pp; pp = &(*pp)->next)
            ;
        *pp = it;
    }
    if (client)
        conv_add(&it->users, client);
    *st = OLE_OK;
    return it;
}

/* whether the object renders format cf: EnumFormats from the start
   until cf or the end (as observed, every time) */
static BOOL item_has_format(ITEM FARP *it, WORD cf)
{
    OLECLIPFORMAT f = 0, n;
    for (;;) {
        n = it->obj->lpvtbl->EnumFormats(it->obj, f);
        if (n == cf)
            return cf != 0;
        if (!n || n == f)
            return FALSE;
        f = n;
    }
}

/* GetData, after handing the object the target device the client set
   since it last got one (observed: StdTargetDevice reaches the object
   only with the next request for data) */
static OLESTATUS item_data(ITEM FARP *it, WORD cf, HANDLE FAR *h)
{
    DOC FARP *d = it->doc;
    *h = NULL;
    if (d->target && it->target_gen != d->target_gen) {
        DWORD n = GlobalSize(d->target);
        HGLOBAL t = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
        void FAR *dst = t ? GlobalLock(t) : NULL;
        const void FAR *src = GlobalLock(d->target);
        it->target_gen = d->target_gen;
        if (dst && src) {
            huge_copy(dst, src, n);
            GlobalUnlock(t);
            it->obj->lpvtbl->SetTargetDevice(it->obj, t);
        } else if (t)
            GlobalFree(t);
        GlobalUnlock(d->target);
    }
    return it->obj->lpvtbl->GetData(it->obj, cf, h);
}

OLESTATUS item_show(ITEM FARP *it, BOOL focus)
{
    return it->obj->lpvtbl->Show(it->obj, focus);
}

/* one WM_DDE_DATA for a notification (fRelease; observed: never
   fAckReq, whatever the advise asked; no data when the advise deferred
   updates) */
static void send_notify(ITEM FARP *it, HWND client, WORD cf, BOOL defer, const char FARP *text)
{
    DOC FARP *d = it->doc;
    HANDLE h = NULL;
    HGLOBAL b = NULL;
    ATOM at;
    if (!defer) {
        if (item_data(it, cf, &h) != OLE_OK)
            return;
        b = data_block(0x2000, cf, h);
        if (!b)
            return;
    }
    at = GlobalAddAtom(text);
    if (!PostMessage(client, WM_DDE_DATA, (WPARAM)d->hwnd, MAKELPARAM(b, at))) {
        if (b)
            GlobalFree(b);
        if (at)
            GlobalDeleteAtom(at);
    }
}

/* A change, save or close of an object, for every client that advised
   on it for that (observed): the client gets every format it advised on
   the object, whichever kind of advise named it, under the item name
   ("item", "item/Save", "item/Close"), deferred or not as its latest
   advise of that kind said. */
void item_notify(ITEM FARP *it, int kind)
{
    ADV FARP *a, FARP *b, FARP *opt;
    char text[300];
    static const char *suffix[] = { "", "/Save", "/Close" };
    for (a = it->advs; a; a = a->next) {
        CONV FARP *c;
        /* each client once: at its first advise */
        for (b = it->advs; b != a; b = b->next)
            if (b->client == a->client)
                break;
        if (b != a)
            continue;
        c = conv_find(it->doc->convs, a->client);
        if (!c || c->terminating)
            continue;
        opt = NULL;
        for (b = a; b; b = b->next)
            if (b->client == a->client && b->kind == kind)
                opt = b;
        if (!opt)
            continue;
        if (str_len(it->name) + 8 > (int)sizeof text)
            continue;                   /* UNVERIFIED: names that long */
        str_cpy(text, it->name);
        str_cpy(text + str_len(text), suffix[kind]);
        /* every format of the client's advises, in order, once */
        for (b = a; b; b = b->next) {
            ADV FARP *e;
            if (b->client != a->client)
                continue;
            for (e = a; e != b; e = e->next)
                if (e->client == a->client && e->cf == b->cf)
                    break;
            if (e != b)
                continue;
            send_notify(it, a->client, b->cf, (opt->flags & 0x4000) != 0, text);
        }
    }
}

/* OLE_RENAMED (observed): the object's ObjectLink is fetched, and every
   conversation that used the object is terminated */
static void item_renamed(ITEM FARP *it)
{
    DOC FARP *d = it->doc;
    CONV FARP *u;
    HANDLE h = NULL;
    if (item_has_format(it, cf_objectlink) && item_data(it, cf_objectlink, &h) == OLE_OK && h)
        GlobalFree(h);
    for (u = it->users; u; u = u->next) {
        CONV FARP *c = conv_find(d->convs, u->client);
        if (c) {
            c->terminating = c->term_posted = TRUE;
            PostMessage(c->client, WM_DDE_TERMINATE, (WPARAM)d->hwnd, 0);
        }
    }
}

int FAR PASCAL __loadds ITEMCALLBACK(LPOLECLIENT cl, OLE_NOTIFICATION n, LPOLEOBJECT obj)
{
    ITEM FARP *it = item_from(cl);
    (void)obj;
    if (!it)
        return OLE_ERROR_HANDLE;        /* UNVERIFIED: a revoked object */
    switch (n) {
    case OLE_CHANGED:
        item_notify(it, ADV_CHANGE);
        break;
    case OLE_SAVED:
        item_notify(it, ADV_SAVE);
        break;
    case OLE_CLOSED:
        item_notify(it, ADV_CLOSE);
        break;
    case OLE_RENAMED:
        item_renamed(it);
        break;
    default:
        break;
    }
    return OLE_OK;
}

/* ---- the document topic ---- */

static void atom_name(ATOM a, char FARP *buf, int n)
{
    buf[0] = 0;
    if (a)
        GlobalGetAtomName(a, buf, n);
}

/* "item/Save" -> kind; FALSE for an unknown suffix */
static BOOL split_item(char FARP *name, WORD FARP *kind)
{
    char FARP *p, FARP *slash = NULL;
    for (p = name; *p; p++)
        if (*p == '/')
            slash = p;
    *kind = ADV_CHANGE;
    if (!slash)
        return TRUE;
    if (str_ieq(slash + 1, "Change"))
        *kind = ADV_CHANGE;
    else if (str_ieq(slash + 1, "Save"))
        *kind = ADV_SAVE;
    else if (str_ieq(slash + 1, "Close"))
        *kind = ADV_CLOSE;
    else
        return FALSE;
    *slash = 0;
    return TRUE;
}

static ADV FARP *adv_new(HWND client, WORD kind, WORD cf, WORD flags, const char FARP *text)
{
    ADV FARP *a = mem_alloc(sizeof *a + str_len(text));
    if (a) {
        a->client = client;
        a->kind = kind;
        a->cf = cf;
        a->flags = flags;
        str_cpy(a->text, text);
    }
    return a;
}

/* adds an advise, replacing the same one of the same client */
static BOOL adv_add(ADV FARP *FARP *l, ADV FARP *a)
{
    ADV FARP *FARP *pp;
    if (!a)
        return FALSE;
    for (pp = l; *pp; pp = &(*pp)->next)
        if ((*pp)->client == a->client && (*pp)->cf == a->cf && str_ieq((*pp)->text, a->text)) {
            a->next = (*pp)->next;
            mem_free(*pp);
            *pp = a;
            return TRUE;
        }
    *pp = a;
    return TRUE;
}

static BOOL adv_remove(ADV FARP *FARP *l, HWND client, WORD cf, const char FARP *text)
{
    BOOL any = FALSE;
    while (*l) {
        ADV FARP *a = *l;
        if (a->client == client && (!cf || a->cf == cf) && str_ieq(a->text, text)) {
            *l = a->next;
            mem_free(a);
            any = TRUE;
        } else
            l = &a->next;
    }
    return any;
}

static WORD doc_execute(DOC FARP *d, HWND client, HGLOBAL hcmd)
{
    const char FARP *src = hcmd ? (const char FARP *)GlobalLock(hcmd) : NULL;
    char FARP *t, FARP *rest, FARP *arg[4];
    char name[32];
    WORD status = 0;
    OLESTATUS r;
    ITEM FARP *it;
    int n;
    if (!src)
        return 0;
    t = tmp_dup(src);
    GlobalUnlock(hcmd);
    if (!t)
        return 0;
    cmd_name(t, name, sizeof name);
    if (t[0] != '[')
        goto done;
    if (str_ieq(name, "StdShowItem")) {
        if (!one_command(t) || (n = cmd_args(t, "s|b", arg, &rest)) < 0 || !cmd_end(rest))
            goto done;
        it = item_get(d, arg[0], client, &r);
        /* observed: the boolean is the inverse of fTakeFocus */
        if (it && item_show(it, n > 1 ? !arg_bool(arg[1]) : TRUE) == OLE_OK)
            status = 0x8000;
    } else if (str_ieq(name, "StdDoVerbItem")) {
        if (!one_command(t) || cmd_args(t, "snbb", arg, &rest) != 4 || !cmd_end(rest))
            goto done;
        it = item_get(d, arg[0], client, &r);
        if (it && it->obj->lpvtbl->DoVerb(it->obj, arg_num(arg[1]), arg_bool(arg[2]),
                                          !arg_bool(arg[3])) == OLE_OK)
            status = 0x8000;
    } else if (str_ieq(name, "StdCloseDocument")) {
        if (!str_ieq(t, "[StdCloseDocument]") || !d->lpdoc)
            goto done;
        if (d->lpdoc->lpvtbl->Close(d->lpdoc) == OLE_OK)
            status = 0x8000;
    } else if (str_ieq(name, "StdNewDocument") || str_ieq(name, "StdNewFromTemplate") ||
               str_ieq(name, "StdEditDocument") || str_ieq(name, "StdOpenDocument") ||
               str_ieq(name, "StdExit")) {
        /* system topic commands: refused here */
    } else if (d->srv->execute && d->lpdoc) {
        if (d->lpdoc->lpvtbl->Execute(d->lpdoc, hcmd) == OLE_OK)
            status = 0x8000;
    }
done:
    tmp_free(t);
    return status;
}

static WORD doc_poke(DOC FARP *d, HWND client, const char FARP *name, HGLOBAL block)
{
    WORD FAR *v = block ? (WORD FAR *)GlobalLock(block) : NULL;
    DWORD size = block ? GlobalSize(block) : 0;
    WORD cf, status = 0;
    OLESTATUS r = OLE_ERROR_GENERIC;
    ITEM FARP *it;
    if (!v)
        return 0;
    cf = v[1];
    if (str_ieq(name, "StdHostNames")) {
        /* {client name offset, document name offset, names} */
        const char FAR *base = (const char FAR *)(v + 4);
        DWORD room = size > 8 ? size - 8 : 0;
        /* observed: the client name is taken at the start of the names
           whatever the first word says (OLECLI writes 0 there); an offset
           past the block reads beyond it on 3.1, w16 gives "" */
        const char FAR *cn = room ? base : "";
        const char FAR *dn = v[3] < room ? base + v[3] : "";
        if (d->lpdoc)
            r = d->lpdoc->lpvtbl->SetHostNames(d->lpdoc, (LPSTR)cn, (LPSTR)dn);
    } else if (str_ieq(name, "StdDocDimensions")) {
        /* {cx, cy} becomes the rectangle {0, cy, cx, 0} (observed) */
        RECT rc;
        rc.left = 0;
        rc.top = (int)v[3];
        rc.right = (int)v[2];
        rc.bottom = 0;
        if (d->lpdoc)
            r = d->lpdoc->lpvtbl->SetDocDimensions(d->lpdoc, &rc);
    } else if (str_ieq(name, "StdColorScheme")) {
        if (d->lpdoc)
            r = d->lpdoc->lpvtbl->SetColorScheme(d->lpdoc, (LOGPALETTE FAR *)(v + 2));
    } else if (str_ieq(name, "StdTargetDevice")) {
        HGLOBAL t;
        GlobalUnlock(block);
        t = data_value(block, 0);
        if (t) {
            if (d->target)
                GlobalFree(d->target);
            d->target = t;
            d->target_gen++;
            if (!d->target_gen)
                d->target_gen++;
            r = OLE_OK;
        }
        return r == OLE_OK ? 0x8000 : 0;
    } else {
        GlobalUnlock(block);
        it = item_get(d, name, client, &r);
        if (it) {
            HGLOBAL h = data_value(block, cf);
            r = it->obj->lpvtbl->SetData(it->obj, cf, h);
        }
        return r == OLE_OK ? 0x8000 : 0;
    }
    GlobalUnlock(block);
    if (r == OLE_OK)
        status = 0x8000;
    return status;
}

LRESULT doc_dde(DOC FARP *d, HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = (HWND)wp;
    CONV FARP *c = conv_find(d->convs, client);
    ATOM atom = HIWORD(lp);
    char name[256];
    OLESTATUS r;
    ITEM FARP *it;
    if (!c && msg != WM_DDE_ACK)
        return 0;                       /* UNVERIFIED: not our client */
    switch (msg) {
    case WM_DDE_REQUEST: {
        WORD cf = LOWORD(lp);
        HANDLE h;
        HGLOBAL b;
        atom_name(atom, name, sizeof name);
        it = item_get(d, name, client, &r);
        if (!it || !item_has_format(it, cf) || item_data(it, cf, &h) != OLE_OK || !h) {
            ack(client, w, 0, atom);
            return 0;
        }
        b = data_block(0x3000, cf, h);
        if (!b || !PostMessage(client, WM_DDE_DATA, (WPARAM)w, MAKELPARAM(b, atom))) {
            if (b)
                GlobalFree(b);
            ack(client, w, 0, atom);
        }
        return 0;
    }
    case WM_DDE_POKE: {
        HGLOBAL block = (HGLOBAL)LOWORD(lp);
        WORD flags = 0, st;
        WORD FAR *v = block ? (WORD FAR *)GlobalLock(block) : NULL;
        if (v) {
            flags = v[0];
            GlobalUnlock(block);
        }
        atom_name(atom, name, sizeof name);
        st = doc_poke(d, client, name, block);
        if (IsWindow(w))
            ack(client, w, st, atom);
        if (st && (flags & 0x2000))     /* fRelease: ours once acknowledged */
            GlobalFree(block);
        return 0;
    }
    case WM_DDE_ADVISE: {
        HGLOBAL opt = (HGLOBAL)LOWORD(lp);
        WORD FAR *o = opt ? (WORD FAR *)GlobalLock(opt) : NULL;
        WORD flags = 0, cf = 0, kind, st = 0;
        if (o) {
            flags = o[0];
            cf = o[1];
            GlobalUnlock(opt);
        }
        atom_name(atom, name, sizeof name);
        {
            char text[256];
            str_cpy(text, name);
            if (!split_item(name, &kind)) {
                st = 0;
            } else if (str_ieq(name, "StdDocumentName")) {
                if (adv_add(&d->renames, adv_new(client, ADV_RENAME, cf, flags, text)))
                    st = 0x8000;
            } else if ((it = item_get(d, name, client, &r)) != NULL && item_has_format(it, cf)) {
                if (adv_add(&it->advs, adv_new(client, kind, cf, flags, text))) {
                    ADV FARP *e;
                    st = 0x8000;
                    /* the latest advise of a kind sets fDeferUpd for all of
                       the client's advises of that kind (observed) */
                    for (e = it->advs; e; e = e->next)
                        if (e->client == client && e->kind == kind)
                            e->flags = (e->flags & ~0x4000) | (flags & 0x4000);
                }
            }
        }
        /* observed: a successful advise is acknowledged only when it has
           fAckReq set; a failed one always */
        if (!st || (flags & 0x8000))
            ack(client, w, st, atom);
        if (st && (flags & 0x8000) && opt)
            GlobalFree(opt);
        return 0;
    }
    case WM_DDE_UNADVISE: {
        WORD cf = LOWORD(lp), kind, st = 0;
        char text[256];
        atom_name(atom, name, sizeof name);
        str_cpy(text, name);
        if (split_item(name, &kind)) {
            if (str_ieq(name, "StdDocumentName")) {
                if (adv_remove(&d->renames, client, cf, text))
                    st = 0x8000;
            } else if ((it = item_get(d, name, client, &r)) != NULL &&
                       adv_remove(&it->advs, client, cf, text))
                st = 0x8000;
        }
        ack(client, w, st, atom);
        return 0;
    }
    case WM_DDE_EXECUTE: {
        WORD st = doc_execute(d, client, (HGLOBAL)HIWORD(lp));
        if (IsWindow(w))
            ack(client, w, st, HIWORD(lp));
        return 0;
    }
    case WM_DDE_TERMINATE: {
        SRV FARP *s = d->srv;
        if (!c->term_posted)
            PostMessage(client, WM_DDE_TERMINATE, (WPARAM)w, 0);
        conv_remove(&d->convs, client);
        adv_drop_client(&d->renames, client);
        for (it = d->items; it; it = it->next) {
            adv_drop_client(&it->advs, client);
            conv_remove(&it->users, client);
        }
        if (d->revoking) {
            if (!d->convs)
                doc_finish(d);
        } else
            srv_check_release(s);
        return 0;
    }
    case WM_DDE_ACK:                    /* to our DATA: nothing to do */
        return 0;
    }
    return 0;
}

LRESULT FAR PASCAL __loadds DOCWNDPROC(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    DOC FARP *d = (DOC FARP *)GetWindowLong(w, 0);
    if (!d)
        return DefWindowProc(w, msg, wp, lp);
    switch (msg) {
    case WM_DDE_INITIATE: {
        ATOM app = LOWORD(lp), topic = HIWORD(lp);
        SRV FARP *s = d->srv;
        if (d->revoking || s->revoking || !app || app != s->atom ||
            topic != d->atom || !topic)
            return 0;
        if (!conv_add(&d->convs, (HWND)wp))
            return 0;
        d->pending = FALSE;
        SendMessage((HWND)wp, WM_DDE_ACK, (WPARAM)w,
                    MAKELPARAM(GlobalAddAtom(s->cls), GlobalAddAtom(d->name)));
        return 0;
    }
    case WM_SVR_TERMINATE: {
        CONV FARP *c, FARP *n;
        conv_post_terms(w, d->convs);
        for (c = d->convs; c; c = n) {  /* clients that are gone */
            n = c->next;
            if (!IsWindow(c->client))
                conv_remove(&d->convs, c->client);
        }
        if (d->revoking && !d->convs)
            doc_finish(d);
        return 0;
    }
    case WM_DESTROY:
        if (!d->released) {             /* the server's task ends */
            SetWindowLong(w, 0, 0);
            d->hwnd = NULL;
            d->released = TRUE;
            doc_destroy(d);
        }
        return 0;
    }
    if (msg >= WM_DDE_FIRST && msg <= WM_DDE_LAST) {
        if (srv_queue(d->srv, w, msg, wp, lp))
            return 0;
        return doc_dde(d, w, msg, wp, lp);
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ItemWndClass: an object's window (it gets no messages of its own) */
LRESULT FAR PASCAL __loadds ITEMWNDPROC(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the APIs ---- */

OLESTATUS FAR PASCAL __loadds OLEREGISTERSERVERDOC(LHSERVER lhs, LPCSTR name,
        LPOLESERVERDOC lpdoc, LHSERVERDOC FAR *lplh)
{
    SRV FARP *s = srv_from(lhs);
    DOC FARP *d;
    if (!s)
        return OLE_ERROR_HANDLE;
    if (!name || !lplh || IsBadReadPtr(name, 1) || IsBadWritePtr(lplh, sizeof *lplh))
        return OLE_ERROR_ADDRESS;
    *lplh = 0;
    d = doc_new(s, name, lpdoc);
    if (!d)
        return OLE_ERROR_MEMORY;
    *lplh = (LHSERVERDOC)d;
    return OLE_OK;
}

OLESTATUS FAR PASCAL __loadds OLEREVOKESERVERDOC(LHSERVERDOC lh)
{
    DOC FARP *d = doc_from(lh);
    if (!d)
        return OLE_ERROR_HANDLE;
    if (d->revoking)
        return OLE_WAIT_FOR_RELEASE;    /* UNVERIFIED */
    return doc_revoke(d);
}

OLESTATUS FAR PASCAL __loadds OLERENAMESERVERDOC(LHSERVERDOC lh, LPCSTR name)
{
    DOC FARP *d = doc_from(lh);
    char FARP *nn;
    ADV FARP *a;
    if (!d)
        return OLE_ERROR_HANDLE;
    if (!name || IsBadReadPtr(name, 1))
        return OLE_ERROR_ADDRESS;
    nn = str_dup(name);
    if (!nn)
        return OLE_ERROR_MEMORY;
    mem_free(d->name);
    d->name = nn;
    if (d->atom)
        GlobalDeleteAtom(d->atom);
    d->atom = *nn ? GlobalAddAtom(nn) : 0;
    /* the clients that advised on StdDocumentName get the new name */
    for (a = d->renames; a; a = a->next) {
        CONV FARP *c = conv_find(d->convs, a->client);
        int n = str_len(nn);
        HGLOBAL h, b;
        char FAR *p;
        ATOM at;
        if (!c || c->terminating)
            continue;
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
        p = h ? (char FAR *)GlobalLock(h) : NULL;
        if (!p)
            continue;
        str_cpy(p, nn);
        GlobalUnlock(h);
        b = data_block(0x2000, a->cf, h);
        if (!b)
            continue;
        at = GlobalAddAtom(a->text);
        if (!PostMessage(a->client, WM_DDE_DATA, (WPARAM)d->hwnd, MAKELPARAM(b, at))) {
            GlobalFree(b);
            if (at)
                GlobalDeleteAtom(at);
        }
    }
    return OLE_OK;
}

OLESTATUS FAR PASCAL __loadds OLESAVEDSERVERDOC(LHSERVERDOC lh)
{
    DOC FARP *d = doc_from(lh);
    ITEM FARP *it;
    if (!d)
        return OLE_ERROR_HANDLE;
    for (it = d->items; it; it = it->next)
        item_notify(it, ADV_SAVE);
    return OLE_OK;
}

OLESTATUS FAR PASCAL __loadds OLEREVERTSERVERDOC(LHSERVERDOC lh)
{
    return doc_from(lh) ? OLE_OK : OLE_ERROR_HANDLE;
}

/* Revokes one object: it is released, and the conversations that used
   it end (observed with OLECLI, which has one conversation per object). */
static void term_now(DOC FARP *d, HWND client)
{
    CONV FARP *c = conv_find(d->convs, client);
    if (c && !c->term_posted) {
        c->terminating = c->term_posted = TRUE;
        PostMessage(client, WM_DDE_TERMINATE, (WPARAM)d->hwnd, 0);
    }
}

OLESTATUS FAR PASCAL __loadds OLEREVOKEOBJECT(LPOLECLIENT cl)
{
    ITEM FARP *it = item_from(cl), FARP *FARP *pp;
    DOC FARP *d;
    CONV FARP *u;
    ADV FARP *a;
    if (!cl)
        return OLE_ERROR_ADDRESS;       /* observed */
    if (!it)
        return OLE_ERROR_HANDLE;
    d = it->doc;
    for (pp = &d->items; *pp; pp = &(*pp)->next)
        if (*pp == it) {
            *pp = it->next;
            break;
        }
    if (it->obj)
        it->obj->lpvtbl->Release(it->obj);
    /* each client of the object gets its TERMINATE now, object by
       object (observed order) */
    for (u = it->users; u; u = u->next)
        term_now(d, u->client);
    for (a = it->advs; a; a = a->next)
        term_now(d, a->client);
    item_free(it);
    return OLE_OK;
}

/* ---- exports Windows 3.1's OLESVR has for its own use (ordinals 24,
   25, 27-31); nothing imports them (docs/exportdb). w16 does not use
   them: they fail harmlessly. ---- */

WORD FAR PASCAL __loadds SENDDATAMSG(void) { return 0; }
WORD FAR PASCAL __loadds FINDITEMWND(void) { return 0; }
WORD FAR PASCAL __loadds TERMINATECLIENTS(void) { return 0; }
WORD FAR PASCAL __loadds TERMINATEDOCCLIENTS(void) { return 0; }
WORD FAR PASCAL __loadds DELETECLIENTINFO(void) { return 0; }
WORD FAR PASCAL __loadds SENDRENAMEMSG(void) { return 0; }
WORD FAR PASCAL __loadds ENUMFORTERMINATE(void) { return 0; }
