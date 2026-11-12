/*
 * dirlist.c - file lists in list and combo boxes: LB_DIR (CB_DIR),
 * DlgDirList, DlgDirListComboBox, DlgDirSelect(Ex) and
 * DlgDirSelectComboBox(Ex).
 *
 * After the Windows 3.1 SDK: LB_DIR adds the files matching a file
 * specification with the given attributes (DDL_*; read-write files unless
 * DDL_EXCLUSIVE), subdirectories as [name] (DDL_DIRECTORY) and drives as
 * [-x-] (DDL_DRIVES), and returns the index of the last item added;
 * DlgDirList changes to the drive and directory a path names, leaves only
 * the file specification in the caller's buffer, fills the list box and
 * shows the current directory in a static control. COMMDLG's File Open
 * box calls DlgDirList for its "Directories:" path (Windows Write's File >
 * Open showed it empty on w16, "c:\windows" on 3.1: lowercase).
 *
 * UNVERIFIED: the case of the names LB_DIR adds (as DOS gives them, in
 * capitals); the order (as found, the list box sorts if LBS_SORT).
 */
#include "user.h"

#pragma pack(1)
typedef struct {
    BYTE  reserved[21];
    BYTE  attr;
    WORD  time, date;
    DWORD size;
    char  name[13];
} FINDBUF;
#pragma pack()

/* DOS through INT 21h (DOSX passes these through from protected mode) */
static BYTE dos_getdrive(void);
#pragma aux dos_getdrive = "mov ah, 19h" "int 21h" value [al] modify [ax];
static void dos_setdrive(BYTE d);
#pragma aux dos_setdrive = "mov ah, 0Eh" "int 21h" parm [dl] modify [ax];
static WORD dos_chdir(const char __far *p);
#pragma aux dos_chdir = "push ds" "mov ds, cx" "mov ah, 3Bh" "int 21h" "sbb ax, ax" "pop ds" \
    parm [cx dx] value [ax];
static WORD dos_getcwd(BYTE drive, char __far *buf);
#pragma aux dos_getcwd = "push ds" "mov ds, cx" "mov ah, 47h" "int 21h" "sbb ax, ax" "pop ds" \
    parm [dl] [cx si] value [ax];
static void __far *dos_getdta(void);
#pragma aux dos_getdta = "push es" "mov ah, 2Fh" "int 21h" "mov dx, es" "mov ax, bx" "pop es" \
    value [dx ax] modify [bx];
static void dos_setdta(void __far *p);
#pragma aux dos_setdta = "push ds" "mov ds, cx" "mov ah, 1Ah" "int 21h" "pop ds" \
    parm [cx dx] modify [ax];
static WORD dos_findfirst(const char __far *spec, WORD attr);
#pragma aux dos_findfirst = "push ds" "mov ds, bx" "mov ah, 4Eh" "int 21h" "sbb ax, ax" "pop ds" \
    parm [bx dx] [cx] value [ax];
static WORD dos_findnext(void);
#pragma aux dos_findnext = "mov ah, 4Fh" "int 21h" "sbb ax, ax" value [ax];

static BOOL drive_exists(BYTE d);

static BOOL has(const char *s, char c)
{
    for (; *s; s++)
        if (*s == c)
            return TRUE;
    return FALSE;
}

/* "X:" at the start of path: make X current (FALSE if there is none) */
static BOOL to_drive(const char *path)
{
    char d;
    if (path[0] == 0 || path[1] != ':')
        return TRUE;
    d = path[0];
    if (d >= 'a' && d <= 'z')
        d -= 'a' - 'A';
    if (d < 'A' || d > 'Z' || !drive_exists((BYTE)(d - 'A')))
        return FALSE;
    dos_setdrive((BYTE)(d - 'A'));
    return TRUE;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s += 'a' - 'A';
}

/* a drive is there if DOS makes it current */
static BOOL drive_exists(BYTE d)
{
    BYTE cur = dos_getdrive(), now;
    dos_setdrive(d);
    now = dos_getdrive();
    dos_setdrive(cur);
    return now == d;
}

/* LB_DIR: returns the index of the last item added, LB_ERR if none or
   on an error */
LRESULT lb_dir(HWND lb, UINT attr, LPCSTR spec)
{
    FINDBUF fb;
    void __far *olddta;
    char item[20];
    LRESULT last = LB_ERR, r;
    WORD dosattr = (attr & (DDL_READONLY | DDL_HIDDEN | DDL_SYSTEM | DDL_ARCHIVE)) |
                   ((attr & DDL_DIRECTORY) ? 0x10 : 0);
    if (!spec || !*spec)
        spec = "*.*";
    olddta = dos_getdta();
    dos_setdta((void __far *)&fb);
    if (attr != DDL_DRIVES && attr != (DDL_DRIVES | DDL_EXCLUSIVE) &&
        !dos_findfirst(spec, dosattr)) {
        do {
            BYTE a = fb.attr;
            BOOL dir = (a & 0x10) != 0;
            BOOL take;
            if (dir) {
                take = (attr & DDL_DIRECTORY) && lstrcmp(fb.name, ".");
            } else if (attr & DDL_EXCLUSIVE) {
                take = (a & attr & (DDL_READONLY | DDL_HIDDEN | DDL_SYSTEM | DDL_ARCHIVE)) != 0;
            } else {
                /* read-write files always; hidden and system ones only if
                   asked for */
                take = !(a & (DDL_HIDDEN | DDL_SYSTEM)) ||
                       (a & attr & (DDL_HIDDEN | DDL_SYSTEM));
            }
            if (!take)
                continue;
            if (dir) {
                item[0] = '[';
                lstrcpy(item + 1, fb.name);
                lstrcat(item, "]");
            } else
                lstrcpy(item, fb.name);
            r = send_message(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
            if (r < 0) {
                last = r;
                break;
            }
            last = r;
        } while (!dos_findnext());
    }
    dos_setdta(olddta);
    if (attr & DDL_DRIVES) {
        BYTE d;
        for (d = 0; d < 26; d++)
            if (drive_exists(d)) {
                item[0] = '[', item[1] = '-', item[2] = (char)('a' + d);
                item[3] = '-', item[4] = ']', item[5] = 0;
                r = send_message(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
                if (r < 0)
                    break;
                last = r;
            }
    }
    return last;
}

/* the path part of spec: change to it; spec keeps the file part. FALSE if
   the path is not there */
static BOOL go_to(LPSTR spec)
{
    char path[128], *p, *cut = NULL;
    int n = lstrlen(spec);
    if (n >= (int)sizeof path)
        return FALSE;
    lstrcpy(path, spec);
    /* a directory named without a file part: all its files */
    if (n && !has(path, '*') && !has(path, '?')) {
        BYTE cur = dos_getdrive();
        if (to_drive(path) && (path[1] == ':' && !path[2] ? 0 : dos_chdir(path)) == 0) {
            lstrcpy(spec, "*.*");
            return TRUE;
        }
        dos_setdrive(cur);
    }
    for (p = path; *p; p++)
        if (*p == '\\' || *p == ':')
            cut = p;
    if (!cut)
        return TRUE;
    if (!to_drive(path))
        return FALSE;
    lstrcpy(spec, cut + 1);
    if (*cut == ':')
        return TRUE;
    if (cut == path + 2 && path[1] == ':')      /* X:\file */
        cut++;
    else if (cut == path)                       /* \file */
        cut++;
    *cut = 0;
    {
        char *dir = path[1] == ':' ? path + 2 : path;
        if (*dir && dos_chdir(dir) != 0)
            return FALSE;
    }
    return TRUE;
}

static void show_path(HWND dlg, int idStatic)
{
    char cwd[80];
    BYTE d = dos_getdrive();
    cwd[0] = (char)('A' + d);
    cwd[1] = ':';
    cwd[2] = '\\';
    if (dos_getcwd(0, cwd + 3) != 0)
        cwd[3] = 0;
    lower(cwd);
    SetDlgItemText(dlg, idStatic, cwd);
}

static int dir_list(HWND dlg, LPSTR spec, int idList, int idStatic, UINT attr, BOOL combo)
{
    char buf[128];
    LPSTR s = spec ? spec : buf;
    if (!spec)
        lstrcpy(buf, "*.*");
    if (!go_to(s))
        return FALSE;
    if (idList) {
        HWND lb = GetDlgItem(dlg, idList);
        if (!lb)
            return FALSE;
        send_message(lb, combo ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
        send_message(lb, combo ? CB_DIR : LB_DIR, attr & ~DDL_POSTMSGS, (LPARAM)s);
    }
    if (idStatic)
        show_path(dlg, idStatic);
    return TRUE;
}

int WINAPI DlgDirList(HWND dlg, LPSTR spec, int idList, int idStatic, UINT attr)
{
    return dir_list(dlg, spec, idList, idStatic, attr, FALSE);
}

int WINAPI DlgDirListComboBox(HWND dlg, LPSTR spec, int idList, int idStatic, UINT attr)
{
    return dir_list(dlg, spec, idList, idStatic, attr, TRUE);
}

/* the selected item into buf: TRUE for a directory or drive (given as
   "name\" or "x:"), FALSE for a file */
static BOOL dir_select(HWND dlg, LPSTR buf, int n, int idList, BOOL combo)
{
    char item[20];
    HWND lb = GetDlgItem(dlg, idList);
    LRESULT i;
    int len;
    if (!lb || n <= 0)
        return FALSE;
    i = send_message(lb, combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0);
    if (i < 0)
        return FALSE;
    if (send_message(lb, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, (WPARAM)i, 0) >= (LRESULT)sizeof item)
        return FALSE;
    send_message(lb, combo ? CB_GETLBTEXT : LB_GETTEXT, (WPARAM)i, (LPARAM)(LPSTR)item);
    len = lstrlen(item);
    if (len >= 5 && item[0] == '[' && item[1] == '-' && item[len - 1] == ']') {
        item[0] = item[2];
        item[1] = ':';
        item[2] = 0;
        lstrcpyn(buf, item, n);
        return TRUE;
    }
    if (len >= 2 && item[0] == '[' && item[len - 1] == ']') {
        item[len - 1] = '\\';
        lstrcpyn(buf, item + 1, n);
        return TRUE;
    }
    lstrcpyn(buf, item, n);
    return FALSE;
}

BOOL WINAPI DlgDirSelect(HWND dlg, LPSTR buf, int idList)
{
    return dir_select(dlg, buf, 128, idList, FALSE);
}

BOOL WINAPI DlgDirSelectEx(HWND dlg, LPSTR buf, int n, int idList)
{
    return dir_select(dlg, buf, n, idList, FALSE);
}

BOOL WINAPI DlgDirSelectComboBox(HWND dlg, LPSTR buf, int idList)
{
    return dir_select(dlg, buf, 128, idList, TRUE);
}

BOOL WINAPI DlgDirSelectComboBoxEx(HWND dlg, LPSTR buf, int n, int idList)
{
    return dir_select(dlg, buf, n, idList, TRUE);
}
