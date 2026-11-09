/*
 * compat2.h - the Windows 2.x build of Cardfile (W16_WIN2).
 *
 * Windows/386 2.11 lacks COMMDLG, OLE, SHELL.DLL, WinHelp, extended
 * window styles, message hooks and the 3.x menu resource format
 * (ARCHITECTURE.md 8, tests/apps/tw2.c). In this build those calls go to
 * compat2.c, which uses only what 2.11 has; pictures are bitmaps, files
 * are 3.0-format (MGC) only. Included by cardfile.h after windows.h.
 */
#ifndef CF_COMPAT2_H
#define CF_COMPAT2_H

#ifdef W16_WIN2
#define WinHelp                     cf_WinHelp
BOOL FAR cf_WinHelp(HWND, LPCSTR, UINT, DWORD);
#define CreateWindowEx(x, c, t, s, a, b, w, h, p, m, i, d) \
    CreateWindow(c, t, s, a, b, w, h, p, m, i, d)
#undef WS_EX_ACCEPTFILES
#define WS_EX_ACCEPTFILES           0
HMENU cf_load_menu(HINSTANCE, LPCSTR);
#ifndef EM_CANUNDO
#define EM_CANUNDO                  (WM_USER + 22)
#endif
#endif

#endif
