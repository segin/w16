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
 * Settings (WIN.INI [VMTask]): Interval (ms: 500, 1000, 2000, 5000),
 * OnTop, Tasks, Graph, and the window's place (Left, Top, Width, Height).
 * Black box (ARCHITECTURE.md 3): the SDK's USER, KERNEL and TOOLHELP,
 * VMTASK's own interface.
 */
#include <windows.h>
#include <toolhelp.h>
#include <shellapi.h>
#include "vmtask.h"

extern BOOL FAR PASCAL IsWinOldApTask(HTASK);           /* KERNEL.158 */
/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

#define APPNAME "VMTask"
#define MAXV    32
#define MAXROWS 112
#define NHIST   160

enum { R_MSG, R_VM, R_TASK };
struct row {
    BYTE kind, indent;
    char name[48];
    int pct;                            /* tenths of a per cent; -1: none */
    DWORD ms;                           /* the time in all; 0xFFFFFFFF: none */
    char status[16];
    char prio[12];
    DWORD key;
};

static HINSTANCE hinst;
static HWND hwnd, hlist;
static int cw, ch, hdr_h, stat_h, graph_h;
static int interval = 1000, ontop, showtasks = 1, showgraph = 1;
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
        if (scale_den && scale_num < scale_den) d = d * scale_num / scale_den;
        r->pct = have_prev ? pct10(d, dwall) : -1;
        r->ms = total;
        lstrcpy(r->status, (WORD)te.hTask == (WORD)GetCurrentTask() ? "Running" : "Ready");
        if (!te.wcEvents && (WORD)te.hTask != (WORD)GetCurrentTask()) lstrcpy(r->status, "Waiting");
    }
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
            }
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
        w = add_row(R_VM, 0, "Windows", 0);
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
    for (i = 0; i < nrows; i++) shown[i] = rows[i];
    top = (int)SendMessage(hlist, LB_GETTOPINDEX, 0, 0);
    sel = (int)SendMessage(hlist, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < nlisted) key = rows[sel].key;
    SendMessage(hlist, WM_SETREDRAW, FALSE, 0);
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < nrows; i++)
        SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(long)i);
    nlisted = nrows;
    SendMessage(hlist, LB_SETTOPINDEX, top < nrows ? top : 0, 0);
    if (key)
        for (i = 0; i < nrows; i++)
            if (rows[i].key == key) { SendMessage(hlist, LB_SETCURSEL, i, 0); break; }
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

static void refresh(void)
{
    RECT rc, g;
    int added = nhist, i;
    sample();
    fill_list();
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

/* column edges, in average character widths */
static const int colx[] = { 1, 24, 31, 41, 55, 64 };
#define COL(i) (colx[i] * cw)

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
        int x = rc.left + COL(0) + r->indent * 2 * cw;
        HRGN clip = CreateRectRgn(rc.left, rc.top, rc.left + COL(1) - 6 * cw, rc.bottom);
        SelectClipRgn(dc, clip);
        text_at(dc, x, y, r->name, 0);
        SelectClipRgn(dc, NULL);
        DeleteObject(clip);
        if (r->pct >= 0) {
            wsprintf(s, "%d%%", (r->pct + 5) / 10);
            text_at(dc, rc.left + COL(1), y, s, 1);
        }
        if (r->ms != 0xFFFFFFFFul) {
            fmt_time(s, r->ms);
            text_at(dc, rc.left + COL(2) + 7 * cw, y, s, 1);
        }
        text_at(dc, rc.left + COL(3), y, r->status, 0);
        text_at(dc, rc.left + COL(4), y, r->prio, 0);
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
    text_at(dc, COL(0), 2, "Name", 0);
    text_at(dc, COL(1), 2, "CPU", 1);
    text_at(dc, COL(2) + 7 * cw, 2, "Time", 1);
    text_at(dc, COL(3), 2, "Status", 0);
    text_at(dc, COL(4), 2, "Priority", 0);
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
    GetClientRect(hwnd, &rc);
    graph_h = showgraph ? 3 * ch + 8 : 0;
    MoveWindow(hlist, 0, hdr_h, rc.right, rc.bottom - hdr_h - stat_h - graph_h, TRUE);
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

LRESULT CALLBACK __export WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hwnd = h;
        hlist = CreateWindow("LISTBOX", NULL,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_OWNERDRAWFIXED
                             | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                             0, 0, 0, 0, h, (HMENU)IDC_LIST, hinst, NULL);
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
        case IDM_EXIT: SendMessage(h, WM_CLOSE, 0, 0); break;
        case IDM_ABOUT:
            ShellAbout(h, "VM Task Manager",
                       "How the processor's time is shared between virtual machines and tasks",
                       LoadIcon(hinst, MAKEINTRESOURCE(IDI_VMTASK)));
            break;
        }
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO FAR *)lp)->ptMinTrackSize.x = COL(3);
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
    interval = GetProfileInt(APPNAME, "Interval", 1000);
    if (interval != 500 && interval != 2000 && interval != 5000) interval = 1000;
    ontop = GetProfileInt(APPNAME, "OnTop", 0) != 0;
    showtasks = GetProfileInt(APPNAME, "Tasks", 1) != 0;
    showgraph = GetProfileInt(APPNAME, "Graph", 1) != 0;
    w = GetProfileInt(APPNAME, "Width", COL(5) + GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXFRAME) + 2);
    hgt = GetProfileInt(APPNAME, "Height", 22 * ch);
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
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    NotifyUnRegister(NULL);
    if (!have_vxd) vmt_hook(0);
    return m.wParam;
}
