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

/* the VMM's control messages (EAX of a device's control procedure; the
 * DDK's Virtual Device Adaptation Guide), shared by the VMM and every
 * w16 VxD */
#define SYS_CRITICAL_INIT   0x00
#define DEVICE_INIT         0x01
#define INIT_COMPLETE       0x02
#define SYS_VM_INIT         0x03
#define SYS_VM_TERMINATE    0x04
#define SYSTEM_EXIT         0x05
#define SYS_CRITICAL_EXIT   0x06
#define CREATE_VM           0x07
#define VM_CRITICAL_INIT    0x08
#define VM_INIT             0x09
#define VM_TERMINATE        0x0A
#define VM_NOT_EXECUTEABLE  0x0B
#define DESTROY_VM          0x0C
#define VM_SUSPEND          0x0D
#define VM_RESUME           0x0E
#define SET_DEVICE_FOCUS    0x0F
#define BEGIN_MESSAGE_MODE  0x10
#define END_MESSAGE_MODE    0x11
#define BEGIN_PM_APP        0x15
#define END_PM_APP          0x16
#define CLOSE_VM_NOTIFY     0x19

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

/* The VMM and w16's VDMAD: the DDB's reference data points at a struct
 * vdmad_link, which the VMM fills in: what the DDK's VDMAD gets from the
 * VMM's memory services (a linear address's physical page, physically
 * contiguous pages for its buffer) and the SYSTEM.INI settings the loader
 * read ([386Enh] DMABufferSize, DMABufferIn1MB: the loader's buffer) */
#define VDMAD_LINK_MAGIC 0x4B4E4C44u        /* "DLNK" */
struct vdmad_link {
    vu32 magic;
    /* the physical address of ring-0 linear address lin; ~0 if not present */
    vu32 (*lin_to_phys)(vu32 lin);
    /* n contiguous pages, a multiple of align pages, below max_phys (0:
     * anywhere): their ring-0 linear address (phys in *phys), or 0 */
    vu32 (*alloc_run)(vu32 n, vu32 align, vu32 max_phys, vu32 *phys);
    vu32 buf_kb;                            /* DMABufferSize (16 when absent) */
    vu32 buf_conv;                          /* the conventional buffer (physical; 0: none) */
};

/* the VM control block's fields the devices read (the DDK's CB_VM_Status,
 * CB_High_Linear, CB_Client_Pointer, CB_VMID) */
#define CB_HIGH_LINEAR(vm)  (*(vu32 *)((vm) + 4))
#define CB_CLIENT(vm)       (*(vu32 *)((vm) + 8))
#define CB_VMID(vm)         (*(vu32 *)((vm) + 12))

#endif
