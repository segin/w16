/*
 * md2ax.c - MD2's direct experiments (included by tests/apps/md2a.c).
 *
 * GetSetMouseData's argument shape comes from MS3's record of Microsoft
 * Mouse 8.20's POINT.EXE, POINTLIB.DLL and POINTER.EXE calling it
 * (tests/apps/md2logs/a/notes.md): Pascal (LPVOID lpData, DWORD dwFlags); on
 * the stack w0 w1 = dwFlags, w2 w3 = lpData.
 *   mode 1: which bytes of the buffer each flag bit fills (a get), the
 *           result with registers preloaded (is DX a result?)
 *   mode 2: PowerEventProc with candidate arguments; MS2's log after each
 *   mode 4: set/get round trips, then input with each setting (MD1's
 *           event procedure records what the driver reports)
 */
extern BYTE __far __cdecl md_code[];
extern WORD __far __cdecl md_len;
extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);

static BYTE gbuf[96];

static void clr(void)
{
    WORD i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

static DWORD gsmd(DWORD fl, BYTE FAR *b)
{
    clr();
    args[12] = SELECTOROF(b);
    args[13] = OFFSETOF(b);
    args[14] = HIWORD(fl);
    args[15] = LOWORD(fl);
    return md_call(p5, args, res);
}

static void fill(BYTE v)
{
    WORD i;
    for (i = 0; i < sizeof gbuf; i++)
        gbuf[i] = v;
}

/* the buffer, bytes still equal to v shown as .. */
static void showbuf(const char *tag, BYTE v, WORD n)
{
    WORD i;
    out(tag);
    for (i = 0; i < n; i++) {
        if (gbuf[i] == v)
            out(" ..");
        else {
            wsprintf(line, " %02X", gbuf[i]);
            out(line);
        }
    }
    out("\r\n");
}

static BYTE aux1[256], aux2[256], aux3[256];

/* the buffer CCh, but +16 and +20 far pointers to aux1 and aux2 (CCh):
   POINTLIB's buffer has two far pointers there and the driver follows them
   (a CCh-filled buffer with flag bit 10 ended MD2: build/md2/scan-ms) */
static void pfill(void)
{
    WORD i;
    fill(0xCC);
    for (i = 0; i < 256; i++)
        aux1[i] = aux2[i] = aux3[i] = 0xCC;
    *(BYTE FAR * FAR *)(gbuf + 16) = (BYTE FAR *)aux1;
    *(BYTE FAR * FAR *)(gbuf + 20) = (BYTE FAR *)aux2;
    *(BYTE FAR * FAR *)(gbuf + 25) = (BYTE FAR *)aux3;
}

static void showaux(const char *tag, BYTE *a)
{
    WORD i, last = 0;
    for (i = 0; i < 256; i++)
        if (a[i] != 0xCC)
            last = i + 1;
    if (!last)
        return;
    out(tag);
    for (i = 0; i < last; i++) {
        if (a[i] == 0xCC)
            out(" ..");
        else {
            wsprintf(line, " %02X", a[i]);
            out(line);
        }
    }
    out("\r\n");
}

static void scan(void)
{
    WORD k;
    DWORD r;
    out("-- mode 1: one flag bit at a time; buffer CCh but +16, +20 far pointers to aux1, aux2 (CCh)\r\n");
    wsprintf(line, "~ buf %04X:%04X aux1 %04X:%04X aux2 %04X:%04X\r\n", SELECTOROF((BYTE FAR *)gbuf),
             OFFSETOF((BYTE FAR *)gbuf), SELECTOROF((BYTE FAR *)aux1), OFFSETOF((BYTE FAR *)aux1),
             SELECTOROF((BYTE FAR *)aux2), OFFSETOF((BYTE FAR *)aux2));
    out(line);
    for (k = 1; k < 32; k++) {
        wsprintf(line, "calling bit %u\r\n", k);
        out(line);
        pfill();
        r = gsmd(1L << k, gbuf);
        wsprintf(line, "G bit%-2u %08lX ret=%04X:%04X pop=%u buf:", k, 1L << k, HIWORD(r), LOWORD(r), res[0]);
        showbuf(line, 0xCC, 48);
        showaux("  aux1:", aux1);
        showaux("  aux2:", aux2);
        showaux("  aux3:", aux3);
    }
    pfill();
    r = gsmd(0xFFFFFFFEL, gbuf);
    wsprintf(line, "G all FFFFFFFE ret=%04X:%04X buf:", HIWORD(r), LOWORD(r));
    showbuf(line, 0xCC, 64);
    showaux("  aux1:", aux1);
    showaux("  aux2:", aux2);
    pfill();
    r = gsmd(0, gbuf);
    wsprintf(line, "G none 0 ret=%04X:%04X buf:", HIWORD(r), LOWORD(r));
    showbuf(line, 0xCC, 48);
    /* is DX a result? the same calls with registers preloaded */
    for (k = 0; k < 4; k++) {
        static const DWORD fls[4] = { 0x10, 0x0, 0x4000, 0x20 };
        WORD i;
        pfill();
        clr();
        args[12] = SELECTOROF((BYTE FAR *)gbuf);
        args[13] = OFFSETOF((BYTE FAR *)gbuf);
        args[14] = HIWORD(fls[k]);
        args[15] = LOWORD(fls[k]);
        for (i = 0; i < 6; i++)
            regs[i] = 0x1111 * (i + 1);
        regs[6] = SELECTOROF((BYTE FAR *)gbuf);
        md_rcall(p5, args, regs);
        wsprintf(line, "R flags %08lX in AX=1111 BX=2222 CX=3333 DX=4444 SI=5555 DI=6666 -> "
                 "AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X fl=%04X pop=%u\r\n",
                 fls[k], regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[7], regs[8]);
        out(line);
    }
}

/* ---- mode 2: PowerEventProc ---- */
static WORD ms2n;               /* MS2 records already shown */

static void ms2log(void)
{
    WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
    WORD ls, n, i;
    WORD FAR *h;
    if (!seg)
        return;
    ls = AllocSelector(SELECTOROF((void FAR *)&ivtsel));
    SetSelectorBase(ls, (DWORD)seg << 4);
    SetSelectorLimit(ls, 0xFFFF);
    h = (WORD FAR *)MAKELP(ls, off);
    if (h[1] == 1500) {
        n = h[0];
        for (i = ms2n; i < n; i++) {
            WORD FAR *e = (WORD FAR *)MAKELP(ls, h[10] + i * 16);
            if (e[0] == 3 || e[0] == 4)
                continue;               /* IRQ 12s: counted, not listed */
            wsprintf(line, "  MS2 %u AX=%04X BX=%04X %04X %04X #%u\r\n",
                     e[0], e[1], e[2], e[3], e[4], e[7]);
            out(line);
        }
        ms2n = n;
    }
    FreeSelector(ls);
}

static void pep(WORD a0, WORD a1, WORD a2)
{
    WORD i;
    clr();
    args[13] = a0;
    args[14] = a1;
    args[15] = a2;
    for (i = 0; i < 6; i++)
        regs[i] = 0x1111 * (i + 1);
    regs[6] = SELECTOROF((BYTE FAR *)gbuf);
    md_rcall(p7, args, regs);
    wsprintf(line, "P (%04X %04X %04X) -> AX=%04X BX=%04X CX=%04X DX=%04X fl=%04X pop=%u\r\n",
             a0, a1, a2, regs[0], regs[1], regs[2], regs[3], regs[7], regs[8]);
    out(line);
    ms2log();
}

static void power(void)
{
    WORD v;
    out("-- mode 2: PowerEventProc (args pushed first..last = a0 a1 a2), MS2 log after\r\n");
    ms2log();
    /* (WORD event, DWORD lParam), pushed event first */
    for (v = 0; v <= 5; v++)
        pep(v, 0, 0);
    /* (DWORD, WORD event) */
    for (v = 0; v <= 5; v++)
        pep(0, 0, v);
    pep(0xFFFF, 0xFFFF, 0xFFFF);
    /* mouse still working? GetSetMouseData get type afterwards */
    fill(0xCC);
    gsmd(0x12, gbuf);
    showbuf("G 12 after:", 0xCC, 16);
}

/* ---- mode 4: settings and what the driver then reports ---- */
#define D_SEL   0
#define D_N     2
#define D_MAX   4
#define D_NEXT  6
#define H_OFF   16
#define D_REC   256
#define MAXREC  1500

static WORD FAR *ev;
static WORD evfrom;

static void evsum(const char *tag)
{
    WORD n = ev[D_N / 2], i;
    long sx = 0, sy = 0;
    WORD nm = 0;
    POINT pt;
    GetCursorPos(&pt);
    wsprintf(line, "== %s: events %u..%u (depth %u, deepest %u; cursor %d,%d; ticks %lu)\r\n", (LPSTR)tag,
             evfrom, n, ev[5], ev[6], pt.x, pt.y, GetTickCount());
    out(line);
    for (i = evfrom; i < n; i++) {
        WORD FAR *e = ev + (D_REC + i * 16) / 2;
        wsprintf(line, "E %04X %d %d\r\n", e[0], (int)e[1], (int)e[2]);
        out(line);
        if (e[0] & 1) {
            sx += (int)e[1];
            sy += (int)e[2];
            nm++;
        }
    }
    wsprintf(line, "OBS %s motion events=%u x=%ld y=%ld\r\n", (LPSTR)tag, nm, sx, sy);
    out(line);
    evfrom = n;
}

/* spec "BIT,OFF,VAL[,OFF,VAL]..." (hex): get with flag BIT, change those
   bytes, set with BIT|1, get again into an EEh-filled buffer */
static void setspec(const char *spec)
{
    DWORD bit = 0, r;
    WORD n, val[16], which = 0, i;
    const char *c;
    for (i = 0; i < 16; i++)
        val[i] = 0;
    for (c = spec; *c; c++) {
        WORD d;
        if (*c == ',') {
            which++;
            continue;
        }
        d = (*c >= 'A' ? (*c & 0xDF) - 'A' + 10 : *c - '0');
        if (which == 0)
            bit = bit * 16 + d;
        else if (which <= 16)
            val[which - 1] = val[which - 1] * 16 + d;
    }
    n = which / 2;
    fill(0);
    gsmd(bit, gbuf);
    for (i = 0; i < n; i++)
        gbuf[val[2 * i]] = (BYTE)val[2 * i + 1];
    r = gsmd(bit | 1, gbuf);
    fill(0xEE);
    gsmd(bit, gbuf);
    wsprintf(line, "SET %s: flags %08lX ret=%04X:%04X; read back:", (LPSTR)spec, bit | 1, HIWORD(r), LOWORD(r));
    showbuf(line, 0xEE, 32);
}

static WORD evcsel;
static FARPROC ume;

/* MD1's recording event procedure (md1s.asm) in a fixed block, passing
   calls on to USER's Mouse_Event */
static void evsetup(void)
{
    WORD sel, i;
    sel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, D_REC + MAXREC * 16L);
    ev = (WORD FAR *)MAKELP(sel, 0);
    for (i = 0; i < md_len; i++)
        ((BYTE FAR *)ev)[H_OFF + i] = md_code[i];
    ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
    ev[D_SEL / 2] = sel;
    ev[D_MAX / 2] = MAXREC;
    ev[D_NEXT / 2] = OFFSETOF(ume);
    ev[D_NEXT / 2 + 1] = SELECTOROF(ume);
    evcsel = AllocDStoCSAlias(sel);
}

/* Disable, then Enable with the recording procedure (own) or USER's */
static void reenable(int own)
{
    clr();
    md_call(p[3], args, res);
    clr();
    args[14] = own ? evcsel : SELECTOROF(ume);
    args[15] = own ? H_OFF : OFFSETOF(ume);
    md_call(p[2], args, res);
}

static void behaviour(void)
{
    WORD secs, step;
    char key[16];
    out("-- mode 4: settings, then input\r\n");
    evsetup();
    reenable(1);
    /* [w16] md2pre: seconds before the first step (the input script starts
       after a fixed time) */
    wait(GetPrivateProfileInt("w16", "md2pre", 0, "SYSTEM.INI"));
    evsum("pre");
    secs = GetPrivateProfileInt("w16", "md2step", 8, "SYSTEM.INI");
    /* the steps: [w16] md2sN=FLAGBIT,OFFSET,VALUE (hex) before step N */
    for (step = 0; step < 10; step++) {
        char spec[40];
        wsprintf(key, "md2s%u", step);
        GetPrivateProfileString("w16", key, "", spec, sizeof spec, "SYSTEM.INI");
        if (step && !spec[0])
            break;
        if (spec[0])
            setspec(spec);
        {
            /* [w16] md2state: flags of the get shown before each step (hex;
               default 135E, every get bit without a pointer; 0: none) */
            char st[12];
            DWORD fl = 0;
            char *c;
            GetPrivateProfileString("w16", "md2state", "135E", st, sizeof st, "SYSTEM.INI");
            for (c = st; *c; c++)
                fl = fl * 16 + (*c >= 'A' ? (*c & 0xDF) - 'A' + 10 : *c - '0');
            if (fl) {
                fill(0xCC);
                gsmd(fl, gbuf);
                showbuf("state:", 0xCC, 48);
            }
        }
        wsprintf(key, "step %u", step);
        out("listening\r\n");
        wait(secs);
        evsum(key);
    }
    reenable(0);
}

/* ---- mode 8: the packet lost after Enable ----
   MS2 (tests/dos/ms2.asm) records every packet the BIOS hands the C207h
   handler (kind 5: status, X, Y); the recording event procedure every
   event. Phases of [w16] md2phase seconds (default 8), each begun by:
     A  Disable, Enable (own procedure)
     B  PowerEventProc(2, 0L) (the driver's own re-initialisation)
     C  Disable, Enable again (A repeated)
     D  GetSetMouseData set of the sensitivity it already has
     E  Disable, Enable, then PowerEventProc(1, 0L)
   (the input script, tests/scripts/input/md2adrop.txt, moves by 1, 2, ...
   9 mickeys in turn, over and over) */
static void drop(void)
{
    WORD secs = GetPrivateProfileInt("w16", "md2phase", 8, "SYSTEM.INI");
    WORD ph;
    static const char *names[] = { "A Disable+Enable", "B PowerEventProc(2)", "C Disable+Enable",
                                   "D GetSetMouseData set", "E Disable+Enable, PowerEventProc(1)" };
    out("-- mode 8: packets (MS2: handler calls) against events, phase by phase\r\n");
    evsetup();
    wait(GetPrivateProfileInt("w16", "md2pre", 0, "SYSTEM.INI"));
    for (ph = 0; ph < 5; ph++) {
        ms2log();               /* whatever came before: shown, then skipped */
        evfrom = ev[D_N / 2];
        wsprintf(line, "PHASE %s (ticks %lu)\r\n", (LPSTR)names[ph], GetTickCount());
        out(line);
        if (ph == 0 || ph == 2 || ph == 4)
            reenable(1);
        if (ph == 1)
            pep(2, 0, 0);
        if (ph == 3) {
            fill(0);
            gsmd(0x100, gbuf);
            gsmd(0x101, gbuf);
        }
        if (ph == 4)
            pep(1, 0, 0);
        wait(secs);
        out("packets the BIOS gave the handler:\r\n");
        ms2log();
        evsum(names[ph]);
    }
    reenable(0);
}

static void experiments(WORD mode)
{
    if (mode & 1)
        scan();
    if (mode & 2)
        power();
    if (mode & 4)
        behaviour();
    if (mode & 8)
        drop();
    if (mode & 16) {
        /* PowerEventProc while the driver is disabled (as during a DOS
           session): does it still set the device up? */
        out("-- mode 16: Disable, PowerEventProc(2), (3), (1), then Enable (USER's)\r\n");
        ms2log();
        evsetup();
        clr();
        md_call(p[3], args, res);
        out("disabled\r\n");
        ms2log();
        pep(2, 0, 0);
        pep(3, 0, 0);
        pep(1, 0, 0);
        reenable(0);
        out("enabled\r\n");
        ms2log();
        pep(4, 0, 0);
        pep(0x8002, 0, 0);
        pep(2, 1, 1);
    }
}
