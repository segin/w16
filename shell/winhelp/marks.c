/*
 * marks.c - w16 Windows Help: bookmarks, annotations, Copy and Print
 * Topic (layer 4: see ARCHITECTURE.md 15.10).
 */
#include "winhelp.h"
#include <commdlg.h>

void bookmark_define(void) { }
void bookmark_more(void) { }
void bookmark_go(int i) { (void)i; }
void bookmark_menu(void) { }
void annotate(void) { }
BOOL annot_has(LPCSTR file, DWORD off) { (void)file; (void)off; return FALSE; }
void annot_open_mark(void) { }

void copy_dialog(void)
{
    DWORD n;
    LPSTR s = view_text(&g_vtopic, &n);
    HGLOBAL g;
    if (!s)
        return;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    if (g) {
        LPSTR d = (LPSTR)GlobalLock(g);
        m_cpy(d, s, (WORD)n + 1);
        GlobalUnlock(g);
        if (OpenClipboard(g_frame)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, g);
            CloseClipboard();
        } else {
            GlobalFree(g);
        }
    }
    hio_free(s);
}

static HGLOBAL devmode, devnames;

void print_setup(void)
{
    PRINTDLG p;
    m_set(&p, 0, sizeof p);
    p.lStructSize = sizeof p;
    p.hwndOwner = g_frame;
    p.hDevMode = devmode;
    p.hDevNames = devnames;
    p.Flags = PD_PRINTSETUP;
    if (PrintDlg(&p)) {
        devmode = p.hDevMode;
        devnames = p.hDevNames;
    }
}

void print_topic(void) { }
