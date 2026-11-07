/*
 * ne.h - Segmented ("New") Executable format definitions.
 *
 * Shared by the 16-bit target code (OpenWatcom) and host tools (gcc/clang).
 *
 * Source: the NE format as publicly documented in the Microsoft Windows 3.1
 * SDK ("Executable-File Header Format"), the MS-DOS Encyclopedia (Microsoft
 * Press, 1988) and the MT-DOS 4.00 "Dynamic Linking" product specification
 * (1984, MIT licence, microsoft/MS-DOS v4.0-ozzie). Rewritten from those
 * descriptions; field names are our own.
 */
#ifndef W16_NE_H
#define W16_NE_H

#include "w16/types.h"

#pragma pack(push, 1)

/* ---- MZ (old executable) header: only the fields the loader needs ---- */
#define MZ_MAGIC        0x5A4D          /* "MZ" */
#define MZ_LFANEW_OFF   0x3C            /* dword: file offset of NE header */
#define MZ_RELOC_OFF    0x18            /* word: >= 0x40 for new executables */

/* ---- NE header (64 bytes, at file offset given by MZ_LFANEW_OFF) ---- */
#define NE_MAGIC        0x454E          /* "NE" */

typedef struct ne_header {
    u16 magic;          /* 00 "NE" */
    u8  linker_ver;     /* 02 */
    u8  linker_rev;     /* 03 */
    u16 entry_off;      /* 04 entry table, relative to NE header */
    u16 entry_len;      /* 06 entry table length in bytes */
    u32 crc;            /* 08 */
    u16 flags;          /* 0C NEF_* */
    u16 auto_data;      /* 0E automatic data segment number (1-based), 0=none */
    u16 heap_size;      /* 10 initial local heap size */
    u16 stack_size;     /* 12 stack size (added to auto data segment) */
    u16 ip;             /* 14 entry point offset */
    u16 cs;             /* 16 entry point segment number (1-based) */
    u16 sp;             /* 18 initial SP */
    u16 ss;             /* 1A stack segment number (1-based) */
    u16 seg_count;      /* 1C entries in segment table */
    u16 modref_count;   /* 1E entries in module reference table */
    u16 nonres_len;     /* 20 non-resident name table size in bytes */
    u16 seg_off;        /* 22 segment table, relative to NE header */
    u16 rsrc_off;       /* 24 resource table, relative to NE header */
    u16 resname_off;    /* 26 resident name table, relative to NE header */
    u16 modref_off;     /* 28 module reference table, relative to NE header */
    u16 impname_off;    /* 2A imported names table, relative to NE header */
    u32 nonres_off;     /* 2C non-resident name table, FILE offset */
    u16 movable_count;  /* 30 movable entry points */
    u16 align_shift;    /* 32 logical sector shift (0 means 9) */
    u16 rsrc_count;     /* 34 resource segments */
    u8  target_os;      /* 36 NEOS_* */
    u8  flags2;         /* 37 NEF2_* */
    u16 fastload_off;   /* 38 fast-load area, in sectors */
    u16 fastload_len;   /* 3A fast-load area length, in sectors */
    u16 min_swap;       /* 3C minimum code swap area */
    u16 expver;         /* 3E expected Windows version, e.g. 0x030A */
} ne_header;

/* ne_header.flags */
#define NEF_SINGLEDATA  0x0001          /* one shared DGROUP (DLLs) */
#define NEF_MULTIDATA   0x0002          /* DGROUP per instance (apps) */
#define NEF_GLOBALINIT  0x0004
#define NEF_PMODEONLY   0x0008          /* protected mode only */
#define NEF_I86         0x0010          /* 8086 instructions */
#define NEF_I286        0x0020          /* 80286 instructions */
#define NEF_I386        0x0040          /* 80386 instructions */
#define NEF_I87         0x0080          /* 8087 instructions */
#define NEF_APPTYPE     0x0700          /* application type mask */
#define NEF_SELFLOAD    0x0800          /* self-loading application */
#define NEF_LINKERR     0x2000          /* errors at link time */
#define NEF_LIBRARY     0x8000          /* library (DLL) module */

/* ne_header.target_os */
#define NEOS_UNKNOWN    0
#define NEOS_OS2        1
#define NEOS_WINDOWS    2
#define NEOS_DOS4       3
#define NEOS_WIN386     4

/* ne_header.flags2 */
#define NEF2_LFN        0x01            /* long file names (OS/2) */
#define NEF2_PMODE      0x02            /* Windows 2.x app runs in prot. mode */
#define NEF2_PROPFONT   0x04            /* Windows 2.x app uses prop. fonts */
#define NEF2_GANGLOAD   0x08            /* fast-load (gang-load) area present */

/* ---- Segment table entry ---- */
typedef struct ne_segment {
    u16 sector;         /* file offset >> align_shift; 0 = no file data */
    u16 file_len;       /* bytes in file; 0 = 64K (when sector != 0) */
    u16 flags;          /* NESEG_* */
    u16 min_alloc;      /* minimum allocation; 0 = 64K */
} ne_segment;

#define NESEG_DATA      0x0001          /* data segment (else code) */
#define NESEG_ALLOCED   0x0002          /* loader: memory allocated */
#define NESEG_LOADED    0x0004          /* loader: segment loaded */
#define NESEG_MOVABLE   0x0010
#define NESEG_PURE      0x0020          /* shareable / pure */
#define NESEG_PRELOAD   0x0040
#define NESEG_RDONLY    0x0080          /* execute-only (code) / read-only (data) */
#define NESEG_RELOCS    0x0100          /* relocation records follow the data */
#define NESEG_DEBUG     0x0200
#define NESEG_DPL       0x0C00          /* privilege level (OS/2) */
#define NESEG_DISCARD   0x1000          /* discardable */
#define NESEG_PRIORITY  0xF000          /* discard priority */

/* ---- Relocation record (8 bytes; preceded by a u16 count) ---- */
typedef struct ne_reloc {
    u8  src;            /* NERS_*: what to patch */
    u8  flags;          /* NERF_*: what the target is */
    u16 offset;         /* offset in segment of first location (chain head) */
    u16 t1;             /* target word 1, meaning depends on flags */
    u16 t2;             /* target word 2 */
} ne_reloc;

/* ne_reloc.src: the kind of location being patched */
#define NERS_LOBYTE     0x00            /* low byte of offset */
#define NERS_SEGMENT    0x02            /* 16-bit segment/selector */
#define NERS_FARPTR     0x03            /* 16:16 far pointer */
#define NERS_OFFSET     0x05            /* 16-bit offset */
#define NERS_FARPTR48   0x0B            /* 16:32 pointer */
#define NERS_OFFSET32   0x0D            /* 32-bit offset */
#define NERS_MASK       0x0F

/* ne_reloc.flags: the target type */
#define NERF_INTERNAL   0x00            /* t1 low byte = segment (0xFF: movable,
                                           then t2 = entry ordinal), else
                                           t2 = offset */
#define NERF_IMPORTORD  0x01            /* t1 = module ref index, t2 = ordinal */
#define NERF_IMPORTNAME 0x02            /* t1 = module ref index,
                                           t2 = offset into imported names */
#define NERF_OSFIXUP    0x03            /* t1 = NEOSFIX_* type */
#define NERF_TYPEMASK   0x03
#define NERF_ADDITIVE   0x04            /* add to the location; no chain */

#define NE_MOVABLE_SEG  0xFF            /* t1 segment value for movable target */
#define NE_CHAIN_END    0xFFFF

/* OS fixups (floating-point emulation hooks), NERF_OSFIXUP t1 values */
#define NEOSFIX_FIARQQ  1
#define NEOSFIX_FJARQQ  2
#define NEOSFIX_FISRQQ  3
#define NEOSFIX_FJSRQQ  4
#define NEOSFIX_FICRQQ  5
#define NEOSFIX_FJCRQQ  6
#define NEOSFIX_FIERQQ  7
#define NEOSFIX_FIDRQQ  8
#define NEOSFIX_FIWRQQ  9

/* ---- Entry table ----
 * A sequence of bundles: u8 count (0 ends the table), u8 type.
 *   type 0x00: unused; skips `count` ordinals, no entries follow
 *   type 0xFF: movable segment; `count` ne_entry_movable follow
 *   type 0xFE: constant entries (value in offset field, no segment)
 *   type 1..0xFD: fixed segment number; `count` ne_entry_fixed follow
 */
#define NEB_UNUSED      0x00
#define NEB_CONSTANT    0xFE
#define NEB_MOVABLE     0xFF

typedef struct ne_entry_fixed {
    u8  flags;          /* NEE_* */
    u16 offset;
} ne_entry_fixed;

typedef struct ne_entry_movable {
    u8  flags;          /* NEE_* */
    u16 int3f;          /* 0x3FCD: INT 3Fh instruction */
    u8  seg;
    u16 offset;
} ne_entry_movable;

#define NEE_EXPORTED    0x01
#define NEE_SHAREDDATA  0x02            /* entry uses the single shared DGROUP */
#define NEE_PARAMWORDS  0xF8            /* OS/2 ring-transition parameter words */

/* ---- Resource table ----
 * u16 align_shift, then type blocks until a type id of 0, then
 * resource name strings (length-prefixed) until a zero length byte.
 */
typedef struct ne_rsrc_type {
    u16 type_id;        /* high bit set: integer type, else offset to name */
    u16 count;
    u32 reserved;       /* loader: resource handler */
} ne_rsrc_type;

typedef struct ne_rsrc_name {
    u16 offset;         /* data offset >> align_shift */
    u16 length;         /* data length >> align_shift */
    u16 flags;          /* NESEG_MOVABLE/PURE/PRELOAD, 0x1000 discardable */
    u16 id;             /* high bit set: integer id, else offset to name */
    u16 handle;         /* loader */
    u16 usage;          /* loader */
} ne_rsrc_name;

#define NE_RSRC_INTID   0x8000

/* Predefined resource types */
#define RT_CURSOR       1
#define RT_BITMAP       2
#define RT_ICON         3
#define RT_MENU         4
#define RT_DIALOG       5
#define RT_STRING       6
#define RT_FONTDIR      7
#define RT_FONT         8
#define RT_ACCELERATOR  9
#define RT_RCDATA       10
#define RT_GROUP_CURSOR 12
#define RT_GROUP_ICON   14
#define RT_NAMETABLE    15
#define RT_VERSION      16

#pragma pack(pop)

#endif /* W16_NE_H */
