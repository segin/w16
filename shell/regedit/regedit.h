/*
 * regedit.h - w16 Registration Info Editor (REGEDIT.EXE): IDs and shared
 * declarations (ARCHITECTURE.md 15.8). Every menu, control and command ID
 * below was observed on Windows 3.1 with SNAP (dumpmenu, dump; probes RE1-
 * RE10, tests/scripts/regedit/obs/), not read from the original's
 * resources. The New Style IDs (2000 and up) are w16's own.
 */
#ifndef REGEDIT_H
#define REGEDIT_H

/* menus (RE1, RE2) */
#define IDM_MERGE        1040   /* 0x410 File > Merge Registration File... */
#define IDM_EXIT         1041
#define IDM_ADD          1056   /* 0x420 Add File Type... / Add Key... */
#define IDM_COPY         1057   /* Copy File Type... / Copy Key... */
#define IDM_MODIFY       1058   /* Modify File Type... (basic) */
#define IDM_DELETE       1059   /* Delete File Type / Delete Key... */
#define IDM_SAVECHANGES  1536   /* 0x600 /V: Save Changes */
#define IDM_RESTORE      1537   /* Restore To Last Save */
#define IDM_SAVEFILE     1538   /* Save Registration File... */
#define IDM_FIND         1552   /* 0x610 Search > Find Key... */
#define IDM_FINDNEXT     1553   /* Find Next */
#define IDM_HELPCONTENTS 1792   /* 0x700 */
#define IDM_HELPSEARCH   1793
#define IDM_HELPHELP     1794
#define IDM_ABOUT        1795

/* the basic view's dialog (RE1) */
#define IDC_TYPES        0x500  /* "Registered File Types:" list */
#define IDC_IDENTS       0x431  /* the hidden list of identifiers (and /V's tree) */

/* the advanced view's dialog (RE2) */
#define IDC_TREE         0x431
#define IDC_HIDDEN1      0x621
#define IDC_HIDDEN2      0x622
#define IDC_VALUE        0x620
#define IDC_PATH         0x623

/* Modify / Add / Copy File Type (RE3) */
#define IDC_IDENT        0x510
#define IDC_USESDDE      0x512
#define IDC_BROWSE       0x513
#define IDC_DDEGROUP     0x515
#define IDC_OPEN         0x520
#define IDC_PRINT        0x521
#define IDC_FILETYPE     0x530
#define IDC_COMMAND      0x531
#define IDC_DDEMSG       0x532
#define IDC_DDENOTRUN    0x533
#define IDC_DDEAPP       0x534
#define IDC_DDETOPIC     0x535
#define IDC_HELP         0x401

/* Add Key, Copy Key, Delete Key, Find Key (RE4) */
#define IDC_KEYPATH      0x630
#define IDC_KEY          0x631
#define IDC_KEYVALUE     0x632

/* dialog templates */
#define DLG_BASIC        100
#define DLG_ADVANCED     101
#define DLG_FILETYPE     102
#define DLG_ADDKEY       103
#define DLG_COPYKEY      104
#define DLG_DELKEY       105
#define DLG_FINDKEY      106
#define DLG_MODIFY       107
#define ACC_ADVANCED     101
#define MENU_BASIC       100
#define MENU_ADVANCED    101
#define IDI_REGEDIT      1

/* strings */
#define IDS_TITLE        1
#define IDS_CLASS        2
#define IDS_MODIFY       3
#define IDS_ADD          4
#define IDS_COPY         5
#define IDS_DELCONFIRM   6
#define IDS_IDEXISTS     7
#define IDS_NOFILETYPE   8
#define IDS_NOIDENT      9
#define IDS_NOCOMMAND    10
#define IDS_MERGED       11
#define IDS_NOTREG       12
#define IDS_CANTOPEN     13
#define IDS_LOSECHANGES  14
#define IDS_SAVEASK      15
#define IDS_MERGETITLE   16
#define IDS_SAVETITLE    17
#define IDS_REGFILTER    18
#define IDS_BROWSETITLE  19
#define IDS_PROGFILTER   20
#define IDS_HELPFILE     21
#define IDS_HELPFILEV    22
#define IDS_ABOUT        23
#define IDS_KEYEXISTS    24
#define IDS_NOKEY        25
#define IDS_CANTCREATE   26
#define IDS_CANTSAVE     27
#define IDS_BADPATH      28
#define IDS_ABOUTOTHER   29

/* ---- New Style (w16's own; not in Windows 3.1) ---- */
#define MENU_NEWSTYLE    200
#define ACC_NEWSTYLE     200
#define DLG_EDITSTRING   200
#define DLG_NSFIND       201
#define IDM_NEWSTYLE     2000   /* View > New Style, in every view when enabled */
#define IDM_NS_IMPORT    2001
#define IDM_NS_EXPORT    2002
#define IDM_NS_CONNECT   2003
#define IDM_NS_DISCONN   2004
#define IDM_NS_PRINT     2005
#define IDM_NS_NEWKEY    2010
#define IDM_NS_NEWSTR    2011
#define IDM_NS_DELETE    2012
#define IDM_NS_RENAME    2013
#define IDM_NS_FIND      2014
#define IDM_NS_FINDNEXT  2015
#define IDM_NS_MODIFY    2016
#define IDM_NS_STATUS    2020
#define IDM_NS_SPLIT     2021
#define IDM_NS_REFRESH   2022
#define IDM_NS_HELP      2030
#define IDM_NS_ABOUT     2031
#define IDC_NS_NAME      2100
#define IDC_NS_DATA      2101
#define IDC_NS_LOOKKEYS  2102
#define IDC_NS_LOOKDATA  2103
#define IDC_NS_WHOLE     2104
#define IDC_NS_TREE      2110
#define IDC_NS_LIST      2111
#define IDC_NS_HEADER    2112
#define IDC_NS_RENAME    2113
#define IDS_NS_TITLE     100
#define IDS_NS_COMPUTER  101
#define IDS_NS_ROOT      102
#define IDS_NS_DEFAULT   103
#define IDS_NS_NOTSET    104
#define IDS_NS_NAME      105
#define IDS_NS_DATA      106
#define IDS_NS_DELKEY    107
#define IDS_NS_DELVAL    108
#define IDS_NS_DELKEYT   109
#define IDS_NS_DELVALT   110
#define IDS_NS_NEWKEY    111
#define IDS_NS_RENEXISTS 112
#define IDS_NS_RENEMPTY  113
#define IDS_NS_FINDDONE  114
#define IDS_NS_IMPORTED  115
#define IDS_NS_EXPORTED  116
#define IDS_NS_IMPTITLE  117
#define IDS_NS_EXPTITLE  118
#define IDS_NS_ABOUT     119
#define IDS_NS_RENBAD    120
#define IDS_NS_MENUITEM  121

#ifndef RC_INVOKED
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string.h>
#include <stdlib.h>
/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
/* KERNEL.349, 350 (SDK 3.1); not declared in OpenWatcom's windows.h */
long FAR PASCAL _hread(HFILE, void _huge *, long);
long FAR PASCAL _hwrite(HFILE, const void _huge *, long);

/* main.c */
extern HINSTANCE g_inst;
extern HWND g_frame, g_view;    /* the frame and the current view's dialog/child */
extern int g_mode;              /* MODE_BASIC, MODE_ADVANCED, MODE_NEWSTYLE */
extern int g_nsSetting;         /* [w16] RegeditNewStyle: 0 stock, 1, 2 */
extern BOOL g_dirty;            /* /V: changes since the last save */
extern char g_sel[];            /* the selected key's path, "" for the root */
#define MODE_BASIC     0
#define MODE_ADVANCED  1
#define MODE_NEWSTYLE  2
#define PATHMAX        512      /* bound: the buffers for paths and values
                                   live in REGEDIT's one data segment (64 KB
                                   with the stack); longer ones are cut */
int  rstr(UINT id, LPSTR buf, int cb);
int  msgbox(HWND owner, UINT idfmt, LPCSTR arg, UINT flags);
void set_mode(int mode);
void help(UINT cmd, DWORD data);
void about(void);
void layout_view(void);
void add_view_menu(HMENU m);
BOOL file_dialog(HWND owner, BOOL save, UINT idtitle, UINT idfilter, LPCSTR defext,
                 LPSTR file, int cb);

/* reg.c: helpers over SHELL's registration API */
LONG  reg_get(LPCSTR path, LPSTR buf, LONG cb);           /* the value of path */
LONG  reg_set(LPCSTR path, LPCSTR value);
BOOL  reg_exists(LPCSTR path);
LONG  reg_delete(LPCSTR path);
LONG  reg_copy(LPCSTR from, LPCSTR to);
LONG  reg_create(LPCSTR path);
LPSTR path_last(LPSTR path);    /* the last component (after the last '\') */

/* regfile.c */
#define MERGE_OK       0
#define MERGE_NOTREG   1
#define MERGE_CANTOPEN 2
typedef void (*MERGEFN)(LPCSTR key, LPCSTR val);
int  merge_file(LPCSTR file, MERGEFN apply);   /* apply NULL: into SHELL */
BOOL save_file(LPCSTR file, LPCSTR path);

/* basic.c, advanced.c */
BOOL FAR PASCAL _export BasicDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL _export AdvDlgProc(HWND, UINT, WPARAM, LPARAM);
void basic_command(HWND, UINT);
void adv_command(HWND, UINT);
void basic_refresh(HWND);
void adv_refresh(HWND);
void adv_save(void);
void adv_merge_line(LPCSTR key, LPCSTR val);

/* newstyle.c */
HWND ns_create(HWND frame);
void ns_command(UINT id);
void ns_size(int cx, int cy);
BOOL ns_translate(MSG FAR *m);
void ns_save(void);
#endif
#endif
