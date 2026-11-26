/*
 * tk66.c - the KERNEL audit, third round (its triage file, docs/notes/kernel-audit-triage.md, is in git history):
 * what Windows 3.1 does where w16's remaining fixes would change what a
 * program sees, observed before w16 changes it.
 *
 * TK66.EXE (the main run):
 *  - "kmod": GlobalHandle of KERNEL's module table (medium 33), and of
 *    the probe's own for comparison;
 *  - "drivetype": GetDriveType past Z: (0101h gave 3 on 3.1, 0 on w16;
 *    medium 39);
 *  - "profile": section and key names holding ']' and '=' (L33), read
 *    back from the cache and from the file, and the file itself;
 *  - "lres": LoadResource 65536 times, then FreeResource (L8: a word
 *    count wraps);
 *  - "kseg2": KERNEL's discardable code segments (2 and 3; medium 33)
 *    after GlobalCompact(-1) and with the heap full;
 *  - "entries": LoadLibrary of TKENT2.DLL (12000 entries; C5): how 3.1
 *    fails it.
 * Copies of TK66.EXE run one case each (their file name's last letter),
 * each in a Windows of its own, as each may end it:
 *  - TK66L.EXE: KERNEL.87 (lstrcmp) with NULL (medium 43);
 *  - TK66S.EXE: the selector functions with selector 0 and a GDT
 *    selector, AllocCStoDSAlias of a conforming code segment (medium 23);
 *  - TK66R.EXE: GlobalReAlloc of KERNEL's segments (medium 33);
 *  - TK66F.EXE: GlobalFree of KERNEL's segments (medium 33);
 *  - TK66I.EXE: LocalInit with a heap starting at 0, 1 and 4 (medium 6:
 *    the heap's first arena and the instance words overlap);
 *  - TK66E.EXE: LoadLibrary of DLLs with 8000 to 13300 entries
 *    (TKEA-TKEE.DLL, TKENT2.DLL, TKENT.DLL; C5): where 3.1 stops;
 *  - TK66N.EXE: TKBADNM.DLL, whose named resource type's name lies past
 *    its module table (tools/mkne.py --type-name-off; medium 16).
 * Every step is logged before it is taken, so a run that ends Windows
 * shows where.
 *
 * Runs in make test beside TK62 (tests/run-tests.sh); the reference run:
 * tests/tk66ref.sh.
 */
#include <windows.h>

#define TK_REF "tk66ref.h"
#define TK_REF286 "tk66ref286.h"
#include "tklog.h"

extern WORD __cdecl sel_ar(WORD sel);
extern WORD __cdecl cur_ds(void);
extern DWORD __cdecl call_ds(WORD ds, void FAR *fn, WORD nargs, WORD a1, WORD a2, WORD a3);

WORD use386;                    /* tk17s.asm */
static char buf[260];
static char scenario;

/* DPMI 0009h, set access rights: 0 when refused */
extern WORD dpmi_rights(WORD sel, WORD rights);
#pragma aux dpmi_rights = \
    "mov ax, 9" \
    "int 31h" \
    "sbb ax, ax" \
    "inc ax" \
    parm [bx] [cx] value [ax];

static FARPROC kord(WORD n)
{
    return GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(n));
}

static WORD rights(WORD sel)
{
    return ((WORD (FAR PASCAL *)(WORD, WORD, WORD))kord(196))(sel, 0, 0);
}

static void step(const char *name)
{
    out("== ");
    out(name);
    out("\r\n");
}

static void note(const char *s)
{
    out("-- ");
    out(s);
    out("\r\n");
}

static void str(const char *name, const char FAR *s, WORD n)
{
    WORD i;
    out("OBS ");
    out(name);
    out("=\"");
    for (i = 0; i < n; i++) {
        char c[2];
        c[1] = 0;
        if (s[i] == '\r') {
            out("\\r");
        } else if (s[i] == '\n') {
            out("\\n");
        } else if (s[i] == 0) {
            out("\\0");
        } else {
            c[0] = s[i];
            out(c);
        }
    }
    out("\"\r\n");
}

/* KERNEL's segment n (1-based): its handle from the module table */
static WORD kseg_handle(WORD n)
{
    WORD ne = (WORD)GetModuleHandle("KERNEL");
    WORD st = *(WORD FAR *)MAKELP(ne, 0x22);
    if (n > *(WORD FAR *)MAKELP(ne, 0x1C))
        return 0;
    return ((WORD FAR *)MAKELP(ne, st + 10 * (n - 1)))[4];
}

/* ---- TK66.EXE ---- */

static void modh(const char *pfx, WORD ne)
{
    static char nm[40];
    DWORD gh = GlobalHandle(ne);
    WORD n = 0;
    while (pfx[n]) {
        nm[n] = pfx[n];
        n++;
    }
#define O(s, v) (lstrcpy(nm + n, s), obs(nm, (v)))
#define E(s, v) (lstrcpy(nm + n, s), obs_env(nm, (v)))
    E("ne", ne);
    E("lo", LOWORD(gh));
    E("hi", HIWORD(gh));
    O("ne odd", ne & 1);
    O("lo is ne", LOWORD(gh) == ne);
    O("lo is ne-1", LOWORD(gh) == ne - 1);
    O("lo is ne+1", LOWORD(gh) == ne + 1);
    O("lo 0", LOWORD(gh) == 0);
    O("hi is ne", HIWORD(gh) == ne);
    O("hi is ne|1", HIWORD(gh) == (ne | 1));
    O("hi 0", HIWORD(gh) == 0);
    gh = GlobalHandle(ne | 1);
    O("odd lo is ne", LOWORD(gh) == ne);
    O("odd hi is ne|1", HIWORD(gh) == (ne | 1));
    O("flags", GlobalFlags((HGLOBAL)ne));
    O("size ok", GlobalSize((HGLOBAL)ne) != 0);
    E("size", (WORD)GlobalSize((HGLOBAL)ne));
#undef O
#undef E
}

static void kmod(void)
{
    step("kmod");
    modh("kmod: ", (WORD)GetModuleHandle("KERNEL"));
    modh("own: ", (WORD)GetModuleHandle("TK66"));
}

static void drivetype(void)
{
    static const WORD d[] = {
        0, 1, 2, 3, 5, 25, 26, 0xFF, 0x100, 0x101, 0x102, 0x103, 0x105,
        0x119, 0x11A, 0x1FF, 0x200, 0x202, 0x203, 0x8002, 0xFF02, 0xFFFF
    };
    static char nm[] = "drivetype: XXXX";
    WORD i, v;
    step("drivetype");
    for (i = 0; i < sizeof d / sizeof d[0]; i++) {
        v = d[i];
        nm[11] = "0123456789ABCDEF"[v >> 12];
        nm[12] = "0123456789ABCDEF"[(v >> 8) & 15];
        nm[13] = "0123456789ABCDEF"[(v >> 4) & 15];
        nm[14] = "0123456789ABCDEF"[v & 15];
        /* D: is mounted in make test's DOSBox only */
        if ((v & 0xFF) == 3)
            obs_env(nm, GetDriveType(v));
        else
            obs(nm, GetDriveType(v));
    }
}

/* L33: names with ']' and '=' */
static void profile(void)
{
    static const char *const sec[] = { "s]x", "s=x", "[s", "t" };
    static const char *const key[] = { "k=y", "k]y", "k", "[k" };
    static char nm[] = "profile: N xxxxxxxx";
    HFILE f;
    OFSTRUCT of;
    WORD i, n, pass;
    step("profile");
    OpenFile("TK66.INI", &of, OF_DELETE);
    for (i = 0; i < 4; i++) {
        nm[9] = (char)('1' + i);
        lstrcpy(nm + 11, "write");
        obs(nm, WritePrivateProfileString(sec[i], key[i], "v", "TK66.INI"));
    }
    for (pass = 0; pass < 2; pass++) {
        if (pass)
            WritePrivateProfileString(NULL, NULL, NULL, "TK66.INI");
        for (i = 0; i < 4; i++) {
            nm[9] = (char)('1' + i);
            lstrcpy(buf, "?");
            n = GetPrivateProfileString(sec[i], key[i], "d", buf, 32, "TK66.INI");
            lstrcpy(nm + 11, pass ? "file" : "cache");
            obs(nm, n == 1 && buf[0] == 'v' ? 1 : n == 1 && buf[0] == 'd' ? 2 : 0x100 + n);
            /* the section's keys */
            n = GetPrivateProfileString(sec[i], NULL, "d", buf, sizeof buf, "TK66.INI");
            lstrcpy(nm + 11, pass ? "fkeys" : "ckeys");
            obs(nm, n);
            str(nm, buf, n);
        }
    }
    f = OpenFile("TK66.INI", &of, OF_READ);
    obs("profile: file opened", f != HFILE_ERROR);
    if (f != HFILE_ERROR) {
        n = _lread(f, buf, sizeof buf);
        _lclose(f);
        obs("profile: file length", n);
        str("profile: file", buf, n);
    }
    OpenFile("TK66.INI", &of, OF_DELETE);
}

/* L8: the usage count of a resource */
static void lres(void)
{
    HINSTANCE hi = (HINSTANCE)(entry_regs[E_DS] & ~1u);
    HRSRC r = FindResource(hi, MAKEINTRESOURCE(1), RT_RCDATA);
    HGLOBAL h, h2;
    WORD same = 1, n;
    DWORD i;
    step("lres");
    obs("lres: found", r != 0);
    if (!r)
        return;
    h = LoadResource(hi, r);
    obs("lres: loaded", h != 0);
    obs("lres: size after 1st", GlobalSize(h) != 0);
    obs("lres: flags after 1st", GlobalFlags(h));
    h2 = LoadResource(hi, r);
    obs("lres: 2nd same", h2 == h);
    obs("lres: size after 2nd", GlobalSize(h) != 0);
    obs("lres: flags after 2nd", GlobalFlags(h));
    n = FreeResource(h);
    obs("lres: free 1 of 2", n == 0 ? 0 : n == (WORD)h ? 1 : 2);
    obs("lres: size after 1 of 2", GlobalSize(h) != 0);
    obs("lres: flags after 1 of 2", GlobalFlags(h));
    obs("lres: free 2 of 2", FreeResource(h));
    obs("lres: size after 2 of 2", GlobalSize(h) != 0);
    obs("lres: flags after 2 of 2", GlobalFlags(h));
    /* 65536 loads: a word count is back at 0 */
    h = LoadResource(hi, r);
    obs("lres: reload same", h == h2);
    obs("lres: size after reload", GlobalSize(h) != 0);
    obs("lres: flags after reload", GlobalFlags(h));
    h2 = LoadResource(hi, r);
    obs("lres: size after reload 2", GlobalSize(h) != 0);
    obs("lres: flags after reload 2", GlobalFlags(h));
    for (i = 2; i < 0x10000UL; i++)
        if (LoadResource(hi, r) != h)
            same = 0;
    obs("lres: 65536 same", same);
    obs("lres: size after 65536", GlobalSize(h) != 0);
    obs("lres: flags after 65536", GlobalFlags(h));
    n = FreeResource(h);
    obs("lres: free after 65536", n);
    obs("lres: size after free", GlobalSize(h) != 0);
    obs("lres: flags after free", GlobalFlags(h));
    h2 = LoadResource(hi, r);
    obs("lres: load after", h2 != 0);
    obs("lres: load after same", h2 == h);
    n = FreeResource(h2);
    obs("lres: free after load", n);
    obs("lres: size at end", GlobalSize(h2) != 0);
}

/* (KERNEL's discardable code segments, 2 and 3, only at the start: 3.1
   discards segment 3 with GlobalCompact(-1), and loads it again when it
   is called; w16 cannot load KERNEL's code again and never discards it:
   afterwards their state is noted, not checked) */
static void kflags(const char *pfx)
{
    static char nm[40];
    WORD i, h, n = 0;
    void (*o)(const char *, WORD);
    while (pfx[n]) {
        nm[n] = pfx[n];
        n++;
    }
    for (i = 1; i <= 4; i++) {
        h = kseg_handle(i);
        o = (i == 2 || i == 3) && pfx[7] != 's' ? obs_env : obs;
        nm[n] = (char)('0' + i);
        lstrcpy(nm + n + 1, " flags");
        o(nm, h ? GlobalFlags((HGLOBAL)h) : 0xEEEE);
        lstrcpy(nm + n + 1, " present");
        o(nm, h ? (sel_ar(h | 1) & 0x80) != 0 : 0xEEEE);
        lstrcpy(nm + n + 1, " size ok");
        o(nm, h ? GlobalSize((HGLOBAL)h) != 0 : 0xEEEE);
    }
}

/* medium 33: KERNEL's discardable code segments (2 and 3) */
#define NBIG 512
static void kseg2(void)
{
    static HGLOBAL big[NBIG];
    WORD n, i;
    step("kseg2");
    kflags("kseg2: start ");
    GlobalCompact((DWORD)-1L);
    kflags("kseg2: compact ");
    for (n = 0; n < NBIG; n++)
        if ((big[n] = GlobalAlloc(GMEM_MOVEABLE, 0x10000UL)) == 0)
            break;
    obs_env("kseg2: 64K blocks", n);
    obs_env("kseg2: heap filled", n < NBIG);
    kflags("kseg2: full ");
    for (i = 0; i < n; i++)
        GlobalFree(big[i]);
    /* a KERNEL call (GlobalCompact itself may be in any segment) */
    obs("kseg2: alloc after", (big[0] = GlobalAlloc(GMEM_MOVEABLE, 0x100)) != 0);
    GlobalFree(big[0]);
    kflags("kseg2: end ");
}

static void entries(void)
{
    UINT h;
    step("entries");
    note("LoadLibrary TKENT2.DLL");
    h = (UINT)LoadLibrary("TKENT2.DLL");
    obs("entries: 12000", h < 32 ? h : 0x20);
    if (h >= 32)
        FreeLibrary((HINSTANCE)h);
    note("LoadLibrary TKENT.DLL");
    h = (UINT)LoadLibrary("TKENT.DLL");
    obs("entries: 13300", h < 32 ? h : 0x20);
    if (h >= 32)
        FreeLibrary((HINSTANCE)h);
}

/* ---- TK66E.EXE: entry tables of growing size (C5) ---- */

static void esweep(void)
{
    static const char *const dll[] = {
        "TKEA.DLL", "TKEB.DLL", "TKEC.DLL", "TKED.DLL", "TKEE.DLL",
        "TKENT2.DLL", "TKENT.DLL"
    };
    static char nm[] = "esweep: TKENT2.DLL";
    WORD i;
    UINT h;
    step("esweep");
    for (i = 0; i < sizeof dll / sizeof dll[0]; i++) {
        note(dll[i]);
        h = (UINT)LoadLibrary(dll[i]);
        lstrcpy(nm + 8, dll[i]);
        obs(nm, h < 32 ? h : 0x20);
        if (h >= 32)
            FreeLibrary((HINSTANCE)h);
    }
}

/* ---- TK66L.EXE: KERNEL.87 (lstrcmp) ---- */

static WORD sgn(int v)
{
    return v < 0 ? 0xFFFF : v > 0 ? 1 : 0;
}

static void klstrcmp(void)
{
    int (FAR PASCAL *cmp)(LPCSTR, LPCSTR) = (int (FAR PASCAL *)(LPCSTR, LPCSTR))kord(87);
    step("klstrcmp");
    obs("klstrcmp: abc abd", sgn(cmp("abc", "abd")));
    obs("klstrcmp: abd abc", sgn(cmp("abd", "abc")));
    obs("klstrcmp: a A", sgn(cmp("a", "A")));
    obs("klstrcmp: ab a", sgn(cmp("ab", "a")));
    obs("klstrcmp: empty", sgn(cmp("", "")));
    obs("klstrcmp: e9 f", sgn(cmp("\xE9", "f")));
    obs("klstrcmp: _ a", sgn(cmp("_", "a")));
    obs("klstrcmp: [ a", sgn(cmp("[", "a")));
    obs("klstrcmp: ` A", sgn(cmp("`", "A")));
    obs("klstrcmp: c9 e9", sgn(cmp("\xC9", "\xE9")));
    obs("klstrcmp: 8a 9a", sgn(cmp("\x8A", "\x9A")));
    obs("klstrcmp: ff 9f", sgn(cmp("\xFF", "\x9F")));
    obs("klstrcmp: d7 f7", sgn(cmp("\xD7", "\xF7")));
    obs("klstrcmp: B a", sgn(cmp("B", "a")));
    obs("klstrcmp: - a", sgn(cmp("-", "a")));
    obs("klstrcmp: a- a", sgn(cmp("a-", "a")));
    obs("klstrcmp: ab a-c", sgn(cmp("ab", "a-c")));
    obs("klstrcmp: 1 a", sgn(cmp("1", "a")));
    obs("klstrcmp: e9 e", sgn(cmp("\xE9", "e")));
    obs("klstrcmp: e9 z", sgn(cmp("\xE9", "z")));
    note("NULL a");
    obs("klstrcmp: NULL a", sgn(cmp(NULL, "a")));
    note("a NULL");
    obs("klstrcmp: a NULL", sgn(cmp("a", NULL)));
    note("NULL NULL");
    obs("klstrcmp: NULL NULL", sgn(cmp(NULL, NULL)));
}

/* ---- TK66S.EXE: selector 0, a GDT selector, a conforming segment ---- */

static void sels(void)
{
    WORD s, a, r;
    DWORD d;
    step("sels");
    d = GetSelectorBase(0);
    obs("sels: base 0 lo", LOWORD(d));
    obs("sels: base 0 hi", HIWORD(d));
    d = GetSelectorLimit(0);
    obs("sels: limit 0 lo", LOWORD(d));
    obs("sels: limit 0 hi", HIWORD(d));
    obs_env("sels: rights 0", rights(0));
    d = GetSelectorBase(0x10);
    obs_env("sels: base 10 lo", LOWORD(d));
    obs_env("sels: base 10 hi", HIWORD(d));
    d = GetSelectorLimit(0x10);
    obs_env("sels: limit 10 lo", LOWORD(d));
    obs_env("sels: limit 10 hi", HIWORD(d));
    obs_env("sels: rights 10", rights(0x10));
    note("AllocDStoCSAlias(0)");
    a = (WORD)AllocDStoCSAlias(0);
    obs("sels: DStoCS 0", a != 0);
    if (a)
        FreeSelector(a);
    note("AllocCStoDSAlias(0)");
    a = (WORD)((WORD (FAR PASCAL *)(WORD))kord(170))(0);
    obs("sels: CStoDS 0", a != 0);
    if (a)
        FreeSelector(a);
    note("FreeSelector(0)");
    obs("sels: free 0", FreeSelector(0));

    /* a conforming code segment (DPMI 0009h, type 1Eh) */
    note("conforming");
    s = AllocSelector(cur_ds());
    obs("sels: conf alloc", s != 0);
    if (s) {
        r = dpmi_rights(s, 0x00FE);
        obs("sels: conf set", r);
        obs("sels: conf rights", sel_ar(s) & 0xFF);
        a = (WORD)((WORD (FAR PASCAL *)(WORD))kord(170))(s);
        obs("sels: conf CStoDS", a != 0);
        if (a) {
            obs("sels: conf CStoDS rights", sel_ar(a) & 0xFF);
            obs("sels: conf CStoDS limit", (WORD)GetSelectorLimit(a) == (WORD)GetSelectorLimit(s));
            FreeSelector(a);
        }
        a = (WORD)AllocDStoCSAlias(s);
        obs("sels: conf DStoCS", a != 0);
        if (a) {
            obs("sels: conf DStoCS rights", sel_ar(a) & 0xFF);
            FreeSelector(a);
        }
        dpmi_rights(s, 0x00F2);
        FreeSelector(s);
    }
    /* last: writes */
    note("PrestoChangoSelector(0, s)");
    s = AllocSelector(0);
    if (s) {
        r = (WORD)((WORD (FAR PASCAL *)(WORD, WORD))kord(177))(0, s);
        obs("sels: presto 0", r != 0);
        FreeSelector(s);
    }
    note("SetSelectorLimit(0)");
    obs("sels: set limit 0", (WORD)((WORD (FAR PASCAL *)(WORD, DWORD))kord(189))(0, 0xFFF));
    note("SetSelectorBase(0)");
    obs("sels: set base 0", (WORD)((WORD (FAR PASCAL *)(WORD, DWORD))kord(187))(0, 0x12340UL));
    d = GetSelectorBase(0);
    obs_env("sels: base 0 after lo", LOWORD(d));
    obs_env("sels: base 0 after hi", HIWORD(d));
    obs_env("sels: limit 0 after", (WORD)GetSelectorLimit(0));
}

/* ---- TK66R.EXE and TK66F.EXE: KERNEL's segments ---- */

static const WORD korder[] = { 2, 3, 1, 4 };

static void krealloc(void)
{
    static char nm[] = "krealloc: N xxxxxxxxxxxxxxxx";
    WORD i, h, k, r;
    DWORD sz;
    step("krealloc");
    for (i = 0; i < 4; i++) {
        k = korder[i];
        h = kseg_handle(k);
        nm[10] = (char)('0' + k);
        if (!h)
            continue;
        sz = GlobalSize((HGLOBAL)h);
        note("same size");
        r = (WORD)GlobalReAlloc((HGLOBAL)h, sz, 0);
        lstrcpy(nm + 12, "same");
        obs(nm, r == 0 ? 0 : r == h ? 1 : 2);
        lstrcpy(nm + 12, "same size");
        obs(nm, GlobalSize((HGLOBAL)h) == sz);
        note("modify");
        r = (WORD)GlobalReAlloc((HGLOBAL)h, 0, GMEM_MODIFY | GMEM_DISCARDABLE);
        lstrcpy(nm + 12, "modify");
        obs(nm, r == 0 ? 0 : r == h ? 1 : 2);
        lstrcpy(nm + 12, "modify flags");
        obs_env(nm, GlobalFlags((HGLOBAL)h));
        note("grow");
        r = (WORD)GlobalReAlloc((HGLOBAL)h, sz + 0x100, 0);
        lstrcpy(nm + 12, "grow");
        obs_env(nm, r == 0 ? 0 : r == h ? 1 : 2);
        lstrcpy(nm + 12, "grow size");
        obs_env(nm, GlobalSize((HGLOBAL)h) == sz ? 0 : GlobalSize((HGLOBAL)h) > sz ? 1 : 2);
        lstrcpy(nm + 12, "grow flags");
        obs_env(nm, GlobalFlags((HGLOBAL)h));
        note("moveable grow");
        r = (WORD)GlobalReAlloc((HGLOBAL)h, sz + 0x10000UL, GMEM_MOVEABLE);
        lstrcpy(nm + 12, "mgrow");
        obs(nm, r == 0 ? 0 : r == h ? 1 : 2);
        lstrcpy(nm + 12, "mgrow size");
        obs_env(nm, GlobalSize((HGLOBAL)h) == sz ? 0 : GlobalSize((HGLOBAL)h) > sz ? 1 : 2);
        note("discard");
        r = (WORD)GlobalReAlloc((HGLOBAL)h, 0, GMEM_MOVEABLE);
        lstrcpy(nm + 12, "discard");
        obs(nm, r == 0 ? 0 : r == h ? 1 : 2);
        lstrcpy(nm + 12, "discard flags");
        obs_env(nm, GlobalFlags((HGLOBAL)h));
        lstrcpy(nm + 12, "discard present");
        obs_env(nm, (sel_ar(h | 1) & 0x80) != 0);
        note("call");
        lstrcpy(nm + 12, "call");
        h = (WORD)GlobalAlloc(GMEM_MOVEABLE, 0x100);
        obs(nm, h != 0);
        GlobalFree((HGLOBAL)h);
    }
}

static void kfree(void)
{
    static char nm[] = "kfree: N xxxxxxxxxx";
    WORD i, h, k, r;
    step("kfree");
    for (i = 0; i < 4; i++) {
        k = korder[i];
        h = kseg_handle(k);
        nm[7] = (char)('0' + k);
        if (!h)
            continue;
        note("free");
        r = (WORD)GlobalFree((HGLOBAL)h);
        lstrcpy(nm + 9, "ret");
        obs(nm, r == 0 ? 0 : r == h ? 1 : 2);
        /* (3.1 freed it, and ended at the next call into it: w16 keeps
           it; what is left is noted only) */
        lstrcpy(nm + 9, "flags");
        obs_env(nm, GlobalFlags((HGLOBAL)h));
        lstrcpy(nm + 9, "size ok");
        obs_env(nm, GlobalSize((HGLOBAL)h) != 0);
        lstrcpy(nm + 9, "present");
        obs_env(nm, (sel_ar(h | 1) & 0x80) != 0);
        note("call");
        lstrcpy(nm + 9, "call");
        h = (WORD)GlobalAlloc(GMEM_MOVEABLE, 0x100);
        obs(nm, h != 0);
        GlobalFree((HGLOBAL)h);
    }
}

/* ---- TK66I.EXE: LocalInit over the instance words (medium 6) ---- */

static void linit1(const char *pfx, WORD start, WORD end, DWORD bytes)
{
    static char nm[40];
    HGLOBAL g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, bytes);
    WORD seg, n = 0, i, h[3];
    while (pfx[n]) {
        nm[n] = pfx[n];
        n++;
    }
    if (!g) {
        lstrcpy(nm + n, "no block");
        obs(nm, 1);
        return;
    }
    seg = SELECTOROF(GlobalLock(g));
    note(pfx);
    lstrcpy(nm + n, "init");
    obs(nm, LocalInit(seg, start, end) != 0);
    lstrcpy(nm + n, "word 6");
    /* (the heap's information block: where the first sentinel went;
       LocalInit(seg, 0, 0) of a 64K block noted only: 3.1's first arena
       wraps round to FFFCh) */
    (start ? obs : obs_env)(nm, *(WORD FAR *)MAKELP(seg, 6));
    dump(nm, seg, start & ~3u, 16);
    for (i = 0; i < 3; i++) {
        h[i] = (WORD)call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED, 0x10, 0);
        lstrcpy(nm + n, "alloc ");
        nm[n + 6] = (char)('1' + i);
        nm[n + 7] = 0;
        obs_env(nm, h[i]);
        lstrcpy(nm + n, "alloc ok ");
        nm[n + 9] = (char)('1' + i);
        nm[n + 10] = 0;
        obs(nm, h[i] != 0);
    }
    note("compact");
    lstrcpy(nm + n, "compact");
    obs_env(nm, (WORD)call_ds(seg, (void FAR *)LocalCompact, 1, 0xFFFF, 0, 0));
    for (i = 0; i < 3; i++)
        if (h[i])
            call_ds(seg, (void FAR *)LocalFree, 1, h[i], 0, 0);
    lstrcpy(nm + n, "alloc after free");
    obs(nm, call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED, 0x10, 0) != 0);
    GlobalUnlock(g);
    GlobalFree(g);
}

/* a heap of its own in a small block (as USER's menu and string heaps
   are: GlobalAlloc, LocalInit): how many 100h-byte LocalAllocs it takes
   (the heap growing with its block), and the block's size then */
static void lgrow(const char *pfx, WORD flags)
{
    static char nm[48];
    HGLOBAL g = GlobalAlloc((flags & ~0x8000) | GMEM_ZEROINIT, 0x400);
    WORD seg, n = 0, i, k;
    while (pfx[n]) {
        nm[n] = pfx[n];
        n++;
    }
    if (!g)
        return;
    seg = SELECTOROF(GlobalLock(g));
    note(pfx);
    if (flags & 0x8000) {
        /* wired (GlobalWire, as Microsoft's USER does its data segment),
           with blocks allocated after it in the way */
        GlobalWire(g);
        for (i = 0; i < 8; i++)
            GlobalAlloc(GMEM_FIXED, 0x100);
    }
    lstrcpy(nm + n, "init");
    obs(nm, LocalInit(seg, 0x10, 0x3FF) != 0);
    for (i = 0; i < 100; i++)
        if (!LOWORD(call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED, 0x100, 0)))
            break;
    lstrcpy(nm + n, "allocs");
    obs(nm, i);
    /* moveable ones: the handle table grows too */
    for (i = 0; i < 3000; i++)
        if (!LOWORD(call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_MOVEABLE, 4, 0)))
            break;
    /* (3.1: 94Fh, KRNL286 950h; w16 951h: within the handle tables'
       layout, which is not checked here) */
    lstrcpy(nm + n, "moveable allocs over 900h");
    obs(nm, i >= 0x900);
    lstrcpy(nm + n, "size");
    k = (WORD)(GlobalSize(g) >> 4);
    obs(nm, k);
    lstrcpy(nm + n, "selector same");
    obs(nm, SELECTOROF(GlobalLock(g)) == seg);
    GlobalUnlock(g);
    GlobalUnlock(g);
    GlobalFree(g);
}

/* LocalInit(seg, 0, size) on a small moveable block of that size, then
   rounds of three LMEM_ZEROINIT allocations of 12h, 20h and 1Ah bytes
   (what Microsoft's USER does per window in a heap of its own) until one
   fails: how many rounds, the block's size then, the last offset */
static void lpat(void)
{
    static const char nm0[] = "linit pattern: ";
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x400);
    WORD seg, i, k, last = 0;
    static const WORD sz[3] = { 0x12, 0x20, 0x1A };
    if (!g)
        return;
    seg = SELECTOROF(GlobalLock(g));
    note(nm0);
    obs("linit pattern: init", LocalInit(seg, 0, 0x400) != 0);
    /* first alloc-and-free churn: the heap must not fill up with it */
    for (i = 0; i < 3000; i++) {
        WORD h1 = LOWORD(call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED | LMEM_ZEROINIT, 0x12, 0));
        WORD h2 = LOWORD(call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED | LMEM_ZEROINIT, 0x1A, 0));
        if (!h1 || !h2)
            break;
        call_ds(seg, (void FAR *)LocalFree, 1, h1, 0, 0);
        call_ds(seg, (void FAR *)LocalFree, 1, h2, 0, 0);
    }
    obs("linit pattern: churn rounds", i);
    obs_env("linit pattern: size after churn", (WORD)(GlobalSize(g) >> 4));
    for (i = 0; i < 2000; i++) {
        for (k = 0; k < 3; k++) {
            WORD h = LOWORD(call_ds(seg, (void FAR *)LocalAlloc, 2, LMEM_FIXED | LMEM_ZEROINIT, sz[k], 0));
            if (!h)
                break;
            last = h;
        }
        if (k < 3)
            break;
    }
    obs("linit pattern: rounds over 300h", i >= 0x300);
    obs_env("linit pattern: rounds", i);
    obs_env("linit pattern: last", last);
    obs("linit pattern: size", (WORD)(GlobalSize(g) >> 4));
    GlobalUnlock(g);
    GlobalFree(g);
}

static void linit(void)
{
    step("linit");
    linit1("linit 8: ", 8, 0x3FF, 0x400);
    linit1("linit 4: ", 4, 0x3FF, 0x400);
    linit1("linit 1: ", 1, 0x3FF, 0x400);
    linit1("linit 0 0: ", 0, 0, 0x10000UL);
    /* the end at the block's size (Microsoft's COMPOBJ.DLL, OLE 2's task
       allocator: LocalInit(sel, 22h, 1000h) on a 1000h-byte block; w16
       refused it), then one byte past it */
    linit1("linit end=size: ", 0x22, 0x1000, 0x1000);
    linit1("linit end>size: ", 0x22, 0x1001, 0x1000);
    lgrow("linit grow moveable: ", GMEM_MOVEABLE);
    lgrow("linit grow wired: ", GMEM_MOVEABLE | 0x8000);
    lpat();
}

/* ---- TK66N.EXE: a named type's name past the module table (medium 16) ---- */

static void badnames(void)
{
    UINT h;
    step("badnames");
    note("LoadLibrary TKBADNM.DLL");
    h = (UINT)LoadLibrary("TKBADNM.DLL");
    obs("badnames: load", h < 32 ? h : 0x20);
    if (h < 32)
        return;
    note("FindResource by type name");
    obs("badnames: find named", FindResource((HINSTANCE)h, MAKEINTRESOURCE(1), "NAMEDT") != 0);
    note("FindResource by type number");
    obs("badnames: find number", FindResource((HINSTANCE)h, MAKEINTRESOURCE(1), RT_RCDATA) != 0);
    FreeLibrary((HINSTANCE)h);
}

int __cdecl tmain(void)
{
    WORD n;
    DWORD f = GetWinFlags();
    use386 = (f & WF_CPU286) == 0;
    out("TK66 start\r\n");
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), buf, sizeof buf);
    if (n > 5 && buf[n - 5] >= 'A' && buf[n - 5] <= 'Z')
        scenario = buf[n - 5];
    switch (scenario) {
    case 'L':
        klstrcmp();
        break;
    case 'S':
        sels();
        break;
    case 'R':
        krealloc();
        break;
    case 'F':
        kfree();
        break;
    case 'I':
        linit();
        break;
    case 'N':
        badnames();
        break;
    case 'E':
        esweep();
        break;
    default:
        kmod();
        drivetype();
        profile();
        lres();
        kseg2();
        entries();
        break;
    }
    out("TK66 done\r\n");
    return failures;
}
