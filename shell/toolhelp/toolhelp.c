/*
 * toolhelp.c - w16's TOOLHELP.DLL (ARCHITECTURE.md 22): Windows 3.1's Tool
 * Helper library. It reports what Windows 3.1's TOOLHELP reports, from
 * the structures w16's KERNEL and USER keep in Windows 3.1's layouts
 * (th.h); notifications come from KERNEL's ToolHelpHook (KERNEL.341), the
 * processor's faults from DPMI (entry.asm).
 *
 * Every behaviour here was observed on Windows 3.1 with the probe
 * tests/toolhelp/thp.c (Microsoft's TOOLHELP under KRNL286 and KRNL386,
 * Standard and 386 Enhanced mode; tests/toolhelp/thscen.sh compares the
 * logs) unless marked UNVERIFIED. Facts from Schulman et al.,
 * Undocumented Windows (1992), ch. 10, are named where they were used,
 * each confirmed by the probe.
 */
#include "th.h"

/* ---- KERNEL and USER entries taken by ordinal ---- */
HMODULE hk, hu, hg;
WORD FAR *thhook;                       /* KERNEL.332 */
static DWORD (FAR PASCAL *kToolHelpHook)(FARPROC);      /* KERNEL.341 */
static WORD (FAR PASCAL *kHasGPHandler)(DWORD);         /* KERNEL.338 */
DWORD (FAR PASCAL *kGlobalMasterHandle)(void);  /* KERNEL.28 */
WORD (FAR PASCAL *kGetExePtr)(WORD);            /* KERNEL.133 */
static DWORD (FAR PASCAL *uSeeUserDo)(WORD, WORD, WORD, WORD);  /* USER.216 */
static WORD (FAR PASCAL *uFreeRes)(WORD);               /* USER.284 */

extern WORD __cdecl __far pit_count(void);
extern DWORD __cdecl __far vtd_time(DWORD entry, WORD fn);
extern void __cdecl __far th_hook(void);
extern void __cdecl __far exc_0(void);
extern void __cdecl __far exc_6(void);
extern void __cdecl __far exc_12(void);
extern void __cdecl __far exc_13(void);
extern void __cdecl __far exc_14(void);
extern void __cdecl __far vec_1(void);
extern void __cdecl __far vec_3(void);
extern void __cdecl __far sw_entry(void);
extern DWORD __cdecl old_hook, old_exc[16], old_vec1, old_vec3, f_cb, sw_csip;
extern WORD __cdecl f_num, f_ip, f_cs, f_fl, f_handle, f_cbax;

WORD dsel;                              /* our DGROUP */
WORD scratch;                           /* a selector for linear reads */

/* ---- small helpers ---- */
WORD lsl(WORD sel)
{
    WORD r = 0;
    _asm {
        xor ax, ax
        lsl ax, sel
        jz ok
        xor ax, ax
    ok: mov r, ax
    }
    return r;
}

BOOL readable(WORD sel, WORD off, WORD n)
{
    WORD r = 0;
    _asm {
        xor ax, ax
        verr sel
        jnz no
        inc ax
    no: mov r, ax
    }
    if (!r || !n)
        return r != 0;
    return lsl(sel) >= off && lsl(sel) - off >= n - 1;
}


void copy(void FAR *d, const void FAR *s, WORD n)
{
    BYTE FAR *a = d;
    const BYTE FAR *b = s;
    while (n--)
        *a++ = *b++;
}

/* the structure checks every entry makes (Schulman ch. 10; the probe:
   dwSize must be the structure's size exactly) */
BOOL good(void FAR *p, WORD size)
{
    if (!p || !readable(SELECTOROF(p), OFFSETOF(p), size))
        return FALSE;
    return *(DWORD FAR *)p == size;
}

/* bytes at a linear address, through the scratch selector */
BOOL lread(DWORD lin, void FAR *d, WORD n)
{
    if (!scratch)
        return FALSE;
    SetSelectorBase(scratch, lin);
    SetSelectorLimit(scratch, n ? n - 1 : 0);
    copy(d, MAKELP(scratch, 0), n);
    return TRUE;
}

/* ===== GlobalHandleToSel (50) ===== */
/* a handle's selector: the handle with bit 0 set, whatever it is
   (observed: 0 gives 1, 8 gives 9; Schulman: no check is made) */
WORD THAPI GlobalHandleToSel(WORD h)
{
    return h | 1;
}

/* ===== modules (59-62) ===== */
BOOL is_module(WORD h)
{
    return h && readable(h | 1, 0, 0x40) && PW(h | 1, 0) == 0x454E;  /* "NE" */
}

static BOOL fill_module(MODULEENTRY FAR *m, WORD h)
{
    WORD s = h | 1, rn, n, i, f;
    if (!is_module(h))
        return FALSE;
    rn = PW(s, 0x26);                   /* resident names: the module's first */
    n = PB(s, rn);
    if (n > MAX_MODULE_NAME)
        n = MAX_MODULE_NAME;
    for (i = 0; i < n; i++)
        m->szModule[i] = PB(s, rn + 1 + i);
    m->szModule[i] = 0;
    m->hModule = s;
    m->wcUsage = PW(s, 0x02);
    f = PW(s, 0x0A);                    /* the file's OFSTRUCT-like record */
    for (i = 0; i < MAX_PATH && f && PB(s, f + 8 + i); i++)
        m->szExePath[i] = PB(s, f + 8 + i);
    m->szExePath[i] = 0;
    m->wNext = PW(s, 0x06);
    return TRUE;
}

BOOL THAPI ModuleFirst(MODULEENTRY FAR *m)
{
    if (!good(m, sizeof *m))
        return FALSE;
    return fill_module(m, thhook[2]);   /* THHOOK 04h: hExeHead */
}

BOOL THAPI ModuleNext(MODULEENTRY FAR *m)
{
    if (!good(m, sizeof *m) || !m->wNext)
        return FALSE;
    return fill_module(m, m->wNext);
}

/* the name exactly, case and all (observed: "user" finds nothing) */
WORD THAPI ModuleFindName(MODULEENTRY FAR *m, LPCSTR name)
{
    BOOL more;
    if (!good(m, sizeof *m) || !name)
        return 0;
    for (more = ModuleFirst(m); more; more = ModuleNext(m))
        if (!lstrcmp(m->szModule, name))
            return m->hModule;
    return 0;
}

WORD THAPI ModuleFindHandle(MODULEENTRY FAR *m, WORD h)
{
    if (!good(m, sizeof *m) || !fill_module(m, h))
        return 0;
    return m->hModule;
}

/* ===== tasks (63-65) ===== */
BOOL is_task(WORD t)
{
    return t && readable(t | 1, 0, 0x100) && PW(t | 1, 0xFA) == 0x4454;  /* "TD" */
}

static BOOL fill_task(TASKENTRY FAR *e, WORD t)
{
    WORD s = t | 1, ss, i;
    if (!is_task(t))
        return FALSE;
    e->hTask = s;
    e->hTaskParent = PW(s, 0x22);
    e->hInst = PW(s, 0x1C);
    e->hModule = PW(s, 0x1E);
    e->wSP = PW(s, 0x02);
    e->wSS = ss = PW(s, 0x04);
    if (readable(ss, 0, 0x10)) {
        e->wStackTop = PW(ss, 0x0A);
        e->wStackMinimum = PW(ss, 0x0C);
        e->wStackBottom = PW(ss, 0x0E);
    }
    e->wcEvents = PW(s, 0x06);
    e->hQueue = PW(s, 0x20);
    for (i = 0; i < 8; i++)             /* its 8 bytes as they are (observed) */
        e->szModule[i] = PB(s, 0xF2 + i);
    e->szModule[8] = 0;
    e->wPSPOffset = PW(s, 0x60);
    e->hNext = PW(s, 0x00);
    return TRUE;
}

BOOL THAPI TaskFirst(TASKENTRY FAR *e)
{
    if (!good(e, sizeof *e))
        return FALSE;
    return fill_task(e, thhook[7]);     /* THHOOK 0Eh: HeadTDB */
}

BOOL THAPI TaskNext(TASKENTRY FAR *e)
{
    if (!good(e, sizeof *e) || !e->hNext)
        return FALSE;
    return fill_task(e, e->hNext);
}

BOOL THAPI TaskFindHandle(TASKENTRY FAR *e, WORD t)
{
    if (!good(e, sizeof *e))
        return FALSE;
    return fill_task(e, t);
}

/* ===== system information (71, 72, 80) ===== */
WORD dgroup_of(WORD hmod)
{
    WORD s = hmod | 1, seg;
    if (!is_module(hmod))
        return 0;
    seg = PW(s, 0x0E);                  /* the automatic data segment */
    if (!seg)
        return 0;
    return PW(s, PW(s, 0x22) + (seg - 1) * 10 + 8);     /* its handle */
}

BOOL THAPI SystemHeapInfo(SYSHEAPINFO FAR *p)
{
    if (!good(p, sizeof *p))
        return FALSE;
    p->wUserFreePercent = uFreeRes ? uFreeRes(2) : 0;   /* GFSR_USERRESOURCES */
    p->wGDIFreePercent = uFreeRes ? uFreeRes(1) : 0;    /* GFSR_GDIRESOURCES */
    /* the selectors (observed: odd) */
    p->hUserSegment = dgroup_of(hu) ? dgroup_of(hu) | 1 : 0;
    p->hGDISegment = dgroup_of(hg) ? dgroup_of(hg) | 1 : 0;
    return TRUE;
}

/* DPMI 0500h's block, copied (Schulman ch. 10) */
BOOL THAPI MemManInfo(MEMMANINFO FAR *p)
{
    static DWORD b[12];
    WORD i;
    _asm {
        push es
        push di
        mov ax, ds
        mov es, ax
        lea di, b
        mov ax, 0500h
        int 31h
        pop di
        pop es
    }
    /* observed: TRUE whatever it gets; DPMI's first nine values as they
       are, the page size 1000h (Schulman: fixed) */
    if (!p || !readable(SELECTOROF(p), OFFSETOF(p), sizeof *p))
        return TRUE;
    for (i = 0; i < 9; i++)
        (&p->dwLargestFreeBlock)[i] = b[i];
    p->wPageSize = 0x1000;
    return TRUE;
}

static DWORD (FAR PASCAL *mmTime)(void);

BOOL THAPI TimerCount(TIMERINFO FAR *p)
{
    DWORD t;
    if (!good(p, sizeof *p))
        return FALSE;
    if (GetWinFlags() & WF_ENHANCED) {
        /* the VTD: since Windows started, and this VM's own time (Schulman
           ch. 10; observed: not GetTickCount's count) */
        static DWORD vtd;
        static BOOL asked;
        if (!asked) {
            WORD es_ = 0, di_ = 0;
            asked = TRUE;
            _asm {
                push es
                push di
                xor di, di
                mov es, di
                mov ax, 1684h
                mov bx, 5
                int 2Fh
                mov es_, es
                mov di_, di
                pop di
                pop es
            }
            vtd = MAKELONG(di_, es_);
        }
        if (vtd && (t = vtd_time(vtd, 0x0101)) != 0xFFFFFFFFUL) {
            p->dwmsSinceStart = t;
            t = vtd_time(vtd, 0x0102);
            p->dwmsThisVM = t != 0xFFFFFFFFUL ? t : p->dwmsSinceStart;
            return TRUE;
        }
    }
    if (!mmTime && GetModuleHandle("MMSYSTEM"))
        mmTime = (DWORD (FAR PASCAL *)(void))GetProcAddress(GetModuleHandle("MMSYSTEM"), "TIMEGETTIME");
    if (mmTime)
        t = mmTime();
    else {
        /* GetTickCount, and how far the 8253 has got towards the next
           tick (1193182 Hz, 65536 counts a tick) */
        static DWORD last;
        DWORD t0;
        WORD c;
        do {
            t0 = GetTickCount();
            c = pit_count();
        } while (GetTickCount() != t0);
        t = t0 + (DWORD)(WORD)(0 - c) * 1000 / 1193182L;
        if (t < last)                   /* observed: it never goes back */
            t = last;
        last = t;
    }
    p->dwmsSinceStart = t;
    p->dwmsThisVM = t;
    return TRUE;
}

/* ===== registrations (NotifyRegister, InterruptRegister) =====
   one per task each, in the order made (Schulman ch. 10; observed: the
   first registered gets a notification first). The lists grow in our
   local heap: no fixed limit. */
typedef struct { WORD task; FARPROC cb; WORD flags; } REG;
typedef struct { HLOCAL h; WORD n, cap; } REGLIST;
static REGLIST nl, il;
static BOOL hooked;
static WORD icur;                       /* dispatch: the next callback */

static REG *regs(REGLIST *l)
{
    return l->h ? (REG *)LocalLock(l->h) : NULL;
}

static void unlock(REGLIST *l)
{
    if (l->h)
        LocalUnlock(l->h);
}

static int reg_find(REGLIST *l, WORD task)
{
    REG *r = regs(l);
    int i, k = -1;
    for (i = 0; r && i < (int)l->n; i++)
        if (r[i].task == task) {
            k = i;
            break;
        }
    unlock(l);
    return k;
}

static BOOL reg_add(REGLIST *l, WORD task, FARPROC cb, WORD flags)
{
    REG *r;
    if (l->n == l->cap) {
        WORD cap = l->cap ? l->cap * 2 : 4;
        HLOCAL h = l->h ? LocalReAlloc(l->h, cap * sizeof(REG), LMEM_MOVEABLE)
                        : LocalAlloc(LMEM_MOVEABLE, cap * sizeof(REG));
        if (!h)
            return FALSE;
        l->h = h;
        l->cap = cap;
    }
    r = regs(l);
    r[l->n].task = task;
    r[l->n].cb = cb;
    r[l->n].flags = flags;
    l->n++;
    unlock(l);
    return TRUE;
}

static BOOL reg_del(REGLIST *l, WORD task)
{
    int k = reg_find(l, task);
    REG *r;
    WORD i;
    if (k < 0)
        return FALSE;
    r = regs(l);
    for (i = k; i + 1 < l->n; i++)
        r[i] = r[i + 1];
    l->n--;
    unlock(l);
    if (l == &il && icur > (WORD)k)
        icur--;
    return TRUE;
}

static void exc_update(void);

static void hook_update(void)
{
    BOOL want = nl.n || il.n;
    if (want && !hooked) {
        old_hook = kToolHelpHook((FARPROC)th_hook);
        hooked = TRUE;
    } else if (!want && hooked) {
        kToolHelpHook((FARPROC)old_hook);
        old_hook = 0;
        hooked = FALSE;
    }
    exc_update();
}

static WORD cur_task(void)
{
    return (WORD)GetCurrentTask();
}

/* ===== notifications (73, 74) ===== */
BOOL THAPI NotifyRegister(WORD task, FARPROC cb, WORD flags)
{
    if (!task)
        task = cur_task();
    if (reg_find(&nl, task) >= 0)    /* observed: any task handle (0x1234) */
        return FALSE;
    if (!reg_add(&nl, task, cb, flags))
        return FALSE;
    hook_update();
    return TRUE;
}

BOOL THAPI NotifyUnRegister(WORD task)
{
    BOOL r;
    if (!task)
        task = cur_task();
    r = reg_del(&nl, task);
    hook_update();
    return r;
}

static NFYLOADSEG nls;
static NFYSTARTDLL nsd;
static NFYLOGERROR nle;
static NFYLOGPARAMERROR nlp;

/* one notification to the callbacks in order, until one says it
   handled it */
static WORD deliver(WORD id, DWORD data)
{
    REG *r;
    WORD i, ret = 0;
    for (i = 0; i < nl.n; i++) {
        REG e;
        r = regs(&nl);
        e = r[i];
        unlock(&nl);
        if ((id == NFY_TASKIN || id == NFY_TASKOUT) && !(e.flags & NF_TASKSWITCH))
            continue;
        if (id == NFY_RIP &&                     /* LOG*: to everyone (observed) */
            !(e.flags & NF_RIP))
            continue;
        if (!e.cb)
            continue;
        ret = ((WORD (FAR PASCAL *)(WORD, DWORD))e.cb)(id, data);
        if (ret)
            break;
    }
    return ret;
}

/* KERNEL's hook (entry.asm th_hook): r = ax bx cx dx si di es ds */
WORD __cdecl __far th_notify(WORD FAR *r)
{
    WORD ret = 0;
    switch (r[0]) {
    case 0x12:                          /* OutputDebugString: ES:SI */
        deliver(NFY_OUTSTR, MAKELONG(r[4], r[6]));
        break;
    case 0x50:                          /* a segment loaded */
        nls.dwSize = sizeof nls;
        nls.wSelector = r[2];
        nls.wSegNum = r[1] + 1;
        nls.wType = r[4];
        nls.wcInstance = r[3];
        nls.lpstrModuleName = (LPCSTR)MAKELP(r[6], r[5]);
        deliver(NFY_LOADSEG, (DWORD)(void FAR *)&nls);
        break;
    case 0x52:                          /* a segment freed: BX */
        deliver(NFY_FREESEG, r[1]);
        break;
    case 0x64:                          /* a library's start */
        nsd.dwSize = sizeof nsd;
        nsd.hModule = r[4];
        nsd.wCS = r[2];
        nsd.wIP = r[1];
        deliver(NFY_STARTDLL, (DWORD)(void FAR *)&nsd);
        break;
    case 0x65:                          /* a module deleted: ES */
        deliver(NFY_DELMODULE, r[6]);
        break;
    case 0x59:                          /* a task starts at CX:BX */
        deliver(NFY_STARTTASK, MAKELONG(r[1], r[2]));
        break;
    case 0x62:                          /* a task ends: BL */
        deliver(NFY_EXITTASK, r[1] & 0xFF);
        reg_del(&nl, cur_task());
        reg_del(&il, cur_task());
        hook_update();
        break;
    case 0x0D:
        deliver(NFY_TASKOUT, 0);
        break;
    case 0x0E:
        deliver(NFY_TASKIN, 0);
        break;
    case 0x66:                          /* LogError: CX code, DX:BX data */
        nle.dwSize = sizeof nle;
        nle.wErrCode = r[2];
        nle.lpInfo = MAKELP(r[3], r[1]);
        deliver(NFY_LOGERROR, (DWORD)(void FAR *)&nle);
        break;
    case 0x67: {                        /* LogParamError: ES:BX -> code, address, parameter */
        WORD FAR *p = (WORD FAR *)MAKELP(r[6], r[1]);
        nlp.dwSize = sizeof nlp;
        nlp.wErrCode = p[0];
        nlp.lpfnErrorAddr = (FARPROC)MAKELP(p[2], p[1]);
        nlp.lpBadParam = (void FAR * FAR *)MAKELP(p[4], p[3]);
        deliver(NFY_LOGPARAMERROR, (DWORD)(void FAR *)&nlp);
        break;
    }
    }
    return ret;
}

/* ===== interrupts and faults (75, 76) ===== */
static const BYTE excs[] = { 0, 6, 12, 13, 14 };
static void (__cdecl __far *const exch[])(void) = { exc_0, exc_6, exc_12, exc_13, exc_14 };
static BOOL exc_on;

/* DPMI 0202h/0204h: a handler's address; 0203h/0205h: set one */
static DWORD dpmi_get(WORD fn, WORD n)
{
    WORD c = 0, d = 0;
    _asm {
        mov ax, fn
        mov bx, n
        int 31h
        mov c, cx
        mov d, dx
    }
    return MAKELONG(d, c);
}

static void dpmi_set(WORD fn, WORD n, DWORD a)
{
    WORD c = HIWORD(a), d = LOWORD(a);
    _asm {
        mov ax, fn
        mov bx, n
        mov cx, c
        mov dx, d
        int 31h
    }
}

/* the handlers are ours while anyone has a callback (Schulman: removed
   with the last one) */
static void exc_update(void)
{
    WORD i;
    if (il.n && !exc_on) {
        for (i = 0; i < sizeof excs; i++) {
            old_exc[excs[i]] = dpmi_get(0x0202, excs[i]);
            dpmi_set(0x0203, excs[i], (DWORD)exch[i]);
        }
        old_vec1 = dpmi_get(0x0204, 1);
        dpmi_set(0x0205, 1, (DWORD)vec_1);
        old_vec3 = dpmi_get(0x0204, 3);
        dpmi_set(0x0205, 3, (DWORD)vec_3);
        exc_on = TRUE;
    } else if (!il.n && exc_on) {
        for (i = 0; i < sizeof excs; i++)
            dpmi_set(0x0203, excs[i], old_exc[excs[i]]);
        dpmi_set(0x0205, 1, old_vec1);
        dpmi_set(0x0205, 3, old_vec3);
        exc_on = FALSE;
    }
}

BOOL THAPI InterruptRegister(WORD task, FARPROC cb)
{
    if (!task)
        task = cur_task();
    if (reg_find(&il, task) >= 0)
        return FALSE;
    if (!reg_add(&il, task, cb, 0))
        return FALSE;
    hook_update();
    return TRUE;
}

BOOL THAPI InterruptUnRegister(WORD task)
{
    BOOL r;
    if (!task)
        task = cur_task();
    r = reg_del(&il, task);
    hook_update();
    return r;
}

/* entry.asm, at a fault: whether the callbacks get it. Not the faults
   Windows recovers from itself (a "__GP" range, KERNEL's HasGPHandler:
   Schulman ch. 10, the callbacks never see Windows' own faults), not
   ours. */
WORD __cdecl __far th_fault_wanted(void)
{
    if (!il.n || f_cs == SELECTOROF((void FAR *)th_fault_wanted))
        return 0;
    if ((f_num == 12 || f_num == 13) && kHasGPHandler &&
        kHasGPHandler(MAKELONG(f_ip, f_cs)))
        return 0;
    icur = 0;
    return 1;
}

/* entry.asm dispatch: the next callback (f_cb, f_cbax), 0 when none */
WORD __cdecl __far th_next_cb(void)
{
    REG *r;
    while (icur < il.n) {
        r = regs(&il);
        f_cb = (DWORD)r[icur].cb;
        unlock(&il);
        icur++;
        if (f_cb) {
            f_cbax = dsel;
            return 1;
        }
    }
    return 0;
}

/* ===== task control (77, 81-83) =====
   A task that is not running is parked by the scheduler with SS:SP (TDB
   02h) at a frame of 8 words, then its BP, then the far address it goes
   on at (SS:SP + 10h, 12h, 14h: Schulman ch. 10, observed with the probe
   under Windows 3.1's KERNEL and kept so by w16's, kernel/kentry.asm) */
#define TDB_SSSP        0x02
#define FRAME_BP        0x10
#define FRAME_IP        0x12

static DWORD FAR *parked_csip(WORD t)
{
    WORD s = t | 1, ss = PW(s, TDB_SSSP + 2), sp = PW(s, TDB_SSSP);
    if (!readable(ss, sp, FRAME_IP + 4))
        return NULL;
    return (DWORD FAR *)MAKELP(ss, sp + FRAME_IP);
}

DWORD THAPI TaskGetCSIP(WORD t)
{
    DWORD FAR *p;
    if (!is_task(t) || (t | 1) == (cur_task() | 1) || !(p = parked_csip(t)))
        return 0;
    return *p;
}

DWORD THAPI TaskSetCSIP(WORD t, WORD cs, WORD ip)
{
    DWORD FAR *p, old;
    if (!is_task(t) || (t | 1) == (cur_task() | 1) || !(p = parked_csip(t)))
        return 0;
    old = *p;
    *p = MAKELONG(ip, cs);
    return old;
}

/* the task runs sw_entry (entry.asm) next, which goes on at the address
   given; one event more makes the scheduler run it (Schulman ch. 10),
   taken back there (th_sw_done) */
BOOL THAPI TaskSwitch(WORD t, DWORD csip)
{
    if (!is_task(t) || (t | 1) == (cur_task() | 1) || !parked_csip(t))
        return FALSE;
    sw_csip = csip;
    TaskSetCSIP(t, SELECTOROF((void FAR *)sw_entry), OFFSETOF((void FAR *)sw_entry));
    PW(t | 1, 0x06)++;
    DirectedYield((HTASK)t);
    return TRUE;
}

void __cdecl __far th_sw_done(void)
{
    WORD t = cur_task() | 1;
    if (PW(t, 0x06))
        PW(t, 0x06)--;
}

static WORD term_flags;

/* ends the running task: with the Application Error box (FatalAppExit,
   as for a fault) or without */
static void term_here(void)
{
    if (term_flags & NO_UAE_BOX) {
        _asm {
            mov ax, 4CFFh
            int 21h
        }
    }
    FatalAppExit(0, NULL);
}

static void __far __loadds term_entry(void)
{
    term_here();
}

/* TerminateApp: the task's callbacks go first (SDK Vol. 2) */
void THAPI TerminateApp(WORD t, WORD flags)
{
    if (!t)
        t = cur_task();
    if (!is_task(t))
        return;
    reg_del(&nl, t);
    reg_del(&il, t);
    hook_update();
    term_flags = flags;
    if ((t | 1) == (cur_task() | 1))
        term_here();
    TaskSwitch(t, (DWORD)(void (__far *)(void))term_entry);
}

/* ===== stack traces (66-68) ===== */
/* the module and segment number of a code selector */
static void where(STACKTRACEENTRY FAR *e)
{
    WORD h, m, s, n, i, st;
    /* a selector the global heap does not know: both left as they are
       (observed: Microsoft's TOOLHELP on a code segment of w16's KERNEL,
       which is not in the global heap) */
    if (!e->wCS || !lsl(e->wCS))
        return;
    h = LOWORD(GlobalHandle(e->wCS));
    if (!h)
        return;
    e->hModule = 0;
    e->wSegment = 0;
    m = kGetExePtr(h);
    if (!is_module(m))
        return;
    s = m | 1;
    e->hModule = s;
    n = PW(s, 0x1C);                    /* segment count */
    st = PW(s, 0x22);                   /* the segment table */
    for (i = 0; i < n; i++)
        if ((PW(s, st + i * 10 + 8) | 1) == (h | 1)) {
            e->wSegment = i + 1;
            return;
        }
}

BOOL THAPI StackTraceFirst(STACKTRACEENTRY FAR *e, WORD t)
{
    WORD s, ss, sp;
    if (!good(e, sizeof *e) || !is_task(t) || (t | 1) == (cur_task() | 1))
        return FALSE;
    s = t | 1;
    ss = PW(s, TDB_SSSP + 2);
    sp = PW(s, TDB_SSSP);
    if (!readable(ss, sp, FRAME_IP + 4))
        return FALSE;
    e->hTask = s;
    e->wSS = ss;
    e->wBP = PW(ss, sp + FRAME_BP) & ~1;
    e->wIP = PW(ss, sp + FRAME_IP);
    e->wCS = PW(ss, sp + FRAME_IP + 2);
    e->wFlags = FRAME_FAR;
    where(e);
    return TRUE;
}

BOOL THAPI StackTraceCSIPFirst(STACKTRACEENTRY FAR *e, WORD ss, WORD cs, WORD ip, WORD bp)
{
    if (!good(e, sizeof *e))
        return FALSE;
    e->hTask = cur_task();
    e->wSS = ss;
    e->wCS = cs;
    e->wIP = ip;
    e->wBP = bp;
    where(e);
    return TRUE;
}

WORD lar(WORD sel)
{
    WORD r = 0;
    _asm {
        xor ax, ax
        lar ax, sel
        jz ok
        xor ax, ax
    ok: mov r, ax
    }
    return r;
}

/* the frame below: [BP] the caller's BP (odd for a far frame), [BP+2]
   the return IP, [BP+4] the return CS of a far one (Schulman ch. 10) */
BOOL THAPI StackTraceNext(STACKTRACEENTRY FAR *e)
{
    WORD ss, bp, nbp, ip, cs, top, bot;
    if (!good(e, sizeof *e))
        return FALSE;
    ss = e->wSS;
    bp = e->wBP;
    if (!readable(ss, bp, 6))
        return FALSE;
    nbp = PW(ss, bp);
    if (!nbp || (nbp & ~1) <= bp)
        return FALSE;
    if (readable(ss, 0, 0x10)) {        /* the stack's limits (DGROUP 0Ah, 0Eh) */
        top = PW(ss, 0x0A);
        bot = PW(ss, 0x0E);
        if (top && bot > top && ((nbp & ~1) < top || (nbp & ~1) >= bot))
            return FALSE;
    }
    ip = PW(ss, bp + 2);
    cs = PW(ss, bp + 4);
    if (!(nbp & 1) && ((cs & 3) != (SELECTOROF((void FAR *)StackTraceNext) & 3) ||
                       !(lar(cs) & 0x0800) || lsl(cs) < ip))
        cs = 0;                         /* a near frame */
    e->wFlags = cs ? FRAME_FAR : FRAME_NEAR;
    if (cs)
        e->wCS = cs;
    e->wIP = ip;
    e->wBP = nbp & ~1;
    where(e);
    return TRUE;
}

/* ===== classes (69, 70): USER's list, its first from UserSeeUserDo
   (USER.216, request 5: Schulman ch. 6 and 10); a class (CLS): 00h the
   next, 04h its name's atom (in USER's data segment), 14h its module ===== */
static WORD user_ds(void)
{
    return dgroup_of(hu) | 1;
}

/* GetAtomName in USER's atom table, with DS = USER's (entry.asm) */
extern WORD __cdecl __far atom_in(WORD ds, WORD atom, LPSTR buf, WORD n);

static BOOL fill_class(CLASSENTRY FAR *c, WORD cls)
{
    WORD u = user_ds();
    if (!cls || !readable(u, cls, 0x16))
        return FALSE;
    c->hInst = PW(u, cls + 0x14);
    c->szClassName[0] = 0;
    atom_in(u, PW(u, cls + 4), c->szClassName, MAX_CLASSNAME + 1);
    c->wNext = PW(u, cls);
    return TRUE;
}

BOOL THAPI ClassFirst(CLASSENTRY FAR *c)
{
    if (!good(c, sizeof *c) || !uSeeUserDo)
        return FALSE;
    return fill_class(c, LOWORD(uSeeUserDo(5, 0, 0, 0)));
}

BOOL THAPI ClassNext(CLASSENTRY FAR *c)
{
    if (!good(c, sizeof *c))
        return FALSE;
    return fill_class(c, c->wNext);
}

/* ===== start ===== */
int __cdecl __far th_init(void)
{
    hk = GetModuleHandle("KERNEL");
    hu = GetModuleHandle("USER");
    hg = GetModuleHandle("GDI");
    thhook = (WORD FAR *)GetProcAddress(hk, MAKEINTRESOURCE(332));
    kToolHelpHook = (DWORD (FAR PASCAL *)(FARPROC))GetProcAddress(hk, MAKEINTRESOURCE(341));
    kHasGPHandler = (WORD (FAR PASCAL *)(DWORD))GetProcAddress(hk, MAKEINTRESOURCE(338));
    kGlobalMasterHandle = (DWORD (FAR PASCAL *)(void))GetProcAddress(hk, MAKEINTRESOURCE(28));
    kGetExePtr = (WORD (FAR PASCAL *)(WORD))GetProcAddress(hk, MAKEINTRESOURCE(133));  /* castcheck: ok */
    uSeeUserDo = (DWORD (FAR PASCAL *)(WORD, WORD, WORD, WORD))GetProcAddress(hu, MAKEINTRESOURCE(216));
    uFreeRes = (WORD (FAR PASCAL *)(WORD))GetProcAddress(hu, MAKEINTRESOURCE(284));  /* castcheck: ok */
    _asm mov dsel, ds
    scratch = AllocSelector(dsel);
    return thhook && kToolHelpHook;
}
