/*
 * group.c - program groups: .GRP files and the PMGroup MDI child windows.
 *
 * .GRP layout, as observed in files written by Windows 3.1 Setup and by
 * the original Program Manager (offsets in bytes, little-endian):
 *   00  "PMCC"
 *   04  checksum: makes the 16-bit word sum of the file zero (an odd
 *       final byte is summed as a word with a zero high byte)
 *   06  cbGroup: size of the group data (tags excluded)
 *   08  nCmdShow of the group window
 *   0A  rcNormal: window rectangle in MDI client coordinates
 *   12  ptMin: position when minimized (-1,-1 = not set)
 *   16  pName: offset of the group name
 *   18  cxIcon, cyIcon (32, 32), then bytes 01 04 and a word 0000
 *       (preserved; their meaning is not yet known)
 *   20  cItems: number of item slots
 *   22  rgiItems[cItems]: item offsets, 0 = empty slot
 * Item record (24 bytes): x, y (position in client coordinates), iIcon,
 *   cbHeader, cbANDPlane, cbXORPlane, pHeader, pANDPlane, pXORPlane,
 *   pName, pCommand, pIconPath. The icon header is 12 bytes: hotspot x,
 *   y, cx, cy, row bytes, planes, bits per pixel; the bits are in the
 *   display driver's format (VGA: 4 planes of 1 bit).
 * Layout of a file Program Manager writes: header, rgiItems, group name,
 *   then for each item its record, name, command, icon path, icon header,
 *   AND bits and XOR bits.
 * Tags follow cbGroup: records { WORD id, WORD item, WORD cb (whole
 *   record), BYTE data[cb - 6] }, the first { 8000h, FFFFh, 10, "PMCC" },
 *   the last { FFFFh, FFFFh, 0 }. There are none if no item has any.
 *   8102h  hotkey: WORD, low byte virtual key, high byte modifiers
 *          (06h = Ctrl+Alt)
 *   8103h  run minimized: no data
 *   8101h  working directory (expected; not yet seen: when the command
 *          has no path, the working directory is prepended to it)
 *
 * Observed: on exit the original rewrites only groups that changed; one
 * merely looked at is left byte for byte as it was.
 *
 * Item order, as observed: rgiItems is the z-order, top first; the
 * initially selected item is the top one. Selecting an item (mouse or
 * arrow keys) raises it to the top. Typing a letter first sends the
 * current item to the bottom, then raises the first item, from the top,
 * whose name starts with that letter. Deleting closes the gap and leaves
 * cItems as it was (empty slots at the end). An item's data blocks have
 * an order of their own: new items' data goes at the end of the file, a
 * deleted item's is removed.
 *
 * Each group lives in global memory as a list of items, and is written
 * back from that model in the layout above. There are no fixed limits
 * (Zero-One-Infinity) beyond the format's own: offsets are 16-bit, so a
 * group file stays below 64K.
 */
#include "progman.h"

#define CX_SPACING      75              /* observed item grid */
#define CY_SPACING      72
#define X_FIRST         21
/* observed: the icon is drawn 2 pixels right of and below the item's
   position, its title starts 1 pixel below the icon */
#define ICON_DX         2
#define ICON_DY         2
#define TITLE_DY        (ICON_DY + 32 + 1)

#define HDR_SIZE        0x22
#define ITEM_SIZE       24
#define TAG_HOTKEY      0x8102
#define TAG_MINIMIZE    0x8103

typedef struct item {
    struct item FAR *dnext;             /* data order */
    int x, y;
    WORD iicon;
    LPSTR name, cmd, iconpath;
    LPSTR icon;                         /* header + AND + XOR bits */
    WORD cbhdr, cband, cbxor;
    LPSTR tags;                         /* this item's tag records */
    WORD cbtags;
    HICON hicon;
    BOOL own_icon;
} item;

typedef struct group {
    struct group FAR *next;
    HWND hwnd;
    int ininum;                         /* N of GroupN= in PROGMAN.INI */
    LPSTR name;
    WORD show;
    RECT rc;
    POINT pmin;
    BYTE hdr18[8];                      /* header bytes 18h..1Fh */
    item FAR * FAR *z;                  /* rgiItems: z-order, top first */
    WORD nz, cap;                       /* used slots, cItems */
    item FAR *dfirst;                   /* data order */
    LPSTR gtags;                        /* other group-level tags */
    WORD cbgtags;
    item FAR *sel;
    BOOL dirty;                         /* items changed since written */
    char path[1];                       /* the .GRP path, allocated to fit */
} group;

static group FAR *first;
static HFONT title_font;
static HICON def_icon;
static char cls_group[] = "PMGroup";
static const BYTE new_hdr18[8] = { 32, 0, 32, 0, 1, 4, 0, 0 };

/* ---- little helpers ---- */

static WORD rd16(const BYTE FAR *p, WORD off)
{
    return p[off] | (p[off + 1] << 8);
}

static void wr16(BYTE FAR *p, WORD off, WORD v)
{
    p[off] = (BYTE)v;
    p[off + 1] = (BYTE)(v >> 8);
}

static void copy(void FAR *d, const void FAR *s, WORD n)
{
    BYTE FAR *dp = d;
    const BYTE FAR *sp = s;
    while (n--)
        *dp++ = *sp++;
}

/* Is a NUL-terminated string at off inside the data? */
static BOOL str_ok(const BYTE FAR *p, WORD size, WORD off)
{
    if (off == 0 || off >= size)
        return FALSE;
    while (off < size)
        if (!p[off++])
            return TRUE;
    return FALSE;
}

static WORD checksum(const BYTE FAR *p, WORD size)
{
    WORD sum = 0, i;
    for (i = 0; i + 1 < size; i += 2)
        sum += rd16(p, i);
    if (size & 1)
        sum += p[size - 1];
    return sum;
}

static group FAR *grp_of(HWND hwnd)
{
    group FAR *g;
    for (g = first; g; g = g->next)
        if (g->hwnd == hwnd)
            return g;
    return NULL;
}

static int zindex(group FAR *g, item FAR *t)
{
    WORD i;
    for (i = 0; i < g->nz; i++)
        if (g->z[i] == t)
            return i;
    return -1;
}

/* ---- items ---- */

static void free_item(item FAR *t)
{
    if (t->own_icon)
        DestroyIcon(t->hicon);
    mem_free(t->name);
    mem_free(t->cmd);
    mem_free(t->iconpath);
    mem_free(t->icon);
    mem_free(t->tags);
    mem_free((LPSTR)t);
}

/* An HICON from the cached bits, if they suit this display. TODO:
   otherwise extract the icon from iconpath. */
static void make_icon(item FAR *t)
{
    HDC dc;
    BYTE FAR *h = (BYTE FAR *)t->icon;
    t->hicon = def_icon;
    t->own_icon = FALSE;
    if (!h || t->cbhdr < 12)
        return;
    dc = GetDC(NULL);
    if (h[10] == GetDeviceCaps(dc, PLANES) && h[11] == GetDeviceCaps(dc, BITSPIXEL)) {
        HICON i = CreateIcon(g_hinst, rd16(h, 4), rd16(h, 6), h[10], h[11],
                             h + t->cbhdr, h + t->cbhdr + t->cband);
        if (i) {
            t->hicon = i;
            t->own_icon = TRUE;
        }
    }
    ReleaseDC(NULL, dc);
}

/* Append item t at the end of the data order. */
static void data_append(group FAR *g, item FAR *t)
{
    item FAR * FAR *pp = &g->dfirst;
    while (*pp)
        pp = &(*pp)->dnext;
    t->dnext = NULL;
    *pp = t;
}

static void data_remove(group FAR *g, item FAR *t)
{
    item FAR * FAR *pp;
    for (pp = &g->dfirst; *pp; pp = &(*pp)->dnext)
        if (*pp == t) {
            *pp = t->dnext;
            return;
        }
}

/* Make room for one more slot. */
static BOOL grow_slots(group FAR *g)
{
    item FAR * FAR *z;
    if (g->nz < g->cap)
        return TRUE;
    z = (item FAR * FAR *)mem_realloc((LPSTR)g->z, (DWORD)(g->cap + 1) * sizeof(item FAR *));
    if (!z)
        return FALSE;
    g->z = z;
    g->cap++;
    return TRUE;
}

/* Move the item at z-index i to the top (observed on selection). */
static void raise_item(group FAR *g, int i)
{
    item FAR *t = g->z[i];
    for (; i > 0; i--)
        g->z[i] = g->z[i - 1];
    g->z[0] = t;
}

/* Move the item at z-index i to the bottom (observed on letter keys). */
static void lower_item(group FAR *g, int i)
{
    item FAR *t = g->z[i];
    for (; i + 1 < (int)g->nz; i++)
        g->z[i] = g->z[i + 1];
    g->z[g->nz - 1] = t;
}

/* ---- parsing ---- */

static LPSTR dup_at(const BYTE FAR *p, WORD off)
{
    return s_dup((const char FAR *)p + off);
}

static BOOL parse(group FAR *g, const BYTE FAR *p, WORD fsize)
{
    WORD size, n, i, slot;
    item FAR * FAR *byslot;
    BOOL ok = FALSE;

    if (fsize < HDR_SIZE || p[0] != 'P' || p[1] != 'M' || p[2] != 'C' || p[3] != 'C')
        return FALSE;
    size = rd16(p, 6);
    if (size < HDR_SIZE || size > fsize)
        return FALSE;
    n = rd16(p, 0x20);
    if (HDR_SIZE + 2UL * n > size || !str_ok(p, size, rd16(p, 0x16)))
        return FALSE;
    g->name = dup_at(p, rd16(p, 0x16));
    g->show = rd16(p, 8);
    g->rc.left = (int)rd16(p, 0x0A);
    g->rc.top = (int)rd16(p, 0x0C);
    g->rc.right = (int)rd16(p, 0x0E);
    g->rc.bottom = (int)rd16(p, 0x10);
    g->pmin.x = (int)rd16(p, 0x12);
    g->pmin.y = (int)rd16(p, 0x14);
    copy(g->hdr18, p + 0x18, 8);
    g->cap = n;
    g->z = (item FAR * FAR *)mem_alloc((DWORD)(n ? n : 1) * sizeof(item FAR *));
    byslot = (item FAR * FAR *)mem_alloc((DWORD)(n ? n : 1) * sizeof(item FAR *));
    if (!g->name || !g->z || !byslot)
        goto out;

    for (slot = 0; slot < n; slot++) {
        WORD it = rd16(p, HDR_SIZE + 2 * slot);
        item FAR *t;
        WORD ph, pa, px;
        if (!it || it + ITEM_SIZE > size)
            continue;                   /* empty slot */
        if (!str_ok(p, size, rd16(p, it + 18)) || !str_ok(p, size, rd16(p, it + 20)))
            continue;
        t = (item FAR *)mem_alloc(sizeof(item));
        if (!t)
            goto out;
        t->x = (int)rd16(p, it);
        t->y = (int)rd16(p, it + 2);
        t->iicon = rd16(p, it + 4);
        t->name = dup_at(p, rd16(p, it + 18));
        t->cmd = dup_at(p, rd16(p, it + 20));
        t->iconpath = str_ok(p, size, rd16(p, it + 22)) ? dup_at(p, rd16(p, it + 22)) : s_dup("");
        t->cbhdr = rd16(p, it + 6);
        t->cband = rd16(p, it + 8);
        t->cbxor = rd16(p, it + 10);
        ph = rd16(p, it + 12);
        pa = rd16(p, it + 14);
        px = rd16(p, it + 16);
        if ((DWORD)ph + t->cbhdr <= size && (DWORD)pa + t->cband <= size &&
            (DWORD)px + t->cbxor <= size &&
            (t->icon = mem_alloc((DWORD)t->cbhdr + t->cband + t->cbxor)) != NULL) {
            copy(t->icon, p + ph, t->cbhdr);
            copy(t->icon + t->cbhdr, p + pa, t->cband);
            copy(t->icon + t->cbhdr + t->cband, p + px, t->cbxor);
        } else {
            t->cbhdr = t->cband = t->cbxor = 0;
        }
        if (!t->name || !t->cmd || !t->iconpath) {
            free_item(t);
            goto out;
        }
        byslot[slot] = t;
        g->z[g->nz++] = t;
        t->tags = (LPSTR)(DWORD)it;     /* sort key for the data order */
    }

    /* data order: by record offset */
    for (i = 0; i < g->nz; i++) {
        item FAR *best = NULL;
        WORD k;
        for (k = 0; k < g->nz; k++) {
            item FAR *t = g->z[k];
            if (t->tags && (!best || (DWORD)t->tags < (DWORD)best->tags))
                best = t;
        }
        best->tags = NULL;
        data_append(g, best);
    }

    /* tags: keep each with its item (by slot), others with the group */
    if (fsize > size) {
        WORD at = size;
        while (at + 6 <= fsize) {
            WORD id = rd16(p, at), it = rd16(p, at + 2), cb = rd16(p, at + 4);
            LPSTR FAR *dst;
            WORD FAR *dcb;
            if (id == 0xFFFF || cb < 6 || at + cb > fsize)
                break;
            if (id == 0x8000) {         /* PMCC: written afresh */
                at += cb;
                continue;
            }
            if (it < n && byslot[it]) {
                dst = &byslot[it]->tags;
                dcb = &byslot[it]->cbtags;
            } else {
                dst = &g->gtags;
                dcb = &g->cbgtags;
            }
            {
                LPSTR nb = mem_realloc(*dst, (DWORD)*dcb + cb);
                if (!nb)
                    goto out;
                copy(nb + *dcb, p + at, cb);
                *dst = nb;
                *dcb += cb;
            }
            at += cb;
        }
    }
    for (i = 0; i < g->nz; i++)
        make_icon(g->z[i]);
    ok = TRUE;
out:
    mem_free((LPSTR)byslot);
    return ok;
}

/* ---- serializing ---- */

/* Build the .GRP file for g in a new global block; *size receives its
   length. NULL if it would not fit the format (64K) or memory. */
static LPSTR serialize(group FAR *g, WORD *size)
{
    DWORD total, cb;
    item FAR *t;
    BOOL anytags = g->cbgtags != 0;
    LPSTR buf;
    BYTE FAR *p;
    WORD at, i;

    total = HDR_SIZE + 2UL * g->cap + s_len(g->name) + 1;
    for (t = g->dfirst; t; t = t->dnext) {
        total += ITEM_SIZE + s_len(t->name) + 1 + s_len(t->cmd) + 1 + s_len(t->iconpath) + 1 +
                 t->cbhdr + t->cband + t->cbxor;
        if (t->cbtags)
            anytags = TRUE;
    }
    cb = total;
    if (anytags) {
        total += 10 + 6 + g->cbgtags;
        for (t = g->dfirst; t; t = t->dnext)
            total += t->cbtags;
    }
    if (total > 0xFFF0UL)
        return NULL;
    buf = mem_alloc(total);
    if (!buf)
        return NULL;
    p = (BYTE FAR *)buf;

    p[0] = 'P'; p[1] = 'M'; p[2] = 'C'; p[3] = 'C';
    wr16(p, 6, (WORD)cb);
    wr16(p, 8, g->show);
    wr16(p, 0x0A, g->rc.left);
    wr16(p, 0x0C, g->rc.top);
    wr16(p, 0x0E, g->rc.right);
    wr16(p, 0x10, g->rc.bottom);
    wr16(p, 0x12, g->pmin.x);
    wr16(p, 0x14, g->pmin.y);
    copy(p + 0x18, g->hdr18, 8);
    wr16(p, 0x20, g->cap);
    at = HDR_SIZE + 2 * g->cap;
    wr16(p, 0x16, at);
    copy(p + at, g->name, s_len(g->name) + 1);
    at += s_len(g->name) + 1;

    for (t = g->dfirst; t; t = t->dnext) {
        WORD rec = at, n;
        at += ITEM_SIZE;
        wr16(p, rec, t->x);
        wr16(p, rec + 2, t->y);
        wr16(p, rec + 4, t->iicon);
        wr16(p, rec + 6, t->cbhdr);
        wr16(p, rec + 8, t->cband);
        wr16(p, rec + 10, t->cbxor);
        wr16(p, rec + 18, at);
        n = s_len(t->name) + 1;
        copy(p + at, t->name, n);
        at += n;
        wr16(p, rec + 20, at);
        n = s_len(t->cmd) + 1;
        copy(p + at, t->cmd, n);
        at += n;
        wr16(p, rec + 22, at);
        n = s_len(t->iconpath) + 1;
        copy(p + at, t->iconpath, n);
        at += n;
        wr16(p, rec + 12, at);
        wr16(p, rec + 14, at + t->cbhdr);
        wr16(p, rec + 16, at + t->cbhdr + t->cband);
        copy(p + at, t->icon, t->cbhdr + t->cband + t->cbxor);
        at += t->cbhdr + t->cband + t->cbxor;
        i = zindex(g, t);
        wr16(p, HDR_SIZE + 2 * i, rec);
    }

    if (anytags) {
        wr16(p, at, 0x8000);
        wr16(p, at + 2, 0xFFFF);
        wr16(p, at + 4, 10);
        p[at + 6] = 'P'; p[at + 7] = 'M'; p[at + 8] = 'C'; p[at + 9] = 'C';
        at += 10;
        for (i = 0; i < g->nz; i++) {   /* in slot order */
            item FAR *u = g->z[i];
            WORD k;
            copy(p + at, u->tags, u->cbtags);
            for (k = 0; k < u->cbtags; k += rd16(p, at + k + 4)) {
                wr16(p, at + k + 2, i);
                if (rd16(p, at + k + 4) < 6)
                    break;
            }
            at += u->cbtags;
        }
        copy(p + at, g->gtags, g->cbgtags);
        at += g->cbgtags;
        wr16(p, at, 0xFFFF);
        wr16(p, at + 2, 0xFFFF);
        wr16(p, at + 4, 0);
        at += 6;
    }
    wr16(p, 4, (WORD)-checksum(p, at));
    *size = at;
    return buf;
}

static BOOL write_group(group FAR *g)
{
    WORD size;
    LPSTR buf = serialize(g, &size);
    OFSTRUCT of;
    HFILE f;
    BOOL ok = FALSE;
    if (!buf)
        return FALSE;
    f = OpenFile(g->path, &of, OF_CREATE | OF_WRITE);
    if (f != HFILE_ERROR) {
        ok = _lwrite(f, buf, size) == size;
        _lclose(f);
    }
    mem_free(buf);
    return ok;
}

/* ---- loading, creating, freeing ---- */

static group FAR *new_group(const char FAR *path)
{
    group FAR *g = (group FAR *)mem_alloc(sizeof(group) + s_len(path));
    if (g)
        s_cpy(g->path, path, s_len(path) + 1);
    return g;
}

static void unlink_group(group FAR *g)
{
    group FAR * FAR *pp;
    for (pp = &first; *pp; pp = &(*pp)->next)
        if (*pp == g) {
            *pp = g->next;
            return;
        }
}

static void free_group(group FAR *g)
{
    item FAR *t = g->dfirst;
    while (t) {
        item FAR *n = t->dnext;
        free_item(t);
        t = n;
    }
    mem_free((LPSTR)g->z);
    mem_free(g->name);
    mem_free(g->gtags);
    mem_free((LPSTR)g);
}

static BOOL read_file(group FAR *g)
{
    OFSTRUCT of;
    HFILE f = OpenFile(g->path, &of, OF_READ);
    long len;
    LPSTR buf;
    BOOL ok = FALSE;

    if (f == HFILE_ERROR)
        return FALSE;
    len = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (len > 0 && len <= 0xFFF0L && (buf = mem_alloc(len)) != NULL) {
        if (_lread(f, buf, (WORD)len) == (WORD)len)
            ok = parse(g, (const BYTE FAR *)buf, (WORD)len);
        mem_free(buf);
    }
    _lclose(f);
    return ok;
}

/* Create the MDI child for g (whose model is complete) and link it in. */
static HWND open_window(group FAR *g, BOOL use_rc)
{
    MDICREATESTRUCT mc;

    g->next = first;
    first = g;
    mc.szClass = cls_group;
    mc.szTitle = g->name;
    mc.hOwner = g_hinst;
    if (use_rc) {
        mc.x = g->rc.left;
        mc.y = g->rc.top;
        mc.cx = g->rc.right - g->rc.left;
        mc.cy = g->rc.bottom - g->rc.top;
    } else {                            /* MDI's default cascade (observed) */
        mc.x = mc.y = mc.cx = mc.cy = CW_USEDEFAULT;
    }
    mc.style = 0x0115;                  /* observed low word of the style */
    mc.lParam = 0;
    g->hwnd = (HWND)(WORD)SendMessage(g_client, WM_MDICREATE, 0, (LPARAM)(LPVOID)&mc);
    if (!g->hwnd) {
        unlink_group(g);
        return NULL;
    }
    /* observed: group windows accept dropped files (Windows 3.1) */
    DragAcceptFiles(g->hwnd, TRUE);
    return g->hwnd;
}

/* The initial selection: the top item (observed; nothing moves). */
static void initial_select(group FAR *g)
{
    g->sel = g->nz ? g->z[0] : NULL;
}

/* Create the window for a group file. ininum is N of GroupN=. */
HWND group_load(const char FAR *grpfile, int ininum)
{
    group FAR *g = new_group(grpfile);

    if (!g)
        return NULL;
    g->ininum = ininum;
    if (!read_file(g)) {
        free_group(g);
        return NULL;
    }
    initial_select(g);
    if (!open_window(g, TRUE)) {
        free_group(g);
        return NULL;
    }
    if (g->pmin.x != -1 || g->pmin.y != -1) {
        /* where the group sits when minimized */
        WINDOWPLACEMENT wp;
        wp.length = sizeof wp;
        GetWindowPlacement(g->hwnd, &wp);
        wp.flags = WPF_SETMINPOSITION;
        wp.ptMinPosition = g->pmin;
        wp.showCmd = SW_SHOWNA;
        SetWindowPlacement(g->hwnd, &wp);
    }
    if (g->show == SW_SHOWMINIMIZED || g->show == SW_MINIMIZE || g->show == SW_SHOWMINNOACTIVE)
        ShowWindow(g->hwnd, SW_SHOWMINNOACTIVE);
    else if (g->show == SW_SHOWMAXIMIZED)
        SendMessage(g_client, WM_MDIMAXIMIZE, (WPARAM)g->hwnd, 0);
    /* each new group goes on top (after minimizing, which sends a window
       to the bottom), so loading in Order= sequence rebuilds the saved
       z-order */
    SetWindowPos(g->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return g->hwnd;
}

/* Take the current window state into g's model; TRUE if it differs
   from what the file holds. */
static BOOL capture_state(group FAR *g)
{
    WINDOWPLACEMENT wp;
    WORD show;
    BOOL changed;
    wp.length = sizeof wp;
    GetWindowPlacement(g->hwnd, &wp);
    show = IsIconic(g->hwnd) ? SW_SHOWMINIMIZED :
           IsZoomed(g->hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    /* a stored show command of 1 or 2 differs only in form */
    changed = !((show == g->show) ||
                (show == SW_SHOWMINIMIZED && (g->show == SW_MINIMIZE ||
                                              g->show == SW_SHOWMINNOACTIVE)));
    changed |= wp.rcNormalPosition.left != g->rc.left || wp.rcNormalPosition.top != g->rc.top ||
               wp.rcNormalPosition.right != g->rc.right ||
               wp.rcNormalPosition.bottom != g->rc.bottom;
    if (changed)
        g->show = show;
    g->rc = wp.rcNormalPosition;
    if ((wp.flags & WPF_SETMINPOSITION) || IsIconic(g->hwnd)) {
        changed |= wp.ptMinPosition.x != g->pmin.x || wp.ptMinPosition.y != g->pmin.y;
        g->pmin = wp.ptMinPosition;
    }
    return changed;
}

/* A new, empty group (File > New > Program Group): write its file and
   show it. Returns the window, or NULL. */
HWND group_create(const char FAR *name, const char FAR *path, int ininum)
{
    group FAR *g = new_group(path);
    if (!g)
        return NULL;
    g->ininum = ininum;
    g->name = s_dup(name);
    g->show = SW_SHOWNORMAL;
    g->pmin.x = g->pmin.y = -1;
    copy(g->hdr18, new_hdr18, 8);
    g->z = (item FAR * FAR *)mem_alloc(sizeof(item FAR *));
    if (!g->name || !g->z || !open_window(g, FALSE)) {
        free_group(g);
        return NULL;
    }
    capture_state(g);
    if (!write_group(g)) {
        DestroyWindow(g->hwnd);         /* frees g */
        return NULL;
    }
    return g->hwnd;
}

/* Write the window state back into each .GRP that changed (Save
   Settings on Exit). */
void group_save_all(void)
{
    group FAR *g;
    for (g = first; g; g = g->next)
        if ((capture_state(g) || g->dirty) && write_group(g))
            g->dirty = FALSE;
}

void group_destroy_all(void)
{
    while (first) {
        group FAR *g = first;
        first = g->next;
        free_group(g);
    }
}

int group_ininum(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    return g ? g->ininum : 0;
}

LPCSTR group_name(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    return g ? g->name : NULL;
}

LPCSTR group_path(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    return g ? g->path : NULL;
}

LPCSTR group_selected_name(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    return g && g->sel ? g->sel->name : NULL;
}

/* ---- layout and painting ---- */

#ifdef W16_WIN2
/* Windows 2.x's DrawText has neither DT_CALCRECT nor DT_WORDBREAK, so
   titles are wrapped here: as many words per line as fit `width` (a
   word longer than that is broken), each line centred on cx. Returns the
   height; *maxw receives the widest line. Draws only if `draw`. */
static int wrap_title(HDC dc, const char FAR *s, int width, int cx, int top, BOOL draw,
                      int *maxw)
{
    TEXTMETRIC tm;
    int y = top;
    GetTextMetrics(dc, &tm);
    *maxw = 0;
    while (*s) {
        int n = 0, fit = 0, w;
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        for (;;) {                      /* extend word by word */
            int m = n;
            while (s[m] == ' ')
                m++;
            while (s[m] && s[m] != ' ')
                m++;
            if (LOWORD(GetTextExtent(dc, s, m)) > width && fit)
                break;
            fit = n = m;
            if (!s[m] || LOWORD(GetTextExtent(dc, s, m)) > width)
                break;
        }
        while (n > 1 && LOWORD(GetTextExtent(dc, s, n)) > width)
            n--;                        /* one word too long: break it */
        w = LOWORD(GetTextExtent(dc, s, n));
        if (w > *maxw)
            *maxw = w;
        if (draw)
            TextOut(dc, cx - w / 2, y, s, n);
        y += tm.tmHeight;
        s += n;
    }
    return y - top;
}
#endif

static void title_rect(HDC dc, item FAR *t, RECT *r)
{
    HFONT old = title_font ? SelectObject(dc, title_font) : NULL;
    int cx = t->x + ICON_DX + 16;
    r->left = cx - CX_SPACING / 2 + 1;
    r->right = cx + CX_SPACING / 2 - 1;
    r->top = t->y + TITLE_DY;
    r->bottom = r->top + 100;
#ifdef W16_WIN2
    {
        int w;
        r->bottom = r->top + wrap_title(dc, t->name, r->right - r->left, cx, r->top, FALSE, &w);
        r->right = r->left + w;
    }
#else
    DrawText(dc, t->name, -1, r, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
#endif
    /* DT_CALCRECT keeps the left edge; centre the result under the icon */
    {
        int w = r->right - r->left;
        r->left = cx - w / 2;
        r->right = r->left + w;
    }
    /* observed highlight: 2 pixels either side of the text and 2 extra
       rows below it (seen for one-line titles) */
    InflateRect(r, 2, 0);
    r->bottom += 2;
    if (old)
        SelectObject(dc, old);
}

static void paint(HWND hwnd, group FAR *g)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HFONT old = title_font ? SelectObject(dc, title_font) : NULL;
    /* observed: the selection is highlighted only in the active group
       while Program Manager itself is the active window */
    BOOL active = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0) == hwnd &&
                  GetActiveWindow() == g_frame;
    WORD i;

    for (i = g->nz; i-- > 0;) {         /* bottom first */
        item FAR *t = g->z[i];
        RECT r;
        BOOL hi = active && t == g->sel;
        DrawIcon(dc, t->x + ICON_DX, t->y + ICON_DY, t->hicon);
        title_rect(dc, t, &r);
        SetBkMode(dc, hi ? OPAQUE : TRANSPARENT);
        SetBkColor(dc, GetSysColor(PM_COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(hi ? PM_COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        if (hi) {
            HBRUSH b = CreateSolidBrush(GetSysColor(PM_COLOR_HIGHLIGHT));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        InflateRect(&r, -2, 0);
        r.bottom -= 2;
#ifdef W16_WIN2
        {
            int w;
            wrap_title(dc, t->name, CX_SPACING - 2, t->x + ICON_DX + 16, r.top, TRUE, &w);
        }
#else
        DrawText(dc, t->name, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
#endif
    }
    if (old)
        SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

/* The topmost item at (x, y), or NULL. */
static item FAR *hit(HWND hwnd, group FAR *g, int x, int y)
{
    HDC dc = GetDC(hwnd);
    item FAR *found = NULL;
    int i;
    for (i = 0; i < (int)g->nz && !found; i++) {   /* top first */
        item FAR *t = g->z[i];
        RECT r;
        if (x >= t->x + ICON_DX && x < t->x + ICON_DX + 32 &&
            y >= t->y + ICON_DY && y < t->y + ICON_DY + 32)
            found = t;
        title_rect(dc, t, &r);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            found = t;
    }
    ReleaseDC(hwnd, dc);
    return found;
}

static void invalidate_item(HWND hwnd, item FAR *t)
{
    HDC dc;
    RECT r;
    if (!t)
        return;
    dc = GetDC(hwnd);
    title_rect(dc, t, &r);
    ReleaseDC(hwnd, dc);
    InvalidateRect(hwnd, &r, TRUE);
    r.left = t->x + ICON_DX;
    r.top = t->y + ICON_DY;
    r.right = r.left + 32;
    r.bottom = r.top + 32;
    InvalidateRect(hwnd, &r, TRUE);
}

static void select_item(HWND hwnd, group FAR *g, item FAR *t)
{
    int i;
    if (!t || t == g->sel)
        return;
    invalidate_item(hwnd, g->sel);
    g->sel = t;
    i = zindex(g, t);
    if (i >= 0)
        raise_item(g, i);               /* observed: selection raises */
    invalidate_item(hwnd, t);
}

/* Keyboard navigation: the nearest item in the given direction. */
static item FAR *neighbour(group FAR *g, int dx, int dy)
{
    item FAR *c = g->sel, FAR *best = NULL;
    long bestd = 0x7FFFFFFFL;
    WORD i;
    if (!c)
        return g->nz ? g->z[0] : NULL;
    for (i = 0; i < g->nz; i++) {
        item FAR *t = g->z[i];
        int ddx = t->x - c->x, ddy = t->y - c->y;
        long d;
        if (t == c)
            continue;
        if ((dx > 0 && ddx <= 0) || (dx < 0 && ddx >= 0) ||
            (dy > 0 && ddy <= 0) || (dy < 0 && ddy >= 0))
            continue;
        /* prefer the same row or column */
        d = dx ? (long)(ddx < 0 ? -ddx : ddx) + 8L * (ddy < 0 ? -ddy : ddy)
               : (long)(ddy < 0 ? -ddy : ddy) + 8L * (ddx < 0 ? -ddx : ddx);
        if (d < bestd) {
            bestd = d;
            best = t;
        }
    }
    return best;
}

BOOL group_open_selected(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    if (!g || !g->sel)
        return FALSE;
    if (pm_exec(g_frame, g->sel->cmd, NULL, FALSE) && g_minonrun)
        ShowWindow(g_frame, SW_MINIMIZE);
    return TRUE;
}

static int grid_cols(HWND hwnd)
{
    RECT rc;
    int cols;
    GetClientRect(hwnd, &rc);
    cols = (rc.right - rc.left) / CX_SPACING;
    return cols < 1 ? 1 : cols;
}

/* Arrange Icons: the observed grid, as many columns as fit. */
void group_arrange(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    int cols, i = 0;
    if (!g)
        return;
    cols = grid_cols(hwnd);
    for (t = g->dfirst; t; t = t->dnext, i++) {
        t->x = X_FIRST + (i % cols) * CX_SPACING;
        t->y = (i / cols) * CY_SPACING;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

/* The first grid cell, in reading order, that no item occupies
   (observed: the first item of an empty group goes to (21, 0)). */
static void free_cell(HWND hwnd, group FAR *g, int FAR *x, int FAR *y)
{
    int cols = grid_cols(hwnd), cell;
    for (cell = 0;; cell++) {
        int cx = X_FIRST + (cell % cols) * CX_SPACING, cy = (cell / cols) * CY_SPACING;
        WORD i;
        for (i = 0; i < g->nz; i++)
            if (g->z[i]->x == cx && g->z[i]->y == cy)
                break;
        if (i == g->nz) {
            *x = cx;
            *y = cy;
            return;
        }
    }
}

/* ---- adding and deleting items ---- */

static BOOL add_tag(item FAR *t, WORD id, const void FAR *data, WORD n)
{
    LPSTR nb = mem_realloc(t->tags, (DWORD)t->cbtags + 6 + n);
    BYTE FAR *p;
    if (!nb)
        return FALSE;
    p = (BYTE FAR *)nb + t->cbtags;
    wr16(p, 0, id);
    wr16(p, 2, 0);                      /* the slot, filled in on writing */
    wr16(p, 4, 6 + n);
    copy(p + 6, data, n);
    t->tags = nb;
    t->cbtags += 6 + n;
    return TRUE;
}

#ifndef W16_WIN2
/* Copy an icon's bits (header, AND, XOR) out of an HICON (see
   ARCHITECTURE.md 6.11: w16's USER must keep this layout). Windows 3.x
   keeps an icon as a global block holding exactly the 12-byte header
   that .GRP files store, followed by the bits (as described in published
   accounts of Windows internals; the sizes are checked here). */
static BOOL icon_bits(item FAR *t, HICON hicon)
{
    const BYTE FAR *h = (const BYTE FAR *)GlobalLock((HGLOBAL)hicon);
    DWORD have = GlobalSize((HGLOBAL)hicon);
    WORD rowb, cy, cband, cbxor;
    BOOL ok = FALSE;
    if (!h)
        return FALSE;
    rowb = rd16(h, 8);
    cy = rd16(h, 6);
    cband = ((rd16(h, 4) + 15) / 16) * 2 * cy;
    cbxor = rowb * cy * h[10];
    if (rowb && cy && (DWORD)12 + cband + cbxor <= have &&
        (t->icon = mem_alloc(12UL + cband + cbxor)) != NULL) {
        copy(t->icon, h, 12 + cband + cbxor);
        t->cbhdr = 12;
        t->cband = cband;
        t->cbxor = cbxor;
        ok = TRUE;
    }
    GlobalUnlock((HGLOBAL)hicon);
    return ok;
}

#endif

/* Is the extension of path one of the program types? */
static BOOL is_program(const char FAR *path)
{
    int n = s_len(path);
    const char FAR *e = path + n;
    while (e > path && e[-1] != '.' && e[-1] != '\\' && e[-1] != ':')
        e--;
    if (e == path || e[-1] != '.')
        return FALSE;
    return !s_icmp(e, "EXE") || !s_icmp(e, "COM") || !s_icmp(e, "PIF") || !s_icmp(e, "BAT");
}

/* The command's program file (its first word) as a new string. */
static LPSTR first_word(const char FAR *cmd)
{
    LPSTR w;
    int n = 0;
    while (*cmd == ' ')
        cmd++;
    while (cmd[n] && cmd[n] != ' ')
        n++;
    w = mem_alloc(n + 1);
    if (w)
        s_cpy(w, cmd, n + 1);
    return w;
}

/* Find the icon for a command: the program's own first icon, or the
   associated program's for a document. Fills iconpath, iicon, bits. */
static void choose_icon(item FAR *t)
{
    LPSTR file = first_word(t->cmd);
    char found[144];                    /* DOS paths: OFSTRUCT's own bound */
    OFSTRUCT of;
    HICON h = NULL;

    t->iicon = 0;
    if (file) {
        if (is_program(file)) {
            if (OpenFile(file, &of, OF_EXIST) != HFILE_ERROR)
                t->iconpath = s_dup(of.szPathName);
        } else if ((UINT)FindExecutable(file, NULL, found) > 32) {
            t->iconpath = s_dup(found);
        }
        mem_free(file);
    }
#ifdef W16_WIN2
    /* no SHELL.DLL (ExtractIcon) and no CreateIcon on Windows 2.x: the
       item keeps its icon path, with no cached bits (TODO) */
    (void)h;
    if (!t->iconpath)
        t->iconpath = s_dup("");
    make_icon(t);
#else
    if (t->iconpath) {
        h = ExtractIcon(g_hinst, t->iconpath, 0);
        if ((UINT)h <= 1)
            h = NULL;
    }
    if (!h) {
        /* our own default item icon (PROGMAN.EXE, third icon) */
        mem_free(t->iconpath);
        t->iconpath = s_dup("PROGMAN.EXE");
        t->iicon = 2;
        h = ExtractIcon(g_hinst, "PROGMAN.EXE", 2);
        if ((UINT)h <= 1)
            h = NULL;
    }
    if (h) {
        icon_bits(t, h);
        DestroyIcon(h);
    }
    if (!t->iconpath)
        t->iconpath = s_dup("");
    make_icon(t);
#endif
}

/* File > New > Program Item. dir and hotkey are optional (hotkey 0 =
   none). Observed: with a working directory and a command without a
   path, the directory is stored in front of the command. */
BOOL group_add_item(HWND hwnd, const char FAR *desc, const char FAR *cmd,
                    const char FAR *dir, WORD hotkey, BOOL minimized)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    LPSTR file;
    BOOL has_path;

    if (!g || !cmd[0] || !grow_slots(g))
        return FALSE;
    t = (item FAR *)mem_alloc(sizeof(item));
    if (!t)
        return FALSE;
    file = first_word(cmd);
    has_path = file && (file[0] && (file[1] == ':' || file[0] == '\\'));
    {
        int k;
        for (k = 0; file && file[k]; k++)
            if (file[k] == '\\')
                has_path = TRUE;
    }
    mem_free(file);
    if (dir && dir[0] && !has_path) {
        t->cmd = s_dup(dir);
        if (t->cmd && t->cmd[s_len(t->cmd) - 1] != '\\')
            s_append(&t->cmd, "\\");
        if (t->cmd) {
            const char FAR *c = cmd;
            while (*c == ' ')
                c++;
            s_append(&t->cmd, c);
        }
    } else {
        t->cmd = s_dup(cmd);
        /* TODO: tag 8101h for a working directory with a pathed command */
    }
    if (desc && desc[0]) {
        t->name = s_dup(desc);
    } else {
        /* UNVERIFIED: an empty description becomes the program's name */
        LPSTR w = first_word(cmd);
        const char FAR *b = w;
        int k;
        for (k = 0; w && w[k]; k++)
            if (w[k] == '\\' || w[k] == ':')
                b = w + k + 1;
        t->name = s_dup(b ? b : "");
        for (k = 0; t->name && t->name[k]; k++)
            if (t->name[k] == '.')
                t->name[k] = 0;
        mem_free(w);
    }
    if (!t->name || !t->cmd) {
        free_item(t);
        return FALSE;
    }
    if (hotkey && !add_tag(t, TAG_HOTKEY, &hotkey, 2)) {
        free_item(t);
        return FALSE;
    }
    if (minimized && !add_tag(t, TAG_MINIMIZE, NULL, 0)) {
        free_item(t);
        return FALSE;
    }
    choose_icon(t);
    free_cell(hwnd, g, &t->x, &t->y);
    {                                   /* on top */
        WORD k;
        for (k = g->nz; k > 0; k--)
            g->z[k] = g->z[k - 1];
        g->z[0] = t;
        g->nz++;
    }
    data_append(g, t);
    if (!write_group(g)) {
        int k;
        for (k = 0; k + 1 < (int)g->nz; k++)
            g->z[k] = g->z[k + 1];
        g->nz--;
        data_remove(g, t);
        free_item(t);
        return FALSE;
    }
    invalidate_item(hwnd, g->sel);
    g->sel = t;
    if (g_autoarrange)
        group_arrange(hwnd);
    invalidate_item(hwnd, t);
    return TRUE;
}

/* Delete the selected item (observed: the slot count stays, the gap
   closes). */
BOOL group_delete_selected(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    int i;
    if (!g || !g->sel)
        return FALSE;
    t = g->sel;
    i = zindex(g, t);
    if (i < 0)
        return FALSE;
    invalidate_item(hwnd, t);
    for (; i + 1 < (int)g->nz; i++)
        g->z[i] = g->z[i + 1];
    g->nz--;
    data_remove(g, t);
    /* UNVERIFIED: which item is selected next; the top one here */
    g->sel = g->nz ? g->z[0] : NULL;
    invalidate_item(hwnd, g->sel);
    free_item(t);
    return write_group(g);
}

/* ---- the window ---- */

LRESULT CALLBACK __export GroupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;

    switch (msg) {
    case WM_PAINT:
        if (g) {
            paint(hwnd, g);
            return 0;
        }
        break;
    case WM_MDIACTIVATE:
    case WM_NCACTIVATE:                 /* also sent when the frame changes */
        if (g)
            invalidate_item(hwnd, g->sel);
        break;
    case WM_SIZE:
        if (g && g_autoarrange && wp != SIZE_MINIMIZED)
            group_arrange(hwnd);
        break;
    case WM_LBUTTONDOWN:
        SendMessage(g_client, WM_MDIACTIVATE, (WPARAM)hwnd, 0);
        if (g && (t = hit(hwnd, g, (short)LOWORD(lp), (short)HIWORD(lp))) != NULL)
            select_item(hwnd, g, t);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (g && (t = hit(hwnd, g, (short)LOWORD(lp), (short)HIWORD(lp))) != NULL) {
            select_item(hwnd, g, t);
            group_open_selected(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (!g)
            break;
        switch (wp) {
        case VK_LEFT:  t = neighbour(g, -1, 0); break;
        case VK_RIGHT: t = neighbour(g, 1, 0);  break;
        case VK_UP:    t = neighbour(g, 0, -1); break;
        case VK_DOWN:  t = neighbour(g, 0, 1);  break;
        case VK_HOME:  t = g->dfirst;           break;
        case VK_END:   for (t = g->dfirst; t && t->dnext; t = t->dnext)
                           ;
                       break;
        default:       return 0;
        }
        select_item(hwnd, g, t);
        return 0;
    case WM_CHAR:
        /* observed: the current item goes to the bottom, then the first
           item from the top whose name starts with the letter is
           selected (and so raised) */
        if (g && g->nz) {
            char c = (char)wp;
            WORD k;
            int cur = g->sel ? zindex(g, g->sel) : -1;
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
            for (k = 0; k < g->nz; k++) {
                char f = g->z[k]->name[0];
                if (f >= 'a' && f <= 'z')
                    f -= 'a' - 'A';
                if (f == c)
                    break;
            }
            if (k == g->nz)
                return 0;               /* UNVERIFIED: no match, nothing moves */
            if (cur >= 0)
                lower_item(g, cur);
            for (k = 0; k < g->nz; k++) {
                char f = g->z[k]->name[0];
                if (f >= 'a' && f <= 'z')
                    f -= 'a' - 'A';
                if (f == c) {
                    item FAR *t = g->z[k];
                    if (t == g->sel)    /* the only match: raise it again */
                        raise_item(g, k);
                    else
                        select_item(hwnd, g, t);
                    break;
                }
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        /* observed: closing a group (Ctrl+F4, its Close command) only
           minimizes it; it stays in Order= and its file */
        if ((wp & 0xFFF0) != SC_CLOSE)
            break;
        /* fall through */
    case WM_CLOSE:
        ShowWindow(hwnd, SW_MINIMIZE);
        return 0;
    case WM_DROPFILES:
        DragFinish((HDROP)wp);          /* TODO: add items for the files */
        return 0;
    case WM_DESTROY:
        if (g) {
            unlink_group(g);
            free_group(g);
        }
        break;
    }
    return DefMDIChildProc(hwnd, msg, wp, lp);
}

BOOL group_register(void)
{
    WNDCLASS wc;
#ifndef W16_WIN2
    LOGFONT lf;                         /* Windows 2.x: the system font */
#endif

#ifdef W16_WIN2
    def_icon = LoadIcon(NULL, IDI_APPLICATION); /* TODO: 2.x-format icons */
#else
    def_icon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_DEFITEM));
#endif
#ifndef W16_WIN2
    /* the icon title font, as the user configured it (Windows 3.1) */
    if (SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0))
        title_font = CreateFontIndirect(&lf);
#else
    {   /* Windows 2.x: 8-point Helv, the small font of the time */
        HDC dc = GetDC(NULL);
        int h = (8 * GetDeviceCaps(dc, LOGPIXELSY) + 36) / 72;
        ReleaseDC(NULL, dc);
        title_font = CreateFont(-h, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                VARIABLE_PITCH | FF_SWISS, "Helv");
    }
#endif

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = GroupWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
#ifdef W16_WIN2
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);     /* TODO: 2.x icons */
#else
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_GROUP));
#endif
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_group;
    return RegisterClass(&wc) != 0;
}
