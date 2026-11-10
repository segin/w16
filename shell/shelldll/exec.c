/*
 * exec.c - w16 SHELL.DLL: FindExecutable (SHELL.21), ShellExecute
 * (SHELL.20) and the "Program Not Found" dialog (FindExeDlgProc,
 * SHELL.101).
 *
 * Observed on Windows 3.1 (probes SH4 and SH11, with SH5 as the program
 * that logs what it was started with):
 * - A file without an extension is a program with ".EXE" added; a file
 *   whose extension WIN.INI [windows] Programs= lists is its own program.
 *   The file is looked for as OpenFile does (in dir, when given, which is
 *   also the started program's current directory); not found gives
 *   OpenFile's error (2, or 3 for a missing directory).
 * - A document's association: the registration database first (".ext"
 *   gives a class; class\shell\<verb>\command the command; %1 becomes the
 *   document's full path, %2 the parameters (UNVERIFIED for %3-%9), %%
 *   a %, % and another character that character; without %1 nothing is
 *   added). A class without the command is "incomplete" (27), unless the
 *   verb is "open" and WIN.INI [extensions] has the extension: that line
 *   ("program args ^.ext", ^ standing for the document's path without its
 *   extension; parameters ignored) serves the "open" verb only. Neither:
 *   no association (31).
 * - FindExecutable returns 1000 when it succeeds (seen in both modes), the
 *   program as the command names it (its first word, not searched for).
 * - When the program of a registered class cannot be started, SHELL asks
 *   for it: WIN.INI [programs] NAME=path first, then the dialog, which
 *   writes [programs] when the directory given holds NAME. Cancel: 15.
 *   With a WIN.INI-only association the WinExec error comes back.
 * UNVERIFIED: ddeexec (SHELL's DDE errors 28-30) is not implemented.
 */
#include "shell.h"

#define FOUND_RESULT    1000            /* FindExecutable (SH11) */
#define CANCEL_RESULT   15              /* the dialog cancelled (SH11) */

typedef struct {
    char full[160];             /* the document or program, full path */
    char cmd[512];              /* the command line */
    char prog[160];             /* the command's program (first word) */
    char regprog[160];          /* the registered command's program */
    char ext[8];
    int is_program;
    int registered;             /* the class came from the database */
} JOB;

static WORD dos_drive(void);
#pragma aux dos_drive = "mov ah, 19h" "int 21h" "xor ah, ah" value [ax];
static void dos_setdrive(WORD d);
#pragma aux dos_setdrive = "mov ah, 0Eh" "int 21h" parm [dx] modify [ax];
static WORD dos_getdir(char __far *p);
#pragma aux dos_getdir = "push ds" "mov ds, dx" "mov ah, 47h" "xor dl, dl" "int 21h" \
    "sbb ax, ax" "pop ds" parm [dx si] value [ax];
static WORD dos_chdir(const char __far *p);
#pragma aux dos_chdir = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 3Bh" "int 21h" \
    "sbb ax, ax" "pop ds" parm [dx ax] value [ax];

typedef struct { WORD drive; char dir[80]; int changed; } CWD;

static void enter_dir(CWD *c, LPCSTR dir)
{
    c->changed = 0;
    if (!dir || !*dir)
        return;
    c->drive = dos_drive();
    c->dir[0] = '\\';
    if (dos_getdir(c->dir + 1))
        return;
    if (dir[1] == ':')
        dos_setdrive((sh_upper(dir[0]) - 'A') & 0x1F);
    if (dos_chdir(dir) && dir[1] == ':' && dir[2] == 0) {
        /* "C:" alone: the drive only */
    }
    c->changed = 1;
}

static void leave_dir(CWD *c)
{
    if (!c->changed)
        return;
    dos_setdrive(c->drive);
    dos_chdir(c->dir);
}

static int same_verb(LPCSTR op)
{
    return !op || !*op || !lstrcmpi(op, "open");
}

/* the command's first word */
static void first_word(char __far *out, const char __far *cmd, WORD cb)
{
    WORD n = 0;
    while (*cmd == ' ')
        cmd++;
    while (cmd[n] && cmd[n] != ' ' && n + 1 < cb) {
        out[n] = cmd[n];
        n++;
    }
    out[n] = 0;
}

static void append(char __far *out, WORD *n, const char __far *s, WORD cb)
{
    while (s && *s && *n + 1 < cb)
        out[(*n)++] = *s++;
    out[*n] = 0;
}

/* the association of a document: 0, or SE_ERR_* */
static int association(JOB *j, LPCSTR op, LPCSTR params, int verbonly)
{
    char cls[80], key[160], raw[256];
    LONG cb = sizeof cls;
    WORD n = 0;
    const char __far *s;
    (void)verbonly;
    j->registered = 0;
    j->regprog[0] = 0;
    raw[0] = 0;
    key[0] = '.';
    lstrcpy(key + 1, j->ext);
    if (!reg_query(HKEY_CLASSES_ROOT, key, cls, &cb) && cls[0]) {
        LONG cb2 = sizeof raw;
        j->registered = 1;
        wsprintf(key, "%s\\shell\\%s\\command", (LPSTR)cls, (LPSTR)(op && *op ? op : "open"));
        if (!reg_query(HKEY_CLASSES_ROOT, key, raw, &cb2) && raw[0]) {
            first_word(j->prog, raw, sizeof j->prog);
            lstrcpy(j->regprog, j->prog);
            for (s = raw; *s; s++) {
                if (*s != '%') {
                    if (n + 1 < sizeof j->cmd)
                        j->cmd[n++] = *s;
                    continue;
                }
                s++;
                if (*s == '1')
                    append(j->cmd, &n, j->full, sizeof j->cmd);
                else if (*s == '2')
                    append(j->cmd, &n, params, sizeof j->cmd);
                else if (*s >= '3' && *s <= '9')
                    ;
                else if (*s && n + 1 < sizeof j->cmd)
                    j->cmd[n++] = *s;
                if (!*s)
                    break;
            }
            j->cmd[n] = 0;
            return 0;
        }
        if (!same_verb(op))
            return SE_ERR_ASSOCINCOMPLETE;
        /* the class's command is missing: WIN.INI can still answer, but
           the program asked for later is the (empty) registered one */
    }
    if (!same_verb(op))
        return SE_ERR_NOASSOC;
    if (!GetProfileString("Extensions", j->ext, "", raw, sizeof raw) || !raw[0])
        return j->registered ? SE_ERR_ASSOCINCOMPLETE : SE_ERR_NOASSOC;
    for (s = raw; *s; s++) {
        if (*s == '^') {
            /* the document's path without its extension */
            const char __far *e = sh_ext(j->full);
            WORD k = e ? (WORD)(e - (const char __far *)j->full) - 1 : sh_len(j->full), i;
            for (i = 0; i < k && n + 1 < sizeof j->cmd; i++)
                j->cmd[n++] = j->full[i];
        } else if (n + 1 < sizeof j->cmd) {
            j->cmd[n++] = *s;
        }
    }
    j->cmd[n] = 0;
    first_word(j->prog, raw, sizeof j->prog);
    return 0;
}

/* resolve file into j: 0 or an error (2, 3, 27, 31) */
static int prepare(JOB *j, LPCSTR file, LPCSTR op, LPCSTR params)
{
    OFSTRUCT of;
    char name[160];
    const char __far *e;
    j->cmd[0] = j->prog[0] = 0;
    if (!file || !*file)
        return 2;
    lstrcpyn(name, file, sizeof name - 4);
    e = sh_ext(name);
    if (!e || !*e) {
        WORD n = sh_len(name);
        if (n && name[n - 1] != '.')
            lstrcat(name, ".");
        lstrcat(name, "EXE");
        e = sh_ext(name);
    }
    if (OpenFile(name, &of, OF_EXIST) == HFILE_ERROR)
        return of.nErrCode ? of.nErrCode : 2;
    OemToAnsi(of.szPathName, j->full);
    lstrcpyn(j->ext, sh_ext(j->full) ? sh_ext(j->full) : "", sizeof j->ext);
    j->is_program = sh_is_program(j->full);
    if (j->is_program)
        return 0;
    return association(j, op, params, 0);
}

HINSTANCE find_exec(LPCSTR file, LPCSTR dir, LPSTR result, int verbonly)
{
    JOB j;
    CWD c;
    int r;
    (void)verbonly;
    enter_dir(&c, dir);
    r = prepare(&j, file, NULL, NULL);
    leave_dir(&c);
    if (result)
        result[0] = 0;
    if (r)
        return (HINSTANCE)r;
    if (result)
        lstrcpy(result, j.is_program ? j.full : j.prog);
    return (HINSTANCE)FOUND_RESULT;
}

HINSTANCE SHAPI FindExecutable(LPCSTR file, LPCSTR dir, LPSTR result)
{
    return find_exec(file, dir, result, 0);
}

/* ---- the Program Not Found dialog ---- */

typedef struct {
    char name[80];              /* NAME.EXT of the program */
    char ext[8];                /* the document's extension, upper case */
    char found[160];            /* the path chosen */
} FINDEXE;

static FINDEXE __far *fe;

typedef BOOL (FAR PASCAL *GETOPENFN)(void FAR *);

/* Locate...: COMMDLG's Open dialog with SHELL's template 300 (a
   directory chooser: Windows 3.1 shows it, SH8); the directory chosen
   goes into the edit field. UNVERIFIED: the exact flags and filter. */
static void locate(HWND dlg)
{
    HINSTANCE cd;
    GETOPENFN gofn;
    struct {
        DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCSTR lpstrFilter;
        LPSTR lpstrCustomFilter; DWORD nMaxCustFilter; DWORD nFilterIndex; LPSTR lpstrFile;
        DWORD nMaxFile; LPSTR lpstrFileTitle; DWORD nMaxFileTitle; LPCSTR lpstrInitialDir;
        LPCSTR lpstrTitle; DWORD Flags; UINT nFileOffset; UINT nFileExtension;
        LPCSTR lpstrDefExt; LPARAM lCustData; void FAR *lpfnHook; LPCSTR lpTemplateName;
    } ofn;
    char file[160], filter[100], msg[80];
    WORD n;
    UINT old = SetErrorMode(0x8000);
    cd = LoadLibrary("COMMDLG.DLL");
    SetErrorMode(old);
    if ((UINT)cd < 32) {
        LoadString(g_inst, IDS_NOCOMMDLG, msg, sizeof msg);
        MessageBox(dlg, msg, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    gofn = (GETOPENFN)GetProcAddress(cd, "GetOpenFileName");
    lstrcpy(file, fe->name);
    n = sh_len(fe->name);
    lstrcpy(filter, fe->name);
    lstrcpy(filter + n + 1, fe->name);
    filter[2 * n + 2] = 0;
    sh_fill(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = dlg;
    ofn.hInstance = g_inst;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.Flags = 0x00000040L | 0x00000004L | 0x00000800L;  /* ENABLETEMPLATE, HIDEREADONLY, PATHMUSTEXIST */
    ofn.lpTemplateName = MAKEINTRESOURCE(DLG_LOCATE);
    if (gofn && gofn(&ofn)) {
        file[ofn.nFileOffset ? ofn.nFileOffset - 1 : 0] = 0;
        SetDlgItemText(dlg, IDC_FE_EDIT, file);
    }
    FreeLibrary(cd);
}

BOOL SHAPI FindExeDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[300], f[200];
    switch (msg) {
    case WM_INITDIALOG:
        fe = (FINDEXE __far *)lp;
        GetDlgItemText(dlg, IDC_FE_TEXT, f, sizeof f);
        wsprintf(t, f, (LPSTR)fe->name, (LPSTR)fe->ext);
        SetDlgItemText(dlg, IDC_FE_TEXT, t);
        GetDlgItemText(dlg, IDC_FE_LOCLABEL, f, sizeof f);
        wsprintf(t, f, (LPSTR)fe->name);
        SetDlgItemText(dlg, IDC_FE_LOCLABEL, t);
        SetDlgItemText(dlg, IDC_FE_EDIT, "C:\\");
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            OFSTRUCT of;
            WORD n;
            GetDlgItemText(dlg, IDC_FE_EDIT, f, sizeof f - 82);
            n = sh_len(f);
            if (n && f[n - 1] != '\\')
                f[n++] = '\\';
            lstrcpy(f + n, fe->name);
            if (OpenFile(f, &of, OF_EXIST) == HFILE_ERROR) {
                char fmt[80], title[60];
                LoadString(g_inst, IDS_CANTFIND, fmt, sizeof fmt);
                wsprintf(t, fmt, (LPSTR)f);
                GetWindowText(dlg, title, sizeof title);
                MessageBox(dlg, t, title, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            lstrcpy(fe->found, f);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_FE_LOCATE:
            locate(dlg);
            return TRUE;
        case IDC_FE_HELP:
            /* UNVERIFIED: the help file and topic Windows 3.1 asks for */
            return TRUE;
        }
        break;
    }
    (void)lp;
    return FALSE;
}

/* replace the command's program (its first word) with path */
static void swap_program(JOB *j, const char *path)
{
    char rest[512];
    const char __far *s = j->cmd;
    while (*s == ' ')
        s++;
    while (*s && *s != ' ')
        s++;
    lstrcpy(rest, s);
    lstrcpy(j->cmd, path);
    lstrcat(j->cmd, rest);
}

HINSTANCE SHAPI ShellExecute(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, int show)
{
    JOB j;
    CWD c;
    int r;
    UINT h;
    enter_dir(&c, dir);
    r = prepare(&j, file, op, params);
    if (r) {
        leave_dir(&c);
        return (HINSTANCE)r;
    }
    if (j.is_program) {
        if (!same_verb(op)) {
            leave_dir(&c);
            return (HINSTANCE)SE_ERR_NOASSOC;
        }
        lstrcpy(j.cmd, file);
        if (params && *params) {
            lstrcat(j.cmd, " ");
            lstrcat(j.cmd, params);
        }
    }
    h = WinExec(j.cmd, show);
    if (h == 2 && j.registered) {
        FINDEXE f;
        const char __far *base = j.regprog, *s;
        char path[160];
        for (s = j.regprog; *s; s++)
            if (*s == '\\' || *s == ':')
                base = s + 1;
        lstrcpy(f.name, base);
        if (!sh_ext(f.name))
            lstrcat(f.name, ".exe");
        lstrcpy(f.ext, j.ext);
        AnsiUpper(f.ext);
        f.found[0] = 0;
        if (GetProfileString("programs", f.name, "", path, sizeof path) && path[0]) {
            swap_program(&j, path);
            h = WinExec(j.cmd, show);
        } else if (DialogBoxParam(g_inst, MAKEINTRESOURCE(DLG_FINDEXE), w,
                                  (DLGPROC)FindExeDlgProc, (LPARAM)(FINDEXE __far *)&f) == IDOK) {
            WriteProfileString("programs", f.name, f.found);
            swap_program(&j, f.found);
            h = WinExec(j.cmd, show);
        } else {
            h = CANCEL_RESULT;
        }
    }
    leave_dir(&c);
    return (HINSTANCE)h;
}
