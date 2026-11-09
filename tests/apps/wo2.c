/*
 * wo2.c - WO2.EXE, a protected-mode interrupt spy for the WINOLDAP work
 * (ARCHITECTURE.md 9.6). Black-box observation of Windows 3.1 Standard
 * mode: started from WIN.INI load= (tests/worun.sh -n windows:load=C:\WO2.EXE),
 * it chains protected-mode INT 2Fh, INT 31h and (some functions of) INT 21h
 * through DPMI (INT 31h 0204h/0205h, wo2s.asm) and records every call made
 * after it: registers in and out, the caller's CS:IP and the module owning
 * that code segment (TOOLHELP). Twice a second the records are appended to
 * C:\WO2.LOG, so whatever WINOLDAP.MOD (or anything else) asks of DOSX,
 * WSWAP and WIN.COM through these interrupts is seen.
 * Command tail: letters selecting the hooks: f = INT 2Fh, d = INT 31h,
 * t = INT 21h (default "fdt"; the functions listed below), a = every
 * INT 21h function but read, write and seek. INT 2Fh 1680h/1689h (idle) are counted only.
 * Without a tail, the letters come from C:\WO2.INI [wo2] hooks=.
 * At start it also records GetWinOldApHooks (KERNEL.344) and what it points
 * to, GetWinFlags.
 */
#include <windows.h>
#include <toolhelp.h>
#include <conio.h>

void _disable(void);
void _enable(void);
#pragma aux _disable = "cli";
#pragma aux _enable = "sti";

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern WORD __cdecl get_0040h(void);
extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern DWORD FAR PASCAL GETWINOLDAPHOOKS(void);
extern WORD FAR PASCAL AllocCStoDSAlias(WORD sel);   /* KERNEL.170 */

extern void __cdecl hook_setup(WORD alias, WORD dgroup);
extern WORD __cdecl hook_install(WORD alias, WORD intno);
extern void __cdecl hook_remove(WORD alias, WORD intno);
extern WORD __cdecl bufsel, bufpos, dropped, n1680, sel40;
extern BYTE __cdecl f21[32], f31[32];
extern WORD __cdecl watchcs, blksel, blkoff, snapsel, nsnap, blk_dx, blk_ax, linsel;
extern void __cdecl snap_now(WORD off);
#define LINSZ 0x400
#define SNAPSZ (BLKSZ + LINSZ)
extern void __cdecl get_block(void);
#define BLKSZ 0x540
static WORD snaps_done;

#define REC 23
#define MAXPEND 64
static WORD pend[MAXPEND], npend;
#define I_BX 1
#define I_ES 6

static char line[400];
static WORD done_pos;
static HFILE lf = HFILE_ERROR;

static void lopen(void)
{
    OFSTRUCT of;
    lf = OpenFile("C:\\WO2.LOG", &of, OF_WRITE);
    if (lf == HFILE_ERROR)
        lf = OpenFile("C:\\WO2.LOG", &of, OF_CREATE | OF_WRITE);
    if (lf != HFILE_ERROR)
        _llseek(lf, 0, 2);
}

static void lclose(void)
{
    if (lf != HFILE_ERROR)
        _lclose(lf);
    lf = HFILE_ERROR;
}

static void out(const char *s)
{
    if (lf != HFILE_ERROR)
        _lwrite(lf, s, lstrlen(s));
}

static WORD last_cs;
static char last_name[12];

static void owner(WORD cs, char *name)
{
    GLOBALENTRY ge;
    MODULEENTRY me;
    if (cs == last_cs) {
        lstrcpy(name, last_name);
        return;
    }
    lstrcpy(name, "?");
    ge.dwSize = sizeof ge;
    me.dwSize = sizeof me;
    if (GlobalEntryHandle(&ge, (HGLOBAL)GlobalHandle(cs))) {
        if (ModuleFindHandle(&me, (HMODULE)ge.hOwner)) {
            lstrcpy(name, me.szModule);
            wsprintf(name + lstrlen(name), ".%d", ge.wData);
        } else
            wsprintf(name, "own%04X", ge.hOwner);
    }
    last_cs = cs;
    lstrcpy(last_name, name);
}

static int snap_b_done(void)
{
    /* the INT 21h AH=0Bh after the last INT 2Fh 4B80h (inside which the
       switch happens) has returned */
    WORD p = done_pos;
    while (p >= REC * 2) {
        WORD FAR *r;
        p -= REC * 2;
        r = (WORD FAR *)MAKELP(bufsel, p);
        if (r[0] == 0x21 && (r[1] >> 8) == 0x0B && r[10] == watchcs)
            return r[22] == 2;
    }
    return 1;
}

static void hexblock(const char *title, const BYTE FAR *p, int size)
{
    int i, j;
    out(title);
    out("\r\n");
    for (i = 0; i < size; i += 32) {
        wsprintf(line, " %04X:", i);
        out(line);
        for (j = 0; j < 32; j++) {
            wsprintf(line, "%s%02X", (LPSTR)((j % 8) ? "" : " "), p[i + j]);
            out(line);
        }
        out("\r\n");
    }
}

BOOL FAR PASCAL __export EnumProc(HWND h, LPARAM lp)
{
    char t[64], c[32];
    (void)lp;
    t[0] = c[0] = 0;
    GetWindowText(h, t, sizeof t);
    GetClassName(h, c, sizeof c);
    wsprintf(line, "  hwnd %04X task %04X style %08lX class %s title [%s]\r\n", h,
             GetWindowTask(h), GetWindowLong(h, GWL_STYLE), (LPSTR)c, (LPSTR)t);
    out(line);
    return TRUE;
}

static void windows_now(void)
{
    static FARPROC fp;
    if (!fp)
        fp = MakeProcInstance((FARPROC)EnumProc, (HINSTANCE)init_regs[5]);
    wsprintf(line, "-- windows (active %04X, focus %04X):\r\n", GetActiveWindow(), GetFocus());
    out(line);
    EnumWindows((WNDENUMPROC)fp, 0);
}

static void snapdump(WORD n)
{
    const BYTE FAR *p = (const BYTE FAR *)MAKELP(snapsel, n * 2 * SNAPSZ);
    wsprintf(line, "== block (%04X:%04X) at INT 2Fh 4B80h #%u:", blk_dx, blk_ax, n);
    hexblock(line, p, BLKSZ);
    wsprintf(line, "-- %u bytes at its 185h then:", LINSZ);
    hexblock(line, p + BLKSZ, LINSZ);
    wsprintf(line, "== block when it returned #%u:", n);
    hexblock(line, p + SNAPSZ, BLKSZ);
    wsprintf(line, "-- %u bytes at its 185h then:", LINSZ);
    hexblock(line, p + SNAPSZ + BLKSZ, LINSZ);
}

static void outregs(WORD FAR *r)
{
    wsprintf(line, "              out ax %04X bx %04X cx %04X dx %04X si %04X di %04X ds %04X es %04X fl %04X\r\n",
             r[11], r[12], r[13], r[14], r[15], r[16], r[17], r[18], r[19]);
    out(line);
}

static void flush(void)
{
    WORD FAR *r;
    WORD pos = bufpos, i;
    char name[40];
    for (i = 0; i < npend; i++) {
        r = (WORD FAR *)MAKELP(bufsel, pend[i]);
        if (r[22] != 2)
            continue;
        if (lf == HFILE_ERROR)
            lopen();
        wsprintf(line, "#%u returned (t%04X now)\r\n", pend[i] / (REC * 2), *(WORD FAR *)MAKELP(sel40, 0x6C));
        out(line);
        outregs(r);
        pend[i--] = pend[--npend];
    }
    if (pos == done_pos && lf == HFILE_ERROR)
        return;
    if (lf == HFILE_ERROR)
        lopen();
    while (done_pos < pos) {
        r = (WORD FAR *)MAKELP(bufsel, done_pos);
        owner(r[10], name);
        wsprintf(line, "#%u t%04X INT %02X in  ax %04X bx %04X cx %04X dx %04X si %04X di %04X ds %04X es %04X fl %04X from %04X:%04X %s\r\n",
                 done_pos / (REC * 2), r[20], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[21], r[10], r[9], (LPSTR)name);
        out(line);
        if (r[22] == 2)
            outregs(r);
        else if (npend < MAXPEND) {
            out("              (still running)\r\n");
            pend[npend++] = done_pos;
        }
        done_pos += REC * 2;
    }
    while (snaps_done < nsnap) {
        if (snaps_done == nsnap - 1 && !snap_b_done())
            break;
        snapdump(snaps_done);
        windows_now();
        snaps_done++;
    }
    wsprintf(line, "-- dropped %u idle %u\r\n", dropped, n1680);
    out(line);
    lclose();
}

static void header(LPSTR tail)
{
    DWORD h = GETWINOLDAPHOOKS();
    WORD FAR *p;
    int i;
    lopen();
    wsprintf(line, "== WO2 tail [%s] winflags %08lX GetWinOldApHooks %08lX\r\n",
             tail, GetWinFlags(), h);
    out(line);
    if (h && !IsBadReadPtr((void FAR *)h, 32)) {
        p = (WORD FAR *)h;
        out("hooks:");
        for (i = 0; i < 16; i++) {
            wsprintf(line, " %04X", p[i]);
            out(line);
        }
        out("\r\n");
    }
    lclose();
}

void __cdecl __far tmain(void)
{
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    LPSTR tail = (LPSTR)MAKELP(init_regs[I_ES], init_regs[I_BX]);
    WORD alias, seg;
    HGLOBAL hb;
    MSG msg;
    int f = 1, d = 1, t = 1, all21 = 0;
    char tl[64];
    int n = 0;

    K_WAITEVENT(0);
    U_INITAPP((WORD)hinst);
    while (tail[n] && tail[n] != '\r' && n < 63) {
        tl[n] = tail[n];
        n++;
    }
    tl[n] = 0;
    if (n == 0)                 /* WIN.INI load= has no arguments */
        n = GetPrivateProfileString("wo2", "hooks", "", tl, sizeof tl, "C:\\WO2.INI");
    if (n > 0) {
        f = d = t = 0;
        for (n = 0; tl[n]; n++) {
            if (tl[n] == 'f') f = 1;
            if (tl[n] == 'd') d = 1;
            if (tl[n] == 't') t = 1;
            if (tl[n] == 'a') t = all21 = 1;
        }
    }
    header(tl);
    sel40 = get_0040h();
    hb = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 65000L);
    bufsel = SELECTOROF(GlobalLock(hb));
    /* INT 21h functions worth seeing (file I/O is in the emulator's log) */
    {
        static const BYTE fns[] = { 0x0E, 0x25, 0x26, 0x31, 0x33, 0x34, 0x35, 0x3B,
                                    0x44, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x50, 0x51,
                                    0x52, 0x55, 0x58, 0x5D, 0x62 };
        for (n = 0; n < sizeof fns; n++)
            f21[fns[n] >> 3] |= 1 << (fns[n] & 7);
        /* INT 31h: all but the descriptor and memory services */
        for (n = 0; n < 32; n++)
            f31[n] = 0xFF;
        f31[0] &= ~1;           /* 00xxh */
        f31[0] &= ~0x20;        /* 05xxh */
        f31[0] &= ~0x40;        /* 06xxh */
        f31[0] &= ~0x80;        /* 07xxh */
        if (all21) {
            for (n = 0; n < 32; n++)
                f21[n] = 0xFF;
            f21[0x3F >> 3] &= ~(1 << (0x3F & 7));       /* read, write, seek */
            f21[0x40 >> 3] &= ~(1 << (0x40 & 7));
            f21[0x42 >> 3] &= ~(1 << (0x42 & 7));
        }
    }
    /* WIN.COM's switcher block, for the snapshots at INT 2Fh 4B80h */
    get_block();
    if (blksel) {
        HGLOBAL hs = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 12L * 2 * SNAPSZ + SNAPSZ);
        snapsel = SELECTOROF(GlobalLock(hs));
    }
    lopen();
    wsprintf(line, "block %04X:%04X (selector %04X)\r\n", blk_dx, blk_ax, blksel);
    out(line);
    if (snapsel) {
        /* the block as it is now, before any DOS application ran */
        snap_now(12 * 2 * SNAPSZ);
        hexblock("== block at start:", (const BYTE FAR *)MAKELP(snapsel, 12 * 2 * SNAPSZ), BLKSZ);
    }
    lclose();
    seg = SELECTOROF((void FAR *)hook_setup);
    alias = AllocCStoDSAlias(seg);
    hook_setup(alias, SELECTOROF((void FAR *)&bufsel));
    lopen();
    if (f) { wsprintf(line, "hook 2F: %u\r\n", hook_install(alias, 0x2F)); out(line); }
    if (d) { wsprintf(line, "hook 31: %u\r\n", hook_install(alias, 0x31)); out(line); }
    /* INT 21h always: it keeps the INT 2Fh hook first (wo2s.asm) */
    wsprintf(line, "hook 21: %u\r\n", hook_install(alias, 0x21)); out(line);
    lclose();
    SetTimer(NULL, 1, 500, NULL);
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_TIMER)
            flush();
        DispatchMessage(&msg);
    }
    flush();
    hook_remove(alias, 0x21);
    if (d) hook_remove(alias, 0x31);
    if (f) hook_remove(alias, 0x2F);
}
