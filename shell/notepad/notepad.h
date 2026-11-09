/*
 * notepad.h - w16 Notepad.
 *
 * A clean-room Notepad made to match Windows 3.1's in look, behaviour and
 * the files it writes. Every visible detail comes from black-box
 * observation of the original on the reference install (tests/refrun.sh
 * with SNAP.EXE: screenshots, window dumps, "info"), and from its menu,
 * dialog, string and accelerator resources read as data
 * (tools/nerdump.py). ARCHITECTURE.md 15.1 has the findings.
 *
 * Two builds from this source: the 3.x build (COMMDLG, WinHelp,
 * ShellAbout, drag and drop) and the Windows 2.x build (W16_WIN2: own
 * Open, Save As and Find dialogs, no help). Medium model, no C runtime.
 */
#ifndef NOTEPAD_H
#define NOTEPAD_H

#include <windows.h>

/* ---- command IDs: the original's (its menu resource) ---- */
#define IDM_SAVE            1
#define IDM_SAVEAS          2
#define IDM_FIND            3
#define IDM_HELPCONTENTS    5
#define IDM_SELECTALL       7
#define IDM_FINDNEXT        8
#define IDM_NEW             9
#define IDM_OPEN            10
#define IDM_ABOUT           11
#define IDM_TIMEDATE        12
#define IDM_PRINT           14
#define IDM_UNDO            25
#define IDM_WORDWRAP        27
#define IDM_EXIT            28
#define IDM_PRINTSETUP      31
#define IDM_PAGESETUP       32
#define IDM_HELPHELP        40
#define IDM_HELPSEARCH      41
#define IDM_CUT             0x300       /* WM_CUT etc., as the original */
#define IDM_COPY            0x301
#define IDM_PASTE           0x302
#define IDM_DELETE          0x303

/* ---- resources ---- */
#define IDI_NOTEPAD         1
#define IDM_MENU            1
#define IDD_OPEN2           1           /* 2.x build: File Open */
#define IDD_SAVEAS2         2           /* 2.x build: File Save As */
#define IDD_FIND2           9           /* 2.x build: Find */
#define IDD_ABOUT2          10          /* 2.x build: About */
#define IDD_PRINTING        12
#define IDD_PAGESETUP       14

/* control IDs (the original's) */
#define IDC_EDIT            15          /* the edit window */
#define IDC_FNAME           15          /* 2.x dialogs: file name */
#define IDC_FILES           16
#define IDC_DIRS            17
#define IDC_PATH            18
#define IDC_FWD             10          /* 2.x Find: Forward, Backward */
#define IDC_BACK            11
#define IDC_CASE            12
#define IDC_PRINTFILE       20          /* Printing: the file */
#define IDC_HEADER          30
#define IDC_FOOTER          31
#define IDC_LEFT            32
#define IDC_RIGHT           33
#define IDC_TOP             34
#define IDC_BOTTOM          35

/* strings: the original's IDs */
#define IDS_CANTOPEN        1
#define IDS_CREATENEW       3
#define IDS_REPLACE         5
#define IDS_CHANGED         10
#define IDS_UNTITLED        11
#define IDS_TITLEPREFIX     12
#define IDS_CANTFIND        16
#define IDS_NOMEMORY        17
#define IDS_TOOLARGE        18
#define IDS_APPNAME         19
#define IDS_PASTELONG       20
#define IDS_DISKFULL        21
#define IDS_BADFORMAT       22
#define IDS_EMPTYDELETE     23
#define IDS_NOTEXT          24
#define IDS_CANTPRINT       25
#define IDS_BADNAME         26
#define IDS_BADNAME2        27
#define IDS_PRINTDISK       28
#define IDS_PRINTMEM        29
#define IDS_CANTCREATE      30
#define IDS_WRAPLARGE       31
#define IDS_FILTERSPEC      33
#define IDS_HELPFILE        34
#define IDS_BADMARGIN       35
#define IDS_HEADER          36
#define IDS_FOOTER          37
#define IDS_LEFT            38
#define IDS_RIGHT           39
#define IDS_TOP             40
#define IDS_BOTTOM          41
#define IDS_HFCODES         42
#define IDS_PRINTOPEN       43
#define IDS_FILTERTEXT      44
#define IDS_FILTERALL       45
#define IDS_OPENTITLE       46
#define IDS_SAVETITLE       47
#define IDS_QUERYEND        48
#define IDS_NODRIVER        50
#define IDS_ABOUT2          60          /* 2.x build: About text (ours) */

/* A DOS path: DOS bounds it (OFSTRUCT holds 128). */
#define DOS_PATH            144

/* ---- main.c ---- */
extern HINSTANCE g_hinst;
extern HWND g_main, g_edit;
extern BOOL g_wrap;
extern char g_file[DOS_PATH];           /* the file, as opened; "" untitled */
void set_title(void);
void sync_scroll(void);
HWND make_edit(BOOL wrap);
int msg_box(HWND owner, int ids, const char FAR *arg, UINT flags);

/* ---- file.c ---- */
BOOL file_query_save(void);             /* FALSE: the user cancelled */
void file_new(void);
void file_open(void);
BOOL file_save(BOOL as);
BOOL file_load(const char FAR *path, BOOL ask_create);
void file_drop(HANDLE hdrop);
void insert_time_date(void);
void time_part(char *buf);
void date_part(char *buf);
BOOL file_is_loading(void);

/* ---- find.c ---- */
extern UINT g_findmsg;                  /* COMMDLG's FINDMSGSTRING message */
extern HWND g_finddlg;
void find_dialog(void);
void find_next(void);
LRESULT find_message(LPARAM lp);

/* ---- print.c ---- */
void page_setup(void);
void print_setup(void);
void print_file(void);
BOOL can_print(void);

/* ---- help.c ---- */
void help(UINT cmd);

/* ---- dlg2.c (2.x build) ---- */
BOOL open_dialog2(char *file, BOOL save);
void find_dialog2(void);
void about_dialog2(void);
HMENU load_menu2(void);

/* ---- util.c ---- */
LPSTR mem_alloc(DWORD n);
void mem_free(LPSTR p);
int s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int max);
void s_cat(char FAR *d, const char FAR *s, int max);
int s_icmp(const char FAR *a, const char FAR *b);
const char FAR *s_basename(const char FAR *path);
LPSTR load_str(int ids);                /* any length; mem_free it */
LPSTR format_msg(int ids, const char FAR *arg);   /* "%%" -> arg */

#endif
