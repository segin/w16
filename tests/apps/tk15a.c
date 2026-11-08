/*
 * tk15a.c - TK15A.EXE, the helper task TK15 starts (tests/apps/tk15.c).
 * It imports TKDLL.DLL (which imports TKDLB.DLL), so their
 * initialisation shows how implicit DLLs of a program started later are
 * initialised. Everything it sees goes to TK15's DGROUP (tk15.h); it has
 * no log of its own.
 */
#include <windows.h>
#define TK15_HELPER
#include "tk15.h"

extern WORD __cdecl entry_regs[11];   /* ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern int __cdecl dos_int21(WORD FAR *r);

extern WORD FAR PASCAL DLL_INFO(WORD i);        /* TKDLL.DLL */
extern WORD FAR PASCAL DLB_INFO(WORD i);        /* TKDLB.DLL */

static WORD pre[8];             /* what pre_init saw */
static tk15sh FAR *sh;
static WORD FAR *v;
static WORD slot, ptask;
static WORD prev_data = 0x5A5A; /* GetInstanceData target */

static void tr(WORD n)
{
    if (sh->ntrace < TK15_TRACE)
        sh->trace[sh->ntrace++] = TR_C(slot, n);
}

/* INT 21h with AX = ax and DS:DX = dsdx (DS:SI = dsdx for 47h); r[] out */
static int dos(WORD *r, WORD ax, const void FAR *dsdx)
{
    WORD i;
    for (i = 0; i < 8; i++)
        r[i] = 0;
    r[0] = ax;
    r[4] = (WORD)(DWORD)dsdx;
    if ((ax >> 8) != 0x47)              /* 47h: DL = drive (0: current) */
        r[3] = r[4];
    r[6] = (WORD)((DWORD)dsdx >> 16);
    return dos_int21((WORD FAR *)r);
}

/* before InitTask (tk15s.asm): DS is ours, the stack is the task's */
void __cdecl pre_init(void)
{
    DWORD t = K_GETCURRENTTASK();
    pre[0] = (WORD)t;
    pre[1] = tdbw((WORD)t, 6);
    pre[2] = DLL_INFO(25);
    pre[3] = DLB_INFO(25);
    pre[4] = cur_ss();
    pre[5] = cur_sp();
}

static void observe(void)
{
    WORD t = pre[0], i, r[8], env, penv, n;
    const char FAR *cmd = MAKELP(init_regs[6], init_regs[1]);
    const char FAR *a, FAR *b;
    FARPROC p;

    v[V_HTASK] = t;
    v[V_EV_ENTRY] = pre[1];
    v[V_PEV_ENTRY] = tdbw(ptask, 6);
    v[V_DLL_PRE] = pre[2];
    v[V_DLB_PRE] = pre[3];
    v[V_SS_PRE] = pre[4];
    v[V_SP_PRE] = pre[5];
    for (i = 0; i < 11; i++)
        v[V_E_AX + i] = entry_regs[i];
    for (i = 0; i < 7; i++)
        v[V_I_AX + i] = init_regs[i];
    v[V_DLL_CALLS] = DLL_INFO(25);
    v[V_DLB_CALLS] = DLB_INFO(25);
    v[V_DLL_SS] = DLL_INFO(10);
    v[V_DLL_SP] = DLL_INFO(7);
    v[V_DLB_SS] = DLB_INFO(10);
    v[V_DLB_SP] = DLB_INFO(7);
    v[V_DLL_DI] = DLL_INFO(5);
    v[V_DLL_ES] = DLL_INFO(9);
    v[V_DLL_CMDLEN] = DLL_INFO(24);
    v[V_DLL_SAWDLB] = DLL_INFO(28);
    v[V_DLL_DSNOW] = DLL_INFO(26);

    v[V_T00] = tdbw(t, 0);
    v[V_T02] = tdbw(t, 2);
    v[V_T04] = tdbw(t, 4);
    v[V_T06] = tdbw(t, 6);
    v[V_T08] = tdbw(t, 8);
    v[V_T0C] = tdbw(t, 0x0C);
    v[V_T1A] = tdbw(t, 0x1A);
    v[V_T1C] = tdbw(t, 0x1C);
    v[V_T1E] = tdbw(t, 0x1E);
    v[V_T20] = tdbw(t, 0x20);
    v[V_T22] = tdbw(t, 0x22);
    v[V_T60] = tdbw(t, 0x60);
    v[V_T62] = tdbw(t, 0x62);
    v[V_T64] = tdbw(t, 0x64);
    v[V_T66] = tdbb(t, 0x66);
    v[V_TAA] = tdbw(t, 0xAA);
    v[V_TAC] = tdbw(t, 0xAC);
    v[V_TAE] = tdbw(t, 0xAE);
    v[V_TB0] = tdbw(t, 0xB0);
    v[V_TB2] = tdbw(t, 0xB2);
    v[V_TB4] = tdbw(t, 0xB4);
    v[V_TB8] = tdbw(t, 0xB8);
    v[V_TFA] = tdbw(t, 0xFA);

    {
        DWORD d = K_GETCURRENTPDB();
        v[V_PDB] = (WORD)d;
        v[V_PDB_DX] = (WORD)(d >> 16);
    }
    dos(r, 0x6200, NULL);
    v[V_I21_62] = r[1];
    dos(r, 0x5100, NULL);
    v[V_I21_51] = r[1];
    v[V_PSP16] = tdbw(v[V_PDB], 0x16);
    env = v[V_PSP2C] = tdbw(v[V_PDB], 0x2C);
    v[V_PSP80] = tdbw(v[V_PDB], 0x80);
    for (i = 0; i < 8; i++)
        v[V_TAIL + i] = tdbw(v[V_PDB], 0x81 + 2 * i);
    v[V_ENV] = (WORD)(K_GETDOSENVIRONMENT() >> 16);
    penv = tdbw(tdbw(ptask, 0x60), 0x2C);
    v[V_ENV_SAMESEL] = env == penv;
    /* the same text: the strings up to the double NUL */
    a = MAKELP(env, 0);
    b = MAKELP(penv, 0);
    for (n = 0; n < 4096 && !(a[n] == 0 && a[n + 1] == 0) && a[n] == b[n]; n++)
        ;
    v[V_ENV_SAMETEXT] = n < 4096 && a[n] == 0 && a[n + 1] == 0 &&
                        b[n] == 0 && b[n + 1] == 0;

    v[V_NTASKS] = K_GETNUMTASKS();
    v[V_HMOD] = GetModuleHandle("TK15A");
    v[V_USAGE] = GetModuleUsage((HINSTANCE)v[V_HMOD]);
    v[V_CS] = cur_cs();
    v[V_DS] = cur_ds();
    v[V_DGSIZE] = (WORD)GlobalSize((HGLOBAL)(cur_ds() & ~1u));
    v[V_PHEAP] = *(WORD __near *)6;
    p = MakeProcInstance((FARPROC)observe, (HINSTANCE)(cur_ds() & ~1u));
    v[V_MPI_SEG] = (WORD)((DWORD)p >> 16);
    v[V_MPI_OFF] = (WORD)(DWORD)p;
    FreeProcInstance(p);
    for (i = 0; i < 16 && cmd[i]; i++)
        ((char FAR *)&v[V_CMD])[i] = cmd[i];
    if (init_regs[4]) {         /* hPrevInstance: its prev_data */
        v[V_GID] = GetInstanceData((HINSTANCE)init_regs[4], (BYTE __near *)(WORD)(DWORD)(void FAR *)&prev_data, 2);
        v[V_PREV_DATA] = prev_data;
    }
    prev_data = 0xA5A5;
}

static void mode_a(void)
{
    WORD r[8];
    char buf[70];
    static const char dir[] = "C:\\TK15DIR";
    HGLOBAL g;

    v[V_WAITRET] = K_WAITEVENT(0);
    tr(2);
    v[V_EV_A1] = tdbw(pre[0], 6);
    K_YIELD();
    tr(3);
    /* own directory, then let TK15 look at its own */
    dos(r, 0x3B00, dir);
    g = GlobalAlloc(GMEM_MOVEABLE, 100);
    v[V_GBLOCK] = (WORD)g;
    v[V_TKID] = (WORD)LoadLibrary("TKID.TPL");
    K_YIELD();
    tr(4);
    /* still in C:\TK15DIR? */
    dos(r, 0x4700, buf);
    v[V_CWD2OK] = str_eq(buf, "TK15DIR");
    v[V_EV_A2] = tdbw(pre[0], 6);
}

static void mode_b(void)
{
    WORD i;
    tr(2);
    for (i = 0; i < 400 && !sh->go; i++)
        K_YIELD();
    tr(3);
}

static void mode_c(void)
{
    K_WAITEVENT(0);
    tr(2);
    v[V_EV_A1] = tdbw(pre[0], 6);
    K_WAITEVENT(0);
    tr(3);
    v[V_EV_A2] = tdbw(pre[0], 6);
    K_WAITEVENT(0);
    tr(4);
}

int __cdecl tmain(void)
{
    const char FAR *p = MAKELP(init_regs[6], init_regs[1]);
    char mode;
    WORD ds, off;

    while (*p == ' ')
        p++;
    mode = *p++;
    slot = hexw(&p);
    ds = hexw(&p);
    off = hexw(&p);
    ptask = hexw(&p);
    if (slot >= TK15_SLOTS)
        return 1;
    sh = MAKELP(ds, off);
    v = sh->v[slot];
    tr(1);
    observe();
    switch (mode) {
    case 'A': mode_a(); break;
    case 'B': mode_b(); break;
    case 'C': mode_c(); break;
    }
    tr(0x0F);
    v[V_DONE] = 1;
    return 0x42;
}
