/*
 * hlp.h - w16 Windows Help: the reader for Windows 3.x .HLP files.
 *
 * Portable C: built into WINHELP.EXE (OpenWatcom, Win16, medium model)
 * and, with HLP_HOST defined, into the host tool tests/winhelp/hlpdump.c
 * that checks the reader against help files on Linux.
 *
 * Sources for the format (facts only, rewritten in w16's own words; no
 * code was taken from anywhere):
 *  - Manfred Winterhoff, "Windows Help File Format / Annotation File Format
 *    / SHG and MRB File Format" (helpfile.txt, distributed with helpdeco,
 *    1997, revision 10) - read as a description of the format only;
 *  - Pete Davis, "The Windows Help File Format", Dr. Dobb's Journal,
 *    September and October 1993 (the internal file system);
 *  - Windows 3.1 SDK, Programmer's Reference Vol. 4, ch. 15 (the Help
 *    statements and macros the compiler turns into these structures).
 * Every structure was then checked against the help files of the reference
 * Windows 3.1 install with w16's own dump tool (tests/winhelp/hlpdump.c).
 */
#ifndef W16_HLP_H
#define W16_HLP_H

#ifdef HLP_HOST
#include <stdint.h>
#include <stddef.h>
#define FAR
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int BOOL;
typedef uint32_t COLORREF;
#define TRUE 1
#define FALSE 0
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#else
#include <windows.h>
#endif

typedef BYTE FAR *LPB;
typedef const BYTE FAR *LPCB;

/* the platform's side (winhelp: hlpio.c; host: hlpdump.c) */
LONG hio_open(const char FAR *path);             /* -1: cannot */
void hio_close(LONG fh);
BOOL hio_read(LONG fh, DWORD off, void FAR *buf, WORD n);
void FAR *hio_alloc(DWORD n);                    /* zeroed; NULL: no memory */
void hio_free(void FAR *p);

#define HLP_MAXFONT     512
#define HLP_MAXWIN      8
#define HLP_CACHE       3

typedef struct {
    BYTE attr;              /* 1 bold, 2 italic, 4 underline, 8 strikeout,
                               10h double underline, 20h small caps */
    BYTE halfpts;
    BYTE family;            /* 1 modern, 2 roman, 3 swiss, 4 script, 5 decorative */
    WORD face;
    COLORREF fg;
} HLPFONT;

typedef struct {            /* a [WINDOWS] definition (|SYSTEM record 6) */
    WORD flags;
    char type[10], name[9], caption[51];
    short x, y, cx, cy, max;
    COLORREF rgb, rgbnsr;
} HLPWIN;

typedef struct {
    LONG block;
    WORD len;
    LPB buf;
} HLPBLK;

typedef struct {
    DWORD pos;              /* TOPICPOS of its header link */
    DWORD off;              /* TOPICOFFSET where it starts */
} HLPTOPIC;

typedef struct HLP {
    LONG fh;
    DWORD dirstart;
    WORD minor;             /* 15 HC30, 21 HC31 */
    WORD sysflags;
    DWORD gendate;          /* when it was compiled (|SYSTEM) */
    BOOL lz;                /* topic blocks LZ77 compressed */
    WORD blksize;           /* |TOPIC block size, header included */
    DWORD dsize;            /* a block's decompressed size */
    char title[128];
    char copyright[128];
    DWORD contents;         /* TOPICOFFSET of the contents topic */
    char FAR *config;       /* the [CONFIG] macros, NUL-separated, two NULs at the end */
    WORD configlen;
    WORD nwin;
    HLPWIN win[HLP_MAXWIN];
    /* |Phrases */
    WORD nphr;
    WORD FAR *phroff;
    LPB phrtext;
    /* |FONT */
    WORD nface, facelen;
    char FAR *faces;
    WORD nfont;
    HLPFONT FAR *fonts;
    /* internal files (offset of their FILEHEADER; 0 when absent) */
    DWORD f_topic, f_context, f_ctxomap, f_kwbtree, f_kwdata, f_kwmap, f_ttl, f_tomap;
    DWORD topicsize;        /* |TOPIC's size */
    /* the topics, in file order */
    WORD ntopics;
    HLPTOPIC FAR *topics;
    /* decompressed topic blocks */
    HLPBLK cache[HLP_CACHE];
    WORD cachenext;
    LPB page;               /* a B+ tree page */
    LPB raw;                /* a compressed topic block */
} HLP;

/* a B+ tree's header */
typedef struct {
    DWORD base;             /* file offset of page 0 */
    WORD pagesize;
    short root, nlevels;
    LONG entries;
    char structure[17];
} HLPBT;

HLP FAR *hlp_open(const char FAR *path, int FAR *err);
void hlp_close(HLP FAR *h);
#define HLPE_NOFILE     1
#define HLPE_NOTHELP    2
#define HLPE_NOMEM      3

DWORD hlp_file(HLP FAR *h, const char FAR *name);    /* an internal file's FILEHEADER, 0: none */
DWORD hlp_filesize(HLP FAR *h, DWORD f);
BOOL hlp_fread(HLP FAR *h, DWORD f, DWORD off, void FAR *buf, WORD n);

LONG hlp_hash(const char FAR *s);
BOOL hlp_context(HLP FAR *h, const char FAR *ctx, DWORD FAR *off);
BOOL hlp_context_hash(HLP FAR *h, LONG hash, DWORD FAR *off);
BOOL hlp_mapid(HLP FAR *h, DWORD id, DWORD FAR *off);
int hlp_topic_at(HLP FAR *h, DWORD off);             /* the topic holding a TOPICOFFSET; -1 */
BOOL hlp_title(HLP FAR *h, int topic, char FAR *buf, WORD n);

/* keywords (|KWBTREE, |KWDATA): enumerate in order */
typedef BOOL (*HLPKWFN)(void FAR *ctx, const char FAR *kw, WORD count, DWORD dataoff);
BOOL hlp_keywords(HLP FAR *h, HLPKWFN fn, void FAR *ctx);
BOOL hlp_kwtopic(HLP FAR *h, DWORD dataoff, WORD i, DWORD FAR *off);
BOOL hlp_kwfind(HLP FAR *h, const char FAR *kw, BOOL partial, WORD FAR *count, DWORD FAR *dataoff);

/* reading |TOPIC */
BOOL hlp_tread(HLP FAR *h, DWORD pos, void FAR *buf, WORD n);
#define HLP_OVERFLOW 0x80000000UL
LONG hlp_posblock(HLP FAR *h, DWORD pos, WORD FAR *off);
typedef struct {
    LONG blocksize, datalen2;
    DWORD prev, next;
    LONG datalen1;
    BYTE type;
} HLPLINK;
#define HLPLINK_SIZE 21
BOOL hlp_link(HLP FAR *h, DWORD pos, HLPLINK FAR *l);
DWORD hlp_nextlink(HLP FAR *h, DWORD pos, HLPLINK FAR *l);

/* a topic's header (record type 2) */
typedef struct {
    DWORD browseback, browsefor;
    LONG num;
    DWORD nonscroll, scroll, nexttopic;
} HLPTHDR;
BOOL hlp_topichdr(HLP FAR *h, int topic, HLPTHDR FAR *t, char FAR *title, WORD ntitle,
                  char FAR *macros, WORD nmacros);

/* a link's data: LinkData1 and LinkData2 (phrase-expanded), in one block
   from hio_alloc; free with hio_free */
typedef struct {
    LPB d1;
    DWORD n1;
    LPB d2;
    DWORD n2;
    void FAR *mem;
} HLPDATA;
BOOL hlp_linkdata(HLP FAR *h, DWORD pos, HLPLINK FAR *l, HLPDATA FAR *d);

/* compressed numbers in LinkData1 and pictures */
WORD hlp_cuword(LPCB FAR *p);
short hlp_csword(LPCB FAR *p);
DWORD hlp_culong(LPCB FAR *p);
LONG hlp_cslong(LPCB FAR *p);
WORD hlp_rw(LPCB p);
DWORD hlp_rl(LPCB p);

DWORD hlp_lz77(LPCB src, DWORD n, LPB dst, DWORD max);
DWORD hlp_runlen(LPCB src, DWORD n, LPB dst, DWORD max);

void hlp_strcpyn(char FAR *d, const char FAR *s, WORD n);
int hlp_stricmp(const char FAR *a, const char FAR *b);
WORD hlp_strlen(const char FAR *s);
void hlp_memcpy(void FAR *d, const void FAR *s, DWORD n);
void hlp_memset(void FAR *d, int c, DWORD n);
int hlp_strcmp(const char FAR *a, const char FAR *b);

#endif
