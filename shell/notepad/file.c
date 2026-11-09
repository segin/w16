/*
 * file.c - w16 Notepad: New, Open, Save, Save As, the command line's
 * file, dropped files, .LOG and Time/Date.
 *
 * Observed on Windows 3.1 (ARCHITECTURE.md 15.1):
 *   - A file is read whole; NUL bytes show as blanks. It keeps the name it
 *     was given ("C:\LOG.TXT"), which messages quote; the title shows the
 *     file name part in capitals.
 *   - A file named on the command line that does not exist: "Cannot find
 *     the %% file. Do you want to create a new file?" (Yes/No); Yes
 *     creates it empty. Both this and "too large" come before the window
 *     shows; after "too large" Notepad starts untitled.
 *   - "Too large" depends on the text and its line count (with 66-byte
 *     lines 51190 bytes load and 51200 do not; with 1002-byte lines 52000
 *     load; with 10-byte lines 40000 do not): it is the edit control
 *     running out of room in Notepad's data segment, i.e. USER's edit
 *     control limit, not a size Notepad checks. w16's Notepad gives the
 *     edit control a 64K segment of its own, so every file the original
 *     opens opens here too, and somewhat larger ones as well (ZOI).
 *   - Saving writes the text exactly, with no newline added and without
 *     Word Wrap's line breaks. An untitled file asks for a name (COMMDLG
 *     Save As, "Text Files (*.TXT)" / "All Files (*.*)", COMMDLG's own
 *     "already exists" question). Nothing to save: untitled, "You have not
 *     entered any text to be saved..." (OK); a named file, "%%\nThis file
 *     is empty and will be deleted..." (OK/Cancel): OK deletes the file
 *     and Notepad is untitled again.
 *   - Closing, New and Open ask "The text in the %% file has changed..."
 *     (Yes/No/Cancel, exclamation) when the text was changed.
 *   - A file starting ".LOG" gets a blank line and the time and date
 *     appended when opened, the caret after them, the text changed.
 *   - Time/Date (F5) inserts "<time>  <date>" at the caret, from WIN.INI
 *     [intl] (see time_date).
 */
#include "notepad.h"
#ifndef W16_WIN2
#include <commdlg.h>
#include <shellapi.h>
#endif

static BOOL loading;                    /* no EN_ERRSPACE message now */

/* ---- reading and writing the edit control ---- */

static WORD text_length(void)
{
    return (WORD)SendMessage(g_edit, WM_GETTEXTLENGTH, 0, 0);
}

static LPSTR get_text(WORD *len)
{
    WORD n = text_length();
    LPSTR p = mem_alloc((DWORD)n + 1);
    if (p)
        GetWindowText(g_edit, p, n + 1);
    *len = n;
    return p;
}

static void reset_edit(void)
{
    SendMessage(g_edit, EM_SETSEL, 0, 0);
    SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
    SendMessage(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
    sync_scroll();
}

static void set_untitled(void)
{
    g_file[0] = 0;
    set_title();
}

/* ---- asking ---- */

BOOL file_query_save(void)
{
    char untitled[32];
    if (!SendMessage(g_edit, EM_GETMODIFY, 0, 0))
        return TRUE;
    LoadString(g_hinst, IDS_UNTITLED, untitled, sizeof untitled);
    switch (msg_box(g_main, IDS_CHANGED, g_file[0] ? g_file : untitled,
                    MB_YESNOCANCEL | MB_ICONEXCLAMATION)) {
    case IDYES:
        return file_save(FALSE);
    case IDNO:
        return TRUE;
    default:
        return FALSE;
    }
}

void file_new(void)
{
    if (!file_query_save())
        return;
    SetWindowText(g_edit, "");
    reset_edit();
    set_untitled();
}

/* ---- time and date ----
 * Observed (F5 under DOSBox-X with TIME set, WIN.INI [intl] varied):
 *   Setup's [intl] (iTime=0 iTLZero=0 sTime=: s1159=AM s2359=PM iDate=0
 *   sDate=/), 09:05 on 8 Nov 2026: "9:05AM  11/8/26";
 *   iTime=1 iTLZero=1 sTime=. s1159=am, 09:05: "09.05am  11/8/26" (the
 *   suffix stays with iTime=1, and sShortDate=dd.MM.yyyy is not used);
 *   iTLZero=1 s1159= iDate=2: "09:05  26/11/8".
 * So: the hour (leading zero with iTLZero), sTime, two-digit minutes, the
 * s1159/s2359 suffix; two blanks; month, day and two-digit year without
 * leading zeros, in iDate's order (0 M-D-Y, 1 D-M-Y, 2 Y-M-D), separated
 * by sDate. */

static int ini_int(const char *key, int def)
{
    return GetProfileInt("intl", key, def);
}

static char *put_num(char *p, int v, BOOL two)
{
    if (two || v >= 10)
        *p++ = (char)('0' + v / 10 % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* DOS Get Time (CX = hours:minutes) and Get Date (CX = year,
   DX = month:day) */
static WORD dos_hm(void);
#pragma aux dos_hm = "mov ah, 2Ch" "int 21h" value [cx] modify [ax dx];
static WORD dos_year(void);
#pragma aux dos_year = "mov ah, 2Ah" "int 21h" value [cx] modify [ax dx];
static WORD dos_md(void);
#pragma aux dos_md = "mov ah, 2Ah" "int 21h" value [dx] modify [ax cx];

/* The time as [intl] asks for it. UNVERIFIED: the hour with iTime=1 in
   the afternoon (see ARCHITECTURE.md 15.1). */
void time_part(char *p)
{
    char sep[4], am[8], pm[8];
    WORD hm = dos_hm();
    int h = hm >> 8, m = hm & 0xFF, h24 = ini_int("iTime", 0), lz = ini_int("iTLZero", 0);
    GetProfileString("intl", "sTime", ":", sep, sizeof sep);
    GetProfileString("intl", "s1159", "AM", am, sizeof am);
    GetProfileString("intl", "s2359", "PM", pm, sizeof pm);
    p = put_num(p, h24 ? h : (h % 12 ? h % 12 : 12), lz);
    s_cpy(p, sep, 4);
    p += s_len(p);
    p = put_num(p, m, TRUE);
    s_cpy(p, h < 12 ? am : pm, 8);
}

/* The date as [intl] iDate and sDate ask for it. */
void date_part(char *p)
{
    char sep[4];
    WORD md = dos_md();
    int y = dos_year() % 100, mo = md >> 8, d = md & 0xFF, order = ini_int("iDate", 0), k;
    int f[3];
    GetProfileString("intl", "sDate", "/", sep, sizeof sep);
    f[0] = order == 2 ? y : order == 1 ? d : mo;
    f[1] = order == 2 ? mo : order == 1 ? mo : d;
    f[2] = order == 2 ? d : y;
    for (k = 0; k < 3; k++) {
        if (k) {
            s_cpy(p, sep, 4);
            p += s_len(p);
        }
        p = put_num(p, f[k], f[k] == y && k == (order == 2 ? 0 : 2));
    }
    *p = 0;
}

/* "<time>  <date>" */
static void time_date(char *buf)
{
    time_part(buf);
    s_cat(buf, "  ", 64);
    date_part(buf + s_len(buf));
}

void insert_time_date(void)
{
    char buf[64];
    time_date(buf);
    SendMessage(g_edit, EM_REPLACESEL, 0, (LPARAM)(LPSTR)buf);
}

/* ---- loading ---- */

/* Put text into the edit control; FALSE if it does not fit. */
static BOOL set_text(LPSTR text, WORD len)
{
    WORD i;
    BOOL ok;
    for (i = 0; i < len; i++)
        if (!text[i])
            text[i] = ' ';              /* observed: NULs become blanks */
    text[len] = 0;
    loading = TRUE;
    SendMessage(g_edit, WM_SETTEXT, 0, (LPARAM)text);
    ok = text_length() == len;          /* 2.x: WM_SETTEXT returns nothing useful */
    loading = FALSE;
    if (!ok)
        SetWindowText(g_edit, "");
    return ok;
}

static BOOL starts_log(const char FAR *t)
{
    return t[0] == '.' && t[1] == 'L' && t[2] == 'O' && t[3] == 'G';
}

BOOL file_load(const char FAR *path, BOOL ask_create)
{
    OFSTRUCT of;
    HFILE f;
    long size;
    LPSTR buf;
    char name[DOS_PATH];

    s_cpy(name, path, sizeof name);
    f = OpenFile(name, &of, OF_READ | OF_SHARE_DENY_WRITE);
    if (f == HFILE_ERROR)
        f = OpenFile(name, &of, OF_READ);
    if (f == HFILE_ERROR) {
        if (ask_create && OpenFile(name, &of, OF_EXIST) == HFILE_ERROR) {
            if (msg_box(g_main, IDS_CREATENEW, name, MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
                /* observed: a name DOS cannot create ("C:\\A*B.TXT")
                   still becomes the title, without a message */
                f = OpenFile(name, &of, OF_CREATE | OF_WRITE);
                if (f != HFILE_ERROR)
                    _lclose(f);
                SetWindowText(g_edit, "");
                reset_edit();
                s_cpy(g_file, name, sizeof g_file);
                set_title();
                return TRUE;
            }
            return FALSE;
        }
        msg_box(g_main, IDS_CANTOPEN, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    size = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (size < 0 || size > 0xFFFEL) {   /* one edit control holds < 64K */
        _lclose(f);
        msg_box(g_main, IDS_TOOLARGE, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    buf = mem_alloc(size + 1);
    if (!buf) {
        _lclose(f);
        msg_box(g_main, IDS_NOMEMORY, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (_lread(f, buf, (UINT)size) != (UINT)size) {
        _lclose(f);
        mem_free(buf);
        msg_box(g_main, IDS_CANTOPEN, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    _lclose(f);
    if (!set_text(buf, (WORD)size)) {
        mem_free(buf);
        reset_edit();
        set_untitled();
        msg_box(g_main, IDS_TOOLARGE, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    reset_edit();
    s_cpy(g_file, name, sizeof g_file);
    set_title();
    if (starts_log(buf)) {
        char stamp[72];
        WORD end;
        stamp[0] = '\r';
        stamp[1] = '\n';
        time_date(stamp + 2);
        s_cat(stamp, "\r\n", sizeof stamp);
        end = text_length();
        SendMessage(g_edit, EM_SETSEL, 0, MAKELONG(end, end));
        SendMessage(g_edit, EM_REPLACESEL, 0, (LPARAM)(LPSTR)stamp);
        SendMessage(g_edit, EM_SETMODIFY, TRUE, 0);
        sync_scroll();
    }
    mem_free(buf);
    return TRUE;
}

BOOL file_is_loading(void)
{
    return loading;
}

/* ---- the common dialogs ---- */

#ifndef W16_WIN2
/* "Text Files (*.TXT)", "*.txt", "All Files (*.*)", "*.*" */
static int make_filter(char *f, int size)
{
    int n;
    n = LoadString(g_hinst, IDS_FILTERTEXT, f, size) + 1;
    s_cpy(f + n, "*.txt", size - n);
    n += 6;
    n += LoadString(g_hinst, IDS_FILTERALL, f + n, size - n) + 1;
    s_cpy(f + n, "*.*", size - n);
    n += 4;
    f[n] = 0;
    return n;
}

static BOOL file_dialog(char *file, BOOL save)
{
    OPENFILENAME ofn;
    char filter[96], title[24];
    BYTE FAR *z = (BYTE FAR *)&ofn;
    unsigned i;
    BOOL ok;
    for (i = 0; i < sizeof ofn; i++)
        z[i] = 0;
    make_filter(filter, sizeof filter);
    LoadString(g_hinst, save ? IDS_SAVETITLE : IDS_OPENTITLE, title, sizeof title);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = DOS_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "txt";            /* UNVERIFIED */
    ofn.Flags = OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    ok = save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
    return ok;
}
#else
#define file_dialog(file, save)     open_dialog2(file, save)
#endif

void file_open(void)
{
    char file[DOS_PATH];
    if (!file_query_save())
        return;
    file[0] = 0;
    if (file_dialog(file, FALSE))
        file_load(file, FALSE);
}

#ifndef W16_WIN2
void file_drop(HANDLE hdrop)
{
    char file[DOS_PATH];
    DragQueryFile(hdrop, 0, file, sizeof file);
    DragFinish(hdrop);
    if (file_query_save())
        file_load(file, FALSE);
}
#endif

/* ---- saving ---- */

static BOOL write_file(const char *name)
{
    OFSTRUCT of;
    HFILE f;
    WORD len;
    LPSTR text = get_text(&len);
    BOOL ok;
    if (!text) {
        msg_box(g_main, IDS_NOMEMORY, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    f = OpenFile(name, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR) {
        mem_free(text);
        msg_box(g_main, IDS_CANTCREATE, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    ok = _lwrite(f, text, len) == len;
    _lclose(f);
    mem_free(text);
    if (!ok) {
        msg_box(g_main, IDS_DISKFULL, name, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    return TRUE;
}

BOOL file_save(BOOL as)
{
    char file[DOS_PATH];
    if (!text_length()) {
        if (!g_file[0]) {
            msg_box(g_main, IDS_NOTEXT, NULL, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        if (!as) {
            OFSTRUCT of;
            if (msg_box(g_main, IDS_EMPTYDELETE, g_file, MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK)
                return FALSE;
            OpenFile(g_file, &of, OF_DELETE);
            reset_edit();
            set_untitled();
            return TRUE;
        }
    }
    if (as || !g_file[0]) {
        s_cpy(file, g_file, sizeof file);
        if (!file_dialog(file, TRUE))
            return FALSE;
    } else {
        s_cpy(file, g_file, sizeof file);
    }
    if (!write_file(file))
        return FALSE;
    s_cpy(g_file, file, sizeof g_file);
    SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
    set_title();
    return TRUE;
}
