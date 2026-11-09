/*
 * cardfile.h - w16 Cardfile.
 *
 * A clean-room Cardfile, made to match Windows 3.1's in look, behaviour
 * and files (.CRD, docs/crd-format.md). Every visible detail comes from
 * black-box observation of the original (tests/cfrun.sh, the CF probes
 * in tests/scripts/cfprobe/, ARCHITECTURE.md 15) and its resources read
 * as data (tools/nerc.py). Built medium model without a C runtime, in two
 * builds from this source: Windows 3.x and, with W16_WIN2, Windows 2.x
 * (no COMMDLG, OLE, WinHelp: compat2.c).
 */
#ifndef CARDFILE_H
#define CARDFILE_H

#include <windows.h>
#ifndef W16_WIN2
#include <shellapi.h>
#include <commdlg.h>
#include <ole.h>
/* SHELL.22, documented in the 3.1 SDK; not in every shellapi.h */
int WINAPI ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
#endif
#include "compat2.h"

/* ---- resources: the original's IDs where they are visible (menu
   commands, control IDs, string numbers), our own otherwise ---- */
#define IDM_MAIN            1
#define IDA_MAIN            1
#define IDI_CARDFILE        1

#define IDM_ABOUT           100
#define IDM_NEW             101
#define IDM_OPEN            102
#define IDM_SAVE            103
#define IDM_SAVEAS          104
#define IDM_PRINT           105
#define IDM_PRINTALL        106
#define IDM_MERGE           107
#define IDM_EXIT            108
#define IDM_PAGESETUP       109
#define IDM_PRINTSETUP      110
#define IDM_INDEX           111
#define IDM_RESTORE         112
#define IDM_CUT             113
#define IDM_COPY            114
#define IDM_PASTE           115
#define IDM_TEXT            116
#define IDM_PICTURE         117
#define IDM_UNDO            118
#define IDM_PASTELINK       119
#define IDM_ADD             121
#define IDM_DELETE          122
#define IDM_DUPLICATE       123
#define IDM_AUTODIAL        124
#define IDM_GOTO            131
#define IDM_FIND            132
#define IDM_FINDNEXT        133
#define IDM_PASTESPECIAL    134
#define IDM_INSERTOBJECT    135
#define IDM_LINKS           136
#define IDM_OBJECT          137
#define IDM_CARDVIEW        141
#define IDM_LISTVIEW        142
#define IDM_HELPCONTENTS    301
#define IDM_HELPSEARCH      302
#define IDM_HELPHELP        303
/* the verbs of the Object popup (Edit > Object) */
#define IDM_VERB0           1000

/* positions of the popups in the menu bar */
#define MENU_FILE           0
#define MENU_EDIT           1
#define MENU_VIEW           2
#define MENU_CARD           3
#define MENU_SEARCH         4
#define EDIT_OBJECT_POS     14          /* "&Object" in the Edit popup */

/* dialogs (the original's template numbers) */
#define IDD_INDEX           103
#define IDD_ADD             104
#define IDD_GOTO            105
#define IDD_PRINTING        108
#define IDD_AUTODIAL        109
#define IDD_PAGESETUP       111
#define IDD_LINKS           114
#define IDD_INVALIDLINK     115
#define IDD_PASTESPECIAL    116
#define IDD_INSERTOBJECT    117
#define IDD_FIND2           118         /* the 2.x build's Find */
#define IDD_OPEN2           120         /* the 2.x build's file dialogs */
#define IDD_SAVE2           121
#define IDD_ABOUT2          122

#define IDC_EDIT            301         /* Index, Add, Go To */
#define IDC_PRINTFILE       110
#define IDC_DIALNUM         306
#define IDC_SETUP           307
#define IDC_SEP1            308
#define IDC_SEP2            309
#define IDC_HEADER          310
#define IDC_FOOTER          311
#define IDC_LEFT            312
#define IDC_RIGHT           313
#define IDC_TOP             314
#define IDC_BOTTOM          315
#define IDC_USEPREFIX       316
#define IDC_PREFIX          317
#define IDC_TONE            206
#define IDC_PULSE           207
#define IDC_BAUD110         211         /* 211..217: 110..19200 */
#define IDC_COM1            401         /* 401..404 */
/* Links dialog */
#define IDC_LINKLIST        257
#define IDC_LINKAUTO        258
#define IDC_LINKMANUAL      259
#define IDC_LINKSRC         260
#define IDC_LINKTYPE        261
#define IDC_CHANGELINK      262
#define IDC_ACTIVATE        150
#define IDC_UPDATENOW       151
#define IDC_CANCELLINK      152
#define IDC_EDITLINK        153
#define IDC_FIXLINK         263
/* Paste Special and Insert Object */
#define IDC_PSPASTE         400
#define IDC_PSLINK          401
#define IDC_PSSRCLABEL      402
#define IDC_PSSOURCE        403
#define IDC_PSITEM          404
#define IDC_TYPELIST        405
/* 2.x file dialogs */
#define IDC_FNAME           11
#define IDC_FLIST           12
#define IDC_FPATH           10
/* 2.x Find */
#define IDC_FINDTEXT        1152
#define IDC_MATCHCASE       1041
#define IDC_FINDUP          1056
#define IDC_FINDDOWN        1057

/* child windows of the frame (observed IDs) */
#define ID_EDIT             200
#define ID_VIEWNAME         201
#define ID_COUNT            202
#define ID_SCROLL           203
#define ID_LIST             204
#define ID_CARD             205

/* strings (the original's numbers) */
#define IDS_DELETE          0
#define IDS_SAVECHANGES     2
#define IDS_CARDS           3
#define IDS_CARD            4
#define IDS_ONECARD         5
#define IDS_UNTITLED        6
#define IDS_CARDDATA        7
#define IDS_CARDFILE        8
#define IDS_APPNAME         9
#define IDS_PICKUP          10
#define IDS_TITLE           11
#define IDS_BADMARGINS      12
#define IDS_CARDVIEW        15
#define IDS_LISTVIEW        16
#define IDS_HELPFILE        18
#define IDS_CANTDIAL        19
#define IDS_NOMEMPRINTPIC   20
#define IDS_NOMEM           21
#define IDS_BADNAME         23
#define IDS_NOTEXTCLIP      24
#define IDS_DISKFULL        26
#define IDS_NOTEMP          27
#define IDS_NOMEMCARD       28
#define IDS_NOMEMRUN        32
#define IDS_BADFORMAT       33
#define IDS_NOTFOUND        34
#define IDS_NOMEMREAD       35
#define IDS_CANTPRINT       36
#define IDS_NOPICCLIP       37
#define IDS_CANTDIAL2       38
#define IDS_CANTSAVE        40
#define IDS_NOMEMPRINT      41
#define IDS_NODISKPRINT     42
#define IDS_PLAIN           43
#define IDS_EXT             44
#define IDS_DEFHEADER       45
#define IDS_DEFFOOTER       46
#define IDS_DEFLEFT         47
#define IDS_DEFRIGHT        48
#define IDS_DEFTOP          49
#define IDS_DEFBOTTOM       50
#define IDS_HFCODES         51
#define IDS_REOPEN          53
#define IDS_CANTSAVE2       54
#define IDS_CANTSAVE3       55
#define IDS_CANTOPEN        56
#define IDS_CANTEXIT        57
#define IDS_READONLY        59
#define IDS_READONLY2       60
#define IDS_TEXTTOOLONG     61
#define IDS_CANTCREATE      62
#define IDS_NOMEMCOPYOBJ    256
#define IDS_NOSERVER        260
#define IDS_CANTVIEW        261
#define IDS_LINKBROKEN      262
#define IDS_NOOLE           263
#define IDS_SERVERBUSY      265
#define IDS_OLEFAILED       266
#define IDS_LINKNOUPDATE    267
#define IDS_NOCLIP          271
#define IDS_NOSERVER2       272
#define IDS_PRINTDRV        275
#define IDS_NOTOLE          514
#define IDS_NOUNDO          515
#define IDS_BUSYNAMED       517
#define IDS_OPENOBJPASTE    518
#define IDS_OPENOBJDEL      520
#define IDS_SAVE30          521
#define IDS_FILTERCRD       768
#define IDS_OPENTITLE       769
#define IDS_SAVEASTITLE     770
#define IDS_FILTER30        771
#define IDS_MERGETITLE      772
#define IDS_CHANGELINK      773
#define IDS_FILTERALL       774
#define IDS_VALIDATE        775
#define IDS_OBJECTN         776
#define IDS_UPDATEEMB       777
#define IDS_INSERTBUSY      778
#define IDS_OBJECTMENU      779
#define IDS_PICTURE         780
#define IDS_BITMAP          781
#define IDS_HASLINKS        782
#define IDS_OBJECT          783
#define IDS_VERBFMT1        784
#define IDS_VERBFMT2        785
#define IDS_EDITVERB        786
#define IDS_NOHELP          900         /* the 2.x build's own */

/* ---- the card file in memory ----
   Each card has a header in one growable array (cards.c): its index line
   and where its body is. A body (text and picture) lives in a file in the
   card file's own format: the open .CRD file, or the scratch file that
   holds cards changed since. Only the current card's body is in memory
   (the edit control's text and g_pic). */

/* index lines: 40 bytes in the file, NUL-terminated (a format bound);
   the original's Index and Add boxes take 39 characters (CF4) */
#define INDEX_BYTES         40
#define INDEX_MAX           39

/* where a card's body is */
#define WHERE_BLANK         0           /* no text, no picture */
#define WHERE_FILE          1           /* in the .CRD file at pos */
#define WHERE_TEMP          2           /* in the scratch file at pos */

/* the body formats: the file's signature */
#define FMT_MGC             0           /* Cardfile 3.0 and before */
#define FMT_RRG             1           /* 3.1: with OLE objects */

typedef struct {                        /* 64 bytes: 1024 per segment */
    DWORD pos;
    BYTE where;
    BYTE fmt;
    WORD spare;
    char index[INDEX_BYTES + 1];
    DWORD newpos;                       /* while saving */
    BYTE pad[64 - 12 - INDEX_BYTES - 1];
} CARDHDR;
typedef CARDHDR FAR *LPCARD;

/* the current card's picture */
#define PIC_NONE            0
#define PIC_BITMAP          1           /* a bitmap (3.0 files; the 2.x build) */
#define PIC_OBJECT          2           /* an OLE object */

typedef struct {
    int kind;
    int x, y;                           /* top left on the card body, pixels */
    int cx, cy;                         /* size, pixels */
    HBITMAP bmp;                        /* PIC_BITMAP */
#ifndef W16_WIN2
    LPOLEOBJECT obj;                    /* PIC_OBJECT */
#endif
    DWORD id;                           /* the object's number */
    WORD otype;                         /* 0 embedded, 1 linked, 2 static */
    int ccx, ccy;                       /* the character size it was made at */
} PICTURE;

/* ---- globals (main.c) ---- */
extern HINSTANCE g_hinst;
extern HWND g_frame, g_card, g_edit, g_list, g_scroll, g_viewname, g_count;
extern HFONT g_font;                    /* the cards' font: SYSTEM_FIXED_FONT */
extern int g_cx, g_cy;                  /* its character cell */
extern BOOL g_listview;                 /* View > List */
extern BOOL g_picmode;                  /* Edit > Picture */
extern char g_path[];                   /* the open file, "" when untitled */
extern BOOL g_dirty;                    /* changed since opened or saved */
extern BOOL g_carddirty;                /* the current card changed */
extern PICTURE g_pic;
extern int g_fmt;                       /* the open file's format */
extern DWORD g_lastobj;                 /* RRG: the last object number */

/* main.c */
void set_title(void);
void update_status(void);
void layout(void);
void show_view(BOOL list);
void card_changed(void);               /* the current card is another one */
int  msg_box(UINT ids, LPCSTR arg, UINT flags);
LPSTR load_str(UINT ids);              /* a static copy (one at a time) */
BOOL query_save(void);
void do_command(WORD id);
void enable_menus(void);

/* cards.c */
extern WORD g_ncards;                  /* number of cards (the format's WORD) */
extern WORD g_cur;                     /* the front card */
BOOL cards_init(void);
void cards_reset(void);
LPCARD card_hdr(WORD i);
WORD card_insert(LPCSTR index);        /* sorted; returns its position */
void card_remove(WORD i);
WORD card_resort(WORD i);              /* after its index changed */
int  index_cmp(LPCSTR a, LPCSTR b);
BOOL body_load(WORD i);                /* into the edit control and g_pic */
BOOL body_store(void);                 /* the current card, if it changed */
void pic_free(PICTURE *p);
HFILE body_file(LPCARD c);             /* the open file holding c's body */
void temp_close(void);
BOOL temp_reset(void);
HFILE g_srcfile_handle(void);
HFILE temp_file(void);                 /* opened if need be */
void temp_grew(void);                  /* after appending to it */

/* crd.c */
BOOL crd_open(LPCSTR path, BOOL merge);
BOOL crd_save(LPCSTR path, int fmt);
BOOL crd_has_objects(void);
BOOL body_read(HFILE f, int fmt, DWORD pos, HGLOBAL FAR *text, WORD FAR *cb, PICTURE *pic);
DWORD body_write(HFILE f, int fmt, LPCSTR text, WORD cb, PICTURE *pic);
BOOL body_copy(HFILE from, int ffmt, DWORD pos, HFILE to, int tfmt, DWORD FAR *newpos);
extern char g_srcpath[];
BOOL merge_body(HFILE f, LPCARD c);

/* cardwnd.c */
BOOL cardwnd_register(HINSTANCE);
void card_paint(HDC dc);
void card_place_edit(void);
void card_front_rect(RECT *r);         /* the front card, in g_card's coordinates */
int  card_visible(void);               /* how many cards the stack shows */
void pic_draw(HDC dc, int x, int y);
LRESULT CALLBACK __export EditWndProc(HWND, UINT, WPARAM, LPARAM);
extern FARPROC g_oldedit;
void go_card(WORD i);
void go_next(int delta);
void go_letter(char c);

/* dialogs.c */
BOOL dlg_index(void);
BOOL dlg_add(void);
void dlg_goto(void);
void dlg_find(void);
void find_next(void);
void dlg_autodial(void);
void dlg_pagesetup(void);
BOOL file_dialog(int which, LPSTR path, int *fmt);   /* FD_OPEN, ... */
#define FD_OPEN             0
#define FD_SAVEAS           1
#define FD_MERGE            2
void about_box(void);
BOOL find_message(UINT msg, LPARAM lp);
extern UINT g_findmsg;
extern HWND g_finddlg;

/* edit.c: clipboard, undo, restore, pictures */
void edit_cut(void);
void edit_copy(void);
void edit_paste(void);
void edit_undo(void);
void edit_restore(void);
BOOL pic_mouse(UINT msg, int x, int y);
void pic_key(WPARAM vk);
void undo_forget(void);

/* print.c */
void print_cards(BOOL all);
void print_setup(void);
void page_defaults(void);
extern char g_header[], g_footer[];
extern char g_margin[4][16];

/* ole.c */
BOOL ole_init(void);
void ole_done(void);
void ole_menu(HMENU edit);
void ole_command(WORD id);
BOOL ole_paste(BOOL link);
void ole_insert(void);
void ole_links(void);
void ole_paste_special(void);
BOOL ole_can_paste(void);
BOOL ole_can_link(void);
BOOL ole_load(HFILE f, PICTURE *p, DWORD FAR *pos);
BOOL ole_save(HFILE f, PICTURE *p);
DWORD ole_skip(HFILE f, DWORD pos);    /* the end of a stored object */
void ole_draw(PICTURE *p, HDC dc, RECT *r);
void ole_free(PICTURE *p);
BOOL ole_clone(PICTURE *from, PICTURE *to);
void ole_activate(PICTURE *p, int verb);
void ole_copy(PICTURE *p);
void ole_rename(LPCSTR path);
void ole_saved(void);

/* help.c */
void help_contents(void);
void help_search(void);
void help_on_help(void);
void help_quit(void);
void help_init(void);
void help_done(void);
void help_menu_select(WPARAM wp, LPARAM lp);

/* util.c */
int  s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int n);   /* n: buffer size */
void s_cat(char FAR *d, const char FAR *s, int n);
void mem_cpy(void FAR *d, const void FAR *s, WORD n);
void mem_set(void FAR *d, int c, WORD n);
int  s_cmpn(const char FAR *a, const char FAR *b, int n);
LPSTR s_utoa(LPSTR d, DWORD v);
const char FAR *base_name(const char FAR *path);
BOOL f_read(HFILE f, void FAR *buf, WORD n);
BOOL f_write(HFILE f, const void FAR *buf, WORD n);
BOOL f_copy(HFILE from, HFILE to, DWORD n);
void fmt_msg(LPSTR out, int size, LPCSTR fmt, LPCSTR arg);

#ifdef W16_WIN2
#define CF_RADIO            BS_RADIOBUTTON
#define CF_CHECK            BS_CHECKBOX
#else
#define CF_RADIO            BS_AUTORADIOBUTTON
#define CF_CHECK            BS_AUTOCHECKBOX
#endif

#endif
