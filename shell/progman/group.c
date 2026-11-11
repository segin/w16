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
    BOOL have_replaced;                 /* DDE ReplaceItem: next position */
    int replaced_x, replaced_y;
    WORD replaced_z;
    LPSTR path;                         /* the .GRP file */
} group;

static group FAR *first;

static void remove_item(HWND hwnd, group FAR *g, item FAR *t);
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

/* An icon file as named in a group (a new string): a bare name
   ("write.exe") is searched for as programs are; our own stock icons
   ("PROGMAN.EXE") come from this very program, wherever it lives. */
LPSTR icon_file(const char FAR *name)
{
    OFSTRUCT of;
    char own[144];                      /* DOS paths: OFSTRUCT's own bound */
    if (!s_icmp(name, "PROGMAN.EXE") && GetModuleFileName(g_hinst, own, sizeof own))
        return s_dup(own);
    if (OpenFile(name, &of, OF_EXIST) != HFILE_ERROR)
        return s_dup(of.szPathName);
    return s_dup(name);
}

/* An HICON from the cached bits if they suit this display, else from the
   icon's file, else the default icon. */
static void make_icon(item FAR *t)
{
    HDC dc;
    BYTE FAR *h = (BYTE FAR *)t->icon;
    t->hicon = def_icon;
    t->own_icon = FALSE;
    if (h && t->cbhdr >= 12) {
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
    /* The cached bits are for another display (or missing): take the icon
       from its file instead, for showing only; the file keeps its cache.
       UNVERIFIED: whether the original also rewrites the cached bits. */
    if (!t->own_icon && t->iconpath && t->iconpath[0]) {
        LPSTR path = icon_file(t->iconpath);
        HICON i = path ? icon_extract(path, t->iicon) : NULL;
        mem_free(path);
        if (i) {
            t->hicon = i;
            t->own_icon = TRUE;
        }
    }
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
        /* in data order (observed: items dropped together, which go on
           top of the z-order in reverse, have their tags in the order of
           their data) */
        for (t = g->dfirst; t; t = t->dnext) {
            item FAR *u = t;
            WORD k;
            i = zindex(g, u);
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
    group FAR *g = (group FAR *)mem_alloc(sizeof(group));
    if (g && (g->path = s_dup(path)) == NULL) {
        mem_free((LPSTR)g);
        g = NULL;
    }
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
    mem_free(g->path);
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
    /* observed: a group whose file has an empty rectangle (as a newly
       created one has until settings are saved) gets MDI's default
       position and size */
    if (use_rc && g->rc.right > g->rc.left && g->rc.bottom > g->rc.top) {
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
    group_rescroll(g->hwnd);
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
    /* observed: a group never minimized is saved with (-1,-1), which is
       also what GetWindowPlacement reports for it */
    changed |= wp.ptMinPosition.x != g->pmin.x || wp.ptMinPosition.y != g->pmin.y;
    g->pmin = wp.ptMinPosition;
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
    /* observed: a new group's file starts with show 0, rectangle and
       minimized position all 0; the window state is saved on exit */
    g->show = 0;
    g->pmin.x = g->pmin.y = 0;
    copy(g->hdr18, new_hdr18, 8);
    g->z = (item FAR * FAR *)mem_alloc(sizeof(item FAR *));
    if (!g->name || !g->z || !open_window(g, FALSE)) {
        free_group(g);
        return NULL;
    }
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
    itemprops ip;
    BOOL ok;
    if (!g || !g->sel || !group_get_props(hwnd, &ip))
        return FALSE;
    /* run the program from its own path, in the working directory */
    ok = pm_exec(g_frame, ip.cmd, ip.dir, ip.minimized);
    itemprops_free(&ip);
    if (ok && g_minonrun)
        ShowWindow(g_frame, SW_MINIMIZE);
    return TRUE;
}

/* How many grid columns fit the group's client area. For a minimized
   group that is the area it has when restored (observed: items moved or
   copied into a minimized group fill its normal-size grid). */
static int grid_cols(HWND hwnd)
{
    RECT rc;
    int cols, width;
    group FAR *g = grp_of(hwnd);
    if (g && IsIconic(hwnd)) {
        width = g->rc.right - g->rc.left - 2 * GetSystemMetrics(SM_CXFRAME);
    } else {
        GetClientRect(hwnd, &rc);
        width = rc.right - rc.left;
    }
    cols = width / CX_SPACING;
    return cols < 1 ? 1 : cols;
}

/* Scroll bars: a group whose items reach outside its client area gets a
   bar for that direction, its range from the items' extent less the
   client area, at position 0 (Windows 3.1: Accessories, 416 x 202, its
   third row cut off, shows a vertical bar with the thumb at the top and
   no horizontal one; w16 showed none). Scrolling moves the items, as
   USER's ScrollChildren moves MDI children; the range is worked out
   again when it ends. UNVERIFIED: the range's exact ends, the step of
   the arrows (8 pixels here) and of a page (the client area). */
static BOOL in_rescroll;

void group_rescroll(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    RECT c, u, r;
    HDC dc;
    WORD i;
    int bar;
    if (!g || IsIconic(hwnd) || in_rescroll)
        return;
    in_rescroll = TRUE;
    GetClientRect(hwnd, &c);
    u = c;
    dc = GetDC(hwnd);
    for (i = 0; i < g->nz; i++) {
        item FAR *t = g->z[i];
        title_rect(dc, t, &r);
        UnionRect(&u, &u, &r);
        SetRect(&r, t->x + ICON_DX, t->y + ICON_DY, t->x + ICON_DX + 32, t->y + ICON_DY + 32);
        UnionRect(&u, &u, &r);
    }
    ReleaseDC(hwnd, dc);
    for (bar = SB_HORZ; bar <= SB_VERT; bar++) {
        int lo = bar == SB_HORZ ? u.left : u.top;
        int hi = bar == SB_HORZ ? u.right - c.right : u.bottom - c.bottom;
        if (lo < 0 || hi > 0) {
            SetScrollRange(hwnd, bar, lo, hi, FALSE);
            SetScrollPos(hwnd, bar, 0, TRUE);
        } else
            SetScrollRange(hwnd, bar, 0, 0, TRUE);      /* hides it */
    }
    in_rescroll = FALSE;
    /* a bar shown or hidden changed the client area: once more */
    GetClientRect(hwnd, &r);
    if (r.right != c.right || r.bottom != c.bottom)
        group_rescroll(hwnd);
}

static void group_scroll(HWND hwnd, int bar, WPARAM code, int thumb)
{
    group FAR *g = grp_of(hwnd);
    int lo, hi, pos, np, d;
    RECT c;
    WORD i;
    if (!g)
        return;
    GetScrollRange(hwnd, bar, &lo, &hi);
    pos = GetScrollPos(hwnd, bar);
    GetClientRect(hwnd, &c);
    switch (code) {
    case SB_LINEUP:        np = pos - 8; break;
    case SB_LINEDOWN:      np = pos + 8; break;
    case SB_PAGEUP:        np = pos - (bar == SB_VERT ? c.bottom : c.right); break;
    case SB_PAGEDOWN:      np = pos + (bar == SB_VERT ? c.bottom : c.right); break;
    case SB_TOP:           np = lo; break;
    case SB_BOTTOM:        np = hi; break;
    case SB_THUMBPOSITION: np = thumb; break;
    case SB_ENDSCROLL:     group_rescroll(hwnd); return;
    default:               return;
    }
    if (np < lo)
        np = lo;
    if (np > hi)
        np = hi;
    d = np - pos;
    if (!d)
        return;
    for (i = 0; i < g->nz; i++) {
        if (bar == SB_HORZ)
            g->z[i]->x -= d;
        else
            g->z[i]->y -= d;
    }
    SetScrollPos(hwnd, bar, np, TRUE);
    ScrollWindow(hwnd, bar == SB_HORZ ? -d : 0, bar == SB_VERT ? -d : 0, NULL, NULL);
    UpdateWindow(hwnd);
}

/* Arrange Icons: the observed grid, as many columns as fit. The items
   keep their reading order: observed with Auto Arrange on, an item
   dropped at (14,7) came before one at (21,0), and one dropped at
   (364,112) after every item of the row at 72. Taken here as rows of
   the grid's height (y / CY_SPACING), then x; items that tie keep their
   data order. UNVERIFIED beyond those cases. */
static int band(int y)
{
    return y >= 0 ? y / CY_SPACING : -((CY_SPACING - 1 - y) / CY_SPACING);
}

static BOOL before(item FAR *a, item FAR *b)
{
    int ra = band(a->y), rb = band(b->y);
    return ra < rb || (ra == rb && a->x < b->x);
}

void group_arrange(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    item FAR * FAR *v;
    WORD n = 0, i, j;
    int cols;
    if (!g)
        return;
    cols = grid_cols(hwnd);
    for (t = g->dfirst; t; t = t->dnext)
        n++;
    v = (item FAR * FAR *)mem_alloc((DWORD)(n ? n : 1) * sizeof(item FAR *));
    if (!v)
        return;
    for (i = 0, t = g->dfirst; t; t = t->dnext) {   /* stable insertion sort */
        for (j = i; j > 0 && before(t, v[j - 1]); j--)
            v[j] = v[j - 1];
        v[j] = t;
        i++;
    }
    for (i = 0; i < n; i++) {
        v[i]->x = X_FIRST + (i % cols) * CX_SPACING;
        v[i]->y = (i / cols) * CY_SPACING;
    }
    mem_free((LPSTR)v);
    InvalidateRect(hwnd, NULL, TRUE);
    group_rescroll(hwnd);
}

/* The first free grid cell in reading order. Observed (items dropped
   from File Manager): a cell is taken when any item's own cell-sized
   rectangle (CX_SPACING x CY_SPACING from its position) overlaps it, so
   an item off the grid blocks the cells it straddles; and the grid
   starts at the smallest x and y of the items (an item at x=14 put the
   next one at x=314 rather than 321). An empty group starts at
   (X_FIRST, 0) (observed). UNVERIFIED: whether File > New places items
   the same way (it does for groups on the grid, where both agree). */
static void free_cell(HWND hwnd, group FAR *g, int FAR *x, int FAR *y)
{
    int cols = grid_cols(hwnd), cell, x0 = X_FIRST, y0 = 0;
    WORD i;
    for (i = 0; i < g->nz; i++) {
        if (!i || g->z[i]->x < x0)
            x0 = g->z[i]->x;
        if (!i || g->z[i]->y < y0)
            y0 = g->z[i]->y;
    }
    for (cell = 0;; cell++) {
        int cx = x0 + (cell % cols) * CX_SPACING, cy = y0 + (cell / cols) * CY_SPACING;
        for (i = 0; i < g->nz; i++) {
            item FAR *t = g->z[i];
            if (t->x < cx + CX_SPACING && cx < t->x + CX_SPACING &&
                t->y < cy + CY_SPACING && cy < t->y + CY_SPACING)
                break;
        }
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

/* Cache icon `index` of file `name` in t (bits only): from SHELL's
   ExtractIcon (3.x), else read from the file ourselves (iconx.c). */
static BOOL fetch_bits(item FAR *t, const char FAR *name, int index)
{
    LPSTR path = icon_file(name);
    BOOL ok = FALSE;
    if (!path)
        return FALSE;
#ifndef W16_WIN2
    {
        HICON h = ExtractIcon(g_hinst, path, index);
        if ((UINT)h > 1) {
            ok = icon_bits(t, h);
            DestroyIcon(h);
        }
    }
#endif
    if (!ok) {
        t->icon = iconx_bits(path, index, &t->cband, &t->cbxor);
        if (t->icon) {
            t->cbhdr = 12;
            ok = TRUE;
        }
    }
    mem_free(path);
    return ok;
}

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

/* The selected item's icon file (a new string) and index. */
LPSTR group_selected_icon(HWND hwnd, WORD FAR *index)
{
    group FAR *g = grp_of(hwnd);
    if (!g || !g->sel)
        return NULL;
    *index = g->sel->iicon;
    return s_dup(g->sel->iconpath);
}

/* Give item t icon `index` of file `path` (Change Icon, DDE AddItem):
   path and index are stored, the bits cached, the old icon dropped. */
static void set_icon(item FAR *t, const char FAR *path, WORD index)
{
    if (t->own_icon)
        DestroyIcon(t->hicon);
    t->own_icon = FALSE;
    mem_free(t->icon);
    mem_free(t->iconpath);
    t->icon = NULL;
    t->cbhdr = t->cband = t->cbxor = 0;
    t->iconpath = s_dup(path);
    t->iicon = index;
    fetch_bits(t, path, index);
    if (!t->iconpath)
        t->iconpath = s_dup("");
    make_icon(t);
}

/* Find the icon for a command: the program's own first icon, or the
   associated program's for a document. Fills iconpath, iicon, bits. */
static void choose_icon(item FAR *t, const char FAR *cmd)
{
    LPSTR file = first_word(cmd);
    char found[144];                    /* DOS paths: OFSTRUCT's own bound */
    OFSTRUCT of;

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
    if (!t->iconpath || !fetch_bits(t, t->iconpath, 0)) {
        /* our own default item icon (PROGMAN.EXE, third icon) */
        mem_free(t->iconpath);
        t->iconpath = s_dup("PROGMAN.EXE");
        t->iicon = 2;
        fetch_bits(t, "PROGMAN.EXE", 2);
    }
    if (!t->iconpath)
        t->iconpath = s_dup("");
    make_icon(t);
}

/* ---- an item's command, working directory and options ----
 * Observed (Program Item Properties on Windows 3.1): with a working
 * directory, the stored command is that directory plus the program's file
 * name (and arguments); if the typed command had its own path, that path
 * goes in tag 8101h (with its trailing backslash). Without a working
 * directory the command is stored as typed. Shown back, the working
 * directory is the stored command's directory, and the command is the
 * 8101h path plus the file name and arguments. */

#define TAG_PATH        0x8101

/* Length of the first word, and of its directory part (up to and
   including the last '\' or ':'). */
static int word_len(const char FAR *s)
{
    int n = 0;
    while (s[n] && s[n] != ' ')
        n++;
    return n;
}

static int dir_part(const char FAR *s, int n)
{
    while (n > 0 && s[n - 1] != '\\' && s[n - 1] != ':')
        n--;
    return n;
}

static const char FAR *skip_blanks(const char FAR *s)
{
    while (*s == ' ')
        s++;
    return s;
}

/* The data of this item's tag `id`, or NULL; *n receives its length. */
static const BYTE FAR *find_tag(item FAR *t, WORD id, WORD *n)
{
    WORD at = 0;
    while (at + 6 <= t->cbtags) {
        const BYTE FAR *p = (const BYTE FAR *)t->tags + at;
        WORD cb = rd16(p, 4);
        if (cb < 6)
            break;
        if (rd16(p, 0) == id) {
            *n = cb - 6;
            return p + 6;
        }
        at += cb;
    }
    return NULL;
}

/* Remove the tags w16 manages (path, hotkey, minimized), keeping others. */
static void drop_own_tags(item FAR *t)
{
    WORD at = 0, out = 0;
    BYTE FAR *p = (BYTE FAR *)t->tags;
    while (at + 6 <= t->cbtags) {
        WORD cb = rd16(p + at, 4), id = rd16(p + at, 0);
        if (cb < 6)
            break;
        if (id != TAG_PATH && id != TAG_HOTKEY && id != TAG_MINIMIZE) {
            copy(p + out, p + at, cb);
            out += cb;
        }
        at += cb;
    }
    t->cbtags = out;
}

/* Fill the fields of t from what the user typed. Returns FALSE when out
   of memory (t is then left for the caller to free or keep). */
static BOOL set_fields(item FAR *t, const char FAR *desc, const char FAR *cmd,
                       const char FAR *dir, WORD hotkey, BOOL minimized)
{
    LPSTR stored = NULL, path = NULL;
    int n, d;

    cmd = skip_blanks(cmd);
    dir = dir ? skip_blanks(dir) : "";
    n = word_len(cmd);
    d = dir_part(cmd, n);
    if (dir[0]) {
        stored = s_dup(dir);
        if (stored && stored[s_len(stored) - 1] != '\\')
            s_append(&stored, "\\");
        if (stored && !s_append(&stored, cmd + d)) {
            mem_free(stored);
            stored = NULL;
        }
        if (d && (path = mem_alloc(d + 1)) != NULL)
            s_cpy(path, cmd, d + 1);
        if (d && !path) {
            mem_free(stored);
            return FALSE;
        }
    } else {
        stored = s_dup(cmd);
    }
    if (!stored) {
        mem_free(path);
        return FALSE;
    }
    mem_free(t->cmd);
    t->cmd = stored;

    mem_free(t->name);
    if (desc && desc[0]) {
        t->name = s_dup(desc);
    } else {
        /* UNVERIFIED: an empty description becomes the program's name */
        int k;
        t->name = mem_alloc(n - d + 1);
        if (t->name) {
            s_cpy(t->name, cmd + d, n - d + 1);
            for (k = 0; t->name[k]; k++)
                if (t->name[k] == '.')
                    t->name[k] = 0;
        }
    }

    /* observed order: hotkey, minimized, path */
    drop_own_tags(t);
    if (!t->name || (hotkey && !add_tag(t, TAG_HOTKEY, &hotkey, 2)) ||
        (minimized && !add_tag(t, TAG_MINIMIZE, NULL, 0)) ||
        (path && !add_tag(t, TAG_PATH, path, s_len(path) + 1))) {
        mem_free(path);
        return FALSE;
    }
    mem_free(path);
    return TRUE;
}

/* The item's fields as the Properties dialog shows them. */
BOOL group_get_props(HWND hwnd, itemprops FAR *ip)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    const BYTE FAR *path;
    WORD np, nh;
    int n, d;
    ip->desc = ip->cmd = ip->dir = ip->iconpath = NULL;
    ip->has_pos = FALSE;
    if (!g || !g->sel)
        return FALSE;
    t = g->sel;
    n = word_len(t->cmd);
    d = dir_part(t->cmd, n);
    path = find_tag(t, TAG_PATH, &np);
    ip->desc = s_dup(t->name);
    ip->cmd = path ? s_dup((const char FAR *)path) : s_dup("");
    if (ip->cmd)
        s_append(&ip->cmd, t->cmd + d);
    /* the directory without its last backslash, except a root ("C:\") */
    ip->dir = mem_alloc(d + 1);
    if (ip->dir) {
        s_cpy(ip->dir, t->cmd, d + 1);
        if (d > 1 && ip->dir[d - 1] == '\\' && !(d == 3 && ip->dir[1] == ':'))
            ip->dir[d - 1] = 0;
    }
    ip->hotkey = 0;
    path = find_tag(t, TAG_HOTKEY, &nh);
    if (path && nh >= 2)
        ip->hotkey = rd16(path, 0);
    ip->minimized = find_tag(t, TAG_MINIMIZE, &nh) != NULL;
    ip->icon = t->hicon;
    if (!ip->desc || !ip->cmd || !ip->dir) {
        itemprops_free(ip);
        return FALSE;
    }
    return TRUE;
}

void itemprops_free(itemprops FAR *ip)
{
    mem_free(ip->desc);
    mem_free(ip->cmd);
    mem_free(ip->dir);
    mem_free(ip->iconpath);
    ip->desc = ip->cmd = ip->dir = ip->iconpath = NULL;
}

/* File > New > Program Item. */
BOOL group_add_item(HWND hwnd, const itemprops FAR *ip)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;

    if (!g || !skip_blanks(ip->cmd)[0] || !grow_slots(g))
        return FALSE;
    t = (item FAR *)mem_alloc(sizeof(item));
    if (!t)
        return FALSE;
    if (!set_fields(t, ip->desc, ip->cmd, ip->dir, ip->hotkey, ip->minimized)) {
        free_item(t);
        return FALSE;
    }
    if (ip->iconpath && ip->iconpath[0])        /* DDE AddItem's icon */
        set_icon(t, ip->iconpath, ip->iconindex);
    else
        choose_icon(t, ip->cmd);
    if (ip->has_pos) {
        t->x = ip->x;
        t->y = ip->y;
    } else if (g->have_replaced) {      /* ReplaceItem: the old position */
        t->x = g->replaced_x;
        t->y = g->replaced_y;
    } else {
        free_cell(hwnd, g, &t->x, &t->y);
    }
    {
        /* where it goes in the z-order: on top for File > New (UNVERIFIED
           beyond an empty group); for DDE AddItem at the bottom, or in the
           place of the item ReplaceItem removed (observed) */
        WORD k, at = 0;
        if (ip->dde)
            at = g->have_replaced && g->replaced_z <= g->nz ? g->replaced_z : g->nz;
        for (k = g->nz; k > at; k--)
            g->z[k] = g->z[k - 1];
        g->z[at] = t;
        g->nz++;
    }
    g->have_replaced = FALSE;
    data_append(g, t);
    if (!write_group(g)) {
        remove_item(hwnd, g, t);
        free_item(t);
        return FALSE;
    }
    invalidate_item(hwnd, g->sel);
    g->sel = t;
    if (g_autoarrange)
        group_arrange(hwnd);
    invalidate_item(hwnd, t);
    group_rescroll(hwnd);
    return TRUE;
}

/* File > Properties on an item. Observed: the item keeps its place in
   the z-order and its data moves to the end of the file. */
BOOL group_edit_selected(HWND hwnd, const itemprops FAR *ip)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    itemprops old;
    BOOL same_program;
    if (!g || !g->sel || !skip_blanks(ip->cmd)[0])
        return FALSE;
    t = g->sel;
    if (!group_get_props(hwnd, &old))
        return FALSE;
    {                                   /* did the program change? */
        const char FAR *a = skip_blanks(old.cmd), FAR *b = skip_blanks(ip->cmd);
        int na = word_len(a), nb = word_len(b), k;
        same_program = na == nb;
        for (k = 0; same_program && k < na; k++) {
            char x = a[k], y = b[k];
            if (x >= 'a' && x <= 'z')
                x -= 'a' - 'A';
            if (y >= 'a' && y <= 'z')
                y -= 'a' - 'A';
            same_program = x == y;
        }
    }
    itemprops_free(&old);
    invalidate_item(hwnd, t);
    if (!set_fields(t, ip->desc, ip->cmd, ip->dir, ip->hotkey, ip->minimized))
        return FALSE;
    if (ip->iconpath && ip->iconpath[0]) {      /* chosen with Change Icon */
        set_icon(t, ip->iconpath, ip->iconindex);
    } else if (!same_program) {         /* UNVERIFIED: a new program, its icon */
        if (t->own_icon)
            DestroyIcon(t->hicon);
        mem_free(t->icon);
        mem_free(t->iconpath);
        t->icon = NULL;
        t->iconpath = NULL;
        t->cbhdr = t->cband = t->cbxor = 0;
        choose_icon(t, ip->cmd);
    }
    data_remove(g, t);
    data_append(g, t);
    invalidate_item(hwnd, t);
    return write_group(g);
}

/* A copy of item t, data and tags included (icon bits shared by value). */
static item FAR *clone_item(item FAR *t)
{
    item FAR *u = (item FAR *)mem_alloc(sizeof(item));
    DWORD nicon = (DWORD)t->cbhdr + t->cband + t->cbxor;
    if (!u)
        return NULL;
    u->iicon = t->iicon;
    u->name = s_dup(t->name);
    u->cmd = s_dup(t->cmd);
    u->iconpath = s_dup(t->iconpath);
    u->cbhdr = t->cbhdr;
    u->cband = t->cband;
    u->cbxor = t->cbxor;
    if (nicon && (u->icon = mem_alloc(nicon)) != NULL)
        copy(u->icon, t->icon, (WORD)nicon);
    u->cbtags = t->cbtags;
    if (t->cbtags && (u->tags = mem_alloc(t->cbtags)) != NULL)
        copy(u->tags, t->tags, t->cbtags);
    if (!u->name || !u->cmd || !u->iconpath || (nicon && !u->icon) || (t->cbtags && !u->tags)) {
        free_item(u);
        return NULL;
    }
    make_icon(u);
    return u;
}

/* Take item t out of g: the gap closes, cItems stays (observed), and the
   new top item is selected (observed after Move and Delete). */
static void remove_item(HWND hwnd, group FAR *g, item FAR *t)
{
    int i = zindex(g, t);
    if (i < 0)
        return;
    invalidate_item(hwnd, t);
    for (; i + 1 < (int)g->nz; i++)
        g->z[i] = g->z[i + 1];
    g->nz--;
    data_remove(g, t);
    if (g->sel == t)
        g->sel = g->nz ? g->z[0] : NULL;
    invalidate_item(hwnd, g->sel);
    group_rescroll(hwnd);
}

/* File > Move and File > Copy (observed): the item arrives in `to` on top
   of the z-order, in the first free grid cell, its data at the end, and
   is selected there; Move then removes it from `from`. */
BOOL group_transfer_selected(HWND from, HWND to, BOOL copy_only)
{
    group FAR *src = grp_of(from), FAR *dst = grp_of(to);
    item FAR *t, FAR *u;
    WORD k;
    if (!src || !dst || !src->sel || !grow_slots(dst))
        return FALSE;
    t = src->sel;
    u = clone_item(t);
    if (!u)
        return FALSE;
    free_cell(to, dst, &u->x, &u->y);
    for (k = dst->nz; k > 0; k--)
        dst->z[k] = dst->z[k - 1];
    dst->z[0] = u;
    dst->nz++;
    data_append(dst, u);
    if (!write_group(dst)) {
        remove_item(to, dst, u);
        free_item(u);
        return FALSE;
    }
    invalidate_item(to, dst->sel);
    dst->sel = u;
    if (g_autoarrange)
        group_arrange(to);
    invalidate_item(to, u);
    group_rescroll(to);
    if (copy_only)
        return TRUE;
    remove_item(from, src, t);
    free_item(t);
    return write_group(src);
}

/* Start every item of the group, as Windows does with StartUp. Each
   item runs as its Properties say (working directory, Run Minimized).
   UNVERIFIED: the original's order; here the order of the file's data. */
void group_run_all(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t, FAR *keep;
    if (!g)
        return;
    keep = g->sel;
    for (t = g->dfirst; t; t = t->dnext) {
        itemprops ip;
        g->sel = t;                     /* group_get_props reads the selection */
        if (group_get_props(hwnd, &ip)) {
            pm_exec(g_frame, ip.cmd, ip.dir, ip.minimized);
            itemprops_free(&ip);
        }
    }
    g->sel = keep;
}

/* ---- for the DDE interface ---- */

HWND group_find(const char FAR *name)
{
    group FAR *g;
    for (g = first; g; g = g->next)
        if (!s_icmp(g->name, name))
            return g->hwnd;
    return NULL;
}

void group_show(HWND hwnd, int show)
{
    if (show == SW_SHOWMAXIMIZED)
        SendMessage(g_client, WM_MDIMAXIMIZE, (WPARAM)hwnd, 0);
    else
        ShowWindow(hwnd, show);
    if (show != SW_HIDE && show != SW_MINIMIZE && show != SW_SHOWMINNOACTIVE &&
        show != SW_SHOWNA)
        SendMessage(g_client, WM_MDIACTIVATE, (WPARAM)hwnd, 0);
}

/* A group as the DDE request for its name answers (observed):
     "name",path,items,show
     "item","command",workdir,iconpath,x,y,iconindex,hotkey,minimized
   items top of the z-order first, each line ending CR LF. */
LPSTR group_dde_text(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    LPSTR s;
    char num[8];
    WORD i;
    BOOL ok;
    if (!g)
        return NULL;
    s = s_dup("\"");
    ok = s && s_append(&s, g->name) && s_append(&s, "\",") && s_append(&s, g->path) &&
         s_append(&s, ",");
    s_utoa(num, g->nz);
    ok = ok && s_append(&s, num) && s_append(&s, ",");
    s_utoa(num, IsIconic(hwnd) ? SW_SHOWMINIMIZED : IsZoomed(hwnd) ? SW_SHOWMAXIMIZED
                                                                  : SW_SHOWNORMAL);
    ok = ok && s_append(&s, num) && s_append(&s, "\r\n");
    /* observed: newest item first (the reverse of the file's data order) */
    for (i = g->nz; ok && i-- > 0;) {
        item FAR *keep = g->sel;
        itemprops ip;
        item FAR *t = g->dfirst;
        WORD k;
        for (k = 0; t && k < i; k++)
            t = t->dnext;
        if (!t)
            break;
        g->sel = t;
        if (!group_get_props(hwnd, &ip)) {
            g->sel = keep;
            ok = FALSE;
            break;
        }
        g->sel = keep;
        ok = s_append(&s, "\"") && s_append(&s, ip.desc) && s_append(&s, "\",\"") &&
             s_append(&s, ip.cmd) && s_append(&s, "\",") && s_append(&s, ip.dir) &&
             s_append(&s, ",") && s_append(&s, t->iconpath) && s_append(&s, ",");
        s_itoa(num, t->x);
        ok = ok && s_append(&s, num) && s_append(&s, ",");
        s_itoa(num, t->y);
        ok = ok && s_append(&s, num) && s_append(&s, ",");
        s_utoa(num, t->iicon);
        ok = ok && s_append(&s, num) && s_append(&s, ",");
        s_utoa(num, ip.hotkey);
        ok = ok && s_append(&s, num) && s_append(&s, ip.minimized ? ",1\r\n" : ",0\r\n");
        itemprops_free(&ip);
    }
    if (!ok) {
        mem_free(s);
        return NULL;
    }
    return s;
}

static item FAR *item_named(group FAR *g, const char FAR *name)
{
    WORD i;
    for (i = 0; i < g->nz; i++)
        if (!s_icmp(g->z[i]->name, name))
            return g->z[i];
    return NULL;
}

BOOL group_delete_named(HWND hwnd, const char FAR *name)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t = g ? item_named(g, name) : NULL;
    if (!t)
        return FALSE;
    remove_item(hwnd, g, t);
    free_item(t);
    return write_group(g);
}

/* DDE ReplaceItem: delete the item; the next AddItem takes its place. */
BOOL group_replace_named(HWND hwnd, const char FAR *name)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t = g ? item_named(g, name) : NULL;
    if (!t)
        return FALSE;
    g->have_replaced = TRUE;
    g->replaced_x = t->x;
    g->replaced_y = t->y;
    g->replaced_z = zindex(g, t);
    remove_item(hwnd, g, t);
    free_item(t);
    return write_group(g);
}

/* Program Group Properties with a different file (observed): the group
   is written to the new file, the old one is deleted, and the same
   GroupN= entry names the new file. */
BOOL group_set_file(HWND hwnd, const char FAR *path)
{
    group FAR *g = grp_of(hwnd);
    LPSTR old, n;
    OFSTRUCT of;
    if (!g)
        return FALSE;
    if (!s_icmp(g->path, path))
        return TRUE;
    n = s_dup(path);
    if (!n)
        return FALSE;
    old = g->path;
    g->path = n;
    if (!write_group(g)) {
        g->path = old;
        mem_free(n);
        return FALSE;
    }
    OpenFile(old, &of, OF_DELETE);
    mem_free(old);
    ini_set_group(g->ininum, g->path);
    return TRUE;
}

/* Program Group Properties on an existing group: a new description. */
BOOL group_rename(HWND hwnd, const char FAR *name)
{
    group FAR *g = grp_of(hwnd);
    LPSTR n = s_dup(name);
    if (!g || !n) {
        mem_free(n);
        return FALSE;
    }
    mem_free(g->name);
    g->name = n;
    SetWindowText(hwnd, n);
    return write_group(g);
}

/* Delete the selected item (observed: the slot count stays, the gap
   closes). */
BOOL group_delete_selected(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;
    if (!g || !g->sel)
        return FALSE;
    t = g->sel;
    remove_item(hwnd, g, t);
    free_item(t);
    return write_group(g);
}

#ifndef W16_WIN2
/* ---- files dropped from File Manager (WM_DROPFILES) ----
 * Observed on Windows 3.1 (tests/scripts/pm/dragdrop*.txt; files dragged
 * from WINFILE.EXE with journalled mouse input):
 * - Group windows accept files (WS_EX_ACCEPTFILES), over their client
 *   and non-client areas and when minimized; the frame and the MDI
 *   client do not (File Manager shows its no-drop cursor there).
 * - Each file becomes an item: description = the file name without its
 *   extension, first letter capital, the rest small ("Notepad",
 *   "Readme"); command line = the full path as dropped, with the
 *   file's directory as working directory (stored as Program Item
 *   Properties stores it: C:\PMDROP\NOTEPAD.EXE plus tag 8101h
 *   "C:\PMDROP\").
 * - Icon: SHELL's ExtractAssociatedIcon. A program's own first icon
 *   (path, 0); a document's associated program as WIN.INI names it
 *   ("notepad.exe", 0); for a file without an icon of its own (a DOS
 *   program, .COM, .BAT, .PIF) SHELL answers with Program Manager's own
 *   file and an index (1, or 6 for a .PIF): the item keeps the dropped
 *   file as icon path with that index, and the bits of our icon there.
 * - A document with no association: a box "Invalid Path", "There is no
 *   association for <path>." (OK, Cancel - the default -; exclamation
 *   icon). Cancel leaves it out; OK adds it with no icon file ("") and
 *   Program Manager's icon 6 (an empty window).
 * - One file dropped on the client area goes where it was dropped: its
 *   icon centred on the point (item position = point - 18, 18). That
 *   includes a minimized group, whose client area is its icon (dropped at
 *   the icon's (22,16): item at (4,-2)). Several files, or a drop on the
 *   caption, go to the next free grid cells (free_cell), in the order
 *   File Manager lists them.
 * - Each new item goes on top of the z-order (the last one selected) and
 *   its data at the end of the file. The file is written after the drop
 *   with the new items in the last slots, in drop order; with Save
 *   Settings on Exit it is written again on exit, in the z-order (new
 *   items first). UNVERIFIED: when exactly the first write happens (a
 *   copy taken two seconds after a drop, File Manager still active, was
 *   unchanged; once Program Manager had been activated it was written).
 * - With Auto Arrange the group is arranged afterwards.
 * - Neither EditLevel nor NoFileMenu stops a drop (observed).
 * - The original stops at 50 items per group (a drop of 50 files on Main
 *   left 50 items; its message was not seen: the test harness stalls on
 *   it). w16 has no such limit (Zero-One-Infinity).
 * - Program Manager is not activated by a drop. */

typedef HICON (FAR PASCAL *EXTRACTASSOC)(HINSTANCE, LPSTR, UINT FAR *);

/* The icon for a dropped file as ExtractAssociatedIcon answers (see
   above). Fills t->iconpath, iicon and the bits; FALSE if SHELL has no
   answer (the caller then falls back to choose_icon). */
static BOOL dropped_icon(item FAR *t, const char FAR *file)
{
    static EXTRACTASSOC fn;
    char own[144];                      /* DOS paths: OFSTRUCT's own bound */
    LPSTR buf;
    UINT index = 0;
    HICON h;
    BOOL ok;
    int n = s_len(file);
    if (!fn) {
        HMODULE shell = GetModuleHandle("SHELL");
        if (shell)
            fn = (EXTRACTASSOC)GetProcAddress(shell, "ExtractAssociatedIcon");
        if (!fn)
            return FALSE;
    }
    /* SHELL may write a path of its own (an associated program's) */
    buf = mem_alloc(n + 260);
    if (!buf)
        return FALSE;
    s_cpy(buf, file, n + 1);
    h = fn(g_hinst, buf, &index);
    if (!h) {
        mem_free(buf);
        return FALSE;
    }
    ok = icon_bits(t, h);
    DestroyIcon(h);
    if (GetModuleFileName(g_hinst, own, sizeof own) && !s_icmp(buf, own))
        s_cpy(buf, file, n + 1);        /* our own icon: the file's path */
    t->iconpath = s_dup(buf);
    t->iicon = index;
    mem_free(buf);
    if (!ok || !t->iconpath) {
        mem_free(t->icon);
        mem_free(t->iconpath);
        t->icon = t->iconpath = NULL;
        t->cbhdr = t->cband = t->cbxor = 0;
        return FALSE;
    }
    make_icon(t);
    return TRUE;
}

/* Add one dropped file to group g; at_point: put it at (x, y). */
static BOOL drop_file(HWND hwnd, group FAR *g, LPSTR file, BOOL at_point, int x, int y)
{
    item FAR *t;
    LPSTR desc, dir;
    int n = s_len(file), d = dir_part(file, n), e, k;
    char found[144];                    /* FindExecutable's: DOS paths */

    BOOL noassoc = FALSE;

    /* a document needs an association (observed message) */
    if (!is_program(file) && (UINT)FindExecutable(file, NULL, found) <= 32) {
        if (ask(g_frame, IDS_INVALIDPATH, IDS_NOASSOC, file,
                MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2) != IDOK)
            return FALSE;
        noassoc = TRUE;
    }
    if (!grow_slots(g))
        return FALSE;
    /* the description: the name without extension, "Capitalised" */
    for (e = n; e > d && file[e - 1] != '.'; e--)
        ;
    if (e == d)
        e = n + 1;
    desc = mem_alloc(e - d);
    dir = mem_alloc(d + 1);
    t = (item FAR *)mem_alloc(sizeof(item));
    if (!desc || !dir || !t) {
        mem_free(desc);
        mem_free(dir);
        mem_free((LPSTR)t);
        return FALSE;
    }
    s_cpy(desc, file + d, e - d);
    AnsiLower(desc);
    AnsiUpperBuff(desc, 1);
    /* the working directory: without its last backslash, except a root */
    s_cpy(dir, file, d + 1);
    if (d > 1 && dir[d - 1] == '\\' && !(d == 3 && dir[1] == ':'))
        dir[d - 1] = 0;
    if (!set_fields(t, desc, file, dir, 0, FALSE)) {
        mem_free(desc);
        mem_free(dir);
        free_item(t);
        return FALSE;
    }
    mem_free(desc);
    mem_free(dir);
    if (noassoc) {                      /* observed: "" and our icon 6 */
        t->iconpath = s_dup("");
        t->iicon = 6;
        fetch_bits(t, "PROGMAN.EXE", 6);
        make_icon(t);
    } else if (!dropped_icon(t, file)) {
        choose_icon(t, file);
    }
    if (at_point) {
        t->x = x;
        t->y = y;
    } else {
        free_cell(hwnd, g, &t->x, &t->y);
    }
    for (k = g->nz; k > 0; k--)         /* on top of the z-order */
        g->z[k] = g->z[k - 1];
    g->z[0] = t;
    g->nz++;
    data_append(g, t);
    invalidate_item(hwnd, g->sel);
    g->sel = t;
    invalidate_item(hwnd, t);
    g->dirty = TRUE;
    return TRUE;
}

/* Write g with the k newest items (z[0..k-1], newest first) in the last
   slots, oldest first, as the original does after a drop. */
static void write_dropped(group FAR *g, WORD k)
{
    item FAR * FAR *keep = (item FAR * FAR *)mem_alloc((DWORD)g->nz * sizeof(item FAR *));
    WORD i;
    if (!keep)
        return;
    for (i = 0; i < g->nz; i++)
        keep[i] = g->z[i];
    for (i = 0; i + k < g->nz; i++)
        g->z[i] = keep[k + i];
    for (i = 0; i < k; i++)
        g->z[g->nz - k + i] = keep[k - 1 - i];
    write_group(g);
    for (i = 0; i < g->nz; i++)
        g->z[i] = keep[i];
    mem_free((LPSTR)keep);
}

static void drop_files(HWND hwnd, HDROP h)
{
    group FAR *g = grp_of(hwnd);
    UINT n = DragQueryFile(h, (UINT)-1, NULL, 0), i;
    WORD before = g ? g->nz : 0;
    POINT pt;
    BOOL in_client = DragQueryPoint(h, &pt);
    for (i = 0; g && i < n; i++) {
        UINT len = DragQueryFile(h, i, NULL, 0);
        LPSTR file = mem_alloc(len + 1);
        if (!file)
            break;
        DragQueryFile(h, i, file, len + 1);
        /* the icon centred on the point: ICON_DX plus half an icon */
        drop_file(hwnd, g, file, n == 1 && in_client,
                  pt.x - ICON_DX - GetSystemMetrics(SM_CXICON) / 2,
                  pt.y - ICON_DY - GetSystemMetrics(SM_CYICON) / 2);
        mem_free(file);
    }
    DragFinish(h);
    if (g && g_autoarrange)
        group_arrange(hwnd);
    if (g && g->nz > before)
        write_dropped(g, g->nz - before);
    if (g)
        group_rescroll(hwnd);
}
#endif

/* ---- the window ---- */

#ifdef PM_DEBUGLOG
void dbg(const char FAR *s, int v);
#endif

LRESULT CALLBACK __export GroupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    group FAR *g = grp_of(hwnd);
    item FAR *t;

#ifdef PM_DEBUGLOG
    if (msg == WM_SYSCOMMAND)
        dbg("GroupWndProc WM_SYSCOMMAND", wp);
    if (msg == WM_CLOSE)
        dbg("GroupWndProc WM_CLOSE", 0);
#endif
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
        else if (g && wp != SIZE_MINIMIZED)
            group_rescroll(hwnd);
        break;
    case WM_HSCROLL:
    case WM_VSCROLL:
        group_scroll(hwnd, msg == WM_HSCROLL ? SB_HORZ : SB_VERT, wp, (short)LOWORD(lp));
        return 0;
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
#ifndef W16_WIN2
    case WM_DROPFILES:
        drop_files(hwnd, (HDROP)wp);
        return 0;
#endif
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
    {   /* LoadIcon can't read 3.x-format icons: our own reader can */
        LPSTR own = icon_file("PROGMAN.EXE");
        def_icon = own ? icon_extract(own, 2) : NULL;
        mem_free(own);
        if (!def_icon)
            def_icon = LoadIcon(NULL, IDI_APPLICATION);
    }
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
