/*
 * cr1.c - CR1: how often the display driver's cursor entries are called,
 * and by whom, under real mouse motion (tests/currec.sh; mouse trails,
 * drivers/vga/README.md). Run as the shell with CURREC.DRV (tests/gdi/
 * currec.asm) in front of the display driver under test: CR1 empties the
 * stand-in's record after Shift was pressed and let go, idles in GetMessage for
 * C:\CR1.IN's seconds (default 20) while tests/inputrun.sh moves the
 * mouse, then writes C:\CR1.LOG:
 *   tally ORD N                   calls of each counted export
 *   rec ORD TICK PIT CS:IP OWNER A0 A1 IF SS  (TICK, CS:IP, SS in hex)
 *                                 each SetCursor (102), MoveCursor (103:
 *                                 A0 y, A1 x), CheckCursor (104), with the
 *                                 BIOS tick, the PIT count, the caller and
 *                                 its module's file, interrupts on (IF)
 * With a second word "frames" in CR1.IN it also keeps, at each of its
 * timer messages (every tick), the screen's rows 100-299 when they
 * changed, in C:\CR1.BIN (tools/vg9png.py's format, phase 1), read from
 * the VGA's memory with interrupts off, as VG9 does.
 */
#include <windows.h>

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

extern void cli(void);
#pragma aux cli = "cli";
extern void sti(void);
#pragma aux sti = "sti";
extern void outb_(unsigned port, unsigned char v);
#pragma aux outb_ = "out dx,al" parm [dx] [al];
extern unsigned char inb_(unsigned port);
#pragma aux inb_ = "in al,dx" parm [dx] value [al];

typedef struct {
    WORD n, max, recsize, nent, tally, buf, tab;
} CRHDR;
typedef CRHDR FAR *(FAR PASCAL *CRINFO)(void);
typedef void (FAR PASCAL *CRRESET)(void);

static HFILE logf = HFILE_ERROR;
static char line[200];
static int lp;

static void put(const char FAR *s)
{
    while (*s && lp < (int)sizeof line - 1)
        line[lp++] = *s++;
}

static void putu(WORD v)
{
    char b[8];
    int i = 7;
    b[7] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    put(b + i);
}

static void puth(WORD v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[4] = 0;
    put(b);
}

static void flush(void)
{
    line[lp++] = '\r';
    line[lp++] = '\n';
    if (logf != HFILE_ERROR)
        _lwrite(logf, line, lp);
    lp = 0;
}

/* ---- frames ---- */
static WORD vram;
static BYTE FAR *blk, FAR *cur, FAR *prev;   /* blk: a header, then cur */
static HFILE bin = HFILE_ERROR;
static WORD nframes, badwrites;
#define Y0 100
#define H 200

static void grab(void)
{
    BYTE idx, rm, mode;
    int p, r;
    WORD c;
    BYTE FAR *v, FAR *d = cur;
    cli();
    idx = inb_(0x3CE);
    outb_(0x3CE, 4);
    rm = inb_(0x3CF);
    outb_(0x3CE, 5);
    mode = inb_(0x3CF);
    outb_(0x3CF, (BYTE)(mode & ~8));
    for (p = 0; p < 4; p++) {
        outb_(0x3CE, 4);
        outb_(0x3CF, (BYTE)p);
        for (r = 0; r < H; r++) {
            v = (BYTE FAR *)MAKELP(vram, (WORD)(Y0 + r) * 80);
            for (c = 0; c < 80; c++)
                *d++ = v[c];
        }
    }
    outb_(0x3CE, 5);
    outb_(0x3CF, mode);
    outb_(0x3CE, 4);
    outb_(0x3CF, rm);
    outb_(0x3CE, idx);
    sti();
}

static void frame(DWORD t0)
{
    struct hd {
        char tag[2];
        WORD seq, phase, y0, h;
        DWORD tick;
        WORD pad;
    } FAR *hd = (struct hd FAR *)blk;
    WORD n = 80U * H * 4, i;
    grab();
    for (i = 0; i < n; i++)
        if (cur[i] != prev[i])
            break;
    if (i == n)
        return;
    hd->tag[0] = 'F';
    hd->tag[1] = 'R';
    hd->seq = nframes++;
    hd->phase = 1;
    hd->y0 = Y0;
    hd->h = H;
    hd->tick = GetTickCount() - t0;
    hd->pad = 0;
    /* one write of header and bits (two writes once left a frame's
       header out) */
    if (_lwrite(bin, (LPCSTR)blk, n + 16) != n + 16)
        badwrites++;
    for (i = 0; i < n; i++)
        prev[i] = cur[i];
}

/* the module a code selector belongs to: GetExePtr (KERNEL.133,
   Schulman) of its handle, then the module's file name */
typedef HANDLE (FAR PASCAL *GEP)(HANDLE);
static GEP gep;

static void owner(WORD sel)
{
    char b[80];
    HANDLE h = (HANDLE)LOWORD(GlobalHandle(sel));
    HMODULE m;
    int n, k;
    if (!gep || !h || (m = (HMODULE)gep(h)) == 0 || (n = GetModuleFileName(m, b, sizeof b)) == 0) {
        put("?");
        return;
    }
    for (k = n; k > 0 && b[k - 1] != '\\'; k--)
        ;
    put(b + k);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = init_regs[5];
    HMODULE disp;
    CRINFO info;
    CRRESET reset;
    CRHDR FAR *h;
    OFSTRUCT of;
    HFILE f;
    char in[32];
    int secs = 20, frames = 0, i;
    DWORD t0, end;
    MSG m;
    WORD sel, ntimer = 0;

    U_INITAPP(hi);
    in[0] = 0;
    f = OpenFile("C:\\CR1.IN", &of, OF_READ);
    if (f != HFILE_ERROR) {
        int n = _lread(f, in, sizeof in - 1);
        in[n > 0 ? n : 0] = 0;
        _lclose(f);
    }
    if (in[0] >= '0' && in[0] <= '9') {
        secs = 0;
        for (i = 0; in[i] >= '0' && in[i] <= '9'; i++)
            secs = secs * 10 + in[i] - '0';
        while (in[i] == ' ')
            i++;
        frames = in[i] == 'f';
    }
    logf = _lcreat("C:\\CR1.LOG", 0);
    disp = GetModuleHandle("DISPLAY");
    info = (CRINFO)GetProcAddress(disp, MAKEINTRESOURCE(900));
    reset = (CRRESET)GetProcAddress(disp, MAKEINTRESOURCE(901));
    if (!info || !reset) {
        put("no CURREC");
        flush();
        _lclose(logf);
        return 1;
    }
    if (frames) {
        vram = LOWORD((DWORD)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(174)));
        blk = (BYTE FAR *)GlobalLock(GlobalAlloc(GMEM_FIXED, 80U * H * 4 + 16));
        cur = blk ? blk + 16 : NULL;
        prev = (BYTE FAR *)GlobalLock(GlobalAlloc(GMEM_FIXED, 80U * H * 4));
        bin = _lcreat("C:\\CR1.BIN", 0);
        if (!cur || !prev || bin == HFILE_ERROR)
            frames = 0;
    }
    SetTimer(NULL, 1, 20, NULL);
    /* the recording starts when Shift has been held down and let go
       (tests/inputrun.sh: keydown shift, sleep, keyup shift; DOSBox's
       uncaptured mouse moved the pointer by itself at times, so motion
       could not be the signal) */
    {
        BOOL down = FALSE;
        while (GetMessage(&m, NULL, 0, 0)) {
            DispatchMessage(&m);
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                down = TRUE;
            else if (down)
                break;
        }
    }
    t0 = GetTickCount();
    end = t0;
    for (i = 0; i < secs; i++)
        end += 1000;
    reset();
    while (GetTickCount() < end && GetMessage(&m, NULL, 0, 0)) {
        if (m.message == WM_TIMER)
            ntimer++;
        if (m.message == WM_TIMER && frames)
            frame(t0);
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    KillTimer(NULL, 1);
    put("timer messages ");
    putu(ntimer);
    flush();
    h = info();
    sel = SELECTOROF(h);
    gep = (GEP)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(133));
    put("secs ");
    putu(secs);
    put(" records ");
    putu(h->n);
    put(" frames ");
    putu(nframes);
    put(" badwrites ");
    putu(badwrites);
    flush();
    for (i = 0; i < (int)h->nent; i++) {
        DWORD FAR *t = (DWORD FAR *)MAKELP(sel, h->tally);
        WORD FAR *tab = (WORD FAR *)MAKELP(sel, h->tab + 14 * i);
        if (!t[i])
            continue;
        put("tally ");
        putu(tab[0]);
        put(" ");
        putu((WORD)t[i]);
        flush();
    }
    for (i = 0; i < (int)h->n; i++) {
        WORD FAR *r = (WORD FAR *)MAKELP(sel, h->buf + h->recsize * i);
        put("rec ");
        putu(r[0]);
        put(" ");
        puth(r[2]);
        puth(r[1]);
        put(" ");
        putu(r[3]);
        put(" ");
        puth(r[4]);
        put(":");
        puth(r[5]);
        put(" ");
        owner(r[4]);
        put(" ");
        putu(r[6]);
        put(" ");
        putu(r[7]);
        put(" ");
        putu((r[8] >> 9) & 1);
        put(" ");
        puth(r[9]);
        flush();
    }
    put("end");
    flush();
    _lclose(logf);
    if (bin != HFILE_ERROR)
        _lclose(bin);
    return 0;
}
