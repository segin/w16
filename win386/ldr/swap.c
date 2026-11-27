/* swap.c - w16's WIN386.EXE, real-mode part: the paging (swap) file
 * (ARCHITECTURE.md 12.16), made before Windows starts and removed when it
 * ends; the VMM (win386/vmm/pager.c) pages to it.
 *
 * SYSTEM.INI [386Enh], as Windows 3.1 takes them (VX18, tests/vmprobe.sh:
 * WIN386.SWP appears or not, 0500h's paging-file size) and as Control
 * Panel's 386 Enhanced applet writes them (shell/cpl/win386):
 *   Paging=no          no paging file
 *   PermSwapDOSDrive=D, PermSwapSizeK=KB
 *                      a permanent file, 386SPART.PAR in D's root (hidden
 *                      and system), kept when Windows ends; made here when
 *                      it is not there (w16's Control Panel writes only
 *                      the keys)
 *   PagingFile=PATH    the temporary file (default: WIN386.SWP in the
 *                      Windows directory)
 *   PagingDrive=D      the temporary file in D's root, as WIN386.SWP
 *   MaxPagingFileSize=KB  its largest size
 *   MinUserDiskSpace=KB   disk space left free (default 500)
 * The temporary file's largest size: half the drive's free space, as
 * Control Panel's recommended size (UNVERIFIED for 3.1), leaving
 * MinUserDiskSpace free, at most MaxPagingFileSize; none below 512 KB (in
 * Windows 3.1, MinUserDiskSpace past the free space gave no paging file:
 * 0500h's size 0, no WIN386.SWP). The VMM keeps the file within the
 * linear address space ([386Enh] PageOverCommit).
 * The temporary file is made 2 MB long (or its largest size, if less) and
 * grows as pages are written past that; it is deleted when Windows ends
 * (Windows 3.1, ARCHITECTURE.md 12.2 step 7).
 */
#include <dos.h>
#include <string.h>
#include "w386.h"

char __far *ini_386_value(const char *key);   /* ldr.c */

static char swappath[128];
static unsigned swaph = 0xFFFF;
static int swaptemp;

static int ini_off(char __far *q)
{
    return q && (*q == '0' || ((*q | 0x20) == 'n') || ((*q | 0x20) == 'f')
                 || ((*q | 0x20) == 'o' && (q[1] | 0x20) == 'f'));
}

static unsigned long ini_kb(const char *key, unsigned long def)
{
    char __far *q = ini_386_value(key);
    unsigned long v = 0;
    int any = 0;
    if (!q) return def;
    while (*q == ' ' || *q == '\t') q++;
    while (*q >= '0' && *q <= '9' && v < 0x10000000UL) v = v * 10 + (*q++ - '0'), any = 1;
    return any ? v : def;
}

/* the value of key up to the line's end or a blank, into buf */
static int ini_word(const char *key, char *buf, unsigned n)
{
    char __far *q = ini_386_value(key);
    unsigned i = 0;
    if (!q) return 0;
    while (*q && *q != '\r' && *q != '\n' && *q != ' ' && *q != '\t' && *q != ';' && i + 1 < n)
        buf[i++] = *q++;
    buf[i] = 0;
    return i != 0;
}

/* free KB on drive (1 = A:), 0 if none */
static unsigned long disk_free_kb(unsigned drive)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)drive;
    intdos(&r, &r);
    if (r.x.ax == 0xFFFF) return 0;
    return (unsigned long)r.x.ax * r.x.cx / 512 * r.x.bx / 2;
}

/* create (or open) path; the handle, not inherited by KRNL386 */
static unsigned swap_open(const char *path, unsigned attr, int keep)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    r.x.ax = 0x6C00;
    r.x.bx = 0x0082;                    /* read/write, not inherited */
    r.x.cx = attr;
    r.x.dx = keep ? 0x0011 : 0x0012;    /* open or create; replace */
    r.x.si = (unsigned)path;
    intdosx(&r, &r, &s);
    if (!r.x.cflag) return r.x.ax;
    /* (a DOS without 6Ch: create, then open without inheritance) */
    r.h.ah = 0x3C;
    r.x.cx = attr;
    r.x.dx = (unsigned)path;
    if (keep) {
        r.x.ax = 0x3D82;
        intdosx(&r, &r, &s);
        if (!r.x.cflag) return r.x.ax;
        r.h.ah = 0x3C;
        r.x.cx = attr;
        r.x.dx = (unsigned)path;
    }
    intdosx(&r, &r, &s);
    if (r.x.cflag) return 0xFFFF;
    r.x.bx = r.x.ax;
    r.h.ah = 0x3E;
    intdos(&r, &r);
    r.x.ax = 0x3D82;
    r.x.dx = (unsigned)path;
    intdosx(&r, &r, &s);
    return r.x.cflag ? 0xFFFF : r.x.ax;
}

/* make the file a size of kb (a permanent one is all there from the start) */
static int swap_extend(unsigned h, unsigned long kb)
{
    union REGS r;
    unsigned long at = kb * 1024UL;
    if (!kb) return 1;
    r.x.ax = 0x4200;
    r.x.bx = h;
    r.x.cx = (unsigned)(at >> 16);
    r.x.dx = (unsigned)at;
    intdos(&r, &r);
    if (r.x.cflag) return 0;
    r.h.ah = 0x40;                      /* a write of 0 bytes sets the size */
    r.x.bx = h;
    r.x.cx = 0;
    intdos(&r, &r);
    return !r.x.cflag;
}

/* windir: the Windows directory with a backslash at its end */
void swap_setup(struct w386_info *in, const char *windir)
{
    char t[128];
    unsigned long kb, free, minfree;
    unsigned drive;

    in->swap_handle = 0xFFFF;
    in->swap_pages = 0;
    in->swap_initial = 0;
    in->swap_type = 1;                  /* PageSwap's "no pager" */
    kb = ini_kb("PageOverCommit", 4);
    in->overcommit = kb >= 1 && kb <= 20 ? (unsigned)kb : 4;
    if (ini_off(ini_386_value("Paging"))) return;
    if (ini_word("PermSwapDOSDrive", t, sizeof t)) {
        /* a permanent paging file */
        kb = ini_kb("PermSwapSizeK", 0) & ~3UL;
        swappath[0] = t[0];
        strcpy(swappath + 1, ":\\386SPART.PAR");
        if (kb < 512) return;
        swaph = swap_open(swappath, 0x06, 1);       /* hidden, system */
        if (swaph == 0xFFFF) return;
        if (!swap_extend(swaph, kb)) {
            union REGS r;
            r.h.ah = 0x3E; r.x.bx = swaph; intdos(&r, &r);
            swaph = 0xFFFF;
            return;
        }
        swaptemp = 0;
        in->swap_initial = kb / 4;
    } else {
        if (ini_word("PagingFile", t, sizeof t) && t[0] && t[1] == ':')
            strcpy(swappath, t);
        else if (ini_word("PagingDrive", t, sizeof t)) {
            swappath[0] = t[0];
            strcpy(swappath + 1, ":\\WIN386.SWP");
        } else {
            strcpy(swappath, windir);
            strcat(swappath, "WIN386.SWP");
        }
        drive = (swappath[0] | 0x20) - 'a' + 1;
        free = disk_free_kb(drive);
        minfree = ini_kb("MinUserDiskSpace", 500);
        /* half the free space (Control Panel's recommended size), leaving
         * MinUserDiskSpace free */
        kb = free / 2;
        if (free - kb < minfree) kb = free > minfree ? free - minfree : 0;
        kb = ini_kb("MaxPagingFileSize", kb) < kb ? ini_kb("MaxPagingFileSize", kb) : kb;
        kb &= ~3UL;
        if (kb < 512) return;
        swaph = swap_open(swappath, 0, 0);
        if (swaph == 0xFFFF) return;
        swaptemp = 1;
        /* Windows 3.1's WIN386.SWP was 2,048 KB once Windows ran, with
         * MaxPagingFileSize 2048 as without it (VX18) */
        in->swap_initial = (kb < 2048 ? kb : 2048) / 4;
        if (!swap_extend(swaph, in->swap_initial * 4)) in->swap_initial = 0;
    }
    in->swap_handle = swaph;
    in->swap_pages = kb / 4;
    in->swap_type = 2;                  /* the MS-DOS pager */
}

/* Windows has ended: the temporary file goes */
void swap_cleanup(void)
{
    union REGS r;
    struct SREGS s;
    if (swaph == 0xFFFF) return;
    r.h.ah = 0x3E;
    r.x.bx = swaph;
    intdos(&r, &r);
    swaph = 0xFFFF;
    if (!swaptemp) return;
    segread(&s);
    r.h.ah = 0x41;
    r.x.dx = (unsigned)swappath;
    intdosx(&r, &r, &s);
}
