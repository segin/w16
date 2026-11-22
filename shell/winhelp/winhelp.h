/*
 * winhelp.h - w16 Windows Help (WINHELP.EXE), a clean-room clone of
 * Windows 3.1's (ARCHITECTURE.md 15.10). The .HLP reader is hlp.c and
 * para.c (sources there); everything the program shows and does comes
 * from black-box observation of 3.1's WINHELP on the reference install
 * (tests/whobs.sh, probes WH1-WH*, tests/scripts/winhelp/obs/) and from
 * the SDK (WinHelp, the Help macros); the original's code and resources
 * were never looked at.
 */
#ifndef W16_WINHELP_H
#define W16_WINHELP_H

#include <windows.h>
#include "hlp.h"
#include "para.h"

/* menu commands (IDs as 3.1's, observed with SNAP's dumpmenu, WH1) */
#define IDM_OPEN        1101
#define IDM_PRINT       1103
#define IDM_PRINTSETUP  1104
#define IDM_EXIT        1105
#define IDM_ANNOTATE    1202
#define IDM_COPY        1203
#define IDM_DEFINE      1301
#define IDM_MORE        1302    /* Bookmark > More... (w16: after 9 bookmarks) */
#define IDM_BOOKMARK0   1310    /* the bookmarks on the menu, 1310-1318 */
#define IDM_ABOUT       1503
#define IDM_ONTOP       10002
#define IDM_HELPHELP    10003

/* the button bar's buttons (3.1's have ID FFFFh; w16 tells them apart
   by these, which nothing outside sees) */
#define IDB_CONTENTS    2001
#define IDB_SEARCH      2002
#define IDB_BACK        2003
#define IDB_HISTORY     2004
#define IDB_PREV        2005
#define IDB_NEXT        2006
#define IDB_USER        2100    /* CreateButton's, 2100 and up */

/* strings */
#define IDS_APPNAME     1       /* "w16 Help" */
#define IDS_HISTORY     2       /* "w16 Help History" */
#define IDS_NOFILE      3       /* Cannot find the %s file... */
#define IDS_NOTOPIC     4       /* Help topic does not exist. */
#define IDS_NOTHELP     5       /* not a Windows Help file */
#define IDS_NOMEM       6
#define IDS_FILTER      7       /* Help Files (*.HLP)|*.hlp| */
#define IDS_OPENTITLE   8
#define IDS_ABOUTOTHER  9
#define IDS_BOOKMARK    10      /* "Bookmark Define" */
#define IDS_ANNOTATE    11
#define IDS_NOKEYWORD   12
#define IDS_BADMACRO    13
#define IDS_NOPRINTER   14
#define IDS_COPYTITLE   15
#define IDS_ABOUTAPP    16      /* "Help" for ShellAbout */

/* dialogs */
#define DLG_SEARCH      100
#define DLG_DEFINE      101
#define DLG_ANNOTATE    102
#define DLG_COPY        103
#define DLG_MORE        104

/* the Search dialog's controls (IDs as 3.1's, WH2) */
#define IDC_SEARCHEDIT  0x66
#define IDC_SEARCHTEXT  0x6D
#define IDC_TOPICSTEXT  0x6E
#define IDC_TOPICS      0x6F
#define IDC_GOTO        0x73
#define IDC_SHOW        0x74
#define IDC_KEYWORDS    0x75

/* Bookmark Define, Annotate, Copy */
#define IDC_BMNAME      0x66
#define IDC_BMLIST      0x67
#define IDC_BMDELETE    0x68
#define IDC_ANNTEXT     0x66
#define IDC_ANNDELETE   0x68
#define IDC_ANNCOPY     0x73
#define IDC_ANNPASTE    0x74
#define IDC_COPYTEXT    0x66

#define IDI_WINHELP     1
#define IDCUR_HAND      1
#define IDB_LOGO        1
#define IDB_ANNOTATE    2
#define IDB_LOGO2       3

/* ---- the display list of a laid-out region ---- */

#define DI_TEXT     0
#define DI_PICT     1
#define DI_LINE     2       /* a border line: x, y, cx (width), cy (thickness) */
#define DI_ANNOT    3       /* the annotation mark (a picture) */
#define DI_EOL      4       /* the end of a line (Copy's text) */
#define DI_EOP      5       /* the end of a paragraph record */
#define HOT_ANNOTATION 0x01 /* the annotation mark's hot spot */

typedef struct {
    short x, y, cx, cy;
    BYTE kind;
    BYTE ascent;            /* text: the font's ascent (baseline y + ascent) */
    WORD font;              /* text: font index; pict: picture slot */
    WORD text;              /* text: offset in the text pool */
    WORD len;
    short hot;              /* hot spot index, -1 none */
} DITEM;

typedef struct {
    BYTE code;              /* the command: E2h-EFh jumps, C8h/CCh macros */
    DWORD hash;             /* jumps: the context's hash (or offset) */
    WORD str;               /* external file ">window" or macro, in the pool; 0xFFFF none */
} HOTSPOT;

/* a picture laid out in a view */
typedef struct {
    HBITMAP bm;
    HGLOBAL mf;             /* a metafile (pict.c) */
    short cx, cy;
    short mfx, mfy;         /* its window extent */
    WORD nhot;              /* SHG hot spots */
    WORD hotfirst;          /* their rectangles in the view's SHG list */
} VPICT;

typedef struct {
    short x, y, cx, cy;     /* relative to the picture */
    short hot;
} SHGHOT;

#define VIEW_MAXFONT 64

typedef struct VIEW {
    HWND hwnd;
    BOOL nsr;               /* the non-scrolling region */
    BOOL popup;
    HLP FAR *hlp;
    /* what is laid out: the links from first up to (not including) end */
    DWORD first, end;
    short width;            /* the width it was laid out for */
    short height;           /* its height */
    short widest;           /* the widest line (popups: the window's width) */
    short scroll;           /* vertical scroll position (pixels) */
    COLORREF bg;
    /* the display list */
    HGLOBAL hitems, hpool, hhot, hpict, hshg;
    DITEM FAR *items;
    WORD nitems, maxitems;
    char FAR *pool;
    WORD npool, maxpool;
    HOTSPOT FAR *hots;
    WORD nhots, maxhots;
    VPICT FAR *picts;
    WORD npicts, maxpicts;
    SHGHOT FAR *shg;
    WORD nshg, maxshg;
    /* fonts in use: |FONT index -> GDI font */
    WORD nfonts;
    WORD fontidx[VIEW_MAXFONT];
    HFONT fonts[VIEW_MAXFONT];
    BYTE fascent[VIEW_MAXFONT];
    BYTE fheight[VIEW_MAXFONT];
    short focus;            /* the hot spot Tab selected, -1 none */
    BOOL annotated;         /* draw the annotation mark first */
} VIEW;

/* ---- the program's state ---- */

#define MAXHIST     40
#define MAXBACK     40
#define MAXBUTTONS  16
#define PATHMAX     144

typedef struct {
    char file[PATHMAX];
    DWORD off;              /* the topic's TOPICOFFSET */
    short scroll;
    char title[64];
} PLACE;

extern HINSTANCE g_inst;
extern HWND g_frame, g_icon, g_topic, g_nsr, g_history, g_note, g_second;
extern BOOL g_api;          /* started by WinHelp ("-x"): class MS_WINHELP */
extern HLP FAR *g_hlp;
extern char g_file[PATHMAX];
extern int g_cur;           /* the topic shown, -1 none */
extern DWORD g_curoff;
extern HLPTHDR g_thdr;
extern char g_title[128];
extern VIEW g_vtopic, g_vnsr, g_vnote, g_vsecond;
extern HCURSOR g_hand;
extern HFONT g_btnfont;
extern HWND g_owner;        /* the last application to call WinHelp */
extern DWORD g_contents;    /* HELP_SETCONTENTS' topic, or the file's */
extern BOOL g_ontop;

/* main.c */
int rstr(UINT id, LPSTR buf, int cb);
int msgbox(HWND owner, UINT id, LPCSTR arg, UINT flags);
void errorbox(UINT id, LPCSTR arg);
BOOL open_file(LPCSTR name, BOOL quiet);
BOOL show_offset(DWORD off, BOOL record, short scroll);
BOOL show_topic(int topic, BOOL record);
BOOL jump_hash(HLP FAR *from, LPCSTR file, DWORD hash, LPCSTR window, BOOL popup, POINT pt);
BOOL jump_context(HLP FAR *from, LPCSTR file, LPCSTR ctx, LPCSTR window, BOOL popup);
HLP FAR *hlp_get(LPCSTR file);
void go_contents(void);
void go_back(void);
void layout_frame(void);
void update_buttons(void);
void add_button(LPCSTR id, LPCSTR text, LPCSTR macro);
void destroy_button(LPCSTR id);
void change_button(LPCSTR id, LPCSTR macro);
void enable_button(LPCSTR id, BOOL on);
void browse_buttons(void);
void show_history(void);
void history_add(void);
void set_ontop(BOOL on);
void full_path(LPCSTR name, LPSTR out);
void file_open_dialog(void);
void do_command(UINT id);
extern PLACE FAR *g_hist;
extern int g_nhist;

/* view.c */
void view_init(VIEW *v, HWND hwnd, BOOL nsr, BOOL popup);
void view_clear(VIEW *v);
void view_layout(VIEW *v, HLP FAR *h, DWORD first, DWORD end, short width);
void view_layout_dc(VIEW *v, HLP FAR *h, DWORD first, DWORD end, short width, HDC dev);
void view_free(VIEW *v);
void view_paint(VIEW *v, HDC dc, RECT *clip, short dx, short dy);
short view_hit(VIEW *v, short x, short y);
void view_activate(VIEW *v, short hot, POINT pt);
LPSTR view_text(VIEW *v, DWORD FAR *len);
void view_setscroll(VIEW *v);
LRESULT view_proc(VIEW *v, HWND w, UINT msg, WPARAM wp, LPARAM lp);
void popup_show(HLP FAR *h, int topic, POINT pt);
void popup_close(void);
LRESULT CALLBACK __export TopicProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK __export NoteProc(HWND, UINT, WPARAM, LPARAM);
void fonts_free(VIEW *v);

/* api.c */
LRESULT api_request(HWND from, HGLOBAL block);
extern UINT g_wmwinhelp;

void show_main(void);

/* search.c */
void search_dialog(LPCSTR initial);
BOOL key_jump(LPCSTR key, BOOL partial);
BOOL multikey_jump(char k, LPCSTR key);

/* macro.c */
BOOL run_macros(LPCSTR m);
BOOL menu_command(UINT cmd);

/* pict.c */
BOOL pict_load(HLP FAR *h, LPCB data, DWORD n, BOOL embedded, VPICT FAR *p, VIEW *v);
void pict_free(VPICT FAR *p);
void pict_draw(HDC dc, VPICT FAR *p, int x, int y);

/* marks.c: bookmarks, annotations, Copy, Print Topic */
void bookmark_define(void);
void bookmark_more(void);
void bookmark_go(int i);
void bookmark_menu(void);
void annotate(void);
BOOL annot_has(LPCSTR file, DWORD off);
void annot_open_mark(void);
void copy_dialog(void);
void copy_topic(void);
void print_topic(void);
void print_setup(void);

/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

/* small helpers */
void m_set(void FAR *d, int c, WORD n);
void m_cpy(void FAR *d, const void FAR *s, WORD n);

#endif
