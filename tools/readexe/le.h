/* readexe - Prints EXE info a la objdump/dumpbin/efd
 * le.h - Structure information for 32-bit Linear Executable format
 * Copyright © 2019 Kirn Gill II <segin2005@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Sources for the layouts below:
 *  - IBM, "LX - Linear eXecutable Module Format Description" (1992), the
 *    published description of the LX format, of which LE is the older
 *    variant used by Windows 3.x/95 virtual device drivers (VxDs). The
 *    header, object table, entry table, name tables and fixup records are
 *    shared between LE and LX; LE differs in the object page table entry
 *    (4 bytes: 24-bit page number + flags) and in using the "last page
 *    size" field where LX has the page offset shift.
 *  - Open Watcom Linker Guide, "The LE/LX executable file format" options
 *    (Open Watcom documents and produces LE executables for DOS extenders
 *    and VxDs).
 *  - Microsoft Windows 3.1 DDK, Virtual Device Adaptation Guide: the Device
 *    Descriptor Block (VxD_Desc_Block) and the INT 20h dynamic link
 *    convention (VMMcall/VxDcall: INT 20h followed by a dword whose high
 *    word is the device ID and low word the service number; bit 15 of the
 *    service number set means VxDjmp).
 *  - The Windows-specific fields at 0xB8..0xC3 (VxD version resource, device
 *    ID, DDK version) were checked against the Windows 3.1 VxDs: e.g. the
 *    INT13 module in WIN386.EXE has device ID 0x0020 and DDK version 0x030A
 *    there, matching its DDB.
 */

#ifndef LE_H
#define LE_H

#include <stdint.h>

struct exe_le_header {
    char        magic[2]; /* "LE" or "LX" */             /* 0x00 */
    uint8_t     byteOrder;                               /* 0x02 */
    uint8_t     wordOrder;                               /* 0x03 */
    uint32_t    level;                                   /* 0x04 */
    uint16_t    cpuType;                                 /* 0x08 */
    uint16_t    osType;                                  /* 0x0A */
    uint32_t    version;                                 /* 0x0C */
    union {                                              /* 0x10 */
        uint32_t flags;
        struct {
            uint32_t    _reserved1       : 2;
            uint32_t    libInit          : 1;   /* per-process library initialization */
            uint32_t    _reserved2       : 1;
            uint32_t    noInternalFixups : 1;
            uint32_t    noExternalFixups : 1;
            uint32_t    _reserved3       : 2;
            uint32_t    pmIncompat       : 1;
            uint32_t    pmCompat         : 1;   /* both set: uses PM */
            uint32_t    _reserved4       : 3;
            uint32_t    moduleNotLoaded  : 1;   /* errors at link time */
            uint32_t    _reserved5       : 1;
            uint32_t    moduleType       : 3;   /* see exe_le_moduletypes */
            uint32_t    _reserved6       : 12;
            uint32_t    libTerm          : 1;   /* per-process library termination */
            uint32_t    _reserved7       : 1;
        };
    };
    uint32_t    pages;                                   /* 0x14 */
    uint32_t    startingObject;                          /* 0x18 */
    uint32_t    entryPoint;                              /* 0x1C */
    uint32_t    stackObject;                             /* 0x20 */
    uint32_t    stackPointer;                            /* 0x24 */
    uint32_t    pageSize;                                /* 0x28 */
    union {                                              /* 0x2C */
        uint32_t    lastPage;   /* LE: bytes on the last page */
        uint32_t    pageShift;  /* LX */
    };
    uint32_t    fixupSize;                               /* 0x30 */
    uint32_t    fixupChecksum;                           /* 0x34 */
    uint32_t    loaderSize;                              /* 0x38 */
    uint32_t    loaderChecksum;                          /* 0x3C */
    uint32_t    objectTableOffset;                       /* 0x40, from LE header */
    uint32_t    objectCount;                             /* 0x44 */
    uint32_t    objectMapOffset;                         /* 0x48, object page table */
    uint32_t    idataMapOffset;                          /* 0x4C, iterated pages */
    uint32_t    resourceOffset;                          /* 0x50 */
    uint32_t    resourceCount;                           /* 0x54 */
    uint32_t    resourceNameOffset;                      /* 0x58, resident name table */
    uint32_t    entryTableOffset;                        /* 0x5C */
    uint32_t    moduleDirectiveOffset;                   /* 0x60 */
    uint32_t    moduleDirectiveCount;                    /* 0x64 */
    uint32_t    fixupPageTableOffset;                    /* 0x68 */
    uint32_t    fixupRecordTableOffset;                  /* 0x6C */
    uint32_t    importModuleNameTableOffset;             /* 0x70 */
    uint32_t    importModuleNameTableCount;              /* 0x74 */
    uint32_t    importProcNameTableOffset;               /* 0x78 */
    uint32_t    pageChecksumTableOffset;                 /* 0x7C */
    uint32_t    dataPagesOffset;                         /* 0x80, from start of file */
    uint32_t    preloadPageCount;                        /* 0x84 */
    uint32_t    nonresidentNameTableOffset;              /* 0x88, from start of file */
    uint32_t    nonresidentNameTableSize;                /* 0x8C */
    uint32_t    nonresidentNameTableChecksum;            /* 0x90 */
    uint32_t    autodataObject;                          /* 0x94 */
    uint32_t    debugSymbolsfOffset;                     /* 0x98 */
    uint32_t    debugSymbolsfSize;                       /* 0x9C */
    uint32_t    instancePagePreloadCount;                /* 0xA0 */
    uint32_t    instancePageDemandLoadCount;             /* 0xA4 */
    uint32_t    heapSize;                                /* 0xA8 */
    uint32_t    stackSize;                               /* 0xAC */
    union {                                              /* 0xB0 */
        uint8_t lxReserved[20];
        struct  {
            uint8_t leReserved[8];
            uint32_t windowsResourceOffset;              /* 0xB8, VxD version resource */
            uint32_t windowsResourceSize;                /* 0xBC */
            uint16_t windowsDeviceID;                    /* 0xC0 */
            uint16_t windowsDDKVersion;                  /* 0xC2 */
        };
    };
};                                                       /* 0xC4 bytes */

enum exe_le_header_cputypes {
    CPU_286 = 1,
    CPU_386,
    CPU_486,
    CPU_586
};

enum exe_le_ostypes {
    LE_OS_OS2 = 1,
    LE_OS_WINDOWS,
    LE_OS_DOS4,
    LE_OS_WIN386
};

enum exe_le_moduletypes {   /* flags bits 15-17 */
    LE_MOD_PROGRAM = 0,
    LE_MOD_LIBRARY = 1,
    LE_MOD_PROTLIBRARY = 3,
    LE_MOD_PDD = 4,
    LE_MOD_VDD = 5
};

/* Object table entry */
struct exe_le_object {
    uint32_t    virtualSize;
    uint32_t    relocBase;
    uint32_t    flags;
    uint32_t    pageMapIndex;   /* 1-based index into the object page table */
    uint32_t    pageMapCount;
    uint32_t    _reserved;
};

enum exe_le_objflags {
    LEOBJ_READ       = 0x0001,
    LEOBJ_WRITE      = 0x0002,
    LEOBJ_EXEC       = 0x0004,
    LEOBJ_RESOURCE   = 0x0008,
    LEOBJ_DISCARD    = 0x0010,
    LEOBJ_SHARED     = 0x0020,
    LEOBJ_PRELOAD    = 0x0040,
    LEOBJ_INVALID    = 0x0080,
    LEOBJ_ZEROFILL   = 0x0100,  /* with 0x0200: resident and contiguous */
    LEOBJ_RESIDENT   = 0x0200,
    LEOBJ_LONGLOCK   = 0x0400,  /* resident and long-lockable */
    LEOBJ_ALIAS1616  = 0x1000,
    LEOBJ_BIG        = 0x2000,  /* 32-bit default (USE32) */
    LEOBJ_CONFORMING = 0x4000,
    LEOBJ_IOPL       = 0x8000
};

/* LE object page table entry; the page number is stored high byte first */
struct exe_le_objpage {
    uint8_t     pageHigh;
    uint8_t     pageMid;
    uint8_t     pageLow;
    uint8_t     flags;          /* 0 legal physical page, 1 iterated, 2 invalid, 3 zero-filled */
};

/* Entry table bundle types */
enum exe_le_bundletypes {
    LE_BUNDLE_UNUSED = 0,
    LE_BUNDLE_16BIT = 1,
    LE_BUNDLE_286GATE = 2,
    LE_BUNDLE_32BIT = 3,
    LE_BUNDLE_FORWARDER = 4
};

/* Fixup record: source byte */
#define LE_FIXSRC_TYPEMASK  0x0F
#define LE_FIXSRC_BYTE      0x00
#define LE_FIXSRC_SEL16     0x02
#define LE_FIXSRC_PTR1616   0x03
#define LE_FIXSRC_OFF16     0x05
#define LE_FIXSRC_PTR1632   0x06
#define LE_FIXSRC_OFF32     0x07
#define LE_FIXSRC_REL32     0x08
#define LE_FIXSRC_ALIAS     0x10
#define LE_FIXSRC_LIST      0x20

/* Fixup record: target flags byte */
#define LE_FIXTGT_TYPEMASK  0x03
#define LE_FIXTGT_INTERNAL  0x00
#define LE_FIXTGT_IMPORD    0x01
#define LE_FIXTGT_IMPNAME   0x02
#define LE_FIXTGT_ENTRY     0x03
#define LE_FIXTGT_ADDITIVE  0x04
#define LE_FIXTGT_OFF32     0x10
#define LE_FIXTGT_ADD32     0x20
#define LE_FIXTGT_OBJ16     0x40
#define LE_FIXTGT_ORD8      0x80

/* Windows 3.x VxD Device Descriptor Block (VxD_Desc_Block), the structure a
 * VxD exports as <name>_DDB. Source: Windows 3.1 DDK, Virtual Device
 * Adaptation Guide; layout confirmed against the Windows 3.1 VxDs (the
 * name field holds the blank-padded device name, the SDK version 030Ah). */
struct vxd_ddb {
    uint32_t    next;               /* 0x00 link, filled by the VMM */
    uint16_t    sdkVersion;         /* 0x04 */
    uint16_t    deviceID;           /* 0x06 Undefined_Device_ID (0) if none */
    uint8_t     versionMajor;       /* 0x08 */
    uint8_t     versionMinor;       /* 0x09 */
    uint16_t    flags;              /* 0x0A */
    char        name[8];            /* 0x0C blank padded */
    uint32_t    initOrder;          /* 0x14 */
    uint32_t    controlProc;        /* 0x18 */
    uint32_t    v86APIProc;         /* 0x1C */
    uint32_t    pmAPIProc;          /* 0x20 */
    uint32_t    v86APICSIP;         /* 0x24 filled by the VMM */
    uint32_t    pmAPICSIP;          /* 0x28 filled by the VMM */
    uint32_t    referenceData;      /* 0x2C from the real-mode init */
    uint32_t    serviceTablePtr;    /* 0x30 */
    uint32_t    serviceTableSize;   /* 0x34 number of services */
};                                  /* 0x38 bytes */

#define VXD_INT20_OPCODE    0x20CD  /* bytes CD 20 */
#define VXD_INT20_JMPFLAG   0x8000  /* VxDjmp rather than VxDcall */

#endif /* LE_H */
