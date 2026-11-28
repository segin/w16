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
static void api_vtd(struct cregs *r);
static void api_vdd(struct cregs *r);
static void api_shell(struct cregs *r);

static const struct vxdapi apis[] = {
    /* function 0 returns (VX3, Windows 3.1): Reboot has no function 0
     * (carry set), SHELL's BX is 1000h */
    { 0x0005, "VTD",      api_vtd,     0x030A, 0, 0 },
    { 0x000A, "VDD",      api_vdd,     0x030A, 0, 0 },
    { 0x000E, "VCD",      api_default, 0x030A, 0, 0 },
    { 0x0017, "SHELL",    api_shell,   0x030A, 0x1000, 0 },
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

/* VTD: 0101h the milliseconds since Windows started, 0102h the current
 * VM's own (Schulman ch. 10: what TOOLHELP's TimerCount asks in 386
 * Enhanced mode, EAX the result; here Get_System_Time and
 * Get_VM_Exec_Time's values). Before, they failed with carry and a trace
 * line, and TimerCount fell back to MMSYSTEM's timeGetTime: each call a
 * trap and a line, and the VM Task Manager's idle count (VMTASK.EXE
 * without VMTASK.386, ARCHITECTURE.md 15.12) got nothing finer than its
 * tick. UNVERIFIED: the rest of the VTD's API */
static void api_vtd(struct cregs *r)
{
    u32 t = vmm_system_time();
    switch (AX(r)) {
    case 0x0101:
        r->eax = t;
        r->eflags &= ~EFL_CF;
        return;
    case 0x0102:
        r->eax = cur_vm->exec_ms + (t - cur_vm->run_since);
        r->eflags &= ~EFL_CF;
        return;
    }
    api_default(r);
}

/* VDD (vdd.c): what it does not answer is traced and fails */
static void api_vdd(struct cregs *r)
{
    if (!vdd_api(r)) api_default(r);
}

/* SHELL (shell.c): its API takes the function in DX */
static void api_shell(struct cregs *r)
{
    shell_api(r);
}

/* INT 2Fh 1684h in protected mode: the entry for device BX, or 0:0 */
/* devices loaded from SYSTEM.INI whose DDB has a PM API procedure: their
 * own procedure answers (vxd.c), through entries after w16's own */
static u32 ext_id[PMBP_API_END - PMBP_API], next_ext;
static u32 ext_seen;                    /* devices whose API has been called (traced once) */

void vxdapi_get(struct cregs *r, u32 *sel, u32 *off)
{
    u32 i;
    if (vxd_pm_api(BX(r))) {
        for (i = 0; i < next_ext && ext_id[i] != BX(r); i++)
            ;
        if (i == next_ext && NAPIS + next_ext < PMBP_API_END - PMBP_API) ext_id[next_ext++] = BX(r);
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

/* ---- V86 mode: Windows 3.1 gives a DOS program entries for VPICD
 * (0003h), VTD (0005h), VMD (000Ch) and DOSMGR (0015h) (VX12 V, in the
 * MS-DOS Prompt), whose function 0 returns AX 030Ah, 030Ah, 0300h,
 * 030Ah, BX 0, carry clear. Here: those, by their function 0 (the rest
 * fail with carry: UNVERIFIED what they do), and a loaded device's own
 * V86 API procedure for any device that has one (vxd.c) ---- */
static const struct { u16 id, ver; } v86_own[] = {
    { 0x0003, 0x030A }, { 0x0005, 0x030A }, { 0x000C, 0x0300 }, { 0x0015, 0x030A },
};
#define NV86OWN (sizeof(v86_own) / sizeof(v86_own[0]))
static u32 v86_ext[W386_NV86API], n_v86_ext;

u32 vxdapi_v86_get(u32 id)
{
    u32 i;
    if (vxd_v86_api(id)) {
        for (i = 0; i < n_v86_ext && v86_ext[i] != id; i++)
            ;
        if (i == n_v86_ext && NV86OWN + n_v86_ext < W386_NV86API) v86_ext[n_v86_ext++] = id;
        if (i < n_v86_ext) return W386_BP_V86API + NV86OWN + i;
    }
    for (i = 0; i < NV86OWN; i++)
        if (v86_own[i].id == id) return W386_BP_V86API + i;
    return 0;
}

void vxdapi_v86_call(struct cregs *r, u32 idx)
{
    u32 n = idx - W386_BP_V86API;
    if (n >= NV86OWN && n - NV86OWN < n_v86_ext) {
        vxd_call_api(vxd_v86_api(v86_ext[n - NV86OWN]), r);
        return;
    }
    if (n < NV86OWN && AX(r) == 0) {
        SETW(r->eax, v86_own[n].ver);
        SETW(r->ebx, 0);
        r->eflags &= ~EFL_CF;
        return;
    }
    r->eflags |= EFL_CF;
}

/* the entry being called, for vxdapi_again */
static u32 cur_index;

/* the API call made again when the client runs next (its return address
 * pushed back, CS:IP its entry): a device waiting for something while the
 * client's VM goes on taking interrupts (SHELL's Alt+Tab title, which
 * waits for Tab or Alt let go) */
void vxdapi_again(struct cregs *r)
{
    dpmi_client_push(r, r->cs);
    dpmi_client_push(r, r->eip);
    r->cs = SEL_PMBP;
    r->eip = cur_index;
}

/* A far call to entry index (PMBP_API + n) */
void vxdapi_call(struct cregs *r, u32 index, u32 retip, u32 retcs)
{
    u32 n = index - PMBP_API;
    cur_index = index;
    r->eip = retip;
    r->cs = retcs;
    if (n >= NAPIS && n - NAPIS < next_ext) {
        /* the loaded device's own API procedure */
        u32 ax = AX(r), bx = BX(r), cx = CX(r), dx = DX(r), si = SI(r), di = DI(r), ds = r->ds, es = r->es;
        vxd_call_api(vxd_pm_api(ext_id[n - NAPIS]), r);
        /* a call the device refused (carry): traced, as for w16's own APIs;
         * each device's first call, and with [w16] vmmtrace=1 every call,
         * with its caller */
        if (info->trace == 1 || !(ext_seen & (1u << ((n - NAPIS) & 31)))) {
            ext_seen |= 1u << ((n - NAPIS) & 31);
            trace("VxD API device %x from %x:%x: ax=%x bx=%x cx=%x dx=%x si=%x di=%x ds=%x es=%x -> ax=%x cf=%d\n",
                  ext_id[n - NAPIS], retcs, retip, ax, bx, cx, dx, si, di, ds, es, AX(r), r->eflags & EFL_CF);
        } else if (r->eflags & EFL_CF)
            trace("VxD API device %x: ax=%x bx=%x cx=%x dx=%x si=%x di=%x ds=%x es=%x -> carry\n",
                  ext_id[n - NAPIS], ax, bx, cx, dx, si, di, ds, es);
        return;
    }
    if (n >= NAPIS) { r->eflags |= EFL_CF; return; }
    cur_api = n;
    if (AX(r) == 0 && apis[n].id != 0x0017 && apis[n].id != 0x000A) {
        /* get version (SHELL's by DX: shell.c; VDD's: vdd.c) */
        SETW(r->eax, apis[n].ver_ax);
        SETW(r->ebx, apis[n].ver_bx);
        if (apis[n].ver_cf) r->eflags |= EFL_CF; else r->eflags &= ~EFL_CF;
        trace("VxD API %s: get version\n", apis[n].name);
        return;
    }
    apis[n].fn(r);
}
