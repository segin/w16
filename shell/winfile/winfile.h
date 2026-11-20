/*
 * winfile.h - w16 File Manager.
 *
 * A clean-room File Manager for Windows 3.1. Everything it shows and does
 * comes from black-box observation of the original on the reference
 * install (SNAP.EXE scripts, tests/wfobs.sh, the probes in
 * tests/scripts/winfile/obs/: window classes, styles and rectangles,
 * menus with their IDs, dialogs' controls, screenshots, the WINFILE.INI
 * it writes) and from the Windows 3.1 SDK (the File Manager extension
 * interface, Programmer's Reference vol. 1 ch. "File Manager
 * extensions", facts only). No code, resource or data of the original
 * was read. Medium model, no C runtime, Windows 3.1 only.
 */
#ifndef WINFILE_H
#define WINFILE_H

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
/* SHELL.22, documented in the 3.1 SDK; not in every shellapi.h */
int WINAPI ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

/* ---- resources ---- */
#define IDM_MAIN            500         /* the menu */
#define IDA_MAIN            1
#define IDI_WINFILE         200         /* the program */
#define IDI_TREE            201         /* a minimised window: tree only, */
#define IDI_DIR             202         /* file list only, */
#define IDI_TREEDIR         204         /* both (WF04: 3.1's IDs) */
#define IDB_STRIP           100         /* the small pictures (tools/mkwfbmp.py) */

/* ---- menu commands: the original's IDs (WF1 dumpmenu) ---- */
#define IDM_OPEN            101
#define IDM_PRINT           102
#define IDM_ASSOCIATE       103
#define IDM_SEARCH          104
#define IDM_RUN             105
#define IDM_MOVE            106
#define IDM_COPY            107
#define IDM_DELETE          108
#define IDM_RENAME          109
#define IDM_ATTRIBS         110
#define IDM_MAKEDIR         111
#define IDM_EXIT            115
#define IDM_SELECT          116
#define IDM_DISKCOPY        201
#define IDM_LABEL           202
#define IDM_FORMAT          203
#define IDM_SYSDISK         204
#define IDM_CONNECT         205
#define IDM_DISCONNECT      206
#define IDM_DRIVESMORE      251
#define IDM_EXPONE          301
#define IDM_EXPSUB          302
#define IDM_EXPALL          303
#define IDM_COLLAPSE        304
#define IDM_VNAME           401
#define IDM_VDETAILS        402
#define IDM_VOTHER          403
#define IDM_BYNAME          404
#define IDM_BYTYPE          405
#define IDM_BYSIZE          406
#define IDM_BYDATE          407
#define IDM_VINCLUDE        409
#define IDM_TREEONLY        411
#define IDM_DIRONLY         412
#define IDM_BOTH            413
#define IDM_SPLIT           414
#define IDM_CONFIRM         501
#define IDM_STATUSBAR       503
#define IDM_MINONRUN        504
#define IDM_EXPANDTREE      505
#define IDM_FONT            510
#define IDM_SAVESETTINGS    511
#define IDM_CASCADE         1001
#define IDM_TILE            1002
#define IDM_REFRESH         1003
#define IDM_ARRANGE         1004
#define IDM_NEWWINDOW       1005
#define IDM_CHILDSTART      1006        /* the MDI window list */
#define IDM_HELPINDEX       1101
#define IDM_HELPHELP        1102
#define IDM_ABOUT           1103
#define IDM_HELPSEARCH      30

/* the menu bar's popups, left to right */
#define MENU_FILE           0
#define MENU_DISK           1
#define MENU_TREE           2
#define MENU_VIEW           3
#define MENU_OPTIONS        4
#define MENU_WINDOW         5
#define MENU_HELP           6

/* ---- strings ---- */
#define IDS_APPNAME         1           /* "File Manager" */
#define IDS_HELPFILE        2           /* "WINFILE.HLP" */
#define IDS_DRIVEFREE       3           /* the status bar's drive part */
#define IDS_TOTAL           4           /* "Total %d file(s) (%s bytes)" */
#define IDS_SELECTED        5           /* "Selected %d file(s) (%s bytes)" */
#define IDS_DRIVELABEL      6           /* "%c: [%s]" */
#define IDS_DRIVENOLABEL    7           /* "%c:" */
#define IDS_NOTEXIST        8           /* "Drive %c does not exist." ... */
#define IDS_READING         9           /* "Reading Directory ..." */
#define IDS_NOFILES         10          /* "No files found." */
#define IDS_ABOUTOTHER      11          /* ShellAbout's second line */
#define IDS_AM              12
#define IDS_PM              13

/* ---- WINFILE.INI ([Settings]; WF1, WF2, WF4) ---- */
#define INI_FILE            "WINFILE.INI"
#define INI_SETTINGS        "Settings"

/* the view field of dirN= (WF4): which details are shown */
#define VIEW_NAMEONLY       0x0000
#define VIEW_SIZE           0x0002
#define VIEW_DATE           0x0004
#define VIEW_TIME           0x0008
#define VIEW_FLAGS          0x0010
#define VIEW_EVERYTHING     (VIEW_SIZE | VIEW_DATE | VIEW_TIME | VIEW_FLAGS)
/* Tree > Indicate Expandable Branches: a bit of the view, so per window,
   and View > Name or All File Details clears it (WF2 A12, WF4 dir1) */
#define VIEW_PLUSES         0x0020
#define VIEW_DETAILMASK     VIEW_EVERYTHING
#define VIEW_DETAILS        VIEW_EVERYTHING

/* the sort field: the IDs of the sort commands less 203 (201 name,
   202 type, 203 size, 204 date; WF4) */
#define SORT_NAME           201
#define SORT_TYPE           202
#define SORT_SIZE           203
#define SORT_DATE           204

/* the attribute field: which files are listed (WF4: 1905 by default,
   768 with Directories and Other Files off) */
#define ATTR_READONLY       0x0001
#define ATTR_HIDDEN         0x0002
#define ATTR_SYSTEM         0x0004
#define ATTR_VOLUME         0x0008
#define ATTR_DIR            0x0010
#define ATTR_ARCHIVE        0x0020
#define ATTR_NORMAL         0x0040
#define ATTR_PROGS          0x0100
#define ATTR_DOCS           0x0200
#define ATTR_OTHER          0x0400
#define ATTR_HS             (ATTR_HIDDEN | ATTR_SYSTEM)
#define ATTR_OTHERS         (ATTR_OTHER | ATTR_NORMAL | ATTR_ARCHIVE | ATTR_READONLY)
#define ATTR_DEFAULT        (ATTR_DIR | ATTR_PROGS | ATTR_DOCS | ATTR_OTHERS)

/* the panes a directory window shows */
#define PANES_BOTH          0
#define PANES_TREE          1
#define PANES_DIR           2

/* ---- child window IDs (WF1 dump) ---- */
#define ID_DRIVES           1
#define ID_DIR              2
#define ID_TREELIST         3
#define ID_TREE             5
#define ID_DIRLIST          6

/* the pictures of the strip: index of a 16 x 16 picture */
#define BM_PROGRAM          0
#define BM_DOC              1
#define BM_FILE             2
#define BM_SYSFILE          3
#define BM_PARENT           4
#define BM_CLOSED           5
#define BM_CLOSEDPLUS       6
#define BM_OPEN             7
#define BM_OPENPLUS         8
#define BM_OPENMINUS        9
#define BM_CLOSEDMINUS      10
/* the drive pictures: index into the third row */
#define BM_DRVCDROM         0
#define BM_DRVFLOPPY        1
#define BM_DRVHARD          2
#define BM_DRVRAM           3
#define BM_DRVNET           4
#define BM_DRVNETOFF        5

#define CX_BM               16
#define CY_BM               16
#define CX_DRIVEBM          27
#define CY_DRIVEBM          14

/* a DOS path, with drive, and room for a file name and pattern: DOS's
   own bound (64-character directories, 8.3 names) */
#define PATHLEN             144

/* ---- a growing store of small records (a directory's files, a drive's
   tree): blocks of global memory, freed together ---- */
typedef struct tagARENA {
    HGLOBAL first;                      /* the newest block (a chain) */
    WORD used, size;                    /* in the newest block */
} ARENA;
LPVOID arena_alloc(ARENA FAR *a, WORD n);
void   arena_free(ARENA FAR *a);

/* ---- a directory entry (the file list's items) ---- */
typedef struct tagDENTRY {
    DWORD size;
    WORD  date, time;
    BYTE  attr;
    BYTE  pic;                          /* BM_* */
    BYTE  sel;                          /* scratch */
    char  name[13];
} DENTRY, FAR *LPDENTRY;

/* ---- a directory of the tree (the tree's items) ---- */
typedef struct tagTNODE FAR *LPTNODE;
typedef struct tagTNODE {
    LPTNODE parent;
    BYTE    level;
    BYTE    flags;
    char    name[13];
} TNODE;
#define TF_EXPANDED         0x01        /* its subdirectories are listed */
#define TF_LASTCHILD        0x02        /* the last of its parent's */
#define TF_HASKIDS          0x04        /* has subdirectories */
#define TF_SCANNED          0x08        /* TF_HASKIDS is known */

/* The status bar's left box shows the drive, the selection or nothing;
   which one changes on events, not with the focus: a selection change in
   the list shows the selection, a new directory or drive the drive,
   Directory Only the selection if the list gets the focus else nothing,
   Tree Only the drive; Tab changes nothing (WF02, WF03, WF5, WF6). */
#define ST_DRIVE            0
#define ST_SELECTED         1
#define ST_NONE             2

/* ---- a directory window (class WFS_Tree) ---- */
typedef struct tagDIRWIN {
    HWND hwnd, drives, tree, treelb, dir, dirlb;
    int  drive;                         /* 0 = A: */
    char path[PATHLEN];                 /* "C:\WINDOWS" */
    char spec[PATHLEN];                 /* "*.*" */
    WORD view, sort, attribs;
    int  split;                         /* the tree pane's right edge */
    int  panes;
    ARENA tarena, darena;               /* tree nodes, the list's entries */
    int  nfiles;                        /* in the list (not "..", not dirs) */
    DWORD bytes;
    int  colw;                          /* the list's column width */
    int  number;                        /* ":n" in the title, 0 none */
    int  stleft;                        /* what the status bar's left box
                                           says: ST_* */
    BOOL ready;
} DIRWIN, FAR *LPDIRWIN;

/* ---- globals (main.c) ---- */
extern HINSTANCE g_hinst;
extern HWND g_frame, g_mdi;
extern HFONT g_font;
extern int g_dytext, g_dxtext;          /* the font's height, average width */
extern int g_dyitem;                    /* a list item's height */
extern BOOL g_statusbar, g_minonrun, g_lower, g_saveonexit;
extern BOOL g_confdel, g_confsubdel, g_confreplace, g_confmouse, g_confdisk;
extern int g_dystatus;
extern HBITMAP g_strip;

/* main.c */
LPSTR load_str(UINT ids);
void  status_update(void);
void  status_set(LPDIRWIN d, int what);
void  status_invalidate(void);
HWND  active_dirwin(void);
LPDIRWIN dirwin_of(HWND w);
void  frame_layout(void);

/* ini.c */
void  ini_load(void);
void  ini_save(void);
void  ini_write_int(LPCSTR key, int v);
void  ini_write_str(LPCSTR key, LPCSTR v);
void  ini_restore_windows(int cmdshow);
void  ini_font(LOGFONT FAR *lf);

/* treewin.c */
BOOL  treewin_register(void);
HWND  treewin_create(LPCSTR path, LPCSTR spec, WORD view, WORD sort, WORD attribs,
                     int split, const RECT FAR *rc, int showcmd, POINT FAR *icon);
void  treewin_layout(LPDIRWIN d);
void  treewin_set_panes(LPDIRWIN d, int panes);
void  treewin_title(LPDIRWIN d);
void  treewin_change_dir(LPDIRWIN d, LPCSTR path);
void  treewin_set_drive(LPDIRWIN d, int drive);
LRESULT treewin_command(LPDIRWIN d, WPARAM id);

/* drives.c */
BOOL  drives_register(void);
void  drives_scan(void);
extern int g_ndrives;
extern BYTE g_drivelist[26];
extern BYTE g_drivepic[26];
int   drive_index(int drive);
void  drives_paint_label(LPDIRWIN d);

/* tree.c */
BOOL  tree_register(void);
void  tree_read(LPDIRWIN d);
void  tree_clone(LPDIRWIN d, LPDIRWIN src);
void  tree_select_path(LPDIRWIN d, LPCSTR path);
void  tree_expand(LPDIRWIN d, int item, int how);
void  tree_collapse(LPDIRWIN d, int item);
void  tree_path_of(LPTNODE n, LPSTR buf, int drive);
LRESULT tree_measure(LPDIRWIN d, MEASUREITEMSTRUCT FAR *mi);

/* dir.c */
BOOL  dir_register(void);
void  dir_read(LPDIRWIN d);
void  dir_set_view(LPDIRWIN d);
void  dir_columns(LPDIRWIN d);
int   dir_selected(LPDIRWIN d, DWORD FAR *bytes);
LPDENTRY dir_item(LPDIRWIN d, int i);
HGLOBAL dir_sel_save(LPDIRWIN d);
void  dir_sel_restore(LPDIRWIN d, HGLOBAL h);

/* bitmap.c */
void  bm_load(void);
void  bm_free(void);
void  bm_draw(HDC dc, int x, int y, int pic, BOOL hilite);
void  bm_draw_drive(HDC dc, int x, int y, int pic);

/* util.c */
int   s_len(LPCSTR s);
void  s_cpy(LPSTR d, LPCSTR s, int n);
void  s_cat(LPSTR d, LPCSTR s, int n);
int   s_icmp(LPCSTR a, LPCSTR b);
void  s_lower(LPSTR s);
void  s_upper(LPSTR s);
int   s_atoi(LPCSTR s);
LPSTR s_utoa(LPSTR buf, DWORD v);
LPSTR s_commas(LPSTR buf, DWORD v);
LPCSTR s_ext(LPCSTR name);
int   fmt(LPSTR out, int n, LPCSTR f, ...);
void  fmt_date(LPSTR out, WORD date);
void  fmt_time(LPSTR out, WORD time);
void  fmt_attr(LPSTR out, BYTE attr);
void  path_add(LPSTR path, LPCSTR name, int n);
void  path_strip(LPSTR path);
BOOL  is_program(LPCSTR name);
BOOL  is_document(LPCSTR name);
void  util_init(void);
void  fill_sys(HDC dc, const RECT FAR *r, int color);
extern char g_thousand;

/* DOS (shell/start.asm, dosfn.asm) */
typedef struct tagFINDBUF {             /* the DOS find buffer (DTA) */
    BYTE  reserved[21];
    BYTE  attr;
    WORD  time, date;
    DWORD size;
    char  name[13];
} FINDBUF;
extern int __cdecl dos_findfirst(const char FAR *spec, int attr, FINDBUF FAR *d);
extern int __cdecl dos_findnext(FINDBUF FAR *d);
extern int __cdecl dos_getcwd(int drive, char FAR *buf);
extern int __cdecl dos_getdrive(void);
extern int __cdecl dos_drivetype(int drive);
extern int __cdecl dos_freespace(int drive, DWORD FAR *freeb, DWORD FAR *totalb);

#endif
