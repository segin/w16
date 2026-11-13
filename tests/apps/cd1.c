/*
 * cd1.c - CD1, the common dialogs probe (black-box observation of
 * COMMDLG.DLL; ARCHITECTURE.md 17).
 *
 * Started by SNAP ("run C:\CD1.EXE"), it shows a small owner window and
 * executes C:\CD1.TXT, one call per line, logging each call's result and
 * the returned structure to C:\CD1.LOG. SNAP meanwhile presses keys in
 * the dialogs and takes screenshots. Lines:
 *   open  k=v ...     GetOpenFileName
 *   save  k=v ...     GetSaveFileName
 *   color k=v ...     ChooseColor
 *   font  k=v ...     ChooseFont
 *   print k=v ...     PrintDlg
 *   find  k=v ...     FindText    (modeless: runs until the dialog closes)
 *   replace k=v ...   ReplaceText
 *   title <path> <n>  GetFileTitle into an n-byte buffer
 *   err               CommDlgExtendedError
 *   wait <ms>         dispatch messages for ms milliseconds
 *   cd <dir>          change the current drive and directory
 *   pwd               log the current directory
 *   attr <path> <hex> set a file's DOS attributes; mkfile <path>: create it
 * Values: '~' stands for a space, '|' for a NUL (filters), "@" alone for
 * a NULL pointer. Keys:
 *   flags=hex  owner=0|1  hook=hex  custdata=hex  size=n (lStructSize)
 *   open/save: filter= custom=n (buffer, initial text cfilt=) index= file=
 *     max= ftitle=n (lpstrFileTitle buffer) dir= title= defext=
 *   color: rgb=hex cust=hex,hex,...
 *   font: face= height= weight= italic= pt= rgb= style= min= max= dc=0|1
 *     (dc=1: a printer DC, CF_PRINTERFONTS)
 *   print: from= to= min= max= copies= keep=1 (pass the previous call's
 *     hDevMode/hDevNames back in)
 *   find/replace: what= with= wlen= rlen=
 * hook bits: 1 log the hook's messages, 2 refuse the first FILEOKSTRING
 * (hook returns 1), 4 hook returns TRUE from WM_INITDIALOG, 8 log every
 * message (not only the interesting ones), 10 half a second after
 * WM_INITDIALOG log which window procedures the dialog and its controls
 * have (by COMMDLG export name, or the control's class name).
 * Only documented APIs are used.
 */
#include <windows.h>
#include <commdlg.h>
#include <drivinit.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE inst;
static HWND owner;
static HFILE logf = HFILE_ERROR;
static char line[1024];
static char script[8192];

/* ---- log ---- */

/* opened and closed for every write: the log survives Windows ending
   while a dialog is still up */
static BOOL log_made;

static void out(LPCSTR s)
{
    OFSTRUCT of;
    logf = OpenFile("C:\\CD1.LOG", &of, log_made ? OF_WRITE : OF_CREATE | OF_WRITE);
    if (logf == HFILE_ERROR)
        return;
    log_made = TRUE;
    _llseek(logf, 0, 2);
    _lwrite(logf, s, lstrlen(s));
    _lclose(logf);
    logf = HFILE_ERROR;
}

static void outf(LPCSTR fmt, ...)
{
    char b[512];
    wvsprintf(b, fmt, (LPSTR)(&fmt + 1));
    out(b);
}

/* a string with NULs shown as '|', up to a double NUL (or n bytes) */
static void outmulti(LPCSTR s, WORD n)
{
    char b[600];
    WORD i = 0;
    if (!s) {
        out("@");
        return;
    }
    out("\"");
    while (i < n && i < sizeof b - 2 && !(s[i] == 0 && (i + 1 >= n || s[i + 1] == 0))) {
        b[i] = s[i] ? s[i] : '|';
        i++;
    }
    b[i] = 0;
    out(b);
    out("\"");
}

static void outstr(LPCSTR s)
{
    if (!s) {
        out("@");
        return;
    }
    out("\"");
    out(s);
    out("\"");
}

/* ---- parsing ---- */

/* compare n characters (no C runtime) */
static int _fstrncmp_local(const char *a, const char *b, int n)
{
    while (n--) {
        if (*a != *b)
            return 1;
        if (!*a)
            return 0;
        a++;
        b++;
    }
    return 0;
}

static char *val_of(char *args, const char *key)
{
    char *p = args;
    int n = lstrlen(key);
    while (*p) {
        while (*p == ' ')
            p++;
        if (!_fstrncmp_local(p, key, n) && p[n] == '=')
            return p + n + 1;
        while (*p && *p != ' ')
            p++;
    }
    return NULL;
}


static DWORD num(const char *s, int base)
{
    DWORD v = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            return v;
        /* shifts: no 32-bit multiply helper without a C runtime */
        v = (base == 16 ? v << 4 : (v << 3) + (v << 1)) + d;
    }
}

static DWORD hexv(char *args, const char *key, DWORD def)
{
    char *v = val_of(args, key);
    return v ? num(v, 16) : def;
}

static int decv(char *args, const char *key, int def)
{
    char *v = val_of(args, key);
    if (!v)
        return def;
    if (*v == '-')
        return -(int)num(v + 1, 10);
    return (int)num(v, 10);
}

/* a value into a new buffer of at least `size` bytes ('~' space, '|' NUL);
   NULL when absent or "@" */
static LPSTR strv(char *args, const char *key, WORD size)
{
    char *v = val_of(args, key);
    WORD n = 0, i;
    LPSTR r;
    if (!v || (v[0] == '@' && (v[1] == ' ' || !v[1])))
        return NULL;
    while (v[n] && v[n] != ' ')
        n++;
    if (size < n + 2)
        size = n + 2;
    r = (LPSTR)GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size));
    for (i = 0; i < n; i++)
        r[i] = v[i] == '~' ? ' ' : v[i] == '|' ? 0 : v[i];
    r[n] = r[n + 1] = 0;
    return r;
}

static LPSTR buf(WORD size)
{
    return (LPSTR)GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size));
}

/* ---- hooks ---- */

static UINT m_fileok, m_lbselch, m_sharevi, m_help, m_colorok, m_setrgb, m_find;
static WORD hookbits;
static int refused;

static BOOL interesting(UINT msg)
{
    if (hookbits & 8)
        return msg != WM_ENTERIDLE && msg != WM_SETCURSOR && msg != WM_MOUSEMOVE &&
               msg != WM_NCHITTEST && msg != WM_NCMOUSEMOVE;
    return msg == WM_INITDIALOG || msg == WM_COMMAND || msg == WM_DESTROY ||
           msg == WM_CLOSE || msg >= 0xC000;
}

static LPCSTR msgname(UINT msg)
{
    if (msg == m_fileok) return "FILEOK";
    if (msg == m_lbselch) return "LBSELCH";
    if (msg == m_sharevi) return "SHAREVI";
    if (msg == m_help) return "HELP";
    if (msg == m_colorok) return "COLOROK";
    if (msg == m_setrgb) return "SETRGB";
    if (msg == m_find) return "FINDMSG";
    switch (msg) {
    case WM_INITDIALOG: return "WM_INITDIALOG";
    case WM_COMMAND: return "WM_COMMAND";
    case WM_DESTROY: return "WM_DESTROY";
    case WM_CLOSE: return "WM_CLOSE";
    }
    return "";
}

static LPVOID cur_struct;

BOOL CALLBACK __export CdHook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg;
    if ((hookbits & 1) && interesting(msg)) {
        outf("  hook %04X %s wp=%04X lp=%08lX", msg, msgname(msg), wp, lp);
        if (msg == WM_INITDIALOG)
            outf(" lp==struct:%d", (LPVOID)lp == cur_struct);
        if (msg == m_fileok || msg == m_sharevi || msg == m_lbselch)
            outf(" lp==struct:%d", (LPVOID)lp == cur_struct);
        if (msg == m_fileok)
            outf(" file=\"%s\"", ((LPOPENFILENAME)lp)->lpstrFile);
        out("\r\n");
    }
    if (msg == WM_INITDIALOG && (hookbits & 0x10))
        PostMessage(owner, WM_USER + 1, (WPARAM)dlg, 0);
    if (msg == WM_INITDIALOG && (hookbits & 4))
        return TRUE;
    if (msg == m_fileok && (hookbits & 2) && !refused) {
        refused = 1;
        return TRUE;
    }
    return FALSE;
}

static FARPROC hookproc(void)
{
    static FARPROC p;
    if (!p)
        p = MakeProcInstance((FARPROC)CdHook, inst);
    return p;
}

/* PrintDlg with hook bit 20: the Print Setup hook is this one ("shook") */
BOOL CALLBACK __export CdSetupHook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg;
    if (interesting(msg))
        outf("  shook %04X %s wp=%04X lp=%08lX\r\n", msg, msgname(msg), wp, lp);
    return FALSE;
}

/* ---- calls ---- */

static void file_call(char *a, BOOL save)
{
    OPENFILENAME o;
    BOOL r;
    WORD i, maxf = decv(a, "max", 256), cust = decv(a, "custom", 0),
         ftitle = decv(a, "ftitle", 0);
    BYTE FAR *z = (BYTE FAR *)&o;
    for (i = 0; i < sizeof o; i++)
        z[i] = 0;
    o.lStructSize = decv(a, "size", sizeof o);
    o.hwndOwner = decv(a, "owner", 1) ? owner : NULL;
    o.hInstance = inst;
    o.lpstrFilter = strv(a, "filter", 0);
    if (cust) {
        o.lpstrCustomFilter = strv(a, "cfilt", cust);
        if (!o.lpstrCustomFilter)
            o.lpstrCustomFilter = buf(cust);
        o.nMaxCustFilter = cust;
    }
    o.nFilterIndex = decv(a, "index", 0);
    o.lpstrFile = strv(a, "file", maxf);
    if (!o.lpstrFile && !val_of(a, "file"))
        o.lpstrFile = buf(maxf ? maxf : 1);
    o.nMaxFile = maxf;
    if (ftitle) {
        o.lpstrFileTitle = buf(ftitle);
        o.nMaxFileTitle = ftitle;
    }
    o.lpstrInitialDir = strv(a, "dir", 0);
    o.lpstrTitle = strv(a, "title", 0);
    o.Flags = hexv(a, "flags", 0);
    o.lpstrDefExt = strv(a, "defext", 0);
    o.lCustData = hexv(a, "custdata", 0x12345678L);
    hookbits = (WORD)hexv(a, "hook", 0);
    refused = 0;
    if (hookbits)
        o.lpfnHook = (LPOFNHOOKPROC)hookproc();
    cur_struct = &o;
    r = save ? GetSaveFileName(&o) : GetOpenFileName(&o);
    outf("  ret=%d err=%04lX file=", r, CommDlgExtendedError());
    if (o.lpstrFile)
        outmulti(o.lpstrFile, (o.Flags & OFN_ALLOWMULTISELECT) ? maxf : lstrlen(o.lpstrFile) + 1);
    else
        out("@");
    outf(" off=%u ext=%u idx=%lu flags=%08lX ftitle=", o.nFileOffset, o.nFileExtension,
         o.nFilterIndex, o.Flags);
    outstr(o.lpstrFileTitle);
    out(" cust=");
    if (o.lpstrCustomFilter)
        outmulti(o.lpstrCustomFilter, cust);
    else
        out("@");
    out("\r\n");
}

static void color_call(char *a)
{
    CHOOSECOLOR c;
    static COLORREF cust[16];
    BOOL r;
    char *v = val_of(a, "cust");
    int i;
    BYTE FAR *z = (BYTE FAR *)&c;
    for (i = 0; i < sizeof c; i++)
        z[i] = 0;
    for (i = 0; i < 16; i++)
        cust[i] = RGB(255, 255, 255);
    for (i = 0; v && i < 16; i++) {
        cust[i] = num(v, 16);
        while (*v && *v != ',' && *v != ' ')
            v++;
        if (*v != ',')
            break;
        v++;
    }
    c.lStructSize = decv(a, "size", sizeof c);
    c.hwndOwner = decv(a, "owner", 1) ? owner : NULL;
    c.hInstance = (HWND)inst;
    c.rgbResult = hexv(a, "rgb", 0);
    c.lpCustColors = cust;
    c.Flags = hexv(a, "flags", 0);
    c.lCustData = hexv(a, "custdata", 0x12345678L);
    hookbits = (WORD)hexv(a, "hook", 0);
    if (hookbits)
        c.lpfnHook = (LPCCHOOKPROC)hookproc();
    cur_struct = &c;
    r = ChooseColor(&c);
    outf("  ret=%d err=%04lX rgb=%08lX flags=%08lX cust=", r, CommDlgExtendedError(),
         c.rgbResult, c.Flags);
    for (i = 0; i < 16; i++)
        outf(i ? ",%06lX" : "%06lX", cust[i]);
    out("\r\n");
}

static void font_call(char *a)
{
    CHOOSEFONT c;
    LOGFONT lf;
    BOOL r;
    LPSTR face = strv(a, "face", 0), style = strv(a, "style", 64);
    int i;
    BYTE FAR *z = (BYTE FAR *)&c;
    for (i = 0; i < sizeof c; i++)
        z[i] = 0;
    z = (BYTE FAR *)&lf;
    for (i = 0; i < sizeof lf; i++)
        z[i] = 0;
    if (face)
        lstrcpy(lf.lfFaceName, face);
    lf.lfHeight = decv(a, "height", 0);
    lf.lfWeight = decv(a, "weight", 0);
    lf.lfItalic = (BYTE)decv(a, "italic", 0);
    lf.lfUnderline = (BYTE)decv(a, "under", 0);
    lf.lfStrikeOut = (BYTE)decv(a, "strike", 0);
    c.lStructSize = decv(a, "size", sizeof c);
    c.hwndOwner = decv(a, "owner", 1) ? owner : NULL;
    if (decv(a, "dc", 0)) {
        char dev[80], *drv, *port, *p;
        GetProfileString("windows", "device", "", dev, sizeof dev);
        for (p = dev; *p && *p != ','; p++)
            ;
        *p++ = 0;
        drv = p;
        for (; *p && *p != ','; p++)
            ;
        *p++ = 0;
        port = p;
        c.hDC = CreateDC(drv, dev, port, NULL);
        outf("  printer dc=%d (%s,%s,%s)\r\n", c.hDC != 0, (LPSTR)dev, (LPSTR)drv, (LPSTR)port);
    }
    c.lpLogFont = &lf;
    c.iPointSize = decv(a, "pt", 0);
    c.Flags = hexv(a, "flags", CF_SCREENFONTS);
    c.rgbColors = hexv(a, "rgb", 0);
    c.lCustData = hexv(a, "custdata", 0x12345678L);
    c.hInstance = inst;
    c.lpszStyle = style ? style : buf(64);
    c.nSizeMin = decv(a, "min", 0);
    c.nSizeMax = decv(a, "max", 0);
    hookbits = (WORD)hexv(a, "hook", 0);
    if (hookbits)
        c.lpfnHook = (LPCFHOOKPROC)hookproc();
    cur_struct = &c;
    r = ChooseFont(&c);
    outf("  ret=%d err=%04lX pt=%d flags=%08lX rgb=%08lX type=%04X style=\"%s\"\r\n",
         r, CommDlgExtendedError(), c.iPointSize, c.Flags, c.rgbColors, c.nFontType,
         c.lpszStyle);
    outf("  lf h=%d w=%d esc=%d ori=%d wt=%d it=%d ul=%d so=%d cs=%d op=%d cp=%d q=%d paf=%02X face=\"%s\"\r\n",
         lf.lfHeight, lf.lfWidth, lf.lfEscapement, lf.lfOrientation, lf.lfWeight,
         lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfCharSet, lf.lfOutPrecision,
         lf.lfClipPrecision, lf.lfQuality, lf.lfPitchAndFamily, (LPSTR)lf.lfFaceName);
    if (decv(a, "tm", 0) && r) {        /* tm=1: the chosen font's metrics */
        HDC sdc = GetDC(NULL);
        HFONT hf = CreateFontIndirect(&lf), of = SelectObject(sdc, hf);
        TEXTMETRIC tm;
        GetTextMetrics(sdc, &tm);
        outf("  tm h=%d asc=%d desc=%d il=%d el=%d\r\n", tm.tmHeight, tm.tmAscent,
             tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading);
        SelectObject(sdc, of);
        DeleteObject(hf);
        ReleaseDC(NULL, sdc);
    }
    if (c.hDC)
        DeleteDC(c.hDC);
}

static HGLOBAL keep_mode, keep_names;

static void print_call(char *a)
{
    PRINTDLG p;
    BOOL r;
    int i;
    BYTE FAR *z = (BYTE FAR *)&p;
    for (i = 0; i < sizeof p; i++)
        z[i] = 0;
    p.lStructSize = decv(a, "size", sizeof p);
    p.hwndOwner = decv(a, "owner", 1) ? owner : NULL;
    if (decv(a, "keep", 0)) {
        p.hDevMode = keep_mode;
        p.hDevNames = keep_names;
    }
    p.Flags = hexv(a, "flags", 0);
    p.nFromPage = decv(a, "from", 0);
    p.nToPage = decv(a, "to", 0);
    p.nMinPage = decv(a, "min", 0);
    p.nMaxPage = decv(a, "max", 0);
    p.nCopies = decv(a, "copies", 0);
    p.hInstance = inst;
    p.lCustData = hexv(a, "custdata", 0x12345678L);
    hookbits = (WORD)hexv(a, "hook", 0);
    if (hookbits) {
        p.lpfnPrintHook = (LPPRINTHOOKPROC)hookproc();
        p.lpfnSetupHook = (LPSETUPHOOKPROC)hookproc();
        if (hookbits & 0x20)
            p.lpfnSetupHook = (LPSETUPHOOKPROC)MakeProcInstance((FARPROC)CdSetupHook, inst);
    }
    cur_struct = &p;
    r = PrintDlg(&p);
    outf("  ret=%d err=%04lX flags=%08lX from=%u to=%u min=%u max=%u copies=%u dc=%d mode=%d names=%d\r\n",
         r, CommDlgExtendedError(), p.Flags, p.nFromPage, p.nToPage, p.nMinPage,
         p.nMaxPage, p.nCopies, p.hDC != 0, p.hDevMode != 0, p.hDevNames != 0);
    if (p.hDevNames) {
        LPDEVNAMES n = (LPDEVNAMES)GlobalLock(p.hDevNames);
        outf("  devnames size=%lu off=%u,%u,%u default=%04X \"%s\" \"%s\" \"%s\"\r\n",
             GlobalSize(p.hDevNames), n->wDriverOffset, n->wDeviceOffset, n->wOutputOffset,
             n->wDefault, (LPSTR)n + n->wDriverOffset, (LPSTR)n + n->wDeviceOffset,
             (LPSTR)n + n->wOutputOffset);
        GlobalUnlock(p.hDevNames);
    }
    if (p.hDevMode) {
        DEVMODE FAR *m = (DEVMODE FAR *)GlobalLock(p.hDevMode);
        /* fields the driver does not claim (dmFields) hold whatever
           happened to be in its memory: shown as "-" */
#define F(bit, v) ((m->dmFields & (bit)) ? (int)(v) : (int)0x8000)
        outf("  devmode size=%lu name=\"%s\" spec=%04X drvver=%04X size=%u extra=%u fields=%08lX orient=%d paper=%d len=%d wid=%d scale=%d copies=%d src=%d qual=%d color=%d duplex=%d yres=%d tt=%d\r\n",
             GlobalSize(p.hDevMode), (LPSTR)m->dmDeviceName, m->dmSpecVersion,
             m->dmDriverVersion, m->dmSize, m->dmDriverExtra, m->dmFields,
             F(0x1, m->dmOrientation), F(0x2, m->dmPaperSize), F(0x4, m->dmPaperLength),
             F(0x8, m->dmPaperWidth), F(0x10, m->dmScale), F(0x100, m->dmCopies),
             F(0x200, m->dmDefaultSource), F(0x400, m->dmPrintQuality),
             F(0x800, m->dmColor), F(0x1000, m->dmDuplex), F(0x2000, m->dmYResolution),
             F(0x4000, m->dmTTOption));
#undef F
        GlobalUnlock(p.hDevMode);
    }
    if (p.hDC)
        DeleteDC(p.hDC);
    keep_mode = p.hDevMode;
    keep_names = p.hDevNames;
}

static FINDREPLACE fr;
static HWND frdlg;

static void find_call(char *a, BOOL replace)
{
    MSG m;
    int i;
    BYTE FAR *z = (BYTE FAR *)&fr;
    WORD wl = decv(a, "wlen", 80), rl = decv(a, "rlen", 80);
    for (i = 0; i < sizeof fr; i++)
        z[i] = 0;
    fr.lStructSize = decv(a, "size", sizeof fr);
    fr.hwndOwner = decv(a, "owner", 1) ? owner : NULL;
    fr.hInstance = inst;
    fr.Flags = hexv(a, "flags", 0);
    fr.lpstrFindWhat = strv(a, "what", wl);
    if (!fr.lpstrFindWhat)
        fr.lpstrFindWhat = buf(wl ? wl : 1);
    fr.lpstrReplaceWith = strv(a, "with", rl);
    if (!fr.lpstrReplaceWith && replace)
        fr.lpstrReplaceWith = buf(rl ? rl : 1);
    fr.wFindWhatLen = wl;
    fr.wReplaceWithLen = rl;
    fr.lCustData = hexv(a, "custdata", 0x12345678L);
    hookbits = (WORD)hexv(a, "hook", 0);
    if (hookbits)
        fr.lpfnHook = (LPFRHOOKPROC)hookproc();
    cur_struct = &fr;
    frdlg = replace ? ReplaceText(&fr) : FindText(&fr);
    outf("  hwnd=%d err=%04lX\r\n", frdlg != 0, CommDlgExtendedError());
    if (!frdlg)
        return;
    while (frdlg && IsWindow(frdlg) && GetMessage(&m, NULL, 0, 0)) {
        if (!IsDialogMessage(frdlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    frdlg = NULL;
}

/* which COMMDLG export, or which class's procedure, `proc` is */
static void procname(FARPROC proc)
{
    static const char *names[] = { "FILEOPENDLGPROC", "FILESAVEDLGPROC", "COLORDLGPROC",
        "FINDTEXTDLGPROC", "REPLACETEXTDLGPROC", "FORMATCHARDLGPROC", "PRINTDLGPROC",
        "PRINTSETUPDLGPROC", "EDITINTEGERONLY", "WANTARROWS", "DWLBSUBCLASS",
        "DWUPARROWHACK", "DWOKSUBCLASS", "FONTSTYLEENUMPROC", "FONTFAMILYENUMPROC", 0 };
    static const char *classes[] = { "Button", "Edit", "Static", "ListBox", "ComboBox",
        "ScrollBar", 0 };
    HINSTANCE cd = GetModuleHandle("COMMDLG");
    int i;
    WNDCLASS wc;
    for (i = 0; names[i]; i++)
        if (GetProcAddress(cd, names[i]) == proc) {
            out(names[i]);
            return;
        }
    for (i = 0; classes[i]; i++)
        if (GetClassInfo(NULL, classes[i], &wc) && (FARPROC)wc.lpfnWndProc == proc) {
            outf("class %s", (LPSTR)classes[i]);
            return;
        }
    if (proc == hookproc())
        out("the hook");
    else
        outf("%04X:%04X", SELECTOROF(proc), OFFSETOF(proc));
}

static void procs(HWND dlg)
{
    HWND c;
    char cls[32];
    out("  procs: dialog wndproc ");
    procname((FARPROC)GetWindowLong(dlg, GWL_WNDPROC));
    out(", dlgproc ");
    procname((FARPROC)GetWindowLong(dlg, DWL_DLGPROC));
    out("\r\n");
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        GetClassName(c, cls, sizeof cls);
        outf("    %04X %s: ", GetWindowWord(c, GWW_ID), (LPSTR)cls);
        procname((FARPROC)GetWindowLong(c, GWL_WNDPROC));
        out("\r\n");
    }
}

LRESULT CALLBACK __export Cd1WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER + 1) {
        DWORD end = GetTickCount() + 500;
        MSG m;
        while ((long)(GetTickCount() - end) < 0 && IsWindow((HWND)wp)) {
            if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
        }
        if (IsWindow((HWND)wp))
            procs((HWND)wp);
        return 0;
    }
    if (msg && msg == m_find) {
        LPFINDREPLACE f = (LPFINDREPLACE)lp;
        outf("  findmsg lp==struct:%d flags=%08lX what=", f == &fr, f->Flags);
        outstr(f->lpstrFindWhat);
        out(" with=");
        outstr(f->lpstrReplaceWith);
        out("\r\n");
        if (f->Flags & FR_DIALOGTERM)
            frdlg = NULL;
        return 0;
    }
    if (msg && msg == m_help) {
        outf("  owner help wp=%04X lp=%08lX\r\n", wp, lp);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static void title_call(char *a)
{
    char path[200], *p = path, b[200];
    int n, r;
    while (*a == ' ')
        a++;
    while (*a && *a != ' ')
        *p++ = *a++;
    *p = 0;
    n = (int)num(a + 1, 10);
    lstrcpy(b, "########");
    r = GetFileTitle(path[0] == '@' && !path[1] ? NULL : path, n ? b : NULL, n);
    outf("  ret=%d buf=\"%s\"\r\n", r, n ? (LPSTR)b : (LPSTR)"");
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    } while ((long)(GetTickCount() - end) < 0);
}

static int is(const char *l, const char *w)
{
    int n = lstrlen(w);
    return !_fstrncmp_local(l, w, n) && (l[n] == ' ' || !l[n]);
}

static void run(char *l)
{
    char *a = l;
    while (*a && *a != ' ')
        a++;
    out("CD1 ");
    out(l);
    out("\r\n");
    if (is(l, "open"))
        file_call(a, FALSE);
    else if (is(l, "save"))
        file_call(a, TRUE);
    else if (is(l, "color"))
        color_call(a);
    else if (is(l, "font"))
        font_call(a);
    else if (is(l, "print"))
        print_call(a);
    else if (is(l, "find"))
        find_call(a, FALSE);
    else if (is(l, "replace"))
        find_call(a, TRUE);
    else if (is(l, "title"))
        title_call(a);
    else if (is(l, "err"))
        outf("  err=%04lX\r\n", CommDlgExtendedError());
    else if (is(l, "wait"))
        pump(num(a + 1, 10));
    else if (is(l, "attr")) {           /* attr <path> <hex>: set DOS attributes */
        char d[128];
        LPSTR dp = d;
        WORD at, i = 0, ok = 0;
        a++;
        while (*a && *a != ' ' && i < 127)
            d[i++] = *a++;
        d[i] = 0;
        at = (WORD)num(a + 1, 16);
        _asm {
            push ds
            mov cx, at
            lds dx, dp
            mov ax, 4301h
            int 21h
            sbb ax, ax
            pop ds
            mov ok, ax
        }
        outf("  attr=%d\r\n", ok == 0);
    } else if (is(l, "mkfile")) {       /* mkfile <path>: an empty file */
        char d[128];
        int i = 0;
        HFILE h;
        a++;
        while (*a && *a != ' ' && i < 127)
            d[i++] = *a++;
        d[i] = 0;
        h = _lcreat(d, 0);
        if (h != HFILE_ERROR)
            _lclose(h);
        outf("  made=%d\r\n", h != HFILE_ERROR);
    } else if (is(l, "pwd")) {
        char d[80];
        LPSTR dp = d + 3;
        BYTE drv = 0;
        _asm {
            mov ah, 19h
            int 21h
            mov drv, al
            push ds
            push si
            lds si, dp
            mov dl, 0
            mov ah, 47h
            int 21h
            pop si
            pop ds
        }
        d[0] = (char)('A' + drv);
        d[1] = ':';
        d[2] = '\\';
        outf("  cwd=%s\r\n", (LPSTR)d);
    } else if (is(l, "cd")) {
        char d[128];
        int i = 0;
        a++;
        while (*a && *a != ' ' && i < 127)
            d[i++] = *a++;
        d[i] = 0;
        if (d[1] == ':') {
            _asm {
                mov dl, byte ptr d
                or dl, 20h
                sub dl, 'a'
                mov ah, 0Eh
                int 21h
            }
        }
        {
            LPSTR dp = d;
            WORD ok = 0;
            _asm {
                push ds
                lds dx, dp
                mov ah, 3Bh
                int 21h
                sbb ax, ax
                pop ds
                mov ok, ax
            }
            outf("  chdir=%d\r\n", ok == 0);
        }
    } else
        out("  unknown\r\n");
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HFILE f;
    int n;
    char *p, *e;

    WaitEvent(0);
    inst = (HINSTANCE)init_regs[5];
    InitApp(inst);
    m_fileok = RegisterWindowMessage("commdlg_FileNameOK");
    m_lbselch = RegisterWindowMessage("commdlg_LBSelChangedNotify");
    m_sharevi = RegisterWindowMessage("commdlg_ShareViolation");
    m_help = RegisterWindowMessage("commdlg_help");
    m_colorok = RegisterWindowMessage("commdlg_ColorOK");
    m_setrgb = RegisterWindowMessage("commdlg_SetRGBColor");
    m_find = RegisterWindowMessage("commdlg_FindReplace");
    wc.style = 0;
    wc.lpfnWndProc = Cd1WndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "CD1";
    RegisterClass(&wc);
    owner = CreateWindow("CD1", "CD1", WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE,
                         0, 0, 200, 60, NULL, NULL, inst, NULL);
    UpdateWindow(owner);
    f = _lopen("C:\\CD1.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        out("CD1: no C:\\CD1.TXT\r\n");
        return 1;
    }
    n = _lread(f, script, sizeof script - 1);
    _lclose(f);
    script[n < 0 ? 0 : n] = 0;
    for (p = script; *p; p = e) {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        if (*p && *p != ';') {
            lstrcpy(line, p);
            run(line);
        }
    }
    out("CD1 done\r\n");
    DestroyWindow(owner);
    return 0;
}
