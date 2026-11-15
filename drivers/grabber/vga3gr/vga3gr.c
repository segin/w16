/*
 * vga3gr.c - w16's VGA.3GR: the 386 Enhanced-mode display grabber that
 * WINOLDAP (WINOA386.MOD) loads from SYSTEM.INI [boot] 386grabber to show
 * a DOS box in a window and to copy its screen to the clipboard.
 *
 * Sources: the Windows 3.1 DDK documentation (Display Grabbers, the
 * Enhanced-mode grabber functions, EXTPAINTSTRUC, the VDD services), and
 * black-box observation of Windows 3.1 with Microsoft's VGA.3GR behind the
 * recorder GR3 (tests/apps/gr3.asm, tests/grabber3.sh): the arguments,
 * results and structure changes of every call, the VDD calls the grabber
 * makes and what the VDD returns, the GDI/USER/KERNEL calls it makes
 * (USTRACE), screenshots and clipboard contents (CB1). Nothing of
 * Microsoft's file was read but its NE header tables.
 * docs/notes/grabber-handover.md "VGA.3GR" has the interface as observed.
 *
 * The DOS box's screen comes from the VDD: VDD_Get_State (the mode, the
 * cursor, the palette registers), VDD_Get_Mod (what changed), VDD_Get_Mem
 * (linear addresses of the VM's video memory planes; text modes: the
 * characters and attributes as the CPU sees them at B800:0000, graphics:
 * one address per plane). The addresses are reached through a selector of
 * our own (SetSelectorBase).
 */
#include "grab.h"

static WORD flatsel[4];         /* data selectors for the VDD's memory */
static VREGS vr;
static WORD dbl[256];           /* a byte's bits each doubled */

/* the 64 colours of the EGA/VGA palette registers: bits 2,1,0 red, green,
   blue at 2/3, bits 5,4,3 the same at 1/3 (observed: attribute 07h with
   palette 07h is painted A8A8A8h, 38h 545454h, 3Fh FCFCFCh) */
static COLORREF egacolor(BYTE v)
{
    BYTE r = (BYTE)(((v & 4) ? 0xA8 : 0) + ((v & 0x20) ? 0x54 : 0));
    BYTE g = (BYTE)(((v & 2) ? 0xA8 : 0) + ((v & 0x10) ? 0x54 : 0));
    BYTE b = (BYTE)(((v & 1) ? 0xA8 : 0) + ((v & 0x08) ? 0x54 : 0));
    return RGB(r, g, b);
}

static const BYTE defpal[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};

int __far __cdecl grab_init(void)
{
    int i;
    for (i = 0; i < 4; i++)
        flatsel[i] = AllocSelector(SELECTOROF((void __far *)&vr));
    for (i = 0; i < 256; i++) {
        WORD w = 0;
        int b;
        for (b = 0; b < 8; b++)
            if (i & (1 << b))
                w |= 3 << (2 * b);
        dbl[i] = w;
    }
    return 1;
}

/* the VDD's memory at linear address a (64 KB from there) through
   selector i */
static void __far *flat(int i, DWORD a)
{
    SetSelectorBase(flatsel[i], a);
    SetSelectorLimit(flatsel[i], 0xFFFFL);
    return MAKELP(flatsel[i], 0);
}

static DWORD vdd(LPEPS p, DWORD fn, void __far *buf, WORD cx)
{
    vr.eax = fn;
    vr.ebx = p->ThisVMHand;
    vr.ecx = cx;
    vr.edx = 0;
    vr.esi = 0;
    vr.es = buf ? SELECTOROF(buf) : 0;
    vr.edi = buf ? OFFSETOF(buf) : 0;
    vdd_call(&vr);
    return vr.eax;
}

/* ---- the screen's geometry ---- */

static int is_text(BYTE mode)
{
    return mode <= 3 || mode == 7;
}

static int text_cols(LPEPS p)
{
    return p->state.mode <= 1 ? 40 : 80;
}

static int text_rows(LPEPS p)
{
    return p->state.rows ? p->state.rows : 25;
}

/* the font for a text mode: the four fonts GetFontList names come to
   WINOLDAP's slots AltFnt[0..3] as 80x25, 40x25, 80x43/50 and 40x43/50
   (EGA80WOA, EGA40WOA, CGA80WOA, CGA40WOA); when the user picks a font in
   the Font dialog, WINOLDAP puts that one in every slot */
static PNTFONT __far *text_font(LPEPS p)
{
    int i = (text_cols(p) == 40 ? 1 : 0) + (text_rows(p) > 25 ? 2 : 0);
    if (p->AltFnt[i].hFont)
        return &p->AltFnt[i];
    return &p->DefFont;
}

static void get_state(LPEPS p)
{
    vdd(p, VDD_GET_STATE, &p->state, sizeof p->state);
    p->EPStatusFlags |= EPS_STATEVALID;
}

/* ---- the caret (text modes) ---- */

/* the cursor's scan lines scaled to the font: top = ceil(start * FH /
   CH), height = (end - start + 1) * FH / CH + 1 (observed: 0Dh-0Eh of a
   16-line cell is top 10 height 2 with a 12-pixel font, 15 and 3 with an
   18-pixel one; 00h-0Fh is 0 and 13 with 12 pixels, one more than the
   cell) */
static void caret_shape(LPEPS p, int *top, int *height)
{
    int ch = text_rows(p) > 25 ? 8 : 16;
    int fh = p->FntHgt;
    int s = p->state.curstart & 0x1F, e = p->state.curend & 0x1F;
    if (e < s)
        e = s;
    *top = (s * fh + ch - 1) / ch;
    *height = (e - s + 1) * fh / ch + 1;
}

static DWORD caret_pos(LPEPS p)
{
    int top, h;
    caret_shape(p, &top, &h);
    return MAKELONG(p->state.curx * p->FntWid - p->ColOrg,
                    p->state.cury * p->FntHgt + top - p->RowOrg);
}

static void caret_update(LPEPS p)
{
    int top, h;
    DWORD pos;
    if (!is_text(p->state.mode)) {
        DestroyCaret();
        p->caretstart = p->caretend = 0xFFFF;
        return;
    }
    if ((BYTE)p->lastmode == 0xFF)
        return;                 /* CursorOff: no caret until CursorOn */
    if (p->caretstart != p->state.curstart || p->caretend != p->state.curend) {
        DestroyCaret();
        caret_shape(p, &top, &h);
        CreateCaret(p->WindHand, 0, p->FntWid, h);
        p->caretstart = p->state.curstart;
        p->caretend = p->state.curend;
    }
    p->lastmode = p->state.mode;
    pos = caret_pos(p);
    SetCaretPos(LOWORD(pos), HIWORD(pos));
}

/* ---- painting, text modes ---- */

static BYTE rowbuf[132];

/* cells c0..c1-1, rows r0..r1-1 of the text at vram (linear, character
   and attribute per cell) */
static void text_cells(LPEPS p, HDC hdc, BYTE __far *vram, int c0, int r0, int c1, int r1)
{
    int cols = text_cols(p), fw = p->FntWid, fh = p->FntHgt;
    int r, c, n;
    COLORREF fg, bg, curfg = (COLORREF)-1, curbg = (COLORREF)-1;
    BYTE __far *cell;
    BYTE a;
    for (r = r0; r < r1; r++) {
        cell = vram + 2 * (r * cols + c0);
        for (c = c0; c < c1; c += n) {
            a = cell[1];
            for (n = 0; c + n < c1 && cell[2 * n + 1] == a; n++)
                rowbuf[n] = cell[2 * n] ? cell[2 * n] : ' ';
            bg = egacolor(p->state.pal[a >> 4]);
            fg = egacolor(p->state.pal[a & 15]);
            if (bg != curbg)
                SetBkColor(hdc, curbg = bg);
            if (fg != curfg)
                SetTextColor(hdc, curfg = fg);
            TextOut(hdc, c * fw - p->ColOrg, r * fh - p->RowOrg, (LPCSTR)rowbuf, n);
            cell += 2 * n;
        }
    }
}

/* paint the client rectangle rc (client pixels) */
static void paint_text(LPEPS p, BYTE __far *vram, RECT *rc)
{
    HDC hdc = p->ps.hdc;
    PNTFONT __far *f = text_font(p);
    int cols = text_cols(p), rows = text_rows(p);
    int fw = p->FntWid, fh = p->FntHgt;
    int tx1 = cols * fw - p->ColOrg, ty1 = rows * fh - p->RowOrg;
    int c0, r0, c1, r1, ytop;
    HFONT ofont;
    HBRUSH br, obr;
    if (!fw || !fh)
        return;
    p->PGVDRect.l = rc->left + p->ColOrg;
    p->PGVDRect.t = rc->top + p->RowOrg;
    p->PGVDRect.r = rc->right + p->ColOrg;
    p->PGVDRect.b = rc->bottom + p->RowOrg;
    c0 = p->PGVDRect.l / fw;
    r0 = p->PGVDRect.t / fh;
    c1 = (p->PGVDRect.r + fw - 1) / fw;
    r1 = (p->PGVDRect.b + fh - 1) / fh;
    p->PTVDRect.l = c0;
    p->PTVDRect.t = r0;
    p->PTVDRect.r = c1;
    p->PTVDRect.b = r1;
    if (c1 > cols)
        c1 = cols;
    if (r1 > rows)
        r1 = rows;
    ofont = SelectObject(hdc, f->hFont);
    br = CreateSolidBrush(GetNearestColor(hdc, egacolor(p->state.pal[0])));
    obr = SelectObject(hdc, br);
    /* what lies beyond the DOS screen: the background */
    if (rc->bottom > ty1)
        PatBlt(hdc, rc->left, ty1, rc->right - rc->left, rc->bottom - ty1, PATCOPY);
    if (rc->right > tx1) {
        ytop = rc->bottom < ty1 ? rc->bottom : ty1;
        if (ytop > rc->top)
            PatBlt(hdc, tx1, rc->top, rc->right - tx1, ytop - rc->top, PATCOPY);
    }
    if (c0 < c1 && r0 < r1)
        text_cells(p, hdc, vram, c0, r0, c1, r1);
    SelectObject(hdc, obr);
    DeleteObject(br);
    SelectObject(hdc, ofont);
    p->TDXpos = c0 * fw - p->ColOrg;
    p->TDYpos = (r1 > r0 ? r1 : r0) * fh - p->RowOrg;
}

/* ---- painting, graphics modes ---- */

#define BANDBYTES 0x4240

/* the graphics modes as shown in a window (observed: SetPaintFnt answers
   640x400 for 04h-06h, 0Dh and 0Eh, 640x350 for 10h, 640x480 for 12h):
   the screen's pixels, how many times each is repeated across and down,
   and how the memory holds them */
#define K_PLANAR 0              /* four planes, a bit per pixel each */
#define K_CGA2   1              /* B800h, 2 bits a pixel, two banks */
#define K_CGA1   2              /* B800h, 1 bit a pixel, two banks */

typedef struct {
    int w, h, xs, ys, kind;
} GMODE;

static void gmode(BYTE mode, GMODE *g)
{
    g->xs = 1;
    g->ys = 1;
    g->kind = K_PLANAR;
    switch (mode) {
    case 0x04: case 0x05:
        g->w = 320; g->h = 200; g->xs = 2; g->ys = 2; g->kind = K_CGA2; break;
    case 0x06:
        g->w = 640; g->h = 200; g->ys = 2; g->kind = K_CGA1; break;
    case 0x0D: case 0x13:
        g->w = 320; g->h = 200; g->xs = 2; g->ys = 2; break;
    case 0x0E:
        g->w = 640; g->h = 200; g->ys = 2; break;
    case 0x0F: case 0x10:
        g->w = 640; g->h = 350; break;
    default:
        g->w = 640; g->h = 480; break;
    }
}

static void gfx_size(BYTE mode, int *w, int *h)
{
    GMODE g;
    gmode(mode, &g);
    *w = g.w * g.xs;
    *h = g.h * g.ys;
}

static int grbcolor(COLORREF rgb);           /* a byte's bits each doubled */
static BYTE linebuf[4][80];     /* one source line as four planes */

/* source line y as four planes of w bits (VGA plane order: bit 0 of a
   pixel value in plane 0) */
static const BYTE cga4[4] = { 0, 11, 13, 15 };

static void src_line(LPEPS p, DWORD __far *mem, GMODE *g, int y)
{
    int bpr = g->w / 8, i, x, v;
    BYTE __far *src;
    BYTE c;
    (void)p;
    if (g->kind == K_PLANAR) {
        for (i = 0; i < 4; i++) {
            src = (BYTE __far *)flat(i, mem[i] + mem[4]) + (WORD)y * bpr;
            for (x = 0; x < bpr; x++)
                linebuf[i][x] = src[x];
        }
        return;
    }
    src = (BYTE __far *)flat(0, mem[0] + mem[4]) + (y & 1) * 0x2000 + (y >> 1) * 80;
    for (i = 0; i < 4; i++)
        for (x = 0; x < bpr; x++)
            linebuf[i][x] = 0;
    for (x = 0; x < g->w; x++) {
        if (g->kind == K_CGA2) {
            c = src[x >> 2];
            v = (c >> (6 - 2 * (x & 3))) & 3;
        } else {
            v = (src[x >> 3] >> (7 - (x & 7))) & 1;
        }
        /* the colours the CGA BIOS gives these modes (mode 4/5:
           black, light cyan, light magenta, white; mode 6: black,
           white), as Microsoft's grabber shows them, whatever the
           palette registers hold */
        v = g->kind == K_CGA2 ? cga4[v] : (v ? 15 : 0);
        for (i = 0; i < 4; i++)
            if (v & (1 << i))
                linebuf[i][x >> 3] |= 0x80 >> (x & 7);
    }
}

/* each band of scan lines goes to a bitmap in the display's own format (a
   scan line is its four planes one after the other, red, green, blue,
   intensity: VGA.DRV's colour bitmaps) and from there to the window. The
   planes of the 4-plane modes are copied as they are, without the DOS
   application's palette (as Microsoft's grabber shows them). */
static const BYTE slot[4] = { 2, 1, 0, 3 };     /* VGA plane -> bitmap */

static void paint_bands(LPEPS p, DWORD __far *mem, GMODE *g, int y0, int y1, RECT *rc)
{
    HDC hdc = p->ps.hdc, mdc;
    int ow = g->w * g->xs, oh = g->h * g->ys, obpr = ow / 8, bpr = g->w / 8;
    int band, y, n, k, i, x, sy, last = -1;
    BYTE __far *bits;
    BYTE __far *d;
    HBITMAP ob;
    band = BANDBYTES / (obpr * 4);
    if (y1 > oh)
        y1 = oh;
    if (!p->hMemDC) {
        p->hMemDC = CreateCompatibleDC(hdc);
        p->hBitmap = CreateCompatibleBitmap(hdc, ow, band);
        p->hBits = GlobalAlloc(GMEM_MOVEABLE, (DWORD)BANDBYTES);
        p->lastmode = 0x1200 | p->state.mode;
    }
    if (!p->hMemDC || !p->hBitmap || !p->hBits)
        return;
    ob = SelectObject(p->hMemDC, p->hBitmap);
    bits = GlobalLock(p->hBits);
    mdc = p->hMemDC;
    for (y = y0; y < y1; y += n) {
        n = y1 - y < band ? y1 - y : band;
        for (k = 0; k < n; k++) {
            sy = (y + k) / g->ys;
            if (sy != last) {
                src_line(p, mem, g, sy);
                last = sy;
            }
            for (i = 0; i < 4; i++) {
                d = bits + (k * 4 + slot[i]) * obpr;
                if (g->xs == 2)
                    for (x = 0; x < bpr; x++) {
                        *d++ = (BYTE)(dbl[linebuf[i][x]] >> 8);
                        *d++ = (BYTE)dbl[linebuf[i][x]];
                    }
                else
                    for (x = 0; x < bpr; x++)
                        *d++ = linebuf[i][x];
            }
        }
        SetBitmapBits(p->hBitmap, (DWORD)(WORD)(n * obpr * 4), bits);
        BitBlt(hdc, rc->left, y - p->RowOrg, rc->right - rc->left, n, mdc,
               rc->left + p->ColOrg, 0, SRCCOPY);
    }
    GlobalUnlock(p->hBits);
    SelectObject(mdc, ob);
}

static void free_gfx(LPEPS p)
{
    if (p->hMemDC)
        DeleteDC(p->hMemDC);
    if (p->hBitmap)
        DeleteObject(p->hBitmap);
    if (p->hBits)
        GlobalFree(p->hBits);
    p->hMemDC = p->hBitmap = p->hBits = 0;
}

static void paint_gfx(LPEPS p, DWORD __far *mem, RECT *rc)
{
    GMODE g;
    int y0, y1;
    gmode(p->state.mode, &g);
    if ((BYTE)p->lastmode != p->state.mode)
        free_gfx(p);
    y0 = rc->top + p->RowOrg;
    y1 = rc->bottom + p->RowOrg;
    if (y0 < 0)
        y0 = 0;
    if (p->state.mode != 0x13)
        paint_bands(p, mem, &g, y0, y1, rc);
    p->PGVDRect.l = rc->left + p->ColOrg;
    p->PGVDRect.t = rc->top + p->RowOrg;
    p->PGVDRect.r = rc->right + p->ColOrg;
    p->PGVDRect.b = rc->bottom + p->RowOrg;
}

/* VDD_Get_Mem's buffer: the planes' linear addresses, a start offset (0
   in every case seen), the planes' sizes, one more address */
static DWORD membuf[10];

static void paint(LPEPS p, RECT *rc)
{
    if (is_text(p->state.mode))
        paint_text(p, (BYTE __far *)flat(0, membuf[0] + membuf[4]), rc);
    else
        paint_gfx(p, membuf, rc);
}

/* ---- the exports ---- */

/* 16: CheckGRBVersion(lpPntStruc): the VDD's version and identifier
   ("VIDEOVGA", copied by the VDD to VerStrng); DX = the VDD's version,
   AX = 0 (compatible), 1 (no VDD or not version 3) or 2 (another
   display's VDD; DX then the grabber's version) */
DWORD EXPORT CHECKGRBVERSION(LPEPS p)
{
    static const char id[8] = "VIDEOVGA";
    DWORD v;
    int i;
    if (!vdd_find())
        return 1;
    v = vdd(p, VDD_GET_VERSION, p->VerStrng, 8) & 0xFFFF;
    if ((v >> 8) != 3)
        return MAKELONG(1, v);
    for (i = 0; i < 8; i++)
        if (p->VerStrng[i] != id[i])
            return MAKELONG(2, 0x030A);
    return MAKELONG(0, v);
}

/* 10: GetFontList(lpFontBuf): the four fonts for windowed text, each
   13 bytes of file name (SYSTEM.INI [386Enh], the name itself as the key
   and the default), then its cell height and width */
static const struct {
    char name[13];
    WORD h, w;
} fontdef[4] = {
    { "EGA80WOA.FON", 12, 8 }, { "EGA40WOA.FON", 12, 16 },
    { "CGA80WOA.FON", 8, 8 }, { "CGA40WOA.FON", 8, 16 }
};

DWORD EXPORT GETFONTLIST(LPSTR buf)
{
    int i, n = 0;
    for (i = 0; i < 4; i++, buf += 17) {
        n = GetPrivateProfileString("386Enh", fontdef[i].name, fontdef[i].name,
                                    buf, 13, "SYSTEM.INI");
        *(WORD __far *)(buf + 13) = fontdef[i].h;
        *(WORD __far *)(buf + 15) = fontdef[i].w;
    }
    return (DWORD)n;
}

/* 11: SetPaintFnt(lpPntStruc, lpWidFullScr, lpHeightFullScr): the
   font for text modes (FntHgt, FntWid; 0 in graphics modes); the full
   screen's size in pixels to the two words (observed: 640 and 300 for
   80x25 with an 8x12 font, not the cells the DDK describes); DX:AX the
   size in cells (text) or pixels (graphics) */
DWORD EXPORT SETPAINTFNT(LPEPS p, int __far *lpw, int __far *lph)
{
    PNTFONT __far *f;
    int w, h, pw, ph;
    if (!(p->EPStatusFlags & EPS_STATEVALID))
        get_state(p);
    if (is_text(p->state.mode)) {
        f = text_font(p);
        p->FntHgt = f->height;
        p->FntWid = f->width;
        w = text_cols(p);
        h = text_rows(p);
        pw = w * p->FntWid;
        ph = h * p->FntHgt;
    } else {
        p->FntHgt = p->FntWid = 0;
        gfx_size(p->state.mode, &w, &h);
        pw = w;
        ph = h;
    }
    *lpw = pw;
    *lph = ph;
    return MAKELONG(w, h);
}

/* 18: GetDisplayUpd(lpPntStruc, wParam, lParam): what changed, from
   VDD_Get_Mod and VDD_Get_State; the VM stays stopped until PaintScreen,
   UpdateScreen or GrbUnLockApp */
DWORD EXPORT GETDISPLAYUPD(LPEPS p, WORD wParam, DWORD lParam)
{
    DWORD a, b;
    WORD f;
    (void)wParam;
    (void)lParam;
    vdd(p, VDD_GET_MOD, &p->mod, 0x100);
    a = vr.eax;
    b = vr.ebx;
    get_state(p);
    if (is_text(p->state.mode)) {
        p->mod.left = (BYTE)a;
        p->mod.top = (BYTE)(a >> 8);
        p->mod.right = (BYTE)b;
        p->mod.bottom = (BYTE)(b >> 8);
    }
    f = p->mod.flags;
    if (p->state.flags & 2)
        f |= fDisp_HCurTrack;
    return MAKELONG(f, LOWORD(vr.edx));
}

static void client_rect(LPEPS p, RECT *rc)
{
    rc->left = p->ps.rcPaint.left;
    rc->top = p->ps.rcPaint.top;
    rc->right = p->ps.rcPaint.right;
    rc->bottom = p->ps.rcPaint.bottom;
}

/* 1: PaintScreen(lpPntStruc): paint rcPaint */
WORD EXPORT PAINTSCREEN(LPEPS p)
{
    RECT rc;
    vdd(p, VDD_GET_MEM, membuf, 0x28);
    client_rect(p, &rc);
    paint(p, &rc);
    vdd(p, VDD_FREE_MEM, 0, 0);
    caret_update(p);
    return 1;
}

/* 12: UpdateScreen(lpPntStruc, wParam, lParam): paint what GetDisplayUpd
   found changed */
WORD EXPORT UPDATESCREEN(LPEPS p, WORD wParam, DWORD lParam)
{
    RECT rc, cl;
    int fw = p->FntWid, fh = p->FntHgt, first, last, i, bpr;
    BYTE __far *bits;
    GMODE g;
    (void)wParam;
    (void)lParam;
    gmode(p->state.mode, &g);
    vdd(p, VDD_GET_MEM, membuf, 0x28);
    client_rect(p, &cl);
    if (p->mod.flags & (fDisp_VRAM | fDisp_Ctlr)) {
        rc = cl;
        if (is_text(p->state.mode)) {
            if (!(p->mod.flags & fDisp_Ctlr) && (BYTE)p->mod.left != 0xFF && fw && fh) {
                rc.left = p->mod.left * fw - p->ColOrg;
                rc.top = p->mod.top * fh - p->RowOrg;
                rc.right = (p->mod.right + 1) * fw - p->ColOrg;
                rc.bottom = (p->mod.bottom + 1) * fh - p->RowOrg;
                IntersectRect(&rc, &rc, &cl);
            }
        } else if (!(p->mod.flags & fDisp_Ctlr) && p->mod.count && g.kind == K_PLANAR) {
            /* a bit per 4 KB page of a plane */
            bpr = g.w / 8;
            bits = (BYTE __far *)&p->mod.left;
            first = -1;
            last = -1;
            for (i = 0; i < (int)p->mod.count && i < 64; i++)
                if (bits[i >> 3] & (1 << (i & 7))) {
                    if (first < 0)
                        first = i;
                    last = i;
                }
            if (first < 0) {
                rc.bottom = rc.top;
            } else {
                /* 4096 / bpr scan lines a page: bpr is 40 or 80 */
                rc.top = (int)((WORD)first * 512u / (WORD)(bpr / 8)) * g.ys - p->RowOrg;
                rc.bottom = (int)((WORD)(last + 1) * 512u / (WORD)(bpr / 8) + 1) * g.ys - p->RowOrg;
                IntersectRect(&rc, &rc, &cl);
            }
        }
        if (!IsRectEmpty(&rc)) {
            p->ps.rcPaint = rc;
            paint(p, &rc);
            if (is_text(p->state.mode)) {
                p->mod.left = rc.left + p->ColOrg;
                p->mod.top = rc.top + p->RowOrg;
                p->mod.right = rc.right + p->ColOrg;
                p->mod.bottom = rc.bottom + p->RowOrg;
            }
        }
    }
    vdd(p, VDD_CLEAR_MOD, 0, 0);
    vdd(p, VDD_FREE_MEM, 0, 0);
    caret_update(p);
    return 1;
}

/* 22: GrbUnLockApp(lpPntStruc): let the VM run again (VDD_Unlock_APP) */
void EXPORT GRBUNLOCKAPP(LPEPS p)
{
    vdd(p, VDD_UNLOCK_APP, 0, 0);
}

/* 2: ScreenFree(lpPntStruc): the DOS box has ended */
void EXPORT SCREENFREE(LPEPS p)
{
    free_gfx(p);
}

/* 13: CursorOn(lpPntStruc) */
DWORD EXPORT CURSORON(LPEPS p)
{
    if (!is_text(p->state.mode))
        return 0xFFFF;
    p->lastmode = p->state.mode;
    p->caretstart = p->caretend = 0xFFFE;
    caret_update(p);
    return 0;
}

/* 15: CursorOff(lpPntStruc) */
WORD EXPORT CURSOROFF(LPEPS p)
{
    if (!is_text(p->state.mode))
        return 0;
    p->caretstart = p->caretend = 0xFFFF;
    p->lastmode = 0x00FF;
    DestroyCaret();
    return 1;
}

/* 14: CursorPosit(lpPntStruc): the caret's position in the client area,
   x in AX and y in DX; -1 without a caret (graphics modes) */
DWORD EXPORT CURSORPOSIT(LPEPS p)
{
    if (!is_text(p->state.mode) || !p->FntHgt)
        return 0xFFFFFFFFL;
    return caret_pos(p);
}

/* 21: GrbGetTextColor(lpPntStruc, rgb): the nearest of the 16 colours of
   the default palette (observed: FFFFFFh 0Fh, 0 0) */
DWORD EXPORT GRBGETTEXTCOLOR(LPEPS p, DWORD rgb)
{
    (void)p;
    return (DWORD)grbcolor(rgb);
}

static int grbcolor(COLORREF rgb)
{
    int i, best = 0, d, bd = 0x7FFF;
    COLORREF c;
    for (i = 0; i < 16; i++) {
        int dr, dg, db;
        c = egacolor(defpal[i]);
        dr = (int)GetRValue(c) - GetRValue(rgb);
        dg = (int)GetGValue(c) - GetGValue(rgb);
        db = (int)GetBValue(c) - GetBValue(rgb);
        d = (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* 23: TranslateWinToFScreen(lpPntStruc): nothing to translate for VGA
   (the windowed screen is 1:1) */
DWORD EXPORT TRANSLATEWINTOFSCREEN(LPEPS p)
{
    return SELECTOROF(p);
}

/* ---- selection: points in screen pixels, on cell boundaries in text
   modes ---- */

static void sel_limits(LPEPS p, int *w, int *h, int *sw, int *sh)
{
    if (is_text(p->state.mode) && p->FntWid && p->FntHgt) {
        *sw = p->FntWid;
        *sh = p->FntHgt;
        *w = text_cols(p) * *sw;
        *h = text_rows(p) * *sh;
    } else {
        *sw = 8;
        *sh = 8;
        gfx_size(p->state.mode, w, h);
    }
}

/* 3: BeginSelection(lpPntStruc, Y, X) */
DWORD EXPORT BEGINSELECTION(LPEPS p, int y, int x)
{
    p->SelctSRect.l = p->SelctSRect.r = x;
    p->SelctSRect.t = p->SelctSRect.b = y;
    p->GrabArea = p->SelctSRect;
    p->SelecFlags |= SELECTON;
    return MAKELONG(x, y);
}

/* 9: EndSelection(lpPntStruc) */
void EXPORT ENDSELECTION(LPEPS p)
{
    p->SelecFlags &= ~SELECTON;
}

/* 4: KeySelection(lpPntStruc, StartType, Mfunc): the start point (Shift
   up) or the end point (Shift down) moved one cell right, left, down or
   up (Mfunc 0-3); the structure is not changed */
DWORD EXPORT KEYSELECTION(LPEPS p, int shift, int dir)
{
    int w, h, sw, sh, x, y, xmax, ymax;
    sel_limits(p, &w, &h, &sw, &sh);
    if (shift) {
        x = p->GrabArea.r;
        y = p->GrabArea.b;
        xmax = w;
        ymax = h;
    } else {
        x = p->GrabArea.l;
        y = p->GrabArea.t;
        xmax = w - sw;
        ymax = h - sh;
    }
    switch (dir) {
    case 0: x += sw; break;
    case 1: x -= sw; break;
    case 2: y += sh; break;
    case 3: y -= sh; break;
    }
    if (x > xmax)
        x = xmax;
    if (y > ymax)
        y = ymax;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    return MAKELONG(x, y);
}

/* 17: AdjustInitEndPt(lpPntStruc, Y, X): the first end point encloses
   the cell at (X, Y) */
DWORD EXPORT ADJUSTINITENDPT(LPEPS p, int y, int x)
{
    int w, h, sw, sh;
    sel_limits(p, &w, &h, &sw, &sh);
    x += sw;
    y += sh;
    if (x > w)
        x = w;
    if (y > h)
        y = h;
    return MAKELONG(x, y);
}

/* 5: MakeSelctRect(lpPntStruc, Y, X): the new end point, on a cell
   boundary and within the screen; AX 1 if the selection changed, DX Y */
DWORD EXPORT MAKESELCTRECT(LPEPS p, int y, int x)
{
    int w, h, sw, sh, changed;
    sel_limits(p, &w, &h, &sw, &sh);
    if (x > w)
        x = w;
    if (y > h)
        y = h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    x -= x % sw;
    y -= y % sh;
    changed = p->GrabArea.r != x || p->GrabArea.b != y;
    p->GrabArea.r = p->SelctSRect.r = x;
    p->GrabArea.b = p->SelctSRect.b = y;
    return MAKELONG(changed, y);
}

/* 8: ConsSelecRec(lpPntStruc): the selection rectangle from GrabArea */
DWORD EXPORT CONSSELECREC(LPEPS p)
{
    p->SelctSRect = p->GrabArea;
    return MAKELONG(p->GrabArea.b, p->GrabArea.b);
}

/* 6: InvertSelection(lpPntStruc): WINOLDAP inverts the rectangle itself;
   the "active" end point */
DWORD EXPORT INVERTSELECTION(LPEPS p)
{
    return MAKELONG(p->SelctSRect.r, p->SelctSRect.b);
}

/* 7: RenderSelection(lpPntStruc, wParam, lParam): the selection as
   CF_OEMTEXT (text modes: each line without its trailing blanks, CR LF
   after every line, NUL at the end) or CF_BITMAP; DX the format, AX the
   handle. wParam -1: WINOLDAP's Copy of a windowed screen; otherwise a
   screen grab (Alt+PrintScreen in full screen): the grabbed copy
   (VDD_Get_GrbState, VDD_Get_GrbMem) */
static HBITMAP render_bitmap(LPEPS p, int x0, int y0, int x1, int y1);

DWORD EXPORT RENDERSELECTION(LPEPS p, WORD wParam, DWORD lParam)
{
    int x0 = p->SelctSRect.l, y0 = p->SelctSRect.t;
    int x1 = p->SelctSRect.r, y1 = p->SelctSRect.b, t;
    int grab = wParam != 0xFFFF;
    int c0, r0, c1, r1, cols, r, c, n;
    HGLOBAL h;
    BYTE __far *s;
    BYTE __far *vram;
    DWORD res;
    (void)lParam;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 == x1 || y0 == y1)
        return 0;
    if (grab) {
        vdd(p, VDD_GET_GRBSTATE, &p->state, sizeof p->state);
        vdd(p, VDD_GET_GRBMEM, membuf, 0x28);
        p->GGrbArea[4] = 1;
    } else {
        get_state(p);
        vdd(p, VDD_GET_MEM, membuf, 0x28);
    }
    if (!is_text(p->state.mode)) {
        HBITMAP hb = render_bitmap(p, x0, y0, x1, y1);
        if (!grab)
            vdd(p, VDD_FREE_MEM, 0, 0);
        return hb ? MAKELONG(hb, CF_BITMAP) : 0xFFFF0000L;
    }
    vram = (BYTE __far *)flat(0, membuf[0] + membuf[4]);
    cols = text_cols(p);
    c0 = x0 / p->FntWid;
    r0 = y0 / p->FntHgt;
    c1 = x1 / p->FntWid;
    r1 = y1 / p->FntHgt;
    p->SelCells[0] = (BYTE)c0;
    p->SelCells[1] = (BYTE)r0;
    p->SelCells[2] = (BYTE)c1;
    p->SelCells[3] = (BYTE)r1;
    p->SelCells[4] = (BYTE)(c1 - c0);
    p->SelCells[5] = (BYTE)(r1 - r0);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE,
                    (DWORD)(WORD)((r1 - r0) * (c1 - c0 + 2) + 1));
    if (!h) {
        res = 0xFFFF0000L;
    } else {
        s = GlobalLock(h);
        for (r = r0; r < r1; r++) {
            BYTE __far *cell = vram + 2 * (r * cols + c0);
            for (n = c1 - c0; n > 0 && (cell[2 * (n - 1)] == ' ' || !cell[2 * (n - 1)]); n--)
                ;
            for (c = 0; c < n; c++)
                *s++ = cell[2 * c] ? cell[2 * c] : ' ';
            *s++ = '\r';
            *s++ = '\n';
        }
        *s = 0;
        GlobalUnlock(h);
        res = MAKELONG(h, CF_OEMTEXT);
    }
    if (!grab)
        vdd(p, VDD_FREE_MEM, 0, 0);
    return res;
}

/* a graphics selection as a bitmap: each line as a 4-bit DIB line whose
   colour table is the palette registers' colours (Microsoft's grabber:
   CreateCompatibleBitmap, then SetDIBits a line at a time) */
static struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[16];
} dib;
static BYTE dibline[320];

static HBITMAP render_bitmap(LPEPS p, int x0, int y0, int x1, int y1)
{
    HDC hdc = GetDC(p->WindHand), mdc;
    HBITMAP hb, ob = 0;
    GMODE g;
    int w = x1 - x0, h = y1 - y0, y, x, sx, v, i;
    COLORREF c;
    if (!hdc)
        return 0;
    gmode(p->state.mode, &g);
    hb = CreateCompatibleBitmap(hdc, w, h);
    mdc = CreateCompatibleDC(hdc);
    if (hb && mdc) {
        ob = SelectObject(mdc, hb);
        dib.h.biSize = sizeof dib.h;
        dib.h.biWidth = w;
        dib.h.biHeight = h;
        dib.h.biPlanes = 1;
        dib.h.biBitCount = 4;
        dib.h.biCompression = BI_RGB;
        for (i = 0; i < 16; i++) {
            /* CGA modes: src_line gives the display's colours already */
            c = egacolor(g.kind == K_PLANAR ? p->state.pal[i] : defpal[i]);
            dib.c[i].rgbRed = GetRValue(c);
            dib.c[i].rgbGreen = GetGValue(c);
            dib.c[i].rgbBlue = GetBValue(c);
            dib.c[i].rgbReserved = 0;
        }
        for (y = 0; y < h; y++) {
            src_line(p, membuf, &g, (y + y0) / g.ys);
            for (x = 0; x < (int)sizeof dibline; x++)
                dibline[x] = 0;
            for (x = 0; x < w && x < 640; x++) {
                sx = (x + x0) / g.xs;
                v = 0;
                for (i = 0; i < 4; i++)
                    if (linebuf[i][sx >> 3] & (0x80 >> (sx & 7)))
                        v |= 1 << i;
                dibline[x >> 1] |= (x & 1) ? v : v << 4;
            }
            SetDIBits(mdc, hb, h - 1 - y, 1, dibline, (BITMAPINFO __far *)&dib, DIB_RGB_COLORS);
        }
        SelectObject(mdc, ob);
    }
    if (mdc)
        DeleteDC(mdc);
    ReleaseDC(p->WindHand, hdc);
    return hb;
}

/* 19: GrabEvent(lpPntStruc, wParam, lParam): not called in any case seen */
void EXPORT GRABEVENT(LPEPS p, WORD wParam, DWORD lParam)
{
    (void)p;
    (void)wParam;
    (void)lParam;
}

/* 20: GrabComplete(lpPntStruc, wParam, lParam): the grab is copied; the
   VDD frees its copy (VDD_Free_Grab) */
void EXPORT GRABCOMPLETE(LPEPS p, WORD wParam, DWORD lParam)
{
    (void)wParam;
    (void)lParam;
    vdd(p, VDD_FREE_GRAB, 0, 0);
    p->GGrbArea[4] = 0;
}
