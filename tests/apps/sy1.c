/*
 * sy1.c - SY1: every function SYSTEM.DRV exports, called from a program
 * and logged (drivers/system/README.md). Run as the shell
 * (tests/sysprobe.sh), once with Windows 3.1's SYSTEM.DRV and once with
 * w16's; the two TK.LOGs must be identical except the lines and fields
 * marked "~" (addresses, selectors, timing the host decides).
 *
 * Every call goes through kb_callx (kb1s.asm): marker values in every
 * register, all registers and the bytes taken off the stack reported. A
 * wrong guess of an argument list here cannot corrupt the probe's stack.
 *
 * Parts:
 *   A  which ordinals exist
 *   B  each entry: bytes taken off, registers (InquireSystem over its
 *      inputs, Get80x87SaveSize, Save/Restore80x87State with a known
 *      coprocessor state - the save area is the probe's own buffer -,
 *      A20_Proc, GetSystemMsecCount, Create/KillSystemTimer, Enable/
 *      DisableSystemTimers), and what each call changes in the real-mode
 *      interrupt table, the BIOS data area and the protected-mode vectors
 *   C  timers: how many, their handles, the ticks between calls for many
 *      rates, the context a timer procedure is called in (registers, the
 *      interrupt flag, the PIC), reentrancy, killing a timer from its own
 *      procedure
 *   D  GetSystemMsecCount against the BIOS tick count; INT 8 interrupts
 *      per BIOS tick (is the PIT reprogrammed?), the PIT's count
 *   E  DisableSystemTimers / EnableSystemTimers: the INT 8 vector, what
 *      stops and what goes on, what happens to the msec count
 *   F  WEP
 * The timer procedures and an INT 8 hook are sy1s.asm's, copied into a
 * fixed block (they run at interrupt time).
 *
 * [w16] sy1parts=ABCDEF (SYSTEM.INI) picks parts; part E also calls
 * EnableSystemTimers twice in a row ([w16] sy1double=0: not).
 */
#include <windows.h>

#define TK_REF "sy1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);
extern WORD __cdecl get_0040h(void);
extern int __cdecl dos_int21(WORD FAR *r);
extern WORD __cdecl __far get_0000h(void);
extern void __cdecl __far kb_callx(FARPROC f, WORD FAR *args, WORD FAR *out);
extern void __cdecl __far get_vec(WORD n, WORD FAR *v);
extern void __cdecl __far set_vec(WORD n, WORD sel, WORD off);
extern WORD __cdecl __far pit_read(void);
extern WORD __cdecl __far get_flags(void);
extern WORD __cdecl __far pic_state(void);
extern void __cdecl __far fpu_prep(void);
extern void __cdecl __far fpu_env(WORD FAR *out);
extern void __cdecl __far fpu_init(void);
extern BYTE __far sy_code[];
extern WORD __far sy_len, __far sy_i8off;

#define D_SEL      0
#define D_N        2
#define D_MAX      4
#define D_DEPTH    6
#define D_MAXDEP   8
#define D_I8CNT    10
#define D_I8PREV   12
#define D_MSEC     16
#define D_KILL     20
#define D_SELFSLOT 24
#define D_SELFH    26
#define D_SELFN    28
#define D_SELFRES  30
#define D_SPINSLOT 32
#define D_SPINDONE 34
#define D_SPINTK   36
#define D_SPINDEP  38
#define D_0040     40
#define D_SPINN    42
#define D_CNT      48
#define H_OFF      96
#define D_REC      1024
#define RSZ        26
#define MAXREC     2000

static WORD args[16];
static WORD rr[20];
static FARPROC p[32];
static char line[300];
static char lb[2048];
static WORD lbn;
static WORD blk;                /* the fixed block: data selector */
static WORD cbsel;              /* its code alias */
static volatile DWORD FAR *ticks;
static WORD sysds;              /* the timer procedures' DS, as seen */
static char parts[16];

static void flush(void)
{
    if (lbn)
        log_write(lb, lbn);
    lbn = 0;
}

static void put(const char FAR *s)
{
    while (*s) {
        if (lbn == sizeof lb)
            flush();
        lb[lbn++] = *s++;
    }
}

static int part(char c)
{
    char *s;
    for (s = parts; *s; s++)
        if (*s == c)
            return 1;
    return 0;
}

#define BW(off) (*(WORD FAR *)MAKELP(blk, (off)))

static void clear(void)
{
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

/* kb_callx's out: [0] popped, [1] flags, [2..9] AX BX CX DX SI DI DS ES,
   [10] BP */
static void call(int ord)
{
    kb_callx(p[ord], args, rr);
}

static WORD popped(void) { return rr[0]; }
static WORD rAX(void) { return rr[2]; }
static WORD rDX(void) { return rr[5]; }

/* the registers a call left, as one line: AX and DX as given (when they
   are results: res), the others whether they kept the markers */
static void regline(const char *what, int ax, int dx)
{
    wsprintf(line, "OBS regs %s: popped=%u %sAX=%04X %sDX=%04X ~BX=%s ~CX=%s SI=%u DI=%u DS=%u ~ES=%u BP=%u IF=%u DF=%u\r\n",
             (LPSTR)what, rr[0], (LPSTR)(ax ? "" : "~"), rr[2], (LPSTR)(dx ? "" : "~"), rr[5],
             (LPSTR)(rr[3] == 0x2222 ? "kept" : "changed"), (LPSTR)(rr[4] == 0x3333 ? "kept" : "changed"),
             rr[6] == 0x5555, rr[7] == 0x6666,
             rr[8] == SELECTOROF((void FAR *)rr), rr[9] == SELECTOROF((void FAR *)rr),
             rr[10] == OFFSETOF((void FAR *)rr), (rr[1] >> 9) & 1, (rr[1] >> 10) & 1);
    put(line);
}

/* wait until the BIOS tick count has changed n times (or a long time:
   returns 0 then) */
static int waitticks(WORD n)
{
    DWORD t0 = *ticks, beat = t0;
    DWORD spin = 0;
    while (*ticks - t0 < n) {
        if (++spin > 60000000L) {
            out("~ the tick count stood still\r\n");
            return 0;
        }
        if ((spin & 0xFFFFFL) == 0) {
            wsprintf(line, "~ spin %lu ticks %lu isr/imr %04X\r\n", spin, *ticks, pic_state());
            out(line);
        }
        /* a heartbeat for the harness (tests/dropin.sh -W ends the run
           when the log stands still for 2 seconds); not compared */
        if (*ticks - beat >= 4) {
            beat = *ticks;
            out("~\r\n");
        }
    }
    return 1;
}

/* the start of a tick */
static void tickedge(void)
{
    waitticks(1);
}

/* ---- snapshots: the real-mode interrupt table, the BIOS data area,
   0050:0000-000F, the protected-mode vectors 00h-7Fh ---- */
static WORD ivt0[512], bda0[136], pmv0[256];

static void snap(WORD FAR *ivt, WORD FAR *bda, WORD FAR *pmv)
{
    WORD i;
    WORD FAR *z = (WORD FAR *)MAKELP(get_0000h(), 0);
    for (i = 0; i < 512; i++)
        ivt[i] = z[i];
    for (i = 0; i < 136; i++)
        bda[i] = z[0x200 + i];          /* 0040:0000 - 0050:000F */
    for (i = 0; i < 128; i++)
        get_vec(i, pmv + 2 * i);
}

static WORD ivt1[512], bda1[136], pmv1[256];

/* what changed since snap(ivt0...): BIOS bytes of the tick count
   (0040:006C-0070) and the keyboard buffer (001A-003D) left out */
static void snapdiff(const char *what)
{
    WORD i;
    snap(ivt1, bda1, pmv1);
    for (i = 0; i < 256; i++)
        if (ivt0[2 * i] != ivt1[2 * i] || ivt0[2 * i + 1] != ivt1[2 * i + 1]) {
            wsprintf(line, "OBS %s: real-mode vector %02X changed ~%04X:%04X\r\n", (LPSTR)what, i,
                     ivt1[2 * i + 1], ivt1[2 * i]);
            put(line);
        }
    for (i = 0; i < 136; i++) {
        WORD off = 2 * i;
        if ((off >= 0x1A && off < 0x3E) || (off >= 0x6C && off < 0x72))
            continue;
        if (bda0[i] != bda1[i]) {
            wsprintf(line, "OBS %s: BIOS data %04X: %04X -> %04X\r\n", (LPSTR)what,
                     0x400 + off, bda0[i], bda1[i]);
            put(line);
        }
    }
    for (i = 0; i < 128; i++)
        if (pmv0[2 * i] != pmv1[2 * i] || pmv0[2 * i + 1] != pmv1[2 * i + 1]) {
            wsprintf(line, "OBS %s: protected-mode vector %02X changed ~%04X:%04X\r\n", (LPSTR)what, i,
                     pmv1[2 * i], pmv1[2 * i + 1]);
            put(line);
        }
    flush();
}

static void snapnow(void)
{
    snap(ivt0, bda0, pmv0);
}

/* ---- the timer procedures (sy1s.asm, in the fixed block) ---- */
static FARPROC cbproc(int i)
{
    return (FARPROC)MAKELP(cbsel, H_OFF + 5 * i);
}

static void reset_recs(void)
{
    WORD i;
    _asm { cli };
    BW(D_N) = 0;
    for (i = 0; i < 16; i++)
        BW(D_CNT + 2 * i) = 0;
    _asm { sti };
}

static WORD create(WORD rate, int slot)
{
    clear();
    args[13] = rate;
    args[14] = SELECTOROF(cbproc(slot));
    args[15] = OFFSETOF(cbproc(slot));
    call(2);
    return rAX();
}

static WORD kill(WORD h)
{
    clear();
    args[15] = h;
    call(3);
    return rAX();
}

/* part A */
static void partA(HMODULE hs)
{
    int i;
    for (i = 1; i < 32; i++) {
        wsprintf(line, "OBS ordinal %d: %s\r\n", i, (LPSTR)(p[i] ? "present" : "-"));
        put(line);
    }
    flush();
    (void)hs;
}

/* part B */
static WORD fsave[64];
static WORD env[12];

static void envline(const char *what)
{
    fpu_env(env);
    /* env[3..6]: where the last instruction and operand were (the
       program's layout and the driver's: "~") */
    wsprintf(line, "OBS fpu %s: control=%04X status=%04X tag=%04X ~ip=%04X:%04X ~op=%04X:%04X"
             " st0=%04X %04X %04X %04X %04X\r\n", (LPSTR)what,
             env[0], env[1], env[2], env[4], env[3], env[6], env[5],
             env[11], env[10], env[9], env[8], env[7]);
    put(line);
}

static void partB(void)
{
    WORD d, n, i;
    int iqargs;                 /* InquireSystem's argument words */
    WORD sz;

    /* InquireSystem: first how many bytes it takes, with all-zero words
       (0 is the timer resolution question for 2 or 3 arguments) */
    clear();
    call(1);
    regline("InquireSystem(0, 0)", 1, 1);
    iqargs = popped() / 2;
    obs("InquireSystem argument words", iqargs);
    /* (flag, drive[, enable]) are the last words pushed */
#define IQ(f, dr, en) do { clear(); \
        if (iqargs >= 3) { args[16 - iqargs] = (f); args[17 - iqargs] = (dr); args[18 - iqargs] = (en); } \
        else { args[14] = (f); args[15] = (dr); } call(1); } while (0)
    /* what the machine says about its drives (the same for both drivers:
       the environment InquireSystem's answers come from): the BIOS's
       equipment word, and DOS's IOCTL answers for each drive */
    obs("BIOS equipment word", *(WORD FAR *)MAKELP(get_0040h(), 0x10));
    for (d = 0; d < 26; d++) {
        static WORD r[8];
        WORD c8, c9, ce;
        WORD a8, a9, d9, ae;
        r[0] = 0x4408; r[1] = d + 1;
        c8 = dos_int21(r);
        a8 = r[0];
        r[0] = 0x4409; r[1] = d + 1;
        c9 = dos_int21(r);
        a9 = r[0];
        d9 = r[3];
        r[0] = 0x440E; r[1] = d + 1;
        ce = dos_int21(r);
        ae = r[0];
        wsprintf(line, "OBS DOS drive %u: 4408h %u:%04X 4409h %u:%04X/%04X 440Eh %u:%04X\r\n",
                 d, c8, a8, c9, a9, d9, ce, ae);
        put(line);
    }
    IQ(0, 5, 0);
    wsprintf(line, "OBS InquireSystem(0, 5) = %04X:%04X\r\n", rDX(), rAX());
    put(line);
    for (d = 0; d < 28; d++) {
        IQ(1, d, 0);
        wsprintf(line, "OBS InquireSystem(1, %u) = %04X:%04X\r\n", d, rDX(), rAX());
        put(line);
    }
    /* drive numbers beyond Z: are outside the interface: 3.1's driver
       answered 0 for 26, 27 and FFFFh but 3 (fixed) for 0101h; logged,
       not compared ("~") */
    IQ(1, 0xFFFF, 0);
    wsprintf(line, "OBS ~InquireSystem(1, FFFF) = %04X:%04X\r\n", rDX(), rAX());
    put(line);
    IQ(1, 0x101, 0);
    wsprintf(line, "OBS ~InquireSystem(1, 0101) = %04X:%04X\r\n", rDX(), rAX());
    put(line);
    IQ(1, 2, 0);
    regline("InquireSystem(1, 2)", 1, 1);
    for (n = 3; n < 8; n++) {
        IQ(n, 0, 0);
        wsprintf(line, "OBS InquireSystem(%u, 0) = %04X:%04X\r\n", n, rDX(), rAX());
        put(line);
    }
    IQ(0xFFFF, 0, 0);
    wsprintf(line, "OBS InquireSystem(FFFF, 0) = %04X:%04X\r\n", rDX(), rAX());
    put(line);
    flush();
    /* one-drive logic (KERNEL: (2, 0) at boot, (2, 1) when it leaves for
       DOS): what it answers and what it changes */
    for (n = 0; n < 4; n++) {
        static const WORD a[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
        snapnow();
        IQ(2, a[n][0], a[n][1]);
        wsprintf(line, "OBS InquireSystem(2, %u, %u) = %04X:%04X\r\n", a[n][0], a[n][1], rDX(), rAX());
        put(line);
        snapdiff("InquireSystem(2)");
    }
    regline("InquireSystem(2, 1, 1)", 1, 1);
    /* drives again after the one-drive calls, then back as at boot */
    for (d = 0; d < 3; d++) {
        IQ(1, d, 0);
        wsprintf(line, "OBS after: InquireSystem(1, %u) = %04X:%04X\r\n", d, rDX(), rAX());
        put(line);
    }
    IQ(2, 0, 0);
    flush();

    /* the coprocessor */
    clear();
    call(7);
    regline("Get80x87SaveSize", 1, 0);
    sz = rAX();
    obs("Get80x87SaveSize", sz);
    fpu_prep();
    envline("prepared");
    for (i = 0; i < 64; i++)
        fsave[i] = 0xCCCC;
    clear();
    args[14] = SELECTOROF((void FAR *)fsave);
    args[15] = OFFSETOF((void FAR *)fsave);
    call(8);
    regline("Save80x87State", 0, 0);
    envline("after save");
    /* the save area: words 3-6 are where the last instruction and operand
       were ("~"); the rest (control, status, tag, the 8 registers) and
       how far the driver wrote */
    put("OBS save area:");
    for (i = 0; i < 52; i++) {
        if (i >= 3 && i <= 6)
            wsprintf(line, " ~%04X", fsave[i]);
        else
            wsprintf(line, " %04X", fsave[i]);
        put(line);
    }
    put("\r\n");
    fpu_init();
    envline("after fninit");
    clear();
    args[14] = SELECTOROF((void FAR *)fsave);
    args[15] = OFFSETOF((void FAR *)fsave);
    call(9);
    regline("Restore80x87State", 0, 0);
    envline("after restore");
    fpu_init();
    flush();

    /* A20_Proc */
    for (n = 0; n < 3; n++) {
        snapnow();
        clear();
        args[15] = n;
        call(20);
        {
            char what[16];
            wsprintf(what, "A20_Proc(%u)", n);
            regline(what, 1, 0);
        }
        snapdiff("A20_Proc");
    }
    /* GetSystemMsecCount */
    clear();
    call(6);
    regline("GetSystemMsecCount", 0, 0);
    flush();
}

/* part C */
static void dumprecs(const char *what, WORD from)
{
    WORD i, n = BW(D_N);
    for (i = from; i < n && i < from + 40; i++) {
        WORD o = D_REC + i * RSZ;
        wsprintf(line, "OBS ~%s rec %u: proc=%u AX=%04X fl=%04X DS=%04X ES=%04X SS=%04X SP=%04X tick=%04X isr/imr=%04X depth=%u ms=%04X%04X\r\n",
                 (LPSTR)what, i, BW(o), BW(o + 2), BW(o + 4), BW(o + 6), BW(o + 8), BW(o + 10), BW(o + 12),
                 BW(o + 14), BW(o + 16), BW(o + 18), BW(o + 22), BW(o + 20));
        put(line);
    }
    flush();
}

struct ratetest { WORD rate; WORD h; WORD t0; };

/* the calls of procedure slot (records in order) as ticks after t0 */
static void rateline(struct ratetest *r, int slot, WORD maxcalls)
{
    WORD i, n = BW(D_N), k = 0, last = 0;
    DWORD mslast = 0;
    wsprintf(line, "OBS rate %u: calls after ticks", r->rate);
    put(line);
    for (i = 0; i < n && k < maxcalls; i++) {
        WORD o = D_REC + i * RSZ;
        if (BW(o) != (WORD)slot)
            continue;
        wsprintf(line, " %u", (WORD)(BW(o + 14) - (k ? last : r->t0)));
        put(line);
        last = BW(o + 14);
        k++;
    }
    /* the msec count between the calls: the tick's 54.925 ms rounded
       as the count's phase falls (not compared) */
    wsprintf(line, "\r\nOBS ~rate %u: msec between calls", r->rate);
    put(line);
    for (i = 0, k = 0; i < n && k < maxcalls; i++) {
        WORD o = D_REC + i * RSZ;
        DWORD ms;
        if (BW(o) != (WORD)slot)
            continue;
        ms = MAKELONG(BW(o + 20), BW(o + 22));
        if (k)
            wsprintf(line, " %lu", ms - mslast);
        else
            line[0] = 0;
        put(line);
        mslast = ms;
        k++;
    }
    put("\r\n");
    flush();
}

static void batch(const WORD *rates, int nr, WORD runticks, WORD maxcalls)
{
    struct ratetest r[8];
    int i;
    reset_recs();
    tickedge();
    for (i = 0; i < nr; i++) {
        r[i].rate = rates[i];
        r[i].t0 = (WORD)*ticks;
        r[i].h = create(rates[i], i);
    }
    waitticks(runticks);
    for (i = 0; i < nr; i++)
        if (r[i].h)
            r[i].h = kill(r[i].h);
        else
            put("OBS create failed\r\n");
    for (i = 0; i < nr; i++) {
        rateline(&r[i], i, maxcalls);
        if (r[i].h)
            put("OBS kill failed\r\n");
    }
    flush();
}

static void partC(void)
{
    WORD h[24], n, i, h2;
    WORD sssame, dsset;

    /* capacity: timers until CreateSystemTimer says 0 */
    for (n = 0; n < 24; n++) {
        h[n] = create(60000, 15);
        if (!h[n])
            break;
    }
    obs("timers available to a program", n);
    put("OBS handles:");
    for (i = 0; i < n; i++) {
        wsprintf(line, " %04X", h[i]);
        put(line);
    }
    put("\r\n");
    regline("CreateSystemTimer", 1, 0);
    /* kill the second, create again: which handle comes back */
    if (n >= 3) {
        WORD k = kill(h[1]);
        regline("KillSystemTimer", 1, 0);
        obs("KillSystemTimer result", k);
        h[1] = create(60000, 15);
        wsprintf(line, "OBS re-created: %04X\r\n", h[1]);
        put(line);
    }
    for (i = 0; i < n; i++) {
        WORD k = kill(h[i]);
        wsprintf(line, "OBS kill %04X = %04X\r\n", h[i], k);
        put(line);
    }
    if (n) {
        h2 = kill(h[0]);
        wsprintf(line, "OBS kill again %04X = %04X\r\n", h[0], h2);
        put(line);
    }
    /* a null procedure */
    clear();
    args[13] = 1000;
    call(2);
    wsprintf(line, "OBS create with a null procedure = %04X\r\n", rAX());
    put(line);
    if (rAX())
        kill(rAX());
    flush();

    /* rates: ticks between calls */
    {
        /* four at a time: what a program gets with Windows 3.1's
           driver (above) */
        static const WORD b1[] = { 0, 1, 27, 28 };
        static const WORD b2[] = { 54, 55, 56, 82 };
        static const WORD b3[] = { 83, 100, 109, 110 };
        static const WORD b4[] = { 111, 164, 165, 200 };
        static const WORD b5[] = { 274, 275, 300, 500 };
        static const WORD b6[] = { 530, 549, 550, 551 };
        static const WORD b7[] = { 1000, 1100, 2000, 10000 };
        static const WORD b8[] = { 65535, 32768 };
        batch(b1, 4, 40, 40);
        batch(b2, 4, 40, 40);
        batch(b3, 4, 40, 40);
        batch(b4, 4, 60, 40);
        batch(b5, 4, 80, 40);
        batch(b6, 4, 100, 40);
        batch(b7, 4, 200, 10);
        batch(b8, 2, 100, 10);
    }

    /* the context of a call */
    reset_recs();
    tickedge();
    h[0] = create(55, 0);
    waitticks(10);
    kill(h[0]);
    dumprecs("context", 0);
    sssame = dsset = 1;
    for (i = 0; i < BW(D_N); i++) {
        WORD o = D_REC + i * RSZ;
        wsprintf(line, "OBS context %u: AX=handle:%u IF=%u isr0=%u imr0=%u depth=%u SS=mine:%u DS=mine:%u\r\n", i,
                 BW(o + 2) == h[0], (BW(o + 4) >> 9) & 1, BW(o + 16) & 1, (BW(o + 16) >> 8) & 1, BW(o + 18),
                 BW(o + 10) == SELECTOROF((void FAR *)&i), BW(o + 6) == SELECTOROF((void FAR *)&i));
        if (i < 4)
            put(line);
        if (i == 0)
            sysds = BW(o + 6);
        else if (BW(o + 6) != sysds)
            dsset = 0;
        if (BW(o + 10) != SELECTOROF((void FAR *)&i))
            sssame = 0;
    }
    obs("calls in 10 ticks at rate 55", BW(D_N));
    obs("DS the same in every call", dsset);
    obs("SS the interrupted program's", sssame);
    {
        /* the stack between the interrupted code and the procedure: SP
           in the wait loop is about this */
        WORD here = OFFSETOF((void FAR *)&here);
        wsprintf(line, "OBS ~stack used before the procedure: %u bytes\r\n",
                 here - BW(D_REC + 12));
        put(line);
    }
    flush();

    /* reentrancy: procedure 1 waits 3 ticks with interrupts on, on its
       first call; procedure 0 at rate 55 meanwhile */
    reset_recs();
    BW(D_SPINSLOT) = 1;
    BW(D_SPINDONE) = 0;
    BW(D_MAXDEP) = 0;
    tickedge();
    h[0] = create(55, 0);
    h[1] = create(55, 1);
    h[2] = create(275, 4);
    waitticks(16);
    kill(h[2]);
    kill(h[1]);
    kill(h[0]);
    BW(D_SPINSLOT) = 0xFFFF;
    obs("reentrancy: wait finished", BW(D_SPINDONE));
    obs("reentrancy: ticks during the wait", BW(D_SPINTK));
    obs("reentrancy: deepest nesting during the wait", BW(D_SPINDEP));
    obs("reentrancy: calls during the wait", BW(D_SPINN));
    obs("reentrancy: deepest nesting", BW(D_MAXDEP));
    wsprintf(line, "OBS reentrancy: calls of 0 %u, of 1 %u, of the 275 ms one %u\r\n",
             BW(D_CNT), BW(D_CNT + 2), BW(D_CNT + 8));
    put(line);
    {
        /* the calls as ticks after the first: which ticks were skipped */
        WORD t00 = BW(D_REC + 14);
        put("OBS reentrancy: calls (procedure@tick)");
        for (i = 0; i < BW(D_N); i++) {
            wsprintf(line, " %u@%u", BW(D_REC + i * RSZ), BW(D_REC + i * RSZ + 14) - t00);
            put(line);
        }
        put("\r\n");
    }
    dumprecs("reentrancy", 0);

    /* a procedure killing its own timer on its 3rd call */
    reset_recs();
    tickedge();
    h[0] = create(55, 2);
    BW(D_SELFH) = h[0];
    BW(D_SELFN) = 3;
    BW(D_SELFRES) = 0xEEEE;
    BW(D_SELFSLOT) = 2;
    waitticks(10);
    BW(D_SELFSLOT) = 0xFFFF;
    obs("self-kill: result", BW(D_SELFRES));
    obs("self-kill: calls", BW(D_CNT + 4));
    h2 = kill(h[0]);
    obs("self-kill: kill afterwards", h2 == h[0] ? 1 : h2);
    flush();
}

/* part D */
static void partD(void)
{
    WORD i, distinct, maxpit, n, hrate;
    DWORD m0, m1, t0;
    WORD v[2];

    /* the msec count at each tick */
    tickedge();
    clear();
    call(6);
    m0 = MAKELONG(rAX(), rDX());
    t0 = m0;
    /* (where the 54s fall is the count's phase: logged, not compared;
       compared: only 54 and 55, and the sum over 40 ticks, 40 x 54.925
       rounded down or up) */
    put("OBS ~msec per tick:");
    n = 0;
    for (i = 0; i < 40; i++) {
        waitticks(1);
        clear();
        call(6);
        m1 = MAKELONG(rAX(), rDX());
        wsprintf(line, " %lu", m1 - m0);
        put(line);
        if (m1 - m0 != 54 && m1 - m0 != 55)
            n++;
        m0 = m1;
    }
    put("\r\n");
    obs("msec per tick other than 54 or 55", n);
    obs("msec over 40 ticks 2197 or 2196", m1 - t0 == 2197 || m1 - t0 == 2196);
    /* within one tick: how many distinct values */
    tickedge();
    t0 = *ticks;
    distinct = 0;
    clear();
    call(6);
    m0 = MAKELONG(rAX(), rDX());
    while (*ticks == t0) {
        clear();
        call(6);
        m1 = MAKELONG(rAX(), rDX());
        if (m1 != m0)
            distinct++;
        m0 = m1;
    }
    /* 0, or 1 when the tick lands between the loop's test and the read */
    obs("msec changes within a tick at most 1", distinct <= 1);
    obs_env("msec changes within a tick", distinct);
    flush();

    /* INT 8 interrupts per BIOS tick: a hook in front */
    get_vec(8, v);
    BW(D_I8PREV) = v[1];
    BW(D_I8PREV + 2) = v[0];
    BW(D_I8CNT) = 0;
    tickedge();
    _asm { cli };
    BW(D_I8CNT) = 0;
    set_vec(8, cbsel, H_OFF + sy_i8off);
    _asm { sti };
    t0 = *ticks;
    waitticks(40);
    _asm { cli };
    n = BW(D_I8CNT);
    set_vec(8, v[0], v[1]);
    _asm { sti };
    wsprintf(line, "OBS INT 8 interrupts in 40 ticks: %u\r\n", n);
    put(line);
    /* a rate-0 timer with the hook in front: the BIOS tick count the hook
       saw (before SYSTEM's handler) against the procedure's (one more:
       the BIOS's INT 8 ran first); the msec count in the procedure
       against the program's right after the tick */
    reset_recs();
    tickedge();
    _asm { cli };
    set_vec(8, cbsel, H_OFF + sy_i8off);
    _asm { sti };
    hrate = create(0, 5);
    {
        WORD same = 0, after = 0, k;
        for (k = 0; k < 10; k++) {
            DWORD tt;
            waitticks(1);
            tt = *ticks;
            clear();
            call(6);
            m1 = MAKELONG(rAX(), rDX());
            for (i = 0; i < BW(D_N); i++) {
                WORD o = D_REC + i * RSZ;
                if (BW(o + 14) == (WORD)tt && MAKELONG(BW(o + 20), BW(o + 22)) == m1)
                    same++;
            }
        }
        for (i = 0; i < BW(D_N); i++) {
            WORD o = D_REC + i * RSZ;
            if (BW(o + 14) == (WORD)(BW(o + 24) + 1))
                after++;
        }
        kill(hrate);
        _asm { cli };
        set_vec(8, v[0], v[1]);
        _asm { sti };
        obs("procedure called after the BIOS's tick (every call)", after == BW(D_N));
        obs("msec in the procedure = right after the tick (of 10)", same);
    }
    /* the PIT's count: the largest seen over a while (the reload value
       is just above it) */
    maxpit = 0;
    for (i = 0; i < 30000; i++) {
        WORD c = pit_read();
        if (c > maxpit)
            maxpit = c;
    }
    obs("PIT count above F000h", maxpit >= 0xF000);
    obs_env("PIT largest count", maxpit);
    flush();
}

/* part E */
static void counted55(WORD wait, const char *what)
{
    WORD h;
    DWORD m0, m1, t0;
    reset_recs();
    tickedge();
    clear();
    call(6);
    m0 = MAKELONG(rAX(), rDX());
    t0 = *ticks;
    h = create(55, 3);
    if (!waitticks(wait))
        put("OBS (the BIOS tick count stood still)\r\n");
    clear();
    call(6);
    m1 = MAKELONG(rAX(), rDX());
    kill(h);
    /* the msec: 0, or 20 x 54.925 rounded either way (the count's phase) */
    wsprintf(line, "OBS %s: %u ticks, timer calls %u, msec %s ~%lu\r\n", (LPSTR)what,
             (WORD)(*ticks - t0), BW(D_CNT + 6),
             (LPSTR)(m1 == m0 ? "0" : m1 - m0 == 1098 || m1 - m0 == 1099 ? "1098-1099" : "other"),
             m1 - m0);
    put(line);
    flush();
}

static void vecline(const char *what, WORD FAR *v0)
{
    WORD v[2];
    get_vec(8, v);
    wsprintf(line, "OBS INT 8 %s: %s (code of SYSTEM's: %u) ~%04X:%04X\r\n", (LPSTR)what,
             (LPSTR)(v[0] == v0[0] && v[1] == v0[1] ? "as at start" : "other"),
             v[0] == SELECTOROF(p[6]), v[0], v[1]);
    put(line);
}

static void partE(void)
{
    WORD v0[2];
    DWORD m0, m1;
    get_vec(8, v0);
    vecline("at start", v0);
    {
        /* whose handler is in front of SYSTEM's at the start */
        char name[80];
        HMODULE m = (HMODULE)K_GETEXEPTR(v0[0]);
        name[0] = 0;
        if (m)
            GetModuleFileName(m, name, sizeof name);
        wsprintf(line, "OBS INT 8 at start belongs to \"%s\"\r\n", (LPSTR)name);
        put(line);
    }
    counted55(20, "enabled");
    snapnow();
    clear();
    call(5);
    regline("DisableSystemTimers", 0, 0);
    snapdiff("DisableSystemTimers");
    vecline("after Disable", v0);
    clear();
    call(6);
    m0 = MAKELONG(rAX(), rDX());
    counted55(20, "disabled");
    clear();
    call(5);
    vecline("after Disable twice", v0);
    snapnow();
    clear();
    call(4);
    regline("EnableSystemTimers", 0, 0);
    snapdiff("EnableSystemTimers");
    clear();
    call(6);
    m1 = MAKELONG(rAX(), rDX());
    wsprintf(line, "OBS msec over the disabled time (about 45 ticks): %lu\r\n", m1 - m0);
    put(line);
    vecline("after Enable", v0);
    counted55(20, "enabled again");
    if (GetPrivateProfileInt("w16", "sy1double", 1, "SYSTEM.INI")) {
        clear();
        call(4);
        vecline("after Enable twice", v0);
        flush();
        counted55(20, "enabled twice");
    }
    flush();
}

int __cdecl tmain(void)
{
    HMODULE hs;
    HGLOBAL g;
    WORD i;

    U_INITAPP(SELECTOROF((void FAR *)&i));      /* (DGROUP's selector) */
    ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    GetPrivateProfileString("w16", "sy1parts", "ABCDEF", parts, sizeof parts, "SYSTEM.INI");
    out("SY1 start\r\n");
    hs = GetModuleHandle("SYSTEM");
    for (i = 1; i < 32; i++)
        p[i] = GetProcAddress(hs, MAKEINTRESOURCE(i));

    /* the fixed block with the procedures */
    g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 65000L);
    blk = SELECTOROF(GlobalLock(g));
    for (i = 0; i < sy_len; i++)
        *(BYTE FAR *)MAKELP(blk, H_OFF + i) = sy_code[i];
    cbsel = AllocDStoCSAlias(blk);
    BW(D_SEL) = blk;
    BW(D_MAX) = MAXREC;
    BW(D_0040) = get_0040h();
    BW(D_SELFSLOT) = 0xFFFF;
    BW(D_SPINSLOT) = 0xFFFF;
    BW(D_KILL) = OFFSETOF(p[3]);
    BW(D_KILL + 2) = SELECTOROF(p[3]);
    BW(D_MSEC) = OFFSETOF(p[6]);
    BW(D_MSEC + 2) = SELECTOROF(p[6]);
    obs_env("block", blk);
    {
        /* SYREC in front: the canary in its automatic data segment (SYRPAD,
           syrec.asm); a test build answering ordinal 99 with a pad: that */
        FARPROC plog = GetProcAddress(hs, MAKEINTRESOURCE(202));
        FARPROC ppad = GetProcAddress(hs, MAKEINTRESOURCE(99));
        if (ppad) {
            WORD FAR *pad = (WORD FAR *)((DWORD (FAR PASCAL *)(void))ppad)();
            put("PAD at start:");
            for (i = 0; i < 2048; i++)
                if (pad[i]) {
                    wsprintf(line, " +%02X=%04X", 2 * i, pad[i]);
                    put(line);
                }
            put("\r\n");
            flush();
        }
        if (plog) {
            WORD FAR *pad = (WORD FAR *)((DWORD (FAR PASCAL *)(void))plog)();
            put("SYREC pad at start:");
            for (i = 0; i < 128; i++)
                if (pad[i]) {
                    wsprintf(line, " +%02X=%04X", 2 * i, pad[i]);
                    put(line);
                }
            put("\r\n");
            flush();
        }
    }

    if (part('A'))
        partA(hs);
    if (part('B'))
        partB();
    if (part('C'))
        partC();
    if (part('D'))
        partD();
    if (part('E'))
        partE();
    if (part('F')) {
        clear();
        call(10);
        regline("WEP(0)", 1, 0);
    }
    flush();
    /* SYREC.DRV in front (tests/apps/syrec.asm): the module of each
       caller's code segment and of each timer procedure's, for the
       decoder (tools/syrec.py), then its records to C:\SYREC.BIN */
    {
        FARPROC plog = GetProcAddress(hs, MAKEINTRESOURCE(200));
        FARPROC pdump = GetProcAddress(hs, MAKEINTRESOURCE(201));
        if (plog && pdump) {
            DWORD (FAR PASCAL *f)(void) = (DWORD (FAR PASCAL *)(void))plog;
            BYTE FAR *h = (BYTE FAR *)f();
            WORD nrec = *(WORD FAR *)h, rs = *(WORD FAR *)(h + 4), r, k;
            WORD nmax = *(WORD FAR *)(h + 2);
            BYTE FAR *recs = h + 400 + nmax * 6;
            static WORD seen[64];
            WORD nseen = 0;
            wsprintf(line, "SYREC header %04X:%04X, %u records; counts",
                     SELECTOROF(h), OFFSETOF(h), nrec);
            put(line);
            for (k = 0; k < 64; k++)
                if (((WORD FAR *)(h + 16))[k]) {
                    wsprintf(line, " %u:%u", k, ((WORD FAR *)(h + 16))[k]);
                    put(line);
                }
            put("\r\n");
            for (r = 0; r <= nrec; r++) {
                WORD cs;
                if (r < nrec)
                    cs = *(WORD FAR *)(recs + r * rs + 30);
                else
                    cs = SELECTOROF(GetProcAddress(GetModuleHandle("SYSTGT"), MAKEINTRESOURCE(1)));
                for (k = 0; k < nseen && seen[k] != cs; k++)
                    ;
                if (k < nseen || nseen == 64)
                    continue;
                seen[nseen++] = cs;
                {
                    HMODULE m = (HMODULE)K_GETEXEPTR(cs);
                    char name[80];
                    name[0] = 0;
                    if (m)
                        GetModuleFileName(m, name, sizeof name);
                    wsprintf(line, "CSMAP %04X %s\r\n", cs, (LPSTR)name);
                    put(line);
                }
            }
            flush();
            pdump();
        }
    }
    out("SY1 done\r\n");
    return 0;
}
