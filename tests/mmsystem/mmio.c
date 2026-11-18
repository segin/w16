/*
 * mmio.c - MMP.EXE's MMIO sections (tests/mmsystem/mmp.c; ARCHITECTURE.md
 * 23): the multimedia file I/O functions on DOS files, memory files and
 * a custom I/O procedure (MMC.DLL's "TST ", which logs every message
 * MMSYSTEM sends it and the MMIOINFO fields it sees), with the results
 * and the MMIOINFO an application reads back. Letters: F (FOURCCs), D
 * (DOS files, RIFF chunks), M (memory files), B (buffering, through the
 * custom procedure), E (errors, flags, procedures, rename).
 *
 * Sources: Windows 3.1 SDK, Multimedia Programmer's Guide ch. 7.
 */
#include <windows.h>
#include <mmsystem.h>

extern void lg(const char *fmt, ...);
extern void put(const char FAR *s, int n);
extern char line[600];

typedef LRESULT (FAR PASCAL *IOPROC)(LPSTR, UINT, LPARAM, LPARAM);
typedef long (FAR PASCAL *DISKPROC)(BYTE FAR * FAR *);
static void fill(void FAR *p, int c, unsigned n)
{
    BYTE FAR *b = (BYTE FAR *)p;
    while (n--)
        *b++ = (BYTE)c;
}
#define _fmemset fill

static IOPROC tstproc;
static DISKPROC tstdisk;
static const FOURCC TST = mmioFOURCC('T', 'S', 'T', ' ');

static const char *fcc(DWORD f)
{
    static char b[4][12];
    static int k;
    char *r = b[k++ & 3];
    int i;
    for (i = 0; i < 4; i++) {
        BYTE c = (BYTE)(f >> (8 * i));
        r[i] = c >= 32 && c < 127 ? c : '.';
    }
    wsprintf(r + 4, "/%02X", (BYTE)(f >> 24));
    return r;
}

/* an MMIOINFO, pointers relative to its buffer */
static void info(const char *what, MMIOINFO FAR *m)
{
    long base = m->pchBuffer ? (long)OFFSETOF(m->pchBuffer) : 0;
#define REL(p) ((p) ? (long)OFFSETOF(p) - base : -1L)
    lg("  %s: flags %08lX fcc %s proc %s err %u task %s cch %ld buf %s next %ld endr %ld endw %ld bufoff %ld disk %ld info %08lX %08lX %08lX res %08lX %08lX h %s",
       (LPSTR)what, m->dwFlags, (LPSTR)fcc(m->fccIOProc),
       (LPSTR)(m->pIOProc == NULL ? "0" : (FARPROC)m->pIOProc == (FARPROC)tstproc ? "TST" : "other"),
       m->wErrorRet, (LPSTR)(m->htask == GetCurrentTask() ? "self" : m->htask ? "other" : "0"),
       m->cchBuffer, (LPSTR)(m->pchBuffer ? "yes" : "0"), REL(m->pchNext), REL(m->pchEndRead),
       REL(m->pchEndWrite), m->lBufOffset, m->lDiskOffset, m->adwInfo[0], m->adwInfo[1],
       m->adwInfo[2], m->dwReserved1, m->dwReserved2, (LPSTR)(m->hmmio ? "yes" : "0"));
#undef REL
}

static void getinfo(const char *what, HMMIO h)
{
    MMIOINFO m;
    UINT r;
    _fmemset(&m, 0xEE, sizeof m);
    r = mmioGetInfo(h, &m, 0);
    if (r)
        lg("  mmioGetInfo = %u", r);
    else
        info(what, &m);
}

static void ck(const char *what, MMCKINFO FAR *c)
{
    lg("  %s: id %s size %ld type %s offset %ld flags %08lX", (LPSTR)what, (LPSTR)fcc(c->ckid),
       c->cksize, (LPSTR)fcc(c->fccType), c->dwDataOffset, c->dwFlags);
}

static void hexfile(const char *name)
{
    static BYTE b[200];
    OFSTRUCT of;
    HFILE f = OpenFile(name, &of, OF_READ);
    int n, i;
    char *p = line;
    if (f == HFILE_ERROR) {
        lg("  %s: not there", (LPSTR)name);
        return;
    }
    n = _lread(f, b, sizeof b);
    _lclose(f);
    p += wsprintf(p, "  %s: %d bytes:", (LPSTR)name, n);
    for (i = 0; i < n && i < 120; i++)
        p += wsprintf(p, " %02X", b[i]);
    lg("%s", (LPSTR)line);
}

static void hexdisk(void)
{
    BYTE FAR *b;
    long n = tstdisk(&b);
    int i;
    char *p = line;
    p += wsprintf(p, "  TST disk: %ld bytes:", n);
    for (i = 0; i < n && i < 120; i++)
        p += wsprintf(p, " %02X", b[i]);
    lg("%s", (LPSTR)line);
}

/* ---- F: FOURCCs ---- */
static void sec_fourcc(void)
{
    static const char *s[] = { "WAVE", "wave", "ab", "", "abcdefg", "a b", "rIfF" };
    int i;
    for (i = 0; i < 7; i++)
        lg("mmioStringToFOURCC(\"%s\") = %s, TOUPPER %s, flags 1 %s", (LPSTR)s[i],
           (LPSTR)fcc(mmioStringToFOURCC(s[i], 0)), (LPSTR)fcc(mmioStringToFOURCC(s[i], MMIO_TOUPPER)),
           (LPSTR)fcc(mmioStringToFOURCC(s[i], 1)));
}

/* ---- D: DOS files ---- */
static void sec_dos(void)
{
    HMMIO h;
    MMCKINFO riff, fmt, data, sub;
    MMIOINFO mi;
    char buf[40];
    long r;
    UINT u;
    _fmemset(&riff, 0xEE, sizeof riff);
    _fmemset(&fmt, 0xEE, sizeof fmt);
    _fmemset(&data, 0xEE, sizeof data);
    _fmemset(&sub, 0xEE, sizeof sub);
    _fmemset(&mi, 0, sizeof mi);
    h = mmioOpen("C:\\MMPTEST.WAV", &mi, MMIO_CREATE | MMIO_WRITE | MMIO_ALLOCBUF);
    lg("mmioOpen(create, write, allocbuf) = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    info("info passed", &mi);
    if (!h)
        return;
    getinfo("after open", h);
    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    riff.cksize = 0;
    u = mmioCreateChunk(h, &riff, MMIO_CREATERIFF);
    lg("mmioCreateChunk(RIFF WAVE) = %u", u);
    ck("riff", &riff);
    fmt.ckid = mmioFOURCC('f', 'm', 't', ' ');
    fmt.cksize = 16;
    u = mmioCreateChunk(h, &fmt, 0);
    lg("mmioCreateChunk(fmt, size 16) = %u", u);
    ck("fmt", &fmt);
    r = mmioWrite(h, "0123456789ABCDEF", 16);
    lg("mmioWrite(16) = %ld", r);
    u = mmioAscend(h, &fmt, 0);
    lg("mmioAscend(fmt) = %u", u);
    ck("fmt", &fmt);
    data.ckid = mmioFOURCC('d', 'a', 't', 'a');
    data.cksize = 0;
    u = mmioCreateChunk(h, &data, 0);
    lg("mmioCreateChunk(data, size 0) = %u", u);
    r = mmioWrite(h, "odd", 3);
    lg("mmioWrite(3) = %ld", r);
    getinfo("in data", h);
    u = mmioAscend(h, &data, 0);
    lg("mmioAscend(data) = %u", u);
    ck("data", &data);
    sub.fccType = mmioFOURCC('I', 'N', 'F', 'O');
    sub.cksize = 0;
    u = mmioCreateChunk(h, &sub, MMIO_CREATELIST);
    lg("mmioCreateChunk(LIST INFO) = %u", u);
    ck("list", &sub);
    u = mmioAscend(h, &sub, 0);
    lg("mmioAscend(list) = %u", u);
    ck("list", &sub);
    u = mmioAscend(h, &riff, 0);
    lg("mmioAscend(riff) = %u", u);
    ck("riff", &riff);
    getinfo("before close", h);
    lg("mmioClose = %u", mmioClose(h, 0));
    hexfile("C:\\MMPTEST.WAV");

    _fmemset(&riff, 0xEE, sizeof riff);
    _fmemset(&fmt, 0xEE, sizeof fmt);
    _fmemset(&data, 0xEE, sizeof data);
    _fmemset(&sub, 0xEE, sizeof sub);
    h = mmioOpen("C:\\MMPTEST.WAV", NULL, MMIO_READ);
    lg("mmioOpen(read) = %s", (LPSTR)(h ? "h" : "0"));
    if (!h)
        return;
    getinfo("unbuffered", h);
    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    u = mmioDescend(h, &riff, NULL, MMIO_FINDRIFF);
    lg("mmioDescend(FINDRIFF WAVE) = %u", u);
    ck("riff", &riff);
    data.ckid = mmioFOURCC('d', 'a', 't', 'a');
    u = mmioDescend(h, &data, &riff, MMIO_FINDCHUNK);
    lg("mmioDescend(FINDCHUNK data) = %u", u);
    ck("data", &data);
    _fmemset(buf, 0, sizeof buf);
    r = mmioRead(h, buf, 10);
    lg("mmioRead(10) = %ld \"%s\"", r, (LPSTR)buf);
    u = mmioAscend(h, &data, 0);
    lg("mmioAscend(data) = %u, at %ld", u, mmioSeek(h, 0, SEEK_CUR));
    sub.fccType = mmioFOURCC('I', 'N', 'F', 'O');
    u = mmioDescend(h, &sub, &riff, MMIO_FINDLIST);
    lg("mmioDescend(FINDLIST INFO) = %u", u);
    ck("list", &sub);
    lg("mmioSeek(12, SET) = %ld", mmioSeek(h, 12, SEEK_SET));
    u = mmioDescend(h, &fmt, NULL, 0);
    lg("mmioDescend(next chunk) = %u", u);
    ck("next", &fmt);
    fmt.ckid = mmioFOURCC('n', 'o', 'n', 'e');
    u = mmioDescend(h, &fmt, &riff, MMIO_FINDCHUNK);
    lg("mmioDescend(FINDCHUNK none) = %u, at %ld", u, mmioSeek(h, 0, SEEK_CUR));
    lg("mmioSeek(0, END) = %ld", mmioSeek(h, 0, SEEK_END));
    lg("mmioSeek(-4, CUR) = %ld", mmioSeek(h, -4, SEEK_CUR));
    lg("mmioSeek(1000, SET) = %ld", mmioSeek(h, 1000, SEEK_SET));
    r = mmioRead(h, buf, 10);
    lg("mmioRead past the end = %ld", r);
    lg("mmioSeek(-1, SET) = %ld, then at %ld", mmioSeek(h, -1, SEEK_SET), mmioSeek(h, 0, SEEK_CUR));
    lg("mmioSeek(0, 7) = %ld, then at %ld", mmioSeek(h, 0, 7), mmioSeek(h, 0, SEEK_CUR));
    r = mmioWrite(h, "x", 1);
    lg("mmioWrite on a read file = %ld, then at %ld", r, mmioSeek(h, 0, SEEK_CUR));
    u = mmioSetBuffer(h, NULL, 100, 0);
    lg("mmioSetBuffer(NULL, 100) = %u", u);
    getinfo("buffered 100", h);
    lg("mmioSeek(8, SET) = %ld", mmioSeek(h, 8, SEEK_SET));
    r = mmioRead(h, buf, 4);
    buf[4] = 0;
    lg("mmioRead(4) = %ld \"%s\"", r, (LPSTR)buf);
    getinfo("after read", h);
    u = mmioSetBuffer(h, NULL, 0, 0);
    lg("mmioSetBuffer(NULL, 0) = %u", u);
    getinfo("unbuffered again", h);
    lg("mmioClose = %u", mmioClose(h, 0));
}

/* ---- M: memory files ---- */
static void sec_mem(void)
{
    static char mem[64];
    MMIOINFO mi;
    HMMIO h;
    char buf[20];
    long r;
    _fmemset(&mi, 0, sizeof mi);
    lstrcpy(mem, "memory file contents");
    mi.fccIOProc = FOURCC_MEM;
    mi.pchBuffer = mem;
    mi.cchBuffer = sizeof mem;
    h = mmioOpen(NULL, &mi, MMIO_READWRITE);
    lg("mmioOpen(MEM, 64 bytes, readwrite) = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h) {
        static const long so[] = { 10, 70, 64, 0, -1, 0, -3, 5, 65, -70 };
        static const int sw[] = { SEEK_SET, SEEK_SET, SEEK_SET, SEEK_END, SEEK_CUR, SEEK_SET,
                                  SEEK_END, SEEK_CUR, SEEK_SET, SEEK_CUR };
        int k;
        for (k = 0; k < 10; k++) {
            long a = mmioSeek(h, so[k], sw[k]);
            lg("  mem mmioSeek(%ld, %d) = %ld, then at %ld", so[k], sw[k], a, mmioSeek(h, 0, SEEK_CUR));
        }
        mmioSeek(h, 0, SEEK_SET);
        getinfo("mem", h);
        r = mmioRead(h, buf, 6);
        buf[6] = 0;
        lg("mmioRead(6) = %ld \"%s\"", r, (LPSTR)buf);
        r = mmioWrite(h, "XY", 2);
        lg("mmioWrite(2) = %ld; memory now \"%s\"", r, (LPSTR)mem);
        getinfo("after write", h);
        lg("mmioSeek(0, END) = %ld", mmioSeek(h, 0, SEEK_END));
        r = mmioWrite(h, "Z", 1);
        lg("mmioWrite at the end = %ld", r);
        lg("mmioSeek(-3, END) = %ld", mmioSeek(h, -3, SEEK_END));
        r = mmioRead(h, buf, 10);
        lg("mmioRead(10) near the end = %ld", r);
        lg("mmioFlush = %u; memory \"%s\"", mmioFlush(h, 0), (LPSTR)mem);
        lg("mmioClose = %u", mmioClose(h, 0));
    }
    /* a growable one: no buffer, ALLOCBUF, adwInfo[0] the increment */
    _fmemset(&mi, 0, sizeof mi);
    mi.fccIOProc = FOURCC_MEM;
    mi.adwInfo[0] = 16;
    h = mmioOpen(NULL, &mi, MMIO_CREATE | MMIO_READWRITE | MMIO_ALLOCBUF);
    lg("mmioOpen(MEM, none, create allocbuf, grow 16) = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h) {
        getinfo("growable", h);
        r = mmioWrite(h, "0123456789012345678901234567890123456789", 40);
        lg("mmioWrite(40) = %ld", r);
        getinfo("grown", h);
        lg("mmioSeek(0, END) = %ld", mmioSeek(h, 0, SEEK_END));
        lg("mmioClose = %u", mmioClose(h, 0));
    }
    _fmemset(&mi, 0, sizeof mi);
    mi.fccIOProc = FOURCC_MEM;
    h = mmioOpen(NULL, &mi, MMIO_READ);
    lg("mmioOpen(MEM, no buffer, no allocbuf) = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h)
        mmioClose(h, 0);
}

/* ---- B: buffering, as the custom procedure sees it ---- */
static void sec_buf(void)
{
    static char big[300];
    HMMIO h;
    MMIOINFO mi;
    char buf[40];
    long r;
    int i;
    LPMMIOPROC p;
    p = mmioInstallIOProc(TST, (LPMMIOPROC)tstproc, MMIO_INSTALLPROC);
    lg("mmioInstallIOProc(TST, install) = %s", (LPSTR)(p == (LPMMIOPROC)tstproc ? "the proc" : p ? "other" : "0"));
    p = mmioInstallIOProc(TST, NULL, MMIO_FINDPROC);
    lg("mmioInstallIOProc(TST, find) = %s", (LPSTR)(p == (LPMMIOPROC)tstproc ? "the proc" : p ? "other" : "0"));
    lg("open FOO.TST+BAR, create readwrite:");
    h = mmioOpen("FOO.TST+BAR", NULL, MMIO_CREATE | MMIO_READWRITE);
    lg("  = %s", (LPSTR)(h ? "h" : "0"));
    if (!h)
        return;
    getinfo("tst", h);
    for (i = 0; i < 300; i++)
        big[i] = (char)('A' + i % 26);
    lg("mmioWrite(10):");
    lg("  = %ld", mmioWrite(h, big, 10));
    lg("mmioSetBuffer(NULL, 64):");
    lg("  = %u", mmioSetBuffer(h, NULL, 64, 0));
    getinfo("buffered 64", h);
    lg("mmioWrite(10):");
    lg("  = %ld", mmioWrite(h, big, 10));
    lg("mmioWrite(100):");
    lg("  = %ld", mmioWrite(h, big + 10, 100));
    getinfo("after", h);
    lg("mmioWrite(30):");
    lg("  = %ld", mmioWrite(h, big + 110, 30));
    lg("mmioSeek(5, SET):");
    lg("  = %ld", mmioSeek(h, 5, SEEK_SET));
    getinfo("after seek", h);
    lg("mmioRead(20):");
    r = mmioRead(h, buf, 20);
    lg("  = %ld", r);
    getinfo("after read", h);
    lg("mmioSeek(-10, CUR):");
    lg("  = %ld", mmioSeek(h, -10, SEEK_CUR));
    lg("mmioSeek(0, END):");
    lg("  = %ld", mmioSeek(h, 0, SEEK_END));
    lg("mmioFlush:");
    lg("  = %u", mmioFlush(h, 0));
    lg("mmioFlush(EMPTYBUF):");
    lg("  = %u", mmioFlush(h, MMIO_EMPTYBUF));
    getinfo("after flush", h);
    lg("mmioSeek(0, SET); mmioAdvance(READ):");
    mmioSeek(h, 0, SEEK_SET);
    _fmemset(&mi, 0, sizeof mi);
    mmioGetInfo(h, &mi, 0);
    lg("  = %u", mmioAdvance(h, &mi, MMIO_READ));
    info("advanced", &mi);
    mi.pchNext = (HPSTR)((DWORD)mi.pchNext + 4);
    lg("mmioSetInfo(next + 4):");
    lg("  = %u", mmioSetInfo(h, &mi, 0));
    lg("mmioSeek(0, CUR) = %ld", mmioSeek(h, 0, SEEK_CUR));
    _fmemset(&mi, 0, sizeof mi);
    mmioGetInfo(h, &mi, 0);
    *(char FAR *)mi.pchNext = '!';
    mi.pchNext = (HPSTR)((DWORD)mi.pchNext + 1);
    mi.dwFlags |= MMIO_DIRTY;
    lg("mmioAdvance(WRITE) after one byte written directly:");
    lg("  = %u", mmioAdvance(h, &mi, MMIO_WRITE));
    info("advanced", &mi);
    mmioSetInfo(h, &mi, 0);
    lg("mmioSendMessage(MMIOM_USER, 5, 6):");
    lg("  = %ld", mmioSendMessage(h, MMIOM_USER, 5, 6));
    lg("mmioSendMessage(7, 0, 0):");
    lg("  = %ld", mmioSendMessage(h, 7, 0, 0));
    lg("mmioClose:");
    lg("  = %u", mmioClose(h, 0));
    hexdisk();
    lg("open FOO.TST+BAR, read, allocbuf:");
    h = mmioOpen("FOO.TST+BAR", NULL, MMIO_READ | MMIO_ALLOCBUF);
    lg("  = %s", (LPSTR)(h ? "h" : "0"));
    if (h) {
        getinfo("tst read", h);
        lg("mmioRead(3):");
        lg("  = %ld", mmioRead(h, buf, 3));
        lg("mmioRead(400):");
        lg("  = %ld", mmioRead(h, big, 299));
        lg("mmioClose:");
        lg("  = %u", mmioClose(h, 0));
    }
    _fmemset(&mi, 0, sizeof mi);
    mi.fccIOProc = TST;
    lg("open by fccIOProc TST, name BAZ, read:");
    h = mmioOpen("BAZ", &mi, MMIO_READ);
    lg("  = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h)
        mmioClose(h, 0);
    _fmemset(&mi, 0, sizeof mi);
    mi.pIOProc = (LPMMIOPROC)tstproc;
    lg("open by pIOProc, name QUX, read:");
    h = mmioOpen("QUX", &mi, MMIO_READ);
    lg("  = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h)
        mmioClose(h, 0);
    lg("open Q.TST+X, read, unbuffered; odd seeks:");
    h = mmioOpen("Q.TST+X", NULL, MMIO_READ);
    if (h) {
        lg("mmioSeek(1000, SET):");
        lg("  = %ld", mmioSeek(h, 1000, SEEK_SET));
        lg("mmioRead(4):");
        lg("  = %ld", mmioRead(h, buf, 4));
        lg("mmioSeek(-1, SET):");
        lg("  = %ld", mmioSeek(h, -1, SEEK_SET));
        lg("mmioSeek(0, 7):");
        lg("  = %ld", mmioSeek(h, 0, 7));
        lg("mmioWrite(1):");
        lg("  = %ld", mmioWrite(h, "x", 1));
        lg("mmioSetBuffer(NULL, 100):");
        lg("  = %u", mmioSetBuffer(h, NULL, 100, 0));
        getinfo("buffered 100", h);
        lg("mmioClose:");
        lg("  = %u", mmioClose(h, 0));
    }
    lg("mmioRename(A.TST, B):");
    lg("  = %u", mmioRename("A.TST", "B", NULL, 0));
    p = mmioInstallIOProc(TST, NULL, MMIO_REMOVEPROC);
    lg("mmioInstallIOProc(TST, remove) = %s", (LPSTR)(p == (LPMMIOPROC)tstproc ? "the proc" : p ? "other" : "0"));
    p = mmioInstallIOProc(TST, NULL, MMIO_FINDPROC);
    lg("mmioInstallIOProc(TST, find) = %s", (LPSTR)(p ? "found" : "0"));
    h = mmioOpen("FOO.TST+BAR", NULL, MMIO_READ);
    lg("open FOO.TST+BAR without the proc = %s", (LPSTR)(h ? "h" : "0"));
    if (h)
        mmioClose(h, 0);
    p = mmioInstallIOProc(FOURCC_DOS, NULL, MMIO_FINDPROC);
    lg("find DOS = %s, MEM = %s", (LPSTR)(p ? "found" : "0"),
       (LPSTR)(mmioInstallIOProc(FOURCC_MEM, NULL, MMIO_FINDPROC) ? "found" : "0"));
}

/* ---- E: errors and the rest ---- */
static void sec_err(void)
{
    MMIOINFO mi;
    HMMIO h;
    OFSTRUCT of;
    HFILE f;
    char name[128];
    static const DWORD fl[] = { MMIO_READ, MMIO_EXIST, MMIO_PARSE, MMIO_DELETE, MMIO_GETTEMP };
    static const char *fn[] = { "read", "exist", "parse", "delete", "gettemp" };
    int i;
    for (i = 0; i < 5; i++) {
        _fmemset(&mi, 0, sizeof mi);
        lstrcpy(name, "C:\\NOFILE.WAV");
        h = mmioOpen(name, &mi, fl[i]);
        lg("mmioOpen(missing, %s) = %04X, err %u, name \"%s\"", (LPSTR)fn[i],
           h == (HMMIO)1 || !h ? (WORD)h : 0xAAAA, mi.wErrorRet, (LPSTR)name);
    }
    f = _lcreat("C:\\MMPE.TXT", 0);
    _lwrite(f, "hello", 5);
    _lclose(f);
    for (i = 0; i < 5; i++) {
        _fmemset(&mi, 0, sizeof mi);
        lstrcpy(name, "c:mmpe.txt");
        h = mmioOpen(name, &mi, fl[i]);
        lg("mmioOpen(c:mmpe.txt, %s) = %04X, err %u, name \"%s\"", (LPSTR)fn[i],
           h == (HMMIO)1 || !h ? (WORD)h : 0xAAAA, mi.wErrorRet, (LPSTR)name);
        if (h && h != (HMMIO)1 && fl[i] != MMIO_EXIST && fl[i] != MMIO_PARSE && fl[i] != MMIO_DELETE)
            mmioClose(h, 0);
        if (fl[i] == MMIO_DELETE)
            hexfile("C:\\MMPE.TXT");
    }
    h = mmioOpen(NULL, NULL, MMIO_READ);
    lg("mmioOpen(NULL, NULL, read) = %s", (LPSTR)(h ? "h" : "0"));
    f = _lcreat("C:\\MMPF.TXT", 0);
    _lwrite(f, "fhopen", 6);
    _llseek(f, 0, 0);
    _fmemset(&mi, 0, sizeof mi);
    mi.adwInfo[0] = (DWORD)f;
    h = mmioOpen(NULL, &mi, MMIO_READ);
    lg("mmioOpen(NULL, adwInfo[0] = a DOS handle) = %s, err %u", (LPSTR)(h ? "h" : "0"), mi.wErrorRet);
    if (h) {
        lg("  read = %ld", mmioRead(h, name, 6));
        lg("  mmioClose(FHOPEN) = %u", mmioClose(h, MMIO_FHOPEN));
        lg("  the handle still open: %s", (LPSTR)(_llseek(f, 0, 1) != HFILE_ERROR ? "yes" : "no"));
    }
    _lclose(f);
    lg("mmioRename(C:\\MMPF.TXT, C:\\MMPG.TXT) = %u", mmioRename("C:\\MMPF.TXT", "C:\\MMPG.TXT", NULL, 0));
    lg("  old %s, new %s", (LPSTR)(OpenFile("C:\\MMPF.TXT", &of, OF_EXIST) != HFILE_ERROR ? "there" : "gone"),
       (LPSTR)(OpenFile("C:\\MMPG.TXT", &of, OF_EXIST) != HFILE_ERROR ? "there" : "gone"));
    lg("mmioRename(missing) = %u", mmioRename("C:\\NOFILE.X", "C:\\NOFILE.Y", NULL, 0));
    lg("mmioClose(0) = %u", mmioClose(0, 0));
    lg("mmioRead(0) = %ld", mmioRead(0, name, 1));
    lg("mmioSeek(0) = %ld", mmioSeek(0, 0, 0));
    lg("mmioGetInfo(0) = %u", mmioGetInfo(0, &mi, 0));
    lg("mmioDescend(0) = %u", mmioDescend(0, (LPMMCKINFO)&mi, NULL, 0));
    lg("mmioFlush(0) = %u", mmioFlush(0, 0));
    lg("mmioSendMessage(0) = %ld", mmioSendMessage(0, MMIOM_USER, 0, 0));
}

void mmio_sections(char c)
{
    if (!tstproc) {
        HINSTANCE mmc = GetModuleHandle("MMC");
        tstproc = (IOPROC)GetProcAddress(mmc, "MMCIOPROC");
        tstdisk = (DISKPROC)GetProcAddress(mmc, "MMCDISK");
    }
    switch (c) {
    case 'F': lg("== fourcc"); sec_fourcc(); break;
    case 'D': lg("== dos"); sec_dos(); break;
    case 'M': lg("== mem"); sec_mem(); break;
    case 'B': lg("== buf"); sec_buf(); break;
    case 'E': lg("== mmioerr"); sec_err(); break;
    }
}
