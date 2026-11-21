/*
 * regfile.c - w16 REGEDIT: registration files (.REG), merged and saved
 * (ARCHITECTURE.md 15.9; observed on Windows 3.1, probes RE6, RE10).
 *
 * Reading: the first line must be REGEDIT (any case), else "This is not a
 * registration file". Every further line that starts with
 * HKEY_CLASSES_ROOT (any case; blanks before it are skipped) names a key:
 * the path follows a backslash and ends at the first blank; then, after
 * blanks, an '=' and at most one blank, the rest of the line (trailing
 * blanks kept, "a = b" stays whole) is the key's value. A line with no
 * '=' only makes the key. Other lines (other roots, comments, blank
 * lines) are skipped. Lines end with CR LF or LF; the last may have none.
 *
 * Writing (Save Registration File, /V): "REGEDIT", then the subtree of the
 * selected key in RegEnumKey's order (newest first), each key before its
 * subkeys: a line "HKEY_CLASSES_ROOT\path = value" for each key that has a
 * value or has no subkeys (then the value is empty: "path = "); the root
 * itself is never written. CR LF line ends, nothing after the last.
 */
#include "regedit.h"
#include <shellapi.h>

static const char hkcr[] = "HKEY_CLASSES_ROOT";

static BOOL is_blank(char c)
{
    return c == ' ' || c == '\t';
}

/* compare the start of s with the upper-case keyword kw, any case */
static BOOL starts(LPCSTR s, LPCSTR kw)
{
    for (; *kw; s++, kw++) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c != *kw)
            return FALSE;
    }
    return TRUE;
}

/* the first 7 characters exactly (case counts) */
static BOOL same7(LPCSTR a, LPCSTR b)
{
    int i;
    for (i = 0; i < 7; i++)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}

static MERGEFN apply_fn;

static void merge_line(LPSTR s)
{
    LPSTR key, val = NULL;
    while (is_blank(*s))
        s++;
    if (!starts(s, hkcr))
        return;
    s += sizeof hkcr - 1;
    /* "HKEY_CLASSES_ROOT = x" sets nothing (RE10): a key must follow */
    if (*s != '\\')
        return;
    s++;
    key = s;
    while (*s && !is_blank(*s))
        s++;
    if (*s) {
        *s++ = 0;
        while (is_blank(*s))
            s++;
        if (*s == '=') {
            s++;
            if (*s == ' ')
                s++;
        }
        val = s;
    }
    /* every line is one RegSetValue on HKEY_CLASSES_ROOT, an empty value
       too (which makes the key without one), RE18 */
    if (apply_fn)
        apply_fn(key, val ? val : "");
    else
        reg_set(key, val ? val : "");
}

int merge_file(LPCSTR file, MERGEFN apply)
{
    OFSTRUCT of;
    HFILE f;
    HGLOBAL h;
    LPSTR buf, p, line;
    LONG size;
    HKEY k;
    f = OpenFile(file, &of, OF_READ);
    if (f == HFILE_ERROR)
        return MERGE_CANTOPEN;
    size = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    /* bound: one segment for the file's text (a .REG file of 3.1's size) */
    if (size > 65000L)
        size = 65000L;
    h = GlobalAlloc(GMEM_MOVEABLE, size + 1);
    if (!h) {
        _lclose(f);
        return MERGE_CANTOPEN;
    }
    buf = GlobalLock(h);
    size = _lread(f, buf, (UINT)size);
    _lclose(f);
    if (size < 0)
        size = 0;
    buf[(UINT)size] = 0;
    p = buf;
    line = p;
    while (*p && *p != '\r' && *p != '\n')
        p++;
    if (*p) *p++ = 0;
    while (is_blank(*line))
        line++;
    /* the header's case matters: "regedit" is refused (RE10) */
    if (!same7(line, "REGEDIT") || (line[7] && !is_blank(line[7]))) {
        GlobalUnlock(h);
        GlobalFree(h);
        return MERGE_NOTREG;
    }
    /* held while merging: one write of REG.DAT at the end (RE18) */
    RegCreateKey(HKEY_CLASSES_ROOT, NULL, &k);
    apply_fn = apply;
    while (*p) {
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }
        line = p;
        while (*p && *p != '\r' && *p != '\n')
            p++;
        if (*p) *p++ = 0;
        merge_line(line);
    }
    RegCloseKey(k);
    GlobalUnlock(h);
    GlobalFree(h);
    return MERGE_OK;
}

/* ---- saving ---- */

static HFILE out;
static BOOL werr;
static char sp[PATHMAX];

static void put(LPCSTR s)
{
    UINT n = lstrlen(s);
    if (n && _lwrite(out, s, n) != n)
        werr = TRUE;
}

static void save_rec(BOOL write_self)
{
    static char val[PATHMAX];
    char name[128];
    HKEY k;
    int n = lstrlen(sp);
    BOOL kids = FALSE;
    if (RegOpenKey(HKEY_CLASSES_ROOT, sp, &k))
        return;
    if (!RegEnumKey(k, 0, name, sizeof name))
        kids = TRUE;
    reg_get(sp, val, sizeof val);
    if (write_self && (val[0] || !kids)) {
        put(hkcr);
        put("\\");
        put(sp);
        put(" = ");
        put(val);
        put("\r\n");
    }
    if (kids) {
        DWORD i;
        for (i = 0; !RegEnumKey(k, i, name, sizeof name); i++) {
            if (n + lstrlen(name) + 2 >= PATHMAX)
                continue;
            if (n) lstrcat(sp, "\\");
            lstrcat(sp, name);
            save_rec(TRUE);
            sp[n] = 0;
        }
    }
    RegCloseKey(k);
}

BOOL save_file(LPCSTR file, LPCSTR path)
{
    OFSTRUCT of;
    out = OpenFile(file, &of, OF_CREATE | OF_WRITE);
    if (out == HFILE_ERROR)
        return FALSE;
    werr = FALSE;
    put("REGEDIT\r\n");
    lstrcpy(sp, path);
    save_rec(path[0] != 0);
    _lclose(out);
    return !werr;
}
