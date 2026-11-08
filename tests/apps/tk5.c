/*
 * tk5.c - DLL loading probe: implicitly loaded DLLs (TKDLL.DLL, which
 * imports TKDLB.DLL), their initialisation, LoadLibrary / FreeLibrary /
 * LoadModule / FreeModule / GetModuleUsage, the LoadLibrary search order,
 * failure codes, GetInstanceData, MakeProcInstance / FreeProcInstance and
 * the prologues of exported procedures.
 *
 * Needs TKID.TPL next to TK5.EXE and TKDLL.DLL, TKDLB.DLL, TKBAD.DLL,
 * TKNOD.DLL, TKNODEP.DLL where LoadLibrary finds them (tests/tk5ref.sh
 * and tests/run-tests.sh put them in the Windows directory); for the PATH
 * part of the search order, PATH must contain C:\TKPATH. The probe makes
 * and removes C:\TKCWD and C:\TKPATH and the files it writes.
 *
 * Output and checks: see tklog.h; the reference values are in tk5ref.h.
 * The exit code is the number of failures. Calls that might hang or fault
 * on Windows come last.
 */
#include <windows.h>

#define TK_REF "tk5ref.h"
#include "tklog.h"

extern int __cdecl dos_int21(WORD FAR *r);

/* TKDLL.DLL */
extern WORD FAR PASCAL DLL_INFO(WORD i);
extern WORD FAR PASCAL DLL_ADD(WORD a, WORD b);         /* by name */
extern WORD FAR PASCAL DLL_SUB(WORD a, WORD b);         /* by ordinal */
extern DWORD FAR PASCAL DLL_MPI(void);
extern DWORD FAR PASCAL DLL_SELFADDR(void);
extern WORD FAR PASCAL DLL_LALLOC(WORD n);
extern WORD FAR PASCAL DLL_PEEK(WORD off);
/* our own exports (prolog.asm) */
extern DWORD FAR PASCAL ASM_PRO_MOV(void);
extern DWORD FAR PASCAL ASM_PRO_PUSH(void);

typedef WORD (FAR PASCAL *infofn)(WORD);
typedef WORD (FAR PASCAL *wordfn)(void);
typedef DWORD (FAR PASCAL *dwordfn)(void);
typedef void (FAR PASCAL *setwepfn)(WORD FAR *);

/* progress, so a run that hangs shows where */
#define STEP(s) out("-- " s "\r\n")

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static WORD sel_of(const void FAR *p)
{
    return (WORD)((DWORD)p >> 16);
}

static WORD off_of(const void FAR *p)
{
    return (WORD)(DWORD)p;
}

extern DWORD __cdecl call_ds(WORD ds, FARPROC fn, WORD nargs, WORD a1, WORD a2, WORD a3);

static WORD thunk_word(FARPROC p)
{
    return *(WORD FAR *)((BYTE FAR *)p + 1);
}

static BYTE byte_at(FARPROC p, WORD i)
{
    return ((BYTE FAR *)p)[i];
}

/* ---- DOS helpers ---- */

static int dos(WORD ax, WORD bx, WORD cx, const void FAR *dsdx, WORD *ax_out)
{
    WORD r[8];
    int c;
    r[0] = ax;
    r[1] = bx;
    r[2] = cx;
    r[3] = off_of(dsdx);
    r[4] = r[5] = 0;
    r[6] = sel_of(dsdx);
    r[7] = 0;
    c = dos_int21(r);
    if (ax_out)
        *ax_out = r[0];
    return !c;
}

static void scopy(char *d, const char FAR *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

static void scat(char *d, const char FAR *s)
{
    while (*d)
        d++;
    scopy(d, s);
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static int seq(const char FAR *a, const char FAR *b)
{
    while (*a && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return upper(*a) == upper(*b);
}

/* dir without a trailing backslash (except "C:\") */
static void trim(char *d)
{
    WORD n = 0;
    while (d[n])
        n++;
    if (n > 3 && d[n - 1] == '\\')
        d[n - 1] = 0;
}

static void path_of(char *out, const char *dir, const char *file)
{
    scopy(out, dir);
    if (out[0] && out[lstrlen(out) - 1] != '\\')
        scat(out, "\\");
    scat(out, file);
}

static int write_file(const char *name, const void FAR *buf, WORD n)
{
    WORD h, w;
    if (!dos(0x3C00, 0, 0, name, &h))
        return 0;
    dos(0x4000, h, n, buf, &w);
    dos(0x3E00, h, 0, 0, NULL);
    return w == n;
}

static void del_file(const char *name)
{
    dos(0x4100, 0, 0, name, NULL);
}

/* ---- implicitly loaded DLLs ---- */

static HMODULE hdll, hdlb;
static WORD dll_ds, dlb_ds;

static void probe_implicit(void)
{
    infofn dlb_info;
    FARPROC p;
    WORD i;

    hdll = GetModuleHandle("TKDLL");
    hdlb = GetModuleHandle("TKDLB");
    obs("TKDLL loaded", hdll != 0);
    obs("TKDLB loaded", hdlb != 0);
    obs("hModule(TKDLL) & 1", (WORD)hdll & 1);
    obs("[hModule(TKDLL):0]", hdll ? *w_at((WORD)hdll, 0) : 0);
    dll_ds = DLL_INFO(26);
    obs_env("TKDLL DS", dll_ds);
    obs("TKDLL DS != TK5 DS", dll_ds != entry_regs[E_DS]);
    obs("TKDLL DS & 1", dll_ds & 1);

    /* the registers its entry point got */
    obs("TKDLL init calls", DLL_INFO(25));
    obs("TKDLL init AX", DLL_INFO(0));
    obs_env("TKDLL init BX", DLL_INFO(1));
    obs("TKDLL init CX(heap)", DLL_INFO(2));
    obs_env("TKDLL init DX", DLL_INFO(3));
    obs("TKDLL init DX==DS", DLL_INFO(3) == dll_ds);
    obs("TKDLL init SI", DLL_INFO(4));
    obs_env("TKDLL init DI", DLL_INFO(5));
    obs_env("TKDLL init BP", DLL_INFO(6));
    obs_env("TKDLL init SP", DLL_INFO(7));
    obs_env("TKDLL init ES", DLL_INFO(9));
    obs_env("TKDLL init SS", DLL_INFO(10));
    obs("TKDLL init DS==DGROUP", DLL_INFO(8) == dll_ds);
    obs("TKDLL init DI==DS&~1", DLL_INFO(5) == (dll_ds & ~1u));
    obs("TKDLL init SS==TK5 SS", DLL_INFO(10) == entry_regs[10]);
    obs("TKDLL init SS==TKDLL DS", DLL_INFO(10) == dll_ds);
    obs("TKDLL init ES==TK5 PSP", DLL_INFO(9) == entry_regs[9]);
    obs("TKDLL init ES==0", DLL_INFO(9) == 0);
    obs("TKDLL init ES:SI bytes", DLL_INFO(24));
    for (i = 0; i < 4; i++)
        obsn("TKDLL init ES:SI word", i, DLL_INFO(20 + i));
    for (i = 0; i < 8; i++)
        obsn("TKDLL init inst", 2 * i, DLL_INFO(12 + i));
    obs("TKDLL pHeap now != 0", DLL_INFO(27) != 0);
    obs("TKDLL LocalAlloc in init != 0", DLL_INFO(30) != 0);
    obs_env("TKDLL DS limit", DLL_INFO(31));
    obs_env("TKDLL GlobalSize(DGROUP)", (WORD)GlobalSize(dll_ds & ~1u));

    /* TKDLB, imported by TKDLL */
    obs("TKDLB initialised before TKDLL", DLL_INFO(28));
    obs("TKDLB DLB_ID through TKDLL", DLL_INFO(29));
    dlb_info = (infofn)GetProcAddress(hdlb, "DLB_INFO");
    obs("GetProcAddress(TKDLB, DLB_INFO) != 0", dlb_info != NULL);
    if (dlb_info) {
        dlb_ds = dlb_info(26);
        obs("TKDLB init calls", dlb_info(25));
        obs("TKDLB init CX(heap)", dlb_info(2));
        obs("TKDLB init DI==DS&~1", dlb_info(5) == (dlb_ds & ~1u));
        obs("TKDLB init SS==TKDLL init SS", dlb_info(10) == DLL_INFO(10));
        obs_env("TKDLB init SP", dlb_info(7));
        obs("TKDLB init SP < TKDLL init SP", dlb_info(7) < DLL_INFO(7));
    }

    /* usage counts */
    obs("GetModuleUsage(TKDLL)", GetModuleUsage((HINSTANCE)hdll));
    obs("GetModuleUsage(TKDLB)", GetModuleUsage((HINSTANCE)hdlb));
    obs("GetModuleUsage(TKDLL hInstance)", GetModuleUsage((HINSTANCE)(dll_ds & ~1u)));
    obs("GetModuleUsage(TK5)", GetModuleUsage((HINSTANCE)hinst()));
    obs_env("GetModuleUsage(KERNEL)", GetModuleUsage(GetModuleHandle("KERNEL")));

    /* imports, exports */
    obs("DLL_ADD(2,3) (by name)", DLL_ADD(2, 3));
    obs("DLL_SUB(5,3) (by ordinal)", DLL_SUB(5, 3));
    p = GetProcAddress(hdll, "DLL_ADD");
    obs("GetProcAddress(DLL_ADD) (non-resident name)==import", p == (FARPROC)DLL_ADD);
    obs("GetProcAddress(dll_add)==import", GetProcAddress(hdll, "dll_add") == (FARPROC)DLL_ADD);
    obs("GetProcAddress(TKDLL, 2)==import",
        GetProcAddress(hdll, (LPCSTR)MAKELP(0, 2)) == (FARPROC)DLL_ADD);
    obs("GetProcAddress(DLL_INFO) (resident)==import",
        GetProcAddress(hdll, "DLL_INFO") == (FARPROC)DLL_INFO);
    obs("GetProcAddress(TKDLL hInstance, DLL_ADD)==import",
        GetProcAddress((HINSTANCE)(dll_ds & ~1u), "DLL_ADD") == (FARPROC)DLL_ADD);
    obs("GetProcAddress(WEP)!=0", GetProcAddress(hdll, "WEP") != NULL);
    obs("GetProcAddress(TKDLL, 10)", GetProcAddress(hdll, (LPCSTR)MAKELP(0, 10)) != NULL);
    obs("GetProcAddress(TKDLL, TKDLL)", GetProcAddress(hdll, "TKDLL") != NULL);
    obs("GetProcAddress(TKDLL, NOSUCH)", GetProcAddress(hdll, "NOSUCH") != NULL);
    obs("DLL_SELFADDR()==import", DLL_SELFADDR() == (DWORD)(FARPROC)DLL_ADD);
}

/* ---- prologues of exported procedures ---- */

static void probe_prologues(void)
{
    FARPROC p;
    DWORD r;

    p = GetProcAddress(hdll, "ASM_PRO_MOV");
    obs("TKDLL ASM_PRO_MOV found", p != NULL);
    if (p) {
        obs("TKDLL ASM_PRO_MOV byte 0", byte_at(p, 0));
        obs("TKDLL ASM_PRO_MOV byte 1", byte_at(p, 1));
        obs("TKDLL ASM_PRO_MOV byte 2", byte_at(p, 2));
        obs("TKDLL ASM_PRO_MOV word 1==DS", *(WORD FAR *)((BYTE FAR *)p + 1) == dll_ds);
        r = ((dwordfn)p)();
        obs("TKDLL ASM_PRO_MOV() AX==TKDLL DS", (WORD)r == dll_ds);
        obs("TKDLL ASM_PRO_MOV() AX==TK5 DS", (WORD)r == entry_regs[E_DS]);
    }
    p = GetProcAddress(hdll, "ASM_PRO_PUSH");
    obs("TKDLL ASM_PRO_PUSH found", p != NULL);
    if (p) {
        obs("TKDLL ASM_PRO_PUSH byte 0", byte_at(p, 0));
        obs("TKDLL ASM_PRO_PUSH byte 1", byte_at(p, 1));
        obs("TKDLL ASM_PRO_PUSH byte 2", byte_at(p, 2));
        obs("TKDLL ASM_PRO_PUSH word 1==DS", *(WORD FAR *)((BYTE FAR *)p + 1) == dll_ds);
        r = ((dwordfn)p)();
        obs("TKDLL ASM_PRO_PUSH() AX==TKDLL DS", (WORD)r == dll_ds);
    }
    p = (FARPROC)ASM_PRO_MOV;
    obs("TK5 ASM_PRO_MOV byte 0", byte_at(p, 0));
    obs("TK5 ASM_PRO_MOV byte 1", byte_at(p, 1));
    obs("TK5 ASM_PRO_MOV byte 2", byte_at(p, 2));
    {
        FARPROC g = GetProcAddress((HINSTANCE)hinst(), "ASM_PRO_MOV");
        obs("TK5 GetProcAddress(ASM_PRO_MOV)==fn", g == p);
        obs("TK5 GetProcAddress(ASM_PRO_MOV)==ordinal 1",
            g == GetProcAddress((HINSTANCE)hinst(), (LPCSTR)MAKELP(0, 1)));
        obs("TK5 GetProcAddress(ASM_PRO_MOV) off==fn off", off_of(g) == off_of(p));
        obs("TK5 GetProcAddress(ASM_PRO_MOV) sel==fn sel", sel_of(g) == sel_of(p));
        obs_env("TK5 GetProcAddress(ASM_PRO_MOV) sel", sel_of(g));
        obs_env("TK5 GetProcAddress(ASM_PRO_MOV) off", off_of(g));
        obs_env("TK5 ASM_PRO_MOV sel", sel_of(p));
        g = GetProcAddress(GetModuleHandle("TK5"), "ASM_PRO_MOV");
        obs("TK5 GetProcAddress(hModule, ASM_PRO_MOV)==fn", g == p);
        obs("TK5 GetProcAddress(hModule, ASM_PRO_MOV)!=0", g != NULL);
        g = GetProcAddress(GetModuleHandle("TK5"), (LPCSTR)MAKELP(0, 2));
        obs("TK5 GetProcAddress(hModule, 2)==ASM_PRO_PUSH", g == (FARPROC)ASM_PRO_PUSH);
    }
    p = (FARPROC)ASM_PRO_PUSH;
    obs("TK5 ASM_PRO_PUSH byte 0", byte_at(p, 0));
    obs("TK5 ASM_PRO_PUSH byte 1", byte_at(p, 1));
    obs("TK5 ASM_PRO_PUSH byte 2", byte_at(p, 2));
}

/* ---- MakeProcInstance ---- */

static void probe_mpi(void)
{
    FARPROC fn = (FARPROC)ASM_PRO_MOV, fn2 = (FARPROC)ASM_PRO_PUSH;
    FARPROC p, p2, p3, p4, q;
    DWORD r;

    p = MakeProcInstance(fn, (HINSTANCE)hinst());
    obs("MPI(app fn, hInstance)!=0", p != NULL);
    obs("MPI(app fn, hInstance)==fn", p == fn);
    obs_env("MPI(app fn) sel", sel_of(p));
    obs_env("MPI(app fn) off", off_of(p));
    if (p && p != fn) {
        obs("thunk byte 0", byte_at(p, 0));
        obs("thunk word 1==DS", *(WORD FAR *)((BYTE FAR *)p + 1) == entry_regs[E_DS]);
        obs("thunk word 1==hInstance", *(WORD FAR *)((BYTE FAR *)p + 1) == hinst());
        obs("thunk byte 3", byte_at(p, 3));
        obs("thunk far ptr==fn", *(FARPROC FAR *)((BYTE FAR *)p + 4) == fn);
        obs("thunk sel==fn sel", sel_of(p) == sel_of(fn));
        obs("thunk sel & 3", sel_of(p) & 3);
        dump("thunk", sel_of(p), off_of(p), 4);
        obs("GlobalHandle(thunk sel)!=0", (WORD)GlobalHandle(sel_of(p)) != 0);
        r = ((dwordfn)p)();
        obs("thunk() AX==DS", (WORD)r == entry_regs[E_DS]);
        obs("thunk() DX", (WORD)(r >> 16));
    }
    p2 = MakeProcInstance(fn, (HINSTANCE)hinst());
    obs("MPI again==first", p2 == p);
    obs("MPI again sel==first", sel_of(p2) == sel_of(p));
    obs("MPI again off-first", off_of(p2) - off_of(p));
    p3 = MakeProcInstance(fn2, (HINSTANCE)hinst());
    obs("MPI(fn2) off-first", off_of(p3) - off_of(p));
    obs("MPI(fn2) sel==first", sel_of(p3) == sel_of(p));
    FreeProcInstance(p);
    p4 = MakeProcInstance(fn2, (HINSTANCE)hinst());
    obs("MPI after FreeProcInstance(first) off-first", off_of(p4) - off_of(p));
    FreeProcInstance(p2);
    FreeProcInstance(p3);
    FreeProcInstance(p4);
    p = MakeProcInstance(fn, (HINSTANCE)(hinst() | 1));
    obs("MPI(fn, DS) word 1==DS", p != fn && thunk_word(p) == entry_regs[E_DS]);
    FreeProcInstance(p);
    p = MakeProcInstance(fn, (HINSTANCE)(dll_ds & ~1u));
    obs("MPI(app fn, DLL hInstance)==fn", p == fn);
    if (p != fn) {
        obs("MPI(app fn, DLL hInstance) word 1==DS", thunk_word(p) == entry_regs[E_DS]);
        obs("MPI(app fn, DLL hInstance) word 1==DLL DS", thunk_word(p) == dll_ds);
        FreeProcInstance(p);
    }
    p = MakeProcInstance(fn, (HINSTANCE)0);
    obs("MPI(app fn, 0)==fn", p == fn);
    if (p && p != fn) {
        obs("MPI(app fn, 0) word 1==DS", thunk_word(p) == entry_regs[E_DS]);
        FreeProcInstance(p);
    }
    /* called with DS = TKDLL's DGROUP, as from code in the DLL */
    p = (FARPROC)call_ds(dll_ds, (FARPROC)MakeProcInstance, 3, sel_of(fn), off_of(fn), hinst());
    obs("MPI(app fn, hInstance) with DLL DS==fn", p == fn);
    if (p && p != fn) {
        obs("MPI with DLL DS word 1==DS", thunk_word(p) == entry_regs[E_DS]);
        obs("MPI with DLL DS word 1==DLL DS", thunk_word(p) == dll_ds);
        FreeProcInstance(p);
    }
    q = GetProcAddress(hdll, "DLL_ADD");
    p = MakeProcInstance(q, (HINSTANCE)hinst());
    obs("MPI(DLL fn, app hInstance)==fn", p == q);
    if (p != q)
        FreeProcInstance(p);
    p = MakeProcInstance(q, (HINSTANCE)(dll_ds & ~1u));
    obs("MPI(DLL fn, DLL hInstance)==fn", p == q);
    if (p != q) {
        obs("MPI(DLL fn, DLL hInstance) word 1==DS", thunk_word(p) == entry_regs[E_DS]);
        obs("MPI(DLL fn, DLL hInstance) word 1==DLL DS", thunk_word(p) == dll_ds);
        FreeProcInstance(p);
    }
    p = (FARPROC)call_ds(dll_ds, (FARPROC)MakeProcInstance, 3, sel_of(q), off_of(q), dll_ds & ~1u);
    obs("MPI(DLL fn, DLL hInstance) with DLL DS==fn", p == q);
    if (p != q)
        FreeProcInstance(p);
    obs("DLL_MPI()==DLL_SELFADDR()", DLL_MPI() == DLL_SELFADDR());
}

/* ---- GetInstanceData ---- */

static WORD gid_word = 0x1357;

static void probe_instancedata(void)
{
    WORD v = gid_word, want = DLL_PEEK((WORD)&gid_word), n;
    n = GetInstanceData((HINSTANCE)(dll_ds & ~1u), (BYTE NEAR *)&gid_word, 2);
    obs("GetInstanceData(TKDLL, 2) ret", n);
    obs("GetInstanceData(TKDLL) copied", gid_word == want);
    obs_env("GetInstanceData(TKDLL) word", gid_word);
    obs_env("GetInstanceData(TKDLL) TKDLL word", want);
    obs("GetInstanceData(TKDLL) unchanged", gid_word == v);
    gid_word = 0x2468;
    n = GetInstanceData((HINSTANCE)hinst(), (BYTE NEAR *)&gid_word, 2);
    obs("GetInstanceData(self) ret", n);
    obs("GetInstanceData(self) word", gid_word);
    gid_word = v;
}

/* ---- LoadLibrary and friends ---- */

static void obs_load(const char *name, HINSTANCE h)
{
    obs(name, (WORD)h < 32 ? (WORD)h : 0x20);
}

static void probe_loadlibrary(void)
{
    HINSTANCE h, h2;
    WORD dlb_inst = dlb_ds & ~1u;
    struct { WORD env; LPSTR cmd; WORD FAR *show; DWORD res; } pb;
    WORD show[2];
    static char nul[] = "";

    h = LoadLibrary("TKDLB.DLL");
    obs_load("LoadLibrary(TKDLB.DLL)", h);
    obs("LoadLibrary(TKDLB.DLL)==hInstance", (WORD)h == dlb_inst);
    obs("GetModuleUsage(TKDLB) after LoadLibrary", GetModuleUsage((HINSTANCE)hdlb));
    h2 = LoadLibrary("tkdlb.dll");
    obs("LoadLibrary(tkdlb.dll)==hInstance", (WORD)h2 == dlb_inst);
    obs("GetModuleUsage(TKDLB) after 2nd LoadLibrary", GetModuleUsage((HINSTANCE)hdlb));
    FreeLibrary(h2);
    obs("GetModuleUsage(TKDLB) after FreeLibrary", GetModuleUsage((HINSTANCE)hdlb));
    FreeLibrary(h);
    obs("GetModuleUsage(TKDLB) after 2nd FreeLibrary", GetModuleUsage((HINSTANCE)hdlb));
    h = LoadLibrary("TKDLB");
    obs_load("LoadLibrary(TKDLB)", h);
    obs("LoadLibrary(TKDLB)==hInstance", (WORD)h == dlb_inst);
    if ((WORD)h >= 32)
        FreeLibrary(h);
    h = LoadLibrary("C:\\TKNODIR\\TKDLB.DLL");
    obs_load("LoadLibrary(C:\\TKNODIR\\TKDLB.DLL)", h);
    obs("LoadLibrary(C:\\TKNODIR\\TKDLB.DLL)==hInstance", (WORD)h == dlb_inst);
    if ((WORD)h >= 32)
        FreeLibrary(h);
    obs("GetModuleUsage(TKDLB) at end", GetModuleUsage((HINSTANCE)hdlb));
    h = LoadLibrary((LPCSTR)MAKELP(0, (WORD)hdlb));
    obs_env("LoadLibrary(MAKELP(0,hModule))", (WORD)h);
    obs("LoadLibrary(MAKELP(0,hModule))==hInstance", (WORD)h == dlb_inst);
    obs("GetModuleUsage(TKDLB) after LoadLibrary(MAKELP(0,hModule))",
        GetModuleUsage((HINSTANCE)hdlb));
    if ((WORD)h >= 32)
        FreeLibrary(h);

    STEP("KERNEL");
    h = LoadLibrary("KERNEL");
    obs_load("LoadLibrary(KERNEL)", h);
    if ((WORD)h >= 32) {
        obs("LoadLibrary(KERNEL) odd", (WORD)h & 1);
        obs("LoadLibrary(KERNEL)==hModule", (WORD)h == (WORD)GetModuleHandle("KERNEL"));
        obs_env("LoadLibrary(KERNEL)", (WORD)h);
        obs_env("hModule(KERNEL)", (WORD)GetModuleHandle("KERNEL"));
        FreeLibrary(h);
    }
    STEP("TKNOD");
    h = LoadLibrary("TKNOD");
    obs_load("LoadLibrary(TKNOD) (not loaded; no extension)", h);
    if ((WORD)h >= 32)
        FreeLibrary(h);
    h = LoadLibrary("TKNOD.DLL");
    obs_load("LoadLibrary(TKNOD.DLL)", h);
    if ((WORD)h >= 32) {
        FARPROC g = GetProcAddress(h, "NOD_GET");
        HMODULE m = GetModuleHandle("TKNOD");
        obs("LoadLibrary(TKNOD.DLL)==hModule", (WORD)h == (WORD)m);
        obs("LoadLibrary(TKNOD.DLL)==hModule&~1", (WORD)h == ((WORD)m & ~1u));
        obs("TKNOD NOD_GET()", g ? ((wordfn)g)() : 0);
        obs("GetModuleUsage(TKNOD)", GetModuleUsage(h));
        FreeLibrary(h);
        obs("TKNOD unloaded", GetModuleHandle("TKNOD") == 0);
    }

    STEP("missing files");
    obs_load("LoadLibrary(TKNOSUCH.DLL)", LoadLibrary("TKNOSUCH.DLL"));
    obs_load("LoadLibrary(TKNOSUCH)", LoadLibrary("TKNOSUCH"));
    obs_load("LoadLibrary(C:\\TKNODIR\\TKNOSUCH.DLL)", LoadLibrary("C:\\TKNODIR\\TKNOSUCH.DLL"));
    {
        static char empty[4] = { 0, 'x', 'y', 0 };
        obs_load("LoadLibrary(empty)", LoadLibrary(empty));
        obs("LoadLibrary(empty) buf[1]", (BYTE)empty[1]);
    }
    {
        static char txt[] = "this is not an executable\r\n";
        static BYTE mz[64];
        mz[0] = 'M';
        mz[1] = 'Z';
        mz[2] = 64;                     /* bytes in last page */
        mz[4] = 1;                      /* pages */
        mz[8] = 4;                      /* header paragraphs */
        mz[0x18] = 0x40;                /* relocation table offset */
        write_file("C:\\TKTXT.DLL", txt, sizeof txt - 1);
        write_file("C:\\TKMZ.DLL", mz, sizeof mz);
        obs_load("LoadLibrary(text file)", LoadLibrary("C:\\TKTXT.DLL"));
        obs_load("LoadLibrary(MZ without NE)", LoadLibrary("C:\\TKMZ.DLL"));
        del_file("C:\\TKTXT.DLL");
        del_file("C:\\TKMZ.DLL");
    }
    STEP("TKBAD");
    h = LoadLibrary("TKBAD.DLL");
    obs_load("LoadLibrary(TKBAD.DLL) (init fails)", h);
    obs("TKBAD loaded afterwards", GetModuleHandle("TKBAD") != 0);

    /* LoadModule / FreeModule with a DLL */
    show[0] = 2;
    show[1] = SW_SHOWNORMAL;
    pb.env = 0;
    pb.cmd = nul;
    pb.show = show;
    pb.res = 0;
    STEP("LoadModule");
    h = LoadModule("TKDLB.DLL", &pb);
    obs_load("LoadModule(TKDLB.DLL)", h);
    obs("LoadModule(TKDLB.DLL)==hInstance", (WORD)h == dlb_inst);
    obs("GetModuleUsage(TKDLB) after LoadModule", GetModuleUsage((HINSTANCE)hdlb));
    obs_env("FreeModule(TKDLB hInstance)", FreeModule(h));
    obs("GetModuleUsage(TKDLB) after FreeModule", GetModuleUsage((HINSTANCE)hdlb));
    h = LoadLibrary("TKDLB.DLL");
    obs_env("FreeModule(TKDLB hModule)", FreeModule((HINSTANCE)hdlb));
    obs("GetModuleUsage(TKDLB) after FreeModule(hModule)", GetModuleUsage((HINSTANCE)hdlb));
}

/* ---- search order ---- */

#define N_LOC 5                 /* the locations this probe tests */
static const char *loc_name[N_LOC] = { "cwd", "windir", "sysdir", "appdir", "path" };
static char loc_dir[N_LOC][80];
static int loc_use[N_LOC];      /* distinct and usable */
static WORD wep_flag;

static int find_mark(BYTE FAR *b, WORD n)
{
    WORD i;
    for (i = 0; i + 6 <= n; i++)
        if (b[i] == 'T' && b[i + 1] == 'K' && b[i + 2] == 'I' && b[i + 3] == 'D' &&
            b[i + 4] == '#')
            return i + 5;
    return -1;
}

static int seq_prefix(const char FAR *p)
{
    const char *t = "C:\\TKPATH";
    while (*t)
        if (upper(*p++) != *t++)
            return 0;
    return *p == 0 || *p == ';' || *p == '\\';
}

/* current directory of the default drive, without the "C:\" */
static void get_cwd(char *buf)
{
    WORD r[8];
    r[0] = 0x4700;
    r[3] = 0;                           /* DL = 0: default drive */
    r[1] = r[2] = r[5] = r[7] = 0;
    r[4] = off_of((char FAR *)buf);
    r[6] = sel_of((char FAR *)buf);
    if (dos_int21(r))
        buf[0] = 0;
}

static int env_has_tkpath(void)
{
    WORD psp = entry_regs[9];
    WORD envsel = *w_at(psp, 0x2C);
    const char FAR *e = (const char FAR *)MAKELP(envsel, 0);
    while (*e) {
        if (e[0] == 'P' && e[1] == 'A' && e[2] == 'T' && e[3] == 'H' && e[4] == '=') {
            const char FAR *p = e + 5;
            for (; *p; p++)
                if (seq_prefix(p))
                    return 1;
        }
        while (*e)
            e++;
        e++;
    }
    return 0;
}

static void probe_search(void)
{
    char tpl[80], name[80], cwd[80];
    WORD i, j, n, size, pos[N_LOC];
    HGLOBAL hb;
    BYTE FAR *buf;
    int mark;
    WORD fh;

    /* the template, next to TK5.EXE */
    GetModuleFileName((HINSTANCE)hinst(), loc_dir[3], sizeof loc_dir[3]);
    for (i = lstrlen(loc_dir[3]); i && loc_dir[3][i - 1] != '\\'; i--)
        ;
    loc_dir[3][i] = 0;
    trim(loc_dir[3]);
    path_of(tpl, loc_dir[3], "TKID.TPL");
    hb = GlobalAlloc(GMEM_FIXED, 0x8000);
    buf = GlobalLock(hb);
    size = 0;
    if (dos(0x3D00, 0, 0, tpl, &fh)) {
        dos(0x3F00, fh, 0x8000, buf, &size);
        dos(0x3E00, fh, 0, 0, NULL);
    }
    mark = find_mark(buf, size);
    obs("TKID.TPL read", size != 0 && mark >= 0);
    if (!size || mark < 0)
        return;

    get_cwd(cwd + 3);                   /* to restore later */
    dos(0x3900, 0, 0, "C:\\TKCWD", NULL);
    dos(0x3900, 0, 0, "C:\\TKPATH", NULL);
    dos(0x3B00, 0, 0, "C:\\TKCWD", NULL);
    scopy(loc_dir[0], "C:\\TKCWD");
    GetWindowsDirectory(loc_dir[1], sizeof loc_dir[1]);
    GetSystemDirectory(loc_dir[2], sizeof loc_dir[2]);
    scopy(loc_dir[4], "C:\\TKPATH");
    trim(loc_dir[1]);
    trim(loc_dir[2]);
    obs_str("windir", loc_dir[1]);
    obs_str("sysdir", loc_dir[2]);
    obs_str("appdir", loc_dir[3]);
    for (i = 0; i < N_LOC; i++) {
        loc_use[i] = 1;
        for (j = 0; j < i; j++)
            if (loc_use[j] && seq(loc_dir[i], loc_dir[j]))
                loc_use[i] = 0;
    }
    if (!env_has_tkpath())
        loc_use[4] = 0;
    obs("PATH has C:\\TKPATH", loc_use[4]);
    for (i = 0; i < N_LOC; i++) {
        if (!loc_use[i])
            continue;
        buf[mark] = (BYTE)(i + 1);
        path_of(name, loc_dir[i], "TKID.DLL");
        if (!write_file(name, buf, size)) {
            obs_str("cannot write", name);
            loc_use[i] = 0;
        }
        pos[i] = 0xFF;
    }

    /* load, see which copy came, delete it, again */
    for (n = 1; n <= N_LOC + 1; n++) {
        HINSTANCE h = LoadLibrary("TKID.DLL");
        wordfn get;
        WORD id;
        if ((WORD)h < 32) {
            obsn("search: LoadLibrary when none left", n, (WORD)h);
            break;
        }
        get = (wordfn)GetProcAddress(h, "ID_GET");
        id = get ? get() : 0;
        if (n == 1) {
            /* usage counts, WEP and a second copy while loaded */
            setwepfn setwep = (setwepfn)GetProcAddress(h, "ID_SETWEP");
            infofn info = (infofn)GetProcAddress(h, "ID_INFO");
            HINSTANCE h2 = LoadLibrary("TKID.DLL");
            HINSTANCE h3 = LoadLibrary("C:\\TKPATH\\TKID.DLL");
            obs("TKID LoadLibrary twice same", h2 == h);
            obs_load("TKID LoadLibrary(other path) while loaded", h3);
            obs("TKID LoadLibrary(other path) while loaded==same", h3 == h);
            obs("TKID usage after 3 loads", GetModuleUsage(h));
            obs("TKID init calls after 3 loads", info ? info(25) : 0);
            if (info) {
                WORD ds = info(26);
                obs("TKID init AX", info(0));
                obs_env("TKID init BX", info(1));
                obs("TKID init CX", info(2));
                obs("TKID init DX==DS", info(3) == ds);
                obs("TKID init SI", info(4));
                obs("TKID init DI==DS&~1", info(5) == (ds & ~1u));
                obs("TKID init DS==DGROUP", info(8) == ds);
                obs("TKID init ES", info(9));
                obs("TKID init SS==TK5 SS", info(10) == entry_regs[10]);
                obs_env("TKID init SP", info(7));
                obs("TKID init SP < TK5 entry SP", info(7) < entry_regs[E_SP]);
                obs("TKID init inst(0006)", info(15));
                obs("TKID LoadLibrary==DS&~1", (WORD)h == (ds & ~1u));
            }
            if ((WORD)h3 >= 32)
                FreeLibrary(h3);
            FreeLibrary(h2);
            obs("TKID usage after 2 frees", GetModuleUsage(h));
            wep_flag = 0;
            if (setwep)
                setwep((WORD FAR *)&wep_flag);
            FreeLibrary(h);
            obs("TKID WEP flag after last FreeLibrary", wep_flag);
        } else {
            FreeLibrary(h);
        }
        obsn("TKID unloaded after FreeLibrary", n, GetModuleHandle("TKID") == 0);
        if (id < 1 || id > N_LOC || !loc_use[id - 1]) {
            obsn("search: unexpected id", n, id);
            break;
        }
        pos[id - 1] = n;
        path_of(name, loc_dir[id - 1], "TKID.DLL");
        del_file(name);
    }
    for (i = 0; i < N_LOC; i++)
        if (loc_use[i]) {
            path_of(name, loc_dir[i], "TKID.DLL");
            del_file(name);
        }
    for (i = 0; i < N_LOC; i++)
        for (j = 0; j < N_LOC; j++) {
            char what[40];
            if (i == j || !loc_use[i] || !loc_use[j])
                continue;
            scopy(what, "search: ");
            scat(what, loc_name[i]);
            scat(what, " before ");
            scat(what, loc_name[j]);
            obs(what, pos[i] < pos[j]);
        }
    for (i = 0; i < N_LOC; i++)
        if (loc_use[i]) {
            char what[40];
            scopy(what, "search: found in ");
            scat(what, loc_name[i]);
            obs(what, pos[i] != 0xFF);
        }
    cwd[0] = 'C';
    cwd[1] = ':';
    cwd[2] = '\\';
    dos(0x3B00, 0, 0, cwd, NULL);
    dos(0x3A00, 0, 0, "C:\\TKCWD", NULL);
    dos(0x3A00, 0, 0, "C:\\TKPATH", NULL);
    GlobalUnlock(hb);
    GlobalFree(hb);
}

int __cdecl tmain(void)
{
    out("TK5 start\r\n");
    probe_implicit();
    STEP("prologues");
    probe_prologues();
    STEP("MakeProcInstance");
    probe_mpi();
    STEP("GetInstanceData");
    probe_instancedata();
    STEP("LoadLibrary");
    probe_loadlibrary();
    STEP("search order");
    probe_search();
    STEP("TKDLL LocalAlloc");
    obs("TKDLL LocalAlloc(8) != 0", DLL_LALLOC(8) != 0);
    STEP("missing dependency");
    {
        HINSTANCE h = LoadLibrary("TKNODEP.DLL");
        obs_load("LoadLibrary(TKNODEP.DLL) (imports a missing module)", h);
        obs("TKNODEP loaded afterwards", GetModuleHandle("TKNODEP") != 0);
    }
    out("TK5 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
