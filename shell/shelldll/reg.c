/*
 * reg.c - w16 SHELL.DLL: the registration database (SHELL.1-7) and its
 * file, REG.DAT in the Windows directory.
 *
 * Everything here was observed on Windows 3.1 with probes SH1 and SH2
 * (tests/apps/sh1.c, sh2.c: return codes, and REG.DAT copied after each
 * step and diffed); the file format is documented in tools/regdat.py and
 * ARCHITECTURE.md 13. SHELL.DLL's code was never looked at.
 *
 * The database is kept in memory in the file's own layout: the entry
 * table (8-byte entries: the top node, the hash buckets, key nodes, text
 * entries and free entries) and the text area (each string preceded by
 * the WORD 2 * entry + 1). Keys and values are text entries shared by
 * reference count. The file is rewritten whenever a change is made while
 * no key is open, or when the last open key is closed.
 *
 * Limits are the format's: entry indices and text offsets are WORDs, so
 * at most 65535 entries and 64K of text. The entry table is a huge block
 * when it passes 64K. Key names are limited as on Windows 3.1 (SH2): 1-63
 * characters from 21h-7Fh, which is an observed compatibility rule, not a
 * buffer size.
 */
#include "shell.h"

#define MAGIC0          0x43434853L     /* "SHCC" */
#define MAGIC1          0x30312E33L     /* "3.10" */
#define HDR_SIZE        0x20
#define NBUCKETS        37              /* Windows 3.1 writes 37 (SH1, SH2) */
#define GROW            16              /* entries added at a time (SH1) */
#define MAXNAME         63              /* longest key name (SH2) */

typedef struct { WORD a, b, c, d; } ENT;
/* key node:   a next sibling, b first child, c name text, d value text
   text entry: a next in hash chain, b references, c length, d offset
   free entry: a next free, b 0 (c, d keep what they held)
   bucket:     a first text entry of the chain (the chain is circular) */

static HGLOBAL h_ent, h_text;
static WORD ent_sel, ahincr;
static WORD n_ent;                      /* entries */
static WORD n_buckets = NBUCKETS;
static WORD free_head;
static char __far *text;
static WORD text_size;
static DWORD text_cap;
static WORD open_count;
static int db_bad, dirty;
static int made;                        /* walk() made a key */

static void compact(void);
LONG SHAPI RegSetValue(HKEY k, LPCSTR sub, DWORD type, LPCSTR data, DWORD cb);
static void delete_tree(WORD k);

/* Deleted keys are taken out of the tree at once but freed only when the
   file is next written, in the order they were deleted (SH1: R07, where a
   value set through a handle of a deleted key was freed with that key,
   between its name and its node). */
static WSTACK doomed;

static void free_doomed(void)
{
    WORD i;
    for (i = 0; i < doomed.n; i++)
        delete_tree(doomed.p[i]);
    ws_free(&doomed);
}
static char path[160];

static ENT __far *E(WORD i)
{
    return (ENT __far *)MAKELP(ent_sel + (i >> 13) * ahincr, (i & 0x1FFF) << 3);
}

/* ---- the file ---- */

static int read_all(HFILE f, void __far *p, DWORD n)
{
    BYTE __far *b = (BYTE __far *)p;
    WORD sel = SELECTOROF(b), off = OFFSETOF(b);
    while (n) {
        WORD k = n > 0x8000L ? 0x8000 : (WORD)n;
        if (_lread(f, (LPSTR)MAKELP(sel, off), k) != k)
            return 0;
        n -= k;
        off += k;
        if (!off)
            sel += ahincr;
    }
    return 1;
}

static int write_all(HFILE f, const void __far *p, DWORD n)
{
    const BYTE __far *b = (const BYTE __far *)p;
    WORD sel = SELECTOROF(b), off = OFFSETOF(b);
    while (n) {
        WORD k = n > 0x8000L ? 0x8000 : (WORD)n;
        if (_lwrite(f, (LPCSTR)MAKELP(sel, off), k) != k)
            return 0;
        n -= k;
        off += k;
        if (!off)
            sel += ahincr;
    }
    return 1;
}

static void flush(void)
{
    HFILE f;
    OFSTRUCT of;
    DWORD hdr[6];
    WORD w[2];
    if (db_bad)
        return;
    free_doomed();
    compact();
    f = OpenFile(path, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    hdr[0] = MAGIC0;
    hdr[1] = MAGIC1;
    hdr[2] = HDR_SIZE;
    hdr[3] = HDR_SIZE;
    hdr[4] = n_ent;
    hdr[5] = HDR_SIZE + (DWORD)n_ent * 8;
    write_all(f, hdr, sizeof hdr);
    hdr[0] = text_size ? text_size : 1;  /* an empty area: 1 (SH2) */
    write_all(f, hdr, 4);
    w[0] = n_buckets;
    w[1] = free_head;
    write_all(f, w, 4);
    write_all(f, E(0), (DWORD)n_ent * 8);
    if (text_size)
        write_all(f, text, text_size);
    else
        write_all(f, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);  /* 16 zeros (SH2) */
    _lclose(f);
    dirty = 0;
}

/* after a change: written now unless a key is open (SH1) */
static void changed(void)
{
    dirty = 1;
    if (!open_count)
        flush();
}

/* ---- entries ---- */

static int set_entries(WORD n)
{
    DWORD size = (DWORD)n * 8;
    HGLOBAL h;
    if (h_ent) {
        GlobalUnlock(h_ent);
        h = GlobalReAlloc(h_ent, size, GMEM_MOVEABLE | GMEM_ZEROINIT);
        if (!h) {
            GlobalLock(h_ent);
            return 0;
        }
    } else {
        h = sh_alloc(size);
        if (!h)
            return 0;
    }
    h_ent = h;
    ent_sel = SELECTOROF(GlobalLock(h));
    return 1;
}

static WORD alloc_ent(void)
{
    WORD i;
    if (!free_head) {
        /* 16 more, linked in order (SH1: R06) */
        WORD first = n_ent, k;
        if (n_ent > 0xFFFF - GROW || !set_entries(n_ent + GROW))
            return 0;
        n_ent += GROW;
        for (k = first; k < n_ent; k++) {
            ENT __far *e = E(k);
            e->a = k + 1 < n_ent ? k + 1 : 0;
            e->b = e->c = e->d = 0;
        }
        free_head = first;
    }
    i = free_head;
    free_head = E(i)->a;
    return i;
}

/* a freed entry keeps its last two words (SH1: R07, R08) */
static void free_ent(WORD i)
{
    ENT __far *e = E(i);
    e->a = free_head;
    e->b = 0;
    free_head = i;
}

/* ---- text ---- */

/* the hash: the sum of the first 39 characters, upper-cased (SH1, SH2:
   longer strings of 62, 63 and 1000 characters sat in the buckets their
   first 39 characters give) */
#define HASHLEN 39

static WORD bucket(const char __far *s, WORD n)
{
    WORD sum = 0;
    if (n > HASHLEN)
        n = HASHLEN;
    while (n--)
        sum += sh_upper(*s++);
    return 1 + sum % n_buckets;
}

static int text_room(WORD more)
{
    DWORD need = (DWORD)text_size + more;
    HGLOBAL h;
    if (need > 0xFFFFL)
        return 0;
    if (need <= text_cap)
        return 1;
    need = (need + 1023) & ~1023L;
    if (need > 0xFFFFL)
        need = 0xFFFFL;
    if (h_text) {
        GlobalUnlock(h_text);
        h = GlobalReAlloc(h_text, need, GMEM_MOVEABLE);
    } else {
        h = sh_alloc(need);
    }
    if (!h) {
        if (h_text)
            text = (char __far *)GlobalLock(h_text);
        return 0;
    }
    h_text = h;
    text = (char __far *)GlobalLock(h);
    text_cap = need;
    return 1;
}

/* Strings whose last reference goes stay where they are until the file
   is written, and only then do the later ones move down (SH1, SH2: a
   freed text entry keeps the offset it had when it was freed, not one
   shifted by strings freed before it in the same session; the live ones
   are packed in the file). The bytes of dead strings are marked here. */
static HGLOBAL h_dead;
static BYTE __far *dead;
static WORD dead_cap;                   /* bytes of the bitmap */

static void mark_dead(WORD from, WORD n)
{
    WORD need = (WORD)((text_cap + 7) / 8);
    if (dead_cap < need) {
        HGLOBAL h = h_dead ? (GlobalUnlock(h_dead),
                              GlobalReAlloc(h_dead, need, GMEM_MOVEABLE | GMEM_ZEROINIT))
                           : sh_alloc(need);
        if (!h) {
            if (h_dead)
                dead = (BYTE __far *)GlobalLock(h_dead);
            return;                     /* the string is merely kept */
        }
        h_dead = h;
        dead = (BYTE __far *)GlobalLock(h);
        dead_cap = need;
    }
    while (n--) {
        dead[from >> 3] |= (BYTE)(1 << (from & 7));
        from++;
    }
}

static void compact(void)
{
    WORD r = 0, w = 0;
    if (!h_dead)
        return;
    while (r < text_size) {
        WORD i, n;
        if (dead[r >> 3] & (1 << (r & 7))) {
            r++;
            continue;
        }
        i = *(WORD __far *)(text + r) >> 1;
        n = E(i)->c + 2;
        if (w != r)
            sh_move(text + w, text + r, n);
        E(i)->d = w + 2;
        w += n;
        r += n;
    }
    text_size = w;
    GlobalUnlock(h_dead);
    GlobalFree(h_dead);
    h_dead = NULL;
    dead = NULL;
    dead_cap = 0;
}

/* the text entry for s (n bytes): an existing one, matched exactly for a
   value but regardless of case for a key name (SH2: a key named
   "txtfile" took the existing value string "TXTFILE"; SH11: a value
   "TXTFILE" did not take "txtfile"), with one more reference; or a new
   one at the end of its chain and of the text area */
static WORD intern(const char __far *s, WORD n, int name)
{
    WORD b = bucket(s, n), i, last = b, t;
    ENT __far *e;
    for (i = E(b)->a; i != b; i = E(i)->a) {
        e = E(i);
        if (e->c == n && (name ? sh_same_ci(text + e->d, s, n) : sh_same(text + e->d, s, n))) {
            e->b++;
            return i;
        }
        last = i;
    }
    if (!text_room(n + 2)) {
        compact();
        if (!text_room(n + 2))
            return 0;
    }
    t = alloc_ent();
    if (!t)
        return 0;
    *(WORD __far *)(text + text_size) = t * 2 + 1;
    sh_move(text + text_size + 2, s, n);
    e = E(t);
    e->a = b;
    e->b = 1;
    e->c = n;
    e->d = text_size + 2;
    text_size += n + 2;
    E(last)->a = t;
    return t;
}

static void unref(WORD t)
{
    ENT __far *e = E(t);
    WORD b, p, off, n;
    if (!t || --e->b)
        return;
    /* out of its chain */
    off = e->d;
    n = e->c;
    b = bucket(text + off, n);
    for (p = b; E(p)->a != t; p = E(p)->a)
        if (E(p)->a == b)
            break;
    if (E(p)->a == t)
        E(p)->a = e->a;
    mark_dead(off - 2, n + 2);
    free_ent(t);
}

/* ---- keys ---- */

static WORD child(WORD node, const char __far *name, WORD n)
{
    WORD c;
    for (c = E(node)->b; c; c = E(c)->a) {
        ENT __far *t = E(E(c)->c);
        if (t->c == n && sh_same_ci(text + t->d, name, n))
            return c;
    }
    return 0;
}

static WORD new_child(WORD node, const char __far *name, WORD n)
{
    WORD t = intern(name, n, 1), k;
    ENT __far *e;
    if (!t)
        return 0;
    k = alloc_ent();
    if (!k) {
        unref(t);
        return 0;
    }
    e = E(k);
    e->a = E(node)->b;
    e->b = 0;
    e->c = t;
    e->d = 0;
    E(node)->b = k;
    return k;
}

/* HKEY_CLASSES_ROOT is the node ".classes" under the top node (made
   when first written to: SH2 with no REG.DAT); a handle whose high word
   is 0 means the top node; otherwise the low word is the node (SH2) */
static LONG key_node(HKEY k, int create, WORD *node)
{
    static const char classes[] = ".classes";
    if (k == HKEY_CLASSES_ROOT) {
        WORD c = child(0, classes, 8);
        if (!c && create)
            c = new_child(0, classes, 8);
        if (!c)
            return create ? ERROR_OUTOFMEMORY : ERROR_BADKEY;
        *node = c;
        return ERROR_SUCCESS;
    }
    if (!HIWORD(k)) {
        *node = 0;
        return ERROR_SUCCESS;
    }
    if (LOWORD(k) >= n_ent)
        return ERROR_BADKEY;
    *node = LOWORD(k);
    return ERROR_SUCCESS;
}

static int valid_name(const char __far *s, WORD n)
{
    if (!n || n > MAXNAME)
        return 0;
    while (n--) {
        BYTE c = (BYTE)*s++;
        if (c <= ' ' || c >= 0x80)
            return 0;
    }
    return 1;
}

/* A database made from nothing gets the OLE servers of WIN.INI's
   [embedding] section (class=description,description,server,format):
   HKEY_CLASSES_ROOT\class = description and class\protocol\
   StdFileEditing\server = server, in the section's order (SH2, SH12).
   Windows 3.1 did it in the first registration call made about 0.9 s
   after the shell started, not before (SH12; UNVERIFIED: the exact
   delay and why SH2 saw it later; the order in which the first class's
   strings were allocated differed slightly from RegSetValue's). */
#define IMPORT_DELAY 1065
static int import_pending;
static DWORD init_tick;

static void import_embedding(void)
{
    HGLOBAL h = NULL;
    char __far *keys = NULL;
    DWORD size = 256;
    char val[256], sub[160];
    const char __far *k;
    for (;;) {
        if (h) {
            GlobalUnlock(h);
            GlobalFree(h);
        }
        h = sh_alloc(size);
        keys = (char __far *)sh_lock(h);
        if (!keys)
            return;
        if ((DWORD)GetProfileString("embedding", NULL, "", keys, (int)size) < size - 2 ||
            size >= 0x8000L)
            break;
        size *= 2;
    }
    for (k = keys; *k; k += sh_len(k) + 1) {
        char __far *f[4];
        int n = 0;
        char __far *p = val;
        if (!GetProfileString("embedding", k, "", val, sizeof val))
            continue;
        f[0] = val;
        for (; *p && n < 3; p++)
            if (*p == ',') {
                *p = 0;
                f[++n] = p + 1;
            }
        if (n < 2)
            continue;
        RegSetValue(HKEY_CLASSES_ROOT, k, REG_SZ, f[0], 0);
        lstrcpy(sub, k);
        lstrcat(sub, "\\protocol\\StdFileEditing\\server");
        RegSetValue(HKEY_CLASSES_ROOT, sub, REG_SZ, f[2], 0);
    }
    GlobalUnlock(h);
    GlobalFree(h);
}

static void maybe_import(void)
{
    if (import_pending && GetTickCount() - init_tick >= IMPORT_DELAY) {
        import_pending = 0;
        import_embedding();
    }
}

/* the node at sub (components separated by '\') below k; with create,
   missing keys are made (every component is checked first) */
static LONG walk(HKEY k, LPCSTR sub, int create, WORD *node)
{
    WORD n;
    LONG r;
    const char __far *p;
    if (db_bad)
        return ERROR_BADDB;
    maybe_import();
    if (create && sub && *sub) {
        for (p = sub;;) {
            WORD len = 0;
            while (p[len] && p[len] != '\\')
                len++;
            if (!valid_name(p, len))
                return ERROR_BADKEY;
            if (!p[len])
                break;
            p += len + 1;
        }
    }
    r = key_node(k, create, &n);
    if (r)
        return r;
    if (sub && *sub) {
        for (p = sub;;) {
            WORD len = 0, c;
            while (p[len] && p[len] != '\\')
                len++;
            if (!len)
                return ERROR_BADKEY;
            c = child(n, p, len);
            if (!c) {
                if (!create)
                    return ERROR_BADKEY;
                c = new_child(n, p, len);
                if (!c)
                    return ERROR_OUTOFMEMORY;
                made = 1;
            }
            n = c;
            if (!p[len])
                break;
            p += len + 1;
        }
    }
    *node = n;
    return ERROR_SUCCESS;
}

/* free a key and everything below it, children first, each node's name,
   then its value, then the node (SH1: R07, R08) */
static void delete_tree(WORD k)
{
    WSTACK st = { 0 };
    for (;;) {
        ENT __far *e = E(k);
        WORD c = e->b;
        if (c && ws_push(&st, k)) {
            e->b = E(c)->a;             /* detach the first child, go down */
            k = c;
            continue;
        }
        unref(e->c);
        unref(E(k)->d);
        free_ent(k);
        if (!st.n)
            break;
        k = ws_pop(&st);
    }
    ws_free(&st);
}

/* ---- loading ---- */

static void empty_db(void)
{
    WORD i;
    n_buckets = NBUCKETS;
    n_ent = 1 + n_buckets;
    set_entries(n_ent);
    for (i = 1; i <= n_buckets; i++)
        E(i)->a = i;
    free_head = 0;
    text_size = 0;
    text_room(1);
}

/* Windows 3.1 rebuilds the tables when it loads them (SH2, three sessions
   from different files): only what the key tree reaches is kept (not
   even the top node's own value), the free entries are relinked in
   order, the table ends 16 entries after the last one in use, the
   chains lose their dead entries and the text area its dead strings. */
static void collect(void)
{
    HGLOBAL hm = sh_alloc((DWORD)n_ent / 8 + 1);
    BYTE __far *mark = (BYTE __far *)sh_lock(hm);
    WSTACK st = { 0 };
    WORD i, last = 0, prev, k;
#define MARK(x) (mark[(x) >> 3] |= (BYTE)(1 << ((x) & 7)))
#define MARKED(x) (mark[(x) >> 3] & (1 << ((x) & 7)))
    if (!mark)
        return;
    for (i = 0; i <= n_buckets; i++)
        MARK(i);
    k = E(0)->b;
    while (k) {
        ENT __far *e = k < n_ent ? E(k) : NULL;
        if (e && !MARKED(k)) {
            MARK(k);
            if (e->c && e->c < n_ent)
                MARK(e->c);
            if (e->d && e->d < n_ent)
                MARK(e->d);
            if (e->a)
                ws_push(&st, e->a);
            if (e->b)
                ws_push(&st, e->b);
        }
        k = ws_pop(&st);
    }
    ws_free(&st);
    for (i = 0; i < n_ent; i++)
        if (MARKED(i))
            last = i;
    /* the chains */
    for (i = 1; i <= n_buckets; i++) {
        WORD p = i, t = E(i)->a, guard = n_ent;
        while (t != i && guard--) {
            if (t >= n_ent)
                break;
            if (MARKED(t)) {
                E(p)->a = t;
                p = t;
            }
            t = E(t)->a;
        }
        E(p)->a = i;
    }
    /* the text area */
    {
        WORD r = 0, w = 0;
        while (r + 2 <= text_size) {
            WORD v = *(WORD __far *)(text + r), n;
            if (!(v & 1)) {
                if (!v)
                    break;
                r += v;
                continue;
            }
            n = (v >> 1) < n_ent ? E(v >> 1)->c : 0;
            if ((v >> 1) < n_ent && MARKED(v >> 1)) {
                sh_move(text + w, text + r, n + 2);
                E(v >> 1)->d = w + 2;
                w += n + 2;
            }
            r += n + 2;
        }
        text_size = w;
    }
    /* the table's new size and its free list */
    k = last + 1;
    if (k <= 0xFFFF - GROW)
        k += GROW;
    if (k != n_ent) {
        WORD old = n_ent;
        set_entries(k);
        for (i = old; i < k; i++)
            sh_fill(E(i), 0, sizeof(ENT));
        n_ent = k;
    }
    free_head = 0;
    prev = 0;
    for (i = 0; i < n_ent; i++) {
        if (i <= last && MARKED(i))
            continue;
        if (prev)
            E(prev)->a = i;
        else
            free_head = i;
        E(i)->b = 0;
        prev = i;
    }
    if (prev)
        E(prev)->a = 0;
    GlobalUnlock(hm);
    GlobalFree(hm);
#undef MARK
#undef MARKED
}

static int load(HFILE f)
{
    DWORD hdr[6];
    WORD w[2];
    if (_lread(f, (LPSTR)hdr, sizeof hdr) != sizeof hdr || hdr[0] != MAGIC0 ||
        hdr[1] != MAGIC1 || hdr[4] > 0xFFFFL || hdr[4] < 1)
        return 0;
    if (_lread(f, (LPSTR)&hdr[5], 4) != 4 || _lread(f, (LPSTR)w, 4) != 4)
        return 0;
    if (hdr[5] > 0xFFFFL || !w[0] || w[0] >= hdr[4])
        return 0;
    n_ent = (WORD)hdr[4];
    n_buckets = w[0];
    free_head = w[1];
    if (!set_entries(n_ent))
        return 0;
    _llseek(f, hdr[2], 0);
    if (!read_all(f, E(0), (DWORD)n_ent * 8))
        return 0;
    text_size = 0;
    if (!text_room((WORD)hdr[5]))
        return 0;
    _llseek(f, hdr[2] + (DWORD)n_ent * 8, 0);
    if (!read_all(f, text, hdr[5]))
        return 0;
    text_size = (WORD)hdr[5];
    collect();
    return 1;
}

void reg_init(void)
{
    HFILE f;
    OFSTRUCT of;
    WORD n;
    ahincr = get_ahincr();
    n = GetWindowsDirectory(path, sizeof path - 12);
    if (n && path[n - 1] != '\\')
        path[n++] = '\\';
    LoadString(g_inst, IDS_REGDAT, path + n, sizeof path - n);
    f = OpenFile(path, &of, OF_READ);
    if (f == HFILE_ERROR) {
        /* no REG.DAT: an empty one is written at once (SH2) */
        empty_db();
        import_pending = 1;
        init_tick = GetTickCount();
        flush();
        return;
    }
    if (!load(f)) {
        /* UNVERIFIED: what Windows 3.1 does with a damaged REG.DAT; w16
           answers ERROR_BADDB and leaves the file alone */
        db_bad = 1;
    }
    _lclose(f);
}

/* ---- the API ---- */

LONG SHAPI RegOpenKey(HKEY k, LPCSTR sub, HKEY __far *res)
{
    WORD n;
    LONG r;
    if (!res)
        return ERROR_INVALID_PARAMETER;
    r = walk(k, sub, 0, &n);
    if (r)
        return r;
    *res = MAKELONG(n, 1);
    open_count++;
    return ERROR_SUCCESS;
}

LONG SHAPI RegCreateKey(HKEY k, LPCSTR sub, HKEY __far *res)
{
    WORD n;
    LONG r;
    if (!res)
        return ERROR_INVALID_PARAMETER;
    made = 0;
    r = walk(k, sub, 1, &n);
    if (!r) {
        /* open first: the new key is written when it is closed (SH1: R02) */
        *res = MAKELONG(n, 1);
        open_count++;
    }
    if (made)
        changed();
    return r;
}

/* closing only counts down: any handle closes, and closing more often
   than opening gives ERROR_INVALID_PARAMETER (SH1) */
LONG SHAPI RegCloseKey(HKEY k)
{
    (void)k;
    if (!open_count)
        return ERROR_INVALID_PARAMETER;
    if (!--open_count && dirty)
        flush();
    return ERROR_SUCCESS;
}

LONG SHAPI RegDeleteKey(HKEY k, LPCSTR sub)
{
    WORD n, parent, p;
    LONG r;
    const char __far *last;
    if (!sub || !*sub)
        return ERROR_BADKEY;
    r = walk(k, sub, 0, &n);
    if (r)
        return r;
    /* the parent: walk again without the last component */
    for (last = sub + sh_len(sub); last > sub && last[-1] != '\\'; last--)
        ;
    if (last > sub) {
        HGLOBAL h;
        char __far *s = sh_strdup(sub, &h);
        if (!s)
            return ERROR_OUTOFMEMORY;
        s[last - sub - 1] = 0;
        r = walk(k, s, 0, &parent);
        GlobalUnlock(h);
        GlobalFree(h);
        if (r)
            return r;
    } else {
        key_node(k, 0, &parent);
    }
    /* out of the parent's list */
    if (E(parent)->b == n) {
        E(parent)->b = E(n)->a;
    } else {
        for (p = E(parent)->b; p && E(p)->a != n; p = E(p)->a)
            ;
        if (p)
            E(p)->a = E(n)->a;
    }
    if (!ws_push(&doomed, n))
        delete_tree(n);
    changed();
    return ERROR_SUCCESS;
}

/* cbData is not used: the value is the string (SH1) */
LONG SHAPI RegSetValue(HKEY k, LPCSTR sub, DWORD type, LPCSTR data, DWORD cb)
{
    WORD n, t = 0, old;
    LONG r;
    (void)cb;
    if (type != REG_SZ)
        return ERROR_INVALID_PARAMETER;
    made = 0;
    r = walk(k, sub, 1, &n);
    if (r) {
        if (made)
            changed();
        return r;
    }
    if (data && *data) {
        t = intern(data, sh_len(data), 0);
        if (!t) {
            if (made)
                changed();
            return ERROR_OUTOFMEMORY;
        }
    }
    old = E(n)->d;
    E(n)->d = t;
    unref(old);
    changed();
    return ERROR_SUCCESS;
}

/* The value is copied as far as *cb allows (nothing when *cb is 0), and
   *cb becomes the length of what is then in buf, plus 1 (SH1: with
   *cb = 0, the length of the buffer's old contents). */
LONG reg_query(HKEY k, LPCSTR sub, LPSTR buf, LONG __far *cb)
{
    WORD n, v;
    LONG r = walk(k, sub, 0, &n);
    if (r)
        return r;
    v = E(n)->d;
    if (!buf) {
        if (cb)
            *cb = (v ? E(v)->c : 0) + 1;
        return ERROR_SUCCESS;
    }
    if (cb && *cb > 0) {
        WORD max = *cb > 0xFFFFL ? 0xFFFF : (WORD)*cb, len = v ? E(v)->c : 0;
        if (len > max - 1)
            len = max - 1;
        if (len)
            sh_move(buf, text + E(v)->d, len);
        buf[len] = 0;
    }
    if (cb)
        *cb = sh_len(buf) + 1;
    return ERROR_SUCCESS;
}

LONG SHAPI RegQueryValue(HKEY k, LPCSTR sub, LPSTR buf, LONG __far *cb)
{
    return reg_query(k, sub, buf, cb);
}

LONG SHAPI RegEnumKey(HKEY k, DWORD index, LPSTR buf, DWORD cb)
{
    WORD n, c;
    LONG r = key_node(k, 0, &n);
    if (db_bad)
        return ERROR_BADDB;
    if (r)
        return r;
    for (c = E(n)->b; c && index; c = E(c)->a)
        index--;
    if (!c)
        return ERROR_BADKEY;
    if (cb && buf) {
        ENT __far *t = E(E(c)->c);
        WORD len = t->c;
        if ((DWORD)len > cb - 1)
            len = (WORD)(cb - 1);
        sh_move(buf, text + t->d, len);
        buf[len] = 0;
    }
    return ERROR_SUCCESS;
}
