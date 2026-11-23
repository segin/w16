/*
 * vmtask.c - VM Task Manager (VMTASK.EXE), a w16 extension: how the
 * processor's time is shared in 386 Enhanced mode, per virtual machine
 * and, within the System VM, per Windows task (ARCHITECTURE.md 15.12).
 * Windows 3.1 had nothing like it; it changes nothing for any other
 * program. Its look follows 3.1's own small utilities: an overlapped
 * window with a menu, the system font, system colours.
 *
 * The window: a list, one row per VM ("Windows" for the System VM, each
 * DOS box by its window's title, "(full screen)" after a full-screen
 * one's), and under "Windows" one row per Windows task (its module
 * name). Columns: the share of the processor over the last interval,
 * the time used in all, the status (Running: it has the execution focus;
 * Background, Suspended, Exclusive, Idle: it has given up its time
 * slice), the priorities (foreground/background). Under the list a graph
 * of the processor's use (Options, Show History) and a status line with
 * Windows' idle time.
 *
 * Where the numbers come from:
 *   - VMTASK.386 (win386/vxd/vmtask.c; INT 2Fh 1684h, device 5716h), in
 *     386 Enhanced mode: a snapshot of every VM at each interval (its
 *     run time, CB_VM_Status, Get_Time_Slice_Priority, SHELL_GetVMInfo),
 *     the System VM's idle time, the system time. A VM's share is its
 *     run time's growth over the system time's; Windows' is the System
 *     VM's less its idle time.
 *   - TOOLHELP: NotifyRegister with NF_TASKSWITCH; at NFY_TASKIN and
 *     NFY_TASKOUT (vmtaskhk.asm) the time a task ran is added to its
 *     count: VMTASK.386's function 2 (the System VM's busy time, so that
 *     neither idling nor other VMs' slices count), else TimerCount less
 *     the idle time that KERNEL's INT 2Fh AX=1689h calls mark (a
 *     protected-mode INT 2Fh handler: Standard mode, or no VMTASK.386).
 *     TaskFirst/TaskNext and TaskFindHandle for the tasks and names.
 *   - Which DOS box is which VM: VMTASK.386 gives no window, so the boxes'
 *     windows (top-level windows of WINOLDAP tasks: IsWinOldApTask) are
 *     matched to the VMs: a box window that is active while a DOS VM has
 *     the execution focus is that VM's; the rest are paired in the order
 *     they appeared (the oldest window, lowest in the Z order at the
 *     first look, with the VM created first).
 * Without VMTASK.386 (or in Standard mode) a line says why no VMs are
 * shown, and the Windows tasks are shown alone.
 *
 * The Task menu, three buttons, Del and a shortcut menu (a right click,
 * or Shift+F10) act on the selected row: Switch To; End Task (WM_CLOSE,
 * then TOOLHELP's TerminateApp if it has not ended 5 s later, each step
 * confirmed, twice for a task w16 needs); End VM (Windows 3.1's
 * Terminate warning, then VMTASK.386's function 3 with Nuke_VM, as SHELL
 * does for Terminate; never the System VM); Details and Copy Details
 * (the IDs and what the device says). Show IDs adds the IDs as columns.
 * Settings (WIN.INI [VMTask]): Interval (ms: 500, 1000, 2000, 5000),
 * OnTop, Tasks, Graph, IDs, and the window's place (Left, Top, Width,
 * Height).
 * Black box (ARCHITECTURE.md 3): the SDK's USER, KERNEL and TOOLHELP,
 * VMTASK's own interface.
 */
#include <windows.h>
#include <toolhelp.h>
#include <shellapi.h>
#include "vmtask.h"

extern BOOL FAR PASCAL IsWinOldApTask(HTASK);           /* KERNEL.158 */
extern void FAR PASCAL SwitchToThisWindow(HWND, BOOL);  /* USER.172 */
static BOOL mem_done;                   /* (memory per task: walk_memory) */
static DWORD task_kb(WORD task);
/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

#define APPNAME "VMTask"
#define MAXV    32
#define MAXROWS 112
#define NHIST   160

enum { R_MSG, R_VM, R_TASK };
struct row {
    BYTE kind, indent;
    BYTE sys;                           /* the Windows row (the System VM) */
    char name[48];
    int pct;                            /* tenths of a per cent; -1: none */
    DWORD ms;                           /* the time in all; 0xFFFFFFFF: none */
    char status[16];
    char prio[12];
    DWORD key;
    /* the IDs: a VM's handle and CB_VMID (0: not known), its box window;
       a task's handle, module and instance */
    DWORD vmh, vmid;
    WORD hwnd, task, hmod, hinst;
    /* a VM's CB_VM_Status, share and SHELL_GetVMInfo flags (Details) */
    DWORD vmstat, share, shflags, fg, bg;
    /* memory: KB (0xFFFFFFFF: not known); a VM's pages outside its first
       MB and its EMS and XMS limits (Details) */
    DWORD mem_kb, out_kb, ems_max, xms_max;
};

static HINSTANCE hinst;
static HWND hwnd, hlist;
static int cw, ch, hdr_h, stat_h, graph_h;
static int interval = 1000, ontop, showtasks = 1, showgraph = 1, showids = 1;
static HWND hbtn[3];                    /* Switch To, End Task, End VM */
static int btn_h;
static FARPROC old_list, list_thunk;
/* a task asked to close (End Task): asked again after END_WAIT ms */
#define END_WAIT 5000
static WORD end_task_h;
static char end_name[48];
/* the columns' x (pixels): name, CPU's right edge, time's right edge,
   status, priority, the end without the IDs; ID's right edge, handle,
   module, window, the end with them */
enum { X_NAME, X_CPU, X_TIME, X_STAT, X_PRIO, X_MEM, X_END1, X_ID, X_HAND, X_MOD, X_WIN, X_END2 };
static int colx[X_END2 + 1];
static int have_vxd, enhanced;
static struct row rows[MAXROWS];
static int nrows, nlisted = -1;
static char statline[96];
static BYTE hist[NHIST];
static int nhist;

/* the last snapshot */
static struct {
    struct vmt_head h;
    struct vmt_vm v[MAXV];
} snap;
static int have_prev;
static struct { DWORD handle, run; } prev[MAXV];
static int nprev;
static DWORD prev_sys, prev_idle, prev_wall, prev_hidle;
static DWORD last_ms[VMT_NSLOTS];       /* each slot's count at the last sample */
static WORD last_task[VMT_NSLOTS];
static DWORD task_delta[VMT_NSLOTS];

/* DOS boxes' windows */
static struct { HWND hwnd; WORD seq; DWORD vm; BYTE seen; } boxes[MAXV];
static int nboxes;
static WORD nextseq;

/* ---- times ---- */

static DWORD wall_ms(void)
{
    TIMERINFO ti;
    ti.dwSize = sizeof(ti);
    if (!TimerCount(&ti)) return GetTickCount();
    return ti.dwmsSinceStart;
}

static DWORD busy_ms(void)
{
    WORD r[7];
    if (have_vxd) {
        r[0] = 2;
        r[1] = r[2] = r[3] = r[4] = r[5] = r[6] = 0;
        if (vmt_api(r)) return MAKELONG(r[0], r[3]);
    }
    return wall_ms() - vmt_hidle;
}

static int slot_of(WORD task)
{
    int i, f = -1;
    for (i = 0; i < VMT_NSLOTS - 1; i++) {
        if (vmt_slots[i].task == task) return i;
        if (f < 0 && !vmt_slots[i].task) f = i;
    }
    if (f < 0) return VMT_NSLOTS - 1;
    vmt_slots[f].task = task;
    vmt_slots[f].ms = 0;
    return f;
}

/* this task is running: its time so far into its count */
static void flush_self(void)
{
    WORD me = (WORD)GetCurrentTask();
    DWORD t;
    if (vmt_cur != me) return;
    t = busy_ms();
    if ((long)(t - vmt_tin) > 0) vmt_slots[slot_of(me)].ms += t - vmt_tin;
    vmt_tin = t;
}

/* ---- formatting ---- */

static void fmt_time(char *s, DWORD ms)
{
    DWORD sec = ms / 1000;
    wsprintf(s, "%lu:%02u:%02u", sec / 3600, (WORD)(sec / 60 % 60), (WORD)(sec % 60));
}

static int pct10(DWORD part, DWORD whole)
{
    if (!whole) return 0;
    if (part > whole) part = whole;
    return (int)((part * 1000 + whole / 2) / whole);
}

static struct row *add_row(int kind, int indent, LPCSTR name, DWORD key)
{
    struct row *r;
    if (nrows >= MAXROWS) return NULL;
    r = &rows[nrows++];
    r->kind = (BYTE)kind;
    r->indent = (BYTE)indent;
    lstrcpyn(r->name, name, sizeof(r->name));
    r->pct = -1;
    r->ms = 0xFFFFFFFFul;
    r->status[0] = r->prio[0] = 0;
    r->key = key;
    r->sys = 0;
    r->vmh = r->vmid = 0;
    r->hwnd = r->task = r->hmod = r->hinst = 0;
    r->vmstat = r->share = r->fg = r->bg = 0;
    r->shflags = 0xFFFFFFFFul;
    r->mem_kb = 0xFFFFFFFFul;
    r->out_kb = r->ems_max = r->xms_max = 0;
    return r;
}

/* ---- DOS boxes and their windows ---- */

static void scan_boxes(void)
{
    HWND z[MAXV], w;
    int nz = 0, i, j;
    for (i = 0; i < nboxes; i++) boxes[i].seen = 0;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (GetWindow(w, GW_OWNER) || !IsWindowVisible(w)) continue;
        if (!IsWinOldApTask(GetWindowTask(w))) continue;
        for (i = 0; i < nboxes && boxes[i].hwnd != w; i++)
            ;
        if (i < nboxes) boxes[i].seen = 1;
        else if (nz < MAXV) z[nz++] = w;
    }
    for (i = 0, j = 0; i < nboxes; i++)
        if (boxes[i].seen) boxes[j++] = boxes[i];
    nboxes = j;
    /* new ones, the lowest in the Z order first */
    while (nz > 0 && nboxes < MAXV) {
        w = z[--nz];
        boxes[nboxes].hwnd = w;
        boxes[nboxes].seq = nextseq++;
        boxes[nboxes].vm = 0;
        boxes[nboxes].seen = 1;
        nboxes++;
    }
}

static int vm_present(DWORD vm)
{
    int i;
    for (i = 1; i < snap.h.rec_count && i < MAXV; i++)
        if (snap.v[i].handle == vm) return 1;
    return 0;
}

static int box_of(DWORD vm)
{
    int i;
    for (i = 0; i < nboxes; i++)
        if (boxes[i].vm == vm) return i;
    return -1;
}

static void match_boxes(void)
{
    HWND act = GetActiveWindow();
    int i, j, best;
    for (i = 0; i < nboxes; i++)
        if (boxes[i].vm && !vm_present(boxes[i].vm)) boxes[i].vm = 0;
    /* the active box window and the VM with the execution focus */
    if (snap.h.focus_vm != snap.h.sys_vm && act) {
        HTASK t = GetWindowTask(act);
        for (i = 0; i < nboxes; i++)
            if (boxes[i].hwnd == act || GetWindowTask(boxes[i].hwnd) == t) {
                for (j = 0; j < nboxes; j++)
                    if (boxes[j].vm == snap.h.focus_vm) boxes[j].vm = 0;
                boxes[i].vm = snap.h.focus_vm;
                break;
            }
    }
    /* the rest in order: the oldest VM without a window to the oldest
       window without a VM */
    for (;;) {
        DWORD vm = 0, created = 0;
        for (i = 1; i < snap.h.rec_count && i < MAXV; i++) {
            struct vmt_vm *v = &snap.v[i];
            if (box_of(v->handle) >= 0) continue;
            if (!vm || v->created < created) { vm = v->handle; created = v->created; }
        }
        if (!vm) break;
        best = -1;
        for (i = 0; i < nboxes; i++)
            if (!boxes[i].vm && (best < 0 || boxes[i].seq < boxes[best].seq)) best = i;
        if (best < 0) break;
        boxes[best].vm = vm;
    }
}

/* ---- a sample ---- */

static DWORD prev_run(DWORD handle, int *found)
{
    int i;
    for (i = 0; i < nprev; i++)
        if (prev[i].handle == handle) { *found = 1; return prev[i].run; }
    *found = 0;
    return 0;
}

static void vm_status(char *s, struct vmt_vm *v, int focus)
{
    DWORD st = v->status;
    LPCSTR t;
    if (st & VMS_NOTEXEC) t = "Ending";
    else if (st & VMS_SUSPENDED) t = "Suspended";
    else if (st & VMS_IDLE) t = "Idle";
    else if (focus && (st & VMS_EXCLUSIVE)) t = "Exclusive";
    else if (focus) t = "Running";
    else if (st & VMS_BACKGROUND) t = "Background";
    else t = "Waiting";
    lstrcpy(s, t);
}

static void task_rows(DWORD dwall, DWORD scale_num, DWORD scale_den)
{
    TASKENTRY te;
    int i, any;
    char name[48];
    struct row *r;
    if (!showtasks) return;
    te.dwSize = sizeof(te);
    for (any = TaskFirst(&te); any; any = TaskNext(&te)) {
        DWORD d = 0, total = 0;
        for (i = 0; i < VMT_NSLOTS; i++)
            if (vmt_slots[i].task == (WORD)te.hTask) {
                d = task_delta[i];
                total = vmt_slots[i].ms;
                break;
            }
        lstrcpyn(name, te.szModule, sizeof(name));
        r = add_row(R_TASK, 1, name, (WORD)te.hTask);
        if (!r) break;
        r->task = (WORD)te.hTask;
        r->hmod = (WORD)te.hModule;
        r->hinst = (WORD)te.hInst;
        if (mem_done) r->mem_kb = task_kb(r->task);
        if (scale_den && scale_num < scale_den) d = d * scale_num / scale_den;
        r->pct = have_prev ? pct10(d, dwall) : -1;
        r->ms = total;
        lstrcpy(r->status, (WORD)te.hTask == (WORD)GetCurrentTask() ? "Running" : "Ready");
        if (!te.wcEvents && (WORD)te.hTask != (WORD)GetCurrentTask()) lstrcpy(r->status, "Waiting");
    }
}

/* ---- memory per task: TOOLHELP's global heap walk, summed by owner ---- */
#define MEM_EVERY 5000                  /* ms between walks (a walk visits every block) */
#define NTMEM 64
static struct { WORD task, pdb, hmod; DWORD bytes; } tmem[NTMEM];
static int ntmem;
static DWORD win_bytes, mem_when;

/* a task's PDB: the TDB's word at 60h (Schulman, Undocumented Windows,
   the Task Database), read with the task handle as a selector */
static WORD task_pdb(WORD task)
{
    WORD FAR *p = (WORD FAR *)MAKELP(task, 0x60);
    if (!task || !IsTask((HTASK)task)) return 0;
    return *p;
}

/* each task's blocks: those its PDB owns, and its module's segments
   (counted for every task of the module: approximate for programs run
   twice); the Windows row's: every block in use */
static void walk_memory(void)
{
    TASKENTRY te;
    GLOBALENTRY ge;
    BOOL any;
    int i;
    ntmem = 0;
    te.dwSize = sizeof(te);
    for (any = TaskFirst(&te); any && ntmem < NTMEM; any = TaskNext(&te)) {
        tmem[ntmem].task = (WORD)te.hTask;
        tmem[ntmem].pdb = task_pdb((WORD)te.hTask);
        tmem[ntmem].hmod = (WORD)te.hModule;
        tmem[ntmem].bytes = 0;
        ntmem++;
    }
    win_bytes = 0;
    ge.dwSize = sizeof(ge);
    for (any = GlobalFirst(&ge, GLOBAL_ALL); any; any = GlobalNext(&ge, GLOBAL_ALL)) {
        WORD o = (WORD)ge.hOwner;
        if (ge.wType == GT_FREE) continue;
        win_bytes += ge.dwBlockSize;
        for (i = 0; i < ntmem; i++)
            if (o && ((o | 1) == (tmem[i].pdb | 1) || (o | 1) == (tmem[i].hmod | 1)))
                tmem[i].bytes += ge.dwBlockSize;
    }
    mem_done = TRUE;
}

static DWORD task_kb(WORD task)
{
    int i;
    for (i = 0; i < ntmem; i++)
        if (tmem[i].task == task) return (tmem[i].bytes + 1023) / 1024;
    return 0xFFFFFFFFul;
}

/* the task counts' growth since the last sample; dead tasks' slots freed */
static DWORD task_deltas(void)
{
    int i;
    DWORD sum = 0;
    for (i = 0; i < VMT_NSLOTS; i++) {
        WORD t = vmt_slots[i].task;
        if (t && !IsTask((HTASK)t)) {
            if (vmt_cur == t) vmt_cur = 0;
            vmt_slots[i].task = 0;
            vmt_slots[i].ms = 0;
            t = 0;
        }
        if (t != last_task[i]) { last_task[i] = t; last_ms[i] = 0; }
        task_delta[i] = t ? vmt_slots[i].ms - last_ms[i] : 0;
        last_ms[i] = vmt_slots[i].ms;
        sum += task_delta[i];
    }
    return sum;
}

static void sample(void)
{
    WORD r[7];
    struct row *w;
    DWORD dsys = 0, didle = 0, dwall, tsum, wall, busy_win = 0;
    int i, j, n, o, ok = 0, found, order[MAXV];
    char name[64];

    flush_self();
    tsum = task_deltas();
    wall = wall_ms();
    if (!mem_done || wall - mem_when >= MEM_EVERY) {
        walk_memory();
        mem_when = wall;
    }
    dwall = wall - prev_wall;
    nrows = 0;
    if (have_vxd) {
        r[0] = 1;
        r[1] = 0;
        r[2] = sizeof(snap);
        r[3] = r[4] = 0;
        r[5] = OFFSETOF((void FAR *)&snap);
        r[6] = SELECTOROF((void FAR *)&snap);
        ok = vmt_api(r) || snap.h.rec_count;
        /* (a device whose records differ is not this program's) */
        if (snap.h.rec_size != sizeof(struct vmt_vm)) ok = 0;
    }
    if (ok) {
        DWORD sysrun = 0, dsysrun = 0;
        dsys = snap.h.sys_time - prev_sys;
        didle = snap.h.idle_ms - prev_idle;
        if (!have_prev) dsys = 0;
        scan_boxes();
        match_boxes();
        /* the System VM first, then the DOS VMs by ID (the order they
           were made) */
        n = snap.h.rec_count < MAXV ? snap.h.rec_count : MAXV;
        for (i = 0; i < n; i++) order[i] = i;
        for (i = 2; i < n; i++)
            for (j = i; j > 1 && snap.v[order[j - 1]].id > snap.v[order[j]].id; j--) {
                int k = order[j];
                order[j] = order[j - 1];
                order[j - 1] = k;
            }
        for (o = 0; o < n; o++) {
            struct vmt_vm *v = &snap.v[order[o]];
            DWORD p = prev_run(v->handle, &found), d = found ? v->run_ms - p : 0;
            int focus = v->handle == snap.h.focus_vm;
            if (order[o] == 0) {
                sysrun = v->run_ms;
                dsysrun = d;
                busy_win = (long)(d - didle) > 0 ? d - didle : 0;
                w = add_row(R_VM, 0, "Windows", v->handle);
                if (!w) break;
                w->sys = 1;
                w->pct = have_prev ? pct10(busy_win, dsys) : -1;
                w->ms = (long)(sysrun - snap.h.idle_ms) > 0 ? sysrun - snap.h.idle_ms : 0;
            } else {
                int b = box_of(v->handle);
                name[0] = 0;
                if (b >= 0) GetWindowText(boxes[b].hwnd, name, 40);
                if (!name[0]) wsprintf(name, "DOS VM %lu", v->id);
                if (v->shell_flags != 0xFFFFFFFFul && !(v->shell_flags & SGVMI_WINDOWED))
                    lstrcat(name, " (full screen)");
                w = add_row(R_VM, 0, name, v->handle);
                if (!w) break;
                w->pct = have_prev && found ? pct10(d, dsys) : -1;
                w->ms = v->run_ms;
                if (b >= 0) w->hwnd = (WORD)boxes[b].hwnd;
            }
            w->vmh = v->handle;
            w->vmid = v->id;
            w->vmstat = v->status;
            w->share = v->cpu_pct;
            w->shflags = v->shell_flags;
            w->fg = v->fg_prio;
            w->bg = v->bg_prio;
            /* a DOS VM: _GetVMPgCount's pages; the Windows row: the
               global heap in use (the System VM's count holds Windows'
               free memory and the VMM's too) */
            if (order[o] != 0) w->mem_kb = v->pages * 4;
            else if (mem_done) w->mem_kb = (win_bytes + 1023) / 1024;
            w->out_kb = v->pages_out * 4;
            w->ems_max = v->ems_max;
            w->xms_max = v->xms_max;
            vm_status(w->status, v, focus);
            wsprintf(w->prio, "%lu/%lu", v->fg_prio, v->bg_prio);
            if (order[o] == 0)
                task_rows(dsys, busy_win, tsum);
        }
        (void)dsysrun;
        nprev = 0;
        for (i = 0; i < snap.h.rec_count && i < MAXV; i++) {
            prev[nprev].handle = snap.v[i].handle;
            prev[nprev].run = snap.v[i].run_ms;
            nprev++;
        }
        prev_sys = snap.h.sys_time;
        prev_idle = snap.h.idle_ms;
        if (have_prev && dsys)
            wsprintf(statline, "Windows idle: %d%%    In use: %d%%",
                     (pct10(didle, dsys) + 5) / 10, 100 - (pct10(didle, dsys) + 5) / 10);
    } else {
        DWORD dh = vmt_hidle - prev_hidle;
        if (!enhanced)
            add_row(R_MSG, 0, "Standard mode: no virtual machines to show.", 0);
        else {
            add_row(R_MSG, 0, "VMTASK.386 is not loaded: virtual", 0);
            add_row(R_MSG, 0, "machines are not shown.", 0);
        }
        w = add_row(R_VM, 0, "Windows", 1);
        if (w) w->sys = 1;
        if (w && mem_done) w->mem_kb = (win_bytes + 1023) / 1024;
        if (w && have_prev) {
            w->pct = pct10(dwall > dh ? dwall - dh : 0, dwall);
            lstrcpy(w->status, "Running");
        }
        task_rows(dwall, 0, 0);
        if (have_prev && dwall)
            wsprintf(statline, "Windows idle: %d%%", (pct10(dh, dwall) + 5) / 10);
        dsys = dwall;
        didle = dh;
    }
    prev_hidle = vmt_hidle;
    prev_wall = wall;
    if (have_prev && dsys) {
        if (nhist < NHIST) nhist++;
        for (i = NHIST - 1; i > 0; i--) hist[i] = hist[i - 1];
        hist[0] = (BYTE)(100 - (pct10(didle, dsys) + 5) / 10);
    }
    if (!have_prev) lstrcpy(statline, "Measuring...");
    have_prev = 1;
}

/* ---- the window ---- */

static struct row shown[MAXROWS];       /* what the list shows */

static int same(const struct row *a, const struct row *b)
{
    const BYTE *p = (const BYTE *)a, *q = (const BYTE *)b;
    int i;
    for (i = 0; i < sizeof(*a); i++)
        if (p[i] != q[i]) return 0;
    return 1;
}

static void fill_list(void)
{
    int i, top, sel;
    DWORD key = 0;
    if (nrows == nlisted) {
        /* the selection stays on its task or VM when rows move */
        sel = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < nrows && shown[sel].key != rows[sel].key)
            for (i = 0; i < nrows; i++)
                if (rows[i].key == shown[sel].key) { SendMessage(hlist, LB_SETCURSEL, i, 0); break; }
        /* only the rows that changed (w16's GDI is slow enough that
           redrawing them all each second showed as busy time) */
        for (i = 0; i < nrows; i++)
            if (!same(&rows[i], &shown[i])) {
                RECT r;
                if (SendMessage(hlist, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&r) != LB_ERR)
                    InvalidateRect(hlist, &r, FALSE);
                shown[i] = rows[i];
            }
        return;
    }
    top = (int)SendMessage(hlist, LB_GETTOPINDEX, 0, 0);
    sel = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < nlisted) key = shown[sel].key;
    for (i = 0; i < nrows; i++) shown[i] = rows[i];
    SendMessage(hlist, WM_SETREDRAW, FALSE, 0);
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < nrows; i++)
        SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(long)i);
    nlisted = nrows;
    SendMessage(hlist, LB_SETTOPINDEX, top < nrows ? top : 0, 0);
    if (key)
        for (i = 0; i < nrows; i++)
            if (rows[i].key == key) { SendMessage(hlist, LB_SETCURSEL, i, 0); break; }
    /* a row is always selected (the menu, the buttons and Del act on it):
       the first that is not a message */
    if ((int)SendMessage(hlist, LB_GETCURSEL, 0, 0) < 0)
        for (i = 0; i < nrows; i++)
            if (rows[i].kind != R_MSG) { SendMessage(hlist, LB_SETCURSEL, i, 0); break; }
    SendMessage(hlist, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hlist, NULL, TRUE);
}

/* the graph's inside (black), in client coordinates; FALSE: none */
static BOOL graph_rect(RECT *g)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    SetRect(g, cw, rc.bottom - stat_h - graph_h + 4, rc.right - cw, rc.bottom - stat_h - 4);
    return showgraph && g->right - g->left > 4 && g->bottom - g->top > 4;
}

static char shown_stat[sizeof(statline)];

static void set_buttons(void);

static void refresh(void)
{
    RECT rc, g;
    int added = nhist, i;
    sample();
    fill_list();
    set_buttons();
    /* the graph: what is there moves left a step, and only the new step
       is drawn (redrawing all of it each second, with w16's GDI, showed
       as a tenth of the processor) */
    if (graph_rect(&g) && (nhist != added || hist[0] != hist[1] || nhist == NHIST)) {
        ScrollWindow(hwnd, -4, 0, &g, &g);
        rc = g;
        rc.left = rc.right - 6;
        InvalidateRect(hwnd, &rc, FALSE);
    }
    for (i = 0; statline[i] == shown_stat[i] && statline[i]; i++)
        ;
    if (statline[i] != shown_stat[i]) {
        lstrcpy(shown_stat, statline);
        GetClientRect(hwnd, &rc);
        rc.top = rc.bottom - stat_h + 1;
        InvalidateRect(hwnd, &rc, FALSE);
    }
}

#define COL(i) (colx[i])

static void text_at(HDC dc, int x, int y, LPCSTR s, int right)
{
    int n = lstrlen(s);
    if (right) x -= LOWORD(GetTextExtent(dc, s, n));
    TextOut(dc, x, y, s, n);
}

static void draw_item(DRAWITEMSTRUCT FAR *d)
{
    struct row *r;
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    char s[24];
    int sel = (d->itemState & ODS_SELECTED) != 0, y;
    COLORREF bk = GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    COLORREF fg = GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    HBRUSH br;
    if ((int)d->itemID < 0 || (int)d->itemID >= nrows) {
        br = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        return;
    }
    r = &rows[d->itemID];
    br = CreateSolidBrush(bk);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, r->kind == R_MSG && !sel ? GetSysColor(COLOR_GRAYTEXT) : fg);
    y = rc.top;
    if (r->kind == R_MSG) {
        text_at(dc, rc.left + cw, y, r->name, 0);
    } else {
        int x = rc.left + COL(X_NAME) + r->indent * 2 * cw;
        HRGN clip = CreateRectRgn(rc.left, rc.top, rc.left + COL(X_CPU) - 5 * cw, rc.bottom);
        SelectClipRgn(dc, clip);
        text_at(dc, x, y, r->name, 0);
        SelectClipRgn(dc, NULL);
        DeleteObject(clip);
        if (r->pct >= 0) {
            wsprintf(s, "%d%%", (r->pct + 5) / 10);
            text_at(dc, rc.left + COL(X_CPU), y, s, 1);
        }
        if (r->ms != 0xFFFFFFFFul) {
            fmt_time(s, r->ms);
            text_at(dc, rc.left + COL(X_TIME), y, s, 1);
        }
        text_at(dc, rc.left + COL(X_STAT), y, r->status, 0);
        text_at(dc, rc.left + COL(X_PRIO), y, r->prio, 0);
        if (r->mem_kb != 0xFFFFFFFFul) {
            wsprintf(s, "%lu K", r->mem_kb);
            text_at(dc, rc.left + COL(X_MEM), y, s, 1);
        }
        if (showids) {
            if (r->vmid) {
                wsprintf(s, "%lu", r->vmid);
                text_at(dc, rc.left + COL(X_ID), y, s, 1);
            }
            s[0] = 0;
            if (r->vmh) wsprintf(s, "%08lX", r->vmh);
            else if (r->task) wsprintf(s, "%04X", r->task);
            text_at(dc, rc.left + COL(X_HAND), y, s, 0);
            if (r->hmod) {
                wsprintf(s, "%04X", r->hmod);
                text_at(dc, rc.left + COL(X_MOD), y, s, 0);
            }
            if (r->hwnd) {
                wsprintf(s, "%04X", r->hwnd);
                text_at(dc, rc.left + COL(X_WIN), y, s, 0);
            }
        }
    }
    if (d->itemState & ODS_FOCUS) DrawFocusRect(dc, &d->rcItem);
}

static void paint(HWND h)
{
    PAINTSTRUCT ps;
    RECT rc, r;
    HDC dc = BeginPaint(h, &ps);
    HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    HPEN shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)), old;
    RECT g;
    int i, n;
    char s[24];
    GetClientRect(h, &rc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    /* the column titles */
    r = rc;
    r.bottom = hdr_h;
    FillRect(dc, &r, face);
    text_at(dc, COL(X_NAME), 2, "Name", 0);
    text_at(dc, COL(X_CPU), 2, "CPU", 1);
    text_at(dc, COL(X_TIME), 2, "Time", 1);
    text_at(dc, COL(X_STAT), 2, "Status", 0);
    text_at(dc, COL(X_PRIO), 2, "Priority", 0);
    text_at(dc, COL(X_MEM), 2, "Memory", 1);
    if (showids) {
        text_at(dc, COL(X_ID), 2, "ID", 1);
        text_at(dc, COL(X_HAND), 2, "Handle", 0);
        text_at(dc, COL(X_MOD), 2, "Module", 0);
        text_at(dc, COL(X_WIN), 2, "Window", 0);
    }
    /* the graph and the status line */
    r = rc;
    r.top = rc.bottom - stat_h - graph_h;
    FillRect(dc, &r, face);
    old = SelectObject(dc, shadow);
    MoveTo(dc, 0, r.top);
    LineTo(dc, rc.right, r.top);
    if (graph_rect(&g)) {
        /* (only what the update region needs: see refresh) */
        int x0 = ps.rcPaint.left - 4, x1 = ps.rcPaint.right + 4, gy, gh2;
        RECT f;
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(0, 128, 0));
        HPEN line = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
        HPEN white = GetStockObject(WHITE_PEN);
        gy = g.top;
        gh2 = g.bottom - g.top;
        if (IntersectRect(&f, &g, &ps.rcPaint))
            FillRect(dc, &f, GetStockObject(BLACK_BRUSH));
        if (ps.rcPaint.left < g.left || ps.rcPaint.right > g.right
            || ps.rcPaint.top < g.top || ps.rcPaint.bottom > g.bottom) {
            SelectObject(dc, shadow);
            MoveTo(dc, g.left - 1, g.bottom);
            LineTo(dc, g.left - 1, g.top - 1);
            LineTo(dc, g.right, g.top - 1);
            SelectObject(dc, white);
            MoveTo(dc, g.right, g.top - 1);
            LineTo(dc, g.right, g.bottom);
            LineTo(dc, g.left - 1, g.bottom);
        }
        if (x0 < g.left) x0 = g.left;
        if (x1 > g.right) x1 = g.right;
        SelectObject(dc, grid);
        for (i = 1; i < 4 && x0 < x1; i++) {
            MoveTo(dc, x0, gy + gh2 * i / 4);
            LineTo(dc, x1, gy + gh2 * i / 4);
        }
        SelectObject(dc, line);
        for (i = 0, n = 0; i < nhist && i * 4 < g.right - g.left; i++) {
            int x = g.right - 1 - i * 4, y = gy + gh2 - 1 - (int)((DWORD)hist[i] * (DWORD)(gh2 - 1) / 100);
            if (x > x1 + 4) continue;
            if (x < x0 - 4) break;
            if (!n++) MoveTo(dc, x, y); else LineTo(dc, x, y);
        }
        SelectObject(dc, shadow);
        DeleteObject(grid);
        DeleteObject(line);
    }
    SelectObject(dc, old);
    text_at(dc, cw, rc.bottom - stat_h + 3, statline, 0);
    wsprintf(s, "Every %s s", (LPSTR)(interval == 500 ? "0.5" : interval == 1000 ? "1" : interval == 2000 ? "2" : "5"));
    text_at(dc, rc.right - cw, rc.bottom - stat_h + 3, s, 1);
    DeleteObject(face);
    DeleteObject(shadow);
    EndPaint(h, &ps);
}

static void layout(void)
{
    RECT rc;
    int i, bw = 0, x, y;
    GetClientRect(hwnd, &rc);
    graph_h = showgraph ? 3 * ch + 8 : 0;
    MoveWindow(hlist, 0, hdr_h, rc.right, rc.bottom - hdr_h - stat_h - graph_h - btn_h, TRUE);
    /* the buttons in a row under the list, as wide as the widest label */
    for (i = 0; i < 3; i++) {
        char t[24];
        HDC dc = GetDC(hbtn[i]);
        int w = GetWindowText(hbtn[i], t, sizeof(t));
        w = LOWORD(GetTextExtent(dc, t, w));
        ReleaseDC(hbtn[i], dc);
        if (w > bw) bw = w;
    }
    bw += 4 * cw;
    y = rc.bottom - stat_h - graph_h - btn_h + 4;
    for (i = 0, x = cw; i < 3; i++, x += bw + cw)
        MoveWindow(hbtn[i], x, y, bw, btn_h - 8, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void set_checks(void)
{
    HMENU m = GetMenu(hwnd);
    CheckMenuItem(m, IDM_INT500, interval == 500 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_INT1000, interval == 1000 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_INT2000, interval == 2000 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_INT5000, interval == 5000 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_ONTOP, ontop ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_TASKS, showtasks ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_GRAPH, showgraph ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_IDS, showids ? MF_CHECKED : MF_UNCHECKED);
}

static void put_int(LPCSTR key, int v)
{
    char s[12];
    wsprintf(s, "%d", v);
    WriteProfileString(APPNAME, key, s);
}

static void save_place(void)
{
    RECT rc;
    if (IsIconic(hwnd) || IsZoomed(hwnd)) return;
    GetWindowRect(hwnd, &rc);
    put_int("Left", rc.left);
    put_int("Top", rc.top);
    put_int("Width", rc.right - rc.left);
    put_int("Height", rc.bottom - rc.top);
}

static void set_interval(int ms)
{
    interval = ms;
    KillTimer(hwnd, 1);
    SetTimer(hwnd, 1, interval, NULL);
    put_int("Interval", interval);
    set_checks();
    InvalidateRect(hwnd, NULL, FALSE);
}

static void set_ontop(int on)
{
    ontop = on;
    SetWindowPos(hwnd, ontop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    put_int("OnTop", ontop);
    set_checks();
}

/* ---- Virtual Devices... and Drivers... ---- */

static void fill_vxds(HWND lb)
{
    static BYTE buf[2 + 96 * sizeof(struct vmt_dev)];
    WORD r[7];
    char t[96];
    int i, n;
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    if (!enhanced) {
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Standard mode: there are no virtual devices.");
        return;
    }
    if (!have_vxd) {
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"VMTASK.386 is not loaded: the devices cannot be listed.");
        return;
    }
    r[0] = 4;
    r[1] = 0;
    r[2] = sizeof(buf);
    r[3] = r[4] = 0;
    r[5] = OFFSETOF((void FAR *)buf);
    r[6] = SELECTOROF((void FAR *)buf);
    vmt_api(r);
    n = *(WORD *)buf;
    for (i = 0; i < n; i++) {
        struct vmt_dev *d = (struct vmt_dev *)(buf + 2) + i;
        char name[9];
        int k;
        for (k = 0; k < 8 && d->name[k] && d->name[k] != ' '; k++) name[k] = d->name[k];
        name[k] = 0;
        wsprintf(t, "%s\t%04X\t%u.%02u\t%08lX\t%lu\t%s%s", (LPSTR)name, d->id, d->major, d->minor,
                 d->init_order, d->services, (LPSTR)(d->flags & 1 ? "V86 " : ""), (LPSTR)(d->flags & 2 ? "PM" : ""));
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
    }
}

static void fill_drivers(HWND lb)
{
    HDRVR d = 0;
    DRIVERINFOSTRUCT di;
    MODULEENTRY me;
    BOOL any;
    char t[200], mod[16], boot[128];
    int k;
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Installable drivers:");
    k = 0;
    while ((d = GetNextDriver(d, GND_FORWARD)) != 0 && k++ < 64) {
        di.length = sizeof(di);
        if (!GetDriverInfo(d, &di)) continue;
        mod[0] = 0;
        me.dwSize = sizeof(me);
        if (ModuleFindHandle(&me, (HMODULE)di.hModule)) lstrcpyn(mod, me.szModule, sizeof(mod));
        wsprintf(t, "  %s\t%s\t%04X\t%04X", (LPSTR)di.szAliasName, (LPSTR)mod, (WORD)d, (WORD)di.hModule);
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
    }
    if (!k) SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"  (none)");
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Drivers loaded as modules (.DRV, [boot] drivers=):");
    GetPrivateProfileString("boot", "drivers", "", boot, sizeof(boot), "SYSTEM.INI");
    AnsiUpper(boot);
    me.dwSize = sizeof(me);
    for (any = ModuleFirst(&me); any; any = ModuleNext(&me)) {
        int n = lstrlen(me.szExePath), drv;
        char m2[12];
        drv = n > 4 && !lstrcmpi(me.szExePath + n - 4, ".DRV");
        lstrcpyn(m2, me.szModule, sizeof(m2));
        AnsiUpper(m2);
        if (!drv && boot[0]) {
            /* a module whose name [boot] drivers= names */
            char *p = boot;
            int ml = lstrlen(m2);
            while (*p) {
                char *q = p;
                int j;
                while (*q && *q != ' ' && *q != ',' && *q != '.') q++;
                for (j = 0; j < ml && p + j < q && p[j] == m2[j]; j++)
                    ;
                if (j == ml && p + j == q) { drv = 1; break; }
                while (*q && *q != ' ' && *q != ',') q++;
                while (*q == ' ' || *q == ',') q++;
                p = q;
            }
        }
        if (!drv) continue;
        wsprintf(t, "  %s\t%s\t%u\t%04X", (LPSTR)me.szModule, (LPSTR)me.szExePath, me.wcUsage, (WORD)me.hModule);
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
    }
}

BOOL CALLBACK __export ListDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    static int vxds;
    switch (msg) {
    case WM_INITDIALOG: {
        /* tab stops in dialog units */
        static int t_vxd[] = { 44, 70, 96, 140, 168 }, t_drv[] = { 64, 174, 204 };
        HWND lb = GetDlgItem(d, IDC_ITEMS);
        vxds = (int)lp;
        if (vxds) SendMessage(lb, LB_SETTABSTOPS, 5, (LPARAM)(int FAR *)t_vxd);
        else SendMessage(lb, LB_SETTABSTOPS, 3, (LPARAM)(int FAR *)t_drv);
        if (vxds) fill_vxds(lb); else fill_drivers(lb);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDC_REFRESH) {
            HWND lb = GetDlgItem(d, IDC_ITEMS);
            if (vxds) fill_vxds(lb); else fill_drivers(lb);
        } else if (wp == IDOK || wp == IDCANCEL)
            EndDialog(d, 0);
        return TRUE;
    }
    return FALSE;
}

static void list_dialog(int vxds)
{
    FARPROC fp = MakeProcInstance((FARPROC)ListDlgProc, hinst);
    DialogBoxParam(hinst, MAKEINTRESOURCE(vxds ? IDD_VXDS : IDD_DRIVERS), hwnd, (DLGPROC)fp, (LPARAM)vxds);
    FreeProcInstance(fp);
}

/* ---- the actions on a row ---- */

static struct row *sel_row(void)
{
    int i = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
    if (i < 0 || i >= nlisted) return NULL;
    return shown[i].kind == R_MSG ? NULL : &shown[i];
}

/* the selected row, copied: the list changes under a message box (its
   modal loop goes on refreshing), so an action works on a copy */
static struct row act;
static struct row *act_row(void)
{
    struct row *r = sel_row();
    if (!r) return NULL;
    act = *r;
    return &act;
}

#define CAN_SWITCH 1
#define CAN_ENDTASK 2
#define CAN_ENDVM 4
static int can(struct row *r)
{
    if (!r) return 0;
    if (r->kind == R_TASK) return CAN_SWITCH | CAN_ENDTASK;
    if (r->sys) return CAN_SWITCH;
    return CAN_SWITCH | (have_vxd ? CAN_ENDVM : 0);
}

static void set_buttons(void)
{
    int c = can(sel_row());
    HMENU m = GetMenu(hwnd);
    EnableWindow(hbtn[0], (c & CAN_SWITCH) != 0);
    EnableWindow(hbtn[1], (c & CAN_ENDTASK) != 0);
    EnableWindow(hbtn[2], (c & CAN_ENDVM) != 0);
    EnableMenuItem(m, IDM_SWITCH, c & CAN_SWITCH ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_ENDTASK, c & CAN_ENDTASK ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_ENDVM, c & CAN_ENDVM ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_DETAILS, c ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_COPY, c ? MF_ENABLED : MF_GRAYED);
}

/* the task's main window: its first visible, unowned top-level window */
static HWND task_window(WORD task)
{
    HWND w;
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        if ((WORD)GetWindowTask(w) == task && !GetWindow(w, GW_OWNER) && IsWindowVisible(w))
            return w;
    return 0;
}

/* Switch To: as the Task List's (its last active popup, restored) */
static void do_switch(struct row *r)
{
    HWND w = 0, p;
    if (!r) return;
    if (r->kind == R_TASK) w = task_window(r->task);
    else if (!r->sys) w = (HWND)r->hwnd;
    else {
        /* the Windows row: the shell's window (Program Manager) */
        w = FindWindow("Progman", NULL);
    }
    if (!w || !IsWindow(w)) { MessageBeep(0); return; }
    p = GetLastActivePopup(w);
    if (!IsWindow(p)) p = w;
    if (GetWindowLong(p, GWL_STYLE) & WS_DISABLED) MessageBeep(0);
    else SwitchToThisWindow(p, TRUE);
}

/* a task w16 needs: the shell, this program, a DOS box's WINOLDAP, the
   multimedia system's task; 0: an ordinary one */
static LPCSTR system_task(struct row *r)
{
    char mod[16];
    HWND pm = FindWindow("Progman", NULL);
    if (r->task == (WORD)GetCurrentTask()) return "the VM Task Manager itself";
    if (pm && (WORD)GetWindowTask(pm) == r->task) return "the shell (Program Manager)";
    if (IsWinOldApTask((HTASK)r->task)) return "a DOS program's window (End VM ends the DOS program)";
    lstrcpyn(mod, r->name, sizeof(mod));
    if (!lstrcmpi(mod, "MMTASK")) return "the multimedia system's task";
    if (!task_window(r->task)) return "a task with no window: part of the system or a driver";
    return 0;
}

static void force_end(WORD task, LPCSTR name)
{
    char t[200];
    if (!IsTask((HTASK)task)) return;
    wsprintf(t, "%s is not responding.\n\nEnd it anyway? Any unsaved information in it "
             "will be lost.", (LPSTR)name);
    if (MessageBox(hwnd, t, "End Task", MB_ICONSTOP | MB_OKCANCEL | MB_DEFBUTTON2) != IDOK) return;
    if (IsTask((HTASK)task)) TerminateApp((HTASK)task, NO_UAE_BOX);
    refresh();
}

/* End Task: confirmed (twice for a task w16 needs), then WM_CLOSE to its
   windows; if it has not ended END_WAIT ms later, offered TerminateApp */
static void end_task(struct row *r)
{
    char t[300];
    LPCSTR why;
    HWND w, next;
    int n = 0;
    WORD task;
    if (!r || r->kind != R_TASK) return;
    task = r->task;
    wsprintf(t, "End %s?\n\nIt is asked to close; any unsaved information in it may be lost.",
             (LPSTR)r->name);
    if (MessageBox(hwnd, t, "End Task", MB_ICONQUESTION | MB_OKCANCEL) != IDOK) return;
    why = system_task(r);
    if (why) {
        wsprintf(t, "%s is %s.\n\nEnding it may stop w16 or other programs from working. "
                 "Are you sure you want to end it?", (LPSTR)r->name, why);
        if (MessageBox(hwnd, t, "End Task", MB_ICONEXCLAMATION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return;
    }
    if (task == (WORD)GetCurrentTask()) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return;
    }
    lstrcpyn(end_name, r->name, sizeof(end_name));
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = next) {
        next = GetWindow(w, GW_HWNDNEXT);
        if ((WORD)GetWindowTask(w) == task && !GetWindow(w, GW_OWNER)) {
            PostMessage(w, WM_CLOSE, 0, 0);
            n++;
        }
    }
    if (!n) {
        force_end(task, end_name);
        return;
    }
    end_task_h = task;
    SetTimer(hwnd, 2, END_WAIT, NULL);
}

/* the warning 3.1's DOS box Settings dialog shows for Terminate */
BOOL CALLBACK __export TermDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowText(d, (LPCSTR)lp);
        SendDlgItemMessage(d, IDC_STOPICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_HAND), 0);
        /* Cancel first, as 3.1's (Return cancels; Tab goes to OK); also
           once the dialog is up (w16's USER put it back on OK) */
        SetFocus(GetDlgItem(d, IDCANCEL));
        PostMessage(d, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(d, IDCANCEL), 1L);
        return FALSE;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) EndDialog(d, wp);
        return TRUE;
    }
    return FALSE;
}

/* End VM: 3.1's Terminate warning, then VMTASK.386's function 3 with
   Nuke_VM, as SHELL ends a VM for Terminate; never the System VM */
static void end_vm(struct row *r)
{
    FARPROC fp;
    int ok;
    WORD reg[7];
    DWORD vm;
    char t[64];
    if (!r || r->kind != R_VM || r->sys || !have_vxd || !r->vmh) return;
    vm = r->vmh;
    lstrcpyn(t, r->name, sizeof(t));
    fp = MakeProcInstance((FARPROC)TermDlgProc, hinst);
    ok = DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_TERMINATE), hwnd, (DLGPROC)fp, (LPARAM)(LPSTR)t);
    FreeProcInstance(fp);
    if (ok != IDOK) return;
    reg[0] = 3;
    reg[1] = LOWORD(vm);
    reg[2] = 1;                         /* Nuke_VM */
    reg[3] = HIWORD(vm);
    reg[4] = reg[5] = reg[6] = 0;
    if (!vmt_api(reg))
        MessageBox(hwnd, reg[0] == 1 ? "The System VM cannot be ended." : "That virtual machine has already ended.",
                   "End VM", MB_ICONEXCLAMATION | MB_OK);
    refresh();
}

/* Details: the row's IDs and what the API tells of it */
static void details(struct row *r, char *t)
{
    char *p = t;
    *p = 0;
    if (!r) return;
    if (r->kind == R_TASK) {
        p += wsprintf(p, "Task: %s\r\nTask handle (hTask): %04X\r\nModule handle: %04X\r\n"
                      "Instance: %04X\r\n", (LPSTR)r->name, r->task, r->hmod, r->hinst);
        if (task_window(r->task))
            p += wsprintf(p, "Main window: %04X\r\n", (WORD)task_window(r->task));
        p += wsprintf(p, "Status: %s\r\n", (LPSTR)r->status);
        if (r->mem_kb != 0xFFFFFFFFul)
            p += wsprintf(p, "Memory: %lu K (global blocks its PDB or module owns; approximate)\r\n", r->mem_kb);
        return;
    }
    p += wsprintf(p, "%s\r\n", (LPSTR)(r->sys ? "Windows (the System VM)" : r->name));
    if (r->vmh) {
        p += wsprintf(p, "VM ID: %lu\r\nVM handle: %08lX\r\n", r->vmid, r->vmh);
        p += wsprintf(p, "Status: %s (CB_VM_Status %04lX)\r\n", (LPSTR)r->status, r->vmstat);
        p += wsprintf(p, "Priority: foreground %lu, background %lu\r\nShare of the processor at most: %lu%%\r\n",
                      r->fg, r->bg, r->share);
    }
    if (!r->sys && r->shflags != 0xFFFFFFFFul)
        p += wsprintf(p, "Display: %s (SHELL_GetVMInfo %08lX)\r\n",
                      (LPSTR)(r->shflags & SGVMI_WINDOWED ? "window" : "full screen"), r->shflags);
    if (r->hwnd) p += wsprintf(p, "Window: %04X\r\n", r->hwnd);
    if (r->sys && r->mem_kb != 0xFFFFFFFFul)
        p += wsprintf(p, "Memory: %lu K in Windows' global heap\r\n", r->mem_kb);
    else if (r->mem_kb != 0xFFFFFFFFul)
        p += wsprintf(p, "Memory: %lu K (_GetVMPgCount: %lu pages; not mapped in its first MB: %lu)\r\n"
                      "EMS at most %lu K, XMS at most %lu K\r\n", r->mem_kb, r->mem_kb / 4, r->out_kb / 4,
                      r->ems_max, r->xms_max);
}

static void show_details(struct row *r)
{
    char t[512];
    details(r, t);
    if (t[0]) MessageBox(hwnd, t, "Details", MB_ICONINFORMATION | MB_OK);
}

static void copy_details(struct row *r)
{
    char t[512];
    HGLOBAL g;
    LPSTR q;
    details(r, t);
    if (!t[0] || !OpenClipboard(hwnd)) return;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(t) + 1);
    if (g && (q = GlobalLock(g)) != NULL) {
        lstrcpy(q, t);
        GlobalUnlock(g);
        EmptyClipboard();
        SetClipboardData(CF_TEXT, g);
    }
    CloseClipboard();
}

/* the shortcut menu for the selected row, at screen point x, y */
static void context_menu(int x, int y)
{
    struct row *r = sel_row();
    int c = can(r);
    HMENU m;
    if (!c) return;
    m = CreatePopupMenu();
    AppendMenu(m, MF_STRING, IDM_SWITCH, "&Switch To");
    if (c & CAN_ENDTASK) AppendMenu(m, MF_STRING, IDM_ENDTASK, "End &Task");
    if (r->kind == R_VM && !r->sys)
        AppendMenu(m, MF_STRING | (c & CAN_ENDVM ? 0 : MF_GRAYED), IDM_ENDVM, "End &VM");
    AppendMenu(m, MF_SEPARATOR, 0, NULL);
    AppendMenu(m, MF_STRING, IDM_DETAILS, r->kind == R_VM && !r->sys ? "&Settings and IDs..." : "&Details...");
    AppendMenu(m, MF_STRING, IDM_COPY, "&Copy Details");
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
    DestroyMenu(m);
}

/* the list: a right click selects the row under it and opens the menu */
LRESULT CALLBACK __export ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_RBUTTONDOWN) {
        int i = (int)SendMessage(h, LB_GETTOPINDEX, 0, 0) + (int)HIWORD(lp) / ch;
        if (i < nlisted) {
            SendMessage(h, LB_SETCURSEL, i, 0);
            set_buttons();
        }
        return 0;
    }
    /* Del ends the selected task or VM, Shift+F10 opens the menu (here,
       not in the accelerator table: Shift+F10 comes as WM_SYSKEYDOWN, which
       TranslateAccelerator passed by) */
    if (msg == WM_KEYDOWN && wp == VK_DELETE) {
        PostMessage(hwnd, WM_COMMAND, IDM_END, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessage(hwnd, WM_COMMAND, IDM_SWITCH, 0);
        return 0;
    }
    if ((msg == WM_SYSKEYDOWN || msg == WM_KEYDOWN) && wp == VK_F10 && GetKeyState(VK_SHIFT) < 0) {
        PostMessage(hwnd, WM_COMMAND, IDM_CONTEXT, 0);
        return 0;
    }
    /* typing a name selects the next row that starts with what was typed
       (a pause of a second starts afresh) */
    if (msg == WM_CHAR && wp > ' ') {
        static char typed[16];
        static int ntyped;
        static DWORD last;
        int i, k, n = nlisted, cur = (int)SendMessage(h, LB_GETCURSEL, 0, 0);
        if (GetTickCount() - last > 1000) ntyped = 0;
        last = GetTickCount();
        if (ntyped < sizeof(typed) - 1) typed[ntyped++] = (char)wp;
        typed[ntyped] = 0;
        for (k = ntyped == 1 ? 1 : 0; k <= n; k++) {
            int j;
            i = ((cur < 0 ? 0 : cur) + k) % (n ? n : 1);
            if (shown[i].kind == R_MSG) continue;
            for (j = 0; j < ntyped; j++) {
                char a = shown[i].name[j], b = typed[j];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) break;
            }
            if (j == ntyped) {
                SendMessage(h, LB_SETCURSEL, i, 0);
                set_buttons();
                break;
            }
        }
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        POINT pt;
        pt.x = LOWORD(lp);
        pt.y = HIWORD(lp);
        ClientToScreen(h, &pt);
        context_menu(pt.x, pt.y);
        return 0;
    }
    return CallWindowProc(old_list, h, msg, wp, lp);
}

static void menu_at_selection(void)
{
    RECT r;
    POINT pt;
    int i = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
    if (i < 0 || SendMessage(hlist, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&r) == LB_ERR) return;
    pt.x = r.left + COL(X_CPU) / 2;
    pt.y = r.bottom;
    ClientToScreen(hlist, &pt);
    context_menu(pt.x, pt.y);
}

static void set_ids(int on)
{
    RECT rc;
    showids = on;
    put_int("IDs", showids);
    set_checks();
    /* wide enough for the columns (within the screen) */
    GetWindowRect(hwnd, &rc);
    if (!IsZoomed(hwnd) && !IsIconic(hwnd)) {
        int want = colx[on ? X_END2 : X_END1] + GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXFRAME) + 2;
        if (want > GetSystemMetrics(SM_CXSCREEN)) want = GetSystemMetrics(SM_CXSCREEN);
        if (on ? rc.right - rc.left < want : rc.right - rc.left > want)
            SetWindowPos(hwnd, 0, 0, 0, want, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    InvalidateRect(hwnd, NULL, TRUE);
    InvalidateRect(hlist, NULL, TRUE);
}

LRESULT CALLBACK __export WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hwnd = h;
        hlist = CreateWindow("LISTBOX", NULL,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | WS_TABSTOP | LBS_OWNERDRAWFIXED
                             | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                             0, 0, 0, 0, h, (HMENU)IDC_LIST, hinst, NULL);
        old_list = (FARPROC)GetWindowLong(hlist, GWL_WNDPROC);
        list_thunk = MakeProcInstance((FARPROC)ListProc, hinst);
        SetWindowLong(hlist, GWL_WNDPROC, (LONG)list_thunk);
        hbtn[0] = CreateWindow("BUTTON", "&Switch To", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 0, 0, h, (HMENU)IDM_SWITCH, hinst, NULL);
        hbtn[1] = CreateWindow("BUTTON", "End &Task", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 0, 0, h, (HMENU)IDM_ENDTASK, hinst, NULL);
        hbtn[2] = CreateWindow("BUTTON", "End &VM", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 0, 0, h, (HMENU)IDM_ENDVM, hinst, NULL);
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_SETFOCUS:
        SetFocus(hlist);
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = ch;
        return TRUE;
    case WM_DRAWITEM:
        draw_item((DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_TIMER:
        if (wp == 2) {
            /* End Task's wait is over: still there, it is offered an end
               by force */
            WORD t = end_task_h;
            KillTimer(h, 2);
            end_task_h = 0;
            refresh();
            if (t && IsTask((HTASK)t)) force_end(t, end_name);
            return 0;
        }
        refresh();
        return 0;
    case WM_COMMAND:
        switch (wp) {
        case IDM_INT500: set_interval(500); break;
        case IDM_INT1000: set_interval(1000); break;
        case IDM_INT2000: set_interval(2000); break;
        case IDM_INT5000: set_interval(5000); break;
        case IDM_ONTOP: set_ontop(!ontop); break;
        case IDM_TASKS:
            showtasks = !showtasks;
            put_int("Tasks", showtasks);
            set_checks();
            refresh();
            break;
        case IDM_GRAPH:
            showgraph = !showgraph;
            put_int("Graph", showgraph);
            set_checks();
            layout();
            break;
        case IDM_REFRESH: refresh(); break;
        case IDM_IDS: set_ids(!showids); break;
        case IDM_SWITCH: do_switch(act_row()); break;
        case IDM_ENDTASK: end_task(act_row()); break;
        case IDM_ENDVM: end_vm(act_row()); break;
        case IDM_END: {
            /* Del: the selected task or VM */
            struct row *r = act_row();
            int c = can(r);
            if (c & CAN_ENDTASK) end_task(r);
            else if (c & CAN_ENDVM) end_vm(r);
            else MessageBeep(0);
            break;
        }
        case IDM_DETAILS: show_details(act_row()); break;
        case IDM_COPY: copy_details(act_row()); break;
        case IDM_CONTEXT: menu_at_selection(); break;
        case IDM_VXDS: list_dialog(1); break;
        case IDM_DRIVERS: list_dialog(0); break;
        case IDC_LIST:
            if (HIWORD(lp) == LBN_SELCHANGE) set_buttons();
            else if (HIWORD(lp) == LBN_DBLCLK) do_switch(act_row());
            break;
        case IDM_EXIT: SendMessage(h, WM_CLOSE, 0, 0); break;
        case IDM_ABOUT:
            ShellAbout(h, "VM Task Manager",
                       "How the processor's time is shared between virtual machines and tasks",
                       LoadIcon(hinst, MAKEINTRESOURCE(IDI_VMTASK)));
            break;
        }
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO FAR *)lp)->ptMinTrackSize.x = COL(X_STAT);
        ((MINMAXINFO FAR *)lp)->ptMinTrackSize.y = 10 * ch;
        return 0;
    case WM_SYSCOLORCHANGE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_CLOSE:
        save_place();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        KillTimer(h, 2);
        if (old_list) SetWindowLong(hlist, GWL_WNDPROC, (LONG)old_list);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void find_device(void)
{
    WORD r[7];
    enhanced = (GetWinFlags() & WF_ENHANCED) != 0;
    if (!enhanced) return;
    vmt_entry = vmt_find(VMT_DEVICE);
    if (!vmt_entry) return;
    r[0] = 0;
    r[1] = r[2] = r[3] = r[4] = r[5] = r[6] = 0;
    if (vmt_api(r) && r[1] == 0x564D && r[2] == 0x544B && (r[3] & 1)) have_vxd = 1;
    else vmt_entry = 0;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HDC dc;
    TEXTMETRIC tm;
    int x, y, w, hgt;
    HACCEL acc;
    (void)cmd;
    hinst = hi;
    if (!hprev) {
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_VMTASK));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = MAKEINTRESOURCE(IDM_MENU);
        wc.lpszClassName = APPNAME;
        if (!RegisterClass(&wc)) return 0;
    }
    dc = GetDC(NULL);
    GetTextMetrics(dc, &tm);
    ReleaseDC(NULL, dc);
    cw = tm.tmAveCharWidth;
    ch = tm.tmHeight;
    hdr_h = ch + 4;
    stat_h = ch + 6;
    btn_h = ch + 16;
    {
        /* the columns, from the system font's widths of what they hold */
#define EXT(t) ((int)LOWORD(GetTextExtent(dc, t, sizeof(t) - 1)))
        dc = GetDC(NULL);
        colx[X_NAME] = cw;
        colx[X_CPU] = colx[X_NAME] + 17 * cw + EXT("100%");
        colx[X_TIME] = colx[X_CPU] + cw + EXT("0:00:00");
        colx[X_STAT] = colx[X_TIME] + 2 * cw;
        colx[X_PRIO] = colx[X_STAT] + EXT("Background") + cw;
        colx[X_MEM] = colx[X_PRIO] + EXT("Priority") + cw + EXT("Memory");
        colx[X_END1] = colx[X_MEM] + cw;
        colx[X_ID] = colx[X_END1] + EXT("99");
        colx[X_HAND] = colx[X_ID] + cw;
        colx[X_MOD] = colx[X_HAND] + EXT("80000000") + cw;
        colx[X_WIN] = colx[X_MOD] + EXT("Module") + cw;
        colx[X_END2] = colx[X_WIN] + EXT("Window") + cw;
        ReleaseDC(NULL, dc);
#undef EXT
    }
    interval = GetProfileInt(APPNAME, "Interval", 1000);
    if (interval != 500 && interval != 2000 && interval != 5000) interval = 1000;
    ontop = GetProfileInt(APPNAME, "OnTop", 0) != 0;
    showtasks = GetProfileInt(APPNAME, "Tasks", 1) != 0;
    showgraph = GetProfileInt(APPNAME, "Graph", 1) != 0;
    showids = GetProfileInt(APPNAME, "IDs", 1) != 0;
    w = colx[showids ? X_END2 : X_END1] + GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXFRAME) + 2;
    if (w > GetSystemMetrics(SM_CXSCREEN)) w = GetSystemMetrics(SM_CXSCREEN);
    w = GetProfileInt(APPNAME, "Width", w);
    hgt = GetProfileInt(APPNAME, "Height", 25 * ch);
    x = GetProfileInt(APPNAME, "Left", CW_USEDEFAULT);
    y = GetProfileInt(APPNAME, "Top", 0);
    vmt_init();
    find_device();
    if (!have_vxd) vmt_hook(1);
    NotifyRegister(NULL, (LPFNNOTIFYCALLBACK)NotifyProc, NF_TASKSWITCH);
    vmt_tin = busy_ms();
    vmt_cur = (WORD)GetCurrentTask();
    hwnd = CreateWindow(APPNAME, "VM Task Manager", WS_OVERLAPPEDWINDOW, x, y, w, hgt,
                        NULL, NULL, hi, NULL);
    if (!hwnd) {
        NotifyUnRegister(NULL);
        if (!have_vxd) vmt_hook(0);
        return 0;
    }
    set_checks();
    refresh();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    if (ontop) set_ontop(1);
    SetTimer(hwnd, 1, interval, NULL);
    acc = LoadAccelerators(hi, MAKEINTRESOURCE(IDM_MENU));
    while (GetMessage(&m, NULL, 0, 0)) {
        if (acc && TranslateAccelerator(hwnd, acc, &m)) continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    NotifyUnRegister(NULL);
    if (!have_vxd) vmt_hook(0);
    return m.wParam;
}
