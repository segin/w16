/*
 * scrnsave.h - w16's screen saver library: the SCRNSAVE.LIB model of the
 * Windows 3.1 SDK (Programmer's Reference vol. 1, ch. 14), written from
 * that description and from black-box observation of Windows 3.1's
 * savers (ARCHITECTURE.md 15.8).
 *
 * A saver is a Windows program with the extension .SCR, linked with
 * scrnsave.o. It provides:
 *   ScreenSaverProc            the full-screen window's procedure; what it
 *                              does not handle goes to DefScreenSaverProc
 *   ScreenSaverConfigureDialog the Setup dialog's procedure (template
 *                              DLG_SCRNSAVECONFIGURE)
 *   RegisterDialogClasses      registers what the Setup dialog needs
 * and the resources: the strings IDS_DESCRIPTION ("Starfield
 * Simulation") and idsAppName ("Screen Saver.Stars": the CONTROL.INI
 * section; the part after the dot is the password dialogs' caption), the
 * icon ID_APP, its Setup dialog, and scrnsave.dlg (the password dialogs).
 * Its module description is "SCRNSAVE : <description>": Control Panel's
 * Desktop lists savers by it.
 *
 * The command line: /s or -s runs the saver full screen; /c, -c or
 * nothing shows the Setup dialog.
 */
#ifndef SCRNSAVE_H
#define SCRNSAVE_H

#include <windows.h>

/* resource IDs (the SDK's values where it gives them) */
#define IDS_DESCRIPTION         1
#define idsAppName              1000
#define IDS_PWCAPTION           1001    /* "Change Password" (its messages' caption) */
#define IDS_PWMISMATCH          1002
#define IDS_PWBADOLD            1003
#define ID_APP                  100
#define DLG_CHANGEPASSWORD      2000
#define DLG_ENTERPASSWORD       2001
#define DLG_INVALIDPASSWORD     2002
#define DLG_SCRNSAVECONFIGURE   2003

/* the password dialogs' controls (SDK, observed with these IDs) */
#define ID_OLDTEXT              100
#define ID_NEWTEXT              101
#define ID_AGAIN                102
#define ID_PASSWORD             103
#define ID_ETOLD                104
#define ID_ETNEW                105
#define ID_ETAGAIN              106
#define ID_ETPASSWORD           107
#define ID_ICON                 108
#define ID_PASSWORDHELP         109

/* Help: CONTROL.HLP's screen saver topics (3.1, probe: THELP) */
#define HELP_FILE               "CONTROL.HLP"
#define HELP_PASSWORD           9000

/* what the saver provides */
LRESULT FAR PASCAL __export ScreenSaverProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL __export ScreenSaverConfigureDialog(HWND, UINT, WPARAM, LPARAM);
BOOL WINAPI RegisterDialogClasses(HINSTANCE);

/* what the library provides */
extern HINSTANCE hMainInstance;
extern HWND hMainWindow;
extern UINT MyHelpMessage;              /* posted to a saver dialog on F1 */
extern char szAppName[];                /* idsAppName's string */
extern char szIniFile[];                /* "CONTROL.INI" */
LRESULT WINAPI DefScreenSaverProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL __export DlgChangePassword(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL __export DlgGetPassword(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL __export DlgInvalidPassword(HWND, UINT, WPARAM, LPARAM);
DWORD FAR PASCAL __export HelpMessageFilterHookFunction(int, WORD, LPMSG);

/* helpers for savers */
int ss_get_int(const char *key, int def);               /* CONTROL.INI [szAppName] */
void ss_put_int(const char *key, int v);
void ss_set_password(HWND dlg);                         /* the Set Password... button */
void ss_register_arrow(HINSTANCE);                      /* the "cpArrow" spin control */
void ss_encode_password(char *s);                       /* in place, uppercased first */
unsigned ss_rand(void);                                 /* 0..32767 */

#endif
