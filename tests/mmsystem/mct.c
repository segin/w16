/*
 * mct.c - MCT.DRV, a test MCI driver for the MCI probe (layer 4;
 * tests/mmsystem/mmci.c, ARCHITECTURE.md 23.1). SYSTEM.INI [mci]
 * NAME=mct.drv OPTIONS makes it an MCI device; MMSYSTEM opens it as it
 * opens MCIWAVE.DRV, and every DriverProc call is recorded by
 * MMSYSLOG.DLL (tests/mmsystem/mdl.c: the message, the flags, the
 * parameter block before and after, the strings it points to) as a
 * logging pass-through driver's would be. Its answers are the probe's to
 * choose (MctSet), so that what MMSYSTEM makes of each kind of answer can
 * be seen: return values with MCI_RESOURCE_RETURNED and the other flags,
 * values and strings written into the parameter block, notifications
 * (mciDriverNotify), waiting (mciDriverYield), command tables.
 *
 * Options (the rest of the [mci] line, MCI_OPEN_DRIVER_PARMS.lpstrParams):
 *   type=N     the device type it reports (MCI_OPEN_DRIVER_PARMS.wType;
 *              default 521, MCI_DEVTYPE_OTHER)
 *   table=NAME a command table from its own resources
 *              (mciLoadCommandResource(hinst, NAME, 0)) as its custom table
 *   fail       DRV_OPEN answers 0
 *   nofill     leaves wType and wCustomCommandTable as MMSYSTEM set them
 * Its calls of mciLoadCommandResource, mciFreeCommandResource,
 * mciDriverNotify, mciDriverYield, mciSetDriverData, mciGetDriverData
 * and mciGetCreatorTask go through MMSYSLOG (imported from it), which
 * records them with their results.
 *
 * The parameter blocks are the Multimedia Programmer's Guide's (ch. 2,
 * MCI_OPEN_PARMS and the rest); MCI_OPEN_DRIVER_PARMS {WORD wDeviceID,
 * LPCSTR lpstrParams, WORD wCustomCommandTable, WORD wType} and the
 * command-table format are as Wine's mmddk.h of before 2002 (X11 licence)
 * gives them, confirmed by what 3.1's MMSYSTEM does with them.
 */
#include <windows.h>
#include "mdl.h"

extern WORD __cdecl lib_regs[12];       /* dllstart.asm: DI = the instance */

/* MMSYSLOG's forwarders of MMSYSTEM's MCI helpers (tests/mmsystem/mdl.lnk) */
UINT FAR PASCAL MCILOADCOMMANDRESOURCE(HINSTANCE, LPCSTR, UINT);
BOOL FAR PASCAL MCIFREECOMMANDRESOURCE(UINT);
BOOL FAR PASCAL MCIDRIVERNOTIFY(HWND, UINT, UINT);
UINT FAR PASCAL MCIDRIVERYIELD(UINT);
BOOL FAR PASCAL MCISETDRIVERDATA(UINT, DWORD);
DWORD FAR PASCAL MCIGETDRIVERDATA(UINT);
HTASK FAR PASCAL MCIGETCREATORTASK(UINT);

#define MCI_NOTIFY  1L
#define MCI_WAIT    2L

static HINSTANCE hinst;
static WORD shimno;

/* the open instances: the identifier DRV_OPEN answers is 1 + the slot */
#define NDEV 8
static struct {
    WORD used, devid, table;
} devs[NDEV];

/* the probe's settings (MctSet) */
static DWORD ret;               /* what the next MCI commands answer */
static WORD nset;
static struct { WORD at; DWORD v; } sets[4];    /* DWORDs written into the block */
static WORD strat;              /* a string written where the DWORD at strat points */
static char str[80];
static WORD notify;             /* MCI_NOTIFY: mciDriverNotify with this status */
static WORD yields;             /* MCI_WAIT: mciDriverYield this many times */
static WORD yieldstop;          /* ... or until it answers non-zero */
static WORD helpers;            /* each command: mciGet/SetDriverData, mciGetCreatorTask */

int __cdecl libmain(void)
{
    hinst = (HINSTANCE)lib_regs[5];
    shimno = MlRegister("MCT");
    return 1;
}

/* MctSet(what, value): 0 everything back to the defaults, 1 the answer,
   2 a DWORD to write (value: HIWORD the offset, LOWORD the DWORD's low
   word; 3 then sets the last one's high word), 4 the offset of the
   pointer to write MctStr's string to (0: none), 5 notify, 6 yields,
   7 helpers */
void FAR PASCAL __loadds MctSet(WORD what, DWORD v)
{
    switch (what) {
    case 0:
        ret = 0;
        nset = 0;
        strat = 0;
        notify = yields = yieldstop = helpers = 0;
        break;
    case 1: ret = v; break;
    case 2:
        if (nset < 4) {
            sets[nset].at = HIWORD(v);
            sets[nset].v = LOWORD(v);
            nset++;
        }
        break;
    case 3:
        if (nset)
            sets[nset - 1].v |= v << 16;
        break;
    case 4: strat = LOWORD(v); break;
    case 5: notify = LOWORD(v); break;
    case 6: yields = LOWORD(v); yieldstop = HIWORD(v); break;
    case 7: helpers = LOWORD(v); break;
    }
}

void FAR PASCAL __loadds MctStr(LPCSTR s)
{
    lstrcpyn(str, s, sizeof str);
}

static WORD num(LPCSTR s)
{
    WORD n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return n;
}

/* the option's value: "key=value" in the options string */
static LPCSTR opt(LPCSTR o, LPCSTR key, char *buf, int len)
{
    int n = lstrlen(key);
    while (o && *o) {
        int i;
        while (*o == ' ')
            o++;
        for (i = 0; i < n && o[i] && (o[i] | 0x20) == key[i]; i++)
            ;
        if (i == n && (o[n] == '=' || o[n] == ' ' || !o[n])) {
            o += n;
            if (*o == '=')
                o++;
            for (i = 0; i < len - 1 && o[i] && o[i] != ' '; i++)
                buf[i] = o[i];
            buf[i] = 0;
            return buf;
        }
        while (*o && *o != ' ')
            o++;
    }
    return 0;
}

struct opendrv {                /* MCI_OPEN_DRIVER_PARMS */
    WORD wDeviceID;
    LPCSTR lpstrParams;
    WORD wCustomCommandTable;
    WORD wType;
};

static DWORD FAR PASCAL __loadds answer(DWORD id, WORD h, WORD msg, DWORD p1, DWORD p2)
{
    char b[40];
    int k;
    (void)h;
    switch (msg) {
    case DRV_LOAD: case DRV_ENABLE: case DRV_DISABLE: case DRV_FREE:
        return 1;
    case DRV_OPEN: {
        struct opendrv FAR *o = (struct opendrv FAR *)p2;
        /* the options: the rest of the [mci] line, which MMSYSTEM hands
           over in MCI_OPEN_DRIVER_PARMS */
        LPCSTR opts = o ? o->lpstrParams : 0;
        (void)p1;
        if (opt(opts, "fail", b, sizeof b))
            return 0;
        for (k = 0; k < NDEV && devs[k].used; k++)
            ;
        if (k == NDEV)
            return 0;
        devs[k].used = 1;
        devs[k].table = 0xFFFF;
        devs[k].devid = o ? o->wDeviceID : 0;
        if (opt(opts, "table", b, sizeof b))
            devs[k].table = MCILOADCOMMANDRESOURCE(hinst, b, 0);
        if (o && !opt(opts, "nofill", b, sizeof b)) {
            o->wType = opt(opts, "type", b, sizeof b) ? num(b) : 521;
            o->wCustomCommandTable = devs[k].table;
        }
        return k + 1;
    }
    case DRV_CLOSE:
        if (id >= 1 && id <= NDEV) {
            k = (int)id - 1;
            if (devs[k].table != 0xFFFF)
                MCIFREECOMMANDRESOURCE(devs[k].table);
            devs[k].used = 0;
        }
        return 1;
    }
    if (msg < 0x800)
        return 0;
    /* an MCI command */
    k = id >= 1 && id <= NDEV ? (int)id - 1 : 0;
    if (helpers) {
        MCISETDRIVERDATA(devs[k].devid, MCIGETDRIVERDATA(devs[k].devid) + 1);
        MCIGETCREATORTASK(devs[k].devid);
    }
    if (p2) {
        DWORD FAR *d = (DWORD FAR *)p2;
        WORD i;
        for (i = 0; i < nset; i++)
            d[sets[i].at / 4] = sets[i].v;
        if (strat && d[strat / 4])
            lstrcpy((LPSTR)d[strat / 4], str);
        if ((p1 & MCI_NOTIFY) && notify)
            MCIDRIVERNOTIFY((HWND)LOWORD(d[0]), devs[k].devid, notify);
    }
    if ((p1 & MCI_WAIT) && yields) {
        WORD i;
        for (i = 0; i < yields; i++) {
            DWORD t = GetTickCount();
            while (GetTickCount() - t < 50)
                ;
            if (MCIDRIVERYIELD(devs[k].devid) && yieldstop)
                break;
        }
    }
    return ret;
}

DWORD __cdecl __far mds_entry(WORD kind, WORD flags, WORD retcs, void __far *args)
{
    return MlCall(shimno, kind, (FARPROC)answer, flags, retcs, args);
}
