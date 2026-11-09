/*
 * winoldap.c - w16's WINOLDAP.MOD: running DOS programs from Standard-mode
 * Windows (ARCHITECTURE.md 9.7). MIT licence (LICENSE).
 *
 * Everything here was observed black-box on Windows 3.1 (tests/worun.sh,
 * the WO probes; Microsoft's WINOLDAP.MOD, WSWAP.EXE and DSWAP.EXE were
 * never disassembled): KERNEL starts this module as a task for a DOS
 * program, with the command line "args NUL path LF" (WO1). It reads the
 * program's PIF (the published PIF format; the fields Windows 3.1's reads,
 * in its order: WO2), describes the program in WIN.COM's switcher block
 * (INT 2Fh 4A05h SI=5, ARCHITECTURE.md 9.5), disables Windows' devices
 * (DisableOEMLayer) and asks DOSX for the switch (INT 2Fh 4B80h + a DOS
 * call): WSWAP swaps Windows out and WIN.COM runs DSWAP, which runs the
 * program. When the user switches back (Alt+Tab, Ctrl+Esc, Alt+Esc) or the
 * program ends, WSWAP swaps Windows in and the DOS call returns. While the
 * program is swapped out this task keeps an icon; opening it switches back.
 */
#include <windows.h>
#include "winoldap.h"

/* KERNEL / USER exports not in OpenWatcom's windows.h */
extern void FAR PASCAL DisableOEMLayer(void);   /* USER.4 */
extern void FAR PASCAL EnableOEMLayer(void);    /* USER.3 */
extern void FAR PASCAL WinOldApCall(WORD mode);   /* KERNEL.151 (Schulman ch. 5) */

/* woasm.asm */
extern DWORD __cdecl woa_block(void);
extern WORD __cdecl woa_segsel(WORD seg);
extern DWORD __cdecl woa_selbase(WORD sel);
extern void __cdecl woa_switch(void);
extern WORD __cdecl woa_memtop(void);
extern DWORD __cdecl woa_diskfree(int drive);
/* shell/start.asm */
extern int __cdecl dos_chdir(const char FAR *path);
extern void __cdecl dos_setdrive(int drive);

/* ---- WIN.COM's switcher block (offsets observed, ARCHITECTURE.md 9.7) ---- */
#define B_TAIL      0x005       /* the program's command tail: length, text, CR */
#define B_GRABBER   0x087       /* the 286 grabber's path */
#define B_SWAPDIR   0x0D7       /* swap directories (WIN.COM) */
#define B_SWAPDIR2  0x11B
#define B_172       0x172       /* WSWAP: 08h when the switch failed */
#define B_173       0x173       /* FFFFh as a switch starts */
#define B_177       0x177
#define B_178       0x178       /* bit 08h: Ctrl+Esc (run the Task List) */
#define B_17A       0x17A       /* 0Fh 10h tasks 01h */
#define B_COUNT     0x17C       /* tasks in the list (not counting Windows') */
#define B_17E       0x17E       /* 0Fh as a switch starts */
#define B_LINEAR    0x185       /* linear address of a buffer for Windows' low memory */
#define B_PROGRAM   0x189       /* the program's path (else DOSX's) */
#define B_RECORDS   0x1D9       /* task records: name at +0 */
#define REC_SIZE    0x32
#define NREC        16          /* records 0-15; 15 is Windows itself */
#define R_WINDOWS   15
#define R_1F        0x1F        /* 80h for a DOS program's record */
#define R_21        0x21        /* FFFFFFFFh for a Windows application's */
#define R_FLAGS     0x28        /* 0Ch new DOS program, 04h swapped out, 06h Windows application, 46h Windows */
#define R_KIND      0x29        /* 80h free, 00h DOS program, 28h Windows application, 68h Windows */
#define R_NEXT      0x2A        /* the next record in switching order */
#define R_SESSION   0x2B        /* the swap file (session) it lives in */
#define R_2C        0x2C
#define R_2D        0x2D
#define R_HWND      0x30
#define NAME_MAX    0x1E

static BYTE FAR *blk;           /* through a selector for its segment */
static char dosx_path[80];      /* what B_PROGRAM held before we wrote it */

static HINSTANCE hinst;
static HWND hwnd;
static HWND first_active;      /* the active window as we started */
static int rec = -1;            /* our record */
static char title[NAME_MAX + 2];
static char program[128];       /* full path */
static char tail[130];          /* command tail text (DOS allows 127) */
static char grabber[128];
static BOOL started, ended;
static DWORD linear;
static HGLOBAL hbuf;

/* ---- strings ---- */
static char sbuf[300];

static int message(int id, UINT flags)
{
    LoadString(hinst, id, sbuf, sizeof sbuf);
    return MessageBox(hwnd, sbuf, title[0] ? title : "WINOLDAP", flags);
}

/* string 9: "X" becomes the drive letter, the K figure goes after
   "Desired disk space:" (as Windows 3.1 shows them) */
static void disk_message(char drive, WORD kb)
{
    char *p, num[12];
    int i;
    LoadString(hinst, IDS_NODISK, sbuf, sizeof sbuf);
    for (p = sbuf; *p && *p != 'X'; p++)
        ;
    if (*p) {               /* the letter stands 16 blanks into the field */
        *p = ' ';
        for (i = 1; i < 16 && p[i] == ' '; i++)
            ;
        if (i == 16)
            p[16] = drive;
    }
    for (p = sbuf; *p && *p != ':'; p++)
        ;
    if (*p) {
        wsprintf(num, " %u K", kb);
        for (i = 0; num[i] && p[1 + i]; i++)
            p[1 + i] = num[i];
    }
    MessageBox(hwnd, sbuf, title, MB_OK | MB_ICONEXCLAMATION);
}

/* ---- the PIF (published format; the fields Windows 3.1 reads) ---- */
static struct {
    WORD maxmem, minmem;        /* 20h, 22h: KB */
    char program[64];           /* 24h */
    BYTE flags1;                /* 63h */
    char directory[64];         /* 65h */
    char params[64];            /* A5h */
    WORD w_ed;                  /* EDh */
    WORD flags2;                /* 16Fh */
    char title[31];             /* 02h, 30 bytes */
    BYTE ext[22], ext286[22];   /* 171h: "MICROSOFT PIFEX", 187h: "WINDOWS 286 3.0" */
    WORD xmsmax, xmsmin;        /* 19Dh */
    WORD flags286;              /* 1A1h */
} pif;

static void readat(HFILE f, LONG off, void FAR *buf, int n)
{
    _llseek(f, off, 0);
    _lread(f, buf, n);
}

static BOOL load_pif(const char *path)
{
    OFSTRUCT of;
    HFILE f = OpenFile(path, &of, OF_READ);
    if (f == HFILE_ERROR)
        return FALSE;
    /* the order Windows 3.1's WINOLDAP reads them in (WO2) */
    _llseek(f, 0, 2);
    readat(f, 0x171, pif.ext, 0x16);
    readat(f, 0x187, pif.ext286, 0x16);
    readat(f, 0x19D, &pif.xmsmax, 4);
    readat(f, 0x1A1, &pif.flags286, 2);
    readat(f, 0x63, &pif.flags1, 1);
    readat(f, 0x20, &pif.maxmem, 4);
    readat(f, 0x24, pif.program, 0x3F);
    readat(f, 0x65, pif.directory, 0x40);
    readat(f, 0xA5, pif.params, 0x40);
    readat(f, 0xED, &pif.w_ed, 2);
    readat(f, 0x16F, &pif.flags2, 2);
    readat(f, 0x02, pif.title, 0x1E);
    _lclose(f);
    pif.program[63] = pif.directory[63] = pif.params[63] = 0;
    pif.title[30] = 0;
    return TRUE;
}

static int has_ext(const char *p, const char *ext)
{
    int n = lstrlen(p), m = lstrlen(ext);
    return n >= m && !lstrcmpi(p + n - m, ext);
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == ':' || *p == '/')
            b = p + 1;
    return b;
}

/* "C:\DIR\NAME.EXT" -> "NAME" (as given) */
static void stem(const char *p, char *out, int max)
{
    const char *b = base_name(p);
    int n = 0;
    while (b[n] && b[n] != '.' && n < max - 1) {
        out[n] = b[n];
        n++;
    }
    out[n] = 0;
}

static void trim(char *s)
{
    int n = lstrlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static BOOL exists(const char *name, char *full)
{
    OFSTRUCT of;
    if (OpenFile(name, &of, OF_READ | OF_EXIST) == HFILE_ERROR)
        return FALSE;
    if (full)
        lstrcpy(full, of.szPathName);
    return TRUE;
}

/* a variable from our DOS environment */
static BOOL get_env(const char *name, char *out, int max)
{
    LPSTR e = GetDOSEnvironment();
    int n = lstrlen(name);
    while (*e) {
        int i;
        for (i = 0; i < n && e[i] && (e[i] | 0x20) == (name[i] | 0x20); i++)
            ;
        if (i == n && e[n] == '=') {
            lstrcpyn(out, e + n + 1, max);
            return TRUE;
        }
        e += lstrlen(e) + 1;
    }
    return FALSE;
}

/* ---- the block ---- */
static BYTE FAR *recp(int r)
{
    return blk + B_RECORDS + r * REC_SIZE;
}

static int free_record(void)
{
    int r;
    for (r = R_WINDOWS - 1; r >= 0; r--)
        if (recp(r)[R_KIND] == 0x80)
            return r;
    return -1;
}

static void set_name(BYTE FAR *r, const char FAR *s)
{
    int i;
    for (i = 0; i < NAME_MAX && s[i]; i++)
        r[i] = s[i];
    r[i] = 0;
}

/* move record r to the front of the switching order (after Windows') */
static void to_front(int r)
{
    BYTE FAR *w = recp(R_WINDOWS);
    int p = R_WINDOWS, guard;
    if (w[R_NEXT] == r)
        return;
    for (guard = 0; guard < NREC; guard++) {    /* unlink */
        BYTE FAR *q = recp(p);
        if (q[R_NEXT] == r) {
            q[R_NEXT] = recp(r)[R_NEXT];
            break;
        }
        p = q[R_NEXT];
        if (p >= NREC || p == R_WINDOWS)
            break;
    }
    recp(r)[R_NEXT] = w[R_NEXT];
    w[R_NEXT] = (BYTE)r;
}

/* the Windows application the user leaves: one record, made when the
   first DOS program starts (the active window's title and handle) and not
   changed afterwards (WO2: a resume or a second program leaves it) */
static void windows_record(void)
{
    HWND a, o;
    int r;
    char t[NAME_MAX + 1];
    for (r = 0; r < R_WINDOWS; r++)
        if (recp(r)[R_KIND] == 0x28)
            return;
    /* the window that was active when this task started (with a PIF's
       Parameters dialog in between, Windows 3.1's still names Program
       Manager: WO2) */
    a = (first_active && IsWindow(first_active)) ? first_active : GetActiveWindow();
    while (a && (o = GetParent(a)) != NULL)
        a = o;
    if (a == hwnd || (a && (!IsWindowVisible(a) || !GetWindowTextLength(a))))
        a = NULL;
    /* else the first visible, titled top-level window in Z order (File >
       Run: Windows 3.1's names Program Manager, not the Run dialog) */
    for (o = GetWindow(GetDesktopWindow(), GW_CHILD); !a && o; o = GetWindow(o, GW_HWNDNEXT))
        if (o != hwnd && IsWindowVisible(o) && GetWindowTextLength(o) &&
            !(GetWindowLong(o, GWL_STYLE) & WS_CHILD))
            a = o;
    /* Windows 3.1's WINOLDAP looks for another window here and never
       finds one when the shell has no window (it stops, ARCHITECTURE.md
       9.7); WSWAP needs a Windows application in its list to come back
       to, so w16 enters the desktop then */
    if (!a)
        a = GetDesktopWindow();
    {
        r = free_record();
        if (r < 0)
            return;
        recp(r)[R_21] = recp(r)[R_21 + 1] = recp(r)[R_21 + 2] = recp(r)[R_21 + 3] = 0xFF;
        recp(r)[R_FLAGS] = 0x06;
        recp(r)[R_KIND] = 0x28;
        recp(r)[R_NEXT] = recp(R_WINDOWS)[R_NEXT];
        recp(r)[R_SESSION] = R_WINDOWS;
        recp(r)[R_2C] = 0x10;
        recp(r)[R_2D] = 0;
        recp(R_WINDOWS)[R_NEXT] = (BYTE)r;
        blk[B_COUNT]++;
    }
    t[0] = 0;
    GetWindowText(a, t, sizeof t);
    set_name(recp(r), t);
    *(WORD FAR *)(recp(r) + R_HWND) = (WORD)a;
}

static void write_tail(void)
{
    int n = lstrlen(tail), i;
    blk[B_TAIL] = (BYTE)n;
    for (i = 0; i < n; i++)
        blk[B_TAIL + 1 + i] = tail[i];
    blk[B_TAIL + 1 + n] = '\r';
}

static void put_string(int off, const char *s, int max)
{
    int i;
    for (i = 0; i < max - 1 && s[i]; i++)
        blk[off + i] = s[i];
    blk[off + i] = 0;
}

/* debugging aid: when C:\WOA.DBG exists, each switch appends the block
   (540h bytes, binary) to it, for tools/woblk.py-style comparisons with
   WO2's snapshots when Windows does not come back to flush them */
static void debug_block(void)
{
    OFSTRUCT of;
    HFILE f = OpenFile("C:\\WOA.DBG", &of, OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, (LPCSTR)blk, 0x540);
    _lclose(f);
}

/* ---- the switch ---- */
static void do_switch(void)
{
    blk[B_172] = 0;                 /* (00h as each switch starts: WO2) */
    blk[B_173] = blk[B_173 + 1] = 0xFF;
    blk[B_178] &= ~0x08;
    blk[B_17E] = R_WINDOWS;
    if (!started) {
        write_tail();
        put_string(B_GRABBER, grabber, sizeof grabber);
        blk[B_17A] = R_WINDOWS;
        blk[B_17A + 1] = 0x10;
        blk[B_17A + 3] = 0x01;
        put_string(B_PROGRAM, program, 80);
    } else
        put_string(B_PROGRAM, dosx_path, 80);
    windows_record();
    if (!started) {
        BYTE FAR *r;
        rec = free_record();
        if (rec < 0) {
            message(IDS_TOOMANY, MB_OK | MB_ICONEXCLAMATION);
            ended = TRUE;
            DestroyWindow(hwnd);
            return;
        }
        r = recp(rec);
        set_name(r, title);
        *(WORD FAR *)(r + R_HWND) = (WORD)hwnd;
        r[R_1F] = 0x80;
        /* 08h: DSWAP shows its MS-DOS Prompt banner (set for
           COMMAND.COM, not for other programs: WO2) */
        r[R_FLAGS] = lstrcmpi(base_name(program), "COMMAND.COM") ? 0x04 : 0x0C;
        r[R_KIND] = 0x00;
        r[R_NEXT] = recp(R_WINDOWS)[R_NEXT];
        recp(R_WINDOWS)[R_NEXT] = (BYTE)rec;
        blk[B_COUNT]++;
    } else
        to_front(rec);
    /* the buffer WSWAP keeps Windows' low memory in while the program
       runs: allocated for each switch and freed after it, as Windows 3.1's
       (its INT 31h 0006h comes before every INT 2Fh 4B80h, and the address
       at 185h changes between switches). 8A000h bytes were observed with
       640 KB of conventional memory, i.e. all of it above 1600h:0;
       UNVERIFIED how Windows 3.1 sizes it */
    hbuf = GlobalAlloc(GMEM_FIXED | GMEM_SHARE, (DWORD)(woa_memtop() - 0x1600) << 4);
    if (!hbuf) {
        message(IDS_NOMEM, MB_OK | MB_ICONEXCLAMATION);
        if (!started) {
            ended = TRUE;
            DestroyWindow(hwnd);
        }
        return;
    }
    linear = woa_selbase(SELECTOROF(GlobalLock(hbuf)));
    *(DWORD FAR *)(blk + B_LINEAR) = linear;
    started = TRUE;
    debug_block();
    DisableOEMLayer();
    woa_switch();
    EnableOEMLayer();
    GlobalUnlock(hbuf);
    GlobalFree(hbuf);
    hbuf = NULL;
    if (blk[B_172] && recp(rec)[R_KIND] != 0x80) {
        /* the switch failed (172h = 08h with no room for the swap file,
           after the disk-space warning): Windows 3.1's WINOLDAP ends then
           and its icon goes. UNVERIFIED: what it does with the record;
           w16 gives it back */
        int p = R_WINDOWS, guard;
        for (guard = 0; guard < NREC; guard++) {
            BYTE FAR *q = recp(p);
            if (q[R_NEXT] == rec) {
                q[R_NEXT] = recp(rec)[R_NEXT];
                break;
            }
            p = q[R_NEXT];
            if (p >= NREC)
                break;
        }
        recp(rec)[R_FLAGS] = 0;
        recp(rec)[R_KIND] = 0x80;
        recp(rec)[R_SESSION] = recp(rec)[R_2C] = recp(rec)[R_2D] = 0;
        if (blk[B_COUNT])
            blk[B_COUNT]--;
    }
    if (recp(rec)[R_KIND] == 0x80) {            /* the program has ended */
        ended = TRUE;
        *(WORD FAR *)(recp(rec) + R_HWND) = 0xFFFF;
        DestroyWindow(hwnd);
        return;
    }
    /* swapped out: WSWAP left at 17Eh the record to come back to; for a
       Windows application, activate its window with our icon next in Z
       order, so that USER's Alt+Tab from there comes back to this program
       (as with Windows 3.1: with two prompts swapped out, Alt+Tab returned
       to the one left last) */
    {
        int t = blk[B_17E];
        BringWindowToTop(hwnd);
        if (t < R_WINDOWS && recp(t)[R_KIND] == 0x28) {
            HWND w = (HWND)*(WORD FAR *)(recp(t) + R_HWND);
            if (IsWindow(w))
                SetActiveWindow(w);
        }
    }
    if (blk[B_178] & 0x08)                      /* Ctrl+Esc */
        WinExec("TASKMAN.EXE", SW_SHOWNORMAL);
}

/* ---- the window ---- */
LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_QUERYOPEN:
        PostMessage(h, WM_USER, 0, 0);
        return FALSE;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_RESTORE || (wp & 0xFFF0) == SC_MAXIMIZE) {
            PostMessage(h, WM_USER, 0, 0);
            return 0;
        }
        if ((wp & 0xFFF0) == SC_CLOSE) {
            message(IDS_STILLACTIVE, MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        break;
    case WM_QUERYENDSESSION:
        message(IDS_QUITFIRST, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    case WM_CLOSE:
        if (!ended) {
            message(IDS_STILLACTIVE, MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        break;
    case WM_TIMER:
        KillTimer(h, 1);
        /* fall through */
    case WM_USER:
        if (!ended)
            do_switch();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static char param_buf[64];

BOOL FAR PASCAL DialogProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowText(h, title);
        SendDlgItemMessage(h, IDC_PARAMS, EM_LIMITTEXT, sizeof param_buf - 1, 0);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            GetDlgItemText(h, IDC_PARAMS, param_buf, sizeof param_buf);
            EndDialog(h, wp == IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- start-up ---- */
static BOOL setup(LPSTR cmd)
{
    char path[128], args[130], name[80];
    char pifpath[128];
    BOOL from_pif;
    DWORD b;
    int i, n;

    /* "args NUL path LF" (WO1) */
    for (n = 0; cmd[n] && n < (int)sizeof args - 1; n++)
        args[n] = cmd[n];
    args[n] = 0;
    cmd += n + 1;
    for (i = 0; cmd[i] && cmd[i] != '\n' && cmd[i] != '\r' && i < (int)sizeof path - 1; i++)
        path[i] = cmd[i];
    path[i] = 0;

    b = woa_block();
    if (!b) {
        stem(path, title, sizeof title);
        message(IDS_NOSWITCHER, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    blk = (BYTE FAR *)MAKELP(woa_segsel(HIWORD(b)), LOWORD(b));
    for (i = 0; i < 79 && blk[B_PROGRAM + i]; i++)
        dosx_path[i] = blk[B_PROGRAM + i];
    dosx_path[i] = 0;

    /* the PIF: the one given, else NAME.PIF beside the program, NAME.PIF
       on the search path, _DEFAULT.PIF (the order Windows 3.1 opens them) */
    if (has_ext(path, ".PIF"))
        lstrcpy(pifpath, path);
    else {
        lstrcpy(pifpath, path);
        n = lstrlen(pifpath);
        while (n > 0 && pifpath[n - 1] != '.' && pifpath[n - 1] != '\\')
            n--;
        if (n > 0 && pifpath[n - 1] == '.')
            pifpath[n - 1] = 0;
        lstrcat(pifpath, ".PIF");
        if (!exists(pifpath, NULL)) {
            stem(path, name, sizeof name);
            lstrcat(name, ".PIF");
            if (!exists(name, pifpath))
                lstrcpy(pifpath, "_DEFAULT.PIF");
        }
    }
    load_pif(pifpath);          /* (none: the fields stay empty) */

    lstrcpy(title, pif.title);
    trim(title);

    if (has_ext(path, ".PIF")) {
        /* the program the PIF names; COMMAND.COM is COMSPEC's */
        trim(pif.program);
        if (!lstrcmpi(pif.program, "COMMAND.COM") && get_env("COMSPEC", program, sizeof program))
            ;
        else if (!exists(pif.program, program)) {
            if (!title[0])
                stem(pif.program, title, sizeof title);
            message(IDS_NOFILE, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        lstrcpy(tail, args[0] ? args : pif.params);
    } else {
        if (!exists(path, program)) {
            stem(path, title, sizeof title);
            message(IDS_NOFILE, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        lstrcpy(tail, args[0] ? args : pif.params);
    }
    from_pif = !args[0];
    if (!title[0]) {
        stem(has_ext(path, ".PIF") ? pif.program : path, title, sizeof title);
        AnsiUpper(title);
    }
    trim(tail);
    if (tail[0] == '?' && tail[1] == 0) {
        tail[0] = 0;
        if (DialogBox(hinst, MAKEINTRESOURCE(IDD_PARAMS), NULL, (DLGPROC)MakeProcInstance((FARPROC)DialogProc, hinst)) <= 0)
            return FALSE;
        lstrcpy(tail, param_buf);
    }
    /* parameters from the PIF (or its "?" dialog) get a leading blank, as
       DOS command tails have; arguments given to WinExec are passed as
       they are (WO2: " a b" and "hello world") */
    if (from_pif && tail[0] && tail[0] != ' ') {
        char t2[130];
        t2[0] = ' ';
        lstrcpyn(t2 + 1, tail, sizeof t2 - 1);
        lstrcpy(tail, t2);
    }
    /* the PIF's start-up directory becomes the current one (WC1 under
       Windows 3.1 found itself in it); the switch block has no field for
       it */
    trim(pif.directory);
    if (pif.directory[0]) {
        if (pif.directory[1] == ':')
            dos_setdrive((pif.directory[0] | 0x20) - 'a');
        if (dos_chdir(pif.directory)) {
            message(IDS_NODIR, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
    }

    /* the icon (Windows 3.1's shows while its later messages are up) */
    hwnd = CreateWindow("WOA", title,
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPSIBLINGS | WS_MINIMIZE,
                        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hinst, NULL);
    if (!hwnd)
        return FALSE;
    ShowWindow(hwnd, SW_SHOWMINNOACTIVE);

    /* KERNEL closes the files it keeps open (module files): Windows 3.1's
       WINOLDAP does it here (DOSBox-X file log: they all close after the
       program is checked, before the grabber is looked for; Schulman: what
       WinOldApCall does for a mode other than 0). Without it DOS ran out of
       file handles for WSWAP at the second switch ("Fatal exit! Windows
       cannot be swapped back"). */
    WinOldApCall(1);

    /* the grabber: SYSTEM.INI [boot] 286grabber= */
    GetPrivateProfileString("boot", "286grabber", "", name, sizeof name, "SYSTEM.INI");
    if (!name[0] || !exists(name, grabber)) {
        message(IDS_NOGRABBER, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    /* room for the program's swap file on the swap drive: the memory DOS
       programs get (block 183h, paragraphs; 96ACh = 603 K in DOSBox),
       observed with no TEMP and no hard disk: "A", "603 K" */
    {
        DWORD need = (DWORD)*(WORD FAR *)(blk + 0x183) << 4;
        char d = blk[B_SWAPDIR];
        if (d >= 'a' && d <= 'z')
            d -= 'a' - 'A';
        if (d >= 'A' && d <= 'Z' && woa_diskfree(d - 'A' + 1) < need) {
            /* a warning: Windows 3.1's switches all the same (WO2) */
            disk_message(d, (WORD)((need + 1023) >> 10));
        }
    }
    if (free_record() < 0) {
        message(IDS_TOOMANY, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    return TRUE;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG msg;
    (void)show;

    hinst = hi;
    first_active = GetActiveWindow();
    if (!hprev) {
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "WOA";
        RegisterClass(&wc);
    }
    if (!setup(cmd)) {
        if (hwnd) {
            ended = TRUE;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    /* switch from the message loop, a timer tick later, so that WinExec
       returns and its caller runs first (as with Windows 3.1's WINOLDAP:
       a shell's log line after WinExec comes before the DOS program runs) */
    if (!SetTimer(hwnd, 1, 50, NULL))
        PostMessage(hwnd, WM_USER, 0, 0);
    while (!ended && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (hbuf)
        GlobalFree(hbuf);
    return 0;
}
