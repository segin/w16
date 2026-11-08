/*
 * tk15.c - task probe: the task database (TDB) and the task APIs of the
 * running task; starting other tasks (LoadModule, WinExec, a second
 * instance); what a task started later sees (registers, PSP, environment,
 * command line, its implicit DLLs' initialisation); Yield, WaitEvent,
 * PostEvent, DirectedYield and the order tasks run in; what is freed when
 * a task ends; MakeProcInstance thunks in the TDB.
 *
 * The task layout probed is the one published in Pietrek, "Windows
 * Internals" (1993), Table 3-2; this probe checks it black-box.
 *
 * Starts TK15A.EXE (tk15a.c, from the directory TK15.EXE is in), which
 * imports TKDLL.DLL and TKDLB.DLL and loads TKID.TPL: tests/tk5ref.sh
 * TK15 puts them in place. TK15A writes what it sees into this program's
 * DGROUP (tk15.h). The probe makes C:\TK15DIR.
 *
 * Output and checks: see tklog.h; reference values in tk15ref.h.
 */
#include <windows.h>

#define TK_REF "tk15ref.h"
#define TK_REF286 "tk15ref286.h"
#include "tklog.h"
#include "tk15.h"

extern int __cdecl dos_int21(WORD FAR *r);

#define STEP(s) out("-- " s "\r\n")

static tk15sh sh;
static char helper[128];
static WORD self_task;

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static void ptr(WORD n)
{
    if (sh.ntrace < TK15_TRACE)
        sh.trace[sh.ntrace++] = TR_P(n);
}

static void reset(void)
{
    BYTE FAR *p = (BYTE FAR *)&sh;
    WORD i;
    for (i = 0; i < sizeof sh; i++)
        p[i] = 0;
}

static int dos(WORD *r, WORD ax, const void FAR *dsdx)
{
    WORD i;
    for (i = 0; i < 8; i++)
        r[i] = 0;
    r[0] = ax;
    r[4] = (WORD)(DWORD)dsdx;
    if ((ax >> 8) != 0x47)
        r[3] = r[4];
    r[6] = (WORD)((DWORD)dsdx >> 16);
    return dos_int21((WORD FAR *)r);
}

static void cwd(char *buf)
{
    WORD r[8];
    buf[0] = 0;
    dos(r, 0x4700, buf);
}

static void name2(char *d, const char *a, const char *b)
{
    while (*a)
        *d++ = *a++;
    *d++ = ' ';
    while ((*d++ = *b++) != 0)
        ;
}

static void trace_report(const char *tag)
{
    WORD i;
    char nm[40];
    name2(nm, tag, "trace length");
    obs(nm, sh.ntrace);
    for (i = 0; i < sh.ntrace; i++) {
        name2(nm, tag, "trace");
        obsn(nm, i, sh.trace[i]);
    }
}

/* a TDB string (NUL-terminated or n bytes) */
static void tdb_str(const char *name, WORD t, WORD off, WORD n)
{
    char b[80];
    WORD i;
    for (i = 0; i < n && i < sizeof b - 1 && tdbb(t, off + i); i++)
        b[i] = (char)tdbb(t, off + i);
    b[i] = 0;
    obs_str(name, b);
}

/* ---- the running task ---- */

static void self(void)
{
    DWORD d = K_GETCURRENTTASK(), pd;
    WORD t = (WORD)d, pdb, r[8], q;
    char dir[70];

    STEP("self");
    self_task = t;
    obs_env("hTask", t);
    obs("hTask & 1", t & 1);
    obs("GetCurrentTask DX==hTask", (WORD)(d >> 16) == t);
    obs("GetNumTasks", K_GETNUMTASKS());
    obs_env("GlobalHandle(hTask)", (WORD)GlobalHandle(t));
    obs("GlobalHandle(hTask)==hTask", (WORD)GlobalHandle(t) == t);
    obs("GlobalSize(hTask)", (WORD)GlobalSize(t));
    obs("GlobalSize(hTask) high", (WORD)(GlobalSize(t) >> 16));
    obs("TDB base below 1 MB", K_GETSELECTORBASE(t) < 0x100000UL);
    obs("TDB base & 15", (WORD)K_GETSELECTORBASE(t) & 15);
    dump("TDB", t, 0, 0x80);
    obs("TDB [00h] next", tdbw(t, 0));
    obs_env("TDB [02h] SP", tdbw(t, 2));
    obs_env("TDB [04h] SS", tdbw(t, 4));
    obs("TDB [06h] events", tdbw(t, 6));
    obs("TDB [08h] priority", tdbw(t, 8));
    obs_env("TDB [0Ch]", tdbw(t, 0x0C));
    obs("TDB [0Ch]==hTask", tdbw(t, 0x0C) == t);
    obs("TDB [14h]", tdbw(t, 0x14));
    obs("TDB [16h] flags", tdbw(t, 0x16));
    obs("TDB [18h] error mode", tdbw(t, 0x18));
    obs("TDB [1Ah] version", tdbw(t, 0x1A));
    obs("TDB [1Ch]==hInstance", tdbw(t, 0x1C) == hinst());
    obs("TDB [1Eh]==hModule", tdbw(t, 0x1E) == (WORD)GetModuleHandle("TK15"));
    obs("TDB [20h] queue", tdbw(t, 0x20));
    obs_env("TDB [22h] parent", tdbw(t, 0x22));
    obs("TDB [22h] is a task", K_ISTASK(tdbw(t, 0x22)));
    pd = K_GETCURRENTPDB();
    pdb = (WORD)pd;
    obs_env("GetCurrentPDB", pdb);
    obs_env("GetCurrentPDB DX", (WORD)(pd >> 16));
    obs("TDB [60h]==GetCurrentPDB", tdbw(t, 0x60) == pdb);
    obs("PDB base - TDB base", (WORD)(K_GETSELECTORBASE(pdb) - K_GETSELECTORBASE(t)));
    obs_env("TDB [62h] DTA off", tdbw(t, 0x62));
    obs_env("TDB [64h] DTA seg", tdbw(t, 0x64));
    obs("TDB DTA==PDB:80h", tdbw(t, 0x62) == 0x80 && tdbw(t, 0x64) == pdb);
    obs("TDB [66h] drive", tdbb(t, 0x66));
    tdb_str("TDB [67h] directory", t, 0x67, 0x41);
    cwd(dir);
    obs_str("current directory", dir);
    obs("TDB [AAh] yield to", tdbw(t, 0xAA));
    obs_env("TDB [ACh]", tdbw(t, 0xAC));
    obs_env("TDB [AEh]", tdbw(t, 0xAE));
    obs_env("TDB [B0h] alias", tdbw(t, 0xB0));
    obs("TDB alias base==TDB base",
        K_GETSELECTORBASE(tdbw(t, 0xB0)) == K_GETSELECTORBASE(t));
    obs("TDB [B2h]", tdbw(t, 0xB2));
    obs("TDB [B4h]", tdbw(t, 0xB4));
    obs("TDB [B6h]", tdbw(t, 0xB6));
    obs("TDB [B8h]", tdbw(t, 0xB8));
    dump("TDB", t, 0xB0, 0x28);
    tdb_str("TDB [F2h] name", t, 0xF2, 8);
    obs("TDB [FAh]", tdbw(t, 0xFA));
    dump("TDB", t, 0x100, 0x80);

    /* the PDB */
    obs("PDB [00h]", tdbw(pdb, 0));
    obs_env("PDB [16h] parent", tdbw(pdb, 0x16));
    obs_env("PDB [2Ch] environment", tdbw(pdb, 0x2C));
    obs("PDB [2Ch]==GetDOSEnvironment",
        tdbw(pdb, 0x2C) == (WORD)(K_GETDOSENVIRONMENT() >> 16));
    obs("GetDOSEnvironment offset", (WORD)K_GETDOSENVIRONMENT());
    obs("PDB [32h] handles", tdbw(pdb, 0x32));
    obs_env("PDB [34h]", tdbw(pdb, 0x34));
    obs_env("PDB [36h]", tdbw(pdb, 0x36));
    obs("PDB JFT in PDB", tdbw(pdb, 0x34) == 0x18 && tdbw(pdb, 0x36) == pdb);
    dos(r, 0x6200, NULL);
    obs("INT 21h 62h==GetCurrentPDB", r[1] == pdb);
    obs_env("INT 21h 62h", r[1]);
    dos(r, 0x5100, NULL);
    obs("INT 21h 51h==GetCurrentPDB", r[1] == pdb);

    /* other task APIs */
    obs("GetExePtr(hInstance)==hModule",
        K_GETEXEPTR(hinst()) == (WORD)GetModuleHandle("TK15"));
    obs("GetExePtr(hModule)==hModule",
        K_GETEXEPTR((WORD)GetModuleHandle("TK15")) == (WORD)GetModuleHandle("TK15"));
    obs("GetExePtr(hTask)==hModule", K_GETEXEPTR(t) == (WORD)GetModuleHandle("TK15"));
    obs("GetExePtr(0)", K_GETEXEPTR(0));
    obs("GetExePtr(KERNEL hModule)==it",
        K_GETEXEPTR((WORD)GetModuleHandle("KERNEL")) == (WORD)GetModuleHandle("KERNEL"));
    obs("IsTask(hTask)==hTask", K_ISTASK(t) == t);
    obs("IsTask(hTask | 1)==hTask | 1", K_ISTASK(t | 1) == (t | 1));
    obs("IsTask(hTask & ~1)==hTask", K_ISTASK(t & ~1u) == t);
    obs("IsTask(hTask & ~1)==hTask & ~1", K_ISTASK(t & ~1u) == (t & ~1u));
    obs("IsTask(hInstance)", K_ISTASK(hinst()));
    obs("IsTask(hModule)", K_ISTASK((WORD)GetModuleHandle("TK15")));
    obs("IsTask(0)", K_ISTASK(0));
    obs("IsTask(PDB)", K_ISTASK(pdb));
    obs("GetTaskDS==DS", K_GETTASKDS() == entry_regs[E_DS]);
    obs("GetTaskDS==hInstance", K_GETTASKDS() == hinst());
    obs_env("GetTaskDS", K_GETTASKDS());
    obs_env("GetExePtr(hTask)", K_GETEXEPTR(t));
    obs("InitTask AX==PDB", init_regs[0] == pdb);
    obs("InitTask DX", init_regs[3]);
    /* these depend on the probe's own size (its reference table!), so
       they are compared relative to each other */
    obs_env("DGROUP size", (WORD)GlobalSize((HGLOBAL)hinst()));
    obs_env("pHeap", *(WORD __near *)6);
    obs_env("entry SP", entry_regs[E_SP]);
    obs("DGROUP size - round32(entry SP + heap)",
        (WORD)GlobalSize((HGLOBAL)hinst()) - ((entry_regs[E_SP] + entry_regs[E_CX] + 31) & ~31u));
    obs("pHeap + heap - 10h - DGROUP size",
        *(WORD __near *)6 + entry_regs[E_CX] - 0x10 - (WORD)GlobalSize((HGLOBAL)hinst()));
    obs("GetTaskQueue(0)", K_GETTASKQUEUE(0));
    obs("GetTaskQueue(hTask)", K_GETTASKQUEUE(t));
    q = K_SETTASKQUEUE(0, 0x1234);
    obs("SetTaskQueue(0, 1234h)", q);
    obs("then GetTaskQueue(0)", K_GETTASKQUEUE(0));
    obs("then TDB [20h]", tdbw(t, 0x20));
    obs("SetTaskQueue(hTask, 0)", K_SETTASKQUEUE(t, 0));
    obs("then TDB [20h] again", tdbw(t, 0x20));

    /* events of the running task */
    obs("PostEvent(0)==hTask", K_POSTEVENT(0) == t);
    obs("events after PostEvent(0)", tdbw(t, 6));
    obs("PostEvent(hTask)==hTask", K_POSTEVENT(t) == t);
    obs("events after PostEvent(hTask)", tdbw(t, 6));
    obs("WaitEvent(0)", K_WAITEVENT(0));
    obs("events after WaitEvent", tdbw(t, 6));
    obs("WaitEvent(0) again", K_WAITEVENT(0));
    obs("events after second WaitEvent", tdbw(t, 6));
    K_YIELD();
    obs("events after Yield", tdbw(t, 6));
    K_OLDYIELD();
    obs("events after OldYield", tdbw(t, 6));
    K_DIRECTEDYIELD(t);
    obs("events after DirectedYield(self)", tdbw(t, 6));
    obs("TDB [AAh] after DirectedYield(self)", tdbw(t, 0xAA));
}

/* ---- MakeProcInstance thunks ---- */

static WORD FAR PASCAL cb(WORD a)
{
    return a + 1;
}

static void thunks(void)
{
    FARPROC p[10], q;
    WORD t = self_task, alias = tdbw(t, 0xB0), i;
    char nm[40];

    STEP("thunks");
    obs("TDB [B8h] before", tdbw(t, 0xB8));
    for (i = 0; i < 10; i++) {
        p[i] = MakeProcInstance((FARPROC)cb, (HINSTANCE)hinst());
        name2(nm, "thunk", "sel==TDB alias");
        obsn(nm, i, (WORD)((DWORD)p[i] >> 16) == alias);
        name2(nm, "thunk", "off");
        if ((WORD)((DWORD)p[i] >> 16) == alias)
            obsn(nm, i, (WORD)(DWORD)p[i]);
        else
            obsn("~thunk off (other segment)", i, (WORD)(DWORD)p[i]);
        name2(nm, "thunk", "TDB [B8h]");
        obsn(nm, i, tdbw(t, 0xB8));
        name2(nm, "thunk", "TDB [B2h]!=0");
        obsn(nm, i, tdbw(t, 0xB2) != 0);
        name2(nm, "thunk", "sel==TDB [B2h]");
        obsn(nm, i, (WORD)((DWORD)p[i] >> 16) == tdbw(t, 0xB2));
    }
    dump("TDB after thunks", t, 0xB0, 0x28);
    if (tdbw(t, 0xB2)) {
        WORD s = tdbw(t, 0xB2);
        obs_env("thunk segment 2", s);
        obs("thunk segment 2 size", (WORD)GlobalSize(s));
        dump("thunk segment 2", s, 0, 0x20);
    }
    obs("thunk 9 call", ((WORD (FAR PASCAL *)(WORD))p[9])(41));
    obs("thunk 0 byte 0", *(BYTE FAR *)p[0]);
    FreeProcInstance(p[3]);
    obs("TDB [B8h] after freeing thunk 3", tdbw(t, 0xB8));
    dump("TDB after free 3", t, 0xB0, 0x28);
    q = MakeProcInstance((FARPROC)cb, (HINSTANCE)hinst());
    obs("next thunk==thunk 3", q == p[3]);
    FreeProcInstance(p[8]);
    q = MakeProcInstance((FARPROC)cb, (HINSTANCE)hinst());
    obs("next thunk==thunk 8", q == p[8]);
    for (i = 0; i < 10; i++)
        FreeProcInstance(p[i]);
    obs("TDB [B8h] after freeing all", tdbw(t, 0xB8));
    dump("TDB after free all", t, 0xB0, 0x28);
    q = MakeProcInstance((FARPROC)cb, (HINSTANCE)hinst());
    obs("thunk after freeing all: off", (WORD)(DWORD)q);
    obs("thunk after freeing all: alias", (WORD)((DWORD)q >> 16) == alias);
    FreeProcInstance(q);
}

/* ---- starting TK15A ---- */

static WORD hex4(char *d, WORD v)
{
    int i;
    for (i = 3; i >= 0; i--) {
        d[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    return 4;
}

/* " M S DDDD OOOO TTTT" */
static WORD args(char *d, char mode, WORD slot)
{
    WORD n = 0;
    d[n++] = ' ';
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

static WORD winexec(char mode, WORD slot, WORD show)
{
    char cmd[200];
    WORD n = 0, i;
    for (i = 0; helper[i]; i++)
        cmd[n++] = helper[i];
    args(cmd + n, mode, slot);
    return K_WINEXEC(cmd, show);
}

static WORD show7[2] = { 2, 7 };
static struct {
    WORD env;
    char FAR *cmd;
    WORD FAR *show;
    DWORD reserved;
} parms;
static char ptail[64];

static WORD loadmodule(char mode, WORD slot)
{
    WORD n = args(ptail + 1, mode, slot);
    ptail[0] = (char)n;
    ptail[1 + n] = '\r';
    parms.env = 0;
    parms.cmd = ptail;
    parms.show = show7;
    parms.reserved = 0;
    return K_LOADMODULE(helper, &parms);
}

/* the observations TK15A made in `slot` */
static void child_report(const char *tag, WORD slot, WORD h)
{
    WORD FAR *v = sh.v[slot];
    char nm[60], s[20];
    WORD i;

#define O(n, val) (name2(nm, tag, n), obs(nm, (val)))
#define OE(n, val) (name2(nm, tag, n), obs_env(nm, (val)))
    O("ran", v[V_DONE]);
    if (!v[V_HTASK])
        return;
    OE("hTask", v[V_HTASK]);
    O("events at entry", v[V_EV_ENTRY]);
    O("parent events at entry", v[V_PEV_ENTRY]);
    O("TKDLL inits before InitTask", v[V_DLL_PRE]);
    O("TKDLB inits before InitTask", v[V_DLB_PRE]);
    O("TKDLL inits after InitTask", v[V_DLL_CALLS]);
    O("TKDLB inits after InitTask", v[V_DLB_CALLS]);
    O("TKDLB inited when TKDLL inited", v[V_DLL_SAWDLB]);
    O("TKDLL init SS==task SS", v[V_DLL_SS] == v[V_E_SS]);
    O("TKDLB init SS==task SS", v[V_DLB_SS] == v[V_E_SS]);
    O("TKDLL init SP==TKDLB init SP", v[V_DLL_SP] == v[V_DLB_SP]);
    OE("entry SP - TKDLL init SP", v[V_E_SP] - v[V_DLL_SP]);
    OE("pre_init SP - TKDLL init SP", v[V_SP_PRE] - v[V_DLL_SP]);
    O("TKDLL init DI==its DS & ~1", v[V_DLL_DI] == (v[V_DLL_DSNOW] & ~1u));
    O("TKDLL init ES", v[V_DLL_ES]);
    O("TKDLL init cmdlen", v[V_DLL_CMDLEN]);
    O("entry AX", v[V_E_AX]);
    O("entry BX", v[V_E_BX]);
    O("entry CX", v[V_E_CX]);
    O("entry DX", v[V_E_DX]);
    OE("entry SI", v[V_E_SI]);
    O("entry BP", v[V_E_BP]);
    O("entry SP", v[V_E_SP]);
    O("entry DI==DS & ~1", v[V_E_DI] == (v[V_E_DS] & ~1u));
    O("entry ES==PDB", v[V_E_ES] == v[V_PDB]);
    O("entry SS==DS", v[V_E_SS] == v[V_E_DS]);
    O("result==hInstance", h == (v[V_E_DS] & ~1u));
    O("InitTask AX==PDB", v[V_I_AX] == v[V_PDB]);
    OE("InitTask AX", v[V_I_AX]);
    O("InitTask BX - 81h", v[V_I_BX] - 0x81);
    O("InitTask ES==PDB", v[V_I_ES] == v[V_PDB]);
    O("InitTask CX", v[V_I_CX]);
    O("InitTask DX", v[V_I_DX]);
    OE("InitTask SI", v[V_I_SI]);
    O("InitTask DI==entry DI", v[V_I_DI] == v[V_E_DI]);
    for (i = 0; i < 16 && ((char FAR *)&v[V_CMD])[i]; i++)
        s[i] = ((char FAR *)&v[V_CMD])[i];
    s[i] = 0;
    name2(nm, tag, "command line");
    obs_str(nm, s);
    O("command line length", i);
    O("PSP [80h] length", v[V_PSP80] & 0xFF);
    for (i = 0; i < 8; i++) {
        /* from word 2 on: TK15's DS etc. in hex, which vary */
        nm[0] = '~';
        name2(nm + 1, tag, "PSP tail word");
        obsn(i < 2 ? nm + 1 : nm, i, v[V_TAIL + i]);
    }
    O("TDB [00h]", v[V_T00]);
    O("TDB [06h] events after InitTask", v[V_T06]);
    O("TDB [08h] priority", v[V_T08]);
    O("TDB [0Ch]==hTask", v[V_T0C] == v[V_HTASK]);
    O("TDB [1Ah] version", v[V_T1A]);
    O("TDB [1Ch]==hInstance", v[V_T1C] == (v[V_E_DS] & ~1u));
    O("TDB [1Eh]==hModule", v[V_T1E] == v[V_HMOD]);
    O("TDB [20h] queue", v[V_T20]);
    O("TDB [22h]==parent hTask", v[V_T22] == self_task);
    OE("TDB [22h]", v[V_T22]);
    O("TDB [60h]==GetCurrentPDB", v[V_T60] == v[V_PDB]);
    O("TDB DTA==PDB:80h", v[V_T62] == 0x80 && v[V_T64] == v[V_PDB]);
    O("TDB [66h] drive", v[V_T66] & 0xFF);
    O("TDB [AAh]", v[V_TAA]);
    OE("TDB [ACh]", v[V_TAC]);
    OE("TDB [AEh]", v[V_TAE]);
    O("TDB [B2h]", v[V_TB2]);
    O("TDB [B4h]", v[V_TB4]);
    O("TDB [B8h]", v[V_TB8]);
    O("TDB [FAh]", v[V_TFA]);
    O("thunk sel==TDB [B0h]", v[V_MPI_SEG] == v[V_TB0]);
    O("thunk off", v[V_MPI_OFF]);
    O("GetCurrentPDB DX==parent PDB [16h]", v[V_PDB_DX] == tdbw(tdbw(self_task, 0x60), 0x16));
    O("PDB!=parent's", v[V_PDB] != tdbw(self_task, 0x60));
    O("INT 21h 62h==PDB", v[V_I21_62] == v[V_PDB]);
    O("INT 21h 51h==PDB", v[V_I21_51] == v[V_PDB]);
    O("PDB [16h]==parent PDB", v[V_PSP16] == tdbw(self_task, 0x60));
    O("PDB [16h]==parent's PDB [16h]", v[V_PSP16] == tdbw(tdbw(self_task, 0x60), 0x16));
    OE("PDB [16h]", v[V_PSP16]);
    O("PDB [2Ch]==GetDOSEnvironment", v[V_PSP2C] == v[V_ENV]);
    O("environment selector==parent's", v[V_ENV_SAMESEL]);
    O("environment text==parent's", v[V_ENV_SAMETEXT]);
    O("GetNumTasks", v[V_NTASKS]);
    O("GetModuleUsage", v[V_USAGE]);
    OE("CS", v[V_CS]);
    OE("DS", v[V_DS]);
    O("DGROUP size", v[V_DGSIZE]);
    O("pHeap", v[V_PHEAP]);
    O("WaitEvent result", v[V_WAITRET]);
    O("events after first wait", v[V_EV_A1]);
    O("events at the end", v[V_EV_A2]);
#undef O
#undef OE
}

/* the task list from the head, as roles: 0 TK15, 1-3 TK15A slots, 9 other */
static void task_list(const char *tag)
{
    WORD t = (WORD)(K_GETCURRENTTASK() >> 16), n = 0, j, role;
    char nm[50];
    for (; t && n < 16; t = tdbw(t, 0), n++) {
        role = 9;
        if (t == self_task)
            role = 0;
        for (j = 0; j < TK15_SLOTS; j++)
            if (sh.v[j][V_HTASK] && t == sh.v[j][V_HTASK])
                role = 1 + j;
        name2(nm, tag, "task list");
        obsn(nm, n, role);
        name2(nm, tag, "task list events");
        obsn(nm, n, tdbw(t, 6));
    }
    name2(nm, tag, "task list length");
    obs(nm, n);
}

/* mode A, started by LoadModule */
static void child_a(void)
{
    WORD h, i, ct, cwd_ok = 0xFFFF, mod, dll;
    char before[70], now[70];
    WORD r[8];

    STEP("LoadModule TK15A");
    reset();
    dos(r, 0x3900, "C:\\TK15DIR");
    cwd(before);
    ptr(1);
    h = loadmodule('A', 0);
    ptr(2);
    obs_env("A LoadModule", h);
    obs("A LoadModule > 32", h > 32);
    if (h <= 32)
        return;
    ct = sh.v[0][V_HTASK];
    obs("A GetNumTasks after LoadModule", K_GETNUMTASKS());
    obs("A IsTask(child)==child", K_ISTASK(ct) == ct);
    obs("A child events after LoadModule", tdbw(ct, 6));
    obs("A own events after LoadModule", tdbw(self_task, 6));
    obs("A child TDB [04h]==its SS", tdbw(ct, 4) == sh.v[0][V_E_SS]);
    obs("A child TDB [02h] < entry SP", tdbw(ct, 2) < sh.v[0][V_E_SP]);
    mod = K_GETEXEPTR(h);
    obs("A GetExePtr(hInstance)==child's hModule", mod == sh.v[0][V_HMOD]);
    obs("A GetExePtr(child hTask)==child's hModule", K_GETEXEPTR(ct) == mod);
    obs("A GetModuleUsage", GetModuleUsage((HINSTANCE)mod));
    obs("A GetModuleUsage(TKDLL)", GetModuleUsage(GetModuleHandle("TKDLL")));
    obs("A GetModuleUsage(TKDLB)", GetModuleUsage(GetModuleHandle("TKDLB")));
    task_list("A");
    for (i = 0; i < 20 && K_ISTASK(ct); i++) {
        ptr(0x10 + i);
        K_YIELD();
        if (cwd_ok == 0xFFFF && sh.v[0][V_GBLOCK]) {
            cwd(now);
            cwd_ok = str_eq(now, before);
            obs("A own directory unchanged", cwd_ok);
            tdb_str("A child TDB [67h] directory", ct, 0x67, 0x41);
            obs("A child TDB [66h] drive", tdbb(ct, 0x66));
            tdb_str("A own TDB [67h] directory", self_task, 0x67, 0x41);
            obs("A child alive", K_ISTASK(ct) != 0);
        }
    }
    ptr(0x3F);
    child_report("A", 0, h);
    trace_report("A");
    obs("A IsTask(child) at the end", K_ISTASK(ct));
    obs("A GetNumTasks at the end", K_GETNUMTASKS());
    obs("A GetModuleHandle(TK15A) at the end", GetModuleHandle("TK15A") != 0);
    obs("A GetModuleHandle(TKDLL) at the end", GetModuleHandle("TKDLL") != 0);
    obs("A GetModuleHandle(TKDLB) at the end", GetModuleHandle("TKDLB") != 0);
    dll = (WORD)GetModuleHandle("TKID");
    obs("A child's LoadLibrary result > 32", sh.v[0][V_TKID] > 32);
    obs("A GetModuleHandle(TKID) at the end", dll != 0);
    if (dll) {
        obs("A GetModuleUsage(TKID)", GetModuleUsage((HINSTANCE)dll));
        FreeLibrary((HINSTANCE)dll);
    }
    obs("A child's GlobalAlloc block: GlobalSize",
        (WORD)GlobalSize((HGLOBAL)sh.v[0][V_GBLOCK]));
    obs("A child's GlobalAlloc block: GlobalFlags",
        GlobalFlags((HGLOBAL)sh.v[0][V_GBLOCK]));
    obs("A child's DGROUP: GlobalSize", (WORD)GlobalSize((HGLOBAL)h));
    obs("A child's directory kept", sh.v[0][V_CWD2OK]);
    cwd(now);
    obs("A own directory unchanged at the end", str_eq(now, before));
    obs("A events of TK15 at the end", tdbw(self_task, 6));
    dos(r, 0x3A00, "C:\\TK15DIR");
}

/* mode B twice, started by WinExec: a second instance */
static void instances(void)
{
    WORD h1, h2, i, l, mod;
    WORD FAR *a = sh.v[0], FAR *b = sh.v[1];

    STEP("WinExec TK15A twice");
    reset();
    ptr(1);
    h1 = winexec('B', 0, 7);
    ptr(2);
    obs("B WinExec 1 > 32", h1 > 32);
    h2 = winexec('B', 1, 3);
    ptr(3);
    obs("B WinExec 2 > 32", h2 > 32);
    if (h1 <= 32 || h2 <= 32) {
        sh.go = 1;
        for (i = 0; i < 50; i++)
            K_YIELD();
        return;
    }
    obs("B GetNumTasks", K_GETNUMTASKS());
    mod = K_GETEXEPTR(h1);
    obs("B same module", mod == K_GETEXEPTR(h2));
    obs("B GetModuleUsage", GetModuleUsage((HINSTANCE)mod));
    obs("B instance 2 hPrevInstance==instance 1", b[V_E_SI] == h1);
    obs("B instance 2 InitTask SI==instance 1", b[V_I_SI] == h1);
    obs("B instance 1 hPrevInstance", a[V_E_SI]);
    obs("B same CS", a[V_CS] == b[V_CS]);
    obs("B different DS", a[V_DS] != b[V_DS]);
    obs("B instance 2 GetInstanceData", b[V_GID]);
    obs("B instance 2 saw instance 1's data", b[V_PREV_DATA]);
    obs("B instance 1 WinExec show", a[V_I_DX]);
    obs("B instance 2 WinExec show", b[V_I_DX]);
    obs("B instance 1 GetModuleUsage", a[V_USAGE]);
    obs("B instance 2 GetModuleUsage", b[V_USAGE]);
    obs("B instance 2 TKDLL inits", b[V_DLL_CALLS]);
    obs("B GetModuleUsage(TKDLL)", GetModuleUsage(GetModuleHandle("TKDLL")));
    obs("B GetModuleUsage(TKDLB)", GetModuleUsage(GetModuleHandle("TKDLB")));
    obs("B instance 2 entry ES==PDB", b[V_E_ES] == b[V_PDB]);
    obs("B different PDB", a[V_PDB] != b[V_PDB]);
    l = (WORD)LoadLibrary(helper);
    obs("B LoadLibrary(TK15A.EXE) while running", l);
    if (l > 32) {
        obs("B LoadLibrary(TK15A.EXE)==instance 1", l == h1);
        obs("B GetModuleUsage after LoadLibrary", GetModuleUsage((HINSTANCE)mod));
        FreeLibrary((HINSTANCE)l);
    }
    l = K_LOADMODULE(helper, (void FAR *)-1L);
    obs("B LoadModule(TK15A.EXE, -1) while running", l);
    task_list("B");
    sh.go = 1;
    for (i = 0; i < 50 && (K_ISTASK(a[V_HTASK]) || K_ISTASK(b[V_HTASK])); i++) {
        ptr(0x10 + i);
        K_YIELD();
    }
    ptr(0x3F);
    child_report("B1", 0, h1);
    child_report("B2", 1, h2);
    trace_report("B");
    obs("B GetModuleHandle(TK15A) at the end", GetModuleHandle("TK15A") != 0);
    obs("B GetNumTasks at the end", K_GETNUMTASKS());
}

/* mode C: WaitEvent, PostEvent, DirectedYield */
static void events(void)
{
    WORD h, ct, i;

    STEP("events");
    reset();
    ptr(1);
    h = loadmodule('C', 2);
    ptr(2);
    obs("C LoadModule > 32", h > 32);
    if (h <= 32)
        return;
    ct = sh.v[2][V_HTASK];
    obs("C child events after LoadModule", tdbw(ct, 6));
    K_YIELD();
    ptr(3);
    obs("C child events after Yield", tdbw(ct, 6));
    obs("C PostEvent(child)==child", K_POSTEVENT(ct) == ct);
    obs("C child events after PostEvent", tdbw(ct, 6));
    ptr(4);
    K_YIELD();
    ptr(5);
    obs("C child events after second Yield", tdbw(ct, 6));
    K_DIRECTEDYIELD(ct);
    ptr(6);
    obs("C TDB [AAh] after DirectedYield", tdbw(self_task, 0xAA));
    K_POSTEVENT(ct);
    K_POSTEVENT(ct);
    obs("C child events after two PostEvents", tdbw(ct, 6));
    K_DIRECTEDYIELD(ct);
    ptr(7);
    obs("C child alive after DirectedYield", K_ISTASK(ct) != 0);
    if (K_ISTASK(ct))
        obs("C child events after DirectedYield", tdbw(ct, 6));
    for (i = 0; i < 20 && K_ISTASK(ct); i++) {
        ptr(0x10 + i);
        K_POSTEVENT(ct);
        K_OLDYIELD();
    }
    ptr(0x3F);
    child_report("C", 2, h);
    trace_report("C");
}

/* failures */
static void errors(void)
{
    static WORD show[2] = { 2, 1 };
    static char tail[] = "\0\r";
    static struct {
        WORD env;
        char FAR *cmd;
        WORD FAR *show;
        DWORD reserved;
    } p;
    WORD r;

    STEP("errors");
    p.env = 0;
    p.cmd = tail;
    p.show = show;
    p.reserved = 0;
    obs("LoadModule(C:\\NOSUCH.EXE)", K_LOADMODULE("C:\\NOSUCH.EXE", &p));
    obs("WinExec(NOSUCH.EXE)", K_WINEXEC("NOSUCH.EXE", 1));
    obs("WinExec(C:\\NODIR\\NOSUCH.EXE)", K_WINEXEC("C:\\NODIR\\NOSUCH.EXE", 1));
    r = (WORD)LoadLibrary(helper);
    obs("LoadLibrary(TK15A.EXE)", r);
    if (r > 32)
        FreeLibrary((HINSTANCE)r);
    obs("GetModuleHandle(TK15A) after", GetModuleHandle("TK15A") != 0);
    r = K_LOADMODULE(helper, (void FAR *)-1L);
    obs("LoadModule(TK15A.EXE, -1)", r);
    obs("GetModuleHandle(TK15A) after LoadModule -1", GetModuleHandle("TK15A") != 0);
}

int __cdecl tmain(void)
{
    WORD n, i;

    out("TK15 start\r\n");
    n = GetModuleFileName((HINSTANCE)hinst(), helper, sizeof helper - 12);
    for (i = n; i && helper[i - 1] != '\\'; i--)
        ;
    lstrcpy(helper + i, "TK15A.EXE");
    self();
    thunks();
    errors();
    child_a();
    instances();
    events();
    obs("GetNumTasks at the end", K_GETNUMTASKS());
    out("TK15 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
