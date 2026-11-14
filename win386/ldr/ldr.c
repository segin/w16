/* ldr.c - w16's WIN386.EXE, the real-mode part: the 386 Enhanced mode
 * loader (ARCHITECTURE.md 12).
 *
 * What it does, in the order Windows 3.1's WIN386 was observed to
 * (ARCHITECTURE.md 12.2): checks the machine, finds SYSTEM.INI, issues the
 * INT 2Fh 1605h start-up broadcast, takes the extended memory from XMS,
 * loads the VMM (the "WIN386" module of its own W3 table, an LE file) into
 * it with its fixups applied, loads the VxDs SYSTEM.INI names the same
 * way after it, builds the page tables and enters the VMM in protected
 * mode. The VMM comes back here in V86 mode as the System VM,
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
static char refstk[W386_REFSTK_SIZE + 16]; /* V86 stack for interrupts reflected from PM */
static unsigned char pmbp[320];
static char xbuf[16384 + 16];       /* DOS calls from PM copy through this */     /* PM breakpoints: HLT (F4h), privileged at ring 3 */
static char ownpath[128], sysdir[128], inipath[128], krnlpath[128];
static unsigned xms_handle;
static unsigned v86cb_off, v86cb_seg;   /* Enable/Disable V86 mode callback (1605h) */
static char fatalmsg[W386_FATAL_SIZE];  /* the VMM's message when it ends Windows early */

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

/* The image: the VMM first, then each VxD loaded, page aligned, from
 * window offset info.img_off on; img_next is where the next one goes. */
static unsigned long img_next;

/* Load the LE module whose header is at file offset leoff in file h (size:
 * the header, loader and fixup sections; data-page offsets are from the
 * start of the file, as in a .386 file and in w16's and Windows 3.1's W3
 * tables) at img_next with its fixups applied. *entry gets the linear
 * address of the LE entry point (the VMM's), *ddb that of entry 1 (a VxD's
 * DDB: its only export). Returns 0 on error. */
static int load_le(int h, unsigned long leoff, unsigned long lesize,
                   unsigned long *entry, unsigned long *ddb)
{
    unsigned long nobj, objoff[8], datapages, npages;
    unsigned long pagesize, lastpage, fpt, frt, objtab, objmap, total, et;
    unsigned char __far *ldr;
    struct le_obj __far *obj;
    unsigned long o, k;

    if (lesize > 65000UL || lesize < 0xB0) return 0;
    ldr = _fmalloc((unsigned)lesize + 16);
    if (!ldr) return 0;
    if (!readat(h, leoff, ldr, (unsigned)lesize)) goto bad;
    if (ldr[0] != 'L' || ldr[1] != 'E') goto bad;
    npages = ld32(ldr + 0x14);
    pagesize = ld32(ldr + 0x28);
    lastpage = ld32(ldr + 0x2C);
    objtab = ld32(ldr + 0x40);
    nobj = ld32(ldr + 0x44);
    objmap = ld32(ldr + 0x48);
    et = ld32(ldr + 0x5C);
    fpt = ld32(ldr + 0x68);
    frt = ld32(ldr + 0x6C);
    datapages = ld32(ldr + 0x80);
    if (pagesize != PAGE || !nobj || nobj > 8) goto bad;
    obj = (struct le_obj __far *)(ldr + objtab);
    /* place the objects one after another, page aligned */
    total = 0;
    for (o = 0; o < nobj; o++) {
        objoff[o] = img_next + total;
        total += (obj[o].vsize + PAGE - 1) & ~(PAGE - 1);
    }
    if (img_next + total + 64 * PAGE > info.xms_size) goto bad;
    if (entry && ld32(ldr + 0x18) >= 1 && ld32(ldr + 0x18) <= nobj)
        *entry = W386_VMM_LIN + objoff[ld32(ldr + 0x18) - 1] + ld32(ldr + 0x1C);
    if (ddb) {
        /* entry 1: the first bundle (count, type 3 = 32-bit, object, then
         * flags and a 32-bit offset per entry); type 0 bundles skip */
        unsigned long e = et, ord = 1;
        *ddb = 0;
        while (e + 2 <= lesize && ldr[e]) {
            unsigned cnt = ldr[e], typ = ldr[e + 1];
            if (typ == 0) { ord += cnt; e += 2; continue; }
            if (typ != 3 || ord != 1 || e + 4 + 5 > lesize) break;
            o = ld16(ldr + e + 2);
            if (o >= 1 && o <= nobj) *ddb = W386_VMM_LIN + objoff[o - 1] + ld32(ldr + e + 5);
            break;
        }
        if (!*ddb) goto bad;
    }
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
                    /* a record: source type, flags, the source offset
                     * (with a source list: a count, the offsets after the
                     * target), the target object and offset */
                    unsigned char src = ldr[j], flg = ldr[j + 1];
                    unsigned long tobj, toff, tgt, cnt, c;
                    long so;
                    if ((flg & 3) != 0) goto bad;                     /* imports: none in a VxD */
                    if (src & 0x20) { cnt = ldr[j + 2]; j += 3; } else { cnt = 1; j += 2; }
                    so = (src & 0x20) ? 0 : (int)ld16(ldr + j);
                    if (!(src & 0x20)) j += 2;
                    if (flg & 0x40) { tobj = ld16(ldr + j); j += 2; } else tobj = ldr[j++];
                    if (flg & 0x10) { toff = ld32(ldr + j); j += 4; } else { toff = ld16(ldr + j); j += 2; }
                    if (!tobj || tobj > nobj) goto bad;
                    tgt = W386_VMM_LIN + objoff[tobj - 1] + toff;
                    for (c = 0; c < cnt; c++) {
                        if (src & 0x20) { so = (int)ld16(ldr + j); j += 2; }
                        switch (src & 0x0F) {
                        case 0x07:  patch32(so, tgt); break;
                        case 0x08:  patch32(so, tgt - (W386_VMM_LIN + objoff[o] + k * PAGE + so + 4)); break;
                        default:    goto bad;
                        }
                    }
                }
            }
            if (!xms_move_to(objoff[o] + k * PAGE, page, PAGE)) goto bad;
        }
    }
    img_next += total;
    _ffree(ldr);
    return 1;
bad:
    _ffree(ldr);
    return 0;
}

/* the W3 table of our own file: the module named name (8 bytes, blank
 * padded); its LE header's offset and size, 0 if it is not there */
static unsigned long w3_find(int h, const char *name, unsigned long *size)
{
    unsigned char hdr[16], ent[16];
    unsigned long w3;
    int i, n;

    if (!readat(h, 0x3C, hdr, 4)) return 0;
    w3 = ld32(hdr);
    if (!readat(h, w3, hdr, 16) || hdr[0] != 'W' || hdr[1] != '3') return 0;
    n = ld16(hdr + 4);
    for (i = 0; i < n; i++) {
        if (!readat(h, w3 + 16 + i * 16UL, ent, 16)) return 0;
        if (!_fmemicmp(ent, name, 8)) {
            *size = ld32(ent + 12);
            return ld32(ent + 8);
        }
    }
    return 0;
}

/* Load the VMM module ("WIN386") of our own W3 table into the window at
 * info.img_off. Returns 0 on error. */
static int load_vmm(void)
{
    int h, ok;
    unsigned long off, size;

    h = open(ownpath, O_RDONLY | O_BINARY);
    if (h < 0) return 0;
    img_next = info.img_off;
    off = w3_find(h, "WIN386  ", &size);
    ok = off && load_le(h, off, size, &info.entry_lin, NULL);
    close(h);
    return ok;
}

/* ---- the VxDs (SYSTEM.INI [386Enh]) ----
 * Windows 3.1 loads the devices that [386Enh] names: device= lines and
 * the keys that name a device (mouse=*vmd, display=*vddvga,
 * keyboard=*vkd, network=*dosnet,*vnetbios, ebios=*ebios: the default
 * SYSTEM.INI's). "*name" is a module of WIN386.EXE's W3 table, anything
 * else a file in WIN386's directory (observed: C:\WINDOWS\SYSTEM\
 * vtdapi.386, the name as written). A name not found gives the observed
 * message and "Press a key to continue", and Windows goes on without it
 * (ARCHITECTURE.md 12.2). w16's own devices are part of its VMM module
 * for now: the built-in names Windows 3.1 has are accepted as provided.
 * UNVERIFIED: which other keys Windows 3.1 reads devices from, and what
 * it does with a name given twice (w16 loads it once). */
static const char *const builtin[] = {
    "vmd", "vnetbios", "dosnet", "ebios", "vddvga", "vkd", "vtd",
    "reboot", "vdmad", "vsd", "v86mmgr", "pageswap", "dosmgr", "vmpoll",
    "wshell", "blockdev", "pagefile", "vfd", "parity", "biosxlat", "vcd",
    "vmcpd", "combuff", "cdpscsi", "int13", "wdctrl", "qemmfix", 0
};
static const char *const devkeys[] = {
    "device", "mouse", "display", "keyboard", "network", "ebios", 0
};
#define MAXNAME 80
static char done_names[2048];       /* the names handled, each NUL ended */
static unsigned done_len;
static unsigned long __far *vxd_ddb;     /* the DDBs' linear addresses, in load order */
static unsigned vxd_cap;

static void wait_key(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
}

static void device_missing(const char *name)
{
    out("Cannot find a device file that may be needed to run w16 in\r\n"
        "386 enhanced mode.\r\n\r\nYou need to run the Setup program again.\r\n");
    out(name);
    out("\r\nPress a key to continue\r\n");
    wait_key();
}

static int name_done(const char *name)
{
    unsigned i = 0, n = strlen(name) + 1;
    while (i < done_len) {
        if (!stricmp(done_names + i, name)) return 1;
        i += strlen(done_names + i) + 1;
    }
    if (done_len + n <= sizeof(done_names)) {
        strcpy(done_names + done_len, name);
        done_len += n;
    }
    return 0;
}

static void load_device(const char *name)
{
    char path[160];
    unsigned long off = 0, size = 0, ddb;
    unsigned char hdr[4];
    int h, i, ok = 0;

    if (!*name || name_done(name)) return;
    if (name[0] == '*') {
        char mod[9];
        for (i = 0; i < 8; i++) mod[i] = ' ';
        mod[8] = 0;
        for (i = 0; i < 8 && name[1 + i]; i++) mod[i] = name[1 + i];
        h = open(ownpath, O_RDONLY | O_BINARY);
        if (h >= 0) {
            if (stricmp(name + 1, "win386") && (off = w3_find(h, mod, &size)) != 0)
                ok = load_le(h, off, size, NULL, &ddb);
            close(h);
        }
        if (!off) {
            for (i = 0; builtin[i]; i++)
                if (!stricmp(builtin[i], name + 1)) return;     /* w16's VMM provides it */
            device_missing(name);
            return;
        }
    } else {
        if (strlen(sysdir) + strlen(name) >= sizeof(path)) { device_missing(name); return; }
        strcpy(path, sysdir);
        strcat(path, name);
        h = open(path, O_RDONLY | O_BINARY);
        if (h < 0) { device_missing(name); return; }
        /* an LE file: MZ stub, e_lfanew -> LE; its sections up to the data pages */
        if (readat(h, 0x3C, hdr, 4)) {
            off = ld32(hdr);
            if (readat(h, off + 0x80, hdr, 4) && ld32(hdr) > off)
                ok = load_le(h, off, ld32(hdr) - off, NULL, &ddb);
        }
        close(h);
    }
    if (!ok) {
        /* UNVERIFIED: what Windows 3.1 says for a file that is not a VxD */
        device_missing(name);
        return;
    }
    if (info.vxd_count == vxd_cap) {
        unsigned long __far *nl = _frealloc(vxd_ddb, (vxd_cap ? vxd_cap * 2 : 16) * 4UL);
        if (!nl) { device_missing(name); return; }      /* UNVERIFIED */
        vxd_ddb = nl;
        vxd_cap = vxd_cap ? vxd_cap * 2 : 16;
    }
    vxd_ddb[(unsigned)info.vxd_count++] = ddb;
}

/* every device the [386Enh] keys name, in the file's order */
static void load_devices(void)
{
    char __far *q = ini;
    char key[16], val[MAXNAME + 1];
    int in386 = 0, i, n;

    while (q && *q) {
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '[') {
            in386 = !_fmemicmp(q, "[386Enh]", 8);
        } else if (in386) {
            n = 0;
            while (*q && *q != '=' && *q != '\r' && *q != '\n' && n < (int)sizeof(key) - 1)
                key[n++] = *q++;
            while (n && (key[n - 1] == ' ' || key[n - 1] == '\t')) n--;
            key[n] = 0;
            if (*q == '=') {
                for (i = 0; devkeys[i]; i++)
                    if (!stricmp(key, devkeys[i])) break;
                q++;
                if (devkeys[i]) {
                    /* a comma-separated list of names */
                    for (;;) {
                        while (*q == ' ' || *q == '\t') q++;
                        n = 0;
                        while (*q && *q != ',' && *q != '\r' && *q != '\n' && *q != ';' && n < MAXNAME)
                            val[n++] = *q++;
                        while (n && (val[n - 1] == ' ' || val[n - 1] == '\t')) n--;
                        val[n] = 0;
                        load_device(val);
                        if (*q != ',') break;
                        q++;
                    }
                }
            }
        }
        while (*q && *q != '\n') q++;
        if (*q) q++;
    }
}

/* the DDB list into the image after the modules, for the VMM */
static int put_vxd_list(void)
{
    if (!info.vxd_count) return 1;
    info.vxd_list = img_next;
    if (img_next + info.vxd_count * 4 + 64 * PAGE > info.xms_size) return 0;
    if (!xms_move_to(img_next, vxd_ddb, info.vxd_count * 4)) return 0;
    img_next += (info.vxd_count * 4 + PAGE - 1) & ~(PAGE - 1);
    return 1;
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
        fail("Cannot run w16 in 386 enhanced mode on this processor.\r\n");
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
            fail("Cannot find the system initialization file needed to run w16.\r\n\r\n"
                 "You need to run the Setup program again.\r\n");
    }
    /* XMS */
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80)
        fail("Cannot run w16 in 386 enhanced mode with the currently installed\r\n"
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
            fail("Cannot run w16 in 386 enhanced mode with the currently installed\r\n"
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
        fail("Not enough extended memory to run w16 in 386 enhanced mode.\r\n");
    }
    x.ax = 0x0900; x.dx = (unsigned)kb;
    xms_call(&x);
    if (x.ax != 1) { broadcast_1606(); fail("Not enough extended memory to run w16 in 386 enhanced mode.\r\n"); }
    xms_handle = x.dx;
    x.ax = 0x0C00; x.dx = xms_handle;
    xms_call(&x);
    if (x.ax != 1) { broadcast_1606(); fail("Not enough extended memory to run w16 in 386 enhanced mode.\r\n"); }
    phys = ((unsigned long)x.dx << 16) | x.bx;
    x.ax = 0x0300;                      /* global A20 on */
    xms_call(&x);

    info.magic = W386_MAGIC;
    info.xms_phys = (phys + PAGE - 1) & ~(PAGE - 1);
    info.xms_size = (kb * 1024UL - (info.xms_phys - phys)) & ~(PAGE - 1);
    info.pt_count = (info.xms_size + 0x3FFFFFUL) >> 22;
    info.img_off = (2 + info.pt_count) * PAGE;
    if (!page) page = _fmalloc((unsigned)PAGE);
    if (load_vmm()) {
        load_devices();
        if (!put_vxd_list()) info.vxd_count = 0;
        info.img_size = img_next - info.img_off;
    }
    if (!info.img_size || !build_page_tables()) {
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
    info.tail_lin = ((unsigned long)_psp << 4) + 0x80;
    segread(&sr);
    info.fatal_lin = ((unsigned long)sr.ds << 4) + (unsigned)fatalmsg;
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

    if (pm_go(&info) == (int)W386_FATAL) {
        /* the VMM ended Windows before the System VM ran: its message */
        x.ax = 0x0400; xms_call(&x);
        x.ax = 0x0D00; x.dx = xms_handle; xms_call(&x);
        x.ax = 0x0A00; x.dx = xms_handle; xms_call(&x);
        out(fatalmsg);
        broadcast_1606();
        return 0xFF;                    /* UNVERIFIED exit code */
    }
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
