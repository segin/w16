/*
 * tk70.c - TK70.EXE: FindResource by name through a name table (resource
 * type 15, RT_NAMETABLE; Pietrek and Schulman list the type as
 * "eliminated in Windows 3.1 NE files", but programs linked for Windows
 * 3.0 have one: the Entertainment Pack setups ask for their dialogs by
 * the names in it, and on w16 DialogBox failed, 2026-11-25). Its
 * resources (tk70.rc, the table from tk70nt.py): RCDATA 1 "one", 2 "two",
 * 3 "three", RCDATA "REAL" "real", DIALOG 1, and table entries
 *   FIRST -> RCDATA 1, second -> RCDATA 2, NOBIT -> RCDATA 3 (id without
 *   8000h), MENUONLY -> MENU 1, TNAMED -> RCDATA 1 with the type name
 *   "XYZ", REAL -> RCDATA 2, MISSING -> RCDATA 9, DLGNAME -> DIALOG 1.
 * Writes, for each query, OBS NAME/TYPE=what FindResource found: the
 * RCDATA's text, "dialog" (a resource that is not RCDATA), or 0.
 */
#include <windows.h>

#define TK_REF "tk70ref.h"
#include "tklog.h"

static HINSTANCE hinst;

static void query(const char *name, LPCSTR type, const char *tname)
{
    HRSRC h = FindResource(hinst, name, type);
    out("OBS ");
    out(name);
    out("/");
    out(tname);
    out("=");
    if (!h)
        out("0");
    else if (type == RT_RCDATA) {
        HGLOBAL g = LoadResource(hinst, h);
        LPSTR p = g ? LockResource(g) : NULL;
        out(p ? p : "(not loaded)");
        if (g) {
            GlobalUnlock(g);
            FreeResource(g);
        }
    } else
        out(SizeofResource(hinst, h) ? "found" : "found, size 0");
    out("\r\n");
}

int __cdecl tmain(void)
{
    hinst = (HINSTANCE)init_regs[5];    /* InitTask's DI */
    out("TK70 start\r\n");
    query("FIRST", RT_RCDATA, "rcdata");
    query("first", RT_RCDATA, "rcdata");
    query("First", RT_RCDATA, "rcdata");
    query("SECOND", RT_RCDATA, "rcdata");
    query("second", RT_RCDATA, "rcdata");
    query("NOBIT", RT_RCDATA, "rcdata");
    query("MENUONLY", RT_RCDATA, "rcdata");
    query("MENUONLY", RT_MENU, "menu");
    query("TNAMED", RT_RCDATA, "rcdata");
    query("REAL", RT_RCDATA, "rcdata");
    query("MISSING", RT_RCDATA, "rcdata");
    query("DLGNAME", RT_DIALOG, "dialog");
    query("DLGNAME", RT_RCDATA, "rcdata");
    query("NONE", RT_RCDATA, "rcdata");
    query("#2", RT_RCDATA, "rcdata");
    query("FIRST", "XYZ", "xyz");
    out("TK70 end\r\n");
    return 0;
}
