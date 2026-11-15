/*
 * wo1.c - WO1: a logging stand-in for WINDOWS\SYSTEM\WINOLDAP.MOD
 * (black-box probe for w16's WINOLDAP.MOD, ARCHITECTURE.md 9.8).
 *
 * Built as an NE module named WINOLDAP with the same exports (WNDPROC @1,
 * DIALOGPROC @2) and copied over WINOLDAP.MOD in a scratch install
 * (tests/worun.sh -m build/dos/WO1.MOD), it records how KERNEL starts it
 * when Windows runs a DOS program, then ends (exit code from C:\WO1.INI
 * [wo1] rc=, default 0):
 *   - the registers at entry and from InitTask (command line ES:BX, nCmdShow
 *     DX, hInstance DI, hPrevInstance SI), the command line's bytes;
 *   - the module file name, GetWinFlags, GetCurrentTask and its TDB
 *     (flags at 16h, module name at F2h), the PDB (PSP) and its first 100h
 *     bytes, the environment's strings;
 *   - WinOldApCall(0)'s registers (KERNEL.151, Schulman ch. 5),
 *     GetWinOldApHooks (KERNEL.344) and what it points to;
 *   - if [wo1] call=N, then WinOldApCall(N) as well.
 * Everything goes to C:\WO.LOG (appended). Exports are logged if called.
 */
#include <windows.h>

extern WORD __cdecl entry_regs[11];   /* ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl init_regs[7];     /* ax bx cx dx si di es */
extern WORD __cdecl woa_regs[6];      /* ax bx cx dx si di after WinOldApCall */
extern void __cdecl woa_call(WORD mode);
extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern DWORD FAR PASCAL GETWINOLDAPHOOKS(void);
extern WORD FAR PASCAL GETCURRENTPDB(void);

static char line[300];
static HFILE lf = HFILE_ERROR;

static void lopen(void)
{
    OFSTRUCT of;
    lf = OpenFile("C:\\WO.LOG", &of, OF_WRITE);
    if (lf == HFILE_ERROR)
        lf = OpenFile("C:\\WO.LOG", &of, OF_CREATE | OF_WRITE);
    if (lf != HFILE_ERROR)
        _llseek(lf, 0, 2);
}

static void out(const char *s)
{
    if (lf != HFILE_ERROR)
        _lwrite(lf, s, lstrlen(s));
}

static void quoted(const char FAR *p, int n)
{
    char b[8];
    int i;
    for (i = 0; i < n; i++) {
        BYTE c = p[i];
        if (c < ' ' || c > '~' || c == '\\') {
            wsprintf(b, "\\%02X", c);
            out(b);
        } else {
            b[0] = c;
            b[1] = 0;
            out(b);
        }
    }
}

static void dump(const char *name, const BYTE FAR *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (i % 16 == 0) {
            wsprintf(line, "%s%s+%02X:", i ? "\r\n" : "", (LPSTR)name, i);
            out(line);
        }
        wsprintf(line, " %02X", p[i]);
        out(line);
    }
    out("\r\n");
}

LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    lopen();
    wsprintf(line, "WNDPROC %04X %04X %04X %08lX\r\n", h, msg, wp, lp);
    out(line);
    _lclose(lf);
    return DefWindowProc(h, msg, wp, lp);
}

BOOL FAR PASCAL DialogProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    lopen();
    wsprintf(line, "DIALOGPROC %04X %04X %04X %08lX\r\n", h, msg, wp, lp);
    out(line);
    _lclose(lf);
    return FALSE;
}

int __cdecl __far tmain(void)
{
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    const char FAR *cmd = (const char FAR *)MAKELP(init_regs[6], init_regs[1]);
    WORD pdb, envsel, task;
    const char FAR *env;
    const BYTE FAR *tdb;
    DWORD hooks;
    int n, rc, call;

    lopen();
    out("== WO1 (WINOLDAP stand-in)\r\n");
    wsprintf(line, "entry ax %04X bx %04X cx %04X dx %04X si %04X di %04X bp %04X sp %04X ds %04X es %04X ss %04X\r\n",
             entry_regs[0], entry_regs[1], entry_regs[2], entry_regs[3], entry_regs[4], entry_regs[5],
             entry_regs[6], entry_regs[7], entry_regs[8], entry_regs[9], entry_regs[10]);
    out(line);
    wsprintf(line, "inittask ax %04X bx %04X cx %04X dx %04X si %04X di %04X es %04X\r\n",
             init_regs[0], init_regs[1], init_regs[2], init_regs[3], init_regs[4], init_regs[5], init_regs[6]);
    out(line);
    if (init_regs[0]) {
        /* the command line: a length byte before ES:BX? record both views */
        out("cmdline [");
        for (n = 0; n < 200 && cmd[n] && cmd[n] != '\r'; n++)
            ;
        quoted(cmd, n);
        out("]\r\n");
        dump("cmd-2", (const BYTE FAR *)cmd - 2, 0x90);
    }
    GetModuleFileName(hinst, line, 200);
    out("module file [");
    out(line);
    wsprintf(line, "] winflags %08lX version %08lX\r\n", GetWinFlags(), GetVersion());
    out(line);
    task = (WORD)GetCurrentTask();
    tdb = (const BYTE FAR *)MAKELP(task, 0);
    wsprintf(line, "task %04X tdb flags(16h) %04X\r\n", task, *(WORD FAR *)(tdb + 0x16));
    out(line);
    dump("tdb", tdb, 0x100);
    pdb = GETCURRENTPDB();
    wsprintf(line, "pdb %04X\r\n", pdb);
    out(line);
    dump("pdb", (const BYTE FAR *)MAKELP(pdb, 0), 0x100);
    envsel = *(WORD FAR *)MAKELP(pdb, 0x2C);
    env = (const char FAR *)MAKELP(envsel, 0);
    wsprintf(line, "env %04X\r\n", envsel);
    out(line);
    for (n = 0; n < 8; n++) {
        int k = lstrlen(env);
        if (!k)
            break;
        out("env [");
        quoted(env, k);
        out("]\r\n");
        env += k + 1;
    }
    K_WAITEVENT(0);
    U_INITAPP((WORD)hinst);
    hooks = GETWINOLDAPHOOKS();
    wsprintf(line, "GetWinOldApHooks %08lX\r\n", hooks);
    out(line);
    if (hooks && !IsBadReadPtr((void FAR *)hooks, 32))
        dump("hooks", (const BYTE FAR *)hooks, 32);
    woa_call(0);
    wsprintf(line, "WinOldApCall(0) ax %04X bx %04X cx %04X dx %04X si %04X di %04X\r\n",
             woa_regs[0], woa_regs[1], woa_regs[2], woa_regs[3], woa_regs[4], woa_regs[5]);
    out(line);
    call = GetPrivateProfileInt("wo1", "call", 0, "C:\\WO1.INI");
    if (call) {
        woa_call(call);
        wsprintf(line, "WinOldApCall(%d) ax %04X bx %04X cx %04X dx %04X si %04X di %04X\r\n", call,
                 woa_regs[0], woa_regs[1], woa_regs[2], woa_regs[3], woa_regs[4], woa_regs[5]);
        out(line);
    }
    rc = GetPrivateProfileInt("wo1", "rc", 0, "C:\\WO1.INI");
    wsprintf(line, "exit %d\r\n", rc);
    out(line);
    _lclose(lf);
    return rc;
}
