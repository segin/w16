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
KCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=_TEXT $(INC)
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
           $(B)/kernel/profile.o $(B)/kernel/dosx.o $(B)/kernel/winboot.o $(B)/kernel/selstub.o $(B)/kernel/kmisc.o $(B)/kernel/fault.o
BOBJS   := $(B)/boot/boot0.o $(B)/boot/boot.o
W87OBJS := $(B)/win87em/win87em.o $(B)/win87em/emu.o
TESTS   := TK1 TK2 TK3 TK4 TK5 TK6 TK6N TK7 TK8 TK8T1 TK8T2 TK8T3 TK8T4 TK8T5 TK9 TK10 \
           TK11 TK12 TK13 TK14 TK15 TK16
# probes for the Windows boot (tests/dropin.sh); not in make test
DROPIN   := TK20 TK21 TK22
# DLLs and files TK5 uses
TESTDLLS := TKDLL.DLL TKDLB.DLL TKID.TPL TKBAD.DLL TKNODEP.DLL TKNOD.DLL \
            TKUSEX.DLL TKEXEM.EXE TK15A.EXE TK16A.EXE TK16B.EXE TK16C.EXE
# tools that run inside a (reference) Windows: tests/refrun.sh
WINTOOLS := SNAP SNAP2 TW2 TEXP3 TEXP2 TDDE TICONS THELP
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

.PHONY: all host dos shell shell2 test clean
all: host dos shell

host: $(B)/host/nedump
dos: $(DOS)/KRNL286.EXE $(DOS)/WIN87EM.DLL $(TESTS:%=$(DOS)/%.EXE) $(TESTDLLS:%=$(DOS)/%) \
     $(WINTOOLS:%=$(DOS)/%.EXE) \
     $(DOS)/KEYFEED.COM $(DOS)/POWEROFF.COM $(DOS)/WINENV.COM $(B)/hdd/MBR.BIN \
     $(DOS)/TK18.COM $(DROPIN:%=$(DOS)/%.EXE)

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
$(B)/kernel/%.o: kernel/%.c kernel/*.h include/w16/*.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) -fo=$@ $<
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

$(B)/tests/tk3.o: tests/apps/tk3ref.h tests/apps/tklog.h
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
$(B)/tests/tk15.o: tests/apps/tk15ref.h tests/apps/tklog.h tests/apps/tk15.h
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

# TK16.EXE: task probe, part 2; its helpers TK16A (plain), TK16B (imports
# TKBAD.DLL) and TK16C (a second data segment) are all tk16a.c
$(B)/tests/tk16.o: tests/apps/tk16ref.h tests/apps/tklog.h tests/apps/tk15.h tests/apps/tk16.h
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
	    name $@ libpath $(WATCOM)/lib286/win option map=$(B)/tests/tk16$*.map \
	    file { $(B)/tests/tk15s.o $(B)/tests/tk16$*.o }

# TK12.EXE: the resource probe, with the resources of tk12.rc
$(B)/tests/tk12.o: tests/apps/tk12ref.h tests/apps/tklog.h
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
$(B)/tests/snap2.o: tests/apps/snap.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -dSNAP_WIN2 -fo=$@ $<
$(DOS)/SNAP2.EXE: $(B)/tests/start.o $(B)/tests/snap2.o tests/apps/app.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/snap2.map file { $(B)/tests/start.o $(B)/tests/snap2.o }
	python3 tools/nemark.py $@ 2.0

# TW2.EXE: the Windows 2.x probe, marked as a Windows 2.0 program
$(B)/tests/tw2.o: tests/apps/tw2api.h
$(DOS)/TW2.EXE: $(B)/tests/start.o $(B)/tests/tw2.o tests/apps/app.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tw2.map file { $(B)/tests/start.o $(B)/tests/tw2.o }
	python3 tools/nemark.py $@ 2.0

$(DOS)/SNAP.EXE: $(B)/tests/start.o $(B)/tests/snap.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/snap.map file { $(B)/tests/start.o $(B)/tests/snap.o }

# ---- DOS test tools ----
$(DOS)/KEYFEED.COM: tools/dos/keyfeed.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# WINENV.COM: test tool, runs KERNEL with windir= set as WIN.COM would
$(DOS)/WINENV.COM: tests/dos/winenv.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# TK18.COM: the DOSX -> KRNL286 contract probe (runs as SYSTEM\KRNL286.EXE)
$(DOS)/TK18.COM: tests/dos/tk18.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

$(DOS)/POWEROFF.COM: tools/dos/poweroff.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(B)/hdd/MBR.BIN: tools/dos/mbr.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

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

test: dos
	tests/run-tests.sh

clean:
	rm -rf $(B)
