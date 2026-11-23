/*
 * vx17.c - VX17.EXE: KERNEL's idle loop seen from a program (ARCHITECTURE.md
 * 12.15). Started by WIN.INI run= (tests/kidlerun.sh) under Windows 3.1 and
 * w16, in 386 Enhanced and Standard mode. It hooks the protected-mode INT
 * 28h, INT 2Fh and INT 31h (vx17s.asm: counts, then the previous handler)
 * and, for each phase, counts what KERNEL's idle loop issues while this
 * task waits:
 *   n28    INT 28h in protected mode
 *   n89    INT 2Fh AX=1689h in protected mode (b0: those with BX=0)
 *   n2f    other INT 2Fh in protected mode
 *   r28    INT 31h AX=0300h for INT 28h (a real-mode interrupt simulated)
 *   r2f    INT 31h AX=0300h for INT 2Fh (r89: with AX=1689h)
 *   r31    other INT 31h AX=0300h calls
 *   rm28   INT 28h that reached real mode, rm89 INT 2Fh AX=1689h there:
 *          MS5.COM's counts (tests/dos/ms5.asm, loaded before WIN: INT 66h
 *          points at its header), when it is there
 * WIN.INI [vx17]:
 *   counts=0,1   the box counts: before each, DOS boxes are started (WinExec
 *                of prog, nCmdShow show) until that many run
 *   prog=C:\WINDOWS\VX17.PIF, show=7 (minimised, not active)
 *   wait=10      seconds of messages after Windows has started
 *   settle=10    seconds after starting boxes
 *   secs=8       seconds of each phase (the real-time clock, CMOS register
 *                0: the same for every VM; the VMs' timer ticks are virtual)
 *   only=LIST    the phases (default idle,tick,busy,msgs)
 * The phases:
 *   idle   GetMessage with a 1 s timer only: Windows has nothing to do
 *   tick   GetMessage with a 55 ms timer: tmr counts its WM_TIMERs
 *          (18.2 a second when none is late: how promptly an idle Windows
 *          wakes for its timer)
 *   busy   a busy loop (loop: Windows' share of the processor)
 *   msgs   message round trips (PostAppMessage, GetMessage)
 * Around each phase INT 2Fh AX=B171h-B174h and B17Fh (vx_mark): VXT.386
 * (tests/vxd/vxt.asm) reports there, so its lines between two marks are
 * that phase's (the VMs' run times, V86 INT 28h and 1689h, the VMM's idle
 * callbacks).
 * Output: TK.LOG as it goes and VX17.LOG (the current directory) at the
 * end: lines "VX17 boxes=N phase=P n28=... tmr=... loop=...".
 * Black box (ARCHITECTURE.md 3): only the published API, DPMI and the clock.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern DWORD __cdecl n28, n89, n89b0, n2f, r28, r2f, r89, r31;
extern void __cdecl __far hook_install(void);
extern void __cdecl __far hook_remove(void);
extern void __cdecl __far vx_mark(WORD n);

WORD seg2sel(WORD seg);
#pragma aux seg2sel = "mov ax,2" "int 31h" parm [bx] value [ax];

BYTE rtc_read(BYTE reg);
#pragma aux rtc_read = "out 70h,al" "in al,71h" parm [al] value [al];

static char res[3072];
static int nres;
static char only[64];

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

static void align(void)
{
    BYTE s = rtc();
    while (rtc() == s)
        ;
}

static int want(const char *name)
{
    const char *p = only;
    int n = lstrlen(name);
    if (!*p) return 1;
    while (*p) {
        int i = 0;
        while (p[i] && p[i] != ',') i++;
        if (i == n) {
            int j = 0;
            while (j < n && p[j] == name[j]) j++;
            if (j == n) return 1;
        }
        p += i;
        if (*p) p++;
    }
    return 0;
}

/* messages for secs seconds with a timer of ms: returns its WM_TIMERs */
static DWORD pump(int secs, UINT ms)
{
    MSG m;
    BYTE s = rtc(), t;
    int n = 0;
    DWORD k = 0;
    UINT id = SetTimer(0, 0, ms, NULL);
    while (n < secs && GetMessage(&m, 0, 0, 0)) {
        if (m.message == WM_TIMER && m.wParam == id) k++;
        else DispatchMessage(&m);
        t = rtc();
        if (t != s) {
            s = t;
            n++;
        }
    }
    KillTimer(0, id);
    return k;
}

static DWORD busy(int secs)
{
    DWORD n = 0;
    BYTE s, t;
    int k = 0;
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

static DWORD msgs(int secs)
{
    DWORD n = 0;
    BYTE s, t;
    int k = 0;
    MSG m;
    HTASK me = GetCurrentTask();
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

static DWORD c0[10];
static DWORD FAR *ms5;          /* MS5's two counts, INT 28h and 1689h */

static void ms5_find(void)
{
    WORD FAR *ivt = (WORD FAR *)MAKELP(seg2sel(0), 0);
    WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
    WORD FAR *h;
    if (!seg) return;
    h = (WORD FAR *)MAKELP(seg2sel(seg), off);
    /* its records: kind, count (dword), ... 8 words each, after 22 bytes */
    if (h[11] == 0x28 && h[19] == 0x2F)
        ms5 = (DWORD FAR *)(h + 12);
}

static DWORD rm28(void) { return ms5 ? ms5[0] : 0; }
static DWORD rm89(void) { return ms5 ? ms5[4] : 0; }

static void snap(void)
{
    c0[0] = n28; c0[1] = n89; c0[2] = n89b0; c0[3] = n2f;
    c0[4] = r28; c0[5] = r2f; c0[6] = r89; c0[7] = r31;
    c0[8] = rm28(); c0[9] = rm89();
}

static void phase(int boxes, int code, const char *name, int secs)
{
    char line[256];
    DWORD k = 0, l = 0;
    if (!want(name)) return;
    /* a phase starts on a second's edge: the idle ones' first second
       is then a whole one too */
    align();
    snap();
    vx_mark(code);
    switch (code) {
    case 1: k = pump(secs, 1000); break;
    case 2: k = pump(secs, 55); break;
    case 3: l = busy(secs); break;
    case 4: l = msgs(secs); break;
    }
    vx_mark(15);
    wsprintf(line, "VX17 boxes=%d phase=%s secs=%d n28=%lu n89=%lu n89b0=%lu n2f=%lu"
             " r28=%lu r2f=%lu r89=%lu r31=%lu rm28=%lu rm89=%lu tmr=%lu loop=%lu\r\n",
             boxes, (LPSTR)name, secs, n28 - c0[0], n89 - c0[1], n89b0 - c0[2],
             n2f - c0[3], r28 - c0[4], r2f - c0[5], r89 - c0[6], r31 - c0[7],
             rm28() - c0[8], rm89() - c0[9], k, l);
    out(line);
}

int __cdecl tmain(void)
{
    char counts[64], prog[128], line[128], *p;
    int show, settle, secs, have = 0, wantn;
    HFILE f;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    GetProfileString("vx17", "counts", "0,1", counts, sizeof(counts));
    GetProfileString("vx17", "prog", "C:\\WINDOWS\\VX17.PIF", prog, sizeof(prog));
    GetProfileString("vx17", "only", "", only, sizeof(only));
    show = GetProfileInt("vx17", "show", SW_SHOWMINNOACTIVE);
    settle = GetProfileInt("vx17", "settle", 10);
    secs = GetProfileInt("vx17", "secs", 8);
    out("VX17 start\r\n");
    pump(GetProfileInt("vx17", "wait", 10), 100);
    ms5_find();
    hook_install();
    for (p = counts; *p; ) {
        wantn = 0;
        while (*p >= '0' && *p <= '9') wantn = wantn * 10 + *p++ - '0';
        while (*p && (*p < '0' || *p > '9')) p++;
        while (have < wantn) {
            UINT r = WinExec(prog, show);
            wsprintf(line, "VX17 WinExec %u\r\n", r);
            out(line);
            if (r < 32) break;
            have++;
            pump(2, 100);
        }
        if (have) pump(settle, 100);
        phase(have, 1, "idle", secs);
        phase(have, 2, "tick", secs);
        phase(have, 3, "busy", secs);
        phase(have, 4, "msgs", secs);
    }
    hook_remove();
    out("VX17 done\r\n");
    f = _lcreat("VX17.LOG", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, res, nres);
        _lclose(f);
    }
    return 0;
}
