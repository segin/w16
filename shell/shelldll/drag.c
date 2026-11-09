/*
 * drag.c - w16 SHELL.DLL: the receiving side of drag and drop (SHELL.9,
 * 11, 12, 13).
 *
 * The drop block that WM_DROPFILES carries in wParam (SH3, and SH12 with
 * File Manager as the source; the layout Schulman et al. give in
 * Undocumented Windows, ch. 6, DROPINFO, except for the last WORD):
 *   +0  WORD  offset of the first name in the block
 *   +2  POINT where the files were dropped (the target's client
 *             coordinates)
 *   +6  WORD  nonzero when dropped outside the client area (DragQueryPoint
 *             returns 1 only when it is 0: SH3; the book calls it bClient)
 *   then NUL-terminated names, ended by an empty one.
 */
#include "shell.h"

#define WS_EX_ACCEPTFILES 0x00000010L

/* only the window's WS_EX_ACCEPTFILES bit changes (SH3) */
void SHAPI DragAcceptFiles(HWND w, BOOL accept)
{
    LONG ex;
    if (!w || !IsWindow(w))
        return;
    ex = GetWindowLong(w, GWL_EXSTYLE);
    if (accept)
        ex |= WS_EX_ACCEPTFILES;
    else
        ex &= ~WS_EX_ACCEPTFILES;
    SetWindowLong(w, GWL_EXSTYLE, ex);
}

/* index (UINT)-1: the number of names; a name past the end: 0, buffer
   untouched; otherwise the name's full length, copied as far as cb
   allows (nothing when cb or buf is 0) (SH3) */
UINT SHAPI DragQueryFile(HGLOBAL h, UINT index, LPSTR buf, UINT cb)
{
    const char __far *p = (const char __far *)GlobalLock(h);
    UINT n = 0, len;
    if (!p)
        return 0;
    p += *(const WORD __far *)p;
    if (index == (UINT)-1) {
        while (*p) {
            p += sh_len(p) + 1;
            n++;
        }
        GlobalUnlock(h);
        return n;
    }
    while (*p && index) {
        p += sh_len(p) + 1;
        index--;
    }
    len = *p ? sh_len(p) : 0;
    if (len && buf && cb)
        sh_copyn(buf, p, cb);
    GlobalUnlock(h);
    return len;
}

BOOL SHAPI DragQueryPoint(HGLOBAL h, POINT __far *pt)
{
    const WORD __far *p = (const WORD __far *)GlobalLock(h);
    BOOL client;
    if (!p)
        return FALSE;
    if (pt) {
        pt->x = (int)p[1];
        pt->y = (int)p[2];
    }
    client = p[3] == 0;
    GlobalUnlock(h);
    return client;
}

void SHAPI DragFinish(HGLOBAL h)
{
    GlobalFree(h);
}
