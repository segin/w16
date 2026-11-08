/*
 * emu.c - WIN87EM's x87 emulator, used when there is no coprocessor.
 *
 * The loader turns each floating-point instruction of a program into an
 * INT (kernel/loader.c, os_fixup): INT 34h-3Bh for escape D8h-DFh
 * (followed by the ModR/M byte and displacement), INT 3Ch for one with a
 * segment prefix (followed by the escape byte, whose top two bits give
 * the segment: 00 DS, 01 SS, 10 CS, 11 ES), INT 3Dh for FWAIT. win87em.asm
 * sends them here with the registers (emu_frame); the instruction is
 * decoded, executed on the emulated register stack and IP moved past it.
 *
 * Semantics: Intel's 8087/80287/80387 documentation (instruction set,
 * register stack, tags, status and control words, exception masking,
 * rounding). Numbers are kept in the 80-bit extended format; arithmetic
 * is done on 64-bit significands with guard bits and rounded as the
 * control word says (precision and rounding control). WIN87EM-specific
 * behaviour (the __fpMath functions, how the program's exception handler
 * is called) was observed on Windows 3.1 (tests/apps/tk6.c).
 *
 * Implemented: loads and stores of all real and integer formats,
 * FLD/FST/FSTP/FXCH/FFREE/FINCSTP/FDECSTP, the constants, FADD/FSUB
 * /FSUBR/FMUL/FDIV/FDIVR in all forms, FCOM/FCOMP/FCOMPP/FICOM/FUCOM*,
 * FTST, FXAM, FCHS, FABS, FSQRT, FRNDINT, FSCALE, FPREM, F2XM1, FYL2X,
 * FYL2XP1, FPTAN, FPATAN, FCLEX/FLDCW/FSTCW/FSTSW/FLDENV/FSTENV/FSAVE/
 * FRSTOR (16-bit format), FNOP and the 8087/287 no-ops (FENI, FDISI,
 * FSETPM).
 * Not emulated, as observed of the 3.1 emulator (see unemulated(): the
 * handler gets 87h): FINIT (programs initialise through __fpMath), FBLD,
 * FBSTP, FXTRACT; likewise the 387's FPREM1, FSIN, FCOS, FSINCOS
 * (UNVERIFIED for those). INT 3Eh is ignored (UNVERIFIED: its use).
 * Checked against the 3.1 emulator with tests/apps/tk6.c, tk7.c, tk8.c
 * (ARCHITECTURE.md 9.2). Where its results come from its own
 * approximations (some square roots one unit in the last place off, the
 * transcendentals a few units, FPTAN's unnormalised pair) w16 computes
 * its own (correctly rounded or within about two units) instead.
 */
#include "emu.h"

#define MK_FP(s, o) ((void FAR *)(((u32)(s) << 16) | (u16)(o)))

u32 umul16(u16 a, u16 b);
#pragma aux umul16 = "mul dx" parm [ax] [dx] value [dx ax] modify exact [ax dx]
u16 get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax]

/* ---- the emulated coprocessor ---- */

typedef struct ext {            /* 80-bit extended real, memory layout */
    u16 m[4];                   /* significand, m[3] holds the integer bit */
    u16 se;                     /* sign << 15 | biased exponent */
} ext;

#define EXP_BIAS    0x3FFF
#define EXP_MAX     0x7FFF

static ext reg[8];              /* physical registers */
static u16 cw, sw, tw;          /* tw: 2 bits per physical register */

/* status word */
#define SW_IE   0x0001
#define SW_DE   0x0002
#define SW_ZE   0x0004
#define SW_OE   0x0008
#define SW_UE   0x0010
#define SW_PE   0x0020
#define SW_SF   0x0040
#define SW_ES   0x0080
#define SW_C0   0x0100
#define SW_C1   0x0200
#define SW_C2   0x0400
#define SW_C3   0x4000
#define SW_B    0x8000
#define SW_TOP  0x3800

/* FPE codes the program's handler gets in AL (observed: 83h for a zero
   divide, 88h for the square root of a negative number; the others as
   Microsoft's C run-time defines them, UNVERIFIED here) */
#define FPE_INVALID     0x81
#define FPE_DENORMAL    0x82
#define FPE_ZERODIVIDE  0x83
#define FPE_OVERFLOW    0x84
#define FPE_UNDERFLOW   0x85
#define FPE_INEXACT     0x86
#define FPE_UNEMULATED  0x87
#define FPE_SQRTNEG     0x88
#define FPE_STACKOVERFLOW  0x8A
#define FPE_STACKUNDERFLOW 0x8B

#define TAG_VALID   0
#define TAG_ZERO    1
#define TAG_SPECIAL 2
#define TAG_EMPTY   3

#define CW_INIT     0x1332      /* observed after __fpMath function 0 */

static u16 fault_code;          /* first unmasked exception of an insn */
static u16 cur_ds;              /* the program's DS, for the handler */

static u16 top(void) { return (sw >> 11) & 7; }
static void set_top(u16 t) { sw = (sw & ~SW_TOP) | ((t & 7) << 11); }
static u16 phys(u16 i) { return (top() + i) & 7; }
static u16 tag_of(u16 p) { return (tw >> (2 * p)) & 3; }
static void set_tag(u16 p, u16 t) { tw = (tw & ~(3u << (2 * p))) | (t << (2 * p)); }

/* Raise exception flags; an unmasked one is reported to the program's
   handler after the instruction (code: its FPE code). Observed with the
   Windows 3.1 emulator: the status word gets the flag but never ES (1/0
   with ZE unmasked leaves 0004h) except for the square root of a
   negative number (0081h), which also reaches the handler masked or not
   (the counts of handler calls agree only so); a stack underflow sets IE only
   (no SF); every flag also goes into the bits __fpMath function 8
   returns (em_acc) until function 9 or 0. */
static void raise(u16 flags, u16 code)
{
    if (code == FPE_SQRTNEG)
        flags |= SW_ES;
    sw |= flags;
    em_acc |= flags;
    if (((flags & 0x3F) & ~cw & 0x3F) || code == FPE_SQRTNEG) {
        if (!fault_code)
            fault_code = code;
    }
}

/* An instruction the emulator does not do. Observed for FINIT (which the
   Windows 3.1 emulator does not emulate: programs use __fpMath function
   0): SW bit 6 (0040h) is set and the handler is called with 87h
   (FPE_UNEMULATED) whatever the masks; nothing else changes.
   UNVERIFIED: which other instructions it leaves out; here those of the
   header comment. */
static void unemulated(void)
{
    sw |= SW_SF;
    em_acc |= SW_SF;
    if (!fault_code)
        fault_code = FPE_UNEMULATED;
}

static int masked(u16 flag)
{
    return (cw & flag) != 0;
}

/* ---- 80-bit values ---- */

static int is_zero(const ext *x)
{
    return (x->se & EXP_MAX) == 0 && !(x->m[0] | x->m[1] | x->m[2] | x->m[3]);
}

static int is_nan(const ext *x)
{
    return (x->se & EXP_MAX) == EXP_MAX &&
           ((x->m[3] & 0x7FFF) | x->m[2] | x->m[1] | x->m[0]);
}

static int is_inf(const ext *x)
{
    return (x->se & EXP_MAX) == EXP_MAX && x->m[3] == 0x8000 &&
           !(x->m[2] | x->m[1] | x->m[0]);
}

static int is_snan(const ext *x)
{
    return is_nan(x) && !(x->m[3] & 0x4000);
}

static void set_indefinite(ext *x)
{
    x->m[0] = x->m[1] = x->m[2] = 0;
    x->m[3] = 0xC000;
    x->se = 0xFFFF;
}

static void set_inf(ext *x, u16 sign)
{
    x->m[0] = x->m[1] = x->m[2] = 0;
    x->m[3] = 0x8000;
    x->se = sign | EXP_MAX;
}

static void set_zero(ext *x, u16 sign)
{
    x->m[0] = x->m[1] = x->m[2] = x->m[3] = 0;
    x->se = sign;
}

static u16 tag_for(const ext *x)
{
    u16 e = x->se & EXP_MAX;
    if (is_zero(x))
        return TAG_ZERO;
    if (e == EXP_MAX || e == 0 || !(x->m[3] & 0x8000))
        return TAG_SPECIAL;
    return TAG_VALID;
}

/* ---- unpacked numbers and wide significands ---- */

/* A finite nonzero number: value = W / 2^127 * 2^exp (W has bit 127 set
   after normalisation), W = w[7..0] (w[0] least significant). */
typedef struct unp {
    u16 sign;                   /* 0 or 8000h */
    s32 exp;
    u16 w[8];
} unp;

static void w_clear(u16 *w, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++)
        w[i] = 0;
}

static int w_is_zero(const u16 *w, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++)
        if (w[i])
            return 0;
    return 1;
}

/* shift right by k bits; bits shifted out OR into bit 0 (sticky) */
static void w_shr_sticky(u16 *w, u16 n, u32 k)
{
    u16 st = 0, i;
    while (k >= 16) {
        st |= w[0];
        for (i = 0; i + 1 < n; i++)
            w[i] = w[i + 1];
        w[n - 1] = 0;
        k -= 16;
        if (w_is_zero(w, n))
            k = 0;              /* nothing left to shift */
    }
    if (k) {
        st |= w[0] & ((1u << k) - 1);
        for (i = 0; i + 1 < n; i++)
            w[i] = (w[i] >> k) | (w[i + 1] << (16 - k));
        w[n - 1] >>= k;
    }
    if (st)
        w[0] |= 1;
}

/* plain shift right by k < 16 bits */
static void w_shr(u16 *w, u16 n, u16 k)
{
    u16 i;
    for (i = 0; i + 1 < n; i++)
        w[i] = (w[i] >> k) | (w[i + 1] << (16 - k));
    w[n - 1] >>= k;
}

static void w_shl(u16 *w, u16 n, u16 k)
{
    u16 i;
    while (k >= 16) {
        for (i = n - 1; i > 0; i--)
            w[i] = w[i - 1];
        w[0] = 0;
        k -= 16;
    }
    if (k) {
        for (i = n - 1; i > 0; i--)
            w[i] = (w[i] << k) | (w[i - 1] >> (16 - k));
        w[0] <<= k;
    }
}

static u16 w_add(u16 *a, const u16 *b, u16 n)
{
    u32 c = 0;
    u16 i;
    for (i = 0; i < n; i++) {
        c += (u32)a[i] + b[i];
        a[i] = (u16)c;
        c >>= 16;
    }
    return (u16)c;
}

static void w_sub(u16 *a, const u16 *b, u16 n)      /* a -= b, a >= b */
{
    u32 br = 0;
    u16 i;
    for (i = 0; i < n; i++) {
        u32 d = (u32)a[i] - b[i] - br;
        a[i] = (u16)d;
        br = (d >> 16) & 1;
    }
}

static int w_cmp(const u16 *a, const u16 *b, u16 n)
{
    u16 i = n;
    while (i--) {
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

/* normalise: bit 127 set; returns 0 if W is zero */
static int normalise(unp *u)
{
    u16 k = 0, t;
    if (w_is_zero(u->w, 8))
        return 0;
    while (!u->w[7]) {
        w_shl(u->w, 8, 16);
        u->exp -= 16;
    }
    for (t = u->w[7]; !(t & 0x8000); t <<= 1)
        k++;
    w_shl(u->w, 8, k);
    u->exp -= k;
    return 1;
}

/* W with its top `bits` bits set (the largest significand) */
static void w_ones(u16 *w, u16 bits)
{
    u16 i, cut = 128 - bits;
    for (i = 0; i < 8; i++) {
        if (cut >= 16) {
            w[i] = 0;
            cut -= 16;
        } else {
            w[i] = (u16)(0xFFFFu << cut);
            cut = 0;
        }
    }
}

/* add v at word k (carry propagates) */
static void w_add32(u16 *w, u16 n, u16 k, u32 v)
{
    while (v && k < n) {
        u32 s = (u32)w[k] + (u16)v;
        w[k] = (u16)s;
        v = (v >> 16) + (s >> 16);
        k++;
    }
}

/* ext -> unp; for finite nonzero values (denormals are normalised, DE) */
static void unpack(const ext *x, unp *u)
{
    u16 e = x->se & EXP_MAX;
    u->sign = x->se & 0x8000;
    w_clear(u->w, 4);
    u->w[4] = x->m[0];
    u->w[5] = x->m[1];
    u->w[6] = x->m[2];
    u->w[7] = x->m[3];
    u->exp = (s32)(e ? e : 1) - EXP_BIAS;
    if (!e || !(x->m[3] & 0x8000))
        raise(SW_DE, FPE_DENORMAL);
    normalise(u);
}

/* Round W to `bits` significant bits by the rounding control; returns 1
   if inexact. May carry into a new top bit (exp + 1). */
static int round_bits(unp *u, u16 bits)
{
    u16 rc = (cw >> 10) & 3, i;
    u16 cut = 128 - bits;       /* number of bits to drop */
    u16 widx = cut / 16, bidx = cut % 16;
    u16 half, rest = 0, lsb, up = 0, inexact;
    u16 add[8];

    /* the dropped bits: half = the first dropped bit, rest = any below */
    if (bidx) {
        half = (u->w[widx] >> (bidx - 1)) & 1;
        rest = u->w[widx] & ((1u << (bidx - 1)) - 1);
        lsb = (u->w[widx] >> bidx) & 1;
    } else {
        half = (u->w[widx - 1] >> 15) & 1;
        rest = u->w[widx - 1] & 0x7FFF;
        lsb = u->w[widx] & 1;
    }
    for (i = 0; i < widx - (bidx ? 0 : 1); i++)
        rest |= u->w[i];
    inexact = half || rest;
    if (!inexact)
        return 0;
    switch (rc) {
    case 0: up = half && (rest || lsb); break;      /* nearest even */
    case 1: up = u->sign != 0; break;               /* down */
    case 2: up = u->sign == 0; break;               /* up */
    case 3: up = 0; break;                          /* chop */
    }
    /* clear the dropped bits */
    for (i = 0; i < widx; i++)
        u->w[i] = 0;
    u->w[widx] &= ~((1u << bidx) - 1);
    if (up) {
        w_clear(add, 8);
        add[widx] = 1u << bidx;
        if (w_add(u->w, add, 8)) {
            /* carried out of bit 127: the value is 2^(exp+1) */
            u->w[7] = 0x8000;
            u->exp++;
        }
    }
    return 1;
}

/* The precision control's significand size (24, 53 or 64 bits). */
static u16 pc_bits(void)
{
    switch ((cw >> 8) & 3) {
    case 0: return 24;
    case 2: return 53;
    default: return 64;
    }
}

/* Round and pack a normalised unp into an extended register value, with
   overflow (OE) and underflow (UE) handling for the given exponent range
   (emin..emax, unbiased) and significand size; the result is in extended
   format either way (memory formats are packed by the store routines). */
static void pack_range(unp *u, ext *x, u16 bits, s32 emin, s32 emax)
{
    if (u->exp < emin) {
        /* tiny: denormalise (masked underflow response) */
        w_shr_sticky(u->w, 8, (u32)(emin - u->exp));
        u->exp = emin;
        if (round_bits(u, bits))
            raise(SW_UE | SW_PE, FPE_UNDERFLOW);
        else if (!masked(SW_UE))
            raise(SW_UE, FPE_UNDERFLOW);
        if (!(u->w[7] & 0x8000)) {
            /* stays denormal (or zero): W/2^127 * 2^emin with bit 127
               clear; an extended denormal has exponent field 0 */
            x->m[0] = u->w[4];
            x->m[1] = u->w[5];
            x->m[2] = u->w[6];
            x->m[3] = u->w[7];
            x->se = u->sign | (emin == 1 - EXP_BIAS ? 0 : (u16)(emin + EXP_BIAS));
            return;
        }
    } else if (round_bits(u, bits)) {
        raise(SW_PE, FPE_INEXACT);
    }
    if (u->exp > emax) {
        u16 rc = (cw >> 10) & 3;
        raise(SW_OE | SW_PE, FPE_OVERFLOW);
        if (rc == 0 || (rc == 1 && u->sign) || (rc == 2 && !u->sign)) {
            set_inf(x, u->sign);
            return;
        }
        /* the largest finite number of this precision */
        w_ones(u->w, bits);
        u->exp = emax;
    }
    x->m[0] = u->w[4];
    x->m[1] = u->w[5];
    x->m[2] = u->w[6];
    x->m[3] = u->w[7];
    x->se = u->sign | (u16)(u->exp + EXP_BIAS);
}

/* an arithmetic result into an extended register (precision control) */
static void pack(unp *u, ext *x)
{
    pack_range(u, x, pc_bits(), 1 - EXP_BIAS, EXP_BIAS);
}

/* ---- the register stack ---- */

static ext *st(u16 i)
{
    return &reg[phys(i)];
}

static int st_empty(u16 i)
{
    return tag_of(phys(i)) == TAG_EMPTY;
}

/* stack underflow: IE (observed: no SF), and the handler is called with
   8Bh whatever the masks (observed) */
static void underflow(void)
{
    raise(SW_IE, FPE_STACKUNDERFLOW);
    if (!fault_code)                    /* reported masked or not */
        fault_code = FPE_STACKUNDERFLOW;
}

/* A store from ST(0) to memory. Observed with the 3.1 emulator (an FSTP
   TBYTE from an empty stack): no exception flag, the register's stale
   contents are stored, and the handler is called with 8Bh (stack
   underflow) although IE was masked. Returns 1: the store goes ahead. */
static int need_store(void)
{
    if (st_empty(0) && !fault_code)
        fault_code = FPE_STACKUNDERFLOW;
    return 1;
}

/* The register stack is deeper than eight (observed: 20 values pushed
   came back intact). Registers below ST(7) are spilled to a block that
   grows as needed (spill, spill_n); ST(0)-ST(7) stay in reg[]. The
   handler is called once with 8Ah (stack overflow), masked or not, when
   the depth first exceeds DEEP_REPORT (observed: at the 15th push, of 20),
   and no flag is set. */
#define DEEP_REPORT     14

u16 __far __pascal GLOBALALLOC(u16 flags, u32 size);    /* KERNEL.15 */
u16 __far __pascal GLOBALFREE(u16 h);                   /* KERNEL.17 */

static u16 spill_sel, spill_cap, spill_n;   /* selector, capacity, used */

/* room for one more spilled register; 0 if out of memory */
static int spill_room(void)
{
    if (spill_n == spill_cap) {
        u16 ncap = spill_cap ? spill_cap * 2 : 8, sel, i;
        if (ncap > 0xFFF0u / sizeof(ext))
            return 0;                   /* one segment's worth */
        sel = GLOBALALLOC(0x0040, umul16(ncap, sizeof(ext)));    /* GMEM_ZEROINIT */
        if (!sel)
            return 0;
        for (i = 0; i < spill_n; i++)
            ((ext FAR *)MK_FP(sel, 0))[i] = ((ext FAR *)MK_FP(spill_sel, 0))[i];
        if (spill_sel)
            GLOBALFREE(spill_sel);
        spill_sel = sel;
        spill_cap = ncap;
    }
    return 1;
}

static u16 depth_now(void)
{
    u16 i, n = 0;
    for (i = 0; i < 8; i++)
        if (tag_of(i) != TAG_EMPTY)
            n++;
    return n + spill_n;
}

static int push(const ext *x)
{
    u16 t = (top() - 1) & 7;
    if (tag_of(t) != TAG_EMPTY) {
        /* ST(7) goes below the eight registers */
        if (!spill_room()) {
            raise(SW_IE, FPE_STACKOVERFLOW);    /* UNVERIFIED */
            return 0;
        }
        ((ext FAR *)MK_FP(spill_sel, 0))[spill_n++] = reg[t];
        set_tag(t, TAG_EMPTY);
        if (depth_now() == DEEP_REPORT && !fault_code)
            fault_code = FPE_STACKOVERFLOW;
    }
    set_top(t);
    reg[t] = *x;
    set_tag(t, tag_for(x));
    return 1;
}

static void pop(void)
{
    u16 t = top();
    set_tag(t, TAG_EMPTY);
    set_top(t + 1);
    if (spill_n) {
        /* the slot just freed is the new ST(7) */
        reg[t] = ((ext FAR *)MK_FP(spill_sel, 0))[--spill_n];
        set_tag(t, tag_for(&reg[t]));
    }
}

static void set_st(u16 i, const ext *x)
{
    u16 p = phys(i);
    reg[p] = *x;
    set_tag(p, tag_for(x));
}

/* ---- NaN handling for two-operand operations ---- */

/* If either operand is a NaN, the result is the (quieted) NaN with the
   larger significand (Intel's rule); an SNaN raises IE. Returns 1 if a
   NaN result was made. */
static int nan_result(const ext *a, const ext *b, ext *r)
{
    const ext *p;
    int an = is_nan(a), bn = b && is_nan(b);
    if (!an && !bn)
        return 0;
    if ((an && is_snan(a)) || (bn && is_snan(b)))
        raise(SW_IE, FPE_INVALID);
    if (an && bn) {
        u16 i = 4;
        p = a;
        while (i--) {
            if (a->m[i] != b->m[i]) {
                p = a->m[i] > b->m[i] ? a : b;
                break;
            }
        }
    } else {
        p = an ? a : b;
    }
    *r = *p;
    r->m[3] |= 0x4000;
    return 1;
}

static void invalid_result(ext *r, u16 code)
{
    raise(SW_IE, code);
    set_indefinite(r);
}

/* ---- arithmetic ---- */

static void add_sub(const ext *a, const ext *b0, ext *r, int subtract)
{
    ext b = *b0;
    unp ua, ub;
    s32 d;
    if (subtract)
        b.se ^= 0x8000;
    if (nan_result(a, &b, r))
        return;
    if (is_inf(a) || is_inf(&b)) {
        if (is_inf(a) && is_inf(&b) && ((a->se ^ b.se) & 0x8000)) {
            invalid_result(r, FPE_INVALID);
            return;
        }
        *r = is_inf(a) ? *a : b;
        return;
    }
    if (is_zero(a) && is_zero(&b)) {
        u16 rc = (cw >> 10) & 3;
        if ((a->se ^ b.se) & 0x8000)
            set_zero(r, rc == 1 ? 0x8000 : 0);
        else
            set_zero(r, a->se & 0x8000);
        return;
    }
    if (is_zero(a)) {
        unpack(&b, &ub);
        pack(&ub, r);
        return;
    }
    if (is_zero(&b)) {
        unpack(a, &ua);
        pack(&ua, r);
        return;
    }
    unpack(a, &ua);
    unpack(&b, &ub);
    /* significands in the top half, one bit of headroom for the carry */
    w_shr_sticky(ua.w, 8, 1);
    w_shr_sticky(ub.w, 8, 1);
    ua.exp++;
    ub.exp++;
    d = ua.exp - ub.exp;
    if (d < 0) {
        unp t = ua;
        ua = ub;
        ub = t;
        d = -d;
    }
    w_shr_sticky(ub.w, 8, d > 200 ? 200 : (u32)d);
    ub.exp = ua.exp;
    if (ua.sign == ub.sign) {
        w_add(ua.w, ub.w, 8);
    } else {
        int c = w_cmp(ua.w, ub.w, 8);
        if (c == 0) {
            u16 rc = (cw >> 10) & 3;
            set_zero(r, rc == 1 ? 0x8000 : 0);
            return;
        }
        if (c < 0) {
            unp t = ua;
            ua = ub;
            ub = t;
        }
        w_sub(ua.w, ub.w, 8);
    }
    normalise(&ua);
    pack(&ua, r);
}

static void mul(const ext *a, const ext *b, ext *r)
{
    unp ua, ub, p;
    u16 i, j;
    if (nan_result(a, b, r))
        return;
    if (is_inf(a) || is_inf(b)) {
        if (is_zero(a) || is_zero(b)) {
            invalid_result(r, FPE_INVALID);
            return;
        }
        set_inf(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    if (is_zero(a) || is_zero(b)) {
        set_zero(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    unpack(a, &ua);
    unpack(b, &ub);
    w_clear(p.w, 8);
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            w_add32(p.w, 8, i + j, umul16(ua.w[4 + i], ub.w[4 + j]));
    p.sign = (a->se ^ b->se) & 0x8000;
    p.exp = ua.exp + ub.exp + 1;
    normalise(&p);
    pack(&p, r);
}

static void divide(const ext *a, const ext *b, ext *r)
{
    unp ua, ub, q;
    u16 rem[5], d[5], i;
    if (nan_result(a, b, r))
        return;
    if (is_inf(a)) {
        if (is_inf(b))
            invalid_result(r, FPE_INVALID);
        else
            set_inf(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    if (is_inf(b)) {
        set_zero(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    if (is_zero(b)) {
        if (is_zero(a)) {
            /* observed (3.1 emulator): 0/0 sets ZE as well as IE */
            raise(SW_ZE, FPE_INVALID);
            invalid_result(r, FPE_INVALID);
            return;
        }
        raise(SW_ZE, FPE_ZERODIVIDE);
        set_inf(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    if (is_zero(a)) {
        set_zero(r, (a->se ^ b->se) & 0x8000);
        return;
    }
    unpack(a, &ua);
    unpack(b, &ub);
    for (i = 0; i < 4; i++) {
        rem[i] = ua.w[4 + i];
        d[i] = ub.w[4 + i];
    }
    rem[4] = d[4] = 0;
    w_clear(q.w, 8);
    /* 68 quotient bits, the first worth 2^0 */
    for (i = 0; i < 68; i++) {
        u16 bit = 127 - i;
        if (w_cmp(rem, d, 5) >= 0) {
            w_sub(rem, d, 5);
            q.w[bit / 16] |= 1u << (bit % 16);
        }
        w_shl(rem, 5, 1);
    }
    if (!w_is_zero(rem, 5))
        q.w[0] |= 1;
    q.sign = (a->se ^ b->se) & 0x8000;
    q.exp = ua.exp - ub.exp;
    normalise(&q);
    pack(&q, r);
}

/* integer square root of X (10 words): R = floor(sqrt(X)), X = remainder */
static void isqrt(u16 *x, u16 *res)
{
    u16 one[10], t[10], i;
    w_clear(res, 10);
    w_clear(one, 10);
    one[9] = 0x4000;                    /* 2^158, the largest power of 4 */
    while (w_cmp(one, x, 10) > 0)
        w_shr(one, 10, 2);
    while (!w_is_zero(one, 10)) {
        for (i = 0; i < 10; i++)
            t[i] = res[i];
        w_add(t, one, 10);
        w_shr(res, 10, 1);
        if (w_cmp(x, t, 10) >= 0) {
            w_sub(x, t, 10);
            w_add(res, one, 10);
        }
        w_shr(one, 10, 2);
    }
}

static void square_root(const ext *a, ext *r)
{
    unp u, res;
    u16 x[10], root[10], i, s;
    s32 ex;
    if (nan_result(a, NULL, r))
        return;
    if (is_zero(a)) {
        *r = *a;
        return;
    }
    if (a->se & 0x8000) {
        invalid_result(r, FPE_SQRTNEG);
        return;
    }
    if (is_inf(a)) {
        /* observed: the 3.1 emulator takes the square root of +infinity
           as invalid, reported like that of a negative number (88h) */
        invalid_result(r, FPE_SQRTNEG);
        return;
    }
    unpack(a, &u);
    /* a = M * 2^(e - 63), M = W's top 64 bits. With X = M * 2^s and s
       68 or 69 so that e - 63 - s is even: sqrt(a) = sqrt(X) *
       2^((e - 63 - s) / 2); sqrt(X) has 66 or 67 bits, enough to round
       to 64 with a sticky bit for the remainder. */
    s = (u.exp & 1) ? 68 : 69;
    ex = u.exp - 63 - s;
    w_clear(x, 10);
    for (i = 0; i < 4; i++)
        x[i] = u.w[4 + i];
    w_shl(x, 10, s);
    isqrt(x, root);
    w_clear(res.w, 8);
    for (i = 0; i < 5; i++)
        res.w[i] = root[i];
    if (!w_is_zero(x, 10))
        res.w[0] |= 1;
    res.sign = 0;
    res.exp = (ex >> 1) + 127;          /* W = R: value R * 2^(ex/2), ex even */
    normalise(&res);
    pack(&res, r);
}

/* round to an integer in extended format by the rounding control */
static void round_int(const ext *a, ext *r)
{
    unp u;
    if (is_nan(a)) {
        nan_result(a, NULL, r);
        return;
    }
    if (is_zero(a)) {
        raise(SW_PE, FPE_INEXACT);      /* observed (3.1 emulator) */
        *r = *a;
        return;
    }
    if (is_inf(a)) {
        *r = *a;
        return;
    }
    unpack(a, &u);
    if (u.exp >= 63) {
        *r = *a;
        return;
    }
    if (u.exp < 0) {
        /* |a| < 1: keep one (zero) integer bit */
        w_shr_sticky(u.w, 8, (u32)-u.exp);
        u.exp = 0;
    }
    /* round at the binary point: keep exp + 1 bits */
    if (round_bits(&u, (u16)(u.exp + 1)))
        raise(SW_PE, FPE_INEXACT);
    if (w_is_zero(u.w, 8)) {
        set_zero(r, u.sign);
        return;
    }
    normalise(&u);
    pack_range(&u, r, 64, 1 - EXP_BIAS, EXP_BIAS);
}

/* ---- conversions to and from memory formats ---- */

static void from_int(s32 lo, s32 hi, int wide, ext *x)  /* hi:lo 64-bit */
{
    unp u;
    u16 neg = (u16)((wide ? hi : lo) < 0);
    w_clear(u.w, 8);
    if (wide) {
        u.w[4] = (u16)lo;
        u.w[5] = (u16)((u32)lo >> 16);
        u.w[6] = (u16)hi;
        u.w[7] = (u16)((u32)hi >> 16);
    } else {
        u.w[4] = (u16)lo;
        u.w[5] = (u16)((u32)lo >> 16);
        if (neg)
            u.w[6] = u.w[7] = 0xFFFF;
    }
    if (neg) {
        u16 i, c = 1;
        for (i = 4; i < 8; i++) {
            u.w[i] = ~u.w[i] + c;
            c = c && u.w[i] == 0;
        }
    }
    if (w_is_zero(u.w, 8)) {
        set_zero(x, 0);
        return;
    }
    u.sign = neg ? 0x8000 : 0;
    u.exp = 63;         /* n at bit 64 of W: value = W/2^127 * 2^63 */
    normalise(&u);
    pack_range(&u, x, 64, 1 - EXP_BIAS, EXP_BIAS);
}

/* Integer of `bits` bits (16, 32, 64) from ST, rounded; out of range or
   NaN: IE and the integer indefinite. Result in out[0..3] (words). */
static void to_int(const ext *a, u16 bits, u16 *out)
{
    unp u;
    ext rr;
    u16 i, n = bits / 16;
    for (i = 0; i < 4; i++)
        out[i] = 0;
    if (is_nan(a) || is_inf(a)) {
        raise(SW_IE, FPE_INVALID);
        out[n - 1] = 0x8000;
        return;
    }
    if (is_zero(a))
        return;
    {
        /* out of range: IE only, no PE from the rounding (observed) */
        u16 sw0 = sw, acc0 = em_acc, fc0 = fault_code;
        round_int(a, &rr);
        if (is_zero(&rr))
            return;
        unpack(&rr, &u);
        if (u.exp >= (s32)bits - 1 &&
            !(u.sign && u.exp == (s32)bits - 1 && u.w[7] == 0x8000 &&
              w_is_zero(u.w, 7))) {     /* only -2^(bits-1) fits */
            sw = sw0;
            em_acc = acc0;
            fault_code = fc0;
            raise(SW_IE, FPE_INVALID);
            out[n - 1] = 0x8000;
            return;
        }
    }
    /* integer = W >> (127 - exp) */
    w_shr_sticky(u.w, 8, (u32)(127 - u.exp));
    for (i = 0; i < n; i++)
        out[i] = u.w[i];
    if (u.sign) {
        u16 c = 1;
        for (i = 0; i < n; i++) {
            out[i] = ~out[i] + c;
            c = c && out[i] == 0;
        }
    }
}

/* real formats: 32 (8-bit exponent, 23 fraction bits), 64 (11, 52) */
static void from_real(const u16 FAR *p, u16 bits, ext *x)
{
    u16 ebits = bits == 32 ? 8 : 11;
    u16 n = bits / 16, top = p[n - 1], i;
    u16 sign = top & 0x8000;
    u16 e = (top & 0x7FFF) >> (15 - ebits);
    u16 emax = (1u << ebits) - 1, bias = emax >> 1;
    unp u;
    w_clear(u.w, 8);
    for (i = 0; i < n; i++)
        u.w[8 - n + i] = p[i];
    /* fraction to the top: shift left past sign and exponent */
    w_shl(u.w, 8, 1 + ebits);
    if (e == emax) {                    /* infinity or NaN: 1.f */
        w_shr_sticky(u.w, 8, 1);
        x->m[0] = u.w[4];
        x->m[1] = u.w[5];
        x->m[2] = u.w[6];
        x->m[3] = u.w[7] | 0x8000;
        x->se = sign | EXP_MAX;
        return;
    }
    if (e == 0 && w_is_zero(u.w, 8)) {
        /* observed (3.1 emulator): -0.0 loads as +0 (or is stored so;
           UNVERIFIED which) */
        set_zero(x, 0);
        return;
    }
    /* W = 0.f (fraction at bit 127 down); value = (e ? 1.f : 0.f) * 2^(e - bias) */
    w_shr_sticky(u.w, 8, 1);
    if (e) {
        u.w[7] |= 0x8000;
        u.exp = (s32)e - bias;
    } else {
        raise(SW_DE, FPE_DENORMAL);
        u.exp = 1 - (s32)bias;
    }
    u.sign = sign;
    normalise(&u);
    pack_range(&u, x, 64, 1 - EXP_BIAS, EXP_BIAS);
}

static void to_real(const ext *a, u16 bits, u16 FAR *p)
{
    u16 ebits = bits == 32 ? 8 : 11, mbits = bits == 32 ? 24 : 53;
    u16 n = bits / 16, emaxb = (1u << ebits) - 1, bias = emaxb >> 1, i;
    u16 w[8];
    ext r;
    unp u;
    u16 sign = a->se & 0x8000;
    u16 e;

    w_clear(w, 8);
    if (is_zero(a)) {
        for (i = 0; i < n; i++)
            p[i] = 0;
        p[n - 1] = sign;
        return;
    }
    if (is_nan(a) || is_inf(a)) {
        ext q = *a;
        if (is_snan(a)) {
            raise(SW_IE, FPE_INVALID);
            if (!masked(SW_IE))
                return;
            q.m[3] |= 0x4000;
        }
        w[4] = q.m[0];
        w[5] = q.m[1];
        w[6] = q.m[2];
        w[7] = q.m[3];
        w_shl(w, 8, 1);                 /* drop the integer bit */
        w_shr_sticky(w, 8, 1 + ebits);
        for (i = 0; i < n; i++)
            p[i] = w[8 - n + i];
        p[n - 1] |= sign | (emaxb << (15 - ebits));
        if (is_nan(a) && !(p[0] | p[1] | (n > 2 ? p[2] : 0) |
                           (p[n - 1] & ((1u << (15 - ebits)) - 1))))
            p[n - 1] |= 1u << (14 - ebits);
        return;
    }
    unpack(a, &u);
    pack_range(&u, &r, mbits, 1 - (s32)bias, (s32)bias);
    if ((r.se & EXP_MAX) == EXP_MAX) {
        for (i = 0; i < n; i++)
            p[i] = 0;
        p[n - 1] = sign | (emaxb << (15 - ebits));
        return;
    }
    /* r: rounded; exponent field */
    w[4] = r.m[0];
    w[5] = r.m[1];
    w[6] = r.m[2];
    w[7] = r.m[3];
    if (r.m[3] & 0x8000)
        e = (u16)((s16)((r.se & EXP_MAX) - EXP_BIAS) + bias);
    else
        e = 0;                          /* denormal */
    w_shl(w, 8, 1);                     /* drop the integer bit */
    w_shr_sticky(w, 8, 1 + ebits);
    for (i = 0; i < n; i++)
        p[i] = w[8 - n + i];
    p[n - 1] = (p[n - 1] & ((1u << (15 - ebits)) - 1)) | sign | (e << (15 - ebits));
}

/* ---- comparisons ---- */

/* compare a with b: sets C3 C2 C0; unordered raises IE (always for an
   SNaN, for any NaN unless `quiet`) */
static void compare(const ext *a, const ext *b, int quiet)
{
    int c;
    sw &= ~(SW_C3 | SW_C2 | SW_C0 | SW_C1);
    if (is_nan(a) || is_nan(b)) {
        if (!quiet || is_snan(a) || is_snan(b))
            raise(SW_IE, FPE_INVALID);
        sw |= SW_C3 | SW_C2 | SW_C0;
        return;
    }
    if (is_zero(a) && is_zero(b)) {
        c = 0;
    } else if ((a->se ^ b->se) & 0x8000) {
        c = (a->se & 0x8000) ? -1 : 1;
    } else {
        /* same sign: compare magnitudes (normalised forms) */
        u16 ea = a->se & EXP_MAX, eb = b->se & EXP_MAX;
        unp ua, ub;
        if (is_inf(a) || is_inf(b)) {
            c = is_inf(a) && is_inf(b) ? 0 : is_inf(a) ? 1 : -1;
        } else if (is_zero(a)) {
            c = -1;
        } else if (is_zero(b)) {
            c = 1;
        } else {
            unpack(a, &ua);
            unpack(b, &ub);
            if (ua.exp != ub.exp)
                c = ua.exp > ub.exp ? 1 : -1;
            else
                c = w_cmp(ua.w, ub.w, 8);
        }
        if (a->se & 0x8000)
            c = -c;
        (void)ea;
        (void)eb;
    }
    if (c < 0)
        sw |= SW_C0;
    else if (c == 0)
        sw |= SW_C3;
}

static void examine(void)
{
    const ext *a = st(0);
    sw &= ~(SW_C3 | SW_C2 | SW_C1 | SW_C0);
    if (a->se & 0x8000)
        sw |= SW_C1;
    if (st_empty(0))
        sw |= SW_C3 | SW_C0;
    else if (is_nan(a))
        sw |= SW_C0;
    else if (is_inf(a))
        sw |= SW_C2 | SW_C0;
    else if (is_zero(a))
        sw |= SW_C3;
    else if (!(a->se & EXP_MAX))
        sw |= SW_C3 | SW_C2;            /* denormal */
    else if (!(a->m[3] & 0x8000))
        ;                               /* unnormal: unsupported, 000 */
    else
        sw |= SW_C2;                    /* normal */
}

/* ---- memory access and decoding ---- */

typedef struct insn {
    u16 esc;                    /* 0-7: D8h-DFh */
    u16 mod, reg, rm;
    u16 seg, off;               /* memory operand (mod != 3) */
} insn;

static u8 code_byte(emu_frame FAR *f)
{
    u8 b = *(u8 FAR *)MK_FP(f->cs, f->ip);
    f->ip++;
    return b;
}

static void decode_ea(emu_frame FAR *f, insn *in, int seg_override, u16 seg)
{
    u16 off = 0, defss = 0;
    if (in->mod == 3)
        return;
    switch (in->rm) {
    case 0: off = f->bx + f->si; break;
    case 1: off = f->bx + f->di; break;
    case 2: off = f->bp + f->si; defss = 1; break;
    case 3: off = f->bp + f->di; defss = 1; break;
    case 4: off = f->si; break;
    case 5: off = f->di; break;
    case 6:
        if (in->mod == 0) {
            off = code_byte(f);
            off |= (u16)code_byte(f) << 8;
        } else {
            off = f->bp;
            defss = 1;
        }
        break;
    case 7: off = f->bx; break;
    }
    if (in->mod == 1) {
        off += (u16)(s16)(signed char)code_byte(f);
    } else if (in->mod == 2) {
        u16 d = code_byte(f);
        d |= (u16)code_byte(f) << 8;
        off += d;
    }
    in->off = off;
    if (seg_override)
        in->seg = seg;
    else
        in->seg = defss ? get_ss() : f->ds;
}

static void FAR *mem(const insn *in)
{
    return MK_FP(in->seg, in->off);
}

/* ---- environment ---- */

/* The status word as the program sees it: TOP always reads 0 (observed:
   the Windows 3.1 emulator does not report it). */
static u16 sw_out(void)
{
    return sw & ~SW_TOP;
}

static void store_env(u16 FAR *p)          /* 14 bytes, 16-bit format */
{
    p[0] = cw;
    p[1] = sw_out();
    p[2] = tw;
    p[3] = p[4] = p[5] = p[6] = 0;     /* instruction/operand pointers */
}

static void load_env(const u16 FAR *p)
{
    cw = p[0];
    sw = p[1];
    tw = p[2];
}

void __cdecl __far emu_reset(void)
{
    u16 i;
    cw = CW_INIT;
    sw = 0;
    tw = 0xFFFF;
    spill_n = 0;
    /* observed once: a register never used stores as 0 with exponent
       3FFFh (UNVERIFIED as the general rule) */
    for (i = 0; i < 8; i++) {
        reg[i].m[0] = reg[i].m[1] = reg[i].m[2] = reg[i].m[3] = 0;
        reg[i].se = EXP_BIAS;
    }
}

static void finit(void)
{
    cw = 0x037F;
    sw = 0;
    tw = 0xFFFF;
}

/* ---- constants (Intel's values, round to nearest) ---- */

static const ext k_one = { { 0, 0, 0, 0x8000 }, 0x3FFF };
static const ext k_l2t = { { 0x8AFE, 0xCD1B, 0x784B, 0xD49A }, 0x4000 };
static const ext k_l2e = { { 0xF0BC, 0x5C17, 0x3B29, 0xB8AA }, 0x3FFF };
static const ext k_pi  = { { 0xC235, 0x2168, 0xDAA2, 0xC90F }, 0x4000 };
static const ext k_lg2 = { { 0xF799, 0xFBCF, 0x9A84, 0x9A20 }, 0x3FFD };
static const ext k_ln2 = { { 0x79AC, 0xD1CF, 0x17F7, 0xB172 }, 0x3FFE };
static const ext k_zero = { { 0, 0, 0, 0 }, 0 };

/* ---- the instructions ---- */

enum { OP_ADD, OP_MUL, OP_COM, OP_COMP, OP_SUB, OP_SUBR, OP_DIV, OP_DIVR };

/* ST(dst) = ST(dst) op x (op from the reg field) */
static void arith(u16 op, u16 dst, const ext *x)
{
    ext r, a;
    if (st_empty(dst)) {
        underflow();
        if (masked(SW_IE)) {
            set_indefinite(&r);
            set_st(dst, &r);
        }
        return;
    }
    a = *st(dst);
    switch (op) {
    case OP_ADD:  add_sub(&a, x, &r, 0); break;
    case OP_MUL:  mul(&a, x, &r); break;
    case OP_COM:
    case OP_COMP: compare(&a, x, 0); return;
    case OP_SUB:  add_sub(&a, x, &r, 1); break;
    case OP_SUBR: add_sub(x, &a, &r, 1); break;
    case OP_DIV:  divide(&a, x, &r); break;
    case OP_DIVR: divide(x, &a, &r); break;
    }
    /* UNVERIFIED: with an unmasked exception the masked result is stored
       too (the handler is called afterwards, emu_entry) */
    set_st(dst, &r);
}

/* register forms of D8/DC/DE: op between ST(0) and ST(i) */
static void arith_reg(u16 esc, u16 op, u16 i)
{
    ext x;
    if (st_empty(i) || st_empty(0)) {
        underflow();
        return;
    }
    if (esc == 0) {             /* D8: ST(0) = ST(0) op ST(i) */
        x = *st(i);
        arith(op, 0, &x);
        if (op == OP_COMP)
            pop();
        return;
    }
    /* DC/DE: ST(i) = ST(i) op ST(0); sub/div are the reversed pairs */
    if (op == OP_SUB)
        op = OP_SUBR;
    else if (op == OP_SUBR)
        op = OP_SUB;
    else if (op == OP_DIV)
        op = OP_DIVR;
    else if (op == OP_DIVR)
        op = OP_DIV;
    x = *st(0);
    if (op == OP_COM || op == OP_COMP) {
        ext a = *st(0);
        x = *st(i);
        compare(&a, &x, 0);
        if (op == OP_COMP || esc == 6)
            pop();
        return;
    }
    arith(op, i, &x);
    if (esc == 6)
        pop();
}

static void load(const ext *x)
{
    push(x);
}

static int need(u16 i)
{
    if (st_empty(i)) {
        underflow();
        return 0;
    }
    return 1;
}

static void fpu_unemulated(void)
{
    unemulated();
}

/* ---- internal arithmetic for FPREM, FSCALE and the transcendentals ---- */

/* These run with their own control word (round to nearest, 64 bits, all
   masked) and leave the status word alone; the instruction sets its
   flags itself. */
typedef struct saved_st {
    u16 cw, sw, acc, fc;
} saved_st;

static void quiet_begin(saved_st *q)
{
    q->cw = cw;
    q->sw = sw;
    q->acc = em_acc;
    q->fc = fault_code;
    cw = 0x037F;
}

static void quiet_end(const saved_st *q)
{
    cw = q->cw;
    sw = q->sw;
    em_acc = q->acc;
    fault_code = q->fc;
}

static ext e_add(ext a, ext b) { ext r; add_sub(&a, &b, &r, 0); return r; }
static ext e_sub(ext a, ext b) { ext r; add_sub(&a, &b, &r, 1); return r; }
static ext e_mul(ext a, ext b) { ext r; mul(&a, &b, &r); return r; }
static ext e_div(ext a, ext b) { ext r; divide(&a, &b, &r); return r; }

static ext e_int(s16 n)
{
    ext r;
    from_int(n, 0, 0, &r);
    return r;
}

/* x * 2^k (exact within the range) */
static ext e_scale(ext x, s16 k)
{
    if (!is_zero(&x))
        x.se = (x.se & 0x8000) | (u16)((x.se & EXP_MAX) + k);
    return x;
}

static int is_neg(const ext *x)
{
    return (x->se & 0x8000) != 0;
}

/* the term no longer changes the sum (70 bits below it) */
static int small_vs(const ext *term, const ext *sum)
{
    if (is_zero(term))
        return 1;
    return (s16)(term->se & EXP_MAX) < (s16)(sum->se & EXP_MAX) - 70;
}

/* t + t^3/3 + t^5/5 + ... (alternating signs if alt): atanh, atan */
static ext odd_series(ext t, int alt)
{
    ext t2 = e_mul(t, t), p = t, sum = t, term;
    s16 n;
    for (n = 3; n < 400; n += 2) {
        p = e_mul(p, t2);
        term = e_div(p, e_int(n));
        if (alt && (n & 2))
            term.se ^= 0x8000;
        if (small_vs(&term, &sum))
            break;
        sum = e_add(sum, term);
    }
    return sum;
}

/* ln(x), x finite and > 0 */
static ext e_ln(ext x)
{
    unp u;
    s16 k;
    ext m, s;
    unpack(&x, &u);
    k = (s16)u.exp;
    m = x;
    m.se = EXP_BIAS;                    /* m in [1, 2) */
    if (!(x.se & EXP_MAX)) {            /* denormal: take the normalised form */
        m.m[0] = u.w[4];
        m.m[1] = u.w[5];
        m.m[2] = u.w[6];
        m.m[3] = u.w[7];
    }
    if (m.m[3] > 0xB504) {              /* above sqrt(2): m / 2 */
        m.se = EXP_BIAS - 1;
        k++;
    }
    /* ln(m) = 2 atanh((m - 1) / (m + 1)) */
    s = e_div(e_sub(m, k_one), e_add(m, k_one));
    s = e_scale(odd_series(s, 0), 1);
    if (k)
        s = e_add(s, e_mul(e_int(k), k_ln2));
    return s;
}

/* 2^x - 1 */
static ext e_exp2m1(ext x)
{
    ext t = e_mul(x, k_ln2), term = t, sum = t;
    s16 n;
    for (n = 2; n < 400; n++) {
        term = e_div(e_mul(term, t), e_int(n));
        if (small_vs(&term, &sum))
            break;
        sum = e_add(sum, term);
    }
    return sum;
}

/* atan(z), z >= 0 */
static ext e_atan(ext z)
{
    ext r;
    int inv = 0, shift = 0;
    if (is_zero(&z))
        return z;
    r = e_sub(z, k_one);
    if (!is_neg(&r)) {
        z = e_div(k_one, z);            /* atan(z) = pi/2 - atan(1/z) */
        inv = 1;
    }
    /* above tan(pi/8) = 0.4142 (up to 1): atan(z) = pi/4 +
       atan((z - 1) / (z + 1)) */
    if ((z.se & EXP_MAX) == EXP_BIAS || (z.se & EXP_MAX) == EXP_BIAS - 1 ||
        ((z.se & EXP_MAX) == EXP_BIAS - 2 && z.m[3] >= 0xD413)) {
        z = e_div(e_sub(z, k_one), e_add(z, k_one));
        shift = 1;
    }
    r = odd_series(z, 1);
    if (shift)
        r = e_add(e_scale(k_pi, -2), r);
    if (inv)
        r = e_sub(e_scale(k_pi, -1), r);
    return r;
}

/* sin and cos of x, |x| <= pi/2 */
static void e_sincos(ext x, ext *sn, ext *cs)
{
    ext term = x, s = x, c = k_one, t;
    s16 n;
    int done_s = 0, done_c = 0;
    for (n = 2; n < 400 && !(done_s && done_c); n += 2) {
        term = e_div(e_mul(term, x), e_int(n));         /* x^n / n! */
        t = term;
        if (n & 2)
            t.se ^= 0x8000;
        if (small_vs(&t, &c))
            done_c = 1;
        else
            c = e_add(c, t);
        term = e_div(e_mul(term, x), e_int(n + 1));     /* x^(n+1)/(n+1)! */
        t = term;
        if (n & 2)
            t.se ^= 0x8000;
        if (small_vs(&t, &s))
            done_s = 1;
        else
            s = e_add(s, t);
    }
    *sn = s;
    *cs = c;
}

static int is_pow2(const ext *x)
{
    return x->m[3] == 0x8000 && !(x->m[2] | x->m[1] | x->m[0]);
}

/* a transcendental result: rounded as the control word says; PE unless
   exact */
static void trans_result(ext v, int exact, ext *r)
{
    unp u;
    if (is_zero(&v) || is_inf(&v) || is_nan(&v)) {
        *r = v;
        return;
    }
    unpack(&v, &u);
    pack(&u, r);
    if (!exact)
        raise(SW_PE, FPE_INEXACT);
}

/* |a| mod |b| with the sign of a; the whole quotient is worked out (its
   low three bits in *q); if nearest, the remainder closest to zero.
   a, b finite and nonzero. */
static void rem_parts(ext *a, const ext *b, u16 *q, int nearest)
{
    unp ua, ub;
    s32 d, i;
    u16 rem[5], dv[5], k, qq = 0;
    unpack(a, &ua);
    unpack(b, &ub);
    d = ua.exp - ub.exp;
    if (d < 0) {
        if (nearest && d == -1) {
            /* |a| in [|b|/2, |b|): a - b is closer when |a| > |b|/2 */
            ext t = *b;
            t.se = (t.se & 0x7FFF) | (a->se & 0x8000);
            t = e_sub(*a, t);
            t.se &= 0x7FFF;
            {
                ext aa = *a;
                aa.se &= 0x7FFF;
                t = e_sub(t, aa);
                if (is_neg(&t)) {
                    ext bb = *b;
                    bb.se = (bb.se & 0x7FFF) | (a->se & 0x8000);
                    *a = e_sub(*a, bb);
                    qq = 1;
                }
            }
        }
        *q = qq;
        return;
    }
    for (k = 0; k < 4; k++) {
        rem[k] = ua.w[4 + k];
        dv[k] = ub.w[4 + k];
    }
    rem[4] = dv[4] = 0;
    for (i = 0; i <= d; i++) {
        qq <<= 1;
        if (w_cmp(rem, dv, 5) >= 0) {
            w_sub(rem, dv, 5);
            qq |= 1;
        }
        if (i < d)
            w_shl(rem, 5, 1);
    }
    if (nearest) {
        u16 twice[5];
        for (k = 0; k < 5; k++)
            twice[k] = rem[k];
        w_shl(twice, 5, 1);
        if (w_cmp(twice, dv, 5) > 0 || (w_cmp(twice, dv, 5) == 0 && (qq & 1))) {
            u16 t[5];
            for (k = 0; k < 5; k++)
                t[k] = dv[k];
            w_sub(t, rem, 5);
            for (k = 0; k < 5; k++)
                rem[k] = t[k];
            ua.sign ^= 0x8000;
            qq++;
        }
    }
    *q = qq;
    if (w_is_zero(rem, 5)) {
        set_zero(a, ua.sign);
        return;
    }
    w_clear(ua.w, 8);
    for (k = 0; k < 4; k++)
        ua.w[4 + k] = rem[k];
    ua.exp = ub.exp;
    normalise(&ua);
    pack_range(&ua, a, 64, 1 - EXP_BIAS, EXP_BIAS);
}

/* FPREM. Observed with the 3.1 emulator: the remainder is complete in
   one go whatever the exponents (C2 never set; 1e30 rem 3 gave 1 with
   the quotient bits in C0 C3 C1), and a zero divisor gives +0 with no
   exception and the condition codes clear. */
static void fprem(void)
{
    ext a, b;
    u16 q;
    if (!need(0) || !need(1))
        return;
    a = *st(0);
    b = *st(1);
    sw &= ~(SW_C0 | SW_C1 | SW_C2 | SW_C3);
    if (is_nan(&a) || is_nan(&b)) {
        if (nan_result(&a, &b, &a))
            set_st(0, &a);
        return;
    }
    if (is_zero(&b)) {
        set_zero(&a, 0);
        set_st(0, &a);
        return;
    }
    if (is_inf(&a)) {                   /* UNVERIFIED */
        invalid_result(&a, FPE_INVALID);
        set_st(0, &a);
        return;
    }
    if (is_zero(&a) || is_inf(&b))
        return;
    {
        saved_st s;
        quiet_begin(&s);
        rem_parts(&a, &b, &q, 0);
        quiet_end(&s);
    }
    if (q & 1) sw |= SW_C1;
    if (q & 2) sw |= SW_C3;
    if (q & 4) sw |= SW_C0;
    set_st(0, &a);
}

/* FSCALE. Observed with the 3.1 emulator: no exception flags at all, and
   the scale taken from ST(1) as the top 16 bits of its significand
   shifted right by (15 - unbiased exponent) & 15, negated if negative
   (0.9 scaled by E666h = -6554; -3.7 by -3; 2.5 by 2). Overflow and
   underflow give infinity and zero. */
static void fscale(void)
{
    ext a, b;
    unp u;
    s16 e, n;
    saved_st s;
    if (!need(0) || !need(1))
        return;
    a = *st(0);
    b = *st(1);
    if (is_nan(&a) || is_nan(&b) || is_inf(&a) || is_zero(&a)) {
        if (nan_result(&a, &b, &a))
            set_st(0, &a);
        return;
    }
    e = (s16)((b.se & EXP_MAX) - EXP_BIAS);
    n = (s16)(b.m[3] >> ((15 - e) & 15));
    if (is_neg(&b))
        n = -n;
    quiet_begin(&s);
    unpack(&a, &u);
    u.exp += n;
    pack(&u, &a);
    quiet_end(&s);
    set_st(0, &a);
}

/* D9 F0-FF (n = rm for F0-F7, 8 + rm for F8-FF) */
static void d9_misc(u16 n)
{
    ext x, y, r;
    saved_st q;
    switch (n) {
    case 6:                                     /* FDECSTP */
        set_top(top() - 1);
        return;
    case 7:                                     /* FINCSTP */
        set_top(top() + 1);
        return;
    case 8 + 2:                                 /* FSQRT */
        if (!need(0))
            return;
        square_root(st(0), &r);
        set_st(0, &r);
        return;
    case 8 + 4:                                 /* FRNDINT */
        if (!need(0))
            return;
        round_int(st(0), &r);
        set_st(0, &r);
        return;
    case 8 + 0:
        fprem();
        return;
    case 8 + 5:
        fscale();
        return;
    case 0:                                     /* F2XM1 */
        if (!need(0))
            return;
        x = *st(0);
        if (is_nan(&x) || is_inf(&x) || is_zero(&x))
            return;                             /* UNVERIFIED: NaN, inf */
        quiet_begin(&q);
        y = e_exp2m1(x);
        quiet_end(&q);
        trans_result(y, 0, &r);
        set_st(0, &r);
        return;
    case 1:                                     /* FYL2X: ST1 * log2(ST0) */
    case 8 + 1: {                               /* FYL2XP1: ST1 * log2(ST0 + 1) */
        int exact = 0;
        if (!need(0) || !need(1))
            return;
        x = *st(0);
        y = *st(1);
        if (is_nan(&x) || is_nan(&y)) {
            nan_result(&x, &y, &r);
        } else if (n == 1 && is_zero(&x)) {
            raise(SW_ZE, FPE_ZERODIVIDE);       /* UNVERIFIED */
            set_inf(&r, (y.se ^ 0x8000) & 0x8000);
        } else if ((n == 1 && is_neg(&x)) || is_inf(&x) || is_inf(&y)) {
            invalid_result(&r, FPE_INVALID);    /* UNVERIFIED */
        } else if (n != 1 && is_zero(&x)) {
            set_zero(&r, (x.se ^ y.se) & 0x8000);
        } else {
            quiet_begin(&q);
            if (n == 1 && is_pow2(&x)) {        /* exact: y * exponent */
                unp u;
                unpack(&x, &u);
                r = e_mul(y, e_int((s16)u.exp));
                exact = 1;
            } else if (n == 1) {
                r = e_mul(y, e_mul(e_ln(x), k_l2e));
            } else {
                /* ln(1 + x) = 2 atanh(x / (2 + x)) */
                ext t = e_div(x, e_add(e_scale(k_one, 1), x));
                r = e_mul(y, e_mul(e_scale(odd_series(t, 0), 1), k_l2e));
            }
            quiet_end(&q);
            trans_result(r, exact, &r);
        }
        pop();
        set_st(0, &r);
        return;
    }
    case 2: {                                   /* FPTAN */
        ext sn, cs;
        if (!need(0))
            return;
        x = *st(0);
        if (is_nan(&x) || is_inf(&x)) {
            invalid_result(&r, FPE_INVALID);    /* UNVERIFIED */
            set_st(0, &r);
            return;
        }
        if (is_zero(&x)) {
            r = x;
        } else {
            u16 neg = x.se & 0x8000, qq;
            x.se &= 0x7FFF;
            quiet_begin(&q);
            /* beyond pi/4 (the 8087's range) reduced modulo pi */
            y = e_sub(x, e_scale(k_pi, -2));
            if (!is_neg(&y))
                rem_parts(&x, &k_pi, &qq, 1);
            e_sincos(x, &sn, &cs);
            r = e_div(sn, cs);
            r.se ^= neg;
            quiet_end(&q);
            trans_result(r, 0, &r);
        }
        /* ST0 = tan(x), then 1.0 pushed: the 387's form. The 8087 and the
           3.1 emulator leave some pair Y (ST1), X (ST0) with Y / X =
           tan(x) (observed), which programs divide. */
        set_st(0, &r);
        x = k_one;
        load(&x);
        return;
    }
    case 3: {                                   /* FPATAN: atan(ST1 / ST0) */
        ext z;
        if (!need(0) || !need(1))
            return;
        x = *st(0);
        y = *st(1);
        if (is_nan(&x) || is_nan(&y)) {
            nan_result(&x, &y, &r);
        } else if (is_zero(&y) && !is_neg(&x)) {
            r = y;
        } else {
            u16 ys = y.se & 0x8000, xs = x.se & 0x8000;
            x.se &= 0x7FFF;
            y.se &= 0x7FFF;
            quiet_begin(&q);
            if (is_zero(&x) || is_inf(&y))
                z = e_scale(k_pi, -1);
            else if (is_inf(&x))
                z = k_zero;
            else
                z = e_atan(e_div(y, x));
            /* the quadrants as the 387 does them; the 3.1 emulator, like
               the 8087, needs 0 <= Y < X and gave other values otherwise
               (observed) */
            if (xs)
                z = e_sub(k_pi, z);
            z.se |= ys;
            quiet_end(&q);
            trans_result(z, 0, &r);
        }
        pop();
        set_st(0, &r);
        return;
    }
    }
    /* FXTRACT (observed: not emulated by the 3.1 emulator), FPREM1,
       FSINCOS, FSIN, FCOS (387 instructions) */
    unemulated();
}

static void exec(insn *in)
{
    ext x, r;
    u16 w[4];
    u16 FAR *pw;

    if (in->mod != 3) {
        pw = (u16 FAR *)mem(in);
        switch (in->esc) {
        case 0:                         /* D8: m32real arithmetic */
        case 4:                         /* DC: m64real */
            from_real(pw, in->esc == 0 ? 32 : 64, &x);
            arith(in->reg, 0, &x);
            if (in->reg == OP_COMP)
                pop();
            return;
        case 2:                         /* DA: m32int */
        case 6:                         /* DE: m16int */
            if (in->esc == 2)
                from_int((s32)((u32)pw[1] << 16 | pw[0]), 0, 0, &x);
            else
                from_int((s32)(s16)pw[0], 0, 0, &x);
            arith(in->reg, 0, &x);
            if (in->reg == OP_COMP)
                pop();
            return;
        case 1:                         /* D9 */
            switch (in->reg) {
            case 0: from_real(pw, 32, &x); load(&x); return;
            case 2:
            case 3:
                if (!need_store())
                    return;
                to_real(st(0), 32, pw);
                if (in->reg == 3)
                    pop();
                return;
            case 4: load_env(pw); return;
            case 5: cw = pw[0]; return;
            case 6: store_env(pw); cw |= 0x3F; return;
            case 7: pw[0] = cw; return;
            }
            break;
        case 3:                         /* DB */
            switch (in->reg) {
            case 0:
                from_int((s32)((u32)pw[1] << 16 | pw[0]), 0, 0, &x);
                load(&x);
                return;
            case 2:
            case 3:
                if (!need_store())
                    return;
                to_int(st(0), 32, w);
                if (!(sw & SW_IE) || masked(SW_IE)) {
                    pw[0] = w[0];
                    pw[1] = w[1];
                }
                if (in->reg == 3)
                    pop();
                return;
            case 5:
                x = *(ext FAR *)pw;
                load(&x);
                return;
            case 7:
                if (!need_store())
                    return;
                *(ext FAR *)pw = *st(0);
                pop();
                return;
            }
            break;
        case 5:                         /* DD */
            switch (in->reg) {
            case 0: from_real(pw, 64, &x); load(&x); return;
            case 2:
            case 3:
                if (!need_store())
                    return;
                to_real(st(0), 64, pw);
                if (in->reg == 3)
                    pop();
                return;
            case 4: {                   /* FRSTOR */
                u16 i;
                load_env(pw);
                for (i = 0; i < 8; i++)
                    reg[(top() + i) & 7] = ((ext FAR *)(pw + 7))[i];
                return;
            }
            case 6: {                   /* FSAVE */
                u16 i;
                store_env(pw);
                for (i = 0; i < 8; i++)
                    ((ext FAR *)(pw + 7))[i] = reg[(top() + i) & 7];
                finit();
                return;
            }
            case 7: pw[0] = sw_out(); return;
            }
            break;
        case 7:                         /* DF */
            switch (in->reg) {
            case 0: from_int((s32)(s16)pw[0], 0, 0, &x); load(&x); return;
            case 2:
            case 3:
                if (!need_store())
                    return;
                to_int(st(0), 16, w);
                if (!(sw & SW_IE) || masked(SW_IE))
                    pw[0] = w[0];
                if (in->reg == 3)
                    pop();
                return;
            case 5:
                from_int((s32)((u32)pw[1] << 16 | pw[0]),
                         (s32)((u32)pw[3] << 16 | pw[2]), 1, &x);
                load(&x);
                return;
            case 7:
                if (!need_store())
                    return;
                to_int(st(0), 64, w);
                if (!(sw & SW_IE) || masked(SW_IE)) {
                    pw[0] = w[0];
                    pw[1] = w[1];
                    pw[2] = w[2];
                    pw[3] = w[3];
                }
                pop();
                return;
            case 4:                     /* FBLD */
            case 6:                     /* FBSTP */
                fpu_unemulated();
                return;
            }
            break;
        }
        fpu_unemulated();
        return;
    }

    /* register forms */
    switch (in->esc) {
    case 0:
        arith_reg(0, in->reg, in->rm);
        return;
    case 4:
        arith_reg(4, in->reg, in->rm);
        return;
    case 6:
        if (in->reg == OP_COMP && in->rm != 1)
            break;                      /* only DE D9 (FCOMPP) */
        if (in->reg == OP_COMP) {       /* FCOMPP */
            if (!need(0) || !need(1))
                return;
            compare(st(0), st(1), 0);
            pop();
            pop();
            return;
        }
        arith_reg(6, in->reg, in->rm);
        return;
    case 1:                             /* D9 */
        switch (in->reg) {
        case 0:                         /* FLD ST(i) */
            if (!need(in->rm))
                return;
            x = *st(in->rm);
            load(&x);
            return;
        case 1: {                       /* FXCH */
            if (st_empty(0) || st_empty(in->rm)) {
                underflow();
                if (!masked(SW_IE))
                    return;
                if (st_empty(0)) {
                    set_indefinite(&x);
                    set_st(0, &x);
                }
                if (st_empty(in->rm)) {
                    set_indefinite(&x);
                    set_st(in->rm, &x);
                }
            }
            x = *st(0);
            r = *st(in->rm);
            set_st(0, &r);
            set_st(in->rm, &x);
            return;
        }
        case 2:                         /* FNOP */
            if (in->rm == 0)
                return;
            break;
        case 4:
            switch (in->rm) {
            case 0:                     /* FCHS */
                if (need(0))
                    st(0)->se ^= 0x8000;
                return;
            case 1:                     /* FABS */
                if (need(0))
                    st(0)->se &= 0x7FFF;
                return;
            case 4:                     /* FTST */
                if (need(0))
                    compare(st(0), &k_zero, 0);
                return;
            case 5:                     /* FXAM */
                examine();
                return;
            }
            break;
        case 5: {                       /* constants */
            static const ext *const k[] = { &k_one, &k_l2t, &k_l2e, &k_pi,
                                            &k_lg2, &k_ln2, &k_zero, NULL };
            if (k[in->rm]) {
                x = *k[in->rm];
                load(&x);
                return;
            }
            break;
        }
        case 6:
        case 7:
            d9_misc((u16)(in->reg == 6 ? in->rm : 8 + in->rm));
            return;
        }
        break;
    case 2:                             /* DA */
        if (in->reg == 5 && in->rm == 1) {      /* FUCOMPP */
            if (!need(0) || !need(1))
                return;
            compare(st(0), st(1), 1);
            pop();
            pop();
            return;
        }
        break;
    case 3:                             /* DB */
        if (in->reg == 4) {
            switch (in->rm) {
            case 0: case 1: case 4:     /* FENI FDISI FSETPM: nothing */
                return;
            case 2:                     /* FCLEX */
                /* observed (3.1 emulator): the condition codes read 0
                   after FLDCW + FCLEX; UNVERIFIED which of the two */
                sw &= ~(0x00FF | SW_B | SW_C0 | SW_C1 | SW_C2 | SW_C3);
                return;
            case 3:                     /* FINIT: not emulated (observed) */
                unemulated();
                return;
            }
        }
        break;
    case 5:                             /* DD */
        switch (in->reg) {
        case 0:                         /* FFREE */
            set_tag(phys(in->rm), TAG_EMPTY);
            return;
        case 2:                         /* FST ST(i) */
        case 3:                         /* FSTP ST(i) */
            if (!need(0))
                return;
            x = *st(0);
            set_st(in->rm, &x);
            if (in->reg == 3)
                pop();
            return;
        case 4:                         /* FUCOM */
        case 5:                         /* FUCOMP */
            if (!need(0) || !need(in->rm))
                return;
            compare(st(0), st(in->rm), 1);
            if (in->reg == 5)
                pop();
            return;
        }
        break;
    case 7:                             /* DF */
        if (in->reg == 4 && in->rm == 0)        /* FSTSW AX: in exec_ax */
            return;
        if (in->reg == 3) {             /* FSTP ST(i), undocumented alias */
            if (!need(0))
                return;
            x = *st(0);
            set_st(in->rm, &x);
            pop();
            return;
        }
        break;
    }
    fpu_unemulated();
}

/* ---- the interrupt entry ---- */

void __cdecl __far emu_entry(emu_frame FAR *f)
{
    insn in;
    u8 b, m;
    int ovr = 0;
    u16 seg = 0;

    if (f->intno == 0x3D || f->intno == 0x3E)
        return;                         /* FWAIT; 3Eh: UNVERIFIED */
    if (f->intno == 0x3C) {
        static const u8 segs[4] = { 0, 1, 2, 3 };   /* DS SS CS ES */
        b = code_byte(f);
        in.esc = b & 7;
        ovr = 1;
        switch (segs[b >> 6]) {
        case 0: seg = f->ds; break;
        case 1: seg = get_ss(); break;
        case 2: seg = f->cs; break;
        case 3: seg = f->es; break;
        }
    } else {
        in.esc = f->intno - 0x34;
    }
    m = code_byte(f);
    in.mod = m >> 6;
    in.reg = (m >> 3) & 7;
    in.rm = m & 7;
    decode_ea(f, &in, ovr, seg);
    cur_ds = f->ds;
    fault_code = 0;
    if (in.esc == 7 && in.mod == 3 && in.reg == 4 && in.rm == 0)
        f->ax = sw_out();               /* FSTSW AX */
    else
        exec(&in);
    if (fault_code) {
        u16 code = fault_code;
        fault_code = 0;
        call_handler(code, cur_ds);
    }
}

/* ---- __fpMath without a coprocessor (observed on Windows 3.1) ---- */

void __cdecl __far emu_fpmath(u16 fn, fpm_regs FAR *rg)
{
    u16 w[4];
    ext r;
    rg->flags &= ~1u;                   /* CF = 0 for every function */
    switch (fn) {
    case 0: case 1: case 2:             /* initialise */
        emu_reset();
        em_acc = 0;
        user_cw = CW_INIT;
        rg->ax = 0;
        break;
    case 3:
        handler = ((u32)rg->dx << 16) | rg->ax;
        rg->ax = 0x253E;
        break;
    case 4:                             /* set CW: all of AX */
        cw = user_cw = rg->ax;
        rg->ax &= 0xFF3C;
        break;
    case 5:
        rg->ax = cw;
        break;
    case 6:                             /* round ST with RC from AX */
        if (!st_empty(0)) {
            u16 save = cw;
            cw = (cw & ~0x0C00) | (rg->ax & 0x0C00);
            round_int(st(0), &r);
            set_st(0, &r);
            cw = save;
        }
        rg->ax = 1;
        break;
    case 7: {                           /* pop ST into DX:AX */
        u16 save = cw;
        cw = (cw & ~0x0C00) | (rg->ax & 0x0C00);
        w[0] = 0;
        w[1] = 0x8000;
        if (!st_empty(0))
            to_int(st(0), 32, w);
        else
            underflow();
        cw = save;
        pop();
        rg->ax = w[0];
        rg->dx = w[1];
        if (fault_code) {
            u16 code = fault_code;
            fault_code = 0;
            call_handler(code, 0);
        }
        break;
    }
    case 8:
        rg->ax = em_acc;
        break;
    case 9:
        sw &= ~(0x00FF | SW_B);
        em_acc = 0;
        rg->ax = 0;
        break;
    case 10:
        rg->ax = depth_now();
        break;
    case 11:
        rg->ax = 0;
        break;
    case 12:
        fn12 = rg->ax;
        break;
    }
}

/* ---- __Win87EmSave / __Win87EmRestore ----
   The area (01CDh bytes) after the 94-byte FSAVE image holds WIN87EM's
   own state. Observed on Windows 3.1 (tests/apps/tk8.c), the words that
   carry a known value: 94/96 the handler of function 3 (offset,
   selector; without a coprocessor only), 98 1 with a coprocessor,
   100 the control word of function 4, 104 the same & FF3Ch (what the
   coprocessor gets), 256 function 12's word. The rest held selectors,
   addresses and values of unknown meaning; w16 keeps its own state from
   offset 304 (the reference's area was zero from 304 to the end) and
   zeroes everything else. */

typedef struct saved {
    u16 cw, sw, tw;
    ext reg[8];
    u16 user_cw, fn12, em_acc;
    u32 handler;
} saved;

#define SAVE_SIZE   0x01CD      /* observed (__WIN87EMINFO) */
#define SAVE_OWN    304

extern u16 __cdecl have87;

void __cdecl __far emu_save(void FAR *buf)
{
    u8 FAR *b = (u8 FAR *)buf;
    saved FAR *s = (saved FAR *)(b + SAVE_OWN);
    u16 i;
    for (i = 94; i < SAVE_SIZE; i++)    /* the whole area is written */
        b[i] = 0;
    if (!have87)
        *(u32 FAR *)(b + 94) = handler;
    *(u16 FAR *)(b + 98) = have87;
    *(u16 FAR *)(b + 100) = user_cw;
    *(u16 FAR *)(b + 104) = user_cw & 0xFF3C;
    *(u16 FAR *)(b + 256) = fn12;
    s->cw = cw;
    s->sw = sw;
    s->tw = tw;
    for (i = 0; i < 8; i++)
        s->reg[i] = reg[i];
    s->user_cw = user_cw;
    s->fn12 = fn12;
    s->em_acc = em_acc;
    s->handler = handler;
}

void __cdecl __far emu_restore(void FAR *buf)
{
    const saved FAR *s = (const saved FAR *)((const u8 FAR *)buf + SAVE_OWN);
    u16 i;
    cw = s->cw;
    sw = s->sw;
    tw = s->tw;
    for (i = 0; i < 8; i++)
        reg[i] = s->reg[i];
    user_cw = s->user_cw;
    fn12 = s->fn12;
    em_acc = s->em_acc;
    handler = s->handler;
}
