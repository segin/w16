/*
 * dde.c - Program Manager's DDE interface (the "PROGMAN" service that
 * installers use to create groups and items).
 *
 * Commands as documented in the Windows 3.1 SDK; the details below were
 * observed on the original with tests/apps/tdde.c (tests/scripts/dde):
 * - The frame window ("Progman") itself answers WM_DDE_INITIATE for
 *   application and topic "PROGMAN" (or either left out).
 * - WM_DDE_EXECUTE is acknowledged with status 8000h (fAck) when every
 *   command succeeded, 0000h otherwise (an unknown command, a group that
 *   does not exist).
 * - WM_DDE_REQUEST, CF_TEXT: "Groups" gives the group names in Window
 *   menu order, each followed by CR LF; a group's name gives its
 *   contents (group_dde_text). The data block has fResponse and fRelease
 *   set (3000h), no fAckReq.
 * Raw DDE messages exist since Windows 2.0, so both builds have this.
 */
#include "progman.h"
#include <dde.h>

static HWND dde_group;                  /* the group AddItem and friends use */

/* ---- execute strings: [Command(arg,"arg, with comma",...)]... ---- */

typedef struct {
    LPSTR buf;                          /* the arguments, NUL-separated */
    int argc;
} args;

/* The n-th argument (from 0), "" if there are fewer. */
static const char FAR *arg(const args *a, int n)
{
    const char FAR *p = a->buf;
    if (n >= a->argc)
        return "";
    while (n--)
        p += s_len(p) + 1;
    return p;
}

static int arg_int(const args *a, int n)
{
    const char FAR *p = arg(a, n);
    return s_atoi(&p);
}

/* Parse one "Name(args)" at *ps (after '['); fills name and a. */
static BOOL parse_command(const char FAR **ps, LPSTR *name, args *a)
{
    const char FAR *s = *ps;
    const char FAR *n0;
    int n;
    *name = NULL;
    a->buf = s_dup("");
    a->argc = 0;
    while (*s == ' ')
        s++;
    n0 = s;
    while (*s && *s != '(' && *s != ']' && *s != ' ')
        s++;
    n = s - n0;
    *name = mem_alloc(n + 1);
    if (!*name || !a->buf)
        return FALSE;
    s_cpy(*name, n0, n + 1);
    while (*s == ' ')
        s++;
    if (*s == '(') {
        s++;
        for (;;) {                      /* one argument per round */
            LPSTR one = s_dup("");
            char c[2];
            BOOL quoted = FALSE;
            while (*s == ' ')
                s++;
            c[1] = 0;
            while (*s && one) {
                if (*s == '"') {
                    quoted = !quoted;
                    s++;
                    continue;
                }
                if (!quoted && (*s == ',' || *s == ')'))
                    break;
                c[0] = *s++;
                if (!s_append(&one, c)) {
                    mem_free(one);
                    one = NULL;
                }
            }
            if (!one)
                return FALSE;
            {                           /* trailing blanks */
                int k = s_len(one);
                while (k > 0 && one[k - 1] == ' ')
                    one[--k] = 0;
            }
            {                           /* append "one\0" to the list */
                int have = 0, k;
                LPSTR nb;
                for (k = 0; k < a->argc; k++)
                    have += s_len(a->buf + have) + 1;
                nb = mem_realloc(a->buf, (DWORD)have + s_len(one) + 2);
                if (!nb) {
                    mem_free(one);
                    return FALSE;
                }
                s_cpy(nb + have, one, s_len(one) + 1);
                a->buf = nb;
                a->argc++;
            }
            mem_free(one);
            if (*s == ',') {
                s++;
                continue;
            }
            if (*s == ')')
                s++;
            break;
        }
    }
    while (*s == ' ')
        s++;
    if (*s != ']')
        return FALSE;
    *ps = s + 1;
    return TRUE;
}

static HWND current_group(void)
{
    if (dde_group && IsWindow(dde_group))
        return dde_group;
    return (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
}

/* Run one command; FALSE if it failed. */
static BOOL run_command(HWND frame, const char FAR *name, const args *a)
{
    HWND g;
    if (!s_icmp(name, "CreateGroup")) {
        /* UNVERIFIED: an existing group of that name is just activated */
        g = group_find(arg(a, 0));
        if (!g)
            g = create_group_named(arg(a, 0), arg(a, 1));
        if (!g)
            return FALSE;
        group_show(g, SW_SHOWNORMAL);
        dde_group = g;
        return TRUE;
    }
    if (!s_icmp(name, "ShowGroup")) {
        g = group_find(arg(a, 0));
        if (!g)
            return FALSE;
        group_show(g, a->argc > 1 ? arg_int(a, 1) : SW_SHOWNORMAL);
        dde_group = g;
        return TRUE;
    }
    if (!s_icmp(name, "DeleteGroup")) {
        g = group_find(arg(a, 0));
        if (!g)
            return FALSE;
        if (g == dde_group)
            dde_group = NULL;
        delete_group(g);
        return TRUE;
    }
    if (!s_icmp(name, "Reload")) {
        /* reread a group's file (UNVERIFIED: no name means every group) */
        HWND next;
        for (g = GetWindow(g_client, GW_CHILD); g; g = next) {
            next = GetWindow(g, GW_HWNDNEXT);
            if (group_name(g) && (!a->argc || !s_icmp(group_name(g), arg(a, 0)))) {
                LPSTR path = s_dup(group_path(g));
                int n = group_ininum(g);
                SendMessage(g_client, WM_MDIDESTROY, (WPARAM)g, 0);
                if (path) {
                    group_load(path, n);
                    mem_free(path);
                }
                if (a->argc)
                    break;
            }
        }
        return TRUE;
    }
    if (!s_icmp(name, "AddItem")) {
        /* AddItem(command[,name[,iconpath[,index[,x,y[,dir[,hotkey[,minimize]]]]]]]) */
        itemprops ip;
        BOOL ok;
        g = current_group();
        if (!g || !arg(a, 0)[0])
            return FALSE;
        ip.cmd = s_dup(arg(a, 0));
        ip.desc = s_dup(arg(a, 1));
        ip.dir = s_dup(arg(a, 6));
        ip.iconpath = a->argc > 2 && arg(a, 2)[0] ? s_dup(arg(a, 2)) : NULL;
        ip.iconindex = arg_int(a, 3);
        ip.has_pos = a->argc > 5 && arg(a, 4)[0] && arg(a, 5)[0];
        ip.x = arg_int(a, 4);
        ip.y = arg_int(a, 5);
        ip.hotkey = (WORD)arg_int(a, 7);
        ip.minimized = arg_int(a, 8) != 0;
        ip.icon = NULL;
        ip.dde = TRUE;
        ok = ip.cmd && ip.desc && ip.dir && group_add_item(g, &ip);
        itemprops_free(&ip);
        return ok;
    }
    if (!s_icmp(name, "DeleteItem")) {
        g = current_group();
        return g && group_delete_named(g, arg(a, 0));
    }
    if (!s_icmp(name, "ReplaceItem")) {
        g = current_group();
        return g && group_replace_named(g, arg(a, 0));
    }
    if (!s_icmp(name, "ExitProgman")) {
        /* ignored when Program Manager is the shell (SDK), and under
           [Restrictions] NoClose (observed: Program Manager stayed; the
           other restrictions do not apply to DDE) */
        if (!g_isshell && !g_noclose) {
            if (arg_int(a, 0))
                SendMessage(frame, WM_CLOSE, 0, 0);
            else
                DestroyWindow(frame);
        }
        return TRUE;
    }
    return FALSE;                       /* observed: unknown -> negative ack */
}

static BOOL execute(HWND frame, const char FAR *cmds)
{
    const char FAR *s = cmds;
    for (;;) {
        LPSTR name;
        args a;
        BOOL ok;
        while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t')
            s++;
        if (!*s)
            return TRUE;
        if (*s++ != '[')
            return FALSE;
        ok = parse_command(&s, &name, &a) && run_command(frame, name, &a);
        mem_free(name);
        mem_free(a.buf);
        if (!ok)
            return FALSE;
    }
}

/* ---- requests ---- */

static LPSTR groups_text(void)
{
    LPSTR s = s_dup("");
    WORD id;
    HWND w;
    /* in Window menu order: by MDI child ID (observed) */
    for (id = IDM_FIRSTCHILD; s && (w = GetDlgItem(g_client, id)) != NULL; id++)
        if (group_name(w) && !(s_append(&s, group_name(w)) && s_append(&s, "\r\n"))) {
            mem_free(s);
            s = NULL;
        }
    return s;
}

static void answer_request(HWND frame, HWND client, WORD fmt, ATOM item)
{
    char name[256];                     /* atom names are at most 255 */
    LPSTR text = NULL;
    HGLOBAL h;
    BYTE FAR *p;
    WORD n;
    HWND g;

    GlobalGetAtomName(item, name, sizeof name);
    if (fmt == CF_TEXT) {
        if (!s_icmp(name, "Groups") || !s_icmp(name, "Progman"))
            text = groups_text();
        else if ((g = group_find(name)) != NULL)
            text = group_dde_text(g);
    }
    if (!text) {                        /* negative acknowledgement */
        if (!PostMessage(client, WM_DDE_ACK, (WPARAM)frame, MAKELPARAM(0, item)))
            GlobalDeleteAtom(item);
        return;
    }
    n = s_len(text);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)n + 5);
    p = h ? (BYTE FAR *)GlobalLock(h) : NULL;
    if (!p) {
        if (h)
            GlobalFree(h);
        mem_free(text);
        PostMessage(client, WM_DDE_ACK, (WPARAM)frame, MAKELPARAM(0, item));
        return;
    }
    p[0] = 0x00;                        /* fResponse | fRelease, no fAckReq */
    p[1] = 0x30;
    p[2] = (BYTE)CF_TEXT;
    p[3] = 0;
    s_cpy((char FAR *)p + 4, text, n + 1);
    GlobalUnlock(h);
    mem_free(text);
    if (!PostMessage(client, WM_DDE_DATA, (WPARAM)frame, MAKELPARAM(h, item))) {
        GlobalFree(h);
        GlobalDeleteAtom(item);
    }
}

/* ---- the messages ---- */

LRESULT dde_message(HWND frame, UINT msg, WPARAM wp, LPARAM lp)
{
    char app[32], topic[32];            /* compared with "PROGMAN" only */
    switch (msg) {
    case WM_DDE_INITIATE:
        app[0] = topic[0] = 0;
        if (LOWORD(lp))
            GlobalGetAtomName(LOWORD(lp), app, sizeof app);
        if (HIWORD(lp))
            GlobalGetAtomName(HIWORD(lp), topic, sizeof topic);
        if ((!app[0] || !s_icmp(app, "PROGMAN")) && (!topic[0] || !s_icmp(topic, "PROGMAN")))
            SendMessage((HWND)wp, WM_DDE_ACK, (WPARAM)frame,
                        MAKELPARAM(GlobalAddAtom("PROGMAN"), GlobalAddAtom("PROGMAN")));
        return 0;
    case WM_DDE_EXECUTE: {
        HGLOBAL h = (HGLOBAL)HIWORD(lp);
        LPSTR copy = NULL;
        const char FAR *cmds = h ? (const char FAR *)GlobalLock(h) : NULL;
        BOOL ok = FALSE;
        if (cmds) {
            copy = s_dup(cmds);         /* the client's block may go away */
            GlobalUnlock(h);
        }
        if (copy) {
            ok = execute(frame, copy);
            mem_free(copy);
        }
        PostMessage((HWND)wp, WM_DDE_ACK, (WPARAM)frame, MAKELPARAM(ok ? 0x8000 : 0, h));
        return 0;
    }
    case WM_DDE_REQUEST:
        answer_request(frame, (HWND)wp, LOWORD(lp), (ATOM)HIWORD(lp));
        return 0;
    case WM_DDE_TERMINATE:
        PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)frame, 0);
        return 0;
    case WM_DDE_ACK:                    /* to our data: nothing more to do */
        if (HIWORD(lp))
            GlobalDeleteAtom((ATOM)HIWORD(lp));
        return 0;
    }
    return 0;
}
