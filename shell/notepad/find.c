/*
 * find.c - w16 Notepad: Find and Find Next.
 *
 * Observed on Windows 3.1: Search > Find is COMMDLG's modeless Find
 * dialog, "Match Whole Word Only" and Help hidden, Down chosen, the text
 * box empty at first (later, the last text). Find Next selects the next
 * match after the selection (Up: before it); none: "Cannot find "%%""
 * with the information icon. The dialog stays up.
 */
#include "notepad.h"
#include <commdlg.h>

UINT g_findmsg;
HWND g_finddlg;
static char what[128];                  /* the text to find */
static BOOL down = TRUE, match_case;

static FINDREPLACE fr;

static char lower(char c)
{
    return (char)(DWORD)AnsiLower((LPSTR)(DWORD)(BYTE)c);
}

static BOOL same_at(const char FAR *t, const char *w)
{
    for (; *w; t++, w++)
        if (*t != *w && (match_case || lower(*t) != lower(*w)))
            return FALSE;
    return TRUE;
}

/* Search from the selection; select the match or say there is none. */
static void search(HWND owner)
{
    WORD len = (WORD)SendMessage(g_edit, WM_GETTEXTLENGTH, 0, 0), n = s_len(what), i;
    DWORD sel = SendMessage(g_edit, EM_GETSEL, 0, 0);
    LPSTR text;
    BOOL found = FALSE;
    if (!n)
        return;
    text = mem_alloc((DWORD)len + 1);
    if (!text) {
        msg_box(owner, IDS_NOMEMORY, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    GetWindowText(g_edit, text, len + 1);
    if (down) {
        for (i = HIWORD(sel); i + n <= len; i++)
            if (same_at(text + i, what)) {
                found = TRUE;
                break;
            }
    } else if (LOWORD(sel) >= n) {
        for (i = LOWORD(sel) - n + 1; i-- > 0;)
            if (same_at(text + i, what)) {
                found = TRUE;
                break;
            }
    }
    mem_free(text);
    if (found) {
        SendMessage(g_edit, EM_SETSEL, 0, MAKELONG(i, i + n));
        sync_scroll();
    } else {
        msg_box(owner, IDS_CANTFIND, what, MB_OK | MB_ICONINFORMATION);
    }
}


void find_dialog(void)
{
    BYTE FAR *z = (BYTE FAR *)&fr;
    unsigned i;
    if (g_finddlg) {
        SetFocus(g_finddlg);
        return;
    }
    for (i = 0; i < sizeof fr; i++)
        z[i] = 0;
    fr.lStructSize = sizeof fr;
    fr.hwndOwner = g_main;
    fr.hInstance = g_hinst;
    fr.Flags = FR_HIDEWHOLEWORD | (down ? FR_DOWN : 0) | (match_case ? FR_MATCHCASE : 0);
    fr.lpstrFindWhat = what;
    fr.wFindWhatLen = sizeof what;
    g_finddlg = FindText(&fr);
}

LRESULT find_message(LPARAM lp)
{
    FINDREPLACE FAR *f = (FINDREPLACE FAR *)lp;
    if (f->Flags & FR_DIALOGTERM) {
        g_finddlg = NULL;
        return 0;
    }
    down = (f->Flags & FR_DOWN) != 0;
    match_case = (f->Flags & FR_MATCHCASE) != 0;
    if (f->Flags & FR_FINDNEXT)
        search(g_finddlg ? g_finddlg : g_main);
    return 0;
}


/* F3: the last search again; nothing searched for yet, the Find dialog
   (UNVERIFIED: what the original does then) */
void find_next(void)
{
    if (!what[0]) {
        find_dialog();
        return;
    }
    search(g_finddlg ? g_finddlg : g_main);
}

