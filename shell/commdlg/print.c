/*
 * print.c - w16 COMMDLG: PrintDlg, the Print and Print Setup dialogs
 * (templates 1538, 1539), and the subclass procedures EditIntegerOnly
 * (the Print dialog's From, To and Copies boxes) and dwUpArrowHack (Print
 * Setup's list of printers) - observed which controls carry them.
 *
 * Observed on Windows 3.1 with printer drivers from its Setup disks in
 * scratch installs (tests/scripts/commdlg/print*.txt, ARCHITECTURE.md 17):
 *  - The printer is WIN.INI's [windows] device= ("device,driver,port")
 *    unless hDevNames names one; the driver is <driver>.DRV, loaded with
 *    LoadLibrary; its ExtDeviceMode makes the DEVMODE (DM_COPY, with
 *    DM_MODIFY when hDevMode comes in), DeviceCapabilities the lists.
 *    A driver that does not load: PDERR_LOADDRVFAILURE.
 *  - DEVNAMES: device name at 8, driver after it, port after that;
 *    wDefault DN_DEFAULTPRN when the default printer is meant, and two
 *    bits more: 4000h on DEVNAMES made by this call (cleared when a dialog
 *    finishes), 8000h set by the Print dialog, cleared by Print Setup.
 *  - PD_RETURNDEFAULT: no dialog; with hDevMode or hDevNames passed in
 *    PDERR_RETDEFFAILURE. Cancel in the Print dialog frees a DEVNAMES the
 *    call made, keeps the DEVMODE.
 *  - The Print dialog: "Default Printer (<device> on <port>)" or
 *    "<device> on <port>"; Print Quality lists the driver's resolutions
 *    ("300 dpi"...) and sets dmPrintQuality; From, To, Copies filled in
 *    that order (Copies first). A page number out of range: a beep, the
 *    box focused and selected; Copies 0: "The 'Copies' field must contain
 *    a positive value." (exclamation, titled "Print").
 *  - Print Setup: Default / Specific printer ("(currently <device> on
 *    <port>)", the [devices] printers "<device> on <port>"), orientation
 *    with a picture, paper size (DC_PAPERS, our strings 1152 + DMPAPER_*)
 *    and source (DC_BINS, DC_BINNAMES); Options... is ExtDeviceMode with
 *    DM_PROMPT.
 */
#include "commdlg.h"
#include <drivinit.h>


/* DeviceCapabilities indices (Windows 3.1 DDK) */
#define DC_PAPERS           2
#define DC_BINS             6
#define DC_BINNAMES         12
#define DC_ENUMRESOLUTIONS  13
#define DC_COPIES           18

#define DMORIENT_PORTRAIT   1
#define DMORIENT_LANDSCAPE  2
#define DM_ORIENTATION_F    0x0001L
#define DM_PAPERSIZE_F      0x0002L
#define DM_DEFAULTSOURCE_F  0x0200L
#define DM_PRINTQUALITY_F   0x0400L

/* wDefault bits beside DN_DEFAULTPRN (observed, see above) */
#define DN_MADE             0x4000
#define DN_PRINTDLG         0x8000

/* the size the original gives a DEVNAMES (observed: GlobalSize 96 for
   42 bytes of names; UNVERIFIED: how it computes it - we ask for the
   header and an 80-byte profile line) */
#define DEVNAMES_SIZE       (sizeof(DEVNAMES) + 80)

typedef int (FAR PASCAL *EXTDEVMODEPROC)(HWND, HANDLE, LPDEVMODE, LPSTR, LPSTR,
                                         LPDEVMODE, LPSTR, WORD);
typedef DWORD (FAR PASCAL *DEVCAPSPROC)(LPSTR, LPSTR, WORD, LPSTR, LPDEVMODE);

typedef struct {
    LPPRINTDLG pd;
    HWND dlg;
    cd_hook hook;
    BOOL setup;                         /* this dialog is Print Setup */
    BOOL made_names;                    /* hDevNames made by this call */
    BOOL made_mode;                     /* hDevMode made by this call */
    BOOL setup_ran;                     /* Print ran Print Setup */
    char device[CCHDEVICENAME + 1];
    char driver[13];
    char port[80];
    BOOL is_default;                    /* the default printer */
    HINSTANCE lib;                      /* the driver */
    EXTDEVMODEPROC extdm;
    DEVCAPSPROC caps;
    LONG FAR *res;                      /* resolutions: x, y pairs */
    int nres;
    int FAR *papers;
    int npapers;
    int FAR *bins;
    int nbins;
} pstate;


/* ---- printers ---- */

/* "device,driver,port" into its parts; FALSE if not all there */
static BOOL split_device(LPSTR line, pstate FAR *st)
{
    LPSTR d = line, drv, port;
    for (drv = d; *drv && *drv != ','; drv++)
        ;
    if (!*drv)
        return FALSE;
    *drv++ = 0;
    for (port = drv; *port && *port != ','; port++)
        ;
    if (!*port)
        return FALSE;
    *port++ = 0;
    while (*drv == ' ')
        drv++;
    while (*port == ' ')
        port++;
    {   /* the first port only */
        LPSTR e;
        for (e = port; *e && *e != ','; e++)
            ;
        *e = 0;
    }
    if (!*d || !*drv || !*port)
        return FALSE;
    s_cpy(st->device, d, sizeof st->device);
    s_cpy(st->driver, drv, sizeof st->driver);
    s_cpy(st->port, port, sizeof st->port);
    return TRUE;
}

/* the [devices] printer called `dev` into st (not the default) */
static BOOL printer_named(pstate FAR *st, LPCSTR dev)
{
    char line[80], full[120];
    GetProfileString("devices", dev, "", line, sizeof line);
    if (!line[0])
        return FALSE;
    s_cpy(full, dev, sizeof full);
    s_cat(full, ",", sizeof full);
    s_cat(full, line, sizeof full);
    st->is_default = FALSE;
    return split_device(full, st);
}

static BOOL default_printer(pstate FAR *st)
{
    char line[80];
    GetProfileString("windows", "device", "", line, sizeof line);
    return split_device(line, st);
}

static BOOL names_from(pstate FAR *st, HGLOBAL h)
{
    LPDEVNAMES n = (LPDEVNAMES)GlobalLock(h);
    if (!n)
        return FALSE;
    s_cpy(st->driver, (LPSTR)n + n->wDriverOffset, sizeof st->driver);
    s_cpy(st->device, (LPSTR)n + n->wDeviceOffset, sizeof st->device);
    s_cpy(st->port, (LPSTR)n + n->wOutputOffset, sizeof st->port);
    st->is_default = (n->wDefault & DN_DEFAULTPRN) != 0;
    GlobalUnlock(h);
    return TRUE;
}

/* a new DEVNAMES of the printer in st */
static HGLOBAL make_names(pstate FAR *st, WORD flags)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, DEVNAMES_SIZE);
    LPDEVNAMES n;
    LPSTR p;
    if (!h)
        return NULL;
    n = (LPDEVNAMES)GlobalLock(h);
    p = (LPSTR)n;
    n->wDeviceOffset = sizeof(DEVNAMES);
    s_cpy(p + n->wDeviceOffset, st->device, CCHDEVICENAME + 1);
    n->wDriverOffset = n->wDeviceOffset + s_len(st->device) + 1;
    s_cpy(p + n->wDriverOffset, st->driver, 13);
    n->wOutputOffset = n->wDriverOffset + s_len(st->driver) + 1;
    s_cpy(p + n->wOutputOffset, st->port, 80);
    n->wDefault = flags;
    GlobalUnlock(h);
    return h;
}

/* the DEVNAMES rewritten for the printer in st, keeping wDefault's bits */
static void set_names(pstate FAR *st, WORD set, WORD clear)
{
    LPPRINTDLG pd = st->pd;
    WORD w = 0;
    HGLOBAL h;
    if (pd->hDevNames) {
        LPDEVNAMES n = (LPDEVNAMES)GlobalLock(pd->hDevNames);
        if (n) {
            w = n->wDefault;
            GlobalUnlock(pd->hDevNames);
        }
    }
    w = (w & ~clear) | set;
    w = st->is_default ? (w | DN_DEFAULTPRN) : (w & ~DN_DEFAULTPRN);
    h = make_names(st, w);
    if (h) {
        if (pd->hDevNames)
            GlobalFree(pd->hDevNames);
        pd->hDevNames = h;
    }
}

static BOOL load_driver(pstate FAR *st)
{
    char file[20];
    s_cpy(file, st->driver, 9);
    s_cat(file, ".DRV", sizeof file);
    st->lib = LoadLibrary(file);
    if ((UINT)st->lib < 32) {
        st->lib = NULL;
        return FALSE;
    }
    st->extdm = (EXTDEVMODEPROC)GetProcAddress(st->lib, "ExtDeviceMode");
    st->caps = (DEVCAPSPROC)GetProcAddress(st->lib, "DeviceCapabilities");
    return TRUE;
}

static void free_driver(pstate FAR *st)
{
    if (st->lib)
        FreeLibrary(st->lib);
    st->lib = NULL;
    mem_free(st->res);
    mem_free(st->papers);
    mem_free(st->bins);
    st->res = NULL;
    st->papers = st->bins = NULL;
    st->nres = st->npapers = st->nbins = 0;
}

/* the DEVMODE from the driver, merged with the caller's */
static BOOL get_devmode(pstate FAR *st, HWND w)
{
    LPPRINTDLG pd = st->pd;
    int size;
    HGLOBAL h;
    LPDEVMODE out, in = NULL;
    if (!st->extdm)
        return TRUE;                    /* an old driver: none */
    size = st->extdm(w, st->lib, NULL, st->device, st->port, NULL, NULL, 0);
    if (size <= 0)
        return FALSE;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!h)
        return FALSE;
    out = (LPDEVMODE)GlobalLock(h);
    if (pd->hDevMode) {
        in = (LPDEVMODE)GlobalLock(pd->hDevMode);
        if (in && lstrcmp(in->dmDeviceName, st->device)) {
            GlobalUnlock(pd->hDevMode);
            in = NULL;
        }
    }
    st->extdm(w, st->lib, out, st->device, st->port, in, NULL,
              DM_COPY | (in ? DM_MODIFY : 0));
    if (in)
        GlobalUnlock(pd->hDevMode);
    GlobalUnlock(h);
    if (pd->hDevMode)
        GlobalFree(pd->hDevMode);
    pd->hDevMode = h;
    return TRUE;
}

/* a DeviceCapabilities list into a new array (n items of `size` bytes) */
static LPVOID caps_list(pstate FAR *st, WORD what, WORD size, int FAR *n)
{
    DWORD k;
    LPVOID a;
    LPDEVMODE dm = st->pd->hDevMode ? (LPDEVMODE)GlobalLock(st->pd->hDevMode) : NULL;
    *n = 0;
    a = NULL;
    if (st->caps) {
        k = st->caps(st->device, st->port, what, NULL, dm);
        if ((long)k > 0 && k < 1000) {
            a = mem_alloc((WORD)k * size);
            if (a) {
                st->caps(st->device, st->port, what, (LPSTR)a, dm);
                *n = (int)k;
            }
        }
    }
    if (dm)
        GlobalUnlock(st->pd->hDevMode);
    return a;
}

static LPDEVMODE lock_dm(pstate FAR *st)
{
    return st->pd->hDevMode ? (LPDEVMODE)GlobalLock(st->pd->hDevMode) : NULL;
}

static void unlock_dm(pstate FAR *st)
{
    if (st->pd->hDevMode)
        GlobalUnlock(st->pd->hDevMode);
}

/* "<device> on <port><tail>" (string 1095) */
static void on_port(LPSTR out, LPCSTR dev, LPCSTR port, LPCSTR tail)
{
    LPSTR f = load_str(IDS_PRNONPORT2);
    if (f) {
        wsprintf(out, f, dev, port, tail);
        mem_free(f);
    } else {
        out[0] = 0;
    }
}

static void error_box(HWND w, UINT ids)
{
    LPSTR t = load_str(ids), c = load_str(IDS_PRINTSETUP);
    char cap[32];
    GetWindowText(w, cap, sizeof cap);
    if (t)
        MessageBox(w, t, cap, MB_OK | MB_ICONEXCLAMATION);
    mem_free(t);
    mem_free(c);
}

/* ---- the Print dialog ---- */

static void fill_quality(pstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb1);
    LPDEVMODE dm;
    int i, sel = 0, q = 0;
    char t[40];
    if (!cb)
        return;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    mem_free(st->res);
    st->res = caps_list(st, DC_ENUMRESOLUTIONS, 2 * sizeof(LONG), &st->nres);
    dm = lock_dm(st);
    if (dm && (dm->dmFields & DM_PRINTQUALITY_F))
        q = dm->dmPrintQuality;
    unlock_dm(st);
    for (i = 0; i < st->nres; i++) {
        int x = (int)st->res[2 * i], y = (int)st->res[2 * i + 1];
        if (x == y)
            wsprintf(t, "%d dpi", x);
        else    /* UNVERIFIED: the text for unequal resolutions */
            wsprintf(t, "%d x %d dpi", x, y);
        SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        if (x == q)
            sel = i;
    }
    if (!st->nres) {                    /* UNVERIFIED: the named qualities */
        for (i = 0; i < 4; i++) {
            LPSTR s = load_str(IDS_QUALITY + i);
            if (s) {
                SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)s);
                mem_free(s);
            }
            if (q == -1 - i)
                sel = i;
        }
    }
    SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static void printer_text(pstate FAR *st)
{
    char t[200];
    if (st->is_default) {
        LPSTR d = load_str(IDS_DEFPRINTER);
        s_cpy(t, d ? d : "", sizeof t);
        mem_free(d);
        on_port(t + s_len(t), st->device, st->port, ")");
    } else {
        on_port(t, st->device, st->port, "");
    }
    SetDlgItemText(st->dlg, stc1, t);
}

static void print_init(pstate FAR *st)
{
    LPPRINTDLG pd = st->pd;
    HWND dlg = st->dlg;
    DWORD fl = pd->Flags;
    /* observed: Copies shows nCopies (PD_USEDEVMODECOPIES too); From
       and To stay empty with PD_NOPAGENUMS */
    if (!pd->nCopies)                   /* observed: 0 becomes 1, kept on Cancel */
        pd->nCopies = 1;
    SetDlgItemInt(dlg, edt3, pd->nCopies, FALSE);
    if (!(fl & PD_NOPAGENUMS)) {
        SetDlgItemInt(dlg, edt1, pd->nFromPage, FALSE);
        SetDlgItemInt(dlg, edt2, pd->nToPage, FALSE);
    }
    subclass(GetDlgItem(dlg, edt1), (FARPROC)EditIntegerOnly);
    subclass(GetDlgItem(dlg, edt2), (FARPROC)EditIntegerOnly);
    subclass(GetDlgItem(dlg, edt3), (FARPROC)EditIntegerOnly);
    SendDlgItemMessage(dlg, cmb1, CB_SETEXTENDEDUI, TRUE, 0);
    printer_text(st);
    CheckRadioButton(dlg, rad1, rad3,
                     (fl & PD_PAGENUMS) ? rad3 : (fl & PD_SELECTION) ? rad2 : rad1);
    if (fl & PD_NOSELECTION)
        EnableWindow(GetDlgItem(dlg, rad2), FALSE);
    if (fl & PD_NOPAGENUMS) {
        EnableWindow(GetDlgItem(dlg, rad3), FALSE);
        EnableWindow(GetDlgItem(dlg, stc2), FALSE);
        EnableWindow(GetDlgItem(dlg, edt1), FALSE);
        EnableWindow(GetDlgItem(dlg, stc3), FALSE);
        EnableWindow(GetDlgItem(dlg, edt2), FALSE);
    }
    CheckDlgButton(dlg, chx1, (fl & PD_PRINTTOFILE) != 0);
    CheckDlgButton(dlg, chx2, (fl & PD_COLLATE) != 0);
    if (fl & PD_HIDEPRINTTOFILE) {
        ShowWindow(GetDlgItem(dlg, chx1), SW_HIDE);
        EnableWindow(GetDlgItem(dlg, chx1), FALSE);
    } else if (fl & PD_DISABLEPRINTTOFILE) {
        EnableWindow(GetDlgItem(dlg, chx1), FALSE);
    }
    if (!(fl & PD_SHOWHELP)) {
        ShowWindow(GetDlgItem(dlg, pshHelp), SW_HIDE);
        EnableWindow(GetDlgItem(dlg, pshHelp), FALSE);
    }
    fill_quality(st);
}

/* a page box: its number, or the box focused (beep) and FALSE */
/* a page box: its number, or a message (observed: "The 'From' value is
   below the minimum page." etc., then the box has the focus) and FALSE.
   ids: the box's first message (below, above, not a number) */
static BOOL page_number(HWND dlg, int id, UINT lo, UINT hi, UINT FAR *v, UINT below,
                        UINT above, UINT bad)
{
    BOOL ok;
    UINT n = GetDlgItemInt(dlg, id, &ok, FALSE), why = 0;
    if (!ok)
        why = bad;          /* UNVERIFIED: letters or > 65535 */
    else if (n < lo)
        why = below;
    else if (n > hi)
        why = above;
    if (why) {
        HWND e = GetDlgItem(dlg, id);
        error_box(dlg, why);
        SetFocus(e);
        SendMessage(e, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }
    *v = n;
    return TRUE;
}

static BOOL print_ok(pstate FAR *st)
{
    LPPRINTDLG pd = st->pd;
    HWND dlg = st->dlg;
    UINT from = pd->nFromPage, to = pd->nToPage, copies;
    BOOL ok;
    LPDEVMODE dm;
    if (IsDlgButtonChecked(dlg, rad3)) {
        /* observed: From above To is taken as it is */
        if (GetWindowTextLength(GetDlgItem(dlg, edt1)) == 0 &&
            GetWindowTextLength(GetDlgItem(dlg, edt2)) == 0) {
            error_box(dlg, IDS_PAGESEMPTY);         /* UNVERIFIED */
            SetFocus(GetDlgItem(dlg, edt1));
            return FALSE;
        }
        if (!page_number(dlg, edt1, pd->nMinPage, pd->nMaxPage, &from,
                         IDS_FROMBELOW, IDS_FROMABOVE, IDS_FROMBAD) ||
            !page_number(dlg, edt2, pd->nMinPage, pd->nMaxPage, &to,
                         IDS_TOBELOW, IDS_TOABOVE, IDS_TOBAD))
            return FALSE;
    }
    if (GetWindowTextLength(GetDlgItem(dlg, edt3)) == 0) {
        error_box(dlg, IDS_COPIESEMPTY);
        SetFocus(GetDlgItem(dlg, edt3));
        return FALSE;
    }
    copies = GetDlgItemInt(dlg, edt3, &ok, FALSE);
    if (!ok) {
        error_box(dlg, IDS_COPIESBAD);
        SetFocus(GetDlgItem(dlg, edt3));
        return FALSE;
    }
    if (copies == 0) {
        error_box(dlg, IDS_COPIESZERO);
        SetFocus(GetDlgItem(dlg, edt3));
        return FALSE;
    }
    pd->Flags &= ~(PD_SELECTION | PD_PAGENUMS | PD_COLLATE | PD_PRINTTOFILE);
    if (IsDlgButtonChecked(dlg, rad2))
        pd->Flags |= PD_SELECTION;
    if (IsDlgButtonChecked(dlg, rad3)) {
        pd->Flags |= PD_PAGENUMS;
        pd->nFromPage = from;
        pd->nToPage = to;
    }
    if (IsDlgButtonChecked(dlg, chx1))
        pd->Flags |= PD_PRINTTOFILE;
    if (IsDlgButtonChecked(dlg, chx2))
        pd->Flags |= PD_COLLATE;
    /* observed: with PD_USEDEVMODECOPIES the copies go into the DEVMODE
       and nCopies comes back 1 */
    pd->nCopies = (pd->Flags & PD_USEDEVMODECOPIES) ? 1 : copies;
    dm = lock_dm(st);
    if (dm) {
        int i = (int)SendDlgItemMessage(dlg, cmb1, CB_GETCURSEL, 0, 0);
        if (i >= 0 && i < st->nres)
            dm->dmPrintQuality = (int)st->res[2 * i];
        else if (i >= 0 && !st->nres)
            dm->dmPrintQuality = -1 - i;
        if (pd->Flags & PD_USEDEVMODECOPIES)
            dm->dmCopies = copies;
    }
    unlock_dm(st);
    return TRUE;
}

/* ---- Print Setup ---- */

/* a WIN.INI [devices] value or the list of keys (key NULL), as long as
   it is, in a new string (with a double NUL at the end) */
static LPSTR devices_string(LPCSTR key)
{
    int size = 256, n;
    LPSTR b = NULL, g;
    for (;;) {
        g = mem_realloc(b, size);
        if (!g) {
            mem_free(b);
            return NULL;
        }
        b = g;
        n = GetProfileString("devices", key, "", b, size);
        if (n < size - 2)
            return b;
        size *= 2;
    }
}

/* Print Setup's list: one entry per printer and port ("<device> on
   <port>", observed: a printer on two ports is there twice), in
   [devices] order. With want: find that entry's printer into st. */
static BOOL each_printer(pstate FAR *st, HWND cb, LPCSTR want)
{
    LPSTR keys = devices_string(NULL), k;
    BOOL found = FALSE;
    char cur[200];
    int sel = 0;
    if (!keys)
        return FALSE;
    on_port(cur, st->device, st->port, "");
    for (k = keys; *k && !found; k += s_len(k) + 1) {
        LPSTR line = devices_string(k), drv, port, e;
        if (!line)
            continue;
        for (drv = line; *drv == ' '; drv++)
            ;
        for (port = drv; *port && *port != ','; port++)
            ;
        if (*port)
            *port++ = 0;
        for (; *port; port = e) {
            char t[200], c;
            while (*port == ' ')
                port++;
            for (e = port; *e && *e != ','; e++)
                ;
            c = *e;
            *e = 0;
            on_port(t, k, port, "");
            if (want) {
                if (!lstrcmpi(t, want)) {
                    s_cpy(st->device, k, sizeof st->device);
                    s_cpy(st->driver, drv, sizeof st->driver);
                    s_cpy(st->port, port, sizeof st->port);
                    found = TRUE;
                    break;
                }
            } else {
                int i = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
                if (!lstrcmpi(t, cur))
                    sel = i;
            }
            if (c)
                e++;
        }
        mem_free(line);
    }
    mem_free(keys);
    if (!want)
        SendMessage(cb, CB_SETCURSEL, sel, 0);
    return found;
}

static void fill_printers(pstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb1);
    if (!cb)
        return;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    each_printer(st, cb, NULL);
}

static void set_orient_icon(pstate FAR *st, BOOL landscape)
{
    HICON i = LoadIcon(cd_inst, MAKEINTRESOURCE(landscape ? ICO_LANDSCAPE : ICO_PORTRAIT));
    SendDlgItemMessage(st->dlg, 0x43C, STM_SETICON, (WPARAM)i, 0);
}

static void fill_paper(pstate FAR *st)
{
    HWND dlg = st->dlg;
    LPDEVMODE dm;
    int i, sel = 0, paper = 0, src = 0, orient = DMORIENT_PORTRAIT;
    char FAR *names;
    int nnames;
    SendDlgItemMessage(dlg, cmb2, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(dlg, cmb3, CB_RESETCONTENT, 0, 0);
    mem_free(st->papers);
    mem_free(st->bins);
    st->papers = caps_list(st, DC_PAPERS, sizeof(int), &st->npapers);
    st->bins = caps_list(st, DC_BINS, sizeof(int), &st->nbins);
    dm = lock_dm(st);
    if (dm) {
        paper = dm->dmPaperSize;
        src = dm->dmDefaultSource;
        orient = dm->dmOrientation;
    }
    unlock_dm(st);
    for (i = 0; i < st->npapers; i++) {
        int p = st->papers[i];
        LPSTR s = load_str(p == 256 ? IDS_USERSIZE : IDS_PAPERNAMES + p);
        SendDlgItemMessage(dlg, cmb2, CB_ADDSTRING, 0, (LPARAM)(s ? s : ""));
        mem_free(s);
        if (p == paper)
            sel = i;
    }
    SendDlgItemMessage(dlg, cmb2, CB_SETCURSEL, sel, 0);
    names = caps_list(st, DC_BINNAMES, 24, &nnames);
    sel = 0;
    for (i = 0; i < st->nbins; i++) {
        char t[25];
        if (i < nnames)
            s_cpy(t, names + 24 * i, 25);
        else
            t[0] = 0;
        SendDlgItemMessage(dlg, cmb3, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        if (st->bins[i] == src)
            sel = i;
    }
    mem_free(names);
    SendDlgItemMessage(dlg, cmb3, CB_SETCURSEL, sel, 0);
    CheckRadioButton(dlg, rad1, rad2, orient == DMORIENT_LANDSCAPE ? rad2 : rad1);
    set_orient_icon(st, orient == DMORIENT_LANDSCAPE);
}

static void setup_texts(pstate FAR *st)
{
    char t[200], dev[200];
    char line[80];
    pstate def;
    LPSTR f = load_str(IDS_CURRENTLY);
    GetProfileString("windows", "device", "", line, sizeof line);
    mem_zero(&def, sizeof def);
    if (split_device(line, (pstate FAR *)&def))
        on_port(dev, def.device, def.port, "");
    else
        dev[0] = 0;
    if (f) {
        wsprintf(t, f, (LPSTR)dev);
        mem_free(f);
    } else {
        t[0] = 0;
    }
    SetDlgItemText(st->dlg, stc1, t);
}

static void setup_init(pstate FAR *st)
{
    HWND dlg = st->dlg;
    DWORD fl = st->pd->Flags;
    setup_texts(st);
    fill_printers(st);
    CheckRadioButton(dlg, rad3, rad4, st->is_default ? rad3 : rad4);
    subclass(GetDlgItem(dlg, cmb1), (FARPROC)dwUpArrowHack);
    SendDlgItemMessage(dlg, cmb1, CB_SETEXTENDEDUI, TRUE, 0);
    SendDlgItemMessage(dlg, cmb2, CB_SETEXTENDEDUI, TRUE, 0);
    SendDlgItemMessage(dlg, cmb3, CB_SETEXTENDEDUI, TRUE, 0);
    fill_paper(st);
    if (!(fl & PD_SHOWHELP)) {
        ShowWindow(GetDlgItem(dlg, pshHelp), SW_HIDE);
        EnableWindow(GetDlgItem(dlg, pshHelp), FALSE);
    }
}

/* the printer chosen in the list ("<device> on <port>") into st */
static BOOL chosen_printer(pstate FAR *st)
{
    char want[200];
    int i = (int)SendDlgItemMessage(st->dlg, cmb1, CB_GETCURSEL, 0, 0);
    if (i < 0)
        return FALSE;
    SendDlgItemMessage(st->dlg, cmb1, CB_GETLBTEXT, i, (LPARAM)(LPSTR)want);
    return each_printer(st, NULL, want);
}

/* the printer changed: its driver and DEVMODE, the lists */
static void switch_printer(pstate FAR *st, BOOL force)
{
    char dev[CCHDEVICENAME + 1], drv[13], port[80];
    s_cpy(dev, st->device, sizeof dev);
    s_cpy(drv, st->driver, sizeof drv);
    s_cpy(port, st->port, sizeof port);
    if (IsDlgButtonChecked(st->dlg, rad3)) {
        if (!default_printer(st))
            return;
    } else if (!chosen_printer(st)) {
        return;
    }
    if (!force && !lstrcmp(dev, st->device) && !lstrcmp(drv, st->driver) &&
        !lstrcmp(port, st->port))
        return;
    free_driver(st);
    if (!load_driver(st)) {
        LPSTR f = load_str(IDS_NODRIVER);
        char t[200], file[20], cap[32];
        s_cpy(file, st->driver, 9);
        s_cat(file, ".DRV", sizeof file);
        if (f) {
            wsprintf(t, f, (LPSTR)st->device, (LPSTR)file);
            GetWindowText(st->dlg, cap, sizeof cap);
            MessageBox(st->dlg, t, cap, MB_OK | MB_ICONEXCLAMATION);
            mem_free(f);
        }
        return;
    }
    if (st->pd->hDevMode) {
        GlobalFree(st->pd->hDevMode);
        st->pd->hDevMode = NULL;
    }
    get_devmode(st, st->dlg);
    fill_paper(st);
}

static void setup_ok(pstate FAR *st)
{
    HWND dlg = st->dlg;
    LPDEVMODE dm = lock_dm(st);
    if (dm) {
        int i = (int)SendDlgItemMessage(dlg, cmb2, CB_GETCURSEL, 0, 0);
        dm->dmOrientation = IsDlgButtonChecked(dlg, rad2) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        if (i >= 0 && i < st->npapers)
            dm->dmPaperSize = st->papers[i];
        i = (int)SendDlgItemMessage(dlg, cmb3, CB_GETCURSEL, 0, 0);
        if (i >= 0 && i < st->nbins)
            dm->dmDefaultSource = st->bins[i];
    }
    unlock_dm(st);
    st->is_default = IsDlgButtonChecked(dlg, rad3) != 0;
}

/* Options...: the driver's AdvancedSetupDialog (observed: its "Options"
   dialog, not the whole set-up ExtDeviceMode shows), on the DEVMODE with
   this dialog's choices; UNVERIFIED: ExtDeviceMode with DM_PROMPT for a
   driver without one */
typedef LONG (FAR PASCAL *ADVSETUPPROC)(HWND, HANDLE, LPDEVMODE, LPDEVMODE);

static void options(pstate FAR *st)
{
    LPDEVMODE dm;
    LONG r = 0;
    ADVSETUPPROC adv = (ADVSETUPPROC)GetProcAddress(st->lib, "AdvancedSetupDialog");
    if (!adv && !st->extdm)
        return;
    setup_ok(st);
    dm = lock_dm(st);
    if (dm) {
        if (adv)
            r = adv(st->dlg, st->lib, dm, dm);
        else
            r = st->extdm(st->dlg, st->lib, dm, st->device, st->port, dm, NULL,
                          DM_COPY | DM_MODIFY | DM_PROMPT);
    }
    unlock_dm(st);
    fill_paper(st);
    /* observed: after OK there, OK is the default button again; after
       Cancel Options... keeps the focus */
    if (r == IDOK)
        SendMessage(st->dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(st->dlg, IDOK), TRUE);
}

/* ---- the procedures ---- */

static int run_setup(pstate FAR *st, HWND owner);

static BOOL print_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, BOOL setup)
{
    pstate FAR *st;
    if (msg == WM_INITDIALOG) {
        st = (pstate FAR *)lp;
        st->dlg = dlg;
        st->setup = setup;
        state_set(dlg, st);
        if (setup)
            setup_init(st);
        else
            print_init(st);
        if (st->hook)
            return (BOOL)call_hook(st->hook, dlg, msg, wp, (LPARAM)st->pd);
        return TRUE;
    }
    st = (pstate FAR *)state_get(dlg);
    if (!st)
        return FALSE;
    if (st->hook) {
        UINT r = call_hook(st->hook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    /* observed: once Print has run Print Setup, its WM_COMMANDs go to the
       Setup hook as well (UNVERIFIED: other messages, the return value) */
    if (!setup && st->setup_ran && msg == WM_COMMAND &&
        (st->pd->Flags & PD_ENABLESETUPHOOK) && st->pd->lpfnSetupHook) {
        UINT r = call_hook((cd_hook)st->pd->lpfnSetupHook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    switch (msg) {
    case WM_DESTROY:
        state_remove(dlg);
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            if (setup) {
                setup_ok(st);
                EndDialog(dlg, IDOK);
            } else if (print_ok(st)) {
                EndDialog(dlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case pshHelp:
            send_help(dlg, st->pd->hwndOwner, (LPARAM)st->pd);
            return TRUE;
        case psh1:
            if (setup) {
                options(st);
            } else {
                /* Print Setup over the Print dialog, then its new state */
                pstate FAR *sub = mem_alloc(sizeof(pstate));
                if (sub) {
                    *sub = *st;
                    sub->hook = (st->pd->Flags & PD_ENABLESETUPHOOK)
                                ? (cd_hook)st->pd->lpfnSetupHook : NULL;
                    sub->res = NULL;
                    sub->papers = sub->bins = NULL;
                    sub->nres = sub->npapers = sub->nbins = 0;
                    sub->lib = NULL;
                    st->setup_ran = TRUE;
                    load_driver(sub);
                    if (run_setup(sub, dlg) == IDOK) {
                        free_driver(st);
                        s_cpy(st->device, sub->device, sizeof st->device);
                        s_cpy(st->driver, sub->driver, sizeof st->driver);
                        s_cpy(st->port, sub->port, sizeof st->port);
                        st->is_default = sub->is_default;
                        /* observed: OK there clears both wDefault bits,
                           and the DEVNAMES stays even if Print is
                           cancelled */
                        set_names(st, 0, DN_MADE | DN_PRINTDLG);
                        st->made_names = FALSE;
                        load_driver(st);
                        printer_text(st);
                        fill_quality(st);
                    }
                    /* observed: OK is the default button again (after
                       OK or Cancel in Print Setup) */
                    SendMessage(dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(dlg, IDOK), TRUE);
                    free_driver(sub);
                    mem_free(sub);
                }
            }
            return TRUE;
        case rad1:
        case rad2:
        case rad3:
        case rad4:
            /* plain radio buttons: the dialog checks them */
            if (HIWORD(lp) != BN_CLICKED)
                return TRUE;
            if (!setup) {
                if (wp != rad4)
                    CheckRadioButton(dlg, rad1, rad3, wp);
            } else if (wp == rad1 || wp == rad2) {
                CheckRadioButton(dlg, rad1, rad2, wp);
                set_orient_icon(st, wp == rad2);
            } else if (!IsDlgButtonChecked(dlg, wp)) {
                /* observed: the other printer button reads the printer's
                   settings anew; Specific moves the focus to the list */
                CheckRadioButton(dlg, rad3, rad4, wp);
                switch_printer(st, TRUE);
                if (wp == rad4)
                    SetFocus(GetDlgItem(dlg, cmb1));
            }
            return TRUE;
        case cmb1:
            /* observed: the list taking the focus makes the printer
               specific (UNVERIFIED: whether it is the focus or the
               drop-down) */
            if (setup && HIWORD(lp) == CBN_SETFOCUS && IsDlgButtonChecked(dlg, rad3))
                CheckRadioButton(dlg, rad3, rad4, rad4);
            if (setup && HIWORD(lp) == CBN_SELCHANGE) {
                CheckRadioButton(dlg, rad3, rad4, rad4);
                switch_printer(st, FALSE);
            }
            return TRUE;
        case edt1:
        case edt2:
            if (!setup && HIWORD(lp) == EN_CHANGE && GetFocus() == (HWND)LOWORD(lp))
                CheckRadioButton(dlg, rad1, rad3, rad3);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CDPROC PrintDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return print_proc(dlg, msg, wp, lp, FALSE);
}

BOOL CDPROC PrintSetupDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return print_proc(dlg, msg, wp, lp, TRUE);
}

static int run_setup(pstate FAR *st, HWND owner)
{
    LPPRINTDLG pd = st->pd;
    cd_template t;
    t.use_handle = (pd->Flags & PD_ENABLESETUPTEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (pd->Flags & PD_ENABLESETUPTEMPLATE);
    t.app = t.use_handle ? (HINSTANCE)pd->hSetupTemplate : pd->hInstance;
    t.name = pd->lpSetupTemplateName;
    t.ours = MAKEINTRESOURCE(DLG_PRNSETUP);
    return cd_modal(&t, owner, (FARPROC)PrintSetupDlgProc, (LPARAM)(LPVOID)st);
}

static int run_print(pstate FAR *st)
{
    LPPRINTDLG pd = st->pd;
    cd_template t;
    t.use_handle = (pd->Flags & PD_ENABLEPRINTTEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (pd->Flags & PD_ENABLEPRINTTEMPLATE);
    t.app = t.use_handle ? (HINSTANCE)pd->hPrintTemplate : pd->hInstance;
    t.name = pd->lpPrintTemplateName;
    t.ours = MAKEINTRESOURCE(DLG_PRINT);
    return cd_modal(&t, pd->hwndOwner, (FARPROC)PrintDlgProc, (LPARAM)(LPVOID)st);
}

static void make_dc(pstate FAR *st)
{
    LPPRINTDLG pd = st->pd;
    LPDEVMODE dm;
    char drv[13];
    if (!(pd->Flags & (PD_RETURNDC | PD_RETURNIC)))
        return;
    s_cpy(drv, st->driver, sizeof drv);
    dm = lock_dm(st);
    if (pd->Flags & PD_RETURNDC)
        pd->hDC = CreateDC(drv, st->device, st->port, (LPSTR)dm);
    else
        pd->hDC = CreateIC(drv, st->device, st->port, (LPSTR)dm);
    unlock_dm(st);
    if (!pd->hDC)
        cd_seterr(PDERR_CREATEICFAILURE);
}

BOOL CDAPI PrintDlg(LPPRINTDLG pd)
{
    pstate FAR *st;
    BOOL r = FALSE;
    int k;
    cd_seterr(0);
    if (!pd) {
        cd_seterr(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (pd->lStructSize != sizeof(PRINTDLG)) {
        cd_seterr(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if (((pd->Flags & PD_ENABLEPRINTHOOK) && !pd->lpfnPrintHook) ||
        ((pd->Flags & PD_ENABLESETUPHOOK) && !pd->lpfnSetupHook)) {
        cd_seterr(CDERR_NOHOOK);
        return FALSE;
    }
    if ((pd->Flags & PD_RETURNDEFAULT) && (pd->hDevMode || pd->hDevNames)) {
        cd_seterr(PDERR_RETDEFFAILURE);
        return FALSE;
    }
    st = mem_alloc(sizeof(pstate));
    if (!st) {
        cd_seterr(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    st->pd = pd;
    if (pd->hDevNames) {
        names_from(st, pd->hDevNames);
    } else {
        /* only a DEVMODE: the printer it names, as a specific printer
           (observed); else the default */
        BOOL found = FALSE;
        if (pd->hDevMode) {
            LPDEVMODE dm = (LPDEVMODE)GlobalLock(pd->hDevMode);
            if (dm) {
                found = printer_named(st, dm->dmDeviceName);
                GlobalUnlock(pd->hDevMode);
            }
        }
        if (!found) {
            if (!default_printer(st)) {
                /* observed: no message with PD_RETURNDEFAULT either */
            if (!(pd->Flags & (PD_NOWARNING | PD_RETURNDEFAULT))) {
                    LPSTR t = load_str(IDS_NODEFPRINTER), c = load_str(IDS_PRINTSETUP);
                    MessageBox(pd->hwndOwner, t, c, MB_OK | MB_ICONEXCLAMATION);
                    mem_free(t);
                    mem_free(c);
                }
                cd_seterr(PDERR_NODEFAULTPRN);
                mem_free(st);
                return FALSE;
            }
            st->is_default = TRUE;
        }
        pd->hDevNames = make_names(st, (st->is_default ? DN_DEFAULTPRN : 0) | DN_MADE);
        st->made_names = TRUE;
    }
    st->made_mode = pd->hDevMode == NULL;
    if (!(pd->Flags & PD_RETURNDEFAULT))
        set_names(st, DN_PRINTDLG, 0);
    if (!load_driver(st)) {
        cd_seterr(PDERR_LOADDRVFAILURE);
        if (pd->Flags & PD_RETURNDEFAULT) {
            GlobalFree(pd->hDevNames);
            pd->hDevNames = NULL;
        }
        mem_free(st);
        return FALSE;
    }
    if (!get_devmode(st, pd->hwndOwner)) {
        cd_seterr(PDERR_GETDEVMODEFAIL);
        free_driver(st);
        mem_free(st);
        return FALSE;
    }
    if (pd->Flags & PD_RETURNDEFAULT) {
        make_dc(st);
        r = TRUE;
    } else {
        BOOL setup = (pd->Flags & PD_PRINTSETUP) != 0;
        st->hook = setup
            ? ((pd->Flags & PD_ENABLESETUPHOOK) ? (cd_hook)pd->lpfnSetupHook : NULL)
            : ((pd->Flags & PD_ENABLEPRINTHOOK) ? (cd_hook)pd->lpfnPrintHook : NULL);
        k = setup ? run_setup(st, pd->hwndOwner) : run_print(st);
        if (k == IDOK) {
            /* observed: Print Setup clears both bits, Print 4000h */
            set_names(st, 0, setup ? DN_MADE | DN_PRINTDLG : DN_MADE);
            make_dc(st);
            r = TRUE;
        } else {
            /* observed: Cancel frees the DEVNAMES the call made; in Print
               Setup the DEVMODE it made too, in Print not */
            if (st->made_names) {
                GlobalFree(pd->hDevNames);
                pd->hDevNames = NULL;
            }
            if (setup && st->made_mode && pd->hDevMode) {
                GlobalFree(pd->hDevMode);
                pd->hDevMode = NULL;
            }
        }
    }
    free_driver(st);
    mem_free(st);
    return r;
}


/* edit controls that take digits only (observed on From, To, Copies):
   other characters beep (UNVERIFIED: the beep) */
LRESULT CDPROC EditIntegerOnly(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CHAR && wp >= ' ' && (wp < '0' || wp > '9')) {
        MessageBeep(0);
        return 0;
    }
    return call_old(w, msg, wp, lp);
}

/* Print Setup's printer list (observed: it carries this procedure;
   UNVERIFIED: what it changes - ours passes everything on) */
LRESULT CDPROC dwUpArrowHack(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return call_old(w, msg, wp, lp);
}
