/*
 * driver.c - installable drivers: OpenDriver, CloseDriver,
 * SendDriverMessage, DefDriverProc, GetDriverModuleHandle, GetDriverInfo,
 * GetNextDriver, the boot drivers of SYSTEM.INI [boot] drivers=, and the
 * messages drivers get when a task or Windows ends (ARCHITECTURE.md 10.7).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 25, Vol. 2
 * (the functions, DriverProc), Vol. 3 (DRIVERINFOSTRUCT); the rest is
 * what probe DRP (tests/drv/drp.c, with its driver DRT.DLL) saw on 3.1's
 * USER, and US1's boot trace (MMSYSTEM's DRV_LOAD with handle 1, then its
 * OpenDriver("timer") = 2):
 *   - A handle is the lowest free number from 1, taken when OpenDriver
 *     starts (a driver opened inside another's DRV_LOAD gets the next);
 *     the open instances form a list in the order their opens finished,
 *     which GetNextDriver walks (GND_REVERSE backwards; with
 *     GND_FIRSTINSTANCEONLY only each module's first instance in the
 *     list, also backwards).
 *   - OpenDriver(name, NULL): [drivers] name=FILE OPTIONS in SYSTEM.INI,
 *     else name is the file itself (options ""); with a section, name
 *     must be in it. The file is loaded (LoadLibrary, once per instance:
 *     its usage counts the instances) and its DRIVERPROC export called.
 *     A module's first instance gets message 0, DRV_LOAD and DRV_ENABLE
 *     (identifier 0); a DRV_LOAD answered 0 ends there (the module freed,
 *     no more messages). Every instance gets DRV_OPEN with identifier =
 *     its handle, lParam1 = the options (never NULL), lParam2 = OpenDriver's
 *     lParam; its answer is the identifier later messages carry, and 0
 *     fails the open (a first instance then gets DRV_DISABLE, DRV_FREE).
 *     The alias GetDriverInfo gives is the name as passed.
 *   - CloseDriver: DRV_CLOSE with the instance's identifier and the two
 *     parameters; 0 keeps the instance open and is returned. Otherwise
 *     the identifier becomes 0, and the module's last instance gets
 *     DRV_DISABLE and DRV_FREE.
 *   - DRV_DISABLE carries the instance's identifier, DRV_FREE always 0.
 *   - DefDriverProc: 1 for DRV_LOAD, DRV_ENABLE, DRV_DISABLE, DRV_FREE and
 *     DRV_INSTALL of a handle that is open (or being opened); 0 otherwise.
 *   - GetDriverInfo: only with length = sizeof(DRIVERINFOSTRUCT) (134);
 *     else 0 and nothing written.
 *   - Boot drivers ([boot] drivers=, e.g. mmsystem.dll) are opened at the
 *     first InitApp once Windows is up (the shell's, US1): message 0,
 *     DRV_LOAD, DRV_ENABLE, no DRV_OPEN (identifier 0).
 *   - A task that ends (USER's signal procedure) leaves its drivers open;
 *     each module's first instance gets DRV_EXITAPPLICATION
 *     (DRVEA_NORMALEXIT) in the task's context.
 *   - ExitWindows: each module's first instance gets DRV_EXITSESSION, then
 *     DRV_DISABLE and DRV_FREE; the other instances nothing.
 * UNVERIFIED: the order among several modules at task and Windows exit
 * (the probe has one module), DRVEA_ABNORMALEXIT for a task killed after a
 * fault, how many drivers 3.1 can have open (64 here), and what message
 * 0 means.
 */
#include "user.h"

#define NDRV    64

typedef LRESULT (__far __pascal *DRVPROC)(DWORD, HDRVR, UINT, LPARAM, LPARAM);

typedef struct DRV {
    WORD used;                  /* the handle is taken */
    WORD listed;                /* in the list (the open has finished) */
    WORD next;                  /* the next handle in the list, 0: none */
    HMODULE hmod;
    DRVPROC proc;
    DWORD id;                   /* DRV_OPEN's answer */
    char alias[128];
} DRV;

static DRV __far *drv;          /* NDRV entries, handle h at drv[h - 1] */
static WORD drvhead;            /* the first handle in the list */

static BOOL drv_init(void)
{
    if (!drv) {
        HGLOBAL g = GlobalAlloc(GMEM_FIXED | GMEM_SHARE | GMEM_ZEROINIT,
                                (DWORD)NDRV * sizeof(DRV));
        if (!g)
            return FALSE;
        drv = (DRV __far *)GlobalLock(g);
    }
    return TRUE;
}

static DRV __far *drv_of(HDRVR h)
{
    WORD i = (WORD)h;
    if (!drv || !i || i > NDRV || !drv[i - 1].used)
        return 0;
    return &drv[i - 1];
}

static DRV __far *listed(HDRVR h)
{
    DRV __far *d = drv_of(h);
    return d && d->listed ? d : 0;
}

static LRESULT call(WORD h, DWORD id, UINT msg, LPARAM l1, LPARAM l2)
{
    DRVPROC p = drv[h - 1].proc;
    return p(id, (HDRVR)h, msg, l1, l2);
}

/* is h the first instance of its module in the list? */
static BOOL first_instance(WORD h)
{
    WORD k;
    for (k = drvhead; k; k = drv[k - 1].next)
        if (drv[k - 1].hmod == drv[h - 1].hmod)
            return k == h;
    return FALSE;
}

static BOOL module_open(HMODULE m, WORD except)
{
    WORD k;
    for (k = 1; k <= NDRV; k++)
        if (k != except && drv[k - 1].used && drv[k - 1].hmod == m)
            return TRUE;
    return FALSE;
}

static void unlist(WORD h)
{
    WORD *pk = &drvhead;
    while (*pk && *pk != h)
        pk = &drv[*pk - 1].next;
    if (*pk)
        *pk = drv[h - 1].next;
    drv[h - 1].next = 0;
    drv[h - 1].listed = 0;
}

static void append(WORD h)
{
    WORD *pk = &drvhead;
    while (*pk)
        pk = &drv[*pk - 1].next;
    *pk = h;
    drv[h - 1].next = 0;
    drv[h - 1].listed = 1;
}

/* the first word of s (the file) into file; s's rest (after blanks) is
   returned */
static LPSTR split(LPSTR s, LPSTR file, WORD n)
{
    WORD i = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    while (*s && *s != ' ' && *s != '\t' && *s != ',') {
        if (i + 1 < n)
            file[i++] = *s;
        s++;
    }
    file[i] = 0;
    while (*s == ' ' || *s == '\t' || *s == ',')
        s++;
    return s;
}

/* open name; boot: a boot driver (no DRV_OPEN) */
static HDRVR open_driver(LPCSTR name, LPCSTR sec, LPARAM l, BOOL boot)
{
    static char ent[160];       /* (not reentered before it is used up) */
    char file[128];
    char opts[128];
    WORD h, i;
    DRV __far *d;
    HMODULE m;
    BOOL first;
    LRESULT r;

    if (!drv_init() || !name || !*name)
        return 0;
    if (!GetPrivateProfileString(sec ? sec : "drivers", name, "", ent, sizeof ent,
                                 "SYSTEM.INI")) {
        if (sec)
            return 0;
        lstr_cpyn(ent, name, sizeof ent);
    }
    lstr_cpyn(opts, split(ent, file, sizeof file), sizeof opts);
    for (h = 1; h <= NDRV && drv[h - 1].used; h++)
        ;
    if (h > NDRV)
        return 0;
    d = &drv[h - 1];
    d->used = 1;
    d->listed = 0;
    d->id = 0;
    d->proc = 0;
    {
        /* no "Cannot find" box for a missing file (3.1: OpenDriver of a
           name with no file, MMSYSTEM's "joystick" at boot, DRP's
           "nothere", gave 0 and nothing on the screen) */
        UINT em = SetErrorMode(SEM_NOOPENFILEERRORBOX);
        m = LoadLibrary(file);
        SetErrorMode(em);
    }
    if ((UINT)m < 32) {
        d->used = 0;
        return 0;
    }
    d = &drv[h - 1];
    d->hmod = m;
    d->proc = (DRVPROC)GetProcAddress(m, "DRIVERPROC");
    if (!d->proc) {
        d->used = 0;
        FreeLibrary(m);
        return 0;
    }
    for (i = 0; i + 1 < sizeof d->alias && name[i]; i++)
        d->alias[i] = name[i];
    d->alias[i] = 0;
    first = !module_open(m, h);
    if (first) {
        call(h, 0, 0, 0, 0);
        if (!call(h, 0, DRV_LOAD, 0, 0)) {
            drv[h - 1].used = 0;
            FreeLibrary(m);
            return 0;
        }
        call(h, 0, DRV_ENABLE, 0, 0);
    }
    if (!boot) {
        r = call(h, h, DRV_OPEN, (LPARAM)(LPSTR)opts, l);
        if (!r) {
            if (first) {
                call(h, 0, DRV_DISABLE, 0, 0);
                call(h, 0, DRV_FREE, 0, 0);
            }
            drv[h - 1].used = 0;
            FreeLibrary(m);
            return 0;
        }
        drv[h - 1].id = r;
    }
    append(h);
    return (HDRVR)h;
}

HDRVR WINAPI OpenDriver(LPCSTR name, LPCSTR sec, LPARAM l)
{
    return open_driver(name, sec, l, FALSE);
}

/* the module's last instance: DRV_DISABLE (with the identifier), DRV_FREE */
static void free_driver(WORD h)
{
    call(h, drv[h - 1].id, DRV_DISABLE, 0, 0);
    call(h, 0, DRV_FREE, 0, 0);
}

LRESULT WINAPI CloseDriver(HDRVR hd, LPARAM l1, LPARAM l2)
{
    DRV __far *d = listed(hd);
    WORD h = (WORD)hd;
    HMODULE m;
    LRESULT r;
    if (!d)
        return 0;
    r = call(h, d->id, DRV_CLOSE, l1, l2);
    if (!r)
        return 0;
    d = &drv[h - 1];
    d->id = 0;
    m = d->hmod;
    if (!module_open(m, h))
        free_driver(h);
    unlist(h);
    drv[h - 1].used = 0;
    FreeLibrary(m);
    return r;
}

LRESULT WINAPI SendDriverMessage(HDRVR hd, UINT msg, LPARAM l1, LPARAM l2)
{
    DRV __far *d = listed(hd);
    if (!d)
        return 0;
    return call((WORD)hd, d->id, msg, l1, l2);
}

LRESULT WINAPI DefDriverProc(DWORD id, HDRVR hd, UINT msg, LPARAM l1, LPARAM l2)
{
    (void)id, (void)l1, (void)l2;
    if (!drv_of(hd))
        return 0;
    switch (msg) {
    case DRV_LOAD: case DRV_ENABLE: case DRV_DISABLE: case DRV_FREE:
    case DRV_INSTALL:
        return 1;
    }
    return 0;
}

HINSTANCE WINAPI GetDriverModuleHandle(HDRVR hd)
{
    DRV __far *d = listed(hd);
    return d ? d->hmod : 0;
}

BOOL WINAPI GetDriverInfo(HDRVR hd, DRIVERINFOSTRUCT __far *dis)
{
    DRV __far *d = listed(hd);
    WORD i;
    if (!d || GP_BAD(dis) || dis->length != sizeof(DRIVERINFOSTRUCT))
        return FALSE;
    dis->hDriver = hd;
    dis->hModule = d->hmod;
    for (i = 0; i < sizeof dis->szAliasName && d->alias[i]; i++)
        dis->szAliasName[i] = d->alias[i];
    if (i < sizeof dis->szAliasName)
        dis->szAliasName[i] = 0;
    return TRUE;
}

HDRVR WINAPI GetNextDriver(HDRVR hd, DWORD flags)
{
    WORD h = (WORD)hd, k, prev;
    BOOL firsts = (flags & GND_FIRSTINSTANCEONLY) != 0;
    if (!drv || (h && !listed(hd)))
        return 0;
    if (!(flags & GND_REVERSE)) {
        for (k = h ? drv[h - 1].next : drvhead; k; k = drv[k - 1].next)
            if (!firsts || first_instance(k))
                return (HDRVR)k;
        return 0;
    }
    /* backwards: the last match before h (before the end with 0) */
    prev = 0;
    for (k = drvhead; k && k != h; k = drv[k - 1].next)
        if (!firsts || first_instance(k))
            prev = k;
    return (HDRVR)prev;
}

/* [boot] drivers= of SYSTEM.INI, at the first InitApp once Windows is up */
void open_boot_drivers(void)
{
    static BOOL done;
    char list[128];
    char name[80];
    LPSTR p;
    if (done)
        return;
    done = TRUE;
    GetPrivateProfileString("boot", "drivers", "", list, sizeof list, "SYSTEM.INI");
    for (p = list; *p;) {
        p = split(p, name, sizeof name);
        if (!*name)
            break;
        open_driver(name, NULL, 0, TRUE);
    }
}

/* each module's first instance, in the list's order */
static void to_firsts(UINT msg, LPARAM l1)
{
    WORD k;
    if (!drv)
        return;
    for (k = drvhead; k; k = drv[k - 1].next)
        if (first_instance(k))
            call(k, drv[k - 1].id, msg, l1, 0);
}

/* a task ends (USER's signal procedure, in its context; killed: after a
   fault, DRVEA_ABNORMALEXIT, UNVERIFIED) */
void drivers_task_exit(BOOL killed)
{
    to_firsts(DRV_EXITAPPLICATION, killed ? DRVEA_ABNORMALEXIT : DRVEA_NORMALEXIT);
}

/* Windows ends (ExitWindows) */
void drivers_exit_session(void)
{
    WORD k;
    to_firsts(DRV_EXITSESSION, 0);
    if (!drv)
        return;
    for (k = drvhead; k; k = drv[k - 1].next)
        if (first_instance(k))
            free_driver(k);
}
