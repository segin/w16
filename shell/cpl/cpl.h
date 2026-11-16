/*
 * cpl.h - the Control Panel applet interface (CPlApplet), as documented in
 * the Windows 3.1 SDK. A .CPL file is a DLL that exports CPlApplet; Control
 * Panel calls it with these messages.
 *
 * The message numbers and structure layouts follow the SDK documentation;
 * each is checked on Windows 3.1 with the CP1 probe (tests/apps/cp1.c,
 * ARCHITECTURE.md 14).
 */
#ifndef W16_CPL_H
#define W16_CPL_H

#define CPL_INIT        1   /* first call; 0 = do not load */
#define CPL_GETCOUNT    2   /* number of applets in this DLL */
#define CPL_INQUIRE     3   /* lParam1 = applet, lParam2 -> CPLINFO */
#define CPL_SELECT      4   /* applet selected */
#define CPL_DBLCLK      5   /* applet chosen: lParam1 = applet, lParam2 = lData */
#define CPL_STOP        6   /* once per applet before CPL_EXIT */
#define CPL_EXIT        7   /* last call before FreeLibrary */
#define CPL_NEWINQUIRE  8   /* lParam1 = applet, lParam2 -> NEWCPLINFO */

#define CPL_DYNAMIC_RES 0   /* idIcon/idName/idInfo: ask with NEWINQUIRE */

typedef struct tagCPLINFO {
    int  idIcon;
    int  idName;
    int  idInfo;
    LONG lData;
} CPLINFO, FAR *LPCPLINFO;

typedef struct tagNEWCPLINFO {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHelpContext;
    LONG  lData;
    HICON hIcon;
    char  szName[32];
    char  szInfo[64];
    char  szHelpFile[128];
} NEWCPLINFO, FAR *LPNEWCPLINFO;

typedef LONG (FAR PASCAL *APPLET_PROC)(HWND, UINT, LPARAM, LPARAM);

#endif
