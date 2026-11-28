/*
 * us40.c - probe US40: the case of the names LB_DIR (and CB_DIR) put in a
 * list box. Found with Adobe Type Manager 3.0's installer: it lists the
 * font disk's metric files with LB_DIR ("*.??m") and writes the names it
 * gets into ATM.INI's [Fonts] lines; on Windows 3.1 they are lower case
 * ("c:\psfonts\pfm\cob_____.pfm"), on w16 they were DOS's capitals.
 *
 * The probe makes C:\US40DIR with a file MIXED.TXT, a file NODOT and a
 * directory SUBDIR.X (DOS keeps every name in capitals) and logs every
 * item of an unsorted list box after LB_DIR "C:\US40DIR\*.*" with
 * DDL_DIRECTORY (unsorted.N: the order LB_DIR adds them in), of a sorted
 * one (dir.N), the sorted one after LB_ADDSTRING of "zzz", "[aaa]",
 * "[-b-]", "_x" and "1a" (add.N), and of a sorted combo box after CB_DIR (cb.N); then the drive items
 * of LB_DIR with DDL_DRIVES | DDL_EXCLUSIVE, the first one only (drv.first:
 * the drives differ between set-ups). Run as the shell on 3.1 and with
 * w16's USER, the logs equal (boot-test case lbdir).
 */
#include <windows.h>
#include <dos.h>

extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define TK_REF "us40ref.h"
#include "tklog.h"

static void items(HWND w, const char *tag, UINT count, UINT gettext, int max)
{
    char b[64];
    int n = (int)SendMessage(w, count, 0, 0), i;
    out("OBS ");
    out(tag);
    out(".count=");
    outhex((WORD)n);
    out("\r\n");
    for (i = 0; i < n && i < max; i++) {
        b[0] = 0;
        SendMessage(w, gettext, i, (LPARAM)(LPSTR)b);
        out("OBS ");
        out(tag);
        out(".");
        outhex((WORD)i);
        out("=\"");
        out(b);
        out("\"\r\n");
    }
}

/* INT 21h 39h: make a directory */
static void dos_mkdir(const char __far *p);
#pragma aux dos_mkdir = "push ds" "push es" "pop ds" "mov ah, 39h" "int 21h" "pop ds" \
    parm [es dx] modify [ax];

static void mkfile(const char *name)
{
    OFSTRUCT of;
    HFILE h = OpenFile(name, &of, OF_CREATE | OF_WRITE);
    if (h != HFILE_ERROR)
        _lclose(h);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    HWND top, lb, cb, ub;
    out("US40 start\r\n");
    InitApp(hi);                        /* a message queue (no C runtime) */
    dos_mkdir("C:\\US40DIR");
    dos_mkdir("C:\\US40DIR\\SUBDIR.X");
    mkfile("C:\\US40DIR\\MIXED.TXT");
    mkfile("C:\\US40DIR\\NODOT");
    /* the boxes in a window of their own (a combo box sends its parent
       messages) */
    top = CreateWindow("STATIC", "", WS_OVERLAPPED, 0, 0, 200, 200, 0, 0, hi, NULL);
    lb = CreateWindow("LISTBOX", "", WS_CHILD | LBS_SORT, 0, 0, 100, 100, top, (HMENU)1, hi, NULL);
    cb = CreateWindow("COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST | CBS_SORT, 0, 0, 100, 100,
                      top, (HMENU)2, hi, NULL);
    ub = CreateWindow("LISTBOX", "", WS_CHILD, 0, 0, 100, 100, top, (HMENU)3, hi, NULL);
    SendMessage(ub, LB_DIR, DDL_DIRECTORY, (LPARAM)(LPSTR)"C:\\US40DIR\\*.*");
    items(ub, "unsorted", LB_GETCOUNT, LB_GETTEXT, 16);
    SendMessage(lb, LB_DIR, DDL_DIRECTORY, (LPARAM)(LPSTR)"C:\\US40DIR\\*.*");
    items(lb, "dir", LB_GETCOUNT, LB_GETTEXT, 16);
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"zzz");
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"[aaa]");
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"[-b-]");
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"_x");
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"1a");
    items(lb, "add", LB_GETCOUNT, LB_GETTEXT, 16);
    SendMessage(cb, CB_DIR, DDL_DIRECTORY, (LPARAM)(LPSTR)"C:\\US40DIR\\*.*");
    items(cb, "cb", CB_GETCOUNT, CB_GETLBTEXT, 16);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    SendMessage(lb, LB_DIR, DDL_DRIVES | DDL_EXCLUSIVE, (LPARAM)(LPSTR)"*.*");
    {
        char b[16];
        b[0] = 0;
        SendMessage(lb, LB_GETTEXT, 0, (LPARAM)(LPSTR)b);
        out("OBS drv.first=\"");
        out(b);
        out("\"\r\n");
    }
    DestroyWindow(top);
    /* the order the sorted boxes show, against lstrcmpi's and lstrcmp's */
    out("OBS cmpi.br=");
    outhex((WORD)lstrcmpi("[a]", "z"));
    out("\r\nOBS cmp.br=");
    outhex((WORD)lstrcmp("[a]", "z"));
    out("\r\nOBS cmpi.us=");
    outhex((WORD)lstrcmpi("_a", "z"));
    out("\r\n");
    out("US40 end\r\n");
    return failures;
}
