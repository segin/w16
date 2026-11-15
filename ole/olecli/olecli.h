/*
 * olecli.h - w16's OLECLI.DLL, the OLE 1.0 client library (ARCHITECTURE.md
 * 16). Clean-room: the behaviour is the documented OLE 1.0 client API
 * (Windows 3.1 SDK; OpenWatcom's ole.h) and what the probes OC1/OCSPY
 * observed of Microsoft's OLECLI on the reference install (tests/ole).
 */
#ifndef OLECLI_H
#define OLECLI_H

#include <windows.h>
#include <dde.h>
#include <ole.h>

/* every entry point loads OLECLI's DS (the callers' DS is theirs) */
#define OLEAPI __far __pascal __loadds

/* ---- objects ---- */

/* One per OLE object: what the client sees is the OLEOBJECT (its vtbl
   pointer comes first); the rest is ours. Allocated in shared global
   memory and kept locked (a far pointer to it is the object). */
typedef struct OBJ OBJ;
typedef OBJ FAR *LPOBJ;
typedef struct DOC DOC;
typedef DOC FAR *LPDOC;

/* what kind of presentation an object keeps */
enum {
    PRES_NONE = 0,
    PRES_MF,                    /* CF_METAFILEPICT */
    PRES_BM,                    /* CF_BITMAP */
    PRES_DIB,                   /* CF_DIB */
    PRES_GEN                    /* any other format (generic) */
};

/* one step of an asynchronous operation (see dde.c) */
typedef struct STEP {
    WORD op;
    WORD fmt;
    HGLOBAL data;               /* string or data the step sends */
    WORD arg;
} STEP;

struct OBJ {
    LPOLEOBJECTVTBL lpvtbl;     /* the OLEOBJECT */
    WORD magic;                 /* OBJ_MAGIC while alive */
    HGLOBAL hself;
    LPOBJ next;                 /* in its document */
    LPDOC doc;
    LPOLECLIENT client;
    LONG type;                  /* OT_LINK, OT_EMBEDDED, OT_STATIC */
    HGLOBAL hclass;             /* class name (string) */
    HGLOBAL htopic;             /* link: document; embedded: DDE name */
    HGLOBAL hitem;              /* link: item */
    HGLOBAL hname;              /* the client's name for the object */
    HGLOBAL hnet;               /* link: network name */
    WORD number;                /* the #nnn of its DDE document name */
    HGLOBAL hnative;            /* Native data */
    WORD pres;                  /* PRES_* */
    WORD presfmt;               /* clipboard format of the presentation */
    HANDLE hpres;               /* metafile pict, bitmap, DIB, data */
    WORD pb;                    /* Paintbrush built-in handler (pb.c) */
    OLEOPT_RENDER render;
    OLECLIPFORMAT renderfmt;
    OLEOPT_UPDATE update;       /* links */
    /* release state */
    BOOL busy;
    OLESTATUS rel_error;
    OLE_RELEASE_METHOD rel_method;
    /* DDE */
    HWND hsys;                  /* our system-topic window */
    HWND hsrvsys;               /* the server's system window */
    HWND hdoc;                  /* our document window */
    HWND hsrvdoc;               /* the server's document window */
    HINSTANCE launched;         /* the server we started */
    BOOL open;                  /* server has the object open */
    BOOL keep;                  /* keep the document conversation */
    /* the operation in progress */
    HGLOBAL hsteps;             /* STEP[] */
    WORD nsteps, cur;
    WORD wait;                  /* what the current step waits for */
    WORD pending_saved;         /* advise data seen since the last callback */
    BYTE advflags;              /* which advise item delivered data */
    BOOL changed;
    BOOL advfail;               /* the server refused an advise */
    HGLOBAL hloose;             /* a command posted without waiting */
    BOOL closing;               /* closed after "/Close" data; TERMINATE due */
    BOOL launched_now;          /* this operation started the server */
    BOOL free_after;            /* free the object after OLE_RELEASE */
    HGLOBAL hlinkdata;          /* OwnerLink/ObjectLink given by GetData */
    LONG objlong;               /* ObjectLong's slot */
    WORD locked;                /* OleLockServer count */
    HGLOBAL hhost;              /* host names (client app, client doc) */
    RECT bounds;
    BOOL hasbounds;
};

#define OBJ_MAGIC 0x434F        /* "OC" */

struct DOC {
    WORD magic;                 /* DOC_MAGIC */
    HGLOBAL hself;
    LPDOC next;
    HGLOBAL hclass, hname;      /* OleRegisterClientDoc's class and name */
    LPOBJ first;
};

#define DOC_MAGIC 0x444F        /* "OD" */

/* ---- globals (olecli.c) ---- */
extern HINSTANCE g_inst;
extern UINT cf_native, cf_ownerlink, cf_objectlink, cf_binary, cf_link;
extern OLEOBJECTVTBL g_vtbl;
extern WORD g_number;           /* the last #nnn given */

/* ---- olecli.c: objects, documents, the API ---- */
LPOBJ obj_check(LPOLEOBJECT o);
LPOBJ obj_new(LPOLECLIENT client, LPDOC doc, LPCSTR name);
void obj_free(LPOBJ o);
LPDOC doc_check(LHCLIENTDOC lh);
void obj_set_number(LPOBJ o);
LPSTR gstr(HGLOBAL h);
HGLOBAL gstr_new(LPCSTR s);
void gstr_unlock(HGLOBAL h);
void gfree(HGLOBAL h);
WORD slen(LPCSTR s);
void callback(LPOBJ o, OLE_NOTIFICATION n);
BOOL is_pbrush(LPCSTR cls);
HGLOBAL obj_embedded_topic(LPOBJ o);
WORD obj_advise_pres(LPOBJ o);

/* ---- reg.c: the registration database ---- */
BOOL reg_server(LPCSTR cls, LPCSTR protocol, LPSTR out, WORD cb);
BOOL reg_class_known(LPCSTR cls);
BOOL reg_class_of_file(LPCSTR file, LPSTR cls, WORD cb);

/* ---- pres.c: presentations ---- */
void pres_free(LPOBJ o);
OLESTATUS pres_bounds(LPOBJ o, RECT FAR *r);
OLESTATUS pres_draw(LPOBJ o, HDC dc, const RECT FAR *r, HDC fmt);
OLESTATUS pres_getdata(LPOBJ o, OLECLIPFORMAT f, HANDLE FAR *h);
HANDLE pres_copy(WORD fmt, HANDLE h);
void pres_set(LPOBJ o, WORD fmt, HANDLE h);
OLECLIPFORMAT pres_enum(LPOBJ o, OLECLIPFORMAT f);
HANDLE copy_global(HGLOBAL h);
HMETAFILE copy_metafile(HMETAFILE mf);
HBITMAP copy_bitmap(HBITMAP b);
BOOL pres_has_picture(LPOBJ o);
void pres_save_body(void FAR *out, LPOBJ ob, BOOL with_class);
void pres_make_static(LPOBJ o);
HGLOBAL link_data(LPOBJ o);
HGLOBAL poke_block(WORD cf, HANDLE h);
OLESTATUS link_from_clip(LPDOC d, LPOLECLIENT c, LPCSTR cls, LPCSTR file, LPCSTR item,
                         LPCSTR name, LPOLEOBJECT FAR *out, OLEOPT_RENDER r,
                         OLECLIPFORMAT f);
OLESTATUS link_loaded(LPOBJ o);

/* the vtbl entries (le.c) */
void FAR * OLEAPI LeQueryProtocol(LPOLEOBJECT, OLE_LPCSTR);
OLESTATUS OLEAPI LeRelease(LPOLEOBJECT);
OLESTATUS OLEAPI LeShow(LPOLEOBJECT, BOOL);
OLESTATUS OLEAPI LeDoVerb(LPOLEOBJECT, UINT, BOOL, BOOL);
OLESTATUS OLEAPI LeGetData(LPOLEOBJECT, OLECLIPFORMAT, HANDLE FAR *);
OLESTATUS OLEAPI LeSetData(LPOLEOBJECT, OLECLIPFORMAT, HANDLE);
OLESTATUS OLEAPI LeSetTargetDevice(LPOLEOBJECT, HGLOBAL);
OLESTATUS OLEAPI LeSetBounds(LPOLEOBJECT, OLE_CONST RECT FAR *);
OLECLIPFORMAT OLEAPI LeEnumFormat(LPOLEOBJECT, OLECLIPFORMAT);
OLESTATUS OLEAPI LeSetColorScheme(LPOLEOBJECT, OLE_CONST LOGPALETTE FAR *);
OLESTATUS OLEAPI LeDelete(LPOLEOBJECT);
OLESTATUS OLEAPI LeSetHostNames(LPOLEOBJECT, OLE_LPCSTR, OLE_LPCSTR);
OLESTATUS OLEAPI LeSaveToStream(LPOLEOBJECT, LPOLESTREAM);
OLESTATUS OLEAPI LeClone(LPOLEOBJECT, LPOLECLIENT, LHCLIENTDOC, OLE_LPCSTR, LPOLEOBJECT FAR *);
OLESTATUS OLEAPI LeCopyFromLink(LPOLEOBJECT, LPOLECLIENT, LHCLIENTDOC, OLE_LPCSTR, LPOLEOBJECT FAR *);
OLESTATUS OLEAPI LeEqual(LPOLEOBJECT, LPOLEOBJECT);
OLESTATUS OLEAPI LeCopyToClipboard(LPOLEOBJECT);
OLESTATUS OLEAPI LeDraw(LPOLEOBJECT, HDC, OLE_CONST RECT FAR *, OLE_CONST RECT FAR *, HDC);
OLESTATUS OLEAPI LeActivate(LPOLEOBJECT, UINT, BOOL, BOOL, HWND, OLE_CONST RECT FAR *);
OLESTATUS OLEAPI LeExecute(LPOLEOBJECT, HGLOBAL, UINT);
OLESTATUS OLEAPI LeClose(LPOLEOBJECT);
OLESTATUS OLEAPI LeUpdate(LPOLEOBJECT);
OLESTATUS OLEAPI LeReconnect(LPOLEOBJECT);
OLESTATUS OLEAPI LeObjectConvert(LPOLEOBJECT, OLE_LPCSTR, LPOLECLIENT, LHCLIENTDOC, OLE_LPCSTR, LPOLEOBJECT FAR *);
OLESTATUS OLEAPI LeGetUpdateOptions(LPOLEOBJECT, OLEOPT_UPDATE FAR *);
OLESTATUS OLEAPI LeSetUpdateOptions(LPOLEOBJECT, OLEOPT_UPDATE);
OLESTATUS OLEAPI ObjRename(LPOLEOBJECT, OLE_LPCSTR);
OLESTATUS OLEAPI ObjQueryName(LPOLEOBJECT, LPSTR, UINT FAR *);
OLESTATUS OLEAPI ObjQueryType(LPOLEOBJECT, LONG FAR *);
OLESTATUS OLEAPI LeQueryBounds(LPOLEOBJECT, RECT FAR *);
OLESTATUS OLEAPI ObjQuerySize(LPOLEOBJECT, DWORD FAR *);
OLESTATUS OLEAPI LeQueryOpen(LPOLEOBJECT);
OLESTATUS OLEAPI LeQueryOutOfDate(LPOLEOBJECT);
OLESTATUS OLEAPI LeQueryReleaseStatus(LPOLEOBJECT);
OLESTATUS OLEAPI LeQueryReleaseError(LPOLEOBJECT);
OLE_RELEASE_METHOD OLEAPI LeQueryReleaseMethod(LPOLEOBJECT);
OLESTATUS OLEAPI LeRequestData(LPOLEOBJECT, OLECLIPFORMAT);
OLESTATUS OLEAPI LeObjectLong(LPOLEOBJECT, UINT, LONG FAR *);
OLESTATUS OLEAPI LeChangeData(LPOLEOBJECT, HANDLE, LPOLECLIENT, BOOL);

/* ---- stream.c ---- */
OLESTATUS strm_save(LPOBJ o, LPOLESTREAM s);
OLESTATUS strm_load(LPOLESTREAM s, LPOLECLIENT c, LPDOC doc, LPCSTR name, LPOLEOBJECT FAR *out);
DWORD strm_size(LPOBJ o);
OLESTATUS strm_load_rest(LPOLESTREAM s, LPOLECLIENT c, LPDOC doc, LPCSTR name,
                         LPOLEOBJECT FAR *out, LONG fmt, LPCSTR clsname);
LRESULT OLEAPI OleWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp);

/* ---- dde.c: conversations with servers ---- */
enum {                          /* steps */
    ST_NONE = 0,
    ST_CONNECT_DOC,             /* INITIATE on the document; launch if needed */
    ST_CONNECT_SYS,             /* INITIATE on the system topic; launch */
    ST_SYS_EXEC,                /* EXECUTE on the system topic */
    ST_DOC_CONNECT,             /* INITIATE on the (new) document topic */
    ST_EXEC,                    /* EXECUTE on the document */
    ST_ADVISE,                  /* ADVISE item fmt (arg: 0 "", 1 /Save, 2 /Close, 3 StdDocumentName) */
    ST_REQUEST,                 /* REQUEST fmt */
    ST_POKE,                    /* POKE item fmt data */
    ST_TERMINATE,               /* end the document conversation */
    ST_CLOSE_SYS,               /* end the system conversation */
    ST_DONE_CHANGED,            /* OLE_CHANGED callback */
    ST_WAIT_DOCTERM,            /* wait for the server to end the document */
    ST_MAYBE_TERMINATE,         /* end it if the server is not visible */
    ST_FINISH_OPEN,             /* show the new object, or close it if an
                                   advise was refused */
    ST_BECOME_EMBEDDED,         /* the object's topic is its DDE name now */
    ST_CONNECT_RUNNING,         /* INITIATE on the document; no start */
    ST_WAIT_SYSTERM,            /* wait for the system conversation's end */
    ST_SKIP_UNLESS_LAUNCHED     /* skip arg steps unless a server was started */
};
enum {                          /* DDE item names (step arg) */
    IT_NONE = 0,                /* "" */
    IT_SAVE,                    /* "/Save" */
    IT_CLOSE,                   /* "/Close" */
    IT_DOCNAME,                 /* "StdDocumentName" */
    IT_HOST,                    /* "StdHostNames" */
    IT_DIM,                     /* "StdDocDimensions" */
    IT_COLOR,                   /* "StdColorScheme" */
    IT_TARGET,                  /* "StdTargetDevice" */
    IT_LINK,                    /* a link's item */
    IT_LINKSAVE,                /* "<item>/Save" */
    IT_LINKCLOSE                /* "<item>/Close" */
};
BOOL server_visible(LPOBJ o);
HWND OLEAPI GetTaskVisibleWindow(HTASK t);
OLESTATUS dde_start(LPOBJ o, OLE_RELEASE_METHOD m);
void dde_add(LPOBJ o, WORD op, WORD fmt, HGLOBAL data, WORD arg);
OLESTATUS dde_run(LPOBJ o);
void dde_close_all(LPOBJ o);
void dde_set_arg(LPOBJ o, WORD i, WORD arg);
BOOL file_exists(LPCSTR f);
BOOL dde_register(HINSTANCE inst);
HGLOBAL dde_exec_str(LPCSTR cmd, LPCSTR a, LPCSTR b);
HGLOBAL dde_exec_str3(LPCSTR cmd, LPCSTR a, LPCSTR b, LPCSTR c);

/* ---- clip.c ---- */
OLESTATUS clip_copy(LPOBJ o);
OLESTATUS clip_query(LPCSTR protocol, OLEOPT_RENDER r, OLECLIPFORMAT f, BOOL link);

/* debugging: a build with -dOLETRACE appends to C:\OLECLI.LOG */
#ifdef OLETRACE
void olecli_trace(LPCSTR fmt, ...);
#define TRACE(x) olecli_trace x
#else
#define TRACE(x)
#endif

#endif
