/*
 * lzp.c - LZP.EXE, the LZEXPAND probe (ARCHITECTURE.md, LZEXPAND): calls
 * every function of LZEXPAND.DLL on the files tests/lzexpand/mkinputs.py
 * makes (in C:\LZ) and logs each call, its result and what it did to the
 * files and handles, to C:\LZP.LOG. Run as the shell on the reference 3.1
 * with Microsoft's LZEXPAND and with w16's (tests/lzexpand/lzrun.sh); the
 * two logs must be equal (tests/lzexpand/lzscen.sh compares them section
 * by section). With "child ..." on its command line it is the second task
 * of the multitasking section.
 *
 * Sections ("== NAME"): LZInit (every kind of file, plain, compressed,
 * broken), LZOpenFile (the OF_ styles, the second try with the compressed
 * name), LZRead, LZSeek, LZClose (handle numbers), LZCopy, LZStart
 * (CopyLZFile, LZDone), GetExpandedName, limit (16 compressed files),
 * tasks (a second task), eof (data that ends before the header's size),
 * zero (a 0-byte read first), seek back, long matches (17 and 18 bytes,
 * which COMPRESS does not write), VER.DLL (VerInstallFile, a 3.1 caller
 * of LZOpenFile and LZCopy, and the library's version).
 *
 * The explorations that worked out how 3.1's LZRead expands (the shell
 * has no arguments: tests/lzexpand/lzrun.sh's LZRUN_ARG writes C:\LZP.ARG):
 * x reads of every size and in sequence, y and w where a failing read
 * wrote into the buffer, z and q the largest read that succeeds after a
 * first one (bisection), k the same on COMPRESS.EXE's own output of the
 * files (C:\LZ\K\*, made by the user with the SDK's COMPRESS in DOSBox;
 * not part of the tests), v GetFileVersionInfoSize of the libraries.
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 9 and
 * Vol. 2 (the LZ functions, GetExpandedName, OpenFile).
 */
#include <windows.h>
#include <lzexpand.h>
#include <ver.h>

extern WORD __cdecl init_regs[7];       /* tests/apps/start.asm */

static const char logname[] = "C:\\LZP.LOG";
static char line[600];
static BYTE __far *buf;                 /* 40000 bytes */
static BYTE __far *buf2;                /* 40000 bytes */
static int child;
static char tag[8] = "";

/* no C runtime (-zl) */
static void fill(void FAR *d, int c, unsigned n)
{
    BYTE FAR *p = d;
    while (n--)
        *p++ = (BYTE)c;
}

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

static void lg(const char *fmt, ...)
{
    int n = lstrlen(tag);
    lstrcpy(line, tag);
    n += wvsprintf(line + n, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

/* a summary of bytes: count, rotating sum, the first and last bytes */
static char *summ(const BYTE FAR *p, long n)
{
    static char s[100];
    WORD sum = 0;
    long i;
    int k = 0, j;
    if (n <= 0) {
        wsprintf(s, "-");
        return s;
    }
    for (i = 0; i < n; i++)
        sum = ((sum << 1) | (sum >> 15)) + p[i];
    k = wsprintf(s, "sum=%04X [", sum);
    for (j = 0; j < 8 && j < n; j++)
        k += wsprintf(s + k, "%02X", p[j]);
    if (n > 8) {
        k += wsprintf(s + k, "..");
        for (j = (n > 12 ? (int)n - 4 : 8); j < n; j++)
            k += wsprintf(s + k, "%02X", p[j]);
    }
    wsprintf(s + k, "]");
    return s;
}

static void path(char *d, const char *name)
{
    lstrcpy(d, "C:\\LZ\\");
    lstrcat(d, name);
}

/* where a DOS handle stands (-1: not open) */
static long dpos(HFILE h)
{
    return _llseek(h, 0, 1);
}

/* the next DOS handle a task would get */
static int nextdos(void)
{
    HFILE h = _lopen("C:\\LZ\\TEXT.TXT", OF_READ);
    if (h != HFILE_ERROR)
        _lclose(h);
    return h;
}

static void rd(const char *what, HFILE h, int cb)
{
    int n;
    fill(buf, 0xAA, 64);
    n = LZRead(h, buf, cb);
    lg("  LZRead(%s, %d) = %d %s", (LPSTR)what, cb, n, (LPSTR)summ(buf, n));
}

static void sk(const char *what, HFILE h, long off, int org)
{
    long r = LZSeek(h, off, org);
    lg("  LZSeek(%s, %ld, %d) = %ld", (LPSTR)what, off, org, r);
}

static void dumpof(OFSTRUCT *of)
{
    lg("    of: cBytes=%d fFixedDisk=%d nErrCode=%d path=\"%s\"",
       of->cBytes, of->fFixedDisk, of->nErrCode, (LPSTR)of->szPathName);
}

/* read a whole file back (after LZCopy) */
static void readback(const char *name)
{
    char p[80];
    HFILE h;
    long tot = 0;
    int n;
    path(p, name);
    h = _lopen(p, OF_READ);
    if (h == HFILE_ERROR) {
        lg("    %s: cannot open", (LPSTR)name);
        return;
    }
    tot = _llseek(h, 0, 2);
    _llseek(h, 0, 0);
    n = _lread(h, buf2, tot > 32000 ? 32000 : (int)tot);
    lg("    %s: size %ld, first %d: %s", (LPSTR)name, tot, n, (LPSTR)summ(buf2, n));
    if (tot > 32000) {
        _llseek(h, tot - 1000, 0);
        n = _lread(h, buf2, 1000);
        lg("    %s: last %d: %s", (LPSTR)name, n, (LPSTR)summ(buf2, n));
    }
    _lclose(h);
}

/* ---- LZInit and the files, plain and broken ---- */
static const char *const files[] = {
    "TEXT.TX_", "TEXT.TXT", "NOCHAR.DA_", "EMPTY.TX_", "BADALG.TX_",
    "ALG0.TX_", "TRUNC.TX_", "HDR.TX_", "HDR13.TX_", "MAGIC.TX_",
    "NODATA.TX_", "SHORTSZ.TX_", "LONGSZ.TX_", "QB.TX_", "SPACES.TX_",
    "LIT.TX_", "LOWER.TX_", "TINY.TX_", "ZERO.TX_", "PLAIN.EX_", 0
};

static void t_init(void)
{
    char p[80];
    HFILE h, r, r2;
    int i, n;
    lg("== LZInit");
    path(p, "TEXT.TX_");
    h = _lopen(p, OF_READ);
    r = LZInit(h);
    lg("  _lopen TEXT.TX_ = %d; LZInit = %d (%04X); dos pos %ld", h, r, r, dpos(h));
    rd("lz", r, 10);
    lg("  dos pos %ld", dpos(h));
    rd("lz", r, 100);
    lg("  dos pos %ld", dpos(h));
    r2 = LZInit(h);
    lg("  LZInit(same dos handle) again = %d; dos pos %ld", r2, dpos(h));
    if (r2 >= 0 && r2 != h) {
        rd("lz2", r2, 10);
        LZClose(r2);
        lg("  LZClose(lz2); dos pos %ld", dpos(h));
    }
    r2 = LZInit(r);
    lg("  LZInit(the LZ handle %d) = %d", r, r2);
    LZClose(r);
    lg("  LZClose(lz): dos pos %ld, _lclose(dos) = %d", dpos(h), _lclose(h));

    path(p, "TEXT.TX_");
    h = _lopen(p, OF_READ);
    _llseek(h, 5, 0);
    r = LZInit(h);
    lg("  at 5: LZInit = %d; dos pos %ld", r, dpos(h));
    if (r >= 0) {
        rd("lz", r, 20);
        LZClose(r);
    }
    _lclose(h);

    for (i = 0; files[i]; i++) {
        path(p, files[i]);
        h = _lopen(p, OF_READ);
        r = LZInit(h);
        lg(" %s: dos %d LZInit = %d; dos pos %ld", (LPSTR)files[i], h, r, dpos(h));
        if (r < 0) {
            lg("  _lclose(dos) = %d", _lclose(h));
            continue;
        }
        fill(buf, 0xAA, 64);
        n = LZRead(r, buf, 8000);
        lg("  LZRead(8000) = %d %s", n, (LPSTR)summ(buf, n));
        rd("lz", r, 100);
        sk("lz", r, 0, 2);
        sk("lz", r, 3, 0);
        rd("lz", r, 8);
        LZClose(r);
        lg("  LZClose: dos pos %ld; next dos %d", dpos(h), nextdos());
        if (r == h)
            continue;
        _lclose(h);
    }
    r = LZInit(0x7F);
    lg(" LZInit(7F) = %d", r);
    /* the caller moves the file's pointer between reads */
    path(p, "BIG.DA_");
    h = _lopen(p, OF_READ);
    r = LZInit(h);
    rd("lz", r, 10);
    _llseek(h, 0, 0);
    rd("lz after _llseek(dos, 0)", r, 3000);
    lg("  dos pos %ld", dpos(h));
    _llseek(h, 500, 0);
    rd("lz after _llseek(dos, 500)", r, 3000);
    lg("  dos pos %ld", dpos(h));
    LZClose(r);
    r = LZInit(-1);
    lg(" LZInit(-1) = %d", r);
    lg(" next dos %d", nextdos());
}

/* ---- LZOpenFile and its styles ---- */
static void t_open1(const char *name, UINT style, int keep)
{
    char p[80];
    OFSTRUCT of;
    HFILE h;
    fill(&of, 0x55, sizeof of);
    if (name[1] == ':')
        lstrcpy(p, name);
    else if (name[0] == '=')
        lstrcpy(p, name + 1);
    else
        path(p, name);
    h = LZOpenFile(p, &of, style);
    lg(" LZOpenFile(%s, %04X) = %d (%04X); next dos %d", (LPSTR)p, style, h, h, nextdos());
    dumpof(&of);
    if (h >= 0 && !(style & (OF_EXIST | OF_PARSE | OF_DELETE))) {
        if (!(style & OF_WRITE))
            rd("h", h, 16);
        if (!keep) {
            LZClose(h);
            lg("  LZClose; next dos %d", nextdos());
        }
    }
}

static void t_open(void)
{
    OFSTRUCT of;
    char p[80];
    HFILE h, h2;
    lg("== LZOpenFile");
    t_open1("TEXT.TX_", OF_READ, 0);
    t_open1("TEXT.TXT", OF_READ, 0);
    t_open1("c:\\lz\\text.tx_", OF_READ, 0);
    t_open1("TEXT.TX_", OF_READ | OF_SHARE_DENY_WRITE, 0);
    t_open1("TEXT.TX_", OF_READ | OF_SHARE_DENY_NONE, 0);
    t_open1("TEXT.TX_", OF_READ | OF_SHARE_EXCLUSIVE, 0);
    t_open1("TEXT.TX_", OF_READ | OF_SHARE_COMPAT, 0);
    t_open1("TEXT.TX_", OF_READWRITE, 0);
    t_open1("TEXT.TX_", OF_WRITE, 0);
    t_open1("TEXT.TX_", OF_EXIST, 0);
    t_open1("TEXT.TX_", OF_EXIST | OF_READ, 0);
    t_open1("TEXT.TX_", OF_PARSE, 0);
    t_open1("TEXT.TX_", OF_READ | OF_PARSE, 0);
    t_open1("MISSING.TX_", OF_READ, 0);
    t_open1("MISSING.TX_", OF_EXIST, 0);
    t_open1("MISSING.TX_", OF_PARSE, 0);
    t_open1("ONLYC.TXT", OF_READ, 0);
    t_open1("ONLYC.TXT", OF_EXIST, 0);
    t_open1("MISSING", OF_READ, 0);
    t_open1("TEXT.TX_", 0x8000 | OF_READ, 0);       /* OF_REOPEN without a buffer */
    /* the compressed name tried after a failure */
    t_open1("MISS.A", OF_READ, 0);
    t_open1("MISS.AB", OF_READ, 0);
    t_open1("MISS.ABC", OF_READ, 0);
    t_open1("A.B\\MISS", OF_READ, 0);
    t_open1("ONE.E", OF_READ, 0);
    t_open1("NONE", OF_READ, 0);
    t_open1("TEXT.TX", OF_READ, 0);
    t_open1("=SYSFILE.TXT", OF_READ, 0);
    t_open1("=SYSFILE.TX_", OF_READ, 0);
    t_open1("C:\\LZ\\ONLYC.TXT", OF_READWRITE, 0);
    t_open1("C:\\LZ\\ONLYC.TXT", OF_WRITE, 0);
    t_open1("C:\\LZ\\ONLYC.TXT", OF_READ | OF_SHARE_DENY_WRITE, 0);
    t_open1("NEW1.OUT", OF_CREATE, 0);
    t_open1("NEW1.OUT", OF_READ, 0);
    t_open1("NEW1.OUT", OF_CREATE | OF_READWRITE, 0);
    t_open1("NEW1.OUT", OF_DELETE, 0);
    t_open1("NEW1.OUT", OF_DELETE, 0);
    t_open1("NEW1.OUT", OF_EXIST, 0);
    t_open1("BADALG.TX_", OF_READ, 0);
    t_open1("TRUNC.TX_", OF_READ, 0);
    t_open1("HDR.TX_", OF_READ, 0);
    t_open1("EMPTY.TX_", OF_READ, 0);
    t_open1("ZERO.TX_", OF_READ, 0);
    t_open1("QB.TX_", OF_READ, 0);
    t_open1("CREAT.TX_", OF_READ | OF_CREATE, 0);   /* a compressed file: truncated */
    t_open1("CREAT.TX_", OF_READ, 0);
    /* OF_REOPEN with the structure of an earlier open */
    fill(&of, 0, sizeof of);
    path(p, "BIG.DA_");
    h = LZOpenFile(p, &of, OF_READ);
    lg(" BIG.DA_ OF_READ = %d", h);
    rd("h", h, 10);
    h2 = LZOpenFile("C:\\JUNK", &of, OF_READ | OF_REOPEN);
    lg(" OF_REOPEN|OF_READ = %d; next dos %d", h2, nextdos());
    dumpof(&of);
    rd("h2", h2, 10);
    rd("h", h, 10);
    LZClose(h2);
    LZClose(h);
    h = LZOpenFile("C:\\JUNK", &of, OF_EXIST | OF_REOPEN);
    lg(" OF_REOPEN|OF_EXIST = %d", h);
    lg(" next dos %d", nextdos());
}

/* ---- LZRead ---- */
static void t_read(void)
{
    char p[80];
    OFSTRUCT of;
    HFILE h, d;
    long tot;
    WORD sum;
    int n, i;
    lg("== LZRead");
    path(p, "TEXT.TX_");
    h = LZOpenFile(p, &of, OF_READ);
    lg(" TEXT.TX_ = %d", h);
    /* (a 0-byte read first on a new handle: == zero) */
    rd("h", h, 1);
    rd("h", h, 0);
    rd("h", h, -1);
    rd("h", h, -32768);
    rd("h", h, 100);
    rd("h", h, 4000);
    rd("h", h, 32767);
    rd("h", h, 10);
    rd("h", h, 10);
    LZClose(h);
    for (i = 0; i < 2; i++) {
        path(p, i ? "BIG.DAT" : "BIG.DA_");
        h = LZOpenFile(p, &of, OF_READ);
        tot = 0;
        sum = 0;
        do {
            n = LZRead(h, buf, i ? 4000 : 3999);
            if (n > 0) {
                int j;
                for (j = 0; j < n; j++)
                    sum = ((sum << 1) | (sum >> 15)) + buf[j];
                tot += n;
            }
        } while (n > 0);
        lg(" %s = %d: read %ld bytes, sum %04X, last %d", (LPSTR)p, h, tot, sum, n);
        LZClose(h);
    }
    path(p, "TEXT.TXT");
    d = _lopen(p, OF_READ);
    lg(" plain dos %d", d);
    rd("dos", d, 10);
    rd("dos", d, -1);
    rd("dos", d, 0);
    rd("dos", d, 32767);
    rd("dos", d, 10);
    _lclose(d);
    rd("closed dos", d, 10);
    rd("7F", 0x7F, 10);
    rd("-1", -1, 10);
}

/* ---- LZSeek ---- */
static void t_seek(void)
{
    char p[80];
    OFSTRUCT of;
    HFILE h, d;
    lg("== LZSeek");
    path(p, "TEXT.TX_");
    h = LZOpenFile(p, &of, OF_READ);
    lg(" TEXT.TX_ = %d", h);
    sk("h", h, 0, 0);
    sk("h", h, 100, 0);
    rd("h", h, 10);
    sk("h", h, 5, 1);
    rd("h", h, 10);
    sk("h", h, -10, 2);
    rd("h", h, 20);
    sk("h", h, 0, 2);
    sk("h", h, 100, 2);
    rd("h", h, 10);
    sk("h", h, 0, 1);
    sk("h", h, -1, 0);
    sk("h", h, 0, 1);
    sk("h", h, 0, 3);
    sk("h", h, 0, -1);
    sk("h", h, -7000, 2);
    sk("h", h, -100000L, 1);
    sk("h", h, 0, 1);
    sk("h", h, 50, 0);
    rd("h", h, 10);
    sk("h", h, 6000, 0);
    rd("h", h, 10);
    sk("h", h, 10, 0);
    rd("h", h, 10);
    sk("h", h, 6835, 0);
    rd("h", h, 10);
    sk("h", h, 6840, 0);
    rd("h", h, 10);
    sk("h", h, 70000L, 0);
    rd("h", h, 10);
    sk("h", h, 0, 0);
    rd("h", h, 10);
    LZClose(h);
    path(p, "BIG.DA_");
    h = LZOpenFile(p, &of, OF_READ);
    sk("big", h, 65530L, 0);
    rd("big", h, 20);
    sk("big", h, 69990L, 0);
    rd("big", h, 20);
    sk("big", h, 1, 0);
    rd("big", h, 5);
    sk("big", h, -5000, 2);
    rd("big", h, 5000);
    LZClose(h);
    path(p, "SPACES.TX_");
    h = LZOpenFile(p, &of, OF_READ);
    sk("spaces", h, 30, 0);
    rd("spaces", h, 40);
    LZClose(h);
    path(p, "TEXT.TXT");
    d = _lopen(p, OF_READ);
    sk("dos", d, 10, 0);
    rd("dos", d, 4);
    sk("dos", d, -1, 0);
    sk("dos", d, 0, 1);
    sk("dos", d, 0, 3);
    sk("dos", d, 100, 2);
    _lclose(d);
    sk("closed dos", d, 0, 0);
    sk("7F", 0x7F, 0, 0);
}

/* ---- LZClose and handle numbers ---- */
static void t_close(void)
{
    char p[80];
    OFSTRUCT of;
    HFILE a, b, c, d, e;
    lg("== LZClose");
    path(p, "TEXT.TX_");
    a = LZOpenFile(p, &of, OF_READ);
    b = LZOpenFile(p, &of, OF_READ);
    c = LZOpenFile(p, &of, OF_READ);
    lg(" a=%d b=%d c=%d; next dos %d", a, b, c, nextdos());
    LZClose(b);
    lg(" LZClose(b); next dos %d", nextdos());
    rd("b", b, 10);
    sk("b", b, 0, 0);
    LZClose(b);
    lg(" LZClose(b) again; next dos %d", nextdos());
    d = LZOpenFile(p, &of, OF_READ);
    e = LZOpenFile(p, &of, OF_READ);
    lg(" d=%d e=%d", d, e);
    LZClose(a);
    LZClose(c);
    LZClose(d);
    LZClose(e);
    lg(" closed a c d e; next dos %d", nextdos());
    LZClose(0x7F);
    LZClose(a + 7);
    lg(" LZClose(7F), (a+7); next dos %d", nextdos());
    a = LZOpenFile(p, &of, OF_READ);
    lg(" again a=%d", a);
    LZClose(a);
}

/* ---- LZCopy ---- */
static HFILE src(const char *name, int how)
{
    char p[80];
    OFSTRUCT of;
    path(p, name);
    if (how == 0)
        return LZOpenFile(p, &of, OF_READ);
    return _lopen(p, OF_READ);
}

static void copy1(const char *name, int how, int pre)
{
    char p[80];
    HFILE s, d;
    long r;
    s = src(name, how);
    if (pre)
        rd("s", s, pre);
    path(p, "OUT.DAT");
    d = _lcreat(p, 0);
    r = LZCopy(s, d);
    lg(" LZCopy(%s %s %d, %d) = %ld; dst pos %ld; next dos %d", (LPSTR)name,
       (LPSTR)(how ? "dos" : "lz"), s, d, r, dpos(d), nextdos());
    rd("s after", s, 10);
    sk("s after", s, 0, 1);
    LZClose(s);
    _lclose(d);
    readback("OUT.DAT");
}

static void t_copy(void)
{
    char p[80];
    HFILE s, d;
    long r;
    static const char *const bad[] = {
        "TRUNC.TX_", "BADALG.TX_", "ALG0.TX_", "LONGSZ.TX_", "SHORTSZ.TX_",
        "QB.TX_", "NODATA.TX_", "HDR.TX_", "MAGIC.TX_", "EMPTY.TX_",
        "ZERO.TX_", "SPACES.TX_", "LIT.TX_", 0
    };
    int i;
    lg("== LZCopy");
    copy1("TEXT.TX_", 0, 0);
    copy1("TEXT.TX_", 1, 0);
    copy1("TEXT.TXT", 0, 0);
    copy1("TEXT.TXT", 1, 0);
    copy1("TEXT.TX_", 0, 100);
    copy1("TEXT.TXT", 1, 100);
    copy1("BIG.DA_", 0, 0);
    copy1("BIG.DA_", 1, 0);
    copy1("BIG.DAT", 1, 0);
    for (i = 0; bad[i]; i++) {
        copy1(bad[i], 0, 0);
        copy1(bad[i], 1, 0);
    }
    /* truncated at several points: what reaches the destination */
    copy1("CUT3000.DA_", 0, 0);
    copy1("CUT5000.DA_", 0, 0);
    copy1("CUT9000.DA_", 0, 0);
    copy1("CUT17000.DA_", 0, 0);
    copy1("CUT30000.DA_", 1, 0);
    /* where LZCopy leaves the source: how much it reads at a time */
    copy1("SHORTBIG.DA_", 1, 0);
    /* a destination that already holds data, then a stream cut inside a
       match: what is left of it */
    {
        static const char *const cf[] = { "TRUNC.TX_", "CUT9000.DA_", "LONGSZ.TX_", 0 };
        for (i = 0; cf[i]; i++) {
            s = src(cf[i], 0);
            path(p, "OUT.DAT");
            d = _lcreat(p, 0);
            _lwrite(d, (LPCSTR)"0123456789", 10);
            r = LZCopy(s, d);
            lg(" LZCopy(%s) after 10 bytes = %ld; dst pos %ld", (LPSTR)cf[i], r, dpos(d));
            LZClose(s);
            _lclose(d);
            readback("OUT.DAT");
        }
    }
    /* bad handles */
    path(p, "OUT.DAT");
    d = _lcreat(p, 0);
    r = LZCopy(0x7F, d);
    lg(" LZCopy(7F, dst) = %ld; next dos %d", r, nextdos());
    _lclose(d);
    s = src("TEXT.TX_", 0);
    r = LZCopy(s, 0x7F);
    lg(" LZCopy(lz, 7F) = %ld; next dos %d", r, nextdos());
    rd("s after", s, 10);
    LZClose(s);
    s = src("TEXT.TX_", 0);
    path(p, "TEXT.TXT");
    d = _lopen(p, OF_READ);
    r = LZCopy(s, d);
    lg(" LZCopy(lz, read-only dst) = %ld; next dos %d", r, nextdos());
    _lclose(d);
    LZClose(s);
    s = src("TEXT.TXT", 1);
    r = LZCopy(s, 0x7F);
    lg(" LZCopy(dos, 7F) = %ld; next dos %d", r, nextdos());
    _lclose(s);
    s = src("TEXT.TX_", 0);
    LZClose(s);
    path(p, "OUT.DAT");
    d = _lcreat(p, 0);
    r = LZCopy(s, d);
    lg(" LZCopy(closed lz, dst) = %ld; next dos %d", r, nextdos());
    _lclose(d);
}

/* ---- LZStart, LZDone, CopyLZFile ---- */
static void clz(const char *name, int how)
{
    char p[80];
    HFILE s, d;
    long r;
    s = src(name, how);
    path(p, "OUT2.DAT");
    d = _lcreat(p, 0);
    r = CopyLZFile(s, d);
    lg(" CopyLZFile(%s %s) = %ld; next dos %d", (LPSTR)name, (LPSTR)(how ? "dos" : "lz"),
       r, nextdos());
    LZClose(s);
    _lclose(d);
    readback("OUT2.DAT");
}

static void t_start(void)
{
    int r;
    lg("== LZStart");
    LZDone();
    lg(" LZDone() first; next dos %d", nextdos());
    clz("TEXT.TX_", 0);
    r = LZStart();
    lg(" LZStart() = %d", r);
    r = LZStart();
    lg(" LZStart() again = %d", r);
    clz("TEXT.TX_", 0);
    clz("TEXT.TX_", 1);
    clz("BIG.DA_", 0);
    clz("TEXT.TXT", 1);
    clz("TRUNC.TX_", 0);
    clz("CUT9000.DA_", 0);
    clz("CUT30000.DA_", 0);
    LZDone();
    lg(" LZDone()");
    clz("TEXT.TX_", 0);
    LZDone();
    lg(" LZDone() again");
    r = LZStart();
    lg(" LZStart() = %d", r);
    copy1("TEXT.TX_", 0, 0);
    LZDone();
}

/* ---- GetExpandedName ---- */
static void gen(const char *name)
{
    char p[100], out[140];
    int r, i, n;
    if (name[0] && name[1] == ':')
        lstrcpy(p, name);
    else if (name[0] == '=')
        lstrcpy(p, name + 1);
    else
        path(p, name);
    for (i = 0; i < sizeof out - 1; i++)
        out[i] = '#';
    out[sizeof out - 1] = 0;
    r = GetExpandedName(p, out);
    n = lstrlen(out);
    lg(" GetExpandedName(\"%s\") = %d \"%s\" (last %02X) then %02X %02X; next dos %d",
       (LPSTR)p, r, (LPSTR)out, n ? (BYTE)out[n - 1] : 0, (BYTE)out[n + 1],
       (BYTE)out[n + 2], nextdos());
}

static void t_name(void)
{
    static const char *const names[] = {
        "TEXT.TX_", "c:\\lz\\text.tx_", "c:\\lz\\Text.Tx_", "C:\\LZ\\TEXT.Tx_",
        "NOCHAR.DA_", "LOWER.TX_", "c:\\lz\\lower.tx_", "NOEXT", "c:\\lz\\noext",
        "ONE.E_", "NONE._", "NONE0._", "NOUND.ABC", "NOUND0.ABC", "TWO.AB",
        "UNDER_", "DIGIT.DA_", "c:\\lz\\digit.da_", "HIGH.DA_", "c:\\lz\\high.da_",
        "PLAIN.EX_", "TEXT.TXT", "MISSING.TX_", "A.B\\NAME", "c:\\lz\\a.b\\name",
        "QB.TX_", "BADALG.TX_", "HDR.TX_", "HDR13.TX_", "MAGIC.TX_", "EMPTY.TX_",
        "ZERO.TX_", "TINY.TX_", "=TEXT.TX_", "=C:TEXT.TX_", "=\\LZ\\TEXT.TX_",
        "=c:/lz/text.tx_", "=C:\\LZ\\.\\TEXT.TX_", "=C:\\LZ\\..\\LZ\\TEXT.TX_",
        /* which letters decide the case */
        "C:\\LZ\\text.tx_", "c:\\lz\\TEXT.TX_", "C:\\LZ\\TEXT.tX_", "C:\\LZ\\tEXT.TX_",
        "C:\\lz\\TEXT.TX_", "c:\\LZ\\TEXT.TX_", "C:\\LZ\\TEXT.tx_", "C:\\LZ\\text.TX_",
        "c:\\lz\\text.Tx_", "c:\\lz\\texT.tx_", "c:\\lz\\digit.dA_",
        "=\\lz\\text.tx_", "=\\LZ\\text.tx_",
        "A.B\\UNDER_", "=SYSFILE.TX_", "=C:\\LZ\\NONE._", 0
    };
    int i;
    lg("== GetExpandedName");
    for (i = 0; names[i]; i++)
        gen(names[i]);
}

/* ---- how many compressed files at once ---- */
static void t_limit(void)
{
    char p[80], nm[20];
    OFSTRUCT of;
    HFILE h[20], d, r;
    int i, n = 0;
    lg("== limit");
    for (i = 0; i < 18; i++) {
        wsprintf(nm, "MANY%02d.TX_", i);
        path(p, nm);
        h[i] = LZOpenFile(p, &of, OF_READ);
        lg(" %s = %d (%04X); nErrCode %d; next dos %d", (LPSTR)nm, h[i], h[i], of.nErrCode,
           nextdos());
    }
    path(p, "TEXT.TXT");
    r = LZOpenFile(p, &of, OF_READ);
    lg(" plain TEXT.TXT when full = %d", r);
    if (r >= 0)
        LZClose(r);
    path(p, "MANY17.TX_");
    d = _lopen(p, OF_READ);
    r = LZInit(d);
    lg(" LZInit when full: dos %d = %d; dos pos %ld; next dos %d", d, r, dpos(d), nextdos());
    _lclose(d);
    for (i = 0; i < 18; i++) {
        if (h[i] >= 0) {
            n++;
            if (i == 3 || i == 0)
                rd("h", h[i], 10);
        }
    }
    lg(" %d open", n);
    LZClose(h[5]);
    path(p, "MANY17.TX_");
    r = LZOpenFile(p, &of, OF_READ);
    lg(" after closing the 6th: %d", r);
    rd("r", r, 10);
    LZClose(r);
    for (i = 0; i < 18; i++)
        if (h[i] >= 0 && i != 5)
            LZClose(h[i]);
    lg(" all closed; next dos %d", nextdos());
}

/* ---- two tasks ---- */
static int hexarg(const char FAR **s)
{
    int v = 0, k = 0;
    while (**s == ' ')
        (*s)++;
    if (**s == '-') {
        (*s)++;
        k = 1;
    }
    for (;;) {
        char c = **s;
        if (c >= '0' && c <= '9')
            v = v * 16 + c - '0';
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + c - 'A' + 10;
        else
            break;
        (*s)++;
    }
    return k ? -v : v;
}

static void t_child(const char FAR *cmd)
{
    char p[80];
    OFSTRUCT of;
    HFILE a, b, pa, pb;
    lstrcpy(tag, "child:");
    pa = hexarg(&cmd);
    pb = hexarg(&cmd);
    lg(" parent's handles %d %d; next dos %d", pa, pb, nextdos());
    path(p, "TEXT.TX_");
    a = LZOpenFile(p, &of, OF_READ);
    lg(" TEXT.TX_ = %d (%04X)", a, a);
    rd("a", a, 10);
    rd("parent's a", pa, 10);
    sk("parent's b", pb, 0, 1);
    path(p, "NOCHAR.DA_");
    b = LZOpenFile(p, &of, OF_READ);
    lg(" NOCHAR.DA_ = %d (%04X)", b, b);
    LZClose(a);
    rd("b", b, 10);
    lg(" ends with b open; next dos %d", nextdos());
}

static void t_tasks(void)
{
    char p[80], cmd[80];
    OFSTRUCT of;
    HFILE a, b, c, x[20];
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    UINT r;
    DWORD t;
    int i, n;
    lg("== tasks");
    path(p, "TEXT.TX_");
    a = LZOpenFile(p, &of, OF_READ);
    path(p, "BIG.DA_");
    b = LZOpenFile(p, &of, OF_READ);
    rd("a", a, 10);
    lg(" a=%d b=%d", a, b);
    wsprintf(cmd, "C:\\LZP.EXE child %X %X", a, b);
    r = WinExec(cmd, SW_SHOWNORMAL);
    lg(" WinExec = %s", (LPSTR)(r > 32 ? "ok" : "failed"));
    t = GetTickCount();
    while (GetModuleUsage(hi) > 1 && GetTickCount() - t < 30000)
        Yield();
    lg(" child done: usage %d", GetModuleUsage(hi));
    rd("a", a, 10);
    sk("b", b, 0, 1);
    path(p, "TEXT.TX_");
    c = LZOpenFile(p, &of, OF_READ);
    lg(" new c=%d", c);
    LZClose(a);
    LZClose(b);
    LZClose(c);
    lg(" closed a b c; next dos %d", nextdos());
    /* the child's handle left open: what is it to this task now? */
    rd("child's b", c + 1, 10);
    rd("child's b+1", c + 2, 10);
    n = 0;
    for (i = 0; i < 18; i++) {
        x[i] = LZOpenFile(p, &of, OF_READ);
        if (x[i] >= 0)
            n++;
    }
    lg(" %d more open: first %d last %d", n, x[0], x[17]);
    for (i = 0; i < 18; i++)
        if (x[i] >= 0)
            LZClose(x[i]);
    lg(" next dos %d", nextdos());
}

/* ---- exploration: reads of every size and in sequence, with the
   positions on both sides (LZSeek(h, 0, 1) and the DOS handle's) ---- */
static HFILE xopen(const char *name, HFILE *dos)
{
    char p[80];
    path(p, name);
    *dos = _lopen(p, OF_READ);
    return LZInit(*dos);
}

static void xrd(HFILE h, HFILE d, int cb)
{
    int n;
    long at;
    fill(buf, 0xAA, 64);
    n = LZRead(h, buf, cb);
    at = LZSeek(h, 0, 1);
    lg("  LZRead(%d) = %d %s; at %ld dos %ld", cb, n, (LPSTR)summ(buf, n), at, dpos(d));
}

static void t_explore(void)
{
    static const int tsz[] = { 1, 500, 1000, 2000, 4000, 4096, 4097, 5000, 6000, 6400,
                               6410, 6411, 6500, 6839, 6840, 6841, 8000, 0 };
    static const int bsz[] = { 100, 1000, 1500, 2000, 2500, 3000, 3999, 4096, 0 };
    HFILE h, d;
    int i;
    lg("== explore");
    {
        static const char *const xf[] = { "TEXT.TX_", "BIG.DA_", "LITBIG.TX_", "RUNS.TX_",
                                          "FAR.TX_", 0 };
        static const int xs[] = { 1013, 1014, 1500, 2000, 2048, 2049, 2500, 3000, 3072, 3073,
                                  3500, 4000, 7000, 0 };
        int f;
        for (f = 0; xf[f]; f++)
            for (i = 0; xs[i]; i++) {
                h = xopen(xf[f], &d);
                lg(" %s fresh:", (LPSTR)xf[f]);
                xrd(h, d, xs[i]);
                LZClose(h);
            }
        for (i = 0; xs[i]; i++) {
            h = xopen("TEXT.TX_", &d);
            lg(" TEXT 500 first:");
            xrd(h, d, 500);
            xrd(h, d, xs[i]);
            LZClose(h);
        }
    }
    for (i = 0; tsz[i]; i++) {
        h = xopen("TEXT.TX_", &d);
        lg(" TEXT fresh:");
        xrd(h, d, tsz[i]);
        xrd(h, d, 10);
        LZClose(h);
    }
    for (i = 0; bsz[i]; i++) {
        h = xopen("BIG.DA_", &d);
        lg(" BIG fresh:");
        xrd(h, d, bsz[i]);
        xrd(h, d, 10);
        LZClose(h);
    }
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT by 1000:");
    for (i = 0; i < 9; i++)
        xrd(h, d, 1000);
    LZClose(h);
    h = xopen("BIG.DA_", &d);
    lg(" BIG by 700:");
    for (i = 0; i < 8; i++)
        xrd(h, d, 700);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT 0 first:");
    xrd(h, d, 0);
    xrd(h, d, 10);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT 10, 0, 10:");
    xrd(h, d, 10);
    xrd(h, d, 0);
    xrd(h, d, 10);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT -1 first:");
    xrd(h, d, -1);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT seek 0 then 8000:");
    LZSeek(h, 0, 0);
    xrd(h, d, 8000);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT seek 5 then 8000:");
    LZSeek(h, 5, 0);
    xrd(h, d, 8000);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" TEXT 10 then seek 5 then 8000:");
    xrd(h, d, 10);
    LZSeek(h, 5, 0);
    xrd(h, d, 8000);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("SHORTSZ.TX_", &d);
    lg(" SHORTSZ:");
    xrd(h, d, 99);
    xrd(h, d, 1);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("SHORTSZ.TX_", &d);
    xrd(h, d, 100);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("SHORTSZ.TX_", &d);
    xrd(h, d, 101);
    LZClose(h);
    h = xopen("LONGSZ.TX_", &d);
    lg(" LONGSZ:");
    xrd(h, d, 499);
    xrd(h, d, 1);
    xrd(h, d, 1);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("NODATA.TX_", &d);
    lg(" NODATA:");
    xrd(h, d, 1);
    xrd(h, d, 0);
    LZClose(h);
}

/* a read compared with the plain file: where the buffer first differs
   from the data, and how far it was written */
static void ycase(const char *cname, const char *pname, int k, int cb)
{
    char p[80];
    HFILE h, d, pf;
    int n, i, first = -1, last = -1, pn;
    path(p, pname);
    pf = _lopen(p, OF_READ);
    _llseek(pf, k, 0);
    pn = _lread(pf, buf2, cb);
    _lclose(pf);
    h = xopen(cname, &d);
    if (k)
        LZRead(h, buf, k);
    fill(buf, 0xAA, cb + 64);
    n = LZRead(h, buf, cb);
    for (i = 0; i < cb + 64; i++) {
        if (first < 0 && (i >= pn || buf[i] != buf2[i]))
            first = i;
        if (buf[i] != 0xAA)
            last = i;
    }
    lg(" %s k=%d cb=%d: %d; at %ld dos %ld; first diff %d, last written %d", (LPSTR)cname, k,
       cb, n, LZSeek(h, 0, 1), dpos(d), first, last);
    if (first >= 0 && first < cb + 64) {
        i = first > 8 ? first - 8 : 0;
        lg("   got  %s", (LPSTR)summ(buf + i, 24));
        lg("   want %s", (LPSTR)summ(buf2 + i, pn - i < 24 ? pn - i : 24));
    }
    LZClose(h);
}

/* the largest read that succeeds after reading k bytes (by bisection) */
static int tryread(const char *cname, int k, int cb, long *at)
{
    HFILE h, d;
    int n;
    h = xopen(cname, &d);
    if (k)
        LZRead(h, buf, k);
    n = LZRead(h, buf, cb);
    *at = LZSeek(h, 0, 1);
    LZClose(h);
    return n;
}

static void zcase(const char *cname, int k)
{
    int lo = 1, hi = 7000, mid, n;
    long at;
    n = tryread(cname, k, hi, &at);
    if (n >= 0) {
        lg(" %s k=%d: 7000 ok (%d)", (LPSTR)cname, k, n);
        return;
    }
    if (tryread(cname, k, 1, &at) < 0) {
        lg(" %s k=%d: 1 fails", (LPSTR)cname, k);
        return;
    }
    while (hi - lo > 1) {
        mid = lo + (hi - lo) / 2;
        if (tryread(cname, k, mid, &at) >= 0)
            lo = mid;
        else
            hi = mid;
    }
    tryread(cname, k, hi, &at);
    lg(" %s k=%d: max ok %d, fails from %d at %ld", (LPSTR)cname, k, lo, hi, at);
}

static void t_z(void)
{
    static const int tk[] = { 0, 1, 2, 13, 57, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                              1000, 1013, 1500, 2000, 3000, 4000, -1 };
    static const int rk[] = { 0, 1, 19, 37, 100, 200, 500, 1000, 2000, -1 };
    static const int bk[] = { 0, 100, 1000, 2000, 3000, 5000, -1 };
    int i;
    lg("== z");
    for (i = 0; tk[i] >= 0; i++)
        zcase("TEXT.TX_", tk[i]);
    for (i = 0; rk[i] >= 0; i++)
        zcase("RUNS.TX_", rk[i]);
    for (i = 0; bk[i] >= 0; i++)
        zcase("BIG.DA_", bk[i]);
    for (i = 0; bk[i] >= 0; i++)
        zcase("FAR.TX_", bk[i]);
    for (i = 0; bk[i] >= 0; i++)
        zcase("LITBIG.TX_", bk[i]);
}

/* where a read wrote in the whole buffer: the runs of changed bytes */
static void wcase(const char *cname, int k, int cb)
{
    HFILE h, d;
    long i, start = -1;
    int n;
    h = xopen(cname, &d);
    if (k)
        LZRead(h, buf, k);
    fill(buf, 0xAA, 40000U);
    n = LZRead(h, buf, cb);
    lg(" %s k=%d cb=%d: %d at %ld", (LPSTR)cname, k, cb, n, LZSeek(h, 0, 1));
    for (i = 0; i <= 40000L; i++) {
        int ch = i < 40000L && buf[(unsigned)i] != 0xAA;
        if (ch && start < 0)
            start = i;
        if (!ch && start >= 0) {
            lg("   written %ld..%ld", start, i - 1);
            start = -1;
        }
    }
    LZClose(h);
}

static void t_w(void)
{
    HFILE h, d;
    lg("== w");
    wcase("LONGSZ.TX_", 0, 30000);
    wcase("LONGSZ.TX_", 0, 600);
    wcase("LONGSZ.TX_", 0, 501);
    wcase("LONGSZ.TX_", 0, 500);
    wcase("LONGSZ.TX_", 400, 30000);
    wcase("LONGSZ.TX_", 499, 2);
    wcase("TRUNC.TX_", 0, 30000);
    wcase("TRUNC.TX_", 0, 3500);
    wcase("TRUNC.TX_", 3000, 1000);
    wcase("CUT5000.DA_", 0, 30000);
    wcase("NODATA.TX_", 0, 30000);
    h = xopen("LONGSZ.TX_", &d);
    xrd(h, d, 499);
    xrd(h, d, 1);
    xrd(h, d, 1);
    xrd(h, d, 1);
    LZClose(h);
    h = xopen("LONGSZ.TX_", &d);
    xrd(h, d, 501);
    xrd(h, d, 1);
    LZClose(h);
    wcase("TEXT.TX_", 0, 2021);
    wcase("TEXT.TX_", 0, 7000);
    wcase("TEXT.TX_", 100, 1009);
    wcase("RUNS.TX_", 0, 1009);
    wcase("RUNS.TX_", 0, 7000);
    wcase("FAR.TX_", 0, 2018);
    wcase("FAR.TX_", 3000, 2016);
    wcase("BIG.DA_", 0, 3043);
}

/* ---- matches of 17 and 18 bytes (never written by COMPRESS.EXE) ---- */
static void t_long(void)
{
    static const char *const lf[] = { "TEXT18.TX_", "RUNS18.TX_", "FAR18.TX_", "BIG18.DA_", 0 };
    static const int lk[] = { 0, 1, 13, 100, 500, 1000, 3000, -1 };
    int f, i;
    lg("== long matches");
    for (f = 0; lf[f]; f++)
        for (i = 0; lk[i] >= 0; i++)
            zcase(lf[f], lk[i]);
    ycase("TEXT18.TX_", "TEXT.TXT", 0, 7000);
    ycase("TEXT18.TX_", "TEXT.TXT", 100, 2000);
    ycase("RUNS18.TX_", "RUNS.DAT", 0, 2000);
    ycase("FAR18.TX_", "FAR.DAT", 0, 2018);
    ycase("BIG18.DA_", "BIG.DAT", 0, 7000);
    ycase("BIG18.DA_", "BIG.DAT", 2000, 7000);
    {
        /* after a failed read: the next ones */
        HFILE h, d;
        h = xopen("TEXT18.TX_", &d);
        lg(" TEXT18 after a failure:");
        xrd(h, d, 7000);
        xrd(h, d, 10);
        xrd(h, d, 2000);
        xrd(h, d, 3000);
        sk("h", h, 0, 0);
        xrd(h, d, 500);
        LZClose(h);
    }
}

/* ---- seeking back and forth: does the next read start again? ---- */
static void t_back(void)
{
    static const long ns[] = { 3000, 10000, 0 };
    static const long ds[] = { 1, 2, 16, 100, 1000, 1009, 1024, 2000, 4000, 4078, 4096, 5000,
                               -1, -100, -2000, -5000, 0 };
    HFILE h, d;
    int a, b;
    lg("== seek back");
    for (a = 0; ns[a]; a++)
        for (b = 0; ds[b]; b++) {
            if (ns[a] - ds[b] < 0)
                continue;
            h = xopen("BIG.DA_", &d);
            LZRead(h, buf, 1000);
            LZSeek(h, ns[a] - 2000, 1);
            LZRead(h, buf, 1000);
            lg(" read to %ld (dos %ld), to %ld:", ns[a], dpos(d), ns[a] - ds[b]);
            LZSeek(h, ns[a] - ds[b], 0);
            xrd(h, d, 10);
            xrd(h, d, 2000);
            LZClose(h);
        }
}

/* ---- the end of the data before the end of the file ---- */
static void t_eof(void)
{
    static const char *const ef[] = { "TRUNC.TX_", "LONGSZ.TX_", "NODATA.TX_", "CUT5000.DA_",
                                      "CUT17000.DA_", "SHORTSZ.TX_", 0 };
    HFILE h, d;
    int i;
    lg("== eof");
    for (i = 0; ef[i]; i++) {
        h = xopen(ef[i], &d);
        lg(" %s:", (LPSTR)ef[i]);
        xrd(h, d, 30000);
        xrd(h, d, 100);
        xrd(h, d, 10);
        sk("h", h, 0, 0);
        xrd(h, d, 700);
        xrd(h, d, 30000);
        sk("h", h, 3000, 0);
        xrd(h, d, 10);
        LZClose(h);
    }
}

/* ---- a real caller: VER.DLL's VerInstallFile, which opens the source
   with LZOpenFile and expands it with LZCopy (SDK Vol. 2) ---- */
static void vinst(UINT fl, const char *srcname, const char *dstname)
{
    char tmp[144];
    UINT len = sizeof tmp;
    DWORD r;
    char p[80];
    tmp[0] = 0;
    r = VerInstallFile(fl, srcname, dstname, "C:\\LZ", "C:\\LZOUT", "C:\\LZOUT", tmp, &len);
    lg(" VerInstallFile(%04X, %s, %s) = %08lX tmp \"%s\" len %u; next dos %d", fl,
       (LPSTR)srcname, (LPSTR)dstname, r, (LPSTR)tmp, len, nextdos());
    lstrcpy(p, "..\\LZOUT\\");
    lstrcat(p, dstname);
    readback(p);
    if (tmp[0] && (r & VIF_TEMPFILE)) {
        lstrcpy(p, "..\\LZOUT\\");
        lstrcat(p, tmp);
        readback(p);
    }
}

static void t_ver(void)
{
    DWORD hv, n;
    HGLOBAL g;
    void FAR *vi;
    VS_FIXEDFILEINFO FAR *ff;
    UINT len;
    lg("== VER.DLL");
    vinst(0, "TEXT.TX_", "TEXT.TXT");
    vinst(0, "BIG.DA_", "BIG.DAT");
    vinst(0, "ONLYC.TXT", "ONLYC.TXT");
    vinst(0, "PLAIN.EX_", "PLAIN.EXE");
    vinst(0, "TRUNC.TX_", "TRUNC.TXT");
    vinst(0, "SHORTSZ.TX_", "SHORTSZ.TXT");
    vinst(0, "MISSING.TX_", "MISSING.TXT");
    vinst(VIFF_FORCEINSTALL, "TEXT.TX_", "TEXT.TXT");
    vinst(0, "NOCHAR.DA_", "NOCHAR.DAT");
    /* the library's version, as a program checks it (SDK Vol. 1 ch. 9) */
    n = GetFileVersionInfoSize("LZEXPAND.DLL", &hv);
    lg(" GetFileVersionInfoSize(LZEXPAND.DLL) %s", (LPSTR)(n ? "> 0" : "= 0"));
    if (n) {
        g = GlobalAlloc(GMEM_FIXED, n);
        vi = GlobalLock(g);
        if (GetFileVersionInfo("LZEXPAND.DLL", hv, n, vi) &&
            VerQueryValue(vi, "\\", (void FAR * FAR *)&ff, &len)) {
            lg(" file version %u.%u.%u.%u, product %u.%u.%u.%u, os %lX type %lX",
               HIWORD(ff->dwFileVersionMS), LOWORD(ff->dwFileVersionMS),
               HIWORD(ff->dwFileVersionLS), LOWORD(ff->dwFileVersionLS),
               HIWORD(ff->dwProductVersionMS), LOWORD(ff->dwProductVersionMS),
               HIWORD(ff->dwProductVersionLS), LOWORD(ff->dwProductVersionLS),
               ff->dwFileOS, ff->dwFileType);
        }
        GlobalUnlock(g);
        GlobalFree(g);
    }
}

/* ---- LZRead with 0 bytes ---- */
static void t_zero(void)
{
    HFILE h, d;
    lg("== zero");
    h = xopen("TEXT.TX_", &d);
    lg(" fresh, 0, then:");
    xrd(h, d, 0);
    xrd(h, d, 10);
    xrd(h, d, 100);
    xrd(h, d, 100);
    xrd(h, d, 1000);
    xrd(h, d, 2000);
    xrd(h, d, 10);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" fresh, 0, 0, then:");
    xrd(h, d, 0);
    xrd(h, d, 0);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" fresh, seek 0, 0, then:");
    LZSeek(h, 0, 0);
    xrd(h, d, 0);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" fresh, seek 100, 0, then:");
    LZSeek(h, 100, 0);
    xrd(h, d, 0);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" fresh, 0, seek 0, then:");
    xrd(h, d, 0);
    LZSeek(h, 0, 0);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("BIG.DA_", &d);
    lg(" BIG fresh, 0, then:");
    xrd(h, d, 0);
    xrd(h, d, 30);
    xrd(h, d, 2000);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" at the end, 0, then:");
    xrd(h, d, 7000);
    xrd(h, d, 0);
    sk("h", h, 0, 0);
    xrd(h, d, 30);
    LZClose(h);
    h = xopen("TEXT.TX_", &d);
    lg(" 10, seek 0, 0, then:");
    xrd(h, d, 10);
    LZSeek(h, 0, 0);
    xrd(h, d, 0);
    xrd(h, d, 30);
    LZClose(h);
}

static void t_y(void)
{
    static const int ks[] = { 0, 0, 0, 500, 500, 100, 1000, -1 };
    static const int cs[] = { 2000, 2048, 7000, 10, 1013, 2048, 2048 };
    int i;
    static const int tb[] = { 2001, 2002, 2003, 2004, 2010, 2020, 2021, 2022, 2039, 2040, 2057,
                              1020, 1030, 1031, 1032, 0 };
    static const int bb[] = { 3001, 3002, 3003, 3004, 3005, 3006, 3007, 3008, 3010, 3039, 3043,
                              0 };
    static const int rb[] = { 1009, 1010, 1011, 1027, 1028, 19, 20, 500, 505, 0 };
    lg("== y");
    for (i = 0; tb[i]; i++)
        ycase("TEXT.TX_", "TEXT.TXT", 0, tb[i]);
    for (i = 0; bb[i]; i++)
        ycase("BIG.DA_", "BIG.DAT", 0, bb[i]);
    for (i = 0; rb[i]; i++)
        ycase("RUNS.TX_", "RUNS.DAT", 0, rb[i]);
    for (i = 0; ks[i] >= 0; i++)
        ycase("TEXT.TX_", "TEXT.TXT", ks[i], cs[i]);
    ycase("BIG.DA_", "BIG.DAT", 0, 3000);
    ycase("BIG.DA_", "BIG.DAT", 0, 3072);
    ycase("BIG.DA_", "BIG.DAT", 0, 7000);
    ycase("FAR.TX_", "FAR.DAT", 0, 2000);
    ycase("FAR.TX_", "FAR.DAT", 0, 2048);
    ycase("RUNS.TX_", "RUNS.DAT", 0, 100);
    ycase("RUNS.TX_", "RUNS.DAT", 0, 500);
    ycase("RUNS.TX_", "RUNS.DAT", 0, 1000);
    ycase("RUNS.TX_", "RUNS.DAT", 0, 1013);
    ycase("RUNS.TX_", "RUNS.DAT", 0, 2048);
    ycase("LITBIG.TX_", "LITBIG.DAT", 0, 7000);
}

int __cdecl __far tmain(void)
{
    const char FAR *cmd = (const char FAR *)MAKELP(GetCurrentPDB(), 0x81);
    HGLOBAL g1 = GlobalAlloc(GMEM_FIXED, 40000L), g2 = GlobalAlloc(GMEM_FIXED, 40000L);
    buf = (BYTE __far *)GlobalLock(g1);
    buf2 = (BYTE __far *)GlobalLock(g2);
    while (*cmd == ' ')
        cmd++;
    if (cmd[0] == 'c' && cmd[1] == 'h') {
        child = 1;
        t_child(cmd + 5);
        return 0;
    }
    SetHandleCount(40);
    {
        /* as the shell it has no arguments: C:\LZP.ARG gives them */
        static char arg[20];
        HFILE f = _lopen("C:\\LZP.ARG", OF_READ);
        if (f != HFILE_ERROR) {
            _lread(f, arg, sizeof arg - 1);
            _lclose(f);
            cmd = arg;
        }
        f = _lcreat(logname, 0);
        _lclose(f);
    }
    lg("LZP start; next dos %d", nextdos());
    if (cmd[0] == 'v') {
        static const char *const vf[] = { "LZEXPAND.DLL", "C:\\WINDOWS\\SYSTEM\\LZEXPAND.DLL",
                                          "DDEML.DLL", "C:\\WINDOWS\\SYSTEM\\DDEML.DLL",
                                          "VER.DLL", 0 };
        DWORD hv;
        HMODULE hm;
        int i;
        lg("== v");
        for (i = 0; vf[i]; i++)
            lg(" GetFileVersionInfoSize(%s) = %ld", (LPSTR)vf[i],
               GetFileVersionInfoSize(vf[i], &hv));
        hm = GetModuleHandle("LZEXPAND");
        lg(" GetModuleHandle(LZEXPAND) %s; FindResource(1, 16) %s", (LPSTR)(hm ? "ok" : "0"),
           (LPSTR)(hm && FindResource(hm, MAKEINTRESOURCE(1), MAKEINTRESOURCE(16)) ? "ok" : "0"));
        return 0;
    }
    if (cmd[0] == 'q') {
        int i;
        static const int qk[] = { 90, 200, 1000, 1005, 1008, 1009, 1010, 1011, 1012, 1016, -1 };
        lg("== q");
        for (i = 0; i <= 20; i++)
            zcase("FAR18.TX_", i);
        for (i = 0; i <= 20; i++)
            zcase("TEXT18.TX_", i);
        for (i = 0; qk[i] >= 0; i++)
            zcase("FAR18.TX_", qk[i]);
        return 0;
    }
    if (cmd[0] == 'k') {
        /* COMPRESS.EXE's own output of the same data (C:\LZ\K) */
        static const int kk[] = { 0, 1, 13, 100, 500, 1000, 1500, -1 };
        int i;
        lg("== k");
        for (i = 0; kk[i] >= 0; i++) {
            zcase("K\\TEXT.TX_", kk[i]);
            zcase("K\\RUNS.DA_", kk[i]);
            zcase("K\\BIG.DA_", kk[i]);
            zcase("K\\FAR.DA_", kk[i]);
            zcase("K\\LITBIG.DA_", kk[i]);
        }
        return 0;
    }
    if (cmd[0] == 'w') {
        t_w();
        return 0;
    }
    if (cmd[0] == 'z') {
        t_z();
        return 0;
    }
    if (cmd[0] == 'y') {
        t_y();
        return 0;
    }
    if (cmd[0] == 'x') {
        t_explore();
        return 0;
    }
    t_init();
    t_open();
    t_read();
    t_seek();
    t_close();
    t_copy();
    t_start();
    t_name();
    t_limit();
    t_tasks();
    t_eof();
    t_zero();
    t_back();
    t_long();
    t_ver();
    lg("LZP end; next dos %d", nextdos());
    return 0;
}
