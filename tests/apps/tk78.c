/*
 * tk78.c - TK78.EXE: a self-loading program (NE flag 0800h; Windows 3.1
 * SDK, Programmer's Reference Vol. 1 ch. 24) whose loader logs what
 * KERNEL gives it. The loader is tk78s.asm (the loader data table,
 * BootApp, LoadAppSeg, ExitProc) and tk78l.c, the program's first
 * segment; this is the program. Run as the shell (boot-test case
 * selfload). Writes C:\TK78.LOG, one "name=value" per line; lines that
 * start with "~" hold values that depend on the layout (selectors,
 * offsets) and are not compared:
 *   T BootApp ... T BootApp returns   what BootApp(hBlock, hFile) got:
 *       its registers, whether hBlock is the module (GetModuleHandle) and
 *       DS, ES or SS, the file handle's position, the loader table's
 *       words KERNEL changed (boot.table+NN), the module table's header
 *       words and segment table (boot.segN: sector, length, flags,
 *       minimum allocation, the handle: 0, even, odd, cs, cs-1; LAR's
 *       access rights), the module references' names;
 *       eap(MODULE,ORD)=   EntryAddrProc (table 14h) on another module
 *       and on the program itself: 0, gpa (GetProcAddress's address),
 *       own, other; the carry and the selector's access rights; the
 *       registers after (~);
 *       myalloc(...)=      MyAlloc (table 10h): the handle (low word)
 *       and selector (high word), their relation, access rights,
 *       GlobalSize, GlobalFlags, GlobalHandle; the automatic data segment
 *       first, then each code segment with a handle only (0 bytes), as
 *       WinPlay3's Blinker loader does, loaded with GlobalReAlloc (flags
 *       0) and AllocCStoDSAlias; SetOwner (table 24h) for each; the last
 *       code segment (not the entry point's) is left unloaded;
 *   T LoadAppSeg        each call of LoadAppSeg by KERNEL (the segment
 *       BootApp left): its segment, the file handle (FFFFh: none, the
 *       loader returns 0 and KERNEL is to open the file and call again);
 *   T main, T late      from the program: EntryAddrProc again with every
 *       segment loaded, the segment table;
 *   T done
 */
#include <windows.h>

extern void __cdecl log_line(const char FAR *s, WORD n);       /* tk78s.asm */
extern void __cdecl late_eap(void);                              /* tk78l.c */

/* exported (ordinal 1, tk78.lnk) */
void FAR PASCAL tk78_exp(void)
{
}

static void text(const char *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_line(s, n);
    log_line("\r\n", 2);
}

int __cdecl __far tmain(void)
{
    text("T main");
    late_eap();
    text("T done");
    return 0;
}
