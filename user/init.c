/*
 * init.c - USER's initialisation (ARCHITECTURE.md 10.2, the boot contract
 * observed with probe US1 on Windows 3.1).
 *
 * KERNEL loads USER.EXE last of the boot modules and runs its entry point
 * (uentry.asm) with the boot task current; USER sets up the keyboard, the
 * mouse, the display's cursor, its DCs, colours and fonts, writing the
 * INIT=/INITDONE= lines of BOOTLOG.TXT through DiagOutput. KERNEL then
 * loads the shell and calls FinalUserInit (USER.400) before running it.
 */
#include "user.h"

HINSTANCE hInstUser;
HMODULE hModUser;
HMODULE hModDisplay;
HFONT hfontSys;
int cxSysChar, cySysChar;
WORD booted;
HBRUSH hbrGray;
static HBITMAP hbmGray;

/* Windows 3.1's values on VGA (probe US2, tests/apps/us2ref.h); those
   that follow from WIN.INI or the display are set in init_metrics */
int sysmet[SM_CMETRICS + 8] = {
    640, 480, 17, 17, 20, 1, 1, 4, 4, 17,       /* 0-9 */
    17, 32, 32, 32, 32, 18, 640, 460, 0, 1,     /* 10-19 */
    17, 17, 0, 0, 0, 0, 0, 0, 102, 26,          /* 20-29 */
    18, 18, 4, 4, 102, 26, 4, 4, 75, 72,        /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0                      /* 40-47 */
};

/* GetSysColor's defaults: Windows 3.1's with no WIN.INI [colors] (US2) */
COLORREF syscolor[COLOR_BTNHIGHLIGHT + 1] = {
    0xC0C0C0L, 0xC0C0C0L, 0x800000L, 0xFFFFFFL, 0xFFFFFFL, 0xFFFFFFL,  /* 0-5 */
    0x000000L, 0x000000L, 0x000000L, 0xFFFFFFL, 0xC0C0C0L, 0xC0C0C0L,  /* 6-11 */
    0xFFFFFFL, 0x800000L, 0xFFFFFFL, 0xC0C0C0L, 0x808080L, 0xC0C0C0L,  /* 12-17 */
    0x000000L, 0x000000L, 0xFFFFFFL                                  /* 18-20 */
};
HBRUSH sysbrush[COLOR_BTNHIGHLIGHT + 1];

/* the [colors] key names of WIN.INI, in COLOR_* order (3.1 SDK) */
static const char *const colornames[] = {
    "Scrollbar", "Background", "ActiveTitle", "InactiveTitle", "Menu", "Window",
    "WindowFrame", "MenuText", "WindowText", "TitleText", "ActiveBorder",
    "InactiveBorder", "AppWorkspace", "Hilight", "HilightText", "ButtonFace",
    "ButtonShadow", "GrayText", "ButtonText", "InactiveTitleText", "ButtonHilight"
};

void diag(LPCSTR s)
{
    K_DIAGOUTPUT(s);
}

static WORD ulog_on = 0xFFFF;
void ulog(LPCSTR s)
{
    HFILE h;
    if (ulog_on == 0xFFFF)
        ulog_on = GetPrivateProfileInt("w16", "userlog", 0, "SYSTEM.INI");
    if (!ulog_on)
        return;
    h = _lopen("C:\\USER.LOG", OF_WRITE);
    if (h == HFILE_ERROR)
        h = _lcreat("C:\\USER.LOG", 0);
    if (h == HFILE_ERROR)
        return;
    _llseek(h, 0, 2);
    _lwrite(h, s, lstr_len(s));
    _lclose(h);
}

void ulogx(LPCSTR s, WORD v)
{
    char b[8];
    int i;
    ulog(s);
    for (i = 3; i >= 0; i--, v >>= 4)
        b[i] = "0123456789ABCDEF"[v & 15];
    b[4] = '\r', b[5] = '\n', b[6] = 0;
    ulog(b);
}

static int parse_int(const char *p, const char **end)
{
    int v = 0;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    *end = p;
    return v;
}

static void load_colors(void)
{
    char buf[32];
    int i;
    for (i = 0; i <= COLOR_BTNHIGHLIGHT; i++) {
        if (GetProfileString("colors", colornames[i], "", buf, sizeof buf)) {
            const char *p = buf;
            int r = parse_int(p, &p), g = parse_int(p, &p), b = parse_int(p, &p);
            syscolor[i] = RGB(r, g, b);
        }
    }
}

void make_sysbrushes(void)
{
    int i;
    for (i = 0; i <= COLOR_BTNHIGHLIGHT; i++) {
        if (sysbrush[i])
            DeleteObject(sysbrush[i]);
        sysbrush[i] = CreateSolidBrush(syscolor[i]);
        G_MAKEOBJECTPRIVATE(sysbrush[i], TRUE);
    }
}

static void init_metrics(void)
{
    int bw = GetProfileInt("windows", "BorderWidth", 3);
    TEXTMETRIC tm;
    HFONT old;
    if (bw < 1)
        bw = 1;
    if (bw > 49)
        bw = 49;                /* UNVERIFIED: Control Panel's range is 1-49 */
    sysmet[SM_CXFRAME] = sysmet[SM_CYFRAME] = bw + 1;
    sysmet[SM_CXSCREEN] = sysmet[SM_CXFULLSCREEN] = GetDeviceCaps(hdcScreen, HORZRES);
    sysmet[SM_CYSCREEN] = GetDeviceCaps(hdcScreen, VERTRES);
    old = SelectObject(hdcScreen, GetStockObject(SYSTEM_FONT));
    GetTextMetrics(hdcScreen, &tm);
    SelectObject(hdcScreen, old);
    cySysChar = tm.tmHeight;
    {
        /* UNVERIFIED formula: (width of the 52 letters / 26 + 1) / 2 gives
           Windows 3.1's 8 for the VGA system font (US2) */
        static char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        cxSysChar = (LOWORD(GetTextExtent(hdcScreen, letters, 52)) / 26 + 1) / 2;
    }
    /* UNVERIFIED derivations, equal to the observed values with the VGA
       system font (16 pixels high): caption 20, menu 18 */
    sysmet[SM_CYCAPTION] = cySysChar + 4;
    sysmet[SM_CYMENU] = cySysChar + 2;
    sysmet[SM_CYFULLSCREEN] = sysmet[SM_CYSCREEN] - sysmet[SM_CYCAPTION];
    sysmet[SM_MOUSEPRESENT] = 1;
    sysmet[SM_SWAPBUTTON] = GetProfileInt("windows", "SwapMouseButtons", 0) != 0;
    sysmet[SM_CXICONSPACING] = GetProfileInt("desktop", "IconSpacing", 75);
    sysmet[SM_CXDOUBLECLK] = GetProfileInt("windows", "DoubleClickWidth", 4);
    sysmet[SM_CYDOUBLECLK] = GetProfileInt("windows", "DoubleClickHeight", 4);
}

/* the 8x8 50% gray pattern (rows 55h, AAh) USER makes at boot (US1) */
static void make_gray(void)
{
    static WORD bits[8] = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA };
    hbmGray = CreateBitmap(8, 8, 1, 1, bits);
    hbrGray = CreatePatternBrush(hbmGray);
    DeleteObject(hbmGray);
    G_MAKEOBJECTPRIVATE(hbrGray, TRUE);
}

static void load_fonts(void)
{
    /* every value of WIN.INI [fonts], in file order */
    static char keys[2048], val[80];
    char *k;
    GetProfileString("fonts", NULL, "", keys, sizeof keys);
    for (k = keys; *k; k += lstr_len(k) + 1)
        if (GetProfileString("fonts", k, "", val, sizeof val))
            AddFontResource(val);
}

static BYTE kbinfo[16];
static BYTE msinfo[16];
static BYTE cursorinfo[8];
extern void UCB check_cursor_proc(void);

int __far __cdecl user_init(HINSTANCE hinst)
{
    hInstUser = hinst;
    hModUser = K_GETEXEPTR(hinst);
    ulog("USER init\r\n");
    InitAtomTable(37);                  /* UNVERIFIED size; atoms grow anyway */

    diag("INIT=Keyboard\r\n");
    KB_INQUIRE(kbinfo);
    diag("INITDONE=Keyboard\r\n");

    diag("INIT=Mouse\r\n");
    if (MS_INQUIRE(msinfo) && msinfo[0])
        diag("STATUS=Mouse driver installed\r\n");
    diag("INITDONE=Mouse\r\n");

    diag("INIT=Display\r\n");
    hModDisplay = GetModuleHandle("DISPLAY");
    dc_init();              /* GDI loads "DISPLAY.drv" for the first CreateDC */
    D_INQUIRE(cursorinfo);
    diag("INITDONE=Display\r\n");

    diag("INIT=Display Resources\r\n");

    init_metrics();
    load_colors();
    make_sysbrushes();
    make_gray();
    timer_init();
    input_init();
    register_system_classes();
    diag("INITDONE=Display Resources\r\n");

    diag("INIT=Fonts\r\n");
    load_fonts();
    diag("INITDONE=Fonts\r\n");

    diag("INIT=Lang Driver\r\n");
    diag("INITDONE=Lang Driver\r\n");

    hwndDesktop = create_window(0, MAKEINTRESOURCE(32769), NULL,
                                WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
                                0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN],
                                0, 0, hInstUser, NULL);
    input_enable();
    redraw_screen();
    return 1;
}

/* USER.400, called by KERNEL once the shell is loaded, before it runs */
void WINAPI FinalUserInit(void)
{
    diag("INIT=Final USER\r\n");
    G_FINALGDIINIT(hbrGray);
    booted = 1;
    diag("INITDONE=Final USER\r\n");
    diag("INIT=Installable Drivers\r\n");
    {
        /* SYSTEM.INI [boot] drivers=: loaded as libraries; UNVERIFIED that
           this is all 3.1 does (it sends them DRV_LOAD; US1 saw MMSYSTEM's
           DriverProc call DefDriverProc for message 1) */
        static char list[128];
        char *p, *q;
        GetPrivateProfileString("boot", "drivers", "", list, sizeof list, "SYSTEM.INI");
        for (p = list; *p;) {
            while (*p == ' ' || *p == ',')
                p++;
            for (q = p; *q && *q != ' ' && *q != ','; q++)
                ;
            if (q == p)
                break;
            if (*q)
                *q++ = 0;
            LoadLibrary(p);
            p = q;
        }
    }
    diag("INITDONE=Installable Drivers\r\n");
}

int WINAPI GetSystemMetrics(int i)
{
    ulog("GetSystemMetrics\r\n");  /* TRACE */
    return i >= 0 && i < SM_CMETRICS + 8 ? sysmet[i] : 0;
}

DWORD WINAPI GetSysColor(int i)
{
    return i >= 0 && i <= COLOR_BTNHIGHLIGHT ? syscolor[i] : 0;
}

void WINAPI SetSysColors(int n, const int __far *idx, const COLORREF __far *rgb)
{
    int i;
    for (i = 0; i < n; i++)
        if (idx[i] >= 0 && idx[i] <= COLOR_BTNHIGHLIGHT)
            syscolor[idx[i]] = rgb[i];
    make_sysbrushes();
    SendMessage(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
    redraw_screen();
}

DWORD WINAPI GetDialogBaseUnits(void)
{
    return MAKELONG(cxSysChar, cySysChar);
}

UINT WINAPI GetFreeSystemResources(UINT what)
{
    (void)what;
    return 90;                  /* UNVERIFIED: a percentage; w16's heaps grow */
}

int WINAPI WEP(int n)
{
    (void)n;
    return 1;
}
