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

The same modules plus **VPD** (printer) and **VWC**. The order is: WIN386, INT13, WDCTRL, VMD, VPD, VWC, DOSNET, VNETBIOS, EBIOS, VDDVGA, VKD, VPICD, VTD, REBOOT, VDMAD, VSD, V86MMGR, PAGESWAP, DOSMGR, VMPOLL, WSHELL, BLOCKDEV, PAGEFILE, VFD, PARITY, BIOSXLAT, VCD, VMCPD, COMBUFF, CDPSCSI, QEMMFIX. WfW 3.11 also ships many external .386 VxDs; list them from its SYSTEM.INI before starting there.

## Still to read (headers only, no code)

readexe doesn't parse LE yet. For each module, the next step is to read from its LE header and Device Descriptor Block: the device ID, init order, version, the exported service table size, and the V86/PM API entry points. A VxD's ID, init order and service table are its interface to other VxDs and to Windows programs (INT 2Fh AX=1684h). Clean-room rules as elsewhere (ARCHITECTURE.md §3): header and DDB fields are format facts; the code is never disassembled; behaviour is observed black-box.
