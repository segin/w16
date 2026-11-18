/*
 * mmio.c - w16's MMSYSTEM.DLL: the multimedia file I/O services (mmio*,
 * ordinals 1210-1226; ARCHITECTURE.md 23).
 *
 * Sources: Windows 3.1 SDK, Multimedia Programmer's Guide ch. 7 (the
 * functions, buffered and unbuffered I/O, memory files, RIFF chunks,
 * custom I/O procedures and their messages, MMIOINFO and MMCKINFO, the
 * flags and error codes); everything the guide leaves open observed on
 * Windows 3.1 with tests/mmsystem/mmio.c (MMP's MMIO sections, through
 * a logging I/O procedure; docs/notes/mmsystem-handover.md):
 *  - a buffer is filled with one MMIOM_READ of its whole size and
 *    written back with MMIOM_WRITEFLUSH of the bytes in it; the
 *    procedure is told to seek only when its lDiskOffset is not where the
 *    buffer goes; a seek inside the buffer moves the pointer only, any
 *    other leaves the buffer empty at the new place without asking the
 *    procedure (SEEK_END asks it);
 *  - an unbuffered file sends every read, write and seek straight on;
 *  - mmioOpen keeps the flags it is given (MMIO_CREATE, MMIO_ALLOCBUF) in
 *    dwFlags, htask stays 0, the caller's MMIOINFO gets only wErrorRet;
 *    MMIO_PARSE, MMIO_EXIST, MMIO_DELETE and MMIO_GETTEMP are OpenFile's
 *    and GetTempFileName's (the name from its fourth character as the
 *    prefix) and answer 1;
 *  - mmioStringToFOURCC takes four bytes whatever they are, a 0 becoming
 *    a space; flags other than MMIO_TOUPPER give FFFFFFFFh;
 *  - a compound name "A.EXT+B" picks the procedure installed for EXT.
 */
#include "mmsys.h"

#define MMIO_RWMODE         0x00000003L
#define MMIO_SHAREMODE      0x00000070L
#define MMIO_CREATE         0x00001000L
#define MMIO_PARSE          0x00000100L
#define MMIO_DELETE         0x00000200L
#define MMIO_EXIST          0x00004000L
#define MMIO_ALLOCBUF       0x00010000L
#define MMIO_GETTEMP        0x00020000L
#define MMIO_DIRTY          0x10000000L
#define MMIO_WRITE          0x00000001L
#define MMIO_READWRITE      0x00000002L
#define MMIO_FHOPEN         0x0010
#define MMIO_EMPTYBUF       0x0010
#define MMIO_TOUPPER        0x0010
#define MMIO_INSTALLPROC    0x00010000L
#define MMIO_GLOBALPROC     0x10000000L
#define MMIO_REMOVEPROC     0x00020000L
#define MMIO_FINDPROC       0x00040000L
#define MMIO_FINDCHUNK      0x0010
#define MMIO_FINDRIFF       0x0020
#define MMIO_FINDLIST       0x0040
#define MMIO_CREATERIFF     0x0020
#define MMIO_CREATELIST     0x0040
#define MMIO_READ_ADV       0x0000          /* mmioAdvance's MMIO_READ */
#define MMIOM_READ          0
#define MMIOM_WRITE         1
#define MMIOM_SEEK          2
#define MMIOM_OPEN          3
#define MMIOM_CLOSE         4
#define MMIOM_WRITEFLUSH    5
#define MMIOM_RENAME        6
#define MMIO_DEFAULTBUFFER  8192L
#define MMIOERR_FILENOTFOUND  (MMIOERR_BASE + 1)
#define MMIOERR_OUTOFMEMORY   (MMIOERR_BASE + 2)
#define MMIOERR_CANNOTOPEN    (MMIOERR_BASE + 3)
#define MMIOERR_CANNOTREAD    (MMIOERR_BASE + 5)
#define MMIOERR_CANNOTWRITE   (MMIOERR_BASE + 6)
#define MMIOERR_CANNOTSEEK    (MMIOERR_BASE + 7)
#define MMIOERR_CANNOTEXPAND  (MMIOERR_BASE + 8)
#define MMIOERR_CHUNKNOTFOUND (MMIOERR_BASE + 9)
#define MMIOERR_UNBUFFERED    (MMIOERR_BASE + 10)

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | \
                         ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
#define FOURCC_RIFF FCC('R', 'I', 'F', 'F')
#define FOURCC_LIST FCC('L', 'I', 'S', 'T')
#define FOURCC_DOS  FCC('D', 'O', 'S', ' ')
#define FOURCC_MEM  FCC('M', 'E', 'M', ' ')

typedef LRESULT (FAR PASCAL *MMIOPROC)(LPSTR, UINT, LPARAM, LPARAM);

#pragma pack(push, 1)
typedef struct {
    DWORD dwFlags;
    DWORD fccIOProc;
    MMIOPROC pIOProc;
    WORD wErrorRet;
    WORD htask;
    long cchBuffer;
    char FAR *pchBuffer;
    char FAR *pchNext;
    char FAR *pchEndRead;
    char FAR *pchEndWrite;
    long lBufOffset;
    long lDiskOffset;
    DWORD adwInfo[3];
    DWORD dwReserved1;
    DWORD dwReserved2;
    WORD hmmio;
} MMIOINFO;
typedef MMIOINFO FAR *LPMMIOINFO;
typedef struct {
    DWORD ckid;
    DWORD cksize;
    DWORD fccType;
    DWORD dwDataOffset;
    DWORD dwFlags;
} MMCKINFO;
typedef MMCKINFO FAR *LPMMCKINFO;
#pragma pack(pop)

/* an open file: its MMIOINFO, then what only MMSYSTEM keeps */
typedef struct {
    MMIOINFO i;
    HGLOBAL hbuf;               /* a buffer of MMSYSTEM's own */
    HGLOBAL next;               /* the next open file */
} MMFILE;
typedef MMFILE FAR *LPMMFILE;

static HGLOBAL files;           /* the open files */

extern WORD __cdecl __far mm_ahincr(void);   /* mmfix.asm: KERNEL's __AHINCR */

/* ---- huge pointers: offsets into a buffer ---- */
static char FAR *at(char FAR *base, long off)
{
    DWORD o = (DWORD)OFFSETOF(base) + (DWORD)off;
    WORD sel = SELECTOROF(base) + (WORD)(o >> 16) * mm_ahincr();
    return (char FAR *)MAKELP(sel, (WORD)o);
}

static long offof(char FAR *base, char FAR *p)
{
    WORD inc = mm_ahincr();
    long segs = inc ? (long)((SELECTOROF(p) - SELECTOROF(base)) / inc) : 0;
    return (segs << 16) + (long)OFFSETOF(p) - (long)OFFSETOF(base);
}

static void hcopy(char FAR *d, char FAR *s, long n)
{
    /* byte by byte across segment boundaries: buffers are small */
    long k;
    for (k = 0; k < n; k++)
        *at(d, k) = *at(s, k);
}

/* ---- the open-file list ---- */
static LPMMFILE lockf(WORD h)
{
    HGLOBAL g;
    for (g = files; g; ) {
        LPMMFILE f = (LPMMFILE)GlobalLock(g);
        HGLOBAL nx;
        if (!f)
            return NULL;
        if ((WORD)g == h)
            return f;
        nx = f->next;
        GlobalUnlock(g);
        g = nx;
    }
    return NULL;
}

static void unlockf(WORD h)
{
    GlobalUnlock((HGLOBAL)h);
}

static LRESULT send(LPMMFILE f, UINT msg, LPARAM l1, LPARAM l2)
{
    if (!f->i.pIOProc)
        return -1;
    return f->i.pIOProc((LPSTR)&f->i, msg, l1, l2);
}

static BOOL ismem(LPMMFILE f)
{
    return f->i.fccIOProc == FOURCC_MEM;
}

/* ---- the DOS and memory procedures ---- */
static WORD dosrename(LPCSTR a, LPCSTR b)
{
    WORD r = 0;
    _asm {
        push ds
        push di
        lds dx, a
        les di, b
        mov ah, 56h
        int 21h
        jnc ok
        mov r, ax
    ok:
        pop di
        pop ds
    }
    return r;
}

static LRESULT FAR PASCAL __loadds dosproc(LPSTR lp, UINT msg, LPARAM l1, LPARAM l2)
{
    LPMMIOINFO m = (LPMMIOINFO)lp;
    HFILE hf = (HFILE)m->adwInfo[0];
    long r;
    OFSTRUCT of;
    switch (msg) {
    case MMIOM_OPEN:
        if (l1) {
            UINT mode = (UINT)(m->dwFlags & (MMIO_RWMODE | MMIO_SHAREMODE));
            if (m->dwFlags & MMIO_CREATE)
                mode |= OF_CREATE;
            hf = OpenFile((LPCSTR)l1, &of, mode);
            if (hf == HFILE_ERROR)
                return MMIOERR_FILENOTFOUND;
            m->adwInfo[0] = (DWORD)hf;
            m->lDiskOffset = 0;
        } else {
            r = _llseek(hf, 0, 1);
            m->lDiskOffset = r == -1 ? 0 : r;
        }
        return 0;
    case MMIOM_CLOSE:
        if (!((WORD)l1 & MMIO_FHOPEN))
            _lclose(hf);
        return 0;
    case MMIOM_READ:
        r = _hread(hf, (void _huge *)l1, l2);
        if (r != -1)
            m->lDiskOffset += r;
        return r;
    case MMIOM_WRITE:
    case MMIOM_WRITEFLUSH:
        r = _hwrite(hf, (const void _huge *)l1, l2);
        if (r != -1)
            m->lDiskOffset += r;
        return r;
    case MMIOM_SEEK:
        r = _llseek(hf, l1, (int)l2);
        if (r != -1)
            m->lDiskOffset = r;
        else {
            /* UNVERIFIED guess that matches 3.1: after a seek DOS refuses
               (to -1), a file without a buffer is at its end (MMP) */
            r = _llseek(hf, 0, 2);
            m->lDiskOffset = r;
            r = -1;
        }
        return r;
    case MMIOM_RENAME:
        return dosrename((LPCSTR)l1, (LPCSTR)l2) ? MMIOERR_FILENOTFOUND : 0;
    }
    return 0;
}

/* A memory file is its buffer: MMSYSTEM itself reads, writes and seeks in
   it (and grows it when it may); the procedure has nothing to do */
static LRESULT FAR PASCAL __loadds memproc(LPSTR lp, UINT msg, LPARAM l1, LPARAM l2)
{
    (void)lp;
    (void)l1;
    (void)l2;
    switch (msg) {
    case MMIOM_READ:
        return 0;
    case MMIOM_WRITE:
    case MMIOM_WRITEFLUSH:
    case MMIOM_SEEK:
        return -1;
    }
    return 0;
}

/* ---- installed procedures ---- */
#define NPROC 16
static struct { DWORD fcc; MMIOPROC proc; WORD task; } procs[NPROC];

static MMIOPROC findproc(DWORD fcc)
{
    int k;
    WORD t = (WORD)GetCurrentTask();
    for (k = NPROC - 1; k >= 0; k--)
        if (procs[k].proc && procs[k].fcc == fcc && (!procs[k].task || procs[k].task == t))
            return procs[k].proc;
    if (fcc == FOURCC_DOS)
        return (MMIOPROC)dosproc;
    if (fcc == FOURCC_MEM)
        return (MMIOPROC)memproc;
    return NULL;
}

MMIOPROC MMAPI MMIOINSTALLIOPROC(DWORD fcc, MMIOPROC proc, DWORD flags)
{
    int k;
    WORD t = (WORD)GetCurrentTask();
    if (flags & MMIO_INSTALLPROC) {
        if (!proc)
            return NULL;
        for (k = 0; k < NPROC; k++)
            if (!procs[k].proc) {
                procs[k].fcc = fcc;
                procs[k].proc = proc;
                procs[k].task = (flags & MMIO_GLOBALPROC) ? 0 : t;
                return proc;
            }
        return NULL;
    }
    if (flags & MMIO_REMOVEPROC) {
        for (k = NPROC - 1; k >= 0; k--)
            if (procs[k].proc && procs[k].fcc == fcc && (!proc || procs[k].proc == proc) &&
                (!procs[k].task || procs[k].task == t)) {
                MMIOPROC p = procs[k].proc;
                procs[k].proc = NULL;
                return p;
            }
        return NULL;
    }
    if (flags & MMIO_FINDPROC)
        return findproc(fcc);
    return NULL;
}

/* ---- FOURCCs ---- */
DWORD MMAPI MMIOSTRINGTOFOURCC(LPCSTR s, UINT flags)
{
    char b[5];
    int k;
    if (flags & ~MMIO_TOUPPER)
        return 0xFFFFFFFFL;
    for (k = 0; k < 4; k++)
        b[k] = s[k] ? s[k] : ' ';
    b[4] = 0;
    if (flags & MMIO_TOUPPER)
        AnsiUpper(b);
    return FCC(b[0], b[1], b[2], b[3]);
}

/* the procedure a compound name "NAME.EXT+ELEMENT" asks for: EXT */
static DWORD compound(LPCSTR name)
{
    LPCSTR plus, dot = NULL, p;
    char ext[5];
    int k;
    if (!name)
        return 0;
    for (plus = name; *plus && *plus != '+'; plus++)
        ;
    if (!*plus)
        return 0;
    for (p = name; p < plus; p++)
        if (*p == '.')
            dot = p + 1;
        else if (*p == '\\' || *p == ':')
            dot = NULL;
    if (!dot)
        return 0;
    for (k = 0; k < 4 && dot + k < plus; k++)
        ext[k] = dot[k];
    for (; k < 4; k++)
        ext[k] = ' ';
    ext[4] = 0;
    AnsiUpper(ext);
    return FCC(ext[0], ext[1], ext[2], ext[3]);
}

/* ---- buffers ---- */
static long bufpos(LPMMFILE f)          /* the file position */
{
    if (!f->i.pchBuffer)
        return f->i.lDiskOffset;
    return f->i.lBufOffset + offof(f->i.pchBuffer, f->i.pchNext);
}

static void emptybuf(LPMMFILE f, long pos)
{
    f->i.lBufOffset = pos;
    f->i.pchNext = f->i.pchEndRead = f->i.pchBuffer;
}

/* a file without a buffer: tell the procedure where MMSYSTEM's place is
   before a read or a write; nonzero when it cannot go there */
static BOOL unbufseek(LPMMFILE f)
{
    if (f->i.lDiskOffset == f->i.lBufOffset)
        return FALSE;
    return send(f, MMIOM_SEEK, f->i.lBufOffset, SEEK_SET) == -1;
}

/* write the buffer back when dirty: 0 or an error */
static UINT flushbuf(LPMMFILE f)
{
    long n, r;
    if (!f->i.pchBuffer || !(f->i.dwFlags & MMIO_DIRTY))
        return 0;
    if (ismem(f)) {
        f->i.dwFlags &= ~MMIO_DIRTY;
        return 0;
    }
    n = offof(f->i.pchBuffer, f->i.pchEndRead);
    if (f->i.lDiskOffset != f->i.lBufOffset &&
        send(f, MMIOM_SEEK, f->i.lBufOffset, 0) == -1)
        return MMIOERR_CANNOTSEEK;
    r = n ? send(f, MMIOM_WRITEFLUSH, (LPARAM)f->i.pchBuffer, n) : 0;
    if (r != n)
        return MMIOERR_CANNOTWRITE;
    f->i.dwFlags &= ~MMIO_DIRTY;
    return 0;
}

/* fill the buffer from the file at lBufOffset: the bytes read */
static long fillbuf(LPMMFILE f)
{
    long r;
    if (ismem(f))
        return 0;
    if (f->i.lDiskOffset != f->i.lBufOffset &&
        send(f, MMIOM_SEEK, f->i.lBufOffset, 0) == -1)
        return -1;
    r = send(f, MMIOM_READ, (LPARAM)f->i.pchBuffer, f->i.cchBuffer);
    f->i.pchNext = f->i.pchBuffer;
    f->i.pchEndRead = at(f->i.pchBuffer, r > 0 ? r : 0);
    return r;
}

static UINT setbuf(LPMMFILE f, char FAR *buf, long cch, BOOL own)
{
    long pos = bufpos(f);
    UINT e = flushbuf(f);
    if (e)
        return e;
    if (f->hbuf) {
        GlobalUnlock(f->hbuf);
        GlobalFree(f->hbuf);
        f->hbuf = 0;
    }
    f->i.dwFlags &= ~MMIO_ALLOCBUF;
    if (cch <= 0) {
        f->i.cchBuffer = 0;
        f->i.pchBuffer = f->i.pchNext = f->i.pchEndRead = f->i.pchEndWrite = NULL;
        f->i.lBufOffset = pos;
        return 0;
    }
    if (!buf) {
        f->hbuf = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, cch);
        buf = f->hbuf ? (char FAR *)GlobalLock(f->hbuf) : NULL;
        if (!buf) {
            if (f->hbuf)
                GlobalFree(f->hbuf);
            f->hbuf = 0;
            f->i.cchBuffer = 0;
            f->i.pchBuffer = f->i.pchNext = f->i.pchEndRead = f->i.pchEndWrite = NULL;
            return MMIOERR_OUTOFMEMORY;
        }
        if (own)
            f->i.dwFlags |= MMIO_ALLOCBUF;
    }
    f->i.cchBuffer = cch;
    f->i.pchBuffer = buf;
    f->i.pchEndWrite = at(buf, cch);
    emptybuf(f, pos);
    return 0;
}

/* a memory file that may grow: by adwInfo[0] bytes at least */
static BOOL growmem(LPMMFILE f, long need)
{
    long inc = (long)f->i.adwInfo[0], cch, nx, er;
    HGLOBAL g;
    char FAR *b;
    if (!f->hbuf || inc <= 0)
        return FALSE;
    cch = f->i.cchBuffer;
    while (cch < need)
        cch += inc;
    nx = offof(f->i.pchBuffer, f->i.pchNext);
    er = offof(f->i.pchBuffer, f->i.pchEndRead);
    GlobalUnlock(f->hbuf);
    g = GlobalReAlloc(f->hbuf, cch, GMEM_MOVEABLE);
    if (!g)
        g = f->hbuf;
    f->hbuf = g;
    b = (char FAR *)GlobalLock(g);
    f->i.pchBuffer = b;
    if (g && GlobalSize(g) >= (DWORD)cch)
        f->i.cchBuffer = cch;
    f->i.pchNext = at(b, nx);
    f->i.pchEndRead = at(b, er);
    f->i.pchEndWrite = at(b, f->i.cchBuffer);
    return f->i.cchBuffer >= need;
}

/* ---- open and close ---- */
WORD MMAPI MMIOOPEN(LPSTR name, LPMMIOINFO lpi, DWORD flags)
{
    HGLOBAL g;
    LPMMFILE f;
    MMIOPROC proc = NULL;
    DWORD fcc = 0;
    OFSTRUCT of;
    HFILE hf;
    LRESULT r;
    WORD err = 0;
    long cch;
    if (lpi)
        lpi->wErrorRet = 0;
    /* the file-name operations */
    if (flags & (MMIO_PARSE | MMIO_EXIST | MMIO_DELETE | MMIO_GETTEMP)) {
        if (!name)
            return 0;
        if (flags & MMIO_GETTEMP) {
            char t[144];
            GetTempFileName(name[0], name + 3, 0, t);
            lstrcpy(name, t);
            return 1;
        }
        hf = OpenFile(name, &of, (flags & MMIO_PARSE) ? OF_PARSE :
                      (flags & MMIO_DELETE) ? OF_DELETE : OF_EXIST);
        if (hf == HFILE_ERROR) {
            if (lpi)
                lpi->wErrorRet = (flags & MMIO_DELETE) ? MMIOERR_CANNOTWRITE : MMIOERR_FILENOTFOUND;
            return 0;
        }
        if (!(flags & MMIO_DELETE))
            lstrcpy(name, of.szPathName);
        return 1;
    }
    /* the procedure */
    if (lpi && lpi->pIOProc) {
        proc = lpi->pIOProc;
        fcc = lpi->fccIOProc;
    } else if (lpi && lpi->fccIOProc) {
        fcc = lpi->fccIOProc;
        proc = findproc(fcc);
    } else if ((fcc = compound(name)) != 0) {
        proc = findproc(fcc);
    } else {
        fcc = FOURCC_DOS;
        proc = findproc(fcc);
    }
    if (!proc) {
        if (lpi)
            lpi->wErrorRet = MMIOERR_FILENOTFOUND;
        return 0;
    }
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, sizeof(MMFILE));
    f = g ? (LPMMFILE)GlobalLock(g) : NULL;
    if (!f) {
        if (g)
            GlobalFree(g);
        if (lpi)
            lpi->wErrorRet = MMIOERR_OUTOFMEMORY;
        return 0;
    }
    f->i.dwFlags = flags;
    f->i.fccIOProc = fcc;
    f->i.pIOProc = proc;
    f->i.hmmio = (WORD)g;
    if (lpi) {
        f->i.adwInfo[0] = lpi->adwInfo[0];
        f->i.adwInfo[1] = lpi->adwInfo[1];
        f->i.adwInfo[2] = lpi->adwInfo[2];
    }
    /* the buffer */
    cch = lpi ? lpi->cchBuffer : 0;
    if (fcc == FOURCC_MEM) {
        if (lpi && lpi->pchBuffer && cch > 0) {
            f->i.cchBuffer = cch;
            f->i.pchBuffer = f->i.pchNext = lpi->pchBuffer;
            f->i.pchEndWrite = at(lpi->pchBuffer, cch);
            f->i.pchEndRead = (flags & MMIO_CREATE) ? lpi->pchBuffer : f->i.pchEndWrite;
        } else if (flags & MMIO_ALLOCBUF) {
            err = setbuf(f, NULL, cch > 0 ? cch : MMIO_DEFAULTBUFFER, TRUE);
        }
    } else if (lpi && lpi->pchBuffer && cch > 0) {
        err = setbuf(f, lpi->pchBuffer, cch, FALSE);
    } else if (flags & MMIO_ALLOCBUF) {
        err = setbuf(f, NULL, cch > 0 ? cch : MMIO_DEFAULTBUFFER, TRUE);
    }
    f->i.dwFlags = flags | (f->i.dwFlags & MMIO_ALLOCBUF);
    if (!err) {
        r = send(f, MMIOM_OPEN, (LPARAM)name, 0);
        err = (WORD)r;
    }
    if (err) {
        if (f->hbuf) {
            GlobalUnlock(f->hbuf);
            GlobalFree(f->hbuf);
        }
        GlobalUnlock(g);
        GlobalFree(g);
        if (lpi)
            lpi->wErrorRet = err;
        return 0;
    }
    f->i.lBufOffset = f->i.lDiskOffset;
    f->next = files;
    files = g;
    GlobalUnlock(g);
    return (WORD)g;
}

UINT MMAPI MMIOCLOSE(WORD h, UINT flags)
{
    LPMMFILE f = lockf(h);
    UINT r;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    r = flushbuf(f);
    if (!r)
        r = (UINT)send(f, MMIOM_CLOSE, flags, 0);
    if (f->hbuf) {
        GlobalUnlock(f->hbuf);
        GlobalFree(f->hbuf);
    }
    /* out of the list */
    {
        HGLOBAL g = files, prev = 0, nx;
        while (g) {
            LPMMFILE e;
            if (g == (HGLOBAL)h) {
                if (prev) {
                    e = (LPMMFILE)GlobalLock(prev);
                    if (e) {
                        e->next = f->next;
                        GlobalUnlock(prev);
                    }
                } else
                    files = f->next;
                break;
            }
            e = (LPMMFILE)GlobalLock(g);
            if (!e)
                break;
            nx = e->next;
            GlobalUnlock(g);
            prev = g;
            g = nx;
        }
    }
    unlockf(h);
    GlobalFree((HGLOBAL)h);
    return r;
}

/* ---- read, write, seek ---- */
long MMAPI MMIOREAD(WORD h, char FAR *pch, long cch)
{
    LPMMFILE f = lockf(h);
    long done = 0, n, r;
    if (!f)
        return -1;
    if (cch < 0)
        cch = 0;
    if (!f->i.pchBuffer) {
        r = unbufseek(f) ? -1 : send(f, MMIOM_READ, (LPARAM)pch, cch);
        f->i.lBufOffset = f->i.lDiskOffset;
        unlockf(h);
        return r;
    }
    while (done < cch) {
        n = offof(f->i.pchNext, f->i.pchEndRead);
        if (n <= 0) {
            if (ismem(f))
                break;
            if (flushbuf(f)) {
                done = done ? done : -1;
                break;
            }
            f->i.lBufOffset += offof(f->i.pchBuffer, f->i.pchEndRead);
            f->i.pchNext = f->i.pchEndRead = f->i.pchBuffer;
            r = fillbuf(f);
            if (r <= 0) {
                if (r < 0 && !done)
                    done = -1;
                break;
            }
            continue;
        }
        if (n > cch - done)
            n = cch - done;
        hcopy(at(pch, done), f->i.pchNext, n);
        f->i.pchNext = at(f->i.pchNext, n);
        done += n;
    }
    unlockf(h);
    return done;
}

long MMAPI MMIOWRITE(WORD h, char FAR *pch, long cch)
{
    LPMMFILE f = lockf(h);
    long done = 0, n, r;
    if (!f)
        return -1;
    if (cch < 0)
        cch = 0;
    if (!f->i.pchBuffer) {
        r = unbufseek(f) ? -1 : send(f, MMIOM_WRITE, (LPARAM)pch, cch);
        f->i.lBufOffset = f->i.lDiskOffset;
        unlockf(h);
        return r;
    }
    if (!(f->i.dwFlags & (MMIO_WRITE | MMIO_READWRITE))) {
        unlockf(h);
        return -1;
    }
    while (done < cch) {
        n = offof(f->i.pchNext, f->i.pchEndWrite);
        if (n <= 0) {
            if (ismem(f)) {
                if (!growmem(f, offof(f->i.pchBuffer, f->i.pchNext) + (cch - done)))
                    break;
                continue;
            }
            if (flushbuf(f)) {
                break;
            }
            f->i.lBufOffset += offof(f->i.pchBuffer, f->i.pchEndRead);
            f->i.pchNext = f->i.pchEndRead = f->i.pchBuffer;
            continue;
        }
        if (n > cch - done)
            n = cch - done;
        hcopy(f->i.pchNext, at(pch, done), n);
        f->i.pchNext = at(f->i.pchNext, n);
        if (offof(f->i.pchEndRead, f->i.pchNext) > 0)
            f->i.pchEndRead = f->i.pchNext;
        f->i.dwFlags |= MMIO_DIRTY;
        done += n;
    }
    unlockf(h);
    return done ? done : (cch ? -1 : 0);
}

long MMAPI MMIOSEEK(WORD h, long off, int origin)
{
    LPMMFILE f = lockf(h);
    long pos, end, r;
    if (!f)
        return -1;
    if (origin < 0 || origin > 2) {
        unlockf(h);
        return -1;
    }
    if (!f->i.pchBuffer) {
        /* the place is MMSYSTEM's (lBufOffset): the procedure is told
           at the next read or write, or now for SEEK_END */
        if (origin == SEEK_END) {
            r = send(f, MMIOM_SEEK, off, SEEK_END);
            if (r != -1)
                f->i.lBufOffset = r;
        } else {
            /* taken as it is, even before the start (3.1's mmioSeek(-1,
               SEEK_SET) answers -1 and is then at -1; the next read or
               write fails: MMP) */
            r = (origin == SEEK_SET ? 0 : f->i.lBufOffset) + off;
            f->i.lBufOffset = r;
        }
        unlockf(h);
        return r;
    }
    if (origin == SEEK_SET && off < 0) {       /* -1 on 3.1 (MMP) */
        unlockf(h);
        return -1;
    }
    end = offof(f->i.pchBuffer, f->i.pchEndRead);
    if (ismem(f)) {
        /* SEEK_END counts back from the end: 3.1's answers -1 to
           (-3, SEEK_END) and 64 to (0, SEEK_END) in a 64-byte file (MMP);
           UNVERIFIED for a positive offset */
        pos = origin == SEEK_SET ? off : origin == SEEK_CUR ?
              offof(f->i.pchBuffer, f->i.pchNext) + off : end - off;
        if (pos < 0 || pos > end) {
            unlockf(h);
            return -1;
        }
        f->i.pchNext = at(f->i.pchBuffer, pos);
        unlockf(h);
        return pos;
    }
    if (origin == SEEK_END) {
        if (flushbuf(f)) {
            unlockf(h);
            return -1;
        }
        r = send(f, MMIOM_SEEK, off, SEEK_END);
        if (r != -1)
            emptybuf(f, r);
        unlockf(h);
        return r;
    }
    pos = origin == SEEK_SET ? off : bufpos(f) + off;
    if (pos < 0) {
        unlockf(h);
        return -1;
    }
    if (pos >= f->i.lBufOffset && pos <= f->i.lBufOffset + end) {
        f->i.pchNext = at(f->i.pchBuffer, pos - f->i.lBufOffset);
    } else {
        if (flushbuf(f)) {
            unlockf(h);
            return -1;
        }
        emptybuf(f, pos);
    }
    unlockf(h);
    return pos;
}

/* ---- the buffer, seen and changed by the application ---- */
UINT MMAPI MMIOGETINFO(WORD h, LPMMIOINFO lpi, UINT flags)
{
    LPMMFILE f = lockf(h);
    (void)flags;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    *lpi = f->i;
    unlockf(h);
    return 0;
}

static void takeinfo(LPMMFILE f, LPMMIOINFO lpi)
{
    if (f->i.pchBuffer) {
        f->i.pchNext = lpi->pchNext;
        f->i.pchEndRead = lpi->pchEndRead;
        if (offof(f->i.pchEndRead, f->i.pchNext) > 0)
            f->i.pchEndRead = f->i.pchNext;
    }
    f->i.dwFlags = (f->i.dwFlags & ~MMIO_DIRTY) | (lpi->dwFlags & MMIO_DIRTY);
    f->i.adwInfo[0] = lpi->adwInfo[0];
    f->i.adwInfo[1] = lpi->adwInfo[1];
    f->i.adwInfo[2] = lpi->adwInfo[2];
}

UINT MMAPI MMIOSETINFO(WORD h, LPMMIOINFO lpi, UINT flags)
{
    LPMMFILE f = lockf(h);
    (void)flags;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    takeinfo(f, lpi);
    unlockf(h);
    return 0;
}

UINT MMAPI MMIOSETBUFFER(WORD h, char FAR *buf, long cch, UINT flags)
{
    LPMMFILE f = lockf(h);
    UINT r;
    (void)flags;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    r = setbuf(f, buf, cch, TRUE);
    unlockf(h);
    return r;
}

UINT MMAPI MMIOFLUSH(WORD h, UINT flags)
{
    LPMMFILE f = lockf(h);
    UINT r;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    r = flushbuf(f);
    if (!r && (flags & MMIO_EMPTYBUF) && f->i.pchBuffer && !ismem(f))
        emptybuf(f, bufpos(f));
    unlockf(h);
    return r;
}

UINT MMAPI MMIOADVANCE(WORD h, LPMMIOINFO lpi, UINT flags)
{
    LPMMFILE f = lockf(h);
    UINT r = 0;
    long n;
    if (!f)
        return MMSYSERR_INVALHANDLE;
    if (!f->i.pchBuffer) {
        unlockf(h);
        return MMIOERR_UNBUFFERED;
    }
    if (lpi)
        takeinfo(f, lpi);
    if (ismem(f)) {
        if ((flags & MMIO_WRITE) && !growmem(f, f->i.cchBuffer + 1))
            r = MMIOERR_CANNOTEXPAND;
    } else {
        long pos = bufpos(f);
        r = flushbuf(f);
        if (!r) {
            emptybuf(f, pos);
            if (!(flags & MMIO_WRITE)) {
                n = fillbuf(f);
                if (n < 0)
                    r = MMIOERR_CANNOTREAD;
            }
        }
    }
    if (lpi)
        *lpi = f->i;
    unlockf(h);
    return r;
}

LRESULT MMAPI MMIOSENDMESSAGE(WORD h, UINT msg, LPARAM l1, LPARAM l2)
{
    LPMMFILE f = lockf(h);
    LRESULT r;
    if (!f)
        return 0;
    r = send(f, msg, l1, l2);
    unlockf(h);
    return r;
}

/* ---- RIFF chunks ---- */
static BOOL readck(WORD h, LPMMCKINFO ck)
{
    DWORD hd[2];
    if (MMIOREAD(h, (char FAR *)hd, 8) != 8)
        return FALSE;
    ck->ckid = hd[0];
    ck->cksize = hd[1];
    ck->dwFlags = 0;
    ck->dwDataOffset = MMIOSEEK(h, 0, SEEK_CUR);
    if (ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST) {
        if (MMIOREAD(h, (char FAR *)&ck->fccType, 4) != 4)
            return FALSE;
    } else
        ck->fccType = 0;
    return TRUE;
}

UINT MMAPI MMIODESCEND(WORD h, LPMMCKINFO ck, LPMMCKINFO parent, UINT flags)
{
    DWORD id = ck->ckid, type = ck->fccType, end = 0xFFFFFFFFL;
    long skip;
    if (parent)
        end = parent->dwDataOffset + parent->cksize;
    for (;;) {
        if (!readck(h, ck))
            return MMIOERR_CHUNKNOTFOUND;
        if (flags & MMIO_FINDCHUNK) {
            if (ck->ckid == id)
                return 0;
        } else if (flags & MMIO_FINDRIFF) {
            if (ck->ckid == FOURCC_RIFF && ck->fccType == type)
                return 0;
        } else if (flags & MMIO_FINDLIST) {
            if (ck->ckid == FOURCC_LIST && ck->fccType == type)
                return 0;
        } else
            return 0;
        /* not it: on past it (padded to a word) */
        skip = (long)((ck->cksize + 1) & ~1L);
        if (ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST)
            skip -= 4;
        if (MMIOSEEK(h, skip, SEEK_CUR) == -1)
            return MMIOERR_CHUNKNOTFOUND;
        if ((DWORD)MMIOSEEK(h, 0, SEEK_CUR) >= end)
            return MMIOERR_CHUNKNOTFOUND;
    }
}

UINT MMAPI MMIOASCEND(WORD h, LPMMCKINFO ck, UINT flags)
{
    long pos, size;
    (void)flags;
    if (ck->dwFlags & MMIO_DIRTY) {
        pos = MMIOSEEK(h, 0, SEEK_CUR);
        if (pos == -1)
            return MMIOERR_CANNOTSEEK;
        size = pos - (long)ck->dwDataOffset;
        if (size < 0)
            return MMIOERR_CANNOTSEEK;
        if ((DWORD)size != ck->cksize) {
            ck->cksize = size;
            if (MMIOSEEK(h, ck->dwDataOffset - 4, SEEK_SET) == -1)
                return MMIOERR_CANNOTSEEK;
            if (MMIOWRITE(h, (char FAR *)&ck->cksize, 4) != 4)
                return MMIOERR_CANNOTWRITE;
            if (MMIOSEEK(h, pos, SEEK_SET) == -1)
                return MMIOERR_CANNOTSEEK;
        }
        if (size & 1) {
            char z = 0;
            if (MMIOWRITE(h, &z, 1) != 1)
                return MMIOERR_CANNOTWRITE;
        }
        return 0;
    }
    if (MMIOSEEK(h, (long)ck->dwDataOffset + (long)((ck->cksize + 1) & ~1L), SEEK_SET) == -1)
        return MMIOERR_CANNOTSEEK;
    return 0;
}

UINT MMAPI MMIOCREATECHUNK(WORD h, LPMMCKINFO ck, UINT flags)
{
    DWORD hd[3];
    long n = 8;
    if (flags & MMIO_CREATERIFF)
        ck->ckid = FOURCC_RIFF;
    else if (flags & MMIO_CREATELIST)
        ck->ckid = FOURCC_LIST;
    hd[0] = ck->ckid;
    hd[1] = ck->cksize;
    if (flags & (MMIO_CREATERIFF | MMIO_CREATELIST)) {
        hd[2] = ck->fccType;
        n = 12;
    }
    ck->dwDataOffset = MMIOSEEK(h, 0, SEEK_CUR) + 8;
    if (MMIOWRITE(h, (char FAR *)hd, n) != n)
        return MMIOERR_CANNOTWRITE;
    ck->dwFlags = MMIO_DIRTY;
    return 0;
}

UINT MMAPI MMIORENAME(LPCSTR a, LPCSTR b, LPMMIOINFO lpi, DWORD flags)
{
    MMIOPROC proc;
    MMIOINFO i;
    (void)flags;
    if (lpi && lpi->pIOProc)
        proc = lpi->pIOProc;
    else if (lpi && lpi->fccIOProc)
        proc = findproc(lpi->fccIOProc);
    else if (compound(a))
        proc = findproc(compound(a));
    else
        proc = findproc(FOURCC_DOS);
    if (!proc)
        return MMIOERR_FILENOTFOUND;
    if (lpi)
        i = *lpi;
    else {
        int k;
        for (k = 0; k < (int)sizeof i; k++)
            ((BYTE *)&i)[k] = 0;
    }
    return (UINT)proc((LPSTR)&i, MMIOM_RENAME, (LPARAM)a, (LPARAM)b);
}
