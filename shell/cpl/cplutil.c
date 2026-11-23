/*
 * cplutil.c - helpers shared by w16's applet DLLs (MAIN.CPL, CPWIN386.CPL):
 * DOS file finding, the Windows and System directories, NE descriptions,
 * file size, copy and delete, number formatting, INI section keys.
 *
 * Sources: the MS-DOS INT 21h functions (1Ah, 2Fh, 41h, 4Eh, 4Fh) and the
 * NE format (the non-resident name table at offset 2Ch of the NE header,
 * its first entry the description) are format facts; the Windows 3.1 SDK
 * for GetProfileString with a NULL key.
 */
#include <windows.h>
#include "cplutil.h"

extern void FAR PASCAL DOS3Call(void);

BOOL cpl_find(const char FAR *spec, WORD attr, CPLFIND FAR *f, BOOL first)
{
    BOOL ok = FALSE;
    WORD dseg = SELECTOROF(spec), doff = OFFSETOF(spec);
    WORD tseg = SELECTOROF(f), toff = OFFSETOF(f);
    WORD oseg = 0, ooff = 0;
    _asm {
        push ds
        push es
        mov ah, 2Fh             /* the caller's DTA */
        call DOS3Call
        mov oseg, es
        mov ooff, bx
        mov dx, toff
        mov ds, tseg
        mov ah, 1Ah
        call DOS3Call
        pop es
        pop ds
        push ds
        mov dx, doff
        mov ds, dseg
        mov cx, attr
        mov ah, 4Eh
        cmp first, 0
        jne go
        mov ah, 4Fh
    go:
        call DOS3Call
        pop ds
        mov ax, 1
        jnc done
        xor ax, ax
    done:
        mov ok, ax
        push ds
        mov dx, ooff
        mov ds, oseg
        mov ah, 1Ah
        call DOS3Call
        pop ds
    }
    return ok;
}

static int dir_slash(char *buf, int n, int size)
{
    if (!n || n >= size - 1)
        return 0;
    if (buf[n - 1] != '\\')
        buf[n++] = '\\';
    buf[n] = 0;
    return n;
}

int cpl_windir(char *buf, int size)
{
    return dir_slash(buf, GetWindowsDirectory(buf, size - 1), size);
}

int cpl_sysdir(char *buf, int size)
{
    return dir_slash(buf, GetSystemDirectory(buf, size - 1), size);
}

void cpl_join(char *buf, int size, const char *dir, const char *name)
{
    int n;
    lstrcpyn(buf, dir, size);
    n = lstrlen(buf);
    if (n && buf[n - 1] != '\\' && buf[n - 1] != ':' && n < size - 1) {
        buf[n++] = '\\';
        buf[n] = 0;
    }
    if (n < size)
        lstrcpyn(buf + n, name, size - n);
}

BOOL cpl_ne_desc(const char *path, char *buf, int size)
{
    HFILE f;
    OFSTRUCT of;
    BYTE hdr[64], len;
    DWORD ne, nonres;
    BOOL ok = FALSE;
    f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        return FALSE;
    if (_lread(f, hdr, 64) == 64 && hdr[0] == 'M' && hdr[1] == 'Z') {
        ne = *(DWORD *)(hdr + 0x3C);
        _llseek(f, ne, 0);
        /* no non-resident name table (its offset or its length 0, the
           WORD at 20h): no description, not the MZ header read as one */
        if (_lread(f, hdr, 64) == 64 && hdr[0] == 'N' && hdr[1] == 'E' &&
            (nonres = *(DWORD *)(hdr + 0x2C)) != 0 && *(WORD *)(hdr + 0x20) != 0) {
            _llseek(f, nonres, 0);
            if (_lread(f, &len, 1) == 1 && len && len < size &&
                _lread(f, buf, len) == len) {
                buf[len] = 0;
                ok = TRUE;
            }
        }
    }
    _lclose(f);
    return ok;
}

long cpl_file_size(const char *path)
{
    HFILE f;
    OFSTRUCT of;
    long n;
    f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        return -1;
    n = _llseek(f, 0, 2);
    _lclose(f);
    return n;
}

BOOL cpl_copy_file(const char *src, const char *dst)
{
    HFILE a, b;
    OFSTRUCT of;
    HGLOBAL hb;
    char FAR *buf;
    UINT n, chunk = 16384;
    BOOL ok = TRUE;
    a = OpenFile(src, &of, OF_READ | OF_SHARE_DENY_WRITE);
    if (a == HFILE_ERROR)
        return FALSE;
    b = OpenFile(dst, &of, OF_CREATE | OF_WRITE);
    if (b == HFILE_ERROR) {
        _lclose(a);
        return FALSE;
    }
    /* as large a buffer as there is, down to 512 bytes */
    while ((hb = GlobalAlloc(GMEM_MOVEABLE, chunk)) == NULL && chunk > 512)
        chunk /= 2;
    buf = hb ? GlobalLock(hb) : NULL;
    if (!buf)
        ok = FALSE;
    while (ok && (n = _lread(a, buf, chunk)) != 0) {
        if (n == HFILE_ERROR || _lwrite(b, buf, n) != n)
            ok = FALSE;
    }
    if (hb) {
        GlobalUnlock(hb);
        GlobalFree(hb);
    }
    _lclose(a);
    _lclose(b);
    if (!ok)
        cpl_delete(dst);
    return ok;
}

BOOL cpl_delete(const char *path)
{
    OFSTRUCT of;
    return OpenFile(path, &of, OF_DELETE) != HFILE_ERROR;
}

void cpl_thousands(char *buf, unsigned long n)
{
    char digits[12], sep[4];
    int k = 0, i, o = 0;
    GetProfileString("intl", "sThousand", ",", sep, sizeof sep);
    do {
        digits[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    for (i = k - 1; i >= 0; i--) {
        buf[o++] = digits[i];
        if (i && i % 3 == 0 && sep[0])
            buf[o++] = sep[0];
    }
    buf[o] = 0;
}

HGLOBAL cpl_section_keys(const char *ini, const char *section)
{
    UINT size = 1024;
    for (;;) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        char FAR *p;
        UINT n;
        if (!h)
            return NULL;
        p = GlobalLock(h);
        n = ini ? GetPrivateProfileString(section, NULL, "", p, size, ini)
                : GetProfileString(section, NULL, "", p, size);
        GlobalUnlock(h);
        /* a full buffer means it may have been cut short: grow (a segment
           is the format's bound) */
        if (n < size - 2 || size >= 0xF000u)
            return h;
        GlobalFree(h);
        size = size > 0x7800u ? 0xF000u : size * 2;
    }
}

BOOL cpl_atol(const char *s, long *v)
{
    long n = 0;
    int neg = 0, any = 0;
    while (*s == ' ')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    for (; *s >= '0' && *s <= '9'; s++) {
        n = n * 10 + (*s - '0');
        if (n > 100000000L)
            return FALSE;
        any = 1;
    }
    while (*s == ' ')
        s++;
    if (!any || *s)
        return FALSE;
    *v = neg ? -n : n;
    return TRUE;
}
