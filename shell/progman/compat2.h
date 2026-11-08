/*
 * compat2.h - the Windows 2.x build of Program Manager (W16_WIN2).
 *
 * Windows/386 2.11 lacks several calls the 3.x build uses (observed with
 * tests/apps/tw2.c, see ARCHITECTURE.md 8): private profiles, WinExec,
 * SHELL.DLL, the MDI client, CreateIcon/DestroyIcon, window placement,
 * ModifyMenu, SystemParametersInfo, WinHelp, GetWindowsDirectory. In
 * this build those names are redirected to compat2.c and mdi2.c, which
 * use only calls 2.11 has. Included by progman.h after windows.h.
 */
#ifndef COMPAT2_H
#define COMPAT2_H

#ifdef W16_WIN2

/* private profiles: our own reader and writer (compat2.c) */
#define GetPrivateProfileString     pm_GetPrivateProfileString
#define GetPrivateProfileInt        pm_GetPrivateProfileInt
#define WritePrivateProfileString   pm_WritePrivateProfileString
int  FAR pm_GetPrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int, LPCSTR);
UINT FAR pm_GetPrivateProfileInt(LPCSTR, LPCSTR, int, LPCSTR);
BOOL FAR pm_WritePrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPCSTR);

/* starting programs: LoadModule */
#define WinExec                     pm_WinExec
#define ShellExecute                pm_ShellExecute
#define FindExecutable              pm_FindExecutable
UINT FAR pm_WinExec(LPCSTR, UINT);
HINSTANCE FAR pm_ShellExecute(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int);
HINSTANCE FAR pm_FindExecutable(LPCSTR, LPCSTR, LPSTR);

/* no SHELL.DLL: no drag and drop, no icon extraction */
#define DragAcceptFiles(w, b)       ((void)0)
#define DragFinish(h)               ((void)0)
#define ExtractIcon(i, f, n)        ((HICON)NULL)

/* no CreateIcon: items show the default icon (TODO: 2.x icons) */
#define CreateIcon(i, w, h, p, b, a, x)  ((HICON)NULL)
#define DestroyIcon(h)              ((void)0)

#define GetWindowPlacement          pm_GetWindowPlacement
#define SetWindowPlacement          pm_SetWindowPlacement
BOOL FAR pm_GetWindowPlacement(HWND, WINDOWPLACEMENT FAR *);
BOOL FAR pm_SetWindowPlacement(HWND, const WINDOWPLACEMENT FAR *);

#define ModifyMenu                  pm_ModifyMenu
BOOL FAR pm_ModifyMenu(HMENU, UINT, UINT, UINT, LPCSTR);

#define SystemParametersInfo(a, b, c, d)  FALSE
#define WinHelp                     pm_WinHelp
BOOL FAR pm_WinHelp(HWND, LPCSTR, UINT, DWORD);
#define GetWindowsDirectory         pm_GetWindowsDirectory
UINT FAR pm_GetWindowsDirectory(LPSTR, UINT);
#define GetKeyNameText(l, b, n)     ((void)(n), 0)
#define MapVirtualKey(c, t)         0

/* MDI: our own client class (mdi2.c) */
#define DefFrameProc                pm_DefFrameProc
#define DefMDIChildProc             pm_DefMDIChildProc
#define TranslateMDISysAccel        pm_TranslateMDISysAccel
LRESULT FAR pm_DefFrameProc(HWND, HWND, UINT, WPARAM, LPARAM);
LRESULT FAR pm_DefMDIChildProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR pm_TranslateMDISysAccel(HWND, MSG FAR *);
BOOL mdi2_register(HINSTANCE);
HMENU pm_load_menu(HINSTANCE, LPCSTR);

/* no highlight colours before 3.0: inverted window colours instead */
#define PM_COLOR_HIGHLIGHT          COLOR_WINDOWTEXT
#define PM_COLOR_HIGHLIGHTTEXT      COLOR_WINDOW
#define MDI_CLIENT_CLASS            "PMMDIClient"

#else
#define MDI_CLIENT_CLASS            "MDICLIENT"
#define PM_COLOR_HIGHLIGHT          COLOR_HIGHLIGHT
#define PM_COLOR_HIGHLIGHTTEXT      COLOR_HIGHLIGHTTEXT
#endif

#endif
