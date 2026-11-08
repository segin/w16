/*
 * tk16.c - task probe, part 2 (part 1: tk15.c): WinExec's command line
 * (blanks, no extension, no path), a task's starting directory,
 * LoadModule's environment, a program whose DLL fails to initialise, and
 * a second instance of a program with two writable data segments.
 *
 * Starts TK16A.EXE, TK16B.EXE (imports TKBAD.DLL) and TK16C.EXE, all from
 * tk16a.c, which report into this program's DGROUP (tk16.h); run by
 * tests/tk5ref.sh TK16. Makes C:\TK16DIR.
 *
 * Output and checks: see tklog.h; reference values in tk16ref.h.
 */
#include <windows.h>

#define TK_REF "tk16ref.h"
#define TK_REF286 "tk16ref286.h"
#include "tklog.h"
#include "tk15.h"
#include "tk16.h"

extern int __cdecl dos_int21(WORD FAR *r);

#define STEP(s) out("-- " s "\r\n")

static tk16sh sh;
static char dir[128];           /* TK16.EXE's directory, with "\" */
static WORD self_task;

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static int dos(WORD ax, const char FAR *dsdx, WORD *ax_out)
{
    WORD r[8], i;
    for (i = 0; i < 8; i++)
        r[i] = 0;
    r[0] = ax;
    r[3] = r[4] = (WORD)(DWORD)dsdx;
    r[6] = (WORD)((DWORD)dsdx >> 16);
    i = dos_int21((WORD FAR *)r);
    if (ax_out)
        *ax_out = r[0];
    return !i;
}

static void reset(void)
{
    BYTE FAR *p = (BYTE FAR *)&sh;
    WORD i;
    for (i = 0; i < sizeof sh; i++)
        p[i] = 0;
}

static WORD hex4(char *d, WORD v)
{
    int i;
    for (i = 3; i >= 0; i--) {
        d[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    return 4;
}

/* "M S DDDD OOOO TTTT" at d; returns its length */
static WORD args(char *d, char mode, WORD slot)
{
    WORD n = 0;
    d[n++] = mode;
    d[n++] = ' ';
    d[n++] = (char)('0' + slot);
    d[n++] = ' ';
    n += hex4(d + n, entry_regs[E_DS]);
    d[n++] = ' ';
    n += hex4(d + n, (WORD)(DWORD)(void FAR *)&sh);
    d[n++] = ' ';
    n += hex4(d + n, self_task);
    d[n] = 0;
    return n;
}

static void str(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

static void slot_str(const char *name, WORD slot, WORD w, WORD nwords)
{
    char b[50];
    WORD i;
    for (i = 0; i < 2 * nwords && ((char FAR *)&sh.v[slot][w])[i]; i++)
        b[i] = ((char FAR *)&sh.v[slot][w])[i];
    b[i] = 0;
    obs_str(name, b);
}

/* let the helpers end */
static void wait_gone(WORD h)
{
    WORD i;
    (void)h;
    sh.go = 1;
    for (i = 0; i < 100 && K_GETNUMTASKS() > 1; i++)
        K_YIELD();
}

/* WinExec: blanks, a name without extension or path, the directory */
static void winexec(void)
{
    char cmd[100], cwd[70];
    WORD h, n;

    STEP("WinExec");
    reset();
    dos(0x3900, "C:\\TK16DIR", NULL);
    dos(0x3B00, "C:\\TK16DIR", NULL);
    /* "TK16A   R ...": three blanks, no extension; TK16A.EXE is in the
       directory of this task's program, not the current one */
    str(cmd, "TK16A   ");
    n = 8;
    args(cmd + n, 'R', 0);
    h = K_WINEXEC(cmd, 1);
    obs("WinExec(TK16A   ...) > 32", h > 32);
    obs("ran", sh.v[0][W_RAN]);
    obs("tail length", sh.v[0][W_TAILLEN]);
    obs("tail starts with 2 blanks", sh.v[0][W_TAIL] == 0x2020);
    slot_str("tail", 0, W_TAIL, 12);
    slot_str("starting directory", 0, W_CWD, 12);
    obs("starting directory is TK16DIR", lstrlen((char FAR *)&sh.v[0][W_CWD]) == 7 &&
        sh.v[0][W_CWD] == 0x4B54 /* "TK" */);
    wait_gone(h);
    /* lower case, with the path */
    reset();
    lstrcpy(cmd, dir);
    str(cmd + lstrlen(cmd), "tk16a.exe ");
    args(cmd + lstrlen(cmd), 'R', 1);
    h = K_WINEXEC(cmd, 1);
    obs("WinExec(lower case path) > 32", h > 32);
    obs("lower case: ran", sh.v[1][W_RAN]);
    wait_gone(h);
    dos(0x3B00, "C:\\", NULL);
    cwd[0] = 0;
    dos(0x3A00, "C:\\TK16DIR", NULL);
    (void)cwd;
}

/* LoadModule with an environment of our own */
static void environment(void)
{
    static char tail[64];
    static WORD show[2] = { 2, 1 };
    static struct {
        WORD env;
        char FAR *cmd;
        WORD FAR *show;
        DWORD reserved;
    } p;
    char path[140];
    HGLOBAL g;
    char FAR *e;
    WORD h, n;

    STEP("environment");
    reset();
    g = GlobalAlloc(GMEM_FIXED, 32);
    e = GlobalLock(g);
    lstrcpy(e, "TK16=YES");
    e[9] = 0;
    e[10] = 0;
    n = args(tail + 1, 'R', 2);
    tail[0] = (char)n;
    tail[1 + n] = '\r';
    p.env = (WORD)((DWORD)e >> 16);
    p.cmd = tail;
    p.show = show;
    p.reserved = 0;
    lstrcpy(path, dir);
    lstrcat(path, "TK16A.EXE");
    h = K_LOADMODULE(path, &p);
    obs("LoadModule(env) > 32", h > 32);
    obs("env: ran", sh.v[2][W_RAN]);
    obs("env: same selector as ours", sh.v[2][W_ENVSAME]);
    slot_str("env: first string", 2, W_ENV, 8);
    obs("env: TK16=YES", sh.v[2][W_ENV] == 0x4B54 && sh.v[2][W_ENV + 2] == 0x593D);
    obs("env: the selector given", sh.v[2][W_ENVSEL] == p.env);
    obs("env: length", sh.v[2][W_ENVLEN]);
    obs("env: word after the double NUL", sh.v[2][W_ENVTAIL]);
    obs("env: block size", (WORD)GlobalSize((HGLOBAL)sh.v[2][W_ENVSEL]));
    wait_gone(h);
    GlobalFree(g);
}

/* TK16B imports TKBAD, whose initialisation fails */
static void baddll(void)
{
    char cmd[100];
    WORD h, r;

    STEP("bad DLL");
    reset();
    dos(0x4100, "C:\\TKWEP.TMP", NULL);
    lstrcpy(cmd, dir);
    str(cmd + lstrlen(cmd), "TK16B.EXE ");
    args(cmd + lstrlen(cmd), 'R', 0);
    h = K_WINEXEC(cmd, 1);
    obs("WinExec(TK16B)", h > 32 ? 0x20 : h);
    obs("TK16B pre_init ran", sh.v[0][W_PRE]);
    obs("TK16B ran after InitTask", sh.v[0][W_RAN]);
    obs("TK16B: GetNumTasks", K_GETNUMTASKS());
    obs("TK16B: GetModuleHandle(TK16B)", GetModuleHandle("TK16B") != 0);
    obs("TK16B: GetModuleHandle(TKBAD)", GetModuleHandle("TKBAD") != 0);
    wait_gone(h);
    obs("TK16B: GetModuleHandle(TK16B) at the end", GetModuleHandle("TK16B") != 0);
    obs("TK16B: GetModuleHandle(TKBAD) at the end", GetModuleHandle("TKBAD") != 0);
    obs("TKBAD's WEP ran", dos(0x4300, "C:\\TKWEP.TMP", &r));
    dos(0x4100, "C:\\TKWEP.TMP", NULL);
}

/* TK16C has two writable data segments */
static void twodata(void)
{
    char cmd[100];
    WORD h1, h2;

    STEP("two data segments");
    reset();
    lstrcpy(cmd, dir);
    str(cmd + lstrlen(cmd), "TK16C.EXE ");
    args(cmd + lstrlen(cmd), 'W', 0);
    h1 = K_WINEXEC(cmd, 1);
    obs("TK16C first > 32", h1 > 32);
    obs("TK16C first ran", sh.v[0][W_RAN]);
    lstrcpy(cmd, dir);
    str(cmd + lstrlen(cmd), "TK16C.EXE ");
    args(cmd + lstrlen(cmd), 'W', 1);
    h2 = K_WINEXEC(cmd, 1);
    obs("TK16C second", h2 > 32 ? 0x20 : h2);
    obs("TK16C second ran", sh.v[1][W_RAN]);
    obs("TK16C GetModuleUsage", GetModuleUsage(GetModuleHandle("TK16C")));
    wait_gone(h1);
    if (h2 > 32)
        wait_gone(h2);
    obs("TK16C gone", GetModuleHandle("TK16C") == 0);
}

int __cdecl tmain(void)
{
    WORD n, i;

    out("TK16 start\r\n");
    self_task = (WORD)K_GETCURRENTTASK();
    n = GetModuleFileName((HINSTANCE)hinst(), dir, sizeof dir - 16);
    for (i = n; i && dir[i - 1] != '\\'; i--)
        ;
    dir[i] = 0;
    winexec();
    environment();
    twodata();
    baddll();
    obs("GetNumTasks at the end", K_GETNUMTASKS());
    out("TK16 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
