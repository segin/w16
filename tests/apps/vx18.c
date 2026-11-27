/*
 * vx18.c - VX18.EXE: virtual memory as a Windows program sees it in 386
 * Enhanced mode (ARCHITECTURE.md 12.17): what Windows 3.1's WIN386
 * reports about its paging (swap) file, run as the shell (tests/vmprobe.sh,
 * tests/refrun.sh VX18.EXE /3) under Windows 3.1's WIN386 and under w16's,
 * with SYSTEM.INI's [386Enh] paging settings varied by the script. Logs to
 * TK.LOG:
 *   flags   GetWinFlags (WF_PAGING, 0800h, among them)
 *   free    GetFreeSpace(0), in KB (DX:AX / 1024)
 *   dpmi    DPMI 0400h's BX (bit 2: virtual memory) and the twelve dwords
 *           of 0500h (free memory information), each as two words
 *   pf      the PageFile device's (0021h) protected-mode API: its entry
 *           (INT 2Fh 1684h) and function 0 (the version). (Function 1
 *           with zero registers ended Windows 3.1 when it had a paging
 *           file; with none it gave AX 0, function 2 AX 2)
 *   swp     the temporary paging file WIN386.SWP in the Windows directory
 *           (or C:\ when [386Enh] PagingFile/PagingDrive moved it, the
 *           script's scenarios): found or not, its size in KB
 *   disk    the free space of drive C: in KB (INT 21h 36h)
 *   big     DPMI 0501h of more than the physical memory (0500h's total
 *           physical pages plus 1 MB), every page written with its own
 *           number, then read back (the count of pages that read back
 *           wrong: 0 when the VMM pages to its file), then 0502h
 * Black box: only what the calls return (ARCHITECTURE.md 3).
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern void __cdecl __far vx_int2f(WORD __far *regs);
extern void __cdecl __far vx_call(void __far *entry, WORD __far *regs);
extern void __cdecl __far vx_int31(WORD __far *regs);
extern void __cdecl __far vx_int21(WORD __far *regs);

/* regs: ax bx cx dx si di es flags */
static WORD r[8];
static DWORD mi[12];
static BYTE dta[64];

static void clr(void)
{
    int i;
    for (i = 0; i < 8; i++) r[i] = 0;
}

static void obsd(const char *name, DWORD v)
{
    out("OBS ~"); out(name); out("=");
    outhex(HIWORD(v)); outhex(LOWORD(v)); out("\r\n");
}

static void obsr(const char *name)
{
    out("OBS ~"); out(name); out("=ax:"); outhex(r[0]);
    out(" bx:"); outhex(r[1]); out(" cx:"); outhex(r[2]); out(" dx:"); outhex(r[3]);
    out(" si:"); outhex(r[4]); out(" di:"); outhex(r[5]); out(" es:"); outhex(r[6]);
    out(" cf:"); outhex(r[7] & 1); out("\r\n");
}

/* the size in KB of the file at path (0: none), via FindFirst */
static void swp(const char *name, const char *path)
{
    clr();
    r[0] = 0x1A00;
    r[3] = OFFSETOF((void FAR *)dta);
    vx_int21(r);
    clr();
    r[0] = 0x4E00;
    r[2] = 0x27;                        /* hidden, system, read-only too */
    r[3] = OFFSETOF((void FAR *)path);
    vx_int21(r);
    if (r[7] & 1) { obsd(name, 0xFFFFFFFFUL); return; }
    obsd(name, *(DWORD FAR *)(dta + 26) / 1024);
}

static char path[144];

/* a * b without the C library's long multiply */
static DWORD mul(DWORD a, WORD b)
{
    DWORD m = 0;
    while (b) {
        if (b & 1) m += a;
        a <<= 1;
        b >>= 1;
    }
    return m;
}

int __cdecl tmain(void)
{
    int i, n;
    DWORD v;
    static char nm[16];
    void __far *pf;

    out("VX18 virtual memory\r\n");
    obsd("flags", GetWinFlags());
    obs("wf_paging", (GetWinFlags() & 0x0800) != 0);   /* (the processor bits differ: w16's DPMI 0400h gives a 386) */
    obsd("free", GetFreeSpace(0) / 1024);
    clr(); r[0] = 0x0400; vx_int31(r);
    obs("dpmi_0400_bx", r[1]);
    clr(); r[0] = 0x0500; r[5] = OFFSETOF((void FAR *)mi); r[6] = SELECTOROF((void FAR *)mi);
    for (i = 0; i < 12; i++) mi[i] = 0;
    vx_int31(r);
    for (i = 0; i < 12; i++) {
        lstrcpy(nm, "dpmi_0500_"); nm[10] = (char)('0' + i / 10); nm[11] = (char)('0' + i % 10); nm[12] = 0;
        obsd(nm, mi[i]);
    }
    /* (the paging file's size depends on the linear address space, which
     * depends on each WIN386's own size, but for a small file set by
     * MaxPagingFileSize: compared then) */
    obs("paging_file", mi[8] != 0);
    obs("paging_file_small", (WORD)(mi[8] <= 0x400 ? mi[8] : 0xFFFF));
    clr(); r[0] = 0x1684; r[1] = 0x0021; vx_int2f(r);
    pf = (r[5] || r[6]) ? (void __far *)MAKELONG(r[5], r[6]) : NULL;
    obs("pf_entry", pf != NULL);
    for (i = 0; pf && i < 1; i++) {
        clr(); r[0] = (WORD)i; vx_call(pf, r);
        lstrcpy(nm, "pf_000"); nm[6] = (char)('0' + i); nm[7] = 0;
        obsr(nm);
    }
    GetWindowsDirectory(path, sizeof path - 16);
    n = lstrlen(path);
    if (n && path[n - 1] != '\\') path[n++] = '\\';
    lstrcpy(path + n, "WIN386.SWP");
    swp("swp_windir", path);
    swp("swp_root", "C:\\WIN386.SWP");
    swp("swp_other", "C:\\PAGE.SWP");
    swp("spart_root", "C:\\386SPART.PAR");
    clr(); r[0] = 0x3600; r[3] = 3; vx_int21(r);
    obsd("disk", mul(mul(r[0], r[2]) >> 10, r[1]));
    /* more than the physical memory, written and read back */
    v = mi[6] + 256;                    /* pages */
    clr(); r[0] = 0x0501; r[1] = HIWORD(v << 12); r[2] = LOWORD(v << 12);
    vx_int31(r);
    obs("big_cf", r[7] & 1);
    if (!(r[7] & 1)) {
        DWORD lin = MAKELONG(r[2], r[1]), hnd = MAKELONG(r[5], r[4]), bad = 0, p;
        WORD sel = AllocSelector(SELECTOROF((void FAR *)mi));
        for (p = 0; p < v; p++) {
            SetSelectorBase(sel, lin + (p << 12));
            SetSelectorLimit(sel, 4095);
            *(DWORD FAR *)MAKELP(sel, 0) = p;
            *(DWORD FAR *)MAKELP(sel, 4092) = ~p;
        }
        for (p = 0; p < v; p++) {
            SetSelectorBase(sel, lin + (p << 12));
            if (*(DWORD FAR *)MAKELP(sel, 0) != p || *(DWORD FAR *)MAKELP(sel, 4092) != ~p) bad++;
        }
        FreeSelector(sel);
        obsd("big_bad", bad);
        clr(); r[0] = 0x0502; r[4] = HIWORD(hnd); r[5] = LOWORD(hnd);
        vx_int31(r);
        obs("big_free_cf", r[7] & 1);
    }
    out("VX18 done\r\n");
    return 0;
}
