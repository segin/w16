/*
 * progman.h - w16 Program Manager.
 *
 * A clean-room Program Manager, made to match the Windows 3.1 one in look,
 * behaviour, and external interfaces (window classes, .GRP files,
 * PROGMAN.INI). The reference for every visible detail is black-box
 * observation of the original (tests/refrun.sh + SNAP.EXE, see
 * ARCHITECTURE.md 6.11).
 *
 * Built medium model, no C runtime.
 */
#ifndef PROGMAN_H
#define PROGMAN_H

#include <windows.h>
#include <shellapi.h>
#include "compat2.h"

/* ---- resource and command IDs (our own numbering) ---- */
#define IDM_MAIN            1
#define IDA_MAIN            1
/* The stock icons have resource IDs 1..STOCK_ICONS in index order, so
   ExtractIcon("PROGMAN.EXE", n) gives stock icon n (tools/stockicons.py:
   the original's 46, by subject). Program Manager's own icon is stock 0,
   a group's stock 7, an item without one stock 2. */
#define STOCK_ICONS         46
#define IDI_PROGMAN         1
#define IDI_GROUP           8
#define IDI_DEFITEM         3
#define IDD_RUN             10
#define IDD_ABOUT           11
#define IDD_NEW             12
#define IDD_GROUPPROPS      13
#define IDD_ITEMPROPS       14
#define IDD_MOVECOPY        15
#define IDD_EXITWIN         16
#define IDD_CHGICON         17

#define IDM_NEW             100
#define IDM_OPEN            101
#define IDM_MOVE            102
#define IDM_COPY            103
#define IDM_DELETE          104
#define IDM_PROPERTIES      105
#define IDM_RUN             106
#define IDM_EXIT            107
#define IDM_AUTOARRANGE     110
#define IDM_MINONRUN        111
#define IDM_SAVESETTINGS    112
#define IDM_CASCADE         120
#define IDM_TILE            121
#define IDM_ARRANGE         122
#define IDM_HELPCONTENTS    130
#define IDM_HELPSEARCH      131
#define IDM_HELPHELP        132
#define IDM_TUTORIAL        133
#define IDM_ABOUT           134

/* the first MDI child ID (observed: the first group window has 500) */
#define IDM_FIRSTCHILD      500

/* positions of the popup menus in the menu bar */
#define MENU_FILE           0
#define MENU_OPTIONS        1
#define MENU_WINDOW         2
#define WINDOW_MENU_FIXED   3           /* Cascade, Tile, Arrange Icons */

/* Automatic radio buttons and check boxes arrived with Windows 3.0; the
   2.x build uses plain ones and checks them itself (dialogs.c) */
#ifdef W16_WIN2
#define PM_RADIO            BS_RADIOBUTTON
#define PM_CHECK            BS_CHECKBOX
#else
#define PM_RADIO            BS_AUTORADIOBUTTON
#define PM_CHECK            BS_AUTOCHECKBOX
#endif

/* Run dialog controls */
#define IDC_RUNCMD          119     /* observed control IDs */
#define IDC_RUNMIN          105
#define IDC_BROWSE          112
#define IDC_HELPBTN         126

/* observed control IDs of the New, group and item properties dialogs */
#define IDC_STATIC          0x63
#define IDC_DESC            0x64
#define IDC_CMDLINE         0x65
#define IDC_CHANGEICON      0x66
#define IDC_ITEMMIN         0x69
#define IDC_WORKDIR_LABEL   0x6E
#define IDC_NEWGROUP        0x74
#define IDC_NEWITEM         0x75
#define IDC_GROUPFILE       0x77
#define IDC_WORKDIR         0x79
#define IDC_HOTKEY          0x7B
#define IDC_ITEMICON        0x7D
#define IDC_MC_LABEL        0x73        /* Move/Copy dialog (observed IDs) */
#define IDC_MC_GROUP        0x74
#define IDC_MC_ITEM         0x75
#define IDC_MC_TO           0x76
#define IDC_EXITICON        200
#ifndef LBS_DISABLENOSCROLL             /* Windows 3.1; not in every header */
#define LBS_DISABLENOSCROLL 0x1000L
#endif
#define IDC_ICONFILE        0x64        /* Change Icon (observed IDs) */
#define IDC_ICONLIST        0x66

/* the shortcut key control (class "pmhotkey", observed) */
#define HKM_SETHOTKEY       (WM_USER + 0)
#define HKM_GETHOTKEY       (WM_USER + 1)
#define HOTKEY_CTRLALT      0x0600      /* modifier byte observed in .GRP */
BOOL hotkey_register(void);
BOOL iconlist_register(void);            /* 2.x: pmiconlist, pmicon */

/* string table */
#define IDS_APPNAME         1
#define IDS_NOTYET          2
#define IDS_EXECFAIL        3
#define IDS_EXIT            4
#define IDS_EXITWINDOWS     5
#define IDS_DELETE          6
#define IDS_DELITEM         7
#define IDS_DELGROUP        8
#define IDS_NOGROUP         9
#define IDS_HOTKEYNONE      10
#define IDS_HOTKEYPREFIX    11
#define IDS_CANTWRITE       12
#define IDS_DLGFAIL         13
#define IDS_MOVETITLE       14
#define IDS_COPYTITLE       15
#define IDS_MOVELABEL       16
#define IDS_COPYLABEL       17
#define IDS_NOICONS         18

/* ---- shared state ---- */
extern HINSTANCE g_hinst;
extern HWND g_frame, g_client;
extern BOOL g_autoarrange, g_minonrun, g_savesettings, g_isshell;
extern char g_ini[];                    /* "PROGMAN.INI" */

/* ---- group.c ----
 * Zero-One-Infinity: no fixed limits on groups, items, or string lengths;
 * everything is sized from the data. The remaining bounds come from the
 * formats themselves (a .GRP's offsets are 16-bit, so a group is < 64K). */
BOOL group_register(void);
HWND group_load(const char FAR *grpfile, int ininum);
HWND group_create(const char FAR *name, const char FAR *path, int ininum);
/* an item as Program Item Properties shows it */
typedef struct {
    LPSTR desc, cmd, dir;               /* global memory (mem_alloc) */
    WORD hotkey;                        /* 0 = none */
    BOOL minimized;
    HICON icon;                         /* the item's, not to be destroyed */
    /* for DDE AddItem only (zero otherwise) */
    LPSTR iconpath;                     /* NULL: the program's own icon */
    WORD iconindex;
    BOOL has_pos;
    int x, y;
    BOOL dde;                           /* placed as DDE AddItem places it */
} itemprops;
BOOL group_get_props(HWND hwnd, itemprops FAR *ip);
void itemprops_free(itemprops FAR *ip);
BOOL group_add_item(HWND hwnd, const itemprops FAR *ip);
BOOL group_edit_selected(HWND hwnd, const itemprops FAR *ip);
LPSTR group_selected_icon(HWND hwnd, WORD FAR *index);
BOOL CALLBACK __export ChangeIconDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL group_rename(HWND hwnd, const char FAR *name);
BOOL group_set_file(HWND hwnd, const char FAR *path);
BOOL group_transfer_selected(HWND from, HWND to, BOOL copy_only);
void group_run_all(HWND hwnd);
HWND group_find(const char FAR *name);
LPSTR group_dde_text(HWND hwnd);
BOOL group_delete_named(HWND hwnd, const char FAR *name);
BOOL group_replace_named(HWND hwnd, const char FAR *name);
void group_show(HWND hwnd, int show);
BOOL group_delete_selected(HWND hwnd);
LPCSTR group_name(HWND hwnd);
LPCSTR group_path(HWND hwnd);
LPCSTR group_selected_name(HWND hwnd);
void group_save_all(void);
void group_destroy_all(void);
int  group_ininum(HWND hwnd);           /* GroupN number, 0 if none */
BOOL group_open_selected(HWND hwnd);
void group_arrange(HWND hwnd);
LRESULT CALLBACK __export GroupWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- main.c: PROGMAN.INI [Groups] and Order= ---- */
int  ini_add_group(const char FAR *path);       /* returns N of GroupN= */
void ini_remove_group(int n, LPSTR order);
LPSTR order_string(BOOL leading);               /* Order= value, z-order */
void ini_set_group(int n, const char FAR *path);
void ini_groups_changed(void);                  /* rewrite Order= */

/* ---- dialogs.c ---- */
BOOL CALLBACK __export NewDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export GroupPropsDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export ItemPropsDlgProc(HWND, UINT, WPARAM, LPARAM);
void cmd_new(HWND frame);
void cmd_delete(HWND frame);
void cmd_properties(HWND frame);
void cmd_movecopy(HWND frame, BOOL copy_only);
HWND create_group_named(const char FAR *name, const char FAR *file);
LRESULT dde_message(HWND frame, UINT msg, WPARAM wp, LPARAM lp);  /* dde.c */
void delete_group(HWND g);
BOOL CALLBACK __export MoveCopyDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export ExitWinDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export RunDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export AboutDlgProc(HWND, UINT, WPARAM, LPARAM);
int  run_dialog(HWND owner, FARPROC proc, int id);

/* ---- dialogs.c: program execution ---- */
BOOL pm_exec(HWND owner, const char FAR *cmd, const char FAR *dir, BOOL minimized);

/* ---- iconx.c: icons read from .ICO, NE (3.x and 2.x) and PE files ---- */
int   iconx_count(const char FAR *path);
LPSTR iconx_bits(const char FAR *path, int index, WORD FAR *cband, WORD FAR *cbxor);
HICON iconx_icon(const char FAR *bits);
/* both: SHELL's ExtractIcon first (3.x), then our own reader */
LPSTR icon_file(const char FAR *name);  /* group.c: an icon file's path */
int   icon_count(const char FAR *path);
HICON icon_extract(const char FAR *path, int index);

/* ---- util.c ---- */
LPSTR mem_alloc(DWORD n);               /* zeroed, locked global memory */
LPSTR mem_realloc(LPSTR p, DWORD n);    /* NULL on failure (p still valid) */
void  mem_free(LPSTR p);
LPSTR s_dup(const char FAR *s);
BOOL  s_append(LPSTR FAR *buf, const char FAR *s);  /* grows *buf */
LPSTR profile_get(const char FAR *sec, const char FAR *key, const char FAR *file);
int  s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int max);
void s_cat(char FAR *d, const char FAR *s, int max);
int  s_icmp(const char FAR *a, const char FAR *b);
void s_utoa(char FAR *d, unsigned v);
void s_itoa(char FAR *d, int v);
int  s_atoi(const char FAR **ps);
void message(HWND owner, int ids, const char FAR *arg);
int  ask(HWND owner, int title_ids, int ids, const char FAR *arg, UINT flags);

extern int __cdecl dos_chdir(const char FAR *path);
extern void __cdecl dos_setdrive(int drive);

#endif
