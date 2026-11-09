/*
 * tk51.c - THHOOK (KERNEL.332), the notification hooks (ToolHelpHook,
 * KERNEL.341) and Microsoft's TOOLHELP.DLL on top of them. Run as the
 * shell of Windows 3.1 (tests/dropin.sh with -f TK51C.EXE=TK51C.EXE; -r
 * for Microsoft's KRNL286), so that the same TOOLHELP reads the same
 * system on both KERNELs:
 *  - THHOOK: which known value each word of the structure holds
 *  - ToolHelpHook with a recording procedure (tk50s.asm): what KERNEL
 *    reports, in which registers, while a DLL is loaded and freed, a
 *    string is written and a program (TK51C, which only exits) runs
 *  - TOOLHELP: GlobalInfo/First/Next/EntryHandle/EntryModule, Module*,
 *    Task*, StackTraceCSIPFirst/Next, MemManInfo, SystemHeapInfo,
 *    LocalInfo/First/Next, ClassFirst/Next, NotifyRegister
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "tk51ref.h"
#include "tklog.h"

extern WORD __cdecl cb_count, cb_ret, cb_log[];
extern void __cdecl __far rec_cb0(void);
extern void __cdecl __far st_run(void);

#define SEG(p) SELECTOROF((void FAR *)(p))
#define OFF(p) OFFSETOF((void FAR *)(p))

static HMODULE hk;
static HINSTANCE th;
static WORD FAR *thhook;
static char name[80];

static FARPROC kproc(WORD ord)
{
    return GetProcAddress(hk, MAKEINTRESOURCE(ord));
}

static FARPROC tproc(const char *n)
{
    FARPROC f = GetProcAddress(th, n);
    if (!f) {
        lstrcpy(name, "TOOLHELP missing ");
        lstrcat(name, n);
        obs(name, 1);
    }
    return f;
}

static void nm2(const char *a, const char FAR *b)
{
    lstrcpy(name, a);
    lstrcat(name, b);
}

static WORD my_hmod(void)
{
    return GetModuleHandle("TK51");
}

/* ---- THHOOK ---- */
static const char *const known[] = {
    "0", "GlobalMasterHandle AX", "GlobalMasterHandle DX", "hKernel",
    "hUser", "hGdi", "current task", "current PDB", "KERNEL PDB",
    "GetVersion AX", "our DS", "our hModule", "GetCurrentTask DX",
    "hShell", "hDisplay", "hSystem", "hKeyboard", "hMouse", "our PDB parent",
    0
};

static void t_thhook(void)
{
    DWORD (FAR PASCAL *gmh)(void) = (DWORD (FAR PASCAL *)(void))kproc(28);
    DWORD m = gmh(), ct = GetCurrentTask();
    DWORD (FAR PASCAL *gcp)(void) = (DWORD (FAR PASCAL *)(void))kproc(37);
    DWORD pdb = gcp();
    WORD v[19], i, j;
    v[0] = 0;
    v[1] = LOWORD(m);
    v[2] = HIWORD(m);
    v[3] = hk;
    v[4] = GetModuleHandle("USER");
    v[5] = GetModuleHandle("GDI");
    v[6] = LOWORD(ct);
    v[7] = LOWORD(pdb);
    v[8] = HIWORD(pdb);
    v[9] = LOWORD(GetVersion());
    v[10] = SEG(name);
    v[11] = my_hmod();
    v[12] = HIWORD(ct);
    v[13] = GetModuleHandle("SHELL");
    v[14] = GetModuleHandle("DISPLAY");
    v[15] = GetModuleHandle("SYSTEM");
    v[16] = GetModuleHandle("KEYBOARD");
    v[17] = GetModuleHandle("MOUSE");
    v[18] = *(WORD FAR *)MAKELP(LOWORD(pdb), 0x16);
    thhook = (WORD FAR *)kproc(332);
    obs("THHOOK missing", thhook == 0);
    if (!thhook)
        return;
    for (i = 0; i < 0x50; i++) {
        WORD w = thhook[i], k = 0xFF;
        for (j = 0; known[j]; j++)
            if (w == v[j]) {
                k = j;
                break;
            }
        obsn("THHOOK word is", i * 2, k);
        obsn("~THHOOK word", i * 2, w);
    }
}

/* ---- the raw hook ---- */
static void dump_hook(const char *what, WORD hlib)
{
    WORD i, n = cb_count;
    char w[40];
    lstrcpy(w, what);
    lstrcat(w, " calls");
    obs(w, n);
    for (i = 0; i < n && i < 40; i++) {
        WORD FAR *e = (WORD FAR *)&cb_log[i * 16];
        WORD code = e[1];
        lstrcpy(w, what);
        lstrcat(w, " AX");
        obsn(w, i, code);
        lstrcpy(w, what);
        lstrcat(w, " DS==KERNEL DS");
        obsn(w, i, thhook && e[7] == SELECTOROF(thhook));
        switch (code) {
        case 0x50:
            lstrcpy(w, what);
            lstrcat(w, " 50h BX");
            obsn(w, i, e[2]);
            lstrcpy(w, what);
            lstrcat(w, " 50h SI");
            obsn(w, i, e[5]);
            lstrcpy(w, what);
            lstrcat(w, " 50h DX==hlib");
            obsn(w, i, e[4] == hlib);
            if (!IsBadStringPtr((LPSTR)MAKELP(e[8], e[6]), 16))
                obs_str("50h ES:DI", (LPSTR)MAKELP(e[8], e[6]));
            obs_env("50h CX", e[3]);
            obs_env("50h DX", e[4]);
            break;
        case 0x52:
        case 0x5C:
            obs_env("52h BX", e[2]);
            break;
        case 0x12:
            if (!IsBadStringPtr((LPSTR)MAKELP(e[8], e[5]), 64))
                obs_str("12h ES:SI", (LPSTR)MAKELP(e[8], e[5]));
            break;
        case 0x59:
            obs_env("59h CX", e[3]);
            obs_env("59h BX", e[2]);
            break;
        case 0x62:
            obs_env("62h BX", e[2]);
            obs_env("62h CX", e[3]);
            obs_env("62h DX", e[4]);
            obs_env("62h SI", e[5]);
            obs_env("62h DI", e[6]);
            obs_env("62h ES", e[8]);
            obs_env("62h [sp+8]", e[11]);
            lstrcpy(w, what);
            lstrcat(w, " 62h [sp+6]");
            obsn(w, i, e[10]);
            lstrcpy(w, what);
            lstrcat(w, " 62h [sp+4]");
            obsn(w, i, e[9]);
            break;
        case 0x64:
            lstrcpy(w, what);
            lstrcat(w, " 64h SI==hlib");
            obsn(w, i, e[5] == hlib);
            obs_env("64h CX", e[3]);
            obs_env("64h BX", e[2]);
            obs_env("64h SI", e[5]);
            break;
        case 0x65:
            lstrcpy(w, what);
            lstrcat(w, " 65h ES==hlib");
            obsn(w, i, e[8] == hlib);
            obs_env("65h ES", e[8]);
            break;
        default:
            obs_env("BX", e[2]);
            obs_env("CX", e[3]);
            obs_env("DX", e[4]);
            obs_env("ES", e[8]);
        }
    }
}

static WORD wait_gone(const char *mod)
{
    WORD i;
    for (i = 0; i < 2000 && GetModuleHandle(mod); i++)
        Yield();
    return i < 2000;
}

static WORD ver_hmod;

static void actions(void)
{
    HINSTANCE h;
    cb_count = 0;
    OutputDebugString("TK51 hello\r\n");
    h = LoadLibrary("VER.DLL");
    ver_hmod = GetModuleHandle("VER");
    obs("LoadLibrary(VER) ok", h > HINSTANCE_ERROR);
    FreeLibrary(h);
    h = WinExec("C:\\TK51C.EXE", SW_SHOWNORMAL);
    obs("WinExec(TK51C) ok", h > HINSTANCE_ERROR);
    obs("TK51C gone", wait_gone("TK51C"));
}

static void t_rawhook(void)
{
    FARPROC thh = kproc(341);
    DWORD (FAR PASCAL *f)(FARPROC) = (DWORD (FAR PASCAL *)(FARPROC))thh;
    DWORD old;
    WORD n;
    if (!thh) {
        obs("ToolHelpHook missing", 1);
        return;
    }
    cb_ret = 0;
    old = f((FARPROC)rec_cb0);
    obs("ToolHelpHook prev", old != 0);
    cb_count = 0;
    OutputDebugString("TK51 hello\r\n");
    n = cb_count;
    obs("raw OutputDebugString calls", n);
    {
        HINSTANCE h;
        cb_count = 0;
        h = LoadLibrary("VER.DLL");
        n = cb_count;
        f((FARPROC)old);
        dump_hook("raw LoadLibrary", h);
        f((FARPROC)rec_cb0);
        cb_count = 0;
        FreeLibrary(h);
        f((FARPROC)old);
        dump_hook("raw FreeLibrary", h);
        f((FARPROC)rec_cb0);
    }
    cb_count = 0;
    {
        HINSTANCE h = WinExec("C:\\TK51C.EXE", SW_SHOWNORMAL);
        obs("WinExec(TK51C) ok", h > HINSTANCE_ERROR);
        obs("TK51C gone", wait_gone("TK51C"));
    }
    f((FARPROC)old);
    dump_hook("raw WinExec", 0);
}

/* ---- TOOLHELP ---- */
typedef BOOL (FAR PASCAL *P1)(void FAR *);
typedef BOOL (FAR PASCAL *P2)(void FAR *, WORD);
typedef BOOL (FAR PASCAL *P3)(void FAR *, WORD, WORD);

static const char *const gtname[] = {
    "UNKNOWN", "DGROUP", "DATA", "CODE", "TASK", "RESOURCE", "MODULE",
    "FREE", "INTERNAL", "SENTINEL", "BURGERMASTER"
};

static void t_global(void)
{
    P1 ginfo = (P1)tproc("GlobalInfo");
    P2 gfirst = (P2)tproc("GlobalFirst"), gnext = (P2)tproc("GlobalNext");
    P2 geh = (P2)tproc("GlobalEntryHandle");
    P3 gem = (P3)tproc("GlobalEntryModule");
    GLOBALINFO gi;
    GLOBALENTRY ge;
    WORD n = 0, types[12], i, badsize = 0, pdbown = 0;
    WORD nlru = 0, nfree = 0;
    BOOL more;
    if (!ginfo || !gfirst || !gnext || !geh || !gem)
        return;
    gi.dwSize = sizeof gi;
    obs("GlobalInfo", ginfo(&gi));
    obs_env("GlobalInfo wcItems", gi.wcItems);
    obs_env("GlobalInfo wcItemsFree", gi.wcItemsFree);
    obs_env("GlobalInfo wcItemsLRU", gi.wcItemsLRU);
    for (i = 0; i < 12; i++)
        types[i] = 0;
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_ALL); more && n < 8000; more = gnext(&ge, GLOBAL_ALL)) {
        n++;
        types[ge.wType < 11 ? ge.wType : 11]++;
        if (ge.wFlags & GF_PDB_OWNER)
            pdbown++;
        if (ge.hBlock && ge.wType != GT_FREE && ge.wType != GT_SENTINEL) {
            if (GlobalSize(ge.hBlock) && GlobalSize(ge.hBlock) != ge.dwBlockSize)
                badsize++;
        }
    }
    obs("GlobalFirst/Next ALL count==wcItems", n == gi.wcItems);
    obs_env("GlobalFirst/Next ALL count", n);
    for (i = 0; i < 11; i++) {
        nm2("~GlobalNext type ", gtname[i]);
        obs(name, types[i]);
    }
    obs("GlobalNext type>10", types[11]);
    obs("GlobalNext size!=GlobalSize", badsize);
    obs_env("GlobalNext GF_PDB_OWNER", pdbown);
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_LRU); more && nlru < 8000; more = gnext(&ge, GLOBAL_LRU))
        nlru++;
    obs("GlobalFirst/Next LRU count==wcItemsLRU", nlru == gi.wcItemsLRU);
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_FREE); more && nfree < 8000; more = gnext(&ge, GLOBAL_FREE))
        nfree++;
    obs("GlobalFirst/Next FREE count==wcItemsFree", nfree == gi.wcItemsFree);
    /* our DGROUP */
    ge.dwSize = sizeof ge;
    obs("GlobalEntryHandle(DS)", geh(&ge, LOWORD(GlobalHandle(SEG(name)))));
    obs("GlobalEntryHandle(DS) wType", ge.wType);
    obs("GlobalEntryHandle(DS) hOwner==hModule", ge.hOwner == my_hmod());
    obs("GlobalEntryHandle(DS) size==GlobalSize", ge.dwBlockSize ==
        GlobalSize(LOWORD(GlobalHandle(SEG(name)))));
    obs("GlobalEntryHandle(DS) address==base", ge.dwAddress ==
        GetSelectorBase(SEG(name)));
    obs("GlobalEntryHandle(DS) wData", ge.wData);
    obs("GlobalEntryHandle(DS) wcLock", ge.wcLock);
    obs("GlobalEntryHandle(DS) wcPageLock", ge.wcPageLock);
    obs("GlobalEntryHandle(DS) wFlags", ge.wFlags);
    obs("GlobalEntryHandle(DS) wHeapPresent", ge.wHeapPresent);
    ge.dwSize = sizeof ge;
    obs("GlobalEntryHandle(task)", geh(&ge, (WORD)GetCurrentTask()));
    obs("GlobalEntryHandle(task) wType", ge.wType);
    obs("GlobalEntryHandle(task) hOwner==task", ge.hOwner == (WORD)GetCurrentTask());
    ge.dwSize = sizeof ge;
    obs("GlobalEntryHandle(hModule)", geh(&ge, my_hmod()));
    obs("GlobalEntryHandle(hModule) wType", ge.wType);
    obs("GlobalEntryHandle(hModule) hOwner==hModule", ge.hOwner == my_hmod());
    ge.dwSize = sizeof ge;
    obs("GlobalEntryHandle(0)", geh(&ge, 0));
    ge.dwSize = sizeof ge;
    obs("GlobalEntryModule(own,1)", gem(&ge, my_hmod(), 1));
    obs("GlobalEntryModule(own,1) wType", ge.wType);
    obs("GlobalEntryModule(own,1) wData", ge.wData);
    obs("GlobalEntryModule(own,1) hBlock==CS handle", ge.hBlock ==
        LOWORD(GlobalHandle(SEG(t_global))));
    ge.dwSize = sizeof ge;
    obs("GlobalEntryModule(own,2)", gem(&ge, my_hmod(), 2));
    obs("GlobalEntryModule(own,2) wType", ge.wType);
    obs("GlobalEntryModule(own,2) hBlock==DS handle", ge.hBlock ==
        LOWORD(GlobalHandle(SEG(name))));
    ge.dwSize = sizeof ge;
    obs("GlobalEntryModule(KERNEL,1)", gem(&ge, hk, 1));
    obs("GlobalEntryModule(KERNEL,1) wType", ge.wType);
    obs("GlobalEntryModule(KERNEL,1) hBlock==CS handle", ge.hBlock ==
        LOWORD(GlobalHandle(SELECTOROF(kproc(3)))));
}

static void t_modules(void)
{
    P1 mfirst = (P1)tproc("ModuleFirst"), mnext = (P1)tproc("ModuleNext");
    WORD (FAR PASCAL *mfn)(void FAR *, LPCSTR) =
        (WORD (FAR PASCAL *)(void FAR *, LPCSTR))tproc("ModuleFindName");
    MODULEENTRY me;
    WORD n = 0;
    BOOL more;
    if (!mfirst || !mnext || !mfn)
        return;
    me.dwSize = sizeof me;
    for (more = mfirst(&me); more && n < 200; more = mnext(&me)) {
        n++;
        obs_str("module", me.szModule);
        nm2("module usage ", me.szModule);
        if (lstrcmp(me.szModule, "KERNEL"))
            obs(name, me.wcUsage);
        else
            obs_env(name, me.wcUsage);
        nm2("module hModule==GetModuleHandle ", me.szModule);
        obs(name, me.hModule == GetModuleHandle(me.szModule));
        nm2("module path ", me.szModule);
        if (lstrcmp(me.szModule, "KERNEL"))
            obs_str(name, me.szExePath);
    }
    obs("ModuleFirst/Next count", n);
    me.dwSize = sizeof me;
    obs("ModuleFindName(USER)==hUser", mfn(&me, "USER") == GetModuleHandle("USER"));
    obs("ModuleFindName(USER) wcUsage", me.wcUsage);
}

static void t_tasks(void)
{
    P1 tfirst = (P1)tproc("TaskFirst"), tnext = (P1)tproc("TaskNext");
    P2 tfh = (P2)tproc("TaskFindHandle");
    TASKENTRY te;
    WORD n = 0, me = (WORD)GetCurrentTask(), FAR *dg = (WORD FAR *)MAKELP(SEG(name), 0);
    BOOL more;
    if (!tfirst || !tnext || !tfh)
        return;
    te.dwSize = sizeof te;
    for (more = tfirst(&te); more && n < 50; more = tnext(&te)) {
        n++;
        obs_str("task", te.szModule);
        if (te.hTask != me)
            continue;
        obs("task hInst==DS", te.hInst == SEG(name));
        obs("task hModule==ours", te.hModule == my_hmod());
        obs("task wSS==SS", te.wSS == SEG(name));
        obs("task wStackTop==DGROUP 0Ah", te.wStackTop == dg[5]);
        obs("task wStackMinimum==DGROUP 0Ch", te.wStackMinimum == dg[6]);
        obs("task wStackBottom==DGROUP 0Eh", te.wStackBottom == dg[7]);
        obs("task wcEvents", te.wcEvents);
        obs("task hQueue==GetTaskQueue", te.hQueue == ((WORD (FAR PASCAL *)(WORD))kproc(35))(0));
        obs("task wPSPOffset", te.wPSPOffset);
        obs("task hNext", te.hNext);
        obs_env("task hTaskParent", te.hTaskParent);
        obs_env("task wSP", te.wSP);
    }
    obs("TaskFirst/Next count", n);
    te.dwSize = sizeof te;
    obs("TaskFindHandle(own)", tfh(&te, me));
    obs("TaskFindHandle(own) hInst==DS", te.hInst == SEG(name));
}

/* StackTraceCSIPFirst/Next from st_run's frames (tk51s.asm) */
static BOOL (FAR PASCAL *stf)(void FAR *, WORD, WORD, WORD, WORD);
static P1 stn;

void __cdecl __far st_walk(WORD ss, WORD cs, WORD ip, WORD bp)
{
    STACKTRACEENTRY st;
    WORD n = 0;
    BOOL more;
    st.dwSize = sizeof st;
    for (more = stf(&st, ss, cs, ip, bp); more && n < 3; more = stn(&st)) {
        obsn("stack frame wCS==ours", n, st.wCS == cs);
        obsn("stack frame hModule==ours", n, st.hModule == my_hmod());
        obsn("stack frame wSegment", n, st.wSegment);
        obsn("stack frame wFlags", n, st.wFlags);
        obsn("stack frame wSS==SS", n, st.wSS == ss);
        obsn("stack frame hTask==ours", n, st.hTask == (WORD)GetCurrentTask());
        n++;
    }
    obs("stack frames (first 3)", n);
}

static void t_stack(void)
{
    stf = (BOOL (FAR PASCAL *)(void FAR *, WORD, WORD, WORD, WORD))tproc("StackTraceCSIPFirst");
    stn = (P1)tproc("StackTraceNext");
    if (stf && stn)
        st_run();
}

static void t_misc(void)
{
    P1 mmi = (P1)tproc("MemManInfo"), shi = (P1)tproc("SystemHeapInfo");
    P1 tc = (P1)tproc("TimerCount");
    P2 linfo = (P2)tproc("LocalInfo"), lfirst = (P2)tproc("LocalFirst");
    P1 lnext = (P1)tproc("LocalNext");
    P1 cfirst = (P1)tproc("ClassFirst"), cnext = (P1)tproc("ClassNext");
    MEMMANINFO mm;
    SYSHEAPINFO sh;
    TIMERINFO ti;
    LOCALINFO li;
    LOCALENTRY le;
    CLASSENTRY ce;
    WORD n = 0, dsh = LOWORD(GlobalHandle(SEG(name)));
    BOOL more;
    if (mmi) {
        mm.dwSize = sizeof mm;
        obs("MemManInfo", mmi(&mm));
        obs("MemManInfo wPageSize", mm.wPageSize);
        obs_env("MemManInfo largest free KB", (WORD)(mm.dwLargestFreeBlock >> 10));
        obs_env("MemManInfo max pages", (WORD)mm.dwMaxPagesAvailable);
        obs_env("MemManInfo max lockable", (WORD)mm.dwMaxPagesLockable);
        obs_env("MemManInfo linear", (WORD)mm.dwTotalLinearSpace);
        obs_env("MemManInfo unlocked", (WORD)mm.dwTotalUnlockedPages);
        obs_env("MemManInfo free pages", (WORD)mm.dwFreePages);
        obs_env("MemManInfo total pages", (WORD)mm.dwTotalPages);
        obs_env("MemManInfo free linear", (WORD)mm.dwFreeLinearSpace);
        obs_env("MemManInfo swap", (WORD)mm.dwSwapFilePages);
    }
    if (shi) {
        sh.dwSize = sizeof sh;
        obs("SystemHeapInfo", shi(&sh));
        obs_env("SystemHeapInfo USER %", sh.wUserFreePercent);
        obs_env("SystemHeapInfo GDI %", sh.wGDIFreePercent);
        obs_env("SystemHeapInfo hUserSegment", sh.hUserSegment);
        obs_env("SystemHeapInfo hGDISegment", sh.hGDISegment);
    }
    if (tc) {
        ti.dwSize = sizeof ti;
        obs("TimerCount", tc(&ti));
        obs("TimerCount this VM == since start", ti.dwmsThisVM == ti.dwmsSinceStart);
    }
    if (linfo && lfirst && lnext) {
        li.dwSize = sizeof li;
        obs("LocalInfo(DS)", linfo(&li, dsh));
        obs_env("LocalInfo(DS) wcItems", li.wcItems);
        le.dwSize = sizeof le;
        for (more = lfirst(&le, dsh); more && n < 500; more = lnext(&le))
            n++;
        obs("LocalFirst/Next count==wcItems", n == li.wcItems);
        obs_env("LocalFirst/Next count", n);
    }
    if (cfirst && cnext) {
        n = 0;
        ce.dwSize = sizeof ce;
        for (more = cfirst(&ce); more && n < 100; more = cnext(&ce)) {
            n++;
            obs_str("class", ce.szClassName);
        }
        obs("ClassFirst/Next count", n);
    }
}

/* ---- NotifyRegister ---- */
static WORD nlog[64][12];
static WORD ncount;

BOOL __far __pascal __loadds __export ncb(WORD id, DWORD data)
{
    WORD FAR *e;
    if (ncount >= 64)
        return FALSE;
    e = nlog[ncount++];
    e[0] = id;
    e[1] = LOWORD(data);
    e[2] = HIWORD(data);
    switch (id) {
    case NFY_LOADSEG: {
        NFYLOADSEG FAR *p = (NFYLOADSEG FAR *)data;
        e[3] = p->wSelector;
        e[4] = p->wSegNum;
        e[5] = p->wType;
        e[6] = p->wcInstance;
        e[7] = *(WORD FAR *)p->lpstrModuleName;
        e[8] = (WORD)p->dwSize;
        break;
    }
    case NFY_STARTDLL: {
        NFYSTARTDLL FAR *p = (NFYSTARTDLL FAR *)data;
        e[3] = p->hModule;
        e[4] = p->wCS;
        e[5] = p->wIP;
        e[8] = (WORD)p->dwSize;
        break;
    }
    case NFY_OUTSTR:
        e[3] = *(WORD FAR *)data;
        break;
    }
    e[9] = (WORD)GetCurrentTask();
    return FALSE;
}

static void t_notify(void)
{
    BOOL (FAR PASCAL *nr)(HTASK, FARPROC, WORD) =
        (BOOL (FAR PASCAL *)(HTASK, FARPROC, WORD))tproc("NotifyRegister");
    BOOL (FAR PASCAL *nu)(HTASK) = (BOOL (FAR PASCAL *)(HTASK))tproc("NotifyUnRegister");
    WORD i;
    if (!nr || !nu)
        return;
    ncount = 0;
    obs("NotifyRegister", nr(0, MakeProcInstance((FARPROC)ncb, (HINSTANCE)SEG(name)), NF_NORMAL));
    actions();
    obs("NotifyUnRegister", nu(0));
    obs("notifications", ncount);
    for (i = 0; i < ncount; i++) {
        WORD FAR *e = nlog[i];
        obsn("notify wID", i, e[0]);
        switch (e[0]) {
        case NFY_LOADSEG:
            obsn("notify LOADSEG wSegNum", i, e[4]);
            obsn("notify LOADSEG wType", i, e[5]);
            obsn("notify LOADSEG wcInstance", i, e[6]);
            obsn("notify LOADSEG name word", i, e[7]);
            obsn("notify LOADSEG dwSize", i, e[8]);
            break;
        case NFY_STARTDLL:
            obsn("notify STARTDLL hModule==VER", i, e[3] == ver_hmod);
            obsn("notify STARTDLL dwSize", i, e[8]);
            break;
        case NFY_OUTSTR:
            obsn("notify OUTSTR first word", i, e[3]);
            break;
        case NFY_EXITTASK:
            obsn("notify EXITTASK data lo", i, e[1]);
            break;
        case NFY_DELMODULE:
            obsn("notify DELMODULE is VER", i, e[1] == ver_hmod);
            break;
        case NFY_STARTTASK:
            obs_env("notify STARTTASK CS", e[2]);
            obs_env("notify STARTTASK IP", e[1]);
            break;
        default:
            obs_env("notify data lo", e[1]);
            obs_env("notify data hi", e[2]);
        }
    }
}

int __cdecl tmain(void)
{
    hk = GetModuleHandle("KERNEL");
    out("TK51 start\r\n");
    t_thhook();
    out("raw hook\r\n");
    t_rawhook();
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
    if (th > HINSTANCE_ERROR) {
        out("global\r\n");
        t_global();
        out("modules\r\n");
        t_modules();
        out("tasks\r\n");
        t_tasks();
        out("stack\r\n");
        t_stack();
        out("misc\r\n");
        t_misc();
        out("notify\r\n");
        t_notify();
    }
    out("TK51 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
