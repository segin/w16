/*
 * find.c - w16 COMMDLG: FindText and ReplaceText, the modeless Find and
 * Replace dialogs (templates 1540, 1541).
 *
 * Observed on Windows 3.1 (tests/scripts/commdlg/find1.txt): the owner
 * hears FINDMSGSTRING with lParam = the caller's FINDREPLACE, its Flags
 * holding the caller's bits with the action's: Find Next FR_FINDNEXT,
 * Replace FR_REPLACE, Replace All FR_REPLACEALL, Cancel or close
 * FR_DIALOGTERM (the others cleared). The check boxes set FR_WHOLEWORD
 * and FR_MATCHCASE, the Up/Down buttons FR_DOWN; the Replace dialog,
 * which has no direction, sets FR_DOWN on every action. FR_NO* disable a
 * control, FR_HIDE* hide (and disable) it; FR_SHOWHELP shows Help. Find
 * Next, Replace and Replace All are disabled while there is nothing to
 * find. A zero wFindWhatLen: FRERR_BUFFERLENGTHZERO; no owner window:
 * NULL, the last error left as it was.
 */
#include "commdlg.h"

#ifdef W16_WIN2
extern LPARAM cd_pending_param;
#endif

typedef struct {
    LPFINDREPLACE fr;
    BOOL replace;
    cd_hook hook;
} frstate;

static void enable(HWND dlg, int id, BOOL on)
{
    HWND w = GetDlgItem(dlg, id);
    if (w)
        EnableWindow(w, on);
}

static void hide(HWND dlg, int id)
{
    HWND w = GetDlgItem(dlg, id);
    if (w) {
        ShowWindow(w, SW_HIDE);
        EnableWindow(w, FALSE);
    }
}

/* the buttons that act on the text: only with something to find */
static void update_buttons(frstate FAR *st, HWND dlg)
{
    BOOL any = GetWindowTextLength(GetDlgItem(dlg, edt1)) > 0;
    enable(dlg, IDOK, any);
    if (st->replace) {
        enable(dlg, psh1, any);
        enable(dlg, psh2, any);
    }
}

static void init(frstate FAR *st, HWND dlg)
{
    LPFINDREPLACE f = st->fr;
    DWORD fl = f->Flags;
    SendDlgItemMessage(dlg, edt1, EM_LIMITTEXT, f->wFindWhatLen - 1, 0);
    SetDlgItemText(dlg, edt1, f->lpstrFindWhat);
    if (st->replace) {
        SendDlgItemMessage(dlg, edt2, EM_LIMITTEXT, f->wReplaceWithLen - 1, 0);
        SetDlgItemText(dlg, edt2, f->lpstrReplaceWith ? f->lpstrReplaceWith : "");
    } else {
        CheckRadioButton(dlg, rad1, rad2, (fl & FR_DOWN) ? rad2 : rad1);
        if (fl & FR_HIDEUPDOWN) {
            hide(dlg, grp1);
            hide(dlg, rad1);
            hide(dlg, rad2);
        } else if (fl & FR_NOUPDOWN) {
            enable(dlg, grp1, FALSE);
            enable(dlg, rad1, FALSE);
            enable(dlg, rad2, FALSE);
        }
    }
    CheckDlgButton(dlg, chx1, (fl & FR_WHOLEWORD) != 0);
    CheckDlgButton(dlg, chx2, (fl & FR_MATCHCASE) != 0);
    if (fl & FR_HIDEWHOLEWORD)
        hide(dlg, chx1);
    else if (fl & FR_NOWHOLEWORD)
        enable(dlg, chx1, FALSE);
    if (fl & FR_HIDEMATCHCASE)
        hide(dlg, chx2);
    else if (fl & FR_NOMATCHCASE)
        enable(dlg, chx2, FALSE);
    if (!(fl & FR_SHOWHELP))
        hide(dlg, pshHelp);
    update_buttons(st, dlg);
}

/* tell the owner: action is FR_FINDNEXT, FR_REPLACE or FR_REPLACEALL */
static void act(frstate FAR *st, HWND dlg, DWORD action)
{
    LPFINDREPLACE f = st->fr;
    f->Flags &= ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL | FR_DIALOGTERM |
                  FR_WHOLEWORD | FR_MATCHCASE | FR_DOWN);
    GetDlgItemText(dlg, edt1, f->lpstrFindWhat, f->wFindWhatLen);
    if (st->replace && f->lpstrReplaceWith)
        GetDlgItemText(dlg, edt2, f->lpstrReplaceWith, f->wReplaceWithLen);
    if (IsDlgButtonChecked(dlg, chx1))
        f->Flags |= FR_WHOLEWORD;
    if (IsDlgButtonChecked(dlg, chx2))
        f->Flags |= FR_MATCHCASE;
    if (st->replace || IsDlgButtonChecked(dlg, rad2))
        f->Flags |= FR_DOWN;
    f->Flags |= action;
    SendMessage(f->hwndOwner, msg_findmsg, 0, (LPARAM)f);
}

static void terminate(frstate FAR *st, HWND dlg)
{
    LPFINDREPLACE f = st->fr;
    f->Flags &= ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL);
    f->Flags |= FR_DIALOGTERM;
    SendMessage(f->hwndOwner, msg_findmsg, 0, (LPARAM)f);
    DestroyWindow(dlg);
}

static BOOL find_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    frstate FAR *st;
    if (msg == WM_INITDIALOG) {
#ifdef W16_WIN2
        lp = cd_pending_param;
#endif
        st = (frstate FAR *)lp;
        state_set(dlg, st);
        init(st, dlg);
        if (st->hook)
            return (BOOL)call_hook(st->hook, dlg, msg, wp, (LPARAM)st->fr);
        return TRUE;
    }
    st = (frstate FAR *)state_get(dlg);
    if (!st)
        return FALSE;
    if (st->hook) {
        UINT r = call_hook(st->hook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    switch (msg) {
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            act(st, dlg, FR_FINDNEXT);
            return TRUE;
        case psh1:
            if (st->replace)
                act(st, dlg, FR_REPLACE);
            return TRUE;
        case psh2:
            if (st->replace)
                act(st, dlg, FR_REPLACEALL);
            return TRUE;
        case IDCANCEL:
            terminate(st, dlg);
            return TRUE;
        case pshHelp:
            send_help(dlg, st->fr->hwndOwner, (LPARAM)st->fr);
            return TRUE;
        case edt1:
            if (HIWORD(lp) == EN_CHANGE)
                update_buttons(st, dlg);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        terminate(st, dlg);
        return TRUE;
    case WM_DESTROY:
        state_remove(dlg);
        mem_free(st);
        return FALSE;
    }
    return FALSE;
}

BOOL CDPROC FindTextDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return find_proc(dlg, msg, wp, lp);
}

BOOL CDPROC ReplaceTextDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return find_proc(dlg, msg, wp, lp);
}

static HWND find_dialog(LPFINDREPLACE f, BOOL replace)
{
    frstate FAR *st;
    cd_template t;
    HWND w;
    if (!f || !f->hwndOwner || !IsWindow(f->hwndOwner))
        return NULL;                    /* observed: the error unchanged */
    cd_seterr(0);
    if (f->lStructSize != sizeof(FINDREPLACE)) {
        cd_seterr(CDERR_STRUCTSIZE);
        return NULL;
    }
    if (!f->lpstrFindWhat || !f->wFindWhatLen ||
        (replace && (!f->lpstrReplaceWith || !f->wReplaceWithLen))) {
        cd_seterr(FRERR_BUFFERLENGTHZERO);
        return NULL;
    }
    if ((f->Flags & FR_ENABLEHOOK) && !f->lpfnHook) {
        cd_seterr(CDERR_NOHOOK);
        return NULL;
    }
    st = mem_alloc(sizeof(frstate));
    if (!st) {
        cd_seterr(CDERR_MEMALLOCFAILURE);
        return NULL;
    }
    st->fr = f;
    st->replace = replace;
    st->hook = (f->Flags & FR_ENABLEHOOK) ? (cd_hook)f->lpfnHook : NULL;
    t.app = f->hInstance;
    t.use_handle = (f->Flags & FR_ENABLETEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (f->Flags & FR_ENABLETEMPLATE);
    t.name = f->lpTemplateName;
    t.ours = MAKEINTRESOURCE(replace ? DLG_REPLACE : DLG_FIND);
    w = cd_modeless(&t, f->hwndOwner,
                    replace ? (FARPROC)ReplaceTextDlgProc : (FARPROC)FindTextDlgProc,
                    (LPARAM)(LPVOID)st);
    if (!w) {
        mem_free(st);
        return NULL;
    }
    ShowWindow(w, SW_SHOWNORMAL);
    return w;
}

HWND CDAPI FindText(LPFINDREPLACE f)
{
    return find_dialog(f, FALSE);
}

HWND CDAPI ReplaceText(LPFINDREPLACE f)
{
    return find_dialog(f, TRUE);
}
