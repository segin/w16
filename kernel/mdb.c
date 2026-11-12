/*
 * mdb.c - the in-memory module table (module database): what an hModule
 * is the selector of.
 *
 * Layout: as published in Pietrek, "Windows Internals" (1993), Table 3-1
 * (facts only), checked black-box on Windows 3.1 with tests/apps/tk11.c
 * (Standard and 386 Enhanced mode agree). It is the NE header and tables
 * of the file, changed as follows:
 *   02h  usage count (GetModuleUsage)
 *   04h  near pointer to the entry table, in its in-memory form: bundles
 *        of {first ordinal - 1, last ordinal, next bundle}, each followed
 *        by 5-byte entries {type (segment number, FFh moveable), flags,
 *        segment, offset}; ordinals not exported have no bundle
 *   06h  selector of the next module table (0 ends the list)
 *   08h  near pointer to the segment table entry of DGROUP
 *   0Ah  near pointer to the load file information: length byte (of what
 *        follows), fixed-media flag (1), error word, DOS date and time
 *        words, the file name (full path, NUL)
 *   0Ch  flags: those of the file, with 0040h set (observed on every
 *        module; meaning unknown) and 4000h for a DLL whose WEP is to be
 *        called
 *   22h  the segment table: entries of 10 bytes, the file's 8 and the
 *        segment's global handle
 *   28h  module reference table: the hModules of the modules imported
 *        from (not offsets of names)
 *   38h, 3Ah  copies of 2Ah (imported names); 3Ah differs for KERNEL
 * Pointers are near, into the table itself; the segment table directly
 * follows the 40h-byte header (observed), then (w16's order, UNVERIFIED)
 * the resource table, resident names, module references, imported names,
 * entry table and load file information.
 */
#include "kernel.h"

#define LF_HEAD 8               /* load file info before the name */

static u16 entry_size(const u8 FAR *p, const u8 FAR *end)
{
    u16 n = 0;
    while (p < end && p[0]) {
        u8 count = p[0], type = p[1];
        p += 2;
        if (type == NEB_UNUSED)
            continue;
        n += 6 + 5 * count;
        p += (type == NEB_MOVABLE ? sizeof(ne_entry_movable)
                                  : sizeof(ne_entry_fixed)) * count;
    }
    return n;
}

/* The in-memory entry table at d (offset `at` in the table). */
static void entry_convert(const u8 FAR *p, const u8 FAR *end, u8 FAR *base, u16 at)
{
    u16 ord = 0, prev = 0;
    while (p < end && p[0]) {
        u8 count = p[0], type = p[1], k;
        u8 FAR *b;
        p += 2;
        if (type == NEB_UNUSED) {
            ord += count;
            continue;
        }
        if (prev)
            *(u16 FAR *)(base + prev + 4) = at;
        b = base + at;
        *(u16 FAR *)(b + 0) = ord;
        *(u16 FAR *)(b + 2) = ord + count;
        *(u16 FAR *)(b + 4) = 0;
        prev = at;
        b += 6;
        for (k = 0; k < count; k++, b += 5) {
            if (type == NEB_MOVABLE) {
                const ne_entry_movable FAR *e = (const ne_entry_movable FAR *)p + k;
                b[0] = NEB_MOVABLE;
                b[1] = e->flags;
                b[2] = e->seg;
                *(u16 FAR *)(b + 3) = e->offset;
            } else {
                const ne_entry_fixed FAR *e = (const ne_entry_fixed FAR *)p + k;
                b[0] = type;            /* UNVERIFIED for constants (FEh) */
                b[1] = e->flags;
                b[2] = type == NEB_CONSTANT ? 0 : type;
                *(u16 FAR *)(b + 3) = e->offset;
            }
        }
        p += (type == NEB_MOVABLE ? sizeof(ne_entry_movable)
                                  : sizeof(ne_entry_fixed)) * count;
        at += 6 + 5 * count;
        ord += count;
    }
}

static void copy(u8 FAR *d, const u8 FAR *s, u16 n)
{
    kmemcpy(d, s, n);
}

/* Build the module table from ft, the file's NE header and tables (from
   the header to the end of the entry table). Returns its selector, 0 if
   out of memory. Usage, next module, segment handles and module
   references are filled in by the loader. */
u16 mdb_build(const u8 FAR *ft, const char FAR *path, u16 date, u16 time,
              int is_kernel)
{
    const ne_header FAR *fh = (const ne_header FAR *)ft;
    u16 nseg = fh->seg_count, plen = kstrlen(path);
    u16 rs_len = fh->resname_off - fh->rsrc_off;
    u16 rn_len = fh->modref_off - fh->resname_off;
    u16 mr_len = 2 * fh->modref_count;
    u16 im_len = fh->entry_off - fh->impname_off;
    u16 en_len = entry_size(ft + fh->entry_off, ft + fh->entry_off + fh->entry_len);
    u16 o_seg = sizeof(ne_header), o_rs, o_rn, o_mr, o_im, o_en, o_lf, i, sel;
    u32 total;
    u8 FAR *t;
    ne_header FAR *h;

    /* the sum in 32 bits first: a file whose tables add up past 64K
       wrapped the offsets before the check (KERNEL audit, low) */
    total = (u32)o_seg + ((u32)nseg << 3) + ((u32)nseg << 1) /* MDB_SEGSIZE */ +
            rs_len + rn_len + mr_len +
            im_len + en_len + 1 + LF_HEAD - 1 + plen + 1;
    if (total > 0xFFF0UL || !galloc(GMEM_FIXED, total, 0, &sel))
        return 0;
    o_rs = o_seg + MDB_SEGSIZE * nseg;
    o_rn = o_rs + rs_len;
    o_mr = o_rn + rn_len;
    o_im = o_mr + mr_len;
    o_en = o_im + im_len;
    o_lf = o_en + en_len;
    /* observed on 3.1 (tests/apps/tk51.c, TOOLHELP's GlobalEntryHandle):
       a module table is its own owner; TOOLHELP then calls it GT_MODULE */
    FarSetOwner(sel, sel);
    t = MK_FP(sel, 0);
    h = (ne_header FAR *)t;

    copy(t, ft, sizeof(ne_header));
    for (i = 0; i < nseg; i++)
        copy(t + o_seg + MDB_SEGSIZE * i, ft + fh->seg_off + sizeof(ne_segment) * i,
             sizeof(ne_segment));
    copy(t + o_rs, ft + fh->rsrc_off, rs_len);
    copy(t + o_rn, ft + fh->resname_off, rn_len);
    copy(t + o_im, ft + fh->impname_off, im_len);
    entry_convert(ft + fh->entry_off, ft + fh->entry_off + fh->entry_len, t, o_en);

    t[o_lf] = (u8)(LF_HEAD - 1 + plen + 1);
    t[o_lf + 1] = 1;                    /* fixed media (UNVERIFIED otherwise) */
    *(u16 FAR *)(t + o_lf + 2) = 0;
    *(u16 FAR *)(t + o_lf + 4) = date;
    *(u16 FAR *)(t + o_lf + 6) = time;
    copy(t + o_lf + LF_HEAD, (const u8 FAR *)path, plen + 1);

    MDB_W(sel, MDB_USAGE) = 0;
    MDB_W(sel, MDB_ENTRY) = en_len ? o_en : 0;
    MDB_W(sel, MDB_NEXT) = 0;
    MDB_W(sel, MDB_DGROUP) = fh->auto_data && fh->auto_data <= nseg
                             ? o_seg + MDB_SEGSIZE * (fh->auto_data - 1) : 0;
    MDB_W(sel, MDB_LOADFILE) = o_lf;
    h->flags = fh->flags | MDBF_LOADED;
    h->seg_off = o_seg;
    h->rsrc_off = o_rs;
    h->resname_off = o_rn;
    h->modref_off = o_mr;
    h->impname_off = o_im;
    MDB_W(sel, 0x38) = o_im;
    /* UNVERIFIED: what KERNEL's 3Ah holds (not 2Ah on Windows 3.1) */
    MDB_W(sel, 0x3A) = is_kernel ? 0 : o_im;
    return sel;
}
