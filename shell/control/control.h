/*
 * control.h - w16 Control Panel (CONTROL.EXE).
 *
 * A clean-room Control Panel, made to match the Windows 3.1 one in look,
 * behaviour and external interfaces (window classes, CONTROL.INI [MMCPL],
 * the CPlApplet calls it makes). Every detail comes from black-box
 * observation of the original with SNAP.EXE and the CP1 applet probe
 * (tests/cpobs.sh, tests/apps/cp1.c; ARCHITECTURE.md 14).
 *
 * Built medium model, no C runtime.
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <windows.h>
#include <shellapi.h>
#include "../cpl/cpl.h"

/* resources (the menu and accelerator IDs are the original's: its menu
   resource 1001 has these command IDs, observed with tools/resdump.py) */
#define IDI_CONTROL         1001
#define IDM_CONTROL         1001
#define IDA_CONTROL         1001
#define IDS_TITLE           1024
#define IDS_LOADING         1025
#define IDS_NOMEM           1026
#define IDS_NOCPL           1027
#define IDS_ABOUT           1040

#define IDM_HELPSEARCH      33
#define IDM_HELPCONTENTS    40
#define IDM_HELPHELP        41
#define IDM_ABOUT           50
#define IDM_EXIT            51
#define IDM_HELPAPPLET      52          /* F1 */
#define IDM_APPLET          1100        /* + applet number: Settings menu */

/* child window IDs (observed: the icon list 0x14, the status line 0x15) */
#define ID_LIST             0x14
#define ID_STATUS           0x15

#endif
