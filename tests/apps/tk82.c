/*
 * tk82.c - a module table made by a program, not by KERNEL's loader.
 * Found with Win32s's Freecell: Win32s's WIN32S16.DLL gives each Win32
 * DLL a 16-bit module of its own, built in memory (observed at run time,
 * its blocks dumped): a moveable block starting "NE" with no segments,
 * flags 8000h, a resource table whose types have Win32s's own handler,
 * a resident name, made its own owner with FarSetOwner, not linked into
 * the module list; and a block of 80h bytes owned by it that is the
 * DLL's 16-bit hInstance. USER's LoadBitmap with that hInstance then asks
 * KERNEL's FindResource for the bitmap; w16's KERNEL knew only the
 * modules its loader made and found nothing, and Freecell said "Out of
 * memory". This probe makes such a module the same way and asks KERNEL
 * (and USER's LoadString) about it; run as the shell on 3.1 and with
 * w16's KERNEL, the logs compared (boot-test case rsrchandler, part
 * foreign):
 *   foreign: GetExePtr of the instance block, of the module's selector
 *        and handle; GetModuleUsage, GetModuleHandle by name and by
 *        handle; FindResource, SizeofResource, LoadResource (what the
 *        type's handler is called with), LockResource, the table's
 *        handle and usage words, FreeResource, LoadString
 */
#include <windows.h>

#define TK_REF "tk82ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_GETEXEPTR(WORD h);             /* 133 */
extern void FAR PASCAL K_FARSETOWNER(WORD h, WORD owner); /* 403 */

/* the module table's layout (offsets) */
#define O_RSRC  0x48            /* after the header and 8 bytes of the
                                   maker's own, as Win32s's */
#define O_T1    (O_RSRC + 2)    /* RT_RCDATA, 2 resources */
#define O_N1    (O_T1 + 8)
#define O_T2    (O_N1 + 2 * 12) /* RT_STRING, 1 */
#define O_N2    (O_T2 + 8)
#define O_TEND  (O_N2 + 12)
#define O_RN    (O_TEND + 2)    /* resident names */
#define O_END   (O_RN + 9)
#define MODSIZE 0x100

static WORD inst, modh, modsel;
static WORD calls, c_hmem, c_hinst, c_hrsrc;

static WORD FAR *mw(WORD off)
{
    return (WORD FAR *)MAKELP(modsel, off);
}

static void id_of(const char *name, WORD v)
{
    out("OBS ");
    out(name);
    out("=");
    out(v == 0 ? "0" : v == inst ? "inst" : v == modsel ? "modsel" :
        v == modh ? "modhandle" : v == (inst | 1) ? "inst|1" : "other");
    out("\r\n");
}

/* the types' handler: logs what it is given, fills a block of
   AllocResource's as Win32s's handlers do (its own DS: KERNEL's callers
   need not hand it the program's) */
static HGLOBAL FAR PASCAL __loadds handler(HGLOBAL h, HINSTANCE i, HRSRC r)
{
    HGLOBAL g;
    BYTE FAR *p;
    calls++;
    c_hmem = (WORD)h;
    c_hinst = (WORD)i;
    c_hrsrc = (WORD)r;
    if (h && !(GlobalFlags(h) & GMEM_DISCARDED))
        return h;
    g = AllocResource(i, r, 0);
    if (!g)
        return 0;
    p = GlobalLock(g);
    if ((WORD)r == O_N2) {      /* strings 0-15: string 1 = "Hello" */
        WORD k;
        for (k = 0; k < 16; k++)
            p[k] = 0;
        p[1] = 5;
        p[2] = 'H'; p[3] = 'e'; p[4] = 'l'; p[5] = 'l'; p[6] = 'o';
    } else {
        p[0] = 'T'; p[1] = 'K'; p[2] = '8'; p[3] = '2';
    }
    GlobalUnlock(g);
    return g;
}

static void name(WORD o, WORD off, WORD len, WORD id)
{
    *mw(o) = off;
    *mw(o + 2) = len;
    *mw(o + 4) = 0x0010;        /* moveable */
    *mw(o + 6) = id;
    *mw(o + 8) = 0;
    *mw(o + 10) = 0;
}

static int build(void)
{
    WORD i;
    BYTE FAR *b;
    FARPROC hp = MakeProcInstance((FARPROC)handler, (HINSTANCE)init_regs[5]);
    modh = (WORD)GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, MODSIZE);
    if (!modh)
        return 0;
    b = GlobalLock((HGLOBAL)modh);
    modsel = SELECTOROF(b);
    for (i = 0; i < MODSIZE; i++)
        b[i] = 0;
    *mw(0x00) = 0x454E;         /* "NE" */
    *mw(0x02) = 1;              /* usage */
    *mw(0x04) = O_END;          /* entry table */
    *mw(0x0A) = O_END;          /* load file information */
    *mw(0x0C) = 0x8000;         /* a library */
    *mw(0x22) = O_RSRC;         /* segment table (none) */
    *mw(0x24) = O_RSRC;
    *mw(0x26) = O_RN;
    *mw(0x28) = O_END;
    *mw(0x2A) = O_END;
    *mw(0x3E) = 0x030A;
    *mw(O_RSRC) = 4;            /* alignment shift */
    *mw(O_T1) = 0x800A;         /* RT_RCDATA */
    *mw(O_T1 + 2) = 2;
    *(FARPROC FAR *)mw(O_T1 + 4) = hp;
    name(O_N1, 0x10, 2, 0x8001);
    name(O_N1 + 12, 0x20, 3, 0x8002);
    *mw(O_T2) = 0x8006;         /* RT_STRING */
    *mw(O_T2 + 2) = 1;
    *(FARPROC FAR *)mw(O_T2 + 4) = hp;
    name(O_N2, 0x30, 1, 0x8001);
    b[O_RN] = 5;
    b[O_RN + 1] = 'T'; b[O_RN + 2] = 'K'; b[O_RN + 3] = 'M';
    b[O_RN + 4] = 'O'; b[O_RN + 5] = 'D';
    K_FARSETOWNER(modsel, modsel);
    inst = (WORD)GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x80);
    if (!inst)
        return 0;
    K_FARSETOWNER(inst, modsel);
    return 1;
}

static void foreign(void)
{
    WORD r, h, h2, n;
    char buf[32];
    BYTE FAR *p;
    out("foreign\r\n");
    obs("foreign: built", build());
    if (!inst)
        return;
    id_of("foreign: GetExePtr(inst)", K_GETEXEPTR(inst));
    id_of("foreign: GetExePtr(modsel)", K_GETEXEPTR(modsel));
    id_of("foreign: GetExePtr(modhandle)", K_GETEXEPTR(modh));
    obs("foreign: GetModuleUsage(inst)", GetModuleUsage((HINSTANCE)inst));
    id_of("foreign: GetModuleHandle(name)", (WORD)GetModuleHandle("TKMOD"));
    id_of("foreign: GetModuleHandle(inst)", (WORD)GetModuleHandle((LPCSTR)MAKELP(0, inst)));
    r = (WORD)FindResource((HINSTANCE)inst, MAKEINTRESOURCE(2), RT_RCDATA);
    obs("foreign: FindResource(inst)", r);
    obs("foreign: FindResource(modsel)",
        (WORD)FindResource((HINSTANCE)modsel, MAKEINTRESOURCE(2), RT_RCDATA));
    obs("foreign: FindResource(inst, #2)",
        (WORD)FindResource((HINSTANCE)inst, "#2", RT_RCDATA));
    obs("foreign: FindResource(inst, 3)",
        (WORD)FindResource((HINSTANCE)inst, MAKEINTRESOURCE(3), RT_RCDATA));
    if (!r)
        return;
    obs("foreign: SizeofResource", (WORD)SizeofResource((HINSTANCE)inst, (HRSRC)r));
    calls = 0;
    h = (WORD)LoadResource((HINSTANCE)inst, (HRSRC)r);
    obs("foreign: LoadResource: handler calls", calls);
    obs("foreign: LoadResource: handler hMem", c_hmem);
    id_of("foreign: LoadResource: handler hInstance", c_hinst);
    obs("foreign: LoadResource: handler hRsrc", c_hrsrc);
    obs("foreign: LoadResource: a handle", h != 0);
    obs("foreign: table handle is it", *mw(r + 8) == h);
    obs("foreign: table usage", *mw(r + 10));
    obs("foreign: table flags", *mw(r + 4));
    if (!h)
        return;
    id_of("foreign: GetExePtr(resource)", K_GETEXEPTR(h));
    calls = 0;
    h2 = (WORD)LoadResource((HINSTANCE)inst, (HRSRC)r);
    obs("foreign: again: handler calls", calls);
    obs("foreign: again: same handle", h2 == h);
    obs("foreign: again: table usage", *mw(r + 10));
    p = LockResource((HGLOBAL)h);
    obs("foreign: LockResource: data", p ? *(WORD FAR *)p : 0);
    if (p)
        GlobalUnlock((HGLOBAL)h);
    obs("foreign: FreeResource 1", FreeResource((HGLOBAL)h) == (BOOL)h);
    obs("foreign: FreeResource 1: usage", *mw(r + 10));
    obs("foreign: FreeResource 2", FreeResource((HGLOBAL)h));
    obs("foreign: FreeResource 2: usage", *mw(r + 10));
    obs("foreign: FreeResource 2: table handle", *mw(r + 8));
    obs("foreign: FreeResource 2: table flags", *mw(r + 4));
    calls = 0;
    buf[0] = 0;
    n = LoadString((HINSTANCE)inst, 1, buf, sizeof buf);
    obs("foreign: LoadString", n);
    obs("foreign: LoadString: handler calls", calls);
    id_of("foreign: LoadString: handler hInstance", c_hinst);
    obs("foreign: LoadString: handler hRsrc", c_hrsrc);
    obs_str("foreign: LoadString: text", buf);
    obs("foreign: LoadString: table handle", *mw(O_N2 + 8) != 0);
    GlobalFree((HGLOBAL)inst);
    GlobalUnlock((HGLOBAL)modh);
    GlobalFree((HGLOBAL)modh);
}

int __cdecl tmain(void)
{
    out("TK82 start\r\n");
    foreign();
    out("TK82 end\r\n");
    return failures;
}
