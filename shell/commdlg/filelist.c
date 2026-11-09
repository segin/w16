/*
 * filelist.c - w16 COMMDLG: the pictures and the owner-drawn lists of the
 * file dialogs.
 *
 * The picture strip (bitmap 576, ours: tools/mkcdart.py) has eight 16x16
 * cells: open folder, the current open folder, closed folder, floppy,
 * fixed disk, CD-ROM, network drive, RAM disk; its blue (0,0,255) shows
 * the list's background. Observed positions in the original (pixel
 * matches of its pictures in screenshots, tests/scripts/commdlg/open1):
 * a folder at depth n is 4 + 4n pixels into its item, the name 21 pixels
 * after the picture's left edge; items of the folder and drive lists are
 * 16 pixels high, file list items as high as the font; names drawn in
 * lower case, centred vertically.
 */
#include "commdlg.h"

static HBITMAP pics, pics_hi;           /* on the window / highlight colour */

#define PIC_W           16
#define PIC_H           16
#define KEY_RGB         RGB(0, 0, 255)

/* a bitmap resource of ours with one colour of its colour table replaced
   (the original exports this as LoadAlterBitmap; UNVERIFIED: its exact
   parameters, which nothing imports) */
HBITMAP CDAPI LoadAlterBitmap(int id, DWORD rgbReplace, DWORD rgbInstead)
{
    HRSRC r = FindResource(cd_inst, MAKEINTRESOURCE(id), RT_BITMAP);
    HGLOBAL h, copy;
    LPBITMAPINFOHEADER bi, b;
    RGBQUAD FAR *pal;
    HBITMAP bm = NULL;
    HDC dc;
    DWORD size;
    int ncol, i;
    if (!r)
        return NULL;
    h = LoadResource(cd_inst, r);
    if (!h)
        return NULL;
    bi = (LPBITMAPINFOHEADER)LockResource(h);
    size = SizeofResource(cd_inst, r);
    copy = GlobalAlloc(GMEM_MOVEABLE, size);
    b = copy ? (LPBITMAPINFOHEADER)GlobalLock(copy) : NULL;
    if (bi && b) {
        BYTE FAR *s = (BYTE FAR *)bi, FAR *d = (BYTE FAR *)b;
        DWORD k;
        for (k = 0; k < size; k++)
            d[(WORD)k] = s[(WORD)k];
        ncol = b->biBitCount <= 8 ? 1 << b->biBitCount : 0;
        if (b->biClrUsed)
            ncol = (int)b->biClrUsed;
        pal = (RGBQUAD FAR *)((BYTE FAR *)b + (WORD)b->biSize);
        for (i = 0; i < ncol; i++)
            if (RGB(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue) == rgbReplace) {
                pal[i].rgbRed = GetRValue(rgbInstead);
                pal[i].rgbGreen = GetGValue(rgbInstead);
                pal[i].rgbBlue = GetBValue(rgbInstead);
            }
        dc = GetDC(NULL);
        bm = CreateDIBitmap(dc, b, CBM_INIT, (BYTE FAR *)pal + ncol * sizeof(RGBQUAD),
                            (LPBITMAPINFO)b, DIB_RGB_COLORS);
        ReleaseDC(NULL, dc);
    }
    if (b)
        GlobalUnlock(copy);
    if (copy)
        GlobalFree(copy);
    if (bi)
        UnlockResource(h);
    FreeResource(h);
    return bm;
}

void pics_load(void)
{
    pics_free();
    pics = LoadAlterBitmap(BMP_DRIVES, KEY_RGB, GetSysColor(COLOR_WINDOW));
    pics_hi = LoadAlterBitmap(BMP_DRIVES, KEY_RGB, GetSysColor(COLOR_HIGHLIGHT));
}

void pics_free(void)
{
    if (pics)
        DeleteObject(pics);
    if (pics_hi)
        DeleteObject(pics_hi);
    pics = pics_hi = NULL;
}

static void draw_pic(HDC dc, int x, int y, int cell, BOOL hi)
{
    HDC mem;
    HBITMAP old;
    HBITMAP b = hi ? pics_hi : pics;
    if (!b)
        return;
    mem = CreateCompatibleDC(dc);
    if (!mem)
        return;
    old = SelectObject(mem, b);
    BitBlt(dc, x, y, PIC_W, PIC_H, mem, cell * PIC_W, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

/* ---- drives ---- */

/* MSCDEX: is drive d (0 = A:) a CD-ROM? */
static BOOL is_cdrom(int d)
{
    WORD rax = 0, rbx = 0;
    _asm {
        mov ax, 150Bh
        xor bx, bx
        mov cx, d
        int 2Fh
        mov rax, ax
        mov rbx, bx
    }
    return rbx == 0xADAD && rax != 0;
}

int drive_kind(int d)
{
    switch (GetDriveType(d)) {
    case DRIVE_REMOVABLE:
        return PIC_FLOPPY;
    case DRIVE_FIXED:
        return PIC_HARD;
    case DRIVE_REMOTE:
        return is_cdrom(d) ? PIC_CDROM : PIC_NET;
    }
    return -1;
}

/* ---- drawing ---- */

/* the dialog's font height (items: max of it and the pictures) */
void measure_item(HWND dlg, MEASUREITEMSTRUCT FAR *m)
{
    HDC dc = GetDC(dlg);
    HFONT f = (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0), of = NULL;
    TEXTMETRIC tm;
    if (f)
        of = SelectObject(dc, f);
    GetTextMetrics(dc, &tm);
    if (of)
        SelectObject(dc, of);
    ReleaseDC(dlg, dc);
    m->itemHeight = tm.tmHeight;
    if (m->CtlID == lst2 || m->CtlID == cmb2)
        if (m->itemHeight < PIC_H)
            m->itemHeight = PIC_H;
}

/* An item: its whole rectangle in the window or highlight colour, the
   picture `cell` at pic_x (-1: none), the text in lower case at text_x,
   centred; the focus rectangle around it all (observed). */
static void draw_item(const DRAWITEMSTRUCT FAR *d, int pic_x, int cell, int text_x, BOOL hi)
{
    char t[DOS_PATH + 4];
    TEXTMETRIC tm;
    int n;
    HBRUSH b;
    if (d->CtlType == ODT_COMBOBOX)
        SendMessage(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)(LPSTR)t);
    else
        SendMessage(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)(LPSTR)t);
    s_lower(t);
    n = s_len(t);
    b = CreateSolidBrush(GetSysColor(hi ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    FillRect(d->hDC, &d->rcItem, b);
    DeleteObject(b);
    if (pic_x >= 0)
        draw_pic(d->hDC, pic_x, d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - PIC_H) / 2,
                 cell, hi);
    GetTextMetrics(d->hDC, &tm);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, GetSysColor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    TextOut(d->hDC, text_x, d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - tm.tmHeight) / 2,
            t, n);
    if (d->itemState & ODS_FOCUS)
        DrawFocusRect(d->hDC, &d->rcItem);
}

void draw_file_item(const DRAWITEMSTRUCT FAR *d)
{
    if (d->itemID == (UINT)-1) {
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(d->hDC, &d->rcItem);
        return;
    }
    draw_item(d, -1, 0, d->rcItem.left + 4, (d->itemState & ODS_SELECTED) != 0);
}

/* the folder list shows its selection only while it has the focus */
void draw_dir_item(const DRAWITEMSTRUCT FAR *d)
{
    DWORD data;
    int x;
    if (d->itemID == (UINT)-1)
        return;
    data = SendMessage(d->hwndItem, LB_GETITEMDATA, d->itemID, 0);
    x = d->rcItem.left + 4 + 4 * LOWORD(data);
    draw_item(d, x, HIWORD(data), x + PIC_W + 4,
              (d->itemState & (ODS_SELECTED | ODS_FOCUS)) == (ODS_SELECTED | ODS_FOCUS));
}

void draw_drive_item(const DRAWITEMSTRUCT FAR *d)
{
    DWORD data;
    int x;
    if (d->itemID == (UINT)-1)
        return;
    data = SendMessage(d->hwndItem, CB_GETITEMDATA, d->itemID, 0);
    x = d->rcItem.left + 4;
    draw_item(d, x, HIWORD(data), x + PIC_W + 4, (d->itemState & ODS_SELECTED) != 0);
}
