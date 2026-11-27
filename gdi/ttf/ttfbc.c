/*
 * ttfbc.c - the TrueType bytecode interpreter: a font's own instructions
 * (fpgm, prep, each glyph's program) grid-fit its outlines, as Windows
 * 3.1's rasteriser runs them. Fonts without instructions, and glyphs
 * whose programs fail, are left to the autohinter (ttfhint.c).
 *
 * Written for w16 from the published specifications only: Apple's
 * TrueType Reference Manual (the instruction set, the graphics state,
 * "Instructing Fonts", the cvt, fpgm, prep, maxp and glyf tables) and
 * Microsoft's OpenType specification (the TrueType instructions,
 * "Instructing TrueType Glyphs", the scan converter's rules). Where they
 * are ambiguous, two permissively licensed interpreters were read as a
 * second reading of the specification, no code taken: opentype.js's
 * hintingtt (MIT) and Google's skrifa (MIT/Apache-2.0); ARCHITECTURE.md
 * 11.11.2 lists what they settled. Public domain like the rest of w16
 * (LICENSE).
 *
 * The rasteriser it imitates is the one GETINFO calls version 3, "16-bit
 * Windows 3.1": two phantom points (the origin and the advance), the
 * simple dropout-control rules (SCANTYPE 0, 1 and 2; 4 and 5, which came
 * with version 34, leave the mode as it was, as the specification says
 * an older rasteriser does), no grayscale or ClearType. Values are
 * 32-bit, distances 26.6, vectors 2.14, all integer arithmetic for an
 * 80286 without a coprocessor; the moves along an axis, nearly all of a
 * font's, take no multiply.
 */
#include "ttfbc.h"
#ifdef BC_TRACE
#include <stdio.h>
#endif

#define ttUSHORT(o) ttfx_U16(f, (o))
#define ttSHORT(o) ttfx_I16(f, (o))

/* ---- arithmetic ---- */

static ttfx_i32 bc_abs(ttfx_i32 v)
{
    return v < 0 ? -v : v;
}

/* a * v / 2^14, rounded (v a 2.14 vector component) */
static ttfx_i32 mul14(ttfx_i32 a, ttfx_i32 v)
{
    ttfx_u32 ua, uv, r;
    int neg = 0;
    if (!a || !v)
        return 0;
    if (v == 0x4000)
        return a;
    if (v == -0x4000)
        return -a;
    if (a < 0) {
        ua = (ttfx_u32)-a;
        neg = 1;
    } else
        ua = (ttfx_u32)a;
    if (v < 0) {
        uv = (ttfx_u32)-v;
        neg ^= 1;
    } else
        uv = (ttfx_u32)v;
    r = (ua >> 14) * uv + (((ua & 0x3FFF) * uv + 0x2000) >> 14);
    return neg ? -(ttfx_i32)r : (ttfx_i32)r;
}

/* a * b / c rounded to nearest, any signs (c != 0) */
static ttfx_i32 muldiv(ttfx_i32 a, ttfx_i32 b, ttfx_i32 c)
{
    if (c < 0) {
        c = -c;
        a = -a;
    }
    return ttfx_MulDivRound(a, b, c);
}

static ttfx_u32 isqrt(ttfx_u32 v)
{
    ttfx_u32 r = 0, b = 0x40000000UL;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

/* the unit vector (2.14) along (dx, dy); 0 if both are 0 */
static int unit_vector(ttfx_i32 dx, ttfx_i32 dy, ttfx_i16 *v)
{
    ttfx_u32 len;
    if (!dx && !dy)
        return 0;
    if (!dy) {
        v[0] = dx > 0 ? 0x4000 : -0x4000;
        v[1] = 0;
        return 1;
    }
    if (!dx) {
        v[0] = 0;
        v[1] = dy > 0 ? 0x4000 : -0x4000;
        return 1;
    }
    /* keep the squares in 32 bits, but no smaller than they need be
       (the bounds tested signed: bc_abs of -2^31 stays negative, and
       that pair doubled to 0 would never leave the loop below) */
    while (dx >= 0x8000L || dx <= -0x8000L || dy >= 0x8000L || dy <= -0x8000L) {
        dx /= 2;
        dy /= 2;
    }
    while (bc_abs(dx) < 0x2000 && bc_abs(dy) < 0x2000) {
        dx *= 2;
        dy *= 2;
    }
    len = isqrt((ttfx_u32)(dx * dx) + (ttfx_u32)(dy * dy));
    v[0] = (ttfx_i16)muldiv(dx, 0x4000, (ttfx_i32)len);
    v[1] = (ttfx_i16)muldiv(dy, 0x4000, (ttfx_i32)len);
    return 1;
}

/* ---- the graphics state ---- */

void ttfx__bcDefaults(ttfx__gs *gs)
{
    gs->pv[0] = gs->fv[0] = gs->dv[0] = 0x4000;
    gs->pv[1] = gs->fv[1] = gs->dv[1] = 0;
    gs->min_dist = 64;
    gs->cvt_cutin = 68;                 /* 17/16 pixel */
    gs->sw_cutin = 0;
    gs->sw_value = 0;
    gs->period = 64;
    gs->phase = 0;
    gs->threshold = 32;
    gs->round_state = BC_RTG;
    gs->loop = 1;
    gs->auto_flip = 1;
    gs->delta_base = 9;
    gs->delta_shift = 3;
    gs->rp[0] = gs->rp[1] = gs->rp[2] = 0;
    gs->zp[0] = gs->zp[1] = gs->zp[2] = 1;
    gs->instctrl = 0;
    gs->scan_control = 0;
    gs->scan_type = 0;
}

/* freedom . projection, kept away from 0 (the specification calls the
   vectors at right angles an error) */
static void bc_vectors(ttfx__bc *bc)
{
    ttfx_i32 d = mul14(bc->gs.fv[0], bc->gs.pv[0]) + mul14(bc->gs.fv[1], bc->gs.pv[1]);
    if (d > -0x400 && d < 0x400)
        d = 0x4000;
    bc->fdotp = d;
}

/* engine compensation for a distance of type t (0 grey, 1 black, 2
   white): none on a screen (black-box fit: any other made Windows 3.1's
   pixels less alike) */
#define bc_comp(d, t) (d)

ttfx_i32 ttfx__bcRound(const ttfx__bc *bc, ttfx_i32 d)
{
    ttfx_i32 r, p;
    switch (bc->gs.round_state) {
    case BC_RTG:
        return d >= 0 ? (d + 32) & ~63L : -((-d + 32) & ~63L);
    case BC_RTHG:
        return d >= 0 ? (d & ~63L) + 32 : -((-d & ~63L) + 32);
    case BC_RTDG:
        return d >= 0 ? (d + 16) & ~31L : -((-d + 16) & ~31L);
    case BC_RDTG:
        return d >= 0 ? d & ~63L : -(-d & ~63L);
    case BC_RUTG:
        return d >= 0 ? (d + 63) & ~63L : -((-d + 63) & ~63L);
    case BC_SROUND:
        /* less the phase, plus the threshold, down to a multiple of the
           period, plus the phase; the sign kept */
        p = bc->gs.period;
        if (d >= 0) {
            r = d - bc->gs.phase + bc->gs.threshold;
            r = (r >= 0 ? r / p : -((-r + p - 1) / p)) * p + bc->gs.phase;
            if (r < 0)
                r = bc->gs.phase;
        } else {
            r = -d - bc->gs.phase + bc->gs.threshold;
            r = -((r >= 0 ? r / p : -((-r + p - 1) / p)) * p + bc->gs.phase);
            if (r > 0)
                r = -bc->gs.phase;
        }
        return r;
    default:
        return d;
    }
}

/* SROUND (grid 64) and S45ROUND (grid 45: sqrt(2)/2 pixel) */
static void bc_sround(ttfx__bc *bc, ttfx_i32 v, ttfx_i32 grid)
{
    ttfx_i32 period, t;
    switch ((int)(v >> 6) & 3) {
    case 0: period = grid / 2; break;
    case 2: period = grid * 2; break;
    default: period = grid; break;
    }
    switch ((int)(v >> 4) & 3) {
    case 0: bc->gs.phase = 0; break;
    case 1: bc->gs.phase = period / 4; break;
    case 2: bc->gs.phase = period / 2; break;
    default: bc->gs.phase = grid * 3 / 4; break;
    }
    t = v & 15;
    bc->gs.threshold = t ? (t - 4) * period / 8 : period - 1;
    bc->gs.period = period ? period : 64;
    bc->gs.round_state = BC_SROUND;
}

/* ---- points ---- */

static ttfx_point bc_nowhere[3];
static ttfx_u8 bc_noflags;

static ttfx__zone *bc_zone(ttfx__bc *bc, int zp)
{
    return &bc->z[bc->gs.zp[zp]];
}

/* point p of zone z: its grid-fitted position (which 0: cur, 1: org,
   2: ous); an error for one out of range */
static ttfx_point TTFX_FAR *bc_pt(ttfx__bc *bc, ttfx__zone *z, ttfx_u32 p, int which)
{
    if (p >= z->n) {
        if (!bc->error)
            bc->error = BC_EPOINT;
        return &bc_nowhere[which];
    }
    return which == 0 ? &z->cur[(unsigned)p] : which == 1 ? &z->org[(unsigned)p] : &z->ous[(unsigned)p];
}

static ttfx_i32 proj(const ttfx__bc *bc, ttfx_i32 dx, ttfx_i32 dy)
{
    if (!bc->gs.pv[1])
        return bc->gs.pv[0] > 0 ? dx : -dx;
    if (!bc->gs.pv[0])
        return bc->gs.pv[1] > 0 ? dy : -dy;
    return mul14(dx, bc->gs.pv[0]) + mul14(dy, bc->gs.pv[1]);
}

static ttfx_i32 dproj(const ttfx__bc *bc, ttfx_i32 dx, ttfx_i32 dy)
{
    if (!bc->gs.dv[1])
        return bc->gs.dv[0] > 0 ? dx : -dx;
    if (!bc->gs.dv[0])
        return bc->gs.dv[1] > 0 ? dy : -dy;
    return mul14(dx, bc->gs.dv[0]) + mul14(dy, bc->gs.dv[1]);
}

/* the original distance from point b (zone zb) to point a (zone za),
   measured along the dual projection vector: in the glyph zone from the
   positions in font units, scaled once (exact), else from the original
   26.6 positions */
static ttfx_i32 bc_odist(ttfx__bc *bc, ttfx__zone *za, ttfx_u32 a, ttfx__zone *zb, ttfx_u32 b)
{
    ttfx_point TTFX_FAR *pa, TTFX_FAR *pb;
    if (za == &bc->z[1] && zb == za) {
        ttfx_i32 dx, dy;
        pa = bc_pt(bc, za, a, 2);
        pb = bc_pt(bc, zb, b, 2);
        dx = pa->x - pb->x;
        dy = pa->y - pb->y;
        if (!za->ous_px) {
            if (!bc->gs.dv[1])
                return ttfx_Scale26(bc->gs.dv[0] > 0 ? dx : -dx, bc->sx);
            if (!bc->gs.dv[0])
                return ttfx_Scale26(bc->gs.dv[1] > 0 ? dy : -dy, bc->sy);
            dx = ttfx_Scale26(dx, bc->sx);
            dy = ttfx_Scale26(dy, bc->sy);
        }
        return dproj(bc, dx, dy);
    }
    pa = bc_pt(bc, za, a, 1);
    pb = bc_pt(bc, zb, b, 1);
    return dproj(bc, pa->x - pb->x, pa->y - pb->y);
}

/* the current distance from b to a along the projection vector */
static ttfx_i32 bc_cdist(ttfx__bc *bc, ttfx__zone *za, ttfx_u32 a, ttfx__zone *zb, ttfx_u32 b)
{
    ttfx_point TTFX_FAR *pa = bc_pt(bc, za, a, 0), TTFX_FAR *pb = bc_pt(bc, zb, b, 0);
    return proj(bc, pa->x - pb->x, pa->y - pb->y);
}

/* move point p of zone z along the freedom vector by d as measured along
   the projection vector; touch it */
static void bc_move(ttfx__bc *bc, ttfx__zone *z, ttfx_u32 p, ttfx_i32 d)
{
    ttfx_point TTFX_FAR *c = bc_pt(bc, z, p, 0);
    ttfx_u8 TTFX_FAR *fl = p < z->n ? &z->flags[(unsigned)p] : &bc_noflags;
    ttfx_i16 fx = bc->gs.fv[0], fy = bc->gs.fv[1];
    if (!fy && fx == bc->fdotp)         /* along x, measured along x */
        c->x += d;
    else if (!fx && fy == bc->fdotp)
        c->y += d;
    else {
        if (fx)
            c->x += muldiv(d, fx, bc->fdotp);
        if (fy)
            c->y += muldiv(d, fy, bc->fdotp);
    }
    if (fx)
        *fl |= BC_TX;
    if (fy)
        *fl |= BC_TY;
}

/* the same for an original position (twilight points made by MIAP, MIRP
   and MSIRP) */
static void bc_move_org(ttfx__bc *bc, ttfx__zone *z, ttfx_u32 p, ttfx_i32 d)
{
    ttfx_point TTFX_FAR *o = bc_pt(bc, z, p, 1);
    if (bc->gs.fv[0])
        o->x += muldiv(d, bc->gs.fv[0], bc->fdotp);
    if (bc->gs.fv[1])
        o->y += muldiv(d, bc->gs.fv[1], bc->fdotp);
}

/* a point moved by (dx, dy), as SHP, SHC, SHZ and SHPIX move them */
static void bc_shift(ttfx__bc *bc, ttfx__zone *z, ttfx_u32 p, ttfx_i32 dx, ttfx_i32 dy, int touch)
{
    ttfx_point TTFX_FAR *c = bc_pt(bc, z, p, 0);
    if (bc->gs.fv[0]) {
        c->x += dx;
        if (touch && p < z->n)
            z->flags[(unsigned)p] |= BC_TX;
    }
    if (bc->gs.fv[1]) {
        c->y += dy;
        if (touch && p < z->n)
            z->flags[(unsigned)p] |= BC_TY;
    }
}

/* SHP, SHC, SHZ: how far the reference point (rp2 in zp1, or with a set
   rp1 in zp0) has moved, along the freedom vector */
static ttfx__zone *bc_refshift(ttfx__bc *bc, int a, ttfx_u16 *rp, ttfx_i32 *dx, ttfx_i32 *dy)
{
    ttfx__zone *z = a ? bc_zone(bc, 0) : bc_zone(bc, 1);
    ttfx_point TTFX_FAR *c, TTFX_FAR *o;
    ttfx_i32 d;
    *rp = a ? bc->gs.rp[1] : bc->gs.rp[2];
    c = bc_pt(bc, z, *rp, 0);
    o = bc_pt(bc, z, *rp, 1);
    d = proj(bc, c->x - o->x, c->y - o->y);
    *dx = bc->gs.fv[0] ? muldiv(d, bc->gs.fv[0], bc->fdotp) : 0;
    *dy = bc->gs.fv[1] ? muldiv(d, bc->gs.fv[1], bc->fdotp) : 0;
    return z;
}

/* ---- IUP ---- */

/* the untouched points from a + 1 to b - 1 (cyclically in the contour
   from s to e) between touched points a and b, along one axis (o: 0 x,
   1 y; the points' coordinates as ttfx_i32 pairs) */
static void iup_range(ttfx__zone *z, unsigned s, unsigned e, unsigned a, unsigned b, int o)
{
    ttfx_i32 TTFX_FAR *ca = &z->cur[a].x + o, TTFX_FAR *cb = &z->cur[b].x + o;
    ttfx_i32 oa = (&z->org[a].x)[o], ob = (&z->org[b].x)[o];
    ttfx_i32 ua = (&z->ous[a].x)[o], ub = (&z->ous[b].x)[o];
    ttfx_i32 lo_u, hi_u, lo_c, hi_c, lo_d, hi_d, du;
    unsigned p;
    if (ua > ub) {
        lo_u = ub; hi_u = ua; lo_c = *cb; hi_c = *ca; lo_d = *cb - ob; hi_d = *ca - oa;
    } else {
        lo_u = ua; hi_u = ub; lo_c = *ca; hi_c = *cb; lo_d = *ca - oa; hi_d = *cb - ob;
    }
    du = hi_u - lo_u;
    for (p = a == e ? s : a + 1; p != b; p = p == e ? s : p + 1) {
        ttfx_i32 u = (&z->ous[p].x)[o];
        ttfx_i32 TTFX_FAR *c = &z->cur[p].x + o;
        if (u <= lo_u)
            *c = (&z->org[p].x)[o] + lo_d;
        else if (u >= hi_u)
            *c = (&z->org[p].x)[o] + hi_d;
        else
            *c = lo_c + muldiv(u - lo_u, hi_c - lo_c, du);
    }
}

static void bc_iup(ttfx__bc *bc, int o)
{
    ttfx__zone *z = bc_zone(bc, 2);
    ttfx_u8 mask = o ? BC_TY : BC_TX;
    unsigned c, s = 0;
    if (z == &bc->z[0])
        return;                         /* (no contours in the twilight zone) */
    for (c = 0; c < z->nc; c++) {
        unsigned e = z->ends[c], p, first, last;
        if (e >= z->n || e < s) {
            bc->error = BC_EPOINT;
            return;
        }
        for (first = s; first <= e && !(z->flags[first] & mask); first++)
            ;
        if (first <= e) {
            last = first;
            for (p = first + 1; p <= e; p++)
                if (z->flags[p] & mask) {
                    if (p > last + 1)
                        iup_range(z, s, e, last, p, o);
                    last = p;
                }
            if (last == first) {
                /* one touched point: the rest shift with it */
                ttfx_i32 d = (&z->cur[first].x)[o] - (&z->org[first].x)[o];
                for (p = s; p <= e; p++)
                    if (p != first)
                        (&z->cur[p].x)[o] = (&z->org[p].x)[o] + d;
            } else if (!(last == e && first == s))
                iup_range(z, s, e, last, first, o);
        }
        s = e + 1;
    }
}

/* ---- the stack ---- */

static ttfx_i32 bc_pop(ttfx__bc *bc)
{
    if (!bc->sp) {
        if (!bc->error)
            bc->error = BC_ESTACK;
        return 0;
    }
    return bc->stack[--bc->sp];
}

static void bc_push(ttfx__bc *bc, ttfx_i32 v)
{
    if (bc->sp >= bc->maxsp) {
        if (!bc->error)
            bc->error = BC_ESTACK;
        return;
    }
    bc->stack[bc->sp++] = v;
}

/* ---- the CVT: kept scaled by the y scale; read and written along x
   at the x scale when the two differ ---- */

static ttfx_i32 bc_cvt(ttfx__bc *bc, ttfx_u32 i)
{
    ttfx_i32 v;
    if (i >= bc->ncvt) {
        if (!bc->error)
            bc->error = BC_ECVT;
        return 0;
    }
    v = bc->cvt[(unsigned)i];
    if (bc->stretched && bc_abs(bc->gs.pv[0]) > bc_abs(bc->gs.pv[1]))
        v = muldiv(v, bc->ppemx, bc->ppemy);
    return v;
}

static void bc_wcvt(ttfx__bc *bc, ttfx_u32 i, ttfx_i32 v)
{
    if (i >= bc->ncvt) {
        if (!bc->error)
            bc->error = BC_ECVT;
        return;
    }
    if (bc->stretched && bc_abs(bc->gs.pv[0]) > bc_abs(bc->gs.pv[1]))
        v = muldiv(v, bc->ppemy, bc->ppemx);
    bc->cvt[(unsigned)i] = v;
}

static ttfx_u16 bc_ppem(ttfx__bc *bc)
{
    return bc_abs(bc->gs.pv[0]) > bc_abs(bc->gs.pv[1]) ? bc->ppemx : bc->ppemy;
}

/* ---- the instruction stream ---- */

/* the offset of the instruction after the one at ip */
static ttfx_u16 bc_next(const ttfx_u8 TTFX_FAR *code, ttfx_u16 ip, ttfx_u16 end)
{
    ttfx_u8 op = code[ip++];
    if (op == 0x40)
        return ip < end ? ip + 1 + code[ip] : end + 1;
    if (op == 0x41)
        return ip < end ? ip + 1 + 2 * code[ip] : end + 1;
    if (op >= 0xB0 && op <= 0xB7)
        return ip + (op - 0xAF);
    if (op >= 0xB8 && op <= 0xBF)
        return ip + 2 * (op - 0xB7);
    return ip;
}

/* from just after an IF (else_too) or an ELSE, past the matching ELSE or
   EIF; end + 1 if there is none */
static ttfx_u16 bc_skip_if(const ttfx_u8 TTFX_FAR *code, ttfx_u16 ip, ttfx_u16 end, int else_too)
{
    int depth = 0;
    while (ip < end) {
        ttfx_u8 op = code[ip];
        if (op == 0x58)
            depth++;
        else if (op == 0x59) {
            if (!depth)
                return ip + 1;
            depth--;
        } else if (op == 0x1B && !depth && else_too)
            return ip + 1;
        ip = bc_next(code, ip, end);
    }
    return end + 1;
}

/* is opcode op one of version 3's instructions? (else an IDEF may
   define it) */
static int bc_defined(ttfx_u8 op)
{
    return !(op == 0x28 || op == 0x7B || op == 0x83 || op == 0x84 || (op >= 0x8F && op <= 0xAF));
}

/* ---- DELTAs ---- */

static void bc_delta(ttfx__bc *bc, int which, ttfx_u8 op)
{
    ttfx_i32 n = bc_pop(bc), i;
    ttfx_u16 ppem = bc_ppem(bc);
    ttfx_i32 base = bc->gs.delta_base + which * 16;
    for (i = 0; i < n && !bc->error; i++) {
        ttfx_u32 p = (ttfx_u32)bc_pop(bc);
        ttfx_i32 arg = bc_pop(bc), mag, d;
        if (((arg >> 4) & 15) + base != ppem)
            continue;
        mag = (arg & 15) - 8;
        if (mag >= 0)
            mag++;
        /* steps of 1 / 2^delta_shift pixel */
        d = mag * 64;
        d = d >= 0 ? d >> bc->gs.delta_shift : -((-d) >> bc->gs.delta_shift);
#ifdef BC_TRACE
        if (ttfx__bcTrace)
            printf("   DELTA%c%d p %ld arg %ld (%02lX) -> %ld\n", op >= 0x73 ? 'C' : 'P', which + 1, (long)p, (long)arg, (long)arg, (long)d);
#endif
        if (op >= 0x73)                 /* DELTAC */
            bc_wcvt(bc, p, bc_cvt(bc, p) + d);
        else
            bc_move(bc, bc_zone(bc, 0), p, d);
    }
}

/* ---- the interpreter ---- */

/* (in line: most instructions pop and push; stk and maxsp are
   ttfx__bcRun's copies, fixed while it runs: sp stays in bc, which the
   helpers pop and push) */
#define POP() (bc->sp ? stk[--bc->sp] : bc_pop(bc))
#define PUSH(v) do { ttfx_i32 v_ = (v); if (bc->sp < maxsp) stk[bc->sp++] = v_; else bc_push(bc, v_); } while (0)
#define GS bc->gs

int ttfx__bcLastError, ttfx__bcLastProg, ttfx__bcTrace;
ttfx_u16 ttfx__bcLastIp;

int ttfx__bcRun(ttfx__bc *bc, int prog)
{
    const ttfx_u8 TTFX_FAR *code = bc->code[prog];
    ttfx_u16 ip = 0, end = bc->len[prog];
    int ncall = 0;
    ttfx__frame calls[BC_MAXCALL];
    ttfx_u8 cprog = (ttfx_u8)prog;
    ttfx_i32 TTFX_FAR *stk = bc->stack;
    ttfx_u16 maxsp = bc->maxsp;
    ttfx_u32 budget = bc->budget;       /* (on the stack: tested each instruction) */
    bc->prog = (ttfx_u8)prog;
    bc_vectors(bc);
    while (!bc->error) {
        ttfx_u8 op;
        ttfx_u16 next;
        ttfx_i32 a, b;
        if (ip >= end) {
            if (ncall)
                bc->error = BC_ECODE;   /* a function without ENDF */
            break;
        }
        if (!budget--) {
            bc->error = BC_ELOOP;
            break;
        }
        op = code[ip];
        next = ip + 1;
#ifdef BC_TRACE
        if (ttfx__bcTrace) {
            extern void bc_trace(ttfx__bc *, int, unsigned, int);
            bc_trace(bc, cprog, ip, op);
        }
#endif
        if (op >= 0xC0) {
            /* MDRP[abcde] (C0-DF), MIRP[abcde] (E0-FF): a set rp0, b keep
               the minimum distance, c round (and for MIRP the cut-in), de
               the distance type (no engine compensation on a screen) */
            ttfx__zone *z0 = bc_zone(bc, 0), *z1 = bc_zone(bc, 1);
            ttfx_i32 d, od, cd;
            ttfx_u32 p;
            ttfx_u16 r0 = GS.rp[0];
            if (op >= 0xE0) {
                ttfx_u32 n = (ttfx_u32)POP();
                ttfx_i32 cv;
                p = (ttfx_u32)POP();
                cv = bc_cvt(bc, n);
                if (bc->error)
                    break;
                /* the single width, if the CVT value is within its cut-in */
                if (bc_abs(cv - GS.sw_value) < GS.sw_cutin)
                    cv = cv >= 0 ? GS.sw_value : -GS.sw_value;
                if (z1 == &bc->z[0]) {
                    /* a twilight point: made afresh, cv from rp0 */
                    ttfx_point TTFX_FAR *o = bc_pt(bc, z1, p, 1);
                    *o = *bc_pt(bc, z0, r0, 1);
                    bc_move_org(bc, z1, p, cv);
                    *bc_pt(bc, z1, p, 0) = *o;
                }
                od = bc_odist(bc, z1, p, z0, r0);
                cd = bc_cdist(bc, z1, p, z0, r0);
                if (GS.auto_flip && (od ^ cv) < 0)
                    cv = -cv;
                if (op & 4) {
                    if (bc_abs(cv - od) > GS.cvt_cutin)
                        cv = od;
                    d = ttfx__bcRound(bc, bc_comp(cv, op & 3));
                } else
                    d = cv;
            } else {
                p = (ttfx_u32)POP();
                od = bc_odist(bc, z1, p, z0, r0);
                cd = bc_cdist(bc, z1, p, z0, r0);
                if (GS.sw_cutin > 0 && bc_abs(od - GS.sw_value) < GS.sw_cutin)
                    od = od >= 0 ? GS.sw_value : -GS.sw_value;
                d = op & 4 ? ttfx__bcRound(bc, bc_comp(od, op & 3)) : od;
            }
            if (op & 8) {               /* the minimum distance, on the original's side */
                if (od >= 0) {
                    if (d < GS.min_dist)
                        d = GS.min_dist;
                } else if (d > -GS.min_dist)
                    d = -GS.min_dist;
            }
#ifdef BC_TRACE
            if (ttfx__bcTrace)
                printf("   M%cRP p %ld rp0 %u: od %ld cd %ld -> d %ld\n", op >= 0xE0 ? 'I' : 'D', (long)p, r0, (long)od, (long)cd, (long)d);
#endif
            bc_move(bc, z1, p, d - cd);
            GS.rp[1] = r0;
            GS.rp[2] = (ttfx_u16)p;
            if (op & 16)
                GS.rp[0] = (ttfx_u16)p;
        } else if (op >= 0xB0) {
            /* PUSHB[abc] (B0-B7), PUSHW[abc] (B8-BF) */
            int n = (op & 7) + 1, i;
            if (op < 0xB8) {
                if ((ttfx_u32)next + n > end) {
                    bc->error = BC_ECODE;
                    break;
                }
                for (i = 0; i < n; i++)
                    PUSH(code[next + i]);
                next += n;
            } else {
                if ((ttfx_u32)next + 2 * n > end) {
                    bc->error = BC_ECODE;
                    break;
                }
                for (i = 0; i < n; i++)
                    PUSH((ttfx_i16)((code[next + 2 * i] << 8) | code[next + 2 * i + 1]));
                next += 2 * n;
            }
        } else switch (op) {
        case 0x00: case 0x01:           /* SVTCA[a]: both vectors along y (0) or x (1) */
        case 0x02: case 0x03:           /* SPVTCA[a] */
        case 0x04: case 0x05: {         /* SFVTCA[a] */
            ttfx_i16 x = (op & 1) ? 0x4000 : 0, y = (op & 1) ? 0 : 0x4000;
            if (op < 4) {
                GS.pv[0] = GS.dv[0] = x;
                GS.pv[1] = GS.dv[1] = y;
            }
            if (op < 2 || op >= 4) {
                GS.fv[0] = x;
                GS.fv[1] = y;
            }
            bc_vectors(bc);
            break;
        }
        case 0x06: case 0x07:           /* SPVTL[a] */
        case 0x08: case 0x09:           /* SFVTL[a] */
        case 0x86: case 0x87: {         /* SDPVTL[a] */
            /* from p2 (popped first, zp2) to p1 (zp1); a: turned 90
               degrees counter-clockwise */
            ttfx_u32 p2 = (ttfx_u32)POP(), p1 = (ttfx_u32)POP();
            ttfx_point TTFX_FAR *c1 = bc_pt(bc, bc_zone(bc, 1), p1, 0), TTFX_FAR *c2 = bc_pt(bc, bc_zone(bc, 2), p2, 0);
            ttfx_i32 dx = c1->x - c2->x, dy = c1->y - c2->y, t;
            ttfx_i16 v[2];
            if (bc->error)
                break;
            if (op & 1) {
                t = dx;
                dx = -dy;
                dy = t;
            }
            if (op >= 0x86) {
                /* the dual vector from the original positions, the
                   projection vector from the current ones */
                ttfx_point TTFX_FAR *o1 = bc_pt(bc, bc_zone(bc, 1), p1, 1), TTFX_FAR *o2 = bc_pt(bc, bc_zone(bc, 2), p2, 1);
                ttfx_i32 ex = o1->x - o2->x, ey = o1->y - o2->y;
                if (op & 1) {
                    t = ex;
                    ex = -ey;
                    ey = t;
                }
                if (unit_vector(dx, dy, v)) {
                    GS.pv[0] = v[0];
                    GS.pv[1] = v[1];
                }
                if (unit_vector(ex, ey, v)) {
                    GS.dv[0] = v[0];
                    GS.dv[1] = v[1];
                } else {
                    GS.dv[0] = GS.pv[0];
                    GS.dv[1] = GS.pv[1];
                }
            } else if (unit_vector(dx, dy, v)) {
                if (op < 8) {
                    GS.pv[0] = GS.dv[0] = v[0];
                    GS.pv[1] = GS.dv[1] = v[1];
                } else {
                    GS.fv[0] = v[0];
                    GS.fv[1] = v[1];
                }
            }
            bc_vectors(bc);
            break;
        }
        case 0x0A: case 0x0B:           /* SPVFS, SFVFS: y popped, then x */
            b = (ttfx_i16)POP();
            a = (ttfx_i16)POP();
            if (op == 0x0A) {
                GS.pv[0] = GS.dv[0] = (ttfx_i16)a;
                GS.pv[1] = GS.dv[1] = (ttfx_i16)b;
            } else {
                GS.fv[0] = (ttfx_i16)a;
                GS.fv[1] = (ttfx_i16)b;
            }
            bc_vectors(bc);
            break;
        case 0x0C:                      /* GPV */
            PUSH(GS.pv[0]);
            PUSH(GS.pv[1]);
            break;
        case 0x0D:                      /* GFV */
            PUSH(GS.fv[0]);
            PUSH(GS.fv[1]);
            break;
        case 0x0E:                      /* SFVTPV */
            GS.fv[0] = GS.pv[0];
            GS.fv[1] = GS.pv[1];
            bc_vectors(bc);
            break;
        case 0x0F: {                    /* ISECT: line A (a0 a1, zp1), B (b0 b1, zp0) */
            ttfx_u32 b1 = (ttfx_u32)POP(), b0 = (ttfx_u32)POP(), a1 = (ttfx_u32)POP(), a0 = (ttfx_u32)POP();
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *zp = bc_zone(bc, 2);
            ttfx_point TTFX_FAR *A0 = bc_pt(bc, bc_zone(bc, 1), a0, 0), TTFX_FAR *A1 = bc_pt(bc, bc_zone(bc, 1), a1, 0);
            ttfx_point TTFX_FAR *B0 = bc_pt(bc, bc_zone(bc, 0), b0, 0), TTFX_FAR *B1 = bc_pt(bc, bc_zone(bc, 0), b1, 0);
            ttfx_point TTFX_FAR *P = bc_pt(bc, zp, p, 0);
            ttfx_i32 dax = A1->x - A0->x, day = A1->y - A0->y, dbx = B1->x - B0->x, dby = B1->y - B0->y;
            ttfx_i32 disc, num;
            if (bc->error)
                break;
            /* in 1/8 pixels the products fit 32 bits for outlines up to
               some 1400 pixels */
            disc = (dax >> 3) * (dby >> 3) - (day >> 3) * (dbx >> 3);
            if (!disc) {
                P->x = (A0->x + A1->x + B0->x + B1->x) / 4;
                P->y = (A0->y + A1->y + B0->y + B1->y) / 4;
            } else {
                ttfx_i32 wx = B0->x - A0->x, wy = B0->y - A0->y;
                num = (wx >> 3) * (dby >> 3) - (wy >> 3) * (dbx >> 3);
                P->x = A0->x + muldiv(dax, num, disc);
                P->y = A0->y + muldiv(day, num, disc);
            }
            if (p < zp->n)
                zp->flags[(unsigned)p] |= BC_TX | BC_TY;
            break;
        }
        case 0x10: case 0x11: case 0x12: /* SRP0, SRP1, SRP2 */
            GS.rp[op - 0x10] = (ttfx_u16)POP();
            break;
        case 0x13: case 0x14: case 0x15: /* SZP0, SZP1, SZP2 */
        case 0x16:                      /* SZPS */
            a = POP();
            if (a != 0 && a != 1) {
                bc->error = BC_EPOINT;
                break;
            }
            if (op == 0x16)
                GS.zp[0] = GS.zp[1] = GS.zp[2] = (ttfx_u8)a;
            else
                GS.zp[op - 0x13] = (ttfx_u8)a;
            break;
        case 0x17:                      /* SLOOP */
            a = POP();
            GS.loop = (ttfx_i16)(a < 0 ? 0 : a > 0x7FFF ? 0x7FFF : a);
            break;
        case 0x18: GS.round_state = BC_RTG; break;      /* RTG */
        case 0x19: GS.round_state = BC_RTHG; break;     /* RTHG */
        case 0x1A: GS.min_dist = POP(); break;          /* SMD */
        case 0x1B:                      /* ELSE (the IF's true part done) */
            next = bc_skip_if(code, next, end, 0);
            break;
        case 0x1C:                      /* JMPR */
            a = POP();
            next = (ttfx_u16)(ip + a);
            if (a == 0 || ip + a < 0 || ip + a > end)
                bc->error = BC_ECODE;
            break;
        case 0x1D: GS.cvt_cutin = POP(); break;         /* SCVTCI */
        case 0x1E: GS.sw_cutin = POP(); break;          /* SSWCI */
        case 0x1F:                      /* SSW: in font units */
            GS.sw_value = ttfx_Scale26(POP(), bc->sy);
            break;
        case 0x20:                      /* DUP */
            a = POP();
            PUSH(a);
            PUSH(a);
            break;
        case 0x21: POP(); break;        /* POP */
        case 0x22: bc->sp = 0; break;   /* CLEAR */
        case 0x23:                      /* SWAP */
            a = POP();
            b = POP();
            PUSH(a);
            PUSH(b);
            break;
        case 0x24: PUSH(bc->sp); break; /* DEPTH */
        case 0x25: case 0x26: {         /* CINDEX, MINDEX */
            ttfx_i32 k = POP();
            if (k <= 0 || k > (ttfx_i32)bc->sp) {
                bc->error = BC_ESTACK;
                break;
            }
            a = bc->stack[bc->sp - (unsigned)k];
            if (op == 0x26) {
                unsigned i;
                for (i = bc->sp - (unsigned)k; i + 1 < bc->sp; i++)
                    bc->stack[i] = bc->stack[i + 1];
                bc->sp--;
            }
            PUSH(a);
            break;
        }
        case 0x27: {                    /* ALIGNPTS: both to their middle */
            ttfx_u32 p2 = (ttfx_u32)POP(), p1 = (ttfx_u32)POP();
            ttfx__zone *za = bc_zone(bc, 1), *zb = bc_zone(bc, 0);
            ttfx_i32 d = bc_cdist(bc, zb, p2, za, p1) / 2;
            bc_move(bc, za, p1, d);
            bc_move(bc, zb, p2, -d);
            break;
        }
        case 0x29: {                    /* UTP */
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *z = bc_zone(bc, 0);
            if (p >= z->n) {
                bc->error = BC_EPOINT;
                break;
            }
            if (GS.fv[0])
                z->flags[(unsigned)p] &= ~BC_TX;
            if (GS.fv[1])
                z->flags[(unsigned)p] &= ~BC_TY;
            break;
        }
        case 0x2A: case 0x2B: {         /* LOOPCALL, CALL */
            ttfx_u32 fn = (ttfx_u32)POP();
            ttfx_i32 count = op == 0x2A ? POP() : 1;
            ttfx__def TTFX_FAR *d;
            if (bc->error)
                break;
            if (fn >= bc->nfdef || !bc->fdef[(unsigned)fn].defined) {
                bc->error = BC_EFUNC;
                break;
            }
            if (count <= 0)
                break;
            if (ncall >= BC_MAXCALL) {
                bc->error = BC_ECALL;
                break;
            }
            d = &bc->fdef[(unsigned)fn];
            calls[ncall].code = code;
            calls[ncall].ip = next;
            calls[ncall].end = end;
            calls[ncall].prog = cprog;
            calls[ncall].start = d->start;
            calls[ncall].count = count;
            ncall++;
            cprog = d->prog;
            code = bc->code[cprog];
            end = d->end + 1;
            next = d->start;
            break;
        }
        case 0x2C: case 0x89: {         /* FDEF, IDEF: only in fpgm and prep */
            ttfx_u32 n = (ttfx_u32)POP();
            ttfx_u16 e = next;
            ttfx__def TTFX_FAR *d = 0;
            if (bc->error)
                break;
            if (cprog > 1) {
                bc->error = BC_EFUNC;
                break;
            }
            while (e < end && code[e] != 0x2D && code[e] != 0x2C && code[e] != 0x89)
                e = bc_next(code, e, end);
            if (e >= end || code[e] != 0x2D) {
                bc->error = BC_EFUNC;
                break;
            }
            if (op == 0x2C) {
                if (n >= bc->nfdef) {
                    bc->error = BC_EFUNC;
                    break;
                }
                d = &bc->fdef[(unsigned)n];
            } else {
                unsigned i;
                for (i = 0; i < bc->nidef; i++)
                    if (!bc->idef[i].defined || bc->idef[i].opcode == n) {
                        d = &bc->idef[i];
                        break;
                    }
                if (!d) {
                    bc->error = BC_EFUNC;
                    break;
                }
                d->opcode = (ttfx_u16)n;
            }
            d->prog = cprog;
            d->defined = 1;
            d->start = next;
            d->end = e;
            next = e + 1;
            break;
        }
        case 0x2D:                      /* ENDF */
            if (!ncall) {
                bc->error = BC_ECODE;
                break;
            }
            if (--calls[ncall - 1].count > 0) {
                next = calls[ncall - 1].start;
                break;
            }
            ncall--;
            code = calls[ncall].code;
            end = calls[ncall].end;
            cprog = calls[ncall].prog;
            next = calls[ncall].ip;
            break;
        case 0x2E: case 0x2F: {         /* MDAP[a]: a rounds */
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *z = bc_zone(bc, 0);
            ttfx_i32 d = 0;
            if (op & 1) {
                ttfx_point TTFX_FAR *c = bc_pt(bc, z, p, 0);
                ttfx_i32 cd = proj(bc, c->x, c->y);
                d = ttfx__bcRound(bc, cd) - cd;
            }
            bc_move(bc, z, p, d);
            GS.rp[0] = GS.rp[1] = (ttfx_u16)p;
            break;
        }
        case 0x30: case 0x31:           /* IUP[a]: 0 in y, 1 in x */
            bc_iup(bc, op == 0x30);
            break;
        case 0x32: case 0x33: {         /* SHP[a] */
            ttfx_i32 dx, dy;
            ttfx_u16 rp;
            ttfx__zone *z = bc_zone(bc, 2);
            bc_refshift(bc, op & 1, &rp, &dx, &dy);
            while (GS.loop-- > 0 && !bc->error)
                bc_shift(bc, z, (ttfx_u32)POP(), dx, dy, 1);
            GS.loop = 1;
            break;
        }
        case 0x34: case 0x35: {         /* SHC[a] */
            ttfx_u32 c = (ttfx_u32)POP();
            ttfx_i32 dx, dy;
            ttfx_u16 rp;
            ttfx__zone *z = bc_zone(bc, 2);
            ttfx__zone *zr = bc_refshift(bc, op & 1, &rp, &dx, &dy);
            unsigned s, e, p;
            if (c >= z->nc) {
                bc->error = BC_EPOINT;
                break;
            }
            s = c ? z->ends[(unsigned)c - 1] + 1 : 0;
            e = z->ends[(unsigned)c];
            for (p = s; p <= e && p < z->n; p++)
                if (zr != z || p != rp)
                    bc_shift(bc, z, p, dx, dy, 1);
            break;
        }
        case 0x36: case 0x37: {         /* SHZ[a]: zone e, untouched */
            ttfx_i32 e = POP(), dx, dy;
            ttfx_u16 rp;
            ttfx__zone *z, *zr;
            unsigned p, n;
            if (e != 0 && e != 1) {
                bc->error = BC_EPOINT;
                break;
            }
            z = &bc->z[(unsigned)e];
            zr = bc_refshift(bc, op & 1, &rp, &dx, &dy);
            /* (the glyph zone's phantom points stay) */
            n = e ? (z->nc ? z->ends[z->nc - 1] + 1 : 0) : z->n;
            for (p = 0; p < n; p++)
                if (zr != z || p != rp)
                    bc_shift(bc, z, p, dx, dy, 0);
            break;
        }
        case 0x38: {                    /* SHPIX: along the freedom vector */
            ttfx_i32 d = POP();
            ttfx_i32 dx = mul14(d, GS.fv[0]), dy = mul14(d, GS.fv[1]);
            ttfx__zone *z = bc_zone(bc, 2);
            while (GS.loop-- > 0 && !bc->error)
                bc_shift(bc, z, (ttfx_u32)POP(), dx, dy, 1);
            GS.loop = 1;
            break;
        }
        case 0x39: {                    /* IP */
            ttfx__zone *z1 = bc_zone(bc, 0), *z2 = bc_zone(bc, 1), *zp = bc_zone(bc, 2);
            ttfx_u16 r1 = GS.rp[1], r2 = GS.rp[2];
            ttfx_i32 orange = bc_odist(bc, z2, r2, z1, r1);
            ttfx_i32 crange = bc_cdist(bc, z2, r2, z1, r1);
            while (GS.loop-- > 0 && !bc->error) {
                ttfx_u32 p = (ttfx_u32)POP();
                ttfx_i32 od = bc_odist(bc, zp, p, z1, r1), cd = bc_cdist(bc, zp, p, z1, r1);
                ttfx_i32 nd = orange ? muldiv(od, crange, orange) : od;
                bc_move(bc, zp, p, nd - cd);
            }
            GS.loop = 1;
            break;
        }
        case 0x3A: case 0x3B: {         /* MSIRP[a] */
            ttfx_i32 d = POP();
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *z0 = bc_zone(bc, 0), *z1 = bc_zone(bc, 1);
            ttfx_u16 r0 = GS.rp[0];
            if (z1 == &bc->z[0]) {
                ttfx_point TTFX_FAR *o = bc_pt(bc, z1, p, 1);
                *o = *bc_pt(bc, z0, r0, 1);
                bc_move_org(bc, z1, p, d);
                *bc_pt(bc, z1, p, 0) = *o;
            }
            bc_move(bc, z1, p, d - bc_cdist(bc, z1, p, z0, r0));
            GS.rp[1] = r0;
            GS.rp[2] = (ttfx_u16)p;
            if (op & 1)
                GS.rp[0] = (ttfx_u16)p;
            break;
        }
        case 0x3C: {                    /* ALIGNRP */
            ttfx__zone *z0 = bc_zone(bc, 0), *z1 = bc_zone(bc, 1);
            while (GS.loop-- > 0 && !bc->error) {
                ttfx_u32 p = (ttfx_u32)POP();
                bc_move(bc, z1, p, -bc_cdist(bc, z1, p, z0, GS.rp[0]));
            }
            GS.loop = 1;
            break;
        }
        case 0x3D: GS.round_state = BC_RTDG; break;     /* RTDG */
        case 0x3E: case 0x3F: {         /* MIAP[a]: to CVT entry n */
            ttfx_u32 n = (ttfx_u32)POP(), p = (ttfx_u32)POP();
            ttfx__zone *z = bc_zone(bc, 0);
            ttfx_i32 d = bc_cvt(bc, n), cd;
            ttfx_point TTFX_FAR *c;
            if (bc->error)
                break;
            if (z == &bc->z[0]) {
                /* a twilight point: put where the CVT says, along the
                   freedom vector from the origin */
                ttfx_point TTFX_FAR *o = bc_pt(bc, z, p, 1);
                o->x = mul14(d, GS.fv[0]);
                o->y = mul14(d, GS.fv[1]);
                *bc_pt(bc, z, p, 0) = *o;
            }
            c = bc_pt(bc, z, p, 0);
            cd = proj(bc, c->x, c->y);
            if (op & 1) {
                if (bc_abs(d - cd) > GS.cvt_cutin)
                    d = cd;
                d = ttfx__bcRound(bc, d);
            }
            bc_move(bc, z, p, d - cd);
            GS.rp[0] = GS.rp[1] = (ttfx_u16)p;
            break;
        }
        case 0x40: case 0x41: {         /* NPUSHB, NPUSHW */
            unsigned n, i;
            if (next >= end) {
                bc->error = BC_ECODE;
                break;
            }
            n = code[next++];
            if ((ttfx_u32)next + (op == 0x40 ? n : 2 * n) > end) {
                bc->error = BC_ECODE;
                break;
            }
            for (i = 0; i < n; i++)
                if (op == 0x40)
                    PUSH(code[next + i]);
                else
                    PUSH((ttfx_i16)((code[next + 2 * i] << 8) | code[next + 2 * i + 1]));
            next += op == 0x40 ? n : 2 * n;
            break;
        }
        case 0x42:                      /* WS */
            a = POP();
            b = POP();
            if ((ttfx_u32)b >= bc->nstor) {
                bc->error = BC_ESTOR;
                break;
            }
            bc->stor[(unsigned)b] = a;
            break;
        case 0x43:                      /* RS */
            a = POP();
            if ((ttfx_u32)a >= bc->nstor) {
                bc->error = BC_ESTOR;
                break;
            }
            PUSH(bc->stor[(unsigned)a]);
            break;
        case 0x44:                      /* WCVTP: in pixels */
            a = POP();
            b = POP();
            bc_wcvt(bc, (ttfx_u32)b, a);
            break;
        case 0x70:                      /* WCVTF: in font units */
            a = POP();
            b = POP();
            bc_wcvt(bc, (ttfx_u32)b, ttfx_Scale26(a, bc_abs(GS.pv[0]) > bc_abs(GS.pv[1]) ? bc->sx : bc->sy));
            break;
        case 0x45:                      /* RCVT */
            a = POP();
            PUSH(bc_cvt(bc, (ttfx_u32)a));
            break;
        case 0x46: case 0x47: {         /* GC[a]: 0 current, 1 original */
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *z = bc_zone(bc, 2);
            if (op == 0x46) {
                ttfx_point TTFX_FAR *c = bc_pt(bc, z, p, 0);
                PUSH(proj(bc, c->x, c->y));
            } else {
                ttfx_point TTFX_FAR *o = bc_pt(bc, z, p, 1);
                PUSH(dproj(bc, o->x, o->y));
            }
            break;
        }
        case 0x48: {                    /* SCFS */
            ttfx_i32 v = POP();
            ttfx_u32 p = (ttfx_u32)POP();
            ttfx__zone *z = bc_zone(bc, 2);
            ttfx_point TTFX_FAR *c = bc_pt(bc, z, p, 0);
            bc_move(bc, z, p, v - proj(bc, c->x, c->y));
            if (z == &bc->z[0])
                *bc_pt(bc, z, p, 1) = *c;
            break;
        }
        case 0x49: case 0x4A: {         /* MD[a]: 0 grid-fitted, 1 original */
            /* from p2 (popped second, zp0) to p1 (popped first, zp1) */
            ttfx_u32 p1 = (ttfx_u32)POP(), p2 = (ttfx_u32)POP();
            ttfx__zone *za = bc_zone(bc, 0), *zb = bc_zone(bc, 1);
            if (op == 0x49)
                PUSH(bc_cdist(bc, za, p2, zb, p1));
            else
                PUSH(bc_odist(bc, za, p2, zb, p1));
            break;
        }
        case 0x4B: PUSH(bc_ppem(bc)); break;            /* MPPEM */
        case 0x4C:                      /* MPS: the point size at 96 dpi, 26.6 */
            PUSH(muldiv(bc_ppem(bc), 72 * 64, 96));
            break;
        case 0x4D: GS.auto_flip = 1; break;             /* FLIPON */
        case 0x4E: GS.auto_flip = 0; break;             /* FLIPOFF */
        case 0x4F: POP(); break;        /* DEBUG */
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
            /* LT, LTEQ, GT, GTEQ, EQ, NEQ: e1 (deeper) against e2 (top) */
            b = POP();
            a = POP();
            switch (op) {
            case 0x50: PUSH(a < b); break;
            case 0x51: PUSH(a <= b); break;
            case 0x52: PUSH(a > b); break;
            case 0x53: PUSH(a >= b); break;
            case 0x54: PUSH(a == b); break;
            default: PUSH(a != b); break;
            }
            break;
        case 0x56: case 0x57:           /* ODD, EVEN: rounded first */
            a = ttfx__bcRound(bc, POP());
            a = (a >> 6) & 1;
            PUSH(op == 0x56 ? a : !a);
            break;
        case 0x58:                      /* IF */
            if (!POP())
                next = bc_skip_if(code, next, end, 1);
            break;
        case 0x59: break;               /* EIF */
        case 0x5A: b = POP(); a = POP(); PUSH(a && b); break;  /* AND */
        case 0x5B: b = POP(); a = POP(); PUSH(a || b); break;  /* OR */
        case 0x5C: PUSH(!POP()); break; /* NOT */
        case 0x5D: bc_delta(bc, 0, op); break;          /* DELTAP1 */
        case 0x71: bc_delta(bc, 1, op); break;          /* DELTAP2 */
        case 0x72: bc_delta(bc, 2, op); break;          /* DELTAP3 */
        case 0x73: bc_delta(bc, 0, op); break;          /* DELTAC1 */
        case 0x74: bc_delta(bc, 1, op); break;          /* DELTAC2 */
        case 0x75: bc_delta(bc, 2, op); break;          /* DELTAC3 */
        case 0x5E: GS.delta_base = (ttfx_i16)POP(); break;   /* SDB */
        case 0x5F:                      /* SDS */
            a = POP();
            GS.delta_shift = (ttfx_i16)(a < 0 ? 0 : a > 6 ? 6 : a);
            break;
        case 0x60: b = POP(); a = POP(); PUSH(a + b); break;   /* ADD */
        case 0x61: b = POP(); a = POP(); PUSH(a - b); break;   /* SUB */
        case 0x62:                      /* DIV: n2 * 64 / n1, truncated */
            b = POP();
            a = POP();
            if (!b) {
                bc->error = BC_EDIV;
                break;
            }
            PUSH(ttfx__MulDiv(a, 64, b, 0));
            break;
        case 0x63:                      /* MUL: n1 * n2 / 64, truncated */
            b = POP();
            a = POP();
            PUSH(ttfx__MulDiv(a, b, 64, 0));
            break;
        case 0x64: PUSH(bc_abs(POP())); break;          /* ABS */
        case 0x65: PUSH(-POP()); break; /* NEG */
        case 0x66: PUSH(POP() & ~63L); break;           /* FLOOR */
        case 0x67: PUSH((POP() + 63) & ~63L); break;    /* CEILING */
        case 0x68: case 0x69: case 0x6A: case 0x6B:     /* ROUND[ab] */
            PUSH(ttfx__bcRound(bc, bc_comp(POP(), op & 3)));
            break;
        case 0x6C: case 0x6D: case 0x6E: case 0x6F:     /* NROUND[ab]: compensation only */
            PUSH(bc_comp(POP(), op & 3));
            break;
        case 0x76: bc_sround(bc, POP(), 64); break;     /* SROUND */
        case 0x77: bc_sround(bc, POP(), 45); break;     /* S45ROUND */
        case 0x78: case 0x79:           /* JROT, JROF: e popped, then the offset */
            a = POP();
            b = POP();
            if ((op == 0x78) == (a != 0)) {
                next = (ttfx_u16)(ip + b);
                if (b == 0 || ip + b < 0 || ip + b > end)
                    bc->error = BC_ECODE;
            }
            break;
        case 0x7A: GS.round_state = BC_ROFF; break;     /* ROFF */
        case 0x7C: GS.round_state = BC_RUTG; break;     /* RUTG */
        case 0x7D: GS.round_state = BC_RDTG; break;     /* RDTG */
        case 0x7E: case 0x7F: POP(); break;             /* SANGW, AA (no longer supported) */
        case 0x80: {                    /* FLIPPT */
            ttfx__zone *z = bc_zone(bc, 0);
            while (GS.loop-- > 0 && !bc->error) {
                ttfx_u32 p = (ttfx_u32)POP();
                if (p >= z->n) {
                    bc->error = BC_EPOINT;
                    break;
                }
                z->flags[(unsigned)p] ^= BC_ON;
            }
            GS.loop = 1;
            break;
        }
        case 0x81: case 0x82: {         /* FLIPRGON, FLIPRGOFF */
            ttfx_u32 hi = (ttfx_u32)POP(), lo = (ttfx_u32)POP(), p;
            ttfx__zone *z = bc_zone(bc, 0);
            if (hi >= z->n || lo > hi) {
                bc->error = BC_EPOINT;
                break;
            }
            for (p = lo; p <= hi; p++)
                if (op == 0x81)
                    z->flags[(unsigned)p] |= BC_ON;
                else
                    z->flags[(unsigned)p] &= ~BC_ON;
            break;
        }
        case 0x85:                      /* SCANCTRL */
            GS.scan_control = (ttfx_u16)POP();
            break;
        case 0x8D:                      /* SCANTYPE: 0-3 (later modes: as before) */
            a = POP();
            if (a >= 0 && a <= 3)
                GS.scan_type = (ttfx_i16)(a == 3 ? 2 : a);
            break;
        case 0x88:                      /* GETINFO: version 3, Windows 3.1's */
            a = POP();
            b = 0;
            if (a & 1)
                b |= 3;
            if ((a & 4) && bc->stretched)
                b |= 0x200;
            PUSH(b);
            break;
        case 0x8A:                      /* ROLL: a b c -> b c a */
            a = POP();
            b = POP();
            {
                ttfx_i32 c = POP();
                PUSH(b);
                PUSH(a);
                PUSH(c);
            }
            break;
        case 0x8B: b = POP(); a = POP(); PUSH(a > b ? a : b); break;  /* MAX */
        case 0x8C: b = POP(); a = POP(); PUSH(a < b ? a : b); break;  /* MIN */
        case 0x8E:                      /* INSTCTRL: selector, value (in prep) */
            a = POP();
            b = POP();
            if (bc->prog == 1 && (a == 1 || a == 2))
                GS.instctrl = (ttfx_u8)((GS.instctrl & ~a) | (b & a));
            break;
        default: {
            /* an instruction defined by IDEF? */
            unsigned i;
            ttfx__def TTFX_FAR *d = 0;
            if (!bc_defined(op))
                for (i = 0; i < bc->nidef; i++)
                    if (bc->idef[i].defined && bc->idef[i].opcode == op) {
                        d = &bc->idef[i];
                        break;
                    }
            if (!d) {
                bc->error = BC_EOPCODE;
                break;
            }
            if (ncall >= BC_MAXCALL) {
                bc->error = BC_ECALL;
                break;
            }
            calls[ncall].code = code;
            calls[ncall].ip = next;
            calls[ncall].end = end;
            calls[ncall].prog = cprog;
            calls[ncall].start = d->start;
            calls[ncall].count = 1;
            ncall++;
            cprog = d->prog;
            code = bc->code[cprog];
            end = d->end + 1;
            next = d->start;
            break;
        }
        }
        if (next > end + 1)
            bc->error = BC_ECODE;
        if (bc->error) {
            ttfx__bcLastError = bc->error;
            ttfx__bcLastProg = cprog;
            ttfx__bcLastIp = ip;
            break;
        }
        ip = next;
    }
    bc->budget = budget;
    return !bc->error;
}

/* ---- a size: fpgm and prep ---- */

#define MAXP(o) (f->maxp ? ttUSHORT(f->maxp + (o)) : 0)
#define BC_SLACK 32                     /* stack elements past maxp's (some fonts need them) */
#define BC_BUDGET 1000000UL             /* instructions a program may run */

typedef struct {
    ttfx_u32 bytes;
    ttfx_u16 ncvt, nstor, nfdef, nidef, ntw, maxstack, fpgmlen, preplen;
    ttfx_i32 cvt, fpgm, prep;
} bc_layout;

static ttfx_u32 bc_layout_of(const ttfx_font *f, bc_layout *l, ttfx__bcsize *h)
{
    ttfx_u32 len, n;
    if (!f->maxp || ttfx_U32(f, f->maxp) < 0x00010000UL)
        return 0;
    l->cvt = ttfx_FindTable(f, "cvt ", &len);
    l->ncvt = l->cvt ? (ttfx_u16)(len / 2) : 0;
    l->fpgm = ttfx_FindTable(f, "fpgm", &len);
    l->fpgmlen = l->fpgm ? (ttfx_u16)(len > 65000UL ? 0 : len) : 0;
    l->prep = ttfx_FindTable(f, "prep", &len);
    l->preplen = l->prep ? (ttfx_u16)(len > 65000UL ? 0 : len) : 0;
    l->ntw = MAXP(16);
    l->nstor = MAXP(18);
    l->nfdef = MAXP(20);
    l->nidef = MAXP(22);
    l->maxstack = MAXP(24) + BC_SLACK;
    n = sizeof(ttfx__bcsize);
    if (h) {
        h->o_cvt = (ttfx_u16)n;
    }
    n += 4UL * l->ncvt;
    if (h)
        h->o_stor = (ttfx_u16)n;
    n += 4UL * l->nstor;
    if (h)
        h->o_fdef = (ttfx_u16)n;
    n += (ttfx_u32)sizeof(ttfx__def) * l->nfdef;
    if (h)
        h->o_idef = (ttfx_u16)n;
    n += (ttfx_u32)sizeof(ttfx__def) * l->nidef;
    if (h)
        h->o_tw = (ttfx_u16)n;
    n += (ttfx_u32)(3 * sizeof(ttfx_point) + 1) * l->ntw;
    n = (n + 1) & ~1UL;
    if (h)
        h->o_fpgm = (ttfx_u16)n;
    n += l->fpgmlen;
    if (h)
        h->o_prep = (ttfx_u16)n;
    n += l->preplen;
    l->bytes = n;
    return n > 65000UL ? 0 : n;
}

ttfx_u32 ttfx_bcBytes(const ttfx_font *f)
{
    bc_layout l;
    return bc_layout_of(f, &l, 0);
}


/* what the graphics state left by prep is for a glyph program: the
   vectors, loop, reference points and zone pointers start afresh with
   each glyph; the rest (rounding, distances, cut-ins, deltas, scan
   control) carries over */
static void bc_glyph_gs(ttfx__gs *gs)
{
    gs->pv[0] = gs->fv[0] = gs->dv[0] = 0x4000;
    gs->pv[1] = gs->fv[1] = gs->dv[1] = 0;
    gs->loop = 1;
    gs->rp[0] = gs->rp[1] = gs->rp[2] = 0;
    gs->zp[0] = gs->zp[1] = gs->zp[2] = 1;
}

#if defined(__WATCOMC__) && !defined(__386__)
/* (by words on the 80286: blocks under 64K) */
static void bc_zero(void TTFX_FAR *p, ttfx_u32 n)
{
    ttfx_u16 TTFX_FAR *d = (ttfx_u16 TTFX_FAR *)p;
    unsigned w = (unsigned)(n >> 1);
    while (w--)
        *d++ = 0;
    if (n & 1)
        *(ttfx_u8 TTFX_FAR *)d = 0;
}

static void bc_copy(void TTFX_FAR *dst, const void TTFX_FAR *src, ttfx_u32 n)
{
    ttfx_u16 TTFX_FAR *d = (ttfx_u16 TTFX_FAR *)dst;
    const ttfx_u16 TTFX_FAR *s = (const ttfx_u16 TTFX_FAR *)src;
    unsigned w = (unsigned)(n >> 1);
    while (w--)
        *d++ = *s++;
    if (n & 1)
        *(ttfx_u8 TTFX_FAR *)d = *(const ttfx_u8 TTFX_FAR *)s;
}
#else
/* (by bytes elsewhere: a compiler may assume words do not alias the
   structures copied) */
static void bc_zero(void TTFX_FAR *p, ttfx_u32 n)
{
    ttfx_u8 TTFX_FAR *d = (ttfx_u8 TTFX_FAR *)p;
    while (n--)
        *d++ = 0;
}

static void bc_copy(void TTFX_FAR *dst, const void TTFX_FAR *src, ttfx_u32 n)
{
    ttfx_u8 TTFX_FAR *d = (ttfx_u8 TTFX_FAR *)dst;
    const ttfx_u8 TTFX_FAR *s = (const ttfx_u8 TTFX_FAR *)src;
    while (n--)
        *d++ = *s++;
}
#endif

/* the interpreter's view of a prepared size (its arrays in the block) */
static void bc_attach(ttfx__bc *bc, const ttfx_font *f, ttfx__bcsize TTFX_FAR *h)
{
    ttfx_u8 TTFX_FAR *base = (ttfx_u8 TTFX_FAR *)h;
    ttfx_u8 TTFX_FAR *tw = base + h->o_tw;
    bc_zero(bc, sizeof *bc);
    bc->f = f;
    bc->base = base;
    bc->sz = h;
    bc->cvt = (ttfx_i32 TTFX_FAR *)(base + h->o_cvt);
    bc->stor = (ttfx_i32 TTFX_FAR *)(base + h->o_stor);
    bc->ncvt = h->ncvt;
    bc->nstor = h->nstor;
    bc->fdef = (ttfx__def TTFX_FAR *)(base + h->o_fdef);
    bc->idef = (ttfx__def TTFX_FAR *)(base + h->o_idef);
    bc->nfdef = h->nfdef;
    bc->nidef = h->nidef;
    bc->z[0].cur = (ttfx_point TTFX_FAR *)tw;
    bc->z[0].org = bc->z[0].cur + h->ntw;
    bc->z[0].ous = bc->z[0].org + h->ntw;
    bc->z[0].flags = (ttfx_u8 TTFX_FAR *)(bc->z[0].ous + h->ntw);
    bc->z[0].n = h->ntw;
    bc->code[0] = base + h->o_fpgm;
    bc->len[0] = h->fpgmlen;
    bc->code[1] = base + h->o_prep;
    bc->len[1] = h->preplen;
    bc->ppemx = h->ppemx;
    bc->ppemy = h->ppemy;
    bc->sx = h->sx;
    bc->sy = h->sy;
    bc->stretched = h->ppemx != h->ppemy;
    bc->maxsp = h->maxstack;
}

int ttfx_bcPrepare(ttfx_font *f, void TTFX_FAR *mem, ttfx_u32 bytes, ttfx_scale sx, ttfx_scale sy,
                   int ppem_x, int ppem_y)
{
    bc_layout l;
    ttfx__bcsize hdr;
    ttfx__bcsize TTFX_FAR *h = (ttfx__bcsize TTFX_FAR *)mem;
    ttfx__bc bc;
    ttfx_u32 need;
    ttfx_u16 i, mp, mc;
    int ok = 1;
    f->bc = 0;
    bc_zero(&hdr, sizeof hdr);
    need = bc_layout_of(f, &l, &hdr);
    if (!need || !mem || bytes < need || ppem_x <= 0 || ppem_y <= 0)
        return 0;
    bc_zero(mem, need);
    hdr.bytes = need;
    hdr.sx = sx;
    hdr.sy = sy;
    hdr.ppemx = (ttfx_u16)ppem_x;
    hdr.ppemy = (ttfx_u16)ppem_y;
    hdr.ncvt = l.ncvt;
    hdr.nstor = l.nstor;
    hdr.nfdef = l.nfdef;
    hdr.nidef = l.nidef;
    hdr.ntw = l.ntw;
    hdr.maxstack = l.maxstack;
    hdr.fpgmlen = l.fpgmlen;
    hdr.preplen = l.preplen;
    mp = MAXP(6);
    if (MAXP(10) > mp)
        mp = MAXP(10);
    mc = MAXP(8);
    if (MAXP(12) > mc)
        mc = MAXP(12);
    hdr.maxdepth = (ttfx_u8)(MAXP(30) > 8 ? 8 : MAXP(30));
    hdr.maxpts = mp + 2 * (hdr.maxdepth + 2) + 4;
    hdr.maxcont = mc + 1;
    bc_copy(h, &hdr, sizeof hdr);
    for (i = 0; i < l.ncvt; i++)
        ((ttfx_i32 TTFX_FAR *)((ttfx_u8 TTFX_FAR *)mem + hdr.o_cvt))[i] = ttfx_Scale26(ttSHORT(l.cvt + 2L * i), sy);
    if (l.fpgmlen)
        ttfx_Read(f, l.fpgm, (ttfx_u8 TTFX_FAR *)mem + hdr.o_fpgm, l.fpgmlen);
    if (l.preplen)
        ttfx_Read(f, l.prep, (ttfx_u8 TTFX_FAR *)mem + hdr.o_prep, l.preplen);

    bc_attach(&bc, f, h);
    bc.stack = (ttfx_i32 TTFX_FAR *)ttfx_alloc(f->alloc, 4UL * hdr.maxstack);
    if (!bc.stack)
        return 0;
    /* fpgm (here once per size: its definitions and storage are then the
       same for every size), then prep, each from the default state */
    ttfx__bcDefaults(&bc.gs);
    bc.budget = BC_BUDGET;
    if (l.fpgmlen)
        ok = ttfx__bcRun(&bc, 0);
    if (ok) {
        ttfx__bcDefaults(&bc.gs);
        bc.sp = 0;
        bc.budget = BC_BUDGET;
        if (l.preplen)
            ok = ttfx__bcRun(&bc, 1);
    }
    ttfx_free(f->alloc, bc.stack);
    if (!ok)
        return 0;
    /* INSTCTRL 2: glyph programs start from the defaults, not prep's */
    if (bc.gs.instctrl & 2) {
        ttfx_u8 ic = bc.gs.instctrl;
        ttfx__bcDefaults(&bc.gs);
        bc.gs.instctrl = ic;
    }
    bc_glyph_gs(&bc.gs);
    h->gs = bc.gs;
    h->ok = 1;
    f->bc = mem;
    return 1;
}

void ttfx_bcUse(ttfx_font *f, void TTFX_FAR *mem)
{
    f->bc = mem && ((ttfx__bcsize TTFX_FAR *)mem)->ok ? mem : 0;
}

/* ---- a glyph ---- */

typedef struct {
    const ttfx_font *f;
    ttfx__bc *bc;
    ttfx_point TTFX_FAR *cur, TTFX_FAR *org, TTFX_FAR *ous;
    ttfx_u8 TTFX_FAR *fl;
    ttfx_u16 TTFX_FAR *ends;
    ttfx_u16 n, nc, maxn, maxc;
    ttfx_u8 ran;                        /* a glyph program ran */
    ttfx__gs last;                      /* the state the last one left */
} bc_loader;

/* run a glyph program over points s.. (np of them, then the two phantom
   points) and contours cs.. (nc) */
static int bc_hint(bc_loader *L, unsigned s, unsigned np, unsigned cs, unsigned nc,
                   const ttfx_u8 TTFX_FAR *code, ttfx_u16 len, int ous_px)
{
    ttfx__bc *bc = L->bc;
    ttfx__zone *z = &bc->z[1];
    unsigned i;
    int ok;
    z->cur = L->cur + s;
    z->org = L->org + s;
    z->ous = L->ous + s;
    z->flags = L->fl + s;
    z->ends = L->ends + cs;
    z->n = (ttfx_u16)(np + 2);
    z->nc = (ttfx_u16)nc;
    z->ous_px = (ttfx_u8)ous_px;
    for (i = cs; i < cs + nc; i++)
        L->ends[i] -= (ttfx_u16)s;
    bc->gs = bc->sz->gs;
    bc->sp = 0;
    bc->code[2] = code;
    bc->len[2] = len;
    bc->budget = BC_BUDGET;
    ok = ttfx__bcRun(bc, 2);
    for (i = cs; i < cs + nc; i++)
        L->ends[i] += (ttfx_u16)s;
    L->last = bc->gs;
    L->ran = 1;
    return ok;
}

#define DSHORT(p) ((ttfx_i16)(((ttfx_u16)(p)[0] << 8) | (p)[1]))
#define DUSHORT(p) ((ttfx_u16)(((ttfx_u16)(p)[0] << 8) | (p)[1]))

static ttfx_i32 bc_rtg(ttfx_i32 v)
{
    return v >= 0 ? (v + 32) & ~63L : -((-v + 32) & ~63L);
}

/* the phantom points of a glyph at the grid: the origin (xMin less the
   left side bearing) rounded, and the advance from it, the advance width
   rounded on its own (Windows 3.1's hdmx widths are, where a glyph's
   xMin is not its left side bearing: Arial's parenright, Times New
   Roman's .notdef) */
static void bc_phantoms(bc_loader *L, ttfx_u16 glyph, ttfx_i32 xmin, ttfx_point *pp, ttfx_i32 *u)
{
    ttfx_u16 adv;
    int lsb;
    ttfx_GetGlyphHMetrics(L->f, glyph, &adv, &lsb);
    u[0] = xmin - lsb;
    u[1] = u[0] + adv;
    pp[0].x = bc_rtg(ttfx_Scale26(u[0], L->bc->sx));
    pp[1].x = pp[0].x + bc_rtg(ttfx_Scale26(adv, L->bc->sx));
    pp[0].y = pp[1].y = 0;
}

static int bc_glyph(bc_loader *L, ttfx_u16 glyph, int depth, ttfx_point *pp)
{
    const ttfx_font *f = L->f;
    ttfx__bc *bc = L->bc;
    ttfx_u32 glen;
    ttfx_i32 g = ttfx__GlyfOffset(f, glyph, &glen), u[2];
    ttfx_u8 TTFX_FAR *data, TTFX_FAR *end;
    ttfx_i16 nc;
    unsigned s = L->n, cs = L->nc, i;
    int hint = !(bc->sz->gs.instctrl & 1);
    if (depth > 8)
        return 0;
    if (g < 0 || glen < 10) {
        bc_phantoms(L, glyph, 0, pp, u);        /* (empty: the origin at 0) */
        return 1;
    }
    if (glen > 65000UL)
        return 0;
    data = (ttfx_u8 TTFX_FAR *)ttfx_alloc(f->alloc, glen + 2);
    if (!data)
        return 0;
    ttfx_Read(f, g, data, (unsigned)glen);
    end = data + (unsigned)glen;
    nc = DSHORT(data);
    bc_phantoms(L, glyph, DSHORT(data + 2), pp, u);
    if (nc >= 0) {
        ttfx_u8 TTFX_FAR *q = data + 10, TTFX_FAR *ins;
        unsigned np, inslen, k;
        ttfx_u8 flag = 0, rep = 0;
        ttfx_i32 v;
        if (10 + 2UL * nc + 2 > glen)
            goto fail;
        np = nc ? DUSHORT(q + 2 * (nc - 1)) + 1 : 0;
        /* in 32 bits: a last end near FFFFh would wrap s + np + 2 past
           a word and pass (a malformed glyph; np of 0 from FFFFh fails
           every end below) */
        if ((ttfx_u32)s + np + 2 > L->maxn || (ttfx_u32)cs + (unsigned)nc > L->maxc)
            goto fail;
        for (i = 0; i < (unsigned)nc; i++) {
            unsigned e = DUSHORT(q + 2 * i);
            if (e >= np || (i && e <= DUSHORT(q + 2 * (i - 1))))
                goto fail;
            L->ends[cs + i] = (ttfx_u16)(s + e);
        }
        q += 2 * nc;
        inslen = DUSHORT(q);
        ins = q + 2;
        /* a length, not a pointer sum: ins + inslen wraps the offset
           when inslen passes what the glyph holds */
        if (ins > end || inslen > (unsigned)(end - ins))
            goto fail;
        q = ins + inslen;
        /* the flags, then x, then y */
        for (i = 0; i < np; i++) {
            if (!rep) {
                if (q >= end)
                    goto fail;
                flag = *q++;
                if (flag & 8) {
                    if (q >= end)
                        goto fail;
                    rep = *q++;
                }
            } else
                rep--;
            L->fl[s + i] = flag;
        }
        for (k = 0; k < 2; k++) {
            ttfx_u8 sh = k ? 4 : 2, same = k ? 32 : 16;
            v = 0;
            for (i = 0; i < np; i++) {
                ttfx_u8 fl = L->fl[s + i];
                if (fl & sh) {
                    if (q >= end)
                        goto fail;
                    v += fl & same ? (ttfx_i32)*q : -(ttfx_i32)*q;
                    q++;
                } else if (!(fl & same)) {
                    if (q + 1 >= end)
                        goto fail;
                    v += DSHORT(q);
                    q += 2;
                }
                if (k)
                    L->ous[s + i].y = v;
                else
                    L->ous[s + i].x = v;
            }
        }
        for (i = 0; i < np; i++) {
            L->org[s + i].x = ttfx_Scale26(L->ous[s + i].x, bc->sx);
            L->org[s + i].y = ttfx_Scale26(L->ous[s + i].y, bc->sy);
            L->cur[s + i] = L->org[s + i];
            L->fl[s + i] &= BC_ON;
        }
        for (k = 0; k < 2; k++) {
            L->ous[s + np + k].x = u[k];
            L->ous[s + np + k].y = 0;
            L->org[s + np + k].x = ttfx_Scale26(u[k], bc->sx);
            L->org[s + np + k].y = 0;
            L->cur[s + np + k] = pp[k];
            L->fl[s + np + k] = 0;
        }
        if (inslen && hint) {
            if (!bc_hint(L, s, np, cs, (unsigned)nc, ins, (ttfx_u16)inslen, 0))
                goto fail;
            pp[0] = L->cur[s + np];
            pp[1] = L->cur[s + np + 1];
        }
        L->n = (ttfx_u16)(s + np);
        L->nc = (ttfx_u16)(cs + nc);
    } else {
        /* a compound glyph: each component loaded (and hinted by its own
           program), transformed, placed; then the compound's program */
        ttfx_u8 TTFX_FAR *q = data + 10;
        ttfx_u16 flags = 0;
        unsigned np, inslen = 0;
        do {
            ttfx_u16 gi;
            ttfx_i32 a1, a2, ma = 0x4000, mb = 0, mc = 0, md = 0x4000;
            int xform = 0;
            unsigned c0, c1;
            ttfx_point cpp[2];
            if (q + 4 > end)
                goto fail;
            flags = DUSHORT(q);
            gi = DUSHORT(q + 2);
            q += 4;
            if (flags & 1) {            /* ARG_1_AND_2_ARE_WORDS */
                if (q + 4 > end)
                    goto fail;
                a1 = flags & 2 ? (ttfx_i32)DSHORT(q) : (ttfx_i32)DUSHORT(q);
                a2 = flags & 2 ? (ttfx_i32)DSHORT(q + 2) : (ttfx_i32)DUSHORT(q + 2);
                q += 4;
            } else {
                if (q + 2 > end)
                    goto fail;
                a1 = flags & 2 ? (ttfx_i32)(ttfx_i8)q[0] : (ttfx_i32)q[0];
                a2 = flags & 2 ? (ttfx_i32)(ttfx_i8)q[1] : (ttfx_i32)q[1];
                q += 2;
            }
            /* the transform's bytes checked before they are read */
            if ((unsigned)(end - q) < (flags & 8 ? 2u : flags & 0x40 ? 4u : flags & 0x80 ? 8u : 0u))
                goto fail;
            if (flags & 8) {            /* WE_HAVE_A_SCALE */
                ma = md = DSHORT(q);
                q += 2;
                xform = 1;
            } else if (flags & 0x40) {  /* WE_HAVE_AN_X_AND_Y_SCALE */
                ma = DSHORT(q);
                md = DSHORT(q + 2);
                q += 4;
                xform = 1;
            } else if (flags & 0x80) {  /* WE_HAVE_A_TWO_BY_TWO */
                ma = DSHORT(q);
                mb = DSHORT(q + 2);
                mc = DSHORT(q + 4);
                md = DSHORT(q + 6);
                q += 8;
                xform = 1;
            }
            c0 = L->n;
            if (!bc_glyph(L, gi, depth + 1, cpp))
                goto fail;
            c1 = L->n;
            if (flags & 0x200) {        /* USE_MY_METRICS */
                pp[0] = cpp[0];
                pp[1] = cpp[1];
            }
            if (xform)
                for (i = c0; i < c1; i++) {
                    ttfx_i32 x = L->cur[i].x, y = L->cur[i].y;
                    L->cur[i].x = mul14(x, ma) + mul14(y, mc);
                    L->cur[i].y = mul14(x, mb) + mul14(y, md);
                }
            if (flags & 2) {            /* ARGS_ARE_XY_VALUES: an offset */
                ttfx_i32 dx, dy;
                if (xform && (flags & 0x800) && !(flags & 0x1000)) {
                    /* SCALED_COMPONENT_OFFSET: scaled by the transform's
                       columns' lengths */
                    a1 = mul14(a1, (ttfx_i32)isqrt((ttfx_u32)(ma * ma) + (ttfx_u32)(mb * mb)));
                    a2 = mul14(a2, (ttfx_i32)isqrt((ttfx_u32)(mc * mc) + (ttfx_u32)(md * md)));
                }
                dx = ttfx_Scale26(a1, bc->sx);
                dy = ttfx_Scale26(a2, bc->sy);
                if (flags & 4) {        /* ROUND_XY_TO_GRID */
                    dx = bc_rtg(dx);
                    dy = bc_rtg(dy);
                }
                for (i = c0; i < c1; i++) {
                    L->cur[i].x += dx;
                    L->cur[i].y += dy;
                }
            } else {
                /* matching points: the component's point a2 onto the
                   compound's point a1 */
                ttfx_i32 dx, dy;
                if ((ttfx_u32)a1 >= c0 - s || (ttfx_u32)a2 >= c1 - c0)
                    goto fail;
                dx = L->cur[s + (unsigned)a1].x - L->cur[c0 + (unsigned)a2].x;
                dy = L->cur[s + (unsigned)a1].y - L->cur[c0 + (unsigned)a2].y;
                for (i = c0; i < c1; i++) {
                    L->cur[i].x += dx;
                    L->cur[i].y += dy;
                }
            }
        } while (flags & 0x20);         /* MORE_COMPONENTS */
        if (flags & 0x100) {            /* WE_HAVE_INSTRUCTIONS */
            if (q + 2 > end)
                goto fail;
            inslen = DUSHORT(q);
            q += 2;
            if (inslen > (unsigned)(end - q))   /* (a pointer sum wraps) */
                goto fail;
        }
        np = L->n - s;
        if (inslen && hint) {
            /* the components as hinted are the compound's original */
            if (s + np + 2 > L->maxn)
                goto fail;
            for (i = s; i < s + np; i++) {
                L->org[i] = L->ous[i] = L->cur[i];
                L->fl[i] &= BC_ON;
            }
            for (i = 0; i < 2; i++) {
                L->cur[s + np + i] = L->org[s + np + i] = L->ous[s + np + i] = pp[i];
                L->fl[s + np + i] = 0;
            }
            if (!bc_hint(L, s, np, cs, L->nc - cs, q, (ttfx_u16)inslen, 1))
                goto fail;
            pp[0] = L->cur[s + np];
            pp[1] = L->cur[s + np + 1];
        }
    }
    ttfx_free(f->alloc, data);
    return 1;
fail:
    ttfx_free(f->alloc, data);
    return 0;
}

/* the scan converter's rules as scan control and the scan type say */
int ttfx__bcDropout(const ttfx__gs TTFX_FAR *gs, ttfx_u16 ppem, int stretched)
{
    ttfx_u16 v = gs->scan_control, t = v & 0xFF;
    int small = t == 0xFF || ppem <= t, on = 0;
    if ((v & 0x100) && small)
        on = 1;
    if ((v & 0x400) && stretched)
        on = 1;
    if ((v & 0x800) && !small)
        on = 0;
    if (v & 0x1000)                     /* (never rotated) */
        on = 0;
    if ((v & 0x2000) && !stretched)
        on = 0;
    if (!on || gs->scan_type == 2)
        return TTFX_DROP_NONE;
    return gs->scan_type == 1 ? TTFX_DROP_NOSTUBS : TTFX_DROP_SIMPLE;
}

int ttfx_LoadOutline(const ttfx_font *f, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy, ttfx_outline *o)
{
    ttfx__bcsize TTFX_FAR *h = (ttfx__bcsize TTFX_FAR *)f->bc;
    ttfx__bc bc;
    bc_loader L;
    ttfx_u8 TTFX_FAR *w;
    ttfx_u32 bytes;
    ttfx_point pp[2];
    ttfx_i32 shift;
    unsigned i;
    o->mem = 0;
    o->n = o->nc = 0;
    if (!h || !(f->hint & TTFX_HINT_BYTECODE) || !h->ok || h->sx != sx || h->sy != sy)
        return 0;
    bc_attach(&bc, f, h);
    bytes = 4UL * h->maxstack + 4UL * h->ncvt + 4UL * h->nstor + (3 * sizeof(ttfx_point) + 1UL) * h->ntw +
            (3 * sizeof(ttfx_point) + 1UL) * h->maxpts + 2UL * h->maxcont;
    if (bytes > 65000UL)
        return 0;
    w = (ttfx_u8 TTFX_FAR *)ttfx_alloc(f->alloc, bytes);
    if (!w)
        return 0;
    /* the glyph's own copies of the CVT, the storage and the twilight
       zone (each glyph starts from what prep left) */
    L.cur = (ttfx_point TTFX_FAR *)w;
    L.org = L.cur + h->maxpts;
    L.ous = L.org + h->maxpts;
    bc.stack = (ttfx_i32 TTFX_FAR *)(L.ous + h->maxpts);
    bc.cvt = bc.stack + h->maxstack;
    bc_copy(bc.cvt, (ttfx_u8 TTFX_FAR *)h + h->o_cvt, 4UL * h->ncvt);
    bc.stor = bc.cvt + h->ncvt;
    bc_copy(bc.stor, (ttfx_u8 TTFX_FAR *)h + h->o_stor, 4UL * h->nstor);
    bc.z[0].cur = (ttfx_point TTFX_FAR *)(bc.stor + h->nstor);
    bc_copy(bc.z[0].cur, (ttfx_u8 TTFX_FAR *)h + h->o_tw, (3 * sizeof(ttfx_point) + 1UL) * h->ntw);
    bc.z[0].org = bc.z[0].cur + h->ntw;
    bc.z[0].ous = bc.z[0].org + h->ntw;
    bc.z[0].flags = (ttfx_u8 TTFX_FAR *)(bc.z[0].ous + h->ntw);
    L.fl = bc.z[0].flags + h->ntw;
    L.ends = (ttfx_u16 TTFX_FAR *)(L.fl + h->maxpts);
    L.f = f;
    L.bc = &bc;
    L.n = L.nc = 0;
    L.maxn = h->maxpts;
    L.maxc = h->maxcont;
    L.ran = 0;
    if (!bc_glyph(&L, glyph, 0, pp)) {
        ttfx_free(f->alloc, w);
        return 0;
    }
    /* to bitmap coordinates: y down, the origin at the first phantom
       point */
    shift = pp[0].x;
    for (i = 0; i < L.n; i++) {
        L.cur[i].x -= shift;
        L.cur[i].y = -L.cur[i].y;
        L.fl[i] &= BC_ON;
    }
    o->p = L.cur;
    o->on = L.fl;
    o->ends = L.ends;
    o->n = L.n;
    o->nc = L.nc;
    o->adv = pp[1].x - pp[0].x;
    o->dropout = ttfx__bcDropout(L.ran ? &L.last : &h->gs, h->ppemy, h->ppemx != h->ppemy);
    o->mem = w;
    return 1;
}

void ttfx_FreeOutline(const ttfx_font *f, ttfx_outline *o)
{
    if (o->mem)
        ttfx_free(f->alloc, o->mem);
    o->mem = 0;
}
