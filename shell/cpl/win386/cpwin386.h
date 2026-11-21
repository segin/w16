/*
 * cpwin386.h - w16's CPWIN386.CPL: Control Panel's "386 Enhanced" applet
 * (device contention, scheduling, virtual memory), shown only in 386
 * Enhanced mode, as Windows 3.1's SYSTEM\CPWIN386.CPL (ARCHITECTURE.md 14).
 *
 * Built large model, SS != DS (a DLL), no C runtime.
 */
#ifndef CPWIN386_H
#define CPWIN386_H

#include <windows.h>
#include "cpl.h"
#include "arrow.h"

#define IDI_ENH386          1

#define IDS_NAME            1
#define IDS_INFO            2
#define IDS_TITLE           3
#define IDS_BAD_MIN         4
#define IDS_BAD_FG          5
#define IDS_BAD_BG          6
#define IDS_VM_CONFIRM      7
#define IDS_VM_TITLE        8
#define IDS_VM_PERM         9
#define IDS_VM_TEMP         10
#define IDS_VM_NONE         11
#define IDS_VM_PERMANENT    12
#define IDS_VM_TEMPORARY    13
#define IDS_VM_NONE2        14
#define IDS_VM_RECMAX       15
#define IDS_VM_NEWSIZE      16
#define IDS_VM_KB           17
#define IDS_VM_MAXSIZE      18
#define IDS_VM_RECSIZE      19

/* dialogs (w16's numbering) */
#define DLG_ENH386          1
#define DLG_VMEM            2
#define DLG_VMRESTART       3

#define IDC_HELP            0x77
/* 386 Enhanced (IDs as 3.1's dialog reports them) */
#define IDC_EN_PORTS        0x384
#define IDC_EN_ALWAYS       0x385
#define IDC_EN_NEVER        0x386
#define IDC_EN_IDLE         0x387
#define IDC_EN_IDLESEC      0x388
#define IDC_EN_MIN          0x38A
#define IDC_EN_FG           0x38C
#define IDC_EN_BG           0x38E
#define IDC_EN_EXCL         0x390
#define IDC_EN_VMEM         0x82
/* Virtual Memory */
#define IDC_VM_CURDRIVE     0x258
#define IDC_VM_CURSIZE      0x259
#define IDC_VM_CURTYPE      0x25A
#define IDC_VM_HELP         0x25B
#define IDC_VM_DRIVE        0x25C
#define IDC_VM_TYPE         0x25D
#define IDC_VM_AVAIL        0x25E
#define IDC_VM_L2           0x25F
#define IDC_VM_V2           0x260
#define IDC_VM_K2           0x261
#define IDC_VM_L3           0x262
#define IDC_VM_V3           0x263
#define IDC_VM_K3           0x264
#define IDC_VM_SIZELBL      0x265
#define IDC_VM_SIZE         0x266
#define IDC_VM_CHANGE       0x26B
#define IDC_VM_32BIT        0x26C
#define IDC_VM_NEWGROUP     0x6F
#define IDC_VR_RESTART      0x269
#define IDC_VR_CONTINUE     0x26A
#define IDC_VR_ICON         0x2BC

extern HINSTANCE g_hinst;

#endif
