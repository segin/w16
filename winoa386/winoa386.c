/*
 * winoa386.c - w16's WINOA386.MOD (module WINOLDAP): DOS programs in 386
 * Enhanced mode, one task and one window per DOS virtual machine. MIT
 * licence (LICENSE). ARCHITECTURE.md 12.11; docs/notes/winoa386-handover.md.
 *
 * KERNEL starts SYSTEM\WINOA386.MOD as a task for a DOS program (WinExec
 * of a .COM, .EXE, .BAT or .PIF) with the command line "args NUL path LF",
 * as in Standard mode (ARCHITECTURE.md 9.8), and loads the 386 grabber
 * (SYSTEM.INI [boot] 386grabber=, module GRABBER) for it. This task asks
 * the SHELL VxD (0017h) for a VM, follows SHELL's events and draws a
 * windowed VM's screen through the grabber.
 *
 * Everything here comes from black-box observation of Windows 3.1
 * (Microsoft's WINOA386.MOD was never disassembled or read): VXS.386's
 * records of the calls Microsoft's WINOA386 makes into SHELL, VKD, VMD
 * and the VDD (tests/vxd/vxs.asm, tests/vxsrun.sh; docs/notes/win386-
 * handover.md step 6), GR3's records of its grabber calls (docs/notes/
 * grabber-handover.md "VGA.3GR"), w16's VMM trace of the same calls, and
 * screenshots. SHELL's protocol is also in win386/vmm/shell.c.
 *
 * SHELL's protected-mode API takes the function in DX:
 *   0   version (EAX 030Ah; EBX the System VM's handle)
 *   9   ES:DI a block: +4 0Fh, +8 a 48-bit pointer to the event procedure
 *       (woa386s.asm), which SHELL calls far with (hwnd, message, wParam,
 *       lParam = an event record's linear address)
 *   3   create the VM: ES:DI the block below; EAX back the VM's handle
 *   15h our window (ESI) for the VM just created
 *   5   the focus to VM EBX (a full-screen VM gets the display, a windowed
 *       one the keyboard)
 *   6   flags, twice: (AX the flags, bit 0 windowed), then (AX, CX FFFFh)
 *   7   a status word (4 windowed, 0 full screen)
 *   0Ch an event record handed back (EDI); 10h, 14h after a switching key;
 *   16h, 4 (destroy), 0Ah when the program has ended.
 * Events (window messages): 414h a hot key in a full-screen VM (wParam 1:
 * Alt+Enter), 41Ah a windowed VM's screen changed, 41Eh a switching key
 * (Alt+Tab, Alt+Esc) took the user out of a full-screen VM, 418h the
 * program has ended.
 */
#include <windows.h>
#include "winoa386.h"
#include "../winoldap/pifread.h"
#include "../drivers/grabber/vga3gr/grab.h"

/* woa386s.asm */
extern DWORD __cdecl __far vxd_find(WORD id);
extern void __cdecl __far vxd_call(DWORD entry, VREGS __far *r);
extern DWORD __cdecl __far woa_eventproc(void);
/* shell/start.asm */
extern int __cdecl dos_chdir(const char FAR *path);
extern void __cdecl dos_setdrive(int drive);

static WORD get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax];
static WORD get_drive(void);    /* DOS's current drive, 0 = A: */
#pragma aux get_drive = "mov ah, 19h" "int 21h" "xor ah, ah" value [ax];

/* the grabber (module GRABBER: winoa386.lnk imports these by ordinal) */
extern WORD  __far __pascal PaintScreen(LPEPS);                     /* 1 */
extern void  __far __pascal ScreenFree(LPEPS);                      /* 2 */
extern DWORD __far __pascal GetFontList(LPSTR);                     /* 10 */
extern DWORD __far __pascal SetPaintFnt(LPEPS, int __far *, int __far *); /* 11 */
extern WORD  __far __pascal UpdateScreen(LPEPS, WORD, DWORD);       /* 12 */
extern DWORD __far __pascal CursorOn(LPEPS);                        /* 13 */
extern DWORD __far __pascal CursorPosit(LPEPS);                     /* 14 */
extern WORD  __far __pascal CursorOff(LPEPS);                       /* 15 */
extern DWORD __far __pascal CheckGRBVersion(LPEPS);                 /* 16 */
extern DWORD __far __pascal GetDisplayUpd(LPEPS, WORD, DWORD);      /* 18 */
extern DWORD __far __pascal GrbGetTextColor(LPEPS, DWORD);          /* 21 */
extern void  __far __pascal GrbUnLockApp(LPEPS);                    /* 22 */
extern DWORD __far __pascal TranslateWinToFScreen(LPEPS);           /* 23 */

#define WM_SHELL_HOTKEY 0x414
#define WM_SHELL_FAULT  0x41B          /* a fault ended the VM (wParam 4: #GP) */
#define WM_SHELL_ENDED  0x418
#define WM_SHELL_DISP   0x41A
#define WM_SHELL_SWITCH 0x41E
#define WM_SHELL_MESSAGE 0x4F0          /* w16's: SHELL_Message for this VM (win386/vmm/shell.c) */
#define WM_START        (WM_USER + 0x80)

#define SHELL_ID 0x0017
#define VKD_ID   0x000D
#define VMD_ID   0x000C

static HINSTANCE hinst;
static HWND hwnd;
static DWORD shell, vkd, vmd;          /* API entries */
static DWORD sysvm, vm;                /* VM handles */
static BOOL windowed, ended, running;
static char title[32];
static char program[128];
static char tail[130];                  /* length, text, CR */
static char directory[68];
static EPS eps;                         /* the grabber's (EXTPAINTSTRUC) */
static int scrw = 640, scrh = 300;      /* the DOS screen in pixels (SetPaintFnt) */

#pragma pack(1)
static struct {                         /* SHELL function 9's block */
    DWORD d0, d4;
    DWORD off;
    WORD sel, pad;
} evblk;
static struct {                         /* SHELL function 3's block (make_block) */
    BYTE flags[4];                      /* +00 (function 7 writes the low bits) */
    DWORD video;                        /* +04 */
    DWORD prog_off; WORD prog_sel;      /* +08 */
    DWORD tail_off; WORD tail_sel;      /* +0E */
    DWORD dir_off; WORD dir_sel;        /* +14 */
    BYTE mem[16];                       /* +1A the 386 data's +00h-+0Fh */
    WORD hwnd;                          /* +2A */
    WORD hinst;                         /* +2C */
    char title[32];                     /* +2E */
} cblk;
#pragma pack()

/* ---- strings ---- */
static char sbuf[256];

static int message(int id, UINT flags)
{
    LoadString(hinst, id, sbuf, sizeof sbuf);
    return MessageBox(hwnd, sbuf, title[0] ? title : "WINOLDAP", flags);
}

/* ---- SHELL ---- */
static VREGS sr;

static VREGS *shell_call(WORD fn, DWORD eax, DWORD ebx, DWORD ecx, DWORD esi, DWORD edi)
{
    sr.eax = eax;
    sr.ebx = ebx;
    sr.ecx = ecx;
    sr.edx = fn;
    sr.esi = esi;
    sr.edi = edi;
    sr.es = get_ds();
    vxd_call(shell, &sr);
    return &sr;
}

static DWORD focused;                   /* the VM function 5 gave the focus to last */

static void focus(DWORD v, DWORD edi)
{
    focused = v;
    shell_call(5, 0, v, 0, (DWORD)(WORD)hwnd, edi);
}

static void handback(LPARAM rec)
{
    shell_call(0x0C, 0, 0, 0, 0, rec);
}

/* bytes at a linear address (str: a string, up to n - 1 bytes and a
 * NUL), through a selector of KERNEL's for the while */
UINT FAR PASCAL AllocSelector(UINT);
UINT FAR PASCAL FreeSelector(UINT);
UINT FAR PASCAL SetSelectorBase(UINT, DWORD);
UINT FAR PASCAL SetSelectorLimit(UINT, DWORD);
static void read_linear(DWORD lin, char *buf, int n, BOOL str)
{
    UINT sel = AllocSelector(get_ds());
    LPSTR p;
    int i;
    if (str) buf[0] = 0;
    if (!sel) return;
    SetSelectorBase(sel, lin);
    SetSelectorLimit(sel, (DWORD)n);
    p = (LPSTR)MAKELP(sel, 0);
    if (!str)
        for (i = 0; i < n; i++) buf[i] = p[i];
    else {
        for (i = 0; i < n - 1 && p[i]; i++) buf[i] = p[i];
        buf[i] = 0;
    }
    FreeSelector(sel);
}

/* SHELL_Message for our VM (a device's: VSBD.386's contention message):
 * the event record's data is the text's linear address; shown in a
 * message box with the window's title, on Windows' screen (Windows 3.1:
 * the box inactive behind it), the box in front again after it if it was */
static void shell_message(UINT flags, LPARAM rec)
{
    static char text[256];
    DWORD lin;
    BOOL front = running && !ended && !windowed && focused == vm;
    read_linear((DWORD)rec + 0x10, (char *)&lin, sizeof lin, FALSE);
    read_linear(lin, text, sizeof text, TRUE);
    if (front) focus(sysvm, rec);
    handback(rec);
    MessageBox(hwnd, text, title[0] ? title : "WINOLDAP", flags | MB_TASKMODAL);
    if (front && running && !ended) focus(vm, (DWORD)(WORD)&cblk);
}

/* ---- the PIF (winoldap/pifread.c: as Windows 3.1's WINOA386 reads it) ---- */
static PIFDATA pif;
static BOOL batch;                      /* a .BAT run through COMMAND.COM */

/* SHELL function 3's block from the PIF, as Windows 3.1's WINOA386 fills
   it (tests/pifread.sh: VXS.386's record of the block for each fixture,
   one setting changed at a time; ARCHITECTURE.md 12.11):
     +00  the 386 flags (+10h): E0h (reserved Alt+Tab, Alt+Esc, Alt+Space)
          as they are, 01h exclusive (the PIF's 04h), 02h background (02h),
          04h windowed (the PIF's 08h full screen clear)
     +01  the 386 data's +11h as it is (Alt+Enter ... 10h detect idle
          time, 20h no high memory area, 40h a shortcut key, 80h EMS locked)
     +02  +12h as it is (01h XMS locked, 02h fast paste, 04h lock
          application memory), 08h a batch file
     +03  40h close window on exit (63h's 10h)
     +04  the video dword (+14h)
     +1A  the 386 data's first 16 bytes: KB desired, required, the
          priorities, EMS and XMS limit and required
   A PIF without a usable 386 section (a 2.x PIF, a damaged one) gives
   the defaults with KB desired and required from 20h and 22h, video 17h,
   full screen with 16Fh's 80h (else windowed), exclusive with its 10h,
   background with 63h's 01h (63h FFh gave it; which bit: ed2x63_*) */
static void make_block(void)
{
    BYTE f0 = pif.d386[D386_F0];
    int i;
    if (!pif.found && pif.opened) {     /* (no PIF at all: the defaults, UNVERIFIED) */
        BYTE b = (BYTE)pif.w_16f;
        *(WORD *)(pif.d386 + D386_MAX) = pif.maxmem;
        *(WORD *)(pif.d386 + D386_MIN) = pif.minmem;
        f0 = (BYTE)(((b & 0x80) ? 0x08 : 0) | ((b & 0x10) ? 0x04 : 0) |
                    ((pif.flags & 0x01) ? 0x02 : 0));
    }
    cblk.flags[0] = (BYTE)((f0 & 0xE0) | ((f0 & 0x04) ? 0x01 : 0) |
                           ((f0 & 0x02) ? 0x02 : 0) | ((f0 & 0x08) ? 0 : 0x04));
    cblk.flags[1] = pif.d386[D386_F1];
    cblk.flags[2] = (BYTE)(pif.d386[D386_F2] | (batch ? 0x08 : 0));
    cblk.flags[3] = (BYTE)((pif.flags & PIFF_CLOSE) || !pif.opened ? 0x40 : 0);
    cblk.video = *(DWORD *)(pif.d386 + D386_VIDEO);
    for (i = 0; i < 16; i++)
        cblk.mem[i] = pif.d386[i];
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

/* a directory there (INT 21h 4300h; a drive's root always) */
static WORD dos_attr(const char FAR *p);
#pragma aux dos_attr = "push ds" "mov ds, cx" "mov ax, 4300h" "int 21h" "mov ax, cx" "jnc ok" \
    "xor ax, ax" "ok:" "pop ds" parm [cx dx] value [ax] modify [cx dx];

static BOOL dir_exists(const char *d)
{
    char b[68];
    int n = lstrlen(d);
    if ((n == 2 && d[1] == ':') || (n == 3 && d[1] == ':' && d[2] == '\\'))
        return TRUE;
    lstrcpyn(b, d, sizeof b);
    if (n > 3 && b[n - 1] == '\\')
        b[n - 1] = 0;
    return (dos_attr(b) & 0x10) != 0;
}

/* the Parameters dialog ("?" as the PIF's parameters) */
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

/* ---- the grabber's fonts: GetFontList names four files (SYSTEM.INI
   [386Enh] keys, default EGA80WOA.FON, EGA40WOA.FON, CGA80WOA.FON,
   CGA40WOA.FON) with their cells; WINOA386 puts them in AltFnt[0..3]
   (80x25, 40x25, 80x43/50, 40x43/50) and the first in DefFont (GR3) ---- */
static char font_file[4][16];           /* the files added (AddFontResource) */
static HFONT font_made[4];              /* the fonts made (CreateFont) */

static void load_fonts(void)
{
    /* Microsoft's VGA.3GR writes more than the 4 x 17 bytes w16's does
       (a smaller buffer here lost WinMain's return address): room to
       spare, UNVERIFIED how much it writes */
    char list[512];
    int i;
    GetFontList((LPSTR)list);
    for (i = 0; i < 4; i++) {
        char *e = list + i * 17;
        int h = *(int *)(e + 13), w = *(int *)(e + 15);  /* words */
        HFONT f;
        /* a file that is not there is left out quietly (then the OEM
           font, 8x12 like EGA80WOA's cells; w16's tree has all four,
           fonts/ega80woa.txt etc.; Windows 3.1's WINOA386 stops with KERNEL's
           "Cannot find" box) */
        if (!exists(e, NULL))
            continue;
        if (AddFontResource(e) <= 0)
            continue;
        lstrcpyn(font_file[i], e, sizeof font_file[i]);
        f = CreateFont(h, w, 0, 0, FW_NORMAL, 0, 0, 0, OEM_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Terminal");
        font_made[i] = f;
        eps.AltFnt[i].hFont = (WORD)f;
        eps.AltFnt[i].height = h;
        eps.AltFnt[i].width = w;
    }
    if (!eps.AltFnt[0].hFont) {
        eps.AltFnt[0].hFont = (WORD)GetStockObject(OEM_FIXED_FONT);
        eps.AltFnt[0].height = 12;
        eps.AltFnt[0].width = 8;
    }
    eps.DefFont = eps.AltFnt[0];
}

/* at the end: the fonts deleted and the files removed again, as Windows
   3.1's WINOA386 leaves them (observed with tests/vmtasksoak.sh: after a
   box has ended, GDI has no more fonts than before and the *WOA.FON
   modules are gone). w16's kept four fonts and the files' modules for
   every box ever run, so that a long session's DOS boxes used up GDI's
   heap. */
static void free_fonts(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (font_made[i])
            DeleteObject(font_made[i]);
        font_made[i] = 0;
        if (font_file[i][0])
            RemoveFontResource(font_file[i]);
        font_file[i][0] = 0;
    }
}

/* ---- the window ---- */
static void window_size(void)
{
    RECT r;
    int x, sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    SetRect(&r, 0, 0, scrw, scrh);
    AdjustWindowRect(&r, style & ~(WS_HSCROLL | WS_VSCROLL), FALSE);
    x = 0;
    /* wider than the screen: a frame's width in from the left, as wide
       as the screen less two frames (Windows 3.1's 80x25 window at 640
       pixels: 4 to 636, measured on its screen) */
    if (r.right - r.left > sw) {
        x = GetSystemMetrics(SM_CXFRAME);
        r.right = r.left + sw - 2 * x;
        r.bottom += GetSystemMetrics(SM_CYHSCROLL);    /* the whole height still shows */
    }
    if (r.bottom - r.top > sh)
        r.bottom = r.top + sh;
    SetWindowPos(hwnd, NULL, x, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void scroll_ranges(void)
{
    RECT c;
    GetClientRect(hwnd, &c);
    SetScrollRange(hwnd, SB_HORZ, 0, c.right < scrw ? scrw - c.right : 0, FALSE);
    SetScrollRange(hwnd, SB_VERT, 0, c.bottom < scrh ? scrh - c.bottom : 0, FALSE);
    if (eps.ColOrg > (WORD)(scrw > c.right ? scrw - c.right : 0))
        eps.ColOrg = 0;
    if (eps.RowOrg > (WORD)(scrh > c.bottom ? scrh - c.bottom : 0))
        eps.RowOrg = 0;
    SetScrollPos(hwnd, SB_HORZ, eps.ColOrg, TRUE);
    SetScrollPos(hwnd, SB_VERT, eps.RowOrg, TRUE);
}

static void paint_font(void)
{
    int w = scrw, h = scrh;
    SetPaintFnt(&eps, (int __far *)&w, (int __far *)&h);
    if (w > 0 && h > 0 && (w != scrw || h != scrh)) {
        scrw = w;
        scrh = h;
        window_size();
        scroll_ranges();
    }
}

/* full screen -> window (Alt+Enter: event 414h; 3.1's order of calls) */
static void to_window(LPARAM rec)
{
    focus(sysvm, rec);
    shell_call(6, 1, vm, 1, 0x500, (DWORD)(WORD)&cblk);
    shell_call(6, 1, vm, 0xFFFF, 0x500, (DWORD)(WORD)&cblk);
    shell_call(7, 4, vm, 0, 0x500, (DWORD)(WORD)&cblk);
    windowed = TRUE;
    eps.EPStatusFlags |= 0x00010000L;
    eps.EPStatusFlags &= ~EPS_STATEVALID;
    paint_font();
    ShowWindow(hwnd, SW_SHOWNORMAL);
    window_size();
    scroll_ranges();
    CursorOn(&eps);
    InvalidateRect(hwnd, NULL, TRUE);
    handback(rec);
    if (GetActiveWindow() == hwnd)
        focus(vm, 0);
}

/* window -> full screen (Alt+Enter in the window) */
static void to_full(LPARAM rec)
{
    focus(sysvm, rec);
    shell_call(6, 0, vm, 1, 0x500, (DWORD)(WORD)&cblk);
    shell_call(6, 0x10, vm, 0xFFFF, 0x500, (DWORD)(WORD)&cblk);
    shell_call(7, 0, vm, 0, 0x500, (DWORD)(WORD)&cblk);
    windowed = FALSE;
    eps.EPStatusFlags &= ~0x00010000L;
    CursorOff(&eps);
    handback(rec);
    ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    focus(vm, (DWORD)(WORD)&cblk);
}

/* a windowed VM's screen changed (event 41Ah) */
static void update(LPARAM rec)
{
    DWORD f;
    HDC dc;
    if (!windowed || IsIconic(hwnd)) {
        handback(rec);
        return;
    }
    f = GetDisplayUpd(&eps, 0, rec);
    if (LOWORD(f) & (fDisp_Ctlr | fDisp_VRAM)) {
        dc = GetDC(hwnd);
        eps.ps.hdc = dc;
        /* the whole client area: the grabber paints what changed within
           rcPaint, which the last WM_PAINT had left at its own rectangle
           (typed characters outside it were never painted) */
        GetClientRect(hwnd, &eps.ps.rcPaint);
        paint_font();
        UpdateScreen(&eps, 0, rec);
        ReleaseDC(hwnd, dc);
        CursorPosit(&eps);
    } else {
        GrbUnLockApp(&eps);
        if (LOWORD(f) & fDisp_Curs)
            CursorPosit(&eps);
    }
    handback(rec);
}

/* the program has ended (event 418h) */
/* the VM goes (function 4) and the window with it */
static BOOL inactive;                   /* ended, kept: close window on exit off */

static void destroy_vm(void)
{
    shell_call(4, 1, vm, 0, 0xB0, 0);
    if (windowed) {
        CursorOff(&eps);
        windowed = FALSE;
    }
    ScreenFree(&eps);
    DestroyWindow(hwnd);
}

static void program_ended(LPARAM rec)
{
    ended = TRUE;
    focus(sysvm, rec);
    /* (3.1: function 6 with AX 418h here, the event's number) */
    shell_call(6, 0x418, vm, 0, (DWORD)(WORD)hwnd, (DWORD)(WORD)&cblk);
    shell_call(0x16, 0, 0, 0, (DWORD)(WORD)hwnd, rec);
    handback(rec);
    if (!(cblk.flags[3] & 0x40)) {
        /* close window on exit off (the PIF's 63h 10h clear; block +03h):
           Windows 3.1's WINOA386 keeps the VM and shows its screen in a
           window titled "(Inactive TITLE)" until the user closes it, with
           SHELL 6 (AX 0, CX 47h), 6 (CX FFFFh), 7 (4) (tests/pifread.sh
           ednoclose, VXS.386); then function 4 */
        char t[48];
        inactive = TRUE;
        shell_call(6, 0, vm, 0x47, 0x38, (DWORD)(WORD)&cblk);
        shell_call(6, 0, vm, 0xFFFF, 0x38, (DWORD)(WORD)&cblk);
        shell_call(7, 4, vm, 0, 0x38, (DWORD)(WORD)&cblk);
        wsprintf(t, "(Inactive %s)", (LPSTR)title);
        SetWindowText(hwnd, t);
        if (!windowed) {
            windowed = TRUE;
            eps.EPStatusFlags |= 0x00010000L;
            eps.EPStatusFlags &= ~EPS_STATEVALID;
            paint_font();
        }
        ShowWindow(hwnd, SW_SHOWNORMAL);
        window_size();
        scroll_ranges();
        InvalidateRect(hwnd, NULL, TRUE);
        focus(sysvm, 0);                /* (3.1: function 5 last) */
        return;
    }
    shell_call(4, 1, vm, 0, 0xB0, 0);
    if (windowed) {
        CursorOff(&eps);
        windowed = FALSE;
    }
    ScreenFree(&eps);
    DestroyWindow(hwnd);
}

/* a switching key took the user out of the full-screen VM (event 41Eh):
   SHELL tells which (10h), and Windows gets the focus */
/* the next window in Windows' switching order after w (top-level,
   visible, titled, not owned), wrapping round; NULL if none */
static HWND next_window(HWND w)
{
    HWND d = GetDesktopWindow(), c, first = GetWindow(d, GW_CHILD);
    int pass;
    c = w ? GetWindow(w, GW_HWNDNEXT) : first;
    for (pass = 0; pass < 2; pass++) {
        for (; c; c = GetWindow(c, GW_HWNDNEXT))
            if (IsWindowVisible(c) && !GetWindow(c, GW_OWNER) &&
                !(GetWindowLong(c, GWL_STYLE) & WS_CHILD) && GetWindowTextLength(c))
                return c;
        c = first;
    }
    return NULL;
}

/* a switching key took the user out of the full-screen VM (event 41Eh).
   Windows 3.1 (VXS.386, Alt+Tab and Alt+Esc from the MS-DOS Prompt):
   WINOA386 offers SHELL the windows in switching order, each with its
   title (function 10h: EBX FFFFFFFFh for a Windows window, else the
   window's VM; ECX the window; DS:SI a block whose title at +1Ch ES:DI
   points to, its dwords 0Fh 9 0Fh 0 0Fh 0Fh 0 as 3.1's). SHELL shows the
   title full screen while Alt is held: EAX 1 back for "the next one"
   (Tab again), EAX 0 with ECX (and EBX) the window chosen. Alt+Esc: the
   first answer is EAX 0. Then function 0Ch with EAX 1 (the System VM
   gets the focus) and the chosen window is activated. */
#pragma pack(1)
static struct { DWORD d[7]; char title[80]; } swblk = { { 0x0F, 9, 0x0F, 0, 0x0F, 0x0F, 0 } };
#pragma pack()

static void switched(LPARAM rec)
{
    HWND c = hwnd, chosen = NULL;
    VREGS *r;
    DWORD t0 = GetTickCount();
    /* EAX 1 comes back at once while Alt is held (Microsoft's SHELL, VXS:
       3.1's WINOA386 asked again about 170 ms later): the next window is
       offered after a pause in which other tasks run. UNVERIFIED how 3.1
       tells "Tab again" from "still held"; at most 30 s */
    while (GetTickCount() - t0 < 30000L) {
        c = next_window(c);
        if (!c)
            break;
        GetWindowText(c, swblk.title, sizeof swblk.title);
        r = shell_call(0x10, 0, c == hwnd ? vm : (GetClassWord(c, GCW_HMODULE) == GetClassWord(hwnd, GCW_HMODULE) ?
                       GetWindowLong(c, 0) : 0xFFFFFFFFL),
                       (DWORD)(WORD)c, (DWORD)(WORD)&swblk, (DWORD)(WORD)swblk.title);
        if (LOWORD(r->eax) != 1) {
            chosen = (HWND)LOWORD(r->ecx);
            break;
        }
        {
            DWORD t = GetTickCount();
            MSG m;
            while (GetTickCount() - t < 150)
                PeekMessage(&m, NULL, 0, 0, PM_NOREMOVE);
        }
    }
    shell_call(0x0C, 1, 0, 0, 0, rec);
    if (!chosen || !IsWindow(chosen))
        chosen = next_window(hwnd);
    if (chosen == hwnd)
        focus(vm, (DWORD)(WORD)&cblk);  /* back to the box itself */
    else if (chosen) {
        if (IsIconic(chosen) && GetClassWord(chosen, GCW_HMODULE) != GetClassWord(hwnd, GCW_HMODULE))
            ShowWindow(chosen, SW_RESTORE);
        SetActiveWindow(chosen);
    }
}

static void paint(void)
{
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (windowed && !IsIconic(hwnd)) {
        eps.ps = ps;
        TranslateWinToFScreen(&eps);
        GetDisplayUpd(&eps, 0xFFFF, 0);
        paint_font();
        PaintScreen(&eps);
    }
    EndPaint(hwnd, &ps);
}

static void do_scroll(int bar, WORD code, int pos)
{
    RECT c;
    int max, page, *cur, old;
    GetClientRect(hwnd, &c);
    if (bar == SB_HORZ) {
        cur = (int *)&eps.ColOrg;
        page = c.right;
        max = scrw > c.right ? scrw - c.right : 0;
    } else {
        cur = (int *)&eps.RowOrg;
        page = c.bottom;
        max = scrh > c.bottom ? scrh - c.bottom : 0;
    }
    old = *cur;
    switch (code) {
    case SB_LINEUP: *cur -= bar == SB_HORZ ? eps.FntWid : eps.FntHgt; break;
    case SB_LINEDOWN: *cur += bar == SB_HORZ ? eps.FntWid : eps.FntHgt; break;
    case SB_PAGEUP: *cur -= page; break;
    case SB_PAGEDOWN: *cur += page; break;
    case SB_THUMBPOSITION: *cur = pos; break;
    case SB_TOP: *cur = 0; break;
    case SB_BOTTOM: *cur = max; break;
    default: return;
    }
    if (*cur < 0) *cur = 0;
    if (*cur > max) *cur = max;
    if (*cur != old) {
        SetScrollPos(hwnd, bar, *cur, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        CursorPosit(&eps);
    }
}

LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SHELL_HOTKEY:
        if (wp == 1) {
            if (windowed) to_full(lp); else to_window(lp);
        } else
            handback(lp);
        return 0;
    case WM_SHELL_DISP:
        update(lp);
        return 0;
    case WM_SHELL_SWITCH:
        switched(lp);
        return 0;
    case WM_SHELL_ENDED:
        program_ended(lp);
        return 0;
    case WM_SHELL_MESSAGE:
        shell_message((UINT)wp, lp);
        return 0;
    case WM_SHELL_FAULT:
        /* Windows 3.1 (VX12 G): a stop box with the window's title, then
         * the VM goes as when its program ends */
        message(IDS_GPFAULT, MB_OK | MB_ICONSTOP);
        program_ended(lp);
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_HSCROLL:
        do_scroll(SB_HORZ, wp, LOWORD(lp));
        return 0;
    case WM_VSCROLL:
        do_scroll(SB_VERT, wp, LOWORD(lp));
        return 0;
    case WM_QUERYOPEN:
        /* opening the icon: the box full screen again (3.1: SHELL 5) */
        if (running && !ended && !windowed) {
            focus(vm, (DWORD)(WORD)&cblk);
            return FALSE;
        }
        break;
    case WM_ACTIVATE:
        if (running && !ended) {
            /* (only when another VM has it: 3.1's windowed start gives
               the VM the focus once, tests/pifread.sh edwin) */
            if (wp != WA_INACTIVE && !HIWORD(lp) && windowed) {
                if (focused != vm)
                    focus(vm, 0);       /* the keyboard to the windowed VM */
            }
            else if (wp != WA_INACTIVE && HIWORD(lp) && !windowed && GetFocus() == h) {
                /* the icon activated by Alt+Tab or Alt+Esc: full screen */
                PostMessage(h, WM_START, 1, 0);
            } else if (wp == WA_INACTIVE && windowed && focused != sysvm)
                focus(sysvm, 0);
        }
        break;
    case WM_START:
        if (running && !ended && !windowed && GetActiveWindow() == h)
            focus(vm, (DWORD)(WORD)&cblk);
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_CLOSE && !ended) {
            message(IDS_STILLACTIVE, MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        if (((wp & 0xFFF0) == SC_RESTORE || (wp & 0xFFF0) == SC_MAXIMIZE) && !windowed && running && !ended) {
            focus(vm, (DWORD)(WORD)&cblk);
            return 0;
        }
        break;
    case WM_QUERYENDSESSION:
        if (!ended) {
            message(IDS_QUITFIRST, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        return TRUE;
    case WM_CLOSE:
        if (!ended) {
            message(IDS_STILLACTIVE, MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }
        if (inactive) {                 /* the inactive window closed */
            inactive = FALSE;
            destroy_vm();
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---- start-up ---- */
static BOOL setup(LPSTR cmd)
{
    char path[128], args[130], name[80], pifpath[128], t[130];
    BOOL from_pif;
    int i, n;

    /* "args NUL path LF" (WO1) */
    for (n = 0; cmd[n] && n < (int)sizeof args - 1; n++)
        args[n] = cmd[n];
    args[n] = 0;
    cmd += n + 1;
    for (i = 0; cmd[i] && cmd[i] != '\n' && cmd[i] != '\r' && i < (int)sizeof path - 1; i++)
        path[i] = cmd[i];
    path[i] = 0;

    /* the PIF: the one given, NAME.PIF beside the program or on the path,
       _DEFAULT.PIF (as Standard mode's WINOLDAP looks) */
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
    pif_read(pifpath, &pif, PIF_386);
    /* the title: the PIF's, else the PIF's (or the program's) name in
       capitals (3.1: a blank title in C:\PROBE.PIF gave "PROBE") */
    lstrcpy(title, pif.title);
    if (!title[0]) {
        stem(path, title, sizeof title);
        AnsiUpper(title);
    }
    if (has_ext(path, ".PIF")) {
        if (!lstrcmpi(pif.program, "COMMAND.COM") && get_env("COMSPEC", program, sizeof program))
            ;
        else if (!exists(pif.program, program)) {
            /* 3.1: "Cannot find file." titled with the program as the PIF
               names it */
            lstrcpyn(title, pif.program, sizeof title);
            message(IDS_NOFILE, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
    } else if (!exists(path, program)) {
        stem(path, title, sizeof title);
        message(IDS_NOFILE, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    /* the parameters: those given to WinExec as they are, else the 386
       section's (never the base's) after a blank, blanks and case kept
       (3.1: "  ea  eb  " gave "   ea  eb  "); "?" asks for them */
    from_pif = !args[0];
    lstrcpy(t, from_pif ? pif.params : args);
    if (!from_pif)
        trim(t);
    if (from_pif && t[0] == '?' && !t[1]) {
        t[0] = 0;
        if (DialogBox(hinst, MAKEINTRESOURCE(IDD_PARAMS), NULL, (DLGPROC)MakeProcInstance((FARPROC)DialogProc, hinst)) <= 0)
            return FALSE;
        lstrcpy(t, param_buf);
    }
    if (from_pif && t[0]) {
        char t2[130];
        t2[0] = ' ';
        lstrcpyn(t2 + 1, t, sizeof t2 - 1);
        lstrcpy(t, t2);
    }
    /* a batch file runs through COMMAND.COM: " /c FILE" and a blank
       before the parameters (3.1: " /c C:\WC1.BAT  enhargs"; block +02h
       08h) */
    if (has_ext(program, ".BAT") && get_env("COMSPEC", name, sizeof name)) {
        char t2[130];
        batch = TRUE;
        wsprintf(t2, " /c %s %s", (LPSTR)program, (LPSTR)t);
        lstrcpyn(t, t2, sizeof t);
        lstrcpy(program, name);
    }
    n = lstrlen(t);
    if (n > 126) n = 126;
    tail[0] = (char)n;
    for (i = 0; i < n; i++)
        tail[1 + i] = t[i];
    tail[1 + n] = '\r';
    tail[2 + n] = 0;

    /* the start-up directory: the PIF's, else the current drive; one that
       is not there stops it (3.1's box, titled with the PIF's title) */
    if (pif.directory[0]) {
        lstrcpy(directory, pif.directory);
        if (!dir_exists(directory)) {
            message(IDS_NODIR, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
    } else {
        directory[0] = (char)('A' + get_drive());
        directory[1] = ':';
        directory[2] = 0;
    }
    make_block();
    return TRUE;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG msg;
    VREGS *r;
    WORD ds_ = get_ds();
    DWORD ep;
    (void)show;

    hinst = hi;
    if (!hprev) {
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 4;          /* the VM's handle (switching) */
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "WOA386";
        RegisterClass(&wc);
    }
    if (!setup(cmd))
        return 0;

    /* the VxDs (3.1's order: SHELL, then VMD and VKD) */
    shell = vxd_find(SHELL_ID);
    if (!shell) {
        message(IDS_NOSHELL, MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    r = shell_call(0, 0x1684, 0xFFFF0017L, 0, 0, 0);
    sysvm = r->ebx;
    vmd = vxd_find(VMD_ID);
    vkd = vxd_find(VKD_ID);
    if (vmd) { sr.eax = 0; vxd_call(vmd, &sr); }
    if (vkd) { sr.eax = 0; vxd_call(vkd, &sr); }

    /* the grabber */
    eps.RNG1DSSEL = 0;
    GrbGetTextColor(&eps, 0x00FFFFFFL);
    GrbGetTextColor(&eps, 0);
    load_fonts();
    CheckGRBVersion(&eps);
    eps.WhtBrshH = (WORD)GetStockObject(WHITE_BRUSH);
    eps.BlkBrshH = (WORD)GetStockObject(BLACK_BRUSH);
    eps.hThisInst = (WORD)hi;

    /* the window: an icon while the VM is full screen */
    hwnd = CreateWindow("WOA386", title,
                        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS | WS_MINIMIZE,
                        0, 0, CW_USEDEFAULT, 0, NULL, NULL, hi, NULL);
    if (!hwnd) {
        free_fonts();
        return 0;
    }
    eps.WindHand = (WORD)hwnd;
    SetScrollRange(hwnd, SB_HORZ, 0, 0, FALSE);
    SetScrollRange(hwnd, SB_VERT, 0, 0, FALSE);
    ShowWindow(hwnd, SW_SHOWMINNOACTIVE);

    /* SHELL: our event procedure and the flat selector (function 9) */
    ep = woa_eventproc();
    evblk.d4 = 0x0F;
    evblk.off = LOWORD(ep);
    evblk.sel = HIWORD(ep);
    r = shell_call(9, 0, 0, 0, 0, (DWORD)(WORD)&evblk);
    eps.RNG1DSSEL = LOWORD(r->edx);

    /* the VM (function 3): the block as 3.1's WINOA386 fills it */
    cblk.prog_off = (WORD)program; cblk.prog_sel = ds_;
    cblk.tail_off = (WORD)tail; cblk.tail_sel = ds_;
    cblk.dir_off = (WORD)directory; cblk.dir_sel = ds_;
    cblk.hwnd = (WORD)hwnd;
    cblk.hinst = (WORD)hi;
    lstrcpyn(cblk.title, title, sizeof cblk.title);
    /* the registers as 3.1's WINOA386 gives them: EAX 1902h, EBX F0h,
       ESI 8 for every PIF observed; ECX 07C7h with the 386 data's +12h
       (XMS locked, fast paste, lock memory) in bits 16-23 (tests/
       pifread.sh; their meaning is UNVERIFIED) */
    r = shell_call(3, 0x1902, 0xF0, 0x07C7 | ((DWORD)pif.d386[D386_F2] << 16), 8, (DWORD)(WORD)&cblk);
    if ((r->flags & 1) || !r->eax) {
        message(IDS_NOMEM, MB_OK | MB_ICONEXCLAMATION);
        ended = TRUE;
        DestroyWindow(hwnd);
        free_fonts();
        return 0;
    }
    vm = r->eax;
    SetWindowLong(hwnd, 0, vm);
    eps.ThisVMHand = vm;
    eps.VM1_Hnd = vm;
    running = TRUE;
    if (cblk.flags[0] & 0x04) {
        /* windowed from the start (the PIF's Display Usage; 3.1: SHELL 5
           to the System VM (EDI 0), 5 to the VM (EDI 1), 15h, then the
           window shows the VM's screen: tests/pifread.sh edwin) */
        focus(sysvm, 0);
        focus(vm, 1);
        shell_call(0x15, 0, vm, 0, (DWORD)(WORD)hwnd, 0);
        windowed = TRUE;
        eps.EPStatusFlags |= 0x00010000L;
        eps.EPStatusFlags &= ~EPS_STATEVALID;
        paint_font();
        ShowWindow(hwnd, SW_SHOWNORMAL);
        window_size();
        scroll_ranges();
        CursorOn(&eps);
        InvalidateRect(hwnd, NULL, TRUE);
    } else {
        focus(vm, MAKELONG(1, ds_));
        shell_call(0x15, 0, vm, 0, (DWORD)(WORD)hwnd, 0);
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    free_fonts();
    return 0;
}
