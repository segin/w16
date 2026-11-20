/*
 * ddeml.h - w16's DDEML.DLL, the DDE Management Library (internal
 * declarations; ARCHITECTURE.md, chapter "DDEML").
 *
 * Sources: Microsoft Windows Programmer's Reference, Volume 1 (Overview),
 * ch. 5; Volume 2 (the Dde* functions); Volume 3 (the DDE transactions
 * XTYP_*, the DDEML structures). The rest is black-box observation of
 * Windows 3.1's DDEML with the probe DM.EXE (tests/ddeml/dm.c); nothing
 * of Microsoft's DDEML was read.
 *
 * Every record lives in DDEML's local heap and is reached through linked
 * lists (no fixed tables); the heap is one 64K data segment, the bound of
 * a library's near heap.
 */
#ifndef W16_DDEML_H
#define W16_DDEML_H

#include <windows.h>
#include <dde.h>
#include <ddeml.h>

#define DMLAPI __loadds FAR PASCAL      /* exported by ddeml.lnk */

/* the classes and their window extra bytes (observed sizes) */
#define CLS_FRAME   "DMGFrame"          /* 4 bytes: LOWORD(id), 0 */
#define CLS_DMG     "DMGClass"          /* 6 bytes: LOWORD(id), 0, 0 */
#define CLS_CLIENT  "DMGClientClass"    /* 6 bytes: conv (near, DS), seq */
#define CLS_SERVER  "DMGServerClass"    /* 6 bytes: same */
#define CLS_HOLD    "DMGHoldingClass"   /* 8 bytes: 0, 0, seq, 0 */
#define CLS_MON     "DMGMonitorClass"   /* 4 bytes: a monitor's second window */

/* private messages between DDEML's own windows */
#define WM_DMG_CALLBACK  (WM_USER + 0x60)  /* DMGClass: lParam -> CBARGS */
#define WM_DMG_MONITOR   (WM_USER + 0x61)  /* monitor window: lParam -> event */
#define WM_DMG_RUN       (WM_USER + 0x62)  /* client: run the held advise data */
#define WM_DMG_NOP       (WM_USER + 0x63)  /* DMGClass: nothing (a queue place) */

typedef struct tagINST INST;
typedef struct tagCONV CONV;
typedef struct tagXACT XACT;
typedef struct tagLINK LINK;
typedef struct tagQMSG QMSG;
typedef struct tagDATA DATA;
typedef struct tagNAME NAME;
typedef struct tagCLIST CLIST;

struct tagNAME {                        /* a registered service name */
    NAME NEAR *next;
    ATOM atom;
};


/* DATA.flags */
#define DF_APPOWNED  0x0001             /* HDATA_APPOWNED */
#define DF_NOHEADER  0x0002             /* EXECUTE block: no flags/format */
#define DF_USED      0x0004             /* given to DDEML: read-only */
#define DF_TEMP      0x0008             /* lent to a callback */
#define DF_LOCKED    0x0010             /* DdeAccessData */

struct tagDATA {
    DATA NEAR *next;
    INST NEAR *inst;
    HGLOBAL h;                          /* the DDE block */
    WORD flags;
    ATOM item;
    WORD fmt;
    DWORD size;                         /* if not 0: the size it shows */
};

struct tagLINK {                        /* an advise loop */
    LINK NEAR *next;
    ATOM item;
    WORD fmt;
    WORD opts;                          /* client: ADVISE options */
    BOOL pending;                       /* server: DATA with fAckReq not yet acked */
    BOOL late;                          /* server: DdePostAdvise while pending */
};

struct tagQMSG {                        /* a message held while blocked */
    QMSG NEAR *next;
    UINT msg;
    WPARAM wp;
    LPARAM lp;
};

/* XACT.state */
#define XS_WAIT    0
#define XS_DONE    1

struct tagXACT {                        /* a client transaction */
    XACT NEAR *next;
    CONV NEAR *conv;
    UINT type;
    UINT fmt;
    ATOM item;
    WORD id;                            /* per-conversation counter */
    BOOL async;
    BOOL abandoned;
    WORD state;
    WORD status;                        /* the ACK's status word */
    WORD err;
    HDDEDATA result;
    HGLOBAL sent;                       /* the block posted (EXECUTE: kept) */
    DWORD user;
};

/* CONV.flags are the ST_* status bits; CONV.mode */
#define CM_SINGLE    0                  /* DdeConnect */
#define CM_LIST      1                  /* DdeConnectList */

struct tagCONV {
    CONV NEAR *next;                    /* the instance's list, oldest first */
    INST NEAR *inst;
    HWND hwnd;
    HWND partner;
    WORD seq;
    WORD status;                        /* ST_* */
    WORD convst;                        /* XST_* */
    WORD mode;
    ATOM svcpartner, svcreq, topic;
    ATOM svcref;                        /* server: the service, held once more */
    ATOM svcask;                        /* client: the service asked for (svcreq once connected) */
    CONVCONTEXT cc;
    DWORD user;
    CLIST NEAR *list;
    WORD lasterr;
    WORD xcount;                        /* async transaction counter */
    XACT NEAR *xacts;                   /* outstanding, oldest first */
    LINK NEAR *links;                   /* newest first */
    QMSG NEAR *queue;                   /* held while blocked, oldest first */
    UINT ltype, lfmt;                   /* CONVINFO's transaction fields */
    ATOM litem;
    BOOL dead;                          /* TERMINATE sent or received */
    BOOL incb;                          /* a callback of this conv is running */
    BOOL initiating;                    /* its INITIATE is being sent */
    BOOL runposted;                     /* WM_DMG_RUN is on its way */
    HWND enumwin;                       /* the window its INITIATE went to last */
};

/* a conversation list is a DMGHoldingClass window (child of the
   client's DMGClass) whose children are the conversations, in their
   z-order; HCONVLIST = MAKELONG(window, number) */
struct tagCLIST {
    CLIST NEAR *next;
    INST NEAR *inst;
    HWND hwnd;
    WORD seq;
    BOOL ended;                         /* DdeDisconnectList ran (word 6: 2000) */
};

/* INST.flags */
#define IF_MONITOR    0x0001
#define IF_CLIENTONLY 0x0002
#define IF_FILTER     0x0004            /* service-name filter on */
#define IF_DYING      0x0008

struct tagINST {
    INST NEAR *next;
    WORD seq;                           /* HIWORD(idInst) */
    HTASK task;
    PFNCALLBACK cb;
    DWORD afCmd;
    WORD flags;
    WORD lasterr;
    HWND frame, dmg, hold, mon;
    HGLOBAL monblk;                     /* a monitor's event block */
    NAME NEAR *names;
    ATOM NEAR *strs;                    /* one entry per create/keep */
    WORD nstrs, maxstrs;
    DATA NEAR *datas;
    CONV NEAR *convs;
    CLIST NEAR *lists;
    WORD blockall;                      /* EC_DISABLE with hConv 0 */
};

/* shared state */
extern HINSTANCE ddeml_inst;
extern INST NEAR *ddeml_insts;
extern WORD ddeml_seq;
extern WORD ddeml_lasterr;              /* for an invalid instance */
extern WORD ddeml_kbcp;                 /* GetKBCodePage */
extern int ddeml_syncs;                 /* tasks in a synchronous transaction */

/* inst.c */
INST NEAR *inst_find(DWORD id);
INST NEAR *inst_of_task(void);
DWORD inst_id(INST NEAR *in);
void seterr(INST NEAR *in, WORD e);
HDDEDATA callback(INST NEAR *in, UINT type, UINT fmt, CONV NEAR *c, HSZ h1, HSZ h2,
                  HDDEDATA d, DWORD d1, DWORD d2);
HDDEDATA callback_hc(INST NEAR *in, UINT type, UINT fmt, CONV NEAR *c, HCONV hc, HSZ h1, HSZ h2,
                     HDDEDATA d, DWORD d1, DWORD d2);
void seterr_quiet(INST NEAR *in, WORD e);
BOOL filtered(INST NEAR *in, UINT type);
LRESULT FAR PASCAL DmgWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT FAR PASCAL SubframeWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
void NEAR *nalloc(WORD n);
void nfree(void *p);
void bzero(void FAR *p, WORD n);
void bcopy(void FAR *d, const void FAR *s, WORD n);
void hcopy(void FAR *d, const void FAR *s, DWORD n);
void FAR *hadd(void FAR *p, DWORD off);
extern WORD __cdecl ddeml_ahincr;
void modal(BOOL *until);

/* hsz.c */
void str_freeall(INST NEAR *in);
ATOM hsz_inc(ATOM a);
ATOM hsz_inc_quiet(ATOM a);
void hsz_dec(ATOM a);
HSZ inst_hsz(INST NEAR *in, ATOM a);

/* data.c */
DATA NEAR *data_find(HDDEDATA h);
HDDEDATA data_handle(DATA NEAR *d);
DATA NEAR *data_new(INST NEAR *in, HGLOBAL h, WORD flags, ATOM item, WORD fmt);
void data_free(DATA NEAR *d, BOOL block);
void data_freeall(INST NEAR *in);
HGLOBAL data_take(DATA NEAR *d, WORD flags);
HDDEDATA data_wrap(INST NEAR *in, HGLOBAL h, BOOL header, ATOM item);
HGLOBAL data_block(INST NEAR *in, const void FAR *p, DWORD cb, BOOL header, WORD fmt);

/* conv.c */
CONV NEAR *conv_find(HCONV h);
HCONV conv_handle(CONV NEAR *c);
HWND newwin(INST NEAR *in, LPCSTR cls, HWND parent, DWORD style, WORD seq);
void conv_free(CONV NEAR *c);
void conv_terminate(CONV NEAR *c, BOOL wait);
void conv_terminate_all(INST NEAR *in);
BOOL conv_initiate(INST NEAR *in, HWND client, ATOM app, ATOM topic, HWND from);
BOOL local_window(HWND h, LPCSTR cls);
LRESULT FAR PASCAL ClientWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT FAR PASCAL ServerWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT FAR PASCAL ConvListWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
void conv_unblock(CONV NEAR *c, UINT cmd);
BOOL conv_waiting(CONV NEAR *c);

/* mon.c */
/* MONHSZSTRUCT.fsAction values 3.1 uses for its own atom traffic
   (seen with tests/ddeml/scripts/xmon.dm): an atom added for a string,
   one more reference, one reference less */
#define MH_ADDATOM  5
#define MH_INCATOM  6
#define MH_DECATOM  7
void mon_hsz(INST NEAR *in, HSZ h, WORD action);
void mon_msg(BOOL post, HWND to, UINT msg, WPARAM wp, LPARAM lp);
void mon_cb(INST NEAR *in, UINT type, UINT fmt, HCONV c, HSZ h1, HSZ h2, HDDEDATA d,
            DWORD d1, DWORD d2, DWORD ret);
void mon_conv(CONV NEAR *c, BOOL connect);
void mon_link(CONV NEAR *c, LINK NEAR *l, BOOL est);
HCONV partner_handle(CONV NEAR *c, BOOL inlink);
BOOL is_dmgclass(HWND h);
BOOL conv_listing(HWND client);
void mon_err(INST NEAR *in, WORD e);
void mon_update(void);
void mon_init(INST NEAR *in);
LRESULT FAR PASCAL MonitorWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT FAR PASCAL DdeSendHookProc(int code, WPARAM wp, LPARAM lp);
LRESULT FAR PASCAL DdePostHookProc(int code, WPARAM wp, LPARAM lp);

#endif
