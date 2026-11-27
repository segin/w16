/*
 * cpwin386.c - the 386 Enhanced applet: device contention for the serial
 * ports, scheduling (SYSTEM.INI [386Enh]) and the Virtual Memory (swap
 * file) dialog.
 *
 * Observed on Windows 3.1 in 386 Enhanced mode (black box: SNAP window
 * dumps, screenshots and SYSTEM.INI after each action;
 * docs/notes/cpl-fonts-ports-handover.md):
 * - Control Panel shows the applet only in 386 Enhanced mode; its status
 *   text "Optimizes Windows for 386 enhanced mode."
 * - Device Contention: the serial ports the BIOS reports, as "Com1" ...;
 *   Always Warn, Never Warn, Idle (in sec.) with the seconds (2 when the
 *   key is absent). Scheduling: Windows in Foreground and in Background
 *   (WinTimeslice, 100,50), Exclusive in Foreground (WinExclusive, 0),
 *   Minimum Timeslice (MinTimeslice, 20).
 * - OK checks Minimum Timeslice ("Invalid minimum timeslice value; it must
 *   be a value between 1 and 10000.") before the foreground and background
 *   values ("Invalid foreground value.", "Invalid background value.";
 *   20000 and 0 are refused), in "Control Panel - 386 Enhanced" boxes with
 *   the exclamation icon; then writes MinTimeslice, WinTimeslice=fg,bg,
 *   WinExclusive and ComNAutoAssign for every port listed (0 Never Warn,
 *   -1 Always Warn: clicked on 3.1; the seconds for Idle), whether changed
 *   or not, and
 *   closes without asking to restart.
 * - Virtual Memory: the current swap file (drive, size, type: "Temporary
 *   (using MS-DOS)"); Change>> widens the dialog (New Settings: drive,
 *   type Permanent/Temporary/None, the space available, the recommended
 *   maximum size, New Size) and removes "Use 32-Bit Disk Access" (shown
 *   disabled without 32-bit disk access). OK after a change asks "Are you
 *   sure you want to make changes to virtual-memory settings?" (Yes/No);
 *   Yes writes [386Enh]: a temporary file's size as MaxPagingFileSize (no
 *   restart asked); None as Paging=0 with PagingFile and MaxPagingFileSize
 *   removed, then a dialog asks to restart Windows (Restart Windows /
 *   Continue).
 * Where w16 differs: the swap file's figures come from 3.1's WIN386
 *   (PageFile device); w16 computes them from SYSTEM.INI and the disk's free
 *   space (the recommended maximum: half the free space, at most
 *   MaxPagingFileSize: UNVERIFIED), and a permanent swap file is written as
 *   [386Enh] PermSwapDOSDrive and PermSwapSizeK (the keys 3.1's SETUP
 *   documents) without making 386SPART.PAR: w16's WIN386 makes it from
 *   those keys when it starts, and takes a temporary file's size by the
 *   same rule as the recommended size here (ARCHITECTURE.md 12.17).
 * UNVERIFIED: the idle range (w16: 1-1000 by the arrows, not checked on
 *   OK), what None and Permanent show in New Settings, the restart dialog
 *   for Permanent.
 */
#include "cpwin386.h"
#include "cplutil.h"

HINSTANCE g_hinst;

static char s_enh[] = "386Enh";
static char s_sysini[] = "SYSTEM.INI";

static int dlg_box(HWND parent, int id, DLGPROC proc)
{
    return DialogBox(g_hinst, MAKEINTRESOURCE(id), parent, proc);
}

static void help(HWND dlg)
{
    WinHelp(dlg, "CONTROL.HLP", HELP_KEY, (DWORD)(LPSTR)"386 Enhanced");
}

static int msg(HWND dlg, int text, int title, UINT flags)
{
    char t[200], c[80];
    LoadString(g_hinst, text, t, sizeof t);
    LoadString(g_hinst, title, c, sizeof c);
    return MessageBox(dlg, t, c, flags);
}

static void get_ini(const char *key, const char *def, char *buf, int size)
{
    GetPrivateProfileString(s_enh, key, def, buf, size, s_sysini);
}

static void put_ini(const char *key, const char *val)
{
    WritePrivateProfileString(s_enh, key, val, s_sysini);
}

static void put_int(const char *key, long v)
{
    char t[16];
    wsprintf(t, "%ld", v);
    put_ini(key, t);
}

static long get_num(HWND dlg, int id, BOOL *ok)
{
    char t[16];
    long v = 0;
    GetDlgItemText(dlg, id, t, sizeof t);
    *ok = cpl_atol(t, &v);
    return v;
}

/* ---- the serial ports the BIOS knows (its data area, 40:0, holds four
   addresses: a format bound) ---- */

static int nports;
static int port_no[4];          /* COM number of list item k */
static int port_val[4];         /* -1 always, 0 never, n idle */
static int cur;

static void find_ports(void)
{
    FARPROC p = GetProcAddress(GetModuleHandle("KERNEL"), "__0040H");
    WORD FAR *bda = p ? (WORD FAR *)MAKELP(LOWORD((DWORD)p), 0) : NULL;
    int i;
    nports = 0;
    for (i = 0; i < 4; i++)
        if (!bda || bda[i])
            port_no[nports++] = i + 1;
}

static void show_port(HWND dlg)
{
    int v = port_val[cur];
    char t[8];
    CheckRadioButton(dlg, IDC_EN_ALWAYS, IDC_EN_IDLE,
                     v < 0 ? IDC_EN_ALWAYS : v == 0 ? IDC_EN_NEVER : IDC_EN_IDLE);
    wsprintf(t, "%d", v > 0 ? v : 2);
    SetDlgItemText(dlg, IDC_EN_IDLESEC, t);
}

static void keep_port(HWND dlg)
{
    BOOL ok;
    long v;
    if (IsDlgButtonChecked(dlg, IDC_EN_NEVER))
        port_val[cur] = 0;
    else if (IsDlgButtonChecked(dlg, IDC_EN_ALWAYS))
        port_val[cur] = -1;
    else {
        v = get_num(dlg, IDC_EN_IDLESEC, &ok);
        port_val[cur] = ok && v > 0 ? (int)(v > 32767L ? 32767L : v) : 2;
    }
}

/* ---- the swap file ---- */

typedef struct {
    int type;                   /* 0 permanent, 1 temporary, 2 none */
    char drive;                 /* 'A'.. */
    long size;                  /* KB */
} SWAP;

static SWAP swap0, swapn;
static BOOL swap_changed;

extern void FAR PASCAL DOS3Call(void);

/* free KB on a drive (INT 21h 36h); -1 if no such drive */
static long free_kb(char drive)
{
    WORD spc = 0, freec = 0, bps = 0;
    BYTE d;
    if (drive >= 'a' && drive <= 'z')
        drive -= 'a' - 'A';
    if (drive < 'A' || drive > 'Z')
        return -1;              /* not a drive letter (DL 0 is the current drive) */
    d = (BYTE)(drive - 'A' + 1);
    _asm {
        mov ah, 36h
        mov dl, d
        call DOS3Call
        mov spc, ax
        mov freec, bx
        mov bps, cx
    }
    if (spc == 0xFFFF)
        return -1;
    return (long)((DWORD)spc * bps / 512 * freec / 2);
}

/* free KB plus the temporary swap file's own size on that drive (observed:
   3.1's Space Available is 4,096 KB above the free space with its swap
   file in use) */
static long avail_kb(char drive)
{
    char t[160];
    long f = free_kb(drive), n;
    if (f < 0)
        return f;
    get_ini("PagingFile", "", t, sizeof t);
    if (!t[0]) {
        char d[160];
        cpl_windir(d, sizeof d);
        cpl_join(t, sizeof t, d, "WIN386.SWP");
    }
    AnsiUpper(t);
    if (t[0] == drive && t[1] == ':' && (n = cpl_file_size(t)) > 0)
        f += n / 1024;
    return f;
}

static long recommended(char drive)
{
    char t[16];
    long f = avail_kb(drive), max;
    if (f < 0)
        return 0;
    f /= 2;
    get_ini("MaxPagingFileSize", "", t, sizeof t);
    if (t[0] && cpl_atol(t, &max) && max >= 0 && max < f)
        f = max;
    return f;
}

static void read_swap(SWAP *s)
{
    char t[160], w[160];
    long v;
    get_ini("Paging", "", t, sizeof t);
    AnsiLower(t);
    get_ini("PermSwapDOSDrive", "", w, sizeof w);
    if (!lstrcmp(t, "0") || !lstrcmp(t, "off") || !lstrcmp(t, "no") || !lstrcmp(t, "false")) {
        s->type = 2;
        cpl_windir(w, sizeof w);
        s->drive = w[0];
        s->size = 0;
    } else if (w[0]) {
        s->type = 0;
        s->drive = w[0];
        get_ini("PermSwapSizeK", "0", t, sizeof t);
        s->size = cpl_atol(t, &v) ? v : 0;
    } else {
        s->type = 1;
        cpl_windir(w, sizeof w);
        get_ini("PagingDrive", "", t, sizeof t);
        if (t[0])
            w[0] = t[0];
        get_ini("PagingFile", "", t, sizeof t);
        if (t[0] && t[1] == ':')
            w[0] = t[0];
        s->drive = w[0];
    }
    if (s->drive >= 'a' && s->drive <= 'z')
        s->drive -= 'a' - 'A';
    if (s->type == 1)
        s->size = recommended(s->drive);
}

static void set_num(HWND dlg, int id, long n)
{
    char t[16];
    cpl_thousands(t, n);
    SetDlgItemText(dlg, id, t);
}

static void set_str(HWND dlg, int id, int sid)
{
    char t[80];
    t[0] = 0;
    if (sid)
        LoadString(g_hinst, sid, t, sizeof t);
    SetDlgItemText(dlg, id, t);
}

static void show_current(HWND dlg)
{
    char t[80];
    wsprintf(t, "%c:", swap0.drive);
    SetDlgItemText(dlg, IDC_VM_CURDRIVE, t);
    if (swap0.type == 2) {
        SetDlgItemText(dlg, IDC_VM_CURDRIVE, "");
        SetDlgItemText(dlg, IDC_VM_CURSIZE, "");
    } else {
        char k[8];
        cpl_thousands(t, swap0.size);
        LoadString(g_hinst, IDS_VM_KB, k, sizeof k);
        lstrcat(t, " ");
        lstrcat(t, k);
        SetDlgItemText(dlg, IDC_VM_CURSIZE, t);
    }
    set_str(dlg, IDC_VM_CURTYPE, IDS_VM_PERM + swap0.type);
}

static char drive_of(HWND dlg)
{
    char t[8];
    int i = (int)SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_GETCURSEL, 0, 0);
    if (i < 0 || SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_GETLBTEXTLEN, i, 0) >= 64)
        return swap0.drive;
    {
        char b[64];
        SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_GETLBTEXT, i, (LPARAM)(LPSTR)b);
        t[0] = b[0];
    }
    return (char)(t[0] >= 'a' ? t[0] - ('a' - 'A') : t[0]);
}

/* the New Settings figures for the chosen drive and type */
static void show_new(HWND dlg)
{
    int type = (int)SendDlgItemMessage(dlg, IDC_VM_TYPE, CB_GETCURSEL, 0, 0);
    char d = drive_of(dlg);
    long f = avail_kb(d), rec = recommended(d);
    set_num(dlg, IDC_VM_AVAIL, f < 0 ? 0 : f);
    set_str(dlg, IDC_VM_L3, 0);
    set_str(dlg, IDC_VM_V3, 0);
    set_str(dlg, IDC_VM_K3, 0);
    /* observed: None keeps the temporary file's figures */
    if (type == 0) {
        set_str(dlg, IDC_VM_L2, IDS_VM_MAXSIZE);
        set_num(dlg, IDC_VM_V2, f < 0 ? 0 : f);
        set_str(dlg, IDC_VM_K2, IDS_VM_KB);
        set_str(dlg, IDC_VM_L3, IDS_VM_RECSIZE);
        set_num(dlg, IDC_VM_V3, rec);
        set_str(dlg, IDC_VM_K3, IDS_VM_KB);
    } else {
        set_str(dlg, IDC_VM_L2, IDS_VM_RECMAX);
        set_num(dlg, IDC_VM_V2, rec);
        set_str(dlg, IDC_VM_K2, IDS_VM_KB);
    }
    {
        char t[16];
        wsprintf(t, "%ld", rec);
        SetDlgItemText(dlg, IDC_VM_SIZE, t);
    }
}

static void fill_drives(HWND dlg)
{
    int n;
    char item[64], spec[8];
    CPLFIND fd;
    for (n = 0; n < 26; n++) {
        UINT t = GetDriveType(n);
        int i, k;
        if (t != DRIVE_FIXED && t != DRIVE_REMOTE)
            continue;
        wsprintf(item, "%c: ", 'a' + n);
        wsprintf(spec, "%c:\\*.*", 'A' + n);
        if (cpl_find(spec, 8, &fd, TRUE) && (fd.attr & 8)) {
            k = lstrlen(item);
            item[k++] = '[';
            for (i = 0; fd.name[i]; i++)
                if (fd.name[i] != '.')
                    item[k++] = fd.name[i];
            item[k++] = ']';
            item[k] = 0;
            AnsiLower(item);
        }
        i = (int)SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
        if ('A' + n == swap0.drive)
            SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_SETCURSEL, i, 0);
    }
    if (SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_GETCURSEL, 0, 0) < 0)
        SendDlgItemMessage(dlg, IDC_VM_DRIVE, CB_SETCURSEL, 0, 0);
}

static int collapsed_h, full_h;

BOOL FAR PASCAL __loadds VmRestartProc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (m == WM_INITDIALOG) {
        SendDlgItemMessage(dlg, IDC_VR_ICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        return TRUE;
    }
    if (m == WM_COMMAND && (wp == IDC_VR_RESTART || wp == IDC_VR_CONTINUE || wp == IDCANCEL)) {
        EndDialog(dlg, wp);
        return TRUE;
    }
    return FALSE;
}

static void write_swap(HWND dlg, SWAP *s)
{
    char t[160];
    BOOL restart = s->type != swap0.type || s->drive != swap0.drive;
    if (s->type == 2) {
        put_ini("Paging", "0");
        put_ini("PagingFile", NULL);
        put_ini("MaxPagingFileSize", NULL);
        put_ini("PermSwapDOSDrive", NULL);
        put_ini("PermSwapSizeK", NULL);
    } else if (s->type == 1) {
        put_ini("Paging", NULL);
        put_ini("PermSwapDOSDrive", NULL);
        put_ini("PermSwapSizeK", NULL);
        if (s->drive != swap0.drive || swap0.type != 1) {
            wsprintf(t, "%c:\\WIN386.SWP", s->drive);
            put_ini("PagingFile", t);
        }
        put_int("MaxPagingFileSize", s->size);
    } else {
        put_ini("Paging", NULL);
        put_ini("PagingFile", NULL);
        put_ini("MaxPagingFileSize", NULL);
        wsprintf(t, "%c", s->drive);
        put_ini("PermSwapDOSDrive", t);
        put_int("PermSwapSizeK", s->size);
    }
    if (restart && dlg_box(GetParent(dlg), DLG_VMRESTART, VmRestartProc) == IDC_VR_RESTART)
        ExitWindows(EW_RESTARTWINDOWS, 0);
}

BOOL FAR PASCAL __loadds VmemProc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG: {
        RECT r, g;
        read_swap(&swap0);
        swap_changed = FALSE;
        show_current(dlg);
        set_str(dlg, IDC_VM_L2, 0);
        set_str(dlg, IDC_VM_K3, IDS_VM_KB);
        /* shown collapsed: down to the New Settings group */
        GetWindowRect(dlg, &r);
        GetWindowRect(GetDlgItem(dlg, IDC_VM_NEWGROUP), &g);
        full_h = r.bottom - r.top;
        {
            /* observed: the client area ends 3 pixels above the New
               Settings group (114 of 278 pixels) */
            RECT c;
            POINT p;
            p.x = p.y = 0;
            ClientToScreen(dlg, &p);
            GetClientRect(dlg, &c);
            collapsed_h = full_h - c.bottom + (g.top - p.y) - 3;
        }
        SetWindowPos(dlg, NULL, 0, 0, r.right - r.left, collapsed_h, SWP_NOMOVE | SWP_NOZORDER);
        return TRUE;
    }
    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mi = (LPMEASUREITEMSTRUCT)lp;
        mi->itemHeight = 16;
        return TRUE;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        char t[64];
        BOOL sel = (d->itemState & ODS_SELECTED) != 0;
        if ((int)d->itemID < 0)
            return TRUE;
        if (SendMessage(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)(LPSTR)t) == CB_ERR)
            t[0] = 0;
        SetBkColor(d->hDC, GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(d->hDC, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        /* observed: a drive picture, the name 22 pixels in (w16's own
           picture: a drive's front with its light) */
        ExtTextOut(d->hDC, d->rcItem.left + 22, d->rcItem.top + 1, ETO_OPAQUE, &d->rcItem, t,
                   lstrlen(t), NULL);
        {
            RECT b;
            HBRUSH gray = CreateSolidBrush(RGB(192, 192, 192));
            SetRect(&b, d->rcItem.left + 5, d->rcItem.top + 4, d->rcItem.left + 21,
                    d->rcItem.top + 12);
            FillRect(d->hDC, &b, gray);
            FrameRect(d->hDC, &b, GetStockObject(BLACK_BRUSH));
            SetRect(&b, b.left + 2, b.bottom - 3, b.left + 5, b.bottom - 2);
            FillRect(d->hDC, &b, GetStockObject(BLACK_BRUSH));
            DeleteObject(gray);
        }
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_VM_CHANGE: {
            RECT r;
            char t[80];
            int k;
            HWND b = GetDlgItem(dlg, IDC_VM_32BIT);
            GetWindowRect(dlg, &r);
            SetWindowPos(dlg, NULL, 0, 0, r.right - r.left, full_h, SWP_NOMOVE | SWP_NOZORDER);
            if (b)
                DestroyWindow(b);
            fill_drives(dlg);
            for (k = 0; k < 3; k++) {
                LoadString(g_hinst, IDS_VM_PERMANENT + k, t, sizeof t);
                SendDlgItemMessage(dlg, IDC_VM_TYPE, CB_INSERTSTRING, k, (LPARAM)(LPSTR)t);
            }
            SendDlgItemMessage(dlg, IDC_VM_TYPE, CB_SETCURSEL, swap0.type, 0);
            EnableWindow(GetDlgItem(dlg, IDC_VM_DRIVE), TRUE);
            EnableWindow(GetDlgItem(dlg, IDC_VM_TYPE), TRUE);
            EnableWindow(GetDlgItem(dlg, IDC_VM_SIZE), TRUE);
            set_str(dlg, IDC_VM_SIZELBL, IDS_VM_NEWSIZE);
            show_new(dlg);
            swap_changed = TRUE;
            SetFocus(GetDlgItem(dlg, IDC_VM_SIZE));
            SendDlgItemMessage(dlg, IDC_VM_SIZE, EM_SETSEL, 0, MAKELPARAM(0, -1));
            EnableWindow(GetDlgItem(dlg, IDC_VM_CHANGE), FALSE);
            return TRUE;
        }
        case IDC_VM_DRIVE:
        case IDC_VM_TYPE:
            if (HIWORD(lp) == CBN_SELCHANGE)
                show_new(dlg);
            return TRUE;
        case IDOK:
            if (swap_changed) {
                BOOL ok;
                long v = get_num(dlg, IDC_VM_SIZE, &ok);
                swapn.type = (int)SendDlgItemMessage(dlg, IDC_VM_TYPE, CB_GETCURSEL, 0, 0);
                swapn.drive = drive_of(dlg);
                swapn.size = ok && v > 0 ? v : 0;
                if (msg(dlg, IDS_VM_CONFIRM, IDS_VM_TITLE, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
                    return TRUE;
                /* observed: the restart dialog comes while this one is
                   still up */
                write_swap(dlg, &swapn);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_VM_HELP:
            help(dlg);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- the 386 Enhanced dialog ---- */

static void set_int(HWND dlg, int id, long v)
{
    char t[16];
    wsprintf(t, "%ld", v);
    SetDlgItemText(dlg, id, t);
}

static void init_enh(HWND dlg)
{
    char t[40], key[20];
    long fg = 100, bg = 50, v;
    int i;
    find_ports();
    for (i = 0; i < nports; i++) {
        wsprintf(t, "Com%d", port_no[i]);
        SendDlgItemMessage(dlg, IDC_EN_PORTS, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        wsprintf(key, "Com%dAutoAssign", port_no[i]);
        get_ini(key, "2", t, sizeof t);
        port_val[i] = cpl_atol(t, &v) ? (v < 0 ? -1 : (int)(v > 32767L ? 32767L : v)) : 2;
    }
    cur = 0;
    SendDlgItemMessage(dlg, IDC_EN_PORTS, LB_SETCURSEL, 0, 0);
    show_port(dlg);
    get_ini("WinTimeslice", "100,50", t, sizeof t);
    for (i = 0; t[i] && t[i] != ','; i++)
        ;
    if (t[i]) {
        t[i] = 0;
        if (!cpl_atol(t, &fg))
            fg = 100;
        if (!cpl_atol(t + i + 1, &bg))
            bg = 50;
    }
    set_int(dlg, IDC_EN_FG, fg);
    set_int(dlg, IDC_EN_BG, bg);
    get_ini("MinTimeslice", "20", t, sizeof t);
    set_int(dlg, IDC_EN_MIN, cpl_atol(t, &v) ? v : 20);
    get_ini("WinExclusive", "0", t, sizeof t);
    AnsiLower(t);
    CheckDlgButton(dlg, IDC_EN_EXCL, t[0] == '1' || !lstrcmp(t, "on") || !lstrcmp(t, "yes") ||
                                         !lstrcmp(t, "true"));
}

static BOOL check(HWND dlg, int id, int text)
{
    BOOL ok;
    long v = get_num(dlg, id, &ok);
    if (ok && v >= 1 && v <= 10000)
        return TRUE;
    msg(dlg, text, IDS_TITLE, MB_OK | MB_ICONEXCLAMATION);
    SetFocus(GetDlgItem(dlg, id));
    SendDlgItemMessage(dlg, id, EM_SETSEL, 0, MAKELPARAM(0, -1));
    return FALSE;
}

static BOOL ok_enh(HWND dlg)
{
    BOOL ok;
    char t[32], key[20];
    int i;
    if (!check(dlg, IDC_EN_MIN, IDS_BAD_MIN) || !check(dlg, IDC_EN_FG, IDS_BAD_FG) ||
        !check(dlg, IDC_EN_BG, IDS_BAD_BG))
        return FALSE;
    keep_port(dlg);
    put_int("MinTimeslice", get_num(dlg, IDC_EN_MIN, &ok));
    wsprintf(t, "%ld,%ld", get_num(dlg, IDC_EN_FG, &ok), get_num(dlg, IDC_EN_BG, &ok));
    put_ini("WinTimeslice", t);
    put_ini("WinExclusive", IsDlgButtonChecked(dlg, IDC_EN_EXCL) ? "1" : "0");
    for (i = 0; i < nports; i++) {
        wsprintf(key, "Com%dAutoAssign", port_no[i]);
        put_int(key, port_val[i]);
    }
    return TRUE;
}

static void spin(HWND dlg, int edit, int delta)
{
    BOOL ok;
    long v = get_num(dlg, edit, &ok), lo = 1, hi = edit == IDC_EN_IDLESEC ? 1000 : 10000;
    if (!ok)
        v = lo;
    v += delta;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    set_int(dlg, edit, v);
    if (edit == IDC_EN_IDLESEC)
        CheckRadioButton(dlg, IDC_EN_ALWAYS, IDC_EN_IDLE, IDC_EN_IDLE);
}

BOOL FAR PASCAL __loadds EnhProc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG:
        init_enh(dlg);
        return TRUE;
    case WM_VSCROLL: {
        HWND a = (HWND)HIWORD(lp);
        int id = GetDlgCtrlID(a);
        if (wp == SB_LINEUP || wp == SB_LINEDOWN)
            spin(dlg, id - 1, wp == SB_LINEUP ? 1 : -1);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_EN_PORTS:
            if (HIWORD(lp) == LBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_EN_PORTS, LB_GETCURSEL, 0, 0);
                keep_port(dlg);
                if (i >= 0 && i < nports)
                    cur = i;
                show_port(dlg);
            }
            return TRUE;
        case IDC_EN_ALWAYS:
        case IDC_EN_NEVER:
        case IDC_EN_IDLE:
            CheckRadioButton(dlg, IDC_EN_ALWAYS, IDC_EN_IDLE, wp);
            keep_port(dlg);
            return TRUE;
        case IDC_EN_EXCL:
            CheckDlgButton(dlg, IDC_EN_EXCL, !IsDlgButtonChecked(dlg, IDC_EN_EXCL));
            return TRUE;
        case IDC_EN_VMEM:
            dlg_box(dlg, DLG_VMEM, VmemProc);
            return TRUE;
        case IDOK:
            if (ok_enh(dlg))
                EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            help(dlg);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- CPlApplet ---- */

LONG FAR PASCAL __loadds CPlApplet(HWND hwnd, UINT m, LPARAM l1, LPARAM l2)
{
    switch (m) {
    case CPL_INIT:
        /* observed: not in Control Panel in Standard mode */
        return (GetWinFlags() & WF_ENHANCED) != 0;
    case CPL_GETCOUNT:
        return 1;
    case CPL_INQUIRE: {
        LPCPLINFO p = (LPCPLINFO)l2;
        if (l1 != 0 || p == NULL)
            return 1;
        p->idIcon = IDI_ENH386;
        p->idName = IDS_NAME;
        p->idInfo = IDS_INFO;
        p->lData = 0;
        return 0;
    }
    case CPL_DBLCLK:
        if (l1 == 0)
            dlg_box(hwnd, DLG_ENH386, EnhProc);
        return 0;
    }
    return 0;
}

int __cdecl __far cpl_libmain(HINSTANCE h)
{
    g_hinst = h;
    arrow_register(h);
    return 1;
}

int FAR PASCAL __loadds WEP(int exit)
{
    (void)exit;
    arrow_unregister(g_hinst);
    return 1;
}
