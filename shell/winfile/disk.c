/*
 * disk.c - w16 File Manager: the Disk menu: Copy Disk, Label Disk,
 * Format Disk, Make System Disk, Select Drive.
 *
 * The boxes and messages are the original's as observed (obs WF3, WF13,
 * WF14): the confirmations ("Confirm Copy Disk", "Confirm Format Disk";
 * with Disk Commands confirmation on), "Insert source disk." /
 * "Insert destination disk.", the error boxes ("Copy Disk Error", "Format
 * Disk Error", "System Disk Error", "Label Disk") with their texts; the
 * Label Disk, Format Disk and Select Drive dialogs' layouts.
 *
 * The work: floppies through the BIOS (INT 13h in real mode through
 * DPMI's simulate-interrupt, with a buffer from GlobalDosAlloc): Copy
 * Disk reads every track of the source into memory, asks for the
 * destination and writes them; Format Disk formats each track (or, Quick
 * Format, only clears the FATs and the root directory) and writes a boot
 * sector, the FATs and the root directory of w16's own making for the
 * capacity chosen (its boot code only says the disk is not a system
 * disk); Make System Disk copies the start-up drive's IO.SYS, MSDOS.SYS
 * and COMMAND.COM. Labels go through DOS (extended FCBs, attribute 8).
 * Only the paths without a disk were checked against 3.1 (DOSBox has no
 * floppy here): with a disk the work is UNVERIFIED.
 */
#include "winfile.h"

#define IDD_LABEL       201
#define IDD_DISKIN      222
#define IDD_CAPACITY    241
#define IDD_SYSTEM      242
#define IDD_QUICK       244
#define IDD_DRIVES      222
#define IDD_HELP        254

extern int __cdecl bios_disk(WORD FAR *regs);
extern int __cdecl dos_fcb(int fn, void FAR *xfcb);
extern int __cdecl dos_bootdrive(void);
extern int __cdecl dos_devtype(int drive);

static int disk_drive;                  /* the floppy worked on, 0 = A: */

/* the boxes belong to the dialog that asked (obs WF14: Format Disk stays,
   disabled, under its error and comes back after it) */
static HWND owner(void)
{
    HWND a = GetActiveWindow();
    return a ? a : g_frame;
}

static int box(UINT text, UINT title, UINT flags)
{
    char t[400], c[64];
    LoadString(g_hinst, text, t, sizeof t);
    LoadString(g_hinst, title, c, sizeof c);
    if (s_len(t) < (int)sizeof t - 2) {
        /* "%c" in the text is the drive */
        char u[400];
        wsprintf(u, t, 'A' + disk_drive);
        return MessageBox(owner(), u, c, flags);
    }
    return MessageBox(owner(), t, c, flags);
}

/* the first floppy drive, or the active window's when it is one */
static int floppy(void)
{
    LPDIRWIN d = dirwin_of(active_dirwin());
    int i;
    if (d && GetDriveType(d->drive) == DRIVE_REMOVABLE)
        return d->drive;
    for (i = 0; i < g_ndrives; i++)
        if (GetDriveType(g_drivelist[i]) == DRIVE_REMOVABLE)
            return g_drivelist[i];
    return 0;
}

/* ---- the BIOS ---- */

typedef struct tagGEOM {
    int heads, sectors, cylinders;      /* per track, tracks a side */
    int media;                          /* the BPB's media byte */
    int total, perfat, rootents, spc;   /* sectors, FAT sectors, root entries, a cluster */
} GEOM;

static DWORD dosbuf;                    /* GlobalDosAlloc: segment, selector */

static BOOL buf_get(void)
{
    if (!dosbuf)
        dosbuf = GlobalDosAlloc(18L * 2 * 512);
    return dosbuf != 0;
}

static void buf_free(void)
{
    if (dosbuf)
        GlobalDosFree(LOWORD(dosbuf));
    dosbuf = 0;
}

static LPBYTE buf_ptr(void)
{
    return (LPBYTE)MAKELP(LOWORD(dosbuf), 0);
}

/* INT 13h function ah on cylinder c, head h, sector s, n sectors, in the
   DOS buffer; three tries as the BIOS asks (reset between) */
static int bios(int ah, int c, int h, int s, int n)
{
    WORD r[6];
    int tries, e = -1;
    for (tries = 0; tries < 3; tries++) {
        r[0] = (WORD)((ah << 8) | n);
        r[1] = 0;
        r[2] = (WORD)(((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | s);
        r[3] = (WORD)((h << 8) | disk_drive);
        r[4] = HIWORD(dosbuf);
        e = bios_disk(r);
        if (!e)
            return 0;
        r[0] = 0;
        r[3] = (WORD)disk_drive;
        bios_disk(r);
    }
    return e ? e : -1;
}

static BOOL read_geometry(GEOM *g)
{
    LPBYTE b;
    if (bios(2, 0, 0, 1, 1))
        return FALSE;
    b = buf_ptr();
    g->sectors = *(WORD FAR *)(b + 24);
    g->heads = *(WORD FAR *)(b + 26);
    g->total = *(WORD FAR *)(b + 19);
    g->media = b[21];
    if (g->sectors <= 0 || g->heads <= 0 || g->total <= 0 || g->sectors > 36)
        return FALSE;
    g->cylinders = g->total / (g->sectors * g->heads);
    return TRUE;
}

/* ---- Copy Disk ---- */

void disk_copy(void)
{
    GEOM g;
    HGLOBAL FAR *tracks = NULL;
    HGLOBAL ht = NULL;
    int c, h, n, i, err = 0;
    disk_drive = floppy();
    if (g_confdisk && box(IDS_COPYDISKCONF, IDS_COPYDISKCONFT,
                          MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
        return;
    box(IDS_INSERTSRC, IDS_COPYDISK, MB_OK);
    if (!buf_get() || !read_geometry(&g)) {
        box(IDS_COPYDISKERR, IDS_COPYDISKERRT, MB_OK | MB_ICONSTOP);
        buf_free();
        return;
    }
    n = g.cylinders * g.heads;
    ht = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)n * sizeof(HGLOBAL));
    if (!ht)
        err = 1;
    else
        tracks = (HGLOBAL FAR *)GlobalLock(ht);
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    for (c = 0; !err && c < g.cylinders; c++)
        for (h = 0; !err && h < g.heads; h++) {
            HGLOBAL t = GlobalAlloc(GMEM_MOVEABLE, (DWORD)g.sectors * 512);
            if (!t || bios(2, c, h, 1, g.sectors)) {
                err = 1;
                if (t)
                    GlobalFree(t);
                break;
            }
            hmemcpy(GlobalLock(t), buf_ptr(), (long)g.sectors * 512);
            GlobalUnlock(t);
            tracks[c * g.heads + h] = t;
        }
    if (!err) {
        box(IDS_INSERTDST, IDS_COPYDISK, MB_OK);
        for (c = 0; !err && c < g.cylinders; c++)
            for (h = 0; !err && h < g.heads; h++) {
                HGLOBAL t = tracks[c * g.heads + h];
                hmemcpy(buf_ptr(), GlobalLock(t), (long)g.sectors * 512);
                GlobalUnlock(t);
                if (bios(3, c, h, 1, g.sectors))
                    err = 1;
            }
    }
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    if (tracks) {
        for (i = 0; i < n; i++)
            if (tracks[i])
                GlobalFree(tracks[i]);
        GlobalUnlock(ht);
    }
    if (ht)
        GlobalFree(ht);
    buf_free();
    if (err)
        box(IDS_COPYDISKERR, IDS_COPYDISKERRT, MB_OK | MB_ICONSTOP);
    refresh_all();
}

/* ---- labels ---- */

/* the extended FCB of drive d with attribute 8 and name (11 characters,
   blank-padded; "???????????" to find any) */
static void make_xfcb(LPBYTE f, int d, LPCSTR name)
{
    int i;
    for (i = 0; i < 44; i++)
        f[i] = 0;
    f[0] = 0xFF;
    f[6] = ATTR_VOLUME;
    f[7] = (BYTE)(d + 1);
    for (i = 0; i < 11; i++)
        f[8 + i] = ' ';
    for (i = 0; i < 11 && name[i]; i++)
        f[8 + i] = name[i];
}

static BOOL set_label(int d, LPCSTR label)
{
    BYTE f[44];
    char up[12];
    int i, k;
    for (i = 0, k = 0; label[i] && k < 11; i++) {
        char c = label[i];
        if (c == ' ' || c == '*' || c == '?' || c == '/' || c == '\\' || c == '|' ||
            c == '.' || c == ',' || c == ';' || c == ':' || c == '+' || c == '=' ||
            c == '[' || c == ']' || c == '(' || c == ')' || c == '&' || c == '^' ||
            c == '<' || c == '>' || c == '"')
            return FALSE;
        up[k++] = c;
    }
    up[k] = 0;
    s_upper(up);
    make_xfcb(f, d, "???????????");
    dos_fcb(0x13, f);                   /* the old label, if any, goes */
    if (!up[0])
        return TRUE;
    make_xfcb(f, d, up);
    if (dos_fcb(0x16, f) != 0)          /* create */
        return FALSE;
    dos_fcb(0x10, f);                   /* close */
    return TRUE;
}

static void current_label(int d, LPSTR out, int n)
{
    FINDBUF f;
    char spec[8];
    int i, k = 0;
    out[0] = 0;
    spec[0] = (char)('A' + d);
    s_cpy(spec + 1, ":\\*.*", sizeof spec - 1);
    if (dos_findfirst(spec, ATTR_VOLUME, &f) == 0 && (f.attr & ATTR_VOLUME)) {
        for (i = 0; f.name[i] && k < n - 1; i++)
            if (f.name[i] != '.')
                out[k++] = f.name[i];
        out[k] = 0;
    }
}

BOOL CALLBACK _export LabelDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[16];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        current_label(disk_drive, t, sizeof t);
        SendDlgItemMessage(h, IDD_LABEL, EM_LIMITTEXT, 11, 0);
        SetDlgItemText(h, IDD_LABEL, t);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            GetDlgItemText(h, IDD_LABEL, t, sizeof t);
            EnableWindow(h, FALSE);
            if (!set_label(disk_drive, t)) {
                box(IDS_LABELERR, IDS_LABELDISK, MB_OK | MB_ICONSTOP);
                EnableWindow(h, TRUE);
                return TRUE;
            }
            EnableWindow(h, TRUE);
            EndDialog(h, 1);
            refresh_all();
            {
                HWND w;
                for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
                    LPDIRWIN e = dirwin_of(w);
                    if (e)
                        InvalidateRect(e->drives, NULL, TRUE);
                }
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + IDM_LABEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void disk_label(void)
{
    LPDIRWIN d = dirwin_of(active_dirwin());
    disk_drive = d ? d->drive : 2;
    run_dialog("DLG_LABEL", (FARPROC)LabelDlgProc);
}

/* ---- Format Disk ---- */

/* the capacities: total sectors, sectors a track, heads, media byte,
   sectors a FAT, root entries, sectors a cluster */
typedef struct tagCAP {
    UINT name;
    int total, spt, heads, media, perfat, root, spc;
} CAP;

static const CAP caps[] = {
    { IDS_CAP1440, 2880, 18, 2, 0xF0, 9, 224, 1 },
    { IDS_CAP720,  1440,  9, 2, 0xF9, 3, 112, 2 },
    { IDS_CAP1200, 2400, 15, 2, 0xF9, 7, 224, 1 },
    { IDS_CAP360,   720,  9, 2, 0xFD, 2, 112, 2 },
};
#define NCAPS (sizeof caps / sizeof caps[0])

static int fmt_cap;
static BOOL fmt_quick, fmt_system;
static char fmt_label[16];

/* w16's boot sector: a BPB and a short program that says the disk does
   not start a system (our own bytes: print the message through the
   BIOS, wait for a key, INT 19h) */
static void boot_sector(LPBYTE b, const CAP FAR *c)
{
    static const char msg[] = "\r\nThis disk does not start a system (w16 format).\r\n"
                              "Press a key to try again.\r\n";
    int i;
    for (i = 0; i < 512; i++)
        b[i] = 0;
    b[0] = 0xEB;                        /* jmp short 3Eh */
    b[1] = 0x3C;
    b[2] = 0x90;
    for (i = 0; i < 8; i++)
        b[3 + i] = "W16FMT  "[i];
    *(WORD FAR *)(b + 11) = 512;
    b[13] = (BYTE)c->spc;
    *(WORD FAR *)(b + 14) = 1;          /* reserved sectors */
    b[16] = 2;                          /* FATs */
    *(WORD FAR *)(b + 17) = (WORD)c->root;
    *(WORD FAR *)(b + 19) = (WORD)c->total;
    b[21] = (BYTE)c->media;
    *(WORD FAR *)(b + 22) = (WORD)c->perfat;
    *(WORD FAR *)(b + 24) = (WORD)c->spt;
    *(WORD FAR *)(b + 26) = (WORD)c->heads;
    b[38] = 0x29;                       /* extended BPB */
    *(DWORD FAR *)(b + 39) = GetTickCount();
    for (i = 0; i < 11; i++)
        b[43 + i] = (BYTE)(fmt_label[i] && i < s_len(fmt_label) ? fmt_label[i] : ' ');
    for (i = 0; i < 8; i++)
        b[54 + i] = "FAT12   "[i];
    /* 3Eh: cli; xor ax,ax; mov ds,ax; mov ss,ax; mov sp,7C00h; sti;
       mov si,7C00h+msg; cld; next: lodsb; or al,al; jz done;
       mov ah,0Eh; mov bx,7; int 10h; jmp next; done: xor ah,ah;
       int 16h; int 19h */
    {
        static const BYTE code[] = {
            0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB,
            0xBE, 0x00, 0x00, 0xFC, 0xAC, 0x08, 0xC0, 0x74, 0x09, 0xB4, 0x0E,
            0xBB, 0x07, 0x00, 0xCD, 0x10, 0xEB, 0xF2, 0x30, 0xE4, 0xCD, 0x16,
            0xCD, 0x19
        };
        int at = 0x3E, m = at + sizeof code;
        for (i = 0; i < (int)sizeof code; i++)
            b[at + i] = code[i];
        *(WORD FAR *)(b + at + 12) = (WORD)(0x7C00 + m);
        for (i = 0; msg[i] && m + i < 510; i++)
            b[m + i] = msg[i];
    }
    b[510] = 0x55;
    b[511] = 0xAA;
}

/* write sectors from lba on (the DOS buffer holds them) */
static int write_lba(const CAP FAR *c, int lba, int n)
{
    LPBYTE p = buf_ptr();
    int k, e;
    for (k = 0; k < n; k++) {
        int s = lba + k, cyl = s / (c->spt * c->heads), rest = s % (c->spt * c->heads);
        if (k)
            hmemcpy(p, p + 512L * k, 512);
        e = bios(3, cyl, rest / c->spt, rest % c->spt + 1, 1);
        if (e)
            return e;
    }
    return 0;
}

static BOOL do_format(void)
{
    const CAP FAR *c = &caps[fmt_cap];
    LPBYTE b;
    int cyl, h, s, i, sec;
    if (!buf_get())
        return FALSE;
    b = buf_ptr();
    if (!fmt_quick) {
        for (cyl = 0; cyl < c->total / (c->spt * c->heads); cyl++)
            for (h = 0; h < c->heads; h++) {
                /* the address fields: C, H, R, N (512 bytes) for each sector */
                for (s = 0; s < c->spt; s++) {
                    b[s * 4] = (BYTE)cyl;
                    b[s * 4 + 1] = (BYTE)h;
                    b[s * 4 + 2] = (BYTE)(s + 1);
                    b[s * 4 + 3] = 2;
                }
                if (bios(5, cyl, h, 1, c->spt))
                    return FALSE;
            }
    } else {
        GEOM g;
        if (!read_geometry(&g))
            return FALSE;
    }
    /* boot sector */
    boot_sector(b, c);
    if (write_lba(c, 0, 1))
        return FALSE;
    /* two FATs: media byte, FF FF, the rest free */
    sec = 1;
    for (i = 0; i < 2; i++) {
        int k;
        for (k = 0; k < c->perfat; k++) {
            int j;
            for (j = 0; j < 512; j++)
                b[j] = 0;
            if (k == 0) {
                b[0] = (BYTE)c->media;
                b[1] = 0xFF;
                b[2] = 0xFF;
            }
            if (write_lba(c, sec++, 1))
                return FALSE;
        }
    }
    /* the root directory, empty but the label */
    for (i = 0; i < c->root * 32 / 512; i++) {
        int j;
        for (j = 0; j < 512; j++)
            b[j] = 0;
        if (i == 0 && fmt_label[0]) {
            for (j = 0; j < 11; j++)
                b[j] = (BYTE)(j < s_len(fmt_label) ? fmt_label[j] : ' ');
            b[11] = ATTR_VOLUME;
        }
        if (write_lba(c, sec++, 1))
            return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK _export FormatDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[40];
    unsigned i;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 0; i < (unsigned)g_ndrives; i++)
            if (GetDriveType(g_drivelist[i]) == DRIVE_REMOVABLE) {
                wsprintf(t, load_str(IDS_DRIVEIN), 'A' + g_drivelist[i]);
                SendDlgItemMessage(h, IDD_DISKIN, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
            }
        SendDlgItemMessage(h, IDD_DISKIN, CB_SETCURSEL, 0, 0);
        {
            /* the capacities the drive takes, from DOS's device
               parameters; "Drive Capacity" when DOS cannot say (obs
               WF13: DOSBox's A:) */
            int t2 = dos_devtype(disk_drive + 1), k;
            static const int fits[4][3] = {
                { 3, -1, -1 },          /* 360 KB drive */
                { 2, 3, -1 },           /* 1.2 MB */
                { 1, -1, -1 },          /* 720 KB */
                { 0, 1, -1 },           /* 1.44 MB */
            };
            int row = t2 == 0 ? 0 : t2 == 1 ? 1 : t2 == 2 ? 2 : (t2 == 7 || t2 == 9) ? 3 : -1;
            if (row < 0) {
                i = (unsigned)SendDlgItemMessage(h, IDD_CAPACITY, CB_ADDSTRING, 0,
                                                 (LPARAM)(LPSTR)load_str(IDS_CAPUNKNOWN));
                SendDlgItemMessage(h, IDD_CAPACITY, CB_SETITEMDATA, i, 0);
            } else {
                for (k = 0; k < 3 && fits[row][k] >= 0; k++) {
                    i = (unsigned)SendDlgItemMessage(h, IDD_CAPACITY, CB_ADDSTRING, 0,
                                                     (LPARAM)(LPSTR)load_str(caps[fits[row][k]].name));
                    SendDlgItemMessage(h, IDD_CAPACITY, CB_SETITEMDATA, i, fits[row][k]);
                }
            }
        }
        SendDlgItemMessage(h, IDD_CAPACITY, CB_SETCURSEL, 0, 0);
        SendDlgItemMessage(h, IDD_LABEL, EM_LIMITTEXT, 11, 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            int sel = (int)SendDlgItemMessage(h, IDD_DISKIN, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                SendDlgItemMessage(h, IDD_DISKIN, CB_GETLBTEXT, sel, (LPARAM)(LPSTR)t);
                for (i = 0; t[i] && t[i] != ':'; i++)
                    ;
                if (i > 0)
                    disk_drive = (t[i - 1] | 0x20) - 'a';
            }
            fmt_cap = (int)SendDlgItemMessage(h, IDD_CAPACITY, CB_GETITEMDATA,
                (int)SendDlgItemMessage(h, IDD_CAPACITY, CB_GETCURSEL, 0, 0), 0);
            if (fmt_cap < 0 || fmt_cap >= (int)NCAPS)
                fmt_cap = 0;
            GetDlgItemText(h, IDD_LABEL, fmt_label, sizeof fmt_label);
            s_upper(fmt_label);
            fmt_quick = IsDlgButtonChecked(h, IDD_QUICK);
            fmt_system = IsDlgButtonChecked(h, IDD_SYSTEM);
            EnableWindow(h, FALSE);
            if (!g_confdisk || box(IDS_FORMATCONF, IDS_FORMATCONFT,
                                   MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
                SetCursor(LoadCursor(NULL, IDC_WAIT));
                if (!do_format())
                    box(IDS_FORMATERR, IDS_FORMATERRT, MB_OK | MB_ICONSTOP);
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                buf_free();
                refresh_all();
            }
            EnableWindow(h, TRUE);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + IDM_FORMAT);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void disk_format(void)
{
    disk_drive = floppy();
    run_dialog("DLG_FORMAT", (FARPROC)FormatDlgProc);
}

/* ---- Make System Disk ---- */

static BOOL copy_sys(int boot, LPCSTR name, int attr)
{
    char from[16], to[16];
    int a, b, n;
    HGLOBAL hb;
    LPBYTE buf;
    from[0] = (char)('A' + boot - 1);
    s_cpy(from + 1, ":\\", sizeof from - 1);
    s_cat(from, name, sizeof from);
    to[0] = (char)('A' + disk_drive);
    s_cpy(to + 1, ":\\", sizeof to - 1);
    s_cat(to, name, sizeof to);
    a = dos_open(from, 0x20);
    if (a < 0)
        return FALSE;
    b = dos_create(to, 0);
    if (b < 0) {
        dos_close(a);
        return FALSE;
    }
    hb = GlobalAlloc(GMEM_MOVEABLE, 16384);
    buf = (LPBYTE)GlobalLock(hb);
    while ((n = dos_read(a, buf, 16384)) > 0)
        if (dos_write(b, buf, n) != n)
            break;
    dos_setftime(b, dos_getftime(a));
    dos_close(a);
    dos_close(b);
    GlobalUnlock(hb);
    GlobalFree(hb);
    dos_setattr(to, attr);
    return TRUE;
}

BOOL CALLBACK _export SysProgDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    (void)h;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && wp == IDCANCEL)
        return TRUE;
    return FALSE;
}

void disk_sysdisk(void)
{
    int boot;
    char t[16];
    FINDBUF f;
    HWND prog;
    FARPROC p;
    BOOL ok;
    disk_drive = floppy();
    if (box(IDS_SYSCONF, IDS_SYSDISK, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
        return;
    /* "Now copying system files" while it works (obs WF14) */
    p = MakeProcInstance((FARPROC)SysProgDlgProc, g_hinst);
    prog = CreateDialog(g_hinst, "DLG_SYSPROG", g_frame, (DLGPROC)p);
    if (prog) {
        EnableWindow(g_frame, FALSE);
        UpdateWindow(prog);
    }
    boot = dos_bootdrive();
    t[0] = (char)('A' + boot - 1);
    s_cpy(t + 1, ":\\IO.SYS", sizeof t - 1);
    ok = dos_findfirst(t, ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE, &f) == 0;
    if (ok) {
        ok = copy_sys(boot, "IO.SYS", ATTR_HS | ATTR_READONLY) &&
             copy_sys(boot, "MSDOS.SYS", ATTR_HS | ATTR_READONLY) &&
             copy_sys(boot, "COMMAND.COM", 0);
        if (!ok) {
            EnableWindow(g_frame, TRUE);
            if (prog)
                DestroyWindow(prog);
            box(IDS_SYSWRITEERR, IDS_SYSERRT, MB_OK | MB_ICONSTOP);
            FreeProcInstance(p);
            return;
        }
    } else {
        box(IDS_SYSERR, IDS_SYSERRT, MB_OK | MB_ICONSTOP);
    }
    EnableWindow(g_frame, TRUE);
    if (prog)
        DestroyWindow(prog);
    FreeProcInstance(p);
    refresh_all();
}

/* ---- Select Drive (obs WF14: "A: ", "C: [C_DRIVE]", sorted) ---- */

BOOL CALLBACK _export DrivesDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = dirwin_of(active_dirwin());
    char t[40], lab[16];
    int i;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 0; i < g_ndrives; i++) {
            lab[0] = 0;
            if (GetDriveType(g_drivelist[i]) != DRIVE_REMOVABLE)
                current_label(g_drivelist[i], lab, sizeof lab);
            if (lab[0])
                wsprintf(t, "%c: [%s]", 'A' + g_drivelist[i], (LPSTR)lab);
            else
                wsprintf(t, "%c: ", 'A' + g_drivelist[i]);
            SendDlgItemMessage(h, IDD_DRIVES, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
            if (d && g_drivelist[i] == d->drive)
                SendDlgItemMessage(h, IDD_DRIVES, LB_SETCURSEL, i, 0);
        }
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || (wp == IDD_DRIVES && HIWORD(lp) == LBN_DBLCLK)) {
            i = (int)SendDlgItemMessage(h, IDD_DRIVES, LB_GETCURSEL, 0, 0);
            EndDialog(h, 1);
            if (i >= 0 && d) {
                SendDlgItemMessage(h, IDD_DRIVES, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
                treewin_set_drive(d, (t[0] | 0x20) - 'a');
            }
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        if (wp == IDD_HELP) {
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + IDM_DRIVESMORE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void disk_select(void)
{
    run_dialog("DLG_DRIVES", (FARPROC)DrivesDlgProc);
}
