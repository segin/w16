/*
 * nedump - dump a 16-bit NE (New Executable) file.
 *
 * Host tool. Used to check our understanding of the NE format, to inspect
 * test binaries, and to list which KERNEL/USER/GDI ordinals an app imports.
 *
 * usage: nedump [-r] file.exe     (-r also dumps relocation records)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w16/ne.h"

W16_STATIC_ASSERT(ne_header_size, sizeof(ne_header) == 64);
W16_STATIC_ASSERT(ne_segment_size, sizeof(ne_segment) == 8);
W16_STATIC_ASSERT(ne_reloc_size, sizeof(ne_reloc) == 8);
W16_STATIC_ASSERT(ne_entry_movable_size, sizeof(ne_entry_movable) == 6);

static u8 *img;
static size_t img_len;
static u32 ne_base;
static ne_header h;

static void die(const char *msg)
{
    fprintf(stderr, "nedump: %s\n", msg);
    exit(1);
}

static void need(u32 off, u32 len)
{
    if (off > img_len || len > img_len - off)
        die("truncated file");
}

static u8 rd8(u32 off) { need(off, 1); return img[off]; }
static u16 rd16(u32 off) { need(off, 2); return (u16)(img[off] | img[off + 1] << 8); }

static void get(void *dst, u32 off, u32 len)
{
    need(off, len);
    memcpy(dst, img + off, len);
}

/* Print a length-prefixed string at `off`; returns bytes consumed. */
static u32 pstr(u32 off)
{
    u8 n = rd8(off);
    need(off + 1, n);
    printf("%.*s", n, (const char *)img + off + 1);
    return 1u + n;
}

static void dump_names(const char *title, u32 off, u32 limit)
{
    printf("\n%s:\n", title);
    while (off < limit && rd8(off) != 0) {
        u8 n = rd8(off);
        printf("  ");
        pstr(off);
        printf("  @%u\n", rd16(off + 1 + n));
        off += 1u + n + 2;
    }
}

static void print_impname(u16 off)
{
    pstr(ne_base + h.impname_off + off);
}

static void print_modref(u16 index)
{
    if (index == 0 || index > h.modref_count) {
        printf("<bad module %u>", index);
        return;
    }
    print_impname(rd16(ne_base + h.modref_off + 2u * (index - 1)));
}

static const char *src_name(u8 s)
{
    switch (s & NERS_MASK) {
    case NERS_LOBYTE:   return "LOBYTE";
    case NERS_SEGMENT:  return "SEGMENT";
    case NERS_FARPTR:   return "FARPTR";
    case NERS_OFFSET:   return "OFFSET";
    case NERS_FARPTR48: return "FARPTR48";
    case NERS_OFFSET32: return "OFFSET32";
    default:            return "?";
    }
}

static void dump_relocs(int segno, const ne_segment *s, unsigned shift)
{
    u32 data_len, off;
    u16 n, i;

    if (!(s->flags & NESEG_RELOCS) || s->sector == 0)
        return;
    data_len = s->file_len ? s->file_len : 0x10000u;
    off = ((u32)s->sector << shift) + data_len;
    n = rd16(off);
    printf("    %u relocations:\n", n);
    off += 2;
    for (i = 0; i < n; i++, off += 8) {
        ne_reloc r;
        get(&r, off, sizeof r);
        printf("      %-8s @%04X%s ", src_name(r.src), r.offset,
               (r.flags & NERF_ADDITIVE) ? " (additive)" : "");
        switch (r.flags & NERF_TYPEMASK) {
        case NERF_INTERNAL:
            if ((r.t1 & 0xFF) == NE_MOVABLE_SEG)
                printf("-> movable entry #%u\n", r.t2);
            else
                printf("-> seg %u:%04X\n", r.t1 & 0xFF, r.t2);
            break;
        case NERF_IMPORTORD:
            printf("-> ");
            print_modref(r.t1);
            printf(".%u\n", r.t2);
            break;
        case NERF_IMPORTNAME:
            printf("-> ");
            print_modref(r.t1);
            printf(".");
            print_impname(r.t2);
            printf("\n");
            break;
        case NERF_OSFIXUP:
            printf("-> OS fixup %u (t2 %04X)\n", r.t1, r.t2);
            break;
        }
    }
    (void)segno;
}

static void dump_entries(void)
{
    u32 off = ne_base + h.entry_off, end = off + h.entry_len;
    unsigned ordinal = 1;

    printf("\nEntry table:\n");
    while (off < end) {
        u8 count = rd8(off), type;
        unsigned i;
        if (count == 0)
            break;
        type = rd8(off + 1);
        off += 2;
        if (type == NEB_UNUSED) {
            ordinal += count;
            continue;
        }
        for (i = 0; i < count; i++, ordinal++) {
            if (type == NEB_MOVABLE) {
                ne_entry_movable e;
                get(&e, off, sizeof e);
                off += sizeof e;
                printf("  #%-4u movable seg %u:%04X flags %02X\n",
                       ordinal, e.seg, e.offset, e.flags);
            } else {
                ne_entry_fixed e;
                get(&e, off, sizeof e);
                off += sizeof e;
                if (type == NEB_CONSTANT)
                    printf("  #%-4u constant %04X flags %02X\n",
                           ordinal, e.offset, e.flags);
                else
                    printf("  #%-4u fixed seg %u:%04X flags %02X\n",
                           ordinal, type, e.offset, e.flags);
            }
        }
    }
}

static void dump_resources(void)
{
    u32 base = ne_base + h.rsrc_off, off;
    unsigned shift;

    if (h.rsrc_off == h.resname_off)
        return;                         /* empty resource table */
    shift = rd16(base);
    off = base + 2;
    printf("\nResources (shift %u):\n", shift);
    for (;;) {
        ne_rsrc_type t;
        unsigned i;
        get(&t, off, sizeof t);
        off += sizeof t;
        if (t.type_id == 0)
            break;
        printf("  type ");
        if (t.type_id & NE_RSRC_INTID)
            printf("%u", t.type_id & 0x7FFF);
        else
            pstr(base + t.type_id);
        printf(" (%u):\n", t.count);
        for (i = 0; i < t.count; i++) {
            ne_rsrc_name r;
            get(&r, off, sizeof r);
            off += sizeof r;
            printf("    id ");
            if (r.id & NE_RSRC_INTID)
                printf("%u", r.id & 0x7FFF);
            else
                pstr(base + r.id);
            printf(" at %08X len %u flags %04X\n",
                   (u32)r.offset << shift, (u32)r.length << shift, r.flags);
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    int relocs = 0, i;
    unsigned shift;
    const char *path;

    if (argc == 3 && strcmp(argv[1], "-r") == 0) {
        relocs = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "usage: nedump [-r] file\n");
        return 2;
    }
    if (!(f = fopen(path, "rb")))
        die("cannot open file");
    fseek(f, 0, SEEK_END);
    img_len = (size_t)ftell(f);
    rewind(f);
    img = malloc(img_len ? img_len : 1);
    if (!img || fread(img, 1, img_len, f) != img_len)
        die("read error");
    fclose(f);

    if (rd16(0) != MZ_MAGIC)
        die("not an MZ executable");
    ne_base = rd16(MZ_LFANEW_OFF) | (u32)rd16(MZ_LFANEW_OFF + 2) << 16;
    get(&h, ne_base, sizeof h);
    if (h.magic != NE_MAGIC)
        die("no NE header");

    printf("NE header at %08X, linker %u.%u, target OS %u, expected Windows %u.%02u\n",
           ne_base, h.linker_ver, h.linker_rev, h.target_os,
           h.expver >> 8, h.expver & 0xFF);
    printf("flags %04X flags2 %02X  auto DS %u  heap %04X  stack %04X\n",
           h.flags, h.flags2, h.auto_data, h.heap_size, h.stack_size);
    printf("CS:IP %u:%04X  SS:SP %u:%04X  segments %u  modules %u  align %u\n",
           h.cs, h.ip, h.ss, h.sp, h.seg_count, h.modref_count, h.align_shift);

    shift = h.align_shift ? h.align_shift : 9;
    printf("\nSegments:\n");
    for (i = 0; i < h.seg_count; i++) {
        ne_segment s;
        get(&s, ne_base + h.seg_off + 8u * i, sizeof s);
        printf("  %2d %s file %08X len %5u alloc %5u flags %04X%s%s%s%s\n",
               i + 1, (s.flags & NESEG_DATA) ? "DATA" : "CODE",
               (u32)s.sector << shift, s.file_len, s.min_alloc, s.flags,
               (s.flags & NESEG_MOVABLE) ? " movable" : " fixed",
               (s.flags & NESEG_PRELOAD) ? " preload" : "",
               (s.flags & NESEG_DISCARD) ? " discardable" : "",
               (s.flags & NESEG_RELOCS) ? " relocs" : "");
        if (relocs)
            dump_relocs(i + 1, &s, shift);
    }

    printf("\nModule references:\n");
    for (i = 1; i <= h.modref_count; i++) {
        printf("  %d ", i);
        print_modref((u16)i);
        printf("\n");
    }

    dump_names("Resident names", ne_base + h.resname_off, ne_base + h.modref_off);
    dump_names("Non-resident names", h.nonres_off, h.nonres_off + h.nonres_len);
    dump_entries();
    dump_resources();
    return 0;
}
