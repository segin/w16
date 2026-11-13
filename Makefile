# w16 build. Requires OpenWatcom v2 (WATCOM, default /opt/watcom), nasm,
# and a host C compiler. `make test` additionally needs DOSBox, DOSBox-X (the
# floating-point tests) and HDPMI16 (tools/fetch-hdpmi.sh).

WATCOM  ?= /opt/watcom
export WATCOM
export PATH := $(WATCOM)/binl64:$(PATH)

B       := build
DOS     := $(B)/dos

HOSTCC  ?= cc
NASM    ?= nasm
WCC     := wcc
WLINK   := wlink

INC     := -Iinclude -Ikernel
# KERNEL: large model, SS != DS, 286, no stack checks, no default libraries
KCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=_TEXT $(INC) -I$(B)/kernel
# bootstrap: small model, 8086, real mode
BCFLAGS := -q -bt=dos -ms -0 -s -zl -ox -wx -we $(INC)
# test applications: large model Windows programs, no C runtime
TCFLAGS := -q -bt=windows -ml -2 -s -zl -ox -wx -we \
           -i=$(WATCOM)/h -i=$(WATCOM)/h/win
# shell programs: medium model, 286, smart callbacks (DS from SS)
SCFLAGS := -q -bt=windows -mm -2 -s -zl -ox -wx -we -zWs \
           -i=$(WATCOM)/h -i=$(WATCOM)/h/win
NFLAGS  := -f obj -Ikernel/inc/
WRC     := wrc

KOBJS   := $(B)/kernel/kentry.o $(B)/kernel/kmain.o $(B)/kernel/util.o \
           $(B)/kernel/dpmi.o $(B)/kernel/global.o $(B)/kernel/module.o \
           $(B)/kernel/thunk.o $(B)/kernel/task.o $(B)/kernel/local.o \
           $(B)/kernel/string.o $(B)/kernel/loader.o $(B)/kernel/procinst.o \
           $(B)/kernel/mdb.o $(B)/kernel/resource.o $(B)/kernel/atom.o $(B)/kernel/file.o \
           $(B)/kernel/profile.o $(B)/kernel/dosx.o $(B)/kernel/winboot.o $(B)/kernel/selector.o $(B)/kernel/kmisc.o $(B)/kernel/fault.o \
           $(B)/kernel/toolhelp.o $(B)/kernel/khook.o $(B)/kernel/kmisc2.o
BOBJS   := $(B)/boot/boot0.o $(B)/boot/boot.o
W87OBJS := $(B)/win87em/win87em.o $(B)/win87em/emu.o
TESTS   := TK1 TK2 TK3 TK4 TK5 TK6 TK6N TK7 TK8 TK8T1 TK8T2 TK8T3 TK8T4 TK8T5 TK9 TK10 \
           TK11 TK12 TK13 TK14 TK15 TK16 TK17 TK17A TK19
# probes for the Windows boot (tests/dropin.sh); not in make test
# DX probes (tests/dosxtest.sh): DOS extender probes in the KERNEL slot
DXPROBES := DX1 DX2 DX4 DX5 DX8
DROPIN   := TK20 TK21 TK22 TK23 TK24 TK25 TK26 TK27 TK28 TK29 TK30 TK31 TK32 TK33 TK34 TK35 TK36 TK37 TK38 TK39 TK40 TK41 TK42 TK43 TK44 TK45 TK46 TK47 TK48 TK49 TK50 TK51 TK51C TK52
# DLLs and files TK5 uses
TESTDLLS := TKDLL.DLL TKDLB.DLL TKID.TPL TKBAD.DLL TKNODEP.DLL TKNOD.DLL \
            TKUSEX.DLL TKEXEM.EXE TK15A.EXE TK16A.EXE TK16B.EXE TK16C.EXE TK16S.EXE TK16I.EXE
# tools that run inside a (reference) Windows: tests/refrun.sh
WINTOOLS := SNAP SNAP2 TW2 TEXP3 TEXP2 TDDE TICONS THELP CA1
SHELL_PM := $(B)/shell/start.o $(B)/shell/progman/main.o \
            $(B)/shell/progman/group.o $(B)/shell/progman/dialogs.o \
            $(B)/shell/progman/util.o $(B)/shell/progman/hotkey.o \
            $(B)/shell/progman/compat2.o $(B)/shell/progman/mdi2.o \
            $(B)/shell/progman/dde.o $(B)/shell/progman/iconx.o \
            $(B)/shell/progman/browse.o $(B)/shell/progman/help.o
ICONS    := $(foreach n,00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 \
                         20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 \
                         40 41 42 43 44 45,$(B)/shell/res/stock$(n).ico)

.SECONDARY:

.PHONY: all host dos shell shell2 test clean install-tree
all: host dos shell

host: $(B)/host/nedump
dos: $(DOS)/KRNL286.EXE $(DOS)/WIN87EM.DLL $(TESTS:%=$(DOS)/%.EXE) $(TESTDLLS:%=$(DOS)/%) \
     $(WINTOOLS:%=$(DOS)/%.EXE) \
     $(DOS)/KEYFEED.COM $(DOS)/POWEROFF.COM $(DOS)/WINENV.COM $(B)/hdd/MBR.BIN \
     $(DOS)/TK18.COM $(DROPIN:%=$(DOS)/%.EXE) $(DOS)/PENWIN.DLL $(DOS)/MS1.COM \
     $(DOS)/DOSX.EXE $(DOS)/DOSXT.EXE $(DXPROBES:%=$(DOS)/%.COM) $(DOS)/DX6.EXE $(DOS)/DX7.EXE $(DOS)/DXD.COM

# ---- host tools ----
$(B)/host/nedump: tools/nedump.c include/w16/ne.h include/w16/types.h
	@mkdir -p $(@D)
	$(HOSTCC) -std=c99 -Wall -Wextra -O2 -Iinclude -o $@ $<

# ---- bootstrap (MZ stub) ----
$(B)/boot/%.o: kernel/boot/%.asm kernel/inc/intcall.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/boot/%.o: kernel/boot/%.c kernel/*.h include/w16/*.h
	@mkdir -p $(@D)
	$(WCC) $(BCFLAGS) -fo=$@ $<
$(B)/boot/boot.exe: $(BOBJS)
	$(WLINK) option quiet format dos name $@ option map=$(B)/boot/boot.map \
	    file { $(BOBJS) }

# ---- KERNEL ----
$(B)/kernel/%.o: kernel/%.asm kernel/inc/intcall.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/kernel/kapi.h: tools/kapiprag.py kernel/*.c kernel/*.h
	@mkdir -p $(@D)
	python3 tools/kapiprag.py $@ kernel/*.c kernel/*.h
$(B)/kernel/%.o: kernel/%.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) -fo=$@ $<
# KERNEL's code outgrew one segment: the Windows boot, the exports for
# Microsoft's modules and the profiles go in a second code segment,
# BOOT_TEXT
KBOOT_C := winboot dosx kmisc selector fault toolhelp kmisc2 profile
$(KBOOT_C:%=$(B)/kernel/%.o): $(B)/kernel/%.o: kernel/%.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) -nt=BOOT_TEXT -fo=$@ $<
$(DOS)/KRNL286.EXE: $(KOBJS) $(B)/boot/boot.exe kernel/kernel.lnk
	@mkdir -p $(@D)
	$(WLINK) @kernel/kernel.lnk name $@ option stub=$(B)/boot/boot.exe \
	    option map=$(B)/kernel/krnl286.map file { $(KOBJS) }

# ---- WIN87EM.DLL: floating-point support (win87em/) ----
$(B)/win87em/%.o: win87em/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/win87em/%.o: win87em/%.c win87em/emu.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) -fo=$@ $<
$(DOS)/WIN87EM.DLL: $(W87OBJS) win87em/win87em.lnk
	@mkdir -p $(@D)
	$(WLINK) @win87em/win87em.lnk name $@ option map=$(B)/win87em/win87em.map \
	    file { $(W87OBJS) }

# ---- test applications: TKn.EXE = tests/apps/start.asm + tests/apps/tkn.c ----
$(B)/tests/start.o: tests/apps/start.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/tests/%.o: tests/apps/%.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/TK%.EXE: $(B)/tests/start.o $(B)/tests/tk%.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk$*.map file { $(B)/tests/start.o $(B)/tests/tk$*.o }

$(B)/tests/tk3.o: tests/apps/tk3ref.h tests/apps/tk3ref286.h tests/apps/tklog.h
$(B)/tests/tk4.o: tests/apps/tk4ref.h tests/apps/tklog.h
$(B)/tests/tk5.o: tests/apps/tk5ref.h tests/apps/tklog.h
$(B)/tests/tk11.o: tests/apps/tk11ref.h tests/apps/tklog.h
$(B)/tests/tk13.o: tests/apps/tk13ref.h tests/apps/tklog.h
$(B)/tests/tk14.o: tests/apps/tk14ref.h tests/apps/tklog.h

# TK11.EXE: the in-memory module table probe (uses TK5's DLLs)
$(DOS)/TK11.EXE: $(B)/tests/start.o $(B)/tests/tk11.o $(B)/tests/prolog.o \
                tests/apps/app.lnk tests/apps/tk11.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk11.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk11.map \
	    file { $(B)/tests/start.o $(B)/tests/tk11.o $(B)/tests/prolog.o }
$(B)/tests/tk6.o: tests/apps/tk6ref.h tests/apps/tklog.h

# TK15.EXE: the task probe; it starts TK15A.EXE (tk15a.c, own start-up
# code tk15s.asm), which imports TKDLL.DLL and TKDLB.DLL
$(B)/tests/tk15.o: tests/apps/tk15ref.h tests/apps/tk15ref286.h tests/apps/tklog.h tests/apps/tk15.h
$(B)/tests/tk15a.o: tests/apps/tk15.h
$(DOS)/TK15.EXE: $(B)/tests/start.o $(B)/tests/tk15.o \
                tests/apps/app.lnk tests/apps/tk15.lnk tests/apps/tk15k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk15.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk15.map file { $(B)/tests/start.o $(B)/tests/tk15.o }
$(DOS)/TK15A.EXE: $(B)/tests/tk15s.o $(B)/tests/tk15a.o \
                 tests/apps/app.lnk tests/apps/tk15a.lnk tests/apps/tk15k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk15a.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk15a.map file { $(B)/tests/tk15s.o $(B)/tests/tk15a.o }

# TK22.EXE: the Windows boot probe (imports DiagOutput, KERNEL.340)
$(B)/tests/tk22.o: tests/apps/tk22ref.h tests/apps/tklog.h
$(DOS)/TK22.EXE: $(B)/tests/start.o $(B)/tests/tk22.o tests/apps/app.lnk tests/apps/tk22.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk22.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk22.map file { $(B)/tests/start.o $(B)/tests/tk22.o }

# TK20.EXE: the first window (imports InitApp, USER.5)
$(B)/tests/tk20.o: tests/apps/tk20ref.h tests/apps/tklog.h
$(DOS)/TK20.EXE: $(B)/tests/start.o $(B)/tests/tk20.o tests/apps/app.lnk tests/apps/tk20.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk20.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk20.map file { $(B)/tests/start.o $(B)/tests/tk20.o }

# TK21.EXE: the system timer (imports SYSTEM.2, 3, 6)
$(B)/tests/tk21.o: tests/apps/tk21ref.h tests/apps/tklog.h
$(DOS)/TK21.EXE: $(B)/tests/start.o $(B)/tests/tk21.o tests/apps/app.lnk tests/apps/tk21.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk21.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk21.map file { $(B)/tests/start.o $(B)/tests/tk21.o }

# TK23.EXE: which set-up step stops the system timer
$(B)/tests/tk23.o: tests/apps/tk23ref.h tests/apps/tklog.h
$(DOS)/TK23.EXE: $(B)/tests/start.o $(B)/tests/tk23.o tests/apps/app.lnk tests/apps/tk23.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk23.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk23.map file { $(B)/tests/start.o $(B)/tests/tk23.o }

# TK24.EXE: GetTaskQueueDS/ES at register level (tk24s.asm)
$(B)/tests/tk24.o: tests/apps/tk24ref.h tests/apps/tklog.h
$(DOS)/TK24.EXE: $(B)/tests/start.o $(B)/tests/tk24.o $(B)/tests/tk24s.o tests/apps/app.lnk tests/apps/tk24.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk24.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk24.map file { $(B)/tests/start.o $(B)/tests/tk24.o $(B)/tests/tk24s.o }

# TK25.EXE: GetCodeInfo
$(B)/tests/tk25.o: tests/apps/tk25ref.h tests/apps/tklog.h
$(DOS)/TK25.EXE: $(B)/tests/start.o $(B)/tests/tk25.o tests/apps/app.lnk tests/apps/tk25.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk25.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk25.map file { $(B)/tests/start.o $(B)/tests/tk25.o }

# TK26.EXE: icons from a program's own resources (tk26.rc, stock icons)
$(B)/tests/tk26.o: tests/apps/tk26ref.h tests/apps/tklog.h
$(B)/tests/tk26.res: tests/apps/tk26.rc $(B)/shell/res/stock00.ico $(B)/shell/res/stock01.ico
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=$(B)/shell/res -fo=$@ $<
$(DOS)/TK26.EXE: $(B)/tests/start.o $(B)/tests/tk26.o $(B)/tests/tk26.res tests/apps/app.lnk tests/apps/tk26.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk26.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk26.map option resource=$(B)/tests/tk26.res \
	    file { $(B)/tests/start.o $(B)/tests/tk26.o }

# TK27.EXE: the cursor block LoadCursor gives
$(B)/tests/tk27.o: tests/apps/tk27ref.h tests/apps/tklog.h
$(DOS)/TK27.EXE: $(B)/tests/start.o $(B)/tests/tk27.o tests/apps/app.lnk tests/apps/tk27.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk27.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk27.map file { $(B)/tests/start.o $(B)/tests/tk27.o }

# TK28.EXE: cursors from a program's resources (tk28.rc; the cursor
# file is made by tests/apps/tk28cur.py)
$(B)/tests/tk28.cur: tests/apps/tk28cur.py
	@mkdir -p $(@D)
	python3 $< $@
$(B)/tests/tk28.o: tests/apps/tk28ref.h tests/apps/tklog.h
$(B)/tests/tk28.res: tests/apps/tk28.rc $(B)/tests/tk28.cur
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=$(B)/tests -fo=$@ $<
$(DOS)/TK28.EXE: $(B)/tests/start.o $(B)/tests/tk28.o $(B)/tests/tk28.res tests/apps/app.lnk tests/apps/tk28.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk28.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk28.map option resource=$(B)/tests/tk28.res \
	    file { $(B)/tests/start.o $(B)/tests/tk28.o }

# TK29.EXE: module usage counts under Windows
$(B)/tests/tk29.o: tests/apps/tk29ref.h tests/apps/tklog.h
$(DOS)/TK29.EXE: $(B)/tests/start.o $(B)/tests/tk29.o tests/apps/app.lnk tests/apps/tk29.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk29.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk29.map file { $(B)/tests/start.o $(B)/tests/tk29.o }

# TK30.EXE: resource handlers of DLLs
$(B)/tests/tk30.o: tests/apps/tk30ref.h tests/apps/tklog.h
$(DOS)/TK30.EXE: $(B)/tests/start.o $(B)/tests/tk30.o tests/apps/app.lnk tests/apps/tk30.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk30.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk30.map file { $(B)/tests/start.o $(B)/tests/tk30.o }

# TK31.EXE: which registers KERNEL's exports keep (tk31s.asm)
$(B)/tests/tk31.o: tests/apps/tk31ref.h tests/apps/tklog.h
$(DOS)/TK31.EXE: $(B)/tests/start.o $(B)/tests/tk31.o $(B)/tests/tk31s.o tests/apps/app.lnk tests/apps/tk31.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk31.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk31.map file { $(B)/tests/start.o $(B)/tests/tk31.o $(B)/tests/tk31s.o }

# TK32.EXE: the task-exit signal (tk32s.asm)
$(B)/tests/tk32.o: tests/apps/tk32ref.h tests/apps/tklog.h
$(DOS)/TK32.EXE: $(B)/tests/start.o $(B)/tests/tk32.o $(B)/tests/tk32s.o tests/apps/app.lnk tests/apps/tk32.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk32.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk32.map file { $(B)/tests/start.o $(B)/tests/tk32.o $(B)/tests/tk32s.o }

# TK33.EXE: stands in for WINOLDAP.MOD (how KERNEL starts DOS programs)
$(B)/tests/tk33.o: tests/apps/tk33ref.h tests/apps/tklog.h
$(DOS)/TK33.EXE: $(B)/tests/start.o $(B)/tests/tk33.o tests/apps/app.lnk tests/apps/tk33.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk33.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk33.map file { $(B)/tests/start.o $(B)/tests/tk33.o }

# TK34.EXE: WinOldApCall, KERNEL.151 (tk34s.asm)
$(B)/tests/tk34.o: tests/apps/tk34ref.h tests/apps/tklog.h
$(DOS)/TK34.EXE: $(B)/tests/start.o $(B)/tests/tk34.o $(B)/tests/tk34s.o tests/apps/app.lnk tests/apps/tk34.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk34.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk34.map file { $(B)/tests/start.o $(B)/tests/tk34.o $(B)/tests/tk34s.o }

# TK35.EXE: a task's directory and windir (logs to C:\TK35.LOG)
$(DOS)/TK35.EXE: $(B)/tests/start.o $(B)/tests/tk35.o tests/apps/app.lnk tests/apps/tk35.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk35.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk35.map file { $(B)/tests/start.o $(B)/tests/tk35.o }

# TK2.EXE: an import KERNEL does not have (KERNEL.27, tk2.lnk)
$(DOS)/TK2.EXE: $(B)/tests/start.o $(B)/tests/tk2.o tests/apps/app.lnk tests/apps/tk2.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk2.map file { $(B)/tests/start.o $(B)/tests/tk2.o }

# TK50.EXE: the KERNEL exports w16 lacked, at register level (tk50s.asm)
$(B)/tests/tk50.o: tests/apps/tk50ref.h tests/apps/tk50ref286.h tests/apps/tklog.h
$(DOS)/TK50.EXE: $(B)/tests/start.o $(B)/tests/tk50.o $(B)/tests/tk50s.o tests/apps/app.lnk tests/apps/tk50.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk50.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk50.map file { $(B)/tests/start.o $(B)/tests/tk50.o $(B)/tests/tk50s.o }

# TK51.EXE: THHOOK, ToolHelpHook and TOOLHELP.DLL (tk51s.asm, tk50s.asm);
# TK51C.EXE, the program it starts, only ends
$(B)/tests/tk51.o: tests/apps/tk51ref.h tests/apps/tklog.h
$(DOS)/TK51.EXE: $(B)/tests/start.o $(B)/tests/tk51.o $(B)/tests/tk51s.o $(B)/tests/tk50s.o \
                tests/apps/app.lnk tests/apps/tk51.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk51.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk51.map \
	    file { $(B)/tests/start.o $(B)/tests/tk51.o $(B)/tests/tk51s.o $(B)/tests/tk50s.o }
$(DOS)/TK51C.EXE: $(B)/tests/start.o $(B)/tests/tk51c.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk51c.map file { $(B)/tests/start.o $(B)/tests/tk51c.o }

# TK36.EXE: the volume label through the DOS extender
$(B)/tests/tk36.o: tests/apps/tk36ref.h tests/apps/tklog.h
$(DOS)/TK36.EXE: $(B)/tests/start.o $(B)/tests/tk36.o tests/apps/app.lnk tests/apps/tk36.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk36.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk36.map file { $(B)/tests/start.o $(B)/tests/tk36.o }

# TK37.EXE: who sets a module's icon and cursor handlers
$(B)/tests/tk37.o: tests/apps/tk37ref.h tests/apps/tklog.h
$(DOS)/TK37.EXE: $(B)/tests/start.o $(B)/tests/tk37.o tests/apps/app.lnk tests/apps/tk37.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk37.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk37.map file { $(B)/tests/start.o $(B)/tests/tk37.o }

# TK38.EXE: __GP tables, HasGPHandler, SetSigHandler
$(B)/tests/tk38.o: tests/apps/tk38ref.h tests/apps/tklog.h
$(DOS)/TK38.EXE: $(B)/tests/start.o $(B)/tests/tk38.o tests/apps/app.lnk tests/apps/tk38.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk38.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk38.map file { $(B)/tests/start.o $(B)/tests/tk38.o }

# TK39.EXE: calls given a bad pointer (__GP recovery)
$(B)/tests/tk39.o: tests/apps/tk39ref.h tests/apps/tklog.h
$(DOS)/TK39.EXE: $(B)/tests/start.o $(B)/tests/tk39.o tests/apps/app.lnk tests/apps/tk39.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk39.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk39.map file { $(B)/tests/start.o $(B)/tests/tk39.o }

# TK40.EXE and its PENWIN.DLL: the state a __GP recovery routine gets
$(B)/tests/tk40.o: tests/apps/tk40ref.h tests/apps/tklog.h
$(DOS)/TK40.EXE: $(B)/tests/start.o $(B)/tests/tk40.o tests/apps/app.lnk tests/apps/tk40.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk40.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk40.map file { $(B)/tests/start.o $(B)/tests/tk40.o }
$(B)/tests/tk40d.o: tests/apps/tk40d.asm
	@mkdir -p $(@D)
	nasm -f obj -o $@ $<
$(DOS)/PENWIN.DLL: $(B)/tests/tk40d.o tests/apps/tk40d.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk40d.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk40d.map file { $(B)/tests/tk40d.o }

# TK41.EXE: what K327 (HandleParamError) does (tk41s.asm)
$(B)/tests/tk41.o: tests/apps/tk41ref.h tests/apps/tklog.h
$(DOS)/TK41.EXE: $(B)/tests/start.o $(B)/tests/tk41.o $(B)/tests/tk41s.o tests/apps/app.lnk tests/apps/tk41.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk41.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk41.map file { $(B)/tests/start.o $(B)/tests/tk41.o $(B)/tests/tk41s.o }

# TK42.EXE: per-task interrupt vectors
$(B)/tests/tk42.o: tests/apps/tk42ref.h tests/apps/tklog.h
$(DOS)/TK42.EXE: $(B)/tests/start.o $(B)/tests/tk42.o tests/apps/app.lnk tests/apps/tk42.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk42.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk42.map file { $(B)/tests/start.o $(B)/tests/tk42.o }

# TK43.EXE: BX, CX, ES after the global-heap calls (tk31s.asm)
$(B)/tests/tk43.o: tests/apps/tk43ref.h tests/apps/tklog.h
$(DOS)/TK43.EXE: $(B)/tests/start.o $(B)/tests/tk43.o $(B)/tests/tk31s.o tests/apps/app.lnk tests/apps/tk43.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk43.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk43.map file { $(B)/tests/start.o $(B)/tests/tk43.o $(B)/tests/tk31s.o }

# TK44.EXE: where keyboard interrupts go after the boot (tk44s.asm)
$(B)/tests/tk44.o: tests/apps/tk44ref.h tests/apps/tklog.h
$(DOS)/TK44.EXE: $(B)/tests/start.o $(B)/tests/tk44.o $(B)/tests/tk44s.o tests/apps/app.lnk tests/apps/tk44.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk44.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk44.map file { $(B)/tests/start.o $(B)/tests/tk44.o $(B)/tests/tk44s.o }

# TK45.EXE: the PICs while Windows is idle
$(B)/tests/tk45.o: tests/apps/tk45ref.h tests/apps/tklog.h
$(DOS)/TK45.EXE: $(B)/tests/start.o $(B)/tests/tk45.o tests/apps/app.lnk tests/apps/tk45.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk45.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk45.map file { $(B)/tests/start.o $(B)/tests/tk45.o }

# TK46.EXE: INT 15h C207h with a protected-mode handler (tk46s.asm)
$(B)/tests/tk46.o: tests/apps/tk46ref.h tests/apps/tklog.h
$(DOS)/TK46.EXE: $(B)/tests/start.o $(B)/tests/tk46.o $(B)/tests/tk46s.o tests/apps/app.lnk tests/apps/tk46.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk46.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk46.map file { $(B)/tests/start.o $(B)/tests/tk46.o $(B)/tests/tk46s.o }

# TK47.EXE: INT 15h C207h with a code selector below 1 MB (tk47s.asm)
$(B)/tests/tk47.o: tests/apps/tk47ref.h tests/apps/tklog.h
$(DOS)/TK47.EXE: $(B)/tests/start.o $(B)/tests/tk47.o $(B)/tests/tk47s.o tests/apps/app.lnk tests/apps/tk47.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk47.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk47.map file { $(B)/tests/start.o $(B)/tests/tk47.o $(B)/tests/tk47s.o }

# TK48.EXE: INT 15h C2xxh and INT 33h from protected mode
$(B)/tests/tk48.o: tests/apps/tk48ref.h tests/apps/tklog.h
$(DOS)/TK48.EXE: $(B)/tests/start.o $(B)/tests/tk48.o tests/apps/app.lnk tests/apps/tk48.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk48.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk48.map file { $(B)/tests/start.o $(B)/tests/tk48.o }

# TK49.EXE: writes out MS1.COM's log (tests/dos/ms1.asm)
$(B)/tests/tk49.o: tests/apps/tk49ref.h tests/apps/tklog.h
$(DOS)/TK49.EXE: $(B)/tests/start.o $(B)/tests/tk49.o tests/apps/app.lnk tests/apps/tk49.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk49.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk49.map file { $(B)/tests/start.o $(B)/tests/tk49.o }
# MS1.COM: a real-mode TSR recording the PS/2 pointing-device calls (TK49 reads it)
$(DOS)/MS1.COM: tests/dos/ms1.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# TK16.EXE: task probe, part 2; its helpers TK16A (plain), TK16B (imports
# TKBAD.DLL), TK16C (a second data segment) and TK16S (a 1K stack and a
# 20h-byte heap) are all tk16a.c
$(B)/tests/tk16.o: tests/apps/tk16ref.h tests/apps/tk16ref286.h tests/apps/tklog.h tests/apps/tk15.h tests/apps/tk16.h
$(DOS)/TK16.EXE: $(B)/tests/start.o $(B)/tests/tk16.o \
                tests/apps/app.lnk tests/apps/tk16.lnk tests/apps/tk15k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk16.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk16.map file { $(B)/tests/start.o $(B)/tests/tk16.o }
$(B)/tests/tk16%.o: tests/apps/tk16a.c tests/apps/tk15.h tests/apps/tk16.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) $(if $(filter B,$*),-dTK16_BAD) $(if $(filter C,$*),-dTK16_FAR) \
	    -fo=$@ $<
$(DOS)/TK16%.EXE: $(B)/tests/tk15s.o $(B)/tests/tk16%.o \
                  tests/apps/app.lnk tests/apps/tk16.lnk tests/apps/tk15k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk16.lnk $(if $(filter B,$*),import ID_GET TKBAD) \
	    $(if $(filter S,$*),option stack=1k option heapsize=32) \
	    name $@ libpath $(WATCOM)/lib286/win option map=$(B)/tests/tk16$*.map \
	    file { $(B)/tests/tk15s.o $(B)/tests/tk16$*.o }

# TK16I.EXE: TK16A with its data segment stored iterated
$(DOS)/TK16I.EXE: $(DOS)/TK16A.EXE tools/mkiterated.py
	python3 tools/mkiterated.py $< $@ ABABABABABABABAB

# TK17.EXE: the selector probe, with descriptor helpers in tk17s.asm
# (TK17A: its huge-block part, the same source with TK17A defined)
$(B)/tests/tk17.o: tests/apps/tk17ref.h tests/apps/tk17ref286.h tests/apps/tklog.h
$(B)/tests/tk17A.o: tests/apps/tk17.c tests/apps/tk17aref.h tests/apps/tk17aref286.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -dTK17A -fo=$@ $<
$(DOS)/TK17.EXE $(DOS)/TK17A.EXE: $(DOS)/TK17%.EXE: $(B)/tests/start.o $(B)/tests/tk17%.o \
                $(B)/tests/tk17s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk17$*.map \
	    file { $(B)/tests/start.o $(B)/tests/tk17$*.o $(B)/tests/tk17s.o }

# TK19.EXE: the global heap probe (KERNEL imports by ordinal: tk19k.lnk)
$(B)/tests/tk19.o: tests/apps/tk19ref.h tests/apps/tklog.h
$(DOS)/TK19.EXE: $(B)/tests/start.o $(B)/tests/tk19.o $(B)/tests/tk19n.o \
                tests/apps/app.lnk tests/apps/tk19.lnk tests/apps/tk19k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk19.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk19.map \
	    file { $(B)/tests/start.o $(B)/tests/tk19.o $(B)/tests/tk19n.o }

# TK12.EXE: the resource probe, with the resources of tk12.rc
$(B)/tests/tk12.o: tests/apps/tk12ref.h tests/apps/tk12ref286.h tests/apps/tklog.h
$(B)/tests/tk12.res: tests/apps/tk12.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/TK12.EXE: $(B)/tests/start.o $(B)/tests/tk12.o $(B)/tests/tk12.res \
                tests/apps/app.lnk tests/apps/tk12.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk12.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk12.map option resource=$(B)/tests/tk12.res \
	    file { $(B)/tests/start.o $(B)/tests/tk12.o }

# TK6.EXE: the floating-point probe; tk6.c is built with emulator fixups
# (-fpi), so its floating-point code carries NE OS fixups
$(B)/tests/tk6.o: tests/apps/tk6.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(B)/tests/tk7.o: tests/apps/tk7.c tests/apps/tk7ref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(DOS)/TK7.EXE: $(B)/tests/start.o $(B)/tests/tk7.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                tests/apps/app.lnk tests/apps/tk7.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk7.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk7.map \
	    file { $(B)/tests/start.o $(B)/tests/tk7.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }
$(B)/tests/tk8.o: tests/apps/tk8.c tests/apps/tk8ref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(DOS)/TK8.EXE: $(B)/tests/start.o $(B)/tests/tk8.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                tests/apps/app.lnk tests/apps/tk8.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk8.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk8.map \
	    file { $(B)/tests/start.o $(B)/tests/tk8.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }
$(B)/tests/tk9.o: tests/apps/tk9.c tests/apps/tk9ref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(DOS)/TK9.EXE: $(B)/tests/start.o $(B)/tests/tk9.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                tests/apps/app.lnk tests/apps/tk9.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk9.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk9.map \
	    file { $(B)/tests/start.o $(B)/tests/tk9.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }
# TK10.EXE: OS fixups as a chain and without the additive flag, made by
# rewriting the linked file (tools/nechain.py)
$(DOS)/TK10.EXE: $(B)/tests/start.o $(B)/tests/tk10.o $(B)/tests/tk10fp.o $(B)/tests/fixrqq.o \
                 tests/apps/app.lnk tests/apps/tk10.lnk tools/nechain.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk10.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk10.map \
	    file { $(B)/tests/start.o $(B)/tests/tk10.o $(B)/tests/tk10fp.o $(B)/tests/fixrqq.o }
	python3 tools/nechain.py $@
$(B)/tests/tk10.o: tests/apps/tk10ref.h tests/apps/tklog.h
# TK8T1-5: one transcendental instruction each (tests/apps/tk8.c, TK8_T)
$(B)/tests/tk8t%.o: tests/apps/tk8.c tests/apps/tk8tref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -dTK8_T=$* -fo=$@ $<
$(DOS)/TK8T%.EXE: $(B)/tests/start.o $(B)/tests/tk8t%.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                  tests/apps/app.lnk tests/apps/tk8.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk8.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk8t$*.map \
	    file { $(B)/tests/start.o $(B)/tests/tk8t$*.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }
$(B)/tests/tk6n.o: tests/apps/tk6n.c tests/apps/tk6nref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(DOS)/TK6N.EXE: $(B)/tests/start.o $(B)/tests/tk6n.o $(B)/tests/fixrqq.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk6n.map \
	    file { $(B)/tests/start.o $(B)/tests/tk6n.o $(B)/tests/fixrqq.o }
$(DOS)/TK6.EXE: $(B)/tests/start.o $(B)/tests/tk6.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                tests/apps/app.lnk tests/apps/tk6.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk6.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk6.map \
	    file { $(B)/tests/start.o $(B)/tests/tk6.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }

# TK5.EXE: the DLL loading probe, with its DLLs (tests/apps/tk5.c)
$(B)/tests/%.o: tests/apps/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/TK5.EXE: $(B)/tests/start.o $(B)/tests/tk5.o $(B)/tests/prolog.o \
                tests/apps/app.lnk tests/apps/tk5.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk5.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk5.map \
	    file { $(B)/tests/start.o $(B)/tests/tk5.o $(B)/tests/prolog.o }
# DLL code runs on its callers' stacks: SS != DS
$(B)/tests/dll/%.o: tests/apps/%.c tests/apps/tkdll.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/tests/dll/tkbad.o: tests/apps/tkid.c tests/apps/tkdll.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -dTK_BAD -fo=$@ $<
$(B)/tests/dll/tknodep.o: tests/apps/tkid.c tests/apps/tkdll.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -dTK_NODEP -fo=$@ $<
$(DOS)/TKDLL.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tkdll.o $(B)/tests/prolog.o \
                  tests/apps/dll.lnk tests/apps/tkdll.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkdll.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tkdll.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tkdll.o $(B)/tests/prolog.o }
$(DOS)/TKDLB.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tkdlb.o \
                  tests/apps/dll.lnk tests/apps/tkdlb.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkdlb.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tkdlb.map file { $(B)/tests/dllstart.o $(B)/tests/dll/tkdlb.o }
$(DOS)/TKID.TPL: $(B)/tests/dllstart.o $(B)/tests/dll/tkid.o \
                 tests/apps/dll.lnk tests/apps/tkid.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkid.lnk option modname=TKID name $@ \
	    libpath $(WATCOM)/lib286/win option map=$(B)/tests/tkid.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tkid.o }
$(DOS)/TKBAD.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tkbad.o \
                  tests/apps/dll.lnk tests/apps/tkid.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkid.lnk option modname=TKBAD name $@ \
	    libpath $(WATCOM)/lib286/win option map=$(B)/tests/tkbad.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tkbad.o }
$(DOS)/TKNODEP.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tknodep.o \
                    tests/apps/dll.lnk tests/apps/tkid.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkid.lnk option modname=TKNODEP import NOFUNC TKNONE \
	    name $@ libpath $(WATCOM)/lib286/win option map=$(B)/tests/tknodep.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tknodep.o }
$(DOS)/TKUSEX.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tknodep.o \
                   tests/apps/dll.lnk tests/apps/tkid.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkid.lnk option modname=TKUSEX import NOFUNC TKEXEM \
	    name $@ libpath $(WATCOM)/lib286/win option map=$(B)/tests/tkusex.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tknodep.o }
$(DOS)/TKEXEM.EXE: $(B)/tests/dllstart.o $(B)/tests/dll/tkid.o \
                   tests/apps/dll.lnk tests/apps/tkid.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkid.lnk option modname=TKEXEM name $@ \
	    libpath $(WATCOM)/lib286/win option map=$(B)/tests/tkexem.map \
	    file { $(B)/tests/dllstart.o $(B)/tests/dll/tkid.o }
$(DOS)/TKNOD.DLL: $(B)/tests/tknod.o
	@mkdir -p $(@D)
	$(WLINK) format windows dll option quiet option modname=TKNOD \
	    option start=nodentry export NOD_GET.1 name $@ \
	    option map=$(B)/tests/tknod.map file { $(B)/tests/tknod.o }

# TEXP3/TEXP2: export-table probes for Windows 3.x and 2.x
$(B)/tests/texp2.o: tests/apps/texp.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -dTEXP_WIN2 -fo=$@ $<
$(DOS)/TEXP3.EXE: $(B)/tests/start.o $(B)/tests/texp.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/texp3.map file { $(B)/tests/start.o $(B)/tests/texp.o }
$(DOS)/TEXP2.EXE: $(B)/tests/start.o $(B)/tests/texp2.o tests/apps/app.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/texp2.map file { $(B)/tests/start.o $(B)/tests/texp2.o }
	python3 tools/nemark.py $@ 2.0

# TICONS.EXE: shows the icons a file offers (ExtractIcon)
$(DOS)/TICONS.EXE: $(B)/tests/start.o $(B)/tests/ticons.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/ticons.map file { $(B)/tests/start.o $(B)/tests/ticons.o } \
	    library shell.lib

# THELP.EXE: a stand-in WINHELP.EXE that logs WinHelp requests
$(DOS)/THELP.EXE: $(B)/tests/start.o $(B)/tests/thelp.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/thelp.map file { $(B)/tests/start.o $(B)/tests/thelp.o }

# TDDE.EXE: Program Manager DDE probe
$(DOS)/TDDE.EXE: $(B)/tests/start.o $(B)/tests/tdde.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tdde.map file { $(B)/tests/start.o $(B)/tests/tdde.o }

# SNAP2.EXE: SNAP for Windows 2.x (marked 2.0)
$(B)/tests/snap2.o: tests/apps/snap.c tests/apps/snap.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -dSNAP_WIN2 -fo=$@ $<
$(B)/tests/snap2.res: tests/apps/snap.rc tests/apps/snap.h $(B)/tests/snap.ico
	$(WRC) -q -r -bt=windows -dSNAP_WIN2 -i=$(WATCOM)/h/win -i=tests/apps -i=$(B)/tests \
	    -fo=$@ $<
$(DOS)/SNAP2.EXE: $(B)/tests/start.o $(B)/tests/snap2.o $(B)/tests/snap2.res tests/apps/app.lnk \
                  tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/snap2.map option resource=$(B)/tests/snap2.res \
	    file { $(B)/tests/start.o $(B)/tests/snap2.o }
	python3 tools/nemark.py $@ 2.0

# TW2.EXE: the Windows 2.x probe, marked as a Windows 2.0 program
$(B)/tests/tw2.o: tests/apps/tw2api.h
$(DOS)/TW2.EXE: $(B)/tests/start.o $(B)/tests/tw2.o tests/apps/app.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tw2.map file { $(B)/tests/start.o $(B)/tests/tw2.o }
	python3 tools/nemark.py $@ 2.0

# SNAP.EXE: its icon (tools/snapicon.py) and the interactive window
$(B)/tests/snap.ico: tools/snapicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 -B tools/snapicon.py $@
$(B)/tests/snap.o: tests/apps/snap.h
$(B)/tests/snap.res: tests/apps/snap.rc tests/apps/snap.h $(B)/tests/snap.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=tests/apps -i=$(B)/tests -fo=$@ $<
$(DOS)/SNAP.EXE: $(B)/tests/start.o $(B)/tests/snap.o $(B)/tests/snap.res tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/snap.map option resource=$(B)/tests/snap.res \
	    file { $(B)/tests/start.o $(B)/tests/snap.o }

# CA1.EXE: drives Calculator and records its display (tests/calcscenarios.sh)
$(DOS)/CA1.EXE: $(B)/tests/start.o $(B)/tests/ca1.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/ca1.map file { $(B)/tests/start.o $(B)/tests/ca1.o }

# ---- DOS test tools ----
$(DOS)/KEYFEED.COM: tools/dos/keyfeed.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# WINENV.COM: test tool, runs KERNEL with windir= set as WIN.COM would
$(DOS)/WINENV.COM: tests/dos/winenv.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# DOSX.EXE: w16's Standard-mode DOS extender (dosx/, ARCHITECTURE.md 9.6)
$(DOS)/DOSX.EXE: dosx/dosx.asm $(wildcard dosx/*.inc)
	@mkdir -p $(@D)
	$(NASM) -f bin -I . -o $@ $<

# DOSXT.EXE: the same with the trace buffer (dosx/trace.inc)
$(DOS)/DOSXT.EXE: dosx/dosx.asm $(wildcard dosx/*.inc)
	@mkdir -p $(@D)
	$(NASM) -f bin -dTRACE -I . -o $@ $<

# DX6.EXE: a Windows program dumping the GDT/IDT (run as the shell)
$(DOS)/DX6.EXE: $(B)/tests/start.o $(B)/tests/dx6.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/dx6.map file { $(B)/tests/start.o $(B)/tests/dx6.o }

# DX7.EXE + DXD.COM: a DOS program started from Standard mode (tests/dosapp.sh)
$(DOS)/DX7.EXE: $(B)/tests/start.o $(B)/tests/dx7.o tests/apps/app.lnk tests/apps/dx7.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/dx7.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/dx7.map file { $(B)/tests/start.o $(B)/tests/dx7.o }
$(DOS)/DXD.COM: tests/dos/dxd.asm tests/dos/dxlib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -I tests/dos/ -o $@ $<

# DXn.COM: DOS extender probes, run in the KERNEL slot (tests/dosxtest.sh)
# (DXPROBES is set at the top, before the dos target expands it)
$(DOS)/DX%.COM: tests/dos/dx%.asm tests/dos/dxlib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -I tests/dos/ -o $@ $<

# TK18.COM: the DOSX -> KRNL286 contract probe (runs as SYSTEM\KRNL286.EXE)
$(DOS)/TK18.COM: tests/dos/tk18.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# ---- WIN.COM (win/win.asm; ARCHITECTURE.md 9.5) ----
# WIN.CNF is the program alone; WIN.COM is WIN.CNF with w16's start-up logo
# (a BI_RLE4 bitmap, assets/boot) appended, as Windows 3.1 Setup builds its
# WIN.COM from WIN.CNF and a logo (copy /b).
$(DOS)/WIN.CNF: win/win.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/WIN.COM: $(DOS)/WIN.CNF assets/boot/W16LOGOR.BMP
	cat $^ > $@
dos: $(DOS)/WIN.COM

# WC1-WC8.COM: the WIN.COM probes (tests/wincom.sh; ARCHITECTURE.md 9.5);
# WC3E.COM: WC3 writing its record after every call; WCB.COM: the BIOS
# data area to C:\BDA.LOG (boot test winprompt);
# WC9.EXE: ExitWindowsExec as the shell
WCPROBES := $(DOS)/WC1.COM $(DOS)/WC2.COM $(DOS)/WC3.COM $(DOS)/WC3B.COM \
            $(DOS)/WC4.COM $(DOS)/WC5.COM $(DOS)/WC6.COM $(DOS)/WC7.COM \
            $(DOS)/WC8.COM $(DOS)/WC9.EXE $(DOS)/WCB.COM $(DOS)/WC3E.COM
wcprobes: $(WCPROBES)
dos: $(WCPROBES)
$(DOS)/WC%.COM: tests/dos/wc%.asm tests/dos/wclib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -o $@ $<
$(DOS)/WCB.COM: tests/dos/wcb.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/WC3E.COM: tests/dos/wc3.asm tests/dos/wclib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -DEACH -o $@ $<
$(DOS)/WC3B.COM: tests/dos/wc3.asm tests/dos/wclib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -DDXAXOFF=0x160 -o $@ $<
$(B)/tests/wc9.o: tests/apps/wc9ref.h tests/apps/tklog.h
$(DOS)/WC9.EXE: $(B)/tests/start.o $(B)/tests/wc9.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/wc9.map file { $(B)/tests/start.o $(B)/tests/wc9.o }

# FN1.EXE: the font metrics probe; FN2.EXE: a window showing every
# character of each font (tests/fonts.sh; ARCHITECTURE.md 6.9)
FNPROBES := $(DOS)/FN1.EXE $(DOS)/FN2.EXE
fnprobes: $(FNPROBES)
dos: $(FNPROBES)
$(DOS)/FN%.EXE: $(B)/tests/start.o $(B)/tests/fn%.o tests/apps/app.lnk tests/apps/fn.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/fn.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/fn$*.map file { $(B)/tests/start.o $(B)/tests/fn$*.o }

# ---- win386: w16's WIN386.EXE (386 Enhanced mode; ARCHITECTURE.md 12) ----
# The real-mode loader (16-bit C and nasm, an MZ program) followed by a W3
# table of LE modules: for now the VMM ("WIN386", 32-bit C and nasm, linked
# by wlink as a VxD). tools/mkw3.py puts them together.
W386     := $(B)/win386
W386INC  := win386/include
W386LDR  := $(W386)/ldr.o $(W386)/ldrasm.o
W386VMM  := $(W386)/vmmasm.o $(W386)/ddb.o $(W386)/vmm.o $(W386)/dpmi.o $(W386)/dosx.o $(W386)/vxdapi.o
W386HDRS := $(W386INC)/w386.h $(W386INC)/w386.inc win386/vmm/vmm.h
.PHONY: win386
win386: $(W386)/WIN386.EXE
$(W386)/ldr.o: win386/ldr/ldr.c $(W386HDRS)
	@mkdir -p $(@D)
	$(WCC) -q -zq -bt=dos -ms -3 -s -os -i=$(WATCOM)/h -i=$(W386INC) -fo=$@ $<
$(W386)/ldrasm.o: win386/ldr/ldrasm.asm $(W386HDRS)
	@mkdir -p $(@D)
	$(NASM) -f obj -I$(W386INC)/ -o $@ $<
$(W386)/%.o: win386/vmm/%.c $(W386HDRS)
	@mkdir -p $(@D)
	wcc386 -q -zq -bt=nt -mf -zl -s -ecc -3s -ox -i=$(WATCOM)/h -i=$(W386INC) -fo=$@ $<
$(W386)/%.o: win386/vmm/%.asm $(W386HDRS)
	@mkdir -p $(@D)
	$(NASM) -f obj -I$(W386INC)/ -o $@ $<
$(W386)/WIN386L.EXE: $(W386LDR)
	$(WLINK) option quiet format dos name $@ option map=$(W386)/ldr.map option stack=4096 \
	    libpath $(WATCOM)/lib286/dos libpath $(WATCOM)/lib286 file { $(W386LDR) }
$(W386)/VMM.386: $(W386VMM) win386/vmm/vmm.lnk
	$(WLINK) @win386/vmm/vmm.lnk name $@ option map=$(W386)/vmm.map file { $(W386VMM) }
$(W386)/WIN386.EXE: $(W386)/WIN386L.EXE $(W386)/VMM.386 tools/mkw3.py
	python3 tools/mkw3.py $@ $(W386)/WIN386L.EXE WIN386=$(W386)/VMM.386

# VX1-VX2.COM: the WIN386 probes (tests/vxobs.sh; ARCHITECTURE.md 12)
VXPROBES := $(DOS)/VX1.COM $(DOS)/VX2.COM $(DOS)/VX3.EXE $(DOS)/VX4.COM
vxprobes: $(VXPROBES)
dos: $(VXPROBES)
$(B)/tests/vx3.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX3.EXE: $(B)/tests/start.o $(B)/tests/vx3.o $(B)/tests/vx3s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx3.map file { $(B)/tests/start.o $(B)/tests/vx3.o $(B)/tests/vx3s.o }
$(DOS)/VX%.COM: tests/dos/vx%.asm tests/dos/dxlib.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -o $@ $<

$(DOS)/POWEROFF.COM: tools/dos/poweroff.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(B)/hdd/MBR.BIN: tools/dos/mbr.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# ---- fonts: w16's own raster fonts (fonts/*.txt; ARCHITECTURE.md 6.9) ----
# tools/fontc.py compiles each glyph source into a .FNT and packages them as
# a .FON (a resource-only NE DLL). Module names, descriptions (their
# "FONTRES aspect,logpixelsx,logpixelsy : " prefix), resource IDs and
# version numbers follow Windows 3.1's files; the free text is our own.
FNT     := $(B)/fonts
FONTC   := python3 tools/fontc.py
FONTINFO = --info CompanyName=w16 --info FileVersion=3.10 --info ProductVersion=3.10 \
           --info "LegalCopyright=Copyright (c) 2026 the w16 contributors, MIT licence" \
           --info ProductName=w16
FONS    := $(FNT)/VGASYS.FON $(FNT)/VGAFIX.FON $(FNT)/VGAOEM.FON $(FNT)/SSERIFE.FON
SSERIFE_SRC := $(foreach p,8 10 12 14 18 24,fonts/sserife$(p).txt)
.PHONY: fonts
fonts: $(FONS)
all: fonts
$(FNT)/VGASYS.FON: fonts/vgasys.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module FONTS --desc "FONTRES 100,96,96 : System (w16)" $(FONTINFO) \
	    --info "FileDescription=System font (VGA res)" --info InternalName=FONTS \
	    --info OriginalFilename=VGASYS.FON 31=$<
$(FNT)/VGAFIX.FON: fonts/vgafix.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module FIXFONTS --desc "FONTRES 100,96,96 : Fixedsys (w16)" $(FONTINFO) \
	    --info "FileDescription=Fixedsys font (VGA res)" --info InternalName=FIXFONTS \
	    --info OriginalFilename=VGAFIX.FON 31=$<
$(FNT)/VGAOEM.FON: fonts/vgaoem.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module OEMFONTS --desc "FONTRES 100,96,96 : Terminal (US) (w16)" $(FONTINFO) \
	    --info "FileDescription=Terminal font, code page 437 (VGA res)" --info InternalName=OEMFONTS \
	    --info OriginalFilename=VGAOEM.FON 30=$<
# the description after the colon is the [fonts] key Control Panel writes
# to WIN.INI, so it is Windows 3.1's
$(FNT)/SSERIFE.FON: $(SSERIFE_SRC) tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module SSERIFE \
	    --desc "FONTRES 100,96,96 : MS Sans Serif 8,10,12,14,18,24 (VGA res)" $(FONTINFO) \
	    --info "FileDescription=MS Sans Serif font (VGA res)" --info InternalName=SSERIFE \
	    --info OriginalFilename=SSERIFE.FON \
	    84=fonts/sserife8.txt 85=fonts/sserife10.txt 86=fonts/sserife12.txt \
	    87=fonts/sserife14.txt 88=fonts/sserife18.txt 89=fonts/sserife24.txt

# ---- shell: PROGMAN.EXE, in two builds from one source tree ----
#   $(DOS)/PROGMAN.EXE    Windows 3.x build (marked 3.0, uses MDI etc.)
#   $(WIN2)/PROGMAN.EXE   Windows 2.x build (W16_WIN2: 8086 code, fixed
#                         segments, no 3.x-only calls; marked 2.0)
WIN2    := $(B)/win2
SHELL_PM2 := $(patsubst $(B)/shell/%,$(B)/shell2/%,$(SHELL_PM))
shell: $(DOS)/PROGMAN.EXE
shell2: $(WIN2)/PROGMAN.EXE

$(B)/shell/start.o: shell/start.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/shell/progman/%.o: shell/progman/%.c shell/progman/progman.h shell/progman/compat2.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(ICONS) &: tools/mkicons.py tools/stockicons.py
	python3 tools/mkicons.py $(B)/shell/res
$(B)/shell/progman/progman.res: shell/progman/progman.rc shell/progman/progman.h $(ICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/progman -i=$(B)/shell/res \
	    -fo=$@ $<
$(DOS)/PROGMAN.EXE: $(SHELL_PM) $(B)/shell/progman/progman.res shell/progman/progman.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/progman/progman.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/progman/progman.map \
	    option resource=$(B)/shell/progman/progman.res file { $(SHELL_PM) }

$(B)/shell2/start.o: $(B)/shell/start.o
	@mkdir -p $(@D)
	cp $< $@
$(B)/shell2/progman/%.o: shell/progman/%.c shell/progman/progman.h shell/progman/compat2.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -0 -dW16_WIN2 -fo=$@ $<
$(B)/shell2/progman/progman.res: shell/progman/progman.rc shell/progman/progman.h $(ICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -dW16_WIN2 -i=$(WATCOM)/h/win -i=shell/progman \
	    -i=$(B)/shell/res -fo=$@ $<
$(WIN2)/PROGMAN.EXE: $(SHELL_PM2) $(B)/shell2/progman/progman.res shell/progman/progman2.lnk \
                     tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/progman/progman2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell2/progman/progman.map \
	    option resource=$(B)/shell2/progman/progman.res file { $(SHELL_PM2) }
	python3 tools/nemark.py $@ 2.0

# ---- shell: CALC.EXE (shell/calc; ARCHITECTURE.md 15.3): x87 code with
# emulator fixups (-fpi, WIN87EM) ----
CALC_SRC := main draw engine fmt paste fpu stat clip help
CALC_OBJ := $(B)/shell/start.o $(CALC_SRC:%=$(B)/shell/calc/%.o) $(B)/shell/calc/fixrqq.o
CALC_HDR := shell/calc/calc.h shell/calc/ui.h
shell: $(DOS)/CALC.EXE
$(B)/shell/calc/%.o: shell/calc/%.c $(CALC_HDR)
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fpi -fo=$@ $<
$(B)/shell/calc/fixrqq.o: tests/apps/fixrqq.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/shell/res/calc.ico: tools/mkcalcicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkcalcicon.py $@
$(B)/shell/calc/calc.res: shell/calc/calc.rc shell/calc/calc.h $(B)/shell/res/calc.ico
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/calc -i=$(B)/shell/res -fo=$@ $<
$(DOS)/CALC.EXE: $(CALC_OBJ) $(B)/shell/calc/calc.res shell/calc/calc.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/calc/calc.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/calc/calc.map \
	    option resource=$(B)/shell/calc/calc.res file { $(CALC_OBJ) }
# the engine on the host (tools/calchost; tests/calcscenarios.sh)
$(B)/host/calchost: tools/calchost/calchost.c tools/calchost/fpu_host.c shell/calc/engine.c \
                    shell/calc/fmt.c shell/calc/paste.c shell/calc/calc.h shell/calc/accel.h
	@mkdir -p $(@D)
	$(HOSTCC) -O0 -mfpmath=387 -o $@ tools/calchost/calchost.c tools/calchost/fpu_host.c \
	    shell/calc/engine.c shell/calc/fmt.c shell/calc/paste.c

# ---- shell: CLOCK.EXE (shell/clock), Windows 3.1 only ----
CLOCK_OBJ := start main face digital intl fontdlg util
SHELL_CK  := $(B)/shell/start.o $(patsubst %,$(B)/shell/clock/%.o,$(filter-out start,$(CLOCK_OBJ)))
CLOCK_H   := shell/clock/clock.h
shell: $(DOS)/CLOCK.EXE
$(B)/shell/clock/clock.ico: tools/clockicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/clockicon.py $@
$(B)/shell/clock/circle.h: tools/clocktable.py
	@mkdir -p $(@D)
	python3 tools/clocktable.py $@
$(B)/shell/clock/face.o: $(B)/shell/clock/circle.h
$(B)/shell/clock/%.o: shell/clock/%.c $(CLOCK_H)
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -i=$(B)/shell/clock -fo=$@ $<
$(B)/shell/clock/clock.res: shell/clock/clock.rc $(CLOCK_H) $(B)/shell/clock/clock.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/clock -i=$(B)/shell/clock -fo=$@ $<
$(DOS)/CLOCK.EXE: $(SHELL_CK) $(B)/shell/clock/clock.res shell/clock/clock.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/clock/clock.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/clock/clock.map \
	    option resource=$(B)/shell/clock/clock.res file { $(SHELL_CK) }
	python3 tools/nemark.py $@ 3.10

test: dos fonts
	python3 tools/castcheck.py $$(find user gdi kernel shell win386 win87em winoldap -name '*.c')
	tests/run-tests.sh

# boot real Windows 3.1 on w16's parts and compare each screen with the
# reference (tests/boottest.sh); run after every merge: make test does not
# boot Windows
boot-test: dos install-tree win386
	tests/boottest.sh
.PHONY: boot-test

clean:
	rm -rf $(B)

# ---- GD1, GD2: logging stand-ins for GDI's interfaces (tests/gdi; probes,
# not in make test; ARCHITECTURE.md 11). GD1.DRV is a DISPLAY driver that
# records every call and forwards it to VGA.DRV renamed VGAREAL
# (tests/gdi/gdtrace.sh makes that copy in a scratch install).
gdprobes: $(DOS)/GD1.DRV
$(B)/gdi/gd1stubs.asm $(B)/gdi/gd1.lnk &: tests/gdi/display.spec tools/mkgdlog.py
	@mkdir -p $(@D)
	python3 tools/mkgdlog.py tests/gdi/display.spec VGAREAL $(B)/gdi/gd1stubs.asm \
	    $(B)/gdi/gd1.lnk DISPLAY 'DISPLAY : 100, 96, 96 : VGA Color Display'
$(B)/gdi/gd1stubs.o: $(B)/gdi/gd1stubs.asm tests/gdi/gdlog.inc
	$(NASM) $(NFLAGS) -Itests/gdi/ -o $@ $<
$(B)/gdi/gdlog.o: tests/gdi/gdlog.asm tests/gdi/gdlog.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -Itests/gdi/ -o $@ $<
$(B)/gdi/gd1log.o: tests/gdi/gdlog.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -dGD_DISPLAY -dGDL_TAG='"D"' -fo=$@ $<
$(DOS)/GD1.DRV: $(B)/gdi/gdlog.o $(B)/gdi/gd1stubs.o $(B)/gdi/gd1log.o $(B)/gdi/gd1.lnk
	@mkdir -p $(@D)
	$(WLINK) @$(B)/gdi/gd1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/gdi/gd1.map \
	    file { $(B)/gdi/gdlog.o $(B)/gdi/gd1stubs.o $(B)/gdi/gd1log.o }
# GD2.EXE: a GDI that records every call and forwards it to GDI.EXE renamed
# GDJ (tests/gdiboot.sh -L); its parameter table comes from Wine's
# X11-licensed gdi.spec (wine-20020228, ARCHITECTURE.md 3), kept as
# tests/gdi/gdi.spec
gdprobes: $(DOS)/GD2.EXE
$(B)/gdi/gd2stubs.asm $(B)/gdi/gd2.lnk &: tests/gdi/gdi.spec tools/mkgdlog.py
	@mkdir -p $(@D)
	python3 tools/mkgdlog.py --wine --only docs/exports/GDI.txt tests/gdi/gdi.spec GDJ $(B)/gdi/gd2stubs.asm \
	    $(B)/gdi/gd2.lnk GDI 'GDI (w16 GD2: logging stand-in)'
$(B)/gdi/gd2stubs.o: $(B)/gdi/gd2stubs.asm tests/gdi/gdlog.inc
	$(NASM) $(NFLAGS) -Itests/gdi/ -o $@ $<
$(B)/gdi/gd2log.o: tests/gdi/gdlog.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -dGD_GDI -dGDL_TAG='"G"' -fo=$@ $<
$(DOS)/GD2.EXE: $(B)/gdi/gdlog.o $(B)/gdi/gd2stubs.o $(B)/gdi/gd2log.o $(B)/gdi/gd2.lnk
	@mkdir -p $(@D)
	$(WLINK) @$(B)/gdi/gd2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/gdi/gd2.map \
	    file { $(B)/gdi/gdlog.o $(B)/gdi/gd2stubs.o $(B)/gdi/gd2log.o }

# ---- GDI.EXE: w16's GDI (gdi/; ARCHITECTURE.md 11) ----
# large model, SS != DS, byte-packed structures (the DDK's), 286
GCFLAGS := -q -bt=windows -ml -zu -zp1 -2 -s -zl -ox -wx -we -wcd=201 -Igdi
GOBJS   := $(B)/gdi/entry.o $(B)/gdi/i4.o $(B)/gdi/stubs.o $(B)/gdi/util.o $(B)/gdi/object.o \
           $(B)/gdi/driver.o $(B)/gdi/dc.o $(B)/gdi/rgn.o $(B)/gdi/bitmap.o \
           $(B)/gdi/blt.o $(B)/gdi/font.o $(B)/gdi/text.o $(B)/gdi/misc.o $(B)/gdi/draw.o
gdi: $(DOS)/GDI.EXE
dos: $(DOS)/GDI.EXE
$(B)/gdi/exports.lnk $(B)/gdi/stubs.asm &: tools/mkgdiexp.py tools/mkgdlog.py \
        docs/exports/GDI.txt tests/gdi/gdi.spec $(wildcard gdi/*.c)
	@mkdir -p $(@D)
	python3 tools/mkgdiexp.py $(B)/gdi/exports.lnk $(B)/gdi/stubs.asm
$(B)/gdi/stubs.o: $(B)/gdi/stubs.asm
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/gdi/%.o: gdi/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/gdi/%.o: gdi/%.c gdi/gdi.h
	@mkdir -p $(@D)
	$(WCC) $(GCFLAGS) -fo=$@ $<
$(DOS)/GDI.EXE: $(GOBJS) gdi/gdi.lnk $(B)/gdi/exports.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @gdi/gdi.lnk @$(B)/gdi/exports.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/gdi/gdi.map file { $(GOBJS) }
	python3 tools/nemark.py $@ 3.10
# GD3.EXE: the drawing probe (tests/apps/gd3.c), as the shell
gdprobes: $(DOS)/GD3.EXE
$(DOS)/GD3.EXE: $(B)/tests/start.o $(B)/tests/gd3.o tests/apps/app.lnk tests/apps/gd3.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/gd3.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd3.map file { $(B)/tests/start.o $(B)/tests/gd3.o }
# ---- WINOLDAP.MOD (Standard-mode DOS application support; ARCHITECTURE.md 9.7) ----
# WO probes: WO2.EXE, a protected-mode interrupt spy (tests/worun.sh)
WOPROBES := $(DOS)/WO1.MOD $(DOS)/WO2.EXE $(DOS)/WO3.COM
woprobes: $(WOPROBES)
$(B)/tests/wo2s.o: tests/apps/wo2s.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/WO2.EXE: $(B)/tests/start.o $(B)/tests/wo2.o $(B)/tests/wo2s.o tests/apps/app.lnk tests/apps/wo2.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/wo2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/wo2.map file { $(B)/tests/start.o $(B)/tests/wo2.o $(B)/tests/wo2s.o }
$(B)/tests/wo1s.o: tests/apps/wo1s.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/WO1.MOD: $(B)/tests/start.o $(B)/tests/wo1.o $(B)/tests/wo1s.o tests/apps/app.lnk tests/apps/wo1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/wo1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/wo1.map file { $(B)/tests/start.o $(B)/tests/wo1.o $(B)/tests/wo1s.o }
$(DOS)/WO%.COM: tests/dos/wo%.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -o $@ $<

# ---- WINOLDAP.MOD: w16's Standard-mode DOS application support ----
WOA_OBJS := $(B)/shell/start.o $(B)/winoldap/winoldap.o $(B)/winoldap/woasm.o
dos: $(DOS)/WINOLDAP.MOD
$(B)/winoldap/%.o: winoldap/%.c winoldap/winoldap.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/winoldap/woasm.o: winoldap/woasm.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/winoldap/winoldap.res: winoldap/winoldap.rc winoldap/winoldap.h $(ICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=winoldap -i=$(B)/shell/res -fo=$@ $<
$(DOS)/WINOLDAP.MOD: $(WOA_OBJS) $(B)/winoldap/winoldap.res winoldap/winoldap.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @winoldap/winoldap.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/winoldap/winoldap.map \
	    option resource=$(B)/winoldap/winoldap.res file { $(WOA_OBJS) }
	python3 tools/nemark.py $@ 3.10

# ---- install-tree: build/w16, a Windows 3.1-style directory with w16's
# programs only, each where Windows 3.1 keeps it (the reference install:
# WIN.COM and PROGMAN.EXE in the Windows directory, the rest in SYSTEM).
# win/README.TXT says what to copy in from one's own Windows to boot it.
# tests/w16tree.sh boots it. build/dos stays as it is.
W16     := $(B)/w16
W16SYS  := $(W16)/SYSTEM
W16TREE := $(W16)/WIN.COM $(W16)/PROGMAN.EXE $(W16)/CLOCK.EXE $(W16)/README.TXT \
           $(W16SYS)/KRNL286.EXE $(W16SYS)/DOSX.EXE $(W16SYS)/WIN87EM.DLL $(W16SYS)/SHELL.DLL \
           $(FONS:$(FNT)/%=$(W16SYS)/%)
install-tree: $(W16TREE)
# Calculator (ARCHITECTURE.md 15.3)
install-tree: $(W16)/CALC.EXE
$(W16)/CALC.EXE: $(DOS)/CALC.EXE
	@mkdir -p $(@D)
	cp $< $@
all: install-tree
$(W16)/WIN.COM $(W16)/PROGMAN.EXE $(W16)/CLOCK.EXE: $(W16)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
$(W16)/README.TXT: win/README.TXT
	@mkdir -p $(@D)
	sed 's/$$/\r/' $< > $@
$(W16SYS)/KRNL286.EXE $(W16SYS)/DOSX.EXE $(W16SYS)/WIN87EM.DLL $(W16SYS)/SHELL.DLL: $(W16SYS)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
$(FONS:$(FNT)/%=$(W16SYS)/%): $(W16SYS)/%: $(FNT)/%
	@mkdir -p $(@D)
	cp $< $@

# ---- USER (user/; ARCHITECTURE.md 10) and its probes ----
# US1: USTRACE.DLL, the call recorder behind the forwarding modules that
# tools/mkinterpose.py generates (tests/ustrace.sh)
UCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we \
           -i=$(WATCOM)/h -i=$(WATCOM)/h/win
$(B)/user/%.o: tests/user/%.c
	@mkdir -p $(@D)
	$(WCC) $(UCFLAGS) -fo=$@ $<
$(B)/user/%.o: tests/user/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/USTRACE.DLL: $(B)/user/usentry.o $(B)/user/ustrace.o tests/user/ustrace.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/user/ustrace.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/user/ustrace.map file { $(B)/user/usentry.o $(B)/user/ustrace.o }
dos: $(DOS)/USTRACE.DLL
# w16's USER.EXE: user/*.c and user/uentry.asm; every ordinal the C code
# does not define gets a stub (tools/mkuserexp.py makes the export list and
# the stubs from user/ordinals.txt and the objects' public names)
USER_C   := $(wildcard user/*.c)
# uentry.o first: its _NULL (class BEGDATA) must start DGROUP, the 16 bytes
# of instance data KERNEL keeps there (the atom table pointer at 8, ...)
USER_OBJ := $(B)/user/uentry.o $(USER_C:user/%.c=$(B)/user/u_%.o)
UUCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win
$(B)/user/u_%.o: user/%.c user/user.h
	@mkdir -p $(@D)
	$(WCC) $(UUCFLAGS) -fo=$@ $<
$(B)/user/uentry.o: user/uentry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/user/stubs.asm $(B)/user/exports.lnk &: $(USER_OBJ) user/ordinals.txt tools/mkuserexp.py
	python3 tools/mkuserexp.py user/ordinals.txt $(B)/user/exports.lnk $(B)/user/stubs.asm $(USER_OBJ)
$(B)/user/stubs.o: $(B)/user/stubs.asm
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/USER.EXE: $(USER_OBJ) $(B)/user/stubs.o $(B)/user/exports.lnk user/user.lnk
	@mkdir -p $(@D)
	$(WLINK) @user/user.lnk @$(B)/user/exports.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/user/user.map file { $(USER_OBJ) $(B)/user/stubs.o }
user: $(DOS)/USER.EXE
dos: $(DOS)/USER.EXE
.PHONY: user

# US2: USER's values and pure functions (tests/apps/us2.c), run as the shell
$(B)/tests/us2.o: tests/apps/us2ref.h tests/apps/tklog.h
$(DOS)/US2.EXE: $(B)/tests/start.o $(B)/tests/us2.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us2.map file { $(B)/tests/start.o $(B)/tests/us2.o }
dos: $(DOS)/US2.EXE
# US3: DrawText's layout (tests/apps/us3.c)
$(B)/tests/us3.o: tests/apps/us3ref.h tests/apps/tklog.h
$(DOS)/US3.EXE: $(B)/tests/start.o $(B)/tests/us3.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us3.map file { $(B)/tests/start.o $(B)/tests/us3.o }
dos: $(DOS)/US3.EXE
# US14: menus in USER's menu heaps; GetFreeSystemResources (tests/apps/us14.c)
$(B)/tests/us14.o: tests/apps/us14ref.h tests/apps/tklog.h
$(DOS)/US14.EXE: $(B)/tests/start.o $(B)/tests/us14.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us14.map file { $(B)/tests/start.o $(B)/tests/us14.o }
dos: $(DOS)/US14.EXE
# US13: the caret's blink (tests/apps/us13.c)
$(B)/tests/us13.o: tests/apps/us13ref.h tests/apps/tklog.h
$(DOS)/US13.EXE: $(B)/tests/start.o $(B)/tests/us13.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us13.map file { $(B)/tests/start.o $(B)/tests/us13.o }
dos: $(DOS)/US13.EXE
# US12: GetFocus inside WM_KILLFOCUS and WM_SETFOCUS (tests/apps/us12.c)
$(B)/tests/us12.o: tests/apps/us12ref.h tests/apps/tklog.h
$(DOS)/US12.EXE: $(B)/tests/start.o $(B)/tests/us12.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us12.map file { $(B)/tests/start.o $(B)/tests/us12.o }
dos: $(DOS)/US12.EXE
# US11: argument sizes of USER's undocumented exports; GetNextQueueWindow
$(B)/tests/us11.o: tests/apps/us11ref.h tests/apps/tklog.h
$(DOS)/US11.EXE: $(B)/tests/start.o $(B)/tests/us11.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us11.map file { $(B)/tests/start.o $(B)/tests/us11.o }
dos: $(DOS)/US11.EXE
# US10: DisableOEMLayer and EnableOEMLayer (tests/apps/us10.c)
$(B)/tests/us10.o: tests/apps/us10ref.h tests/apps/tklog.h
$(DOS)/US10.EXE: $(B)/tests/start.o $(B)/tests/us10.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us10.map file { $(B)/tests/start.o $(B)/tests/us10.o } \
	    import EnableOEMLayer USER.3 import DisableOEMLayer USER.4
dos: $(DOS)/US10.EXE
# US9: DragObject and DragDetect (tests/apps/us9.c)
$(B)/tests/us9.o: tests/apps/us9ref.h tests/apps/tklog.h tests/apps/shprobe.h
$(DOS)/US9.EXE: $(B)/tests/start.o $(B)/tests/us9.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us9.map file { $(B)/tests/start.o $(B)/tests/us9.o } \
	    library shell.lib import DragObject USER.464 import DragDetect USER.465
dos: $(DOS)/US9.EXE
# US8: USER's WNet functions without a network (tests/apps/us8.c)
$(B)/tests/us8.o: tests/apps/us8ref.h tests/apps/tklog.h
$(DOS)/US8.EXE: $(B)/tests/start.o $(B)/tests/us8.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us8.map file { $(B)/tests/start.o $(B)/tests/us8.o }
dos: $(DOS)/US8.EXE
# US7: WinHelp and WINHELP.EXE (tests/apps/us7.c)
$(B)/tests/us7.o: tests/apps/us7ref.h tests/apps/tklog.h
$(DOS)/US7.EXE: $(B)/tests/start.o $(B)/tests/us7.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us7.map file { $(B)/tests/start.o $(B)/tests/us7.o }
dos: $(DOS)/US7.EXE
# US6: EM_GETTHUMB (tests/apps/us6.c)
$(B)/tests/us6.o: tests/apps/us6ref.h tests/apps/tklog.h
$(B)/tests/mevent.o: tests/apps/mevent.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/US6.EXE: $(B)/tests/start.o $(B)/tests/us6.o $(B)/tests/mevent.o tests/apps/app.lnk tests/apps/us6.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us6.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us6.map file { $(B)/tests/start.o $(B)/tests/us6.o $(B)/tests/mevent.o }
dos: $(DOS)/US6.EXE
# US4: a gallery of USER's drawing, saved as BMPs (tests/apps/us4.c, us4.rc;
# tests/userscreens.sh)
$(B)/tests/us4.res: tests/apps/us4.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US4.EXE: $(B)/tests/start.o $(B)/tests/us4.o $(B)/tests/us4.res tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us4.map option resource=$(B)/tests/us4.res \
	    file { $(B)/tests/start.o $(B)/tests/us4.o }
dos: $(DOS)/US4.EXE
# US5: where menu-bar items go (tests/apps/us5.c, us5.rc)
$(B)/tests/us5.res: tests/apps/us5.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US5.EXE: $(B)/tests/start.o $(B)/tests/us5.o $(B)/tests/us5.res tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us5.map option resource=$(B)/tests/us5.res \
	    file { $(B)/tests/start.o $(B)/tests/us5.o }
dos: $(DOS)/US5.EXE

# ---- SHELL.DLL probes (tests/apps/shN.c; ARCHITECTURE.md 13): run as the
# shell on Windows 3.1 and with w16's SHELL.DLL (tests/shellprobe.sh)
SHPROBES := SH1 SH2 SH3 SH4 SH5 SH6 SH7 SH8 SH9 SH10 SH11 SH12 SH13
$(B)/tests/sh%.o: tests/apps/sh%.c tests/apps/sh%ref.h tests/apps/shprobe.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/SH%.EXE: $(B)/tests/start.o $(B)/tests/sh%.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/sh$*.map file { $(B)/tests/start.o $(B)/tests/sh$*.o } \
	    library shell.lib
shprobes: $(SHPROBES:%=$(DOS)/%.EXE)
dos: shprobes
.PHONY: shprobes

# ---- SHELL.DLL: w16's shell library (shell/shelldll/; ARCHITECTURE.md 13).
# Windows 3.x only: Windows 2.x has no SHELL.DLL. Large model, SS != DS.
SHCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=SHELL_TEXT \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win
SHDLL_C  := util reg drag env hook icon exec about misc
SHDLL_O  := $(B)/shelldll/entry.o $(B)/shelldll/i4.o $(SHDLL_C:%=$(B)/shelldll/%.o)
$(B)/shelldll/%.o: shell/shelldll/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/shelldll/%.o: shell/shelldll/%.c shell/shelldll/shell.h
	@mkdir -p $(@D)
	$(WCC) $(SHCFLAGS) -fo=$@ $<
$(B)/shelldll/shlogo.bmp: tools/mkaboutlogo.py tools/mklogo.py
	@mkdir -p $(@D)
	python3 -B tools/mkaboutlogo.py $@
$(B)/shelldll/shell.res: shell/shelldll/shell.rc shell/shelldll/shell.h $(B)/shelldll/shlogo.bmp
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/shelldll -i=$(B)/shelldll \
	    -fo=$@ $<
$(DOS)/SHELL.DLL: $(SHDLL_O) $(B)/shelldll/shell.res shell/shelldll/shell.lnk tools/nenoname.py
	@mkdir -p $(@D)
	$(WLINK) @shell/shelldll/shell.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shelldll/shell.map \
	    option resource=$(B)/shelldll/shell.res file { $(SHDLL_O) }
	python3 tools/nenoname.py $@ 104-128
	python3 tools/nemark.py $@ 3.10
shelldll: $(DOS)/SHELL.DLL
dos: $(DOS)/SHELL.DLL
.PHONY: shelldll
# ---- shell: NOTEPAD.EXE (Windows 3.1; shell/notepad, ARCHITECTURE.md 15.1) ----
NP_SRC  := main file find print help util
SHELL_NP  := $(B)/shell/start.o $(NP_SRC:%=$(B)/shell/notepad/%.o)
shell: $(DOS)/NOTEPAD.EXE
$(B)/shell/res/notepad.ico: tools/npicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 -B tools/npicon.py $@
$(B)/shell/notepad/%.o: shell/notepad/%.c shell/notepad/notepad.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/notepad/notepad.res: shell/notepad/notepad.rc shell/notepad/notepad.h $(B)/shell/res/notepad.ico
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/notepad -i=$(B)/shell/res -fo=$@ $<
$(DOS)/NOTEPAD.EXE: $(SHELL_NP) $(B)/shell/notepad/notepad.res shell/notepad/notepad.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/notepad/notepad.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/notepad/notepad.map \
	    option resource=$(B)/shell/notepad/notepad.res file { $(SHELL_NP) }
install-tree: $(W16)/NOTEPAD.EXE
$(W16)/NOTEPAD.EXE: $(DOS)/NOTEPAD.EXE
	@mkdir -p $(@D)
	cp $< $@

# ---- the w16 VGA display driver's graphics (drivers/vga; no driver code
# yet): the OBM_ bitmaps, OIC_ icons and OCR_ cursors as .BMP/.ICO/.CUR
# under build/vga, drawn as text in drivers/vga/art (tools/vgaart.py)
VGAART_SRC := $(wildcard drivers/vga/art/*.txt)
vgaart: $(B)/vga/.stamp
$(B)/vga/.stamp: $(VGAART_SRC) tools/vgaart.py
	@mkdir -p $(@D)
	python3 -B tools/vgaart.py build drivers/vga/art $(B)/vga
	@touch $@
.PHONY: vgaart

# ---- MOUSE.DRV: w16's PS/2 mouse driver (drivers/mouse; README.md there)
$(B)/mouse/mouse.o: drivers/mouse/mouse.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/MOUSE.DRV: $(B)/mouse/mouse.o drivers/mouse/mouse.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/mouse/mouse.lnk name $@ option map=$(B)/mouse/mouse.map \
	    file { $(B)/mouse/mouse.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/MOUSE.DRV
# MD1.EXE: the mouse driver's interface observed (tests/apps/md1.c; run by
# tests/mouseprobe.sh with Windows 3.1's MOUSE.DRV and with w16's)
$(B)/tests/md1.o: tests/apps/md1ref.h tests/apps/tklog.h
$(DOS)/MD1.EXE: $(B)/tests/start.o $(B)/tests/md1.o $(B)/tests/md1s.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/md1.map file { $(B)/tests/start.o $(B)/tests/md1.o $(B)/tests/md1s.o }
# MS2.COM: MS1 with INT 33h as well (tests/dos/ms2.asm; TK49 reads its log)
$(DOS)/MS2.COM: tests/dos/ms2.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/MD1.EXE $(DOS)/MS2.COM
# the install tree gets it too (build/w16/SYSTEM, win/README.TXT)
install-tree: $(W16SYS)/MOUSE.DRV
$(W16SYS)/MOUSE.DRV: $(DOS)/MOUSE.DRV
	@mkdir -p $(@D)
	cp $< $@

# ---- MD2: MOUSE.DRV's open questions observed (drivers/mouse/README.md;
# findings and logs in tests/apps/md2logs/{a,b,c}/)
# A: GetSetMouseData. MD2A.EXE (tests/apps/md2a.c, md2ax.c); MS3A.DRV, a
# recording MOUSE in front of MOUSX, a renamed copy of the real driver
# (tests/apps/ms3a.asm); MS4A.COM, an APM BIOS that records its callers
# (tests/dos/ms4a.asm); tests/md2aprobe.sh
$(B)/tests/md2a.o: tests/apps/md2ax.c tests/apps/md1ref.h tests/apps/tklog.h
$(DOS)/MD2A.EXE: $(B)/tests/start.o $(B)/tests/md2a.o $(B)/tests/md2as.o $(B)/tests/md1s.o tests/apps/app.lnk tests/apps/md2a.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md2a.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/md2a.map file { $(B)/tests/start.o $(B)/tests/md2a.o $(B)/tests/md2as.o $(B)/tests/md1s.o }
$(DOS)/MS3A.DRV: $(B)/tests/ms3a.o tests/apps/ms3a.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/ms3a.lnk name $@ option map=$(B)/tests/ms3a.map file { $(B)/tests/ms3a.o }
	python3 tools/nemark.py $@ 3.10
# B: PowerEventProc. MD2B.EXE (tests/apps/md2b.c); MS3B.COM, MS2 with INT
# 15h AH=53h (tests/dos/ms3b.asm); MS4B.COM, an APM BIOS with scheduled
# power events (tests/dos/ms4b.asm); MOUSEPT.DRV and MOUSEPF.DRV, test
# builds of w16's driver (-DPWRTRACE, -DPWRFAIL); tests/md2bprobe.sh
$(B)/tests/md2b.o: tests/apps/md1ref.h tests/apps/tklog.h
$(DOS)/MD2B.EXE: $(B)/tests/start.o $(B)/tests/md2b.o $(B)/tests/md1s.o tests/apps/app.lnk tests/apps/md2b.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md2b.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/md2b.map file { $(B)/tests/start.o $(B)/tests/md2b.o $(B)/tests/md1s.o }
$(B)/mouse/mousept.o: drivers/mouse/mouse.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -DPWRTRACE -o $@ $<
$(B)/mouse/mousepf.o: drivers/mouse/mouse.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -DPWRTRACE -DPWRFAIL -o $@ $<
$(DOS)/MOUSEPT.DRV: $(B)/mouse/mousept.o drivers/mouse/mouse.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/mouse/mouse.lnk name $@ option map=$(B)/mouse/mousept.map file { $< }
	python3 tools/nemark.py $@ 3.10
$(DOS)/MOUSEPF.DRV: $(B)/mouse/mousepf.o drivers/mouse/mouse.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/mouse/mouse.lnk name $@ option map=$(B)/mouse/mousepf.map file { $< }
	python3 tools/nemark.py $@ 3.10
# C: the packet after Enable. MD2C.EXE (tests/apps/md2c.c, md2cs.asm);
# MS3C.COM, packets of MD2C's choosing (tests/dos/ms3c.asm);
# tests/md2cprobe.sh, tests/md2cview.py
$(B)/tests/md2c.o: tests/apps/md1ref.h tests/apps/tklog.h
$(DOS)/MD2C.EXE: $(B)/tests/start.o $(B)/tests/md2c.o $(B)/tests/md2cs.o tests/apps/app.lnk tests/apps/md2c.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md2c.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/md2c.map file { $(B)/tests/start.o $(B)/tests/md2c.o $(B)/tests/md2cs.o }
$(DOS)/MS3B.COM: tests/dos/ms3b.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/MS3C.COM: tests/dos/ms3c.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/MS4A.COM: tests/dos/ms4a.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/MS4B.COM: tests/dos/ms4b.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/MD2A.EXE $(DOS)/MS3A.DRV $(DOS)/MS4A.COM $(DOS)/MD2B.EXE $(DOS)/MS3B.COM \
     $(DOS)/MS4B.COM $(DOS)/MOUSEPT.DRV $(DOS)/MOUSEPF.DRV $(DOS)/MD2C.EXE $(DOS)/MS3C.COM
