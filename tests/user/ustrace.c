/*
 * ustrace.c - USTRACE.DLL, probe US1's recorder (ARCHITECTURE.md 10).
 *
 * US1 observes the calls made into a module and the calls it makes, by
 * putting a forwarding module in front of the real one (tools/mkinterpose.py
 * generates those; tests/ustrace.sh sets the boot up in a scratch copy of the
 * reference install). Every forwarding thunk calls TRREC here with its
 * caller's frame, which is copied into a record (registers, return address,
 * 16 stack words, up to three far-pointer arguments' first 24 bytes, the
 * current task); for modules whose returns are logged, TRREC also swaps the
 * return address for the thunk module's return stub, which calls TRRET.
 *
 * Records go to a 1 MB memory buffer and are written to C:\USTRACE.BIN
 * (append) when USER's GetMessage, PeekMessage, WaitMessage or ExitWindows
 * is entered: task time, never inside DOS or an interrupt. Per-ordinal call
 * counts go to C:\USCOUNT.BIN. tools/ustrace.py decodes both.
 *
 * SYSTEM.INI [ustrace]: cap=N (default 40) records per module and ordinal
 * (0: no limit); returns are recorded under the same cap.
 *
 * Callable at interrupt time: TRREC/TRRET touch only this DLL's fixed data
 * and fixed buffers, with interrupts off while a record is reserved.
 */
#include <windows.h>
#include <dos.h>

#define NSEG     16
#define PERSEG   390            /* records per 64 KB block */
#define NMOD     16
#define NORD     1024
#define NSHADOW  1024

typedef struct rec {
    WORD kind, modid, ord, task;    /* kind 1 entry, 2 return, 3 lost return */
    DWORD seq;
    WORD regs[12];                  /* ax bx cx dx si di bp ds es ss sp fl */
    WORD ret_ip, ret_cs;
    WORD args[16];                  /* stack words above the return address */
    struct { WORD argw, sel, off; BYTE len, pad; BYTE data[24]; } ptr[3];
} rec;                              /* 168 bytes */

typedef struct shadow {
    WORD ss, slot, ret_ip, ret_cs, abytes, modid, ord, logged;
    DWORD seq;
} shadow;

static WORD bufsel[NSEG];
static WORD nrec;
static DWORD seq, lost;
static WORD __far *counts;          /* NMOD * NORD */
static WORD cap = 40;
static shadow sh[NSHADOW];
static WORD nsh;
static WORD polls;
static WORD exiting;                   /* ExitWindows entered */
static char binname[] = "C:\\USTRACE.BIN";
static char cntname[] = "C:\\USCOUNT.BIN";


WORD irq_save(void);
#pragma aux irq_save = "pushf" "pop ax" "cli" value [ax];
void irq_restore(WORD);
#pragma aux irq_restore = "push ax" "popf" parm [ax];
/* bytes readable at sel:off (0 if none), at most n */
WORD readable(WORD sel, WORD off, WORD n);
#pragma aux readable = \
    ".286p" \
    "verr ax" "jnz bad" \
    "lsl cx, ax" "jnz bad" \
    "cmp bx, cx" "ja bad" \
    "sub cx, bx" "inc cx" "jz done" \
    "cmp cx, dx" "jae done" \
    "mov dx, cx" "jmp done" \
    "bad: xor dx, dx" \
    "done:" \
    parm [ax] [bx] [dx] value [dx] modify [cx];

static rec __far *reserve(void)
{
    WORD f = irq_save(), i;
    rec __far *r = 0;
    if (nrec < NSEG * PERSEG && bufsel[nrec / PERSEG]) {
        i = nrec++;
        r = (rec __far *)MAKELP(bufsel[i / PERSEG], (i % PERSEG) * sizeof(rec));
    } else
        lost++;
    irq_restore(f);
    return r;
}

/* frame: es ds di si bp sp bx dx cx ax fl bp [slot0 slot1] ... (see
   tools/mkinterpose.py); top = index of the caller's return address */
static void fill(rec __far *r, WORD __far *frame, WORD top, WORD pmask)
{
    static const BYTE map[12] = { 9, 6, 8, 7, 3, 2, 11, 1, 0, 0, 0, 10 };
    WORD i, k, j;
    for (i = 0; i < 12; i++)
        r->regs[i] = frame[map[i]];
    r->regs[9] = FP_SEG(frame);
    r->regs[10] = FP_OFF(frame) + 2 * top;    /* SP as the callee sees it */
    r->ret_ip = frame[top];
    r->ret_cs = frame[top + 1];
    k = readable(FP_SEG(frame), FP_OFF(frame) + 2 * (top + 2), 32) / 2;
    for (i = 0; i < 16; i++)
        r->args[i] = i < k ? frame[top + 2 + i] : 0;
    for (i = 0; i < 3; i++)
        r->ptr[i].len = 0, r->ptr[i].argw = 0xFFFF;
    for (i = 0, k = 0; i < 15 && k < 3; i++) {
        WORD off, sel, n;
        if (!(pmask & (1u << i)))
            continue;
        off = r->args[i];
        sel = r->args[i + 1];
        r->ptr[k].argw = i;
        r->ptr[k].sel = sel;
        r->ptr[k].off = off;
        n = sel ? readable(sel, off, 24) : 0;
        r->ptr[k].len = (BYTE)n;
        for (j = 0; j < n; j++)
            r->ptr[k].data[j] = ((BYTE __far *)MAKELP(sel, off))[j];
        k++;
    }
    r->task = GetCurrentTask();
}

/* KERNEL.133, GetExePtr: the module of any of its handles (Schulman,
   Undocumented Windows; USER itself imports it) */
extern HMODULE __far __pascal GETEXEPTR(HANDLE);
#define NSEEN 512
static WORD seen[NSEEN], nseen;
static char segname[] = "C:\\USSEGS.TXT";

static void hexw(char *p, WORD v)
{
    int i;
    for (i = 3; i >= 0; i--, v >>= 4)
        p[i] = "0123456789ABCDEF"[v & 15];
}

/* append "CS MODULE SEG" lines for the callers' code segments not yet
   named (task time only) */
static void name_segments(WORD n)
{
    WORD i, k, cs;
    char line[32];
    HFILE h = HFILE_ERROR;
    for (i = 0; i < n; i++) {
        rec __far *r = (rec __far *)MAKELP(bufsel[i / PERSEG], (i % PERSEG) * sizeof(rec));
        cs = r->ret_cs;
        for (k = 0; k < nseen && seen[k] != cs; k++)
            ;
        if (k < nseen || nseen == NSEEN)
            continue;
        seen[nseen++] = cs;
        {
            DWORD gh = readable(cs, 0, 1) ? GlobalHandle(cs) : 0;
            HMODULE m = gh ? GETEXEPTR((HANDLE)LOWORD(gh)) : 0;
            BYTE __far *db;
            WORD segno = 0, nl, j;
            if (m && readable((WORD)m, 0, 0x40) == 0x40) {
                db = (BYTE __far *)MAKELP(m, 0);
                if (db[0] == 'N' && db[1] == 'E') {
                    WORD st = *(WORD __far *)(db + 0x22), cnt = *(WORD __far *)(db + 0x1C);
                    for (j = 0; j < cnt; j++) {
                        WORD hd = *(WORD __far *)(db + st + 10 * j + 8);
                        if (hd && (hd | 1) == (cs | 1)) {
                            segno = j + 1;
                            break;
                        }
                    }
                } else
                    m = 0;
            } else
                m = 0;
            if (h == HFILE_ERROR) {
                h = _lopen(segname, OF_WRITE);
                if (h == HFILE_ERROR)
                    h = _lcreat(segname, 0);
                if (h == HFILE_ERROR)
                    return;
                _llseek(h, 0, 2);
            }
            hexw(line, cs);
            line[4] = ' ';
            _lwrite(h, line, 5);
            if (m) {
                db = (BYTE __far *)MAKELP(m, 0);
                db += *(WORD __far *)(db + 0x26);
                nl = db[0];
                _lwrite(h, (LPCSTR)(db + 1), nl);
                line[0] = ' ';
                hexw(line + 1, segno);
                line[5] = '\r', line[6] = '\n';
                _lwrite(h, line, 7);
            } else
                _lwrite(h, "?\r\n", 3);
        }
    }
    if (h != HFILE_ERROR)
        _lclose(h);
}

static void flush(void)
{
    WORD f, n, i, s;
    HFILE h;
    f = irq_save();
    n = nrec;
    irq_restore(f);
    if (n) {
        name_segments(n);
        h = _lopen(binname, OF_WRITE);
        if (h != HFILE_ERROR) {
            _llseek(h, 0, 2);
            for (s = 0; s * PERSEG < n; s++) {
                i = n - s * PERSEG;
                if (i > PERSEG)
                    i = PERSEG;
                _lwrite(h, MAKELP(bufsel[s], 0), i * sizeof(rec));
            }
            _lclose(h);
        }
        f = irq_save();
        /* records reserved meanwhile (interrupt time) move down */
        if (nrec > n) {
            for (i = n; i < nrec; i++) {
                rec __far *a = (rec __far *)MAKELP(bufsel[i / PERSEG], (i % PERSEG) * sizeof(rec));
                rec __far *b = (rec __far *)MAKELP(bufsel[(i - n) / PERSEG], ((i - n) % PERSEG) * sizeof(rec));
                *b = *a;
            }
        }
        nrec -= n;
        irq_restore(f);
    }
    if (n || !(polls & 255)) {
        h = _lcreat(cntname, 0);
        if (h != HFILE_ERROR) {
            DWORD hdr[2];
            hdr[0] = seq;
            hdr[1] = lost;
            _lwrite(h, (LPCSTR)hdr, sizeof hdr);
            _lwrite(h, (LPCSTR)counts, NMOD * NORD * 2);
            _lclose(h);
        }
    }
}

/* the thunk's frame; returns nothing. wantret: swap the return address
   for stub (the thunk module's return stub) */
void __far __pascal __loadds TRREC(WORD modid, WORD ord, WORD pmask, WORD abytes,
                                  WORD wantret, void __far *stub, WORD __far *frame)
{
    WORD f, c, log;
    DWORD myseq;
    rec __far *r;
    f = irq_save();
    myseq = ++seq;
    c = 0;
    if (counts && modid < NMOD && ord < NORD) {
        c = counts[modid * NORD + ord];
        if (c != 0xFFFF)
            counts[modid * NORD + ord] = c + 1;
    }
    irq_restore(f);
    log = !cap || c < cap;
    if (log && (r = reserve()) != 0) {
        r->kind = 1;
        r->modid = modid;
        r->ord = ord;
        r->seq = myseq;
        fill(r, frame, 15, pmask);
    }
    if (wantret) {
        f = irq_save();
        if (nsh < NSHADOW) {
            shadow *s = &sh[nsh++];
            s->ss = FP_SEG(frame);
            s->slot = FP_OFF(frame) + 30;
            s->ret_ip = frame[15];
            s->ret_cs = frame[16];
            s->abytes = abytes;
            s->modid = modid;
            s->ord = ord;
            s->logged = log;
            s->seq = myseq;
            frame[15] = FP_OFF(stub);
            frame[16] = FP_SEG(stub);
        }
        irq_restore(f);
    }
    if (modid == 0 && (ord == 108 || ord == 109 || ord == 112 || ord == 7)) {
        polls++;
        if (ord == 7)
            exiting = 1;
        flush();
    } else if (exiting && modid != 0)
        /* after ExitWindows: each of USER's calls out (task time) is
           written at once, as Windows ends without another poll */
        flush();
}

/* the return stub's frame: es ds di si bp sp bx dx cx ax fl bp [slot0
   slot1] then the caller's stack; puts the real return address in the
   slots */
void __far __pascal __loadds TRRET(WORD __far *frame)
{
    WORD f, i, found, sp, ss;
    rec __far *r;
    shadow s;
    ss = FP_SEG(frame);
    sp = FP_OFF(frame) + 28;            /* SP after the callee's return */
    f = irq_save();
    found = 0xFFFF;
    for (i = nsh; i-- > 0;) {
        if (sh[i].ss != ss)
            continue;
        if (sh[i].abytes == 0xFFFF || sh[i].slot + 4 + sh[i].abytes == sp) {
            found = i;
            break;
        }
    }
    if (found == 0xFFFF) {
        irq_restore(f);
        FatalExit(0x5553);              /* "US": a return we cannot place */
        return;
    }
    s = sh[found];
    /* drop it, and deeper entries of this stack (left by Throw) */
    {
        WORD o = found;
        for (i = found + 1; i < nsh; i++)
            if (sh[i].ss != ss)
                sh[o++] = sh[i];
        nsh = o;
    }
    irq_restore(f);
    frame[12] = s.ret_ip;
    frame[13] = s.ret_cs;
    if (s.logged && (r = reserve()) != 0) {
        r->kind = 2;
        r->modid = s.modid;
        r->ord = s.ord;
        r->seq = s.seq;
        fill(r, frame, 12, 0);
        r->regs[10] = sp;
    }
}

void __far __pascal __loadds TRFLUSH(void)
{
    flush();
}

int __far __cdecl hub_init(void)
{
    WORD i;
    HFILE h;
    HGLOBAL g;
    cap = GetPrivateProfileInt("ustrace", "cap", 40, "SYSTEM.INI");
    for (i = 0; i < NSEG; i++) {
        g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)PERSEG * sizeof(rec));
        if (!g)
            break;
        bufsel[i] = FP_SEG(GlobalLock(g));
    }
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)NMOD * NORD * 2);
    if (g)
        counts = (WORD __far *)GlobalLock(g);
    h = _lcreat(binname, 0);
    if (h != HFILE_ERROR)
        _lclose(h);
    h = _lcreat(segname, 0);
    if (h != HFILE_ERROR)
        _lclose(h);
    return 1;
}
