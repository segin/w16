/*
 * svr.c - OLESVR's servers: OleRegisterServer and the other server
 * APIs, blocking, and the system topic (SrvrWndClass windows) of the
 * OLE 1.0 DDE protocol (ARCHITECTURE.md 16.2, observed on Windows 3.1
 * with tests/olesvr).
 */
#include <shellapi.h>
#include "olesvr.h"

/* KERNEL.334, KERNEL.335 */
extern BOOL WINAPI IsBadReadPtr(const void FAR *, UINT);
extern BOOL WINAPI IsBadWritePtr(void FAR *, UINT);

SRV FARP *srv_from(LHSERVER lh)
{
    SRV FARP *s;
    for (s = svr_list; s; s = s->next)
        if ((LHSERVER)s == lh && !s->released)
            return s;
    return NULL;
}

static BOOL reg_has(const char FARP *cls, const char *sub)
{
    char key[300], val[16];
    LONG n = sizeof val;
    int l = str_len(cls);
    if (l + 40 > (int)sizeof key)
        return FALSE;                   /* UNVERIFIED: class names that long */
    str_cpy(key, cls);
    str_cpy(key + l, sub);
    return RegQueryValue(HKEY_CLASSES_ROOT, key, val, &n) == ERROR_SUCCESS;
}

static int ndocs(SRV FARP *s)
{
    int n = 0;
    DOC FARP *d;
    for (d = s->docs; d; d = d->next)
        n++;
    return n;
}

/* every conversation of the server and its documents */
static BOOL any_conv(SRV FARP *s)
{
    DOC FARP *d;
    CONV FARP *c;
    for (c = s->convs; c; c = c->next)
        if (!c->closing)
            return TRUE;
    for (d = s->docs; d; d = d->next)
        for (c = d->convs; c; c = c->next)
            if (!c->closing)
                return TRUE;
    return FALSE;
}

static void srv_free(SRV FARP *s)
{
    SRV FARP *FARP *pp;
    QMSG FARP *q;
    for (pp = &svr_list; *pp; pp = &(*pp)->next)
        if (*pp == s) {
            *pp = s->next;
            break;
        }
    while (s->convs)
        conv_unlink(s->hwnd, &s->convs, s->convs->client);
    while ((q = s->queue) != NULL) {
        s->queue = q->next;
        mem_free(q);
    }
    if (s->hwnd && IsWindow(s->hwnd)) {
        SetWindowLong(s->hwnd, 0, 0);
        DestroyWindow(s->hwnd);
    }
    if (s->atom)
        GlobalDeleteAtom(s->atom);
    mem_free(s->cls);
    mem_free(s);
}

/* Not revoked, and the last conversation is gone: the server may go
   (Release; it is not revoked). Observed: after a document conversation
   ends (also through a revoke), when no conversation is left; after the
   system topic's ends only when the server has no documents either. */
void srv_check_release(SRV FARP *s)
{
    if (!s->revoking && !s->released && !any_conv(s))
        s->lpsrv->lpvtbl->Release(s->lpsrv);
}

/* Revoked and everything gone: Release and free it. */
void srv_try_finish(SRV FARP *s)
{
    if (s->revoking && !s->released && !s->docs && !s->convs) {
        s->released = TRUE;
        s->lpsrv->lpvtbl->Release(s->lpsrv);
        srv_free(s);
    }
}

/* ---- the APIs ---- */

OLESTATUS FAR PASCAL __loadds OLEREGISTERSERVER(LPCSTR cls, LPOLESERVER lpsrv,
        LHSERVER FAR *lplh, HINSTANCE inst, OLE_SERVER_USE use)
{
    SRV FARP *s;
    if (!cls || !lpsrv || !lplh || IsBadReadPtr(cls, 1) || IsBadReadPtr(lpsrv, 4) ||
        IsBadWritePtr(lplh, sizeof *lplh))
        return OLE_ERROR_ADDRESS;
    *lplh = 0;
    /* the class must be registered with a server for StdFileEditing */
    if (!*cls || !reg_has(cls, "\\protocol\\StdFileEditing\\server"))
        return OLE_ERROR_CLASS;
    s = mem_alloc(sizeof *s);
    if (!s)
        return OLE_ERROR_MEMORY;
    s->magic = SRV_MAGIC;
    s->lpsrv = lpsrv;
    s->inst = inst;
    s->use = use;
    s->cls = str_dup(cls);
    /* the class name stays an atom while the server lives (observed: an
       INITIATE spelt in other case sees the registered spelling) */
    s->atom = GlobalAddAtom(cls);
    s->execute = reg_has(cls, "\\protocol\\StdExecute\\server");
    /* observed: a hidden overlapped window titled "Srvr" */
    s->hwnd = CreateWindow("SrvrWndClass", "Srvr", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL,
                           svr_inst, NULL);
    if (!s->cls || !s->hwnd) {
        if (s->hwnd)
            DestroyWindow(s->hwnd);
        mem_free(s->cls);
        mem_free(s);
        return OLE_ERROR_MEMORY;
    }
    SetWindowLong(s->hwnd, 0, (LONG)s);
    s->next = svr_list;
    svr_list = s;
    *lplh = (LHSERVER)s;
    return OLE_OK;
}

OLESTATUS FAR PASCAL __loadds OLEREVOKESERVER(LHSERVER lh)
{
    SRV FARP *s = srv_from(lh);
    DOC FARP *d, FARP *n;
    CONV FARP *c;
    if (!s)
        return OLE_ERROR_HANDLE;
    if (s->revoking)
        return OLE_WAIT_FOR_RELEASE;    /* UNVERIFIED: revoked twice */
    s->revoking = TRUE;
    s->busy = TRUE;                     /* documents finish quietly */
    for (d = s->docs; d; d = n) {
        n = d->next;
        if (!d->revoking)
            doc_revoke(d);
    }
    s->busy = FALSE;
    conv_close_now(s->hwnd, &s->convs);
    for (c = s->convs; c; c = c->next)
        c->terminating = TRUE;
    if (s->convs)
        PostMessage(s->hwnd, WM_SVR_TERMINATE, 0, 0);
    if (!s->docs && !s->convs) {
        srv_try_finish(s);
        return OLE_OK;
    }
    return OLE_WAIT_FOR_RELEASE;
}

OLESTATUS FAR PASCAL __loadds OLEBLOCKSERVER(LHSERVER lh)
{
    SRV FARP *s = srv_from(lh);
    if (!s)
        return OLE_ERROR_HANDLE;
    if (s->blocked)
        return OLE_ERROR_SERVER_BLOCKED;
    s->blocked = TRUE;
    return OLE_OK;
}

static void dispatch(SRV FARP *s, QMSG FARP *q);

OLESTATUS FAR PASCAL __loadds OLEUNBLOCKSERVER(LHSERVER lh, BOOL FAR *more)
{
    SRV FARP *s = srv_from(lh);
    QMSG FARP *q;
    if (!s)
        return OLE_ERROR_HANDLE;
    if (!more || IsBadWritePtr(more, sizeof *more))
        return OLE_ERROR_ADDRESS;
    q = s->queue;
    if (q) {
        s->queue = q->next;
        dispatch(s, q);
        mem_free(q);
        /* the server may have been revoked and freed meanwhile */
        s = srv_from(lh);
    }
    if (s && !s->queue)
        s->blocked = FALSE;
    *more = s && s->queue != NULL;
    return OLE_OK;
}

DWORD FAR PASCAL __loadds OLEQUERYSERVERVERSION(void)
{
    return 0x00001001L;                 /* as Windows 3.1's OLESVR */
}

/* ---- blocking: DDE messages queued while the server is blocked ---- */

static void dispatch(SRV FARP *s, QMSG FARP *q)
{
    LONG p;
    if (!IsWindow(q->hwnd))
        return;
    p = GetWindowLong(q->hwnd, 0);
    if (!p)
        return;
    if (q->hwnd == s->hwnd && q->msg == WM_DDE_TERMINATE) {
        /* observed (scenario block): a system-topic TERMINATE that waited
           in the queue is answered, but the conversation stays on the
           books: the server is not let go when its last document goes,
           and that client's next INITIATE is not answered */
        if (conv_find(s->convs, (HWND)q->wp))
            PostMessage((HWND)q->wp, WM_DDE_TERMINATE, (WPARAM)s->hwnd, 0);
    } else if (q->hwnd == s->hwnd)
        srv_dde(s, q->hwnd, q->msg, q->wp, q->lp);
    else
        doc_dde((DOC FARP *)p, q->hwnd, q->msg, q->wp, q->lp);
}

/* whether a posted DDE message for one of the server's windows has to
   wait (OleBlockServer); it is then queued */
BOOL srv_queue(SRV FARP *s, HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    QMSG FARP *q, FARP *FARP *pp;
    if (!s->blocked)
        return FALSE;
    q = mem_alloc(sizeof *q);
    if (!q)
        return FALSE;
    q->hwnd = w;
    q->msg = msg;
    q->wp = wp;
    q->lp = lp;
    for (pp = &s->queue; *pp; pp = &(*pp)->next)
        ;
    *pp = q;
    return TRUE;
}

/* ---- the system topic ---- */

static BOOL is_name(const char FARP *a, const char *b)
{
    return str_ieq(a, b);
}

/* strict form of the Std commands: "[Name(args)]" and nothing more,
   checked before anything is done (observed: two commands in one
   EXECUTE, spaces, a missing bracket are refused without a callback) */
BOOL one_command(const char FARP *t)
{
    int n = str_len(t), i;
    if (n < 3 || t[0] != '[' || t[n - 1] != ']')
        return FALSE;
    for (i = 1; i < n - 1; i++)
        if (t[i] == ']')
            return FALSE;
    return TRUE;
}

/* the command's name: "[Name(" or "[Name]" */
void cmd_name(const char FARP *t, char FARP *out, int max)
{
    int i = 0;
    if (*t == '[')
        t++;
    while (*t && *t != '(' && *t != ']' && i < max - 1)
        out[i++] = *t++;
    out[i] = 0;
}

/* a bare argument (up to ',' or ')') equals word, ignoring case */
static BOOL tok_ieq(const char FARP *a, const char *word)
{
    while (*word) {
        char x = *a++, y = *word++;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (x != y)
            return FALSE;
    }
    return *a == ',' || *a == ')';
}

/* Reads the arguments of a strict command: '(' "str" , TRUE , 3 ')'
   (strings quoted, without escapes; booleans TRUE or FALSE; numbers
   unsigned decimal; no spaces). kinds lists them: 's', 'b', 'n'; those
   after '|' are optional. Quoted strings are cut in place. Returns the
   number read, or -1 when the text does not match; *rest is what
   follows the last one. */
int cmd_args(char FARP *t, const char *kinds, char FARP *FARP *arg, char FARP *FARP *rest)
{
    char FARP *p = t;
    int n = 0;
    BOOL optional = FALSE;
    while (*p && *p != '(' && *p != ']')
        p++;
    if (*p != '(') {
        *rest = p;
        return *kinds && *kinds != '|' ? -1 : 0;
    }
    p++;
    for (; *kinds; kinds++) {
        if (*kinds == '|') {
            optional = TRUE;
            continue;
        }
        if (n > 0) {
            if (*p != ',') {
                if (optional)
                    break;
                return -1;
            }
            p++;
        }
        arg[n] = p;
        if (*kinds == 's') {
            if (*p != '"')
                return -1;
            arg[n] = ++p;
            while (*p && *p != '"')
                p++;
            if (!*p || (p[1] != ',' && p[1] != ')'))
                return -1;              /* observed: "D""14" is refused */
            *p++ = 0;
        } else if (*kinds == 'b') {
            if (!tok_ieq(p, "TRUE") && !tok_ieq(p, "FALSE"))
                return -1;
            while (*p != ',' && *p != ')')
                p++;
        } else {
            if (*p < '0' || *p > '9')
                return -1;
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p != ',' && *p != ')')
                return -1;
        }
        n++;
    }
    *rest = p;
    return n;
}

/* after the arguments ")]" must end the text */
BOOL cmd_end(const char FARP *rest)
{
    return rest[0] == ')' && rest[1] == ']' && rest[2] == 0;
}

BOOL arg_bool(const char FARP *a)
{
    return a[0] == 'T' || a[0] == 't';
}

WORD arg_num(const char FARP *a)
{
    WORD v = 0;
    while (*a >= '0' && *a <= '9')
        v = v * 10 + (*a++ - '0');
    return v;
}

/* new document for StdNewDocument and the like: create it, ask the
   server, keep it only when the server agrees */
static DOC FARP *new_doc(SRV FARP *s, const char FARP *name)
{
    if (s->use == OLE_SERVER_MULTI && s->docs)
        return NULL;                    /* one document per instance */
    return doc_new(s, name, NULL);
}

static WORD sys_execute(SRV FARP *s, HWND client, HGLOBAL hcmd)
{
    char FARP *t, FARP *rest;
    char FARP *arg[4];
    char name[32];
    const char FARP *src = hcmd ? (const char FARP *)GlobalLock(hcmd) : NULL;
    WORD status = 0;
    int n;
    DOC FARP *d;
    LPOLESERVERDOC lpdoc;
    OLESTATUS r;
    if (!src)
        return 0;
    t = tmp_dup(src);
    GlobalUnlock(hcmd);
    if (!t)
        return 0;
    cmd_name(t, name, sizeof name);
    if (t[0] != '[') {
        status = 0;
    } else if (is_name(name, "StdNewDocument") || is_name(name, "StdNewFromTemplate") ||
               is_name(name, "StdEditDocument") || is_name(name, "StdOpenDocument") ||
               is_name(name, "StdShowItem") || is_name(name, "StdCloseDocument") ||
               is_name(name, "StdExit") || is_name(name, "StdDoVerbItem")) {
        if (!one_command(t))
            goto done;
        if (is_name(name, "StdNewDocument") || is_name(name, "StdNewFromTemplate")) {
            BOOL tmpl = is_name(name, "StdNewFromTemplate");
            n = cmd_args(t, tmpl ? "sss" : "ss", arg, &rest);
            if (n < 0 || (d = new_doc(s, arg[1])) == NULL)
                goto done;
            lpdoc = NULL;
            if (tmpl)
                r = s->lpsrv->lpvtbl->CreateFromTemplate(s->lpsrv, (LHSERVERDOC)d, arg[0],
                                                         arg[1], arg[2], (LPOLESERVERDOC)&lpdoc);
            else
                r = s->lpsrv->lpvtbl->Create(s->lpsrv, (LHSERVERDOC)d, arg[0], arg[1], (LPOLESERVERDOC)&lpdoc);
            if (r != OLE_OK) {
                doc_destroy(d);
                goto done;
            }
            d->lpdoc = lpdoc;
            d->pending = TRUE;
            if (!cmd_end(rest)) {
                /* observed: an extra argument is found after the call;
                   the document goes again */
                doc_destroy(d);
                goto done;
            }
            status = 0x8000;
        } else if (is_name(name, "StdEditDocument") || is_name(name, "StdOpenDocument")) {
            BOOL open = is_name(name, "StdOpenDocument");
            n = cmd_args(t, "s", arg, &rest);
            if (n < 0)
                goto done;
            if (open && (d = doc_find(s, arg[0])) != NULL) {
                status = cmd_end(rest) ? 0x8000 : 0;   /* already open */
                goto done;
            }
            if ((d = new_doc(s, arg[0])) == NULL)
                goto done;
            lpdoc = NULL;
            if (open)
                r = s->lpsrv->lpvtbl->Open(s->lpsrv, (LHSERVERDOC)d, arg[0], (LPOLESERVERDOC)&lpdoc);
            else
                r = s->lpsrv->lpvtbl->Edit(s->lpsrv, (LHSERVERDOC)d, s->cls, arg[0], (LPOLESERVERDOC)&lpdoc);
            if (r != OLE_OK) {
                doc_destroy(d);
                goto done;
            }
            d->lpdoc = lpdoc;
            d->pending = TRUE;
            if (!cmd_end(rest)) {
                doc_destroy(d);
                goto done;
            }
            status = 0x8000;
        } else if (is_name(name, "StdShowItem")) {
            ITEM FARP *it;
            BOOL focus = TRUE;
            n = cmd_args(t, "ss|b", arg, &rest);
            if (n < 0 || !cmd_end(rest) || (d = doc_find(s, arg[0])) == NULL)
                goto done;
            if (n > 2)
                focus = !arg_bool(arg[2]);
            it = item_get(d, arg[1], client, &r);
            if (it && item_show(it, focus) == OLE_OK)
                status = 0x8000;
        } else if (is_name(name, "StdCloseDocument")) {
            n = cmd_args(t, "s", arg, &rest);
            if (n < 0 || !cmd_end(rest) || (d = doc_find(s, arg[0])) == NULL)
                goto done;
            if (d->lpdoc && d->lpdoc->lpvtbl->Close(d->lpdoc) == OLE_OK)
                status = 0x8000;
        } else if (is_name(name, "StdExit")) {
            if (t[1 + str_len("StdExit")] != ']')
                goto done;
            if (s->lpsrv->lpvtbl->Exit(s->lpsrv) == OLE_OK)
                status = 0x8000;
        }
        /* StdDoVerbItem belongs to a document topic: refused here */
    } else if (s->execute) {
        /* not a Std command: the server's own (StdExecute protocol) */
        if (s->lpsrv->lpvtbl->Execute(s->lpsrv, hcmd) == OLE_OK)
            status = 0x8000;
    }
done:
    tmp_free(t);
    return status;
}

/* the DDE messages of a system topic conversation (also when they come
   out of the queue of a blocked server) */
LRESULT srv_dde(SRV FARP *s, HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = (HWND)wp;
    CONV FARP *c;
    switch (msg) {
    case WM_DDE_EXECUTE: {
        WORD st;
        if (!conv_find(s->convs, client))
            return 0;                   /* UNVERIFIED: no conversation */
        st = sys_execute(s, client, (HGLOBAL)HIWORD(lp));
        /* the server may be gone (StdExit, revoke): the window too */
        if (IsWindow(w))
            ack(client, w, st, HIWORD(lp));
        return 0;
    }
    case WM_DDE_REQUEST:
        /* observed: a positive ACK, no data */
        ack(client, w, 0x8000, HIWORD(lp));
        return 0;
    case WM_DDE_POKE:
    case WM_DDE_ADVISE:
    case WM_DDE_UNADVISE:
        /* UNVERIFIED for ADVISE/UNADVISE: POKE was observed to get no
           answer at all */
        return 0;
    case WM_DDE_TERMINATE:
        c = conv_find(s->convs, client);
        if (!c)
            return 0;
        /* observed: the end of a system conversation lets the server go
           only when it is an OLE_SERVER_MULTI one without documents.
           Release comes before the answering TERMINATE: a server that
           revokes itself in Release gets OLE_OK at once (the client has
           gone), and the client gets one TERMINATE (relq scenario) */
        c->closing = TRUE;
        if (!s->revoking && s->use == OLE_SERVER_MULTI && !ndocs(s))
            srv_check_release(s);
        if (!srv_from((LHSERVER)s))
            return 0;                   /* revoked and gone meanwhile */
        c = conv_find(s->convs, client);
        if (c) {
            if (!c->term_posted)
                PostMessage(client, WM_DDE_TERMINATE, (WPARAM)w, 0);
            conv_unlink(s->hwnd, &s->convs, client);
        }
        if (s->revoking)
            srv_try_finish(s);
        return 0;
    case WM_DDE_ACK:
        return 0;
    }
    return 0;
}

static ATOM find_atom(LPCSTR s)
{
    return GlobalFindAtom(s);
}

LRESULT FAR PASCAL __loadds SRVRWNDPROC(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    SRV FARP *s = (SRV FARP *)GetWindowLong(w, 0);
    if (!s)
        return DefWindowProc(w, msg, wp, lp);
    switch (msg) {
    case WM_DDE_INITIATE: {
        ATOM app = LOWORD(lp), topic = HIWORD(lp);
        DOC FARP *d, FARP *n;
        if (s->revoking || !app || app != s->atom || !topic)
            return 0;
        /* a document topic: the documents (child windows) answer, oldest
           first, as observed */
        for (d = s->docs; d; d = n) {
            n = d->next;
            if (d->atom == topic && d->hwnd)
                SendMessage(d->hwnd, WM_DDE_INITIATE, wp, lp);
        }
        /* observed: an OLE_SERVER_MULTI server (one document per
           instance) that has a document does not answer, so that the
           client starts another instance */
        if (s->use == OLE_SERVER_MULTI && s->docs)
            return 0;
        if (topic != find_atom("OLEsystem") && topic != find_atom("System"))
            return 0;
        /* a client already in conversation with the system topic gets
           no second one (UNVERIFIED but for the case above) */
        if (conv_find(s->convs, (HWND)wp))
            return 0;
        if (!conv_link(s->hwnd, &s->convs, (HWND)wp))
            return 0;
        {
            char tn[16];
            GlobalGetAtomName(topic, tn, sizeof tn);
            SendMessage((HWND)wp, WM_DDE_ACK, (WPARAM)w,
                        MAKELPARAM(GlobalAddAtom(s->cls),
                                   GlobalAddAtom(str_ieq(tn, "System") ? "System" : "OLEsystem")));
        }
        return 0;
    }
    case WM_SVR_TERMINATE:
        conv_post_terms(w, s->convs);
        /* clients that are gone will never answer */
        {
            CONV FARP *c, FARP *n;
            for (c = s->convs; c; c = n) {
                n = c->next;
                if (!IsWindow(c->client))
                    conv_unlink(s->hwnd, &s->convs, c->client);
            }
        }
        if (s->revoking)
            srv_try_finish(s);
        return 0;
    case WM_DESTROY:
        /* the server's task ends without having revoked it: forget it
           (UNVERIFIED: what Windows 3.1's OLESVR does then) */
        if (!s->released) {
            SetWindowLong(w, 0, 0);
            s->hwnd = NULL;
            s->released = TRUE;
            conv_post_terms(w, s->convs);
            while (s->docs) {
                DOC FARP *d = s->docs;
                d->released = TRUE;
                doc_destroy(d);
            }
            srv_free(s);
        }
        return 0;
    }
    if (msg >= WM_DDE_FIRST && msg <= WM_DDE_LAST) {
        if (srv_queue(s, w, msg, wp, lp))
            return 0;
        return srv_dde(s, w, msg, wp, lp);
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the library's start ---- */

int FAR __cdecl svr_init(WORD inst)
{
    WNDCLASS wc;
    svr_inst = (HINSTANCE)inst;
    wc.style = 0;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 8;                  /* observed (w16 uses the first 4) */
    wc.hInstance = svr_inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = SRVRWNDPROC;
    wc.lpszClassName = "SrvrWndClass";
    if (!RegisterClass(&wc))
        return 0;
    wc.lpfnWndProc = DOCWNDPROC;
    wc.lpszClassName = "DocWndClass";
    if (!RegisterClass(&wc))
        return 0;
    wc.cbWndExtra = 4;                  /* observed */
    wc.lpfnWndProc = ITEMWNDPROC;
    wc.lpszClassName = "ItemWndClass";
    if (!RegisterClass(&wc))
        return 0;
    cf_native = RegisterClipboardFormat("Native");
    cf_ownerlink = RegisterClipboardFormat("OwnerLink");
    cf_objectlink = RegisterClipboardFormat("ObjectLink");
    cf_binary = RegisterClipboardFormat("Binary");
    return 1;
}
