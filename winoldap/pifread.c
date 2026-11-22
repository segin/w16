/*
 * pifread.c - reading a .PIF as Windows 3.1's WINOLDAP.MOD (Standard
 * mode) and WINOA386.MOD (386 Enhanced mode) do: one reader for w16's two
 * modules (winoldap/, winoa386/). MIT licence (LICENSE).
 *
 * The format (ARCHITECTURE.md 15.11, shell/pifedit/pif.c's table): the
 * 171h-byte base section (Windows 2.x's PIF), then section headers from
 * 171h, each 16 bytes of name and three words: the next header's offset
 * (FFFFh: the last), the data's offset and its length. The first is
 * "MICROSOFT PIFEX"; 3.1's PIF Editor puts "WINDOWS 386 3.0" next and
 * "WINDOWS 286 3.0" last, Setup's DOSPRMPT.PIF and _DEFAULT.PIF the other
 * way round.
 *
 * How 3.1 reads one, observed black-box (tests/pifread.sh: DOSBox-X's file
 * log of each seek and read, VXS.386's record of what WINOA386 gives
 * SHELL, WO2's of what WINOLDAP gives WIN.COM's switcher, WC1.COM's of
 * what the program got; fixtures tests/scripts/pifread/mkpifs.py; the
 * modules themselves were never looked into):
 *   - It seeks to the end for the size. A file shorter than 171h bytes
 *     (WINOA386: of 171h bytes or less) has no headers to read (UNVERIFIED
 *     between 100h and 171h bytes). Else it reads the header at 171h; if
 *     that is
 *     "MICROSOFT PIFEX" it follows the next offsets, header by header,
 *     until it finds its mode's section, reaches FFFFh, or a read comes
 *     back short (a next past the end). The checksum (byte 1) is not
 *     looked at.
 *   - Its section found (the 386 one only with a length of 68h exactly:
 *     40h and 70h were refused; the 286 one's length was not varied), it
 *     reads its fields at the data offset the header
 *     gives, each read into the defaults, so a read past the end leaves
 *     them: the 386 data FFFFh/0 KB, priorities 100/50, EMS and XMS 1024/0,
 *     flags 08 10 02 (full screen, detect idle time, fast paste), video
 *     17h, parameters empty.
 *   - Not found (no headers, an unknown first header, no such section, a
 *     short one): the base section stands for it, as a Windows 2.x PIF:
 *     WINOA386 reads EDh, 16Fh, 63h, 20h (KB desired, required) and the
 *     base's parameters (A5h).
 *   - 386 Enhanced mode takes the parameters from the 386 section only:
 *     empty there is empty, never the base's (Standard mode's).
 *   - Then both read 63h, the program (24h), the start-up directory (65h)
 *     and the title (02h); Standard mode reads 20h (4), A5h, EDh, 16Fh too.
 * Text fields end at their first NUL and lose the blanks at their end,
 * but for the parameters, which keep them (all 64 bytes when there is no
 * NUL: the tail " PPP...P", 65 characters).
 */
#include <windows.h>
#include "pifread.h"

static const char sig_ex[] = "MICROSOFT PIFEX";
static const char sig_386[] = "WINDOWS 386 3.0";
static const char sig_286[] = "WINDOWS 286 3.0";

#define BASE_END    0x171
#define HDR_SIZE    22

static HFILE pf;

static UINT readat(LONG off, void *buf, UINT n)
{
    UINT r;
    if (_llseek(pf, off, 0) != off)
        return 0;
    r = _lread(pf, buf, n);
    return r == (UINT)-1 ? 0 : r;
}

/* a text field: to its first NUL, blanks at its end cut */
static void text(char *dst, UINT len)
{
    int n = 0;
    while ((UINT)n < len && dst[n])
        n++;
    while (n > 0 && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

static BOOL is_sig(const BYTE *h, const char *sig)
{
    int i;
    for (i = 0; sig[i]; i++)
        if (h[i] != (BYTE)sig[i])
            return FALSE;
    return h[i] == 0;
}

/* the data offset of the mode's section, 0 if not there */
static WORD find(const char *sig, WORD len)
{
    BYTE h[HDR_SIZE];
    WORD at;
    int guard;
    if (readat(BASE_END, h, HDR_SIZE) < HDR_SIZE || !is_sig(h, sig_ex))
        return 0;
    at = *(WORD *)(h + 16);
    /* (a loop in the chain: 3.1's WINOA386 never came back, its PIF
       read hung; w16 stops after 16 headers) */
    for (guard = 0; guard < 16 && at != 0xFFFF; guard++) {
        if (readat(at, h, HDR_SIZE) < HDR_SIZE)
            return 0;
        if (is_sig(h, sig))
            return !len || *(WORD *)(h + 20) == len ? *(WORD *)(h + 18) : 0;
        at = *(WORD *)(h + 16);
    }
    return 0;
}

static void d386_defaults(BYTE *d)
{
    int i;
    for (i = 0; i < D386_SIZE; i++)
        d[i] = 0;
    *(WORD *)(d + D386_MAX) = 0xFFFF;
    *(WORD *)(d + D386_FG) = 100;
    *(WORD *)(d + D386_BG) = 50;
    *(WORD *)(d + D386_EMSLIM) = 1024;
    *(WORD *)(d + D386_XMSLIM) = 1024;
    d[D386_F0] = 0x08;
    d[D386_F1] = 0x10;
    d[D386_F2] = 0x02;
    d[D386_VIDEO] = 0x17;
}

BOOL pif_read(const char *path, PIFDATA *p, int mode)
{
    OFSTRUCT of;
    LONG size;
    WORD d;
    BYTE *q = (BYTE *)p;
    UINT i;

    for (i = 0; i < sizeof(*p); i++)
        q[i] = 0;
    d386_defaults(p->d386);
    /* what a read past the end leaves (3.1's WINOA386 started a 100h-byte
       PIF full screen and not exclusive: 16Fh as the PIF Editor writes
       it, E0h; UNVERIFIED beyond that) */
    p->w_ed = 0x0007;
    p->w_16f = 0x20E0;
    pf = OpenFile(path, &of, OF_READ);
    if (pf == HFILE_ERROR)
        return FALSE;
    p->opened = TRUE;
    size = _llseek(pf, 0, 2);
    if (mode == PIF_386) {
        d = size > BASE_END ? find(sig_386, 0x68) : 0;
        if (d) {
            p->found = TRUE;
            readat(d, p->d386, 16);
            readat(d + D386_F0, p->d386 + D386_F0, 4);
            readat(d + D386_VIDEO, p->d386 + D386_VIDEO, 4);
            readat(d + 0x28, p->params, 64);
            readat(d + D386_HOTKEY, p->d386 + D386_HOTKEY, 16);
        } else {
            readat(0xED, &p->w_ed, 2);
            readat(0x16F, &p->w_16f, 2);
            readat(0x63, &p->flags, 1);
            readat(0x20, &p->maxmem, 4);
            readat(0xA5, p->params, 64);
        }
        readat(0x63, &p->flags, 1);
        readat(0x24, p->program, 63);
        readat(0x65, p->directory, 64);
        readat(0x02, p->title, 30);
    } else {
        d = size >= BASE_END ? find(sig_286, 0) : 0;
        if (d) {
            p->found = TRUE;
            readat(d, &p->xmslim, 4);
            readat(d + 4, &p->flags286, 2);
        }
        readat(0x63, &p->flags, 1);
        readat(0x20, &p->maxmem, 4);
        readat(0x24, p->program, 63);
        readat(0x65, p->directory, 64);
        readat(0xA5, p->params, 64);
        readat(0xED, &p->w_ed, 2);
        readat(0x16F, &p->w_16f, 2);
        readat(0x02, p->title, 30);
    }
    _lclose(pf);
    text(p->title, 30);
    text(p->program, 63);
    text(p->directory, 64);
    p->params[64] = 0;          /* (to the first NUL, blanks kept) */
    return TRUE;
}
