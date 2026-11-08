/*
 * resource.c - KERNEL resource services: FindResource, LoadResource,
 * LockResource, FreeResource, SizeofResource, AccessResource,
 * AllocResource, SetResourceHandler, the default resource handler, and
 * preloading a module's resources.
 *
 * The resource table is the NE file's (include/w16/ne.h), copied into the
 * module table right after the segment table (mdb.c); KERNEL fills in the
 * handler of each type and the handle and usage words of each resource.
 * Layout as published in Schulman et al., "Undocumented Windows" (1992),
 * chapter 5 (facts only); everything below was observed black-box on
 * Windows 3.1 with tests/apps/tk12.c, Standard and 386 Enhanced mode
 * identical (ARCHITECTURE.md 9.2 lists the findings):
 *  - an HRSRC is the offset of the resource's entry in the module table;
 *  - every type's handler is KERNEL's default handler after loading;
 *  - flag 0004h of a resource means its data is in memory: set when
 *    LoadResource finds or reads the data and when LockResource reloads
 *    it, kept when the block is discarded, cleared when it is freed; not
 *    set by preloading or by the default handler itself.
 */
#include "kernel.h"

#define RNF_LOADED      0x0004          /* resource flags: data in memory */
#define RNF_MOVEABLE    0x0010
#define RNF_PRELOAD     0x0040
#define RNF_DISCARD     0x1000

typedef u16 (__far __pascal *rsrc_proc)(u16 hmem, u16 hinst, u16 hrsrc);

u16  KAPI GlobalFlags(u16 h);
void FAR * KAPI GlobalLock(u16 h);
u16  KAPI GlobalUnlock(u16 h);
u16  KAPI DefResourceHandler(u16 hmem, u16 hinst, u16 hrsrc);

#define RT(m)       (NE_HDR(m)->rsrc_off)
#define RT_END(m)   (NE_HDR(m)->resname_off)
#define TYPE(m, o)  ((ne_rsrc_type FAR *)MK_FP((m)->ne_sel, (o)))
#define NAME(m, o)  ((ne_rsrc_name FAR *)MK_FP((m)->ne_sel, (o)))

static int has_table(module FAR *m)
{
    return RT_END(m) >= RT(m) + 4;
}

/* The type entries: first, and the one after t (0 at the end). */
static u16 type_first(module FAR *m)
{
    u16 t = RT(m) + 2;
    return has_table(m) && t + sizeof(ne_rsrc_type) <= RT_END(m) &&
           TYPE(m, t)->type_id ? t : 0;
}

static u16 type_next(module FAR *m, u16 t)
{
    u16 room = RT_END(m) - t - sizeof(ne_rsrc_type), n;
    if (TYPE(m, t)->count > room / sizeof(ne_rsrc_name))
        return 0;                       /* a broken table */
    n = t + sizeof(ne_rsrc_type) + TYPE(m, t)->count * sizeof(ne_rsrc_name);
    if (RT_END(m) - n < sizeof(ne_rsrc_type) || !TYPE(m, n)->type_id)
        return 0;
    return n;
}

/* The type entry holding resource entry r, 0 if r is not one. */
static u16 type_of(module FAR *m, u16 r)
{
    u16 t;
    for (t = type_first(m); t; t = type_next(m, t)) {
        u16 first = t + sizeof(ne_rsrc_type);
        u16 end = first + TYPE(m, t)->count * sizeof(ne_rsrc_name);
        if (r >= first && r < end && (r - first) % sizeof(ne_rsrc_name) == 0)
            return t;
    }
    return 0;
}

static u16 shift_of(module FAR *m)
{
    return *(u16 FAR *)MK_FP(m->ne_sel, RT(m));
}

static u32 size_of(module FAR *m, u16 r)
{
    return (u32)NAME(m, r)->length << shift_of(m);
}

/* ---- identifiers ---- */

/* An identifier as FindResource takes it: MAKEINTRESOURCE(n) or "#n"
   (decimal, observed: "#003" is 3, "#1x" is 1, "#65537" is 1, so the
   digits are read up to the first other character into 16 bits) is the
   integer n (*id = n | 8000h, as in the table); any other string is a
   name. Returns 0 for an integer id, 1 for a name. */
static int ident(const char FAR *s, u16 *id)
{
    if (!FP_SEG(s)) {
        *id = FP_OFF(s) | NE_RSRC_INTID;
        return FP_OFF(s) ? 0 : 2;       /* 0 matches nothing */
    }
    if (s[0] == '#') {
        u16 n = 0;
        for (s++; *s >= '0' && *s <= '9'; s++)
            n = n * 10 + (u16)(*s - '0');
        *id = n | NE_RSRC_INTID;
        return n ? 0 : 2;
    }
    return 1;
}

/* Does table id `tid` match identifier s? Names: observed, the query is
   upper-cased and compared exactly with the table's (counted) string, so
   a lower-case name in a table never matches. UNVERIFIED: letters other
   than a-z (Windows may use AnsiUpper). */
static int id_match(module FAR *m, u16 tid, const char FAR *s)
{
    u16 id, i;
    const u8 FAR *p;
    switch (ident(s, &id)) {
    case 0:
        return tid == id;
    case 1:
        if (tid & NE_RSRC_INTID)
            return 0;
        p = MK_FP(m->ne_sel, RT(m) + tid);
        for (i = 0; i < p[0]; i++) {
            u8 c = (u8)s[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            if (!c || c != p[1 + i])
                return 0;
        }
        return s[i] == 0;
    default:
        return 0;
    }
}

static u16 find_type(module FAR *m, const char FAR *type)
{
    u16 t;
    for (t = type_first(m); t; t = type_next(m, t))
        if (id_match(m, TYPE(m, t)->type_id, type))
            return t;
    return 0;
}

/* The module and entry of a resource handle in any module's table. */
static int find_handle(u16 h, module FAR **mp, u16 *rp)
{
    module FAR *m;
    u16 t, k;
    h &= ~1u;
    for (m = module_first; m; m = m->next)
        for (t = type_first(m); t; t = type_next(m, t))
            for (k = 0; k < TYPE(m, t)->count; k++) {
                u16 r = t + sizeof(ne_rsrc_type) + k * sizeof(ne_rsrc_name);
                if (NAME(m, r)->handle && (NAME(m, r)->handle & ~1u) == h) {
                    *mp = m;
                    *rp = r;
                    return 1;
                }
            }
    return 0;
}

static u16 call_handler(module FAR *m, u16 t, u16 hmem, u16 hinst, u16 r)
{
    rsrc_proc p = (rsrc_proc)TYPE(m, t)->reserved;
    if (!p)
        p = DefResourceHandler;         /* UNVERIFIED: a null handler */
    return p(hmem, hinst, r);
}

/* ---- the module's file ---- */

static u16 res_open(module FAR *m, u16 r)
{
    regs16 x;
    u32 pos = (u32)NAME(m, r)->offset << shift_of(m);
    u16 fh;

    x.ax = 0x3D00;      /* UNVERIFIED: sharing mode (observed: read-only) */
    x.dx = FP_OFF(m->path);
    x.ds = FP_SEG(m->path);
    x.es = 0;
    if (dos21(&x))
        return 0xFFFF;
    fh = x.ax;
    x.ax = 0x4200;
    x.bx = fh;
    x.cx = (u16)(pos >> 16);
    x.dx = (u16)pos;
    x.ds = x.es = 0;
    if (dos21(&x)) {
        x.ax = 0x3E00;
        x.bx = fh;
        x.ds = x.es = 0;
        dos21(&x);
        return 0xFFFF;
    }
    return fh;
}

/* Read resource r into the block whose first selector is sel. */
static int res_read(module FAR *m, u16 r, u16 sel)
{
    u16 fh = res_open(m, r);
    u32 left = size_of(m, r), done = 0;
    regs16 x;
    int ok = 1;

    if (fh == 0xFFFF)
        return 0;
    while (ok && left) {
        u16 n = left > 0x8000UL ? 0x8000 : (u16)left;
        x.ax = 0x3F00;
        x.bx = fh;
        x.cx = n;
        x.ds = sel + (u16)(done >> 16) * sel_inc;
        x.dx = (u16)done;
        x.es = 0;
        /* a resource may end before its rounded length (end of file) */
        if (dos21(&x))
            ok = 0;
        else if (x.ax < n)
            break;
        done += n;
        left -= n;
    }
    x.ax = 0x3E00;
    x.bx = fh;
    x.ds = x.es = 0;
    dos21(&x);
    return ok;
}

/* GMEM flags for a resource's memory, from its flags. Observed: a
   discardable resource's block has GlobalFlags 0100h, others 0. */
static u16 mem_flags(module FAR *m, u16 r)
{
    u16 f = NAME(m, r)->flags;
    if (f & RNF_DISCARD)
        return GMEM_MOVEABLE | GMEM_DISCARDABLE;
    return (f & RNF_MOVEABLE) ? GMEM_MOVEABLE : GMEM_FIXED;
}

/* ---- loading and freeing modules (loader.c) ---- */

/* Every type gets the default handler; PRELOAD resources are read (they
   get a handle, usage 0, flag 0004h not set: observed). */
void rsrc_init(module FAR *m)
{
    u16 t, k;
    for (t = type_first(m); t; t = type_next(m, t))
        TYPE(m, t)->reserved = (u32)(void __far *)DefResourceHandler;
    for (t = type_first(m); t; t = type_next(m, t))
        for (k = 0; k < TYPE(m, t)->count; k++) {
            u16 r = t + sizeof(ne_rsrc_type) + k * sizeof(ne_rsrc_name);
            NAME(m, r)->handle = NAME(m, r)->usage = 0;
            NAME(m, r)->flags &= ~RNF_LOADED;
            if (NAME(m, r)->flags & RNF_PRELOAD)
                NAME(m, r)->handle = DefResourceHandler(0, m->ne_sel, r);
        }
}

void rsrc_free(module FAR *m)
{
    u16 t, k;
    for (t = type_first(m); t; t = type_next(m, t))
        for (k = 0; k < TYPE(m, t)->count; k++) {
            u16 r = t + sizeof(ne_rsrc_type) + k * sizeof(ne_rsrc_name);
            if (NAME(m, r)->handle)
                GlobalFree(NAME(m, r)->handle);
            NAME(m, r)->handle = 0;
        }
}

/* ---- API ---- */

/* Observed: the offset of the resource's entry in the module table, or 0
   (also for a NULL hInstance). hInstance or hModule. */
u16 KAPI FindResource(u16 hinst, const char FAR *name, const char FAR *type) /* KERNEL.60 */
{
    module FAR *m = module_of(hinst);
    u16 t, k;
    if (!m || (t = find_type(m, type)) == 0)
        return 0;
    for (k = 0; k < TYPE(m, t)->count; k++) {
        u16 r = t + sizeof(ne_rsrc_type) + k * sizeof(ne_rsrc_name);
        if (id_match(m, NAME(m, r)->id, name))
            return r;
    }
    return 0;
}

/* Observed:
   - loaded already (a handle in the table): usage + 1 and the handle,
     also when the block is discarded (it stays so; LockResource reloads);
   - a discardable resource not loaded: a discarded block (GlobalFlags
     4100h, size 0) becomes its handle; the handler is not called;
   - any other: the type's handler is called with (0, hInstance as given,
     hRsrc); its result (0: LoadResource returns 0, nothing changes)
     becomes the handle. */
u16 KAPI LoadResource(u16 hinst, u16 r)                        /* KERNEL.61 */
{
    module FAR *m = module_of(hinst);
    ne_rsrc_name FAR *n;
    u16 t, h;

    if (!m || !r || (t = type_of(m, r)) == 0)
        return 0;
    n = NAME(m, r);
    if (n->handle) {
        n->usage++;
        if (!(GlobalFlags(n->handle) & GMEM_DISCARDED))
            n->flags |= RNF_LOADED;
        return n->handle;
    }
    if (n->flags & RNF_DISCARD) {
        h = galloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0, m->ne_sel, NULL);
        if (!h)
            return 0;
        n->handle = h;
        n->usage++;
        return h;
    }
    h = call_handler(m, t, 0, hinst, r);
    if (!h)
        return 0;
    n = NAME(m, r);
    n->handle = h;
    n->usage++;
    n->flags |= RNF_LOADED;
    return h;
}

/* Observed: GlobalLock, except that a discarded resource is reloaded
   first by its type's handler, called with (handle, hModule, hRsrc); if
   that returns 0, so does LockResource. */
void FAR * KAPI LockResource(u16 h)                            /* KERNEL.62 */
{
    module FAR *m;
    u16 r, t;
    if (!h)
        return NULL;
    if ((GlobalFlags(h) & GMEM_DISCARDED) && find_handle(h, &m, &r) &&
        (t = type_of(m, r)) != 0) {
        h = call_handler(m, t, h, m->ne_sel, r);
        if (!h)
            return NULL;
        NAME(m, r)->handle = h;
        NAME(m, r)->flags |= RNF_LOADED;
    }
    return GlobalLock(h);
}

/* Observed: usage - 1; while it stays above 0 the result is the handle,
   else 0. At 0 the block of a resource that is not discardable is freed
   (the table's handle becomes 0, flag 0004h is cleared); a discardable one
   stays until FreeResource is called again with the usage at 0. A handle
   that is no resource's (AllocResource, GlobalAlloc) is freed; 0 gives 0. */
u16 KAPI FreeResource(u16 h)                                   /* KERNEL.63 */
{
    module FAR *m;
    ne_rsrc_name FAR *n;
    u16 r;

    if (!h)
        return 0;
    if (!find_handle(h, &m, &r)) {
        GlobalFree(h);
        return 0;
    }
    n = NAME(m, r);
    if (n->usage) {
        if (--n->usage)
            return h;
        if (n->flags & RNF_DISCARD)
            return 0;
    }
    GlobalFree(n->handle);
    n->handle = 0;
    n->flags &= ~RNF_LOADED;
    return 0;
}

/* Observed: a new DOS handle of the module's file each time, read-only
   (writing gives error 5), at the resource's offset; -1 on failure
   (UNVERIFIED for the failure value). */
u16 KAPI AccessResource(u16 hinst, u16 r)                      /* KERNEL.64 */
{
    module FAR *m = module_of(hinst);
    if (!m || !r || !type_of(m, r))
        return 0xFFFF;
    return res_open(m, r);
}

/* Observed: the length in the table shifted by the resource shift (so
   rounded up to the alignment); 0 for hRsrc 0. */
u32 KAPI SizeofResource(u16 hinst, u16 r)                      /* KERNEL.65 */
{
    module FAR *m = module_of(hinst);
    if (!m || !r || !type_of(m, r))
        return 0;
    return size_of(m, r);
}

/* Observed: a new block of `size` bytes, or of the resource's size when
   size is 0 (not the larger of the two: 10 bytes for a 1008-byte resource
   gave GlobalSize 20h), discardable (GlobalFlags 0100h) when the resource
   is, else fixed or moveable as the resource; the table is not changed. */
u16 KAPI AllocResource(u16 hinst, u16 r, u32 size)             /* KERNEL.66 */
{
    module FAR *m = module_of(hinst);
    if (!m || !r || !type_of(m, r))
        return 0;
    if (!size)
        size = size_of(m, r);
    return galloc(mem_flags(m, r), size, m->ne_sel, NULL);
}

/* Observed: returns the type's previous handler (every type starts with
   the same one, KERNEL's default handler) and installs the new one; 0 if
   the module has no such type. Types as in FindResource. */
u32 KAPI SetResourceHandler(u16 hinst, const char FAR *type, u32 proc) /* KERNEL.67 */
{
    module FAR *m = module_of(hinst);
    u16 t;
    u32 old;
    if (!m || (t = find_type(m, type)) == 0)
        return 0;
    old = TYPE(m, t)->reserved;
    TYPE(m, t)->reserved = proc;
    return old;
}

/* The default resource handler (what SetResourceHandler returns first).
   Observed, called directly: with hMem 0 and a (discarded) handle in the
   table, it reads the data into that handle and returns it; usage and
   flag 0004h are left alone. Called by LockResource with the discarded
   handle, it reloads into that handle. Otherwise (UNVERIFIED) it
   allocates a block as AllocResource does. 0 if the data cannot be
   read. */
u16 KAPI DefResourceHandler(u16 hmem, u16 hinst, u16 r)
{
    module FAR *m = module_of(hinst);
    u16 h, sel;
    int fresh = 0;

    if (!m || !r || !type_of(m, r))
        return 0;
    if (!hmem)
        hmem = NAME(m, r)->handle;
    if (hmem) {
        h = GlobalReAlloc(hmem, size_of(m, r) ? size_of(m, r) : 1, 0);
        if (!h)
            return 0;
    } else {
        h = galloc(mem_flags(m, r), size_of(m, r), m->ne_sel, NULL);
        if (!h)
            return 0;
        fresh = 1;
    }
    sel = h | 1;
    if (size_of(m, r) && !res_read(m, r, sel)) {
        if (fresh)
            GlobalFree(h);
        return 0;
    }
    return h;
}
