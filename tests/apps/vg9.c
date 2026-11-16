/*
 * vg9.c - VG9: mouse trails (drivers/vga/README.md, "Mouse trails"). The
 * display driver's MOUSETRAILS escape (Windows 3.1 DDK: Control escape 39)
 * and the trail it draws, observed through the documented interface only:
 *
 * - the escape: QUERYESCSUPPORT(MOUSETRAILS) and Escape(MOUSETRAILS, n)
 *   for a list of n, each result with WIN.INI [windows] MouseTrails as
 *   GetProfileString then reads it;
 * - the trail: the driver's MoveCursor (DISPLAY.103) called with chosen
 *   positions, and the screen read back from the VGA's memory (the four
 *   planes of a band of rows, with interrupts off so the driver's timer
 *   code does not run between the read map's setting and the reads),
 *   recorded to C:\VG9.BIN whenever it changes: what the trail looks like
 *   after each move, and how it goes away when the moves stop (CheckCursor,
 *   the timer); with cursor shapes changed (USER's SetCursor), hidden
 *   (ShowCursor), drawing under it (GDI), the trail turned off, at the
 *   screen's edges.
 *
 * C:\VG9.IN (optional), first word: "boot" logs only the state the driver
 * starts in (WIN.INI MouseTrails read at Enable?) and records a few moves.
 * Run as the shell by tests/vgatrails.sh with Windows 3.1's VGA.DRV and
 * with w16's; tools/vg9png.py turns VG9.BIN into images, and the two
 * drivers' logs and frames are compared. Values and screens only.
 */
#include <windows.h>

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);

extern void cli(void);
#pragma aux cli = "cli";
extern void sti(void);
#pragma aux sti = "sti";
extern void outb_(unsigned port, unsigned char v);
#pragma aux outb_ = "out dx,al" parm [dx] [al];
extern unsigned char inb_(unsigned port);
#pragma aux inb_ = "in al,dx" parm [dx] value [al];

#define MOUSETRAILS 39

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void num(const char *name, int v)
{
    char s[16];
    int i = 15;
    unsigned u = v < 0 ? -v : v;
    s[15] = 0;
    do {
        s[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        s[--i] = '-';
    out(" ");
    out(name);
    out("=");
    out(s + i);
}

typedef void (FAR PASCAL *MOVECURSOR)(WORD x, WORD y);
static MOVECURSOR movecursor;
static WORD vram;
static HDC sdc;

/* ---- the screen band recorded: rows y0 .. y0 + h - 1, all 80 bytes ---- */
static BYTE __far *cur, __far *prev;
static int band_y0 = 0, band_h = 200;
static HFILE bin = HFILE_ERROR;
static int frames;
static int phase;
static DWORD t0;

#define BAND_MAX 200
#define BAND_BYTES (80U * BAND_MAX * 4)   /* 64000: one segment */

/* the band into d; interrupts already off */
static void grab_into(BYTE __far *d)
{
    BYTE idx, rm, mode;
    int p, r;
    WORD c;
    BYTE __far *v;
    idx = inb_(0x3CE);
    outb_(0x3CE, 4);
    rm = inb_(0x3CF);
    outb_(0x3CE, 5);
    mode = inb_(0x3CF);
    outb_(0x3CF, (BYTE)(mode & ~8));
    for (p = 0; p < 4; p++) {
        outb_(0x3CE, 4);
        outb_(0x3CF, (BYTE)p);
        for (r = 0; r < band_h; r++) {
            v = (BYTE __far *)MAKELP(vram, (WORD)(band_y0 + r) * 80);
            for (c = 0; c < 80; c++)
                *d++ = v[c];
        }
    }
    outb_(0x3CE, 5);
    outb_(0x3CF, mode);
    outb_(0x3CE, 4);
    outb_(0x3CF, rm);
    outb_(0x3CE, idx);
}

static void grab(void)
{
    cli();
    grab_into(cur);
    sti();
}

static void put_frame(const char *why)
{
    struct {
        char tag[2];
        WORD seq, phase, y0, h;
        DWORD tick;
        WORD pad;
    } hd;
    WORD n = 80U * band_h * 4, i;
    hd.tag[0] = 'F';
    hd.tag[1] = 'R';
    hd.seq = (WORD)frames;
    hd.phase = (WORD)phase;
    hd.y0 = (WORD)band_y0;
    hd.h = (WORD)band_h;
    hd.tick = GetTickCount() - t0;
    hd.pad = 0;
    if (bin != HFILE_ERROR) {
        _lwrite(bin, (LPCSTR)&hd, sizeof hd);
        _lwrite(bin, (LPCSTR)cur, n);
    }
    for (i = 0; i < n; i++)
        prev[i] = cur[i];
    out("frame");
    num("n", frames);
    num("phase", phase);
    out(" ms=");                      /* the time's low word */
    {
        char t[6];
        WORD u = (WORD)hd.tick;
        int k = 5;
        t[5] = 0;
        do {
            t[--k] = (char)('0' + u % 10);
            u /= 10;
        } while (u);
        out(t + k);
    }
    out(" ");
    out(why);
    out("\r\n");
    frames++;
}

static int changed(void)
{
    WORD n = 80U * band_h * 4, i;
    for (i = 0; i < n; i++)
        if (cur[i] != prev[i])
            return 1;
    return 0;
}

/* a frame now, labelled */
static void shot(const char *why)
{
    grab();
    put_frame(why);
}

/* every change of the band for ms milliseconds */
static void watch(DWORD ms, const char *why)
{
    DWORD s = GetTickCount();
    while (GetTickCount() - s < ms) {
        grab();
        if (changed())
            put_frame(why);
    }
}

/* a wait with no frames */
static void pause(DWORD ms)
{
    DWORD st = GetTickCount();
    while (GetTickCount() - st < ms)
        ;
}

static void tick_edge(void)
{
    DWORD s = GetTickCount();
    while (GetTickCount() == s)
        ;
}

static void band(int y0, int h)
{
    band_y0 = y0;
    band_h = h;
}

static void begin(int ph, const char *what)
{
    phase = ph;
    out("phase");
    num("n", ph);
    out(" ");
    out(what);
    out("\r\n");
}

/* ---- the escape ---- */
static int query(void)
{
    int n = MOUSETRAILS;
    return Escape(sdc, QUERYESCSUPPORT, sizeof n, (LPCSTR)&n, NULL);
}

static void profile(void)
{
    char b[40];
    GetProfileString("windows", "MouseTrails", "<none>", b, sizeof b);
    out(" ini=");
    out(b);
}

static int trails(int n)
{
    int v = n, r = Escape(sdc, MOUSETRAILS, sizeof v, (LPCSTR)&v, NULL);
    out("escape");
    num("in", n);
    num("r", r);
    num("v", v);
    num("q", query());
    profile();
    out("\r\n");
    return r;
}

static void ini(const char *v)
{
    WriteProfileString("windows", "MouseTrails", v);
    out("set ini=");
    out(v ? v : "<deleted>");
    num("q", query());
    out("\r\n");
}

/* moves along y from x0 by dx, n times; a shot after each (each = 0:
   only after the last) */
static void moves(int x0, int y, int dx, int n, int each, const char *why)
{
    int i;
    for (i = 0; i < n; i++) {
        movecursor((WORD)(x0 + i * dx), (WORD)y);
        if (each)
            shot(why);
    }
    if (!each)
        shot(why);
}

static void park(int x, int y)
{
    movecursor((WORD)x, (WORD)y);
    watch(1500, "park");
}

static void escape_tests(void)
{
    static const int seq[] = {
        -1, 0, -1, 3, 0, -3, -2, 5, -2, -3, 0, 7, 8, 9, -4, 100, 1, 2,
        0, -1, -5, 0, 0, -1
    };
    int i;
    begin(1, "escape");
    out("start");
    num("q", query());
    profile();
    out("\r\n");
    for (i = 0; i < sizeof seq / sizeof seq[0]; i++)
        trails(seq[i]);
    ini(NULL);
    trails(-1);
    ini(NULL);
    trails(0);
    ini(NULL);
    trails(-2);
    trails(-3);
    ini("4");
    trails(-1);
    ini("-6");
    trails(-1);
    ini("0");
    trails(-1);
    ini("12");
    trails(-1);
    ini("abc");
    trails(-1);
    ini("-3");
    trails(0);
    trails(-3);
    trails(0);
}

static void trail_tests(void)
{
    HCURSOR arrow = LoadCursor(NULL, IDC_ARROW);
    int i;

    band(0, 200);
    SetCursor(arrow);

    begin(2, "len7 moves, a shot after each, then stop");
    trails(7);
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 14, 1, "move");
    watch(2500, "stop");

    begin(3, "len7 six moves at once, then stop");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved6");
    watch(2500, "stop");

    begin(4, "len7 one move per tick");
    park(40, 100);
    for (i = 0; i < 10; i++) {
        tick_edge();
        movecursor((WORD)(80 + i * 40), 100);
        shot("tickmove");
    }
    watch(2500, "stop");

    begin(5, "len3");
    trails(3);
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 8, 1, "move");
    watch(2500, "stop");

    begin(6, "len1");
    trails(1);
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 5, 1, "move");
    watch(1500, "stop");

    begin(7, "len2 small steps");
    trails(2);
    park(40, 100);
    tick_edge();
    moves(80, 100, 3, 10, 1, "move");
    watch(1500, "stop");

    begin(8, "len7 small steps");
    trails(7);
    park(40, 100);
    tick_edge();
    moves(80, 100, 5, 12, 1, "move");
    watch(2500, "stop");

    begin(9, "len7 diagonal");
    park(40, 20);
    tick_edge();
    for (i = 0; i < 10; i++) {
        movecursor((WORD)(60 + i * 30), (WORD)(30 + i * 15));
        shot("move");
    }
    watch(2500, "stop");

    begin(10, "same position again");
    park(300, 100);
    tick_edge();
    for (i = 0; i < 5; i++) {
        movecursor(300, 100);
        shot("same");
    }
    watch(1500, "stop");

    begin(11, "ibeam");
    SetCursor(LoadCursor(NULL, IDC_IBEAM));
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 8, 1, "move");
    watch(2500, "stop");

    begin(12, "hourglass");
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 8, 1, "move");
    watch(2500, "stop");

    begin(13, "cross");
    SetCursor(LoadCursor(NULL, IDC_CROSS));
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 8, 1, "move");
    watch(2500, "stop");

    begin(14, "shape changed during the trail");
    SetCursor(arrow);
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    shot("setwait");
    watch(2500, "stop");
    SetCursor(arrow);

    begin(15, "hidden during the trail, shown again");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    ShowCursor(FALSE);
    shot("hidden");
    watch(1500, "hidden");
    moves(320, 100, 40, 4, 1, "hiddenmove");
    watch(1500, "hidden");
    ShowCursor(TRUE);
    shot("shown");
    watch(2500, "shown");

    begin(16, "drawing under the trail");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 7, 0, "moved");
    {
        HBRUSH g = CreateSolidBrush(RGB(128, 128, 128)), ob;
        ob = SelectObject(sdc, g);
        PatBlt(sdc, 150, 90, 120, 40, PATCOPY);
        shot("patblt");
        TextOut(sdc, 60, 105, "Trails under text, trails under text.", 37);
        shot("text");
        MoveTo(sdc, 0, 110);
        LineTo(sdc, 640, 112);
        shot("line");
        SelectObject(sdc, ob);
        DeleteObject(g);
    }
    watch(2500, "stop");
    moves(400, 100, -40, 8, 1, "back");
    {
        HBRUSH g = CreateSolidBrush(RGB(0, 0, 255)), ob;
        ob = SelectObject(sdc, g);
        PatBlt(sdc, 0, 80, 640, 60, PATCOPY);
        SelectObject(sdc, ob);
        DeleteObject(g);
    }
    shot("cleared");
    watch(2500, "stop");

    begin(17, "turned off during the trail");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    trails(0);
    shot("off");
    watch(1500, "off");
    moves(320, 100, 40, 4, 1, "offmove");
    watch(1500, "off");
    trails(-1);

    begin(18, "trail turned on while moved");
    trails(0);
    park(40, 100);
    moves(80, 100, 40, 3, 0, "moved");
    trails(7);
    shot("on");
    moves(200, 100, 40, 4, 1, "move");
    watch(2500, "stop");

    begin(19, "edges: top left");
    park(100, 100);
    tick_edge();
    for (i = 0; i < 8; i++) {
        movecursor((WORD)(60 - i * 10), (WORD)(30 - i * 5));
        shot("move");
    }
    watch(2500, "stop");

    begin(20, "edges: bottom right");
    band(280, 200);
    park(560, 400);
    tick_edge();
    for (i = 0; i < 8; i++) {
        movecursor((WORD)(600 + i * 6), (WORD)(440 + i * 6));
        shot("move");
    }
    watch(2500, "stop");
    band(0, 200);

    begin(21, "length changed during the trail");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 7, 0, "moved");
    trails(2);
    shot("len2");
    watch(2500, "stop");
    trails(7);
    park(320, 240);
}

typedef void (FAR PASCAL *CHECKCURSOR)(void);
static CHECKCURSOR checkcursor;

/* a cursor of 32 x 32: AND mask all 1s but `holes` 0 bits on row 8,
   XOR mask a block on rows 4-11 */
static HCURSOR make_cursor(int holes)
{
    static BYTE andm[128], xorm[128];
    int i;
    for (i = 0; i < 128; i++) {
        andm[i] = 0xFF;
        xorm[i] = 0;
    }
    for (i = 4; i < 12; i++)
        xorm[i * 4] = 0xF0;
    for (i = 0; i < holes; i++)
        andm[8 * 4 + 1] &= (BYTE)~(0x80 >> i);
    return CreateCursor((HINSTANCE)init_regs[5], 0, 0, 32, 32, andm, xorm);
}

static void more_tests(void)
{
    HCURSOR arrow = LoadCursor(NULL, IDC_ARROW);
    int i;
    band(0, 200);
    SetCursor(arrow);
    trails(7);

    begin(22, "CheckCursor called directly");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    for (i = 0; i < 8; i++) {
        checkcursor();
        shot("check");
    }
    watch(1000, "stop");

    begin(23, "the same position, then away");
    park(40, 100);
    tick_edge();
    movecursor(80, 100);
    movecursor(80, 100);
    movecursor(80, 100);
    movecursor(120, 100);
    shot("away");
    watch(1500, "stop");

    begin(24, "drawing away from the trail");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    {
        HBRUSH g = CreateSolidBrush(RGB(0, 128, 0)), ob;
        ob = SelectObject(sdc, g);
        PatBlt(sdc, 400, 160, 100, 30, PATCOPY);
        shot("far");
        PatBlt(sdc, 0, 100, 20, 10, PATCOPY);
        shot("near");
        SelectObject(sdc, ob);
        DeleteObject(g);
    }
    watch(1500, "stop");
    begin(25, "drawing away from the cursor, trails off");
    trails(0);
    park(200, 100);
    {
        HBRUSH g = CreateSolidBrush(RGB(0, 0, 128)), ob;
        ob = SelectObject(sdc, g);
        PatBlt(sdc, 400, 160, 100, 30, PATCOPY);
        shot("far");
        PatBlt(sdc, 190, 90, 10, 10, PATCOPY);
        shot("near");
        SelectObject(sdc, ob);
        DeleteObject(g);
    }
    watch(1000, "stop");
    trails(7);

    begin(26, "escapes during the trail");
    {
        static const int seq[] = { 99, 8, -1, -3, 7, -2, -2, -3, 3, 0 };
        int k;
        for (k = 0; k < sizeof seq / sizeof seq[0]; k++) {
            park(40, 100);
            tick_edge();
            moves(80, 100, 40, 6, 0, "moved");
            if (seq[k] == 99) {
                query();
                shot("query");
            } else {
                trails(seq[k]);
                shot("escape");
            }
            watch(600, "after");
        }
    }

    begin(27, "escape when off, WIN.INI changed by others");
    {
        static const char *v[] = { "4", NULL, "-6", "0", "12", "abc", " 5", "3x", "-0", "+2" };
        int k;
        for (k = 0; k < sizeof v / sizeof v[0]; k++) {
            trails(-2);
            ini(v[k]);
            trails(-1);
        }
        trails(-2);
        for (k = 0; k < sizeof v / sizeof v[0]; k++) {
            ini(v[k]);
            trails(0);
        }
        trails(-2);
        trails(8);
        trails(-4);
        trails(-5);
        trails(100);
        trails(-3);
        trails(9);
        trails(-7);
    }
    begin(28, "KERNEL's profile alone");
    WriteProfileString("windows", "MouseTrails", "-7");
    profile();
    out("\r\n");
    WriteProfileString("windows", "MouseTrails", "7");
    profile();
    out("\r\n");
    WriteProfileString("windows", "MouseTrails", "3");
    profile();
    out("\r\n");

    begin(29, "XOR-only cursors");
    trails(7);
    for (i = 0; i < 3; i++) {
        HCURSOR c = make_cursor(i);
        SetCursor(c);
        park(40, 100);
        tick_edge();
        moves(80, 100, 40, 4, 0, "moved");
        watch(600, "stop");
        SetCursor(arrow);
        DestroyCursor(c);
    }

    begin(31, "shape set again during the trail");
    SetCursor(arrow);
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    {
        typedef void (FAR PASCAL *SETCURSOR)(LPVOID);
        HCURSOR w = LoadCursor(NULL, IDC_WAIT);
        SetCursor(w);
        shot("wait");
        moves(320, 100, 40, 3, 1, "move");
        SetCursor(arrow);
        shot("arrow");
        watch(1000, "stop");
    }
    trails(7);
    park(320, 240);
}

static void fill(int x, int y, int w, int h, COLORREF c)
{
    HBRUSH g = CreateSolidBrush(c), ob;
    ob = SelectObject(sdc, g);
    PatBlt(sdc, x, y, w, h, PATCOPY);
    SelectObject(sdc, ob);
    DeleteObject(g);
}

static void diag(void)
{
    int i;
    park(40, 20);
    tick_edge();
    for (i = 0; i < 6; i++)
        movecursor((WORD)(60 + i * 30), (WORD)(30 + i * 15));
}

static void late_tests(void)
{
    HCURSOR arrow = LoadCursor(NULL, IDC_ARROW);
    int i;
    band(0, 200);
    SetCursor(arrow);
    trails(7);

    begin(32, "drawing inside the trail's bounds, touching no image");
    diag();
    shot("moved");
    fill(196, 30, 20, 8, RGB(0, 128, 0));
    shot("gap");
    watch(1000, "stop");
    diag();
    shot("moved");
    fill(150, 118, 4, 4, RGB(0, 128, 0));
    shot("gap2");
    watch(1000, "stop");

    begin(33, "a move right after drawing excluded the trail");
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 6, 0, "moved");
    fill(150, 95, 30, 10, RGB(128, 0, 0));
    movecursor(360, 100);
    shot("move");
    movecursor(400, 100);
    shot("move");
    watch(1000, "stop");

    begin(34, "hidden, moved, shown at once");
    park(40, 100);
    tick_edge();
    ShowCursor(FALSE);
    moves(80, 100, 40, 4, 0, "hiddenmoved");
    ShowCursor(TRUE);
    shot("shown");
    moves(240, 100, 40, 2, 1, "move");
    watch(1000, "stop");

    begin(35, "an XOR-only cursor moved, then the arrow");
    SetCursor(LoadCursor(NULL, IDC_IBEAM));
    park(40, 100);
    tick_edge();
    moves(80, 100, 40, 4, 0, "moved");
    SetCursor(arrow);
    shot("arrow");
    moves(240, 100, 40, 2, 1, "move");
    watch(1000, "stop");

    begin(36, "values below -3 when off");
    {
        static const int v[] = { -4, -5, -7, -8, -100, -32768 };
        int k;
        for (k = 0; k < sizeof v / sizeof v[0]; k++) {
            trails(3);
            trails(0);
            trails(v[k]);
            trails(-2);
            trails(v[k]);
        }
        trails(0);
        trails(32767);
        trails(0);
        trails(1000);
    }
    trails(7);

    begin(38, "after drawing over the trail: CheckCursor calls until it shows");
    {
        static const int lens[] = { 1, 2, 4, 7 };
        int L, k;
        for (L = 0; L < 4; L++)
            for (k = 0; k <= 2; k++) {
                park(40, 100);
                tick_edge();
                moves(80, 100, 40, lens[L] - 1, 0, "moved");
                fill(40, 120, 8, 4, RGB(0, 0, 128));
                for (i = 0; i < k; i++)
                    checkcursor();
                shot("checks");
                out("trial");
                num("len", lens[L]);
                num("k", k);
                out("\r\n");
                watch(700, "after");
            }
    }

    begin(37, "the screen edge, the trail kept");
    park(320, 100);
    tick_edge();
    for (i = 0; i < 6; i++)
        movecursor((WORD)(620 + i * 4), 100);
    shot("moved");
    watch(1000, "stop");
    park(320, 240);
}

/* ---- the comparison (C:\VG9.IN "det"; tests/vgatrails.sh, the boot
   test): each case runs its operations with interrupts off, so that no
   timer tick (CheckCursor) falls between them, and keeps a frame at every
   '|'; the frames are written afterwards. The trail's states then do not
   depend on the machine's speed. Operations:
     Mx,y  MoveCursor     C  CheckCursor        |  a frame
     Sn    SetCursor: 0 arrow, 1 hourglass, 2 cross, 3 I-beam, 4 an XOR-only
           cursor, 5 the same with one AND bit 0
     H, U  ShowCursor(FALSE), ShowCursor(TRUE)
     Px,y,w,h,c  PatBlt in colour c (1 maroon, 2 green, 3 navy)
     En    Escape(MOUSETRAILS, n) (-2, -3: no WIN.INI); E99 the query ---- */
static HCURSOR shapes[6];

static const char *getint(const char *p, int *v)
{
    int neg = 0, n = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9')
        n = n * 10 + (*p++ - '0');
    *v = neg ? -n : n;
    if (*p == ',')
        p++;
    return p;
}

static const COLORREF dcol[4] = {
    RGB(192, 192, 192), RGB(128, 0, 0), RGB(0, 128, 0), RGB(0, 0, 128)
};

typedef struct {
    int len, shape, x, y, band0;
    const char *ops;
} DCASE;

static const DCASE dcases[] = {
    { 7, 0, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | M200,100 | M240,100 | M280,100 | C | M320,100 | M360,100 | M400,100 | M440,100 | M480,100 | C | C | C | C | C | C | C | C |" },
    { 3, 0, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | M200,100 | M240,100 | M280,100 | C | M320,100 | C | C | C |" },
    { 7, 0, 80, 100, 0, "M85,100 | M90,100 | M95,100 | M100,100 | M105,100 | M110,100 | M115,100 | M120,100 | M125,100 | C | C | C | C | C | C | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M80,100 M80,100 | M120,100 | C | C | C | C | C |" },
    { 7, 0, 100, 60, 0, "M60,30 | M40,20 | M20,10 | M10,5 | M0,0 | M0,0 | C | C | C | C | C | C |" },
    { 7, 0, 560, 400, 280, "M600,440 | M620,460 | M630,470 | M639,479 | C | C | C | C | C |" },
    { 7, 1, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | C | C |" },
    { 7, 2, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | C | C |" },
    { 7, 3, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | C | C |" },
    { 7, 4, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | C | C |" },
    { 7, 5, 40, 100, 0, "M80,100 | M120,100 | M160,100 | C | C | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M120,100 M160,100 M200,100 | S1 | M240,100 | M280,100 | S0 | C | M320,100 | C | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M120,100 M160,100 | H | M200,100 | M240,100 | U | M280,100 | C | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M120,100 M160,100 M200,100 M240,100 M280,100 | P150,105,30,10,1 | C | M320,100 | M360,100 | C | C | C |" },
    { 7, 0, 40, 20, 0, "M60,30 M90,45 M120,60 M150,75 M180,90 M210,105 | P196,30,20,8,2 | P150,118,4,4,2 | C | C | C | C | C | C | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M120,100 M160,100 M200,100 | P150,105,30,10,3 | M360,100 | M400,100 | C | C |" },
    { 7, 0, 40, 100, 0, "| P40,120,8,4,1 | C | M80,100 | P60,110,40,4,2 | C |" },
    { 7, 0, 40, 100, 0, "M80,100 M120,100 M160,100 | E99 | E-3 | E-2 | M200,100 | M240,100 | E-2 | C | E-3 | M280,100 | M320,100 | C | C |" },
    { 0, 0, 40, 100, 0, "M80,100 | M120,100 | C | P115,105,10,10,1 C | P400,160,40,10,2 |" },
    { 2, 0, 40, 100, 0, "M80,100 | M83,100 | M86,100 | C | M89,100 | C | C |" },
    { 0, 0, 40, 100, 0, "M0,0 | M0,1 | M1,0 | M0,2 | M5,0 | M0,5 | M1,1 | M3,3 | M0,0 C |" },
    { 0, 1, 40, 100, 0, "M100,15 | M100,16 | M100,17 | M100,0 | M100,1 | M100,2 | M0,30 | M15,30 | M16,30 | M17,30 |" },
    { 7, 0, 40, 100, 0, "M0,0 C C | M0,1 C C | M1,0 C C | M0,2 C C | M5,0 C C | M0,5 C C | M1,1 C C | M3,3 C C |" },
};

static void det_case(int n, const DCASE *dc)
{
    BYTE __far *pool[48];
    int np = 0, hidden = 0, i, v[5];
    const char *p;
    WORD sz;
    BYTE pic;
    begin(100 + n, dc->ops);
    band(dc->band0, 200);
    sz = 80U * band_h * 4;
    if (dc->len > 0)
        trails(dc->len);
    else if (!dc->len)
        trails(0);
    SetCursor(shapes[dc->shape]);
    fill(0, band_y0, 640, band_h, dcol[0]);
    movecursor((WORD)dc->x, (WORD)dc->y);
    pause(700);
    tick_edge();
    /* the timer masked at the PIC as well: 3.1's driver lets interrupts in
       again inside its entries (seen: ticks fell between the steps) */
    pic = inb_(0x21);
    outb_(0x21, (BYTE)(pic | 1));
    cli();
    for (p = dc->ops; *p; ) {
        char op = *p++;
        switch (op) {
        case 'M':
            p = getint(p, &v[0]);
            p = getint(p, &v[1]);
            movecursor((WORD)v[0], (WORD)v[1]);
            break;
        case 'C':
            checkcursor();
            break;
        case 'S':
            p = getint(p, &v[0]);
            SetCursor(shapes[v[0]]);
            break;
        case 'H':
            ShowCursor(FALSE);
            hidden++;
            break;
        case 'U':
            ShowCursor(TRUE);
            hidden--;
            break;
        case 'P':
            for (i = 0; i < 5; i++)
                p = getint(p, &v[i]);
            fill(v[0], v[1], v[2], v[3], dcol[v[4] & 3]);
            break;
        case 'E': {
            int e;
            p = getint(p, &e);
            if (e == 99)
                query();
            else
                Escape(sdc, MOUSETRAILS, sizeof e, (LPCSTR)&e, NULL);
            break;
        }
        case '|':
            if (np < 48) {
                if (!(pool[np] = (BYTE __far *)GlobalLock(GlobalAlloc(GMEM_FIXED, sz))))
                    break;
                grab_into(pool[np++]);
            }
            break;
        }
    }
    sti();
    outb_(0x21, pic);
    for (i = 0; i < np; i++) {
        WORD k;
        for (k = 0; k < sz; k++)
            cur[k] = pool[i][k];
        put_frame("step");
        GlobalFree((HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(pool[i]))));
    }
    while (hidden > 0) {
        ShowCursor(TRUE);
        hidden--;
    }
    SetCursor(shapes[0]);
    pause(700);
}

static void det_shapes(void)
{
    shapes[0] = LoadCursor(NULL, IDC_ARROW);
    shapes[1] = LoadCursor(NULL, IDC_WAIT);
    shapes[2] = LoadCursor(NULL, IDC_CROSS);
    shapes[3] = LoadCursor(NULL, IDC_IBEAM);
    shapes[4] = make_cursor(0);
    shapes[5] = make_cursor(1);
}

static void det_tests(void)
{
    int i;
    det_shapes();
    for (i = 0; i < sizeof dcases / sizeof dcases[0]; i++)
        det_case(i, &dcases[i]);
    trails(7);
    DestroyCursor(shapes[4]);
    DestroyCursor(shapes[5]);
}

static void boot_tests(void)
{
    /* the trail as WIN.INI set it at Enable: its state, then moves and
       ticks with interrupts off (det_case, the size left as it is) */
    static const DCASE bc = { -1, 0, 40, 100, 0,
        "M80,100 | M120,100 | M160,100 | M200,100 | M240,100 | M280,100 | M320,100 | M360,100 | C | C | C | C | C | C | C | C |" };
    begin(30, "boot");
    out("start");
    num("q", query());
    profile();
    out("\r\n");
    det_shapes();
    det_case(30, &bc);
    out("end");
    num("q", query());
    profile();
    out("\r\n");
}

static void stackfill(BYTE v)
{
    volatile BYTE b[1024];
    int i;
    for (i = 0; i < sizeof b; i++)
        b[i] = v;
}

int __cdecl tmain(void)
{
    HINSTANCE hi = init_regs[5];
    char mode[8];
    HFILE f;
    OFSTRUCT of;
    U_INITAPP(hi);
    sdc = GetDC(0);
    movecursor = (MOVECURSOR)GetProcAddress(GetModuleHandle("DISPLAY"), MAKEINTRESOURCE(103));
    checkcursor = (CHECKCURSOR)GetProcAddress(GetModuleHandle("DISPLAY"), MAKEINTRESOURCE(104));
    vram = LOWORD((DWORD)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(174)));
    cur = (BYTE __far *)GlobalLock(GlobalAlloc(GMEM_FIXED, BAND_BYTES));
    prev = (BYTE __far *)GlobalLock(GlobalAlloc(GMEM_FIXED, BAND_BYTES));
    if (!movecursor || !vram || !cur || !prev) {
        out("setup failed\r\n");
        return 1;
    }
    t0 = GetTickCount();
    mode[0] = 0;
    f = OpenFile("C:\\VG9.IN", &of, OF_READ);
    if (f != HFILE_ERROR) {
        int n = _lread(f, mode, sizeof mode - 1);
        mode[n > 0 ? n : 0] = 0;
        _lclose(f);
    }
    bin = _lcreat("C:\\VG9.BIN", 0);
    if (mode[0] == 'b') {
        boot_tests();
    } else if (mode[0] == 's') {
        /* the caller's stack below SP filled first: with spaces, zeros,
           'x' (does the value written depend on it?) */
        static const BYTE fills[] = { 0x20, 0, 'x', 0x20, 0 };
        int k;
        for (k = 0; k < sizeof fills; k++) {
            stackfill(fills[k]);
            trails(0);
            stackfill(fills[k]);
            trails(-1);
            stackfill(fills[k]);
            trails(3);
            stackfill(fills[k]);
            trails(7);
        }
    } else if (mode[0] == 'e') {
        escape_tests();
        trails(5);
    } else if (mode[0] == 'm') {
        more_tests();
    } else if (mode[0] == 'd') {
        det_tests();
    } else if (mode[0] == 'l') {
        late_tests();
    } else {
        escape_tests();
        trail_tests();
        more_tests();
        late_tests();
    }
    out("end\r\n");
    _lclose(bin);
    ReleaseDC(0, sdc);
    return 0;
}
