/*
 * olesvr.h - w16's OLESVR.DLL, the OLE 1.0 server library
 * (ARCHITECTURE.md 16.2).
 *
 * A server (OleRegisterServer) gets a hidden top-level window of class
 * SrvrWndClass that answers WM_DDE_INITIATE for (class, "OLEsystem" or
 * "System"); each document (OleRegisterServerDoc, or a StdNewDocument
 * etc. from a client) gets one of class DocWndClass that answers
 * (class, document name). The objects a document hands out
 * (OLESERVERDOCVTBL GetObject) are ITEMs, kept per document across
 * conversations; their OLECLIENT is the library's, whose CallBack sends
 * the DDE data of the clients' advises. Everything is in linked lists of
 * global blocks: no limits on servers, documents, objects or clients.
 */
#ifndef OLESVR_H
#define OLESVR_H

#include <windows.h>
#include <dde.h>
#include <ole.h>

#define FARP FAR

/* a DDE conversation: one client window talking to one of our windows */
typedef struct CONV {
    struct CONV FARP *next;
    HWND client;
    BOOL terminating;       /* we posted (or will post) WM_DDE_TERMINATE */
    BOOL term_posted;
    BOOL closing;           /* the client's TERMINATE is being handled */
} CONV;

/* an advise: client wants data of format cf when the item changes (kind
   ADV_CHANGE), is saved or closed; text is the item as advised */
#define ADV_CHANGE 0
#define ADV_SAVE   1
#define ADV_CLOSE  2
#define ADV_RENAME 3                    /* StdDocumentName */
typedef struct ADV {
    struct ADV FARP *next;
    HWND client;
    WORD kind, cf, flags;
    char text[1];                       /* item name as advised */
} ADV;

struct DOC;

/* an object of a document; its OLECLIENT comes first, so the LPOLECLIENT
   a server gets is the ITEM */
typedef struct ITEM {
    OLECLIENT cl;
    struct ITEM FARP *next;
    struct DOC FARP *doc;
    LPOLEOBJECT obj;
    ADV FARP *advs;
    CONV FARP *users;                   /* clients that used the item */
    WORD target_gen;                    /* the target device it has */
    HWND hwnd;                          /* its ItemWndClass window */
    char name[1];
} ITEM;

typedef struct DOC {
    WORD magic;
    struct DOC FARP *next;
    struct SRV FARP *srv;
    LPOLESERVERDOC lpdoc;
    HWND hwnd;
    CONV FARP *convs;
    ITEM FARP *items;
    ADV FARP *renames;                  /* StdDocumentName advises */
    BOOL revoking, pending, released;
    ATOM atom;                          /* the topic: documents are found
                                           by atom, as Windows 3.1's are */
    HGLOBAL target;                     /* StdTargetDevice, for GetData */
    WORD target_gen;
    char FARP *name;
} DOC;

/* a DDE message that arrived while the server was blocked */
typedef struct QMSG {
    struct QMSG FARP *next;
    HWND hwnd;
    UINT msg;
    WPARAM wp;
    LPARAM lp;
} QMSG;

typedef struct SRV {
    WORD magic;
    struct SRV FARP *next;
    LPOLESERVER lpsrv;
    HINSTANCE inst;
    OLE_SERVER_USE use;
    HWND hwnd;
    CONV FARP *convs;
    DOC FARP *docs;
    QMSG FARP *queue;
    BOOL blocked, revoking, released, execute;
    BOOL busy;                          /* inside OleRevokeServer */
    ATOM atom;                          /* the class name */
    char FARP *cls;
} SRV;

#define SRV_MAGIC 0x5653                /* "SV" */
#define DOC_MAGIC 0x4F44                /* "DO" */

/* util.c */
extern HINSTANCE svr_inst;
extern SRV FARP *svr_list;
extern WORD cf_native, cf_ownerlink, cf_objectlink, cf_binary;
void FARP *mem_alloc(DWORD n);
void mem_free(void FARP *p);
char FARP *str_dup(const char FARP *s);
char FARP *tmp_dup(const char FARP *s);
void tmp_free(char FARP *p);
int str_len(const char FARP *s);
void str_cpy(char FARP *d, const char FARP *s);
BOOL str_ieq(const char FARP *a, const char FARP *b);
CONV FARP *conv_find(CONV FARP *l, HWND client);
CONV FARP *conv_add(CONV FARP **l, HWND client);
void conv_remove(CONV FARP **l, HWND client);
void huge_copy(void FAR *dst, const void FAR *src, DWORD n);
HGLOBAL data_block(WORD flags, WORD cf, HGLOBAL h);
HGLOBAL data_value(HGLOBAL block, WORD cf);
void ack(HWND to, HWND from, WORD status, WORD hi);

/* svr.c */
SRV FARP *srv_from(LHSERVER lh);
void srv_check_release(SRV FARP *s);
void srv_try_finish(SRV FARP *s);
BOOL srv_queue(SRV FARP *s, HWND w, UINT msg, WPARAM wp, LPARAM lp);
int cmd_args(char FARP *t, const char *kinds, char FARP *FARP *arg, char FARP *FARP *rest);
BOOL cmd_end(const char FARP *rest);
BOOL arg_bool(const char FARP *a);
WORD arg_num(const char FARP *a);
BOOL one_command(const char FARP *t);
void cmd_name(const char FARP *t, char FARP *out, int max);
LRESULT srv_dde(SRV FARP *s, HWND w, UINT msg, WPARAM wp, LPARAM lp);

/* doc.c */
DOC FARP *doc_new(SRV FARP *s, const char FARP *name, LPOLESERVERDOC lpdoc);
DOC FARP *doc_find(SRV FARP *s, const char FARP *name);
DOC FARP *doc_from(LHSERVERDOC lh);
OLESTATUS doc_revoke(DOC FARP *d);
void doc_destroy(DOC FARP *d);
void doc_try_finish(DOC FARP *d);
ITEM FARP *item_get(DOC FARP *d, const char FARP *name, HWND client, OLESTATUS FARP *st);
OLESTATUS item_show(ITEM FARP *it, BOOL focus);
void item_notify(ITEM FARP *it, int kind);
LRESULT doc_dde(DOC FARP *d, HWND w, UINT msg, WPARAM wp, LPARAM lp);
void conv_post_terms(HWND from, CONV FARP *l);

LRESULT FAR PASCAL __loadds SRVRWNDPROC(HWND, UINT, WPARAM, LPARAM);
LRESULT FAR PASCAL __loadds DOCWNDPROC(HWND, UINT, WPARAM, LPARAM);
LRESULT FAR PASCAL __loadds ITEMWNDPROC(HWND, UINT, WPARAM, LPARAM);
int FAR PASCAL __loadds ITEMCALLBACK(LPOLECLIENT, OLE_NOTIFICATION, LPOLEOBJECT);

/* a private message: post the TERMINATEs a revoke decided on */
#define WM_SVR_TERMINATE (WM_USER + 0x10)

#endif
