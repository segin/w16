/*
 * hlpdump.c - host tool: w16 WINHELP's .HLP reader (shell/winhelp/hlp.c,
 * para.c) built for Linux, to check it against help files.
 *
 *   hlpdump FILE.HLP            the file's |SYSTEM, fonts, topics and text
 *   hlpdump -c FILE.HLP CTX...  the hash of each context string, and its
 *                               topic if |CONTEXT has it
 *   hlpdump -k FILE.HLP         the keywords and their topics
 *   hlpdump -m FILE.HLP         the [MAP] numbers and their topics
 *   hlpdump -q FILE.HLP ...     no output; exit 1 if a file did not parse
 *                               (every topic walked, every item read)
 * Build: cc -DHLP_HOST -Ishell/winhelp -o build/hlpdump tests/winhelp/hlpdump.c
 *        shell/winhelp/hlp.c shell/winhelp/para.c (tests/winhelp/hlpcheck.sh)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hlp.h"
#include "para.h"

static FILE *files[16];

LONG hio_open(const char *path)
{
    int i;
    for (i = 0; i < 16; i++)
        if (!files[i]) {
            files[i] = fopen(path, "rb");
            return files[i] ? i : -1;
        }
    return -1;
}

void hio_close(LONG fh) { fclose(files[fh]); files[fh] = NULL; }

BOOL hio_read(LONG fh, DWORD off, void *buf, WORD n)
{
    if (fseek(files[fh], off, SEEK_SET))
        return FALSE;
    return fread(buf, 1, n, files[fh]) == n;
}

void *hio_alloc(DWORD n) { return calloc(1, n ? n : 1); }
void hio_free(void *p) { free(p); }

static int quiet;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void out(const char *fmt, ...)
{
    va_list a;
    if (quiet)
        return;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}

static int bad;

static void dump_link(HLP *h, DWORD pos, HLPLINK *l)
{
    HLPDATA d;
    HLPITER it;
    int k;
    if (!hlp_linkdata(h, pos, l, &d)) {
        out("  !! cannot read link data\n");
        bad = 1;
        return;
    }
    if (l->type == 2) {
        hio_free(d.mem);
        return;
    }
    if (l->type != 0x20 && l->type != 0x23 && l->type != 1) {
        out("  link type %02X\n", l->type);
        hio_free(d.mem);
        return;
    }
    if (!para_begin(&it, l->type, d.d1, d.n1, d.d2, d.n2)) {
        out("  !! bad link start\n");
        bad = 1;
        hio_free(d.mem);
        return;
    }
    out("  [%s len=%u", l->type == 0x23 ? "table" : "text", it.topiclength);
    if (it.table.ncols) {
        int i;
        out(" cols=%d type=%d min=%d", it.table.ncols, it.table.tabletype, it.table.minwidth);
        for (i = 0; i < it.table.ncols; i++)
            out(" (%d,%d)", it.table.gap[i], it.table.width[i]);
    }
    out("]\n");
    while ((k = para_next(&it)) != PI_END) {
        switch (k) {
        case PI_PARA:
            out("  {para col=%d bits=%04X above=%d below=%d lines=%d left=%d right=%d first=%d border=%02X/%d tabs=%u",
                it.para.column, it.para.bits, it.para.above, it.para.below, it.para.lines,
                it.para.left, it.para.right, it.para.first, it.para.border, it.para.borderwidth,
                it.para.ntabs);
            {
                unsigned i;
                for (i = 0; i < it.para.ntabs; i++)
                    out(" %u/%u", it.para.tab[i], it.para.tabtype[i]);
            }
            out("}");
            break;
        case PI_TEXT: out("%.*s", it.len, it.text); break;
        case PI_FONT: out("{f%u}", it.num); break;
        case PI_BREAK: out("{line}\n"); break;
        case PI_PAREND: out("{par}\n"); break;
        case PI_TAB: out("{tab}"); break;
        case PI_PICT:
            out("{pict %c type=%02X size=%lu hot=%u", "clr"[it.align], it.pictype,
                (unsigned long)it.dlen, it.hotspots);
            if ((it.pictype == 3 || it.pictype == 0x22) && it.dlen >= 4)
                out(" embedded=%u bm=%u", hlp_rw(it.data), hlp_rw(it.data + 2));
            if (it.pictype == 5 && it.dlen > 6)
                out(" '%s'", (const char *)it.data + 6);
            out("}");
            break;
        case PI_HOTEND: out("{/hot}"); break;
        case PI_JUMP:
            out("{jump %02X %08lX", it.code, (unsigned long)it.off);
            if (it.wnum != 0xFF) out(" win=%u", it.wnum);
            if (it.file) out(" file=%s", it.file);
            if (it.window) out(" window=%s", it.window);
            out("}");
            break;
        case PI_MACRO: out("{macro %02X %.*s}", it.code, it.len, it.text); break;
        case PI_NBSP: out("{~}"); break;
        case PI_NBHYPH: out("{_}"); break;
        case PI_CELLEND: out("{cell}\n"); break;
        }
    }
    if (it.d1 != it.e1) {
        out("\n  !! %ld bytes of LinkData1 left\n", (long)(it.e1 - it.d1));
        bad = 1;
    }
    out("\n");
    hio_free(d.mem);
}

static void dump(HLP *h)
{
    unsigned i;
    out("title \"%s\" copyright \"%s\" minor %u flags %u contents %08lX\n", h->title,
        h->copyright, h->minor, h->sysflags, (unsigned long)h->contents);
    if (h->config) {
        const char *c = h->config;
        while (*c) {
            out("config %s\n", c);
            c += strlen(c) + 1;
        }
    }
    for (i = 0; i < h->nwin; i++)
        out("window type=%s name=%s caption=%s %d,%d %dx%d max=%d rgb=%06lX nsr=%06lX flags=%04X\n",
            h->win[i].type, h->win[i].name, h->win[i].caption, h->win[i].x, h->win[i].y,
            h->win[i].cx, h->win[i].cy, h->win[i].max, (unsigned long)h->win[i].rgb,
            (unsigned long)h->win[i].rgbnsr, h->win[i].flags);
    for (i = 0; i < h->nfont; i++)
        out("font %u: %s %u/2pt attr=%02X family=%u fg=%06lX\n", i,
            h->fonts[i].face < h->nface ? h->faces + h->fonts[i].face * 32 : "?",
            h->fonts[i].halfpts, h->fonts[i].attr, h->fonts[i].family,
            (unsigned long)h->fonts[i].fg);
    for (i = 0; i < h->ntopics; i++) {
        HLPTHDR t;
        char title[256], macros[1024];
        HLPLINK l;
        DWORD pos;
        if (!hlp_topichdr(h, i, &t, title, sizeof title, macros, sizeof macros)) {
            out("!! topic %u: cannot read its header\n", i);
            bad = 1;
            continue;
        }
        out("\n==== topic %u at %08lX offset %08lX \"%s\" num=%ld back=%08lX for=%08lX nsr=%08lX scroll=%08lX next=%08lX\n",
            i, (unsigned long)h->topics[i].pos, (unsigned long)h->topics[i].off, title, (long)t.num,
            (unsigned long)t.browseback, (unsigned long)t.browsefor, (unsigned long)t.nonscroll,
            (unsigned long)t.scroll, (unsigned long)t.nexttopic);
        {
            const char *m = macros;
            while (*m) {
                out("  macro %s\n", m);
                m += strlen(m) + 1;
            }
        }
        pos = h->topics[i].pos;
        if (!hlp_link(h, pos, &l))
            continue;
        for (pos = hlp_nextlink(h, pos, &l); pos && hlp_link(h, pos, &l); pos = hlp_nextlink(h, pos, &l)) {
            if (l.type == 2)
                break;
            out("  @%08lX%s", (unsigned long)pos,
                pos == t.nonscroll ? " (non-scrolling)" : pos == t.scroll ? " (scrolling)" : "");
            dump_link(h, pos, &l);
        }
    }
}

static BOOL kwprint(void *c, const char *kw, WORD count, DWORD dataoff)
{
    HLP *h = c;
    WORD i;
    out("%s:", kw);
    for (i = 0; i < count; i++) {
        DWORD off;
        char t[128] = "";
        if (hlp_kwtopic(h, dataoff, i, &off))
            hlp_title(h, hlp_topic_at(h, off), t, sizeof t);
        out(" [%s]", t);
    }
    out("\n");
    return TRUE;
}

int main(int argc, char **argv)
{
    int mode = 0, i, err, status = 0;
    HLP *h;
    if (argc > 1 && argv[1][0] == '-') {
        mode = argv[1][1];
        argv++, argc--;
    }
    if (mode == 'q')
        quiet = 1;
    if (argc < 2) {
        fprintf(stderr, "usage: hlpdump [-c|-k|-m|-q] FILE.HLP [...]\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        h = hlp_open(argv[i], &err);
        if (!h) {
            fprintf(stderr, "%s: cannot open (%d)\n", argv[i], err);
            status = 1;
            continue;
        }
        if (mode == 'c') {
            int j;
            for (j = i + 1; j < argc; j++) {
                DWORD off;
                char t[128] = "";
                BOOL ok = hlp_context(h, argv[j], &off);
                if (ok)
                    hlp_title(h, hlp_topic_at(h, off), t, sizeof t);
                printf("%s %ld %s\n", argv[j], (long)hlp_hash(argv[j]), ok ? t : "(none)");
            }
            hlp_close(h);
            break;
        }
        if (mode == 'k') {
            hlp_keywords(h, kwprint, h);
        } else if (mode == 'm') {
            BYTE b[8];
            WORD n, j;
            if (h->f_ctxomap && hlp_fread(h, h->f_ctxomap, 0, b, 2)) {
                n = hlp_rw(b);
                for (j = 0; j < n; j++) {
                    char t[128] = "";
                    hlp_fread(h, h->f_ctxomap, 2 + j * 8, b, 8);
                    hlp_title(h, hlp_topic_at(h, hlp_rl(b + 4)), t, sizeof t);
                    printf("%lu %08lX %s\n", (unsigned long)hlp_rl(b), (unsigned long)hlp_rl(b + 4), t);
                }
            }
        } else {
            bad = 0;
            dump(h);
            if (bad) {
                fprintf(stderr, "%s: parse errors\n", argv[i]);
                status = 1;
            }
        }
        hlp_close(h);
    }
    return status;
}
