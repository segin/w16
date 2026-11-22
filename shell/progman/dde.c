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
 * - The command block comes back with a NUL at the end of each argument
 *   of the commands run (see ends below).
 * - WM_DDE_TERMINATE is answered with a sent WM_DDE_TERMINATE.
 * Observed with the DDE spy (tests/ole/ocspy.c run by OC1, ARCHITECTURE.md
 * 16.1, 9.6) on TDDE and on the 7-Zip 26.05 16-bit installer: the same
 * messages, atoms, status words and data as 3.1's Program Manager. One
 * difference remains: 3.1 answered the installer's whole conversation
 * within one 55 ms tick, w16 takes about 0.5 s (each command writes the
 * group file).
 * Raw DDE messages exist since Windows 2.0, so both builds have this.
 */
#include "progman.h"
#include <dde.h>

/* Where each argument of the commands run ends in the client's command
   block: 3.1's Program Manager writes a NUL there (observed with TDDE,
   tests/scripts/dde/block.txt: "[AddItem(NOTEPAD.EXE,Note Pad)]" comes
   back as "[AddItem(NOTEPAD.EXE\0Note Pad\0]"; a quoted argument's
   closing quote, an unquoted one's first trailing blank or its ',' or ')'
   becomes the NUL; nothing for an unknown command). w16 parses a copy and
   writes them into the block before the ACK. */
static WORD FAR *ends;
static WORD nends, capends;
static BOOL noting;                     /* the first pass: note them */

static void add_end(WORD at)
{
    if (!noting)
        return;
    if (nends == capends) {
        WORD FAR *n = (WORD FAR *)mem_realloc((LPSTR)ends, (DWORD)(capends + 16) * sizeof(WORD));
        if (!n)
            return;
        ends = n;
        capends += 16;
    }
    ends[nends++] = at;
}

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

/* Windows 3.1's lengths (observed, tests/scripts/dde/longname.txt): a
   group name is cut after 30 characters, an item title after 40, a NUL
   written into the block there. w16 has no such limits ([w16]
   ProgmanUnlimited=1, the default; 0 is stock 3.1). */
static WORD arg_limit(const char FAR *cmd, int argi)
{
    if (!g_stock_limits)
        return 0;
    if (argi == 0 && !s_icmp(cmd, "CreateGroup"))
        return 30;
    if (argi == 1 && !s_icmp(cmd, "AddItem"))
        return 40;
    return 0;
}

/* Parse one "Name(args)" at *ps (after '['); fills name and a, and
   notes where each argument ends (offsets from base, add_end). */
static BOOL parse_command(const char FAR *base, const char FAR **ps, LPSTR *name, args *a)
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
        while (*s == ' ')
            s++;
        /* "()": no arguments at all (observed: [AddItem()] and
           [CreateGroup()] fail with the block untouched, [Reload()]
           reloads every group, tests/scripts/dde/args.txt, reload.txt) */
        if (*s == ')') {
            s++;
            goto closed;
        }
        for (;;) {                      /* one argument per round */
            LPSTR one = s_dup("");
            char c[2];
            BOOL quoted = FALSE, any = FALSE;
            const char FAR *end, FAR *cut = NULL;
            WORD keep = 0, len = 0;     /* up to the last quoted or non-blank */
            WORD limit = arg_limit(*name, a->argc);
            while (*s == ' ')
                s++;
            end = s;
            c[1] = 0;
            while (*s && one) {
                if (*s == '"') {
                    quoted = !quoted;
                    any = TRUE;
                    s++;
                    continue;
                }
                if (!quoted && (*s == ',' || *s == ')'))
                    break;
                if (limit && len == limit) {
                    /* past 3.1's length: cut there (a NUL in the block,
                       besides the one at the argument's end) */
                    if (!cut)
                        cut = s;
                    if (quoted || *s != ' ')
                        end = s + 1;
                    s++;
                    continue;
                }
                if (quoted || *s != ' ') {
                    end = s + 1;
                    keep = len + 1;
                }
                any = TRUE;
                c[0] = *s++;
                len++;
                if (!s_append(&one, c)) {
                    mem_free(one);
                    one = NULL;
                }
            }
            if (!one)
                return FALSE;
            /* an empty last argument is none (observed: [AddItem(N,T,,,,)]
               fails as five arguments, like [AddItem(N,T,,,40)], and its
               ')' stays; [AddItem(N,T,,,,,,,0,)] works as nine) */
            if (!any && *s == ')' && a->argc) {
                mem_free(one);
                s++;
                break;
            }
            if (cut)
                add_end((WORD)(cut - base));
            if (*end)
                add_end((WORD)(end - base));
            /* trailing blanks go, but not those inside quotes (observed:
               CreateGroup("  Spaced  ") makes a group of that very name,
               tests/scripts/dde/spaced.txt) */
            one[keep] = 0;
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
closed:
    while (*s == ' ')
        s++;
    if (*s != ']')
        return FALSE;
    *ps = s + 1;
    return TRUE;
}

/* AddItem, DeleteItem and ReplaceItem work on the active group (SDK;
   observed: after ShowGroup(A,1) left restored group A behind the active
   B, AddItem went to B) */
static HWND current_group(void)
{
    return (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
}

/* ExitProgman's request, carried out once the whole block has run:
   0 none, 1 close as the user would, 2 save first (observed: 3.1 ran
   "[ExitProgman(0)][CreateGroup(After)]" through, made the group, sent no
   ACK and ended; tests/scripts/dde/exit0.txt) */
static int exit_req;

/* Reread group g's file (Reload). */
static void reload_one(HWND g)
{
    LPSTR path = s_dup(group_path(g));
    int n = group_ininum(g);
    SendMessage(g_client, WM_MDIDESTROY, (WPARAM)g, 0);
    if (path) {
        /* observed: the reloaded group is the active one (reload.txt) */
        if ((g = group_load(path, n)) != NULL)
            SendMessage(g_client, WM_MDIACTIVATE, (WPARAM)g, 0);
        mem_free(path);
    }
}

/* Run one command; FALSE if it failed. The argument counts are 3.1's
   (observed, tests/scripts/dde/args.txt): ShowGroup takes exactly two,
   DeleteItem and ReplaceItem one, CreateGroup and DeleteGroup at least
   one, AddItem one to nine but not five (a position needs x and y). */
static BOOL run_command(HWND frame, const char FAR *name, const args *a)
{
    HWND g;
    if (!s_icmp(name, "CreateGroup")) {
        /* SDK: an existing group of that name is just activated */
        if (a->argc < 1)
            return FALSE;
        g = group_find(arg(a, 0));
        if (!g)
            g = create_group_named(arg(a, 0), arg(a, 1));
        /* observed: a group file that cannot be written (no such
           directory) makes no group, and the command still succeeds
           (tests/scripts/dde/badpath.txt) */
        if (!g)
            return TRUE;
        group_show(g, SW_SHOWNORMAL);
        SendMessage(g_client, WM_MDIACTIVATE, (WPARAM)g, 0);
        return TRUE;
    }
    if (!s_icmp(name, "ShowGroup")) {
        if (a->argc != 2 || (g = group_find(arg(a, 0))) == NULL)
            return FALSE;
        group_show(g, arg_int(a, 1));
        return TRUE;
    }
    if (!s_icmp(name, "DeleteGroup")) {
        if (a->argc < 1 || (g = group_find(arg(a, 0))) == NULL)
            return FALSE;
        delete_group(g);
        return TRUE;
    }
    if (!s_icmp(name, "Reload")) {
        /* SDK: a group's file read again; without a name every group
           unloaded and [Groups] read again. Observed: [Reload()] and
           [Reload] are that, Reload(no such group) fails (reload.txt).
           Not matched: 3.1 then leaves the reloaded groups out of its
           "Groups" answer (ARCHITECTURE.md 9.6) */
        if (a->argc) {
            if ((g = group_find(arg(a, 0))) == NULL)
                return FALSE;
            reload_one(g);
            return TRUE;
        }
        for (;;) {                      /* each pass starts afresh: no stale */
            for (g = GetWindow(g_client, GW_CHILD); g && !group_name(g);
                 g = GetWindow(g, GW_HWNDNEXT))
                ;                       /* (icon titles are children too) */
            if (!g)
                break;
            SendMessage(g_client, WM_MDIDESTROY, (WPARAM)g, 0);
        }
        load_groups();
        return TRUE;
    }
    if (!s_icmp(name, "AddItem")) {
        /* AddItem(command[,name[,iconpath[,index[,x,y[,dir[,hotkey[,minimize]]]]]]]) */
        itemprops ip;
        BOOL ok;
        g = current_group();
        if (!g || a->argc < 1 || a->argc == 5 || a->argc > 9 || !arg(a, 0)[0])
            return FALSE;
        /* observed: the 51st item fails (longname.txt, many.txt); w16 has
           no such limit unless [w16] ProgmanUnlimited=0 */
        if (g_stock_limits && group_count(g) >= 50) {
            group_slot_only(g);
            return FALSE;
        }
        ip.cmd = s_dup(arg(a, 0));
        ip.desc = s_dup(arg(a, 1));
        ip.dir = s_dup(arg(a, 6));
        ip.iconpath = a->argc > 2 && arg(a, 2)[0] ? s_dup(arg(a, 2)) : NULL;
        ip.iconindex = arg_int(a, 3);
        /* observed: x and y given, even empty (0), place the item */
        ip.has_pos = a->argc >= 6;
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
        return g && a->argc == 1 && group_delete_named(g, arg(a, 0));
    }
    if (!s_icmp(name, "ReplaceItem")) {
        g = current_group();
        return g && a->argc == 1 && group_replace_named(g, arg(a, 0));
    }
    if (!s_icmp(name, "ExitProgman")) {
        /* ignored when Program Manager is the shell (SDK), and under
           [Restrictions] NoClose (observed: Program Manager stayed; the
           other restrictions do not apply to DDE). Observed: with
           ExitProgman(1) the groups and settings are saved even with
           Save Settings on Exit off, with ExitProgman(0) as on any exit
           (exit1.txt, exit0.txt); [ExitProgman()] failed (exit2.txt) */
        if (a->argc != 1)
            return FALSE;
        if (!g_isshell && !g_noclose)
            exit_req = arg_int(a, 0) ? 2 : 1;
        (void)frame;
        return TRUE;
    }
    return FALSE;                       /* observed: unknown -> negative ack */
}

static const char *const known[] = {
    "CreateGroup", "ShowGroup", "DeleteGroup", "Reload", "AddItem", "DeleteItem",
    "ReplaceItem", "ExitProgman"
};

/* One pass over the commands: the first parses them all and notes
   where their arguments end, stopping at an unknown command (nothing is
   run then); the second runs them in order up to the first that fails.
   Observed: "[DeleteItem(Nope)][AddItem(CLOCK.EXE,After)]" came back with
   both commands' NULs and added nothing; "[Bogus()][AddItem(...)]" came
   back untouched (tests/scripts/dde/block.txt). UNVERIFIED: what a known
   command before an unknown one does. */
static BOOL pass(HWND frame, const char FAR *cmds, BOOL run)
{
    const char FAR *s = cmds;
    for (;;) {
        LPSTR name;
        args a;
        BOOL ok;
        WORD had = nends, k;
        while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t')
            s++;
        if (!*s)
            return TRUE;
        if (*s++ != '[')
            return FALSE;
        ok = parse_command(cmds, &s, &name, &a);
        for (k = 0; name && k < sizeof known / sizeof known[0] && s_icmp(name, known[k]); k++)
            ;
        if (!name || k == sizeof known / sizeof known[0]) {
            nends = had;                /* an unknown command is left as it is */
            ok = FALSE;
        }
        if (ok && run)
            ok = run_command(frame, name, &a);
        mem_free(name);
        mem_free(a.buf);
        if (!ok)
            return FALSE;
    }
}

static BOOL execute(HWND frame, const char FAR *cmds)
{
    BOOL ok;
    noting = TRUE;
    ok = pass(frame, cmds, FALSE);
    noting = FALSE;
    return ok && pass(frame, cmds, TRUE);
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

    name[0] = 0;                        /* (an atom that is no atom) */
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
        if (!PostMessage(client, WM_DDE_ACK, (WPARAM)frame, MAKELPARAM(0, item)))
            GlobalDeleteAtom(item);
        return;
    }
    p[0] = 0x00;                        /* fResponse | fRelease, no fAckReq */
    p[1] = 0x30;
    p[2] = (BYTE)CF_TEXT;
    p[3] = 0;
    {                                   /* (n may pass 32K: no int lengths) */
        WORD k;
        for (k = 0; k <= n; k++)
            p[4 + k] = text[k];
    }
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
        /* (an atom that is no atom names nothing: not a wildcard) */
        if (LOWORD(lp) && !GlobalGetAtomName(LOWORD(lp), app, sizeof app))
            return 0;
        if (HIWORD(lp) && !GlobalGetAtomName(HIWORD(lp), topic, sizeof topic))
            return 0;
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
            WORD n = s_len(copy), k;
            char FAR *p;
            /* a command can let messages in (a message box): another
               EXECUTE then gets the ends of its own */
            WORD FAR *o_ends = ends;
            WORD o_nends = nends, o_capends = capends;
            BOOL o_noting = noting;
            ends = NULL;
            nends = capends = 0;
            ok = execute(frame, copy);
            mem_free(copy);
            if (nends && (p = (char FAR *)GlobalLock(h)) != NULL) {
                for (k = 0; k < nends; k++)
                    if (ends[k] < n)
                        p[ends[k]] = 0;
                GlobalUnlock(h);
            }
            mem_free((LPSTR)ends);
            ends = o_ends;
            nends = o_nends;
            capends = o_capends;
            noting = o_noting;
        }
        if (exit_req) {
            /* observed: no ACK at all, Program Manager just ends, also
               when a later command failed (exit3.txt) */
            int how = exit_req;
            exit_req = 0;
            pm_exit(how == 2);
            return 0;
        }
        PostMessage((HWND)wp, WM_DDE_ACK, (WPARAM)frame, MAKELPARAM(ok ? 0x8000 : 0, h));
        return 0;
    }
    case WM_DDE_REQUEST:
        answer_request(frame, (HWND)wp, LOWORD(lp), (ATOM)HIWORD(lp));
        return 0;
    case WM_DDE_TERMINATE:
        /* answered with SendMessage, each time one comes (observed with
           the OCSPY DDE spy: TDDE's TERMINATE got one sent back; the 7-Zip
           16-bit installer, which answers a server's TERMINATE with
           another, got two) */
        if (IsWindow((HWND)wp))
            SendMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)frame, 0);
        return 0;
    case WM_DDE_ACK:                    /* to our data: nothing more to do */
        if (HIWORD(lp))
            GlobalDeleteAtom((ATOM)HIWORD(lp));
        return 0;
    }
    return 0;
}
