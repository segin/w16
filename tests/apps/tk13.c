/*
 * tk13.c - local atoms: InitAtomTable, AddAtom, FindAtom, DeleteAtom,
 * GetAtomName, GetAtomHandle.
 *
 * Looks at the atom table black-box: the pointer at offset 08h of the
 * data segment, the table (a bucket count and bucket heads) and the
 * entries the handles point to. Fresh local heaps in global blocks
 * (call_ds) give atom tables of chosen sizes, so the bucket of each test
 * string in a 37- and an 8191-bucket table can be recorded; together they
 * pin down the hash function.
 *
 * Output and checks: see tklog.h; reference values in tk13ref.h. Atom
 * values and handles depend on the program's layout and are printed as
 * OBS ~ and checked relative to each other.
 */
#include <windows.h>

#define TK_REF "tk13ref.h"
#include "tklog.h"

extern DWORD __cdecl call_ds(WORD ds, void FAR *fn, WORD nargs, WORD a1, WORD a2, WORD a3);

static WORD ds(void)
{
    return entry_regs[E_DS];
}

static WORD W(WORD sel, WORD off)
{
    return *w_at(sel, off);
}

static BYTE B(WORD sel, WORD off)
{
    return *(BYTE FAR *)MAKELP(sel, off);
}

static WORD slen(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    return n;
}

static void fill(char *b, WORD n, char c)
{
    WORD i;
    for (i = 0; i < n; i++)
        b[i] = c;
}

/* name + suffix -> buf */
static const char *nm(const char *a, const char *b)
{
    static char buf[80];
    WORD i = 0;
    while (*a && i < 60)
        buf[i++] = *a++;
    while (*b && i < 78)
        buf[i++] = *b++;
    buf[i] = 0;
    return buf;
}

/* a string as "a b c" hex bytes, with its length, for obs names */
static WORD sum(const char FAR *s, WORD n)
{
    WORD h = 0, i;
    for (i = 0; i < n; i++)
        h = (WORD)((h << 3) | (h >> 13)) ^ (BYTE)s[i];
    return h;
}

static const char *const hs[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
    "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "AA", "AB", "BA", "AC", "CA", "BB", "ABC", "CBA", "ABCD", "DCBA",
    "HELLO", "WORLD", "EDIT", "BUTTON", "STATIC", "LISTBOX", "COMBOBOX",
    "SCROLLBAR", "#", "#X", "Z9", "0", "9", "_", "~", "@", "AZ", "ZA",
    "ABCDEFGHIJKLMNOP", "THE QUICK BROWN FOX", "zz", "Az", "aZ",
    "\x80", "\xC4", "\xFF", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    0
};

/* bucket and chain position of handle h in table t of segment sel */
static void where(WORD sel, WORD t, WORD h, WORD *bucket, WORD *pos)
{
    WORD n = W(sel, t), b, e, p, guard;
    for (b = 0; b < n; b++)
        for (e = W(sel, t + 2 + 2 * b), p = 0, guard = 0; e && guard < 500; e = W(sel, e), p++, guard++)
            if (e == h) {
                *bucket = b;
                *pos = p;
                return;
            }
    *bucket = *pos = 0xFFFF;
}

static WORD fresh_heap(WORD size, HGLOBAL *g)
{
    WORD sel;
    *g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size);
    sel = (WORD)((DWORD)GlobalLock(*g) >> 16);
    LocalInit(sel, 16, size - 1);
    return sel;
}

static WORD c_init(WORD sel, WORD n)
{
    return (WORD)call_ds(sel, (void FAR *)InitAtomTable, 1, n, 0, 0);
}

static WORD c_add(WORD sel, const char FAR *s)
{
    return (WORD)call_ds(sel, (void FAR *)AddAtom, 2, SELECTOROF(s), OFFSETOF(s), 0);
}

static WORD c_handle(WORD sel, WORD a)
{
    return (WORD)call_ds(sel, (void FAR *)GetAtomHandle, 1, a, 0, 0);
}

/* ---- the task's own table: made by the first AddAtom ---- */
static void probe_basic(void)
{
    ATOM a, b, c;
    WORD t, h, r;
    char buf[64];

    obs("DS:08 before", W(ds(), 8));
    obs("FindAtom before any table", FindAtom("Hello"));
    obs("DS:08 after FindAtom", W(ds(), 8) != 0);
    a = AddAtom("Hello");
    t = W(ds(), 8);
    obs_env("atom Hello", a);
    obs_env("DS:08", t);
    obs("table - pHeap", t - W(ds(), 6));
    obs("table size (default)", W(ds(), t));
    obs("table LocalSize", LocalSize((HLOCAL)t));
    obs("table LocalFlags", LocalFlags((HLOCAL)t));
    obs("table fixed (multiple of 4)", (t & 3) == 0);
    dump("table", ds(), t, 8);
    h = (WORD)GetAtomHandle(a);
    obs_env("GetAtomHandle(Hello)", h);
    obs("atom == C000 | handle >> 2", a == (0xC000 | (h >> 2)));
    obs("handle LocalSize", LocalSize((HLOCAL)h));
    obs("handle LocalFlags", LocalFlags((HLOCAL)h));
    dump("entry Hello", ds(), h, 6);
    obs("entry [0] next", W(ds(), h));
    obs("entry [2] count", W(ds(), h + 2));
    obs("entry [4] length", B(ds(), h + 4));
    obs("entry [5..] name", sum((char FAR *)MAKELP(ds(), h + 5), 6) == sum("Hello", 6));
    b = AddAtom("HELLO");
    obs("AddAtom(HELLO)==Hello", b == a);
    obs("entry count after 2nd add", W(ds(), h + 2));
    c = FindAtom("hElLo");
    obs("FindAtom(hElLo)==Hello", c == a);
    obs("entry count after FindAtom", W(ds(), h + 2));
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, sizeof buf);
    obs("GetAtomName(Hello) ret", r);
    obs("GetAtomName(Hello) case kept", sum(buf, 6) == sum("Hello", 6));
    obs("FindAtom( Hello)", FindAtom(" Hello"));
    obs("FindAtom(Hello )", FindAtom("Hello "));
    obs("FindAtom(Hell)", FindAtom("Hell"));

    /* truncation */
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, 3);
    obs("GetAtomName(Hello,3) ret", r);
    obs("GetAtomName(Hello,3) buf[0]", (BYTE)buf[0]);
    obs("GetAtomName(Hello,3) buf[1]", (BYTE)buf[1]);
    obs("GetAtomName(Hello,3) buf[2]", (BYTE)buf[2]);
    obs("GetAtomName(Hello,3) buf[3]", (BYTE)buf[3]);
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, 5);
    obs("GetAtomName(Hello,5) ret", r);
    obs("GetAtomName(Hello,5) buf[4]", (BYTE)buf[4]);
    obs("GetAtomName(Hello,5) buf[5]", (BYTE)buf[5]);
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, 6);
    obs("GetAtomName(Hello,6) ret", r);
    obs("GetAtomName(Hello,6) buf[5]", (BYTE)buf[5]);
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, 1);
    obs("GetAtomName(Hello,1) ret", r);
    obs("GetAtomName(Hello,1) buf[0]", (BYTE)buf[0]);
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(a, buf, 0);
    obs("GetAtomName(Hello,0) ret", r);
    obs("GetAtomName(Hello,0) buf[0]", (BYTE)buf[0]);

    /* reference counts and deletion */
    obs("DeleteAtom(Hello) 1", DeleteAtom(a));
    obs("entry count after delete 1", W(ds(), h + 2));
    obs("FindAtom after delete 1", FindAtom("Hello") == a);
    obs("DeleteAtom(Hello) 2", DeleteAtom(a));
    obs("FindAtom after delete 2", FindAtom("Hello"));
    r = DeleteAtom(a);
    obs("DeleteAtom(Hello) 3 nonzero", r != 0);
    obs_env("DeleteAtom(Hello) 3", r);
    fill(buf, sizeof buf, 'x');
    obs("GetAtomName(deleted) ret", GetAtomName(a, buf, sizeof buf));
    obs("GetAtomName(deleted) buf[0]", (BYTE)buf[0]);
    obs("GetAtomHandle(deleted)==old handle", (WORD)GetAtomHandle(a) == h);
    a = AddAtom("Hello");
    obs("re-added Hello same handle", (WORD)GetAtomHandle(a) == h);
    obs("re-added count", W(ds(), h + 2));
    DeleteAtom(a);

    /* empty string */
    obs("AddAtom(\"\")", AddAtom(""));
    obs("FindAtom(\"\")", FindAtom(""));

    /* stored case: first one wins */
    a = AddAtom("mixedCase");
    b = AddAtom("MIXEDCASE");
    fill(buf, sizeof buf, 'x');
    GetAtomName(b, buf, sizeof buf);
    obs("stored case is first", sum(buf, 10) == sum("mixedCase", 10));
    DeleteAtom(a);
    DeleteAtom(b);

    /* characters above 7Fh: case folding? */
    a = AddAtom("\xE4x");
    obs("FindAtom(C4 x) == AddAtom(E4 x)", FindAtom("\xC4x") == a);
    obs("FindAtom(E4 X) == AddAtom(E4 x)", FindAtom("\xE4X") == a);
    DeleteAtom(a);
    a = AddAtom("\xC4y");
    obs("FindAtom(E4 y) == AddAtom(C4 y)", FindAtom("\xE4y") == a);
    DeleteAtom(a);

    /* the table's chain: two atoms in one bucket. "A" and ... decided by
       the hash (probe_hash records the buckets); here only the next link */
}

/* ---- integer atoms ---- */
static const char *const ints[] = {
    "#123", "#0", "#1", "#65535", "#49151", "#49152", "#70000", "#65536",
    "#12a", "#abc", "# 12", "#-1", "#+5", "#", "#0012", "##1", "#1 ",
    "#00000000000000000005", "#4294967297", 0
};

static void probe_int(void)
{
    WORD i, a, r;
    char buf[64];
    static const WORD mk[] = { 0, 1, 0x7B, 0xBFFF, 0xC000, 0xC001, 0xFFFF };

    for (i = 0; ints[i]; i++) {
        a = AddAtom(ints[i]);
        if (a >= 0xC000 || a == 0) {
            obs(nm("AddAtom ", ints[i]), a >= 0xC000 ? 0xC000 : a);
            if (a) {
                fill(buf, sizeof buf, 0);
                GetAtomName(a, buf, sizeof buf);
                obs(nm("  name same: ", ints[i]), sum(buf, slen(ints[i]) + 1) == sum(ints[i], slen(ints[i]) + 1));
                obs(nm("  FindAtom same: ", ints[i]), FindAtom(ints[i]) == a);
                DeleteAtom(a);
            }
        } else
            obs(nm("AddAtom ", ints[i]), a);
    }
    for (i = 0; i < sizeof mk / sizeof mk[0]; i++) {
        a = AddAtom((LPCSTR)MAKELP(0, mk[i]));
        obsn("AddAtom(MAKEINTATOM)", mk[i], a);
        obsn("FindAtom(MAKEINTATOM)", mk[i], FindAtom((LPCSTR)MAKELP(0, mk[i])));
        if (mk[i] < 0xC000)
            obsn("DeleteAtom(int)", mk[i], DeleteAtom(mk[i]));
    }
    obs("FindAtom(#123)", FindAtom("#123"));
    obs("DeleteAtom(7B)", DeleteAtom(0x7B));
    obs("GetAtomHandle(7B)", (WORD)GetAtomHandle(0x7B));
    obs("GetAtomHandle(0)", (WORD)GetAtomHandle(0));

    fill(buf, sizeof buf, 'x');
    r = GetAtomName(0x7B, buf, sizeof buf);
    obs("GetAtomName(7B) ret", r);
    obs("GetAtomName(7B) is #123", sum(buf, 5) == sum("#123", 5));
    obs("GetAtomName(7B) buf[5]", (BYTE)buf[5]);
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(5, buf, sizeof buf);
    obs("GetAtomName(5) ret", r);
    obs("GetAtomName(5) is #5", sum(buf, 3) == sum("#5", 3));
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(0xBFFF, buf, sizeof buf);
    obs("GetAtomName(BFFF) ret", r);
    obs("GetAtomName(BFFF) is #49151", sum(buf, 7) == sum("#49151", 7));
    fill(buf, sizeof buf, 'x');
    r = GetAtomName(0, buf, sizeof buf);
    obs("GetAtomName(0) ret", r);
    obs("GetAtomName(0) buf[0]", (BYTE)buf[0]);
    obs("GetAtomName(0) buf[1]", (BYTE)buf[1]);
    for (i = 0; i <= 7; i++) {
        fill(buf, sizeof buf, 'x');
        r = GetAtomName(0x7B, buf, i);
        obsn("GetAtomName(7B,n) ret", i, r);
        obsn("GetAtomName(7B,n) buf[0]", i, (BYTE)buf[0]);
        obsn("GetAtomName(7B,n) buf[n-1]", i, i ? (BYTE)buf[i - 1] : 0);
        obsn("GetAtomName(7B,n) buf[n]", i, (BYTE)buf[i]);
    }
}

/* ---- table sizes on fresh heaps ---- */
static void probe_tables(void)
{
    HGLOBAL g;
    WORD sel, r, t, a, h;
    static const WORD sizes[] = { 0, 1, 2, 5, 37, 100, 0xFFFF };
    WORD i;

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        sel = fresh_heap(0x2000, &g);
        r = c_init(sel, sizes[i]);
        obsn("InitAtomTable(n) ret", sizes[i], r);
        t = W(sel, 8);
        obsn("InitAtomTable(n) DS:08 set", sizes[i], t != 0);
        if (t) {
            obsn("InitAtomTable(n) size", sizes[i], W(sel, t));
            obsn("InitAtomTable(n) LocalSize", sizes[i],
                 (WORD)call_ds(sel, (void FAR *)LocalSize, 1, t, 0, 0));
            obsn("InitAtomTable(n) offset from pHeap", sizes[i], t - W(sel, 6));
        }
        if (r && t) {
            a = c_add(sel, "Hello");
            h = c_handle(sel, a);
            obsn("  Hello atom == C000|h>>2", sizes[i], a == (0xC000 | (h >> 2)));
            obsn("  Hello offset from table", sizes[i], h - t);
            r = c_init(sel, 7);
            obsn("  InitAtomTable(7) again ret", sizes[i], r);
            obsn("  DS:08 same", sizes[i], W(sel, 8) == t);
            obsn("  size after", sizes[i], W(sel, W(sel, 8)));
        }
        GlobalUnlock(g);
        GlobalFree(g);
    }
    /* no InitAtomTable: AddAtom makes the default table */
    sel = fresh_heap(0x2000, &g);
    a = c_add(sel, "Hello");
    t = W(sel, 8);
    obs("fresh AddAtom: table size", W(sel, t));
    obs("fresh AddAtom: table offset from pHeap", t - W(sel, 6));
    obs("fresh AddAtom: entry offset from table", c_handle(sel, a) - t);
    a = c_add(sel, "World");
    obs("fresh AddAtom 2: entry offset from table", c_handle(sel, a) - t);
    GlobalUnlock(g);
    GlobalFree(g);
}

/* ---- buckets: the hash function ---- */
static void probe_hash(void)
{
    HGLOBAL g;
    WORD sel, i, a, b, p, t;
    WORD at[80];

    /* in the task's (default) table */
    t = W(ds(), 8);
    for (i = 0; hs[i]; i++)
        at[i] = AddAtom(hs[i]);
    for (i = 0; hs[i]; i++) {
        where(ds(), t, (WORD)GetAtomHandle(at[i]), &b, &p);
        obs(nm("bucket37 ", hs[i]), b);
        obs(nm("chainpos37 ", hs[i]), p);
    }
    /* lower case finds the same */
    obs("FindAtom(hello)==HELLO", FindAtom("hello") == at[36]);
    for (i = 0; hs[i]; i++)
        DeleteAtom(at[i]);
    obs("all deleted: buckets empty", W(ds(), t + 2) == 0);

    sel = fresh_heap(0x7000, &g);
    obs("InitAtomTable(8191)", c_init(sel, 8191));
    t = W(sel, 8);
    for (i = 0; hs[i]; i++) {
        a = c_add(sel, hs[i]);
        where(sel, t, c_handle(sel, a), &b, &p);
        obs(nm("bucket8191 ", hs[i]), b);
    }
    GlobalUnlock(g);
    GlobalFree(g);
    sel = fresh_heap(0x7000, &g);
    obs("InitAtomTable(8192)", c_init(sel, 8192));
    t = W(sel, 8);
    for (i = 0; hs[i]; i++) {
        a = c_add(sel, hs[i]);
        where(sel, t, c_handle(sel, a), &b, &p);
        obs(nm("bucket8192 ", hs[i]), b);
    }
    GlobalUnlock(g);
    GlobalFree(g);

    /* characters 80h-FFh (and a-z): the hash of a one-character name is
       the character after case folding, so its bucket in an 8191-bucket
       table shows the folding */
    sel = fresh_heap(0x7000, &g);
    c_init(sel, 8191);
    t = W(sel, 8);
    for (i = 0x61; i < 0x100; i++) {
        char s[2];
        if (i == 0x7B)
            i = 0x80;
        s[0] = (char)i;
        s[1] = 0;
        a = c_add(sel, s);
        where(sel, t, c_handle(sel, a), &b, &p);
        obsn("fold", i, b);
    }
    GlobalUnlock(g);
    GlobalFree(g);
}

/* ---- long names ---- */
static void probe_long(void)
{
    static char s[400];
    static char buf[400];
    static const WORD lens[] = { 254, 255, 256, 300 };
    WORD i, a, r;

    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        fill(s, sizeof s, 0);
        fill(s, lens[i], 'L');
        s[lens[i] - 1] = (char)('0' + i);
        a = AddAtom(s);
        obsn("long AddAtom ok", lens[i], a >= 0xC000);
        if (a >= 0xC000) {
            WORD h = (WORD)GetAtomHandle(a);
            obsn("long entry length byte", lens[i], B(ds(), h + 4));
            obsn("long LocalSize", lens[i], LocalSize((HLOCAL)h));
            fill(buf, sizeof buf, 'x');
            r = GetAtomName(a, buf, sizeof buf);
            obsn("long GetAtomName ret", lens[i], r);
            obsn("long FindAtom", lens[i], FindAtom(s) == a);
            DeleteAtom(a);
        }
    }
}

/* ---- string atoms that are not atoms (last: may read anywhere) ---- */
static void probe_bogus(void)
{
    char buf[16];
    WORD a = AddAtom("Bogus"), h = (WORD)GetAtomHandle(a);
    obs_env("DeleteAtom(C001)", DeleteAtom(0xC001));
    obs_env("DeleteAtom(FFFF)", DeleteAtom(0xFFFF));
    obs_env("DeleteAtom(C000)", DeleteAtom(0xC000));
    fill(buf, sizeof buf, 'x');
    obs_env("GetAtomName(C001)", GetAtomName(0xC001, buf, sizeof buf));
    obs_env("GetAtomName(C001) buf[0]", (BYTE)buf[0]);
    /* the handle of Bogus + 4 */
    obs_env("DeleteAtom(Bogus+1)", DeleteAtom(a + 1));
    obs("Bogus still there", FindAtom("Bogus") == a);
    obs("Bogus count", W(ds(), h + 2));
}

int __cdecl tmain(void)
{
    out("TK13 start\r\n");
    probe_basic();
    probe_int();
    probe_hash();
    probe_tables();
    probe_long();
    probe_bogus();
    out("TK13 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
