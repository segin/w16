/*
 * maincpl.h - w16's MAIN.CPL: the Control Panel applets of Windows 3.1's
 * MAIN.CPL (Color, Fonts, Ports, Mouse, Desktop, Keyboard, Printers,
 * International, Date/Time), each made to match the original's dialogs
 * and the settings it writes (ARCHITECTURE.md 14).
 *
 * Built large model, SS != DS (a DLL), no C runtime.
 */
#ifndef MAINCPL_H
#define MAINCPL_H

#include <windows.h>
#include "cpl.h"

/* applets, in the original's order in Control Panel */
#define AP_COLOR 0
#define AP_FONTS 1
#define AP_PORTS 2
#define AP_MOUSE 3
#define AP_DESKTOP 4
#define AP_KEYBOARD 5
#define AP_PRINTERS 6
#define AP_INTL 7
#define AP_DATETIME 8
#define AP_COUNT 9

/* icons: 1 + applet */
#define IDI_APPLET          1
#define IDI_COLOR           1
#define IDI_FONTS           2
#define IDI_PORTS           3
#define IDI_MOUSE           4
#define IDI_DESKTOP         5
#define IDI_KEYBOARD        6
#define IDI_PRINTERS        7
#define IDI_INTL            8
#define IDI_DATETIME        9

/* strings (the original's numbering, observed with tools/resdump.py) */
#define IDS_NOMEM           0
#define IDS_CONTROLPANEL    1
#define IDS_NAME_COLOR      48
#define IDS_NAME_PRINTERS   49
#define IDS_NAME_FONTS      50
#define IDS_NAME_INTL       51
#define IDS_NAME_PORTS      52
#define IDS_NAME_KEYBOARD   53
#define IDS_NAME_MOUSE      54
#define IDS_NAME_DATETIME   55
#define IDS_NAME_DESKTOP    56
#define IDS_INFO_COLOR      600     /* + the name's offset from 48 */
#define IDS_NOTYET          4000    /* w16's own */
#define IDS_ELEMENT         4100    /* + scheme index: Screen Element names */
#define IDS_SAMPLE_ACTIVE   93
#define IDS_SAMPLE_INACTIVE 94
#define IDS_SAMPLE_MENU     95
#define IDS_SAMPLE_WINTEXT  96
#define IDS_SAMPLE_DISABLED 97
#define IDS_SAMPLE_HILIGHTED 98
#define IDS_DEFAULT_SCHEME  99
#define IDS_SCHEME_NOREMOVE 100
#define IDS_SAMPLE_OK       108
#define IDS_SCHEME_DEFAULT  104
#define IDS_SCHEME_EQUAL    105
#define IDS_SCHEME_BRACKET  106
#define IDS_SCHEME_BLANK    107
#define IDS_SCHEME_REMOVE   236
#define IDS_DK_REMOVEPAT    239
#define IDS_DK_GRAN         208
#define IDS_DK_BORDER       214
#define IDS_DK_SPACING      218
#define IDS_DK_DELAY        220

/* dialogs (the original's resource IDs) */
#define DLG_KEYBOARD        5
#define DLG_MOUSE           6
#define DLG_DESKTOP         8
#define DLG_EDITPATTERN     34
#define DLG_CUSTOMCOLOR     26
#define DLG_SAVESCHEME      27
#define DLG_COLOR           100


/* common control IDs */
#define IDC_HELP            119

/* Keyboard */
#define IDC_KB_TEST         600
#define IDC_KB_SPEED        601
#define IDC_KB_DELAY        603

/* Color */
#define IDC_CO_SAVE         711
#define IDC_CO_REMOVE       712
#define IDC_CO_PALETTE      714
#define IDC_CO_SCHEMES      715
#define IDC_CO_ELEMENT      716
#define IDC_CO_SAMPLE       717
#define IDC_CO_DEFINE       719
#define IDC_CO_BASIC        32
#define IDC_CO_CUSTOM       80
#define IDC_SS_NAME         711
#define IDC_CC_LUM          702
#define IDC_CC_HUE          703
#define IDC_CC_SAT          704
#define IDC_CC_LUMV         705
#define IDC_CC_RED          706
#define IDC_CC_GREEN        707
#define IDC_CC_BLUE         708
#define IDC_CC_SAMPLE       709
#define IDC_CC_SPECTRUM     710
#define IDC_CC_SOLID        713
#define IDC_CC_HUEA         720
#define IDC_CC_SATA         721
#define IDC_CC_LUMA         722
#define IDC_CC_REDA         723
#define IDC_CC_GREENA       724
#define IDC_CC_BLUEA        725
#define IDC_CC_ADD          731

/* Desktop */
#define IDC_DK_PATTERN      102
#define IDC_DK_EDITPAT      103
#define IDC_DK_WALLPAPER    105
#define IDC_DK_CENTER       107
#define IDC_DK_TILE         108
#define IDC_DK_GRAN         113
#define IDC_DK_GRANA        114
#define IDC_DK_SPACING      116
#define IDC_DK_SPACINGA     117
#define IDC_DK_WRAP         118
#define IDC_DK_COOLSWITCH   121
#define IDC_DK_BORDER       954
#define IDC_DK_BORDERA      955
#define IDC_DK_BLINK        958
#define IDC_DK_BLINKER      959
#define IDC_DK_SAVER        960
#define IDC_DK_DELAY        961
#define IDC_DK_DELAYA       962
#define IDC_DK_TEST         963
#define IDC_DK_SETUP        964
#define IDC_EP_GRID         101
#define IDC_EP_NAME         102
#define IDC_EP_ADD          109
#define IDC_EP_CHANGE       110
#define IDC_EP_REMOVE       111
#define IDC_EP_SAMPLE       115

/* Mouse */
#define IDC_MS_SWAP         500
#define IDC_MS_L            503
#define IDC_MS_R            504
#define IDC_MS_BOX          505
#define IDC_MS_LBOX         506
#define IDC_MS_RBOX         507
#define IDC_MS_TRAILS       509
#define IDC_MS_TEST         529
#define IDC_MS_DBLCLK       531
#define IDC_MS_TRACK        532

/* the spin control class of the original ("cpArrow"): a narrow box with
   an up and a down arrow, which sends its parent WM_VSCROLL SB_LINEUP or
   SB_LINEDOWN with its window handle */
extern char cls_arrow[];

extern HINSTANCE g_hinst;

/* main.c */
int cpl_dialog(HWND parent, int id, DLGPROC proc);
void cpl_help(HWND dlg, int applet);

/* applets */
void keyboard_applet(HWND parent);
void mouse_applet(HWND parent);
void color_applet(HWND parent);
void desktop_applet(HWND parent);

#endif
