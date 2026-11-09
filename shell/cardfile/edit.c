/*
 * edit.c - w16 Cardfile: the Edit menu on the front card: Undo, Cut,
 * Copy, Paste, Restore, and moving the picture in Picture mode.
 *
 * In Text mode the commands go to the card's Edit control. In Picture
 * mode (Edit > Picture) they work on the card's picture: on Windows 3.1
 * an OLE object (a pasted bitmap becomes a static object, CF6); in the
 * 2.x build a bitmap, as Cardfile 2.x and 3.0 kept them.
 */
#include "cardfile.h"

void undo_forget(void)
{
    SendMessage(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
}

void edit_undo(void)
{
    if (!g_picmode)
        SendMessage(g_edit, WM_UNDO, 0, 0);
}

/* Restore: the card as it was when it came to the front */
void edit_restore(void)
{
    g_carddirty = FALSE;
    SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
    body_load(g_cur);
    InvalidateRect(g_edit, NULL, TRUE);
}

static void pic_changed(void)
{
    g_carddirty = TRUE;
    InvalidateRect(g_edit, NULL, TRUE);
}

/* ---- the picture on the clipboard ---- */

static void pic_copy(void)
{
    if (g_pic.kind == PIC_BITMAP && g_pic.bmp) {
        HDC dc = GetDC(NULL), a = CreateCompatibleDC(dc), b = CreateCompatibleDC(dc);
        HBITMAP copy = CreateBitmap(g_pic.cx, g_pic.cy, 1, 1, NULL);
        ReleaseDC(NULL, dc);
        if (copy) {
            HBITMAP oa = SelectObject(a, g_pic.bmp), ob = SelectObject(b, copy);
            BitBlt(b, 0, 0, g_pic.cx, g_pic.cy, a, 0, 0, SRCCOPY);
            SelectObject(a, oa);
            SelectObject(b, ob);
            if (OpenClipboard(g_frame)) {
                EmptyClipboard();
                SetClipboardData(CF_BITMAP, copy);
                CloseClipboard();
            } else {
                DeleteObject(copy);
            }
        }
        DeleteDC(a);
        DeleteDC(b);
    }
#ifndef W16_WIN2
    else if (g_pic.kind == PIC_OBJECT) {
        ole_copy(&g_pic);
    }
#endif
}

#ifdef W16_WIN2
/* the 2.x build keeps pictures as monochrome bitmaps, as 2.x did */
static BOOL pic_paste_bitmap(void)
{
    HBITMAP src, mono;
    BITMAP bm;
    HDC dc, a, b;
    if (!IsClipboardFormatAvailable(CF_BITMAP) || !OpenClipboard(g_frame)) {
        msg_box(IDS_NOPICCLIP, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    src = GetClipboardData(CF_BITMAP);
    GetObject(src, sizeof bm, (LPSTR)&bm);
    mono = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
    dc = GetDC(NULL);
    a = CreateCompatibleDC(dc);
    b = CreateCompatibleDC(dc);
    ReleaseDC(NULL, dc);
    if (mono) {
        HBITMAP oa = SelectObject(a, src), ob = SelectObject(b, mono);
        BitBlt(b, 0, 0, bm.bmWidth, bm.bmHeight, a, 0, 0, SRCCOPY);
        SelectObject(a, oa);
        SelectObject(b, ob);
    }
    DeleteDC(a);
    DeleteDC(b);
    CloseClipboard();
    if (!mono)
        return FALSE;
    pic_free(&g_pic);
    g_pic.kind = PIC_BITMAP;
    g_pic.bmp = mono;
    g_pic.cx = bm.bmWidth;
    g_pic.cy = bm.bmHeight;
    return TRUE;
}
#endif

void edit_cut(void)
{
    if (!g_picmode) {
        SendMessage(g_edit, WM_CUT, 0, 0);
        return;
    }
    if (g_pic.kind == PIC_NONE)
        return;
    pic_copy();
    pic_free(&g_pic);
    pic_changed();
}

void edit_copy(void)
{
    if (!g_picmode)
        SendMessage(g_edit, WM_COPY, 0, 0);
    else
        pic_copy();
}

void edit_paste(void)
{
    if (!g_picmode) {
        /* text that does not fit the card is refused (string 61) */
        WORD before = GetWindowTextLength(g_edit);
        DWORD sel = SendMessage(g_edit, EM_GETSEL, 0, 0);
        DWORD len = 0;
        if (OpenClipboard(g_frame)) {
            HGLOBAL h = GetClipboardData(CF_TEXT);
            if (h) {
                LPSTR p = GlobalLock(h);
                len = s_len(p);
                GlobalUnlock(h);
            }
            CloseClipboard();
            if (!h) {
                msg_box(IDS_NOTEXTCLIP, NULL, MB_OK | MB_ICONEXCLAMATION);
                return;
            }
        }
        SendMessage(g_edit, WM_PASTE, 0, 0);
        if (len && GetWindowTextLength(g_edit) - before + (HIWORD(sel) - LOWORD(sel)) < len) {
            SendMessage(g_edit, WM_UNDO, 0, 0);
            msg_box(IDS_TEXTTOOLONG, NULL, MB_OK | MB_ICONEXCLAMATION);
        }
        return;
    }
#ifdef W16_WIN2
    if (pic_paste_bitmap())
        pic_changed();
#else
    if (ole_paste(FALSE))
        pic_changed();
#endif
}

/* ---- moving the picture ---- */

static BOOL dragging;
static POINT grab;

/* keep the picture's top left on the card body */
static void pic_clamp(void)
{
    int w = 40 * g_cx + 1, h = 11 * g_cy;
    if (g_pic.x > w - 1)
        g_pic.x = w - 1;
    if (g_pic.y > h - 1)
        g_pic.y = h - 1;
    if (g_pic.x < 0)
        g_pic.x = 0;
    if (g_pic.y < 0)
        g_pic.y = 0;
}

BOOL pic_mouse(UINT msg, int x, int y)
{
    if (g_pic.kind == PIC_NONE)
        return TRUE;
    switch (msg) {
    case WM_LBUTTONDOWN:
        dragging = TRUE;
        grab.x = x - g_pic.x;
        grab.y = y - g_pic.y;
        SetCapture(g_edit);
        break;
    case WM_MOUSEMOVE:
        if (dragging) {
            g_pic.x = x - grab.x;
            g_pic.y = y - grab.y;
            pic_clamp();
            pic_changed();
            UpdateWindow(g_edit);
        }
        break;
    case WM_LBUTTONUP:
        if (dragging) {
            dragging = FALSE;
            ReleaseCapture();
        }
        break;
    case WM_LBUTTONDBLCLK:
#ifndef W16_WIN2
        if (g_pic.kind == PIC_OBJECT)
            ole_activate(&g_pic, 0);
#endif
        break;
    }
    return TRUE;
}

/* arrow keys move the picture a pixel at a time (UNVERIFIED step) */
void pic_key(WPARAM vk)
{
    if (g_pic.kind == PIC_NONE)
        return;
    switch (vk) {
    case VK_LEFT:
        g_pic.x--;
        break;
    case VK_RIGHT:
        g_pic.x++;
        break;
    case VK_UP:
        g_pic.y--;
        break;
    case VK_DOWN:
        g_pic.y++;
        break;
    default:
        return;
    }
    pic_clamp();
    pic_changed();
}
