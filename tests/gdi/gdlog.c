/*
 * gdlog.c - the recording side of the logging stand-ins (GD1: the display
 * driver, GD2: GDI), see gdlog.asm and tools/mkgdlog.py.
 *
 * Every call is written to a text log (GDL_FILE) as
 *     >seq NAME args...  @caller
 *       .what: decoded or hex-dumped data the arguments point to
 *     <seq NAME = DX:AX
 *       .what: data the call filled in
 * indented by the nesting depth. Arguments are printed as w = word (hex),
 * l = dword, p = far pointer (sel:off). Structures are decoded from the
 * Windows 3.1 DDK documentation (Device Driver Adaptation Guide, the
 * reference page of each function and structure) for GD1, and from the
 * Windows 3.1 SDK reference for GD2.
 *
 * Black-box observation only: the stand-in sees what crosses the module
 * boundary, nothing of the real module's code.
 */
#include <windows.h>

typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned char u8;

/* the table tools/mkgdlog.py generates (far pointers: large model) */
struct ent { u16 ord, mode, nbytes; const char far *name, far *args; };
extern u16 __cdecl gdl_count;
extern struct ent __cdecl gdl_tab[];
extern void __cdecl far gdl_append(const char far *path, const char far *buf, u16 len);

#ifndef GDL_FILE
#define GDL_FILE "C:\\GDLOG.TXT"
#endif

static char buf[4096];
static u16 blen;
static u16 depth;
static u32 seq;
static int busy;                /* re-entered while recording: no log */

#define SIDE 64
/* the caller's return address and a copy of the arguments (the callee
   pops them, and the stack below the caller's SP is reused at once) */
#define MAXARGW 24
static struct { u16 ip, cs, idx; u32 seq; u16 a[MAXARGW]; } side[SIDE];

static void flush(void)
{
    if (blen) {
        gdl_append(GDL_FILE, buf, blen);
        blen = 0;
    }
}

static void put(const char *s)
{
    while (*s) {
        if (blen >= sizeof buf - 2)
            flush();
        buf[blen++] = *s++;
    }
}

static void putc_(char c)
{
    char s[2];
    s[0] = c;
    s[1] = 0;
    put(s);
}

static void hex(u32 v, int digits)
{
    char s[9];
    int i;
    for (i = digits - 1; i >= 0; i--) {
        s[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    s[digits] = 0;
    put(s);
}

static void dec(int v)
{
    char s[8];
    int i = 7;
    unsigned u = v < 0 ? -v : v;
    s[i] = 0;
    do {
        s[--i] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (v < 0)
        s[--i] = '-';
    put(s + i);
}

static void nl(void)
{
    put("\r\n");
}

static void indent(void)
{
    u16 i;
    for (i = 0; i < depth; i++)
        put("  ");
}

static void far *ptr(u16 far *a)
{
    return (void far *)(((u32)a[1] << 16) | a[0]);
}

static int readable(const void far *p, u16 n)
{
    return p && !IsBadReadPtr(p, n);
}

/* .label: n bytes in hex */
static void dump(const char *label, const void far *p, u16 n)
{
    const u8 far *b = p;
    u16 i;
    indent();
    put("  .");
    put(label);
    put(":");
    if (!p) {
        put(" null");
        nl();
        return;
    }
    if (!readable(p, n)) {
        put(" unreadable");
        nl();
        return;
    }
    for (i = 0; i < n; i++) {
        if (i && i % 32 == 0) {
            nl();
            indent();
            put("   ");
        }
        putc_(' ');
        hex(b[i], 2);
    }
    nl();
}

static void dumpw(const char *label, const void far *p, u16 nwords)
{
    const short far *w = p;
    u16 i;
    indent();
    put("  .");
    put(label);
    put(":");
    if (!p || !readable(p, nwords * 2)) {
        put(p ? " unreadable" : " null");
        nl();
        return;
    }
    for (i = 0; i < nwords; i++) {
        if (i && i % 16 == 0) {
            nl();
            indent();
            put("   ");
        }
        putc_(' ');
        dec(w[i]);
    }
    nl();
}

static void str(const char *label, const char far *s, int n)
{
    int i;
    indent();
    put("  .");
    put(label);
    put(": ");
    if (!s || !readable(s, n < 0 ? 1 : n)) {
        put(s ? "unreadable" : "null");
        nl();
        return;
    }
    putc_('"');
    for (i = 0; n < 0 ? s[i] != 0 && i < 128 : i < n; i++) {
        u8 c = s[i];
        if (c < 32 || c >= 127 || c == '"' || c == '\\') {
            put("\\x");
            hex(c, 2);
        } else
            putc_(c);
    }
    putc_('"');
    nl();
}

static void rect(const char *label, const void far *p)
{
    dumpw(label, p, 4);
}

/* the argument words of the call, by parameter number (declaration
   order); Pascal pushes left to right, so the last parameter is lowest */
static u16 far *argp(u16 far *args, const char *spec, int k)
{
    int i, off = 0, n = 0;
    for (i = 0; spec[i]; i++)
        n += spec[i] == 'w' ? 1 : 2;
    for (i = 0; i <= k; i++)
        off += spec[i] == 'w' ? 1 : 2;
    return args + (n - off);
}

#define W(k) (*argp(args, e->args, k))
#define L(k) (*(u32 far *)argp(args, e->args, k))
#define P(k) ((void far *)ptr(argp(args, e->args, k)))

#ifdef GD_DISPLAY
/* a PDEVICE (first word nonzero) or a PBITMAP (first word 0) */
static void pdev(const char *label, void far *p)
{
    if (p && readable(p, 2) && *(u16 far *)p == 0)
        dump(label, p, 32);
    else
        dump(label, p, 8);
}

static void fontinfo(void far *p)
{
    dump("font", p, 0x41);
}

static void drawmode(void far *p)
{
    dump("dm", p, 32);
}

static int same(const char far *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void enter_detail(struct ent *e, u16 far *args)
{
    const char *n = e->name;
    if (same(n, "OUTPUT")) {
        int style = W(1), cnt = W(2);
        pdev("dev", P(0));
        dumpw("pts", P(3), cnt > 0 && cnt < 200 ? cnt * 2 : 2);
        dump("pen", P(4), 16);
        dump("brush", P(5), 16);
        drawmode(P(6));
        rect("clip", P(7));
        (void)style;
    } else if (same(n, "BITBLT")) {
        pdev("dst", P(0));
        if (P(3))
            pdev("src", P(3));
        dump("brush", P(9), 16);
        drawmode(P(10));
    } else if (same(n, "EXTTEXTOUT") || same(n, "STRBLT")) {
        int cnt = (int)W(5);
        pdev("dev", P(0));
        rect("clip", P(3));
        str("str", P(4), cnt < 0 ? -cnt : cnt);
        fontinfo(P(6));
        drawmode(P(7));
        dump("xform", P(8), 32);
        if (same(n, "EXTTEXTOUT")) {
            if (P(9))
                dumpw("dx", P(9), cnt < 0 ? -cnt : cnt);
            rect("opaque", P(10));
        }
    } else if (same(n, "REALIZEOBJECT")) {
        int style = (int)W(1);
        int as = style < 0 ? -style : style;
        pdev("dev", P(0));
        dump("in", P(2), as == 1 ? 10 : as == 2 ? 12 : as == 3 ? 50 : as == 5 ? 32 : 16);
        if (style < 0)
            dump("out", P(3), 16);
        if (as == 3)
            dump("xform", P(4), 32);
    } else if (same(n, "COLORINFO")) {
        pdev("dev", P(0));
    } else if (same(n, "PIXEL")) {
        pdev("dev", P(0));
        if (P(4))
            drawmode(P(4));
    } else if (same(n, "ENABLE")) {
        dump("dev", P(0), 16);
        str("type", P(2), -1);
        str("file", P(3), -1);
        dump("data", P(4), 16);
    } else if (same(n, "CONTROL")) {
        pdev("dev", P(0));
        dump("in", P(2), 16);
    } else if (same(n, "GETCHARWIDTH")) {
        pdev("dev", P(0));
        fontinfo(P(4));
        drawmode(P(5));
    } else if (same(n, "FASTBORDER")) {
        rect("rect", P(0));
        pdev("dev", P(4));
        dump("brush", P(5), 16);
        drawmode(P(6));
        rect("clip", P(7));
    } else if (same(n, "DIBSCREENBLT")) {
        pdev("dev", P(0));
        rect("clip", P(5));
        drawmode(P(6));
        dump("bmi", P(8), 40);
        dumpw("xlate", P(9), 16);
    } else if (same(n, "DEVICEBITMAPBITS")) {
        pdev("bmp", P(0));
        dump("bmi", P(5), 40);
        drawmode(P(6));
        dumpw("xlate", P(7), 16);
    } else if (same(n, "SAVESCREENBITMAP")) {
        rect("rect", P(0));
    } else if (same(n, "SCANLR")) {
        pdev("dev", P(0));
    } else if (same(n, "DISABLE")) {
        pdev("dev", P(0));
    }
}

static void exit_detail(struct ent *e, u16 far *args, u32 r)
{
    const char *n = e->name;
    if (same(n, "ENABLE")) {
        if (W(1) & 1)
            dump("gdiinfo", P(0), 0x6E);
        else
            dump("dev", P(0), 32);
    } else if (same(n, "REALIZEOBJECT")) {
        if (P(3) && (int)W(1) > 0)
            dump("out", P(3), (u16)r > 0 && (u16)r < 64 ? (u16)r : 32);
    } else if (same(n, "COLORINFO")) {
        dump("pcolor", P(2), 4);
    } else if (same(n, "GETCHARWIDTH")) {
        int c = W(3) - W(2) + 1;
        dumpw("widths", P(1), c > 0 && c < 256 ? c : 1);
    } else if (same(n, "CONTROL")) {
        dump("out", P(3), 16);
    }
}
#else
static void enter_detail(struct ent *e, u16 far *args) { (void)e; (void)args; }
static void exit_detail(struct ent *e, u16 far *args, u32 r) { (void)e; (void)args; (void)r; }
#endif

void __cdecl far gdl_init(void)
{
    put("# log start");
    nl();
    flush();
}

/* called by gdl_common: frame = return IP, CS, arguments. Returns 1 when
   the result is to be recorded (the return address is then replaced). */
u16 __cdecl far gdl_enter(u16 idx, u16 far *frame)
{
    struct ent *e = &gdl_tab[idx];
    u16 far *args = frame + 2;
    int i, k;
    if (busy)
        return 0;
    busy = 1;
    seq++;
    indent();
    putc_('>');
    hex(seq, 6);
    putc_(' ');
    put(e->name);
    if (e->mode == 1) {
        for (i = 0; i < 8; i++) {
            putc_(' ');
            hex(args[i], 4);
        }
    } else {
        for (k = 0; e->args[k]; k++) {
            u16 far *a = argp(args, e->args, k);
            putc_(' ');
            if (e->args[k] == 'w')
                hex(a[0], 4);
            else if (e->args[k] == 'l')
                hex(*(u32 far *)a, 8);
            else {
                hex(a[1], 4);
                putc_(':');
                hex(a[0], 4);
            }
        }
    }
    put("  @");
    hex(frame[1], 4);
    putc_(':');
    hex(frame[0], 4);
    nl();
    if (e->mode == 0)
        enter_detail(e, args);
    busy = 0;
    if (e->mode != 0 || depth >= SIDE) {
        if (!depth)
            flush();
        return 0;
    }
    side[depth].ip = frame[0];
    side[depth].cs = frame[1];
    side[depth].idx = idx;
    side[depth].seq = seq;
    for (i = 0; i < MAXARGW && i < e->nbytes / 2; i++)
        side[depth].a[i] = args[i];
    depth++;
    return 1;
}

u32 __cdecl far gdl_exit(u32 r)
{
    struct ent *e;
    depth--;
    e = &gdl_tab[side[depth].idx];
    busy = 1;
    indent();
    putc_('<');
    hex(side[depth].seq, 6);
    putc_(' ');
    put(e->name);
    put(" = ");
    hex(r, 8);
    nl();
    exit_detail(e, side[depth].a, r);
    busy = 0;
    if (!depth)
        flush();
    return ((u32)side[depth].cs << 16) | side[depth].ip;
}
