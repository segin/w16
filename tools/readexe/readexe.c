/* readexe - Prints EXE info a la objdump/dumpbin/efd 
 * 
 * Copyright © 2019-2024 Kirn Gill II <segin2005@gmail.com>
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <strings.h>
#include <fcntl.h>
#include <err.h> /* -I. or such for platforms without err.h */

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define VERSION "0.1.4"
#endif

/* Big assumptions on little-endianiness here */

#include "mz.h"
#include "ne.h"
#include "le.h"
#include "w3.h"


struct THIS {
    FILE *fd;                               /* standard I/O library file descriptor */
    char *fname;                            /* File name of the executable we're inspecting, passed as the sole argument. */
    struct exe_mz_header *mz;               /* DOS (MZ) header */
    struct exe_mz_new_header *mzx;          /* eXtended DOS (MZ) header */
    struct exe_ne_header *ne;               /* New Executable (NE) header */
    struct exe_ne_segment *nesegs;          /* NE segments */
    int ne_importCount;                     /* number of entries in NE imported names table  */
    int ne_moduleCount;                     /* number of module references in modules table */
    struct exe_ne_module *nemods;           /* NE imported modules */
    struct exe_le_header *le;               /* Linear Executable (LE/LX) header */
    struct exe_w3_header *w3;               /* W3 header */
    int wx_modcount;                        /* W3/W4 LE module count */
    uint32_t le_base;                       /* file offset of the LE header */
    uint32_t le_nrbase;                     /* added to the LE non-resident name table offset */
    uint8_t *le_ldrbuf;                     /* LE header, loader and fixup sections */
    uint32_t le_ldrlen;
    struct exe_le_object *le_objs;          /* LE object table */
    uint8_t **le_objimg;                    /* each LE object's pages, loaded */
    struct { uint32_t obj, off, tobj, toff; } *le_fix;  /* internal 32-bit fixups applied */
    uint32_t le_nfix;
    uint16_t le_ddb_ordinal;                /* entry of the VxD's *_DDB export */
    char le_ddb_name[256];
    int verbose;                            /* -v: page map and every fixup record */
    int services;                           /* -s: VxD service calls (INT 20h) */
    char *w3module;                         /* -m: only this W3 module */
};

void read_ne_exe(struct THIS *this);
void read_ne_segments(struct THIS *this);
void read_ne_modules_import(struct THIS *this);
void read_next_header(struct THIS *this);
void read_ne_header(struct THIS *this);
void get_ne_modules_count(struct THIS *this);
int read_at(struct THIS *this, uint32_t off, void *buf, uint32_t len);
uint8_t *le_ldr(struct THIS *this, uint32_t off, uint32_t len);
void read_le_header(struct THIS *this);
uint32_t le_page_number(struct THIS *this, uint32_t idx, uint8_t *flags);
void load_le_objects(struct THIS *this);
void read_le_objects(struct THIS *this);
void read_le_name_table(struct THIS *this, uint8_t *p, uint32_t len, const char *title);
void read_le_names(struct THIS *this);
int walk_le_entries(struct THIS *this, uint16_t want, uint16_t *obj_out, uint32_t *off_out);
void read_le_entries(struct THIS *this);
char *le_import_module_name(struct THIS *this, uint16_t modord, char *buf, size_t size);
void read_le_imports(struct THIS *this);
void read_le_fixups(struct THIS *this);
void print_le_ptr(struct THIS *this, uint32_t obj, uint32_t at, const char *label);
void read_le_ddb(struct THIS *this);
void read_le_int20(struct THIS *this);
void free_le(struct THIS *this);
void read_le_at(struct THIS *this, uint32_t base, uint32_t nrbase);
void read_le_exe(struct THIS *this);
void read_w3_exe(struct THIS *this);
void read_mz_reloc(struct THIS *this);

struct THIS *init_this(void);
void destroy_this(struct THIS *this);
void display_help(struct THIS *this);
int main(int argc, char *argv[]);

void read_ne_exe(struct THIS *this) {
    if ((this->ne = (struct exe_ne_header *) malloc(sizeof(struct exe_ne_header)))) { 
        fseek(this->fd, this->mzx->nextHeader, SEEK_SET);
        if (fread(this->ne, 1, sizeof(struct exe_ne_header), this->fd)!= sizeof(struct exe_ne_header)) {
            if (ferror(this->fd)) warn("Cannot read %s", this->fname);
            if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        } else {
            read_ne_header(this);
            read_ne_modules_import(this);
            read_ne_segments(this);
        }
    } else err(1, "Cannot allocate memory");
    return;
}

void read_ne_segments(struct THIS *this) {
    uint32_t seg, segsz, minalloc;

    printf("\n\n");
    if ((this->nesegs = (struct exe_ne_segment *) malloc(sizeof(struct exe_ne_segment) * this->ne->segmentCount))) {
        fseek(this->fd, this->mzx->nextHeader + this->ne->segmentTableOffset, SEEK_SET);

        if (fread(this->nesegs, 1, (sizeof(struct exe_ne_segment) * this->ne->segmentCount), this->fd) != (sizeof(struct exe_ne_segment) * this->ne->segmentCount)) {
            if (ferror(this->fd)) warn("Cannot read %s", this->fname);
            if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        } else {
            for(int i = 0; i < this->ne->segmentCount; i++) {
                printf("Segment %d: %s%s%s%s%s%s%s%s\n", i, 
                    this->nesegs[i].segType ? "DATA " : "CODE ",
                    this->nesegs[i].allocated ? "ALLOCATED " : "",
                    this->nesegs[i].loaded ? "LOADED " : "",
                    this->nesegs[i].relocatable ? "MOVEABLE " : "",
                    this->nesegs[i].shared ? "PURE " : "IMPURE ",
                    this->nesegs[i].preload ? "PRELOAD " : "",
                    this->nesegs[i].relocations ? "RELOCINFO " : "",
                    this->nesegs[i].discardable ? "DISCARD " : ""
                );
                printf("  Offset      (file)   Length   (dec)     Mem \n");
                /* While the underlying structures contain 16-bit values, 32-bit values are used in RAM to account for the case of a value of zero, equal to 0x10000. */
                seg = this->nesegs[i].segmentOffset; 
                segsz = (uint32_t) this->nesegs[i].segmentSize ? this->nesegs[i].segmentSize : 0x10000;
                minalloc = (uint32_t) this->nesegs[i].minimumAllocation ? this->nesegs[i].minimumAllocation : 0x10000;
                printf("  0x%04"PRIx32"  0x%08"PRIx32"   0x%04"PRIx32"   %5"PRIu32"  0x%04"PRIx32"\n\n", 
                    seg, 
                    seg << this->ne->offsetShiftCount, 
                    segsz, 
                    segsz,
                    minalloc);
            }
        }
    } else err(1, "Cannot allocate memory");
}

void read_ne_relocs(struct THIS *this) {
    uint16_t segmentRelocationEntries, i, j;
    off_t oldoffset = ftell(this->fd);
    struct exe_ne_reloc *relocentry = malloc(sizeof(struct exe_ne_reloc));;
    
    if(!relocentry) err(1, "Cannot allocate memory");
    for(i=0;i<this->ne->segmentCount;i++) {
        fseek(this->fd, this->nesegs[i].segmentOffset << this->ne->offsetShiftCount, SEEK_SET);
        printf("Relocation table for segment %d:\n", i);
        fread(&segmentRelocationEntries, 2, 1, this->fd);
        if (segmentRelocationEntries) {
            for(j=0;j<segmentRelocationEntries;j++){
                fread(relocentry, 1, sizeof(struct exe_ne_reloc), this->fd);
                printf(" [%3d] ", j);
                switch(relocentry->relocationType){
                    case RELTYPE_INTREF:
                        printf(" [%3d] ");
                        break;
                };    
            }
        } else 
            printf("No relocations for segment.\n");
        printf("\n");
    }
    fseek(this->fd, oldoffset, SEEK_SET);
    free(relocentry);
}

char *get_ne_import_module_name(struct THIS *this, int module) {
    int i = module * 2;
    off_t oldoffset = ftell(this->fd), soff;
    uint16_t loff;
    uint8_t size;
    char *name;

    fseek(this->fd, (this->ne->modulesTableOffset + this->mzx->nextHeader + i), SEEK_SET);
    if (fread(&loff, 1, sizeof(uint16_t), this->fd)!= sizeof(uint16_t)) {
        if (ferror(this->fd)) warn("Cannot read %s", this->fname);
        if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
    } else {
        soff = (this->ne->importedNamesTableOffset + this->mzx->nextHeader) + loff;
        fseek(this->fd, soff, SEEK_SET);
        size = fgetc(this->fd);
        if (((int8_t) size != -1) && !ferror(this->fd)) {
            if (name = malloc(size + 1)) { 
                memset(name, 0, size + 1);
                if (fread(name, 1, size, this->fd)!= size) {
                    if (ferror(this->fd)) warn("Cannot read %s", this->fname);
                    if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
                } else {
                    fseek(this->fd, oldoffset, SEEK_SET);
                    return name;
                }
            } else err(1, "Cannot allocate memory");
        } else warnx("Unexpected end of file: %s", this->fname);
    }
    /* we should never return NULL or even get here; either we have a valid malloc()'d string pointer or execution cannot continue.*/
    fprintf(stderr, "Reached end of get_ne_import_module_name()!");
    abort();
    return NULL; 
}

void read_ne_modules_import(struct THIS *this) {
    int i;
    char *name;

    printf(
        "\n\n"
        "Imported modules:\n"
        "-----------------\n"
    );
    for(i=0;i<this->ne->modRefCount;i++) {
        if(name = get_ne_import_module_name(this, i)) {
            printf("  [%2d]: %s\n", i+1, name);
            free(name);
        } else errx(1, "Cannot read file: ", this->fname);
    }
}

void read_ne_header(struct THIS *this) {
    char *msg;

    printf("New Executable with magic:\t%c%c\n", this->ne->magic[0], this->ne->magic[1]);
    printf("Linker version:\t\t\t%"PRIu8".%"PRIu8"\n", this->ne->linkerMajor, this->ne->linkerMinor);
    printf("Entry table offset:\t\t0x%04" PRIx16 " (File offset 0x%08" PRIx32 ")\n", this->ne->entryTableOffset, ((uint32_t) this->ne->entryTableOffset + this->mzx->nextHeader));
    printf("Entry table size:\t\t0x%04"PRIx16" (%"PRIu16" bytes)\n", this->ne->entryTableSize, this->ne->entryTableSize);
    printf("Header CRC:\t\t\t0x%08"PRIx32"\n", this->ne->fileCrc);
    printf(".EXE Flags:\t\t\t0x%02"PRIx8"\n", this->ne->progFlags);
    switch(this->ne->dataType) {
        case DATA_NONE:
            msg = "Not indicated";
            break;
        case DATA_SINGLEDATA:
            msg = "SINGLEDATA";
            break;
        case DATA_MULTIPLEDATA:
            msg = "MULTIPLEDATA";
            break;
        case DATA_AUTODATA:
            msg = "AUTODATA";
            break;
    }
    printf(" - Data Segment Model:\t\t%s\n", msg);
    printf(" - Global initialization:\t%s\n", this->ne->globalInit ? "true" : "false");
    printf(" - Protected Mode only:\t\t%s\n", this->ne->pmModeOnly ? "true" : "false");
    printf(" - 8086 opcodes used:\t\t%s\n", this->ne->ops8086 ? "true" : "false");
    printf(" - 80286 opcodes used:\t\t%s\n", this->ne->ops80286 ? "true" : "false");
    printf(" - 80386 opcodes used:\t\t%s\n", this->ne->ops80386 ? "true" : "false");
    printf(" - FPU/80x87 opcodes used:\t%s\n", this->ne->ops80x87 ? "true" : "false");
    printf("Application flags:\t\t0x%02"PRIx8"\n", this->ne->appFlags);
    switch(this->ne->appType) {
        case APP_NONE:
            msg = "Not indicated";
            break;
        case APP_FULLSCREEN:
            msg = "OS/2 Fullscreen CUI application";
            break;
        case APP_COMPATIBLE:
            msg = "OS/2 Presentation Manager compatible CUI application";
            break;
        case APP_WINPM:
            msg = "Windows or Presentation Manager GUI application";
            break;
    }
    printf(" - Application type:\t\t%s\n", msg);
    printf(" - OS/2 Family executable:\t%s\n", this->ne->os2FamExec ? "true" : "false");
    printf(" - Is executable:\t\t%s\n", this->ne->executable ? "true" : "false");
    printf(" - Generated with link errors:\t%s\n", this->ne->linkErrors ? "true" : "false");
    printf(" - Is library (DLL or driver):\t%s\n", this->ne->libraryBit ? "true" : "false");
    printf("AUTODATA segment address:\t0x%04"PRIx16"\n", this->ne->autoDataSegAddr);
    printf("Initial heap size:\t\t0x%04"PRIx16"\n", this->ne->initHeapSize);
    printf("Initial stack size:\t\t0x%04"PRIx16"\n", this->ne->initStackSize);
    printf("Initial CS:IP (entrypoint):\t%04"PRIx16":%04"PRIx16"\n", (this->ne->entryPoint >> 16), (this->ne->entryPoint & 0xFFFF));
    printf("Initial SS:SP (stack):\t\t%04"PRIx16":%04"PRIx16"\n", (this->ne->initStackPtr >> 16), (this->ne->initStackPtr & 0xFFFF));
    printf("Segment count:\t\t\t0x%04"PRIx16" (%"PRIu16")\n", this->ne->segmentCount, this->ne->segmentCount);
    printf("Module reference count:\t\t%04"PRIx16" (%"PRIu16")\n", this->ne->modRefCount, this->ne->modRefCount);
    printf("Non-resident name table size:\t0x%04"PRIx16" (%"PRIu16" bytes)\n", this->ne->nonResidentTableSize, this->ne->nonResidentTableSize);
    printf("Offset of segment table:\t0x%04"PRIx16" (File offset 0x%08"PRIx32")\n", this->ne->segmentTableOffset, (this->ne->segmentTableOffset + this->mzx->nextHeader));
    printf("Offset of resource table:\t0x%04"PRIx16" (File offset 0x%08"PRIx32")\n", this->ne->resourceTableOffset, (this->ne->resourceTableOffset + this->mzx->nextHeader));
    printf("Offset of resident name table:\t0x%04"PRIx16" (File offset 0x%08"PRIx32")\n", this->ne->residentNamesTableOffset, (this->ne->residentNamesTableOffset + this->mzx->nextHeader));
    printf("Offset of module table:\t\t0x%04"PRIx16" (File offset 0x%08"PRIx32")\n", this->ne->modulesTableOffset, (this->ne->modulesTableOffset + this->mzx->nextHeader));
    printf("Offset of imported names table:\t0x%04"PRIx16" (File offset 0x%08"PRIx32")\n", this->ne->importedNamesTableOffset, (this->ne->importedNamesTableOffset + this->mzx->nextHeader));
    printf("Non-resident names table:\t0x%08"PRIx32" (File offset)\n", this->ne->nonResidentTableOffset);
    printf("Movable entry points:\t\t0x%08"PRIx32" (%"PRIu32")\n", this->ne->movableEntryPoints, this->ne->movableEntryPoints);
    printf("Offset shift count:\t\t0x%04"PRIx16" (%"PRIu16")\n", this->ne->offsetShiftCount, this->ne->offsetShiftCount);
    printf("Resource table size:\t\t0x%04"PRIx16" (%"PRIu16")\n", this->ne->offsetShiftCount, this->ne->offsetShiftCount);
    switch(this->ne->targetOS) {
        case OS_UNKNOWN:
            msg = "Unknown";
            break;
        case OS_OS2:
            msg = "OS/2";
            break;
        case OS_WINDOWS:
            msg = "Windows";
            break;
        case OS_MSDOS:
            msg = "MT MS-DOS 4.0";
            break;
        case OS_WIN386:
            msg = "Windows/386";
            break;
        case OS_BOSS:
            msg = "Borland Operating System Services or HX Extender DPMI-16";
            break;
        case OS_HX:
            msg = "HX Extender DPMI-32";
            break;
        case OS_PHARLAP286OS2:
            msg = "Phar Lap 286|DOS-Extender (OS/2)";
            break;
        case OS_PHARLAP286WIN:
            msg = "Phar Lap 286|DOS-Extender (Windows)";
            break;
    }
    printf("Target operating system:\t%s (0x%02"PRIx8")\n", msg, this->ne->targetOS);
    printf("Executable flags:\t\t%s%s%s%s\n", 
        this->ne->os2LFN ? "LONGFILENAME " : "",
        this->ne->os2PMode ? "PROTECTEDMODE " : "",
        this->ne->os2Fonts ? "PROPORTIONALFONTS " : "",
        this->ne->fastLoad ? "GANGLOADAREA " : ""); 
    if(this->ne->fastLoad) { 
        printf("GangLoad/FastLoad area offset:\t0x%04"PRIx16"\n", this->ne->returnThunksOffset);
        printf("GangLoad/FastLoad area size:\t0x%04"PRIx16"\n", this->ne->segmentReferenceOffset);        
    }
    printf("Windows version:\t\t%"PRIu8".%"PRIu8" (0x%04"PRIx16")\n", this->ne->windowsVersionMajor, this->ne->windowsVersionMinor, this->ne->windowsVersion);
}

/* Read len bytes at file offset off. Returns 1 on success. */
int read_at(struct THIS *this, uint32_t off, void *buf, uint32_t len) {
    if (fseek(this->fd, off, SEEK_SET)) {
        warn("Cannot seek in %s", this->fname);
        return 0;
    }
    if (fread(buf, 1, len, this->fd) != len) {
        if (ferror(this->fd)) warn("Cannot read %s", this->fname);
        if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        return 0;
    }
    return 1;
}

/* Bytes of the loader/fixup sections, which are addressed from the LE header. */
uint8_t *le_ldr(struct THIS *this, uint32_t off, uint32_t len) {
    if (off > this->le_ldrlen || len > this->le_ldrlen - off) return NULL;
    return this->le_ldrbuf + off;
}

void read_le_header(struct THIS *this) {
    struct exe_le_header *le = this->le;
    char *msg;

    printf("Linear Executable with magic:\t%c%c\n", le->magic[0], le->magic[1]);
    printf("Byte order, word order:\t\t%s, %s\n", le->byteOrder ? "big endian" : "little endian", le->wordOrder ? "big endian" : "little endian");
    printf("Format level:\t\t\t0x%08"PRIx32"\n", le->level);
    switch(le->cpuType) {
        case CPU_286: msg = "80286"; break;
        case CPU_386: msg = "80386"; break;
        case CPU_486: msg = "80486"; break;
        case CPU_586: msg = "80586"; break;
        default:      msg = "Unknown"; break;
    }
    printf("CPU type:\t\t\t%s (0x%04"PRIx16")\n", msg, le->cpuType);
    switch(le->osType) {
        case LE_OS_OS2:     msg = "OS/2"; break;
        case LE_OS_WINDOWS: msg = "Windows"; break;
        case LE_OS_DOS4:    msg = "DOS 4.x"; break;
        case LE_OS_WIN386:  msg = "Windows 386 (VxD)"; break;
        default:            msg = "Unknown"; break;
    }
    printf("Target operating system:\t%s (0x%04"PRIx16")\n", msg, le->osType);
    printf("Module version:\t\t\t0x%08"PRIx32"\n", le->version);
    switch(le->moduleType) {
        case LE_MOD_PROGRAM:     msg = "Program"; break;
        case LE_MOD_LIBRARY:     msg = "Library"; break;
        case LE_MOD_PROTLIBRARY: msg = "Protected memory library"; break;
        case LE_MOD_PDD:         msg = "Physical device driver"; break;
        case LE_MOD_VDD:         msg = "Virtual device driver"; break;
        default:                 msg = "Unknown"; break;
    }
    printf("Module flags:\t\t\t0x%08"PRIx32"\n", le->flags);
    printf(" - Module type:\t\t\t%s\n", msg);
    printf(" - Per-process init/term:\t%s/%s\n", le->libInit ? "true" : "false", le->libTerm ? "true" : "false");
    printf(" - Internal fixups removed:\t%s\n", le->noInternalFixups ? "true" : "false");
    printf(" - External fixups removed:\t%s\n", le->noExternalFixups ? "true" : "false");
    printf(" - Not loadable (link errors):\t%s\n", le->moduleNotLoaded ? "true" : "false");
    printf("Memory pages:\t\t\t%"PRIu32"\n", le->pages);
    printf("Initial EIP (entrypoint):\t%"PRIu32":%08"PRIx32"\n", le->startingObject, le->entryPoint);
    printf("Initial ESP (stack):\t\t%"PRIu32":%08"PRIx32"\n", le->stackObject, le->stackPointer);
    printf("Page size:\t\t\t0x%08"PRIx32"\n", le->pageSize);
    if (le->magic[1] == 'X')
        printf("Page offset shift:\t\t%"PRIu32"\n", le->pageShift);
    else
        printf("Bytes on last page:\t\t0x%08"PRIx32"\n", le->lastPage);
    printf("Fixup section size:\t\t0x%08"PRIx32"\n", le->fixupSize);
    printf("Loader section size:\t\t0x%08"PRIx32"\n", le->loaderSize);
    printf("Object table:\t\t\t0x%08"PRIx32" (%"PRIu32" objects)\n", le->objectTableOffset, le->objectCount);
    printf("Object page table:\t\t0x%08"PRIx32"\n", le->objectMapOffset);
    printf("Iterated pages:\t\t\t0x%08"PRIx32"\n", le->idataMapOffset);
    printf("Resource table:\t\t\t0x%08"PRIx32" (%"PRIu32" entries)\n", le->resourceOffset, le->resourceCount);
    printf("Resident name table:\t\t0x%08"PRIx32"\n", le->resourceNameOffset);
    printf("Entry table:\t\t\t0x%08"PRIx32"\n", le->entryTableOffset);
    printf("Module directives:\t\t0x%08"PRIx32" (%"PRIu32" entries)\n", le->moduleDirectiveOffset, le->moduleDirectiveCount);
    printf("Fixup page table:\t\t0x%08"PRIx32"\n", le->fixupPageTableOffset);
    printf("Fixup record table:\t\t0x%08"PRIx32"\n", le->fixupRecordTableOffset);
    printf("Import module table:\t\t0x%08"PRIx32" (%"PRIu32" modules)\n", le->importModuleNameTableOffset, le->importModuleNameTableCount);
    printf("Import procedure table:\t\t0x%08"PRIx32"\n", le->importProcNameTableOffset);
    printf("Data pages:\t\t\t0x%08"PRIx32" (File offset)\n", le->dataPagesOffset);
    printf("Preload pages:\t\t\t%"PRIu32"\n", le->preloadPageCount);
    printf("Non-resident name table:\t0x%08"PRIx32" (File offset 0x%08"PRIx32", %"PRIu32" bytes)\n", le->nonresidentNameTableOffset, le->nonresidentNameTableOffset + this->le_nrbase, le->nonresidentNameTableSize);
    printf("Automatic data object:\t\t%"PRIu32"\n", le->autodataObject);
    printf("Debug information:\t\t0x%08"PRIx32" (%"PRIu32" bytes)\n", le->debugSymbolsfOffset, le->debugSymbolsfSize);
    printf("Heap size, stack size:\t\t0x%08"PRIx32", 0x%08"PRIx32"\n", le->heapSize, le->stackSize);
    if (le->magic[1] == 'E') {
        printf("VxD version resource:\t\t0x%08"PRIx32" (%"PRIu32" bytes)\n", le->windowsResourceOffset, le->windowsResourceSize);
        printf("VxD device ID:\t\t\t0x%04"PRIx16"\n", le->windowsDeviceID);
        printf("VxD DDK version:\t\t%"PRIu8".%02"PRIu8" (0x%04"PRIx16")\n", (uint8_t) (le->windowsDDKVersion >> 8), (uint8_t) le->windowsDDKVersion, le->windowsDDKVersion);
    }
}

/* Physical page number (1-based) of entry idx (1-based) in the object page table. */
uint32_t le_page_number(struct THIS *this, uint32_t idx, uint8_t *flags) {
    uint8_t *p;

    if (this->le->magic[1] == 'X') {
        /* LX: dword file offset (shifted), word size, word flags. Not a page number. */
        if (!(p = le_ldr(this, this->le->objectMapOffset + (idx - 1) * 8, 8))) return 0;
        if (flags) *flags = p[6];
        return idx;
    }
    if (!(p = le_ldr(this, this->le->objectMapOffset + (idx - 1) * 4, 4))) return 0;
    if (flags) *flags = p[3];
    return ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
}

/* Load every object's pages into memory, for the DDB and the INT 20h sites. */
void load_le_objects(struct THIS *this) {
    struct exe_le_header *le = this->le;
    uint32_t i, j, page, size, off;
    uint8_t pflags;

    if (!(this->le_objimg = calloc(le->objectCount, sizeof(uint8_t *)))) err(1, "Cannot allocate memory");
    for (i = 0; i < le->objectCount; i++) {
        struct exe_le_object *o = &this->le_objs[i];
        uint32_t span = o->pageMapCount * le->pageSize;
        if (span < o->virtualSize) span = o->virtualSize;
        if (!(this->le_objimg[i] = calloc(1, span + 4))) err(1, "Cannot allocate memory");
        if (le->magic[1] == 'X') continue;  /* LX page offsets are not handled */
        for (j = 0; j < o->pageMapCount; j++) {
            page = le_page_number(this, o->pageMapIndex + j, &pflags);
            if (!page || pflags) continue;  /* zero-filled, invalid or iterated */
            size = (page == le->pages) ? le->lastPage : le->pageSize;
            off = le->dataPagesOffset + (page - 1) * le->pageSize;
            read_at(this, off, this->le_objimg[i] + j * le->pageSize, size);
        }
    }
}

void read_le_objects(struct THIS *this) {
    struct exe_le_header *le = this->le;
    uint32_t i, j, page;
    uint8_t pflags;
    uint8_t *p;

    if (!(p = le_ldr(this, le->objectTableOffset, le->objectCount * sizeof(struct exe_le_object)))) {
        warnx("Object table is outside the loader section: %s", this->fname);
        return;
    }
    if (!(this->le_objs = malloc(le->objectCount * sizeof(struct exe_le_object) + 1))) err(1, "Cannot allocate memory");
    memcpy(this->le_objs, p, le->objectCount * sizeof(struct exe_le_object));
    printf(
        "\n\n"
        "Objects:\n"
        "  #  VirtSize  RelocBase Flags     Pages (first)\n"
        "---------------------------------------------------\n"
    );
    for (i = 0; i < le->objectCount; i++) {
        struct exe_le_object *o = &this->le_objs[i];
        printf("  %"PRIu32"  %08"PRIx32"  %08"PRIx32"  %08"PRIx32"  %"PRIu32" (%"PRIu32")  %s%s%s%s%s%s%s%s%s%s%s%s\n",
            i + 1, o->virtualSize, o->relocBase, o->flags, o->pageMapCount, o->pageMapIndex,
            (o->flags & LEOBJ_READ) ? "READ " : "",
            (o->flags & LEOBJ_WRITE) ? "WRITE " : "",
            (o->flags & LEOBJ_EXEC) ? "EXEC " : "",
            (o->flags & LEOBJ_RESOURCE) ? "RESOURCE " : "",
            (o->flags & LEOBJ_DISCARD) ? "DISCARD " : "",
            (o->flags & LEOBJ_SHARED) ? "SHARED " : "",
            (o->flags & LEOBJ_PRELOAD) ? "PRELOAD " : "",
            (o->flags & LEOBJ_INVALID) ? "INVALID " : "",
            ((o->flags & (LEOBJ_ZEROFILL | LEOBJ_RESIDENT)) == LEOBJ_ZEROFILL) ? "ZEROFILL " :
                ((o->flags & (LEOBJ_ZEROFILL | LEOBJ_RESIDENT)) == LEOBJ_RESIDENT) ? "RESIDENT " :
                ((o->flags & (LEOBJ_ZEROFILL | LEOBJ_RESIDENT)) ? "CONTIGUOUS " : ""),
            (o->flags & LEOBJ_LONGLOCK) ? "LONGLOCKABLE " : "",
            (o->flags & LEOBJ_BIG) ? "USE32 " : "USE16 ",
            (o->flags & LEOBJ_IOPL) ? "IOPL " : "");
        if (this->verbose)
            for (j = 0; j < o->pageMapCount; j++) {
                page = le_page_number(this, o->pageMapIndex + j, &pflags);
                printf("       page map %"PRIu32": page %"PRIu32", flags 0x%02"PRIx8" (file offset 0x%08"PRIx32")\n",
                    o->pageMapIndex + j, page, pflags, le->dataPagesOffset + (page - 1) * le->pageSize);
            }
    }
}

/* Print a resident or nonresident name table; remember a name ending in _DDB. */
void read_le_name_table(struct THIS *this, uint8_t *p, uint32_t len, const char *title) {
    uint32_t i = 0;
    uint8_t n;
    uint16_t ord;
    char name[256];

    printf("\n\n%s:\n", title);
    while (i < len && (n = p[i])) {
        if (i + 1 + n + 2 > len) break;
        memcpy(name, p + i + 1, n);
        name[n] = '\0';
        ord = (uint16_t) (p[i + 1 + n] | (p[i + 2 + n] << 8));
        printf("  [%3"PRIu16"] %s\n", ord, name);
        if (n > 4 && !strcmp(name + n - 4, "_DDB") && ord) {
            this->le_ddb_ordinal = ord;
            snprintf(this->le_ddb_name, sizeof(this->le_ddb_name), "%s", name);
        }
        i += 1 + n + 2;
    }
}

void read_le_names(struct THIS *this) {
    struct exe_le_header *le = this->le;
    uint8_t *p, *nr;
    uint32_t len;

    if ((p = le_ldr(this, le->resourceNameOffset, 1))) {
        len = this->le_ldrlen - le->resourceNameOffset;
        read_le_name_table(this, p, len, "Resident names");
    }
    if (this->le_nrbase) {
        /* Observed in WIN386.EXE: the modules' non-resident name tables were
         * not carried into the W3 container (the offsets point past the
         * module's data, where there are no names). */
        printf("\n\nNon-resident names:\n  (not carried in the W3 container)\n");
    } else if (le->nonresidentNameTableOffset && le->nonresidentNameTableSize) {
        if (!(nr = malloc(le->nonresidentNameTableSize))) err(1, "Cannot allocate memory");
        if (read_at(this, le->nonresidentNameTableOffset + this->le_nrbase, nr, le->nonresidentNameTableSize))
            read_le_name_table(this, nr, le->nonresidentNameTableSize, "Non-resident names");
        free(nr);
    }
}

/* Walk the entry table; print it, and find the object and offset of an ordinal. */
int walk_le_entries(struct THIS *this, uint16_t want, uint16_t *obj_out, uint32_t *off_out) {
    uint8_t *p;
    uint32_t i, end;
    uint16_t ord = 1, obj;
    uint8_t count, type, eflags;
    uint32_t off;
    int k;

    if (!(p = le_ldr(this, this->le->entryTableOffset, 1))) return 0;
    end = this->le_ldrlen - this->le->entryTableOffset;
    i = 0;
    while (i < end && (count = p[i])) {
        if (i + 2 > end) break;
        type = p[i + 1] & 0x7F;
        i += 2;
        if (type == LE_BUNDLE_UNUSED) { ord += count; continue; }
        if (i + 2 > end) break;
        obj = (uint16_t) (p[i] | (p[i + 1] << 8));
        i += 2;
        for (k = 0; k < count; k++, ord++) {
            switch (type) {
                case LE_BUNDLE_16BIT:
                    if (i + 3 > end) return 0;
                    eflags = p[i]; off = p[i + 1] | (p[i + 2] << 8); i += 3; break;
                case LE_BUNDLE_286GATE:
                    if (i + 5 > end) return 0;
                    eflags = p[i]; off = p[i + 1] | (p[i + 2] << 8); i += 5; break;
                case LE_BUNDLE_32BIT:
                    if (i + 5 > end) return 0;
                    eflags = p[i];
                    off = (uint32_t) p[i + 1] | ((uint32_t) p[i + 2] << 8) | ((uint32_t) p[i + 3] << 16) | ((uint32_t) p[i + 4] << 24);
                    i += 5; break;
                default:
                    warnx("Unknown entry table bundle type %"PRIu8" in %s", type, this->fname);
                    return 0;
            }
            if (!want)
                printf("  [%3"PRIu16"] %"PRIu16":%08"PRIx32" %s%s%s\n", ord, obj, off,
                    (eflags & 1) ? "EXPORTED " : "",
                    (eflags & 2) ? "SHARED-DATA " : "",
                    type == LE_BUNDLE_16BIT ? "16-bit" : type == LE_BUNDLE_286GATE ? "286 call gate" : "32-bit");
            else if (want == ord) {
                *obj_out = obj;
                *off_out = off;
                return 1;
            }
        }
    }
    return 0;
}

void read_le_entries(struct THIS *this) {
    printf(
        "\n\n"
        "Entry table:\n"
    );
    walk_le_entries(this, 0, NULL, NULL);
}

char *le_import_module_name(struct THIS *this, uint16_t modord, char *buf, size_t size) {
    uint8_t *p;
    uint32_t i = 0;
    uint16_t k;

    snprintf(buf, size, "module#%"PRIu16, modord);
    if (!(p = le_ldr(this, this->le->importModuleNameTableOffset, 1))) return buf;
    for (k = 1; k < modord; k++) {
        if (!le_ldr(this, this->le->importModuleNameTableOffset + i, 1)) return buf;
        i += 1 + p[i];
    }
    if (le_ldr(this, this->le->importModuleNameTableOffset + i, 1 + p[i]))
        snprintf(buf, size, "%.*s", p[i], p + i + 1);
    return buf;
}

void read_le_imports(struct THIS *this) {
    char name[256];
    uint32_t k;

    printf(
        "\n\n"
        "Imported modules:\n"
        "-----------------\n"
    );
    if (!this->le->importModuleNameTableCount) printf("  (none)\n");
    for (k = 1; k <= this->le->importModuleNameTableCount; k++)
        printf("  [%2"PRIu32"]: %s\n", k, le_import_module_name(this, (uint16_t) k, name, sizeof(name)));
}

/* Parse every fixup record. Internal 32-bit offset fixups are applied to the
 * in-memory object images as object-relative values, and remembered, so the
 * DDB's pointer fields can be shown as object:offset. */
void read_le_fixups(struct THIS *this) {
    struct exe_le_header *le = this->le;
    uint32_t page, start, end, i, srccount, k;
    uint8_t *fpt, *rec, src, flg;
    int16_t srcoff;
    uint32_t tobj, toff, add, ordinal;
    uint16_t modord;
    uint32_t counts[4] = {0, 0, 0, 0};
    uint32_t obj, pageinobj;
    uint16_t srcoffs[256];
    char name[256];

    if (!(fpt = le_ldr(this, le->fixupPageTableOffset, (le->pages + 1) * 4))) {
        warnx("Fixup page table is outside the fixup section: %s", this->fname);
        return;
    }
    if (this->verbose) printf("\n\nFixup records:\n");
    for (page = 1; page <= le->pages; page++) {
        start = fpt[(page - 1) * 4] | (fpt[(page - 1) * 4 + 1] << 8) | (fpt[(page - 1) * 4 + 2] << 16) | ((uint32_t) fpt[(page - 1) * 4 + 3] << 24);
        end = fpt[page * 4] | (fpt[page * 4 + 1] << 8) | (fpt[page * 4 + 2] << 16) | ((uint32_t) fpt[page * 4 + 3] << 24);
        if (end <= start) continue;
        if (!(rec = le_ldr(this, le->fixupRecordTableOffset + start, end - start))) {
            warnx("Fixup records for page %"PRIu32" are outside the fixup section: %s", page, this->fname);
            continue;
        }
        /* Which object holds this page? */
        obj = 0; pageinobj = 0;
        for (k = 0; k < le->objectCount; k++)
            if (page >= this->le_objs[k].pageMapIndex && page < this->le_objs[k].pageMapIndex + this->le_objs[k].pageMapCount) {
                obj = k + 1;
                pageinobj = page - this->le_objs[k].pageMapIndex;
            }
        i = 0;
        while (i + 2 <= end - start) {
            src = rec[i]; flg = rec[i + 1]; i += 2;
            srccount = 0;
            if (src & LE_FIXSRC_LIST) { srccount = rec[i]; i += 1; srcoff = 0; }
            else { srcoff = (int16_t) (rec[i] | (rec[i + 1] << 8)); i += 2; }
            tobj = toff = add = ordinal = 0; modord = 0;
            switch (flg & LE_FIXTGT_TYPEMASK) {
                case LE_FIXTGT_INTERNAL:
                    if (flg & LE_FIXTGT_OBJ16) { tobj = rec[i] | (rec[i + 1] << 8); i += 2; } else tobj = rec[i++];
                    if ((src & LE_FIXSRC_TYPEMASK) != LE_FIXSRC_SEL16) {
                        if (flg & LE_FIXTGT_OFF32) { toff = rec[i] | (rec[i + 1] << 8) | (rec[i + 2] << 16) | ((uint32_t) rec[i + 3] << 24); i += 4; }
                        else { toff = rec[i] | (rec[i + 1] << 8); i += 2; }
                    }
                    break;
                case LE_FIXTGT_IMPORD:
                    if (flg & LE_FIXTGT_OBJ16) { modord = rec[i] | (rec[i + 1] << 8); i += 2; } else modord = rec[i++];
                    if (flg & LE_FIXTGT_ORD8) ordinal = rec[i++];
                    else if (flg & LE_FIXTGT_OFF32) { ordinal = rec[i] | (rec[i + 1] << 8) | (rec[i + 2] << 16) | ((uint32_t) rec[i + 3] << 24); i += 4; }
                    else { ordinal = rec[i] | (rec[i + 1] << 8); i += 2; }
                    break;
                case LE_FIXTGT_IMPNAME:
                    if (flg & LE_FIXTGT_OBJ16) { modord = rec[i] | (rec[i + 1] << 8); i += 2; } else modord = rec[i++];
                    if (flg & LE_FIXTGT_OFF32) { ordinal = rec[i] | (rec[i + 1] << 8) | (rec[i + 2] << 16) | ((uint32_t) rec[i + 3] << 24); i += 4; }
                    else { ordinal = rec[i] | (rec[i + 1] << 8); i += 2; }
                    break;
                case LE_FIXTGT_ENTRY:
                    if (flg & LE_FIXTGT_OBJ16) { ordinal = rec[i] | (rec[i + 1] << 8); i += 2; } else ordinal = rec[i++];
                    break;
            }
            if (flg & LE_FIXTGT_ADDITIVE) {
                if (flg & LE_FIXTGT_ADD32) { add = rec[i] | (rec[i + 1] << 8) | (rec[i + 2] << 16) | ((uint32_t) rec[i + 3] << 24); i += 4; }
                else { add = rec[i] | (rec[i + 1] << 8); i += 2; }
            }
            if (srccount) {
                for (k = 0; k < srccount; k++) {
                    srcoffs[k & 0xFF] = (uint16_t) (rec[i] | (rec[i + 1] << 8));
                    i += 2;
                }
            } else {
                srccount = 1;
                srcoffs[0] = (uint16_t) srcoff;
            }
            counts[flg & LE_FIXTGT_TYPEMASK] += srccount;
            for (k = 0; k < srccount && k < 256; k++) {
                srcoff = (int16_t) srcoffs[k];
                if (this->verbose) {
                    printf("  page %3"PRIu32" +%04"PRIx16" src 0x%02"PRIx8" flags 0x%02"PRIx8" -> ", page, (uint16_t) srcoff, src, flg);
                    switch (flg & LE_FIXTGT_TYPEMASK) {
                        case LE_FIXTGT_INTERNAL: printf("%"PRIu32":%08"PRIx32, tobj, toff); break;
                        case LE_FIXTGT_IMPORD:   printf("%s.%"PRIu32, le_import_module_name(this, modord, name, sizeof(name)), ordinal); break;
                        case LE_FIXTGT_IMPNAME:  printf("%s.@0x%"PRIx32, le_import_module_name(this, modord, name, sizeof(name)), ordinal); break;
                        case LE_FIXTGT_ENTRY:    printf("entry %"PRIu32, ordinal); break;
                    }
                    if (flg & LE_FIXTGT_ADDITIVE) printf(" +0x%"PRIx32, add);
                    printf("\n");
                }
                /* Apply a 32-bit internal offset fixup to our object image (object-relative). */
                if (obj && (flg & LE_FIXTGT_TYPEMASK) == LE_FIXTGT_INTERNAL && (src & LE_FIXSRC_TYPEMASK) == LE_FIXSRC_OFF32) {
                    int32_t at = (int32_t) (pageinobj * le->pageSize) + srcoff;
                    struct exe_le_object *o = &this->le_objs[obj - 1];
                    if (at >= 0 && (uint32_t) at + 4 <= o->pageMapCount * le->pageSize + 4 && this->le_objimg && this->le_objimg[obj - 1]) {
                        uint8_t *d = this->le_objimg[obj - 1] + at;
                        d[0] = (uint8_t) toff; d[1] = (uint8_t) (toff >> 8); d[2] = (uint8_t) (toff >> 16); d[3] = (uint8_t) (toff >> 24);
                        if (this->le_nfix % 256 == 0)
                            if (!(this->le_fix = realloc(this->le_fix, (this->le_nfix + 256) * sizeof(*this->le_fix)))) err(1, "Cannot allocate memory");
                        this->le_fix[this->le_nfix].obj = obj;
                        this->le_fix[this->le_nfix].off = (uint32_t) at;
                        this->le_fix[this->le_nfix].tobj = tobj;
                        this->le_fix[this->le_nfix].toff = toff;
                        this->le_nfix++;
                    }
                }
            }
        }
    }
    printf("\n\nFixups:\t\t\t\t%"PRIu32" internal, %"PRIu32" import by ordinal, %"PRIu32" import by name, %"PRIu32" internal entry\n",
        counts[0], counts[1], counts[2], counts[3]);
}

/* An object-relative pointer field: shows obj:offset when an internal fixup targets it. */
void print_le_ptr(struct THIS *this, uint32_t obj, uint32_t at, const char *label) {
    uint32_t k;
    uint8_t *d = this->le_objimg[obj - 1] + at;
    uint32_t v = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t) d[3] << 24);

    for (k = 0; k < this->le_nfix; k++)
        if (this->le_fix[k].obj == obj && this->le_fix[k].off == at) {
            printf("%s%"PRIu32":%08"PRIx32"\n", label, this->le_fix[k].tobj, this->le_fix[k].toff);
            return;
        }
    if (v) printf("%s0x%08"PRIx32" (no fixup)\n", label, v);
    else printf("%snone\n", label);
}

void read_le_ddb(struct THIS *this) {
    uint16_t obj;
    uint32_t off;
    struct vxd_ddb ddb;
    struct exe_le_object *o;

    if (!this->le_ddb_ordinal) {
        /* Without the names (a W3 module), try the first export: a VxD
         * exports its DDB and normally nothing else. Accepted only if it
         * looks like a DDB (see below). */
        if (!this->le_nrbase) return;
        this->le_ddb_ordinal = 1;
        snprintf(this->le_ddb_name, sizeof(this->le_ddb_name), "first export");
    }
    if (!walk_le_entries(this, this->le_ddb_ordinal, &obj, &off) || !obj || obj > this->le->objectCount) {
        warnx("%s: entry %"PRIu16" not found", this->le_ddb_name, this->le_ddb_ordinal);
        return;
    }
    o = &this->le_objs[obj - 1];
    if (off + sizeof(struct vxd_ddb) > o->pageMapCount * this->le->pageSize) {
        warnx("%s lies outside its object", this->le_ddb_name);
        return;
    }
    memcpy(&ddb, this->le_objimg[obj - 1] + off, sizeof(ddb));
    if ((ddb.sdkVersion >> 8) != 3 && (ddb.sdkVersion >> 8) != 4) {
        printf("\n\nNo VxD Device Descriptor Block found (%s has SDK version 0x%04"PRIx16")\n", this->le_ddb_name, ddb.sdkVersion);
        return;
    }
    printf("\n\nVxD Device Descriptor Block (%s, entry %"PRIu16" at %"PRIu16":%08"PRIx32"):\n", this->le_ddb_name, this->le_ddb_ordinal, obj, off);
    printf("  Device name:\t\t\t\"%.8s\"\n", ddb.name);
    printf("  SDK version:\t\t\t%"PRIu8".%02"PRIu8" (0x%04"PRIx16")\n", (uint8_t) (ddb.sdkVersion >> 8), (uint8_t) ddb.sdkVersion, ddb.sdkVersion);
    printf("  Device ID:\t\t\t0x%04"PRIx16"%s\n", ddb.deviceID, ddb.deviceID ? "" : " (Undefined_Device_ID)");
    printf("  Device version:\t\t%"PRIu8".%02"PRIu8"\n", ddb.versionMajor, ddb.versionMinor);
    printf("  Flags:\t\t\t0x%04"PRIx16"\n", ddb.flags);
    printf("  Init order:\t\t\t0x%08"PRIx32"\n", ddb.initOrder);
    print_le_ptr(this, obj, off + 0x18, "  Control procedure:\t\t");
    print_le_ptr(this, obj, off + 0x1C, "  V86 API procedure:\t\t");
    print_le_ptr(this, obj, off + 0x20, "  PM API procedure:\t\t");
    printf("  V86 API CS:IP:\t\t%04"PRIx16":%04"PRIx16"\n", (uint16_t) (ddb.v86APICSIP >> 16), (uint16_t) ddb.v86APICSIP);
    printf("  PM API CS:IP:\t\t\t%04"PRIx16":%04"PRIx16"\n", (uint16_t) (ddb.pmAPICSIP >> 16), (uint16_t) ddb.pmAPICSIP);
    printf("  Reference data:\t\t0x%08"PRIx32"\n", ddb.referenceData);
    print_le_ptr(this, obj, off + 0x30, "  Service table:\t\t");
    printf("  Services:\t\t\t%"PRIu32"\n", ddb.serviceTableSize);
}

/* VxD dynamic links: INT 20h (CD 20) followed by a dword, the device ID in
 * the high word and the service number in the low word (bit 15: VxDjmp).
 * Only executable objects are searched, and only the dword that follows
 * the two opcode bytes is read; the code itself is not decoded, so a CD 20
 * pair inside other data would also be listed. */
void read_le_int20(struct THIS *this) {
    uint32_t i, j, n = 0;
    uint8_t *d;

    printf("\n\nVxD service calls (INT 20h dynamic links):\n");
    for (i = 0; i < this->le->objectCount; i++) {
        struct exe_le_object *o = &this->le_objs[i];
        if (!(o->flags & LEOBJ_EXEC)) continue;
        d = this->le_objimg[i];
        for (j = 0; j + 6 <= o->virtualSize; j++)
            if (d[j] == 0xCD && d[j + 1] == 0x20) {
                uint16_t svc = (uint16_t) (d[j + 2] | (d[j + 3] << 8));
                uint16_t dev = (uint16_t) (d[j + 4] | (d[j + 5] << 8));
                printf("  %"PRIu32":%08"PRIx32"  device 0x%04"PRIx16" service 0x%04"PRIx16" %s\n", i + 1, j, dev, (uint16_t) (svc & ~VXD_INT20_JMPFLAG), (svc & VXD_INT20_JMPFLAG) ? "jmp" : "call");
                n++;
                j += 5;
            }
    }
    printf("  %"PRIu32" sites\n", n);
}

void free_le(struct THIS *this) {
    uint32_t i;

    if (this->le_objimg) {
        for (i = 0; i < this->le->objectCount; i++) free(this->le_objimg[i]);
        free(this->le_objimg);
    }
    free(this->le_objs);
    free(this->le_ldrbuf);
    free(this->le_fix);
    free(this->le);
    this->le_objimg = NULL;
    this->le_objs = NULL;
    this->le_ldrbuf = NULL;
    this->le_fix = NULL;
    this->le = NULL;
    this->le_nfix = 0;
    this->le_ddb_ordinal = 0;
    this->le_ddb_name[0] = '\0';
}

/* Read the LE module whose header is at file offset base. nrbase is added to
 * the non-resident name table offset: 0 in a stand-alone file, where that
 * offset is from the start of the file, and the module's offset in a W3
 * container (WIN386.EXE), where it is from the module's start (observed: the
 * data page offset is from the start of WIN386.EXE, the non-resident name
 * offset from the module). */
void read_le_at(struct THIS *this, uint32_t base, uint32_t nrbase) {
    uint32_t len;

    if (!(this->le = (struct exe_le_header *) malloc(sizeof(struct exe_le_header)))) err(1, "Cannot allocate memory");
    if (!read_at(this, base, this->le, sizeof(struct exe_le_header))) { free_le(this); return; }
    this->le_base = base;
    this->le_nrbase = nrbase;
    /* The loader and fixup sections follow the header and are addressed from it. */
    len = this->le->fixupPageTableOffset + this->le->fixupSize;
    if (len < this->le->objectTableOffset + this->le->loaderSize) len = this->le->objectTableOffset + this->le->loaderSize;
    if (len < sizeof(struct exe_le_header)) len = sizeof(struct exe_le_header);
    if (!(this->le_ldrbuf = calloc(1, len + 1))) err(1, "Cannot allocate memory");
    this->le_ldrlen = len;
    if (!read_at(this, base, this->le_ldrbuf, len)) { free_le(this); return; }
    read_le_header(this);
    read_le_objects(this);
    if (this->le_objs) {
        load_le_objects(this);
        read_le_names(this);
        read_le_entries(this);
        read_le_imports(this);
        read_le_fixups(this);
        read_le_ddb(this);
        if (this->services) read_le_int20(this);
    }
    free_le(this);
}

void read_le_exe(struct THIS *this) {
    read_le_at(this, this->mzx->nextHeader, 0);
}

void read_w3_exe(struct THIS *this) {
    struct exe_w3_modentry mod;
    
    if ((this->w3 = (struct exe_w3_header *) malloc(sizeof(struct exe_w3_header)))) { 
        fseek(this->fd, this->mzx->nextHeader, SEEK_SET);
        if (fread(this->w3, 1, sizeof(struct exe_w3_header), this->fd)!= sizeof(struct exe_w3_header)) {
            if (ferror(this->fd)) warn("Cannot read %s", this->fname);
            if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        } else {
            printf("VMM version: %"PRIu8".%"PRIu8" (0x%04"PRIx16")\n", this->w3->vmm_major, this->w3->vmm_minor, this->w3->vmm_version);
            printf(
                "VxD Module Table:\n"
                "   ID   Name          Offset      Size       (dec)\n"
                "------------------------------------------------------\n"
            );
            this->wx_modcount = this->w3->modcount;
            for(int i=0; i<this->wx_modcount; i++)  
                if (fread(&mod, 1, sizeof(struct exe_w3_modentry), this->fd)!= sizeof(struct exe_w3_modentry)) {
                    if (ferror(this->fd)) warn("Cannot read %s", this->fname);
                    if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
                } else 
                    printf("  [%02x] \"%.8s\"     0x%08"PRIx32"  0x%08"PRIx32" (%"PRIu32" bytes)\n", i, mod.name, mod.offset, mod.size, mod.size);
            for(int i=0; i<this->wx_modcount; i++) {
                char name[9];
                int n;
                if (!read_at(this, this->mzx->nextHeader + sizeof(struct exe_w3_header) + i * sizeof(struct exe_w3_modentry), &mod, sizeof(mod))) break;
                memcpy(name, mod.name, 8);
                for (n = 8; n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\0'); n--) ;
                name[n] = '\0';
                if (this->w3module && strcasecmp(this->w3module, name)) continue;
                printf("\n\n");
                printf("W3 module [%02x] %s: Linear Executable header at offset 0x%08"PRIx32"\n", i, name, mod.offset);
                read_le_at(this, mod.offset, mod.offset);
            }
        }
    } else err(1, "Cannot allocate memory");
    return; 
}

void read_next_header(struct THIS *this) {
    char next_magic[2];

    if(fseek(this->fd, this->mzx->nextHeader, SEEK_SET)) {
        if (ferror(this->fd)) warn("Cannot read %s", this->fname);
        if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
    } else {
        if (fread(&next_magic, 1, sizeof(next_magic), this->fd) != sizeof(next_magic)) {
            if (ferror(this->fd)) warn("Cannot read %s", this->fname);
            if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        } else {
            if (((next_magic[0] == 'N') && (next_magic[1] == 'E'))) {
                printf("\n\n");
                printf("New Executable header found at offset 0x%08"PRIx32"\n", this->mzx->nextHeader);
                read_ne_exe(this);
            } else if (((next_magic[0] == 'P') && (next_magic[1] == 'E'))) {
                printf("\n\n");
                printf("Portable Executable header found at offset 0x%08"PRIx32"\n", this->mzx->nextHeader);
                // read_pe_exe(this);
            } else if (((next_magic[0] == 'L') && (next_magic[1] == 'E')) ||
                       ((next_magic[0] == 'L') && (next_magic[1] == 'X'))) {
                printf("\n\n");
                printf("Linear Executable header found at offset 0x%08"PRIx32"\n", this->mzx->nextHeader);
                read_le_exe(this);
            } else if ((next_magic[0] == 'W') && (next_magic[1] == '3')) {
                printf("\n\n");
                printf("W3 Executable header found at offset 0x%08"PRIx32"\n", this->mzx->nextHeader);
                read_w3_exe(this);                           
            } else {
                printf("\n\n");
                printf("Unknown next header type: %c%c/0x%04"PRIx16"\n", next_magic[0], next_magic[1], *((uint16_t *) &next_magic));
            }
        }
    }
}

struct THIS *init_this(void) {
    struct THIS *this;

    if (!(this = malloc(sizeof(struct THIS)))) err(1, "Cannot allocate memory");
    memset(this, 0, sizeof(struct THIS));
    return this;
}

void destroy_this(struct THIS *this) {
    if (this->w3) free(this->w3);
    if (this->le) free_le(this);
    if (this->nemods) free(this->nemods);
    if (this->nesegs) free(this->nesegs);
    if (this->ne) free(this->ne);
    if (this->mzx) free(this->mzx);
    if (this->mz) free(this->mz);
    if (this->fd) if ((fclose(this->fd))) err(1, "Cannot close %s", this->fname);
    free(this);
}

void read_mz_reloc(struct THIS *this) {
    struct exe_mz_reloc reloc;
    off_t oldoffset = ftell(this->fd);

    printf("MZ EXE relocaton table\n"
           "Number of relocations: %d\n", this->mz->relocationEntries);
    fseek(this->fd, this->mz->relocationOffset, SEEK_SET);
    for(int i=0; i<this->mz->relocationEntries; i++)
        if (fread(&reloc, 1, sizeof(struct exe_mz_reloc), this->fd)!= sizeof(struct exe_mz_reloc)) {
            if (ferror(this->fd)) warn("Cannot read %s", this->fname);
            if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
        } else
            printf("  [%d] %04x:%04x\n", i, reloc.segment, reloc.offset);
    fseek(this->fd, oldoffset, SEEK_SET);
    return;
}

void display_help(struct THIS *this) { 
    printf(
        "readexe: Displays information on various Microsoft EXE formats.\n"
        "Version "VERSION"\n\n"
        "  Usage: readexe [-h] [-v] [-s] [-m module] [-n offset] EXEFILE.EXE\n\n"
        "  -n\tManually specify offset to next header.\n"
            "\toffset is read as decimal unless prefixed 0x/0X.\n"
        "  -v\tVerbose: LE object page map and every fixup record.\n"
        "  -s\tList VxD service calls (INT 20h dynamic links) in LE code.\n"
        "  -m\tIn a W3 file (WIN386.EXE), dump only the named module.\n"
        "  -h\tDisplay this help.\n\n"
        "Report bugs at https://github.com/segin/readexe\n"
    );
    destroy_this(this);
    exit(0);
}

int main(int argc, char *argv[]) {
    const uint32_t mz_page_size = 512;
    const uint32_t mz_paragraph_size = 16;
    struct THIS *this;
    uint32_t memuse;
    int option;
    char *endptr;
    long int noffset = -1;

#ifdef NEED_ERR
    setprogname(argv[0]);
#endif
    this = init_this();
    if (argc < 2) { 
        warnx("Not enough arguments.");
        display_help(this);
    }
    while((option = getopt(argc, argv, "hvsm:n:")) != -1) {
        switch(option) {
            case 'h':
            case '?':
                display_help(this);
                break;
            case 'v':
                this->verbose = 1;
                break;
            case 's':
                this->services = 1;
                break;
            case 'm':
                this->w3module = optarg;
                break;
            case 'n':
                noffset = strtoul(optarg, &endptr, 0);
                if (*endptr != '\0' || (noffset == ULONG_MAX && errno == ERANGE)) err(1, "Invalid value: %s\n", optarg);
                break;
            default:
                abort();
        }
    }
    if(optind < argc) this->fname = argv[optind]; else display_help(this);
    if (!(this->fd = fopen(this->fname, "rb"))) err(1, "Cannot open %s", this->fname);
    if (noffset != -1) { 
        if (!(this->mzx = malloc(sizeof(struct exe_mz_new_header)))) err(1, "Cannot allocate memory");
        this->mzx->nextHeader = noffset;
        read_next_header(this);
    }
    if (!(this->mz = malloc(sizeof(struct exe_mz_header)))) err(1, "Cannot allocate memory");
    if (fread(this->mz, 1, sizeof(struct exe_mz_header), this->fd) != sizeof(struct exe_mz_header)) {
        if (ferror(this->fd)) warn("Cannot read %s", this->fname);
        if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
    } else {
        if (    ((this->mz->magic[0] == 'M') && (this->mz->magic[1] == 'Z')) 
            ||  ((this->mz->magic[1] == 'M') && (this->mz->magic[0] == 'Z')) ) {
            printf("%s:\n", this->fname);
            printf("DOS executable with magic:\t%c%c (0x%"PRIx8"%"PRIx8")\n", this->mz->magic[0], this->mz->magic[1], this->mz->magic[1], this->mz->magic[0]);
            printf("Number of executable pages:\t0x%04"PRIx16" (%"PRIu32"+ bytes)\n", this->mz->pageCount, ((this->mz->pageCount - 1) * mz_page_size));
            printf("Size of final page:\t\t0x%08"PRIx16" (%"PRIu16" bytes)\n", this->mz->lastPageSize, this->mz->lastPageSize);
            memuse = (((this->mz->pageCount - 1) * mz_page_size) + this->mz->lastPageSize);
            printf("Total code size:\t\t0x%08"PRIx32" (%"PRIu32" bytes)\n", memuse, memuse);
            printf("Total relocation entries:\t0x%04"PRIx16"\n", this->mz->relocationEntries);
            printf("Header size in paragraphs:\t0x%04"PRIx16" (%"PRIu32" bytes)\n", this->mz->hdrSize, (this->mz->hdrSize * mz_paragraph_size));
            printf("Minimum heap in paragraphs:\t0x%04"PRIx16" (%"PRIu32" bytes)\n", this->mz->minMemory, (this->mz->minMemory * mz_paragraph_size));
            printf("Maximum heap in paragraphs:\t0x%04"PRIx16" (%"PRIu32" bytes)\n", this->mz->maxMemory, (this->mz->maxMemory * mz_paragraph_size));
            memuse += (this->mz->minMemory * mz_paragraph_size);
            printf("Minimum memory to load:\t\t%"PRIu32" bytes\n", memuse);
            printf("Initial CS:IP (entrypoint):\t%04"PRIx16":%04"PRIx16"\n", this->mz->initCodeSeg, this->mz->initInstPtr);
            printf("Initial SS:SP (stack):\t\t%04"PRIx16":%04"PRIx16"\n", this->mz->stackSegment, this->mz->stackPointer);
            printf("Checksum:\t\t\t0x%04"PRIx16"\n", this->mz->checksum);
            printf("Relocation table offset:\t0x%04"PRIx16"\n", this->mz->relocationOffset);
            printf("Overlay:\t\t\t0x%04"PRIx16"\n\n", this->mz->overlayNumber);
            if (this->mz->relocationEntries) read_mz_reloc(this);
            /* check for next header */
            if(this->mz->relocationOffset >= 0x40) {
                if (!(this->mzx = malloc(sizeof(struct exe_mz_new_header)))) err(1, "Cannot allocate memory");
                if (fread(this->mzx, 1, sizeof(struct exe_mz_new_header), this->fd) != sizeof(struct exe_mz_new_header)) {
                    if (ferror(this->fd)) warn("Cannot read %s", this->fname);
                    if (feof(this->fd)) warnx("Unexpected end of file: %s", this->fname);
                } else {
                    printf("Offset to next header:\t\t0x%08"PRIx32"\n", this->mzx->nextHeader);
                    read_next_header(this);
                }
            }
        } else
            fprintf(stdout, "Not a DOS/MZ executable: %s\n", this->fname);
    }
    destroy_this(this);
    return(0);
}
