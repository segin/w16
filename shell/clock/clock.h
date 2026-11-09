/*
 * clock.h - w16 Clock.
 *
 * A clean-room Clock, made to match the Windows 3.1 one in look and
 * behaviour. Every visible detail comes from black-box observation of the
 * original on the reference install (ARCHITECTURE.md 15.2): screenshots
 * (tests/apps/snap.c), window and menu dumps, the CLOCK.INI it writes, and
 * the GDI calls it makes as GD2 records them (tests/refrun.sh with
 * REFRUN_GDLOG=1, tools/gdcalls.py). tests/clockscenarios.sh compares
 * ours with it.
 *
 * Built medium model, no C runtime; Windows 3.1 only.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <windows.h>
#include <shellapi.h>
/* SHELL.DLL's About box (not in OpenWatcom's shellapi.h) */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

/* ---- resources and commands ---- */
/* the Settings menu's command IDs and the system menu's Always on Top are
   the original's (observed: SNAP dumpmenu) */
#define IDM_ANALOG          1
#define IDM_DIGITAL         2
#define IDM_SETFONT         3
#define IDM_ABOUT           4
#define IDM_TOPMOST         5
#define IDM_NOTITLE         6
#define IDM_SECONDS         7
#define IDM_DATE            8

#define IDI_CLOCK           1
#define IDM_MENU            1
#define IDD_FONT            100         /* the Font dialog (ChooseFont template) */

#define IDS_APPNAME         2           /* "Clock" */
#define IDS_NOTIMER         6
#define IDS_TOPMOST         9           /* "Always on &Top" */

/* the Font dialog's controls: ChooseFont's (commdlg's dlgs.h numbers) */
#define IDC_FONTNAME        1136        /* cmb1 */
#define IDC_SAMPLE          1092        /* stc5 */

/* ---- options (CLOCK.INI Options=, in its order) ---- */
typedef struct {
    int analog;                         /* 1 analog, 0 digital */
    int minimized;                      /* written on exit */
    int noseconds;
    int notitle;
    int topmost;
    int nodate;
} OPTIONS;

extern HINSTANCE g_hinst;
extern HWND g_wnd;
extern OPTIONS g_opt;
extern LOGFONT g_lf;                    /* the digital font (face, pitch...) */

/* system colours as the clock uses them (main.c) */
extern HBRUSH g_brshadow, g_brface, g_brteal;
extern HPEN g_pnhand, g_pnshadow, g_pnface, g_pncyan, g_pnred;
extern int g_aspv, g_asph;              /* pixels per 100 mm */

/* ---- the time ---- */
typedef struct {
    int hour, minute, second;
    int day, month, year;
} CLOCKTIME;

void time_now(CLOCKTIME *t);            /* main.c: DOS time and date */

/* ---- intl.c: WIN.INI [intl] ---- */
void intl_load(void);
void intl_time(const CLOCKTIME *t, char *hh, char *mm, char *ss, char *ampm);
void intl_date(const CLOCKTIME *t, char *buf, BOOL year);
void intl_measure(char *buf, BOOL seconds);   /* "88:88:88 MM" */
void intl_icon_measure(char *buf);            /* "88:88" */
BOOL intl_lead_zero(void);
extern char g_sep[4];

/* ---- face.c: analog ---- */
void face_init(void);
void face_size(int cx, int cy);
void face_paint(HDC dc, const CLOCKTIME *t);
void face_tick(HDC dc, const CLOCKTIME *old, const CLOCKTIME *t);
void face_icon(HDC dc, const CLOCKTIME *t);

/* ---- digital.c ---- */
void digi_size(HDC dc, int cx, int cy);
void digi_reset(void);
void digi_paint(HDC dc, const CLOCKTIME *t, BOOL all);
BOOL digi_changed(const CLOCKTIME *t);
void digi_free(void);

/* ---- util.c ---- */
int s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int n);
void s_cat(char FAR *d, const char FAR *s, int n);
int s_utoa(char *buf, unsigned v);
int s_atoi(const char FAR **p);
void icon_frame(HDC dc);

/* ---- fontdlg.c ---- */
BOOL font_choose(HWND owner);

#endif
