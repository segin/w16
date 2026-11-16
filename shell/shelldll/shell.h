/*
 * shell.h - w16's SHELL.DLL (ARCHITECTURE.md 13): shared declarations.
 *
 * Large model, SS != DS (the DLL runs on its callers' stacks), no C
 * runtime. Every exported function is SHAPI: far pascal, loads DS.
 * Behaviour comes from black-box probes on Windows 3.1 (tests/apps/sh*.c,
 * results in ARCHITECTURE.md 13), the Windows 3.1 SDK documentation, and
 * the facts noted where they are used.
 */
#ifndef W16_SHELL_H
#define W16_SHELL_H

#include <windows.h>

#define SHAPI   __far __pascal __loadds

typedef DWORD HKEY;

/* registration database errors (SDK, shellapi.h) */
#define ERROR_SUCCESS           0L
#define ERROR_BADDB             1L
#define ERROR_BADKEY            2L
#define ERROR_CANTOPEN          3L
#define ERROR_CANTREAD          4L
#define ERROR_CANTWRITE         5L
#define ERROR_OUTOFMEMORY       6L
#define ERROR_INVALID_PARAMETER 7L
#define REG_SZ                  1
#define HKEY_CLASSES_ROOT       1L

/* ShellExecute errors (SDK) */
#define SE_ERR_ASSOCINCOMPLETE  27
#define SE_ERR_NOASSOC          31

/* resources (shell.rc) */
#define DLG_ABOUT       100
#define DLG_FINDEXE     200
#define DLG_LOCATE      300
#define BMP_LOGO        130
#define IDS_REGDAT      208
#define IDS_REALMODE    209
#define IDS_REALLARGE   210
#define IDS_REALSMALL   211
#define IDS_STANDARD    212
#define IDS_ENHANCED    213
#define IDS_RESOURCES   215
#define IDS_VERSION     216
#define IDS_DEBUG       217
#define IDS_KBFREE      218
#define IDS_KBFREEEMS   219
#define IDS_PCTFREE     220
#define IDS_CANTFIND    223
#define IDS_FOUND       224
#define IDS_NOTFOUND    225
#define IDS_NOCOMMDLG   226
#define IDS_CLOSENOOPEN 227
#define IDS_ABOUTFMT    240     /* w16: "About %s" */
#define IDS_LINE1FMT    241     /* w16: the first line, "<product> %s" */

/* the About box's controls (as on Windows 3.1, probe SH8) */
#define IDC_ABICON        0x6F
#define IDC_LINE1       0x65
#define IDC_VERSION     0x70
#define IDC_OTHER       0x73
#define IDC_USER        0x6C
#define IDC_ORG         0x6D
#define IDC_SERIAL      0x6E
#define IDC_MODE        0x66
#define IDC_MEMLABEL    0x67
#define IDC_MEM         0x68
#define IDC_RESLABEL    0x71
#define IDC_RES         0x69
#define IDC_SMARTLABEL  0x6A
#define IDC_SMART       0x6B
/* the Program Not Found dialog's */
#define IDC_FE_TEXT     0xCD
#define IDC_FE_LOCLABEL 0xCE
#define IDC_FE_EDIT     0xCB
#define IDC_FE_LOCATE   0xC9
#define IDC_FE_HELP     0xCC

extern HINSTANCE g_inst;

/* undocumented imports (Schulman et al., Undocumented Windows, ch. 5-6;
   each confirmed by use on Windows 3.1) */
extern HGLOBAL FAR PASCAL DirectResAlloc(HINSTANCE, WORD, WORD);    /* KERNEL.168 */
extern WORD FAR PASCAL GetIconID(HGLOBAL, DWORD);                   /* USER.455 */
extern HICON FAR PASCAL LoadIconHandler(HGLOBAL, BOOL);             /* USER.456 */
extern WORD __cdecl get_ahincr(void);                               /* entry.asm */

/* util.c */
void sh_move(void __far *dst, const void __far *src, WORD n);
void sh_fill(void __far *dst, BYTE v, WORD n);
BYTE sh_upper(BYTE c);
int sh_same_ci(const char __far *a, const char __far *b, WORD n);
int sh_same(const char __far *a, const char __far *b, WORD n);
WORD sh_len(const char __far *s);
void sh_copyn(char __far *dst, const char __far *src, WORD cb);
HGLOBAL sh_alloc(DWORD size);
void __far *sh_lock(HGLOBAL h);
char __far *sh_strdup(const char __far *s, HGLOBAL *ph);
int sh_vsprintf(char __far *out, const char __far *fmt, const void __far *args);
int sh_sprintf1(char __far *out, const char __far *fmt, const char __far *a);
int sh_sprintf2(char __far *out, const char __far *fmt, const char __far *a,
                const char __far *b);
void sh_commas(char __far *out, DWORD v);
const char __far *sh_ext(const char __far *path);
int sh_is_program(const char __far *path);

/* word stack in global memory (no fixed depth) */
typedef struct { HGLOBAL h; WORD __far *p; WORD n, cap; } WSTACK;
int ws_push(WSTACK *s, WORD v);
WORD ws_pop(WSTACK *s);
void ws_free(WSTACK *s);

/* reg.c */
void reg_init(void);
LONG reg_query(HKEY k, LPCSTR sub, LPSTR buf, LONG __far *cb);

/* exec.c */
HINSTANCE find_exec(LPCSTR file, LPCSTR dir, LPSTR result, int verbonly);

/* icon.c */
HICON extract_icon(HINSTANCE inst, LPCSTR file, UINT index);

/* hook.c */
void hook_init(void);

#endif
