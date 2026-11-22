/*
 * macro.c - w16 Windows Help: Help macros (Windows 3.1 SDK, Programmer's
 * Reference Vol. 4, ch. 15, "Help Macro Reference": names, short forms,
 * parameters and what each does).
 *
 * A macro string is macros separated by semicolons; a macro is a name and
 * its parameters in parentheses, separated by commas: strings in double
 * quotes or in `...' (which nest), numbers, or macros themselves (for
 * IfThen and friends). Names are not case-sensitive. Unknown macros are
 * ignored (w16: UNVERIFIED whether 3.1 says so in a message box).
 *
 * Done: About, Annotate, AppendItem, Back, BookmarkDefine, BookmarkMore,
 * BrowseButtons, ChangeButtonBinding (CBB), ChangeItemBinding, CheckItem,
 * CloseWindow, Contents, CopyDialog, CopyTopic, CreateButton (CB),
 * DeleteItem, DeleteMark, DestroyButton, DisableButton (DB), DisableItem,
 * EnableButton (EB), EnableItem, ExecProgram (EP), Exit, FileOpen,
 * FocusWindow, GotoMark, HelpOn, HelpOnTop, History, IfThen,
 * IfThenElse, InsertItem, InsertMenu, IsMark, JumpContents, JumpContext
 * (JC), JumpHelpOn, JumpId (JI), JumpKeyword (JK), Next, Not,
 * PopupContext (PC), PopupId (PI), PositionWindow, Prev, Print,
 * PrinterSetup, RegisterRoutine (RR) with the routines it names,
 * ResetMenu, SaveMark, Search, SetContents, SetHelpOnFile, UncheckItem.
 */
#include "winhelp.h"

#define MAXARGS 8
#define ARGLEN  256

typedef struct {
    char name[32];
    int n;
    char arg[MAXARGS][ARGLEN];
} CALL;

static char helpon[PATHMAX] = "WINHELP.HLP";

/* ---- parsing ---- */

static LPCSTR skip(LPCSTR s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    return s;
}

/* one argument: a quoted string (" " or ` '), or everything up to the
   comma or parenthesis that ends it (nested parentheses kept) */
static LPCSTR parse_arg(LPCSTR s, LPSTR out)
{
    int n = 0;
    s = skip(s);
    if (*s == '"') {
        s++;
        while (*s && *s != '"') {
            if (n < ARGLEN - 1)
                out[n++] = *s;
            s++;
        }
        if (*s)
            s++;
    } else if (*s == '`') {
        int depth = 1;
        s++;
        while (*s) {
            if (*s == '`')
                depth++;
            else if (*s == '\'' && --depth == 0)
                break;
            if (n < ARGLEN - 1)
                out[n++] = *s;
            s++;
        }
        if (*s)
            s++;
    } else {
        int depth = 0;
        while (*s && !(depth == 0 && (*s == ',' || *s == ')'))) {
            if (*s == '(')
                depth++;
            else if (*s == ')')
                depth--;
            else if (*s == '"') {
                if (n < ARGLEN - 1)
                    out[n++] = *s;
                s++;
                while (*s && *s != '"') {
                    if (n < ARGLEN - 1)
                        out[n++] = *s;
                    s++;
                }
            }
            if (*s) {
                if (n < ARGLEN - 1)
                    out[n++] = *s;
                s++;
            }
        }
        while (n && (out[n - 1] == ' ' || out[n - 1] == '\t'))
            n--;
    }
    out[n] = 0;
    return skip(s);
}

static LPCSTR parse_call(LPCSTR s, CALL *c)
{
    int n = 0;
    s = skip(s);
    while (*s && *s != '(' && *s != ';' && *s != ' ' && n < (int)sizeof c->name - 1)
        c->name[n++] = *s++;
    c->name[n] = 0;
    c->n = 0;
    s = skip(s);
    if (*s == '(') {
        s = skip(s + 1);
        if (*s == ')') {
            s++;
        } else {
            for (;;) {
                char tmp[ARGLEN];
                s = parse_arg(s, c->n < MAXARGS ? c->arg[c->n] : tmp);
                if (c->n < MAXARGS)
                    c->n++;
                if (*s == ',') {
                    s++;
                    continue;
                }
                if (*s == ')')
                    s++;
                break;
            }
        }
    }
    s = skip(s);
    if (*s == ';')
        s++;
    return s;
}

static long num(LPCSTR s)
{
    long v = 0;
    BOOL neg = FALSE, hex = FALSE;
    s = skip(s);
    if (*s == '-') {
        neg = TRUE;
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        hex = TRUE;
        s += 2;
    }
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (hex && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (hex && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            break;
        v = v * (hex ? 16 : 10) + d;
    }
    return neg ? -v : v;
}

static BOOL is(CALL *c, LPCSTR a, LPCSTR b)
{
    return !lstrcmpi(c->name, a) || (b && !lstrcmpi(c->name, b));
}

/* "file>window" splits into file and window */
static void split_file(LPSTR arg, LPSTR *window)
{
    LPSTR p;
    *window = NULL;
    for (p = arg; *p; p++)
        if (*p == '>') {
            *p = 0;
            *window = p + 1;
            break;
        }
}

/* ---- marks ---- */

#define MAXMARKS 8
static struct {
    char name[32];
    char file[PATHMAX];
    DWORD off;
} marks[MAXMARKS];
static int nmarks;

static int mark_find(LPCSTR n)
{
    int i;
    for (i = 0; i < nmarks; i++)
        if (!lstrcmp(marks[i].name, n))
            return i;
    return -1;
}

/* ---- menus ---- */

#define MAXITEMS 32
typedef struct {
    char id[32];
    UINT cmd;
    char macro[ARGLEN];
    HMENU menu;
} MITEM;
static MITEM FAR *items;
static int nitems;
static UINT nextcmd = 3000;

static HMENU menu_by_id(LPCSTR id)
{
    HMENU m = GetMenu(g_frame);
    static const char *const std[] = { "mnu_file", "mnu_edit", "mnu_bookmark", "mnu_helpon" };
    int i;
    for (i = 0; i < 4; i++)
        if (!lstrcmpi(id, std[i]))
            return GetSubMenu(m, i);
    for (i = 0; i < nitems; i++)
        if (!lstrcmpi(items[i].id, id) && items[i].menu && !items[i].cmd)
            return items[i].menu;
    return NULL;
}

static int item_find(LPCSTR id)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (!lstrcmpi(items[i].id, id))
            return i;
    return -1;
}

BOOL menu_command(UINT cmd)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (items[i].cmd == cmd) {
            run_macros(items[i].macro);
            return TRUE;
        }
    return FALSE;
}

/* ---- RegisterRoutine: DLL functions, called with the Pascal convention
   from the prototype letters: i, u (16-bit), I, U (32-bit), s, S (far
   string) ---- */

#define MAXROUT 16
static struct {
    char name[32];
    char proto[16];
    FARPROC fn;
} routs[MAXROUT];
static int nrouts;

typedef DWORD (FAR PASCAL *W0)(void);
typedef DWORD (FAR PASCAL *W1)(WORD);
typedef DWORD (FAR PASCAL *W2)(WORD, WORD);
typedef DWORD (FAR PASCAL *W3)(WORD, WORD, WORD);
typedef DWORD (FAR PASCAL *W4)(WORD, WORD, WORD, WORD);
typedef DWORD (FAR PASCAL *W5)(WORD, WORD, WORD, WORD, WORD);
typedef DWORD (FAR PASCAL *W6)(WORD, WORD, WORD, WORD, WORD, WORD);
typedef DWORD (FAR PASCAL *W7)(WORD, WORD, WORD, WORD, WORD, WORD, WORD);
typedef DWORD (FAR PASCAL *W8)(WORD, WORD, WORD, WORD, WORD, WORD, WORD, WORD);

static DWORD call_routine(int r, CALL *c)
{
    WORD w[8];
    int k = 0, i;
    LPCSTR p = routs[r].proto;
    if (*p == '=')
        p++;
    /* the first letter may be the return type ("U=...") */
    {
        LPCSTR eq = routs[r].proto;
        while (*eq && *eq != '=')
            eq++;
        if (*eq == '=')
            p = eq + 1;
        else
            p = routs[r].proto;
    }
    for (i = 0; *p && i < c->n && k < 8; p++, i++) {
        switch (*p) {
        case 'i': case 'u':
            w[k++] = (WORD)num(c->arg[i]);
            break;
        case 'I': case 'U': {
            long v = num(c->arg[i]);
            w[k++] = HIWORD(v);
            if (k < 8)
                w[k++] = LOWORD(v);
            break;
        }
        case 's': case 'S': {
            LPSTR s = c->arg[i];
            w[k++] = SELECTOROF(s);
            if (k < 8)
                w[k++] = OFFSETOF(s);
            break;
        }
        default:
            w[k++] = (WORD)num(c->arg[i]);
            break;
        }
    }
    switch (k) {
    case 0: return ((W0)routs[r].fn)();
    case 1: return ((W1)routs[r].fn)(w[0]);
    case 2: return ((W2)routs[r].fn)(w[0], w[1]);
    case 3: return ((W3)routs[r].fn)(w[0], w[1], w[2]);
    case 4: return ((W4)routs[r].fn)(w[0], w[1], w[2], w[3]);
    case 5: return ((W5)routs[r].fn)(w[0], w[1], w[2], w[3], w[4]);
    case 6: return ((W6)routs[r].fn)(w[0], w[1], w[2], w[3], w[4], w[5]);
    case 7: return ((W7)routs[r].fn)(w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
    default: return ((W8)routs[r].fn)(w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
    }
}

/* ---- running ---- */

static BOOL run_one(CALL *c);

static BOOL eval_cond(LPCSTR m)
{
    CALL c;
    parse_call(m, &c);
    return run_one(&c);
}

/* returns the macro's value where it has one (IsMark, Not, routines);
   TRUE otherwise */
static BOOL run_one(CALL *c)
{
    LPSTR win;
    POINT pt;
    int i;
    if (!c->name[0])
        return TRUE;
    if (is(c, "JumpId", "JI")) {
        if (c->n >= 2) {
            split_file(c->arg[0], &win);
            return jump_context(g_hlp, c->arg[0][0] ? (LPCSTR)c->arg[0] : (LPCSTR)NULL, c->arg[1], win, FALSE);
        }
    } else if (is(c, "PopupId", "PI")) {
        if (c->n >= 2)
            return jump_context(g_hlp, c->arg[0][0] ? (LPCSTR)c->arg[0] : (LPCSTR)NULL, c->arg[1], NULL, TRUE);
    } else if (is(c, "JumpContext", "JC") || is(c, "PopupContext", "PC")) {
        if (c->n >= 2) {
            BOOL pop = is(c, "PopupContext", "PC");
            HLP FAR *h;
            DWORD off;
            split_file(c->arg[0], &win);
            h = c->arg[0][0] ? hlp_get(c->arg[0]) : g_hlp;
            if (!h || !hlp_mapid(h, (DWORD)num(c->arg[1]), &off)) {
                errorbox(IDS_NOTOPIC, NULL);
                return FALSE;
            }
            if (pop) {
                GetCursorPos(&pt);
                popup_show(h, hlp_topic_at(h, off), pt);
                return TRUE;
            }
            if (h != g_hlp && !open_file(c->arg[0], FALSE))
                return FALSE;
            return show_offset(off, TRUE, 0);
        }
    } else if (is(c, "JumpKeyword", "JK")) {
        if (c->n >= 2) {
            split_file(c->arg[0], &win);
            if (c->arg[0][0] && !open_file(c->arg[0], FALSE))
                return FALSE;
            return key_jump(c->arg[1], FALSE);
        }
    } else if (is(c, "JumpContents", NULL)) {
        if (c->n >= 1) {
            split_file(c->arg[0], &win);
            if (!open_file(c->arg[0], FALSE))
                return FALSE;
        }
        go_contents();
    } else if (is(c, "JumpHelpOn", NULL) || is(c, "HelpOn", NULL)) {
        WinHelp(g_frame, helpon, HELP_CONTENTS, 0);
    } else if (is(c, "SetHelpOnFile", NULL)) {
        if (c->n >= 1)
            hlp_strcpyn(helpon, c->arg[0], sizeof helpon);
    } else if (is(c, "SetContents", NULL)) {
        if (c->n >= 2 && g_hlp) {
            DWORD off;
            if (hlp_mapid(g_hlp, (DWORD)num(c->arg[1]), &off))
                g_contents = off;
        }
    } else if (is(c, "CreateButton", "CB")) {
        if (c->n >= 3)
            add_button(c->arg[0], c->arg[1], c->arg[2]);
    } else if (is(c, "DestroyButton", NULL)) {
        if (c->n >= 1)
            destroy_button(c->arg[0]);
    } else if (is(c, "ChangeButtonBinding", "CBB")) {
        if (c->n >= 2)
            change_button(c->arg[0], c->arg[1]);
    } else if (is(c, "DisableButton", "DB")) {
        if (c->n >= 1)
            enable_button(c->arg[0], FALSE);
    } else if (is(c, "EnableButton", "EB")) {
        if (c->n >= 1)
            enable_button(c->arg[0], TRUE);
    } else if (is(c, "BrowseButtons", NULL)) {
        browse_buttons();
    } else if (is(c, "Back", NULL)) {
        go_back();
    } else if (is(c, "Contents", NULL)) {
        go_contents();
    } else if (is(c, "History", NULL)) {
        show_history();
    } else if (is(c, "Search", NULL)) {
        search_dialog(NULL);
    } else if (is(c, "Next", NULL)) {
        do_command(IDB_NEXT);
    } else if (is(c, "Prev", NULL)) {
        do_command(IDB_PREV);
    } else if (is(c, "Exit", NULL)) {
        PostMessage(g_frame, WM_CLOSE, 0, 0);
    } else if (is(c, "FileOpen", NULL)) {
        file_open_dialog();
    } else if (is(c, "Print", NULL)) {
        print_topic();
    } else if (is(c, "PrinterSetup", NULL)) {
        print_setup();
    } else if (is(c, "CopyDialog", NULL)) {
        copy_dialog();
    } else if (is(c, "CopyTopic", NULL)) {
        copy_topic();
    } else if (is(c, "Annotate", NULL)) {
        annotate();
    } else if (is(c, "BookmarkDefine", NULL)) {
        bookmark_define();
    } else if (is(c, "BookmarkMore", NULL)) {
        bookmark_more();
    } else if (is(c, "HelpOnTop", NULL)) {
        set_ontop(!g_ontop);
    } else if (is(c, "About", NULL)) {
        do_command(IDM_ABOUT);
    } else if (is(c, "ExecProgram", "EP")) {
        if (c->n >= 1) {
            static const int show[] = { SW_SHOWNORMAL, SW_SHOWMINIMIZED, SW_SHOWMAXIMIZED };
            int s = c->n >= 2 ? (int)num(c->arg[1]) : 0;
            return WinExec(c->arg[0], show[s >= 0 && s <= 2 ? s : 0]) > 31;
        }
    } else if (is(c, "CloseWindow", NULL)) {
        if (c->n >= 1 && !lstrcmpi(c->arg[0], "main"))
            PostMessage(g_frame, WM_CLOSE, 0, 0);
        else if (g_second)
            DestroyWindow(g_second);
    } else if (is(c, "FocusWindow", NULL)) {
        if (c->n >= 1 && lstrcmpi(c->arg[0], "main") && g_second)
            SetActiveWindow(g_second);
        else
            SetActiveWindow(g_frame);
    } else if (is(c, "PositionWindow", NULL)) {
        if (c->n >= 6) {
            int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
            HWND w = lstrcmpi(c->arg[5], "main") && g_second ? g_second : g_frame;
            MoveWindow(w, (int)(num(c->arg[0]) * sx / 1024), (int)(num(c->arg[1]) * sy / 1024),
                       (int)(num(c->arg[2]) * sx / 1024), (int)(num(c->arg[3]) * sy / 1024), TRUE);
            ShowWindow(w, (int)num(c->arg[4]));
        }
    } else if (is(c, "SaveMark", NULL)) {
        if (c->n >= 1 && g_hlp) {
            i = mark_find(c->arg[0]);
            if (i < 0 && nmarks < MAXMARKS)
                i = nmarks++;
            if (i >= 0) {
                hlp_strcpyn(marks[i].name, c->arg[0], sizeof marks[i].name);
                lstrcpy(marks[i].file, g_file);
                marks[i].off = g_curoff;
            }
        }
    } else if (is(c, "GotoMark", NULL)) {
        if (c->n >= 1 && (i = mark_find(c->arg[0])) >= 0) {
            if (lstrcmpi(marks[i].file, g_file) && !open_file(marks[i].file, FALSE))
                return FALSE;
            return show_offset(marks[i].off, TRUE, 0);
        }
    } else if (is(c, "DeleteMark", NULL)) {
        if (c->n >= 1 && (i = mark_find(c->arg[0])) >= 0) {
            marks[i] = marks[--nmarks];
        }
    } else if (is(c, "IsMark", NULL)) {
        return c->n >= 1 && mark_find(c->arg[0]) >= 0;
    } else if (is(c, "Not", NULL)) {
        return c->n >= 1 ? !eval_cond(c->arg[0]) : FALSE;
    } else if (is(c, "IfThen", NULL)) {
        if (c->n >= 2 && eval_cond(c->arg[0]))
            run_macros(c->arg[1]);
    } else if (is(c, "IfThenElse", NULL)) {
        if (c->n >= 3)
            run_macros(eval_cond(c->arg[0]) ? c->arg[1] : c->arg[2]);
    } else if (is(c, "InsertMenu", NULL)) {
        if (!items)
            items = (MITEM FAR *)hio_alloc(sizeof(MITEM) * MAXITEMS);
        if (items && c->n >= 2 && nitems < MAXITEMS) {
            HMENU m = CreatePopupMenu(), bar = GetMenu(g_frame);
            int pos = c->n >= 3 ? (int)num(c->arg[2]) : -1;
            hlp_strcpyn(items[nitems].id, c->arg[0], sizeof items[nitems].id);
            items[nitems].cmd = 0;
            items[nitems].menu = m;
            nitems++;
            InsertMenu(bar, pos, MF_BYPOSITION | MF_POPUP, (UINT)m, c->arg[1]);
            DrawMenuBar(g_frame);
        }
    } else if (is(c, "AppendItem", NULL) || is(c, "InsertItem", NULL)) {
        BOOL ins = is(c, "InsertItem", NULL);
        if (!items)
            items = (MITEM FAR *)hio_alloc(sizeof(MITEM) * MAXITEMS);
        if (items && c->n >= 4 && nitems < MAXITEMS) {
            HMENU m = menu_by_id(c->arg[0]);
            if (m) {
                hlp_strcpyn(items[nitems].id, c->arg[1], sizeof items[nitems].id);
                items[nitems].cmd = nextcmd++;
                items[nitems].menu = m;
                hlp_strcpyn(items[nitems].macro, c->arg[3], sizeof items[nitems].macro);
                if (ins && c->n >= 5)
                    InsertMenu(m, (UINT)num(c->arg[4]), MF_BYPOSITION | MF_STRING, items[nitems].cmd, c->arg[2]);
                else
                    AppendMenu(m, MF_STRING, items[nitems].cmd, c->arg[2]);
                nitems++;
            }
        }
    } else if (is(c, "DeleteItem", NULL)) {
        if (c->n >= 1 && (i = item_find(c->arg[0])) >= 0) {
            DeleteMenu(items[i].menu, items[i].cmd, MF_BYCOMMAND);
            items[i] = items[--nitems];
        }
    } else if (is(c, "ChangeItemBinding", NULL)) {
        if (c->n >= 2 && (i = item_find(c->arg[0])) >= 0)
            hlp_strcpyn(items[i].macro, c->arg[1], sizeof items[i].macro);
    } else if (is(c, "CheckItem", NULL) || is(c, "UncheckItem", NULL)) {
        if (c->n >= 1 && (i = item_find(c->arg[0])) >= 0)
            CheckMenuItem(items[i].menu, items[i].cmd, is(c, "CheckItem", NULL) ? MF_CHECKED : MF_UNCHECKED);
    } else if (is(c, "DisableItem", NULL) || is(c, "EnableItem", NULL)) {
        if (c->n >= 1 && (i = item_find(c->arg[0])) >= 0)
            EnableMenuItem(items[i].menu, items[i].cmd, is(c, "EnableItem", NULL) ? MF_ENABLED : MF_GRAYED);
    } else if (is(c, "ResetMenu", NULL)) {
        HMENU old = GetMenu(g_frame);
        SetMenu(g_frame, LoadMenu(g_inst, MAKEINTRESOURCE(1)));
        DestroyMenu(old);
        nitems = 0;
        bookmark_menu();
    } else if (is(c, "RegisterRoutine", "RR")) {
        if (c->n >= 2 && nrouts < MAXROUT) {
            HINSTANCE lib = LoadLibrary(c->arg[0]);
            if ((UINT)lib < 32) {
                char dll[PATHMAX];
                wsprintf(dll, "%s.DLL", (LPSTR)c->arg[0]);
                lib = LoadLibrary(dll);
            }
            if ((UINT)lib >= 32) {
                FARPROC f = GetProcAddress(lib, c->arg[1]);
                if (f) {
                    hlp_strcpyn(routs[nrouts].name, c->arg[1], sizeof routs[nrouts].name);
                    hlp_strcpyn(routs[nrouts].proto, c->n >= 3 ? c->arg[2] : "", sizeof routs[nrouts].proto);
                    routs[nrouts].fn = f;
                    nrouts++;
                }
            }
        }
    } else {
        for (i = 0; i < nrouts; i++)
            if (!lstrcmpi(routs[i].name, c->name))
                return call_routine(i, c) != 0;
        return FALSE;
    }
    return TRUE;
}

BOOL run_macros(LPCSTR m)
{
    BOOL ok = TRUE;
    while (m && *m) {
        CALL c;
        m = parse_call(m, &c);
        if (c.name[0] && !run_one(&c))
            ok = FALSE;
    }
    return ok;
}
