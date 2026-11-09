/*
 * compat2.h - the Windows 2.x build of Clock (W16_WIN2).
 *
 * Windows/386 2.11 lacks calls the 3.x build uses (ARCHITECTURE.md 8,
 * tests/apps/tw2.c): private profiles, ChooseFont (no COMMDLG), ShellAbout
 * (no SHELL.DLL), window placement, the topmost z-order, AppendMenu, and
 * it cannot read the 3.x menu resource format. In this build those are
 * replaced by compat2.c, which uses only calls 2.11 has. Included by
 * clock.h after windows.h.
 */
#ifndef CK_COMPAT2_H
#define CK_COMPAT2_H

#ifdef W16_WIN2
#define GetPrivateProfileString     ck_GetPrivateProfileString
#define GetPrivateProfileInt        ck_GetPrivateProfileInt
#define WritePrivateProfileString   ck_WritePrivateProfileString
int  FAR ck_GetPrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int, LPCSTR);
UINT FAR ck_GetPrivateProfileInt(LPCSTR, LPCSTR, int, LPCSTR);
BOOL FAR ck_WritePrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPCSTR);

HMENU ck_load_menu(HINSTANCE, LPCSTR);
void ck_restored_rect(HWND, RECT *);
void ck_track_rect(HWND);               /* note the restored rectangle */
void ck_about(HWND);
/* the button colours (COLOR_BTNFACE and on) came with 3.0: Windows 3.1's
   defaults instead */
#define GetSysColor                 ck_GetSysColor
DWORD ck_GetSysColor(int);
/* an opaque ExtTextOut fills with the nearest solid colour, which for the
   button face is white on 2.x: fill with the (dithered) brush instead */
#define ExtTextOut                  ck_ExtTextOut
BOOL ck_ExtTextOut(HDC, int, int, UINT, const RECT FAR *, LPCSTR, UINT, int FAR *);
/* 2.x has no SWP_FRAMECHANGED: SWP_DRAWFRAME is the same bit */
#else
#define ck_load_menu                LoadMenu
#endif

#endif
