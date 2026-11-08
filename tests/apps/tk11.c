/*
 * tk11.c - the in-memory module table (what an hModule points to) of
 * TK11.EXE, TKDLL.DLL (imported), TKDLB.DLL (imported by TKDLL) and
 * KERNEL.
 *
 * The fields probed are those of the layout published in Pietrek,
 * "Windows Internals" (1993), Table 3-1; this probe checks them black-box
 * against Windows 3.1 before w16 relies on them.
 *
 * Needs the same files as TK5 (tests/tk5ref.sh TK11.EXE puts them in
 * place). Output and checks: see tklog.h; reference values in tk11ref.h.
 */
#include <windows.h>

#define TK_REF "tk11ref.h"
#include "tklog.h"

extern WORD FAR PASCAL DLL_INFO(WORD i);        /* TKDLL.DLL */

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static WORD W(WORD sel, WORD off)
{
    return *w_at(sel, off);
}

static BYTE B(WORD sel, WORD off)
{
    return *(BYTE FAR *)MAKELP(sel, off);
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static int same_ci(const char FAR *a, const char FAR *b)
{
    while (*a && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return upper(*a) == upper(*b);
}

/* is the length-prefixed string at sel:off equal to s */
static int pstr_is(WORD sel, WORD off, const char *s)
{
    BYTE n = B(sel, off), i;
    for (i = 0; i < n; i++)
        if (!s[i] || upper((char)B(sel, off + 1 + i)) != s[i])
            return 0;
    return s[n] == 0;
}

static void name(char *d, const char *a, const char *b)
{
    while (*a)
        *d++ = *a++;
    *d++ = ' ';
    while ((*d++ = *b++) != 0)
        ;
}

/* the in-memory entry table (offset 04h): bundles of {first ordinal - 1,
   last ordinal, next bundle}, each followed by 5-byte entries {type,
   flags, segment, offset}. Finds ordinal `ord`. */
static int entry_of(WORD m, WORD ord, BYTE *type, BYTE *flags, BYTE *seg, WORD *off)
{
    WORD b = W(m, 4), guard = 0;
    while (b && guard++ < 1000) {
        WORD first = W(m, b), last = W(m, b + 2);
        if (ord > first && ord <= last) {
            WORD e = b + 6 + 5 * (ord - first - 1);
            *type = B(m, e);
            *flags = B(m, e + 1);
            *seg = B(m, e + 2);
            *off = W(m, e + 3);
            return 1;
        }
        b = W(m, b + 4);
    }
    return 0;
}

/* exact: the values depend only on the module's file (not for KERNEL,
   whose own file and segments are its implementation's business); the
   segment flags are never compared (bits 0400h and F000h change in ways
   not understood) */
static void probe_module(const char *tag, HMODULE hm, WORD inst, int exact)
{
    WORD m = (WORD)hm, nseg, segtab, dg, lf, i, n;
    char nm[80], path[128];
    void (*ov)(const char *, WORD) = exact ? obs : obs_env;

    name(nm, tag, "[0]");
    obs(nm, W(m, 0));
    name(nm, tag, "usage==GetModuleUsage");
    obs(nm, W(m, 2) == (WORD)GetModuleUsage((HINSTANCE)m));
    name(nm, tag, "flags");
    ov(nm, W(m, 0x0C));
    name(nm, tag, "autodata");
    ov(nm, W(m, 0x0E));
    name(nm, tag, "heap");
    ov(nm, W(m, 0x10));
    name(nm, tag, "stack");
    ov(nm, W(m, 0x12));
    name(nm, tag, "CS:IP seg");
    ov(nm, W(m, 0x16));
    name(nm, tag, "CS:IP off");
    ov(nm, W(m, 0x14));
    name(nm, tag, "SS:SP seg");
    ov(nm, W(m, 0x1A));
    name(nm, tag, "SS:SP off");
    obs_env(nm, W(m, 0x18));
    nseg = W(m, 0x1C);
    name(nm, tag, "segments");
    ov(nm, nseg);
    name(nm, tag, "modrefs");
    ov(nm, W(m, 0x1E));
    segtab = W(m, 0x22);
    name(nm, tag, "segtab");
    obs_env(nm, segtab);
    name(nm, tag, "rsrctab-segtab==10*segments");
    obs(nm, W(m, 0x24) - segtab == 10 * nseg);
    name(nm, tag, "resident name 0 is module name");
    obs(nm, pstr_is(m, W(m, 0x26), tag));
    name(nm, tag, "[38h]==impnames");
    obs(nm, W(m, 0x38) == W(m, 0x2A));
    name(nm, tag, "[3Ah]==impnames");
    ov(nm, W(m, 0x3A) == W(m, 0x2A));
    name(nm, tag, "first imported name byte");
    obs(nm, B(m, W(m, 0x2A)));
    name(nm, tag, "next module");
    obs_env(nm, W(m, 6));

    /* segment table: 10-byte entries, handle in the last word */
    for (i = 0; i < nseg && i < 4; i++) {
        WORD e = segtab + 10 * i, h = W(m, e + 8);
        char t[40];
        name(nm, tag, "seg");
        lstrcpy(t, nm);
        t[lstrlen(t) + 1] = 0;
        t[lstrlen(t)] = (char)('1' + i);
        name(nm, t, "flags");
        obs_env(nm, W(m, e + 4));
        name(nm, t, "handle");
        obs_env(nm, h);
        name(nm, t, "handle==GlobalHandle");
        ov(nm, h && (WORD)GlobalHandle(h | 1) == h);
    }

    /* DGROUP's segment table entry */
    dg = W(m, 8);
    name(nm, tag, "[08h]==segtab entry of autodata");
    obs(nm, W(m, 0x0E) ? dg == segtab + 10 * (W(m, 0x0E) - 1) : dg == 0);
    if (W(m, 0x0E)) {
        name(nm, tag, "DGROUP entry handle==hInstance");
        ov(nm, W(m, dg + 8) == inst);
    }

    /* load file information */
    lf = W(m, 0x0A);
    name(nm, tag, "loadfile length byte");
    obs_env(nm, B(m, lf));
    name(nm, tag, "loadfile fixed media");
    obs(nm, B(m, lf + 1));
    n = GetModuleFileName((HINSTANCE)m, path, sizeof path);
    name(nm, tag, "loadfile name==GetModuleFileName");
    obs(nm, n && same_ci((const char FAR *)MAKELP(m, lf + 8), path));
    name(nm, tag, "loadfile length - strlen(name)");
    obs(nm, B(m, lf) - n);
}

int __cdecl tmain(void)
{
    HMODULE hk, hd, hb, hs;
    WORD m, n, found_d, found_s, guard;
    BYTE type, flags, seg;
    WORD off;
    FARPROC p;

    out("TK11 start\r\n");
    hk = GetModuleHandle("KERNEL");
    hd = GetModuleHandle("TKDLL");
    hb = GetModuleHandle("TKDLB");
    hs = GetModuleHandle("TK11");
    (void)DLL_INFO(0);

    probe_module("TKDLL", hd, DLL_INFO(26) & ~1u, 1);
    probe_module("TKDLB", hb, (WORD)LoadLibrary("TKDLB.DLL"), 1);
    FreeLibrary(GetModuleHandle("TKDLB"));
    probe_module("TK11", hs, hinst(), 1);
    probe_module("KERNEL", hk, 0, 0);

    /* module list through offset 06h, from KERNEL */
    n = found_d = found_s = 0;
    for (m = (WORD)hk, guard = 0; m && guard < 200; m = W(m, 6), guard++) {
        if (m == (WORD)hd)
            found_d = 1;
        if (m == (WORD)hs)
            found_s = 1;
        n++;
    }
    obs("list from KERNEL ends", guard < 200);
    obs("list from KERNEL has TKDLL", found_d);
    obs("list from KERNEL has TK11", found_s);
    obs_env("modules in list", n);

    /* modules TKDLL imports from (offset 28h: hModules) */
    m = (WORD)hd;
    obs("TKDLL modref 1==TKDLB", W(m, W(m, 0x28)) == (WORD)hb);
    obs("TKDLL modref 2==KERNEL", W(m, W(m, 0x28) + 2) == (WORD)hk);

    /* entry table */
    p = GetProcAddress(hd, "DLL_ADD");
    obs("TKDLL entry 2 found", entry_of(m, 2, &type, &flags, &seg, &off));
    obs("TKDLL entry 2 type", type);
    obs("TKDLL entry 2 flags", flags);
    obs("TKDLL entry 2 segment", seg);
    obs("TKDLL entry 2 offset==GetProcAddress", off == (WORD)(DWORD)p);
    obs("TKDLL entry 10 found", entry_of(m, 10, &type, &flags, &seg, &off));
    obs("TKDLL entry 11 (WEP) found", entry_of(m, 11, &type, &flags, &seg, &off));
    obs("TKDLL entry table first bundle first", W(m, W(m, 4)));
    obs("TKDLL entry table first bundle last", W(m, W(m, 4) + 2));
    obs("TKDLL entry 1 found", entry_of(m, 1, &type, &flags, &seg, &off));
    obs("TK11 entry 1 found", entry_of((WORD)hs, 1, &type, &flags, &seg, &off));

    out("TK11 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
