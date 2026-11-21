/*
 * tk67.c - the WIN87EM audit probe: what Windows 3.1's WIN87EM does in
 * the cases the audit of w16's (win87em/) found wrong or unknown, with a
 * coprocessor and with the emulator (tests/tk6ref.sh). Inline assembly in
 * a file built with -fpi, so every instruction carries an emulator fixup.
 * Built in parts (TK67_P, one program each, TK67P1-TK67P5), since the
 * 3.1 emulator has ended programs part way before (TK8):
 *   1  FSTENV/FLDENV and FSAVE/FRSTOR with values on the stack, and with
 *      images whose TOP is not 0; FIST/FISTP with invalid unmasked; the
 *      sign of a NaN through FSUB/FSUBR, quiet NaNs in the other
 *      operations; FLD of a signaling NaN; the save area with more than
 *      eight values on the stack
 *   2, 6-9  __fpMath 7 and 6 with an unmasked exception, one case each
 *      (the 3.1 emulator's program ended after the first): the handler's
 *      calls (it notes each in the log at once) and the DS it is given
 *   3  FYL2X with zero operands and with unnormals; FYL2XP1 of tiny
 *      values; FPATAN's special cases
 *   4, 10, 11  two tasks (with TK67H.EXE): what one task's __fpMath 0,
 *      3, 4 and 2 do to another's state, the deep stack after the other
 *      ended, and whose handler an exception reaches (4: the other task
 *      ended with __fpMath 2; 10: while it runs; 11: it ended without 2)
 *   5  WIN87EM's entries and an emulated instruction with DF set
 * Observations are named "87.*" or "em.*" by configuration.
 */
#include <windows.h>

/* each part uses only some of the helpers and variables below */
#pragma disable_message(202)

#define TK_REF "tk67ref.h"
#include "tklog.h"
#include "tk67.h"

#define STEP(s) out("-- " s "\r\n")

extern void FAR PASCAL FPMATH(void);
extern WORD FAR PASCAL FPSAVE(void FAR *buf, WORD n);
extern WORD FAR PASCAL FPRESTORE(void FAR *buf, WORD n);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);
extern void __cdecl fp_handler(void);
extern WORD __cdecl h_regs[12];
extern WORD __cdecl h_stack[8];
extern WORD __cdecl h_calls;
extern WORD __cdecl std_fpm(WORD bx, WORD ax);
extern WORD __cdecl std_save(void FAR *buf, WORD n);
extern WORD __cdecl std_ax;
extern void __cdecl lhandler(void);
extern WORD __cdecl lh_ax, lh_ds, lh_calls, lh_regds, lh_ss;
extern WORD __cdecl lh_stk[8];
extern WORD FAR PASCAL FPINFO(void FAR *buf, WORD n);

static int have87;
static WORD cwv, swv, flv;
static double qa, qb;
static short v16;
static long v32;
static long v64[2];
static BYTE t80[10];
static WORD env[7];
static WORD img[47];
static WORD rr[10];

static void fpm(WORD bx, WORD ax, WORD dx)
{
    WORD i;
    for (i = 0; i < 10; i++)
        rr[i] = 0;
    rr[0] = ax;
    rr[1] = bx;
    rr[3] = dx;
    regcall((FARPROC)FPMATH, rr);
}

/* "87." / "em." + prefix + "." + n [+ "." + suffix] */
static void ob(const char *prefix, WORD n, const char *suffix, WORD v, int env_)
{
    char b[48];
    WORD k = 0;
    const char *p = have87 ? "87." : "em.";
    while (*p)
        b[k++] = *p++;
    while (*prefix)
        b[k++] = *prefix++;
    b[k++] = '.';
    if (n >= 10)
        b[k++] = (char)('0' + n / 10);
    b[k++] = (char)('0' + n % 10);
    if (suffix) {
        b[k++] = '.';
        while (*suffix)
            b[k++] = *suffix++;
    }
    b[k] = 0;
    if (env_)
        obs_env(b, v);
    else
        obs(b, v);
}

static WORD depth(void)
{
    fpm(10, 0, 0);
    return rr[0];
}

static void setcw(WORD cw)
{
    cwv = cw;
    _asm {
        fldcw cwv
        fclex
    }
    h_calls = 0;
}

/* the handler's calls (count << 8 | AL of the last), and whether the DS
   it found on its stack (after the return address) was ours */
static void rep_h(const char *op, WORD i)
{
    ob(op, i, "h", h_calls ? (h_calls << 8) | (h_regs[0] & 0xFF) : 0, 0);
    if (h_calls)
        ob(op, i, "hds", h_stack[2] == entry_regs[E_DS], 0);
}

static void rep_sw(const char *op, WORD i)
{
    ob(op, i, "s", swv & 0x003F, 0);
    rep_h(op, i);
}

static void rep_t80(const char *op, WORD i, int env_)
{
    static const char *const n[] = { "0", "1", "2", "3", "4" };
    WORD j;
    for (j = 0; j < 5; j++)
        ob(op, i, n[j], ((WORD *)t80)[j], env_);
}

#define N(a) (sizeof a / sizeof a[0])

WORD get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax]

/* WIN87EM's data and code selectors (__Win87EmInfo) */
static WORD w87ds, w87cs;

/* what a selector is: 1 ours (DS), 2 our SS, 3 WIN87EM's DS, 4 its code,
   5 our code, 0 none of these */
static WORD what_sel(WORD s)
{
    return s == entry_regs[E_DS] ? 1 : s == entry_regs[10] ? 2 : s == w87ds ? 3 :
           s == w87cs ? 4 : s == get_cs() ? 5 : 0;
}

/* lhandler (tk67s.asm) was called. It may run on another stack (IRQ 13),
   so nothing here is on the stack: the line is made in a static buffer. */
static char lh_line[160];
static WORD lh_len;

static void lh_put(const char *s)
{
    while (*s)
        lh_line[lh_len++] = *s++;
}

static void lh_hex(WORD v)
{
    static WORD v_, d_;
    v_ = v;
    for (d_ = 0; d_ < 4; d_++)
        lh_line[lh_len++] = "0123456789ABCDEF"[(v_ >> (12 - 4 * d_)) & 15];
}

void __cdecl __far lh_note(void)
{
    static WORD i;
    lh_len = 0;
    lh_put("-- handler: al=");
    lh_hex(lh_ax & 0xFF);
    lh_put(" ds ours=");
    lh_hex(lh_ds == entry_regs[E_DS]);
    lh_put(" ds reg=");
    lh_hex(what_sel(lh_regds));
    lh_put(" ss=");
    lh_hex(what_sel(lh_ss));
    lh_put(" stack:");
    for (i = 0; i < 8; i++) {
        lh_put(" ");
        lh_hex(lh_stk[i]);
        lh_put("/");
        lh_hex(what_sel(lh_stk[i]));
    }
    lh_put("\r\n");
    log_write(lh_line, lh_len);
}

static const double vals[] = { 1.0, 2.0, 3.0 };

/* push vals[0..n-1] */
static void push_vals(WORD n)
{
    WORD k;
    for (k = 0; k < n; k++) {
        qa = vals[k];
        _asm {
            fld qa
        }
    }
}

/* pop up to n values (no more than the depth says, so as not to pop an
   empty stack: the 3.1 emulator's program ended after that, TK8); how
   many came back as pushed, in order */
static WORD pop_vals(WORD n)
{
    WORD k, good = 0, d = depth();
    if (d < n)
        n = d;
    for (k = 0; k < n; k++) {
        _asm {
            fstp qb
        }
        if (qb == vals[n - 1 - k] && good == k)
            good++;
    }
    return good;
}

#if TK67_P == 1

/* ---- FSTENV / FLDENV, FSAVE / FRSTOR ---- */

static void envs(void)
{
    WORD n, i, d;
    STEP("fstenv");
    for (n = 1; n <= 3; n++) {
        fpm(0, 0, 0);
        setcw(0x037F);
        push_vals(n);
        _asm {
            fstenv env
        }
        /* DOSBox-X's coprocessor reads control word bit 6 as 1 (TK6) */
        ob("fstenv", n, "cw", env[0] & ~0x40, 0);
        ob("fstenv", n, "sw", env[1], 0);
        ob("fstenv", n, "tw", env[2], 0);
        for (i = 3; i < 7; i++)         /* addresses: shown */
            obs_env(have87 ? "87.fstenv.ip" : "em.fstenv.ip", env[i]);
        _asm {
            fldenv env
            fstsw swv
        }
        ob("fldenv", n, "depth", depth(), 0);
        ob("fldenv", n, "good", pop_vals(n), 0);
        rep_sw("fldenv", n);
        fpm(0, 0, 0);
    }
    STEP("fsave");
    for (n = 1; n <= 3; n++) {
        fpm(0, 0, 0);
        setcw(0x037F);
        push_vals(n);
        for (i = 0; i < N(img); i++)
            img[i] = 0xCCCC;
        _asm {
            fsave img
        }
        ob("fsave", n, "cw", img[0] & ~0x40, 0);
        ob("fsave", n, "sw", img[1], 0);
        ob("fsave", n, "tw", img[2], 0);
        /* the registers in use (the others hold what was there before) */
        for (i = 0; i < 5 * n; i++)
            obsn(have87 ? "87.fsave.reg" : "em.fsave.reg", n * 0x100 + i, img[7 + i]);
        _asm {
            fstcw cwv
            fstsw swv
        }
        ob("fsave", n, "cwafter", cwv & ~0x40, 0);
        ob("fsave", n, "swafter", swv, 0);
        ob("fsave", n, "depth", depth(), 0);
        _asm {
            frstor img
            fstsw swv
        }
        ob("frstor", n, "depth", depth(), 0);
        ob("frstor", n, "good", pop_vals(n), 0);
        rep_sw("frstor", n);
        fpm(0, 0, 0);
    }
    /* images made here: from an empty stack, an environment whose tag word
       says ST(0) (bits 0-1, the logical order) or physical register 7
       (bits 14-15) is in use, TOP 0; then one with TOP 6 and physical
       registers 6 and 7 in use, as a coprocessor would have it */
    STEP("fldenv.made");
    {
        static const WORD made[3][3] = {
            { 0x037F, 0x0000, 0xFFFC },
            { 0x037F, 0x0000, 0x3FFF },
            { 0x037F, 0x3000, 0x0FFF },
        };
        for (n = 0; n < 3; n++) {
            fpm(0, 0, 0);
            setcw(0x037F);
            for (i = 0; i < 7; i++)
                env[i] = i < 3 ? made[n][i] : 0;
            _asm {
                fldenv env
                fstsw swv
            }
            ob("made", n, "depth", depth(), 0);
            ob("made", n, "sw", swv, 0);
            rep_h("made", n);
            fpm(0, 0, 0);
        }
    }
    /* an image of two values with TOP changed to 5 */
    STEP("fldenv.top5");
    fpm(0, 0, 0);
    setcw(0x037F);
    push_vals(2);
    _asm {
        fstenv env
    }
    env[1] = (env[1] & ~0x3800) | 0x2800;
    _asm {
        fldenv env
        fstsw swv
    }
    d = depth();
    ob("top5", 0, "depth", d, 0);
    ob("top5", 0, "sw", swv, 0);
    ob("top5", 0, "good", pop_vals(2), 0);
    rep_h("top5", 0);
    fpm(0, 0, 0);
    STEP("frstor.top5");
    setcw(0x037F);
    push_vals(2);
    _asm {
        fsave img
    }
    img[1] = (img[1] & ~0x3800) | 0x2800;
    _asm {
        frstor img
        fstsw swv
    }
    ob("rtop5", 0, "depth", depth(), 0);
    ob("rtop5", 0, "sw", swv, 0);
    ob("rtop5", 0, "good", pop_vals(2), 0);
    rep_h("rtop5", 0);
    fpm(0, 0, 0);
}

/* ---- FIST / FISTP with invalid unmasked ---- */

static void fists(void)
{
    STEP("fist");
    fpm(0, 0, 0);
    setcw(0x037E);                      /* invalid unmasked */
    /* 0: out of range, FISTP word */
    qa = 1e20;
    v16 = 0x5A5A;
    _asm {
        fld qa
        fistp v16
        fstsw swv
    }
    ob("fist", 0, "mem", v16, 0);
    ob("fist", 0, "depth", depth(), 0);
    rep_sw("fist", 0);
    /* 1: then a valid FISTP word, the flag not cleared */
    h_calls = 0;
    qa = 5.0;
    v16 = 0x5A5A;
    _asm {
        fld qa
        fistp v16
        fstsw swv
    }
    ob("fist", 1, "mem", v16, 0);
    ob("fist", 1, "depth", depth(), 0);
    rep_sw("fist", 1);
    fpm(0, 0, 0);
    /* 2: FIST dword out of range (no pop), 3: a valid FISTP dword after */
    setcw(0x037E);
    qa = 5e9;
    v32 = 0x5A5A5A5AL;
    _asm {
        fld qa
        fist v32
        fstsw swv
    }
    ob("fist", 2, "mem", (WORD)v32, 0);
    ob("fist", 2, "memhi", (WORD)(v32 >> 16), 0);
    ob("fist", 2, "depth", depth(), 0);
    rep_sw("fist", 2);
    fpm(0, 0, 0);
    setcw(0x037E);
    qa = 5e9;
    _asm {
        fld qa
        fistp v32
    }
    h_calls = 0;
    qa = 7.0;
    v32 = 0x5A5A5A5AL;
    _asm {
        fld qa
        fistp v32
        fstsw swv
    }
    ob("fist", 3, "mem", (WORD)v32, 0);
    ob("fist", 3, "depth", depth(), 0);
    rep_sw("fist", 3);
    fpm(0, 0, 0);
    /* 4: FISTP qword out of range, 5: a valid one after */
    setcw(0x037E);
    qa = 1e30;
    v64[0] = v64[1] = 0x5A5A5A5AL;
    _asm {
        fld qa
        fistp qword ptr v64
        fstsw swv
    }
    ob("fist", 4, "mem", (WORD)v64[0], 0);
    ob("fist", 4, "memhi", (WORD)(v64[1] >> 16), 0);
    ob("fist", 4, "depth", depth(), 0);
    rep_sw("fist", 4);
    h_calls = 0;
    qa = 9.0;
    v64[0] = v64[1] = 0x5A5A5A5AL;
    _asm {
        fld qa
        fistp qword ptr v64
        fstsw swv
    }
    ob("fist", 5, "mem", (WORD)v64[0], 0);
    ob("fist", 5, "memhi", (WORD)(v64[1] >> 16), 0);
    ob("fist", 5, "depth", depth(), 0);
    rep_sw("fist", 5);
    fpm(0, 0, 0);
}

/* ---- NaNs ---- */

static const BYTE qn[8] = { 1, 0, 0, 0, 0, 0, 0xF8, 0x7F };     /* +QNaN */
static const BYTE nqn[8] = { 1, 0, 0, 0, 0, 0, 0xF8, 0xFF };    /* -QNaN */
static const BYTE sn32[4] = { 1, 0, 0x80, 0x7F };               /* SNaN */
static const BYTE sn64[8] = { 1, 0, 0, 0, 0, 0, 0xF0, 0x7F };
static const double one = 1.0;

static void nans(void)
{
    WORD i;
    STEP("nan");
    for (i = 0; i < 7; i++) {
        fpm(0, 0, 0);
        setcw(0x037F);
        switch (i) {
        case 0:                         /* 1 - NaN */
            _asm {
                fld1
                fsub qword ptr qn
            }
            break;
        case 1:                         /* NaN - 1 (FSUBR) */
            _asm {
                fld1
                fsubr qword ptr qn
            }
            break;
        case 2:                         /* NaN - 1 */
            _asm {
                fld qword ptr qn
                fsub one
            }
            break;
        case 3:                         /* 1 - NaN (FSUBR) */
            _asm {
                fld qword ptr qn
                fsubr one
            }
            break;
        case 4:                         /* 1 - (-NaN) */
            _asm {
                fld1
                fsub qword ptr nqn
            }
            break;
        case 5:                         /* -NaN loaded */
            _asm {
                fld qword ptr nqn
            }
            break;
        case 6:                         /* 1 - NaN, registers */
            _asm {
                fld1
                fld qword ptr qn
                fsubp st(1), st
            }
            break;
        }
        _asm {
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_t80("nan", i, 0);
        rep_sw("nan", i);
    }
    /* a quiet NaN in the other operations */
    STEP("qnan");
    for (i = 0; i < 10; i++) {
        fpm(0, 0, 0);
        setcw(0x037F);
        switch (i) {
        case 0:
            _asm {
                fld1
                fadd qword ptr qn
            }
            break;
        case 1:
            _asm {
                fld1
                fmul qword ptr qn
            }
            break;
        case 2:
            _asm {
                fld1
                fdiv qword ptr qn
            }
            break;
        case 3:
            _asm {
                fld qword ptr qn
                fsqrt
            }
            break;
        case 4:
            _asm {
                fld qword ptr qn
                frndint
            }
            break;
        case 5:
            _asm {
                fld qword ptr qn
                fst qb
            }
            break;
        case 6:
            _asm {
                fld qword ptr qn
                fst v32
            }
            break;
        case 7:
            _asm {
                fld1
                fcom qword ptr qn
            }
            break;
        case 8:
            _asm {
                fld qword ptr qn
                fld1
                fadd st, st(1)
            }
            break;
        case 9:
            _asm {
                fld qword ptr qn
                fchs
            }
            break;
        }
        _asm {
            fstsw swv
            fstp tbyte ptr t80
        }
        rep_t80("qnan", i, 0);
        rep_sw("qnan", i);
    }
    STEP("snan");
    for (i = 0; i < 4; i++) {
        fpm(0, 0, 0);
        setcw(i < 2 ? 0x037F : 0x037E);
        if (i & 1) {
            _asm {
                fld qword ptr sn64
                fstsw swv
            }
        } else {
            _asm {
                fld dword ptr sn32
                fstsw swv
            }
        }
        ob("snan", i, "depth", depth(), 0);
        rep_sw("snan", i);
        if (i < 2) {
            _asm {
                fstp tbyte ptr t80
            }
            rep_t80("snan", i, 0);
        }
        fpm(0, 0, 0);
    }
}

/* ---- the save area with more than eight values (emulator) ---- */

static BYTE area[0x1D0];

static void deep_save(void)
{
    WORD k, good, d;
    static short v;
    if (have87)
        return;                         /* nine values do not fit */
    STEP("deepsave");
    /* 0: twelve pushed, saved, all popped, restored */
    fpm(0, 0, 0);
    setcw(0x037F);
    for (k = 1; k <= 12; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    ob("dsave", 0, "ret", FPSAVE(area, sizeof area), 0);
    ob("dsave", 0, "depthsaved", depth(), 0);
    for (k = 12, good = 0; k >= 1; k--) {
        _asm {
            fistp v
        }
        if (v == (short)k && good == 12 - k)
            good++;
    }
    ob("dsave", 0, "goodbefore", good, 0);
    ob("dsave", 0, "rret", FPRESTORE(area, sizeof area), 0);
    d = depth();
    ob("dsave", 0, "depth", d, 0);
    for (k = 12, good = 0; d && k >= 1; k--, d--) {
        _asm {
            fistp v
        }
        if (v == (short)k && good == 12 - k)
            good++;
    }
    ob("dsave", 0, "good", good, 0);
    ob("dsave", 0, "depthafter", depth(), 0);
    rep_h("dsave", 0);
    /* 1: three pushed and saved, nine more pushed, restored */
    fpm(0, 0, 0);
    setcw(0x037F);
    for (k = 1; k <= 3; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    FPSAVE(area, sizeof area);
    for (k = 4; k <= 12; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    FPRESTORE(area, sizeof area);
    d = depth();
    ob("dsave", 1, "depth", d, 0);
    for (k = 3, good = 0; d && k >= 1; k--, d--) {
        _asm {
            fistp v
        }
        if (v == (short)k && good == 3 - k)
            good++;
    }
    ob("dsave", 1, "good", good, 0);
    ob("dsave", 1, "depthafter", depth(), 0);
    rep_h("dsave", 1);
    /* 2: twelve saved (1-12), all popped, twelve others pushed (101-112)
       and popped, restored: what the values below ST(7) are now */
    fpm(0, 0, 0);
    setcw(0x037F);
    for (k = 1; k <= 12; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    FPSAVE(area, sizeof area);
    for (k = 1; k <= 12; k++) {
        _asm {
            fistp v
        }
    }
    for (k = 101; k <= 112; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    for (k = 1; k <= 12; k++) {
        _asm {
            fistp v
        }
    }
    FPRESTORE(area, sizeof area);
    d = depth();
    ob("dsave", 2, "depth", d, 0);
    for (k = 1; d && k <= 12; k++, d--) {
        _asm {
            fistp v
        }
        if (k >= 8)
            obsn(have87 ? "87.dsave.2.pop" : "em.dsave.2.pop", k, v);
    }
    rep_h("dsave", 2);
    fpm(0, 0, 0);
}

static void part(void)
{
    envs();
    fists();
    nans();
    deep_save();
}

#elif TK67_P == 2 || (TK67_P >= 6 && TK67_P <= 9)

/* ---- __fpMath 6 and 7 with an unmasked exception (lhandler) ---- */

static const BYTE qn[8] = { 1, 0, 0, 0, 0, 0, 0xF8, 0x7F };
static const WORD snan80[5] = { 1, 0, 0, 0x8000, 0x7FFF };

static void rep_lh(const char *op, WORD i)
{
    ob(op, i, "h", lh_calls ? (lh_calls << 8) | (lh_ax & 0xFF) : 0, 0);
    if (lh_calls)
        ob(op, i, "hds", lh_ds == entry_regs[E_DS], 0);
}

static void fpm7(const char *op, WORD i)
{
    lh_calls = 0;
    fpm(7, 0, 0);
    ob(op, i, "ax", rr[0], 0);
    ob(op, i, "dx", rr[3], 0);
    rep_lh(op, i);
}

static void part(void)
{
    fpm(0, 0, 0);
#if TK67_P == 2
    /* 0: out of range with the control word of __fpMath 0 (1332h: invalid
       unmasked) */
    STEP("fpmath7.range");
    qa = 5e9;
    _asm {
        fld qa
    }
    fpm7("f7", 0);
    ob("f7", 0, "depth", depth(), 0);
#elif TK67_P == 6
    /* 1: a NaN */
    STEP("fpmath7.nan");
    _asm {
        fld qword ptr qn
    }
    fpm7("f7", 1);
    ob("f7", 1, "depth", depth(), 0);
#elif TK67_P == 7
    /* 2: precision unmasked, 2.5 rounded by 6, then 7 twice (2.0, 3.0) */
    STEP("fpmath6.pe");
    fpm(4, 0x1312, 0);
    qa = 3.0;
    qb = 2.5;
    _asm {
        fld qa
        fld qb
    }
    lh_calls = 0;
    fpm(6, 0, 0);
    ob("f6", 2, "ax", rr[0], 0);
    rep_lh("f6", 2);
    fpm7("f6.7a", 2);
    fpm7("f6.7b", 2);
    ob("f6", 2, "depth", depth(), 0);
#elif TK67_P == 8
    /* 3: a signaling NaN rounded by 6, then 7 twice (the NaN, then 3.0) */
    STEP("fpmath6.snan");
    qa = 3.0;
    _asm {
        fld qa
        fld tbyte ptr snan80
    }
    lh_calls = 0;
    fpm(6, 0, 0);
    ob("f6", 3, "ax", rr[0], 0);
    rep_lh("f6", 3);
    fpm7("f6.7a", 3);
    fpm7("f6.7b", 3);
    ob("f6", 3, "depth", depth(), 0);
#else
    /* 4: 7 on an empty stack */
    STEP("fpmath7.empty");
    fpm7("f7", 4);
    ob("f7", 4, "depth", depth(), 0);
#endif
    fpm(0, 0, 0);
}

#elif TK67_P == 3

/* ---- FYL2X with zeros and unnormals, FYL2XP1 of tiny values, FPATAN's
   special cases ---- */

static const WORD un[2][5] = {
    { 0, 0, 0, 0x4000, 0x3FFF },        /* 0.5, unnormal */
    { 0, 0, 0, 0x6000, 0x3FFF },        /* 0.75, unnormal */
};
/* extended values, by number: +0, -0, +inf, -inf, 1, -1, 2^16000, the
   smallest denormal 2^-16445, the smallest normal 2^-16382, 2^-16000, 2 */
static const WORD xv[11][5] = {
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0x8000 },
    { 0, 0, 0, 0x8000, 0x7FFF }, { 0, 0, 0, 0x8000, 0xFFFF },
    { 0, 0, 0, 0x8000, 0x3FFF }, { 0, 0, 0, 0x8000, 0xBFFF },
    { 0, 0, 0, 0x8000, 0x7E7F }, { 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0x8000, 0x0001 }, { 0, 0, 0, 0x8000, 0x017F },
    { 0, 0, 0, 0x8000, 0x4000 },
};
static WORD ya[5], xa[5];

/* ST(1) = value number y, ST(0) = number x */
static void load_yx(WORD y, WORD x)
{
    WORD j;
    for (j = 0; j < 5; j++) {
        ya[j] = xv[y][j];
        xa[j] = xv[x][j];
    }
    _asm {
        fld tbyte ptr ya
        fld tbyte ptr xa
    }
}

static void part(void)
{
    WORD i;
    STEP("fyl2x.zero");
    {
        /* y, x (value numbers); 4, 5 with invalid and zero divide unmasked */
        static const BYTE c[9][2] = {
            { 0, 0 }, { 1, 0 }, { 4, 0 }, { 4, 1 }, { 0, 0 }, { 4, 0 },
            { 10, 0 }, { 5, 0 }, { 6, 0 },
        };
        for (i = 0; i < 9; i++) {
            fpm(0, 0, 0);
            setcw(i == 4 || i == 5 ? 0x037A : 0x037F);
            load_yx(c[i][0], c[i][1]);
            _asm {
                fyl2x
                fstsw swv
                fstp tbyte ptr t80
            }
            rep_t80("l2z", i, 0);
            rep_sw("l2z", i);
        }
    }
    STEP("fyl2x.unnormal");
    for (i = 0; i < 2; i++) {
        WORD j;
        for (j = 0; j < 5; j++)
            ((WORD *)t80)[j] = un[i][j];
        fpm(0, 0, 0);
        setcw(0x037F);
        _asm {
            fld1
            fld tbyte ptr t80
            fyl2x
            fstsw swv
            fstp tbyte ptr t80
        }
        rep_t80("l2u", i, 1);
        rep_sw("l2u", i);
    }
    /* FYL2XP1 of tiny values: the exponent word and the flags are checked
       (the 3.1 emulator's last bits differ, TK8) */
    STEP("fyl2xp1.tiny");
    {
        static const BYTE c[4][2] = { { 6, 7 }, { 4, 7 }, { 4, 8 }, { 4, 9 } };
        for (i = 0; i < 4; i++) {
            fpm(0, 0, 0);
            setcw(0x037F);
            load_yx(c[i][0], c[i][1]);
            _asm {
                fyl2xp1
                fstsw swv
                fstp tbyte ptr t80
            }
            ob("l1d", i, "e", ((WORD *)t80)[4], 0);
            ob("l1d", i, "m", ((WORD *)t80)[3], 1);
            rep_sw("l1d", i);
        }
    }
    /* FPATAN of the special operands: the sign and exponent word, the top
       word and the flags are checked */
    STEP("fpatan.special");
    {
        /* y, x (value numbers) */
        static const BYTE c[18][2] = {
            { 0, 1 }, { 1, 1 }, { 2, 2 }, { 3, 2 }, { 2, 3 }, { 3, 3 },
            { 0, 0 }, { 1, 0 }, { 0, 4 }, { 0, 5 }, { 1, 5 }, { 4, 0 },
            { 4, 1 }, { 2, 4 }, { 4, 2 }, { 4, 3 }, { 5, 3 }, { 2, 5 },
        };
        for (i = 0; i < 18; i++) {
            fpm(0, 0, 0);
            setcw(0x037F);
            load_yx(c[i][0], c[i][1]);
            _asm {
                fpatan
                fstsw swv
                fstp tbyte ptr t80
            }
            ob("atn", i, "e", ((WORD *)t80)[4], 0);
            ob("atn", i, "m", ((WORD *)t80)[3], 0);
            ob("atn", i, "l", ((WORD *)t80)[0], 1);
            rep_sw("atn", i);
        }
    }
    fpm(0, 0, 0);
}

#elif TK67_P == 4 || TK67_P == 10 || TK67_P == 11

/* ---- two tasks ---- */

/* the observations' name, one per part */
#define TN (TK67_P == 4 ? "task" : TK67_P == 10 ? "taskb" : "taskc")

static tk67sh sh;

static void hex4(char *p, WORD v)
{
    WORD i;
    for (i = 0; i < 4; i++)
        p[i] = "0123456789ABCDEF"[(v >> (12 - 4 * i)) & 15];
}

/* the square root of -1 (reported with 88h masked or not, TK6): our
   handler's calls */
static void sqrt_neg(WORD i)
{
    h_calls = 0;
    _asm {
        fld1
        fchs
        fsqrt
        fstp st(0)
    }
    ob(TN, i, "h", h_calls ? (h_calls << 8) | (h_regs[0] & 0xFF) : 0, 0);
}

/* nine values pushed and popped: the deep stack (below ST(7)) after the
   other task's */
static void deep9(WORD k)
{
    WORD i, d, good;
    static short v;
    for (i = 1; i <= 9; i++) {
        v = (short)i;
        _asm {
            fild v
        }
    }
    ob(TN, k, "depth", depth(), 0);
    for (i = 9, good = 0, d = depth(); d && i >= 1; i--, d--) {
        _asm {
            fistp v
        }
        if (v == (short)i && good == 9 - i)
            good++;
    }
    ob(TN, k, "good", good, 0);
    fpm(0, 0, 0);
}

static void part(void)
{
    char cmd[160];
    WORD n, i;
    UINT t;
    /* TK67H's mode: 0 ends with __fpMath 2, 1 waits for us first, 2 ends
       without __fpMath 2 */
    WORD mode = TK67_P == 4 ? 0 : TK67_P == 10 ? 1 : 2;
    STEP("tasks");
    fpm(0, 0, 0);
    fpm(4, 0x137F, 0);                  /* every exception masked */
    if (mode == 0)
        push_vals(2);
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), cmd, sizeof cmd - 24);
    for (i = n; i && cmd[i - 1] != '\\'; i--)
        ;
    lstrcpy(cmd + i, "TK67H.EXE ");
    n = lstrlen(cmd);
    hex4(cmd + n, entry_regs[E_DS]);
    cmd[n + 4] = ' ';
    hex4(cmd + n + 5, (WORD)(void NEAR *)&sh);
    cmd[n + 9] = ' ';
    cmd[n + 10] = (char)('0' + mode);
    cmd[n + 11] = 0;
    t = WinExec(cmd, SW_SHOWNORMAL);
    ob(TN, 0, "execok", t > 32, 0);
    if (t <= 32)
        return;
    if (mode == 1) {
        /* while TK67H runs, with its own handler set */
        for (i = 0; i < 2000 && !sh.ready; i++)
            Yield();
        ob(TN, 3, "ready", sh.ready, 0);
        sqrt_neg(3);
        fpm(0, 0, 0);
        sh.go = 1;
    }
    for (i = 0; i < 2000 && !sh.done; i++)
        Yield();
    for (i = 0; i < 2000 && IsTask((HTASK)sh.task); i++)
        Yield();
    ob(TN, 0, "ran", sh.done, 0);
    ob(TN, 0, "ended", !IsTask((HTASK)sh.task), 0);
    /* what the other task saw */
    ob(TN, 0, "hcw0", sh.cw0, 0);
    ob(TN, 0, "hdepth0", sh.depth0, 0);
    ob(TN, 0, "hcw1", sh.cw1, 0);
    ob(TN, 0, "hdepth1", sh.depth1, 0);
    ob(TN, 0, "hgood", sh.good, 0);
    ob(TN, 0, "hdepth2", sh.depth2, 0);
    ob(TN, 0, "hcalls", sh.hcalls, 0);
    if (mode == 1) {
        ob(TN, 3, "hcalls1", sh.hcalls1, 0);
        ob(TN, 3, "hcalls2", sh.hcalls2, 0);
    }
    /* and what is left of ours */
    fpm(5, 0, 0);
    ob(TN, 0, "cw", rr[0], 0);
    _asm {
        fstcw cwv
    }
    ob(TN, 0, "fstcw", cwv & ~0x40, 0);
    ob(TN, 0, "depth", depth(), 0);
    if (mode == 0)
        ob(TN, 0, "good", pop_vals(2), 0);
    fpm(0, 0, 0);
    STEP("tasks.deep");
    deep9(1);
    /* last: whose handler an exception reaches now */
    STEP("tasks.handler");
    sqrt_neg(2);
    fpm(0, 0, 0);
}

#elif TK67_P == 5

/* ---- the direction flag ---- */

static BYTE area[0x1D0];

static void part(void)
{
    WORD f;
    STEP("df");
    fpm(0, 0, 0);
    fpm(4, 0x1272, 0);
    f = std_fpm(5, 0);
    ob("df", 0, "ax", std_ax, 0);
    ob("df", 0, "df", (f & 0x400) != 0, 0);
    f = std_fpm(10, 0);
    ob("df", 1, "ax", std_ax, 0);
    ob("df", 1, "df", (f & 0x400) != 0, 0);
    fpm(0, 0, 0);
    /* an emulated load and store with DF set */
    qa = -2.75;
    qb = 0;
    _asm {
        std
        fld qa
        fstp qb
        pushf
        pop flv
        cld
    }
    ob("df", 3, "ok", qb == -2.75, 0);
    ob("df", 3, "df", (flv & 0x400) != 0, 0);
    fpm(0, 0, 0);
    /* last: __Win87EmSave with DF set (the 3.1 program ended there) */
    fpm(4, 0x1272, 0);
    qa = 6.375;
    _asm {
        fld qa
    }
    f = std_save(area, sizeof area);
    ob("df", 2, "ax", std_ax, 0);
    ob("df", 2, "df", (f & 0x400) != 0, 0);
    ob("df", 2, "cw", *(WORD *)(area + 100), 0);
    FPRESTORE(area, sizeof area);
    ob("df", 2, "depth", depth(), 0);
    fpm(0, 0, 0);
}

#elif TK67_P == 12

/* ---- __fpMath 6's AX ---- */

static const WORD f6v[5][5] = {
    { 0, 0, 0, 0xC000, 0x7FFF },        /* QNaN */
    { 1, 0, 0, 0x8000, 0x7FFF },        /* SNaN */
    { 0, 0, 0, 0x8000, 0xFFFF },        /* -inf */
    { 0, 0, 0, 0xA000, 0x4000 },        /* 2.5 */
    { 0, 0, 0, 0, 0 },                  /* 0 */
};

static void part(void)
{
    static const WORD cws[3] = { 0x137F, 0x1332, 0x0F3F };
    WORD i, j, k;
    STEP("fpmath6.ax");
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 5; i++) {
            /* IE unmasked (1332h) only where nothing is invalid */
            if (k == 1 && i < 3)
                continue;
            fpm(0, 0, 0);
            fpm(4, cws[k], 0);
            for (j = 0; j < 5; j++)
                ((WORD *)t80)[j] = f6v[i][j];
            _asm {
                fld tbyte ptr t80
            }
            h_calls = 0;
            fpm(6, 0x0400, 0);
            ob("f6ax", k * 10 + i, "ax", rr[0], 0);
            rep_h("f6ax", k * 10 + i);
            ob("f6ax", k * 10 + i, "depth", depth(), 0);
        }
    }
    fpm(0, 0, 0);
}

#elif TK67_P == 13

/* ---- __fpMath 7 and FISTP with invalid unmasked on a coprocessor (the
   N4 case of the audit; only a real coprocessor raises the exception:
   tests/86box.sh) ---- */

static const BYTE qn[8] = { 1, 0, 0, 0, 0, 0, 0xF8, 0x7F };

/* the status word now, then an FWAIT'd instruction (where a pending
   exception is reported) and our handler's calls */
static void after(const char *op, WORD i)
{
    _asm {
        fnstsw swv
    }
    ob(op, i, "s", swv & 0x00FF, 0);
    ob(op, i, "depth", depth(), 0);
    _asm {
        fld1
        fstp st(0)
    }
    ob(op, i, "h", lh_calls ? (lh_calls << 8) | (lh_ax & 0xFF) : 0, 0);
    if (lh_calls)
        ob(op, i, "hds", lh_ds == entry_regs[E_DS], 0);
    lh_calls = 0;
}

static void part(void)
{
    WORD i;
    STEP("hw.fpmath7");
    for (i = 0; i < 3; i++) {
        fpm(0, 0, 0);                   /* the coprocessor gets 1330h */
        h_calls = 0;
        /* a valid value first: what DX:AX holds before */
        qa = 12345.0;
        _asm {
            fld qa
        }
        fpm(7, 0, 0);
        ob("h7", i, "ax0", rr[0], 0);
        ob("h7", i, "dx0", rr[3], 0);
        switch (i) {
        case 0:
            qa = 5e9;
            _asm {
                fld qa
            }
            break;
        case 1:
            _asm {
                fld qword ptr qn
            }
            break;
        case 2:
            qa = -7e9;
            _asm {
                fld qa
            }
            break;
        }
        fpm(7, 0, 0);
        ob("h7", i, "ax", rr[0], 0);
        ob("h7", i, "dx", rr[3], 0);
        after("h7", i);
        fpm(0, 0, 0);
    }
    STEP("hw.fistp");
    fpm(0, 0, 0);
    h_calls = 0;
    qa = 5e9;
    v32 = 0x5A5A5A5AL;
    _asm {
        fld qa
        fistp v32
    }
    ob("hfist", 0, "mem", (WORD)v32, 0);
    ob("hfist", 0, "memhi", (WORD)(v32 >> 16), 0);
    after("hfist", 0);
    fpm(0, 0, 0);
    /* 1/0 with zero divide unmasked (1330h): does IRQ 13 reach the
       handler (83h)? */
    STEP("hw.div0");
    h_calls = 0;
    _asm {
        fld1
        fldz
        fdivp st(1), st
    }
    after("hdiv", 0);
    fpm(0, 0, 0);
}

#endif

int __cdecl tmain(void)
{
#if TK67_P == 2 || (TK67_P >= 6 && TK67_P <= 9) || TK67_P == 13
    FARPROC h = (FARPROC)lhandler;
#else
    FARPROC h = (FARPROC)fp_handler;
#endif
    WORD info[6];
    out("TK67 start\r\n");
    have87 = (GetWinFlags() & WF_80x87) != 0;
    obs_env("WF_80x87", have87);
    FPINFO(info, sizeof info);
    w87ds = info[2];
    w87cs = info[3];
    fpm(0, 0, 0);
    fpm(3, (WORD)(DWORD)h, (WORD)((DWORD)h >> 16));
    part();
    out("TK67 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
