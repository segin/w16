/*
 * tk78l.c - TK78's loader (with tk78s.asm the program's first segment,
 * TK78L_TEXT): BootApp, LoadAppSeg and ExitProc of a self-loading program
 * as the Windows 3.1 SDK describes them (Programmer's Reference Vol. 1
 * ch. 24), each logging what KERNEL hands it and what KERNEL's loader
 * procedures (the table's MyAlloc, EntryAddrProc, SetOwner) give back.
 * Compiled with -zc -zu -zdf: no data segment is needed (BootApp runs
 * before the program's data segment exists); constants live here.
 * See tk78.c for the log.
 */
#include <windows.h>

typedef unsigned short W;
typedef unsigned char B;
#define MK_FP(s, o) ((void __far *)(((DWORD)(s) << 16) | (W)(o)))

extern char __far ldr_table[];
UINT WINAPI AllocCStoDSAlias(UINT);
extern DWORD __cdecl kcall(DWORD fn, W __far *out, W n, W a1, W a2, W a3);
extern W __cdecl lar(W sel);
extern W __cdecl lsl(W sel);
extern W __cdecl get_cs(void);
extern DWORD __cdecl dos_pos(W h);
extern void __cdecl log_line(const char __far *s, W n);

#define TBL(o) (*(W __far *)(ldr_table + (o)))
#define TBLD(o) (*(DWORD __far *)(ldr_table + (o)))
#define MW(h, o) (*(W __far *)MK_FP(h, o))
#define MB(h, o) (*(B __far *)MK_FP(h, o))
#define SEGE(h, i) ((W)(MW(h, 0x22) + 10 * ((i) - 1)))

typedef struct { char s[160]; W n; } line;

static void put(line __far *l, const char __far *s)
{
    while (*s && l->n < sizeof l->s - 3)
        l->s[l->n++] = *s++;
}

static void hex(line __far *l, W v)
{
    int i;
    for (i = 3; i >= 0; i--)
        l->s[l->n++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
}

static void sp(line __far *l)
{
    l->s[l->n++] = ' ';
}

static void end(line __far *l)
{
    l->s[l->n++] = '\r';
    l->s[l->n++] = '\n';
    log_line(l->s, l->n);
    l->n = 0;
}

/* "name=v" */
static void obs(line __far *l, const char __far *name, W v)
{
    put(l, name);
    put(l, "=");
    hex(l, v);
    end(l);
}

static void text(line __far *l, const char __far *s)
{
    put(l, s);
    end(l);
}

/* the module name of hModule h (its resident names' first) */
static void modname(line __far *l, W h)
{
    W o, k, n;
    if (!h || lar(h) == 0xFFFF || MW(h, 0) != 0x454E) {
        put(l, "?");
        return;
    }
    o = MW(h, 0x26);
    n = MB(h, o);
    for (k = 0; k < n && k < 8; k++)
        l->s[l->n++] = MB(h, o + 1 + k);
}

static W modh(const char __far *name)
{
    return GetModuleHandle(name);
}

/* ---- loading a segment (the SDK's LoadAppSeg) ---- */

/* what the far pointer of an import resolves to */
static DWORD import_of(W hb, W modix, W ord, int byname)
{
    W hm, o, n, k;
    char nm[64];
    if (!modix || modix > MW(hb, 0x1E))
        return 0;
    hm = MW(hb, MW(hb, 0x28) + 2 * (modix - 1));
    if (!byname)
        return (DWORD)GetProcAddress(hm, MAKEINTRESOURCE(ord));
    o = MW(hb, 0x2A) + ord;
    n = MB(hb, o);
    for (k = 0; k < n && k < sizeof nm - 1; k++)
        nm[k] = MB(hb, o + 1 + k);
    nm[k] = 0;
    return (DWORD)GetProcAddress(hm, nm);
}

static W sel_of(W hb, W seg)
{
    W h;
    if (seg == 1)
        return get_cs();
    h = MW(hb, SEGE(hb, seg) + 8);
    return HIWORD(GlobalHandle(h));
}

/* load segment seg of hb from hFile: memory, data, relocations; returns
   its selector, 0 on failure */
static W do_load(W hb, W hf, W seg)
{
    W e = SEGE(hb, seg), h = MW(hb, e + 8), flags = MW(hb, e + 4);
    W flen = MW(hb, e + 2), min = MW(hb, e + 6), shift = MW(hb, 0x32);
    DWORD pos = MW(hb, e), size;
    W sel, wsel, k, cnt;
    B __far *p;
    if (!shift)
        shift = 9;
    for (k = 0; k < shift; k++)
        pos <<= 1;
    size = min ? min : 0x10000UL;
    if (seg == MW(hb, 0x0E))
        size += MW(hb, 0x10) + MW(hb, 0x12);
    if (size > 0xFFF0UL)
        size = 0xFFF0UL;
    if (!HIWORD(GlobalHandle(h)) || GlobalSize(h) < size) {
        /* (a handle only: as Blinker does, GlobalReAlloc with flags 0) */
        W g = GlobalReAlloc(h, size, 0);
        line l;
        l.n = 0;
        put(&l, "realloc(seg");
        hex(&l, seg);
        put(&l, ")=");
        put(&l, !g ? "0" : g == h ? "h" : g == (h | 1) ? "h|1" : "other");
        end(&l);
        put(&l, "~realloc ar gflags ");
        hex(&l, lar(HIWORD(GlobalHandle(h))));
        sp(&l);
        hex(&l, GlobalFlags(h));
        end(&l);
        if (!g)
            return 0;
    }
    sel = HIWORD(GlobalHandle(h));
    if (!sel)
        return 0;
    wsel = (flags & 1) ? sel : AllocCStoDSAlias(sel);
    {
        line l;
        l.n = 0;
        put(&l, "~load(seg");
        hex(&l, seg);
        put(&l, ") h sel alias size ");
        hex(&l, h);
        sp(&l);
        hex(&l, sel);
        sp(&l);
        hex(&l, wsel);
        sp(&l);
        hex(&l, (W)size);
        end(&l);
    }
    if (!wsel)
        return 0;
    p = MK_FP(wsel, 0);
    for (k = 0; k < (W)size; k++)
        p[k] = 0;
    if (MW(hb, e) && flen) {
        if (_llseek(hf, pos, 0) != (LONG)pos || _lread(hf, p, flen) != flen)
            goto fail;
    }
    if (flags & 0x0100) {
        B r[8 * 8];
        W o[9];
        if (_lread(hf, &cnt, 2) != 2)
            goto fail;
        while (cnt) {
            W m = cnt > 8 ? 8 : cnt, i;
            if (_lread(hf, r, 8 * m) != 8 * m)
                goto fail;
            for (i = 0; i < m; i++) {
                B FAR *q = r + 8 * i;
                B st = q[0], ft = q[1];
                W off = *(W *)(q + 2), t1 = *(W *)(q + 4), t2 = *(W *)(q + 6);
                DWORD a;
                switch (ft & 3) {
                case 0:
                    if ((t1 & 0xFF) == 0xFF)
                        a = kcall(TBLD(0x14), o, 2, hb, t2, 0);
                    else
                        a = MAKELONG(t2, sel_of(hb, t1 & 0xFF));
                    break;
                case 1:
                    a = import_of(hb, t1, t2, 0);
                    break;
                case 2:
                    a = import_of(hb, t1, t2, 1);
                    break;
                default:
                    continue;   /* OS fixups: none in this probe */
                }
                if (!a)
                    goto fail;
                for (;;) {
                    W nx = *(W __far *)(p + off);
                    if (ft & 4) {
                        if (st == 2)
                            *(W __far *)(p + off) += HIWORD(a);
                        else if (st == 3) {
                            *(W __far *)(p + off) += LOWORD(a);
                            *(W __far *)(p + off + 2) += HIWORD(a);
                        } else if (st == 5)
                            *(W __far *)(p + off) += LOWORD(a);
                        else if (st == 0)
                            p[off] += (B)LOWORD(a);
                        break;
                    }
                    if (st == 2)
                        *(W __far *)(p + off) = HIWORD(a);
                    else if (st == 3) {
                        *(W __far *)(p + off) = LOWORD(a);
                        *(W __far *)(p + off + 2) = HIWORD(a);
                    } else if (st == 5)
                        *(W __far *)(p + off) = LOWORD(a);
                    else {
                        p[off] = (B)LOWORD(a);
                        break;
                    }
                    if (nx == 0xFFFF)
                        break;
                    off = nx;
                }
            }
            cnt -= m;
        }
    }
    /* (the alias is not freed: the compiled code may still hold it in DS,
       which a far call's return would then reload) */
    MW(hb, e + 4) |= 6;                 /* allocated, loaded (SDK) */
    return sel;
fail:
    return 0;
}

/* ---- the observations ---- */

static void regs(line __far *l, const char __far *name, W __far *r, W n)
{
    W i;
    put(l, "~");
    put(l, name);
    put(l, "=");
    for (i = 0; i < n; i++) {
        hex(l, r[i]);
        if (i + 1 < n)
            sp(l);
    }
    end(l);
}

/* the offset of entry ord's 5-byte record in module table h's entry
   table (in-memory form: bundles of {first - 1, last, next}), 0 if none */
static W entry_rec(W h, W ord)
{
    W b = MW(h, 4);
    while (b) {
        if (ord > MW(h, b) && ord <= MW(h, b + 2))
            return b + 6 + 5 * (ord - MW(h, b) - 1);
        b = MW(h, b + 4);
    }
    return 0;
}

/* EntryAddrProc(h, ord) through the table, against GetProcAddress, and
   the registers it leaves: BX (the entry's record?), CX (AX | DX?), ES
   (the module table?), ZF and CF */
static void eap(line __far *l, W hb, const char __far *mn, W h, W ord)
{
    W o[9], er = entry_rec(h, ord);
    DWORD a = kcall(TBLD(0x14), o, 2, h, ord, 0), g = 0;
    if (h != hb)
        g = (DWORD)GetProcAddress(h, MAKEINTRESOURCE(ord));
    put(l, "eap(");
    put(l, mn);
    put(l, ",");
    hex(l, ord);
    put(l, ")=");
    if (!a)
        put(l, "0");
    else if (h != hb && g == a)
        put(l, "gpa");
    else if (h == hb)
        put(l, "own");
    else {
        put(l, "other gpa ");
        hex(l, HIWORD(g));
        put(l, ":");
        hex(l, LOWORD(g));
    }
    put(l, " bx ");
    put(l, !er ? (o[1] == 0x6B6B ? "kept" : "?") : o[1] == er ? "rec" : o[1] == er + 3 ? "rec+3" : "other");
    put(l, " cx ");
    put(l, o[2] == (LOWORD(a) | HIWORD(a)) ? "ax|dx" : o[2] == 0x6B6B ? "kept" : "other");
    put(l, " es ");
    put(l, o[6] == h ? "h" : "other");
    put(l, " zf ");
    hex(l, (o[8] >> 6) & 1);
    put(l, " cf ");
    hex(l, o[8] & 1);
    put(l, " ar ");
    hex(l, a ? lar(HIWORD(a)) & 0xFF : 0);
    end(l);
    put(l, "~eap(");
    put(l, mn);
    put(l, ",");
    hex(l, ord);
    put(l, ") ");
    hex(l, HIWORD(a));
    put(l, ":");
    hex(l, LOWORD(a));
    put(l, " bx cx si di es fl rec ");
    hex(l, o[1]);
    sp(l);
    hex(l, o[2]);
    sp(l);
    hex(l, o[4]);
    sp(l);
    hex(l, o[5]);
    sp(l);
    hex(l, o[6]);
    sp(l);
    hex(l, o[8]);
    sp(l);
    hex(l, er);
    end(l);
}

/* the segment table entries */
static void segtab(line __far *l, const char __far *when, W hb)
{
    W i, n = MW(hb, 0x1C);
    for (i = 1; i <= n; i++) {
        W e = SEGE(hb, i), h = MW(hb, e + 8);
        put(l, when);
        put(l, ".seg");
        hex(l, i);
        put(l, "=");
        hex(l, MW(hb, e));
        sp(l);
        hex(l, MW(hb, e + 2));
        sp(l);
        hex(l, MW(hb, e + 4));
        sp(l);
        hex(l, MW(hb, e + 6));
        put(l, " h ");
        if (!h)
            put(l, "0");
        else if (i == 1 && h == get_cs())
            put(l, "cs");
        else if (i == 1 && (h | 1) == get_cs())
            put(l, "cs-1");
        else
            put(l, (h & 1) ? "odd" : "even");
        /* (not right after BootApp: w16 makes code segments executable
           once all are loaded, 3.1 as MyAlloc and GlobalReAlloc go) */
        if (when[1] != 'o') {
            put(l, " ar ");
            hex(l, h ? lar(h) & 0xFF : 0);
        }
        end(l);
    }
}

/* MyAlloc(flags, size, elem) through the table: AX and DX, their
   relation (3.1: AX the selector, DX the handle; for 0 bytes AX 0),
   GlobalHandle of each, the selector's access rights, GlobalSize */
static DWORD myalloc(line __far *l, const char __far *name, W flags, W size, W elem)
{
    W o[9];
    DWORD a = kcall(TBLD(0x10), o, 3, flags, size, elem);
    W ax = LOWORD(a), dx = HIWORD(a), h = dx ? dx : ax;
    DWORD g = h ? GlobalHandle(h) : 0, s = h ? GlobalSize(h) : 0;
    put(l, name);
    put(l, "=");
    if (!a)
        put(l, "0");
    else {
        put(l, "ax ");
        put(l, !ax ? "0" : ax == (dx | 1) ? "dx|1" : ax == (dx & ~1) ? "dx&~1" : "other");
        /* (GlobalHandle of a discarded block's handle: 3.1 gives the
           handle, w16 0; not a self-loader matter) */
        if (ax) {
            put(l, " ghandle(dx) ");
            put(l, LOWORD(g) == h ? "dx" : "other");
            put(l, HIWORD(g) == 0 ? " 0" : HIWORD(g) == ax ? " ax" : " other");
            put(l, " ar ");
            hex(l, lar(ax) & 0xFF);
        }
        put(l, " size ");
        put(l, !s ? "0" : s >= ((DWORD)size << elem) ? "enough" : "less");
    }
    end(l);
    put(l, "~");
    put(l, name);
    put(l, " dx:ax ");
    hex(l, dx);
    put(l, ":");
    hex(l, ax);
    put(l, " ar(dx) ");
    hex(l, lar(dx));
    put(l, " gflags ");
    hex(l, h ? GlobalFlags(h) : 0);
    put(l, " ghandle ");
    hex(l, HIWORD(g));
    put(l, ":");
    hex(l, LOWORD(g));
    put(l, " size ");
    hex(l, HIWORD(s));
    hex(l, LOWORD(s));
    put(l, " bx cx es ");
    hex(l, o[1]);
    sp(l);
    hex(l, o[2]);
    sp(l);
    hex(l, o[6]);
    end(l);
    return a;
}

W __cdecl boot_c(W __far *r, W hb, W hf)
{
    line l;
    W i, cs = get_cs(), n, hk, hu, hg, defer = 0;
    DWORD pos;
    l.n = 0;
    text(&l, "T BootApp");
    regs(&l, "boot.regs ax bx cx dx si di ds es ss sp bp fl ip cs", r, 14);
    obs(&l, "boot.hblock_is_module", hb == GetModuleHandle("TK78"));
    obs(&l, "boot.ds_is_hblock", r[6] == hb);
    obs(&l, "boot.es_is_hblock", r[7] == hb);
    obs(&l, "boot.ss_is_hblock", r[8] == hb);
    obs(&l, "boot.ds_is_cs", r[6] == cs);
    pos = dos_pos(hf);
    put(&l, "boot.filepos=");
    hex(&l, HIWORD(pos));
    hex(&l, LOWORD(pos));
    end(&l);
    obs(&l, "boot.cs_ar", lar(cs) & 0xFF);
    obs(&l, "~boot.cs_lsl", lsl(cs));
    obs(&l, "~boot.si", r[4]);
    obs(&l, "boot.hblock_ar", lar(hb) & 0xFF);
    obs(&l, "boot.cx_is_cs", r[2] == cs);
    obs(&l, "boot.es_is_cs", r[7] == cs);
    obs(&l, "boot.dx_is_seg1", r[3] == MW(hb, SEGE(hb, 1) + 8));
    obs(&l, "boot.table+0E_is_ds", TBL(0x0E) == r[6]);
    if (TBL(0x0E) != 0x5A5A && lar(TBL(0x0E)) != 0xFFFF) {
        W k, s = TBL(0x0E), o = TBL(0x0C);
        put(&l, "~boot.*table+0C=");
        for (k = 0; k < 16; k++) {
            hex(&l, MW(s, o + 2 * k));
            sp(&l);
        }
        end(&l);
    }
    /* the loader data table: every word KERNEL changed */
    for (i = 0; i < 0x40; i += 2) {
        W v = TBL(i), want = 0x5A5A;
        const char __far *what;
        if (i == 0)
            want = 0x3041;
        if (i == 4 || i == 8 || i == 0x18)
            continue;
        if (i == 6 || i == 0x0A || i == 0x1A) {
            obs(&l, i == 6 ? "boot.table+06_is_cs" : i == 0x0A ? "boot.table+0A_is_cs" :
                "boot.table+1A_is_cs", v == cs);
            continue;
        }
        what = v == want ? "file" : "changed";
        put(&l, "boot.table+");
        hex(&l, i);
        put(&l, "=");
        put(&l, what);
        end(&l);
        if (v != want) {
            put(&l, "~boot.table+");
            hex(&l, i);
            put(&l, "=");
            hex(&l, v);
            end(&l);
        }
    }
    /* the module table */
    obs(&l, "boot.mt.usage", MW(hb, 2));
    obs(&l, "boot.mt.flags", MW(hb, 0x0C));
    obs(&l, "boot.mt.autodata", MW(hb, 0x0E));
    obs(&l, "boot.mt.heap", MW(hb, 0x10));
    obs(&l, "boot.mt.stack", MW(hb, 0x12));
    obs(&l, "boot.mt.dgroup_is_entry", MW(hb, 8) == SEGE(hb, MW(hb, 0x0E)));
    obs(&l, "boot.mt.segcount", MW(hb, 0x1C));
    obs(&l, "boot.mt.modrefs", MW(hb, 0x1E));
    obs(&l, "boot.mt.align", MW(hb, 0x32));
    segtab(&l, "boot", hb);
    n = MW(hb, 0x1E);
    for (i = 0; i < n; i++) {
        put(&l, "boot.modref");
        hex(&l, i + 1);
        put(&l, "=");
        modname(&l, MW(hb, MW(hb, 0x28) + 2 * i));
        end(&l);
    }

    /* KERNEL's procedures */
    hk = modh("KERNEL");
    hu = modh("USER");
    hg = modh("GDI");
    eap(&l, hb, "KERNEL", hk, 49);
    eap(&l, hb, "KERNEL", hk, 90);
    eap(&l, hb, "KERNEL", hk, 3);
    eap(&l, hb, "KERNEL", hk, 114);
    eap(&l, hb, "KERNEL", hk, 178);
    eap(&l, hb, "KERNEL", hk, 999);
    eap(&l, hb, "USER", hu, 1);
    eap(&l, hb, "USER", hu, 0x6F);
    eap(&l, hb, "GDI", hg, 0x42);
    eap(&l, hb, "own", hb, 1);
    eap(&l, hb, "own", hb, 2);
    eap(&l, hb, "own", hb, 3);
    {
        DWORD a;
        a = myalloc(&l, "myalloc(code,100,0)", 0x1D50, 0x100, 0);
        if (a)
            GlobalFree(HIWORD(a) ? HIWORD(a) : LOWORD(a));
        a = myalloc(&l, "myalloc(data,10,4)", 0x0C51, 0x10, 4);
        if (a)
            GlobalFree(HIWORD(a) ? HIWORD(a) : LOWORD(a));
        a = myalloc(&l, "myalloc(movdata,100,0)", 0x0C51 | 0x10, 0x100, 0);
        if (a)
            GlobalFree(HIWORD(a) ? HIWORD(a) : LOWORD(a));
    }

    /* the program's segments: the automatic data segment first (SDK), then
       the code; the entry point's segment is left allocated (a handle
       only) but not loaded, for KERNEL to load */
    n = MW(hb, 0x1C);
    defer = GetPrivateProfileInt("tk78", "defer", 0, "C:\\TK78.INI") ? MW(hb, 0x16) : 0;
    {
        W ad = MW(hb, 0x0E), e = SEGE(hb, ad), o[9];
        DWORD size = (DWORD)MW(hb, e + 6) + MW(hb, 0x10) + MW(hb, 0x12), a;
        a = myalloc(&l, "myalloc(dgroup)", MW(hb, e + 4), (W)size, 0);
        MW(hb, e + 8) = HIWORD(a) ? HIWORD(a) : LOWORD(a);
        MW(hb, e + 4) |= 2;
        kcall(TBLD(0x24), o, 2, MW(hb, e + 8), hb, 0);
        regs(&l, "setowner(dgroup) ax bx cx dx", o, 4);
        obs(&l, "boot.dgroup_loaded", do_load(hb, hf, ad) != 0);
        if (GetPrivateProfileInt("tk78", "dgmin", 0, "C:\\TK78.INI"))
            MW(hb, e + 6) = (W)size;
    }
    for (i = n; i >= 2; i--) {
        W e = SEGE(hb, i), o[9];
        DWORD a;
        if (MW(hb, e + 4) & 1)
            continue;
        a = myalloc(&l, i == defer ? "myalloc(code,0,0) deferred" : "myalloc(code,0,0)",
                    MW(hb, e + 4), 0, 0);
        MW(hb, e + 8) = HIWORD(a) ? HIWORD(a) : LOWORD(a);
        MW(hb, e + 4) |= 2;
        kcall(TBLD(0x24), o, 2, MW(hb, e + 8), hb, 0);
        if (i == defer)
            continue;
        put(&l, "boot.loaded seg");
        hex(&l, i);
        put(&l, "=");
        hex(&l, do_load(hb, hf, i) != 0);
        end(&l);
    }
    segtab(&l, "booted", hb);
    pos = dos_pos(hf);
    put(&l, "~boot.filepos at the end=");
    hex(&l, HIWORD(pos));
    hex(&l, LOWORD(pos));
    end(&l);
    {
        /* BootApp's AX (C:\\TK78.INI [tk78] ret=; F001h hBlock, F002h the
           automatic data segment's handle) */
        W rv = GetPrivateProfileInt("tk78", "ret", 0xF001, "C:\\TK78.INI");
        if (rv == 0xF001)
            rv = hb;
        else if (rv == 0xF002)
            rv = MW(hb, MW(hb, 8) + 8);
        obs(&l, "~boot.returns", rv);
        text(&l, "T BootApp returns");
        return rv;
    }
}

W __cdecl load_c(W __far *r, W hb, W hf, W seg)
{
    line l;
    W sel, e;
    DWORD pos;
    l.n = 0;
    text(&l, "T LoadAppSeg");
    regs(&l, "load.regs ax bx cx dx si di ds es ss sp bp fl ip cs", r, 14);
    obs(&l, "load.seg", seg);
    obs(&l, "load.hblock_is_module", hb == GetModuleHandle("TK78"));
    obs(&l, "load.hfile_is_ffff", hf == 0xFFFF);
    obs(&l, "load.ds_is_hblock", r[6] == hb);
    e = SEGE(hb, seg);
    obs(&l, "load.segflags", MW(hb, e + 4));
    put(&l, "load.handle=");
    {
        W h = MW(hb, e + 8);
        DWORD g = h ? GlobalHandle(h) : 0;
        put(&l, !h ? "0" : (h & 1) ? "odd" : "even");
        put(&l, " ghandle sel ");
        put(&l, !HIWORD(g) ? "0" : HIWORD(g) == (h | 1) ? "h|1" : "other");
        put(&l, " size ");
        hex(&l, h ? (W)GlobalSize(h) : 0);
    }
    end(&l);
    if (hf == 0xFFFF) {
        text(&l, "load: no file, returns 0");
        return 0;
    }
    pos = dos_pos(hf);
    put(&l, "~load.filepos=");
    hex(&l, HIWORD(pos));
    hex(&l, LOWORD(pos));
    end(&l);
    obs(&l, "load.file_ok", pos != 0xFFFFFFFFUL);
    sel = do_load(hb, hf, seg);
    obs(&l, "load.loaded", sel != 0);
    return sel;
}

void __cdecl exit_c(W __far *r, W hb)
{
    line l;
    l.n = 0;
    text(&l, "T ExitProc");
    obs(&l, "exit.hblock_is_module", hb == GetModuleHandle("TK78"));
    (void)r;
}

/* exported from the loader segment (ordinal 2) */
void FAR PASCAL tk78_ldrexp(void)
{
}

/* EntryAddrProc again, from the program (tk78.c), with every segment
   loaded */
void __cdecl late_eap(void)
{
    line l;
    W hb = GetModuleHandle("TK78");
    l.n = 0;
    text(&l, "T late");
    eap(&l, hb, "KERNEL", modh("KERNEL"), 49);
    eap(&l, hb, "KERNEL", modh("KERNEL"), 90);
    eap(&l, hb, "own", hb, 1);
    eap(&l, hb, "own", hb, 2);
    segtab(&l, "late", hb);
    obs(&l, "late.mt.flags", MW(hb, 0x0C));
    obs(&l, "late.table+10_kept", TBL(0x12) != 0x5A5A);
}
