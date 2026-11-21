/*
 * ports.c - the Ports applet: the four serial ports, their settings (WIN.INI
 * [ports]) and their advanced settings (SYSTEM.INI [386Enh]).
 *
 * Observed on Windows 3.1 (black box: SNAP window dumps, screenshots and
 * the INI files written; docs/notes/cpl-fonts-ports-handover.md):
 * - The Ports dialog: four owner-drawn buttons (the applet's icon, centred)
 *   with "COM1:" .. "COM4:" below; the current port's icon has a frame of
 *   COLOR_HIGHLIGHT 2 pixels out. COM1 is current at first. The default
 *   button (IDOK) reads "Cancel" until a Settings dialog is closed with
 *   OK, then "Close".
 * - Settings for COMn: Baud Rate (an editable list: 110 ... 19200, other
 *   values typed are kept), Data Bits 4-8, Parity Even/Odd/None/Mark/Space,
 *   Stop Bits 1/1.5/2, Flow Control Xon / Xoff, Hardware, None. OK writes
 *   WIN.INI [ports] "COMn:=baud,parity,data,stop[,x|,p]" (parity e/o/n/m/s;
 *   x for Xon / Xoff, p for Hardware, nothing for None: "57600,s,4,2").
 *   A port without a key shows 9600,n,8,1,x.
 * - Advanced Settings for COMn: Base I/O Port Address (editable: Default,
 *   03F8, 02F8, 03E8, 02E8, 02E0; the port's address shown as text) and
 *   Interrupt Request Line (Default, 15 ... 2). OK writes SYSTEM.INI
 *   [386Enh] COMnBase (the text, unless "Default") and COMnIrq (unless
 *   Default; a Default choice removes the key) and, when they changed,
 *   asks to restart ("System Setting Change"). Offered in Standard mode too.
 * UNVERIFIED: where 3.1 takes a port's address and IRQ from when SYSTEM.INI
 * has none (w16: the standard 3F8/4, 2F8/3, 3E8/4, 2E8/3); what happens to
 * a baud rate typed that the driver does not take.
 */
#include "maincpl.h"
#include "cplutil.h"

static int cur_port;            /* 0..3 */
static HWND ports_dlg;

static char s_ports[] = "ports";
static char s_enh[] = "386Enh";
static char s_sysini[] = "SYSTEM.INI";

static const char *bauds[] = { "110", "300", "600", "1200", "2400", "4800", "9600", "19200" };
static const char *databits[] = { "4", "5", "6", "7", "8" };
static const char *parities[] = { "Even", "Odd", "None", "Mark", "Space" };
static const char parity_ch[] = "eonms";
static const char *stops[] = { "1", "1.5", "2" };
static const char *flows[] = { "Xon / Xoff", "Hardware", "None" };
static const char *bases[] = { "Default", "03F8", "02F8", "03E8", "02E8", "02E0" };
static const char *def_base[] = { "03F8", "02F8", "03E8", "02E8" };
static const int def_irq[] = { 4, 3, 4, 3 };

static void fill(HWND dlg, int id, const char **items, int n)
{
    int i;
    for (i = 0; i < n; i++)
        SendDlgItemMessage(dlg, id, CB_ADDSTRING, 0, (LPARAM)(LPSTR)items[i]);
}

static void select(HWND dlg, int id, const char *s)
{
    if (SendDlgItemMessage(dlg, id, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)s) == CB_ERR)
        SetDlgItemText(dlg, id, s);
}

static int cursel(HWND dlg, int id)
{
    return (int)SendDlgItemMessage(dlg, id, CB_GETCURSEL, 0, 0);
}

/* one field of "a,b,c" (n from 0) into buf; "" when absent */
static void field(const char *s, int n, char *buf, int size)
{
    int k = 0;
    for (; *s && n; s++)
        if (*s == ',')
            n--;
    if (n) {
        *buf = 0;
        return;
    }
    while (*s == ' ')
        s++;
    while (*s && *s != ',' && k < size - 1)
        buf[k++] = *s++;
    while (k && buf[k - 1] == ' ')
        k--;
    buf[k] = 0;
}

/* ---- System Setting Change (also used by the Fonts applet) ---- */

static int restart_text;

BOOL FAR PASCAL __loadds RestartProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char t[256];
        LoadString(g_hinst, restart_text, t, sizeof t);
        SetDlgItemText(dlg, IDC_RS_TEXT, t);
        SendDlgItemMessage(dlg, IDC_RS_ICON, STM_SETICON,
                           (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(dlg, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* "Your ... settings have changed": Restart Now quits and restarts
   Windows (ExitWindows, EW_RESTARTWINDOWS) */
void cpl_restart_prompt(HWND parent, int text)
{
    restart_text = text;
    if (cpl_dialog(parent, DLG_RESTART, RestartProc) == IDOK)
        ExitWindows(EW_RESTARTWINDOWS, 0);
}

/* ---- Advanced Settings ---- */

static char adv_base0[16];
static int adv_irq0;

BOOL FAR PASCAL __loadds AdvancedProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char key[16], t[64], fmt[64];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        int i;
        GetWindowText(dlg, fmt, sizeof fmt);
        wsprintf(t, fmt, cur_port + 1);
        SetWindowText(dlg, t);
        fill(dlg, IDC_PT_BASE, bases, sizeof bases / sizeof bases[0]);
        SendDlgItemMessage(dlg, IDC_PT_IRQ, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Default");
        for (i = 15; i >= 2; i--) {
            wsprintf(t, "%d", i);
            SendDlgItemMessage(dlg, IDC_PT_IRQ, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        }
        wsprintf(key, "COM%dBase", cur_port + 1);
        GetPrivateProfileString(s_enh, key, def_base[cur_port], adv_base0, sizeof adv_base0,
                                s_sysini);
        /* the address as text, no item selected (3.1: Down then picks
           "Default") */
        SetDlgItemText(dlg, IDC_PT_BASE, adv_base0);
        wsprintf(key, "COM%dIrq", cur_port + 1);
        adv_irq0 = GetPrivateProfileInt(s_enh, key, def_irq[cur_port], s_sysini);
        wsprintf(t, "%d", adv_irq0);
        select(dlg, IDC_PT_IRQ, t);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char base[16];
            int irq, changed;
            GetDlgItemText(dlg, IDC_PT_BASE, base, sizeof base);
            wsprintf(key, "COM%dBase", cur_port + 1);
            changed = lstrcmpi(base, adv_base0) != 0;
            WritePrivateProfileString(s_enh, key, lstrcmpi(base, "Default") ? base : NULL,
                                      s_sysini);
            irq = cursel(dlg, IDC_PT_IRQ);
            /* item 1 is IRQ 15, the last IRQ 2 */
            irq = irq > 0 ? 16 - irq : 0;
            changed |= irq != adv_irq0;
            wsprintf(key, "COM%dIrq", cur_port + 1);
            wsprintf(t, "%d", irq);
            WritePrivateProfileString(s_enh, key, irq ? t : NULL, s_sysini);
            if (changed)
                cpl_restart_prompt(dlg, IDS_RS_COM);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_PORTS);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Settings ---- */

BOOL FAR PASCAL __loadds SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char key[8], val[64], t[64], fmt[64];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        int i;
        GetWindowText(dlg, fmt, sizeof fmt);
        wsprintf(t, fmt, cur_port + 1);
        SetWindowText(dlg, t);
        fill(dlg, IDC_PT_BAUD, bauds, sizeof bauds / sizeof bauds[0]);
        fill(dlg, IDC_PT_DATA, databits, sizeof databits / sizeof databits[0]);
        fill(dlg, IDC_PT_PARITY, parities, sizeof parities / sizeof parities[0]);
        fill(dlg, IDC_PT_STOP, stops, sizeof stops / sizeof stops[0]);
        fill(dlg, IDC_PT_FLOW, flows, sizeof flows / sizeof flows[0]);
        wsprintf(key, "COM%d:", cur_port + 1);
        GetProfileString(s_ports, key, "9600,n,8,1,x", val, sizeof val);
        field(val, 0, t, sizeof t);
        select(dlg, IDC_PT_BAUD, t[0] ? t : "9600");
        field(val, 2, t, sizeof t);
        select(dlg, IDC_PT_DATA, t[0] ? t : "8");
        field(val, 1, t, sizeof t);
        AnsiLower(t);
        for (i = 0; i < 5 && parity_ch[i] != t[0]; i++)
            ;
        SendDlgItemMessage(dlg, IDC_PT_PARITY, CB_SETCURSEL, i < 5 ? i : 2, 0);
        field(val, 3, t, sizeof t);
        select(dlg, IDC_PT_STOP, t[0] ? t : "1");
        field(val, 4, t, sizeof t);
        AnsiLower(t);
        SendDlgItemMessage(dlg, IDC_PT_FLOW, CB_SETCURSEL,
                           t[0] == 'x' ? 0 : t[0] == 'p' ? 1 : 2, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char baud[16], data[4], stop[4];
            int p = cursel(dlg, IDC_PT_PARITY), f = cursel(dlg, IDC_PT_FLOW);
            GetDlgItemText(dlg, IDC_PT_BAUD, baud, sizeof baud);
            GetDlgItemText(dlg, IDC_PT_DATA, data, sizeof data);
            GetDlgItemText(dlg, IDC_PT_STOP, stop, sizeof stop);
            wsprintf(val, "%s,%c,%s,%s%s", (LPSTR)baud, parity_ch[p < 0 ? 2 : p],
                     (LPSTR)data, (LPSTR)stop,
                     (LPSTR)(f == 0 ? ",x" : f == 1 ? ",p" : ""));
            wsprintf(key, "COM%d:", cur_port + 1);
            WriteProfileString(s_ports, key, val);
            LoadString(g_hinst, IDS_CLOSE, t, sizeof t);
            SetDlgItemText(ports_dlg, IDOK, t);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_PT_ADVANCED:
            cpl_dialog(dlg, DLG_PORTADV, AdvancedProc);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_PORTS);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Ports ---- */

static void draw_port(LPDRAWITEMSTRUCT d)
{
    RECT r = d->rcItem;
    int x = r.left + (r.right - r.left - 32) / 2, y = r.top + (r.bottom - r.top - 32) / 2;
    HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(d->hDC, &r, b);
    DeleteObject(b);
    DrawIcon(d->hDC, x, y, LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_PORTS)));
    if ((int)d->CtlID - IDC_PT_COM1 == cur_port) {
        SetRect(&r, x - 2, y - 2, x + 34, y + 34);
        b = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FrameRect(d->hDC, &r, b);
        DeleteObject(b);
    }
}

static void set_port(HWND dlg, int p)
{
    if (p == cur_port)
        return;
    InvalidateRect(GetDlgItem(dlg, IDC_PT_COM1 + cur_port), NULL, FALSE);
    cur_port = p;
    InvalidateRect(GetDlgItem(dlg, IDC_PT_COM1 + cur_port), NULL, FALSE);
}

BOOL FAR PASCAL __loadds PortsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        ports_dlg = dlg;
        cur_port = 0;
        return TRUE;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        /* a port button taking the focus (Tab, its mnemonic: 3.1 opens
           that port's settings then) becomes the current port */
        if ((d->itemAction & ODA_FOCUS) && (d->itemState & ODS_FOCUS))
            set_port(dlg, d->CtlID - IDC_PT_COM1);
        draw_port(d);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp >= IDC_PT_COM1 && wp < IDC_PT_COM1 + 4) {
            set_port(dlg, wp - IDC_PT_COM1);
            if (HIWORD(lp) == BN_DOUBLECLICKED)
                cpl_dialog(dlg, DLG_PORTSET, SettingsProc);
            return TRUE;
        }
        switch (wp) {
        case IDOK:
        case IDCANCEL:
            EndDialog(dlg, wp);
            return TRUE;
        case IDC_PT_SETTINGS:
            cpl_dialog(dlg, DLG_PORTSET, SettingsProc);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_PORTS);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ports_applet(HWND parent)
{
    cpl_dialog(parent, DLG_PORTS, PortsProc);
}
