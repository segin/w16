/*
 * md2c.c - MD2: which packets after Enable a mouse driver passes to its
 * event procedure (tests/apps/md2logs/c/notes.md, drivers/mouse/README.md:
 * "the second packet after each Enable never reaches the event
 * procedure"). Black-box: the driver is only called through its exported
 * Enable and Disable, and fed packets through the PS/2 BIOS's C207h
 * handler interface.
 *
 * Needs MS3 (tests/dos/ms3c.asm) loaded before WIN. MS3 holds the handler
 * the driver gives INT 15h C207h and can replace each packet the BIOS
 * delivers with packets from a table MD2 writes into it: several in one
 * BIOS callback, after a hold-off, or with a minimum gap. A steady stream
 * of real pointer motion (made by tests/md2cprobe.sh, xdotool through
 * DOSBox) supplies the BIOS callbacks that carry them.
 *
 * MD2 runs as the shell. [w16] md2exp in SYSTEM.INI picks a list of
 * cycles (below); each cycle is a short program of Disable, Enable, arm
 * MS3, wait. MD2's own event procedure (md2cs.asm) records each call (AX
 * BX CX DX, BIOS tick, MS3's sequence number at the time, cycle) and
 * passes it on to USER's Mouse_Event. At the end USER's procedure is put
 * back and everything is written to TK.LOG:
 *   C n ...       a cycle starts (MS3's sequence number then)
 *   S ...         a step (what it did, MS3's sequence number)
 *   E cyc ax bx cx dx tick ms3seq IF   an event procedure call
 *   R kind a b c d tick depth seq      an MS3 record (ms3c.asm's header)
 */
#include <windows.h>

#define TK_REF "md1ref.h"
#include "tklog.h"

extern BYTE __far __cdecl md2_code[];
extern WORD __far __cdecl md2_len;
extern DWORD __cdecl __far md2_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern int FAR PASCAL U_INITAPP(WORD hinst);

#define D_SEL   0
#define D_N     2
#define D_MAX   4
#define D_NEXT  6
#define D_NEST  12
#define D_BIOS  14
#define D_M3SEL 16
#define D_M3OFF 18
#define D_CYCLE 20
#define H_OFF   32
#define D_REC   512
#define MAXREC  1500

/* MS3's header (ms3c.asm), word offsets */
#define M_COUNT  0
#define M_SEQ    3
#define M_LOGP   4
#define M_MODE   5
#define M_TABN   6
#define M_TABI   7
#define M_MULT   8
#define M_HOLD   9
#define M_GAP    10
#define M_T0     11
#define M_TLAST  12
#define M_MAGIC  13
#define M_NDELIV 14
#define M_NSWAL  15
#define M_TABP   22

static WORD args[16];
static WORD res[2];
static WORD FAR *m3;            /* MS3's header, or NULL */
static WORD FAR *bios;          /* segment 40h */
static WORD FAR *blk;           /* MD2's event block */
static FARPROC pen, pdis, ume;
static WORD csel;
static char line[128];

/* tables: status, X, Y (PS/2: status bit 3 always set; bit 0 left,
   bit 1 right; X and Y the low bytes, signs in status bits 4, 5) */
static const WORD t_mot[] = { 8,1,0, 8,2,0, 8,3,0, 8,4,0, 8,5,0, 8,6,0, 8,7,0, 8,8,0 };
static const WORD t_btn1[] = { 9,0,0, 8,0,0, 8,3,0, 8,4,0, 8,5,0, 8,6,0 };
static const WORD t_btn2[] = { 8,1,0, 9,0,0, 8,0,0, 8,4,0, 8,5,0, 8,6,0 };
static const WORD t_zero2[] = { 8,1,0, 8,0,0, 8,3,0, 8,4,0, 8,5,0, 8,6,0 };
static const WORD t_dup[] = { 8,1,0, 8,1,0, 8,1,0, 8,4,0, 8,5,0, 8,6,0 };
static const WORD t_y[] = { 8,0,1, 8,0,2, 8,0,3, 8,0,4, 8,0,5, 8,0,6 };
static const WORD t_one[] = { 8,9,0 };
static const WORD t_mot16[] = { 8,1,0, 8,2,0, 8,3,0, 8,4,0, 8,5,0, 8,6,0, 8,7,0, 8,8,0,
                                8,9,0, 8,10,0, 8,11,0, 8,12,0, 8,13,0, 8,14,0, 8,15,0, 8,16,0 };
static const WORD t_bad2[] = { 8,1,0, 0,2,0, 8,3,0, 8,4,0, 8,5,0, 8,6,0 };
static const WORD t_rbtn2[] = { 8,1,0, 10,0,0, 8,0,0, 8,4,0, 8,5,0, 8,6,0 };

enum { OP_END, OP_CYC, OP_D, OP_E, OP_ARM, OP_SWAL, OP_PASS, OP_W, OP_WD, OP_EU };
typedef struct {
    int op;
    const WORD *tab;
    WORD n, mult, hold, gap;    /* OP_W: n = seconds; OP_WD: n deliveries, mult = max secs */
    const char *what;
} step;

#define ARM(t, m, h, g) { OP_ARM, t, sizeof t / 6, m, h, g, #t }
#define CYC(s) { OP_CYC, 0, 0, 0, 0, 0, s }
#define DIS { OP_D, 0, 0, 0, 0, 0, "Disable" }
#define ENA { OP_E, 0, 0, 0, 0, 0, "Enable" }
#define W(s) { OP_W, 0, s, 0, 0, 0, "wait" }
#define WD(n, s) { OP_WD, 0, n, s, 0, 0, "wait deliveries" }
#define SWAL { OP_SWAL, 0, 0, 0, 0, 0, "swallow" }
#define PASS { OP_PASS, 0, 0, 0, 0, 0, "pass" }

/* experiment 1: position, content, packets per callback, timing */
static const step exp1[] = {
    CYC("motion 1..8, one per callback"), DIS, ARM(t_mot, 1, 0, 0), ENA, WD(8, 12),
    CYC("button down first"), DIS, ARM(t_btn1, 1, 0, 0), ENA, WD(6, 10),
    CYC("button down second"), DIS, ARM(t_btn2, 1, 0, 0), ENA, WD(6, 10),
    CYC("zero packet second"), DIS, ARM(t_zero2, 1, 0, 0), ENA, WD(6, 10),
    CYC("same packet three times"), DIS, ARM(t_dup, 1, 0, 0), ENA, WD(6, 10),
    CYC("Y motion"), DIS, ARM(t_y, 1, 0, 0), ENA, WD(6, 10),
    CYC("motion 1..8, four per callback"), DIS, ARM(t_mot, 4, 0, 0), ENA, W(4),
    CYC("motion 1..8, all in one callback"), DIS, ARM(t_mot, 8, 0, 0), ENA, W(3),
    CYC("first packet 10 s after arming"), DIS, ARM(t_mot, 1, 182, 0), ENA, WD(8, 25),
    CYC("3 s between packets"), DIS, ARM(t_mot, 1, 0, 55), ENA, WD(8, 35),
    CYC("status without bit 3 second"), DIS, ARM(t_bad2, 1, 0, 0), ENA, WD(6, 10),
    CYC("right button second"), DIS, ARM(t_rbtn2, 1, 0, 0), ENA, WD(6, 10),
    { OP_END }
};

/* experiment 2: what resets it */
static const step exp2[] = {
    CYC("baseline"), DIS, ARM(t_mot, 1, 0, 0), ENA, WD(8, 12),
    CYC("re-armed without Disable/Enable"), ARM(t_mot, 1, 0, 0), WD(8, 12),
    CYC("Enable twice, no Disable between"), DIS, ARM(t_mot, 1, 0, 0), ENA, ENA, WD(8, 12),
    CYC("Enable, no packet, Disable, Enable"), DIS, SWAL, ENA, W(3), DIS, ARM(t_mot, 1, 0, 0), ENA, WD(8, 12),
    CYC("Enable, one packet, Disable, Enable"), DIS, ARM(t_one, 1, 0, 0), ENA, WD(1, 6), W(1), DIS, ARM(t_mot, 1, 0, 0), ENA, WD(8, 12),
    CYC("Enable, 1 s, packets"), DIS, ARM(t_mot, 1, 18, 0), ENA, WD(8, 14),
    CYC("16 packets"), DIS, ARM(t_mot16, 1, 0, 0), ENA, WD(16, 20),
    CYC("Enable while packets flow (2 per callback)"), DIS, ARM(t_mot16, 2, 0, 0), ENA, WD(8, 12),
    { OP_END }
};

/* experiment 3: MS3 passes the BIOS's packets as they are */
static const step exp3[] = {
    CYC("pass-through"), DIS, PASS, ENA, W(12),
    CYC("pass-through again"), DIS, PASS, ENA, W(12),
    { OP_END }
};

static DWORD call(FARPROC f)
{
    return md2_call(f, args, res);
}

static void clear(void)
{
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

static void pump_ticks(WORD ms)
{
    MSG m;
    UINT id = SetTimer(0, 0, ms, NULL);     /* hwnd 0: the ID is Windows' */
    while (GetMessage(&m, 0, 0, 0) && !(m.message == WM_TIMER && m.wParam == id))
        DispatchMessage(&m);
    KillTimer(0, id);
}

static WORD seqnow(void)
{
    return m3 ? m3[M_SEQ] : 0;
}

static void note(const char *what, WORD a)
{
    wsprintf(line, "S %s %u seq=%u tick=%u deliv=%u swal=%u\r\n", (LPSTR)what, a,
             seqnow(), bios[0x6C / 2], m3 ? m3[M_NDELIV] : 0, m3 ? m3[M_NSWAL] : 0);
    out(line);
}

static void arm(const WORD *t, WORD n, WORD mult, WORD hold, WORD gap)
{
    WORD FAR *tab = (WORD FAR *)MAKELP(SELECTOROF(m3), m3[M_TABP]);
    WORD i;
    m3[M_MODE] = 0;
    for (i = 0; i < n * 3; i++)
        tab[i] = t[i];
    m3[M_TABN] = n;
    m3[M_TABI] = 0;
    m3[M_MULT] = mult;
    m3[M_HOLD] = hold;
    m3[M_GAP] = gap;
    m3[M_NDELIV] = 0;
    m3[M_T0] = bios[0x6C / 2];
    m3[M_MODE] = 1;
}

static void run(const step *s)
{
    WORD cyc = 0, i;
    for (; s->op != OP_END; s++) {
        switch (s->op) {
        case OP_CYC:
            blk[D_CYCLE / 2] = ++cyc;
            wsprintf(line, "C %u %s seq=%u\r\n", cyc, (LPSTR)s->what, seqnow());
            out(line);
            break;
        case OP_D:
            clear();
            call(pdis);
            note("Disable", 0);
            break;
        case OP_E:
            clear();
            args[14] = csel;
            args[15] = H_OFF;
            call(pen);
            note("Enable", 0);
            break;
        case OP_EU:
            clear();
            args[14] = SELECTOROF(ume);
            args[15] = OFFSETOF(ume);
            call(pen);
            note("Enable USER", 0);
            break;
        case OP_ARM: {
            char what[80];
            arm(s->tab, s->n, s->mult, s->hold, s->gap);
            wsprintf(what, "arm %s n=%u mult=%u hold=%u gap=%u", (LPSTR)s->what,
                     s->n, s->mult, s->hold, s->gap);
            note(what, 0);
            break;
        }
        case OP_SWAL:
            m3[M_MODE] = 2;
            note("swallow", 0);
            break;
        case OP_PASS:
            m3[M_MODE] = 0;
            note("pass", 0);
            break;
        case OP_W:
            for (i = 0; i < s->n * 10; i++)
                pump_ticks(100);
            note("waited", s->n);
            break;
        case OP_WD:
            for (i = 0; i < s->mult * 10; i++) {
                if (m3[M_TABI] >= m3[M_TABN] || m3[M_TABI] >= s->n)
                    break;
                pump_ticks(100);
            }
            pump_ticks(300);
            note("delivered", m3[M_TABI]);
            break;
        }
    }
}

int __cdecl tmain(void)
{
    HMODULE hm = GetModuleHandle("MOUSE");
    WORD i, n, sel, exp, ls, ivtsel;
    WORD FAR *ivt;
    U_INITAPP(init_regs[5]);
    out("MD2 start\r\n");
    if (!hm) {
        out("no MOUSE\r\n");
        return 1;
    }
    GetModuleFileName(hm, line, sizeof line);
    out("OBS ~file=\"");
    out(line);
    out("\"\r\n");
    pen = GetProcAddress(hm, MAKEINTRESOURCE(2));
    pdis = GetProcAddress(hm, MAKEINTRESOURCE(3));
    ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
    bios = (WORD FAR *)MAKELP((WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0040H"), 0);

    /* MS3, through INT 66h's vector */
    ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    ls = AllocSelector(SELECTOROF((void FAR *)&i));
    SetSelectorBase(ls, (DWORD)ivt[0x66 * 2 + 1] << 4);
    SetSelectorLimit(ls, 0xFFFF);
    m3 = (WORD FAR *)MAKELP(ls, ivt[0x66 * 2]);
    if (!ivt[0x66 * 2 + 1] || m3[M_MAGIC] != 0x334D) {
        out("no MS3\r\n");
        return 1;
    }

    sel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, D_REC + MAXREC * 16L);
    if (!sel)
        return 1;
    blk = (WORD FAR *)MAKELP(sel, 0);
    for (i = 0; i < md2_len; i++)
        ((BYTE FAR *)blk)[H_OFF + i] = md2_code[i];
    blk[D_SEL / 2] = sel;
    blk[D_MAX / 2] = MAXREC;
    blk[D_NEXT / 2] = OFFSETOF(ume);
    blk[D_NEXT / 2 + 1] = SELECTOROF(ume);
    blk[D_BIOS / 2] = SELECTOROF(bios);
    blk[D_M3SEL / 2] = ls;
    blk[D_M3OFF / 2] = OFFSETOF(m3);
    csel = AllocDStoCSAlias(sel);

    /* wait for the input stream: MS3 swallows (USER's procedure still in
       place) until three BIOS packets have come */
    m3[M_NSWAL] = 0;
    m3[M_MODE] = 2;
    for (i = 0; i < 900 && m3[M_NSWAL] < 3; i++)
        pump_ticks(100);
    m3[M_MODE] = 0;
    note("input stream", m3[M_NSWAL]);

    exp = GetPrivateProfileInt("w16", "md2exp", 1, "SYSTEM.INI");
    wsprintf(line, "experiment %u\r\n", exp);
    out(line);
    run(exp == 2 ? exp2 : exp == 3 ? exp3 : exp1);

    clear();
    call(pdis);
    m3[M_MODE] = 0;
    clear();
    args[14] = SELECTOROF(ume);
    args[15] = OFFSETOF(ume);
    call(pen);
    note("USER's event procedure back", 0);

    n = blk[D_N / 2];
    wsprintf(line, "events %u nest %u\r\n", n, blk[D_NEST / 2]);
    out(line);
    for (i = 0; i < n; i++) {
        WORD FAR *e = blk + (D_REC + i * 16) / 2;
        wsprintf(line, "E %u %04X %d %d %u t=%u q=%u IF=%u\r\n", e[6], e[0], (int)e[1],
                 (int)e[2], e[3], e[4], e[5], (e[7] >> 9) & 1);
        out(line);
    }
    n = m3[M_COUNT];
    wsprintf(line, "ms3 records %u\r\n", n);
    out(line);
    for (i = 0; i < n; i++) {
        WORD FAR *r = (WORD FAR *)MAKELP(ls, m3[M_LOGP] + i * 16);
        wsprintf(line, "R %u %04X %04X %04X %04X t=%u d=%u #%u\r\n",
                 r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
        out(line);
    }
    out("MD2 done\r\n");
    return 0;
}
