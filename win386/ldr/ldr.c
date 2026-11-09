/* ldr.c - w16's WIN386.EXE, the real-mode part: the 386 Enhanced mode
 * loader (ARCHITECTURE.md 12).
 *
 * What it does, in the order Windows 3.1's WIN386 was observed to
 * (ARCHITECTURE.md 12.2): checks the machine, finds SYSTEM.INI, issues the
 * INT 2Fh 1605h start-up broadcast, takes the extended memory from XMS,
 * loads the VMM (the "WIN386" module of its own W3 table, an LE file) into
 * it with its fixups applied, builds the page tables and enters the VMM in
 * protected mode. The VMM comes back here in V86 mode as the System VM,
 * where this code EXECs KRNL386.EXE; when that ends, the exit breakpoint
 * returns to real mode, the 1606h broadcast is issued and WIN386 exits
 * with KERNEL's exit code.
 *
 * Messages are Windows 3.1's, byte for byte, where they were observed;
 * the others are marked UNVERIFIED.
 *
 * Sources: XMS 2.0; IBM's LX format description (LE layout); Windows 3.1
 * DDK, INT 2Fh 1605h/1606h; black-box observation (tests/vxobs.sh).
 */
#include <dos.h>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include "w386.h"

/* ldrasm.asm */
extern int __cdecl cpu_is_386(void);
extern unsigned __cdecl cpu_msw(void);
struct xregs { unsigned ax, bx, dx, si; };
extern void __cdecl xms_call(struct xregs *r);
extern int __cdecl pm_go(struct w386_info *info);
extern int __cdecl pm_exit(int code);
extern unsigned __cdecl bp_table_off(void);
extern unsigned __cdecl rm_tramp_off(void);
extern void __far *xms_entry;
#pragma aux xms_entry "_*"

static struct w386_info info;
static char refstk[2048];           /* V86 stack for interrupts reflected from PM */
static unsigned char pmbp[320];
static char xbuf[16384 + 16];       /* DOS calls from PM copy through this */     /* PM breakpoints: HLT (F4h), privileged at ring 3 */
static char ownpath[128], sysdir[128], inipath[128], krnlpath[128];
static unsigned xms_handle;
static unsigned v86cb_off, v86cb_seg;   /* Enable/Disable V86 mode callback (1605h) */

/* ---- output ---- */
static void out(const char *s)
{
    union REGS r;
    struct SREGS sr;

    segread(&sr);
    r.h.ah = 0x40;
    r.x.bx = 1;
    r.x.cx = strlen(s);
    r.x.dx = (unsigned)s;
    intdosx(&r, &r, &sr);
}

static void fail(const char *s)
{
    out(s);
    exit(0xFF);
}

/* ---- files ---- */
static int exists(const char *path)
{
    int h = open(path, O_RDONLY | O_BINARY);
    if (h < 0) return 0;
    close(h);
    return 1;
}

static int readat(int h, unsigned long off, void __far *buf, unsigned len)
{
    unsigned got;

    if (lseek(h, off, SEEK_SET) != (long)off) return 0;
    if (_dos_read(h, buf, len, &got) || got != len) return 0;
    return 1;
}

/* ---- XMS ---- */
static int xms_move_to(unsigned long dst, void __far *src, unsigned long len)
{
    static struct {
        unsigned long len;
        unsigned sh;
        unsigned long so;
        unsigned dh;
        unsigned long doff;
    } mv;
    struct xregs r;

    mv.len = (len + 1) & ~1UL;
    mv.sh = 0;
    mv.so = ((unsigned long)FP_SEG(src) << 16) | FP_OFF(src);
    mv.dh = xms_handle;
    mv.doff = dst;
    r.ax = 0x0B00;
    r.si = (unsigned)&mv;
    xms_call(&r);
    return r.ax == 1;
}

/* ---- SYSTEM.INI ---- */
static char __far *ini;
static unsigned inilen;

static int load_ini(const char *path)
{
    int h = open(path, O_RDONLY | O_BINARY);
    long len;

    if (h < 0) return 0;
    len = lseek(h, 0, SEEK_END);
    if (len > 60000L) len = 60000L;     /* DOS-sized buffer; UNVERIFIED limit */
    inilen = (unsigned)len;
    ini = _fmalloc(inilen + 1);
    if (!ini || !readat(h, 0, ini, inilen)) { close(h); return 0; }
    ini[inilen] = 0;
    close(h);
    return 1;
}

/* ---- the VMM image ---- */
#define PAGE 4096UL

struct le_obj { unsigned long vsize, base, flags, pmidx, pmcnt, res; };

static unsigned char __far *page;   /* one page being prepared */

static unsigned long ld32(const unsigned char __far *p)
{
    return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned ld16(const unsigned char __far *p)
{
    return p[0] | (p[1] << 8);
}

/* Patch a 32-bit value at offset at (may be partly outside this page). */
static void patch32(long at, unsigned long v)
{
    int i;
    for (i = 0; i < 4; i++, v >>= 8)
        if (at + i >= 0 && at + i < (long)PAGE) page[at + i] = (unsigned char)v;
}

/* Read the W3 table of our own file, load the VMM module ("WIN386") into
 * the window at info.img_off with its fixups applied. Returns 0 on error. */
static int load_vmm(void)
{
    int h, i, n;
    unsigned char hdr[16], ent[16];
    unsigned long w3, modoff = 0, modsize = 0, nobj, objoff[8], datapages, npages;
    unsigned long pagesize, lastpage, fpt, frt, objtab, objmap, total;
    unsigned char __far *ldr;
    struct le_obj __far *obj;
    unsigned long o, k;

    h = open(ownpath, O_RDONLY | O_BINARY);
    if (h < 0) return 0;
    if (!readat(h, 0x3C, hdr, 4)) goto bad;
    w3 = ld32(hdr);
    if (!readat(h, w3, hdr, 16) || hdr[0] != 'W' || hdr[1] != '3') goto bad;
    n = ld16(hdr + 4);
    for (i = 0; i < n; i++) {
        if (!readat(h, w3 + 16 + i * 16UL, ent, 16)) goto bad;
        if (!_fmemcmp(ent, "WIN386  ", 8)) {
            modoff = ld32(ent + 8);
            modsize = ld32(ent + 12);
        }
    }
    if (!modoff || modsize > 65000UL) goto bad;
    ldr = _fmalloc((unsigned)modsize + 16);
    page = _fmalloc((unsigned)PAGE);
    if (!ldr || !page) goto bad;
    if (!readat(h, modoff, ldr, (unsigned)modsize)) goto bad;
    if (ldr[0] != 'L' || ldr[1] != 'E') goto bad;
    npages = ld32(ldr + 0x14);
    pagesize = ld32(ldr + 0x28);
    lastpage = ld32(ldr + 0x2C);
    objtab = ld32(ldr + 0x40);
    nobj = ld32(ldr + 0x44);
    objmap = ld32(ldr + 0x48);
    fpt = ld32(ldr + 0x68);
    frt = ld32(ldr + 0x6C);
    datapages = ld32(ldr + 0x80);
    if (pagesize != PAGE || nobj > 8) goto bad;
    obj = (struct le_obj __far *)(ldr + objtab);
    /* place the objects one after another, page aligned */
    total = 0;
    for (o = 0; o < nobj; o++) {
        objoff[o] = info.img_off + total;
        total += (obj[o].vsize + PAGE - 1) & ~(PAGE - 1);
    }
    info.img_size = total;
    if (info.img_off + total + 64 * PAGE > info.xms_size) goto bad;
    /* the entry: the DDB's control procedure is not it; the VMM's LE entry
     * point (EIP object/offset in the header) is */
    info.entry_lin = W386_VMM_LIN + objoff[ld32(ldr + 0x18) - 1] + ld32(ldr + 0x1C);
    for (o = 0; o < nobj; o++) {
        for (k = 0; k * PAGE < obj[o].vsize; k++) {
            unsigned long pg = 0, start, end, j;
            _fmemset(page, 0, (unsigned)PAGE);
            if (k < obj[o].pmcnt) {
                unsigned char __far *pm = ldr + objmap + (obj[o].pmidx - 1 + k) * 4;
                pg = ((unsigned long)pm[0] << 16) | ((unsigned long)pm[1] << 8) | pm[2];
                if (pg && pm[3] == 0) {
                    unsigned len = (unsigned)(pg == npages ? lastpage : PAGE);
                    if (!readat(h, datapages + (pg - 1) * PAGE, page, len)) goto bad;
                }
            }
            if (pg) {
                /* fixups of this page */
                start = ld32(ldr + fpt + (pg - 1) * 4);
                end = ld32(ldr + fpt + pg * 4);
                j = frt + start;
                while (j < frt + end) {
                    unsigned char src = ldr[j], flg = ldr[j + 1];
                    long so = (int)ld16(ldr + j + 2);
                    unsigned long tobj, toff, tgt;
                    j += 4;
                    if ((src & 0x20) || (flg & 3) != 0) goto bad;     /* lists, imports */
                    if (flg & 0x40) { tobj = ld16(ldr + j); j += 2; } else tobj = ldr[j++];
                    if (flg & 0x10) { toff = ld32(ldr + j); j += 4; } else { toff = ld16(ldr + j); j += 2; }
                    if (!tobj || tobj > nobj) goto bad;
                    tgt = W386_VMM_LIN + objoff[tobj - 1] + toff;
                    switch (src & 0x0F) {
                    case 0x07:  patch32(so, tgt); break;
                    case 0x08:  patch32(so, tgt - (W386_VMM_LIN + objoff[o] + k * PAGE + so + 4)); break;
                    default:    goto bad;
                    }
                }
            }
            if (!xms_move_to(objoff[o] + k * PAGE, page, PAGE)) goto bad;
        }
    }
    close(h);
    _ffree(ldr);
    return 1;
bad:
    close(h);
    return 0;
}

/* Page directory (window offset 0), the identity table for the first 4 MB
 * (only the first megabyte and the HMA are present), and the tables that
 * map the window at W386_VMM_LIN. */
static int build_page_tables(void)
{
    unsigned long __far *pt = (unsigned long __far *)page;
    unsigned long i, t;

    _fmemset(page, 0, (unsigned)PAGE);
    pt[0] = (info.xms_phys + PAGE) | 7;                 /* user, writable, present */
    for (t = 0; t < info.pt_count; t++)
        pt[(W386_VMM_LIN >> 22) + t] = (info.xms_phys + (2 + t) * PAGE) | 3;
    if (!xms_move_to(0, page, PAGE)) return 0;
    _fmemset(page, 0, (unsigned)PAGE);
    for (i = 0; i < 0x110; i++)
        pt[i] = (i * PAGE) | 7;
    if (!xms_move_to(PAGE, page, PAGE)) return 0;
    for (t = 0; t < info.pt_count; t++) {
        for (i = 0; i < 1024; i++) {
            unsigned long off = (t * 1024 + i) * PAGE;
            pt[(unsigned)i] = off < info.xms_size ? ((info.xms_phys + off) | 3) : 0;
        }
        if (!xms_move_to((2 + t) * PAGE, page, PAGE)) return 0;
    }
    return 1;
}

/* EXEC KRNL386.EXE with our own command tail, FCBs and environment. */
static int run_kernel(void)
{
    static struct {
        unsigned env;
        void __far *tail, __far *fcb1, __far *fcb2;
    } pb;
    union REGS r;
    struct SREGS sr;

    pb.env = 0;
    pb.tail = MK_FP(_psp, 0x80);
    pb.fcb1 = MK_FP(_psp, 0x5C);
    pb.fcb2 = MK_FP(_psp, 0x6C);
    segread(&sr);
    sr.es = sr.ds;
    r.x.ax = 0x4B00;
    r.x.dx = (unsigned)krnlpath;
    r.x.bx = (unsigned)&pb;
    intdosx(&r, &r, &sr);
    if (r.x.cflag) {
        out("Cannot find or load required file KRNL386.EXE. The file was not found.\r\n");
        return 0xFF;
    }
    r.h.ah = 0x4D;
    intdos(&r, &r);
    return r.h.al;
}

static void broadcast_1606(void)
{
    union REGS r;
    r.x.ax = 0x1606;
    r.x.dx = 0;
    int86(0x2F, &r, &r);
}

int main(int argc, char **argv)
{
    union REGS r;
    struct SREGS sr;
    struct xregs x;
    char *p;
    int code;
    unsigned long kb, phys;

    (void)argc;
    if (!cpu_is_386()) {
        /* UNVERIFIED: Windows 3.1's WIN386 hung in DOSBox-X on an 80286 */
        fail("Cannot run Windows in 386 enhanced mode on this processor.\r\n");
    }
    if (cpu_msw() & 1) {
        /* UNVERIFIED wording; GEMMIS/the 1605h V86 callback is handled below,
         * a V86 monitor without it is not */
    }
    /* where we are: SYSTEM.INI and KRNL386.EXE are found from here */
    strcpy(ownpath, argv[0]);
    strcpy(sysdir, ownpath);
    p = strrchr(sysdir, '\\');
    if (p) p[1] = 0; else sysdir[0] = 0;
    strcpy(inipath, sysdir);
    strcat(inipath, "SYSTEM.INI");
    if (!load_ini(inipath)) {
        /* the parent directory */
        strcpy(inipath, sysdir);
        p = inipath + strlen(inipath);
        if (p > inipath) p[-1] = 0;
        p = strrchr(inipath, '\\');
        if (p) p[1] = 0; else inipath[0] = 0;
        strcat(inipath, "SYSTEM.INI");
        if (!load_ini(inipath))
            fail("Cannot find the system initialization file needed to run Windows.\r\n\r\n"
                 "You need to run the Setup program again.\r\n");
    }
    /* XMS */
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80)
        fail("Cannot run Windows in 386 enhanced mode with the currently installed\r\n"
             "extended memory driver.\r\n");
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &sr);
    xms_entry = MK_FP(sr.es, r.x.bx);
    strcpy(krnlpath, sysdir);
    strcat(krnlpath, "KRNL386.EXE");
    if (!exists(krnlpath))
        fail("Cannot find or load required file KRNL386.EXE. The file was not found.\r\n");

    /* INT 2Fh 1605h: ES:BX = DS:SI = 0, CX = 0, DX = 0 (Enhanced), DI = 030Ah */
    {
        union REGS b;
        struct SREGS bs;
        segread(&bs);
        b.x.ax = 0x1605;
        b.x.bx = 0; bs.es = 0;
        b.x.si = 0; bs.ds = 0;
        b.x.cx = 0;
        b.x.dx = 0;
        b.x.di = 0x030A;
        int86x(0x2F, &b, &b, &bs);
        if (b.x.cx) {
            broadcast_1606();
            exit(0xFF);         /* UNVERIFIED exit code */
        }
        v86cb_off = b.x.si;
        v86cb_seg = bs.ds;
        if (v86cb_seg | v86cb_off) {
            /* A V86 monitor (an EMM) offered its Enable/Disable callback.
             * Not supported yet (DOSBox-X's EMM does not run in V86 mode,
             * so it could not be tested). UNVERIFIED wording. */
            broadcast_1606();
            fail("Cannot run Windows in 386 enhanced mode with the currently installed\r\n"
                 "memory manager.\r\n");
        }
        /* ES:BX: the Win386_Startup_Info_Struc chain; not used yet (no
         * VxDs from TSRs, no instance data): see ARCHITECTURE.md 12 */
    }

    /* all the extended memory */
    x.ax = 0x0800; x.bx = 0;
    xms_call(&x);
    kb = x.ax;
    info.mem_kb = x.dx;
    if (kb < 1024) {
        broadcast_1606();
        /* UNVERIFIED: Windows 3.1's own message for too little memory */
        fail("Not enough extended memory to run Windows in 386 enhanced mode.\r\n");
    }
    x.ax = 0x0900; x.dx = (unsigned)kb;
    xms_call(&x);
    if (x.ax != 1) { broadcast_1606(); fail("Not enough extended memory to run Windows in 386 enhanced mode.\r\n"); }
    xms_handle = x.dx;
    x.ax = 0x0C00; x.dx = xms_handle;
    xms_call(&x);
    if (x.ax != 1) { broadcast_1606(); fail("Not enough extended memory to run Windows in 386 enhanced mode.\r\n"); }
    phys = ((unsigned long)x.dx << 16) | x.bx;
    x.ax = 0x0300;                      /* global A20 on */
    xms_call(&x);

    info.magic = W386_MAGIC;
    info.xms_phys = (phys + PAGE - 1) & ~(PAGE - 1);
    info.xms_size = (kb * 1024UL - (info.xms_phys - phys)) & ~(PAGE - 1);
    info.pt_count = (info.xms_size + 0x3FFFFFUL) >> 22;
    info.img_off = (2 + info.pt_count) * PAGE;
    if (!page) page = _fmalloc((unsigned)PAGE);
    if (!load_vmm() || !build_page_tables()) {
        x.ax = 0x0D00; x.dx = xms_handle; xms_call(&x);
        x.ax = 0x0A00; x.dx = xms_handle; xms_call(&x);
        broadcast_1606();
        fail("Cannot find or load required file WIN386.EXE.\r\n");      /* UNVERIFIED */
    }
    segread(&sr);
    info.info_lin = ((unsigned long)sr.ds << 4) + (unsigned)&info;
    info.ldr_cs = sr.cs;
    info.ldr_ds = sr.ds;
    info.bp_off = bp_table_off();
    info.bp_count = 64;
    info.rm_tramp = rm_tramp_off();
    info.refstk_seg = sr.ds;
    info.refstk_top = (unsigned)(refstk + sizeof(refstk) - 16);
    info.psp = _psp;
    memset(pmbp, 0xF4, sizeof(pmbp));
    info.pmbp_off = (unsigned)pmbp;
    info.pmbp_count = sizeof(pmbp);
    r.x.ax = 0x5D06;                    /* the swappable data area: current PSP */
    intdosx(&r, &r, &sr);
    info.sda_lin = r.x.cflag ? 0 : ((unsigned long)sr.ds << 4) + r.x.si;
    segread(&sr);
    /* the transfer buffer: paragraph aligned, in our data segment */
    info.xbuf_seg = sr.ds + (((unsigned)xbuf + 15) >> 4);
    info.xbuf_size = sizeof(xbuf) - 16;
    info.pic1_base = 0x08;
    info.pic2_base = 0x70;
    info.log_port = 0x3F8;              /* the VMM's trace goes to COM1 */
    {   /* [w16] vmmtrace=1 (w16's own key) turns on the detailed trace */
        char __far *q;
        for (q = ini; q && *q; q++)
            if ((*q == 'v' || *q == 'V') && !_fmemicmp(q, "vmmtrace=1", 10)) info.trace = 1;
    }
    r.h.ah = 0x48; r.x.bx = 0xFFFF;
    intdos(&r, &r);
    info.conv_free = r.x.bx;
    _ffree(page);
    _ffree(ini);

    pm_go(&info);
    /* ---- from here on: V86 mode, the System VM ---- */
    code = run_kernel();
    code = pm_exit(code);
    /* ---- real mode again ---- */
    x.ax = 0x0400; xms_call(&x);        /* global A20 off */
    x.ax = 0x0D00; x.dx = xms_handle; xms_call(&x);
    x.ax = 0x0A00; x.dx = xms_handle; xms_call(&x);
    broadcast_1606();
    return code;
}
