/*
 * sb16.c - w16's SB16.DRV: DriverProc, the configuration, the card's DSP,
 * mixer and the DMA controller, and the work done at interrupt time
 * (drivers/sb16/README.md, ARCHITECTURE.md 24).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 25 and
 * Vol. 2 (installable drivers: DriverProc, the DRV_* messages, DRVCNF_*;
 * GetPrivateProfileString, GetDOSEnvironment, GlobalDOSAlloc), the
 * Multimedia Programmer's Guide (DriverCallback's role, the callback
 * messages). What MMSYSTEM asks of a device driver and what Windows 3.1's
 * own Sound Blaster driver (SNDBLST2.DRV) answers was observed black-box
 * with the logging drivers of tests/mmsystem (ARCHITECTURE.md 23.2, 24.1):
 * message 0 answered 0, DRV_LOAD 1, DRV_ENABLE 0, DRV_OPEN 1 (lParam1 is
 * USER's empty string), DRVM_INIT 8 (MMSYSERR_NOTSUPPORTED) on every
 * entry, callbacks with the client's type and DCB_NOSWITCH. The card:
 * Creative's Sound Blaster Series Hardware Programming Guide (facts only:
 * the DSP's reset, read, write and status ports at base+6, +0Ah, +0Ch,
 * +0Eh, the reset answer AAh, command E1h for the version, the mixer's
 * address and data ports at base+4 and +5, registers 80h (IRQ) and 81h
 * (DMA) of the SB16's mixer, the BLASTER environment setting's letters);
 * the 8237's register map for the DMA controller.
 */
#include "sb16.h"
#include <conio.h>

WORD sb_port = 0x220, sb_irq = 5, sb_dma8 = 1, sb_dma16 = 5, sb_mpu = 0x330;
WORD sb_dspver;
BYTE sb_ok, mpu_ok;
LPSTR dmabuf;
DWORD dmaphys;
static DWORD dosmem;                    /* GlobalDOSAlloc's answer */
static BYTE enabled;
static char inisect[] = "sb16.drv";

/* ---- the card's DSP ---- */
static void iodelay(void)
{
    inp(0x80);                          /* about a microsecond each */
    inp(0x80);
    inp(0x80);
    inp(0x80);
}

int dsp_write(BYTE b)
{
    WORD i;
    for (i = 0; i < 0x8000; i++)
        if (!(inp(sb_port + 0x0C) & 0x80)) {
            outp(sb_port + 0x0C, b);
            return 1;
        }
    return 0;
}

int dsp_read(void)
{
    WORD i;
    for (i = 0; i < 0x8000; i++)
        if (inp(sb_port + 0x0E) & 0x80)
            return inp(sb_port + 0x0A);
    return -1;
}

/* the DSP reset: 1 then 0 at base+6, then AAh to read */
int dsp_reset(void)
{
    WORD i;
    outp(sb_port + 6, 1);
    iodelay();
    iodelay();
    outp(sb_port + 6, 0);
    for (i = 0; i < 2000; i++)
        if ((inp(sb_port + 0x0E) & 0x80) && inp(sb_port + 0x0A) == 0xAA)
            return 1;
    return 0;
}

void mix_write(BYTE reg, BYTE v)
{
    WORD f = sb_cli();
    outp(sb_port + 4, reg);
    outp(sb_port + 5, v);
    sb_sti(f);
}

BYTE mix_read(BYTE reg)
{
    WORD f = sb_cli();
    BYTE v;
    outp(sb_port + 4, reg);
    v = (BYTE)inp(sb_port + 5);
    sb_sti(f);
    return v;
}

/* ---- the DMA controller (8237: channels 0-3 bytes, 5-7 words) ---- */
static const BYTE dmapage[8] = { 0x87, 0x83, 0x81, 0x82, 0, 0x8B, 0x89, 0x8A };

/* channel CH auto-initialised over LEN bytes at PHYS: playback reads
   memory, recording (rec) writes it */
void dma_start(WORD ch, DWORD phys, WORD len, int rec)
{
    WORD f = sb_cli();
    BYTE mode = (BYTE)((rec ? 0x54 : 0x58) | (ch & 3));  /* single, auto-init */
    if (ch < 4) {
        WORD a = (WORD)phys, n = len - 1;
        outp(0x0A, 4 | ch);
        outp(0x0C, 0);
        outp(0x0B, mode);
        outp(ch << 1, a & 0xFF);
        outp(ch << 1, a >> 8);
        outp(dmapage[ch], (BYTE)(phys >> 16));
        outp((ch << 1) + 1, n & 0xFF);
        outp((ch << 1) + 1, n >> 8);
        outp(0x0A, ch);
    } else {
        WORD c = ch & 3, a = (WORD)(phys >> 1), n = (len >> 1) - 1;
        outp(0xD4, 4 | c);
        outp(0xD8, 0);
        outp(0xD6, mode);
        outp(0xC0 + (c << 2), a & 0xFF);
        outp(0xC0 + (c << 2), a >> 8);
        outp(dmapage[ch], (BYTE)(phys >> 16) & 0xFE);
        outp(0xC2 + (c << 2), n & 0xFF);
        outp(0xC2 + (c << 2), n >> 8);
        outp(0xD4, c);
    }
    sb_sti(f);
}

void dma_stop(WORD ch)
{
    if (ch < 4)
        outp(0x0A, 4 | ch);
    else
        outp(0xD4, 4 | (ch & 3));
}

/* the bytes still to go in the channel's current pass */
WORD dma_left(WORD ch)
{
    WORD f = sb_cli(), lo, hi, hi2, n;
    if (ch < 4) {
        WORD port = (ch << 1) + 1;
        outp(0x0C, 0);
        (void)inp(port);
        hi = inp(port);
        do {
            hi2 = hi;
            outp(0x0C, 0);
            lo = inp(port);
            hi = inp(port);
        } while (hi != hi2);
        n = ((hi << 8) | lo) + 1;
    } else {
        WORD port = 0xC2 + ((ch & 3) << 2);
        outp(0xD8, 0);
        (void)inp(port);
        hi = inp(port);
        do {
            hi2 = hi;
            outp(0xD8, 0);
            lo = inp(port);
            hi = inp(port);
        } while (hi != hi2);
        n = (((hi << 8) | lo) + 1) << 1;
    }
    sb_sti(f);
    return n;
}

/* ---- callbacks ---- */
void notify(struct cbinfo *c, WORD msg, DWORD dw1, DWORD dw2)
{
    if (c->flags & 7)
        DRIVERCALLBACK(c->cb, c->flags, c->h, msg, c->inst, dw1, dw2);
}

/* a capabilities structure, cut to the caller's size (SNDBLST2 writes
   exactly the bytes asked for) */
void copycaps(LPVOID dst, const void *src, WORD size, DWORD want)
{
    LPSTR d = (LPSTR)dst;
    const char FAR *s = (const char FAR *)src;
    WORD i;
    if (want < size)
        size = (WORD)want;
    for (i = 0; i < size; i++)
        d[i] = s[i];
}

/* ---- the configuration ---- */
static WORD hexval(const char FAR *s)
{
    WORD v = 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9')
            v = (v << 4) + (*s - '0');
        else if ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'f')
            v = (v << 4) + ((*s | 0x20) - 'a' + 10);
        else
            return v;
    }
}

static WORD decval(const char FAR *s)
{
    WORD v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

/* [sb16.drv] KEY= of SYSTEM.INI: 1 and the value when present */
static int inikey(const char *key, int hex, WORD *v)
{
    char b[16];
    if (!GetPrivateProfileString(inisect, key, "", b, sizeof b, "system.ini") || !b[0])
        return 0;
    *v = hex ? hexval(b) : decval(b);
    return 1;
}

/* the BLASTER setting's letter L: 1 and the value when present
   (A port, I IRQ, D DMA, H high DMA, P MPU-401 port; hex for A and P) */
static int blaster(char l, WORD *v)
{
    LPSTR e = GetDOSEnvironment();
    static const char name[] = "BLASTER=";
    int i;
    if (!e)
        return 0;
    while (*e) {
        for (i = 0; name[i] && (e[i] & 0xDF) == (name[i] & 0xDF); i++)
            ;
        if (!name[i]) {
            e += i;
            while (*e) {
                while (*e == ' ' || *e == '\t')
                    e++;
                if ((*e & 0xDF) == l) {
                    *v = (l == 'A' || l == 'P') ? hexval(e + 1) : decval(e + 1);
                    return 1;
                }
                while (*e && *e != ' ' && *e != '\t')
                    e++;
            }
            return 0;
        }
        while (*e)
            e++;
        e++;
    }
    return 0;
}

static void config(void)
{
    WORD v;
    BYTE r;
    int irqset = 0, dmaset = 0, hdmaset = 0;
    if (inikey("port", 1, &v) || blaster('A', &v))
        sb_port = v;
    if (inikey("mpuport", 1, &v) || blaster('P', &v))
        sb_mpu = v;
    if (inikey("int", 0, &v) || blaster('I', &v))
        sb_irq = v, irqset = 1;
    if (inikey("dma", 0, &v) || blaster('D', &v))
        sb_dma8 = v, dmaset = 1;
    if (inikey("hdma", 0, &v) || blaster('H', &v))
        sb_dma16 = v, hdmaset = 1;
    sb_dspver = 0;
    if (!dsp_reset())
        return;
    if (!dsp_write(0xE1))
        return;
    v = dsp_read() << 8;
    sb_dspver = v | (BYTE)dsp_read();
    if (sb_dspver < 0x400)
        return;
    /* the SB16's mixer says where its IRQ and DMA are (80h: bits 0-3 IRQ
       2, 5, 7, 10; 81h: bits 0, 1, 3 DMA 0, 1, 3, bits 5-7 DMA 5-7) */
    r = mix_read(0x80);
    if (!irqset)
        sb_irq = (r & 2) ? 5 : (r & 4) ? 7 : (r & 1) ? 2 : (r & 8) ? 10 : sb_irq;
    r = mix_read(0x81);
    if (!dmaset)
        sb_dma8 = (r & 2) ? 1 : (r & 1) ? 0 : (r & 8) ? 3 : sb_dma8;
    if (!hdmaset)
        sb_dma16 = (r & 0x20) ? 5 : (r & 0x40) ? 6 : (r & 0x80) ? 7 : sb_dma16;
}

/* the DMA buffer: twice DMABUF of conventional memory, the half of it
   that does not cross a 64 KB boundary (a 16-bit channel's 128 KB ones
   are 64 KB ones too) */
static int dmaalloc(void)
{
    DWORD a, b;
    dosmem = GlobalDOSAlloc(2L * DMABUF);
    if (!dosmem)
        return 0;
    a = (DWORD)HIWORD(dosmem) << 4;
    b = (a + 0xFFFFL) & 0xFFFF0000L;    /* the next boundary */
    if (b > a && b < a + DMABUF)
        dmaphys = b;
    else
        dmaphys = a;
    dmabuf = (LPSTR)MAKELP(LOWORD(dosmem), (WORD)(dmaphys - a));
    return 1;
}

/* 386 Enhanced mode: the card is one VM's at a time, VSBD.386's to give
   (win386/vxd/vsbd.c, ARCHITECTURE.md 24.6): taken (API function 1) for
   the enable and while a wave device is open, given back (2) after, as
   Windows 3.1's SNDBLST2.DRV takes Windows 3.1's VSBD.386's. Without the
   VxD in Enhanced mode no devices: the card is not ours to touch */
static BYTE enh, vsbd, takes;
static void enable_card(void);

int sb_take(void)
{
    if (!enh)
        return 1;
    if (!vsbd)
        return 0;
    if (takes++)
        return 1;
    if (HIWORD(sb_vxd(1, sb_port))) {   /* another VM has it */
        takes = 0;
        return 0;
    }
    return 1;
}

void sb_give(void)
{
    if (!enh || !vsbd || !takes)
        return;
    if (!--takes)
        sb_vxd(2, sb_port);
}

static void enable(void)
{
    if (enabled)
        return;
    enabled = 1;
    enh = (GetWinFlags() & WF_ENHANCED) != 0;
    if (enh) {
        vsbd = sb_vxd_entry() != 0;
        if (!vsbd || !sb_take())
            return;
    }
    enable_card();
    sb_give();
}

static void enable_card(void)
{
    config();
    if (sb_dspver >= 0x400 && sb_dma8 < 4 && sb_dma16 >= 5 && sb_dma16 <= 7 && dmaalloc()) {
        mpu_ok = (BYTE)mpu_reset();
        if (sb_hook(sb_irq)) {
            sb_ok = 1;
            dsp_write(0xD1);            /* speaker on (the DSP's output) */
            vol_init();
            wave_init();
        }
    } else if (sb_dspver)
        mpu_ok = (BYTE)mpu_reset();
}

static void disable(void)
{
    if (enabled && sb_ok && !sb_take()) {
        /* another VM has the card: ours goes without touching it */
        sb_unhook();
        sb_ok = mpu_ok = 0;
    }
    if (!enabled)
        return;
    enabled = 0;
    if (sb_ok) {
        dsp_reset();
        dma_stop(sb_dma8);
        dma_stop(sb_dma16);
        sb_unhook();
        sb_ok = 0;
    }
    if (mpu_ok)
        outp(sb_mpu + 1, 0xFF);         /* the MPU-401 out of UART mode */
    mpu_ok = 0;
    if (dosmem) {
        GlobalDOSFree(LOWORD(dosmem));
        dosmem = 0;
    }
    sb_give();
}

/* ---- the configuration dialog (DRV_CONFIGURE; sb16.rc): the five
   settings as SYSTEM.INI [sb16.drv] has them, the ones in use shown;
   OK writes them and asks for Windows to be restarted ---- */
#define IDC_PORT 101
#define IDC_INT  102
#define IDC_DMA  103
#define IDC_HDMA 104
#define IDC_MPU  105
static const char *const keys[5] = { "port", "int", "dma", "hdma", "mpuport" };

BOOL FAR PASCAL __export __loadds SbSetupProc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    char b[16];
    int i;
    (void)l;
    switch (m) {
    case WM_INITDIALOG:
        wsprintf(b, "%X", sb_port);
        SetDlgItemText(d, IDC_PORT, b);
        SetDlgItemInt(d, IDC_INT, sb_irq, FALSE);
        SetDlgItemInt(d, IDC_DMA, sb_dma8, FALSE);
        SetDlgItemInt(d, IDC_HDMA, sb_dma16, FALSE);
        wsprintf(b, "%X", sb_mpu);
        SetDlgItemText(d, IDC_MPU, b);
        return TRUE;
    case WM_COMMAND:
        if (w == IDOK) {
            for (i = 0; i < 5; i++) {
                GetDlgItemText(d, IDC_PORT + i, b, sizeof b);
                WritePrivateProfileString(inisect, keys[i], b, "system.ini");
            }
            EndDialog(d, DRVCNF_RESTART);
            return TRUE;
        }
        if (w == IDCANCEL) {
            EndDialog(d, DRVCNF_CANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* exported as DRIVERPROC (sb16.lnk; windows.h has a type of that name) */
LRESULT ENTRY SB_DRIVERPROC(DWORD id, HDRVR h, UINT msg, LPARAM l1, LPARAM l2)
{
    int r;
    switch (msg) {
    case 0:
        return 0;
    case DRV_LOAD:
        return 1;
    case DRV_ENABLE:
        enable();
        return 0;                       /* SNDBLST2's answer */
    case DRV_DISABLE:
    case DRV_FREE:
        disable();
        return 1;
    case DRV_OPEN:
    case DRV_CLOSE:
        return 1;
    case DRV_QUERYCONFIGURE:
        return 1;
    case DRV_CONFIGURE:
        r = DialogBox((HINSTANCE)sb_hinst, "SB16SETUP", (HWND)LOWORD(l1), (DLGPROC)SbSetupProc);
        return r < 0 ? DRVCNF_CANCEL : r;
    case DRV_INSTALL:
        return DRVCNF_RESTART;
    case DRV_REMOVE:
        return DRVCNF_OK;
    }
    return DefDriverProc(id, h, msg, l1, l2);
}

/* ---- at interrupt time (sb16a.asm's handler, interrupts enabled, on
   MMSYSTEM's interrupt stack, never reentered) ---- */
void __cdecl __far sb_work(void)
{
    WORD f;
    for (;;) {
        f = sb_cli();
        if (ev8) {
            ev8--;
            sb_sti(f);
            wave_irq(0);
        } else if (ev16) {
            ev16--;
            sb_sti(f);
            wave_irq(1);
        } else if (mpuqh != mpuqt) {
            sb_sti(f);
            midi_input();
        } else {
            sb_sti(f);
            return;
        }
    }
}
