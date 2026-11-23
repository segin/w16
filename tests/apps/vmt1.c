/*
 * vmt1.c - VMT1.EXE: VMTASK.386's API from a Windows program (ARCHITECTURE.md
 * 15.12; tests/vmtaskrun.sh). Started by WIN.INI run= under Microsoft's
 * WIN386 (Windows 3.1) and w16's, in 386 Enhanced mode, with VMTASK.386
 * loaded (SYSTEM.INI [386Enh] device=vmtask.386).
 *
 * It finds the device (INT 2Fh AX=1684h BX=5716h), logs function 0, then
 * for each step of WIN.INI [vmt1] progs (a list of PIFs, started one by
 * one with WinExec, minimised and not active, so that Windows keeps the
 * focus) takes a snapshot (function 1), waits secs seconds of the
 * real-time clock in GetMessage (Windows idle), takes another, and logs
 * the differences: the system time, the System VM's idle time and idle
 * callbacks, and for each VM its run time and share of the interval, its
 * status, priorities, Get_Time_Slice_Priority's share, SHELL_GetVMInfo's
 * flags, Get_VM_Exec_Time (the current VM only); function 2's busy time
 * over the same interval. Then the checks (lines "CHECK ok ..." or
 * "CHECK FAIL ..."):
 *   - the VMs' run times add up to the system time (within 5 per cent);
 *   - no box: Windows idle at least half the time;
 *   - each box whose PIF title starts with "B" (busy) has at least half
 *     the time, each starting with "P" (polling: VMPOLL finds it idle)
 *     at most a tenth;
 *   - a box whose title has "F" second (full screen) has SHELL_GetVMInfo
 *     bit 2 clear, the others (windowed) set;
 * and a note of how much the System VM's Get_VM_Exec_Time grew beside
 * VMTASK's count of its run time.
 * With end=N (WIN.INI [vmt1]) it then ends the N-th box through
 * function 3 (method=0 Close_VM, 1 Nuke_VM), after checking that ending
 * the System VM is refused, and checks that the VM and its window are
 * gone and the step after.
 * WIN.INI [vmt1]: progs=C:\W\B.PIF,C:\W\P.PIF  secs=6  settle=6  wait=8
 * end=0 method=0; final=S gone=MODULE vms=N (final_checks); launch=PROGRAM
 * (started after wait, before the first step; with late=1 after the
 * boxes)
 * Output: VMT1.LOG (the current directory) at the end, TK.LOG as it goes.
 * Black box (ARCHITECTURE.md 3): only the published API, VMTASK's own
 * interface and the clock.
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
struct vmt_dev {
    char name[8];
    WORD id;
    BYTE major, minor;
    DWORD init_order, services, flags;
};
#pragma pack()
#define MAXV 16
struct snap {
    struct vmt_head h;
    struct vmt_vm v[MAXV];
};
static struct snap s0, s1;

static char res[6144];
static int nres, fails;
static void FAR *entry;
static char titles[MAXV][32];          /* each box's PIF title, by start order */
static DWORD box_handle[MAXV];
static int nbox;

static void out(const char *s)
{
    int n = lstrlen(s);
    log_write(s, n);
    if (nres + n < sizeof(res)) {
        lstrcpy(res + nres, s);
        nres += n;
    }
}

/* the real-time clock's seconds, in binary (register B bit 2 clear: BCD) */
static BYTE rtc(void)
{
    BYTE v;
    while (rtc_read(0x0A) & 0x80)
        ;
    v = rtc_read(0);
    if (!(rtc_read(0x0B) & 4)) v = (BYTE)((v >> 4) * 10 + (v & 15));
    return v;
}

static void pump(int secs)
{
    MSG m;
    BYTE s = rtc(), t;
    int n = 0;
    UINT id = SetTimer(0, 0, 1000, NULL);
    while (n < secs && GetMessage(&m, 0, 0, 0)) {
        DispatchMessage(&m);
        t = rtc();
        if (t != s) {
            /* the seconds gone, not one a message: beside a busy box a
               timer's messages may come late (w16's tree: at half rate) */
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

static int snapshot(struct snap *p)
{
    WORD r[8];
    r[1] = 0;
    r[2] = sizeof(*p);
    r[3] = 0;
    r[4] = 0;
    r[5] = OFFSETOF(p);
    r[6] = SELECTOROF((void FAR *)p);
    return api(1, r);
}

static DWORD busy(void)
{
    WORD r[8];
    r[1] = r[2] = r[3] = r[4] = r[5] = 0;
    r[6] = 0;
    if (!api(2, r)) return 0;
    return MAKELONG(r[0], r[3]);
}

static void check(int ok, const char *what)
{
    char line[160];
    wsprintf(line, "CHECK %s %s\r\n", ok ? (LPSTR)"ok" : (LPSTR)"FAIL", (LPSTR)what);
    out(line);
    if (!ok) fails++;
}

static struct vmt_vm *find(struct snap *p, DWORD h)
{
    int i;
    for (i = 0; i < p->h.rec_count && i < MAXV; i++)
        if (p->v[i].handle == h) return &p->v[i];
    return NULL;
}

/* the box started as number i: the VM created i-th, by handle (VMs are
   matched to the PIFs by their order of creation) */
static void learn_boxes(struct snap *p)
{
    int i, j;
    for (i = 1; i < p->h.rec_count && i < MAXV; i++) {
        for (j = 0; j < nbox; j++)
            if (box_handle[j] == p->v[i].handle) break;
        if (j < nbox) continue;
        for (j = 0; j < nbox; j++)
            if (!box_handle[j]) {
                box_handle[j] = p->v[i].handle;
                break;
            }
    }
}

static const char *title_of(DWORD h)
{
    int j;
    for (j = 0; j < nbox; j++)
        if (box_handle[j] == h) return titles[j];
    return "?";
}

static void step(int boxes, int secs)
{
    char line[256];
    DWORD dsys, didle, dcalls, b0, sum = 0;
    long dbusy;
    int i;
    struct vmt_vm *a, *b;
    b0 = busy();
    if (!snapshot(&s0)) { out("VMT1 snapshot failed\r\n"); fails++; return; }
    pump(secs);
    if (!snapshot(&s1)) { out("VMT1 snapshot failed\r\n"); fails++; return; }
    dbusy = (long)(busy() - b0);    /* (it may go back a millisecond: rounding) */
    learn_boxes(&s1);
    dsys = s1.h.sys_time - s0.h.sys_time;
    didle = s1.h.idle_ms - s0.h.idle_ms;
    dcalls = s1.h.idle_calls - s0.h.idle_calls;
    wsprintf(line, "VMT1 boxes=%d sys=%lu idle=%lu (%lu%%) calls=%lu busy=%ld vms=%u gran=%lu focus=%s\r\n",
             boxes, dsys, didle, dsys ? didle * 100 / dsys : 0, dcalls, dbusy, s1.h.vm_count,
             s1.h.granularity, (LPSTR)(s1.h.focus_vm == s1.h.sys_vm ? "sys" : "box"));
    out(line);
    for (i = 0; i < s1.h.rec_count && i < MAXV; i++) {
        DWORD d;
        b = &s1.v[i];
        a = find(&s0, b->handle);
        d = a ? b->run_ms - a->run_ms : b->run_ms;
        sum += d;
        wsprintf(line, "VMT1  vm id=%lu %s status=%04lX run=%lu (%lu%%) exec=%ld ts=%lX prio=%lu/%lu share=%lu shell=%lX runs=%lu\r\n",
                 b->id, (LPSTR)(i ? title_of(b->handle) : "Windows"), b->status, d, dsys ? d * 100 / dsys : 0,
                 (b->exec_ms == 0xFFFFFFFFul || !a) ? -1L : (long)(b->exec_ms - a->exec_ms),
                 b->ts_flags, b->fg_prio, b->bg_prio, b->cpu_pct, b->shell_flags,
                 a ? b->runs - a->runs : b->runs);
        out(line);
        wsprintf(line, "VMT1  vm id=%lu handle=%08lX pages=%lu out=%lu ems<=%luK xms<=%luK\r\n",
                 b->id, b->handle, b->pages, b->pages_out, b->ems_max, b->xms_max);
        out(line);
        if (i > 0 && b->status != 0x000A) {
            /* (a box not yet started, status Ah, has no memory yet) */
            wsprintf(line, "%s's memory: %lu pages (at least 64: 256 KB)", (LPSTR)title_of(b->handle), b->pages);
            check(b->pages >= 64, line);
        }
        if (i == 0 && a && b->exec_ms != 0xFFFFFFFFul && a->exec_ms != 0xFFFFFFFFul) {
            DWORD e = b->exec_ms - a->exec_ms;
            /* (a note, not a check: Windows 3.1's Get_VM_Exec_Time for
               the System VM did not move at all in these runs) */
            wsprintf(line, "NOTE System VM: Get_VM_Exec_Time %lu ms, run time %lu ms\r\n", e, d);
            out(line);
        }
        if (i > 0) {
            const char *t = title_of(b->handle);
            if (t[0] == 'B') {
                wsprintf(line, "busy box %s has %lu%% (at least 50)", (LPSTR)t, dsys ? d * 100 / dsys : 0);
                check(dsys && d * 2 >= dsys, line);
            }
            if (t[0] == 'P') {
                wsprintf(line, "polling box %s has %lu%% (at most 10)", (LPSTR)t, dsys ? d * 100 / dsys : 0);
                check(d * 10 <= dsys, line);
            }
            if (t[0] && t[1] == 'F') {
                wsprintf(line, "full-screen box %s: SHELL_GetVMInfo %lX, bit 2 clear", (LPSTR)t, b->shell_flags);
                check(b->shell_flags != 0xFFFFFFFFul && !(b->shell_flags & 4), line);
            } else if (t[0] && t[0] != '?') {
                wsprintf(line, "windowed box %s: SHELL_GetVMInfo %lX, bit 2 set", (LPSTR)t, b->shell_flags);
                check(b->shell_flags != 0xFFFFFFFFul && (b->shell_flags & 4), line);
            }
        }
    }
    wsprintf(line, "run times add up to %lu of %lu ms", sum, dsys);
    check(sum + sum / 20 + 20 >= dsys && dsys + dsys / 20 + 20 >= sum, line);
    if (!boxes && s1.h.vm_count == 1) {
        wsprintf(line, "no box: Windows idle %lu%% (at least 50)", dsys ? didle * 100 / dsys : 0);
        check(didle * 2 >= dsys, line);
    }
    wsprintf(line, "busy time (function 2) %ld ms of the System VM's run %lu less idle %lu",
             dbusy, s1.v[0].run_ms - s0.v[0].run_ms, didle);
    {
        long w = (long)((s1.v[0].run_ms - s0.v[0].run_ms) - didle);
        check(dbusy + dbusy / 10 + 30 >= w && w + w / 10 + 30 >= dbusy, line);
    }
}

/* function 4: the devices; VMM first, VMTASK, VPICD, VTD, SHELL there */
static void devices(void)
{
    static BYTE buf[2 + 96 * sizeof(struct vmt_dev)];
    WORD r[8];
    char line[128];
    int i, n, vmm = 0, self = 0, pic = 0, vtd = 0, shell = 0;
    r[1] = 0;
    r[2] = sizeof(buf);
    r[3] = r[4] = 0;
    r[5] = OFFSETOF((void FAR *)buf);
    r[6] = SELECTOROF((void FAR *)buf);
    if (!api(4, r)) { check(0, "function 4 (the devices)"); return; }
    n = *(WORD *)buf;
    for (i = 0; i < n; i++) {
        struct vmt_dev *d = (struct vmt_dev *)(buf + 2) + i;
        char nm[9];
        int k;
        for (k = 0; k < 8; k++) nm[k] = d->name[k];
        nm[8] = 0;
        wsprintf(line, "VMT1 device %s id=%04X %u.%02u order=%08lX services=%lu flags=%lX\r\n",
                 (LPSTR)nm, d->id, d->major, d->minor, d->init_order, d->services, d->flags);
        out(line);
        if (i == 0 && d->id == 1) vmm = 1;
        if (d->id == 0x5716) self = 1;
        if (d->id == 0x0003) pic = 1;
        if (d->id == 0x0005) vtd = 1;
        if (d->id == 0x0017) shell = 1;
    }
    wsprintf(line, "devices: %d, the VMM first, VMTASK, VPICD, VTD, SHELL among them", n);
    check(n >= 8 && vmm && self && pic && vtd && shell, line);
}

/* the top-level windows of WINOLDAP tasks (the DOS boxes' windows) */
static int box_windows(void)
{
    HWND w;
    int n = 0;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        if (!GetWindow(w, GW_OWNER) && IsWinOldApTask(GetWindowTask(w))) n++;
    return n;
}

static int have_vm(DWORD h)
{
    return snapshot(&s1) && find(&s1, h) != NULL;
}

/* function 3: end box i (1-based) with method (0 Close_VM, 1 Nuke_VM);
   first the System VM, which must be refused */
static void end_step(int i, int method, int secs)
{
    char line[160];
    WORD r[8];
    DWORD h = box_handle[i - 1];
    int w0 = box_windows(), w1, gone;
    r[1] = LOWORD(s1.h.sys_vm);
    r[3] = HIWORD(s1.h.sys_vm);
    r[2] = 0;
    r[4] = r[5] = r[6] = 0;
    api(3, r);
    wsprintf(line, "end the System VM: ax=%04X cf=%d (refused: 1, carry)", r[0], r[7] & 1);
    check(r[0] == 1 && (r[7] & 1), line);
    r[1] = LOWORD(h);
    r[3] = HIWORD(h);
    r[2] = method;
    r[4] = 3000;
    r[5] = r[6] = 0;
    api(3, r);
    wsprintf(line, "VMT1 end %s (vm %08lX) by %s: ax=%04X cf=%d\r\n", (LPSTR)titles[i - 1], h,
             (LPSTR)(method ? "Nuke_VM" : "Close_VM"), r[0], r[7] & 1);
    out(line);
    pump(secs);
    gone = !have_vm(h);
    w1 = box_windows();
    wsprintf(line, "%s's VM gone after %d s (box windows %d before, %d after)", (LPSTR)titles[i - 1], secs, w0, w1);
    check(gone, line);
    wsprintf(line, "%s's window closed", (LPSTR)titles[i - 1]);
    check(w1 == w0 - 1, line);
    step(nbox - 1, secs);
}

/* the tasks, for comparing with the VM Task Manager's IDs */
static void list_tasks(void)
{
    TASKENTRY te;
    char line[96];
    BOOL any;
    te.dwSize = sizeof(te);
    for (any = TaskFirst(&te); any; any = TaskNext(&te)) {
        wsprintf(line, "VMT1 task %s hTask=%04X hModule=%04X hInst=%04X\r\n", (LPSTR)te.szModule,
                 (WORD)te.hTask, (WORD)te.hModule, (WORD)te.hInst);
        out(line);
    }
}

/* WIN.INI [vmt1] final=S: S seconds later (another program, the VM Task
   Manager driven by tests/vmtaskend.sh, has ended things meanwhile), the
   module gone= must be gone and vms= VMs left */
static void final_checks(void)
{
    char gone[32], line[128];
    int secs = GetProfileInt("vmt1", "final", 0), vms = GetProfileInt("vmt1", "vms", 0);
    if (!secs) return;
    pump(secs);
    list_tasks();
    GetProfileString("vmt1", "gone", "", gone, sizeof(gone));
    if (gone[0]) {
        wsprintf(line, "%s has ended", (LPSTR)gone);
        check(!GetModuleHandle(gone), line);
    }
    if (vms && snapshot(&s1)) {
        int i;
        for (i = 0; i < s1.h.rec_count && i < MAXV; i++) {
            wsprintf(line, "VMT1 left: vm id=%lu handle=%08lX\r\n", s1.v[i].id, s1.v[i].handle);
            out(line);
        }
        wsprintf(line, "%u VMs left (%d expected)", s1.h.vm_count, vms);
        check(s1.h.vm_count == vms, line);
    }
}

/* [vmt1] launch=: a program started (after the wait, or with late=1
   after the boxes), so that it is the active window then */
static void launch_now(void)
{
    char launch[128], line[180];
    GetProfileString("vmt1", "launch", "", launch, sizeof(launch));
    if (!launch[0]) return;
    wsprintf(line, "VMT1 launch %s %u\r\n", (LPSTR)launch, WinExec(launch, SW_SHOWNORMAL));
    out(line);
    pump(2);
}

int __cdecl tmain(void)
{
    char progs[256], line[160], *p;
    int secs, settle, n = 0, endbox, method;
    WORD r[8];
    HFILE f;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    GetProfileString("vmt1", "progs", "", progs, sizeof(progs));
    secs = GetProfileInt("vmt1", "secs", 6);
    endbox = GetProfileInt("vmt1", "end", 0);
    method = GetProfileInt("vmt1", "method", 0);
    settle = GetProfileInt("vmt1", "settle", 6);
    out("VMT1 start\r\n");
    pump(GetProfileInt("vmt1", "wait", 8));
    r[0] = 0x1684;
    r[1] = 0x5716;
    r[2] = r[3] = r[4] = r[5] = r[6] = 0;
    vx_int2f(r);
    entry = MAKELP(r[6], r[5]);
    wsprintf(line, "VMT1 1684h: %04X:%04X winflags=%04lX\r\n", r[6], r[5], GetWinFlags());
    out(line);
    if (!r[6] && !r[5]) {
        check(0, "VMTASK.386's entry");
        goto done;
    }
    r[1] = r[2] = r[3] = r[4] = r[5] = r[6] = 0;
    api(0, r);
    wsprintf(line, "VMT1 version ax=%04X bx=%04X cx=%04X dx=%04X cf=%d\r\n", r[0], r[1], r[2], r[3], r[7] & 1);
    out(line);
    check(r[0] == 0x0100 && r[1] == 0x564D && r[2] == 0x544B && !(r[7] & 1), "function 0: 1.00, VMTK");
    if (!GetProfileInt("vmt1", "late", 0)) launch_now();
    list_tasks();
    devices();
    step(0, secs);
    for (p = progs; *p && nbox < MAXV; ) {
        char pif[128];
        int i = 0;
        UINT rc;
        while (*p && *p != ',' && i < sizeof(pif) - 1) pif[i++] = *p++;
        pif[i] = 0;
        if (*p == ',') p++;
        /* the title: the PIF's file name without the directory and .PIF */
        {
            char *t = pif + i, *d = titles[nbox];
            int k = 0;
            while (t > pif && t[-1] != '\\') t--;
            while (*t && *t != '.' && k < 31) d[k++] = *t++;
            d[k] = 0;
        }
        rc = WinExec(pif, SW_SHOWMINNOACTIVE);
        wsprintf(line, "VMT1 WinExec %s %u\r\n", (LPSTR)pif, rc);
        out(line);
        if (rc < 32) { fails++; break; }
        nbox++;
        n++;
        pump(settle);
        step(n, secs);
    }
    if (GetProfileInt("vmt1", "late", 0)) launch_now();
    if (endbox > 0 && endbox <= nbox && have_vm(box_handle[endbox - 1]))
        end_step(endbox, method, secs);
    final_checks();
done:
    wsprintf(line, "VMT1 done, %d failed\r\n", fails);
    out(line);
    f = _lcreat("VMT1.LOG", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, res, nres);
        _lclose(f);
    }
    return 0;
}
