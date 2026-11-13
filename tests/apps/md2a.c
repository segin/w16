/*
 * md2a.c - MD2: MOUSE.DRV's undocumented entries GetSetMouseData (5) and
 * PowerEventProc (7), observed black-box (tests/apps/md2logs/a/notes.md).
 * Runs as the shell; [w16] in SYSTEM.INI says what to do:
 *   md2run=CMD    WinExec CMD first (e.g. Microsoft Mouse 8.20's POINT.EXE)
 *   md2secs=N     then pump messages for N seconds (default 20)
 *   md2mode=M     bit mask of direct experiments (md2ax.c, below)
 * Last, if MOUSE is MS3 (tests/apps/ms3a.asm, a recording driver in front
 * of the real one), its record of every call to MOUSE is written out.
 */
#include <windows.h>
#ifndef NOTH
#include <toolhelp.h>
#endif

#define TK_REF "md1ref.h"
#include "tklog.h"

extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern DWORD __cdecl __far md_rcall(FARPROC f, WORD FAR *args, WORD FAR *regs);
extern int FAR PASCAL U_INITAPP(WORD hinst);

static char line[200];
static WORD args[16];
static WORD res[2];
static WORD regs[12];
static FARPROC p5, p7, p[9];

static void wait(WORD secs)
{
    MSG m;
    WORD i, id;
    if (!secs)
        return;
    /* (with no window SetTimer picks the ID: KillTimer needs that one) */
    id = SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < secs; i++)
        while (GetMessage(&m, 0, 0, 0) && !(m.message == WM_TIMER && m.wParam == id)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    KillTimer(0, id);
}

static int differ(BYTE FAR *a, BYTE FAR *b, WORD n)
{
    while (n--)
        if (*a++ != *b++)
            return 1;
    return 0;
}

static void hexs(const char *name, BYTE FAR *b, WORD n)
{
    WORD i;
    out(name);
    for (i = 0; i < n; i++) {
        wsprintf(line, " %02X", b[i]);
        out(line);
    }
    out("\r\n");
}

/* which module a code selector belongs to (TOOLHELP) */
static void modof(WORD sel, char *buf)
#ifdef NOTH
{
    wsprintf(buf, "sel %04X", sel);
}
#else
{
    GLOBALENTRY ge;
    MODULEENTRY me;
    DWORD h = GlobalHandle(sel);
    lstrcpy(buf, "?");
    ge.dwSize = sizeof ge;
    me.dwSize = sizeof me;
    if (!LOWORD(h) || !GlobalEntryHandle(&ge, (HGLOBAL)LOWORD(h)))
        return;
    if (ModuleFindHandle(&me, (HMODULE)ge.hOwner))
        wsprintf(buf, "%s:%u", (LPSTR)me.szModule, ge.wData);
    else
        wsprintf(buf, "owner %04X", ge.hOwner);
}
#endif

static void dumplog(void)
{
    HMODULE hm = GetModuleHandle("MOUSE");
    FARPROC lg = GetProcAddress(hm, MAKEINTRESOURCE(20));
    WORD FAR *h, FAR *r;
    WORD i, k, n;
    char mod[64];
    if (!lg) {
        out("no MS3 log (MOUSE is not MS3)\r\n");
        return;
    }
    h = (WORD FAR *)((DWORD(FAR PASCAL *)(void))lg)();
    n = h[0];
    wsprintf(line, "MS3 log: %u records of %u calls\r\n", n, h[3]);
    out(line);
    for (i = 0; i < n; i++) {
        r = (WORD FAR *)((BYTE FAR *)h + 16 + i * h[2]);
        modof(r[2], mod);
        wsprintf(line, "C #%u ord=%u from %04X:%04X (%s) in AX=%04X BX=%04X CX=%04X DX=%04X "
                 "SI=%04X DI=%04X DS=%04X ES=%04X\r\n",
                 r[23], r[0], r[2], r[1], (LPSTR)mod, r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]);
        out(line);
        wsprintf(line, "  args w0..w7 = %04X %04X %04X %04X %04X %04X %04X %04X\r\n",
                 r[11], r[12], r[13], r[14], r[15], r[16], r[17], r[18]);
        out(line);
        wsprintf(line, "  out AX=%04X DX=%04X BX=%04X CX=%04X ES=%04X flags=%04X popped=%u ptrs=%X\r\n",
                 r[19], r[20], r[25], r[26], r[27], r[21], r[22], r[24]);
        out(line);
        for (k = 0; k < 4; k++)
            if (r[24] & (1 << k)) {
                BYTE FAR *b = (BYTE FAR *)r + 64 + 64 * k;
                wsprintf(line, "  w%u:w%u before", k + 1, k);
                hexs(line, b, 32);
                if (differ(b, b + 32, 32)) {
                    wsprintf(line, "  w%u:w%u after ", k + 1, k);
                    hexs(line, b + 32, 32);
                } else
                    out("  (unchanged)\r\n");
            }
    }
}

/* MS4 (tests/dos/ms4a.asm, a pretend APM BIOS) loaded before WIN: its
   record of the INT 15h AH=53h calls (INT 65h points at the header) */
static void dumpms4(void)
{
    WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    WORD off = ivt[0x65 * 2], seg = ivt[0x65 * 2 + 1];
    WORD ls, i, n;
    WORD FAR *h;
    if (!seg)
        return;
    ls = AllocSelector(SELECTOROF((void FAR *)&ivtsel));
    SetSelectorBase(ls, (DWORD)seg << 4);
    SetSelectorLimit(ls, 0xFFFF);
    h = (WORD FAR *)MAKELP(ls, off);
    if (h[1] == 600) {
        n = h[0];
        wsprintf(line, "MS4 log: %u records, %u event polls\r\n", n, h[3]);
        out(line);
        for (i = 0; i < n; i++) {
            /* the records start 18 bytes after the header */
            WORD FAR *e = (WORD FAR *)((BYTE FAR *)h + 18 + i * 16);
            wsprintf(line, "A %s AX=%04X BX=%04X CX=%04X DX=%04X%s #%u\r\n",
                     (LPSTR)(e[0] == 11 ? "call" : "ret "), e[1], e[2], e[3], e[4],
                     (LPSTR)(e[0] == 12 && e[5] ? " CF" : ""), e[7]);
            out(line);
        }
    }
    FreeSelector(ls);
}

#include "md2ax.c"

int __cdecl tmain(void)
{
    HMODULE hm;
    char cmd[128];
    WORD secs, mode, i;
    U_INITAPP(init_regs[5]);
    out("MD2 start\r\n");
    hm = GetModuleHandle("MOUSE");
    GetModuleFileName(hm, line, sizeof line);
    out("OBS ~file=\"");
    out(line);
    out("\"\r\n");
    for (i = 1; i <= 8; i++)
        p[i] = GetProcAddress(hm, MAKEINTRESOURCE(i));
    p5 = p[5];
    p7 = p[7];
    mode = GetPrivateProfileInt("w16", "md2mode", 0, "SYSTEM.INI");
    GetPrivateProfileString("w16", "md2run", "", cmd, sizeof cmd, "SYSTEM.INI");
    if (cmd[0]) {
        UINT r = WinExec(cmd, SW_SHOWNORMAL);
        wsprintf(line, "WinExec(%s) = %u\r\n", (LPSTR)cmd, r);
        out(line);
    }
    secs = GetPrivateProfileInt("w16", "md2secs", cmd[0] ? 20 : 0, "SYSTEM.INI");
    wait(secs);
    experiments(mode);
    if (!(mode & 2))
        ms2log();
    dumplog();
    dumpms4();
    out("MD2 done\r\n");
    return 0;
}
