/*
 * pict.c - w16 Write: pictures and OLE objects in the text.
 *
 * A picture is a paragraph of its own whose PAP has bit 10h of byte 16
 * set; its "text" is a 40-byte header and the picture data, with no
 * CR LF (WR7). Header (WR7 for objects; the rest from the layout of
 * Write 3.0 pictures, UNVERIFIED until a 3.0 document is probed):
 *    0 mm         88h bitmap, E4h OLE object (WR7), else a metafile's
 *                 mapping mode
 *    2 xExt, 4 yExt  metafile extents
 *    8 dxaOffset  from the left indent
 *   10 dxaSize, 12 dyaSize   shown size, twips
 *   16 BITMAP (14 bytes) for bitmaps; for objects the data size (dword)
 *      and an object number (dword at 24) the original makes up per
 *      session
 *   30 cbHeader (40), 32 cbSize (dword, old pictures)
 *   36 mx, 38 my  scale, per mille (1000)
 * An object's data is OLECLI's stream (OleSaveToStream): version 0501h,
 * format (1 linked, 2 embedded, 3 static), then for a static object the
 * class name ("DIB", "METAFILEPICT", "BITMAP": dword length with NUL,
 * the name), width and height in HIMETRIC, the data size and the data;
 * embedded and linked objects have class, topic and item names, native
 * data, then a presentation object of the same shape (OLE 1.0's stream
 * layout as documented with OLE 1.0; the linked case UNVERIFIED).
 *
 * w16 draws what it can read itself: DIBs and metafiles (static or as
 * an object's presentation) and old bitmaps. Activating objects goes
 * through OLECLI (plan).
 */
#include "write.h"

static DWORD rd(DWORD cp, WORD n)
{
    DWORD v = 0;
    doc_get(&g_doc, cp, (char FAR *)&v, n);
    return v;
}

BOOL pict_read(DWORD cp, DWORD cpLim, PICT *p)
{
    m_set(p, 0, sizeof *p);
    if (cpLim < cp + 40)
        return FALSE;
    p->mm = (WORD)rd(cp, 2);
    p->xExt = (int)rd(cp + 2, 2);
    p->yExt = (int)rd(cp + 4, 2);
    p->dxaOffset = (int)rd(cp + 8, 2);
    p->dxaSize = (int)rd(cp + 10, 2);
    p->dyaSize = (int)rd(cp + 12, 2);
    p->mx = (WORD)rd(cp + 36, 2);
    p->my = (WORD)rd(cp + 38, 2);
    if (!p->mx)
        p->mx = 1000;
    if (!p->my)
        p->my = 1000;
    p->cpData = cp + (WORD)rd(cp + 30, 2);
    p->cbData = cpLim - p->cpData;
    if (p->mm == 0x88) {
        p->kind = PK_BITMAP;
        doc_get(&g_doc, cp + 16, (char FAR *)&p->bm, 14);
    } else if (p->mm == 0xE4) {
        DWORD c = p->cpData, fmt, n;
        int pass;
        char cls[16];
        if (rd(c, 4) != 0x0501)
            return TRUE;                /* unknown: shown as a frame */
        fmt = rd(c + 4, 4);
        c += 8;
        for (pass = 0; pass < 4; pass++) {
            n = rd(c, 4);               /* class name */
            m_set(cls, 0, sizeof cls);
            doc_get(&g_doc, c + 4, cls, n < sizeof cls ? n : sizeof cls - 1);
            c += 4 + n;
            if (fmt == 3 || pass > 0) {
                /* a presentation: width, height, size, data */
                if (!s_icmp(cls, "METAFILEPICT"))
                    p->kind = PK_MF;
                else if (!s_icmp(cls, "DIB"))
                    p->kind = PK_DIB;
                else if (!s_icmp(cls, "BITMAP"))
                    p->kind = PK_OBITMAP;
                p->cpData = c + 12;
                p->cbData = rd(c + 8, 4);
                if (p->kind == PK_MF) {
                    p->mfMM = (int)rd(p->cpData, 2);
                    p->xExt = (int)rd(p->cpData + 2, 2);
                    p->yExt = (int)rd(p->cpData + 4, 2);
                    p->cpData += 8;
                    p->cbData -= 8;
                }
                return TRUE;
            }
            /* embedded or linked: topic and item names, then for an
               embedded object the native data; then the presentation */
            n = rd(c, 4);
            c += 4 + n;
            n = rd(c, 4);
            c += 4 + n;
            if (fmt == 2) {
                n = rd(c, 4);
                c += 4 + n;
            } else {
                n = rd(c, 4);           /* linked: network name (UNVERIFIED) */
                c += 4 + n;
                c += 4;                 /* link update options (UNVERIFIED) */
            }
            if (rd(c, 4) != 0x0501)
                return TRUE;
            fmt = rd(c + 4, 4);
            c += 8;
            if (fmt == 0)
                return TRUE;            /* no presentation */
        }
    } else {
        p->kind = PK_MF;
        p->mfMM = p->mm;
    }
    return TRUE;
}

/* the picture's data as a global block */
static HGLOBAL data_block(const PICT *p)
{
    HMEM m;
    DWORD o;
    char buf[256];
    m.h = NULL;
    m.p = NULL;
    m.size = 0;
    if (!p->cbData || !hm_resize(&m, p->cbData))
        return NULL;
    for (o = 0; o < p->cbData;) {
        WORD k = (WORD)(p->cbData - o > sizeof buf ? sizeof buf : p->cbData - o);
        doc_get(&g_doc, p->cpData + o, buf, k);
        hm_put(&m, o, buf, k);
        o += k;
    }
    GlobalUnlock(m.h);
    return m.h;
}

void pict_draw(HDC dc, const PICT *p, int x, int y, int w, int h)
{
    HGLOBAL g = data_block(p);
    BOOL done = FALSE;
    if (g) {
        if (p->kind == PK_MF) {
            HMETAFILE mf = SetMetaFileBits(g);
            if (mf) {
                int save = SaveDC(dc);
                SetMapMode(dc, p->mfMM ? p->mfMM : MM_ANISOTROPIC);
                SetViewportOrg(dc, x, y);
                if (p->mfMM == MM_ANISOTROPIC || p->mfMM == MM_ISOTROPIC || !p->mfMM)
                    SetViewportExt(dc, w, h);
                IntersectClipRect(dc, 0, 0, 0x7FFF, 0x7FFF);
                PlayMetaFile(dc, mf);
                RestoreDC(dc, save);
                DeleteMetaFile(mf);     /* frees g too */
                g = NULL;
                done = TRUE;
            }
#ifndef W16_WIN2
        } else if (p->kind == PK_DIB) {
            BITMAPINFOHEADER FAR *bi = (BITMAPINFOHEADER FAR *)GlobalLock(g);
            int ncol = bi->biClrUsed ? (int)bi->biClrUsed :
                       bi->biBitCount <= 8 ? 1 << bi->biBitCount : 0;
            StretchDIBits(dc, x, y, w, h, 0, 0, (int)bi->biWidth, (int)bi->biHeight,
                          (BYTE FAR *)bi + bi->biSize + ncol * sizeof(RGBQUAD),
                          (BITMAPINFO FAR *)bi, DIB_RGB_COLORS, SRCCOPY);
            GlobalUnlock(g);
            done = TRUE;
#endif
        } else if (p->kind == PK_BITMAP) {
            BYTE FAR *bits = (BYTE FAR *)GlobalLock(g);
            HBITMAP bm = CreateBitmap(p->bm.bmWidth, p->bm.bmHeight, p->bm.bmPlanes,
                                      p->bm.bmBitsPixel, bits);
            if (bm) {
                HDC mdc = CreateCompatibleDC(dc);
                HBITMAP old = SelectObject(mdc, bm);
                SetStretchBltMode(dc, COLORONCOLOR);
                StretchBlt(dc, x, y, w, h, mdc, 0, 0, p->bm.bmWidth, p->bm.bmHeight, SRCCOPY);
                SelectObject(mdc, old);
                DeleteDC(mdc);
                DeleteObject(bm);
                done = TRUE;
            }
            GlobalUnlock(g);
        }
        if (g)
            GlobalFree(g);
    }
    if (!done) {                        /* what cannot be drawn: a frame */
        HBRUSH old = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, x, y, x + w, y + h);
        SelectObject(dc, old);
    }
}
