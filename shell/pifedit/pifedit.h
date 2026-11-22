/*
 * pifedit.h - w16 PIF Editor (PIFEDIT.EXE), a clean-room clone of Windows
 * 3.1's (ARCHITECTURE.md 15.11). Every ID, text and layout below was
 * observed on the reference install's running PIF Editor (SNAP probes
 * PE1-PE21, tests/scripts/pifedit/obs/, run with tests/pifobs.sh); the
 * .PIF layout comes from the files it saved (pif.c's header).
 */
#ifndef PIFEDIT_H
#define PIFEDIT_H

#include <windows.h>

/* menu commands (PE1: 3.1's IDs) */
#define IDM_OPEN        10
#define IDM_SAVE        11
#define IDM_NEW         12
#define IDM_SAVEAS      13
#define IDM_ABOUT       14
#define IDM_EXIT        15
#define IDM_STANDARD    17
#define IDM_ENHANCED    18
#define IDM_HELPSTD     34
#define IDM_HELPENH     35
#define IDM_HELPADV     36
#define IDM_HELPSEARCH  37
#define IDM_HELPHELP    0xFFFC
#define IDM_HELPCONTENTS 0xFFFF

/* dialogs and resources */
#define DLG_STANDARD    1
#define DLG_ENHANCED    2
#define DLG_ADVANCED    3
#define IDI_PIFEDIT     1
#define MENU_PIFEDIT    1

/* controls (3.1's IDs, PE1, PE2) */
#define IDC_PROGRAM     0x0064
#define IDC_TITLE       0x0065
#define IDC_PARAMS      0x0066
#define IDC_DIR         0x0067
#define IDC_MEMREQ      0x0068
#define IDC_MEMDES      0x0069
#define IDC_ADVANCED    0x005A
#define IDC_COM1        0x00C9
#define IDC_KEYBOARD    0x00CB
#define IDC_COM2        0x00CC
#define IDC_COM3        0x00CE
#define IDC_COM4        0x00CF
#define IDC_NOSWITCH    0x012C
#define IDC_TEXT        0x012D
#define IDC_GRAPHICS    0x012E
#define IDC_NOSAVE      0x0134
#define IDC_NOEXCHANGE  0x0190
#define IDC_CLOSEEXIT   0x01F4
#define IDC_FGPRIO      0x0259
#define IDC_BGPRIO      0x025A
#define IDC_IDLE        0x025B
#define IDC_EMSREQ      0x025C
#define IDC_EMSLIM      0x025D
#define IDC_EMSLOCK     0x025E
#define IDC_XMSREQ      0x025F
#define IDC_XMSLIM      0x0260
#define IDC_XMSLOCK     0x0261
#define IDC_BACKGROUND  0x0262
#define IDC_WINDOWED    0x0263
#define IDC_FULLSCREEN  0x0264
#define IDC_EXCLUSIVE   0x0265
#define IDC_ALLOWCLOSE  0x0266
#define IDC_HOTKEY      0x0267
#define IDC_ALTTAB      0x0268
#define IDC_ALTESC      0x0269
#define IDC_ALTENTER    0x026A
#define IDC_ALTSPACE    0x026B
#define IDC_ALTPRTSC    0x026C
#define IDC_PRTSC       0x026D
#define IDC_CTRLESC     0x026E
#define IDC_HMA         0x026F
#define IDC_FASTPASTE   0x0270
#define IDC_LOCKMEM     0x0271
#define IDC_EMULATE     0x02BC
#define IDC_MONTEXT     0x02BD
#define IDC_MONLOW      0x02BE
#define IDC_MONHIGH     0x02BF
#define IDC_RETAIN      0x02C0
#define IDC_VTEXT       0x02C1
#define IDC_VLOW        0x02C2
#define IDC_VHIGH       0x02C3

/* strings */
#define IDS_TITLE       1       /* "PIF Editor" */
#define IDS_UNTITLED    2       /* "(Untitled)" */
#define IDS_CLASS       3       /* "Pif" */
#define IDS_HKCLASS     4       /* "PifHKy" */
#define IDS_HELPFILE    5       /* "PIFEDIT.HLP" */
#define IDS_FILTER      6       /* COMMDLG filter, '|' for NUL */
#define IDS_DEFEXT      7
#define IDS_NONE        8       /* "None" (no shortcut key) */
#define IDS_ALT         9
#define IDS_CTRL        10
#define IDS_SHIFT       11
#define IDS_SAVECHANGES 20
#define IDS_NOT386      21
#define IDS_NOTSTD      22
#define IDS_BADEXT      23
#define IDS_OLDPIF      24
#define IDS_NOTPIFEXT   25
#define IDS_ALLOWCLOSE  26
#define IDS_BADHOTKEY   27
#define IDS_MEMREQ640   28
#define IDS_MEMDES640   29
#define IDS_REQ16384    30
#define IDS_LIM16384    31
#define IDS_FILENOTFOUND 32
#define IDS_PATHNOTFOUND 33
#define IDS_CANTSAVE    34
#define IDS_PRIORITY    35
#define IDS_INVALIDPIF  36
#define IDS_FMTHELPON   40      /* "Press F1 for Help on %s." */
#define IDS_ADVBUTTON   41
#define IDS_ADVOK       42
#define IDS_ADVCANCEL   43
#define IDS_OPENTITLE   44
#define IDS_SAVETITLE   45
/* the topic names of "Press F1 for Help on %s.", by topic number */
#define IDS_TOPIC       100
/* the menu items' status lines: IDS_MENU + the item's ID (the system
   menu's by SC_ code / 16, from IDS_SYSMENU) */
#define IDS_MENU        200
#define IDS_SYSMENU     300

/* topics (status line and help), one per control group */
enum {
    T_NONE = 0, T_PROGRAM, T_TITLE, T_PARAMS, T_DIR, T_VIDEOMODE, T_MEMREQ,
    T_XMS, T_DIRECTMOD, T_NOEXCHANGE, T_NOSWITCH, T_CLOSEEXIT, T_NOSAVE,
    T_RESERVE, T_VIDEOMEM, T_EMS, T_DISPLAY, T_EXECUTION, T_PRIORITY,
    T_IDLE, T_EMSLOCK, T_XMSLOCK, T_HMA, T_LOCKMEM, T_MONITOR, T_EMULATE,
    T_RETAIN, T_FASTPASTE, T_ALLOWCLOSE, T_HOTKEY, T_ADVBUTTON, T_ADVOK,
    T_ADVCANCEL, T_COUNT
};

/* the PIF (pif.c) */
#define PIF_SIZE        0x221   /* a new PIF: base, PIFEX, 386, 286 */
#define PIF_BUFMAX      1024    /* the largest file read */
#define PIF_BASE        0x171   /* the base (Windows 2.x) section */

typedef struct {
    char title[31], program[64], params[65], dir[65];
    int memreq, memdes;         /* KB (std: base min; 386: min, max) */
    int xmsreq, xmslim, emsreq, emslim;
    int fgprio, bgprio;
    WORD f63, f16f, std286, flags386, video386;
    WORD hkscan, hkmods;
    BOOL hotkey;
    BOOL closeset;              /* Close Window on Exit was used */
} PIFVIEW;

extern BYTE g_pif[PIF_BUFMAX + 80];
extern UINT g_pifsize;
extern UINT g_off386, g_off286;
extern BOOL g_fieldfix, g_badsum;		/* [w16] PifFieldFix */

void pif_new(void);
int pif_load(LPCSTR path, HWND owner);       /* 0 ok, else IDS_ error */
BOOL pif_save(LPCSTR path);
void pif_get(PIFVIEW FAR *v, BOOL enh);
void pif_put(const PIFVIEW FAR *v, BOOL enh);
void pif_trimloaded(void);

/* main.c */
extern HINSTANCE g_inst;
extern HWND g_frame, g_view, g_adv;
extern BOOL g_enh;              /* the 386 Enhanced view is shown */
extern BOOL g_dirty;
int rstr(UINT id, LPSTR buf, int cb);
int msgbox(HWND owner, UINT id, UINT flags);
void status_set(HWND w, int topic);
void status_text(HWND w, LPCSTR text);
void status_paint(HWND w, HDC dc);
int status_topic(HWND ctl);
void help_f1(HWND ctl);

/* dlg.c */
BOOL CALLBACK view_proc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK adv_proc(HWND, UINT, WPARAM, LPARAM);
void view_create(void);
void view_fill(void);
BOOL view_commit(BOOL validate);
BOOL view_validate(HWND ctl);
void adv_run(void);
extern PIFVIEW g_v;

/* hotkey.c */
BOOL hotkey_register(HINSTANCE inst);
void hotkey_set(HWND w, WORD scan, WORD mods, BOOL on);
BOOL hotkey_get(HWND w, WORD FAR *scan, WORD FAR *mods, BOOL FAR *on);

/* the hotkey's modifier bits in the PIF (PE12) */
#define HK_SHIFT        0x03
#define HK_CTRL         0x04
#define HK_ALT          0x08

#endif
