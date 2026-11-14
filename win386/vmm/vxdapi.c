/* vxdapi.c - w16 VMM: the protected-mode API entry points of the virtual
 * devices (INT 2Fh AX=1684h), ARCHITECTURE.md 12.5.
 *
 * Windows 3.1 hands out a PM entry for exactly the devices whose DDB
 * declares a PM API procedure (observed with VX1, ARCHITECTURE.md 12.2):
 * VPICD, VTD, Reboot, VDD, VMD, VKD, VCD, SHELL, PageFile and VTDAPI. w16
 * gives the same set. Each entry is a HLT in the host's ring-3 breakpoint
 * segment, called far by the client; the device's handler gets the
 * client's registers.
 *
 * Until each device's API is implemented (from the DDK documentation and
 * what VX3 records under Windows 3.1), a call is logged to the trace with
 * its registers and fails with carry set: the log is how the calls USER
 * and the drivers make are found (the host's side of the black box).
 */
#include "vmm.h"

struct vxdapi {
    u16 id;
    const char *name;
    void (*fn)(struct cregs *r);
    u16 ver_ax, ver_bx;     /* function 0, as VX3 saw it under Windows 3.1 */
    u16 ver_cf;
};

static void api_default(struct cregs *r);
static void api_vdd(struct cregs *r);

static const struct vxdapi apis[] = {
    /* function 0 returns (VX3, Windows 3.1): Reboot has no function 0
     * (carry set), SHELL's BX is 1000h */
    { 0x0005, "VTD",      api_default, 0x030A, 0, 0 },
    { 0x000A, "VDD",      api_vdd,     0x030A, 0, 0 },
    { 0x000E, "VCD",      api_default, 0x030A, 0, 0 },
    { 0x0017, "SHELL",    api_default, 0x030A, 0x1000, 0 },
    { 0x0021, "PageFile", api_default, 0x010A, 0, 0 },
    { 0x0442, "VTDAPI",   api_default, 0x0001, 0, 0 },
};
#define NAPIS (sizeof(apis) / sizeof(apis[0]))

static u32 cur_api;

static void api_default(struct cregs *r)
{
    trace("VxD API %s (%x): ax=%x bx=%x cx=%x dx=%x si=%x di=%x ds=%x es=%x -> not implemented\n",
          apis[cur_api].name, apis[cur_api].id, AX(r), BX(r), CX(r), DX(r), SI(r), DI(r), r->ds, r->es);
    r->eflags |= EFL_CF;
}

/* VDD. VX5 (Windows 3.1): AX=000Ch with VGA.DRV's registers (BX=9B0Ah
 * CX=3101h DX=0 SI=01F4h DI=0106h) returns AX = BX, the others as they
 * were, carry clear. UNVERIFIED what it does. */
static void api_vdd(struct cregs *r)
{
    if (AX(r) == 0x000C) {
        SETW(r->eax, BX(r));
        r->eflags &= ~EFL_CF;
        return;
    }
    api_default(r);
}

/* INT 2Fh 1684h in protected mode: the entry for device BX, or 0:0 */
/* devices loaded from SYSTEM.INI whose DDB has a PM API procedure: their
 * own procedure answers (vxd.c), through entries after w16's own */
static u32 ext_id[PMBP_COUNT - PMBP_API], next_ext;

void vxdapi_get(struct cregs *r, u32 *sel, u32 *off)
{
    u32 i;
    if (vxd_pm_api(BX(r))) {
        for (i = 0; i < next_ext && ext_id[i] != BX(r); i++)
            ;
        if (i == next_ext && NAPIS + next_ext < PMBP_COUNT - PMBP_API) ext_id[next_ext++] = BX(r);
        if (i < next_ext) {
            *sel = SEL_PMBP;
            *off = PMBP_API + NAPIS + i;
            return;
        }
    }
    for (i = 0; i < NAPIS; i++)
        if (apis[i].id == BX(r)) {
            *sel = SEL_PMBP;
            *off = PMBP_API + i;
            return;
        }
    *sel = *off = 0;
}

/* A far call to entry index (PMBP_API + n) */
void vxdapi_call(struct cregs *r, u32 index, u32 retip, u32 retcs)
{
    u32 n = index - PMBP_API;
    r->eip = retip;
    r->cs = retcs;
    if (n >= NAPIS && n - NAPIS < next_ext) {
        /* the loaded device's own API procedure */
        vxd_call_api(vxd_pm_api(ext_id[n - NAPIS]), r);
        return;
    }
    if (n >= NAPIS) { r->eflags |= EFL_CF; return; }
    cur_api = n;
    if (AX(r) == 0) {               /* get version */
        SETW(r->eax, apis[n].ver_ax);
        SETW(r->ebx, apis[n].ver_bx);
        if (apis[n].ver_cf) r->eflags |= EFL_CF; else r->eflags &= ~EFL_CF;
        trace("VxD API %s: get version\n", apis[n].name);
        return;
    }
    apis[n].fn(r);
}
