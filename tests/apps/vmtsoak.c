/*
 * vmtsoak.c - VMTSOAK.EXE: the VM Task Manager left running while other
 * programs and DOS boxes come and go (ARCHITECTURE.md 15.12;
 * tests/vmtasksoak.sh). Started by WIN.INI run=, it starts launch= (the
 * VM Task Manager, at its fastest interval), then for each of cycles=
 * cycles:
 *   - starts the next program of apps= (a list), minimised and not
 *     active, waits, and closes it (WM_CLOSE to its top-level windows);
 *   - every other cycle starts box= (a PIF), waits, and ends its VM
 *     through VMTASK.386's function 3 (Close_VM and Nuke_VM in turn, as
 *     the VM Task Manager's End VM does);
 *   - once everything it started has gone, logs a sample: the free
 *     global memory (GetFreeSpace), the free system, GDI and USER
 *     resources (GetFreeSystemResources), the global heap's block count
 *     (TOOLHELP's GlobalInfo), the number of tasks, the VM Task
 *     Manager's own global blocks (those its module or instance owns)
 *     and whether its window is still there; then GDI's objects by type
 *     (TOOLHELP's LocalFirst/LocalNext) and the font files' modules
 *     (*.FON) with their usage counts.
 * Then the checks ("CHECK ok ..." / "CHECK FAIL ..."): the VM Task
 * Manager still runs, and from the sixth sample (the second with fewer
 * than 11 cycles: every program and a box run once, their one-time
 * allocations made) to the last the resources stay within a
 * small margin: no leak that grows with each refresh.
 * WIN.INI [vmtsoak]: launch= apps=A,B,... box= cycles=N wait=S (before
 * the first cycle) appsecs=S boxsecs=S; gdifill=N userfill=N memfill=KB
 * (fill_heaps: the heaps or global memory nearly full first)
 * Output: VMTSOAK.LOG at the end (current directory), TK.LOG as it goes.
 * Black box (ARCHITECTURE.md 3): the published API, TOOLHELP, VMTASK's
 * own interface and the clock.
 */
#include <windows.h>
#include <toolhelp.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl __far vx_int2f(WORD FAR *regs);
extern void __cdecl __far vx_call(void FAR *entry, WORD FAR *regs);
extern BOOL FAR PASCAL IsWinOldApTask(HTASK);           /* KERNEL.158 */

BYTE rtc_read(BYTE reg);
#pragma aux rtc_read = "out 70h,al" "in al,71h" parm [al] value [al];

#pragma pack(1)
struct vmt_head {
    WORD head_size, rec_size, vm_count, rec_count;
    DWORD sys_time, idle_ms, idle_calls, sys_vm, focus_vm, granularity;
};
struct vmt_vm {
    DWORD handle, id, status, run_ms, exec_ms, ts_flags, fg_prio, bg_prio;
    DWORD cpu_pct, shell_flags, created, runs;
    DWORD pages, pages_out, ems_max, xms_max;
};
#pragma pack()
#define MAXV 16
static struct {
    struct vmt_head h;
    struct vmt_vm v[MAXV];
} snap;

static char res[8192];
static int nres, fails;
static void FAR *entry;

struct sample {
    DWORD free_kb, blocks, own_kb, own_blocks;
    WORD sys, gdi, user, tasks, gdi_items, user_items;
    WORD gdit[11];                      /* GDI's objects by type (LT_GDI_*) */
};

static void out(const char *s)
{
    int n = lstrlen(s);
    log_write(s, n);
    if (nres + n < sizeof(res)) {
        lstrcpy(res + nres, s);
        nres += n;
    }
}

static void check(int ok, const char *what)
{
    char line[200];
    wsprintf(line, "CHECK %s %s\r\n", ok ? (LPSTR)"ok" : (LPSTR)"FAIL", (LPSTR)what);
    out(line);
    if (!ok) fails++;
}

static BYTE rtc(void)
{
    BYTE v;
    while (rtc_read(0x0A) & 0x80)
        ;
    v = rtc_read(0);
    if (!(rtc_read(0x0B) & 4)) v = (BYTE)((v >> 4) * 10 + (v & 15));
    return v;
}

/* messages for secs seconds of the real-time clock */
static void pump(int secs)
{
    MSG m;
    BYTE s = rtc(), t;
    int n = 0;
    UINT id = SetTimer(0, 0, 500, NULL);
    while (n < secs && GetMessage(&m, 0, 0, 0)) {
        DispatchMessage(&m);
        t = rtc();
        if (t != s) {
            n += (t + 60 - s) % 60;
            s = t;
        }
    }
    KillTimer(0, id);
}

static int api(WORD fn, WORD FAR *r)
{
    r[0] = fn;
    vx_call(entry, r);
    return !(r[7] & 1);
}

static int snapshot(void)
{
    WORD r[8];
    if (!entry) return 0;
    r[1] = 0;
    r[2] = sizeof(snap);
    r[3] = r[4] = 0;
    r[5] = OFFSETOF((void FAR *)&snap);
    r[6] = SELECTOROF((void FAR *)&snap);
    return api(1, r);
}

/* the DOS VMs there are (the System VM left out) */
static int dos_vms(void)
{
    return snapshot() ? snap.h.rec_count - 1 : 0;
}

/* every DOS VM ended through function 3 (method 0 Close_VM, 1 Nuke_VM) */
static void end_boxes(int method)
{
    WORD r[8];
    int i, n;
    char line[96];
    if (!snapshot()) return;
    n = snap.h.rec_count < MAXV ? snap.h.rec_count : MAXV;
    for (i = 1; i < n; i++) {
        DWORD h = snap.v[i].handle;
        r[1] = LOWORD(h);
        r[3] = HIWORD(h);
        r[2] = method;
        r[4] = 3000;
        r[5] = r[6] = 0;
        api(3, r);
        wsprintf(line, "VMTSOAK end vm %08lX by %s: ax=%04X cf=%d\r\n", h,
                 (LPSTR)(method ? "Nuke_VM" : "Close_VM"), r[0], r[7] & 1);
        out(line);
    }
}

/* WM_CLOSE to the top-level windows of instance hi's task */
static int close_app(HINSTANCE hi)
{
    HWND w, next;
    int n = 0;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = next) {
        next = GetWindow(w, GW_HWNDNEXT);
        if ((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE) == hi && !GetWindow(w, GW_OWNER)) {
            PostMessage(w, WM_CLOSE, 0, 0);
            n++;
        }
    }
    return n;
}

/* [vmtsoak] gdifill=N, userfill=N: GDI objects (brushes) and USER
   objects (menus) made and kept until no more than N per cent of the
   heap is free (-1: until one cannot be made), as a long session's
   leaks and many programs leave it: what the VM Task Manager does when
   its allocations start to fail */
#define NFILL 6000
static WORD fills[NFILL];
static int nfills;

static void fill_heaps(void)
{
    char line[96];
    int g = GetProfileInt("vmtsoak", "gdifill", 0), u = GetProfileInt("vmtsoak", "userfill", 0);
    int ng = 0, nu = 0;
    while (g && nfills < NFILL && (g < 0 || GetFreeSystemResources(GFSR_GDIRESOURCES) > (WORD)g)) {
        HBRUSH b = CreateSolidBrush(RGB(nfills & 255, 1, 2));
        if (!b) break;
        fills[nfills++] = (WORD)b;
        ng++;
    }
    while (u && nfills < NFILL && (u < 0 || GetFreeSystemResources(GFSR_USERRESOURCES) > (WORD)u)) {
        HMENU m = CreateMenu();
        if (!m) break;
        fills[nfills++] = (WORD)m;
        nu++;
    }
    /* memfill=KB: fixed 64K blocks (then smaller) until no more than KB
       of global memory is free, so that discardable code and resources
       go and come back from their files as on a full machine */
    {
        DWORD want = (DWORD)GetProfileInt("vmtsoak", "memfill", 0) * 1024;
        DWORD sz = 65536L;
        int nm = 0;
        while (want && nfills < NFILL && GetFreeSpace(0) > want + 4096 && sz >= 4096) {
            DWORD rem = GetFreeSpace(0) - want;
            HGLOBAL g = GlobalAlloc(GMEM_FIXED, rem < sz ? rem : sz);
            if (!g) { sz /= 2; continue; }
            fills[nfills++] = (WORD)g;
            nm++;
        }
        wsprintf(line, "VMTSOAK memfill: %d blocks, %luK free\r\n", nm, GetFreeSpace(0) / 1024);
        out(line);
    }
    wsprintf(line, "VMTSOAK filled: %d brushes, %d menus; gdi %u%% user %u%% free\r\n", ng, nu,
             GetFreeSystemResources(GFSR_GDIRESOURCES), GetFreeSystemResources(GFSR_USERRESOURCES));
    out(line);
}

static HMODULE vmt_mod;
static HINSTANCE vmt_inst;

static void take(struct sample *s)
{
    SYSHEAPINFO shi;
    GLOBALINFO gi;
    GLOBALENTRY ge;
    BOOL any;
    s->free_kb = GetFreeSpace(0) / 1024;
    s->sys = GetFreeSystemResources(GFSR_SYSTEMRESOURCES);
    s->gdi = GetFreeSystemResources(GFSR_GDIRESOURCES);
    s->user = GetFreeSystemResources(GFSR_USERRESOURCES);
    shi.dwSize = sizeof(shi);
    SystemHeapInfo(&shi);
    s->gdi_items = s->user_items = 0;
    {
        LOCALINFO li;
        li.dwSize = sizeof(li);
        if (LocalInfo(&li, shi.hGDISegment)) s->gdi_items = li.wcItems;
        li.dwSize = sizeof(li);
        if (LocalInfo(&li, shi.hUserSegment)) s->user_items = li.wcItems;
    }
    {
        LOCALENTRY le;
        int k;
        for (k = 0; k < 11; k++) s->gdit[k] = 0;
        le.dwSize = sizeof(le);
        for (any = LocalFirst(&le, shi.hGDISegment); any; any = LocalNext(&le))
            if (le.wType < 11) s->gdit[le.wType]++;
    }
    gi.dwSize = sizeof(gi);
    s->blocks = GlobalInfo(&gi) ? gi.wcItems : 0;
    s->tasks = GetNumTasks();
    s->own_kb = s->own_blocks = 0;
    ge.dwSize = sizeof(ge);
    if (vmt_mod)
        for (any = GlobalFirst(&ge, GLOBAL_ALL); any; any = GlobalNext(&ge, GLOBAL_ALL)) {
            WORD o = (WORD)ge.hOwner;
            if (ge.wType == GT_FREE || !o) continue;
            if ((o | 1) == ((WORD)vmt_mod | 1) || (o | 1) == ((WORD)vmt_inst | 1)) {
                s->own_kb += ge.dwBlockSize;
                s->own_blocks++;
            }
        }
    s->own_kb = (s->own_kb + 1023) / 1024;
}

static void log_sample(int c, struct sample *s, int alive)
{
    char line[200];
    wsprintf(line, "VMTSOAK sample %d free=%luK blocks=%lu sys=%u%% gdi=%u%%/%u user=%u%%/%u "
             "tasks=%u vmtask=%s own=%luK/%lu\r\n", c, s->free_kb, s->blocks, s->sys, s->gdi,
             s->gdi_items, s->user, s->user_items, s->tasks, (LPSTR)(alive ? "up" : "GONE"), s->own_kb, s->own_blocks);
    out(line);
    wsprintf(line, "VMTSOAK   gdi objects pen=%u brush=%u font=%u palette=%u bitmap=%u rgn=%u dc=%u "
             "ddc=%u mdc=%u mf=%u\r\n", s->gdit[1], s->gdit[2], s->gdit[3], s->gdit[4], s->gdit[5],
             s->gdit[6], s->gdit[7], s->gdit[8], s->gdit[9], s->gdit[10]);
    out(line);
    {
        /* the font files' modules and their usage (AddFontResource
           counts there): a DOS box's fonts, *WOA.FON */
        MODULEENTRY me;
        BOOL any;
        char *p = line;
        p += wsprintf(p, "VMTSOAK   fonts");
        me.dwSize = sizeof(me);
        for (any = ModuleFirst(&me); any; any = ModuleNext(&me)) {
            int n = lstrlen(me.szExePath);
            if (n > 4 && !lstrcmpi(me.szExePath + n - 4, ".FON") && p - line < 160)
                p += wsprintf(p, " %s=%u", (LPSTR)me.szModule, me.wcUsage);
        }
        lstrcpy(p, "\r\n");
        out(line);
    }
}

/* the VM Task Manager's module and the instance of its task */
static void find_vmtask(void)
{
    TASKENTRY te;
    BOOL any;
    te.dwSize = sizeof(te);
    for (any = TaskFirst(&te); any; any = TaskNext(&te))
        if (!lstrcmpi(te.szModule, "VMTASK")) {
            vmt_mod = te.hModule;
            vmt_inst = te.hInst;
        }
}

int __cdecl tmain(void)
{
    char launch[128], apps[256], box[128], line[200], app[16][80];
    int cycles, appsecs, boxsecs, napps = 0, c, i, alive = 1, base;
    struct sample first, s, last;
    WORD r[8];
    HFILE f;
    char *p;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    GetProfileString("vmtsoak", "launch", "", launch, sizeof(launch));
    GetProfileString("vmtsoak", "apps", "", apps, sizeof(apps));
    GetProfileString("vmtsoak", "box", "", box, sizeof(box));
    cycles = GetProfileInt("vmtsoak", "cycles", 20);
    appsecs = GetProfileInt("vmtsoak", "appsecs", 3);
    boxsecs = GetProfileInt("vmtsoak", "boxsecs", 5);
    for (p = apps; *p && napps < 16; ) {
        i = 0;
        while (*p && *p != ',' && i < 79) app[napps][i++] = *p++;
        app[napps][i] = 0;
        if (*p == ',') p++;
        if (i) napps++;
    }
    out("VMTSOAK start\r\n");
    pump(GetProfileInt("vmtsoak", "wait", 5));
    r[0] = 0x1684;
    r[1] = 0x5716;
    r[2] = r[3] = r[4] = r[5] = r[6] = 0;
    vx_int2f(r);
    entry = MAKELP(r[6], r[5]);
    if (!r[6] && !r[5]) entry = NULL;
    wsprintf(line, "VMTSOAK device %s\r\n", (LPSTR)(entry ? "there" : "not there"));
    out(line);
    if (launch[0]) {
        wsprintf(line, "VMTSOAK launch %s %u\r\n", (LPSTR)launch, WinExec(launch, SW_SHOWNORMAL));
        out(line);
        pump(3);
    }
    find_vmtask();
    base = GetNumTasks();
    fill_heaps();
    for (c = 0; c < cycles; c++) {
        if (napps) {
            HINSTANCE hi;
            hi = (HINSTANCE)WinExec(app[c % napps], SW_SHOWMINNOACTIVE);
            wsprintf(line, "VMTSOAK cycle %d %s %u\r\n", c, (LPSTR)app[c % napps], (WORD)hi);
            out(line);
            pump(appsecs);
            if ((WORD)hi >= 32) close_app(hi);
        }
        if (box[0] && entry && (c & 1)) {
            UINT rc = WinExec(box, SW_SHOWMINNOACTIVE);
            wsprintf(line, "VMTSOAK box %s %u\r\n", (LPSTR)box, rc);
            out(line);
            pump(boxsecs);
            end_boxes((c >> 1) & 1);
        }
        /* until what this cycle started has gone (at most 20 s) */
        for (i = 0; i < 20; i++) {
            pump(1);
            if (!dos_vms() && GetNumTasks() <= base) break;
        }
        pump(2);
        alive = !launch[0] || FindWindow("VMTask", NULL) != 0;
        take(&s);
        log_sample(c, &s, alive);
        if (c == (cycles > 10 ? 5 : 1)) first = s;
        last = s;
        if (!alive) break;
    }
    if (launch[0]) check(alive, "the VM Task Manager still runs");
    if (cycles > 2 && alive) {
        wsprintf(line, "free global memory %luK -> %luK (at most 64K less)", first.free_kb, last.free_kb);
        check(last.free_kb + 64 >= first.free_kb, line);
        wsprintf(line, "global blocks %lu -> %lu (at most 16 more)", first.blocks, last.blocks);
        check(last.blocks <= first.blocks + 16, line);
        wsprintf(line, "free GDI resources %u%% -> %u%% (at most 1 less)", first.gdi, last.gdi);
        check(last.gdi + 1 >= first.gdi, line);
        wsprintf(line, "GDI's local heap items %u -> %u (at most 8 more)", first.gdi_items, last.gdi_items);
        check(last.gdi_items <= first.gdi_items + 8, line);
        wsprintf(line, "USER's local heap items %u -> %u (at most 8 more)", first.user_items, last.user_items);
        check(last.user_items <= first.user_items + 8, line);
        wsprintf(line, "free USER resources %u%% -> %u%% (at most 1 less)", first.user, last.user);
        check(last.user + 1 >= first.user, line);
        wsprintf(line, "the VM Task Manager's blocks %lu (%luK) -> %lu (%luK) (at most 2 more)",
                 first.own_blocks, first.own_kb, last.own_blocks, last.own_kb);
        check(last.own_blocks <= first.own_blocks + 2, line);
        wsprintf(line, "tasks %u -> %u (the same)", first.tasks, last.tasks);
        check(last.tasks == first.tasks, line);
    }
    wsprintf(line, "VMTSOAK done, %d failed\r\n", fails);
    out(line);
    f = _lcreat("VMTSOAK.LOG", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, res, nres);
        _lclose(f);
    }
    return 0;
}
