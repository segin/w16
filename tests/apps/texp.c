/*
 * texp.c - which export ordinals does each system module have? (probe)
 *
 * Loads each module (LoadLibrary) and asks GetProcAddress for every
 * ordinal up to MAX_ORD, logging the ones that exist as ranges:
 *     EXP KERNEL 1-25 27-35 ...
 * Black-box observation of the export tables; the names come from other
 * sources (tools/exports.py). Built twice: TEXP3.EXE (marked 3.0) and
 * TEXP2.EXE (marked 2.0, imports only 1.x/2.x calls). TEXP2 ends the
 * Windows session itself (ExitWindows), for tests/w386run.sh.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);

/* Observed on Windows 3.1: GetProcAddress ignores bit 15 of an ordinal
   (32769 finds ordinal 1), so 7FFFh covers every distinct ordinal. */
#define MAX_ORD 0x7FFFu

/* module name, and the file LoadLibrary needs if it is not loaded */
static const char *mods[][2] = {
    { "KERNEL", "" }, { "USER", "" }, { "GDI", "" }, { "KEYBOARD", "" },
    { "SYSTEM", "" }, { "SOUND", "" }, { "DISPLAY", "" }, { "MOUSE", "" },
    { "COMM", "" }, { "WIN87EM", "WIN87EM.DLL" }, { "SHELL", "SHELL.DLL" },
    { "COMMDLG", "COMMDLG.DLL" }, { "TOOLHELP", "TOOLHELP.DLL" },
    { "DDEML", "DDEML.DLL" }, { "LZEXPAND", "LZEXPAND.DLL" }, { "VER", "VER.DLL" },
    { "MMSYSTEM", "MMSYSTEM.DLL" }, { "OLECLI", "OLECLI.DLL" },
    { "OLESVR", "OLESVR.DLL" }, { "WINOLDAP", "WINOLDAP.MOD" }
};

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outdec(WORD v)
{
    char b[6];
    int i = 5;
    b[5] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    out(b + i);
}

int __cdecl tmain(void)
{
    int m;
    out("TEXP start\r\n");
    for (m = 0; m < (int)(sizeof mods / sizeof mods[0]); m++) {
        HINSTANCE h = GetModuleHandle(mods[m][0]);
        BOOL loaded = FALSE;
        WORD o, start = 0;
        if (!h) {
            OFSTRUCT of;
            /* only files that exist: Windows 2.x puts up a box for a
               missing one, which would stop a headless run */
            h = mods[m][1][0] && OpenFile(mods[m][1], &of, OF_EXIST) != HFILE_ERROR
                ? LoadLibrary(mods[m][1]) : NULL;
            if ((UINT)h < 32) {
                out("EXP ");
                out(mods[m][0]);
                out(" absent\r\n");
                continue;
            }
            loaded = TRUE;
        }
        out("EXP ");
        out(mods[m][0]);
        for (o = 1;; o++) {
            BOOL have = GetProcAddress(h, MAKEINTRESOURCE(o)) != NULL;
            if (have && !start)
                start = o;
            if (!have && start) {
                out(" ");
                outdec(start);
                if (o - 1 != start) {
                    out("-");
                    outdec(o - 1);
                }
                start = 0;
            }
            if (o == MAX_ORD) {         /* the last one: close a run */
                if (start) {
                    out(" ");
                    outdec(start);
                    if (o != start) {
                        out("-");
                        outdec(o);
                    }
                }
                break;
            }
        }
        out("\r\n");
        if (loaded)
            FreeLibrary(h);
    }
    out("TEXP done\r\n");
#ifdef TEXP_WIN2
    ExitWindows(0, 0);
#endif
    return 0;
}
