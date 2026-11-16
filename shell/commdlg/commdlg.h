/*
 * commdlg.h - w16 COMMDLG.DLL, the common dialogs (internal header).
 *
 * A clean-room reimplementation of Windows 3.1's common dialog library from
 * the Windows 3.1 SDK's documentation and black-box observation of the
 * original (tests/apps/cd1.c, tests/commdlgscen.sh; ARCHITECTURE.md 17).
 * Large model, SS != DS (the code runs on its callers' stacks); every
 * entry point and callback loads DS itself (__loadds). Windows 3.x only:
 * w16 builds no Windows 2.x COMMDLG (2.x builds are Program Manager's).
 */
#ifndef W16_COMMDLG_H
#define W16_COMMDLG_H

#include <windows.h>
#include <commdlg.h>
#include <cderr.h>
#include <dlgs.h>
#include "cdlgs.h"

/* exported entry points and callbacks (commdlg.lnk names them) */
#define CDAPI   __far __pascal __loadds
#define CDPROC  __far __pascal __loadds

/* registered messages (Windows 3.1 SDK commdlg.h; observed: the hooks
   and owners of the original receive exactly these, tests/apps/cd1.c) */
#define S_LBSELCH       "commdlg_LBSelChangedNotify"
#define S_SHAREVI       "commdlg_ShareViolation"
#define S_FILEOK        "commdlg_FileNameOK"
#define S_COLOROK       "commdlg_ColorOK"
#define S_SETRGB        "commdlg_SetRGBColor"
#define S_FINDMSG       "commdlg_FindReplace"
#define S_HELP          "commdlg_help"

/* the window property holding a dialog's state (observed string 1056) */
#define PROP_STATE      "DlgInstData"

/* A DOS path. DOS bounds it: a drive, 64 bytes of directory, a 12-letter
   name and the NUL; OFSTRUCT's szPathName holds 128. */
#define DOS_PATH        144

/* ---- util.c ---- */
extern HINSTANCE cd_inst;
extern UINT msg_lbselch, msg_sharevi, msg_fileok, msg_colorok, msg_setrgb,
            msg_findmsg, msg_help;

void    cd_seterr(DWORD e);
DWORD   cd_geterr(void);

LPVOID  mem_alloc(DWORD n);             /* zeroed; NULL on failure */
LPVOID  mem_realloc(LPVOID p, DWORD n);
void    mem_free(LPVOID p);
void    mem_zero(LPVOID p, WORD n);
void    mem_copy(LPVOID d, const void FAR *s, WORD n);

int     s_len(LPCSTR s);
void    s_cpy(LPSTR d, LPCSTR s, int size);     /* truncating, NUL-ended */
void    s_cat(LPSTR d, LPCSTR s, int size);
LPSTR   s_dup(LPCSTR s);
int     s_icmp(LPCSTR a, LPCSTR b);
LPSTR   s_chr(LPCSTR s, char c);
LPSTR   s_rchr(LPCSTR s, char c);
void    s_lower(LPSTR s);               /* ASCII letters only */
void    s_upper(LPSTR s);
int     s_atoi(LPCSTR s, BOOL FAR *ok); /* decimal, ok: digits only */
void    s_itoa(int v, LPSTR d);

LPSTR   load_str(UINT ids);             /* new string; "" if absent */
int     cd_msgbox(HWND owner, LPCSTR caption, UINT ids, LPCSTR arg, UINT style);
int     cd_msgbox_c(HWND owner, LPCSTR caption, UINT ids, char drive, UINT style);

/* run a common dialog: picks the template (the application's, by handle
   or by name, or ours `ours`), sets CDERR_* on failure; modeless returns
   the window, modal the EndDialog value (-1: failure) */
typedef struct {
    HINSTANCE app;                      /* hInstance from the structure */
    BOOL use_handle, use_name;
    LPCSTR name;                        /* lpTemplateName */
    LPCSTR ours;                        /* our template's name */
} cd_template;
int     cd_modal(const cd_template FAR *t, HWND owner, FARPROC proc, LPARAM lp);
HWND    cd_modeless(const cd_template FAR *t, HWND owner, FARPROC proc, LPARAM lp);

/* the state of a dialog, a property of its window */
void    state_set(HWND dlg, LPVOID st);
LPVOID  state_get(HWND dlg);
void    state_remove(HWND dlg);

/* call the hook (if any) of a dialog */
typedef UINT (CALLBACK *cd_hook)(HWND, UINT, WPARAM, LPARAM);
UINT    call_hook(cd_hook h, HWND dlg, UINT msg, WPARAM wp, LPARAM lp);

/* Help pressed: HELPMSGSTRING to the owner (wParam: the dialog) */
void    send_help(HWND dlg, HWND owner, LPARAM lp);

/* DOS (int 21h) */
int     dos_getdrive(void);             /* 0 = A: */
void    dos_setdrive(int d);
BOOL    dos_chdir(LPCSTR path);
BOOL    dos_getcwd(int drive, LPSTR buf);       /* drive 1 = A:; "DIR\SUB" */
BOOL    dos_drive_exists(int d);
int     dos_findfirst(LPCSTR spec, int attr, LPVOID dta);
int     dos_findnext(LPVOID dta);
int     dos_getattr(LPCSTR path);       /* -1: error, dos_err() why */
int     dos_err(void);
int     dos_creat_new(LPCSTR path);     /* 0 ok, else DOS error */
void    dos_delete(LPCSTR path);
int     dos_open_test(LPCSTR path, int mode);   /* 0 ok, else DOS error */
LPSTR   cur_dir(void);                  /* "C:\DIR\SUB", new string */

typedef struct {                        /* the DOS find buffer (DTA) */
    BYTE reserved[21];
    BYTE attr;
    WORD time, date;
    DWORD size;
    char name[13];
} dos_dta;

/* ---- filelist.c: drawing the file dialogs' lists ---- */
HBITMAP CDAPI LoadAlterBitmap(int id, DWORD rgbReplace, DWORD rgbInstead);
void    pics_load(void);
void    pics_free(void);
int     drive_kind(int d);              /* PIC_FLOPPY.. or -1: none */
#define PIC_OPEN        0               /* the cells of the picture strip */
#define PIC_OPENCUR     1
#define PIC_CLOSED      2
#define PIC_FLOPPY      3
#define PIC_HARD        4
#define PIC_CDROM       5
#define PIC_NET         6
#define PIC_RAM         7
void    draw_dir_item(const DRAWITEMSTRUCT FAR *d);
void    draw_drive_item(const DRAWITEMSTRUCT FAR *d);
void    draw_file_item(const DRAWITEMSTRUCT FAR *d, BOOL save);
void    measure_item(HWND dlg, MEASUREITEMSTRUCT FAR *m);

/* ---- the dialogs' procedures (exported) ---- */
BOOL CDPROC FileOpenDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC FileSaveDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC FindTextDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC ReplaceTextDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC ColorDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC FormatCharDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC PrintDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL CDPROC PrintSetupDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CDPROC dwLBSubclass(HWND, UINT, WPARAM, LPARAM);
LRESULT CDPROC dwOKSubclass(HWND, UINT, WPARAM, LPARAM);
LRESULT CDPROC dwUpArrowHack(HWND, UINT, WPARAM, LPARAM);
LRESULT CDPROC WantArrows(HWND, UINT, WPARAM, LPARAM);
LRESULT CDPROC EditIntegerOnly(HWND, UINT, WPARAM, LPARAM);
int CDPROC FontFamilyEnumProc(LPLOGFONT, LPTEXTMETRIC, int, LPARAM);
int CDPROC FontStyleEnumProc(LPLOGFONT, LPTEXTMETRIC, int, LPARAM);

/* subclassing helpers: the original procedure of a subclassed control */
void    subclass(HWND w, FARPROC proc);
LRESULT call_old(HWND w, UINT msg, WPARAM wp, LPARAM lp);

void    cd_init_color(void);
void    cd_init_font(void);


#endif
