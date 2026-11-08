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

/* ---- resource and command IDs (our own numbering) ---- */
#define IDM_MAIN            1
#define IDA_MAIN            1
#define IDI_PROGMAN         1
#define IDI_GROUP           2
#define IDI_DEFITEM         3
#define IDD_RUN             10
#define IDD_ABOUT           11

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

/* Run dialog controls */
#define IDC_RUNCMD          119     /* observed control IDs */
#define IDC_RUNMIN          105
#define IDC_BROWSE          112
#define IDC_HELPBTN         126

/* string table */
#define IDS_APPNAME         1
#define IDS_NOTYET          2
#define IDS_EXECFAIL        3
#define IDS_EXIT            4
#define IDS_EXITWINDOWS     5

/* ---- shared state ---- */
extern HINSTANCE g_hinst;
extern HWND g_frame, g_client;
extern BOOL g_autoarrange, g_minonrun, g_savesettings;
extern char g_ini[];                    /* "PROGMAN.INI" */

/* ---- group.c ---- */
#define MAX_GROUPS          40
BOOL group_register(void);
HWND group_load(const char *grpfile, int ininum);
void group_save_all(void);
void group_destroy_all(void);
int  group_ininum(HWND hwnd);           /* GroupN number, 0 if none */
BOOL group_open_selected(HWND hwnd);
void group_arrange(HWND hwnd);
LRESULT CALLBACK __export GroupWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- dialogs.c ---- */
BOOL CALLBACK __export RunDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK __export AboutDlgProc(HWND, UINT, WPARAM, LPARAM);
int  run_dialog(HWND owner, FARPROC proc, int id);

/* ---- dialogs.c: program execution ---- */
BOOL pm_exec(HWND owner, const char FAR *cmd, const char FAR *dir, BOOL minimized);

/* ---- util.c ---- */
int  s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int max);
void s_cat(char FAR *d, const char FAR *s, int max);
int  s_icmp(const char FAR *a, const char FAR *b);
void s_utoa(char FAR *d, unsigned v);
int  s_atoi(const char FAR **ps);
void message(HWND owner, int ids, const char FAR *arg);

extern int __cdecl dos_chdir(const char FAR *path);
extern void __cdecl dos_setdrive(int drive);

#endif
