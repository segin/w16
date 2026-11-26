/*
 * tk73.c - TK73.EXE: what an installer's start-up asks of KERNEL before
 * it looks at the disk (the Entertainment Pack setups said there were
 * zero bytes free, 2026-11-25). Run as the shell (tests/dropin.sh).
 * Writes to TK.LOG:
 *   OBS envtail=N "TEXT"     what follows the environment's double NUL:
 *                            the word there and the string after it (DOS
 *                            puts the program's path there; C start-up
 *                            code takes argv[0] from it)
 *   OBS ~cmd="TEXT"          InitTask's command line (ES:BX)
 *   OBS ~modname="TEXT"      GetModuleFileName of the program
 *   OBS gwd(K)=R "TEXT"      GetWindowsDirectory with a buffer of L+K
 *                            bytes (L: its length), the buffer filled with
 *                            '#' before; GetSystemDirectory the same (gsd)
 *   OBS free36(D)=...        INT 21h AH=36h for drive D (0, 3, 1Bh, C0h),
 *                            AX BX CX DX and the carry, through DOS3Call
 * then "TK73 end".
 */
#include <windows.h>

#define TK_REF "tk73ref.h"
#include "tklog.h"

static char buf[300];

static void outstr(const char FAR *s, WORD max)
{
    WORD i;
    out("\"");
    for (i = 0; i < max && s[i]; i++) {
        char c[2];
        c[0] = s[i];
        c[1] = 0;
        if (s[i] < 0x20 || s[i] > 0x7E) {
            out("\\");
            outhex((BYTE)s[i]);
        } else
            out(c);
    }
    out("\"");
}

static void outdec(WORD v)
{
    char b[6];
    int n = 5;
    b[5] = 0;
    do {
        b[--n] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    out(b + n);
}

/* INT 21h AH=36h through DOS3Call (KERNEL.102): tk73s.asm */
extern void __cdecl __far free36(BYTE drive, WORD __far *out);
static WORD r[5];

static void dirs(const char *name, UINT (FAR PASCAL *fn)(LPSTR, UINT))
{
    UINT len = fn(buf, sizeof buf), r, k;
    static const int ks[] = { -100, -3, -2, -1, 0, 1, 2 };
    for (k = 0; k < sizeof ks / sizeof ks[0]; k++) {
        int n = (int)len + ks[k];
        UINT j;
        if (n < 0)
            n = 0;
        for (j = 0; j < sizeof buf - 1; j++)
            buf[j] = '#';
        buf[sizeof buf - 1] = 0;
        r = fn(buf, n);
        out("OBS ");
        out(name);
        out("(");
        if (ks[k] == -100)
            out("0");
        else {
            out("L");
            if (ks[k] < 0)
                out("-");
            else
                out("+");
            outdec(ks[k] < 0 ? -ks[k] : ks[k]);
        }
        out(")=");
        if (r == len)
            out("L");
        else if (r == len + 1)
            out("L+1");
        else
            outhex(r);
        out(" ");
        outstr(buf, len + 4);
        out("\r\n");
    }
}

int __cdecl tmain(void)
{
    LPSTR env = GetDOSEnvironment(), cmd;
    WORD i;
    static const BYTE drives[] = { 0, 3, 0x1B, 0xC0 };

    out("TK73 start\r\n");
    /* the environment's end: the double NUL, then (DOS) a word and the
       program's path */
    for (i = 0; i < 0x7FF0 && (env[i] || env[i + 1]); i++)
        ;
    out("OBS envtail=");
    outhex(*(WORD FAR *)(env + i + 2));
    out(" ");
    outstr(env + i + 4, 80);
    out("\r\n");
    cmd = MAKELP(init_regs[6], init_regs[1]);
    out("OBS ~cmd=");
    outstr(cmd, 80);
    out("\r\n");
    GetModuleFileName(GetModuleHandle("TK73"), buf, sizeof buf);
    out("OBS ~modname=");
    outstr(buf, 128);
    out("\r\n");
    dirs("gwd", GetWindowsDirectory);
    dirs("gsd", GetSystemDirectory);
    for (i = 0; i < sizeof drives; i++) {
        free36(drives[i], r);
        out("OBS ~free36(");
        outhex(drives[i]);
        out(")=");
        outhex(r[0]);
        out(" ");
        outhex(r[1]);
        out(" ");
        outhex(r[2]);
        out(" ");
        outhex(r[3]);
        out(r[4] ? " C" : " NC");
        out("\r\n");
    }
    out("TK73 end\r\n");
    return 0;
}
