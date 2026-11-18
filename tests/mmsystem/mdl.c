/*
 * mdl.c - MMSYSLOG.DLL, the record keeper of the logging pass-through
 * device drivers MDS*.DRV (tests/mmsystem/mds.c; ARCHITECTURE.md 23.1).
 *
 * A shim driver stands where MMSYSTEM opens a wave, MIDI or auxiliary
 * driver (SYSTEM.INI [drivers] wave=mdsw1.drv, with the real driver's
 * file in [mmslog] MDSW1=sndblst2.drv), loads the real one as a library
 * and hands every call of its DriverProc and xxxMessage entries to MlCall
 * here, which records it (arguments, the bytes behind the ones that are
 * pointers, before and after) and makes the call. A scratch copy of the
 * real driver can also have its imports of MMSYSTEM renamed to MMSYSLOG
 * (tools/nerename.py; the names have the same length): this library then
 * stands between the driver and MMSYSTEM too, recording each
 * DriverCallback and each MIDI call a driver makes (MIDIMAP's) and
 * passing it on unchanged (mdlent.asm, a far jump with the stack as it
 * was). Black-box observation between Microsoft's modules
 * (ARCHITECTURE.md 3): only values that cross the interface are recorded.
 *
 * The records stay in this library's fixed data (callbacks come at
 * interrupt time); the probe MMP.EXE asks for them as text with MlDump,
 * after each step. Values the probe has named (MlName: its handles,
 * headers, callbacks) are printed by name; other pointers by the module
 * that owns their selector, so that the log does not depend on where the
 * heap put things.
 *
 * The driver messages' numbers and parameters are those Wine's mmddk.h
 * of before 2002 (X11 licence) gives; the records show what is actually
 * sent.
 */
#include <windows.h>
#include "mdl.h"

extern WORD __cdecl __far md_peek(const void __far *src, void __far *dst, WORD n);
extern WORD __cdecl __far md_valid(WORD sel);
extern WORD __cdecl __far md_cli(void);
extern void __cdecl __far md_sti(WORD fl);
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */

#define NREC 170
#define PK1 72
#define PK2 48
#define NSTR 3          /* strings kept behind an MCI parameter block */
#define LSTR (PK1 / NSTR)

#define RT_CALL 1       /* a call of a shim's entry */
#define RT_DCB  2       /* DriverCallback from a renamed driver */
#define RT_FWD  3       /* another MMSYSTEM function from a renamed driver */
#define RT_RET  4       /* an MCI driver helper from a renamed driver, with its result */

struct rec {
    BYTE type, shim, kind, depth;
    WORD seq, msg, dev, flags, retcs, task;
    DWORD id, user, p1, p2, r;
    BYTE n1, n2, nu, nf;
    BYTE b1[PK1], a1[PK1], b2[PK2], a2[PK2], bu[4], au[4], bf[18];
    BYTE ns, soff[NSTR];        /* MCI: the strings in b1 (before), a1 (after) */
};

static struct rec recs[NREC];
static WORD nrec, seq, lost, depth, quiet;
static char shims[8][10];
static WORD nshims;
static struct { DWORD v; char n[14]; } names[80];
static WORD nnames;
static WORD probess;
static HTASK probetask;
static char line[700];

extern WORD __cdecl md_ords[], md_nfwd;
extern FARPROC __cdecl md_tbl[];

/* where the MMSYSTEM functions mdlent.asm passes on are (MMSYSTEM is
   loaded before any driver it opens) */
int __cdecl libmain(void)
{
    HMODULE m = GetModuleHandle("MMSYSTEM");
    WORD i;
    for (i = 0; i < md_nfwd; i++)
        md_tbl[i] = m ? GetProcAddress(m, MAKEINTRESOURCE(md_ords[i])) : 0;
    return 1;
}

static struct rec *newrec(void)
{
    struct rec *t = 0;
    WORD fl = md_cli();
    if (quiet)
        ;
    else if (nrec < NREC) {
        t = &recs[nrec++];
        t->seq = ++seq;
    } else {
        ++seq;
        ++lost;
    }
    md_sti(fl);
    return t;
}

WORD FAR PASCAL __loadds MlRegister(LPCSTR shim)
{
    WORD i;
    for (i = 0; i < nshims; i++)
        if (!lstrcmpi(shims[i], shim))
            return i;
    if (nshims >= 8)
        return 7;
    lstrcpyn(shims[nshims], shim, sizeof shims[0]);
    return nshims++;
}

/* how many bytes behind each parameter to keep: s1 behind p1, s2 behind
   p2, su behind dwUser, sf behind the WAVEFORMAT a WAVEOPENDESC points to */
static void peeks(WORD kind, WORD msg, DWORD p2, WORD *s1, WORD *s2, WORD *su, WORD *sf)
{
    WORD caps = p2 > PK1 ? PK1 : (WORD)p2;
    *s1 = *s2 = *su = *sf = 0;
    switch (kind) {
    case MDK_DRV:
        if (msg == 3) {                 /* DRV_OPEN: the options */
            *s1 = 16;
            *s2 = 10;                   /* an MCI driver's MCI_OPEN_DRIVER_PARMS */
        } else if (msg >= 0x800)        /* MCI commands: the parameter block */
            *s2 = PK2;
        break;
    case MDK_WOD:
    case MDK_WID:
        if (kind == MDK_WID)
            msg = msg >= 50 ? msg - 47 : 0;     /* WIDM_x = WODM_x + 47 */
        switch (msg) {
        case 4: *s1 = caps; break;              /* GETDEVCAPS */
        case 5: *s1 = 14; *su = 4; *sf = 18; break;     /* OPEN */
        case 7: case 8: case 9: *s1 = 32; break;        /* PREPARE UNPREPARE WRITE/ADDBUFFER */
        case 13: *s1 = p2 < 12 ? (WORD)p2 : 12; break; /* GETPOS (WIDM 60): MMTIME */
        case 14: case 16: case 18: *s1 = 4; break;      /* GETPITCH GETVOLUME GETPLAYBACKRATE */
        }
        if (kind == MDK_WID && (msg == 10 || msg == 11 || msg == 12))
            *s1 = 0;                            /* START STOP RESET */
        break;
    case MDK_MOD:
        switch (msg) {
        case 2: *s1 = caps; break;
        case 3: *s1 = 10; *su = 4; break;
        case 5: case 6: case 8: *s1 = 28; break;
        case 10: *s1 = 4; break;
        case 12: case 13: *s1 = 32; break;      /* the patch arrays */
        }
        break;
    case MDK_MID:
        switch (msg) {
        case 54: *s1 = caps; break;
        case 55: *s1 = 10; *su = 4; break;
        case 57: case 58: case 59: *s1 = 28; break;
        }
        break;
    case MDK_AUX:
        if (msg == 4)
            *s1 = caps;
        else if (msg == 5)
            *s1 = 4;
        break;
    }
}

/* the strings an MCI parameter block points to (any DWORD from offset 4
   on that is a readable far pointer; DRV_OPEN's MCI_OPEN_DRIVER_PARMS
   has its one at 2; LDT selectors only), LSTR - 1 bytes of each at most, into dst; the first
   call (before) finds them, the second (after) reads the same ones */
static void mcistrings(struct rec *t, WORD msg, BYTE *dst, int find)
{
    WORD o, k, n;
    if (find) {
        t->ns = 0;
        for (o = msg == 3 ? 2 : 4; o + 4 <= t->n2 && t->ns < NSTR; o += msg == 3 ? 10 : 4) {
            DWORD v = *(DWORD *)(t->b2 + o);
            BYTE c;
            if ((HIWORD(v) & 4) && md_valid(HIWORD(v)) && md_peek((void __far *)v, &c, 1))
                t->soff[t->ns++] = (BYTE)o;
        }
    }
    for (k = 0; k < t->ns; k++) {
        BYTE *d = dst + k * LSTR;
        BYTE __far *v = *(BYTE __far **)(t->b2 + t->soff[k]);
        for (n = 0; n < LSTR - 1; n++)
            if (!md_peek(v + n, d + n, 1) || !d[n])
                break;
        d[n] = 0;
    }
}

DWORD FAR PASCAL __loadds MlCall(WORD shim, WORD kind, FARPROC real, WORD flags, WORD retcs,
                                 void FAR *args)
{
    struct rec *t;
    DWORD r = 0, user, p1, p2;
    WORD msg, dev, s1, s2, su, sf;
    struct mdmsgargs FAR *m = (struct mdmsgargs FAR *)args;
    struct mddrvargs FAR *d = (struct mddrvargs FAR *)args;
    if (kind == MDK_DRV) {
        msg = d->msg;
        dev = d->h;
        user = d->id;
        p1 = d->p1;
        p2 = d->p2;
    } else {
        msg = m->msg;
        dev = m->dev;
        user = m->user;
        p1 = m->p1;
        p2 = m->p2;
    }
    ++depth;
    t = newrec();
    peeks(kind, msg, p2, &s1, &s2, &su, &sf);
    if (t) {
        t->type = RT_CALL;
        t->shim = (BYTE)shim;
        t->kind = (BYTE)kind;
        t->depth = (BYTE)depth;
        t->msg = msg;
        t->dev = dev;
        t->user = user;
        t->id = 0;
        t->p1 = p1;
        t->p2 = p2;
        t->flags = flags;
        t->retcs = retcs;
        t->task = (flags & 0x200) ? (WORD)GetCurrentTask() : 0;
        t->n1 = (BYTE)md_peek((void __far *)p1, t->b1, s1);
        t->n2 = (BYTE)md_peek((void __far *)p2, t->b2, s2);
        t->nu = (BYTE)md_peek((void __far *)user, t->bu, su);
        t->nf = 0;
        if (sf && t->n1 >= 6)
            t->nf = (BYTE)md_peek(*(void __far * FAR *)(t->b1 + 2), t->bf, sf);
        t->ns = 0;
        if (kind == MDK_DRV && t->n2 && msg >= 0x800)
            mcistrings(t, msg, t->b1, 1);
        else if (kind == MDK_DRV && t->n2 && msg == 3)
            mcistrings(t, msg, t->a1, 1);       /* b1 holds DRV_OPEN's p1 */
    }
    if (real) {
        if (kind == MDK_DRV)
            r = ((MDDRVPROC)real)(d->id, d->h, d->msg, d->p1, d->p2);
        else
            r = ((MDPROC)real)(m->dev, m->msg, m->user, m->p1, m->p2);
    }
    if (t) {
        t->r = r;
        if (t->n1 && !(kind == MDK_DRV && msg == 3))     /* DRV_OPEN: a1 holds strings */
            md_peek((void __far *)p1, t->a1, t->n1);
        if (t->n2)
            md_peek((void __far *)p2, t->a2, t->n2);
        if (t->nu)
            md_peek((void __far *)user, t->au, t->nu);
        if (t->ns && msg != 3)
            mcistrings(t, msg, t->a1, 0);
    }
    --depth;
    return r;
}

/* DriverCallback(dwCallback, uFlags, hDevice, uMessage, dwUser, dw1, dw2)
   as a renamed driver calls it (mdlent.asm, before it jumps on) */
struct dcbargs { DWORD dw2, dw1, user; WORD msg, dev, flags; DWORD cb; };

void __cdecl __far md_dcb(WORD retcs, WORD fl, struct dcbargs __far *a)
{
    struct rec *t = newrec();
    if (!t)
        return;
    t->type = RT_DCB;
    t->ns = 0;
    t->shim = 0;
    t->kind = 0;
    t->depth = (BYTE)(depth + 1);
    t->msg = a->msg;
    t->dev = a->dev;
    t->flags = a->flags;
    t->id = a->cb;
    t->user = a->user;
    t->p1 = a->dw1;
    t->p2 = a->dw2;
    t->retcs = retcs;
    t->task = fl;                       /* the flags at the call */
    t->r = 0;
    /* WOM_DONE, WIM_DATA, MIM_LONGDATA, MIM_LONGERROR, MOM_DONE: a header */
    t->n1 = (BYTE)md_peek((void __far *)a->dw1,  t->b1,
                          a->msg == 0x3BD || a->msg == 0x3C0 ? 32 :
                          a->msg == 0x3C4 || a->msg == 0x3C6 || a->msg == 0x3C9 ? 28 : 0);
    t->n2 = t->nu = t->nf = 0;
}

/* another MMSYSTEM function called by a renamed driver: its ordinal and
   the first eight words of its arguments */
void __cdecl __far md_fwd(WORD ord, WORD retcs, WORD __far *a)
{
    struct rec *t = newrec();
    int i;
    if (!t)
        return;
    t->type = RT_FWD;
    t->ns = 0;
    t->msg = ord;
    t->retcs = retcs;
    t->depth = (BYTE)(depth + 1);
    for (i = 0; i < 8; i++)
        ((WORD *)t->b1)[i] = a[i];
    t->n1 = 16;
    t->n2 = t->nu = t->nf = 0;
}

/* an MCI driver helper called by a renamed driver: its ordinal, its
   arguments and its result (mdlent.asm) */
void __cdecl __far md_ret(WORD ord, WORD retcs, WORD __far *a, WORD nbytes, DWORD r)
{
    struct rec *t = newrec();
    WORD i;
    if (!t)
        return;
    t->type = RT_RET;
    t->msg = ord;
    t->retcs = retcs;
    t->depth = (BYTE)(depth + 1);
    t->r = r;
    for (i = 0; i < nbytes / 2 && i < 8; i++)
        ((WORD *)t->b1)[i] = a[i];
    t->n1 = (BYTE)(nbytes / 2);
    t->n2 = t->nu = t->nf = t->ns = 0;
    /* mciLoadCommandResource: its name (a resource name or a number) */
    if (ord == 705 && t->n1 == 4 && a[2] != 0) {
        BYTE __far *v = MAKELP(a[2], a[1]);
        for (i = 0; i < LSTR - 1; i++)
            if (!md_peek(v + i, t->a1 + i, 1) || !t->a1[i])
                break;
        t->a1[i] = 0;
        t->ns = 1;
    }
}

void FAR PASCAL __loadds MlName(DWORD value, LPCSTR name)
{
    WORD i;
    for (i = 0; i < nnames; i++)
        if (!lstrcmp(names[i].n, name)) {
            names[i].v = value;
            return;
        }
    if (nnames < sizeof names / sizeof names[0]) {
        names[nnames].v = value;
        lstrcpyn(names[nnames].n, name, sizeof names[0].n);
        nnames++;
    }
}

void FAR PASCAL __loadds MlProbe(WORD ss, HTASK task)
{
    probess = ss;
    probetask = task;
}

void FAR PASCAL __loadds MlQuiet(WORD on)
{
    quiet = on;
}

WORD FAR PASCAL __loadds MlCount(void)
{
    return nrec;
}

/* ---- the text ---- */

static const char *modname(WORD sel)
{
    static char b[4][12];
    static int k;
    char *r = b[k++ & 3];
    WORD m;
    if (!md_valid(sel))
        return 0;
    if (sel == probess)
        return "stack";
    m = K_GETEXEPTR(sel);
    if (!m)
        return "nomod";
    {
        BYTE FAR *ne = (BYTE FAR *)MAKELP(m, 0);
        BYTE FAR *rn = (BYTE FAR *)MAKELP(m, *(WORD FAR *)(ne + 0x26));
        int n = rn[0], i;
        if (n > 10)
            n = 10;
        for (i = 0; i < n; i++)
            r[i] = rn[1 + i];
        r[n] = 0;
    }
    return r;
}

/* a value: its name, or for a pointer the module owning its selector */
static char *val(char *p, DWORD v, int ptr)
{
    WORD i;
    const char *m;
    for (i = 0; i < nnames; i++)
        if (names[i].v == v)
            return p + wsprintf(p, "%s", (LPSTR)names[i].n);
    if (ptr && v) {
        m = modname(HIWORD(v));
        if (m)
            return p + wsprintf(p, "[%s]", (LPSTR)m);
    }
    return p + wsprintf(p, "%08lX", v);
}

static char *wval(char *p, WORD v)
{
    WORD i;
    for (i = 0; i < nnames; i++)
        if (names[i].v == (DWORD)v)
            return p + wsprintf(p, "%s", (LPSTR)names[i].n);
    return p + wsprintf(p, "%04X", v);
}

static char *hex(char *p, const BYTE *b, int n)
{
    static const char x[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < n; i++) {
        *p++ = x[b[i] >> 4];
        *p++ = x[b[i] & 15];
    }
    *p = 0;
    return p;
}

static int same(const BYTE *a, const BYTE *b, int n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

/* a WAVEHDR (8 dwords) or MIDIHDR (7) */
static char *hdr(char *p, const BYTE *b, int wave)
{
    const DWORD *d = (const DWORD *)b;
    p += wsprintf(p, "{data ");
    p = val(p, d[0], 1);
    p += wsprintf(p, " len %ld rec %ld user %08lX flags %04lX", d[1], d[2], d[3], d[4]);
    if (wave)
        p += wsprintf(p, " loops %ld", d[5]);
    p += wsprintf(p, " next ");
    p = val(p, d[wave ? 6 : 5], 1);
    p += wsprintf(p, " res ");
    p = val(p, d[wave ? 7 : 6], 1);
    *p++ = '}';
    *p = 0;
    return p;
}

static int ishdr(struct rec *t)
{
    WORD m = t->msg;
    if (t->type == RT_DCB)
        return m == 0x3BD || m == 0x3C0 ? 2 : (m == 0x3C4 || m == 0x3C6 || m == 0x3C9) ? 1 : 0;
    switch (t->kind) {
    case MDK_WOD: return m >= 7 && m <= 9 ? 2 : 0;
    case MDK_WID: return m >= 54 && m <= 56 ? 2 : 0;
    case MDK_MOD: return m == 5 || m == 6 || m == 8 ? 1 : 0;
    case MDK_MID: return m >= 57 && m <= 59 ? 1 : 0;
    }
    return 0;
}

static int isopen(struct rec *t)
{
    WORD m = t->msg;
    switch (t->kind) {
    case MDK_WOD: return m == 5 ? 2 : 0;
    case MDK_WID: return m == 52 ? 2 : 0;
    case MDK_MOD: return m == 3 ? 1 : 0;
    case MDK_MID: return m == 55 ? 1 : 0;
    }
    return 0;
}

static char *bytes(char *p, struct rec *t, const char *what, const BYTE *b, const BYTE *a, int n)
{
    int h = ishdr(t), o = isopen(t);
    p += wsprintf(p, " [%s ", (LPSTR)what);
    if (h && n >= 28 && what[1] == '1') {
        p = hdr(p, b, h == 2);
        if (a && !same(a, b, n)) {
            p += wsprintf(p, " -> ");
            p = hdr(p, a, h == 2);
        }
    } else if (o && what[1] == '1') {
        /* WAVEOPENDESC {hWave, lpFormat, dwCallback, dwInstance},
           MIDIOPENDESC {hMidi, dwCallback, dwInstance} */
        p += wsprintf(p, "h ");
        p = wval(p, *(const WORD *)b);
        if (o == 2) {
            p += wsprintf(p, " fmt ");
            p = val(p, *(const DWORD *)(b + 2), 1);
        }
        p += wsprintf(p, " cb ");
        p = val(p, *(const DWORD *)(b + (o == 2 ? 6 : 2)), 1);
        p += wsprintf(p, " inst ");
        p = val(p, *(const DWORD *)(b + (o == 2 ? 10 : 6)), 0);
    } else {
        p = hex(p, b, n);
        if (a && !same(a, b, n)) {
            p += wsprintf(p, " -> ");
            p = hex(p, a, n);
        }
    }
    *p++ = ']';
    *p = 0;
    return p;
}

/* an MCI parameter block (or DRV_OPEN's MCI_OPEN_DRIVER_PARMS) before
   and after: the bytes up to the last one either has that is not 0, the
   string pointers found shown as "pppppppp" (their values are the heap's) */
static char *hexm(char *p, struct rec *t, const BYTE *b, int n)
{
    static const char x[] = "0123456789ABCDEF";
    int i, k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < t->ns; k++)
            if (i >= t->soff[k] && i < t->soff[k] + 4)
                break;
        if (k < t->ns) {
            *p++ = 'p';
            *p++ = 'p';
        } else {
            *p++ = x[b[i] >> 4];
            *p++ = x[b[i] & 15];
        }
    }
    *p = 0;
    return p;
}

static char *mciblock(char *p, struct rec *t)
{
    int n = t->n2;
    while (n > 4 && !t->b2[n - 1] && !t->a2[n - 1])
        n--;
    n = (n + 3) & ~3;
    if (n > t->n2)
        n = t->n2;
    p += wsprintf(p, " [p2 ");
    p = hexm(p, t, t->b2, n);
    if (!same(t->a2, t->b2, n)) {
        p += wsprintf(p, " -> ");
        p = hexm(p, t, t->a2, n);
    }
    *p++ = ']';
    *p = 0;
    return p;
}

static LPSTR who(WORD cs)
{
    const char *m = modname(cs);
    return (LPSTR)(m ? m : "?");
}

static const char *const kinds[] = { "DriverProc", "wod", "wid", "mod", "mid", "aux" };

void FAR PASCAL __loadds MlDump(LPCSTR name)
{
    HFILE f = _lopen(name, OF_WRITE);
    WORD i;
    char *p;
    if (f == HFILE_ERROR)
        f = _lcreat(name, 0);
    else
        _llseek(f, 0, 2);
    if (f == HFILE_ERROR)
        return;
    for (i = 0; i < nrec; i++) {
        struct rec *t = &recs[i];
        WORD k;
        p = line;
        if (t->type == RT_CALL) {
            p += wsprintf(p, "  MD %s %s %04X dev ", (LPSTR)shims[t->shim],
                          (LPSTR)kinds[t->kind], t->msg);
            p = wval(p, t->dev);
            p += wsprintf(p, " user ");
            p = val(p, t->user, t->nu != 0);
            p += wsprintf(p, " p1 ");
            p = val(p, t->p1, t->n1 != 0);
            p += wsprintf(p, " p2 ");
            p = val(p, t->p2, t->n2 != 0);
            p += wsprintf(p, " -> %08lX if=%d d=%d from=%s", t->r, (t->flags & 0x200) != 0,
                          t->depth, who(t->retcs));
            if (t->n1 && t->kind == MDK_DRV && t->msg == 3) {
                /* DRV_OPEN's options: the string (what follows it is the
                   caller's stack) */
                int i;
                p += wsprintf(p, " [p1 \"");
                for (i = 0; i < t->n1 && t->b1[i]; i++)
                    *p++ = t->b1[i];
                p += wsprintf(p, "\"]");
            } else if (t->n1)
                p = bytes(p, t, "p1", t->b1, t->a1, t->n1);
            if (t->n2 && t->kind == MDK_DRV && (t->msg >= 0x800 || t->msg == 3))
                p = mciblock(p, t);
            else if (t->n2)
                p = bytes(p, t, "p2", t->b2, t->a2, t->n2);
            if (t->nu)
                p = bytes(p, t, "user", t->bu, t->au, t->nu);
            if (t->nf)
                p = bytes(p, t, "fmt", t->bf, 0, t->nf);
            for (k = 0; k < t->ns; k++) {
                const char *b = (const char *)(t->msg == 3 ? t->a1 : t->b1) + k * LSTR;
                const char *a = (const char *)t->a1 + k * LSTR;
                p += wsprintf(p, " +%u\"%s\"", t->soff[k], (LPSTR)b);
                if (lstrcmp(a, b))
                    p += wsprintf(p, "->\"%s\"", (LPSTR)a);
            }
        } else if (t->type == RT_DCB) {
            p += wsprintf(p, "  DCB cb ");
            p = val(p, t->id, 1);
            p += wsprintf(p, " flags %04X dev ", t->flags);
            p = wval(p, t->dev);
            p += wsprintf(p, " msg %04X user ", t->msg);
            p = val(p, t->user, 0);
            p += wsprintf(p, " p1 ");
            p = val(p, t->p1, t->n1 != 0);
            p += wsprintf(p, " p2 %08lX if=%d d=%d from=%s", t->p2, (t->task & 0x200) != 0,
                          t->depth, who(t->retcs));
            if (t->n1)
                p = bytes(p, t, "p1", t->b1, 0, t->n1);
        } else if (t->type == RT_RET) {
            const WORD *w = (const WORD *)t->b1;
            int k;
            p += wsprintf(p, "  FWD MMSYSTEM.%u from=%s args", t->msg, who(t->retcs));
            for (k = 0; k < t->n1; k++)
                p += wsprintf(p, " %04X", w[k]);
            if (t->ns)
                p += wsprintf(p, " \"%s\"", (LPSTR)t->a1);
            p += wsprintf(p, " -> %08lX d=%d", t->r, t->depth);
        } else {
            const WORD *w = (const WORD *)t->b1;
            p += wsprintf(p, "  FWD MMSYSTEM.%u from=%s args %04X %04X %04X %04X %04X %04X %04X %04X",
                          t->msg, who(t->retcs), w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
        }
        *p++ = '\r';
        *p++ = '\n';
        _lwrite(f, line, p - line);
    }
    if (lost) {
        int n = wsprintf(line, "  MD lost %u\r\n", lost);
        _lwrite(f, line, n);
    }
    nrec = 0;
    lost = 0;
    _lclose(f);
}
