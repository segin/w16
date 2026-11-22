/*
 * vx13.c - VX13.EXE: how much of the processor Windows gets while DOS
 * boxes run beside it (the idle-detection work, VMPOLL: ARCHITECTURE.md
 * 12.12). Started by WIN.INI run= under Windows 3.1's WIN386 and under
 * w16's (tests/vmpollrun.sh). WIN.INI [vx13]:
 *   counts=0,1,2,4   the phases: before each, boxes are started (WinExec of
 *                    prog, nCmdShow show) until that many run
 *   prog=CMD         what each box runs (a PIF; default C:\WINDOWS\VX13.PIF)
 *   show=7           WinExec's nCmdShow (7: minimised, not active)
 *   wait=10          seconds after Windows has started, messages processed
 *   settle=15        seconds after starting boxes, messages processed
 *   secs=8           seconds of each measurement
 * Each phase measures, by the real-time clock's seconds (CMOS register 0,
 * which every VM reads alike: the VMs' own timer ticks are virtual):
 *   loop   iterations of a busy loop in those seconds (Windows' share of the
 *          processor: no messages processed meanwhile);
 *   cli    (with no boxes, a line of its own) CLI/STI pairs in those
 *          seconds: each traps to the VMM;
 *   msgs   messages in those seconds posted to the task and taken back with
 *          GetMessage (a message round trip, Windows' responsiveness).
 * Output: TK.LOG as it goes, and VX13.LOG (the current directory) at the
 * end, all at once, for tests/dropin.sh -W: lines
 *   VX13 boxes=N loop=L msgs=M
 * Black box (ARCHITECTURE.md 3): only the published API and the clock.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

BYTE rtc_read(BYTE reg);
#pragma aux rtc_read = "out 70h,al" "in al,71h" parm [al] value [al];

void cli_sti(void);
#pragma aux cli_sti = "cli" "sti";

static char res[1024];
static int nres;

static void out(const char *s)
{
    int n = lstrlen(s);
    log_write(s, n);
    if (nres + n < sizeof(res)) {
        lstrcpy(res + nres, s);
        nres += n;
    }
}

static BYTE rtc(void)
{
    while (rtc_read(0x0A) & 0x80)
        ;
    return rtc_read(0);
}

/* messages processed for secs seconds: Windows idles meanwhile */
static void pump(int secs)
{
    MSG m;
    BYTE s = rtc(), t;
    int n = 0;
    UINT id = SetTimer(0, 0, 100, NULL);
    while (n < secs && GetMessage(&m, 0, 0, 0)) {
        DispatchMessage(&m);
        t = rtc();
        if (t != s) {
            s = t;
            n++;
        }
    }
    KillTimer(0, id);
}

static void align(void)
{
    BYTE s = rtc();
    while (rtc() == s)
        ;
}

static DWORD busy(int secs)
{
    DWORD n = 0;
    BYTE s, t;
    int k = 0;
    align();
    s = rtc();
    for (;;) {
        n++;
        if (!(n & 1023)) {
            t = rtc();
            if (t != s) {
                s = t;
                if (++k == secs) break;
            }
        }
    }
    return n;
}

/* CLI and STI pairs in those seconds (each a trap to the VMM: the client
 * runs with IOPL 0, ARCHITECTURE.md 12.3) */
static DWORD clis(int secs)
{
    DWORD n = 0;
    BYTE s, t;
    int k = 0;
    align();
    s = rtc();
    for (;;) {
        cli_sti();
        n++;
        if (!(n & 255)) {
            t = rtc();
            if (t != s) {
                s = t;
                if (++k == secs) break;
            }
        }
    }
    return n;
}

static DWORD msgs(int secs)
{
    DWORD n = 0;
    BYTE s, t;
    int k = 0;
    MSG m;
    HTASK me = GetCurrentTask();
    align();
    s = rtc();
    for (;;) {
        PostAppMessage(me, WM_USER, 0, 0);
        if (!GetMessage(&m, 0, 0, 0)) break;
        if (m.message != WM_USER) DispatchMessage(&m);
        n++;
        if (!(n & 15)) {
            t = rtc();
            if (t != s) {
                s = t;
                if (++k == secs) break;
            }
        }
    }
    return n;
}

int __cdecl tmain(void)
{
    char counts[64], prog[128], line[128], *p;
    int show, settle, secs, have = 0, want;
    HFILE f;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    GetProfileString("vx13", "counts", "0,1,2,4", counts, sizeof(counts));
    GetProfileString("vx13", "prog", "C:\\WINDOWS\\VX13.PIF", prog, sizeof(prog));
    show = GetProfileInt("vx13", "show", SW_SHOWMINNOACTIVE);
    settle = GetProfileInt("vx13", "settle", 15);
    secs = GetProfileInt("vx13", "secs", 8);
    out("VX13 start\r\n");
    pump(GetProfileInt("vx13", "wait", 10));
    for (p = counts; *p; ) {
        want = 0;
        while (*p >= '0' && *p <= '9') want = want * 10 + *p++ - '0';
        while (*p && (*p < '0' || *p > '9')) p++;
        while (have < want) {
            UINT r = WinExec(prog, show);
            wsprintf(line, "VX13 WinExec %u\r\n", r);
            out(line);
            if (r < 32) break;
            have++;
            pump(2);
        }
        pump(settle);
        if (!have) {
            wsprintf(line, "VX13 cli=%lu\r\n", clis(secs));
            out(line);
        }
        {
            DWORD l = busy(secs);
            DWORD m = msgs(secs);
            wsprintf(line, "VX13 boxes=%d loop=%lu msgs=%lu\r\n", have, l, m);
            out(line);
        }
    }
    out("VX13 done\r\n");
    f = _lcreat("VX13.LOG", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, res, nres);
        _lclose(f);
    }
    return 0;
}
