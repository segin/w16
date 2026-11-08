/*
 * profile.c - .INI files: GetProfileInt/String, WriteProfileString and
 * the private-profile versions.
 *
 * Behaviour observed black-box on Windows 3.1 with tests/apps/tk14.c
 * (Standard and 386 Enhanced mode agree); ARCHITECTURE.md 9.2 lists the
 * findings. The model that reproduces them:
 *
 *  - Two cached files: WIN.INI and one private file (the last one used).
 *    Every call looks for the file; if it is gone the cache is dropped
 *    and the file reads as empty; if it is the cached one, the cached
 *    copy is used even when the file changed on disk. Using another
 *    private file, or WritePrivateProfileString(NULL, NULL, NULL, f) /
 *    WriteProfileString(NULL, NULL, NULL) (which return 0), drops it.
 *  - The cached copy is normalised when the file is read: blanks (space,
 *    tab) at the start of every line go, and on lines that do not start
 *    with '[' the blanks around the first '=' too; a Ctrl-Z at the very
 *    end is dropped. Writes change the cached copy without normalising
 *    what they insert, and write the whole copy back to the file at once.
 *  - A file name without a path means the Windows directory (no search).
 *
 * The cached copy is a global block of any size, addressed as a huge
 * buffer; lines may be of any length.
 */
#include "kfile.h"

#define GMEM_FIXED_     0x0000

typedef struct pcache {
    char path[128];             /* full path */
    int  valid;                 /* the copy holds path's file */
    u16  hnd, sel;              /* the copy (sel 0 when empty) */
    u32  len;
} pcache;

static pcache win_ini, priv_ini;

#define AT(pc, off) HUGE_AT((pc)->sel, (off))

typedef struct line {
    u32 start, cend, next;      /* content [start, cend), next line */
    int term;                   /* ends in LF */
} line;

static int blank(u8 c)
{
    return c == ' ' || c == '\t';
}

static u8 fold(u8 c)
{
    /* UNVERIFIED: how names above 7Fh compare (ASCII folding here) */
    return (c >= 'a' && c <= 'z') ? (u8)(c - 32) : c;
}

static void drop(pcache *pc)
{
    if (pc->hnd)
        GlobalFree(pc->hnd);
    pc->hnd = pc->sel = 0;
    pc->len = 0;
    pc->valid = 0;
}

static void get_line(const pcache *pc, u32 pos, line *ln)
{
    u32 e = pos;
    ln->start = pos;
    while (e < pc->len && *AT(pc, e) != '\n')
        e++;
    ln->term = e < pc->len;
    ln->next = ln->term ? e + 1 : e;
    if (ln->term && e > pos && *AT(pc, e - 1) == '\r')
        e--;
    ln->cend = e;
}

/* first '=' of the line's content, or 0xFFFFFFFF */
static u32 equals_of(const pcache *pc, const line *ln)
{
    u32 i;
    for (i = ln->start; i < ln->cend; i++)
        if (*AT(pc, i) == '=')
            return i;
    return 0xFFFFFFFFUL;
}

static void normalise(pcache *pc)
{
    u32 r = 0, w = 0, q, i;
    line ln;
    if (pc->len && *AT(pc, pc->len - 1) == 0x1A)       /* Ctrl-Z at the end */
        pc->len--;
    while (r < pc->len) {
        while (r < pc->len && blank(*AT(pc, r)))
            r++;
        get_line(pc, r, &ln);
        q = (ln.start < ln.cend && *AT(pc, ln.start) != '[') ? equals_of(pc, &ln)
                                                            : 0xFFFFFFFFUL;
        i = ln.start;
        if (q != 0xFFFFFFFFUL) {
            u32 k = q;
            while (k > ln.start && blank(*AT(pc, k - 1)))
                k--;
            for (; i < k; i++)
                *AT(pc, w++) = *AT(pc, i);
            *AT(pc, w++) = '=';
            for (i = q + 1; i < ln.cend && blank(*AT(pc, i)); i++)
                ;
        }
        for (; i < ln.next; i++)
            *AT(pc, w++) = *AT(pc, i);
        r = ln.next;
    }
    pc->len = w;
}

/* The full path of profile `file`: the Windows directory if it has no
   path. 0 if the name is not valid. */
static int resolve(const char FAR *file, char *path)
{
    char tmp[160];
    u16 n, err;
    if (file_has_path(file))
        return file_canon(file, path, &err);
    n = GetWindowsDirectory(tmp, 128);
    if (n && tmp[n - 1] != '\\')
        tmp[n++] = '\\';
    while (*file && n < sizeof tmp - 1)
        tmp[n++] = *file++;
    tmp[n] = 0;
    return file_canon(tmp, path, &err);
}

static int same_path(const char *a, const char *b)
{
    while (*a && fold(*a) == fold(*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

/* the cache for a private profile: WIN.INI has its own */
static pcache *cache_for(const char *path)
{
    char w[128];
    if (resolve("WIN.INI", w) && same_path(w, path))
        return &win_ini;
    return &priv_ini;
}

/* Make pc hold `path`. Returns 1 if the file exists; else the cache is
   empty (and pc->path set, for a write that creates the file). */
static int load(pcache *pc, const char *path)
{
    u16 fh, err, sel;
    u32 size;

    fh = dos_open(path, 0, &err);
    if (fh == HFILE_ERROR) {
        drop(pc);
        kmemcpy(pc->path, path, sizeof pc->path);
        return 0;
    }
    if (pc->valid && same_path(pc->path, path)) {
        dos_close(fh);
        return 1;
    }
    drop(pc);
    size = dos_seek(fh, 0, 2);
    dos_seek(fh, 0, 0);
    if (size == 0xFFFFFFFFUL)
        size = 0;
    if (size) {
        pc->hnd = galloc(GMEM_FIXED_, size, 0, &sel);
        if (!pc->hnd) {
            dos_close(fh);
            return 0;
        }
        pc->sel = sel;
        size = dos_rw(0x3F, fh, sel, 0, size);
        if (size == 0xFFFFFFFFUL)
            size = 0;
    }
    dos_close(fh);
    pc->len = size;
    kmemcpy(pc->path, path, sizeof pc->path);
    pc->valid = 1;
    normalise(pc);
    return 1;
}

/* ---- names ---- */

typedef struct str {
    const char FAR *p;
    u16 n;
} str;

static str trimmed(const char FAR *s, int lead)
{
    str r;
    r.p = s;
    r.n = s ? kstrlen(s) : 0;
    while (lead && r.n && blank((u8)*r.p)) {
        r.p++;
        r.n--;
    }
    while (r.n && blank((u8)r.p[r.n - 1]))
        r.n--;
    return r;
}

static int name_is(const pcache *pc, u32 at, u32 end, str s)
{
    u16 i;
    if (end - at != s.n)
        return 0;
    for (i = 0; i < s.n; i++)
        if (fold(*AT(pc, at + i)) != fold((u8)s.p[i]))
            return 0;
    return 1;
}

/* Section header line for `sec`: its line in *hdr; 0 if none. Only the
   first section of a name counts (observed). A header is a line that
   starts with '[' and has a ']'; text after the ']' is ignored. */
static int find_section(const pcache *pc, str sec, line *hdr)
{
    u32 pos = 0, q;
    while (pos < pc->len) {
        get_line(pc, pos, hdr);
        if (hdr->start < hdr->cend && *AT(pc, hdr->start) == '[') {
            for (q = hdr->start + 1; q < hdr->cend && *AT(pc, q) != ']'; q++)
                ;
            if (q < hdr->cend && name_is(pc, hdr->start + 1, q, sec))
                return 1;
        }
        pos = hdr->next;
    }
    return 0;
}

/* end of the section whose header is hdr: the next line that starts
   with '[' (UNVERIFIED: also one without ']'), or the end */
static u32 section_end(const pcache *pc, const line *hdr)
{
    u32 pos = hdr->next;
    line ln;
    while (pos < pc->len) {
        get_line(pc, pos, &ln);
        if (ln.start < ln.cend && *AT(pc, ln.start) == '[')
            return pos;
        pos = ln.next;
    }
    return pc->len;
}

/* the line of key `key` in the section after hdr; *eq = its '=' */
static int find_key(const pcache *pc, const line *hdr, str key, line *ln, u32 *eq)
{
    u32 pos = hdr->next, end = section_end(pc, hdr);
    while (pos < end) {
        get_line(pc, pos, ln);
        *eq = equals_of(pc, ln);
        if (*eq != 0xFFFFFFFFUL && name_is(pc, ln->start, *eq, key))
            return 1;
        pos = ln->next;
    }
    return 0;
}

/* ---- reading ---- */

/* Copy bytes [from, to) of sel (huge) with GetProfileString's value
   rules: trailing blanks dropped, then one pair of enclosing quotes
   (" or '); at most cap - 1 characters and a NUL. Returns the count. */
static u16 put_value(u16 sel, u32 from, u32 to, char FAR *buf, u32 cap)
{
    u32 i, n;
    while (to > from && blank(*HUGE_AT(sel, to - 1)))
        to--;
    if (to - from >= 2) {
        u8 q = *HUGE_AT(sel, from);
        if ((q == '"' || q == '\'') && *HUGE_AT(sel, to - 1) == q) {
            from++;
            to--;
        }
    }
    n = to - from;
    if (n > cap - 1)
        n = cap - 1;
    for (i = 0; i < n; i++)
        buf[(u16)i] = *HUGE_AT(sel, from + i);
    buf[(u16)n] = 0;
    return (u16)n;
}

static u16 put_default(const char FAR *def, char FAR *buf, u32 cap)
{
    if (!def)
        def = "";
    return put_value(FP_SEG(def), FP_OFF(def), FP_OFF(def) + kstrlen(def), buf, cap);
}

/* value bytes of a key line: after '=', up to the end or a NUL */
static u32 value_end(const pcache *pc, u32 eq, const line *ln)
{
    u32 i;
    for (i = eq + 1; i < ln->cend && *AT(pc, i); i++)
        ;
    return i;
}

/* The key names of a section, each with a NUL, and one more NUL.
   Observed: when the list and the extra NUL do not fit (n < total + 2),
   the first n - 2 bytes are copied, the bytes at n - 3 and n - 2 become
   NUL and n - 2 is returned. Lines without '=' and lines starting with
   ';' are not listed. */
static u16 put_keys(const pcache *pc, const line *hdr, char FAR *buf, u32 cap)
{
    u32 pos, end = section_end(pc, hdr), total = 0, limit, w = 0, eq, i;
    line ln;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        if (pass)
            limit = total + 2 <= cap ? total : (cap >= 2 ? cap - 2 : 0);
        for (pos = hdr->next; pos < end; pos = ln.next) {
            get_line(pc, pos, &ln);
            eq = equals_of(pc, &ln);
            if (eq == 0xFFFFFFFFUL || *AT(pc, ln.start) == ';')
                continue;
            if (!pass)
                total += eq - ln.start + 1;
            else
                for (i = ln.start; i <= eq && w < limit; i++, w++)
                    buf[(u16)w] = i < eq ? *AT(pc, i) : 0;
        }
    }
    if (total + 2 <= cap) {
        buf[(u16)total] = 0;
        if (!total)
            buf[1] = 0;
        return (u16)total;
    }
    if (cap >= 3)
        buf[(u16)(cap - 3)] = 0;
    buf[(u16)(cap >= 2 ? cap - 2 : 0)] = 0;
    return (u16)limit;
}

static int read_string(const char FAR *sec, const char FAR *key, const char FAR *def,
                       char FAR *buf, int n, const char *path, pcache *pc)
{
    u32 cap = n ? (u32)(u16)n : 0x10000UL;      /* observed: n = 0 is no limit */
    line hdr, ln;
    u32 eq;

    if (sec && load(pc, path) && find_section(pc, trimmed(sec, 1), &hdr)) {
        if (!key)
            return put_keys(pc, &hdr, buf, cap);
        if (find_key(pc, &hdr, trimmed(key, 1), &ln, &eq))
            return put_value(pc->sel, eq + 1, value_end(pc, eq, &ln), buf, cap);
    }
    return put_default(def, buf, cap);
}

/* GetProfileInt's number (fitted to the observations): blanks, then an
   optional '-', an optional "0x" or "x" (hexadecimal; "x5" is 5), then
   another optional '-' ("0x-5" is -5); digits up to the first other
   character, in 16 bits. Anything else ("+7", "abc", "") reads 0. */
static u16 parse_int(const pcache *pc, u32 i, u32 end)
{
    u16 v = 0, base = 10, d;
    int neg = 0;
    while (i < end && blank(*AT(pc, i)))
        i++;
    if (i < end && *AT(pc, i) == '-') {
        neg = 1;
        i++;
    }
    if (i + 1 < end && *AT(pc, i) == '0' && fold(*AT(pc, i + 1)) == 'X') {
        base = 16;
        i += 2;
    } else if (i < end && fold(*AT(pc, i)) == 'X') {
        base = 16;
        i++;
    }
    if (i < end && *AT(pc, i) == '-') {
        neg = 1;
        i++;
    }
    for (; i < end; i++) {
        u8 c = fold(*AT(pc, i));
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = (u16)(v * base + d);
    }
    return neg ? (u16)-v : v;
}

static u16 read_int(const char FAR *sec, const char FAR *key, u16 def,
                    const char *path, pcache *pc)
{
    line hdr, ln;
    u32 eq;
    if (sec && key && load(pc, path) && find_section(pc, trimmed(sec, 1), &hdr) &&
        find_key(pc, &hdr, trimmed(key, 1), &ln, &eq))
        return parse_int(pc, eq + 1, value_end(pc, eq, &ln));
    return def;
}

/* ---- writing ---- */

typedef struct piece {
    u16 sel;
    u32 off, n;
} piece;

static void hcopy(u16 dsel, u32 doff, u16 ssel, u32 soff, u32 n)
{
    while (n) {
        u32 c = n, room;
        room = 0x10000UL - (u16)doff;
        if (c > room)
            c = room;
        room = 0x10000UL - (u16)soff;
        if (c > room)
            c = room;
        if (c > 0x8000UL)
            c = 0x8000UL;
        kmemcpy(HUGE_AT(dsel, doff), HUGE_AT(ssel, soff), (u16)c);
        doff += c;
        soff += c;
        n -= c;
    }
}

static piece far_piece(const char FAR *s, u32 n)
{
    piece p;
    p.sel = FP_SEG(s);
    p.off = FP_OFF(s);
    p.n = n;
    return p;
}

/* Replace bytes [a, b) of the copy by the pieces. */
static int splice(pcache *pc, u32 a, u32 b, const piece *pcs, u16 np)
{
    u32 len = pc->len - (b - a), w;
    u16 i, sel = 0, h = 0;
    for (i = 0; i < np; i++)
        len += pcs[i].n;
    if (len) {
        h = galloc(GMEM_FIXED_, len, 0, &sel);
        if (!h)
            return 0;
    }
    w = 0;
    if (a) {
        hcopy(sel, 0, pc->sel, 0, a);
        w = a;
    }
    for (i = 0; i < np; i++) {
        if (pcs[i].n)
            hcopy(sel, w, pcs[i].sel, pcs[i].off, pcs[i].n);
        w += pcs[i].n;
    }
    if (pc->len > b)
        hcopy(sel, w, pc->sel, b, pc->len - b);
    if (pc->hnd)
        GlobalFree(pc->hnd);
    pc->hnd = h;
    pc->sel = sel;
    pc->len = len;
    return 1;
}

/* write the whole copy back; 0 if the file cannot be made */
static int flush_out(pcache *pc)
{
    u16 err, fh = dos_create(pc->path, 0, &err);
    if (fh == HFILE_ERROR) {
        drop(pc);
        return 0;
    }
    if (pc->len)
        dos_rw(0x40, fh, pc->sel, 0, pc->len);
    dos_close(fh);
    pc->valid = 1;
    return 1;
}

/* the last line in [from, to) whose content is not empty */
static int last_nonblank(const pcache *pc, u32 from, u32 to, line *last)
{
    line ln;
    int found = 0;
    while (from < to) {
        get_line(pc, from, &ln);
        if (ln.cend > ln.start) {
            *last = ln;
            found = 1;
        }
        from = ln.next;
    }
    return found;
}

static int write_string(const char FAR *sec, const char FAR *key, const char FAR *val,
                        const char *path, pcache *pc)
{
    piece pcs[8];
    line hdr, ln;
    str s, k;
    u32 eq, vn = 0;
    u16 np = 0;

    if (!sec)
        return 0;               /* UNVERIFIED: 3.1 wrote a garbage section */
    load(pc, path);
    /* observed: a written section or key loses trailing blanks only */
    s = trimmed(sec, 0);
    if (find_section(pc, s, &hdr)) {
        if (!key) {
            if (!splice(pc, hdr.start, section_end(pc, &hdr), NULL, 0))
                return 0;
            return flush_out(pc);
        }
        k = trimmed(key, 0);
        if (!val) {
            if (find_key(pc, &hdr, k, &ln, &eq) && !splice(pc, ln.start, ln.next, NULL, 0))
                return 0;
            return flush_out(pc);
        }
        /* observed: the value ends at the first control character, and
           loses trailing blanks */
        while (val[vn] && (u8)val[vn] >= 0x20)
            vn++;
        while (vn && blank((u8)val[vn - 1]))
            vn--;
        if (find_key(pc, &hdr, k, &ln, &eq)) {
            /* the key keeps its spelling in the file */
            pcs[0] = far_piece(val, vn);
            if (!splice(pc, eq + 1, ln.cend, pcs, 1))
                return 0;
            return flush_out(pc);
        }
        /* a new key goes after the section's last line that is not
           blank (comments count) */
        last_nonblank(pc, hdr.start, section_end(pc, &hdr), &ln);
        if (!ln.term)
            pcs[np++] = far_piece("\r\n", 2);
        pcs[np++] = far_piece(k.p, k.n);
        pcs[np++] = far_piece("=", 1);
        pcs[np++] = far_piece(val, vn);
        if (ln.term)
            pcs[np++] = far_piece("\r\n", 2);
        if (!splice(pc, ln.next, ln.next, pcs, np))
            return 0;
        return flush_out(pc);
    }
    if (!key || !val)
        return flush_out(pc);   /* nothing to delete; the file is made */
    k = trimmed(key, 0);
    while (val[vn] && (u8)val[vn] >= 0x20)
        vn++;
    while (vn && blank((u8)val[vn - 1]))
        vn--;
    /* a new section goes after the file's last line that is not blank,
       a blank line before it; in a file of blank lines, at the start */
    if (last_nonblank(pc, 0, pc->len, &ln)) {
        pcs[np++] = far_piece(ln.term ? "\r\n" : "\r\n\r\n", ln.term ? 2 : 4);
        eq = ln.next;
    } else
        eq = 0;
    pcs[np++] = far_piece("[", 1);
    pcs[np++] = far_piece(s.p, s.n);
    pcs[np++] = far_piece("]\r\n", 3);
    pcs[np++] = far_piece(k.p, k.n);
    pcs[np++] = far_piece("=", 1);
    pcs[np++] = far_piece(val, vn);
    if (!eq || ln.term)
        pcs[np++] = far_piece("\r\n", 2);
    if (!splice(pc, eq, eq, pcs, np))
        return 0;
    return flush_out(pc);
}

/* ---- the APIs ---- */

static int win_ini_path(char *path)
{
    return resolve("WIN.INI", path);
}

u16 KAPI GetProfileInt(const char FAR *sec, const char FAR *key, u16 def)  /* KERNEL.57 */
{
    char path[128];
    if (!win_ini_path(path))
        return def;
    return read_int(sec, key, def, path, &win_ini);
}

int KAPI GetProfileString(const char FAR *sec, const char FAR *key,
                          const char FAR *def, char FAR *buf, int n)  /* KERNEL.58 */
{
    char path[128];
    if (!win_ini_path(path))
        return put_default(def, buf, n ? (u32)(u16)n : 0x10000UL);
    return read_string(sec, key, def, buf, n, path, &win_ini);
}

u16 KAPI WriteProfileString(const char FAR *sec, const char FAR *key,
                            const char FAR *val)                /* KERNEL.59 */
{
    char path[128];
    if (!sec && !key && !val) {         /* observed: returns 0 */
        drop(&win_ini);
        return 0;
    }
    if (!win_ini_path(path))
        return 0;
    return write_string(sec, key, val, path, &win_ini);
}

u16 KAPI GetPrivateProfileInt(const char FAR *sec, const char FAR *key, u16 def,
                              const char FAR *file)             /* KERNEL.127 */
{
    char path[128];
    if (!file || !resolve(file, path))
        return def;
    return read_int(sec, key, def, path, cache_for(path));
}

int KAPI GetPrivateProfileString(const char FAR *sec, const char FAR *key,
                                 const char FAR *def, char FAR *buf, int n,
                                 const char FAR *file)          /* KERNEL.128 */
{
    char path[128];
    if (!file || !resolve(file, path))
        return put_default(def, buf, n ? (u32)(u16)n : 0x10000UL);
    return read_string(sec, key, def, buf, n, path, cache_for(path));
}

u16 KAPI WritePrivateProfileString(const char FAR *sec, const char FAR *key,
                                   const char FAR *val,
                                   const char FAR *file)        /* KERNEL.129 */
{
    char path[128];
    if (!sec && !key && !val) {         /* observed: returns 0 */
        drop(&priv_ini);
        return 0;
    }
    if (!file || !resolve(file, path))
        return 0;
    return write_string(sec, key, val, path, cache_for(path));
}
