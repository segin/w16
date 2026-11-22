/*
 * pif.c - the PIF Editor's file: a .PIF held as the bytes 3.1's PIF Editor
 * writes (ARCHITECTURE.md 15.11).
 *
 * Sources. The published descriptions of the format were the starting map
 * (Ralf Brown's file-format list, "Program Information File"; the Windows
 * 3.1 Resource Kit's and User's Guide's descriptions of each setting),
 * facts only; every offset and bit below was then confirmed by black-box
 * observation of 3.1's PIF Editor: one setting changed at a time, the file
 * saved, the bytes compared (probes PE4, PE7, PE8, PE12-PE17,
 * tests/scripts/pifedit/obs/). PIFEDIT.EXE itself was never looked into.
 *
 * Layout of a new PIF (545 bytes, both modes):
 *   000h  0      reserved
 *   001h  checksum: the sum of bytes 002h-170h, modulo 256
 *   002h  title, 30 bytes, blank padded, no terminator
 *   020h  word: KB maximum (Standard mode: see pif_put)
 *   022h  word: KB required (Standard mode's "KB Required")
 *   024h  program, 63 bytes, upper case, NUL then blanks
 *   063h  flags: 80h COM1, 40h COM2, 10h Close Window on Exit, 08h No
 *         Screen Exchange, 04h Prevent Program Switch, 02h graphics
 *   064h  0
 *   065h  start-up directory, 64 bytes, upper case, NUL then blanks
 *   0A5h  Standard mode's parameters, 64 bytes, NUL, blanks from the
 *         second byte after it (an empty field keeps its second byte)
 *   0E5h  7F 01 00 FF 19 50 00 00, 0EDh word 0007h (10h: graphics)
 *   0EFh  zeros
 *   16Fh  E0h (10h: Keyboard), 170h 20h (40h: saved in Standard mode)
 *   171h  "MICROSOFT PIFEX" header: next 187h, offset 0, length 171h
 *   187h  "WINDOWS 386 3.0": next 205h, data 19Dh, length 68h
 *   19Dh  386 data (see D386_*)
 *   205h  "WINDOWS 286 3.0": next FFFFh, data 21Bh, length 6
 *   21Bh  word XMS KB limit, word XMS KB required, flags (see S286_*)
 * A file read keeps its own layout (Setup's _DEFAULT.PIF and DOSPRMPT.PIF
 * have the 286 section first): the data go where its headers say. A PIF
 * of Windows 2.x (the base section alone) gets the two sections appended
 * as above, the 386 one's memory from the base.
 */
#include "pifedit.h"
#include <string.h>

BYTE g_pif[PIF_BUFMAX + 80];
UINT g_pifsize;
UINT g_off386, g_off286;
BOOL g_fieldfix = TRUE;         /* [w16] PifFieldFix (main.c) */
BOOL g_badsum;                  /* the file read had a wrong checksum */

/* the 386 section's data (PE8, PE12) */
#define D386_MAX        0x00    /* KB Desired */
#define D386_MIN        0x02    /* KB Required */
#define D386_FG         0x04
#define D386_BG         0x06
#define D386_EMSLIM     0x08
#define D386_EMSREQ     0x0A
#define D386_XMSLIM     0x0C
#define D386_XMSREQ     0x0E
#define D386_FLAGS      0x10    /* three bytes, below */
#define D386_VIDEO      0x14
#define D386_HKSCAN     0x18
#define D386_HKMODS     0x1A
#define D386_HKFLAG     0x1C    /* 000Fh once a key is set */
#define D386_PARAMS     0x28    /* 64 bytes */
#define D386_LEN        0x68

static const char sig_ex[] = "MICROSOFT PIFEX";
static const char sig_386[] = "WINDOWS 386 3.0";
static const char sig_286[] = "WINDOWS 286 3.0";

static WORD getw(UINT off)
{
    return g_pif[off] | ((WORD)g_pif[off + 1] << 8);
}

static void setw(UINT off, WORD v)
{
    g_pif[off] = (BYTE)v;
    g_pif[off + 1] = (BYTE)(v >> 8);
}

/* a section header: 16 bytes of name, next, data offset, data length */
static void header(UINT at, const char *name, WORD next, WORD off, WORD len)
{
    _fmemset(g_pif + at, 0, 16);
    _fmemcpy(g_pif + at, name, lstrlen(name));
    setw(at + 16, next);
    setw(at + 18, off);
    setw(at + 20, len);
}

/* the default 386 data (PE2, PE8) */
static void data386(UINT d, WORD max, WORD min)
{
    _fmemset(g_pif + d, 0, D386_LEN);
    setw(d + D386_MAX, max);
    setw(d + D386_MIN, min);
    setw(d + D386_FG, 100);
    setw(d + D386_BG, 50);
    setw(d + D386_EMSLIM, 1024);
    setw(d + D386_XMSLIM, 1024);
    g_pif[d + D386_FLAGS] = 0x08;       /* full screen */
    g_pif[d + D386_FLAGS + 1] = 0x10;   /* detect idle time */
    g_pif[d + D386_FLAGS + 2] = 0x02;   /* allow fast paste */
    g_pif[d + D386_VIDEO] = 0x1F;       /* text; ports not monitored; emulate */
}

/* the extension sections after the base: the layout of a new PIF */
static void extension(WORD max, WORD min)
{
    header(0x171, sig_ex, 0x187, 0, PIF_BASE);
    header(0x187, sig_386, 0x205, 0x19D, D386_LEN);
    data386(0x19D, max, min);
    header(0x205, sig_286, 0xFFFF, 0x21B, 6);
    _fmemset(g_pif + 0x21B, 0, 6);
    g_off386 = 0x19D;
    g_off286 = 0x21B;
    g_pifsize = PIF_SIZE;
}

void pif_new(void)
{
    static const BYTE screen[] = { 0x7F, 0x01, 0x00, 0xFF, 0x19, 0x50, 0x00, 0x00, 0x07, 0x00 };
    _fmemset(g_pif, 0, sizeof g_pif);
    _fmemset(g_pif + 0x02, ' ', 30);
    setw(0x20, 128);
    setw(0x22, 128);
    g_pif[0x63] = 0x10;
    _fmemcpy(g_pif + 0xE5, screen, sizeof screen);
    g_pif[0x16F] = 0xE0;
    g_pif[0x170] = 0x20;
    extension(640, 128);
}

/* After a file is read, 3.1's PIF Editor leaves each text field cut after
   its last character that is not a blank: a NUL there, which for a field
   filled to its end with NULs is the byte after the field (Setup's PIFs:
   the low byte of 020h after the title, 0E5h after the parameters; a
   Standard-mode PIF's 386 parameters: the first byte of the next
   section's name). Those writes outside the field are made only with
   [w16] PifFieldFix=0 (PE13, PE14, PE16, PE17). */
static void trim(UINT off, UINT len)
{
    int i = len - 1;
    while (i >= 0 && g_pif[off + i] == ' ')
        i--;
    if ((UINT)(i + 1) < len || !g_fieldfix)
        g_pif[off + i + 1] = 0;
}

void pif_trimloaded(void)
{
    trim(0x02, 30);
    trim(0x24, 63);
    trim(0x65, 64);
    trim(0xA5, 64);
    if (g_off386)
        trim(g_off386 + D386_PARAMS, 64);
}

static int find(const char *name)
{
    UINT at = 0x171, n = 0;
    while (at + 22 <= g_pifsize && n++ < 16) {
        if (_fmemcmp(g_pif + at, name, lstrlen(name) + 1) == 0)
            return at;
        at = getw(at + 16);
        if (at == 0xFFFF)
            break;
    }
    return 0;
}

/* the last header of the chain */
static UINT last_header(void)
{
    UINT at = 0x171, n = 0, next;
    while (n++ < 16 && (next = getw(at + 16)) != 0xFFFF && next + 22 <= g_pifsize)
        at = next;
    return at;
}

/* a missing section is appended (UNVERIFIED what 3.1 does: every file
   observed has both sections or none) */
static UINT append(const char *name, WORD len)
{
    UINT at = g_pifsize, last = last_header();
    if (at + 22 + len > PIF_BUFMAX)
        return 0;
    setw(last + 16, at);
    header(at, name, 0xFFFF, at + 22, len);
    _fmemset(g_pif + at + 22, 0, len);
    g_pifsize = at + 22 + len;
    return at + 22;
}

/* 0, IDS_OLDPIF or IDS_INVALIDPIF (read and converted: say so before it
   is shown) or an error string */
int pif_load(LPCSTR path, HWND owner)
{
    HFILE f;
    OFSTRUCT of;
    UINT n;
    int h, r;
    BYTE prev[10];
    static BYTE in[PIF_BUFMAX + 1];
    (void)owner;
    f = OpenFile(path, &of, OF_READ);
    if (f == HFILE_ERROR)
        return of.nErrCode == 3 ? IDS_PATHNOTFOUND : IDS_FILENOTFOUND;
    _fmemset(in, 0, sizeof in);
    n = _lread(f, in, sizeof in);
    _lclose(f);
    /* shorter than the base or longer than any PIF: 3.1 says "File not
       found." and keeps what it shows (PE19: 301 and 1472 bytes; the
       longest accepted is UNVERIFIED, 525 bytes were) */
    if (n == (UINT)-1 || n < PIF_BASE || n > PIF_BUFMAX)
        return IDS_FILENOTFOUND;
    /* what the 386 data held after its hot key (below) */
    _fmemcpy(prev, g_pif + (g_off386 ? g_off386 : 0x19D) + 2, sizeof prev);
    _fmemset(g_pif, 0, sizeof g_pif);
    _fmemcpy(g_pif, in, n);
    g_pifsize = n;
    {
        UINT i;
        BYTE sum = 0;
        for (i = 2; i < PIF_BASE; i++)
            sum += g_pif[i];
        g_badsum = sum != g_pif[1];
    }
    g_off386 = g_off286 = 0;
    if (n < PIF_BASE + 22 || _fmemcmp(in + 0x171, sig_ex, sizeof sig_ex) != 0) {
        /* no extension: a Windows 2.x PIF (the base alone, PE9, PE14),
           or of another length "an invalid Windows PIF" (PF10): converted
           to the layout of a new PIF. Read in Standard mode, its new 386
           section has, after the hot key, what the 386 data held before
           from their third byte (PF10: the previous file's), and blanks
           in its parameters from the fourth byte; in 386 Enhanced mode
           zeros, the parameters then written as any (PE14 R19) */
        r = n == PIF_BASE ? IDS_OLDPIF : IDS_INVALIDPIF;
        pif_trimloaded();
        extension(getw(0x20), getw(0x22));
        if (!g_enh) {
            _fmemcpy(g_pif + g_off386 + 0x1E, prev, sizeof prev);
            _fmemset(g_pif + g_off386 + D386_PARAMS + 3, ' ', 61);
        }
        return r;
    }
    if ((h = find(sig_386)) != 0)
        g_off386 = getw(h + 18);
    if ((h = find(sig_286)) != 0)
        g_off286 = getw(h + 18);
    if (g_off386 + D386_LEN > g_pifsize)
        g_off386 = 0;
    if (g_off286 + 6 > g_pifsize)
        g_off286 = 0;
    if (!g_off386 && (g_off386 = append(sig_386, D386_LEN)) != 0)
        data386(g_off386, getw(0x20), getw(0x22));
    if (!g_off286)
        g_off286 = append(sig_286, 6);
    pif_trimloaded();
    return 0;
}

BOOL pif_save(LPCSTR path)
{
    HFILE f;
    OFSTRUCT of;
    UINT i;
    BYTE sum = 0;
    for (i = 2; i < PIF_BASE; i++)
        sum += g_pif[i];
    g_pif[1] = sum;
    f = OpenFile(path, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return FALSE;
    i = _lwrite(f, (LPCSTR)g_pif, g_pifsize);
    _lclose(f);
    return i == g_pifsize;
}

/* a text field out of the file: up to its NUL, blanks at the end dropped */
static void getstr(LPSTR to, UINT off, UINT len)
{
    UINT i = 0;
    while (i < len && g_pif[off + i])
        to[i] = g_pif[off + i], i++;
    while (i > 0 && to[i - 1] == ' ')
        i--;
    to[i] = 0;
}

void pif_get(PIFVIEW FAR *v, BOOL enh)
{
    UINT d = g_off386;
    getstr(v->title, 0x02, 30);
    getstr(v->program, 0x24, 63);
    getstr(v->dir, 0x65, 64);
    /* Close Window on Exit shows checked for a PIF whose flags byte is 0
       (Setup's, PE21), but the file's bit is what is saved until the box
       is used */
    v->f63 = g_pif[0x63] ? g_pif[0x63] : 0x10;
    v->closeset = FALSE;
    v->f16f = g_pif[0x16F];
    if (enh && d) {
        getstr(v->params, d + D386_PARAMS, 64);
        v->memreq = (int)getw(d + D386_MIN);
        v->memdes = (int)getw(d + D386_MAX);
        v->emslim = (int)getw(d + D386_EMSLIM);
        v->emsreq = (int)getw(d + D386_EMSREQ);
        v->xmslim = (int)getw(d + D386_XMSLIM);
        v->xmsreq = (int)getw(d + D386_XMSREQ);
    } else {
        getstr(v->params, 0xA5, 64);
        v->memreq = (int)getw(0x22);
        v->memdes = (int)getw(0x20);
        v->xmslim = g_off286 ? (int)getw(g_off286) : 0;
        v->xmsreq = g_off286 ? (int)getw(g_off286 + 2) : 0;
    }
    if (d) {
        v->fgprio = (int)getw(d + D386_FG);
        v->bgprio = (int)getw(d + D386_BG);
        v->flags386 = g_pif[d + D386_FLAGS] | ((WORD)g_pif[d + D386_FLAGS + 1] << 8);
        v->video386 = g_pif[d + D386_VIDEO] | ((WORD)g_pif[d + D386_FLAGS + 2] << 8);
        v->hkscan = getw(d + D386_HKSCAN);
        v->hkmods = getw(d + D386_HKMODS);
        v->hotkey = (g_pif[d + D386_FLAGS + 1] & 0x40) != 0;
    }
    v->std286 = g_off286 ? getw(g_off286 + 4) : 0;
}

/* a text field into the file: the text and its NUL, then blanks from
   FROM bytes after its start (at least) to the field's end */
static void putstr(UINT off, UINT len, LPCSTR s, UINT from, BOOL nul)
{
    UINT n = lstrlen(s), i;
    if (n > len)
        n = len;
    _fmemcpy(g_pif + off, s, n);
    if (nul && n < len)
        g_pif[off + n] = 0;
    i = n + (nul ? 1 : 0);
    if (i < from)
        i = from;
    for (; i < len; i++)
        g_pif[off + i] = ' ';
}

void pif_put(const PIFVIEW FAR *v, BOOL enh)
{
    UINT d = g_off386;
    char up[65];
    putstr(0x02, 30, v->title, 0, FALSE);
    lstrcpy(up, v->program);
    AnsiUpper(up);
    putstr(0x24, 63, up, 0, TRUE);
    lstrcpy(up, v->dir);
    AnsiUpper(up);
    putstr(0x65, 64, up, 0, TRUE);
    if (!enh) {
        putstr(0xA5, 64, v->params, 2, TRUE);
        setw(0x22, (WORD)v->memreq);
        if ((WORD)v->memreq > getw(0x20))
            setw(0x20, (WORD)v->memreq);
        g_pif[0x63] = (BYTE)((v->f63 & ~0x10) | (v->closeset ? v->f63 & 0x10 : g_pif[0x63] & 0x10));
        if (v->f63 & 0x02)
            g_pif[0xED] |= 0x10;
        else
            g_pif[0xED] &= ~0x10;
        g_pif[0x16F] = (BYTE)v->f16f;
        g_pif[0x170] |= 0x40;
        if (g_off286) {
            setw(g_off286, (WORD)v->xmslim);
            setw(g_off286 + 2, (WORD)v->xmsreq);
            setw(g_off286 + 4, v->std286);
        }
    } else {
        if (v->closeset)
            g_pif[0x63] = (BYTE)((g_pif[0x63] & ~0x10) | (v->f63 & 0x10));
        if (d) {
            putstr(d + D386_PARAMS, 64, v->params, 2, TRUE);
            setw(d + D386_MIN, (WORD)v->memreq);
            setw(d + D386_MAX, (WORD)v->memdes);
            setw(d + D386_EMSLIM, (WORD)v->emslim);
            setw(d + D386_EMSREQ, (WORD)v->emsreq);
            setw(d + D386_XMSLIM, (WORD)v->xmslim);
            setw(d + D386_XMSREQ, (WORD)v->xmsreq);
            setw(d + D386_FG, (WORD)v->fgprio);
            setw(d + D386_BG, (WORD)v->bgprio);
            g_pif[d + D386_FLAGS] = (BYTE)v->flags386;
            g_pif[d + D386_FLAGS + 1] = (BYTE)((v->flags386 >> 8) & ~0x40);
            g_pif[d + D386_FLAGS + 2] = (BYTE)(v->video386 >> 8);
            g_pif[d + D386_VIDEO] = (BYTE)v->video386;
            if (v->hotkey) {
                g_pif[d + D386_FLAGS + 1] |= 0x40;
                setw(d + D386_HKSCAN, v->hkscan);
                setw(d + D386_HKMODS, v->hkmods);
                setw(d + D386_HKFLAG, 0x000F);
            }
        }
    }
}
