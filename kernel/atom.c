/*
 * atom.c - local atoms: InitAtomTable, AddAtom, FindAtom, DeleteAtom,
 * GetAtomName, GetAtomHandle. (The global atom functions are USER's.)
 *
 * The atom table lives in the local heap of the caller's DS; kentry.asm's
 * LOCALAPI wrappers pass that DS as the first argument. Everything here
 * was observed black-box on Windows 3.1 with tests/apps/tk13.c (Standard
 * and 386 Enhanced mode agree); ARCHITECTURE.md 9.2 lists the findings.
 *
 * Layout:
 *   DS:0008  near pointer to the table (0 until the first atom call)
 *   table    a fixed local block: bucket count n, then n bucket heads
 *   entry    a fixed local block of 6 + length bytes:
 *            +0 next entry in the bucket, +2 usage count, +4 length byte,
 *            +5 the name as first added, NUL-terminated
 * A string atom is C000h | (entry >> 2), so GetAtomHandle(atom) is
 * atom << 2. New entries go to the end of their bucket's chain.
 *
 * Hash (fitted to the buckets of 62 names in tables of 37, 8191 and 8192
 * buckets; it reproduces all of them): h = rol(h,1) + h + ror(h,1) + c
 * over the case-folded characters c, 16-bit, starting from 0; bucket =
 * h mod n (unsigned). Case folding: a-z and E0h-FEh except F7h lose 20h.
 */
#include "kernel.h"

extern u16 KAPI LAlloc(u16 ds, u16 flags, u16 n);
extern u16 KAPI LFree(u16 ds, u16 h);

#define DS_ATOMTABLE    0x08
#define ATOM_BUCKETS    37      /* default table size (observed) */
#define ATOM_MAXLEN     255     /* longer names fail (length byte) */
#define ATOM_STRING     0xC000  /* string atoms are C000h and up */
#define LMEM_FIXED      0x0000
#define LMEM_ZEROINIT   0x0040

#define AE_NEXT         0
#define AE_COUNT        2
#define AE_LEN          4
#define AE_NAME         5

#define W(s, o)  (*(u16 FAR *)MK_FP((s), (o)))
#define B(s, o)  (*(u8 FAR *)MK_FP((s), (o)))


/* the case folding atoms use (observed: a-z, E0h-FEh but F7h) */
static u8 fold(u8 c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return (u8)(c - 0x20);
    return c;
}

static u16 hash(const u8 FAR *s, u16 len)
{
    u16 h = 0, i;
    for (i = 0; i < len; i++)
        h = (u16)(((h << 1) | (h >> 15)) + h + ((h >> 1) | (h << 15)) + fold(s[i]));
    return h;
}

static u16 make_table(u16 ds, u16 n)
{
    u16 t, i;
    if (!n)
        n = ATOM_BUCKETS;
    /* UNVERIFIED (deliberate difference): Windows 3.1 computes the size
       in 16 bits, so InitAtomTable(8000h) made a 2-byte table with a
       bucket count of 8000h and then wrote beyond it; w16 fails instead */
    if (n > (0xFFFFu - 2) / 2)
        return 0;
    t = LAlloc(ds, LMEM_FIXED | LMEM_ZEROINIT, 2 + 2 * n);
    if (!t)
        return 0;
    W(ds, t) = n;
    for (i = 0; i < n; i++)
        W(ds, t + 2 + 2 * i) = 0;
    W(ds, DS_ATOMTABLE) = t;
    return t;
}

static u16 table_of(u16 ds)
{
    u16 t = W(ds, DS_ATOMTABLE);
    return t ? t : make_table(ds, 0);
}

/* "#digits" (possibly no digits) is an integer atom, the number taken
   mod 10000h; observed: anything else after '#' makes a string atom. */
static int int_name(const char FAR *s, u16 *v)
{
    u16 n = 0, i;
    if (s[0] != '#')
        return 0;
    for (i = 1; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        n = (u16)(n * 10 + (s[i] - '0'));
    }
    *v = n;
    return 1;
}

/* The entry of name s (length len) in table t, or 0; *prev = the entry
   before it in the chain (0 if first), *slot = the bucket head's offset */
static u16 lookup(u16 ds, u16 t, const char FAR *s, u16 len, u16 *slot, u16 *prev)
{
    u16 e, i;
    *slot = t + 2 + 2 * (hash((const u8 FAR *)s, len) % W(ds, t));
    *prev = 0;
    for (e = W(ds, *slot); e; *prev = e, e = W(ds, e + AE_NEXT)) {
        if (B(ds, e + AE_LEN) != len)
            continue;
        for (i = 0; i < len; i++)
            if (fold(B(ds, e + AE_NAME + i)) != fold((u8)s[i]))
                break;
        if (i == len)
            return e;
    }
    return 0;
}

/* Common front of AddAtom and FindAtom: 1 with *atom set when the answer
   needs no table lookup (integer atoms, invalid names). */
static int atom_direct(const char FAR *s, u16 *atom, u16 *len)
{
    u16 v;
    if (FP_SEG(s) == 0) {               /* MAKEINTATOM */
        *atom = FP_OFF(s) < ATOM_STRING ? FP_OFF(s) : 0;
        return 1;
    }
    if (int_name(s, &v)) {
        *atom = v < ATOM_STRING ? v : 0;
        return 1;
    }
    *len = kstrlen(s);
    if (*len == 0 || *len > ATOM_MAXLEN) {
        *atom = 0;
        return 1;
    }
    return 0;
}

u16 KAPI AINIT(u16 ds, u16 n)                                   /* KERNEL.68 */
{
    u16 t = W(ds, DS_ATOMTABLE);
    /* observed: returns the table (also an existing one, unchanged) */
    return t ? t : make_table(ds, n);
}

u16 KAPI AFIND(u16 ds, const char FAR *s)                       /* KERNEL.69 */
{
    u16 atom, len, t, slot, prev, e;
    t = table_of(ds);                   /* observed: FindAtom makes it too */
    if (atom_direct(s, &atom, &len))
        return atom;
    if (!t)
        return 0;
    e = lookup(ds, t, s, len, &slot, &prev);
    return e ? ATOM_STRING | (e >> 2) : 0;
}

u16 KAPI AADD(u16 ds, const char FAR *s)                        /* KERNEL.70 */
{
    static char name[ATOM_MAXLEN + 1];     /* not on the caller's stack
                                                  (KERNEL audit #16) */
    u16 atom, len, t, slot, prev, e, i;
    t = table_of(ds);
    if (atom_direct(s, &atom, &len))
        return atom;
    if (!t)
        return 0;
    e = lookup(ds, t, s, len, &slot, &prev);
    if (e) {
        W(ds, e + AE_COUNT)++;
        return ATOM_STRING | (e >> 2);
    }
    /* the name may be in this heap: copy it before allocating */
    kmemcpy(name, s, len);
    e = LAlloc(ds, LMEM_FIXED, AE_NAME + len + 1);
    if (!e)
        return 0;
    W(ds, e + AE_NEXT) = 0;
    W(ds, e + AE_COUNT) = 1;
    B(ds, e + AE_LEN) = (u8)len;
    for (i = 0; i < len; i++)
        B(ds, e + AE_NAME + i) = name[i];
    B(ds, e + AE_NAME + len) = 0;
    /* append at the end of the chain (the table may not have moved, but
       find the end again: the slot address is still valid) */
    for (prev = 0, i = W(ds, slot); i; prev = i, i = W(ds, i + AE_NEXT))
        ;
    if (prev)
        W(ds, prev + AE_NEXT) = e;
    else
        W(ds, slot) = e;
    return ATOM_STRING | (e >> 2);
}

/* the entry of string atom `atom` if it is one in DS's table, else 0 */
static u16 entry_of(u16 ds, u16 atom, u16 *slot, u16 *prev)
{
    u16 e = atom << 2, t = W(ds, DS_ATOMTABLE), lim = sel_lim16(ds), len, x;
    if (!t || !e || e > lim || lim - e < AE_NAME)
        return 0;
    len = B(ds, e + AE_LEN);
    if (lim - e - AE_NAME < len)
        return 0;
    x = lookup(ds, t, MK_FP(ds, e + AE_NAME), len, slot, prev);
    return x == e ? e : 0;
}

u16 KAPI ADEL(u16 ds, u16 atom)                                 /* KERNEL.71 */
{
    u16 e, slot, prev;
    if (atom < ATOM_STRING)
        return 0;                       /* integer atoms: observed 0 */
    e = entry_of(ds, atom, &slot, &prev);
    if (!e) {
        /* observed on 3.1: 0 for C000h and FFFFh; for other non-atoms an
           unrelated nonzero value (04EFh / 0297h). UNVERIFIED: w16 returns
           the atom, as the SDK documents for a failure. */
        if (atom == ATOM_STRING || (u16)(atom << 2) > sel_lim16(ds))
            return 0;
        return atom;
    }
    if (--W(ds, e + AE_COUNT) == 0) {
        if (prev)
            W(ds, prev + AE_NEXT) = W(ds, e + AE_NEXT);
        else
            W(ds, slot) = W(ds, e + AE_NEXT);
        LFree(ds, e);
    }
    return 0;
}

/* copy at most n - 1 characters and a NUL (nothing if n <= 0); returns
   the characters copied (observed for string and integer atoms) */
static u16 copy_name(char FAR *buf, int n, const char FAR *s, u16 len)
{
    u16 i;
    if (n <= 0)
        return 0;
    if (len > (u16)(n - 1))
        len = (u16)(n - 1);
    for (i = 0; i < len; i++)
        buf[i] = s[i];
    buf[len] = 0;
    return len;
}

u16 KAPI ANAME(u16 ds, u16 atom, char FAR *buf, int n)          /* KERNEL.72 */
{
    char num[7];
    u16 e, slot, prev, i, v;
    if (atom == 0)
        return copy_name(buf, n, "", 0);
    if (atom < ATOM_STRING) {           /* "#" and the decimal number */
        i = sizeof num;
        num[--i] = 0;
        for (v = atom; v; v /= 10)
            num[--i] = (char)('0' + v % 10);
        num[--i] = '#';
        return copy_name(buf, n, num + i, sizeof num - 1 - i);
    }
    e = entry_of(ds, atom, &slot, &prev);
    if (!e)
        return copy_name(buf, n, "", 0);
    return copy_name(buf, n, MK_FP(ds, e + AE_NAME), B(ds, e + AE_LEN));
}

u16 KAPI AHANDLE(u16 ds, u16 atom)                              /* KERNEL.73 */
{
    (void)ds;
    /* observed: not checked (a deleted atom gives its old handle) */
    return atom < ATOM_STRING ? 0 : (u16)(atom << 2);
}
