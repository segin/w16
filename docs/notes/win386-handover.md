# WIN386 work: handover note

Kept current as the work goes, so it survives a context reset. Spec: ARCHITECTURE.md §12, docs/win386-vxds.md.

## Goal and plan (the user's, 2026-11-13)

386 Enhanced mode boots to Program Manager and is usable (mouse, keyboard, display, exit) with w16's WIN386.EXE: first with Microsoft's KRNL386/USER/GDI/drivers, then with as many w16 parts as run there.
1. Fix the INT 74h reflection with PM selectors in DS/ES, and the bad-CS resume after INT 2Fh 4003h. **Done.**
2. w16's own VxD services that the System VM's drivers call (VMD, VDD, Reboot, VKD, VPICD, VTD...), observed black-box (probes) and from the DDK docs. **The boot's calls done** (VX5); Ctrl+Alt+Del, DOS boxes etc. later.
3. The VxD loader: SYSTEM.INI [386Enh] device=, init-order control messages, INT 20h dynamic linking, third-party .386 files. **First part done** (ARCHITECTURE.md 12.7): VXP.386 sees what it sees under 3.1; Microsoft's VTDAPI.386 loads and answers.
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
- Nested interrupts on the V86 reflection stack overwrote the interrupted V86 code's stack (hang while fonts loaded, ~1 in 7 under load): traps from V86 code on that stack lower its start (vmm.c trap()); stack 8 KB with an overflow check. 40/40 boots after.
- VMM heartbeat in the trace every 20 s of ticks: V86/PM position, code bytes, stack, registers, IRQ counts, PIC masks. Use it on any hang.

## Observed, not acted on

- PM API entry offsets (003Bh:0360h + 2n) are handed out in the order devices are first asked for, not device order. w16 uses device order.

## The VxD loader (step 3): state

- Done: loader reads [386Enh] device=/mouse=/display=/keyboard=/network=/ebios=, loads files (LE, source-list fixups), *names accepted as w16 built-ins; VMM (win386/vmm/vxd.c) sorts by init order, sends 0,1,2,3 / 15h,16h / 4,5,6 as observed with VXP; INT 20h patched to CALL/JMP [slot]; services: VMM 0-5, lists 9Bh-A5h, VTD 0,2,3,4; 1684h for loaded devices' PM API.
- Probe: `tests/vxpprobe.sh ms|w16` (DOSBox-X, keys for Exit Windows); `make vxprobes` builds VXP.386.
- Next: more VMM services as devices need them (docs/win386-services.md), the dynamic-link failure shown on screen and Windows ended as 3.1 does, w16's VxDs as their own W3 modules, Create_VM etc. for DOS boxes, timer reprogramming for VTD_Begin_Min_Int_Period.

## Built-in devices as modules (2026-11-14, coordinator's order: VPICD first, then VKD, VMD, REBOOT)

- **VPICD done** (ARCHITECTURE.md 12.8): module VPICD in the W3 table (win386/vxd/vpicd.c, vpicdddb.asm; runtime vxdrt.asm/vxdrt.inc; shared w16vxd.h). Owns the 8259s; the VMM hands every IRQ to it (struct vpicd_link via DDB ref data); VTD (in the VMM) takes IRQ 0 through it. All 21 services. VXP results equal 3.1's except IRQ 1/12 (no VKD/VMD yet). Not yet: virtual PIC per VM (needs IOPL 0 for PM clients + port trapping) — the big next VPICD step.
- Bug met on the way: a request must be cleared *before* it is simulated (the VM's handler runs inside deliver(); traps on the way re-delivered it: a timer storm).

- **VMD done** (win386/vxd/vmd.c, vmdddb.asm, module VMD): IRQ 12 taken from VPICD at Device_Init, every mouse interrupt passed to the System VM; services 0-2 and PM API as observed (VX5 replay and VXP equal 3.1's; IRQ 12's status at exit shows ECX=4, physically masked, where 3.1 showed 0: UNVERIFIED why). Input runs pass.

### Facts for VKD / VMD / REBOOT (observed on 3.1 with VXP and screenshots)
- DDBs (readexe): VKD 000Dh 2.00 init 38000000h, 15 services, PM API; VMD 000Ch 3.00 init 34000000h, 3 services, V86+PM API; Reboot 0009h 2.00 init 24000000h, 0 services, PM API. W3 module names VKD, VMD, REBOOT.
- VKD services (RBIL names, numbers): 0 Get_Version, 1 Define_Hot_Key, 2 Remove_Hot_Key, 3 Local_Enable, 4 Local_Disable, 5 Reflect_Hot_Key, 6 Cancel_Hot_Key_State, 7 Force_Keys, 8 Get_Kbd_Owner, 9 Define_Paste_Mode, 0Ah Start_Paste, 0Bh Cancel_Paste, 0Ch Get_Msg_Key, 0Dh Peek_Msg_Key, 0Eh Flush_Msg_Key_Queue. VMD: 0 Get_Version, 1 Set_Mouse_Type, 2 Get_Mouse_Owner.
- Results: VKD_Get_Version AX=0200h; Get_Kbd_Owner = System VM; Peek_Msg_Key none (EAX=0, ZF). VMD_Get_Version AX=0300h CF clear; Get_Mouse_Owner 0 at init, System VM at System_Exit; Set_Mouse_Type(EAX=-74h, ECX=4) EAX=1 CF set. VPICD: IRQ 1 owned (VKD) from init; IRQ 12 owned only later (VMD takes it when MOUSE.DRV registers).
- PM APIs (VX5): VMD 0100h fails CF set registers unchanged; VDD 000Ch AX=BX; Reboot 0201h set / 0202h get KERNEL's Ctrl+Alt+Del handler (ES:DI; DS:SI sanity byte); RBIL also: 0100h warm boot, 0203h "not responding" box, 0204h PM INT 01h handler.
- Ctrl+Alt+Del with nothing hung (tests/scripts/input/win386cad.txt, plain DOSBox inputrun): text mode 80x25, background blue (attr 1Fh: white on blue); title row 6 cols 36-42 " Windows " with attr 71h (blue on light grey); text at col 5 rows 8,9,10,12,13,15,16 and row 17 at col 8; row 19 col 27 "Press any key to continue _" (cursor after). Text: rows 8-10 "Although you can use CTRL+ALT+DEL to quit an application that has" / "stopped responding to the system, there is no application in this" / "state."; 12-13 "To quit an application, use the application's quit or exit command," / "or choose the Close command from the Control menu."; 15-17 "*  Press any key to return to Windows." / "*  Press CTRL+ALT+DEL again to restart your computer. You will" / "   lose any unsaved information in all applications." (w16: "w16" for "Windows" in user text). A key returns to Windows with the screen exactly as before (no repaint visible).
- Design notes: hot keys in VKD's IRQ 1 Hw_Int_Proc (read port 60h; KEYBOARD.DRV re-reads 60h, which keeps its value until the controller's next byte; PM port 60h cannot be trapped while IOPL 3); REBOOT defines Ctrl+Alt+Del with VKD_Define_Hot_Key at Device_Init; the message screen needs VMM Exec_Int (INT 10h mode 3 and back) plus saving/restoring VGA registers, planes and DAC at ring 0. VMD: virtualise IRQ 12 when MOUSE.DRV registers (observed), pass to the System VM; the INT 15h C207h handler bridge stays in dpmi.c.
