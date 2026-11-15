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

/* GD1 and GD2 write one file, C:\\GD.LOG, each record flushed at once so
   that their records interleave in call order; GDL_TAG marks whose */
#define GDL_FILE "C:\\GD.LOG"
#ifndef GDL_TAG
#define GDL_TAG "?"
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
static struct { u16 ip, cs, idx, fsp; u32 seq; u16 a[MAXARGW]; } side[SIDE];

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

static int same(const char far *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

#if defined(GD_DISPLAY) && defined(GD_VALUES)
/* VGREC: the display driver interface, values only. Nothing is dumped
   from behind a pointer the driver owns or fills with its own data (the
   PDEVICE, physical pens, brushes and objects, escape output): only the
   documented structures GDI builds (PBITMAP, DRAWMODE, TEXTXFORM, LPEN,
   LBRUSH, rectangles, points, strings, BITMAPINFOHEADER) and GDIINFO,
   which GDI reads, decoded field by field. */
static void putw_(const char *name, int v)
{
    put(" ");
    put(name);
    put("=");
    dec(v);
}

static void line_start(const char *label)
{
    indent();
    put("  .");
    put(label);
    put(":");
}

static void pdev(const char *label, void far *p)
{
    const short far *w = p;
    line_start(label);
    if (!p || !readable(p, 2)) {
        put(p ? " unreadable" : " null");
    } else if (w[0] != 0) {
        put(" pdevice");
    } else if (readable(p, 32)) {
        const u8 far *b = p;
        putw_("w", w[1]);
        putw_("h", w[2]);
        putw_("wb", w[3]);
        putw_("planes", b[8]);
        putw_("bpp", b[9]);
        put(" bits=");
        hex(w[6], 4);
        put(":");
        hex(w[5], 4);
        putw_("segidx", w[11]);
        putw_("scanseg", w[12]);
        putw_("fill", w[13]);
    }
    nl();
}

static void drawmode(void far *p)
{
    const short far *w = p;
    line_start("dm");
    if (!p || !readable(p, 32)) {
        put(p ? " unreadable" : " null");
    } else {
        putw_("rop2", w[0]);
        putw_("bkmode", w[1]);
        put(" bk=");
        hex(*(u32 far *)(w + 2), 8);
        put(" fg=");
        hex(*(u32 far *)(w + 4), 8);
        putw_("tbx", w[6]);
        putw_("bx", w[7]);
        putw_("berr", w[8]);
        putw_("brem", w[9]);
        putw_("bcnt", w[10]);
        putw_("cx", w[11]);
        put(" lbk=");
        hex(*(u32 far *)(w + 12), 8);
        put(" lfg=");
        hex(*(u32 far *)(w + 14), 8);
    }
    nl();
}

static void xform(void far *p)
{
    const short far *w = p;
    const char far *c = p;
    line_start("xform");
    if (!p || !readable(p, 20)) {
        put(p ? " unreadable" : " null");
    } else {
        putw_("h", w[0]);
        putw_("w", w[1]);
        putw_("esc", w[2]);
        putw_("orient", w[3]);
        putw_("weight", w[4]);
        putw_("ital", c[10]);
        putw_("ul", c[11]);
        putw_("so", c[12]);
        putw_("outp", c[13]);
        putw_("clipp", c[14]);
        putw_("accel", *(short far *)(c + 15));
        putw_("ovh", *(short far *)(c + 17));
    }
    nl();
}

static void fontinfo(void far *p)
{
    const u8 far *b = p;
    line_start("font");
    if (!p || !readable(p, 0x32)) {
        put(p ? " unreadable" : " null");
    } else {
        putw_("type", *(short far *)b);
        putw_("pts", *(short far *)(b + 2));
        putw_("ascent", *(short far *)(b + 8));
        putw_("weight", *(short far *)(b + 0x11));
        putw_("charset", b[0x13]);
        putw_("pixw", *(short far *)(b + 0x14));
        putw_("pixh", *(short far *)(b + 0x16));
        putw_("pf", b[0x18]);
        putw_("avgw", *(short far *)(b + 0x19));
        putw_("maxw", *(short far *)(b + 0x1B));
        putw_("first", b[0x1D]);
        putw_("last", b[0x1E]);
        putw_("dflt", b[0x1F]);
        putw_("brk", b[0x20]);
    }
    nl();
}

static void bmih(const char *label, void far *p)
{
    const u8 far *b = p;
    line_start(label);
    if (!p || !readable(p, 40)) {
        put(p ? " unreadable" : " null");
    } else {
        putw_("size", *(short far *)b);
        putw_("w", *(short far *)(b + 4));
        putw_("h", *(short far *)(b + 8));
        putw_("planes", *(short far *)(b + 12));
        putw_("bpp", *(short far *)(b + 14));
        putw_("comp", *(short far *)(b + 16));
        putw_("image", *(short far *)(b + 20));
        putw_("used", *(short far *)(b + 32));
    }
    nl();
}

static void enter_detail(struct ent *e, u16 far *args)
{
    const char *n = e->name;
    if (same(n, "OUTPUT")) {
        int cnt = W(2);
        pdev("dev", P(0));
        dumpw("pts", P(3), cnt > 0 && cnt < 200 ? cnt * 2 : 2);
        drawmode(P(6));
        rect("clip", P(7));
    } else if (same(n, "BITBLT")) {
        pdev("dst", P(0));
        if (P(3))
            pdev("src", P(3));
        drawmode(P(10));
    } else if (same(n, "EXTTEXTOUT") || same(n, "STRBLT")) {
        int cnt = (int)W(5);
        pdev("dev", P(0));
        rect("clip", P(3));
        str("str", P(4), cnt < 0 ? -cnt : cnt);
        fontinfo(P(6));
        drawmode(P(7));
        xform(P(8));
        if (same(n, "EXTTEXTOUT")) {
            if (P(9))
                dumpw("dx", P(9), cnt < 0 ? -cnt : cnt);
            rect("opaque", P(10));
        }
    } else if (same(n, "REALIZEOBJECT")) {
        int style = (int)W(1);
        pdev("dev", P(0));
        if (style == 1)
            dumpw("lpen", P(2), 5);
        else if (style == 2) {
            const u16 far *w = P(2);
            line_start("lbrush");
            if (w && readable(w, 12)) {
                putw_("style", w[0]);
                put(" color=");
                hex(*(u32 far *)(w + 1), 8);
                putw_("hatch", w[3]);
                put(" bk=");
                hex(*(u32 far *)(w + 4), 8);
            }
            nl();
            /* the brush origin is the parameter itself, a POINT (x low
               word, y high word), not a pointer: never read through it */
            line_start("org");
            putw_("x", (short)argp(args, e->args, 4)[0]);
            putw_("y", (short)argp(args, e->args, 4)[1]);
            nl();
        } else if (style == 3) {
            line_start("lfont");
            if (P(2) && readable(P(2), 50)) {
                const short far *w = P(2);
                putw_("h", w[0]);
                putw_("w", w[1]);
                putw_("weight", w[4]);
                put(" face=");
                str("face", (const char far *)P(2) + 18, -1);
            } else
                nl();
        }
    } else if (same(n, "COLORINFO") || same(n, "SCANLR") || same(n, "DISABLE")
               || same(n, "CONTROL")) {
        pdev("dev", P(0));
    } else if (same(n, "PIXEL")) {
        pdev("dev", P(0));
        if (P(4))
            drawmode(P(4));
    } else if (same(n, "ENABLE")) {
        str("type", P(2), -1);
        str("file", P(3), -1);
    } else if (same(n, "GETCHARWIDTH")) {
        pdev("dev", P(0));
        fontinfo(P(4));
        drawmode(P(5));
        xform(P(6));
    } else if (same(n, "FASTBORDER")) {
        rect("rect", P(0));
        pdev("dev", P(4));
        drawmode(P(6));
        rect("clip", P(7));
    } else if (same(n, "DIBSCREENBLT")) {
        pdev("dev", P(0));
        rect("clip", P(5));
        drawmode(P(6));
        bmih("bmi", P(8));
        dumpw("xlate", P(9), 16);
    } else if (same(n, "DEVICEBITMAPBITS")) {
        pdev("bmp", P(0));
        bmih("bmi", P(5));
        drawmode(P(6));
        dumpw("xlate", P(7), 16);
    } else if (same(n, "SAVESCREENBITMAP")) {
        rect("rect", P(0));
    }
}

static void exit_detail(struct ent *e, u16 far *args, u32 r)
{
    const char *n = e->name;
    (void)r;
    (void)dump;
    if (same(n, "ENABLE")) {
        if (W(1) & 1)
            dumpw("gdiinfo", P(0), 0x6E / 2);
    } else if (same(n, "COLORINFO")) {
        if (P(2))
            dumpw("pcolor", P(2), 2);
    } else if (same(n, "GETCHARWIDTH")) {
        int c = W(3) - W(2) + 1;
        dumpw("widths", P(1), c > 0 && c < 256 ? c : 1);
    } else if (same(n, "EXTTEXTOUT") || same(n, "STRBLT")) {
        if ((int)W(5) < 0)
            drawmode(P(7));
    } else if (same(n, "DEVICEBITMAPBITS")) {
        if (W(1))
            bmih("bmi", P(5));
    }
}
#elif defined(GD_DISPLAY)
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
/* GD2: GDI's own interface (Windows 3.1 SDK reference for the
   documented functions; parameter types from Wine's gdi.spec) */
static void enter_detail(struct ent *e, u16 far *args)
{
    const char far *n = e->name;
    if (same(n, "TEXTOUT")) {
        str("str", P(3), (int)W(4));
    } else if (same(n, "EXTTEXTOUT")) {
        int c = (int)W(6);
        rect("rect", P(4));
        str("str", P(5), c);
    } else if (same(n, "GETTEXTEXTENT")) {
        str("str", P(1), (int)W(2));
    } else if (same(n, "CREATEFONTINDIRECT")) {
        dump("lf", P(0), 50);
        str("face", (char far *)P(0) + 18, -1);
    } else if (same(n, "CREATEBRUSHINDIRECT") || same(n, "CREATEPENINDIRECT")) {
        dump("in", P(0), 10);
    } else if (same(n, "CREATEDC") || same(n, "CREATEIC")) {
        str("driver", P(0), -1);
        dump("driver bytes", P(0), 16);
        str("device", P(1), -1);
        str("output", P(2), -1);
    } else if (same(n, "ADDFONTRESOURCE")) {
        str("file", P(0), -1);
    } else if (same(n, "SETDIBITS") || same(n, "GETDIBITS")) {
        dump("bmi", P(5), 40);
    } else if (same(n, "STRETCHDIBITS")) {
        dump("bmi", P(10), 40);
    } else if (same(n, "SETDIBITSTODEVICE")) {
        dump("bmi", P(10), 40);
    } else if (same(n, "CREATEDIBITMAP")) {
        dump("bmih", P(1), 40);
        dump("bmi", P(4), 40);
    } else if (same(n, "CREATEBITMAP")) {
        dump("bits", P(4), 32);
    } else if (same(n, "SETBITMAPBITS")) {
        dump("bits", P(2), 32);
    } else if (same(n, "CREATEPALETTE")) {
        dump("pal", P(0), 4 + 4 * 20);
    } else if (same(n, "SETRECTRGN") || same(n, "FASTWINDOWFRAME")) {
        rect("rect", P(1));
    } else if (same(n, "CREATERECTRGNINDIRECT")) {
        rect("rect", P(0));
    } else if (same(n, "POLYGON") || same(n, "POLYLINE")) {
        dumpw("pts", P(1), W(2) * 2 < 200 ? W(2) * 2 : 200);
    } else if (same(n, "DPTOLP") || same(n, "LPTODP")) {
        dumpw("pts", P(1), W(2) * 2 < 64 ? W(2) * 2 : 64);
    } else if (same(n, "ESCAPE")) {
        dump("in", P(3), 16);
    }
}

static void exit_detail(struct ent *e, u16 far *args, u32 r)
{
    const char far *n = e->name;
    if (same(n, "CREATEDC") || same(n, "CREATEIC")) {
        dump("driver", P(0), 16);           /* does GDI write into it? */
    } else if (same(n, "GETTEXTMETRICS")) {
        dump("tm", P(1), 31);
    } else if (same(n, "GETOBJECT")) {
        dump("obj", P(2), (int)W(1) > 0 && W(1) < 64 ? W(1) : 16);
    } else if (same(n, "GETCLIPBOX") || same(n, "GETRGNBOX") || same(n, "GETBOUNDSRECT")) {
        rect("rect", P(1));
    } else if (same(n, "GETCHARWIDTH")) {
        int c = W(2) - W(1) + 1;
        dumpw("widths", P(3), c > 0 && c < 256 ? c : 1);
    } else if (same(n, "GETTEXTFACE")) {
        str("face", P(2), -1);
    } else if (same(n, "DPTOLP") || same(n, "LPTODP")) {
        dumpw("pts", P(1), W(2) * 2 < 64 ? W(2) * 2 : 64);
    } else if (same(n, "GETSYSTEMPALETTEENTRIES")) {
        dump("pal", P(3), W(2) * 4 < 64 ? W(2) * 4 : 64);
    } else if (same(n, "GETDIBITS")) {
        dump("bmi", P(5), 40);
    } else if (same(n, "GETCURLOGFONT")) {
        (void)r;
    }
}
#endif

void __cdecl far gdl_init(void)
{
    put("# log start ");
    put(GDL_TAG);
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
    put(GDL_TAG);
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
    flush();
#ifdef GD_POPS
    /* VGREC: the undocumented entries' results and pops are recorded too */
    if (e->mode == 2 || depth >= SIDE)
        return 0;
#else
    if (e->mode != 0 || depth >= SIDE)
        return 0;
#endif
    side[depth].ip = frame[0];
    side[depth].cs = frame[1];
    side[depth].idx = idx;
    side[depth].seq = seq;
    side[depth].fsp = OFFSETOF(frame);
    for (i = 0; i < MAXARGW && i < e->nbytes / 2; i++)
        side[depth].a[i] = args[i];
    depth++;
    return 1;
}

u32 __cdecl far gdl_exit(u32 r, u16 sp)
{
    struct ent *e;
    depth--;
    e = &gdl_tab[side[depth].idx];
    busy = 1;
    indent();
    put(GDL_TAG);
    putc_('<');
    hex(side[depth].seq, 6);
    putc_(' ');
    put(e->name);
    put(" = ");
    hex(r, 8);
#ifdef GD_POPS
    /* the bytes the real function popped: its SP on return less the
       return address's place and the 4 bytes of the address */
    put(" pop=");
    dec((int)(sp - side[depth].fsp - 4));
#else
    (void)sp;
#endif
    nl();
    if (e->mode == 0)
        exit_detail(e, side[depth].a, r);
    busy = 0;
    flush();
    return ((u32)side[depth].cs << 16) | side[depth].ip;
}
