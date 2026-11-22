/* w16vxd.h - what w16's own VxD modules (win386/vxd) and its VMM share
 * (ARCHITECTURE.md 12.8). Not a Windows interface: the register block of
 * the C entry points (vxdrt.asm), the DDK's Client_Reg_Struc (rewritten
 * from the Virtual Device Adaptation Guide), and the private link between
 * the VMM and w16's VPICD, which owns the 8259s.
 */
#ifndef W16VXD_H
#define W16VXD_H

typedef unsigned char  vu8;
typedef unsigned short vu16;
typedef unsigned int   vu32;

/* registers as an entry point got them (PUSHAD order, then the flags and
 * one word of the entry's own: a service number or a C function) */
struct vregs {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    vu32 eflags, arg;
};
#define VF_CF 0x0001
#define VF_ZF 0x0040
#define VF_IF 0x0200

#pragma pack(1)
/* the DDK's Client_Reg_Struc */
struct client_regs {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax, error, eip;
    vu16 cs, p1;
    vu32 eflags, esp;
    vu16 ss, p2, es, p3, ds, p4, fs, p5, gs, p6;
    vu32 alt_eip;
    vu16 alt_cs, p7;
    vu32 alt_eflags, alt_esp;
    vu16 alt_ss, p8, alt_es, p9, alt_ds, p10, alt_fs, p11, alt_gs, p12;
};

/* the DDK's VxD_Desc_Block */
struct vxd_ddb {
    vu32 next;
    vu16 sdk_version, device_id;
    vu8 major, minor;
    vu16 flags;
    char name[8];
    vu32 init_order, control, v86_api, pm_api, v86_csip, pm_csip, ref_data;
    vu32 svc_table, svc_count;
};

/* the DDK's VPICD_IRQ_Descriptor */
struct vid {
    vu16 irq, options;
    vu32 hw_int_proc, virt_int_proc, eoi_proc, mask_change_proc, iret_proc;
    vu32 iret_time_out;
};
#pragma pack()

/* The VMM and w16's VPICD. The VPICD module's DDB reference data points
 * at a struct vpicd_link; the VMM fills in its half when it finds the
 * device and from then on hands every hardware interrupt to hw_int. */
#define VPICD_LINK_MAGIC 0x4B4E4C56u        /* "VLNK" */
#define VPICD_BASE1 0x50                    /* where VPICD puts the master's vectors */
#define VPICD_BASE2 0x58                    /* and the slave's (the VMM's IDT gates) */
struct vpicd_link {
    vu32 magic;
    /* VPICD's: a hardware interrupt on IRQ irq has come (interrupts off) */
    void (*hw_int)(vu32 irq);
    /* VPICD's: the VMM is about to resume the VM: deliver what is pending */
    void (*resume)(void);
    /* VPICD's: a simulated interrupt delivered with iret set has returned
     * (its handler's IRET); innermost first */
    void (*iret)(void);
    /* the VMM's: can the VM take an interrupt now (during hw_int, resume
     * or a service called then): interrupts on */
    int (*can_deliver)(void);
    /* the VMM's: simulate interrupt vector vec into the current VM where it
     * was interrupted; iret: call iret when its handler returns. Returns 0
     * if the VM cannot take it now */
    int (*deliver)(vu32 vec, int iret);
    /* the VMM's: the System VM's handle and the PIC bases it had at start */
    vu32 sys_vm;
    vu32 rm_base1, rm_base2;
    /* the VMM's: the VM running now; an interrupt request for VM vm (not
     * the current one, or idle): give it time soon */
    vu32 (*cur_vm)(void);
    void (*wake)(vu32 vm);
    /* VPICD's (set at Sys_Critical_Init): has the current VM a request it
     * would take once its interrupts are on (the VMM's CLI/STI fast path) */
    int (*pending)(void);
};

/* The VMM and w16's REBOOT: the DDB's reference data points at a struct
 * reboot_link, whose functions the VMM fills in: a DOS VM's title (its
 * PIF's, as SHELL had it from WINOA386: the Ctrl+Alt+Del screen for a DOS
 * program shows it) and closing a DOS VM (that screen's Enter) */
#define REBOOT_LINK_MAGIC 0x4B4E4C52u       /* "RLNK" */
struct reboot_link {
    vu32 magic;
    int (*title)(vu32 vm, char *buf, int n);    /* 0: none */
    void (*close)(vu32 vm);
};

/* the VM control block's fields the devices read (the DDK's CB_VM_Status,
 * CB_High_Linear, CB_Client_Pointer, CB_VMID) */
#define CB_HIGH_LINEAR(vm)  (*(vu32 *)((vm) + 4))
#define CB_CLIENT(vm)       (*(vu32 *)((vm) + 8))
#define CB_VMID(vm)         (*(vu32 *)((vm) + 12))

#endif
