/*
 * compat2.h - the Windows 2.x build of Calculator (W16_WIN2).
 *
 * Windows/386 2.11 has no WinHelp, no SHELL.DLL (ShellAbout) and no
 * WIN87EM (ARCHITECTURE.md 8, docs/exports); compat2.c stands in.
 */
#ifndef CALC_COMPAT2_H
#define CALC_COMPAT2_H

#ifdef W16_WIN2
#define WinHelp         ca_WinHelp
#define ShellAbout      ca_ShellAbout
BOOL FAR ca_WinHelp(HWND, LPCSTR, UINT, DWORD);
int FAR ca_ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
#endif

#endif
