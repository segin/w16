# WIN386 work: handover note

Kept current as the work goes, so it survives a context reset. Spec: ARCHITECTURE.md §12, docs/win386-vxds.md.

## Goal and plan (the user's, 2026-11-13)

386 Enhanced mode boots to Program Manager and is usable (mouse, keyboard, display, exit) with w16's WIN386.EXE: first with Microsoft's KRNL386/USER/GDI/drivers, then with as many w16 parts as run there.
1. Fix the INT 74h reflection with PM selectors in DS/ES, and the bad-CS resume after INT 2Fh 4003h. **Done.**
2. w16's own VxD services that the System VM's drivers call (VMD, VDD, Reboot, VKD, VPICD, VTD...), observed black-box (probes) and from the DDK docs. **The boot's calls done** (VX5); Ctrl+Alt+Del, DOS boxes etc. later.
3. The VxD loader: SYSTEM.INI [386Enh] device=, init-order control messages, INT 20h dynamic linking, third-party .386 files. **Next.**
4. Reliability: 10/10 on installation-05, then `win386` in the default boot-test cases; a case with mouse/keyboard input and exit. **Done**: 20/20 boots, 10/10 input runs; `win386` and `win386input` are default cases.

## How to run

- Build: `make win386` (build/win386/WIN386.EXE); probes `make vxprobes` (VX1-VX5).
- One boot: `tests/win386.sh -n -o DIR` (w16 only; without -n also Microsoft's and a pixel compare). `-i w16:vmmtrace=1` for the detailed COM1 trace, `-c ARGS` WIN's arguments, `-x "xdotool words;..."` input after the boot (in DOSBox-X xdotool's pointer motion does not reach the guest; keys do).
- Mouse/keys/exit: boot-test case `win386input` (tests/scripts/input/win386input.txt via tests/inputrun.sh, plain DOSBox). By hand: `tests/inputrun.sh -s -b 60 -t 240 SCRIPT OUT -r -c "WIN /3 /B" -f .tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE` (dropin.sh deletes KRNL386.EXE; -f puts it back). For the VMM trace in such a run: `DROPIN_MACHINE=svga_s3 DROPIN_CONF="[serial]<newline>serial1=file file:/abs/com1.txt"` and dropin's `-p 386` (DOSBox-X; plain DOSBox 0.74 has no serial file).
- Heavy runs on installation-05: rsync the worktree to /mnt/build/w16-runs/<worktree name> (as tools/remote-test.sh does, .tools linked to /mnt/build/w16/.tools), `make win386 dos` there; `tools/remote-test.sh -b win386 win386input` for the boot-test cases.
- Observing 3.1: `tests/refrun.sh build/dos/VX5.EXE /3` (VxD API replays; `REFRUN_FILES=build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV` for w16's mouse driver), `tests/vxobs.sh` scenarios (VX1, VX4).
- DDK docs: `.tools/docs/ddag31` (drivers) and `.tools/docs/vdag31` (Virtual Device Adaptation Guide: VMM services, control messages, Client_Reg_Struc; fetched 2026-11-13 from dos-help.soulsphere.org/vdag31qh.hlp, the same archive as ddag31). Facts only (ARCHITECTURE.md §3).

## Fixed (2026-11-13)

- IRQ 8-15 vectors (70h-77h) chained from a PM handler to the host's default were reflected as software interrupts: DS/ES translation messages for every mouse interrupt, and the V86 handler's registers copied into the interrupted code (the "bad CS" panic).
- MOUSE.DRV's INT 15h C207h (PS/2 handler) from PM: the BIOS gets a V86 breakpoint (W386_BP_PS2CB) that runs the PM handler on the host stack with status, X, Y, 0.
- Exit Windows: KRNL386 ends with INT 21h AX=0001h; treated as 4C00h, after making the client's PSP current (as w16's DOSX).
- Nested interrupts on the host stack overwrote the running handler's pushed data (the intermittent #GP/hang): hstk_base/hstk_run/dpmi_note_pm in dpmi.c.
- VMM heartbeat in the trace every 20 s of ticks.

## Observed, not acted on

- PM API entry offsets (003Bh:0360h + 2n) are handed out in the order devices are first asked for, not device order. w16 uses device order.

## Next: the VxD loader (step 3), plan

1. A test-only probe VxD of w16's own (VXP.386, nasm + wlink `format windows vxd`) that logs every control message (EAX, EBX, ESI) and a few VMM service results (Get_VMM_Version 0001h:0000h, Get_Cur_VM_Handle 0001h:0001h, Get_Sys_VM_Handle 0001h:0003h) to COM1; run under Microsoft's WIN386 with `device=vxp.386` added to [386Enh] (iniset.py replaces the first device= key: add an append mode or a distinct key) to get the reference order and values.
2. The loader reads [386Enh] device= (and mouse=, display=, keyboard=, network=, ebios=): `*name` accepted when w16 provides it, else the observed "Cannot find a device file" message; files loaded from SYSTEM as LE (reuse the WIN386 module loader), handed to the VMM.
3. The VMM: DDBs in init order, Sys_Critical_Init / Device_Init / Init_Complete (EBX = System VM handle, ESI = the command tail), INT 20h links patched to calls through service tables, the "Invalid VxD dynamic link" message for missing ones; System_Exit / Sys_Critical_Exit at exit.
4. Then VTDAPI.386 (Microsoft's external, default device=vtdapi.386): 20 VMM and 4 VTD services.
