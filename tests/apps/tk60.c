/*
 * tk60.c - how KERNEL finds a loaded module by name: GetModuleHandle,
 * LoadLibrary and LoadModule with module names, file names, paths,
 * handles, and modules whose module name is not their file's name.
 *
 * Pietrek, "Windows Internals" (1993), ch. 3 (LMAlreadyLoaded) says
 * LoadModule compares the argument's base name and extension with the
 * loaded modules' file names first, then its base name with their module
 * names. The probe checks that, and GetModuleHandle, black-box.
 *
 * Needs TKID.TPL next to TK60.EXE (tests/tk5ref.sh TK60, and
 * tests/run-tests.sh's TK5 layout). From it the probe writes, in C:\TKMN,
 * copies with other module names (the name in the resident-name table
 * patched; the code is TKID's):
 *   file TKFA.DLL  module TKFB      file TKFB.DLL  module TKFC
 *   file TKFD.DLL  module TKFC      file TKFE.DLL  module TKFE
 *   file TKFN      module TKFM      file TKFX.EXE  module TKFY (a library)
 *   file TKFO.DLL  module TKFN (TKFN's file name)
 * TK60.EXE itself is module TKSIXTY (tk60.lnk). It removes them again.
 *
 * Observations name a module by a number (who()): 0 none, 1 TKFA.DLL,
 * 2 TKFB.DLL, 3 TKFE.DLL, 4 TKFN, 5 TKFX.EXE, 6 TK60.EXE, 7 KERNEL,
 * 8 TKFD.DLL, 9 TKFO.DLL, EE another. LoadLibrary and LoadModule: 100h + who() of the
 * module loaded or found, or the error code.
 *
 * Output and checks: see tklog.h; reference values in tk60ref.h.
 */
#include <windows.h>

#define TK_REF "tk60ref.h"
#include "tklog.h"

extern int __cdecl dos_int21(WORD FAR *r);

#define STEP(s) out("-- " s "\r\n")

typedef struct {
    WORD env;
    LPSTR cmd;
    WORD FAR *show;
    DWORD res;
} lparms;

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static int dos(WORD ax, WORD bx, WORD cx, const void FAR *dsdx, WORD *ax_out)
{
    WORD r[8];
    int c;
    r[0] = ax;
    r[1] = bx;
    r[2] = cx;
    r[3] = (WORD)(DWORD)dsdx;
    r[4] = r[5] = 0;
    r[6] = (WORD)((DWORD)dsdx >> 16);
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

/* ---- the files ---- */

typedef struct {
    const char *file;           /* under C:\TKMN */
    const char *mod;            /* 4 characters, as TKID */
} tfile;

static const tfile files[] = {
    { "TKFA.DLL", "TKFB" },
    { "TKFB.DLL", "TKFC" },
    { "TKFE.DLL", "TKFE" },
    { "TKFN", "TKFM" },
    { "TKFX.EXE", "TKFY" },
    { 0, 0 },                   /* 6: TK60.EXE, 7: KERNEL */
    { 0, 0 },
    { "TKFD.DLL", "TKFC" },
    { "TKFO.DLL", "TKFN" },
};
#define NFILES 9
#define W_SELF 6
#define W_KERNEL 7

static WORD hmod[NFILES + 1];   /* hModule of who() 1..8, while loaded */

static int write_file(const char *name, const void FAR *buf, WORD n)
{
    WORD h, w = 0;
    if (!dos(0x3C00, 0, 0, name, &h))
        return 0;
    dos(0x4000, h, n, buf, &w);
    dos(0x3E00, h, 0, 0, NULL);
    return w == n;
}

static int make_files(void)
{
    char path[128];
    WORD i, fh, size = 0, ne, rn;
    HGLOBAL hb = GlobalAlloc(GMEM_FIXED, 0x8000);
    BYTE FAR *buf = GlobalLock(hb);
    int ok = 1;

    GetModuleFileName((HINSTANCE)hinst(), path, sizeof path);
    for (i = lstrlen(path); i && path[i - 1] != '\\'; i--)
        ;
    path[i] = 0;
    scat(path, "TKID.TPL");
    if (dos(0x3D00, 0, 0, path, &fh)) {
        dos(0x3F00, fh, 0x8000, buf, &size);
        dos(0x3E00, fh, 0, 0, NULL);
    }
    ne = size > 0x40 ? *(WORD FAR *)(buf + 0x3C) : 0;
    rn = ne && ne + 0x28 < size ? ne + *(WORD FAR *)(buf + ne + 0x26) : 0;
    if (!rn || rn + 5 > size || buf[rn] != 4 || buf[rn + 1] != 'T') {
        obs("template read", 0);
        GlobalFree(hb);
        return 0;
    }
    dos(0x3900, 0, 0, "C:\\TKMN", NULL);
    for (i = 0; i < NFILES; i++) {
        if (!files[i].file)
            continue;
        scopy(path, "C:\\TKMN\\");
        scat(path, files[i].file);
        {
            WORD k;
            for (k = 0; k < 4; k++)
                buf[rn + 1 + k] = (BYTE)files[i].mod[k];
        }
        if (!write_file(path, buf, size))
            ok = 0;
    }
    obs("files written", ok);
    GlobalFree(hb);
    return ok;
}

static void del_files(void)
{
    char path[64];
    WORD i;
    for (i = 0; i < NFILES; i++) {
        if (!files[i].file)
            continue;
        scopy(path, "C:\\TKMN\\");
        scat(path, files[i].file);
        dos(0x4100, 0, 0, path, NULL);
    }
    dos(0x3A00, 0, 0, "C:\\TKMN", NULL);
}

/* ---- naming results ---- */

static WORD who(WORD h)
{
    WORD i;
    if (!h)
        return 0;
    for (i = 1; i <= NFILES; i++)
        if (hmod[i] && (hmod[i] | 3) == (h | 3))
            return i;
    return 0xEE;
}

static WORD exeptr(WORD h)
{
    return (WORD)GetModuleHandle((LPCSTR)MAKELP(0, h));
}

/* the part of the probe: a name observed in several parts is named
   apart */
static const char *part = "";

/* obs "PART PREFIX(NAME)" */
static void obsq(const char *prefix, const char FAR *name, WORD v)
{
    char b[96];
    scopy(b, part);
    scat(b, prefix);
    scat(b, "(");
    scat(b, name);
    scat(b, ")");
    obs(b, v);
}

static void gmh(const char FAR *name)
{
    obsq("GetModuleHandle", name, who((WORD)GetModuleHandle(name)));
}

static WORD loadres(HINSTANCE h)
{
    return (WORD)h < 32 ? (WORD)h : 0x100 | who(exeptr((WORD)h));
}

/* LoadLibrary, logged, and freed again (the usage it added) */
static void ll(const char FAR *name)
{
    HINSTANCE h = LoadLibrary(name);
    obsq("LoadLibrary", name, loadres(h));
    if ((WORD)h >= 32)
        FreeLibrary(h);
}

static void lm(const char FAR *name)
{
    static WORD show[2] = { 2, SW_SHOWNORMAL };
    static char nul[2] = { 0, 13 };
    lparms pb;
    HINSTANCE h;
    pb.env = 0;
    pb.cmd = nul;
    pb.show = show;
    pb.res = 0;
    h = LoadModule(name, &pb);
    obsq("LoadModule", name, loadres(h));
    if ((WORD)h >= 32)
        FreeModule(h);
}

static void usage(const char *label, WORD w)
{
    char b[64];
    scopy(b, "usage ");
    scat(b, label);
    obs(b, hmod[w] ? GetModuleUsage((HINSTANCE)hmod[w]) : 0xFFFF);
}

/* load a test file by full path; its hModule becomes who() w */
static HINSTANCE load(WORD w)
{
    char path[64];
    HINSTANCE h;
    scopy(path, "C:\\TKMN\\");
    scat(path, files[w - 1].file);
    h = LoadLibrary(path);
    if ((WORD)h >= 32 && who(exeptr((WORD)h)) == 0xEE)
        hmod[w] = exeptr((WORD)h);
    obsq("load", files[w - 1].file, loadres(h));
    return h;
}

static void unload(WORD w, HINSTANCE h)
{
    /* not GetModuleHandle(MAKELP(0, h)) afterwards: with a freed
       selector it faults on 3.1 */
    if ((WORD)h < 32)
        return;
    if (hmod[w] && GetModuleUsage(h) == 1)
        hmod[w] = 0;
    FreeLibrary(h);
}

static char kfile[16], kbase[16];

static void kernel_names(void)
{
    char p[128];
    WORD i, s = 0, d = 0;
    hmod[W_KERNEL] = (WORD)GetModuleHandle("KERNEL");
    GetModuleFileName((HINSTANCE)hmod[W_KERNEL], p, sizeof p);
    for (i = 0; p[i]; i++)
        if (p[i] == '\\')
            s = i + 1;
    for (i = 0; p[s + i] && i < 15; i++) {
        kfile[i] = p[s + i];
        if (p[s + i] == '.' && !d)
            d = i;
    }
    kfile[i] = 0;
    for (i = 0; i < d; i++)
        kbase[i] = kfile[i];
    kbase[i] = 0;
}

/* ---- the probes ---- */

static void probe_handles(void)
{
    HINSTANCE ha = load(1);
    FARPROC f = (WORD)ha >= 32 ? GetProcAddress(ha, "ID_GET") : NULL;
    WORD m = hmod[1];
    STEP("handles");
    obs("GetModuleHandle(MAKELP(0,hInstance))", who(exeptr((WORD)ha)));
    obs("GetModuleHandle(MAKELP(0,hInstance|1))", who(exeptr((WORD)ha | 1)));
    obs("GetModuleHandle(MAKELP(0,hModule))", who(exeptr(m)));
    obs("GetModuleHandle(MAKELP(0,hModule&~3))", who(exeptr(m & ~3)));
    obs("GetModuleHandle(MAKELP(0,code selector))", who(exeptr(HIWORD((DWORD)f))));
    obs("GetModuleHandle(MAKELP(0,own hInstance))", who(exeptr(hinst())));
    obs("GetModuleHandle(MAKELP(0,own CS))", who(exeptr(HIWORD((DWORD)(FARPROC)probe_handles))));
    obs("GetModuleHandle(MAKELP(0,hTask))", who(exeptr((WORD)GetCurrentTask())));
    obs("GetModuleHandle(NULL)", who((WORD)GetModuleHandle(NULL)));
    obs("result is an hModule", (WORD)GetModuleHandle("TKFB") == m);
    obs("result is odd", ((WORD)GetModuleHandle("TKFB") & 1) != 0);
    obs("result points to NE", m && *(WORD FAR *)MAKELP(m, 0) == 0x454E);
    obs("result == GetExePtr", (WORD)GetModuleHandle("TKFB") == exeptr((WORD)ha));
    /* DX: the first module table of the list (Pietrek ch. 3: "upon
       return from this call, DX contains the module handle of the first
       module in the system"; Visual C++ 1.52's WPS.EXE walks the modules
       from it) */
    {
        typedef DWORD (FAR PASCAL *GMH32)(LPCSTR);
        GMH32 g = (GMH32)GetModuleHandle;
        obs("GetModuleHandle DX(TKFB)", who(HIWORD(g("TKFB"))));
        obs("GetModuleHandle DX(KERNEL)", who(HIWORD(g("KERNEL"))));
        obs("GetModuleHandle DX(unknown)", who(HIWORD(g("TKNOSUCH"))));
        obs("GetModuleHandle DX(MAKELP(0,hInstance))", who(HIWORD(g((LPCSTR)MAKELP(0, (WORD)ha)))));
        obs("GetModuleHandle DX(MAKELP(0,hTask))", who(HIWORD(g((LPCSTR)MAKELP(0, (WORD)GetCurrentTask())))));
        obs("GetModuleHandle DX(NULL)", who(HIWORD(g(NULL))));
    }
    unload(1, ha);
}

/* A loaded alone: what LoadLibrary and LoadModule find by each name */
static void probe_load_a(void)
{
    HINSTANCE ha;
    STEP("TKFA.DLL loaded alone");
    part = "A: ";
    ha = load(1);
    usage("A after load", 1);
    ll("C:\\TKMN\\TKFB.DLL");
    usage("A after LoadLibrary(TKFB.DLL)", 1);
    ll("TKFB.DLL");
    ll("TKFB");
    ll("tkfb");
    ll("TKFA.DLL");
    ll("tkfa.dll");
    ll("C:\\NOWHERE\\TKFA.DLL");
    ll("D:\\TKFA.DLL");
    ll("TKFA");
    ll("TKFA.EXE");
    ll("TKFA.");
    ll("TKFB.EXE");
    ll("TKFB.XYZ");
    ll("C:\\NOWHERE\\TKFB.DLL");
    ll("C:\\NOWHERE\\TKFB");
    lm("C:\\TKMN\\TKFB.DLL");
    lm("TKFB");
    lm("TKFA.DLL");
    lm("C:\\NOWHERE\\TKFA.DLL");
    usage("A at the end", 1);
    gmh("TKFB");
    gmh("TKFB.DLL");
    gmh("TKFA.DLL");
    gmh("TKFA");
    unload(1, ha);
    obs("A unloaded", GetModuleHandle("TKFB") == 0);
}

static void probe_names(void)
{
    HINSTANCE hb, ha, he, hn, hx;
    STEP("B, A, E, N, X loaded");
    part = "";
    hb = load(2);
    ha = load(1);
    he = load(3);
    hn = load(4);
    if ((WORD)hn < 32) {
        hn = LoadLibrary("C:\\TKMN\\TKFN.");
        obs("load(TKFN.)", loadres(hn));
        if ((WORD)hn >= 32 && who(exeptr((WORD)hn)) == 0xEE)
            hmod[4] = exeptr((WORD)hn);
    }
    hx = load(5);
    {
        char p[128];
        if (hmod[4]) {
            GetModuleFileName((HINSTANCE)hmod[4], p, sizeof p);
            obs_str("TKFN's file name", p);
        }
    }
    usage("A", 1);
    usage("B", 2);

    STEP("GetModuleHandle by name");
    /* module names */
    gmh("TKFB");
    gmh("tkfb");
    gmh("TkFb");
    gmh("TKFC");
    gmh("TKFE");
    gmh("TKFM");
    gmh("TKFY");
    gmh("TKSIXTY");
    gmh("KERNEL");
    gmh("kernel");
    /* file names */
    gmh("TKFA.DLL");
    gmh("tkfa.dll");
    gmh("TKFB.DLL");
    gmh("tkfb.dll");
    gmh("TKFE.DLL");
    gmh("TKFX.EXE");
    gmh("TK60.EXE");
    obsq("GetModuleHandle", "kernel's file", who((WORD)GetModuleHandle(kfile)));
    obsq("GetModuleHandle", "kernel's file less extension",
         who((WORD)GetModuleHandle(kbase)));
    /* a file's base name without its extension; a module's with one */
    gmh("TKFA");
    gmh("TKFX");
    gmh("TK60");
    gmh("TKFN");
    gmh("TKFN.");
    gmh("TKFC.DLL");
    gmh("TKFC.EXE");
    gmh("TKFE.EXE");
    gmh("TKFM.DLL");
    gmh("TKFY.EXE");
    gmh("TKFY.DLL");
    gmh("TKSIXTY.EXE");
    gmh("KERNEL.EXE");
    /* other extensions, partial ones */
    gmh("TKFA.EXE");
    gmh("TKFA.");
    gmh("TKFB.");
    gmh("TKFC.");
    gmh("TKFE.");
    gmh("TKFA.D");
    gmh("TKFA.DL");
    gmh("TKFA.DLLX");
    gmh("TKFA.DLL.");
    gmh("TKFE.DLL ");
    gmh("TKFX.DLL");
    /* paths */
    gmh("C:\\TKMN\\TKFA.DLL");
    gmh("c:\\tkmn\\tkfa.dll");
    gmh("C:\\NOWHERE\\TKFA.DLL");
    gmh("D:\\TKFA.DLL");
    gmh("C:TKFA.DLL");
    gmh("C:/TKMN/TKFA.DLL");
    gmh("TKMN\\TKFA.DLL");
    gmh("\\TKFA.DLL");
    gmh("C:\\TKMN\\TKFB");
    gmh("C:\\TKMN\\TKFC");
    gmh("C:\\NOWHERE\\TKFC");
    gmh("C:\\A.B\\TKFC");
    gmh("C:\\A.B\\TKFA.DLL");
    gmh("C:\\TKMN\\TKFB.DLL");
    gmh("C:\\TKMN\\TKFC.DLL");
    gmh("C:\\TKMN\\TKFN");
    gmh("C:TKFC");
    gmh("C:/TKFC");
    gmh("TKMN/TKFC");
    /* odd strings */
    gmh("");
    gmh(".");
    gmh(".DLL");
    gmh("\\");
    gmh("C:\\");
    gmh("C:");
    gmh("TKFB ");
    gmh(" TKFB");
    gmh("TKFBX");
    gmh("TKF");
    gmh("TKFEXXXXXXXXXXXX");
    gmh("TKFEXXXXXXXXXXXX.DLL");
    /* a file name and what follows it */
    gmh("TKFNX");
    gmh("TKFN.DLL");
    gmh("TKFN.X");
    gmh("C:\\TKMN\\TKFNX");
    gmh("TKFA.DLLXXXXXXXXXXXXXX");
    gmh("TKFA.DLL\\X");
    gmh("TKFA.DLL/");

    STEP("LoadLibrary by name, all loaded");
    ll("TKFB.DLL");
    ll("TKFC.DLL");
    ll("TKFC");
    ll("C:\\NOWHERE\\TKFC.DLL");
    ll("TKFM.DLL");
    ll("TKFM");
    ll("TKFN");
    ll("TKFY.DLL");
    ll("TKFY.EXE");
    ll("TKFY");
    ll("TKFX.EXE");
    ll("TKFX.DLL");
    ll("TKSIXTY.DLL");
    ll("TKSIXTY");
    ll("TK60.DLL");
    ll("KERNEL.DLL");
    ll("KERNEL");
    {
        /* "" in a buffer of its own: 3.1's LoadLibrary("") wrote past
           the string (into the probe's other constants) */
        static char e[16];
        WORD i;
        for (i = 1; i < sizeof e; i++)
            e[i] = (char)0xAA;
        e[0] = 0;
        ll(e);
        for (i = 0; i < 8; i++)
            obsn("LoadLibrary(\"\") left byte", i, (BYTE)e[i]);
        for (i = 1; i < sizeof e; i++)
            e[i] = (char)0xAA;
        lm(e);
        for (i = 0; i < 8; i++)
            obsn("LoadModule(\"\") left byte", i, (BYTE)e[i]);
        for (i = 1; i < sizeof e; i++)
            e[i] = (char)0xAA;
        gmh(e);
        for (i = 0; i < 8; i++)
            obsn("GetModuleHandle(\"\") left byte", i, (BYTE)e[i]);
    }
    ll("TKFA.DLLX");
    ll("TKFNX");
    ll("TKFN.DLL");
    lm("TKFC");
    lm("TKFC.DLL");
    lm("TKFY.EXE");
    usage("A at the end", 1);
    usage("B at the end", 2);

    STEP("a second module named TKFC");
    part = "C2: ";
    {
        HINSTANCE hd = load(8);
        usage("B after loading TKFD.DLL", 2);
        usage("D after loading it", 8);
        gmh("TKFC");
        gmh("TKFD.DLL");
        gmh("TKFB.DLL");
        ll("TKFC.DLL");
        unload(8, hd);
        obs("B still loaded", GetModuleHandle("TKFB.DLL") != 0 || GetModuleHandle("TKFC") != 0);
        usage("B at the end", 2);
    }

    STEP("a module named TKFN beside the file TKFN");
    part = "N2: ";
    {
        HINSTANCE ho = load(9);
        gmh("TKFN");
        gmh("TKFO.DLL");
        gmh("TKFO");
        ll("TKFN");
        ll("TKFN.DLL");
        ll("TKFO.DLL");
        unload(9, ho);
    }

    part = "";
    unload(5, hx);
    unload(4, hn);
    unload(3, he);
    unload(1, ha);
    unload(2, hb);
    obs("all unloaded", !GetModuleHandle("TKFB") && !GetModuleHandle("TKFC") &&
                        !GetModuleHandle("TKFE") && !GetModuleHandle("TKFM") &&
                        !GetModuleHandle("TKFY"));
}

int __cdecl tmain(void)
{
    out("TK60 start\r\n");
    SetErrorMode(SEM_NOOPENFILEERRORBOX);
    hmod[W_SELF] = exeptr(hinst());
    kernel_names();
    obs_str("kernel's file", kfile);
    if (make_files()) {
        probe_handles();
        probe_load_a();
        probe_names();
    }
    del_files();
    out("TK60 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
