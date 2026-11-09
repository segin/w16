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

/* dialogs (the original's resource IDs) */
#define DLG_KEYBOARD        5
#define DLG_MOUSE           6

/* common control IDs */
#define IDC_HELP            119

/* Keyboard */
#define IDC_KB_TEST         600
#define IDC_KB_SPEED        601
#define IDC_KB_DELAY        603

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

#endif
