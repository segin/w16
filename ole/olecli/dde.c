/*
 * dde.c - OLECLI's conversations with OLE servers (ARCHITECTURE.md 16.3).
 *
 * The OLE 1.0 protocol is DDE (Windows 3.1 SDK, "DDE" and the OLE
 * protocol appendix); the exact sequences here are the ones probe OC1/
 * OCSPY observed between Microsoft's OLECLI and OLESVR servers
 * (Paintbrush, Sound Recorder):
 *
 *  - two kinds of hidden top-level windows: OleSrvrWndClass (title "")
 *    for the system topic, OleDocWndClass (title "Window Name") for a
 *    document; INITIATE is broadcast (SendMessage to -1);
 *  - the system topic is "OLEsystem", then "System"; when no server
 *    answers, the window is destroyed, the server is started with
 *    LoadModule("<server> -Embedding [file]") and a new window initiates;
 *  - an operation is a sequence of steps; each posted message waits for
 *    its ACK (or DATA); then OLE_RELEASE.
 *
 * An operation is a list of STEPs (olecli.h). dde_run executes steps until
 * one has to wait for a posted answer: the API then returns
 * OLE_WAIT_FOR_RELEASE and the window procedure continues the list as the
 * answers come; at the end the client gets OLE_RELEASE.
 */
#include "olecli.h"

#define SRVR_CLASS "OleSrvrWndClass"
#define DOC_CLASS "OleDocWndClass"

/* what a step waits for */
enum { W_NONE, W_ACK, W_DATA, W_TERM, W_DOCTERM, W_SYSTERM, W_SYSTERM_KEEP };

static HWND g_initiating;       /* the window whose INITIATE is out */
static HWND g_answer;           /* the first server that answered */

LRESULT OLEAPI OleWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp);

BOOL dde_register(HINSTANCE inst)
{
    WNDCLASS wc;
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = OleWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 4;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = SRVR_CLASS;
    if (!RegisterClass(&wc))
        return FALSE;
    wc.lpszClassName = DOC_CLASS;
    return RegisterClass(&wc);
}

static HWND make_window(LPOBJ o, BOOL doc)
{
    HWND w = CreateWindow(doc ? DOC_CLASS : SRVR_CLASS, doc ? "Window Name" : "",
                          WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, g_inst, NULL);
    if (w)
        SetWindowLong(w, 0, (LONG)o);
    return w;
}

static void kill_window(HWND FAR *w)
{
    if (*w && IsWindow(*w)) {
        SetWindowLong(*w, 0, 0);
        DestroyWindow(*w);
    }
    *w = NULL;
}

/* INITIATE app/topic from w; the first answer wins (others are
   terminated, as DDE requires) */
static HWND initiate(HWND w, LPCSTR app, LPCSTR topic)
{
    ATOM a = GlobalAddAtom(app), t = GlobalAddAtom(topic);
    HWND r;
    g_initiating = w;
    g_answer = NULL;
    SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)w, MAKELONG(a, t));
    g_initiating = NULL;
    if (a)
        GlobalDeleteAtom(a);
    if (t)
        GlobalDeleteAtom(t);
    r = g_answer;
    g_answer = NULL;
    return r;
}

/* ---- the step list ---- */

static STEP FAR *steps(LPOBJ o)
{
    return (STEP FAR *)GlobalLock(o->hsteps);
}

void dde_add(LPOBJ o, WORD op, WORD fmt, HGLOBAL data, WORD arg)
{
    STEP FAR *s;
    WORD cap = o->hsteps ? (WORD)(GlobalSize(o->hsteps) / sizeof(STEP)) : 0;
    if (o->nsteps >= cap) {
        HGLOBAL h;
        cap = cap ? cap * 2 : 8;
        h = o->hsteps ? GlobalReAlloc(o->hsteps, cap * sizeof(STEP), GMEM_MOVEABLE)
                      : GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, cap * sizeof(STEP));
        if (!h)
            return;
        o->hsteps = h;
    }
    s = steps(o);
    s[o->nsteps].op = op;
    s[o->nsteps].fmt = fmt;
    s[o->nsteps].data = data;
    s[o->nsteps].arg = arg;
    o->nsteps++;
    GlobalUnlock(o->hsteps);
}

/* insert a step right after the current one */
static void insert_next(LPOBJ o, WORD op, WORD fmt, HGLOBAL data, WORD arg)
{
    STEP FAR *s;
    WORD i;
    dde_add(o, ST_NONE, 0, NULL, 0);
    s = steps(o);
    for (i = o->nsteps - 1; i > o->cur + 1; i--)
        s[i] = s[i - 1];
    s[o->cur + 1].op = op;
    s[o->cur + 1].fmt = fmt;
    s[o->cur + 1].data = data;
    s[o->cur + 1].arg = arg;
    GlobalUnlock(o->hsteps);
}

static void clear_steps(LPOBJ o)
{
    STEP FAR *s;
    WORD i;
    if (!o->hsteps)
        return;
    s = steps(o);
    for (i = o->cur; i < o->nsteps; i++)
        if (s[i].data && s[i].op != ST_POKE)
            GlobalFree(s[i].data);
    GlobalUnlock(o->hsteps);
    o->nsteps = o->cur = 0;
}

void dde_set_arg(LPOBJ o, WORD i, WORD arg)
{
    STEP FAR *s = steps(o);
    s[i].arg = arg;
    GlobalUnlock(o->hsteps);
}

BOOL file_exists(LPCSTR f)
{
    OFSTRUCT of;
    return OpenFile(f, &of, OF_EXIST) != HFILE_ERROR;
}

OLESTATUS dde_start(LPOBJ o, OLE_RELEASE_METHOD m)
{
    clear_steps(o);
    o->launched_now = FALSE;
    o->rel_method = m;
    o->rel_error = OLE_OK;
    return OLE_OK;
}

/* "[cmd("a","b","c")]" with the quoted arguments given (NULL: fewer) */
HGLOBAL dde_exec_str(LPCSTR cmd, LPCSTR a, LPCSTR b)
{
    WORD n = slen(cmd) + slen(a) + slen(b) + 16;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    LPSTR p;
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    lstrcpy(p, "[");
    lstrcat(p, cmd);
    if (a) {
        lstrcat(p, "(\"");
        lstrcat(p, a);
        lstrcat(p, "\"");
        if (b) {
            lstrcat(p, ",\"");
            lstrcat(p, b);
            lstrcat(p, "\"");
        }
        lstrcat(p, ")");
    }
    lstrcat(p, "]");
    GlobalUnlock(h);
    return h;
}

/* "[cmd("a","b","c")]" (StdNewFromTemplate) */
HGLOBAL dde_exec_str3(LPCSTR cmd, LPCSTR a, LPCSTR b, LPCSTR c)
{
    WORD n = slen(cmd) + slen(a) + slen(b) + slen(c) + 20;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    LPSTR p;
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    lstrcpy(p, "[");
    lstrcat(p, cmd);
    lstrcat(p, "(\"");
    lstrcat(p, a);
    lstrcat(p, "\",\"");
    lstrcat(p, b);
    lstrcat(p, "\",\"");
    lstrcat(p, c);
    lstrcat(p, "\")]");
    GlobalUnlock(h);
    return h;
}

/* item names (step arg) */
static LPCSTR item_name(LPOBJ o, WORD arg, LPSTR buf, WORD cb)
{
    static const char *const names[] = {
        "", "/Save", "/Close", "StdDocumentName", "StdHostNames", "StdDocDimensions",
        "StdColorScheme", "StdTargetDevice"
    };
    if (arg == IT_LINK) {
        LPSTR s = gstr(o->hitem);
        lstrcpyn(buf, s, cb);
        gstr_unlock(o->hitem);
        return buf;
    }
    if (arg == IT_LINKSAVE || arg == IT_LINKCLOSE) {
        LPSTR s = gstr(o->hitem);
        lstrcpyn(buf, s, cb - 8);
        gstr_unlock(o->hitem);
        lstrcat(buf, arg == IT_LINKSAVE ? "/Save" : "/Close");
        return buf;
    }
    return names[arg];
}

/* ---- server start ---- */

static HINSTANCE launch(LPOBJ o, LPCSTR file, WORD show)
{
    char path[260];
    HGLOBAL ht;
    LPSTR tail;
    WORD showw[2];
    struct {
        WORD env;
        LPSTR tail;
        WORD FAR *show;
        DWORD reserved;
    } pb;
    LPSTR cls = gstr(o->hclass);
    BOOL ok = reg_server(cls, "StdFileEditing", path, sizeof path);
    HINSTANCE h;
    gstr_unlock(o->hclass);
    if (!ok)
        return NULL;
    /* The command tail is the string " -Embedding [file]" as it is: its
       first character, the space, is what LoadModule takes for the
       length byte (observed with OCSRV: the server's PSP tail has length
       32 and starts "-Embedding "; its command line is "-Embedding " for
       a new object, "-Embedding <file>" for a file). */
    ht = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 13 + slen(file) + 34);
    if (!ht)
        return NULL;
    tail = (LPSTR)GlobalLock(ht);
    lstrcpy(tail, " -Embedding ");
    if (file)
        lstrcat(tail, file);
    showw[0] = 2;
    showw[1] = show;
    pb.env = 0;
    pb.tail = tail;
    pb.show = showw;
    pb.reserved = 0;
    h = LoadModule(path, &pb);
    GlobalUnlock(ht);
    GlobalFree(ht);
    if ((UINT)h < 32)
        return NULL;
    return h;
}

/* initiate after a server start: it may take a while to register
   (UNVERIFIED how long OLECLI keeps trying; one answer was always there
   at once on the reference install) */
static HWND initiate_retry(HWND w, LPCSTR app, LPCSTR topic, HINSTANCE inst)
{
    DWORD end = GetTickCount() + 30000L;
    HWND r;
    MSG msg;
    for (;;) {
        r = initiate(w, app, topic);
        if (r || (long)(GetTickCount() - end) >= 0)
            return r;
        if (inst && !GetModuleUsage(inst))
            return NULL;
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else
            Yield();
    }
}

/* ---- running the steps ---- */

static void post_ack_neg(HWND to, HWND from, WORD item)
{
    PostMessage(to, WM_DDE_ACK, (WPARAM)from, MAKELONG(0, item));
}

enum { R_NEXT, R_WAIT, R_FAIL };

static int step_do(LPOBJ o, STEP FAR *s, OLESTATUS FAR *err)
{
    LPSTR cls;
    char buf[256];
    HWND r;
    switch (s->op) {
    case ST_NONE:
        return R_NEXT;

    case ST_CONNECT_SYS:
        if (o->hsrvsys && IsWindow(o->hsrvsys))
            return R_NEXT;
        cls = gstr(o->hclass);
        lstrcpyn(buf, cls, sizeof buf);
        gstr_unlock(o->hclass);
        o->hsys = make_window(o, FALSE);
        r = initiate(o->hsys, buf, "OLEsystem");
        if (!r)
            r = initiate(o->hsys, buf, "System");
        if (!r) {
            HINSTANCE h;
            kill_window(&o->hsys);
            h = launch(o, NULL, s->arg);
            o->launched_now = TRUE;
            if (!h) {
                *err = OLE_ERROR_LAUNCH;
                return R_FAIL;
            }
            o->launched = h;
            o->hsys = make_window(o, FALSE);
            r = initiate_retry(o->hsys, buf, "OLEsystem", h);
            if (!r) {
                kill_window(&o->hsys);
                *err = OLE_ERROR_COMM;
                return R_FAIL;
            }
        }
        o->hsrvsys = r;
        return R_NEXT;

    case ST_SYS_EXEC:
        o->wait = W_ACK;
        if (!PostMessage(o->hsrvsys, WM_DDE_EXECUTE, (WPARAM)o->hsys,
                         MAKELONG(0, s->data))) {
            *err = OLE_ERROR_COMM;
            return R_FAIL;
        }
        return R_WAIT;

    case ST_DOC_CONNECT:
        {
            HGLOBAL t = s->data ? s->data : o->htopic;
            LPSTR tp;
            cls = gstr(o->hclass);
            lstrcpyn(buf, cls, sizeof buf);
            gstr_unlock(o->hclass);
            o->hdoc = make_window(o, TRUE);
            tp = gstr(t);
            r = initiate(o->hdoc, buf, tp);
            gstr_unlock(t);
            if (!r) {
                kill_window(&o->hdoc);
                *err = s->arg ? s->arg : OLE_ERROR_OPEN;
                return R_FAIL;
            }
            o->hsrvdoc = r;
            o->open = TRUE;
            return R_NEXT;
        }

    case ST_CONNECT_DOC:
        /* a document by file name: a server that has it open answers its
           topic; else a running server is asked to open it; else one is
           started on it */
        {
            char file[260];
            LPSTR p;
            HINSTANCE h;
            if (o->hsrvdoc && IsWindow(o->hsrvdoc) && o->open)
                return R_NEXT;
            cls = gstr(o->hclass);
            lstrcpyn(buf, cls, sizeof buf);
            gstr_unlock(o->hclass);
            p = gstr(o->htopic);
            lstrcpyn(file, p, sizeof file);
            gstr_unlock(o->htopic);
            o->hdoc = make_window(o, TRUE);
            r = initiate(o->hdoc, buf, file);
            if (r) {
                o->hsrvdoc = r;
                o->open = TRUE;
                return R_NEXT;
            }
            kill_window(&o->hdoc);
            o->hsys = make_window(o, FALSE);
            r = initiate(o->hsys, buf, "OLEsystem");
            if (!r)
                r = initiate(o->hsys, buf, "System");
            if (r) {
                /* UNVERIFIED (not observed yet): StdOpenDocument on the
                   system topic, then the document */
                o->hsrvsys = r;
                insert_next(o, ST_DOC_CONNECT, 0, NULL, 0);
                insert_next(o, ST_SYS_EXEC, 0, dde_exec_str("StdOpenDocument", file, NULL),
                            OLE_ERROR_OPEN);
                return R_NEXT;
            }
            kill_window(&o->hsys);
            if (!file_exists(file)) {
                /* observed for a file that is not there: one more INITIATE
                   on the document (a server may have it open unsaved),
                   then OLE_ERROR_OPEN; no server is started */
                o->hdoc = make_window(o, TRUE);
                r = initiate(o->hdoc, buf, file);
                if (r) {
                    o->hsrvdoc = r;
                    o->open = TRUE;
                    return R_NEXT;
                }
                kill_window(&o->hdoc);
                *err = OLE_ERROR_OPEN;
                return R_FAIL;
            }
            h = launch(o, file, s->arg);
            o->launched_now = TRUE;
            if (!h) {
                *err = OLE_ERROR_LAUNCH;
                return R_FAIL;
            }
            o->launched = h;
            o->hdoc = make_window(o, TRUE);
            r = initiate_retry(o->hdoc, buf, file, h);
            if (!r) {
                kill_window(&o->hdoc);
                *err = OLE_ERROR_OPEN;
                return R_FAIL;
            }
            o->hsrvdoc = r;
            o->open = TRUE;
            return R_NEXT;
        }

    case ST_EXEC:
        o->wait = W_ACK;
        if (!o->hsrvdoc || !PostMessage(o->hsrvdoc, WM_DDE_EXECUTE, (WPARAM)o->hdoc,
                                        MAKELONG(0, s->data))) {
            *err = OLE_ERROR_COMM;
            return R_FAIL;
        }
        return R_WAIT;

    case ST_ADVISE:
        {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DDEADVISE));
            DDEADVISE FAR *a;
            ATOM item;
            if (!h) {
                *err = OLE_ERROR_MEMORY;
                return R_FAIL;
            }
            a = (DDEADVISE FAR *)GlobalLock(h);
            *(WORD FAR *)a = 0x8000;            /* fAckReq (observed flags 8000) */
            a->cfFormat = s->fmt;
            GlobalUnlock(h);
            item = GlobalAddAtom(item_name(o, s->arg, buf, sizeof buf));
            s->data = h;
            o->wait = W_ACK;
            if (!PostMessage(o->hsrvdoc, WM_DDE_ADVISE, (WPARAM)o->hdoc, MAKELONG(h, item))) {
                GlobalFree(h);
                s->data = NULL;
                if (item)
                    GlobalDeleteAtom(item);
                *err = OLE_ERROR_COMM;
                return R_FAIL;
            }
            return R_WAIT;
        }

    case ST_REQUEST:
        {
            ATOM item = GlobalAddAtom(item_name(o, s->arg, buf, sizeof buf));
            o->wait = W_DATA;
            if (!PostMessage(o->hsrvdoc, WM_DDE_REQUEST, (WPARAM)o->hdoc,
                             MAKELONG(s->fmt, item))) {
                if (item)
                    GlobalDeleteAtom(item);
                *err = OLE_ERROR_COMM;
                return R_FAIL;
            }
            return R_WAIT;
        }

    case ST_POKE:
        {
            ATOM item = GlobalAddAtom(item_name(o, s->arg, buf, sizeof buf));
            o->wait = W_ACK;
            if (!PostMessage(o->hsrvdoc, WM_DDE_POKE, (WPARAM)o->hdoc,
                             MAKELONG(s->data, item))) {
                if (item)
                    GlobalDeleteAtom(item);
                *err = OLE_ERROR_COMM;
                return R_FAIL;
            }
            return R_WAIT;
        }

    case ST_TERMINATE:
        if (!o->hdoc || !o->hsrvdoc || !IsWindow(o->hsrvdoc)) {
            kill_window(&o->hdoc);
            o->hsrvdoc = NULL;
            o->open = FALSE;
            return R_NEXT;
        }
        o->wait = W_TERM;
        PostMessage(o->hsrvdoc, WM_DDE_TERMINATE, (WPARAM)o->hdoc, 0);
        return R_WAIT;

    case ST_WAIT_DOCTERM:
        if (!o->hdoc)
            return R_NEXT;
        o->wait = W_DOCTERM;
        return R_WAIT;

    case ST_CLOSE_SYS:
        if (!o->hsys)
            return R_NEXT;
        if (!o->hsrvsys || !IsWindow(o->hsrvsys)) {
            kill_window(&o->hsys);
            o->hsrvsys = NULL;
            return R_NEXT;
        }
        o->wait = W_TERM;
        PostMessage(o->hsrvsys, WM_DDE_TERMINATE, (WPARAM)o->hsys, 0);
        return R_WAIT;

    case ST_MAYBE_TERMINATE:
        /* a server nobody can see (started hidden for us) is let go;
           one the user has open keeps the conversation (observed) */
        if (!server_visible(o))
            insert_next(o, ST_TERMINATE, 0, NULL, 0);
        return R_NEXT;

    case ST_DONE_CHANGED:
        callback(o, OLE_CHANGED);
        return R_NEXT;

    case ST_SKIP_UNLESS_LAUNCHED:
        if (!o->launched_now)
            o->cur += s->arg;
        return R_NEXT;

    case ST_FINISH_OPEN:
        /* observed: when the server refused an advise, OLECLI closes the
           new document again (the object stays blank, no error) */
        if (o->advfail) {
            o->advfail = FALSE;
            insert_next(o, ST_WAIT_SYSTERM, 0, NULL, 1);
            insert_next(o, ST_WAIT_DOCTERM, 0, NULL, 0);
            insert_next(o, ST_EXEC, 0, dde_exec_str("StdCloseDocument", NULL, NULL), 0);
        } else if (s->arg)
            insert_next(o, ST_EXEC, 0, dde_exec_str("StdShowItem", "", NULL), OLE_ERROR_SHOW);
        return R_NEXT;

    case ST_BECOME_EMBEDDED:
        obj_set_number(o);
        gfree(o->htopic);
        o->htopic = obj_embedded_topic(o);
        return R_NEXT;

    case ST_CONNECT_RUNNING:
        /* only a server that has the document open already */
        {
            char file[260];
            LPSTR p;
            cls = gstr(o->hclass);
            lstrcpyn(buf, cls, sizeof buf);
            gstr_unlock(o->hclass);
            p = gstr(o->htopic);
            lstrcpyn(file, p, sizeof file);
            gstr_unlock(o->htopic);
            o->hdoc = make_window(o, TRUE);
            r = initiate(o->hdoc, buf, file);
            if (!r) {
                kill_window(&o->hdoc);
                clear_steps(o);
                return R_NEXT;
            }
            o->hsrvdoc = r;
            o->open = TRUE;
            return R_NEXT;
        }

    case ST_WAIT_SYSTERM:
        /* a server OLE started ends the system conversation itself when
           its document is closed (observed) */
        if (!o->hsys)
            return R_NEXT;
        if (!o->hsrvsys || !IsWindow(o->hsrvsys)) {
            kill_window(&o->hsys);
            o->hsrvsys = NULL;
            return R_NEXT;
        }
        if (!o->launched) {
            o->wait = W_TERM;
            PostMessage(o->hsrvsys, WM_DDE_TERMINATE, (WPARAM)o->hsys, 0);
            return R_WAIT;
        }
        o->wait = s->arg ? W_SYSTERM_KEEP : W_SYSTERM;
        return R_WAIT;
    }
    return R_NEXT;
}

/* Run steps until one waits (OLE_WAIT_FOR_RELEASE) or all are done
   (OLE_OK, or the error). from_api: the steps started in an API call (the
   call returns the status; no OLE_RELEASE if nothing had to wait). */
static OLESTATUS run(LPOBJ o, BOOL from_api)
{
    OLESTATUS err = OLE_OK;
    for (;;) {
        STEP FAR *s;
        int r;
        if (o->cur >= o->nsteps) {
            o->nsteps = o->cur = 0;
            o->wait = W_NONE;
            if (o->busy) {
                o->busy = FALSE;
                callback(o, OLE_RELEASE);
                if (o->free_after) {
                    dde_close_all(o);
                    obj_free(o);
                }
            }
            return OLE_OK;
        }
        s = steps(o);
        TRACE(("step %d/%d op %d arg %d launched_now %d open %d", o->cur, o->nsteps,
               s[o->cur].op, s[o->cur].arg, o->launched_now, o->open));
        r = step_do(o, &s[o->cur], &err);
        GlobalUnlock(o->hsteps);
        if (r == R_WAIT) {
            if (!o->busy) {
                o->busy = TRUE;
                return OLE_WAIT_FOR_RELEASE;
            }
            return OLE_WAIT_FOR_RELEASE;
        }
        if (r == R_FAIL) {
            o->rel_error = err;
            clear_steps(o);
            o->wait = W_NONE;
            if (o->busy) {
                o->busy = FALSE;
                callback(o, OLE_RELEASE);
            }
            return from_api ? err : OLE_OK;
        }
        o->cur++;
    }
}

OLESTATUS dde_run(LPOBJ o)
{
    return run(o, TRUE);
}

static void step_next(LPOBJ o)
{
    o->wait = W_NONE;
    o->cur++;
    run(o, FALSE);
}

static void step_fail(LPOBJ o, OLESTATUS e)
{
    o->rel_error = e;
    clear_steps(o);
    o->wait = W_NONE;
    if (o->busy) {
        o->busy = FALSE;
        callback(o, OLE_RELEASE);
    }
}

/* ---- incoming ---- */

/* data from the server: Native, a presentation, or the document's name */
static void take_data(LPOBJ o, HGLOBAL h, ATOM item, BOOL FAR *saved, BOOL FAR *closed)
{
    DDEDATA FAR *d = (DDEDATA FAR *)GlobalLock(h);
    WORD flags = *(WORD FAR *)d;
    WORD cf = d->cfFormat;
    char name[64];
    BOOL release = (flags & 0x2000) != 0;
    BOOL ackreq = (flags & 0x8000) != 0;
    name[0] = 0;
    if (item)
        GlobalGetAtomName(item, name, sizeof name);
    *saved = *closed = FALSE;
    if (cf == cf_native) {
        DWORD n = GlobalSize(h) - 4;
        /* a block of the DDE data's size less the header: its GlobalSize
           is what a stream gets (observed); the rounding is zeroed (in
           Microsoft's it is whatever memory held) */
        HGLOBAL nh = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n);
        if (nh) {
            hmemcpy(GlobalLock(nh), (BYTE FAR *)d + 4, n);
            GlobalUnlock(nh);
            gfree(o->hnative);
            o->hnative = nh;
        }
        GlobalUnlock(h);
        if (release)
            GlobalFree(h);
    } else if (cf == cf_binary && !lstrcmp(name, "StdDocumentName")) {
        /* the linked document was renamed: the new name is the data */
        gfree(o->htopic);
        o->htopic = gstr_new((LPSTR)d + 4);
        GlobalUnlock(h);
        if (release)
            GlobalFree(h);
        callback(o, OLE_RENAMED);
    } else {
        HANDLE ph = *(HANDLE FAR *)((BYTE FAR *)d + 4);
        HANDLE keep;
        if (cf == CF_METAFILEPICT || cf == CF_BITMAP || cf == CF_PALETTE) {
            /* always a copy: the server's blocks belong to its task and go
               when it exits (observed: a picture kept as sent was gone
               after the server closed); with fRelease the originals are
               freed here, as DDE has the receiver do */
            keep = pres_copy(cf, ph);
            GlobalUnlock(h);
            if (release) {
                if (cf == CF_METAFILEPICT) {
                    METAFILEPICT FAR *m = (METAFILEPICT FAR *)GlobalLock(ph);
                    if (m) {
                        DeleteMetaFile(m->hMF);
                        GlobalUnlock(ph);
                    }
                    GlobalFree(ph);
                } else
                    DeleteObject(ph);
                GlobalFree(h);
            }
        } else {
            DWORD n = GlobalSize(h) - 4;
            keep = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n);
            if (keep) {
                hmemcpy(GlobalLock(keep), (BYTE FAR *)d + 4, n);
                GlobalUnlock(keep);
            }
            GlobalUnlock(h);
            if (release)
                GlobalFree(h);
        }
        if (keep)
            pres_set(o, cf, keep);
    }
    (void)ackreq;
    if (!lstrcmpi(name + slen(name) - (slen(name) >= 5 ? 5 : slen(name)), "/Save"))
        *saved = TRUE;
    if (slen(name) >= 6 && !lstrcmpi(name + slen(name) - 6, "/Close"))
        *closed = TRUE;
}

/* the format the advise batch ends with: the presentation's, or Native
   when the object keeps no presentation of its own */
static BOOL last_of_batch(LPOBJ o, WORD cf)
{
    WORD pf = obj_advise_pres(o);
    return pf ? cf == pf : cf == cf_native;
}

static void on_ack(LPOBJ o, HWND w, HWND from, LPARAM lp)
{
    STEP FAR *s;
    WORD status = LOWORD(lp), hi = HIWORD(lp);
    BOOL ok = (status & 0x8000) != 0;
    WORD op;
    if (o->wait != W_ACK && o->wait != W_DATA) {
        /* an answer nobody waits for: the command it returns, or its atom */
        if (hi && hi == (WORD)o->hloose) {
            GlobalFree(o->hloose);
            o->hloose = NULL;
        } else if (hi >= 0xC000)
            GlobalDeleteAtom(hi);
        return;
    }
    (void)w;
    (void)from;
    s = steps(o);
    op = s[o->cur].op;
    switch (op) {
    case ST_SYS_EXEC:
    case ST_EXEC:
        /* the command string comes back with the ACK */
        if (hi && s[o->cur].data == (HGLOBAL)hi) {
            GlobalFree((HGLOBAL)hi);
            s[o->cur].data = NULL;
        }
        if (!ok && s[o->cur].arg) {
            OLESTATUS e = s[o->cur].arg;
            GlobalUnlock(o->hsteps);
            step_fail(o, e);
            return;
        }
        break;
    case ST_ADVISE:
        if (hi)
            GlobalDeleteAtom(hi);
        if (!ok) {
            if (s[o->cur].data)
                GlobalFree(s[o->cur].data);
            o->advfail = TRUE;
        }
        s[o->cur].data = NULL;
        break;
    case ST_POKE:
        if (hi)
            GlobalDeleteAtom(hi);
        /* fRelease was not set: the poked data is ours to free */
        if (s[o->cur].data) {
            GlobalFree(s[o->cur].data);
            s[o->cur].data = NULL;
        }
        break;
    case ST_REQUEST:
        /* a negative answer instead of data */
        if (hi)
            GlobalDeleteAtom(hi);
        if (!ok) {
            OLESTATUS e = s[o->cur].fmt == cf_native ? OLE_ERROR_REQUEST_NATIVE
                                                     : OLE_ERROR_REQUEST_PICT;
            GlobalUnlock(o->hsteps);
            step_fail(o, e);
            return;
        }
        break;
    }
    GlobalUnlock(o->hsteps);
    step_next(o);
}

static void on_data(LPOBJ o, HWND w, HWND from, LPARAM lp)
{
    HGLOBAL h = (HGLOBAL)LOWORD(lp);
    ATOM item = HIWORD(lp);
    DDEDATA FAR *d;
    WORD flags, cf;
    BOOL saved, closed, response;
    if (!h)
        return;
    d = (DDEDATA FAR *)GlobalLock(h);
    flags = *(WORD FAR *)d;
    cf = d->cfFormat;
    GlobalUnlock(h);
    response = (flags & 0x1000) != 0;
    if (flags & 0x8000)                 /* fAckReq */
        PostMessage(from, WM_DDE_ACK, (WPARAM)w, MAKELONG(0x8000, item));
    take_data(o, h, item, &saved, &closed);
    if (!(flags & 0x8000) && item)
        GlobalDeleteAtom(item);
    if (response && o->wait == W_DATA) {
        step_next(o);
        return;
    }
    /* advise data: one callback when the batch is complete */
    /* (the data on "/Close", sent when the server closes a changed
       object, is OLE_CHANGED: Write, given nothing then, dropped the new
       object; UNVERIFIED by a probe client: tests/ole/obs/srvclose.txt) */
    if (!last_of_batch(o, cf))
        return;
    callback(o, saved ? OLE_SAVED : OLE_CHANGED);
    if (closed && o->hdoc && o->hsrvdoc && o->wait == W_NONE) {
        /* observed: after the "/Close" data OLECLI sends
           [StdCloseDocument] and the client gets OLE_CLOSED at once, before
           the server's TERMINATE (which is then just answered) */
        HGLOBAL c = dde_exec_str("StdCloseDocument", NULL, NULL);
        if (!PostMessage(o->hsrvdoc, WM_DDE_EXECUTE, (WPARAM)o->hdoc, MAKELONG(0, c)))
            GlobalFree(c);
        else
            o->hloose = c;
        o->closing = TRUE;
        callback(o, OLE_CLOSED);
    }
}

/* the index of the next step op from the current one, or 0xFFFF */
static WORD find_step(LPOBJ o, WORD op)
{
    STEP FAR *s;
    WORD i, r = 0xFFFF;
    if (!o->hsteps)
        return r;
    s = steps(o);
    for (i = o->cur; i < o->nsteps; i++)
        if (s[i].op == op) {
            r = i;
            break;
        }
    GlobalUnlock(o->hsteps);
    return r;
}

static void on_terminate(LPOBJ o, HWND w, HWND from)
{
    BOOL doc = (w == o->hdoc);
    if (doc) {
        WORD at;
        if (o->wait != W_TERM)
            PostMessage(from, WM_DDE_TERMINATE, (WPARAM)w, 0);
        kill_window(&o->hdoc);
        o->hsrvdoc = NULL;
        o->open = FALSE;
        if (o->wait == W_TERM || o->wait == W_DOCTERM)
            step_next(o);
        else if (o->wait != W_NONE && (at = find_step(o, ST_WAIT_DOCTERM)) != 0xFFFF) {
            /* closing anyway: the server's end is the close's (observed:
               no OLE_CLOSED then) */
            o->cur = at;
            step_next(o);
        } else {
            if (o->type == OT_EMBEDDED && !o->closing)
                callback(o, OLE_CLOSED);
            o->closing = FALSE;
            if (o->wait != W_NONE)
                step_fail(o, OLE_ERROR_TERMINATE);
        }
    } else if (w == o->hsys && o->wait == W_SYSTERM_KEEP) {
        /* after a refused advise: the server's end is noted and the window
           kept; it is answered only when the object goes, with [StdExit]
           (observed: OleDelete then sends StdExit and TERMINATE from it) */
        step_next(o);
    } else if (w == o->hsys) {
        if (o->wait != W_TERM)
            PostMessage(from, WM_DDE_TERMINATE, (WPARAM)w, 0);
        kill_window(&o->hsys);
        o->hsrvsys = NULL;
        if (o->wait == W_TERM || o->wait == W_SYSTERM)
            step_next(o);
    } else
        PostMessage(from, WM_DDE_TERMINATE, (WPARAM)w, 0);
}

LRESULT OLEAPI OleWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    LPOBJ o;
    if (m < WM_DDE_FIRST || m > WM_DDE_LAST)
        return DefWindowProc(w, m, wp, lp);
    if (m == WM_DDE_ACK)
        TRACE(("ack to %x initiating %x from %x lp %lx", (WORD)w, (WORD)g_initiating,
               (WORD)wp, lp));
    if (m == WM_DDE_ACK && w == g_initiating) {
        /* an answer to our INITIATE (sent): the atoms are the server's */
        if (!g_answer)
            g_answer = (HWND)wp;
        else
            PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0);
        if (LOWORD(lp))
            GlobalDeleteAtom(LOWORD(lp));
        if (HIWORD(lp))
            GlobalDeleteAtom(HIWORD(lp));
        return 0;
    }
    if (m == WM_DDE_INITIATE)
        return 0;
    o = (LPOBJ)GetWindowLong(w, 0);
    if (!o || o->magic != OBJ_MAGIC) {
        if (m == WM_DDE_TERMINATE)
            PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0);
        return 0;
    }
    switch (m) {
    case WM_DDE_ACK:
        on_ack(o, w, (HWND)wp, lp);
        break;
    case WM_DDE_DATA:
        on_data(o, w, (HWND)wp, lp);
        break;
    case WM_DDE_TERMINATE:
        on_terminate(o, w, (HWND)wp);
        break;
    case WM_DDE_POKE:
    case WM_DDE_EXECUTE:
    case WM_DDE_ADVISE:
    case WM_DDE_UNADVISE:
    case WM_DDE_REQUEST:
        /* a client's messages: we are none's server */
        post_ack_neg((HWND)wp, w, HIWORD(lp));
        break;
    }
    return 0;
}

/* ---- visibility of a server (GetTaskVisibleWindow, OLECLI.954) ---- */

static HWND g_vis;

BOOL OLEAPI VisEnum(HWND w, LPARAM lp)
{
    (void)lp;
    if (IsWindowVisible(w)) {
        g_vis = w;
        return FALSE;
    }
    return TRUE;
}

/* OLECLI.954: a visible top-level window of the task, or NULL */
HWND OLEAPI GetTaskVisibleWindow(HTASK t)
{
    g_vis = NULL;
    EnumTaskWindows(t, (WNDENUMPROC)VisEnum, 0);
    return g_vis;
}

BOOL server_visible(LPOBJ o)
{
    if (!o->hsrvdoc || !IsWindow(o->hsrvdoc))
        return FALSE;
    return GetTaskVisibleWindow(GetWindowTask(o->hsrvdoc)) != NULL;
}

/* end every conversation at once (object deleted) */
void dde_close_all(LPOBJ o)
{
    if (o->hdoc) {
        if (o->hsrvdoc && IsWindow(o->hsrvdoc))
            PostMessage(o->hsrvdoc, WM_DDE_TERMINATE, (WPARAM)o->hdoc, 0);
        kill_window(&o->hdoc);
    }
    if (o->hsys) {
        if (o->hsrvsys && IsWindow(o->hsrvsys)) {
            HGLOBAL h = dde_exec_str("StdExit", NULL, NULL);
            if (!PostMessage(o->hsrvsys, WM_DDE_EXECUTE, (WPARAM)o->hsys, MAKELONG(0, h)))
                GlobalFree(h);
            PostMessage(o->hsrvsys, WM_DDE_TERMINATE, (WPARAM)o->hsys, 0);
        }
        kill_window(&o->hsys);
    }
    o->hsrvdoc = o->hsrvsys = NULL;
    o->open = FALSE;
    clear_steps(o);
}
