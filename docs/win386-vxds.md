# WIN386.EXE: the bundled VxDs to recreate

WIN386.EXE is the 386 Enhanced mode loader: an MZ stub followed by a W3 container holding the VMM and the built-in virtual device drivers (VxDs) as LE modules. SYSTEM.INI's `[386Enh]` section names a built-in VxD with a leading `*` (`device=*vpicd`) and an external one by file name (`device=vtdapi.386`). For w16 to be a drop-in replacement, it must provide every module listed below, under the same names.

The list below came from reading the file headers with the user's `readexe` (https://github.com/segin/readexe, ISC-style licence), which parses the W3 module table. That is a published file format; nothing was disassembled. Offsets and sizes are the W3 table's entries as readexe prints them. W3 version: VMM 3.10 (030Ah) in both installs.

## Windows 3.1 (.tools/ref/win31)

| # | Module | W3 offset | Size | `[386Enh]` reference |
|---|---|---|---|---|
| 00 | WIN386 (the VMM) | 6C00h | 3B88h | — |
| 01 | INT13 | 21C00h | 022Bh | (FastDisk; not in the default SYSTEM.INI) |
| 02 | WDCTRL | 23400h | 02AAh | (FastDisk controller; not in the default SYSTEM.INI) |
| 03 | VMD | 26C00h | 0308h | `mouse=*vmd` |
| 04 | VNETBIOS | 29400h | 0355h | `network=*dosnet,*vnetbios` |
| 05 | DOSNET | 2BC00h | 023Fh | `network=*dosnet,*vnetbios` |
| 06 | EBIOS | 2E400h | 01BEh | `ebios=*ebios` |
| 07 | VDDVGA | 30C00h | 0DBFh | `display=*vddvga` |
| 08 | VKD | 3B800h | 090Ch | `keyboard=*vkd` |
| 09 | VPICD | 40400h | 082Fh | `device=*vpicd` |
| 0A | VTD | 43400h | 052Bh | `device=*vtd` |
| 0B | REBOOT | 45800h | 0257h | `device=*reboot` |
| 0C | VDMAD | 47400h | 0790h | `device=*vdmad` |
| 0D | VSD | 4A800h | 019Bh | `device=*vsd` |
| 0E | V86MMGR | 4C400h | 0F99h | `device=*v86mmgr` |
| 0F | PAGESWAP | 58C00h | 038Fh | `device=*pageswap` |
| 10 | DOSMGR | 5B400h | 17EAh | `device=*dosmgr` |
| 11 | VMPOLL | 67400h | 0224h | `device=*vmpoll` |
| 12 | WSHELL | 68C00h | 0DAAh | `device=*wshell` |
| 13 | BLOCKDEV | 6F800h | 028Eh | `device=*BLOCKDEV` |
| 14 | PAGEFILE | 72400h | 048Bh | `device=*PAGEFILE` |
| 15 | VFD | 75400h | 018Ah | `device=*vfd` |
| 16 | PARITY | 76C00h | 0168h | `device=*parity` |
| 17 | BIOSXLAT | 78400h | 01B4h | `device=*biosxlat` |
| 18 | VCD | 79C00h | 04B3h | `device=*vcd` |
| 19 | VMCPD | 7C400h | 021Eh | `device=*vmcpd` |
| 1A | COMBUFF | 7EC00h | 0203h | `device=*combuff` |
| 1B | CDPSCSI | 80400h | 0155h | `device=*cdpscsi` |
| 1C | QEMMFIX | 82C00h | 01ACh | (not in the default SYSTEM.INI) |

The default install also loads one external VxD: `device=vtdapi.386` (SYSTEM\VTDAPI.386, the timer API for MMSYSTEM).

## Windows for Workgroups 3.11 (.tools/ref/wfw311a)

The same modules plus **VPD** (printer) and **VWC**. The order is: WIN386, INT13, WDCTRL, VMD, VPD, VWC, DOSNET, VNETBIOS, EBIOS, VDDVGA, VKD, VPICD, VTD, REBOOT, VDMAD, VSD, V86MMGR, PAGESWAP, DOSMGR, VMPOLL, WSHELL, BLOCKDEV, PAGEFILE, VFD, PARITY, BIOSXLAT, VCD, VMCPD, COMBUFF, CDPSCSI, QEMMFIX. WfW 3.11 also ships twelve external .386 VxDs in SYSTEM: VTDAPI, VPMTD, VCOMM, SERIAL, LPT, IFSMGR, VCACHE, VSHARE (all named by `device=` in its SYSTEM.INI), IOS, VFAT and VXDLDR (not named in SYSTEM.INI; UNVERIFIED who loads them) and none other.

## The LE modules and their Device Descriptor Blocks

readexe now parses LE (w16's extension, `tools/readexe/`, a vendored copy of the user's readexe with its LICENSE.md; the change is also kept as a patch in `tools/readexe/patches/` for upstreaming, and was never pushed anywhere). For each LE module it prints the header, object table and page map, resident and non-resident names, entry table, imports, fixup counts (`-v`: every record), the VxD's Device Descriptor Block found through its `*_DDB` export with pointer fields resolved through the fixups, and (`-s`) the dynamic-link IDs that follow each INT 20h in executable objects. `tools/vxdmap.sh` runs it over both reference installs and writes **docs/win386-services.md**: the per-VxD tables (name, ID, version, init order, service count, API presence, objects, fixups), a caller x callee matrix, and for every VxD service that any module calls, its callers. That file is the recreation map; the highlights are below.

Sources: the LE layout from IBM's "LX - Linear eXecutable Module Format Description" (LE differs only in the 4-byte object page entries and the last-page-size field) and the Open Watcom linker documentation; the DDB (VxD_Desc_Block) fields and the INT 20h convention (INT 20h, then a dword: service number in the low word, bit 15 = jump, device ID in the high word) from the Windows 3.1 DDK's Virtual Device Adaptation Guide, each checked against the files (the DDB's name field holds the blank-padded device name, SDK version 030Ah, and its ID equals the one at LE header offset C0h); service names from Ralf Brown's Interrupt List release 61 (INT 20h, tables 01265 ff.), freely distributed documentation. Nothing was disassembled: only the dword after each CD 20 byte pair in a code object was read. Pairs that are not dynamic links are dropped when the device ID is unknown or the service number is not below that device's DDB service count (3 pairs in VMM, 2 in VDD, 2 in DOSMGR, 1 in VSHARE).

**Format facts observed:**
- Each W3 table entry points at a bare LE header (no MZ stub); the entry's size is the LE header + loader + fixup sections (e.g. INT13: fixup page table at 110h + fixup size 11Bh = 22Bh). The LE's data-pages offset (80h) is from the start of WIN386.EXE, page-aligned (200h). The non-resident name table offset is stale: the names are not in WIN386.EXE (the offset is relative to the original stand-alone file; the bytes there belong to something else), so a built-in VxD's DDB is found as its only export, entry 1. The resident name is the DDB name (`VMM` for the WIN386 module).
- Every module has the LE flags 00008020h (library module, external fixups removed), CPU 386, OS 4 (Windows 386), page size 1000h, and two to four objects: preloaded 32-bit code (flags 2045h: the locked code and data), discardable 32-bit code (2015h: the init code and data, VxD_ICODE/IDATA in DDK terms), in some external VxDs 32-bit code that is neither (2005h: IOS, VSHARE; UNVERIFIED: pageable), and, in 14 built-in and several external modules, a 16-bit object (1005h): the real-mode initialisation code, run by the loader before protected mode (UNVERIFIED that this is what it is: the DDK's VxD_REAL_INIT segment; it is the only USE16 object). Relocation bases vary (0, 1000h-aligned, 10000h steps in VSHARE) and do not matter to a loader that applies the fixups. No module has imports: all cross-VxD calls are INT 20h dynamic links.
- The device ID is also in the LE header at C0h (word), the DDK version at C2h (030Ah); the DDB holds the SDK version (030Ah, 0300h for VTDAPI and VPMTD).
- The SYSTEM.INI name is the W3 module name, not the device name: `*vddvga` loads the module whose DDB is `VDD`, `*wshell` the `SHELL` device, `*reboot` `Reboot`, `*pageswap` `PageSwap`, `*pagefile` `PageFile`. Programs find VxDs by device ID (INT 2Fh AX=1684h), so w16 must keep the device names and IDs as well as the W3 module names.

**Windows 3.1 VxDs in init order** (the order the VMM sends the control messages; the DDB's init order, then W3 order for equal values; UNVERIFIED tie-break): VMM 00000000h, QEMMFix 00000000h (no ID), VPICD 0C000000h, VTD 14000000h, PageFile 18000000h, PageSwap 1C000000h, PARITY 20000000h, Reboot 24000000h, EBIOS 26000000h, VDD 28000000h, VSD 2C000000h, VCD 30000000h, VMD 34000000h, VKD 38000000h, BLOCKDEV 40000000h, INT13 42000000h, VFD 44000000h, VMCPD 48000000h, BIOSXLAT 50000000h, VNETBIOS 54000000h, DOSMGR 58000000h, DOSNET 5C000000h, VMPOLL 64000000h, CDPSCSI, COMBUFF and VTDAPI 80000000h (Undefined_Init_Order), VDMAD 90000000h, V86MMGR A0000000h, WDCTRL A8000000h, SHELL B0000000h.

**Device IDs and services provided** (Windows 3.1): VMM 0001h 242 services; VPICD 0003h 21; VDMAD 0004h 24; VTD 0005h 8; V86MMGR 0006h 21; PageSwap 0007h 7; PARITY 0008h 0; Reboot 0009h 0; VDD 000Ah 14; VSD 000Bh 2; VMD 000Ch 3; VKD 000Dh 15; VCD 000Eh 4; BLOCKDEV 0010h 7; VMCPD 0011h 3; EBIOS 0012h 2; BIOSXLAT 0013h 0; VNETBIOS 0014h 1; DOSMGR 0015h 12; SHELL 0017h 6; VMPOLL 0018h 3; DOSNET 001Ah 3; VFD 001Bh 0; INT13 0020h 5; PageFile 0021h 7; VTDAPI 0442h 0. WDCTRL, COMBUFF, CDPSCSI and QEMMFix have no ID (Undefined_Device_ID: they offer no services or API). WfW 3.11 adds VPD 000Fh (0 services) and VWC (no ID), its VCD has 9 services and VNETBIOS 4, and its external VxDs are IOS 0010h (15; it replaces BLOCKDEV's ID), VXDLDR 0027h (6), VCOMM 002Bh (26), VPMTD 003Ah, VTDAPI 0442h, VSHARE 0483h (7), IFSMgr 0484h (61), VFAT 0486h, VCache 048Bh (14), SERIAL and LPT (no ID).

**V86/PM API entry points** (what INT 2Fh AX=1684h hands out; Windows 3.1): V86 and PM: VPICD, VTD, VMD; PM only: VDD, VKD, Reboot, PageFile, VCD, SHELL, VTDAPI; V86 only: DOSMGR. The VMM itself has none.

**Service usage highlights** (Windows 3.1 set; details in docs/win386-services.md): 199 of the VMM's 242 services are called by some module (the VMM's own module calls 134 of them through INT 20h itself), so a VMM that runs Microsoft's VxDs needs nearly all of them; the busiest callers are DOSMGR (89 VMM services), V86MMGR (82) and SHELL (73). VTD, PageSwap, VSD, VMCPD, EBIOS, SHELL, VMPOLL, DOSNET, INT13 and PageFile have every service called; nobody calls VPICD 3 of 21, VDMAD 2 of 24, V86MMGR 3 of 21, VKD 2 of 15. The VMM calls into VPICD, VTD, V86MMGR, PageSwap, VMCPD, VNETBIOS, DOSMGR and SHELL: these are the devices it cannot boot without (with its own ID, VMM, and the ones whose services it uses in init).

**Plan from this map.** The VMM services needed first are those the core VxDs call during Sys_Critical_Init/Device_Init/Init_Complete, i.e. the services of the boot set (VMM, VPICD, VTD, V86MMGR, DOSMGR, VDD, VKD, SHELL, PageSwap/PageFile, BLOCKDEV, VDMAD). Two routes: (a) w16's VMM loads Microsoft's VxDs, and then must provide all 199 called services with exactly the documented behaviour; (b) w16 also replaces the VxDs, and the set narrows to what our own VxDs need, but every service number must still exist for third-party .386 files (VTDAPI calls 20 VMM services and 4 VTD ones; IFSMGR 12 of its own...). ARCHITECTURE.md §12 tracks which route each piece takes.

