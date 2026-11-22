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
           TK11 TK12 TK13 TK14 TK15 TK16 TK17 TK17A TK19 TK54 TK58 TK60 TK61 TK62 TK64 TK65 TK66 \
           TK67P1 TK67P2 TK67P3 TK67P4 TK67P5 TK67P6 TK67P7 TK67P8 TK67P9 TK67P10 TK67P11 TK67P12 TK67P13
# probes for the Windows boot (tests/dropin.sh); not in make test
# DX probes (tests/dosxtest.sh): DOS extender probes in the KERNEL slot
DXPROBES := DX1 DX2 DX4 DX5 DX8
DROPIN   := TK20 TK21 TK22 TK23 TK24 TK25 TK26 TK27 TK28 TK29 TK30 TK31 TK32 TK33 TK34 TK35 TK36 TK37 TK38 TK39 TK40 TK41 TK42 TK43 TK44 TK45 TK46 TK47 TK48 TK49 TK50 TK51 TK51C TK52 TK53 TK55 TK56 TK57 TK59 TK63
# TK65's import DLLs (TK65IMP below)
TK65I   := TK65I1.DLL TK65I2.DLL TK65I3.DLL TK65I4.DLL TK65I5.DLL TK65I6.DLL TK65I7.DLL TK65I8.DLL TK65I9.DLL
# DLLs and files TK5 uses
TESTDLLS := TKDLL.DLL TKDLB.DLL TKID.TPL TKBAD.DLL TKNODEP.DLL TKNOD.DLL \
            TKUSEX.DLL TKEXEM.EXE TK15A.EXE TK16A.EXE TK16B.EXE TK16C.EXE TK16S.EXE TK16I.EXE TK61A.EXE \
            TK62A.EXE TKENT.DLL TKENT2.DLL TKBADNE.DLL TKBIGDG.DLL TKDG.DLL TKWA.DLL TKWB.DLL \
            TK64A.EXE TK64M.EXE TK64O.EXE TK64P.EXE TK65D.DLL $(TK65I) \
            TK66L.EXE TK66S.EXE TK66R.EXE TK66F.EXE TK66I.EXE TK66N.EXE TK66E.EXE TKBADNM.DLL \
            TK67H.EXE \
            TKEA.DLL TKEB.DLL TKEC.DLL TKED.DLL TKEE.DLL
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
dos: $(DOS)/KRNL286.EXE $(DOS)/KRNL386.EXE $(DOS)/WIN87EM.DLL $(TESTS:%=$(DOS)/%.EXE) $(TESTDLLS:%=$(DOS)/%) \
     $(WINTOOLS:%=$(DOS)/%.EXE) \
     $(DOS)/KEYFEED.COM $(DOS)/POWEROFF.COM $(DOS)/WINENV.COM $(B)/hdd/MBR.BIN \
     $(DOS)/TK18.COM $(DROPIN:%=$(DOS)/%.EXE) $(DOS)/PENWIN.DLL $(DOS)/MS1.COM \
     $(DOS)/DOSX.EXE $(DOS)/DOSXT.EXE $(DXPROBES:%=$(DOS)/%.COM) $(DOS)/XM1.COM $(DOS)/DX6.EXE $(DOS)/DX7.EXE $(DOS)/DXD.COM

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
# the global heap (global.c) in a code segment of its own, HEAP_TEXT, as
# KRNL386's (below): _TEXT had outgrown 64K, and KERNEL then has 3.1's
# four segments (code, two more code segments, DGROUP: TK62 "kseg")
$(B)/kernel/global.o: kernel/global.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) -nt=HEAP_TEXT -fo=$@ $<
$(DOS)/KRNL286.EXE: $(KOBJS) $(B)/boot/boot.exe kernel/kernel.lnk
	@mkdir -p $(@D)
	$(WLINK) @kernel/kernel.lnk name $@ option stub=$(B)/boot/boot.exe \
	    option map=$(B)/kernel/krnl286.map file { $(KOBJS) }

# ---- KRNL386.EXE: the same kernel/ sources built with -DKRNL386 (the
# differences Windows 3.1's KRNL386 shows, each observed: see
# docs/notes/krnl386-handover.md), its objects in build/kernel386: 386
# code (wcc -3), with FS and GS pegged (-zfp -zgp): left to the compiler,
# it kept selectors in FS (util.c's string loops), and a 386 DOS
# extender, saving and reloading FS around an interrupt, faulted on one
# freed meanwhile (GP in DOSX with the selector as the error code). The
# assembly helpers KRNL386 needs are kernel/k386.asm ----
K386C := -3 -zfp -zgp -dKRNL386
K386OBJS := $(KOBJS:$(B)/kernel/%=$(B)/kernel386/%) $(B)/kernel386/k386.o $(B)/kernel386/k386c.o $(B)/kernel386/global386.o
$(B)/kernel386/%.o: kernel/%.asm kernel/inc/intcall.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -DKRNL386 -o $@ $<
$(B)/kernel386/%.o: kernel/%.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) $(K386C) -fo=$@ $<
$(KBOOT_C:%=$(B)/kernel386/%.o) $(B)/kernel386/k386c.o: $(B)/kernel386/%.o: kernel/%.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) $(K386C) -nt=BOOT_TEXT -fo=$@ $<
# KRNL386's global heap (global386.c) in a code segment of its own
$(B)/kernel386/global386.o: kernel/global386.c kernel/*.h include/w16/*.h $(B)/kernel/kapi.h
	@mkdir -p $(@D)
	$(WCC) $(KCFLAGS) $(K386C) -nt=HEAP_TEXT -fo=$@ $<
$(DOS)/KRNL386.EXE: $(K386OBJS) $(B)/boot/boot.exe kernel/kernel.lnk
	@mkdir -p $(@D)
	$(WLINK) @kernel/kernel.lnk name $@ option stub=$(B)/boot/boot.exe \
	    option map=$(B)/kernel386/krnl386.map file { $(K386OBJS) }

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
$(B)/tests/tk53.o: tests/apps/tk53ref.h tests/apps/tk53ref286.h tests/apps/tklog.h
$(B)/tests/tk54.o: tests/apps/tk54ref.h tests/apps/tk54ref286.h tests/apps/tklog.h
$(B)/tests/tk4.o: tests/apps/tk4ref.h tests/apps/tklog.h
$(B)/tests/tk5.o: tests/apps/tk5ref.h tests/apps/tklog.h
$(B)/tests/tk11.o: tests/apps/tk11ref.h tests/apps/tklog.h
$(B)/tests/tk13.o: tests/apps/tk13ref.h tests/apps/tk13ref286.h tests/apps/tklog.h
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

# TK57.EXE: KERNEL in 386 Enhanced mode: WinFlags, the Reboot API (tk57s.asm)
$(B)/tests/tk57.o: tests/apps/tk57ref.h tests/apps/tklog.h
$(DOS)/TK57.EXE: $(B)/tests/start.o $(B)/tests/tk57.o $(B)/tests/tk57s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk57.map file { $(B)/tests/start.o $(B)/tests/tk57.o $(B)/tests/tk57s.o }

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
# VIDMODE.COM: exit code = the BIOS video mode (boot-test lastexit)
$(DOS)/VIDMODE.COM: tests/dos/vidmode.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/VIDMODE.COM
# CLOCKSET.COM: DOS's date and time set to a fixed moment (tests/dropin.sh,
# before WIN: screens never show the host's clock)
$(DOS)/CLOCKSET.COM: tests/dos/clockset.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/CLOCKSET.COM
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

# TK60.EXE: finding loaded modules by name (module TKSIXTY: tk60.lnk)
$(B)/tests/tk60.o: tests/apps/tk60ref.h tests/apps/tklog.h
$(DOS)/TK60.EXE: $(B)/tests/start.o $(B)/tests/tk60.o tests/apps/app.lnk tests/apps/tk60.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk60.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk60.map file { $(B)/tests/start.o $(B)/tests/tk60.o }
# TK61.EXE: freed selectors (descriptor helpers tk17s.asm, tk61s.asm);
# TK61A.EXE, the task it starts, ends with its blocks in FS and GS
$(B)/tests/tk61.o: tests/apps/tk61ref.h tests/apps/tk61ref286.h tests/apps/tklog.h tests/apps/tk61.h
$(B)/tests/tk61a.o: tests/apps/tk61.h
$(DOS)/TK61.EXE: $(B)/tests/start.o $(B)/tests/tk61.o $(B)/tests/tk17s.o $(B)/tests/tk61s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk61.map \
	    file { $(B)/tests/start.o $(B)/tests/tk61.o $(B)/tests/tk17s.o $(B)/tests/tk61s.o }
$(DOS)/TK61A.EXE: $(B)/tests/start.o $(B)/tests/tk61a.o $(B)/tests/tk17s.o $(B)/tests/tk61s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk61a.map \
	    file { $(B)/tests/start.o $(B)/tests/tk61a.o $(B)/tests/tk17s.o $(B)/tests/tk61s.o }
# TK62.EXE: the KERNEL audit's regressions and observations; TK62A.EXE,
# the task it starts; TKENT.DLL and TKENT2.DLL, entry tables of 13300 and
# 12000 entries (tools/mkne.py)
$(B)/tests/tk62.o: tests/apps/tk62ref.h tests/apps/tk62ref286.h tests/apps/tklog.h tests/apps/tk62.h
$(B)/tests/tk62a.o: tests/apps/tk62.h
$(DOS)/TK62.EXE: $(B)/tests/start.o $(B)/tests/tk62.o $(B)/tests/tk17s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk62.map \
	    file { $(B)/tests/start.o $(B)/tests/tk62.o $(B)/tests/tk17s.o }
$(DOS)/TK62A.EXE: $(B)/tests/start.o $(B)/tests/tk62a.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk62a.map file { $(B)/tests/start.o $(B)/tests/tk62a.o }
$(DOS)/TKENT.DLL: tools/mkne.py
	@mkdir -p $(@D)
	python3 tools/mkne.py $@ --module TKENT --entries 13300
$(DOS)/TKENT2.DLL: tools/mkne.py
	@mkdir -p $(@D)
	python3 tools/mkne.py $@ --module TKENT2 --entries 12000
# TKBADNE.DLL: an entry table length that runs the tables past 64K (H9)
$(DOS)/TKBADNE.DLL: tools/mkne.py
	@mkdir -p $(@D)
	python3 tools/mkne.py $@ --module TKBADNE --entry-len 0xFFF0
# TKBIGDG.DLL: a DGROUP with FFF8h bytes of file data (H7); TKDG.DLL: 100h
$(DOS)/TKBIGDG.DLL: tools/mkne.py
	@mkdir -p $(@D)
	python3 tools/mkne.py $@ --module TKBIGDG --dgroup 0xFFF8
# TKWA.DLL imports TKWB.DLL: TK62 "wep" (the order of their WEPs)
$(DOS)/TKWA.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tkwa.o tests/apps/dll.lnk tests/apps/tkwa.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkwa.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tkwa.map file { $(B)/tests/dllstart.o $(B)/tests/dll/tkwa.o }
$(DOS)/TKWB.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tkwb.o tests/apps/dll.lnk tests/apps/tkwb.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tkwb.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tkwb.map file { $(B)/tests/dllstart.o $(B)/tests/dll/tkwb.o }
$(DOS)/TKDG.DLL: tools/mkne.py
	@mkdir -p $(@D)
	python3 tools/mkne.py $@ --module TKDG --dgroup 0x100
# TK64.EXE: KERNEL's FileCdr (tests/apps/tk64.c); TK64A.EXE, the task it
# starts. Its callback runs on other tasks' stacks: SS != DS (-zu).
$(B)/tests/tk64.o: tests/apps/tk64.c tests/apps/tk64ref.h tests/apps/tklog.h tests/apps/tk64.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/tests/tk64a.o: tests/apps/tk64.h
$(DOS)/TK64.EXE: $(B)/tests/start.o $(B)/tests/tk64.o $(B)/tests/tk64s.o tests/apps/app.lnk tests/apps/tk64.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk64.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk64.map \
	    file { $(B)/tests/start.o $(B)/tests/tk64.o $(B)/tests/tk64s.o }
# TK64M, TK64O, TK64P: the same program, one case each (its file name)
$(DOS)/TK64M.EXE $(DOS)/TK64O.EXE $(DOS)/TK64P.EXE: $(DOS)/TK64.EXE
	cp $< $@
$(DOS)/TK64A.EXE: $(B)/tests/start.o $(B)/tests/tk64a.o tests/apps/app.lnk tests/apps/tk64.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk64.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk64a.map file { $(B)/tests/start.o $(B)/tests/tk64a.o }
# TK66.EXE: the KERNEL audit, third round (tests/apps/tk66.c), with its
# resource (tk66.rc); TK66L, S, R, F, I, N and E: the same program, one
# case each (its file name)
$(B)/tests/tk66.o: tests/apps/tk66ref.h tests/apps/tk66ref286.h tests/apps/tklog.h
$(B)/tests/tk66.res: tests/apps/tk66.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/TK66.EXE: $(B)/tests/start.o $(B)/tests/tk66.o $(B)/tests/tk17s.o $(B)/tests/tk66.res tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk66.map option resource=$(B)/tests/tk66.res \
	    file { $(B)/tests/start.o $(B)/tests/tk66.o $(B)/tests/tk17s.o }
$(DOS)/TK66L.EXE $(DOS)/TK66S.EXE $(DOS)/TK66R.EXE $(DOS)/TK66F.EXE $(DOS)/TK66I.EXE $(DOS)/TK66N.EXE $(DOS)/TK66E.EXE: $(DOS)/TK66.EXE
	cp $< $@
# TKEA-TKEE.DLL: 8000, 10000, 10800, 11000 and 11500 entries (TK66E)
TKE_N := A:8000 B:10000 C:10800 D:11000 E:11500
$(foreach e,$(TKE_N),$(eval $$(DOS)/TKE$(word 1,$(subst :, ,$(e))).DLL: tools/mkne.py ; @mkdir -p $$(@D) && python3 tools/mkne.py $$@ --module TKE$(word 1,$(subst :, ,$(e))) --entries $(word 2,$(subst :, ,$(e)))))
# TKBADNM.DLL: a named resource type whose name lies past the module
# table (medium 16)
$(DOS)/TKBADNM.DLL: tools/mkne.py
	@mkdir -p $(@D) $(B)/tests
	printf 'x' > $(B)/tests/tkbadnm.bin
	python3 tools/mkne.py $@ --module TKBADNM --type-name-off 0xFFF0 NAMEDT:1:$(B)/tests/tkbadnm.bin

# TK65.EXE: GetProcAddress and imports by name (tests/apps/tk65.c);
# TK65D.DLL, exports in every case; TK65I1-9.DLL, each importing one of
# them by the name at the same place in TK65IMP (and in tk65.c's imp[])
TK65IMP := MixedRes MIXEDRES upperres MixedNr mixednr uppernr UPPERRES lowerres \#4
$(B)/tests/tk65.o: tests/apps/tk65ref.h tests/apps/tklog.h
$(DOS)/TK65.EXE: $(B)/tests/start.o $(B)/tests/tk65.o tests/apps/app.lnk tests/apps/tk65.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk65.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk65.map file { $(B)/tests/start.o $(B)/tests/tk65.o }
$(DOS)/TK65D.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tk65d.o tests/apps/dll.lnk tests/apps/tk65d.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk65d.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk65d.map file { $(B)/tests/dllstart.o $(B)/tests/dll/tk65d.o }
$(TK65I:%=$(DOS)/%): $(DOS)/TK65I%.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/tk65i.o tests/apps/dll.lnk tests/apps/tk65i.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk65i.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option modname=TK65I$* \
	    import TKIMP TK65D.\'$(word $*,$(TK65IMP))\' \
	    option map=$(B)/tests/tk65i$*.map file { $(B)/tests/dllstart.o $(B)/tests/dll/tk65i.o }
# TK58.EXE: the KRNL386 global heap probe (KERNEL imports by ordinal: tk58k.lnk)
$(B)/tests/tk58.o: tests/apps/tk58ref.h tests/apps/tklog.h
$(DOS)/TK58.EXE: $(B)/tests/start.o $(B)/tests/tk58.o tests/apps/app.lnk tests/apps/tk58k.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk58k.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk58.map file { $(B)/tests/start.o $(B)/tests/tk58.o }

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
# TK67P1-13: the WIN87EM audit probe, in parts (tests/apps/tk67.c, TK67_P);
# TK67H.EXE, the task TK67P4, 10 and 11 start
$(B)/tests/tk67p%.o: tests/apps/tk67.c tests/apps/tk67.h tests/apps/tk67ref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -dTK67_P=$* -fo=$@ $<
$(DOS)/TK67P%.EXE: $(B)/tests/start.o $(B)/tests/tk67p%.o $(B)/tests/tk6fp.o $(B)/tests/tk67s.o \
                   $(B)/tests/fixrqq.o tests/apps/app.lnk tests/apps/tk67.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk67.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk67p$*.map \
	    file { $(B)/tests/start.o $(B)/tests/tk67p$*.o $(B)/tests/tk6fp.o $(B)/tests/tk67s.o $(B)/tests/fixrqq.o }
$(B)/tests/tk67h.o: tests/apps/tk67h.c tests/apps/tk67.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fpi -fo=$@ $<
$(DOS)/TK67H.EXE: $(B)/tests/start.o $(B)/tests/tk67h.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o \
                  tests/apps/app.lnk tests/apps/tk67h.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/tk67h.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tk67h.map \
	    file { $(B)/tests/start.o $(B)/tests/tk67h.o $(B)/tests/tk6fp.o $(B)/tests/fixrqq.o }
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

# FMX.DLL (module FMX0): the File Manager extension probe (tests/apps/fmx.c,
# tests/wfext.sh); copies FMX1.DLL ... are renamed by tools/nerename.py
dos: $(DOS)/FMX.DLL
$(DOS)/FMX.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/fmx.o tests/apps/dll.lnk tests/apps/fmx.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/fmx.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/fmx.map file { $(B)/tests/dllstart.o $(B)/tests/dll/fmx.o }
# XHELL.DLL: logs a program's SHELL registration calls (tests/apps/xhell.c,
# probe RE15; the program's SHELL imports renamed by tools/nerename.py)
dos: $(DOS)/XHELL.DLL
$(DOS)/XHELL.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/xhell.o tests/apps/dll.lnk tests/apps/xhell.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/xhell.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/xhell.map file { $(B)/tests/dllstart.o $(B)/tests/dll/xhell.o }
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

# DOSXND.EXE: without the drop of a PS/2 packet before the client takes
# INT 74h (a test build: tests/mousestress.sh -D nd)
$(DOS)/DOSXND.EXE: dosx/dosx.asm $(wildcard dosx/*.inc)
	@mkdir -p $(@D)
	$(NASM) -f bin -dNOPS2DROP -I . -o $@ $<
dos: $(DOS)/DOSXND.EXE

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

# XM1.COM: what the XMS driver reports (08h, 88h, 89h; boot-test bigmem notes)
$(DOS)/XM1.COM: tests/dos/xm1.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

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

# FN1.EXE: the font metrics probe; FN3.EXE: its companion for the rest of
# the VGA set and the DOS-box fonts; FN2.EXE: a window showing every
# character of each font (tests/fonts.sh; ARCHITECTURE.md 6.9)
FNPROBES := $(DOS)/FN1.EXE $(DOS)/FN2.EXE $(DOS)/FN3.EXE
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
W386VMM  := $(W386)/vmmasm.o $(W386)/ddb.o $(W386)/vmm.o $(W386)/dpmi.o $(W386)/dosx.o $(W386)/vxdapi.o $(W386)/vxd.o $(W386)/io.o \
            $(W386)/vm.o $(W386)/shell.o $(W386)/vdd.o
W386HDRS := $(W386INC)/w386.h $(W386INC)/w386.inc win386/vmm/vmm.h $(W386INC)/w16vxd.h
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
# w16's own VxD modules (win386/vxd: C and nasm, ARCHITECTURE.md 12.8),
# each an LE VxD and a module of WIN386.EXE's W3 table under Windows 3.1's
# module name
W386VXDHDRS := $(W386INC)/w16vxd.h $(W386INC)/vxdrt.inc
$(W386)/vxd/%.o: win386/vxd/%.c $(W386VXDHDRS)
	@mkdir -p $(@D)
	wcc386 -q -zq -bt=nt -mf -zl -s -ecc -3s -ox -i=$(WATCOM)/h -i=$(W386INC) -fo=$@ $<
$(W386)/vxd/%.o: win386/vxd/%.asm $(W386VXDHDRS)
	@mkdir -p $(@D)
	$(NASM) -f obj -I$(W386INC)/ -o $@ $<
W386VPICD := $(W386)/vxd/vpicdddb.o $(W386)/vxd/vpicd.o $(W386)/vxd/vxdrt.o
$(W386)/VPICD.386: $(W386VPICD) win386/vxd/vpicd.lnk
	$(WLINK) @win386/vxd/vpicd.lnk name $@ option map=$(W386)/vpicd.map file { $(W386VPICD) }
W386VMD := $(W386)/vxd/vmdddb.o $(W386)/vxd/vmd.o $(W386)/vxd/vxdrt.o
$(W386)/VMD.386: $(W386VMD) win386/vxd/vmd.lnk
	$(WLINK) @win386/vxd/vmd.lnk name $@ option map=$(W386)/vmd.map file { $(W386VMD) }
W386VKD := $(W386)/vxd/vkdddb.o $(W386)/vxd/vkd.o $(W386)/vxd/vxdrt.o
$(W386)/VKD.386: $(W386VKD) win386/vxd/vkd.lnk
	$(WLINK) @win386/vxd/vkd.lnk name $@ option map=$(W386)/vkd.map file { $(W386VKD) }
W386REBOOT := $(W386)/vxd/rebootdd.o $(W386)/vxd/reboot.o $(W386)/vxd/vxdrt.o
$(W386)/REBOOT.386: $(W386REBOOT) win386/vxd/reboot.lnk
	$(WLINK) @win386/vxd/reboot.lnk name $@ option map=$(W386)/reboot.map file { $(W386REBOOT) }
W386VSD := $(W386)/vxd/vsdddb.o $(W386)/vxd/vsd.o $(W386)/vxd/vxdrt.o
$(W386)/VSD.386: $(W386VSD) win386/vxd/vsd.lnk
	$(WLINK) @win386/vxd/vsd.lnk name $@ option map=$(W386)/vsd.map file { $(W386VSD) }
W386MODS := VPICD=$(W386)/VPICD.386 VMD=$(W386)/VMD.386 VKD=$(W386)/VKD.386 REBOOT=$(W386)/REBOOT.386 VSD=$(W386)/VSD.386
$(W386)/WIN386.EXE: $(W386)/WIN386L.EXE $(W386)/VMM.386 $(W386)/VPICD.386 $(W386)/VMD.386 $(W386)/VKD.386 $(W386)/REBOOT.386 $(W386)/VSD.386 tools/mkw3.py
	python3 tools/mkw3.py $@ $(W386)/WIN386L.EXE WIN386=$(W386)/VMM.386 $(W386MODS)

# VX1-VX2.COM: the WIN386 probes (tests/vxobs.sh; ARCHITECTURE.md 12)
VXPROBES := $(DOS)/VX1.COM $(DOS)/VX2.COM $(DOS)/VX3.EXE $(DOS)/VX4.COM $(DOS)/VX5.EXE $(DOS)/VX6.COM $(DOS)/VX7.COM $(DOS)/VX8.EXE $(DOS)/VX11.EXE $(DOS)/VX12.COM $(DOS)/VXI.386 $(DOS)/VXP.386 $(DOS)/VXQ.386 $(DOS)/VXS.386 $(DOS)/VXV.386 $(DOS)/SPK.COM
vxprobes: $(VXPROBES)
dos: $(VXPROBES)
$(B)/tests/vx3.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX3.EXE: $(B)/tests/start.o $(B)/tests/vx3.o $(B)/tests/vx3s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx3.map file { $(B)/tests/start.o $(B)/tests/vx3.o $(B)/tests/vx3s.o }
$(B)/tests/vx5.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX5.EXE: $(B)/tests/start.o $(B)/tests/vx5.o $(B)/tests/vx3s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx5.map file { $(B)/tests/start.o $(B)/tests/vx5.o $(B)/tests/vx3s.o }
$(B)/tests/vx8.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX8.EXE: $(B)/tests/start.o $(B)/tests/vx8.o $(B)/tests/vx3s.o $(B)/tests/vx8s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx8.map file { $(B)/tests/start.o $(B)/tests/vx8.o $(B)/tests/vx3s.o $(B)/tests/vx8s.o }
# VX11.EXE: the WIN386 audit's DPMI probe (tests/apps/vx11.c)
$(B)/tests/vx11.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX11.EXE: $(B)/tests/start.o $(B)/tests/vx11.o $(B)/tests/vx11s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx11.map file { $(B)/tests/start.o $(B)/tests/vx11.o $(B)/tests/vx11s.o }
# VXP.386: the probe VxD (tests/vxd/vxp.asm)
$(DOS)/VXP.386: tests/vxd/vxp.asm tests/vxd/vxp.lnk
	@mkdir -p $(@D) $(B)/tests
	$(NASM) -f obj -o $(B)/tests/vxp.o tests/vxd/vxp.asm
	$(WLINK) @tests/vxd/vxp.lnk name $@ file $(B)/tests/vxp.o
$(DOS)/VXI.386: tests/vxd/vxp.asm tests/vxd/vxp.lnk
	@mkdir -p $(@D) $(B)/tests
	$(NASM) -f obj -DIOPROBE -o $(B)/tests/vxi.o tests/vxd/vxp.asm
	$(WLINK) @tests/vxd/vxp.lnk name $@ file $(B)/tests/vxi.o
$(DOS)/VXV.386: tests/vxd/vxp.asm tests/vxd/vxp.lnk
	@mkdir -p $(@D) $(B)/tests
	$(NASM) -f obj -DVSDPROBE -o $(B)/tests/vxv.o tests/vxd/vxp.asm
	$(WLINK) @tests/vxd/vxp.lnk name $@ file $(B)/tests/vxv.o
$(DOS)/VXQ.386: tests/vxd/vxp.asm tests/vxd/vxp.lnk
	@mkdir -p $(@D) $(B)/tests
	$(NASM) -f obj -DBADLINK -o $(B)/tests/vxq.o tests/vxd/vxp.asm
	$(WLINK) @tests/vxd/vxp.lnk name $@ file $(B)/tests/vxq.o
# VXS.386: the DOS-box probe VxD (tests/vxd/vxs.asm)
$(DOS)/VXS.386: tests/vxd/vxs.asm tests/vxd/vxs.lnk
	@mkdir -p $(@D) $(B)/tests
	$(NASM) -f obj -o $(B)/tests/vxs.o tests/vxd/vxs.asm
	$(NASM) -f obj -DSVCHOOK -o $(B)/tests/vxsh.o tests/vxd/vxs.asm
	$(WLINK) @tests/vxd/vxs.lnk name $(DOS)/VXSH.386 file $(B)/tests/vxsh.o
	$(NASM) -f obj -DALLVM -o $(B)/tests/vxss.o tests/vxd/vxs.asm
	$(WLINK) @tests/vxd/vxs.lnk name $(DOS)/VXSS.386 file $(B)/tests/vxss.o
	$(WLINK) @tests/vxd/vxs.lnk name $@ file $(B)/tests/vxs.o
# SPK.COM: the Virtual Sound Device's DOS-side probe (tests/vsdprobe.sh)
$(DOS)/SPK.COM: tests/dos/spk.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
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
           --info _version=3.10.0.103 \
           --info "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
           --info ProductName=w16
FONS    := $(FNT)/VGASYS.FON $(FNT)/VGAFIX.FON $(FNT)/VGAOEM.FON $(FNT)/SSERIFE.FON
SSERIFE_SRC := $(foreach p,8 10 12 14 18 24,fonts/sserife$(p).txt)
.PHONY: fonts
fonts: $(FONS)
all: fonts
$(FNT)/VGASYS.FON: fonts/vgasys.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module FONTS --desc "FONTRES 100,96,96 : System (w16)" --file-size 7280 $(FONTINFO) \
	    --info "FileDescription=System font (VGA res)" --info InternalName=FONTS \
	    --info OriginalFilename=VGASYS.FON 31=$<
$(FNT)/VGAFIX.FON: fonts/vgafix.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module FIXFONTS --desc "FONTRES 100,96,96 : Fixedsys (w16)" --file-size 5360 $(FONTINFO) \
	    --info "FileDescription=Fixedsys font (VGA res)" --info InternalName=FIXFONTS \
	    --info OriginalFilename=VGAFIX.FON 31=$<
$(FNT)/VGAOEM.FON: fonts/vgaoem.txt tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module OEMFONTS --desc "FONTRES 100,96,96 : Terminal (US) (w16)" --file-size 5168 $(FONTINFO) \
	    --info "FileDescription=Terminal font, code page 437 (VGA res)" --info InternalName=OEMFONTS \
	    --info OriginalFilename=VGAOEM.FON 30=$<
# the description after the colon is the [fonts] key Control Panel writes
# to WIN.INI, so it is Windows 3.1's
$(FNT)/SSERIFE.FON: $(SSERIFE_SRC) tools/fontc.py
	@mkdir -p $(@D)
	$(FONTC) fon $@ --module SSERIFE \
	    --desc "FONTRES 100,96,96 : MS Sans Serif 8,10,12,14,18,24 (VGA res)" --file-size 64544 $(FONTINFO) \
	    --info "FileDescription=MS Sans Serif font (VGA res)" --info InternalName=SSERIFE \
	    --info OriginalFilename=SSERIFE.FON \
	    84=fonts/sserife8.txt 85=fonts/sserife10.txt 86=fonts/sserife12.txt \
	    87=fonts/sserife14.txt 88=fonts/sserife18.txt 89=fonts/sserife24.txt

# The rest of the VGA set (ARCHITECTURE.md 6.9.2): sources drawn by
# tools/fontgen.py (fonts/skeleton.txt, fonts/tiny.txt) and committed.
# Module names, resource IDs and resource sizes (SizeofResource) are what
# probe FN3 observes on 3.1: SMALLE.FON aligns its resources to 512 bytes,
# two DOSAPP.FON fonts are padded to 3.1's resource lengths. [fonts] keys
# are the descriptions of the WIN.INI ones.
FONS2   := $(FNT)/COURE.FON $(FNT)/SERIFE.FON $(FNT)/SMALLE.FON $(FNT)/SYMBOLE.FON \
           $(FNT)/DOSAPP.FON $(FNT)/EGA80WOA.FON $(FNT)/EGA40WOA.FON \
           $(FNT)/CGA80WOA.FON $(FNT)/CGA40WOA.FON
FONS    += $(FONS2)
fonts: $(FONS2)
comma := ,
# fon-rule FILE, description, file description, extra options, ID=SOURCE...
define fon-rule
$(FNT)/$(1).FON: $(foreach s,$(5),$(word 2,$(subst =, ,$(s)))) tools/fontc.py
	@mkdir -p $$(@D)
	$$(FONTC) fon $$@ --module $(1) --desc "FONTRES 100,96,96 : $(2)" $$(FONTINFO) \
	    --info "FileDescription=$(3)" --info InternalName=$(1) \
	    --info OriginalFilename=$(1).FON $(4) $(5)
endef
$(eval $(call fon-rule,COURE,Courier 10$(comma)12$(comma)15 (VGA res),Courier font (VGA res),--file-size 23408,\
    90=fonts/coure10.txt 91=fonts/coure12.txt 92=fonts/coure15.txt))
$(eval $(call fon-rule,SERIFE,MS Serif 8$(comma)10$(comma)12$(comma)14$(comma)18$(comma)24 (VGA res),MS Serif font (VGA res),--file-size 57936,\
    78=fonts/serife8.txt 79=fonts/serife10.txt 80=fonts/serife12.txt \
    81=fonts/serife14.txt 82=fonts/serife18.txt 83=fonts/serife24.txt))
$(eval $(call fon-rule,SMALLE,Small Fonts (VGA res),Small Fonts (VGA res),--shift 9 --file-size 26112,\
    1=fonts/smalle2.txt 2=fonts/smalle3.txt 3=fonts/smalle4.txt 4=fonts/smalle5.txt \
    5=fonts/smalle6.txt 6=fonts/smalle7.txt 7=fonts/serife6.txt 8=fonts/serife7.txt))
$(eval $(call fon-rule,SYMBOLE,Symbol 8$(comma)10$(comma)12$(comma)14$(comma)18$(comma)24 (VGA res),Symbol font (VGA res),--file-size 56336,\
    1=fonts/symbole8.txt 2=fonts/symbole10.txt 3=fonts/symbole12.txt \
    4=fonts/symbole14.txt 5=fonts/symbole18.txt 6=fonts/symbole24.txt))
$(eval $(call fon-rule,DOSAPP,Terminal for DOS windows (w16),DOS window fonts (VGA res),--pad 1=3072 --pad 6=9728 --file-size 36656,\
    1=fonts/dosapp4x6.txt 2=fonts/dosapp5x12.txt 3=fonts/dosapp6x8.txt \
    4=fonts/dosapp7x12.txt 5=fonts/dosapp10x18.txt 6=fonts/dosapp12x16.txt))
$(eval $(call fon-rule,EGA80WOA,Terminal 80 columns (w16),DOS window font 8x12,--file-size 5312,131=fonts/ega80woa.txt))
$(eval $(call fon-rule,EGA40WOA,Terminal 40 columns (w16),DOS window font 16x12,--file-size 8368,130=fonts/ega40woa.txt))
$(eval $(call fon-rule,CGA80WOA,Terminal 80 columns (w16),DOS window font 8x8,--file-size 4304,101=fonts/cga80woa.txt))
$(eval $(call fon-rule,CGA40WOA,Terminal 40 columns (w16),DOS window font 16x8,--file-size 6336,100=fonts/cga40woa.txt))

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

# ---- shell: CARDFILE.EXE (shell/cardfile; ARCHITECTURE.md 15), Windows 3.1
# only ----
CF_NAMES := main cards cardwnd crd dialogs filedlg edit print ole help util
SHELL_CF := $(B)/shell/start.o $(CF_NAMES:%=$(B)/shell/cardfile/%.o)
CF_HDRS  := shell/cardfile/cardfile.h
shell: $(DOS)/CARDFILE.EXE
$(B)/shell/cardfile/%.o: shell/cardfile/%.c $(CF_HDRS)
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/res/cardfile.ico: tools/mkcardicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkcardicon.py $@
$(B)/shell/cardfile/cardfile.res: shell/cardfile/cardfile.rc $(CF_HDRS) $(B)/shell/res/cardfile.ico
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/cardfile -i=$(B)/shell/res -fo=$@ $<
$(DOS)/CARDFILE.EXE: $(SHELL_CF) $(B)/shell/cardfile/cardfile.res shell/cardfile/cardfile.lnk \
                     tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/cardfile/cardfile.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/cardfile/cardfile.map \
	    option resource=$(B)/shell/cardfile/cardfile.res file { $(SHELL_CF) }
	python3 tools/nemark.py $@ 3.10

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
$(B)/shell/calc/calcver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 1 \
	    "CompanyName=the w16 contributors" "FileDescription=w16 Calculator" \
	    FileVersion=3.10 InternalName=Calc \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=CALC.EXE ProductName=w16 ProductVersion=3.10
$(B)/shell/calc/calc.res: shell/calc/calc.rc shell/calc/calc.h $(B)/shell/res/calc.ico \
    $(B)/shell/calc/calcver.bin
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/calc -i=$(B)/shell/res -i=$(B)/shell/calc -fo=$@ $<
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

# ---- shell: screen savers (shell/scrnsave; ARCHITECTURE.md 15.8): the
# library (scrnsave.c, pwenc.c, scrnsave.dlg) linked into each saver ----
SCRNSAVE_O := $(B)/shell/start.o $(B)/shell/write/i4.o $(B)/shell/scrnsave/scrnsave.o $(B)/shell/scrnsave/pwenc.o
SCRNSAVE_H := shell/scrnsave/scrnsave.h
SAVERS     := SSSTARS
shell: $(SAVERS:%=$(DOS)/%.SCR)
$(B)/shell/scrnsave/%.o: shell/scrnsave/%.c $(SCRNSAVE_H) $(wildcard shell/scrnsave/ss*.h)
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/scrnsave/ssstars.ico: tools/starsicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/starsicon.py $@
$(B)/shell/scrnsave/%.res: shell/scrnsave/%.rc shell/scrnsave/scrnsave.dlg $(SCRNSAVE_H) \
                           shell/scrnsave/%.h $(B)/shell/scrnsave/%.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/scrnsave -i=$(B)/shell/scrnsave -fo=$@ $<
$(DOS)/SSSTARS.SCR: $(SCRNSAVE_O) $(B)/shell/scrnsave/ssstars.o $(B)/shell/scrnsave/ssstars.res \
                    shell/scrnsave/ssstars.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/scrnsave/ssstars.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/scrnsave/ssstars.map \
	    option resource=$(B)/shell/scrnsave/ssstars.res \
	    file { $(SCRNSAVE_O) $(B)/shell/scrnsave/ssstars.o }
	python3 tools/nemark.py $@ 3.10

# the password encoding on the host against 3.1's observed passwords
# (tests/scripts/ss/pwtest.py, make test)
$(B)/host/pwtest: tests/scripts/ss/pwtest.c shell/scrnsave/pwenc.c
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -o $@ tests/scripts/ss/pwtest.c shell/scrnsave/pwenc.c

# ---- shell: TASKMAN.EXE, the Task List (shell/taskman; ARCHITECTURE.md 15.5) ----
SHELL_TM := $(B)/shell/start.o $(B)/shell/taskman/taskman.o
shell: $(DOS)/TASKMAN.EXE
$(B)/shell/taskman/taskman.o: shell/taskman/taskman.c shell/taskman/taskman.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/taskman/taskman.res: shell/taskman/taskman.rc shell/taskman/taskman.h
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/taskman -fo=$@ $<
$(DOS)/TASKMAN.EXE: $(SHELL_TM) $(B)/shell/taskman/taskman.res shell/taskman/taskman.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/taskman/taskman.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/taskman/taskman.map \
	    option resource=$(B)/shell/taskman/taskman.res file { $(SHELL_TM) }
	python3 tools/nemark.py $@ 3.10

# ---- shell: WRITE.EXE (shell/write/; ARCHITECTURE.md 15.6), Windows 3.1
# only ----
WR_C     := main doc wrifile view cmds pict ole print hdrftr util ruler
SHELL_WR := $(B)/shell/start.o $(B)/shell/write/i4.o $(WR_C:%=$(B)/shell/write/%.o)
shell: $(DOS)/WRITE.EXE
$(B)/shell/write/%.o: shell/write/%.c shell/write/write.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/write/i4.o: shell/write/i4.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/shell/write/write.ico: tools/mkwriteicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkwriteicon.py $@
$(B)/shell/write/rbuttons.bmp: tools/mkwriteruler.py
	@mkdir -p $(@D)
	python3 tools/mkwriteruler.py buttons $@
$(B)/shell/write/rmarks.bmp: tools/mkwriteruler.py
	@mkdir -p $(@D)
	python3 tools/mkwriteruler.py marks $@
$(B)/shell/write/write.res: shell/write/write.rc shell/write/write.h $(B)/shell/write/write.ico \
    $(B)/shell/write/rbuttons.bmp $(B)/shell/write/rmarks.bmp
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/write -i=$(B)/shell/write -fo=$@ $<
$(DOS)/WRITE.EXE: $(SHELL_WR) $(B)/shell/write/write.res shell/write/write.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/write/write.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/write/write.map \
	    option resource=$(B)/shell/write/write.res file { $(SHELL_WR) }
	python3 tools/nemark.py $@ 3.10

# ---- shell: WINFILE.EXE (shell/winfile/; ARCHITECTURE.md 15.7), Windows
# 3.1 only ----
WF_C     := main ini treewin drives tree dir dialogs fileops assoc search dnd disk ext bitmap util
SHELL_WF := $(B)/shell/start.o $(B)/shell/winfile/dosfn.o $(WF_C:%=$(B)/shell/winfile/%.o)
shell: $(DOS)/WINFILE.EXE
$(B)/shell/winfile/%.o: shell/winfile/%.c shell/winfile/winfile.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/winfile/dosfn.o: shell/winfile/dosfn.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/shell/winfile/winfile.ico: tools/mkwfileicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkwfileicon.py $(@D)
$(B)/shell/winfile/wfstrip.bmp: tools/mkwfbmp.py
	@mkdir -p $(@D)
	python3 tools/mkwfbmp.py $@
$(B)/shell/winfile/winfile.res: shell/winfile/winfile.rc shell/winfile/winfile.h \
    $(B)/shell/winfile/winfile.ico $(B)/shell/winfile/wfstrip.bmp tools/mkwfileicon.py
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/winfile -i=$(B)/shell/winfile -fo=$@ $<
$(DOS)/WINFILE.EXE: $(SHELL_WF) $(B)/shell/winfile/winfile.res shell/winfile/winfile.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/winfile/winfile.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/winfile/winfile.map \
	    option resource=$(B)/shell/winfile/winfile.res file { $(SHELL_WF) }
	python3 tools/nemark.py $@ 3.10
# ---- shell: REGEDIT.EXE, the Registration Info Editor (shell/regedit/;
# ARCHITECTURE.md 15.8), Windows 3.1 only ----
RE_C     := main basic advanced reg regfile newstyle
SHELL_RE := $(B)/shell/start.o $(RE_C:%=$(B)/shell/regedit/%.o)
shell: $(DOS)/REGEDIT.EXE
$(B)/shell/regedit/%.o: shell/regedit/%.c shell/regedit/regedit.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/regedit/regedit.ico: tools/mkregicon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkregicon.py $@
$(B)/shell/regedit/regedit.res: shell/regedit/regedit.rc shell/regedit/regedit.h $(B)/shell/regedit/regedit.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/regedit -i=$(B)/shell/regedit -fo=$@ $<
$(DOS)/REGEDIT.EXE: $(SHELL_RE) $(B)/shell/regedit/regedit.res shell/regedit/regedit.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/regedit/regedit.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/regedit/regedit.map \
	    option resource=$(B)/shell/regedit/regedit.res file { $(SHELL_RE) }
	python3 tools/nemark.py $@ 3.10
# ---- shell: PIFEDIT.EXE, the PIF Editor (shell/pifedit/; ARCHITECTURE.md
# 15.11) ----
PE_C     := main dlg pif hotkey
SHELL_PE := $(B)/shell/start.o $(PE_C:%=$(B)/shell/pifedit/%.o)
shell: $(DOS)/PIFEDIT.EXE
$(B)/shell/pifedit/%.o: shell/pifedit/%.c shell/pifedit/pifedit.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/pifedit/pifedit.ico: tools/mkpificon.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkpificon.py $@
$(B)/shell/pifedit/pifedit.res: shell/pifedit/pifedit.rc shell/pifedit/pifedit.h $(B)/shell/pifedit/pifedit.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/pifedit -i=$(B)/shell/pifedit -fo=$@ $<
$(DOS)/PIFEDIT.EXE: $(SHELL_PE) $(B)/shell/pifedit/pifedit.res shell/pifedit/pifedit.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/pifedit/pifedit.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/pifedit/pifedit.map \
	    option resource=$(B)/shell/pifedit/pifedit.res file { $(SHELL_PE) }
	python3 tools/nemark.py $@ 3.10
# ---- shell: WINHELP.EXE, Windows Help (shell/winhelp/; ARCHITECTURE.md
# 15.10) ----
WH_C     := main view api search macro pict marks hfs hlpio hlp para
SHELL_WH := $(B)/shell/start.o $(B)/shell/write/i4.o $(WH_C:%=$(B)/shell/winhelp/%.o)
shell: $(DOS)/WINHELP.EXE
$(B)/shell/winhelp/%.o: shell/winhelp/%.c shell/winhelp/winhelp.h shell/winhelp/hlp.h shell/winhelp/para.h shell/winhelp/hfs.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/winhelp/winhelp.ico: tools/mkhelpart.py tools/mkicons.py
	@mkdir -p $(@D)
	python3 tools/mkhelpart.py $(@D)
$(B)/shell/winhelp/hand.cur $(B)/shell/winhelp/logo.bmp $(B)/shell/winhelp/annot.bmp: $(B)/shell/winhelp/winhelp.ico
$(B)/shell/winhelp/winhelp.res: shell/winhelp/winhelp.rc shell/winhelp/winhelp.h $(B)/shell/winhelp/winhelp.ico \
    $(B)/shell/winhelp/hand.cur $(B)/shell/winhelp/logo.bmp $(B)/shell/winhelp/annot.bmp
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/winhelp -i=$(B)/shell/winhelp -fo=$@ $<
$(DOS)/WINHELP.EXE: $(SHELL_WH) $(B)/shell/winhelp/winhelp.res shell/winhelp/winhelp.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/winhelp/winhelp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/winhelp/winhelp.map \
	    option resource=$(B)/shell/winhelp/winhelp.res file { $(SHELL_WH) }
	python3 tools/nemark.py $@ 3.10
# hlpdump: the .HLP reader on the build host (tests/winhelp/hlpdump.c)
$(B)/hlpdump: tests/winhelp/hlpdump.c shell/winhelp/hlp.c shell/winhelp/para.c shell/winhelp/hlp.h shell/winhelp/para.h
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -Wall -DHLP_HOST -Ishell/winhelp -o $@ tests/winhelp/hlpdump.c shell/winhelp/hlp.c shell/winhelp/para.c
# hfstest: the bookmark and annotation file writer on the build host
# (tests/winhelp/hfstest.c)
$(B)/hfstest: tests/winhelp/hfstest.c shell/winhelp/hfs.c shell/winhelp/hlp.c shell/winhelp/hfs.h shell/winhelp/hlp.h
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -Wall -DHLP_HOST -Ishell/winhelp -o $@ tests/winhelp/hfstest.c shell/winhelp/hfs.c shell/winhelp/hlp.c
# WRMET.EXE: probe WR9, the font metrics Write's layout depends on
dos: $(DOS)/WRMET.EXE
$(DOS)/WRMET.EXE: $(B)/tests/start.o $(B)/tests/wrmet.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/wrmet.map file { $(B)/tests/start.o $(B)/tests/wrmet.o }


test: dos fonts grpcheck $(B)/host/pwtest $(B)/hlpdump
	python3 tests/scripts/ss/pwtest.py $(B)/host/pwtest
	WATCOM=$(WATCOM) tests/winhelp/hlpcheck.sh $(B)/hlpdump
	python3 tools/castcheck.py $$(find user gdi kernel shell win386 win87em winoldap drivers -name '*.c')
	WATCOM=$(WATCOM) python3 tools/irqcheck.py $(DOS)/USER.EXE $(B)/user/user.map $(USER_OBJ) $(B)/user/stubs.o
	python3 tools/irqcheck.py --fixed $(DOS)/SYSTEM.DRV $(DOS)/KEYBOARD.DRV $(DOS)/MOUSE.DRV $(DOS)/COMM.DRV $(DOS)/VGA.DRV $(DOS)/MMSOUND.DRV \
	    $(DOS)/SB16.DRV
	RUNTESTS_DPMI="hdpmi dosx" RUNTESTS_KERNEL="$${RUNTESTS_KERNEL:-KRNL286 KRNL386}" tests/run-tests.sh

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
# CURREC.DRV: a DISPLAY stand-in that only counts the calls and records
# the cursor ones with their time and caller (tests/gdi/currec.asm,
# tests/currec.sh; mouse trails, drivers/vga/README.md)
vgaprobes: $(DOS)/CURREC.DRV
dos: $(DOS)/CURREC.DRV
# CR1.EXE: the cursor calls CURREC.DRV recorded, under real motion
# (tests/apps/cr1.c, tests/currec.sh)
$(DOS)/CR1.EXE: $(B)/tests/start.o $(B)/tests/cr1.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cr1.map file { $(B)/tests/start.o $(B)/tests/cr1.o }
dos: $(DOS)/CR1.EXE
$(B)/gdi/crstubs.asm $(B)/gdi/currec.lnk &: tests/gdi/currec.spec tools/mkgdlog.py
	@mkdir -p $(@D)
	python3 tools/mkgdlog.py tests/gdi/currec.spec VGAREAL $(B)/gdi/crstubs.asm \
	    $(B)/gdi/currec.lnk DISPLAY 'DISPLAY : 100, 96, 96 : VGA Color Display'
	printf 'import __0040H KERNEL.193\nexport CURREC_INFO.900\nexport CURREC_RESET.901\n' >> $(B)/gdi/currec.lnk
$(B)/gdi/crstubs.o: $(B)/gdi/crstubs.asm tests/gdi/gdlog.inc
	$(NASM) $(NFLAGS) -Itests/gdi/ -o $@ $<
$(B)/gdi/currec.o: tests/gdi/currec.asm tests/gdi/gdlog.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -Itests/gdi/ -o $@ $<
$(DOS)/CURREC.DRV: $(B)/gdi/currec.o $(B)/gdi/crstubs.o $(B)/gdi/currec.lnk
	@mkdir -p $(@D)
	$(WLINK) @$(B)/gdi/currec.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/gdi/currec.map file { $(B)/gdi/currec.o $(B)/gdi/crstubs.o }
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
# VGREC.DRV: GD1 that records values only (no dump of what lies behind a
# pointer the driver owns: PDEVICE, physical objects) and the bytes each
# call popped, also of the undocumented entries (drivers/vga/README.md;
# tests/vgaprobe.sh puts it in front of the display driver under test)
vgaprobes: $(DOS)/VGREC.DRV
$(B)/gdi/vgreclog.o: tests/gdi/gdlog.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -dGD_DISPLAY -dGD_VALUES -dGD_POPS -dGDL_TAG='"D"' -fo=$@ $<
$(DOS)/VGREC.DRV: $(B)/gdi/gdlog.o $(B)/gdi/gd1stubs.o $(B)/gdi/vgreclog.o $(B)/gdi/gd1.lnk
	@mkdir -p $(@D)
	$(WLINK) @$(B)/gdi/gd1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/gdi/vgrec.map \
	    file { $(B)/gdi/gdlog.o $(B)/gdi/gd1stubs.o $(B)/gdi/vgreclog.o }
# VG1.EXE: the display driver's values as programs see them (tests/apps/vg1.c;
# tests/vgaprobe.sh -p, Windows 3.1's VGA.DRV against w16's)
$(B)/tests/vg1.o: tests/apps/vg1ref.h tests/apps/tklog.h
$(DOS)/VG1.EXE: $(B)/tests/start.o $(B)/tests/vg1.o $(B)/tests/md1s.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg1.map file { $(B)/tests/start.o $(B)/tests/vg1.o $(B)/tests/md1s.o }
vgaprobes: $(DOS)/VG1.EXE
# VG3.EXE: GetNearestColor for all 2^24 colours (tests/apps/vg3.c)
$(DOS)/VG3.EXE: $(B)/tests/start.o $(B)/tests/vg3.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg3.map file { $(B)/tests/start.o $(B)/tests/vg3.o }
vgaprobes: $(DOS)/VG3.EXE
# VG4.EXE: solid-brush patterns for all colours of a range of reds (tests/apps/vg4.c)
$(DOS)/VG4.EXE: $(B)/tests/start.o $(B)/tests/vg4.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg4.map file { $(B)/tests/start.o $(B)/tests/vg4.o }
vgaprobes: $(DOS)/VG4.EXE
# VG5.EXE: lines of every pen style (tests/apps/vg5.c)
$(B)/tests/vg5.o: tests/apps/vg1ref.h tests/apps/tklog.h
$(DOS)/VG5.EXE: $(B)/tests/start.o $(B)/tests/vg5.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg5.map file { $(B)/tests/start.o $(B)/tests/vg5.o }
vgaprobes: $(DOS)/VG5.EXE
# VG6.EXE: text with every attribute (tests/apps/vg6.c)
$(B)/tests/vg6.o: tests/apps/vg1ref.h tests/apps/tklog.h
$(DOS)/VG6.EXE: $(B)/tests/start.o $(B)/tests/vg6.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg6.map file { $(B)/tests/start.o $(B)/tests/vg6.o }
vgaprobes: $(DOS)/VG6.EXE
# VG7.EXE: raster operations, conversions, DIBs, hatches (tests/apps/vg7.c)
$(B)/tests/vg7.o: tests/apps/vg1ref.h tests/apps/tklog.h
$(DOS)/VG7.EXE: $(B)/tests/start.o $(B)/tests/vg7.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg7.map file { $(B)/tests/start.o $(B)/tests/vg7.o }
vgaprobes: $(DOS)/VG7.EXE
# VG8.EXE: EnumObj, escapes, flood fills (tests/apps/vg8.c)
$(B)/tests/vg8.o: tests/apps/vg1ref.h tests/apps/tklog.h
$(DOS)/VG8.EXE: $(B)/tests/start.o $(B)/tests/vg8.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg8.map file { $(B)/tests/start.o $(B)/tests/vg8.o }
vgaprobes: $(DOS)/VG8.EXE
dos: $(DOS)/VG8.EXE
# VG9.EXE: mouse trails, the MOUSETRAILS escape and the trail on the screen
# (tests/apps/vg9.c, tests/vgatrails.sh)
$(DOS)/VG9.EXE: $(B)/tests/start.o $(B)/tests/vg9.o tests/apps/app.lnk tests/apps/md1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vg9.map file { $(B)/tests/start.o $(B)/tests/vg9.o }
vgaprobes: $(DOS)/VG9.EXE
dos: $(DOS)/VG9.EXE
dos: $(DOS)/VG7.EXE
# the VGA probes are built with the rest (the boot test runs them)
dos: $(DOS)/VGREC.DRV $(DOS)/VG1.EXE $(DOS)/VG3.EXE $(DOS)/VG4.EXE $(DOS)/VG5.EXE $(DOS)/VG6.EXE
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
GOBJS   := $(B)/gdi/entry.o $(B)/gdi/i4.o $(B)/gdi/stubs.o $(B)/gdi/util.o $(B)/gdi/heap.o $(B)/gdi/object.o \
           $(B)/gdi/driver.o $(B)/gdi/dc.o $(B)/gdi/rgn.o $(B)/gdi/bitmap.o \
           $(B)/gdi/blt.o $(B)/gdi/font.o $(B)/gdi/text.o $(B)/gdi/misc.o $(B)/gdi/draw.o \
           $(B)/gdi/meta.o
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
# GD4.EXE: GDI's heaps, object costs and limits (tests/apps/gd4.c; tests/gdiheap.sh)
gdprobes: $(DOS)/GD4.EXE
dos: $(DOS)/GD4.EXE
$(B)/tests/gd4.o: tests/apps/gd4ref.h tests/apps/tklog.h
$(DOS)/GD4.EXE: $(B)/tests/start.o $(B)/tests/gd4.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd4.map file { $(B)/tests/start.o $(B)/tests/gd4.o }
# GD5.EXE: the GDI audit's probe (tests/apps/gd5.c; tests/gdiaudit.sh,
# the boot-test case gdiaudit)
gdprobes: $(DOS)/GD5.EXE
dos: $(DOS)/GD5.EXE
$(B)/tests/gd5.o: tests/apps/gd5ref.h tests/apps/tklog.h
$(DOS)/GD5.EXE: $(B)/tests/start.o $(B)/tests/gd5.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd5.map file { $(B)/tests/start.o $(B)/tests/gd5.o }
# GD7.EXE: DIBs on the VGA, every format and way (tests/apps/gd7.c;
# tests/gdiheap.sh, the boot-test case gdidib)
gdprobes: $(DOS)/GD7.EXE
dos: $(DOS)/GD7.EXE
$(B)/tests/gd7.o: tests/apps/gd7ref.h tests/apps/tklog.h
$(DOS)/GD7.EXE: $(B)/tests/start.o $(B)/tests/gd7.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd7.map file { $(B)/tests/start.o $(B)/tests/gd7.o }
# GD8.EXE: the GDI audit's last open items (tests/apps/gd8.c; the boot-test
# case gdiaudit; under GD1/GD2 with tests/gdiboot.sh -l -L)
gdprobes: $(DOS)/GD8.EXE
dos: $(DOS)/GD8.EXE
$(B)/tests/gd8.o: tests/apps/gd8ref.h tests/apps/tklog.h
$(DOS)/GD8.EXE: $(B)/tests/start.o $(B)/tests/gd8.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd8.map file { $(B)/tests/start.o $(B)/tests/gd8.o }
# GD9.EXE: metafiles (tests/apps/gd9.c; tests/gdiheap.sh -p, the boot-test
# case gdimeta)
gdprobes: $(DOS)/GD9.EXE
dos: $(DOS)/GD9.EXE
$(B)/tests/gd9.o: tests/apps/gd9ref.h tests/apps/tklog.h
$(DOS)/GD9.EXE: $(B)/tests/start.o $(B)/tests/gd9.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd9.map file { $(B)/tests/start.o $(B)/tests/gd9.o }
# GD10.EXE: raster font simulations, italic, underline, strike-out,
# emboldening and integer scaling (tests/apps/gd10.c; the boot-test case gdifont)
gdprobes: $(DOS)/GD10.EXE
dos: $(DOS)/GD10.EXE
$(B)/tests/gd10.o: tests/apps/gd10ref.h tests/apps/tklog.h
$(DOS)/GD10.EXE: $(B)/tests/start.o $(B)/tests/gd10.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd10.map file { $(B)/tests/start.o $(B)/tests/gd10.o }
# GD11.EXE: the font mapper and raster font integer scaling
# (tests/apps/gd11.c, tests/gdi/gd11ref.txt; the boot-test case gdifont)
gdprobes: $(DOS)/GD11.EXE
dos: $(DOS)/GD11.EXE
$(DOS)/GD11.EXE: $(B)/tests/start.o $(B)/tests/gd11.o tests/apps/app.lnk tests/apps/fn.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/fn.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd11.map file { $(B)/tests/start.o $(B)/tests/gd11.o }
# GD12.EXE: integer-scaled fonts drawn for GD1's log (tests/apps/gd12.c;
# tests/gdiboot.sh -l with it as the shell)
gdprobes: $(DOS)/GD12.EXE
$(DOS)/GD12.EXE: $(B)/tests/start.o $(B)/tests/gd12.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd12.map file { $(B)/tests/start.o $(B)/tests/gd12.o }
# GD6.EXE: GDI's 32-bit arithmetic helpers (gdi/i4.asm linked in place of
# the library's; tests/apps/gd6.c); make test
dos: $(DOS)/GD6.EXE
$(B)/tests/gd6.o: tests/apps/gd6ref.h tests/apps/tklog.h
$(DOS)/GD6.EXE: $(B)/tests/start.o $(B)/tests/gd6.o $(B)/gdi/i4.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/gd6.map file { $(B)/tests/start.o $(B)/tests/gd6.o $(B)/gdi/i4.o }
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
WOA_OBJS := $(B)/shell/start.o $(B)/winoldap/winoldap.o $(B)/winoldap/pifread.o $(B)/winoldap/woasm.o
dos: $(DOS)/WINOLDAP.MOD
$(B)/winoldap/%.o: winoldap/%.c winoldap/winoldap.h winoldap/pifread.h
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

# ---- WINOA386.MOD: w16's 386 Enhanced-mode DOS application support
# (winoa386/, ARCHITECTURE.md 12.11): module WINOLDAP, imports GRABBER;
# the PIF reader is WINOLDAP's (winoldap/pifread.c) ----
WOA3_OBJS := $(B)/shell/start.o $(B)/winoa386/winoa386.o $(B)/winoldap/pifread.o $(B)/winoa386/woa386s.o
dos: $(DOS)/WINOA386.MOD
$(B)/winoa386/winoa386.o: winoa386/winoa386.c winoa386/winoa386.h winoldap/pifread.h drivers/grabber/vga3gr/grab.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/winoa386/woa386s.o: winoa386/woa386s.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/winoa386/winoa386.res: winoa386/winoa386.rc winoa386/winoa386.h $(ICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=winoa386 -i=$(B)/shell/res -fo=$@ $<
$(DOS)/WINOA386.MOD: $(WOA3_OBJS) $(B)/winoa386/winoa386.res winoa386/winoa386.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @winoa386/winoa386.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/winoa386/winoa386.map \
	    option resource=$(B)/winoa386/winoa386.res file { $(WOA3_OBJS) }
	python3 tools/nemark.py $@ 3.10

# ---- WSWAP.EXE and DSWAP.EXE: the Standard-mode task switcher (swap/,
# ARCHITECTURE.md 9.9). .COM-format programs (DOS loads any file without an
# MZ header as one) under the names WIN.COM looks for; DSWAPD.EXE logs to
# C:\DSWAP.DBG (a debugging build). SW1.COM: the probe they were observed
# with (tests/dos/sw1.asm, tests/dos/sw1dump.py) ----
dos: $(DOS)/WSWAP.EXE $(DOS)/DSWAP.EXE
$(DOS)/WSWAP.EXE: swap/wswap.asm swap/swap.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Iswap/ -o $@ $<
$(DOS)/DSWAP.EXE: swap/dswap.asm swap/swap.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Iswap/ -o $@ $<
$(DOS)/DSWAPD.EXE: swap/dswap.asm swap/swap.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Iswap/ -dDEBUG -o $@ $<
SWPROBES := $(DOS)/SW1.COM $(DOS)/DSWAPD.EXE
swprobes: $(SWPROBES)
dos: $(SWPROBES)
$(DOS)/SW1.COM: tests/dos/sw1.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# ---- VGACOLOR.2GR: w16's Standard-mode display grabber (drivers/grabber/,
# ARCHITECTURE.md 9.11): a binary image (nasm -f bin) ----
dos: $(DOS)/VGACOLOR.2GR
$(DOS)/VGACOLOR.2GR: drivers/grabber/vgacolor.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<

# ---- VGA.3GR: w16's 386 Enhanced-mode display grabber (drivers/grabber/
# vga3gr/, ARCHITECTURE.md 12.9): an NE DLL, module GRABBER ----
G3CFLAGS := -q -bt=windows -ml -zu -zp1 -3 -s -zl -ox -wx -we -Idrivers/grabber/vga3gr \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win
G3OBJS := $(B)/grab3/entry.o $(B)/grab3/vga3gr.o
$(B)/grab3/entry.o: drivers/grabber/vga3gr/entry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/grab3/vga3gr.o: drivers/grabber/vga3gr/vga3gr.c drivers/grabber/vga3gr/grab.h
	@mkdir -p $(@D)
	$(WCC) $(G3CFLAGS) -fo=$@ $<
$(DOS)/VGA.3GR: $(G3OBJS) drivers/grabber/vga3gr/vga3gr.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/grabber/vga3gr/vga3gr.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/grab3/vga3gr.map file { $(G3OBJS) }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/VGA.3GR

# ---- the display grabbers' probes (ARCHITECTURE.md 9.11, tests/grabber.sh):
# GR1.2GR, a recording stand-in grabber (the test appends the real one);
# GV1.COM, a DOS program drawing a fixed screen in a given video mode;
# CB1.EXE, a clipboard viewer that records every clipboard change ----
GRPROBES := $(DOS)/GR1.2GR $(DOS)/GV1.COM $(DOS)/CB1.EXE $(DOS)/GRT.COM
grprobes: $(GRPROBES)
dos: $(GRPROBES)
$(DOS)/GR1.2GR: tests/dos/gr1.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/GRT.COM: tests/dos/grt.asm tests/dos/gvdraw.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -o $@ $<
$(DOS)/GV1.COM: tests/dos/gv1.asm tests/dos/gvdraw.inc
	@mkdir -p $(@D)
	$(NASM) -f bin -Itests/dos/ -o $@ $<
# GR3.3GR: a recording 386 grabber in front of GRABTGT, a renamed copy of the
# grabber under test (tests/apps/gr3.asm; tests/grabber3.sh)
grprobes: $(DOS)/GR3.3GR $(DOS)/GT3.COM
dos: $(DOS)/GR3.3GR $(DOS)/GT3.COM
$(DOS)/GT3.COM: tests/dos/gt3.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
$(DOS)/GR3.3GR: $(B)/tests/gr3.o tests/apps/gr3.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/gr3.lnk name $@ option map=$(B)/tests/gr3.map file { $(B)/tests/gr3.o }
	python3 tools/nemark.py $@ 3.10
$(DOS)/CB1.EXE: $(B)/tests/start.o $(B)/tests/cb1.o tests/apps/app.lnk tests/apps/cb1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/cb1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cb1.map file { $(B)/tests/start.o $(B)/tests/cb1.o }

# ---- install-tree: build/w16, a Windows 3.1-style directory with w16's
# programs only, each where Windows 3.1 keeps it (the reference install:
# WIN.COM and PROGMAN.EXE in the Windows directory, the rest in SYSTEM).
# assets/README.TXT says what to copy in from one's own Windows to boot it.
# tests/w16tree.sh boots it. build/dos stays as it is.
W16     := $(B)/w16
W16SYS  := $(W16)/SYSTEM
W16TREE := $(W16)/WIN.COM $(W16)/PROGMAN.EXE $(W16)/CLOCK.EXE $(W16)/README.TXT \
           $(W16SYS)/KRNL286.EXE $(W16SYS)/KRNL386.EXE $(W16SYS)/DOSX.EXE $(W16SYS)/WIN87EM.DLL $(W16SYS)/SHELL.DLL \
           $(W16SYS)/USER.EXE $(W16SYS)/GDI.EXE $(W16SYS)/WINOLDAP.MOD \
           $(FONS:$(FNT)/%=$(W16SYS)/%)
install-tree: $(W16TREE)
# the task switcher (swap/)
install-tree: $(W16SYS)/WSWAP.EXE $(W16SYS)/DSWAP.EXE
$(W16SYS)/WSWAP.EXE $(W16SYS)/DSWAP.EXE: $(W16SYS)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
# the Task List (ARCHITECTURE.md 15.5), in the Windows directory as on 3.1
install-tree: $(W16)/TASKMAN.EXE
$(W16)/TASKMAN.EXE: $(DOS)/TASKMAN.EXE
	@mkdir -p $(@D)
	cp $< $@
# Calculator (ARCHITECTURE.md 15.3)
install-tree: $(W16)/CALC.EXE
$(W16)/CALC.EXE: $(DOS)/CALC.EXE
	@mkdir -p $(@D)
	cp $< $@
# Cardfile (ARCHITECTURE.md 15), in the Windows directory as on 3.1
install-tree: $(W16)/CARDFILE.EXE
$(W16)/CARDFILE.EXE: $(DOS)/CARDFILE.EXE
	@mkdir -p $(@D)
	cp $< $@
# the OLE client library Cardfile imports (ole/olecli/; ARCHITECTURE.md 16)
install-tree: $(W16SYS)/OLECLI.DLL
$(W16SYS)/OLECLI.DLL: $(DOS)/OLECLI.DLL
	@mkdir -p $(@D)
	cp $< $@
# the OLE server library servers such as Paintbrush import (ole/olesvr/)
install-tree: $(W16SYS)/OLESVR.DLL
$(W16SYS)/OLESVR.DLL: $(DOS)/OLESVR.DLL
	@mkdir -p $(@D)
	cp $< $@
all: install-tree
$(W16)/WIN.COM $(W16)/PROGMAN.EXE $(W16)/CLOCK.EXE: $(W16)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
# File Manager (shell/winfile/): WINDOWS\WINFILE.EXE, as in 3.1
W16TREE += $(W16)/WINFILE.EXE
install-tree: $(W16)/WINFILE.EXE
$(W16)/WINFILE.EXE: $(DOS)/WINFILE.EXE
	@mkdir -p $(@D)
	cp $< $@
# the screen savers (shell/scrnsave/): WINDOWS\*.SCR, as in 3.1 (Control
# Panel's Desktop lists the Windows directory's)
W16TREE += $(SAVERS:%=$(W16)/%.SCR)
install-tree: $(SAVERS:%=$(W16)/%.SCR)
$(SAVERS:%=$(W16)/%.SCR): $(W16)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
# the Registration Info Editor (shell/regedit/): WINDOWS\REGEDIT.EXE, as in 3.1
W16TREE += $(W16)/REGEDIT.EXE
install-tree: $(W16)/REGEDIT.EXE
$(W16)/REGEDIT.EXE: $(DOS)/REGEDIT.EXE
	@mkdir -p $(@D)
	cp $< $@
# the PIF Editor (shell/pifedit/): WINDOWS\PIFEDIT.EXE, as in 3.1
W16TREE += $(W16)/PIFEDIT.EXE
install-tree: $(W16)/PIFEDIT.EXE
$(W16)/PIFEDIT.EXE: $(DOS)/PIFEDIT.EXE
	@mkdir -p $(@D)
	cp $< $@
# Windows Help (shell/winhelp/): WINDOWS\WINHELP.EXE, as in 3.1 (USER's
# WinHelp starts it from the Windows directory)
W16TREE += $(W16)/WINHELP.EXE
install-tree: $(W16)/WINHELP.EXE
$(W16)/WINHELP.EXE: $(DOS)/WINHELP.EXE
	@mkdir -p $(@D)
	cp $< $@
# Write (shell/write/): WINDOWS\WRITE.EXE, as in 3.1
W16TREE += $(W16)/WRITE.EXE
install-tree: $(W16)/WRITE.EXE
$(W16)/WRITE.EXE: $(DOS)/WRITE.EXE
	@mkdir -p $(@D)
	cp $< $@
$(W16)/README.TXT: assets/README.TXT
	@mkdir -p $(@D)
	sed 's/$$/\r/' $< > $@
# the default SYSTEM.INI, WIN.INI and CONTROL.INI (assets/; w16's own, laid out
# as 3.1's; CONTROL.INI: Control Panel's colour schemes, w16's own)
install-tree: $(W16)/SYSTEM.INI $(W16)/WIN.INI $(W16)/CONTROL.INI
$(W16)/SYSTEM.INI $(W16)/WIN.INI $(W16)/CONTROL.INI: $(W16)/%: assets/%
	@mkdir -p $(@D)
	sed 's/$$/\r/' $< > $@
# Program Manager's default groups (assets/progman: PROGMAN.INI and the
# .GRP files, which tools/mkgrp.py makes from the *.txt definitions and the
# icons of w16's own programs; `make groups` makes them again, and make
# test checks that they are up to date)
GRPS := MAIN ACCESSOR STARTUP
install-tree: $(W16)/PROGMAN.INI $(GRPS:%=$(W16)/%.GRP)
$(W16)/PROGMAN.INI: assets/progman/PROGMAN.INI
	@mkdir -p $(@D)
	sed 's/$$/\r/' $< > $@
$(GRPS:%=$(W16)/%.GRP): $(W16)/%: assets/progman/%
	@mkdir -p $(@D)
	cp $< $@
GRP_ICONS := $(DOS)/PROGMAN.EXE $(DOS)/CONTROL.EXE $(DOS)/NOTEPAD.EXE $(DOS)/CARDFILE.EXE $(DOS)/CALC.EXE $(DOS)/CLOCK.EXE \
             $(DOS)/WRITE.EXE $(DOS)/WINFILE.EXE $(DOS)/REGEDIT.EXE $(DOS)/PIFEDIT.EXE
groups: $(GRP_ICONS)
	for g in $(GRPS); do python3 tools/mkgrp.py -d $(DOS) assets/progman/$$g.txt assets/progman/$$g.GRP || exit 1; done
grpcheck: $(GRP_ICONS)
	@mkdir -p $(B)/grpcheck
	@for g in $(GRPS); do python3 tools/mkgrp.py -d $(DOS) assets/progman/$$g.txt $(B)/grpcheck/$$g.GRP || exit 1; \
	    cmp -s $(B)/grpcheck/$$g.GRP assets/progman/$$g.GRP || \
	    { echo "assets/progman/$$g.GRP is out of date (definition or icons changed): make groups"; exit 1; }; done
	@echo "grpcheck: assets/progman/*.GRP match their definitions and w16's icons"
	python3 tests/grpref.py
.PHONY: groups grpcheck
$(W16SYS)/KRNL286.EXE $(W16SYS)/KRNL386.EXE $(W16SYS)/DOSX.EXE $(W16SYS)/WIN87EM.DLL $(W16SYS)/SHELL.DLL \
$(W16SYS)/USER.EXE $(W16SYS)/GDI.EXE $(W16SYS)/WINOLDAP.MOD: $(W16SYS)/%: $(DOS)/%
	@mkdir -p $(@D)
	cp $< $@
$(FONS:$(FNT)/%=$(W16SYS)/%): $(W16SYS)/%: $(FNT)/%
	@mkdir -p $(@D)
	cp $< $@
# the display grabber (drivers/grabber/)
install-tree: $(W16SYS)/VGACOLOR.2GR
$(W16SYS)/VGACOLOR.2GR: $(DOS)/VGACOLOR.2GR
	@mkdir -p $(@D)
	cp $< $@
install-tree: $(W16SYS)/VGA.3GR
$(W16SYS)/VGA.3GR: $(DOS)/VGA.3GR
	@mkdir -p $(@D)
	cp $< $@
# 386 Enhanced mode (win386/; ARCHITECTURE.md 12): WIN /3. Its built-in
# devices load from SYSTEM.INI's [386Enh] *names (assets/SYSTEM.INI);
# DOS programs there run through WINOA386.MOD (winoa386/)
install-tree: $(W16SYS)/WIN386.EXE $(W16SYS)/WINOA386.MOD
$(W16SYS)/WIN386.EXE: $(W386)/WIN386.EXE
	@mkdir -p $(@D)
	cp $< $@
$(W16SYS)/WINOA386.MOD: $(DOS)/WINOA386.MOD
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
USER_OBJ := $(B)/user/uentry.o $(USER_C:user/%.c=$(B)/user/u_%.o) $(B)/user/gp.o
# -zc: string literals and const data in the code segments, not DGROUP,
# whose room is windows' and classes' (ARCHITECTURE.md 10.3, US15)
UUCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -zc -ox -wx -we \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win
$(B)/user/u_%.o: user/%.c user/user.h
	@mkdir -p $(@D)
	$(WCC) $(UUCFLAGS) -fo=$@ $<
# build/w16ver.h: w16's version (the file VERSION, its one source) and the
# build: the source tree's commit, `git rev-parse --short=7 HEAD`, or
# W16_BUILD from the environment (tools/remote-test.sh passes it: a
# worktree's copy on the build host is no git checkout), else "unknown".
# Rewritten only when it changes, so a build of the same commit is the
# same. USER's desktop stamp shows both (user/deskver.c).
W16_VERSION := $(shell cat VERSION)
W16_BUILD   ?= $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
$(B)/w16ver.h: VERSION FORCE
	@mkdir -p $(@D)
	@printf '#define W16_VERSION "%s"\n#define W16_BUILD "%s"\n' '$(W16_VERSION)' '$(W16_BUILD)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
.PHONY: FORCE
FORCE:
$(B)/user/u_deskver.o: $(B)/w16ver.h
$(B)/user/u_deskver.o: UUCFLAGS += -i=$(B)
$(B)/user/uentry.o: user/uentry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
# gp.asm: what touches a caller's memory, and the "__GP" table (USER.484)
$(B)/user/gp.o: user/gp.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/user/stubs.asm $(B)/user/exports.lnk &: $(USER_OBJ) user/ordinals.txt tools/mkuserexp.py
	python3 tools/mkuserexp.py user/ordinals.txt $(B)/user/exports.lnk $(B)/user/stubs.asm $(USER_OBJ)
$(B)/user/stubs.o: $(B)/user/stubs.asm
	$(NASM) $(NFLAGS) -o $@ $<
# tools/nemovsel.py: no entry-table entries for the __loadds selector
# fixups wlink links through movable entries (the entry table holds
# Windows 3.1's USER exports only)
$(DOS)/USER.EXE: $(USER_OBJ) $(B)/user/stubs.o $(B)/user/exports.lnk user/user.lnk tools/nemovsel.py
	@mkdir -p $(@D)
	$(WLINK) @user/user.lnk @$(B)/user/exports.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/user/user.map file { $(USER_OBJ) $(B)/user/stubs.o }
	python3 tools/nemovsel.py $@
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
# US15: what USER's objects cost in its heaps, and how many fit (tests/apps/us15.c)
$(B)/tests/us15.o: tests/apps/us15ref.h tests/apps/us15ref286.h tests/apps/tklog.h
$(DOS)/US15.EXE: $(B)/tests/start.o $(B)/tests/us15.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us15.map file { $(B)/tests/start.o $(B)/tests/us15.o }
dos: $(DOS)/US15.EXE
# US17: the scroll-bar menu, w16's extension (tests/apps/us17.c)
$(B)/tests/us17.o: tests/apps/us17ref.h tests/apps/tklog.h
$(DOS)/US17.EXE: $(B)/tests/start.o $(B)/tests/us17.o $(B)/tests/mevent.o tests/apps/app.lnk tests/apps/us17.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us17.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us17.map file { $(B)/tests/start.o $(B)/tests/us17.o $(B)/tests/mevent.o }
dos: $(DOS)/US17.EXE
# US18: a maximised MDI child's controls in the frame's menu bar (tests/apps/us18.c)
$(B)/tests/us18.o: tests/apps/us18ref.h tests/apps/tklog.h
$(DOS)/US18.EXE: $(B)/tests/start.o $(B)/tests/us18.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us18.map file { $(B)/tests/start.o $(B)/tests/us18.o }
dos: $(DOS)/US18.EXE
# US19: dragging a scroll bar's thumb with real input (tests/apps/us19.c)
$(B)/tests/us19.o: tests/apps/us19ref.h tests/apps/tklog.h
$(DOS)/US19.EXE: $(B)/tests/start.o $(B)/tests/us19.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us19.map file { $(B)/tests/start.o $(B)/tests/us19.o }
dos: $(DOS)/US19.EXE
# US18A.EXE: US18 marked for Windows 3.10 (BS_USERBUTTON becomes a push button there)
$(DOS)/US18A.EXE: $(DOS)/US18.EXE
	cp $< $@
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/US18A.EXE
# US20: an edit control's text, EM_GETHANDLE and EM_SETHANDLE (tests/apps/us20.c)
$(B)/tests/us20.o: tests/apps/us20ref.h tests/apps/tklog.h
$(DOS)/US20.EXE: $(B)/tests/start.o $(B)/tests/us20.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us20.map file { $(B)/tests/start.o $(B)/tests/us20.o }
dos: $(DOS)/US20.EXE
# US21: Microsoft's Notepad's data segment around its edit control's text (tests/apps/us21.c)
$(B)/tests/us21.o: tests/apps/us20ref.h tests/apps/tklog.h
$(DOS)/US21.EXE: $(B)/tests/start.o $(B)/tests/us21.o tests/apps/app.lnk tests/apps/us21.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us21.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us21.map file { $(B)/tests/start.o $(B)/tests/us21.o }
dos: $(DOS)/US21.EXE
# US23: the system menu in the menu heaps; USER's heaps shrinking
# (tests/apps/us23.c), with its helper task US23A (us23a.c)
$(B)/tests/us23.o: tests/apps/us23ref.h tests/apps/us23ref286.h tests/apps/tklog.h
$(DOS)/US23.EXE: $(B)/tests/start.o $(B)/tests/us23.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us23.map file { $(B)/tests/start.o $(B)/tests/us23.o }
dos: $(DOS)/US23.EXE
$(DOS)/US23A.EXE: $(B)/tests/start.o $(B)/tests/us23a.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us23a.map file { $(B)/tests/start.o $(B)/tests/us23a.o }
dos: $(DOS)/US23A.EXE
# US22: what follows ReleaseCapture (tests/apps/us22.c)
$(B)/tests/us22.o: tests/apps/us20ref.h tests/apps/tklog.h
$(DOS)/US22.EXE: $(B)/tests/start.o $(B)/tests/us22.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us22.map file { $(B)/tests/start.o $(B)/tests/us22.o }
dos: $(DOS)/US22.EXE
# TM1, TM2: the Task List's start and what it lists and does (tests/apps/
# tm1.c, tm2.c; ARCHITECTURE.md 15.5)
$(B)/tests/tm3.o: tests/apps/tm3ref.h tests/apps/tklog.h
$(DOS)/TM3.EXE: $(B)/tests/start.o $(B)/tests/tm3.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tm3.map file { $(B)/tests/start.o $(B)/tests/tm3.o }
dos: $(DOS)/TM3.EXE
$(B)/tests/tm4.o: tests/apps/tm3ref.h tests/apps/tklog.h
$(DOS)/TM4.EXE: $(B)/tests/start.o $(B)/tests/tm4.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tm4.map file { $(B)/tests/start.o $(B)/tests/tm4.o }
dos: $(DOS)/TM4.EXE
$(DOS)/TM1.EXE $(DOS)/TM2.EXE: $(DOS)/TM%.EXE: $(B)/shell/start.o $(B)/tests/tm%.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/tm$*.map file { $(B)/shell/start.o $(B)/tests/tm$*.o }
dos: $(DOS)/TM1.EXE $(DOS)/TM2.EXE
# US25: mouse moves made while a program is busy (tests/apps/us25.c)
$(B)/tests/us25.o: tests/apps/us25ref.h tests/apps/tklog.h
$(DOS)/US25.EXE: $(B)/tests/start.o $(B)/tests/us25.o $(B)/tests/mevent.o tests/apps/app.lnk tests/apps/us25.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us25.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us25.map file { $(B)/tests/start.o $(B)/tests/us25.o $(B)/tests/mevent.o }
dos: $(DOS)/US25.EXE
# US26: the caption's buttons and the scroll bars' arrows held down (tests/apps/us26.c)
$(B)/tests/us26.o: tests/apps/us26ref.h tests/apps/tklog.h
$(DOS)/US26.EXE: $(B)/tests/start.o $(B)/tests/us26.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us26.map file { $(B)/tests/start.o $(B)/tests/us26.o }
dos: $(DOS)/US26.EXE
# US27: USER's functions given a bad pointer, USER's "__GP" (tests/apps/us27.c,
# us27s.asm: the TOOLHELP fault callback; us27.rc)
$(B)/tests/us27.o: tests/apps/us27ref.h tests/apps/tklog.h
$(B)/tests/us27.res: tests/apps/us27.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US27.EXE: $(B)/tests/start.o $(B)/tests/us27.o $(B)/tests/us27s.o $(B)/tests/us27.res \
                 tests/apps/app.lnk tests/apps/us27.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us27.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us27.map option resource=$(B)/tests/us27.res \
	    file { $(B)/tests/start.o $(B)/tests/us27.o $(B)/tests/us27s.o }
dos: $(DOS)/US27.EXE
# US28: where WM_MDICREATE puts CW_USEDEFAULT children as they come and go
# (tests/apps/us28.c)
$(B)/tests/us28.o: tests/apps/us28ref.h tests/apps/tklog.h
$(DOS)/US28.EXE: $(B)/tests/start.o $(B)/tests/us28.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us28.map file { $(B)/tests/start.o $(B)/tests/us28.o }
dos: $(DOS)/US28.EXE
# US31: a window dragged by its caption while timers, posted messages and
# a WH_GETMESSAGE hook that keeps posting (QuickTime's) go on (tests/apps/us31.c)
$(B)/tests/us31.o: tests/apps/us31ref.h tests/apps/tklog.h
$(DOS)/US31.EXE: $(B)/tests/start.o $(B)/tests/us31.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us31.map file { $(B)/tests/start.o $(B)/tests/us31.o }
dos: $(DOS)/US31.EXE
$(B)/tests/us31b.o: tests/apps/us31.c tests/apps/us31ref.h tests/apps/tklog.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -DUS31_OTHER -fo=$@ tests/apps/us31.c
$(DOS)/US31B.EXE: $(B)/tests/start.o $(B)/tests/us31b.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us31b.map file { $(B)/tests/start.o $(B)/tests/us31b.o }
dos: $(DOS)/US31B.EXE
# US29: the USER audit's probe (tests/apps/us29.c; docs/notes/user-audit-triage.md),
# its helper task US29A (us29a.c) and DLL US29D (us29d.c)
$(B)/tests/us29.o: tests/apps/us29ref.h tests/apps/tklog.h
$(DOS)/US29.EXE: $(B)/tests/start.o $(B)/tests/us29.o $(B)/tests/us29s.o $(B)/tests/mevent.o tests/apps/app.lnk tests/apps/us29.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us29.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us29.map file { $(B)/tests/start.o $(B)/tests/us29.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
$(DOS)/US29A.EXE: $(B)/tests/start.o $(B)/tests/us29a.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us29a.map file { $(B)/tests/start.o $(B)/tests/us29a.o }
$(DOS)/US29D.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/us29d.o tests/apps/dll.lnk tests/apps/us29d.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us29d.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us29d.map file { $(B)/tests/dllstart.o $(B)/tests/dll/us29d.o }
dos: $(DOS)/US29.EXE $(DOS)/US29A.EXE $(DOS)/US29D.DLL
# US30: where 3.1 calls the hooks (tests/apps/us30.c; USER audit, item 8)
$(B)/tests/us30.o: tests/apps/us30ref.h tests/apps/tklog.h
$(B)/tests/us30.res: tests/apps/us30.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US30.EXE: $(B)/tests/start.o $(B)/tests/us30.o $(B)/tests/us29s.o $(B)/tests/mevent.o \
                 $(B)/tests/us30.res tests/apps/app.lnk tests/apps/us30.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us30.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us30.map option resource=$(B)/tests/us30.res \
	    file { $(B)/tests/start.o $(B)/tests/us30.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
dos: $(DOS)/US30.EXE
# US32: a list box's height and where it draws a partly shown item
# (tests/apps/us32.c; QuickTime's Control Panel)
$(B)/tests/us32.o: tests/apps/us32ref.h tests/apps/tklog.h
$(B)/tests/us32.res: tests/apps/us32.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US32.EXE: $(B)/tests/start.o $(B)/tests/us32.o $(B)/tests/us29s.o $(B)/tests/mevent.o \
                 $(B)/tests/us32.res tests/apps/app.lnk tests/apps/us32.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us32.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us32.map option resource=$(B)/tests/us32.res \
	    file { $(B)/tests/start.o $(B)/tests/us32.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
dos: $(DOS)/US32.EXE
# US33: how a list box draws (owner-drawn), GetDC(NULL), LB_SETTABSTOPS,
# child dialogs' frames (tests/apps/us33.c)
$(B)/tests/us33.o: tests/apps/us32ref.h tests/apps/tklog.h
$(B)/tests/us33.res: tests/apps/us33.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US33.EXE: $(B)/tests/start.o $(B)/tests/us33.o $(B)/tests/us29s.o $(B)/tests/mevent.o \
                 $(B)/tests/us33.res tests/apps/app.lnk tests/apps/us33.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us33.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us33.map option resource=$(B)/tests/us33.res \
	    file { $(B)/tests/start.o $(B)/tests/us33.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
dos: $(DOS)/US33.EXE
# US34: a combo box's layout in every style and font (tests/apps/us34.c;
# Control Panel's Desktop dialog)
$(B)/tests/us34.o: tests/apps/us34ref.h tests/apps/tklog.h
$(B)/tests/us34.res: tests/apps/us34.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=$@ $<
$(DOS)/US34.EXE: $(B)/tests/start.o $(B)/tests/us34.o $(B)/tests/us29s.o $(B)/tests/mevent.o \
                 $(B)/tests/us34.res tests/apps/app.lnk tests/apps/us34.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us34.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us34.map option resource=$(B)/tests/us34.res \
	    file { $(B)/tests/start.o $(B)/tests/us34.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
dos: $(DOS)/US34.EXE
# US35: a multi-line edit control's word wrap and undo (tests/apps/us35.c)
$(B)/tests/us35.o: tests/apps/us35ref.h tests/apps/tklog.h
$(DOS)/US35.EXE: $(B)/tests/start.o $(B)/tests/us35.o $(B)/tests/us29s.o $(B)/tests/mevent.o \
                 tests/apps/app.lnk tests/apps/us35.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/us35.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us35.map \
	    file { $(B)/tests/start.o $(B)/tests/us35.o $(B)/tests/us29s.o $(B)/tests/mevent.o }
dos: $(DOS)/US35.EXE
# US32S: a window spy for US32's question (tests/apps/us32s.c)
$(B)/tests/us32s.o: tests/apps/us32ref.h tests/apps/tklog.h
$(DOS)/US32S.EXE: $(B)/tests/start.o $(B)/tests/us32s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us32s.map file { $(B)/tests/start.o $(B)/tests/us32s.o }
dos: $(DOS)/US32S.EXE
# US24: a window created with WS_MINIMIZE, as WINOLDAP's icon (tests/apps/us24.c)
$(B)/tests/us24.o: tests/apps/us20ref.h tests/apps/tklog.h
$(DOS)/US24.EXE: $(B)/tests/start.o $(B)/tests/us24.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us24.map file { $(B)/tests/start.o $(B)/tests/us24.o }
dos: $(DOS)/US24.EXE
# US16: what RegisterClass returns, for a 3.00 and a 3.10 program (tests/apps/us16.c)
$(B)/tests/us16.o: tests/apps/us16ref.h tests/apps/tklog.h
$(DOS)/US16.EXE: $(B)/tests/start.o $(B)/tests/us16.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/us16.map file { $(B)/tests/start.o $(B)/tests/us16.o }
$(DOS)/US16A.EXE: $(DOS)/US16.EXE
	cp $< $@
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/US16.EXE $(DOS)/US16A.EXE
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

# ---- Control Panel: CONTROL.EXE (shell/control) and the applet DLLs
# (shell/cpl); ARCHITECTURE.md 14. CP1.CPL is the applet probe
# (tests/apps/cp1.c, tests/cpobs.sh).
# applets: large model, SS != DS (a DLL), 286
CPCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win -ishell/cpl
dos: $(DOS)/CP1.CPL
$(B)/shell/cpl/libentry.o: shell/cpl/libentry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/tests/cp1/cp1.o: tests/apps/cp1.c shell/cpl/cpl.h
	@mkdir -p $(@D)
	$(WCC) $(CPCFLAGS) -fo=$@ $<
$(B)/tests/cp1/cp1-1.ico: tests/apps/cp1icons.py tools/mkicons.py
	python3 tests/apps/cp1icons.py $(@D)
$(B)/tests/cp1/cp1.res: tests/apps/cp1.rc $(B)/tests/cp1/cp1-1.ico
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=$(B)/tests/cp1 -fo=$@ $<
$(DOS)/CP1.CPL: $(B)/shell/cpl/libentry.o $(B)/tests/cp1/cp1.o $(B)/tests/cp1/cp1.res tests/apps/cp1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/cp1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cp1/cp1.map option resource=$(B)/tests/cp1/cp1.res \
	    file { $(B)/shell/cpl/libentry.o $(B)/tests/cp1/cp1.o }
# CONTROL.EXE: medium model like the other shell programs (shell/start.asm)
CPICONS := $(patsubst %,$(B)/shell/cpl/res/%.ico,control color fonts ports mouse \
             desktop keyboard printers intl datetime drivers sound enh386)
dos: $(DOS)/CONTROL.EXE
$(CPICONS) &: tools/cplicons.py tools/mkicons.py
	python3 tools/cplicons.py $(B)/shell/cpl/res
$(B)/shell/control/%.o: shell/control/%.c shell/control/control.h shell/cpl/cpl.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(B)/shell/control/control.res: shell/control/control.rc shell/control/control.h $(CPICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/control -i=$(B)/shell/cpl/res \
	    -fo=$@ $<
$(DOS)/CONTROL.EXE: $(B)/shell/start.o $(B)/shell/control/control.o \
                    $(B)/shell/control/control.res shell/control/control.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/control/control.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/control/control.map \
	    option resource=$(B)/shell/control/control.res \
	    file { $(B)/shell/start.o $(B)/shell/control/control.o }
# MAIN.CPL (shell/cpl/main): the applets of the original's MAIN.CPL
MAINCPL_O := $(B)/shell/cpl/libentry.o $(B)/shell/cpl/main/main.o \
             $(B)/shell/cpl/main/keyboard.o $(B)/shell/cpl/main/mouse.o \
             $(B)/shell/cpl/main/color.o $(B)/shell/cpl/main/desktop.o \
             $(B)/shell/cpl/main/fonts.o $(B)/shell/cpl/main/ports.o \
             $(B)/shell/cpl/arrow.o $(B)/shell/cpl/cplutil.o $(B)/gdi/i4.o
dos: $(DOS)/MAIN.CPL
$(B)/shell/cpl/%.o: shell/cpl/%.c shell/cpl/arrow.h shell/cpl/cplutil.h
	@mkdir -p $(@D)
	$(WCC) $(CPCFLAGS) -fo=$@ $<
$(B)/shell/cpl/main/%.o: shell/cpl/main/%.c shell/cpl/main/maincpl.h shell/cpl/cpl.h \
                         shell/cpl/arrow.h shell/cpl/cplutil.h
	@mkdir -p $(@D)
	$(WCC) $(CPCFLAGS) -fo=$@ $<
$(B)/shell/cpl/main/main.res: shell/cpl/main/main.rc shell/cpl/main/maincpl.h $(CPICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/cpl/main -i=shell/cpl \
	    -i=$(B)/shell/cpl/res -fo=$@ $<
$(DOS)/MAIN.CPL: $(MAINCPL_O) $(B)/shell/cpl/main/main.res shell/cpl/main/main.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/cpl/main/main.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/cpl/main/main.map \
	    option resource=$(B)/shell/cpl/main/main.res file { $(MAINCPL_O) }
# CPWIN386.CPL (shell/cpl/win386): the 386 Enhanced applet, a file of its
# own as on 3.1 (only offered in 386 Enhanced mode)
CPWIN386_O := $(B)/shell/cpl/libentry.o $(B)/shell/cpl/win386/cpwin386.o \
              $(B)/shell/cpl/arrow.o $(B)/shell/cpl/cplutil.o $(B)/gdi/i4.o
dos: $(DOS)/CPWIN386.CPL
$(B)/shell/cpl/win386/%.o: shell/cpl/win386/%.c shell/cpl/win386/cpwin386.h shell/cpl/cpl.h \
                           shell/cpl/arrow.h shell/cpl/cplutil.h
	@mkdir -p $(@D)
	$(WCC) $(CPCFLAGS) -fo=$@ $<
$(B)/shell/cpl/win386/cpwin386.res: shell/cpl/win386/cpwin386.rc shell/cpl/win386/cpwin386.h $(CPICONS)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/cpl/win386 -i=shell/cpl \
	    -i=$(B)/shell/cpl/res -fo=$@ $<
$(DOS)/CPWIN386.CPL: $(CPWIN386_O) $(B)/shell/cpl/win386/cpwin386.res shell/cpl/win386/cpwin386.lnk
	@mkdir -p $(@D)
	$(WLINK) @shell/cpl/win386/cpwin386.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/shell/cpl/win386/cpwin386.map \
	    option resource=$(B)/shell/cpl/win386/cpwin386.res file { $(CPWIN386_O) }

# ---- OLE (ARCHITECTURE.md 16): OLECLI.DLL (ole/olecli/) and its probes ----
# OC1.EXE: scripted OLE client probe; OCSPY.DLL: its log and DDE spy
# (tests/olescenarios.sh, tests/ole/*.txt)
OCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we \
           -i=$(WATCOM)/h -i=$(WATCOM)/h/win
$(B)/ole/i4.o: ole/olecli/i4.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/ole/%.o: tests/ole/%.c
	@mkdir -p $(@D)
	$(WCC) $(OCFLAGS) -fo=$@ $<
$(B)/ole/dllstart.o: tests/apps/dllstart.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/ole/ocfault.o: tests/ole/ocfault.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/OCSPY.DLL: $(B)/ole/dllstart.o $(B)/ole/ocspy.o $(B)/ole/ocfault.o $(B)/ole/i4.o tests/ole/ocspy.lnk tests/apps/dll.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/ole/ocspy.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ole/ocspy.map file { $(B)/ole/dllstart.o $(B)/ole/ocspy.o $(B)/ole/ocfault.o $(B)/ole/i4.o } \
	    library toolhelp.lib
$(DOS)/OC1.EXE: $(B)/tests/start.o $(B)/ole/oc1.o $(B)/ole/i4.o $(DOS)/OCSPY.DLL tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ole/oc1.map option stack=40k file { $(B)/tests/start.o $(B)/ole/oc1.o $(B)/ole/i4.o } \
	    library olecli.lib library shell.lib import SPYLOG OCSPY.1 import SPYSTART OCSPY.2 \
	    import SPYSTOP OCSPY.3 import SPYWINNAME OCSPY.4 import SPYTIMES OCSPY.8
$(DOS)/OCSRV.EXE: $(B)/tests/start.o $(B)/ole/ocsrv.o $(DOS)/OCSPY.DLL tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ole/ocsrv.map file { $(B)/tests/start.o $(B)/ole/ocsrv.o } \
	    import SPYLOG OCSPY.1
# OLECLI.DLL: large model, its own DS in every entry (__loadds), no C library
OLCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -dSTRICT -Iole/olecli \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win
OLOBJS  := $(B)/ole/olentry.o $(B)/ole/olecli.o $(B)/ole/le.o $(B)/ole/dde.o \
           $(B)/ole/stream.o $(B)/ole/pres.o $(B)/ole/clip.o $(B)/ole/reg.o \
           $(B)/ole/defs.o $(B)/ole/i4.o
$(B)/ole/olentry.o: ole/olecli/entry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/ole/%.o: ole/olecli/%.c ole/olecli/olecli.h
	@mkdir -p $(@D)
	$(WCC) $(OLCFLAGS) -fo=$@ $<
$(B)/ole/olecliver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 1.10.0.0 --product 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" \
	    "FileDescription=Object Linking and Embedding Client Library" \
	    FileVersion=1.10.000 InternalName=OLECLI \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=OLECLI.DLL ProductName=w16 ProductVersion=3.10
$(B)/ole/olecli.res: ole/olecli/olecli.rc $(B)/ole/olecliver.bin
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=$(B)/ole -fo=$@ $<
$(B)/ole/exports.lnk: tools/mkoleexp.py docs/exportdb/exports.csv $(wildcard ole/olecli/*.c) ole/olecli/entry.asm
	@mkdir -p $(@D)
	python3 tools/mkoleexp.py $@
$(DOS)/OLECLI.DLL: $(OLOBJS) $(B)/ole/olecli.res ole/olecli/olecli.lnk $(B)/ole/exports.lnk \
                   tools/nestripnames.py tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @ole/olecli/olecli.lnk @$(B)/ole/exports.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ole/olecli.map option resource=$(B)/ole/olecli.res file { $(OLOBJS) }
	python3 tools/nestripnames.py $@ OLECLI_
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/OLECLI.DLL
oleprobes: $(DOS)/OC1.EXE $(DOS)/OCSPY.DLL $(DOS)/OCSRV.EXE
dos: oleprobes
.PHONY: oleprobes

# ---- SHELL.DLL probes (tests/apps/shN.c; ARCHITECTURE.md 13): run as the
# shell on Windows 3.1 and with w16's SHELL.DLL (tests/shellprobe.sh)
SHPROBES := SH1 SH2 SH3 SH4 SH5 SH6 SH7 SH8 SH9 SH10 SH11 SH12 SH13 SH14
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
# the install tree gets it too (build/w16/SYSTEM, assets/README.TXT)
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
$(B)/mouse/mouselh.o: drivers/mouse/mouse.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -DLATEHOOK -o $@ $<
# MOUSELH.DRV: w16's driver enabling the device before it takes INT 74h,
# as Windows 3.1's does (a test build: tests/mousestress.sh -M lh)
$(DOS)/MOUSELH.DRV: $(B)/mouse/mouselh.o drivers/mouse/mouse.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/mouse/mouse.lnk name $@ option map=$(B)/mouse/mouselh.map file { $< }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/MOUSELH.DRV
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

# ---- COMM.DRV: w16's serial and parallel communications driver
# (drivers/comm; README.md there)
$(B)/comm/comm.o: drivers/comm/comm.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/COMM.DRV: $(B)/comm/comm.o drivers/comm/comm.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/comm/comm.lnk name $@ option map=$(B)/comm/comm.map \
	    file { $(B)/comm/comm.o }
	python3 tools/nenoname.py $@ 101
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/COMM.DRV
install-tree: $(W16SYS)/COMM.DRV
$(W16SYS)/COMM.DRV: $(DOS)/COMM.DRV
	@mkdir -p $(@D)
	cp $< $@
# CM1.EXE: the communications driver observed (tests/apps/cm1.c, md1s.asm's
# call thunk; run by tests/commprobe.sh with Windows 3.1's COMM.DRV and
# with w16's, tests/commhost.py at the far end of COM1)
$(B)/tests/cm1.o: tests/apps/cm1ref.h tests/apps/tklog.h
$(DOS)/CM1.EXE: $(B)/tests/start.o $(B)/tests/cm1.o $(B)/tests/md1s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cm1.map file { $(B)/tests/start.o $(B)/tests/cm1.o $(B)/tests/md1s.o }
dos: $(DOS)/CM1.EXE
# ---- SYSTEM.DRV: w16's system driver (drivers/system; README.md there)
$(B)/system/system.o: drivers/system/system.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(DOS)/SYSTEM.DRV: $(B)/system/system.o drivers/system/system.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/system/system.lnk name $@ option map=$(B)/system/system.map \
	    file { $(B)/system/system.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/SYSTEM.DRV
install-tree: $(W16SYS)/SYSTEM.DRV
$(W16SYS)/SYSTEM.DRV: $(DOS)/SYSTEM.DRV
	@mkdir -p $(@D)
	cp $< $@
# ---- SYSTEM.DRV probes (drivers/system; README.md there): SY1.EXE calls
# every export (tests/apps/sy1.c; run by tests/sysprobe.sh with Windows
# 3.1's SYSTEM.DRV and with w16's); SYREC.DRV, a recording SYSTEM in front
# of SYSTGT, a renamed copy of the driver under test (tests/apps/syrec.asm)
$(B)/tests/sy1.o: tests/apps/sy1ref.h tests/apps/tklog.h
$(DOS)/SY1.EXE: $(B)/tests/start.o $(B)/tests/sy1.o $(B)/tests/sy1s.o $(B)/tests/kb1s.o tests/apps/app.lnk tests/apps/sy1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/sy1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/sy1.map file { $(B)/tests/start.o $(B)/tests/sy1.o $(B)/tests/sy1s.o $(B)/tests/kb1s.o }
$(DOS)/SYREC.DRV: $(B)/tests/syrec.o tests/apps/syrec.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/syrec.lnk name $@ option map=$(B)/tests/syrec.map file { $(B)/tests/syrec.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/SY1.EXE $(DOS)/SYREC.DRV
# ---- MMSOUND.DRV: w16's sound driver, module SOUND (drivers/sound;
# README.md there), with its note table from tools/sndtables.py
$(B)/sound/sndtab.inc: tools/sndtables.py
	@mkdir -p $(@D)
	python3 tools/sndtables.py $@
$(B)/sound/sound.o: drivers/sound/sound.asm $(B)/sound/sndtab.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -I$(B)/sound/ -o $@ $<
$(DOS)/MMSOUND.DRV: $(B)/sound/sound.o drivers/sound/sound.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/sound/sound.lnk name $@ option map=$(B)/sound/sound.map \
	    file { $(B)/sound/sound.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/MMSOUND.DRV
install-tree: $(W16SYS)/MMSOUND.DRV
$(W16SYS)/MMSOUND.DRV: $(DOS)/MMSOUND.DRV
	@mkdir -p $(@D)
	cp $< $@
# ---- TIMER.DRV: w16's multimedia timer driver, module TIMER (drivers/timer;
# README.md there; ARCHITECTURE.md 21)
$(B)/timer/timer.o: drivers/timer/timer.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/timer/timerver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 3 --subtype 8 \
	    "CompanyName=w16 project" \
	    "FileDescription=Multimedia timer driver" \
	    FileVersion=3.10 InternalName=timer.drv \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=timer.drv ProductName=w16 ProductVersion=3.10
$(B)/timer/timer.res: drivers/timer/timer.rc $(B)/timer/timerver.bin
	@mkdir -p $(@D)
	cd $(B)/timer && $(WRC) -q -r -bt=windows -fo=timer.res $(CURDIR)/drivers/timer/timer.rc
$(DOS)/TIMER.DRV: $(B)/timer/timer.o $(B)/timer/timer.res drivers/timer/timer.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @drivers/timer/timer.lnk name $@ option map=$(B)/timer/timer.map \
	    option resource=$(B)/timer/timer.res file { $(B)/timer/timer.o }
	python3 tools/nemark.py $@ 3.10 8309 08
dos: $(DOS)/TIMER.DRV
install-tree: $(W16SYS)/TIMER.DRV
$(W16SYS)/TIMER.DRV: $(DOS)/TIMER.DRV
	@mkdir -p $(@D)
	cp $< $@
# ---- SB16.DRV: w16's Sound Blaster 16 driver, module SB16 (drivers/sb16;
# README.md there; ARCHITECTURE.md 24). Large model, SS != DS (it runs on
# its callers' stacks and at interrupt time); not in the default
# SYSTEM.INI (assets/README.TXT says how to enable it) ----
SB16CFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=SB_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
SB16OBJS := $(B)/sb16/sb16a.o $(B)/sb16/sb16.o $(B)/sb16/wave.o $(B)/sb16/midi.o $(B)/sb16/aux.o \
            $(B)/gdi/i4.o
$(B)/sb16/%.o: drivers/sb16/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/sb16/%.o: drivers/sb16/%.c drivers/sb16/sb16.h
	@mkdir -p $(@D)
	$(WCC) $(SB16CFLAGS) -fo=$@ $<
$(B)/sb16/sb16ver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 1.0.0.0 --type 3 --subtype 9 \
	    "CompanyName=w16 project" \
	    "FileDescription=Sound Blaster 16 driver" \
	    FileVersion=1.0 InternalName=sb16.drv \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=sb16.drv ProductName=w16 ProductVersion=3.10
$(B)/sb16/sb16.res: drivers/sb16/sb16.rc $(B)/sb16/sb16ver.bin
	@mkdir -p $(@D)
	cd $(B)/sb16 && $(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -fo=sb16.res $(CURDIR)/drivers/sb16/sb16.rc
$(DOS)/SB16.DRV: $(SB16OBJS) $(B)/sb16/sb16.res drivers/sb16/sb16.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @drivers/sb16/sb16.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/sb16/sb16.map option resource=$(B)/sb16/sb16.res file { $(SB16OBJS) }
	python3 tools/nemark.py $@ 3.10 8309 08
dos: $(DOS)/SB16.DRV
install-tree: $(W16SYS)/SB16.DRV
$(W16SYS)/SB16.DRV: $(DOS)/SB16.DRV
	@mkdir -p $(@D)
	cp $< $@
# SBP.EXE, its playing probe (tests/sb16/sbp.c; tests/sb16/sbaudio.sh)
dos: $(DOS)/SBP.EXE
$(B)/sb16/tests/sbp.o: tests/sb16/sbp.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
SBPOBJS := $(B)/tests/start.o $(B)/sb16/tests/sbp.o $(B)/gdi/i4.o
$(DOS)/SBP.EXE: $(SBPOBJS) tests/apps/app.lnk tests/sb16/sbp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/sb16/sbp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/sb16/tests/sbp.map file { $(SBPOBJS) }
# ---- SN1.EXE: the sound driver observed (tests/apps/sn1.c; run by
# tests/soundprobe.sh with Windows 3.1's MMSOUND.DRV and with w16's)
$(B)/tests/sn1.o: tests/apps/sn1ref.h tests/apps/tklog.h
$(DOS)/SN1.EXE: $(B)/tests/start.o $(B)/tests/sn1.o $(B)/tests/sn1s.o $(B)/tests/kb1s.o tests/apps/app.lnk tests/apps/sn1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/sn1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/sn1.map file { $(B)/tests/start.o $(B)/tests/sn1.o $(B)/tests/sn1s.o $(B)/tests/kb1s.o }
dos: $(DOS)/SN1.EXE
# SNDREC.DRV: a recording SOUND in front of SOUNT, a renamed copy of the
# driver under test (tests/apps/sndrec.asm; tests/soundprobe.sh)
$(DOS)/SNDREC.DRV: $(B)/tests/sndrec.o tests/apps/sndrec.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/sndrec.lnk name $@ option map=$(B)/tests/sndrec.map file { $(B)/tests/sndrec.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/SNDREC.DRV
# KRNLOG.DLL: a recording KERNEL for the sound driver under test, whose
# imports of KERNEL are renamed to KRNLOG (tests/apps/krnlog.c;
# tests/soundprobe.sh beep)
$(DOS)/KRNLOG.DLL: $(B)/tests/dllstart.o $(B)/tests/dll/krnlog.o $(B)/tests/krnlogs.o \
                   tests/apps/dll.lnk tests/apps/krnlog.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/krnlog.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/krnlog.map file { $(B)/tests/dllstart.o $(B)/tests/dll/krnlog.o $(B)/tests/krnlogs.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/KRNLOG.DLL
# COMMREC.DRV: a recording stand-in for COMM.DRV, in front of the real one
# renamed MCOM (tests/apps/commrec.c; tests/commusr.sh)
$(DOS)/COMMREC.DRV: $(B)/tests/dllstart.o $(B)/tests/dll/commrec.o \
                    tests/apps/dll.lnk tests/apps/commrec.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/commrec.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/commrec.map file { $(B)/tests/dllstart.o $(B)/tests/dll/commrec.o }
dos: $(DOS)/COMMREC.DRV
# CM2.EXE: USER's comm functions observed (tests/apps/cm2.c; tests/commusr.sh)
$(B)/tests/cm2.o: tests/apps/cm2ref.h tests/apps/tklog.h
$(DOS)/CM2.EXE: $(B)/tests/start.o $(B)/tests/cm2.o $(B)/tests/md1s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cm2.map file { $(B)/tests/start.o $(B)/tests/cm2.o $(B)/tests/md1s.o }
dos: $(DOS)/CM2.EXE
# ---- KEYBOARD.DRV: w16's keyboard driver (drivers/keyboard; README.md there)
# KB1.EXE: every export called over all its inputs (tests/apps/kb1.c; run by
# tests/kbprobe.sh with Windows 3.1's KEYBOARD.DRV and with w16's)
$(B)/tests/kb1.o: tests/apps/kb1ref.h tests/apps/tklog.h
$(DOS)/KB1.EXE: $(B)/tests/start.o $(B)/tests/kb1.o $(B)/tests/md1s.o $(B)/tests/kb1s.o tests/apps/app.lnk tests/apps/kb1.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/kb1.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/kb1.map file { $(B)/tests/start.o $(B)/tests/kb1.o $(B)/tests/md1s.o $(B)/tests/kb1s.o }
dos: $(DOS)/KB1.EXE
# KBREC.DRV: a recording KEYBOARD in front of KBTARGET, a renamed copy of the
# driver under test (tests/apps/kbrec.asm; tests/kbprobe.sh)
$(DOS)/KBREC.DRV: $(B)/tests/kbrec.o tests/apps/kbrec.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/kbrec.lnk name $@ option map=$(B)/tests/kbrec.map file { $(B)/tests/kbrec.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/KBREC.DRV
# KB2.EXE: keyboard messages and KBREC's and KB4's records under real
# input (tests/apps/kb2.c); KB4.COM: a real-mode TSR recording INT 9, INT
# 16h, INT 15h AH=4Fh (tests/dos/kb4.asm)
$(B)/tests/kb2.o: tests/apps/kb1ref.h tests/apps/tklog.h
$(DOS)/KB2.EXE: $(B)/tests/start.o $(B)/tests/kb2.o $(B)/tests/kb2s.o tests/apps/app.lnk tests/apps/kb2.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/kb2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/kb2.map file { $(B)/tests/start.o $(B)/tests/kb2.o $(B)/tests/kb2s.o }
$(DOS)/KB4.COM: tests/dos/kb4.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/KB2.EXE $(DOS)/KB4.COM
# the driver: NASM, with the ANSI/OEM tables made by tools/kbtables.py
$(B)/keyboard/kbtables.inc: tools/kbtables.py
	@mkdir -p $(@D)
	python3 tools/kbtables.py $@
$(B)/keyboard/keyboard.o: drivers/keyboard/keyboard.asm $(B)/keyboard/kbtables.inc
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -I$(B)/keyboard/ -o $@ $<
$(DOS)/KEYBOARD.DRV: $(B)/keyboard/keyboard.o drivers/keyboard/keyboard.lnk
	@mkdir -p $(@D)
	$(WLINK) @drivers/keyboard/keyboard.lnk name $@ option map=$(B)/keyboard/keyboard.map \
	    file { $(B)/keyboard/keyboard.o }
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/KEYBOARD.DRV
# the install tree gets it too (build/w16/SYSTEM)
install-tree: $(W16SYS)/KEYBOARD.DRV
$(W16SYS)/KEYBOARD.DRV: $(DOS)/KEYBOARD.DRV
	@mkdir -p $(@D)
	cp $< $@

# COMMDLG.DLL and its probe (shell/commdlg/commdlg.mk)
include shell/commdlg/commdlg.mk
# MS5.COM: counts the idle calls (INT 28h, INT 2Fh 1689h) and INT 74h that
# reach real mode (tests/dos/ms5.asm; TK49 reads it)
$(DOS)/MS5.COM: tests/dos/ms5.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -o $@ $<
dos: $(DOS)/MS5.COM

# ---- VGA.DRV: w16's VGA display driver (drivers/vga; README.md there):
# C (large model, SS != DS, exports __loadds) and entry.asm, every segment
# fixed; resources from tools/vgares.py (w16's art from make vgaart, the
# configuration and stock-font data). build/vga/link.lnk names the objects
# so tests/vgaprobe.sh -m can relink them with other resources.
VCFLAGS := -q -bt=windows -ml -zu -zp1 -2 -s -zl -ox -wx -we -Idrivers/vga
VOBJS   := $(B)/vga/entry.o $(B)/vga/vga.o $(B)/vga/surf.o $(B)/vga/rop.o $(B)/vga/color.o \
           $(B)/vga/blt.o $(B)/vga/output.o $(B)/vga/text.o $(B)/vga/dib.o $(B)/vga/cursor.o \
           $(B)/gdi/i4.o
$(B)/vga/entry.o: drivers/vga/entry.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/vga/%.o: drivers/vga/%.c drivers/vga/vga.h
	@mkdir -p $(@D)
	$(WCC) $(VCFLAGS) -fo=$@ $<
$(B)/vga/vga.res: $(B)/vga/.stamp tools/vgares.py tools/fontc.py
	python3 -B tools/vgares.py $(B)/vga $@
$(B)/vga/link.lnk: drivers/vga/vga.lnk Makefile
	@mkdir -p $(@D)
	{ echo '@drivers/vga/vga.lnk'; echo 'file { $(VOBJS) }'; } > $@
$(DOS)/VGA.DRV: $(VOBJS) $(B)/vga/link.lnk $(B)/vga/vga.res tools/nenoname.py tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @$(B)/vga/link.lnk name $@ option map=$(B)/vga/vga.map \
	    option resource=$(B)/vga/vga.res
	python3 tools/nenoname.py $@ 501-502
	python3 tools/nemark.py $@ 3.10
dos: $(DOS)/VGA.DRV
install-tree: $(W16SYS)/VGA.DRV
$(W16SYS)/VGA.DRV: $(DOS)/VGA.DRV
	@mkdir -p $(@D)
	cp $< $@

# MD3.EXE: the mouse stress probe (tests/apps/md3.c, tests/mousestress.sh)
$(B)/tests/md3.o: tests/apps/md1ref.h tests/apps/tklog.h
$(DOS)/MD3.EXE: $(B)/tests/start.o $(B)/tests/md3.o tests/apps/app.lnk tests/apps/md3.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/md3.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/md3.map file { $(B)/tests/start.o $(B)/tests/md3.o }
dos: $(DOS)/MD3.EXE

# VX9.COM (tests/dos/vx9.asm, the generic VX%.COM rule) and VX9A.EXE: a
# task's exit as DOS sees it in 386 Enhanced mode (tests/vx9run.sh)
$(B)/tests/vx9a.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX9A.EXE: $(B)/tests/start.o $(B)/tests/vx9a.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx9a.map file { $(B)/tests/start.o $(B)/tests/vx9a.o }
vxprobes: $(DOS)/VX9.COM $(DOS)/VX9A.EXE $(DOS)/TK51C.EXE
# VX10.EXE: the DOS box API spy (tests/apps/vx10.c, tests/vx10run.sh)
$(B)/tests/vx10.o: tests/apps/vx3ref.h tests/apps/tklog.h
$(DOS)/VX10.EXE: $(B)/tests/start.o $(B)/tests/vx10.o $(B)/tests/vx10s.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/vx10.map file { $(B)/tests/start.o $(B)/tests/vx10.o $(B)/tests/vx10s.o }
vxprobes: $(DOS)/VX10.EXE

# Control Panel in the install tree: CONTROL.EXE in the Windows directory,
# MAIN.CPL in SYSTEM, as on 3.1
install-tree: $(W16)/CONTROL.EXE $(W16SYS)/MAIN.CPL $(W16SYS)/CPWIN386.CPL
$(W16)/CONTROL.EXE: $(DOS)/CONTROL.EXE
	@mkdir -p $(@D)
	cp $< $@
$(W16SYS)/MAIN.CPL: $(DOS)/MAIN.CPL
	@mkdir -p $(@D)
	cp $< $@
$(W16SYS)/CPWIN386.CPL: $(DOS)/CPWIN386.CPL
	@mkdir -p $(@D)
	cp $< $@

# ---- OLESVR (ole/olesvr/; ARCHITECTURE.md 16) and its probes ----
# OS1 (a logging OLE server), OS2 (driver: OLE and raw DDE client) and
# OSSPY.DLL (system-wide DDE spy, shared log C:\OS.LOG); tests/olesvrscen.sh
OSPROBES := $(DOS)/OSSPY.DLL $(DOS)/OS1.EXE $(DOS)/OS2.EXE
osprobes: $(OSPROBES)
dos: $(OSPROBES)
.PHONY: osprobes
$(B)/olesvr/tests/osspy.o: tests/olesvr/osspy.c tests/olesvr/osspy.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/olesvr/tests/%.o: tests/olesvr/%.c tests/olesvr/osspy.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/OSSPY.DLL: $(B)/tests/dllstart.o $(B)/olesvr/tests/osspy.o tests/apps/dll.lnk tests/olesvr/osspy.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/olesvr/osspy.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/olesvr/tests/osspy.map \
	    file { $(B)/tests/dllstart.o $(B)/olesvr/tests/osspy.o }
$(DOS)/OS1.EXE $(DOS)/OS2.EXE: $(DOS)/OS%.EXE: $(B)/tests/start.o $(B)/olesvr/tests/os%.o \
                tests/apps/app.lnk tests/olesvr/os.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/olesvr/os.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/olesvr/tests/os$*.map file { $(B)/tests/start.o $(B)/olesvr/tests/os$*.o }
# ---- DDEML.DLL (shell/ddeml/; ARCHITECTURE.md, DDEML): large model, SS !=
# DS (it runs on its callers' stacks), every entry loads its DS ----
DMLCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=DDEML_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
DMLOBJS   := $(B)/ddeml/entry.o $(B)/ddeml/inst.o $(B)/ddeml/hsz.o $(B)/ddeml/data.o \
             $(B)/ddeml/conv.o $(B)/ddeml/mon.o
ddeml: $(DOS)/DDEML.DLL
dos: $(DOS)/DDEML.DLL
.PHONY: ddeml
$(B)/ddeml/%.o: shell/ddeml/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/ddeml/%.o: shell/ddeml/%.c shell/ddeml/ddeml.h
	@mkdir -p $(@D)
	$(WCC) $(DMLCFLAGS) -fo=$@ $<
$(B)/ddeml/ddemlver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" "FileDescription=DDE Management Library" \
	    FileVersion=3.10 InternalName=DDEML \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=DDEML.DLL ProductName=w16 ProductVersion=3.10
$(B)/ddeml/ddeml.res: shell/ddeml/ddeml.rc $(B)/ddeml/ddemlver.bin
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=$(B)/ddeml -fo=$@ $<
$(DOS)/DDEML.DLL: $(DMLOBJS) $(B)/ddeml/ddeml.res shell/ddeml/ddeml.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/ddeml/ddeml.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ddeml/ddeml.map option resource=$(B)/ddeml/ddeml.res file { $(DMLOBJS) }
	python3 tools/nemark.py $@ 3.00
install-tree: $(W16SYS)/DDEML.DLL
$(W16SYS)/DDEML.DLL: $(DOS)/DDEML.DLL
	@mkdir -p $(@D)
	cp $< $@
# ---- DDEML probes (tests/ddeml/; ARCHITECTURE.md, DDEML) ----
# DM.EXE: DDEML client/server/monitor and raw-DDE peer, logging through
# OSSPY.DLL (tests/olesvr); tests/ddeml/dmrun.sh, tests/ddemlscen.sh
dmprobes: $(DOS)/DM.EXE $(DOS)/OSSPY.DLL
dos: $(DOS)/DM.EXE
.PHONY: dmprobes
$(B)/ddeml/tests/dm.o: tests/ddeml/dm.c tests/olesvr/osspy.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/DM.EXE: $(B)/tests/start.o $(B)/ddeml/tests/dm.o tests/apps/app.lnk tests/ddeml/dm.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/ddeml/dm.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ddeml/tests/dm.map file { $(B)/tests/start.o $(B)/ddeml/tests/dm.o }
# ---- LZEXPAND.DLL (shell/lzexpand/; ARCHITECTURE.md, LZEXPAND): large
# model, SS != DS (it runs on its callers' stacks), every entry loads its DS
LZXCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=LZEXPAND_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
LZXOBJS   := $(B)/lzexpand/entry.o $(B)/lzexpand/lzexp.o
lzexpand: $(DOS)/LZEXPAND.DLL
dos: $(DOS)/LZEXPAND.DLL
.PHONY: lzexpand
$(B)/lzexpand/%.o: shell/lzexpand/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/lzexpand/%.o: shell/lzexpand/%.c shell/lzexpand/lzexpand.h
	@mkdir -p $(@D)
	$(WCC) $(LZXCFLAGS) -fo=$@ $<
$(B)/lzexpand/lzver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" "FileDescription=File expansion library" \
	    FileVersion=3.10 InternalName=LZEXPAND \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=LZEXPAND.DLL ProductName=w16 ProductVersion=3.10
$(B)/lzexpand/lzexpand.res: shell/lzexpand/lzexpand.rc $(B)/lzexpand/lzver.bin
	@mkdir -p $(@D)
	cd $(B)/lzexpand && $(WRC) -q -r -bt=windows -fo=lzexpand.res $(CURDIR)/shell/lzexpand/lzexpand.rc
$(DOS)/LZEXPAND.DLL: $(LZXOBJS) $(B)/lzexpand/lzexpand.res shell/lzexpand/lzexpand.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/lzexpand/lzexpand.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/lzexpand/lzexpand.map option resource=$(B)/lzexpand/lzexpand.res file { $(LZXOBJS) }
	python3 tools/nemark.py $@ 3.00 8301 08
install-tree: $(W16SYS)/LZEXPAND.DLL
$(W16SYS)/LZEXPAND.DLL: $(DOS)/LZEXPAND.DLL
	@mkdir -p $(@D)
	cp $< $@
# ---- VER.DLL (shell/ver/; ARCHITECTURE.md, VER): large model, SS != DS
# (it runs on its callers' stacks), every entry loads its DS
VERCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=VER_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
VEROBJS   := $(B)/ver/entry.o $(B)/ver/ver.o
ver: $(DOS)/VER.DLL
dos: $(DOS)/VER.DLL
.PHONY: ver
$(B)/ver/%.o: shell/ver/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/ver/%.o: shell/ver/%.c shell/ver/ver.h
	@mkdir -p $(@D)
	$(WCC) $(VERCFLAGS) -fo=$@ $<
$(B)/ver/verver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" \
	    "FileDescription=Version checking and file installation library" \
	    FileVersion=3.10 InternalName=VER \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=VER.DLL ProductName=w16 ProductVersion=3.10
$(B)/ver/ver.res: shell/ver/ver.rc $(B)/ver/verver.bin
	@mkdir -p $(@D)
	cd $(B)/ver && $(WRC) -q -r -bt=windows -fo=ver.res $(CURDIR)/shell/ver/ver.rc
$(DOS)/VER.DLL: $(VEROBJS) $(B)/ver/ver.res shell/ver/ver.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/ver/ver.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ver/ver.map option resource=$(B)/ver/ver.res file { $(VEROBJS) }
	python3 tools/nemark.py $@ 3.00 8301 08
install-tree: $(W16SYS)/VER.DLL
$(W16SYS)/VER.DLL: $(DOS)/VER.DLL
	@mkdir -p $(@D)
	cp $< $@
# ---- TOOLHELP.DLL (shell/toolhelp/; ARCHITECTURE.md 22): large model,
# SS != DS (it runs on its callers' stacks), every entry loads its DS
THCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=TH_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
THOBJS   := $(B)/toolhelp/entry.o $(B)/toolhelp/toolhelp.o $(B)/toolhelp/global.o $(B)/gdi/i4.o
toolhelp: $(DOS)/TOOLHELP.DLL
dos: $(DOS)/TOOLHELP.DLL
.PHONY: toolhelp
$(B)/toolhelp/%.o: shell/toolhelp/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/toolhelp/%.o: shell/toolhelp/%.c shell/toolhelp/th.h
	@mkdir -p $(@D)
	$(WCC) $(THCFLAGS) -fo=$@ $<
$(B)/toolhelp/thver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" \
	    "FileDescription=Windows Debug/Tool helper library" \
	    FileVersion=3.10 InternalName=TOOLHELP \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=TOOLHELP.DLL ProductName=w16 ProductVersion=3.10
$(B)/toolhelp/toolhelp.res: shell/toolhelp/toolhelp.rc $(B)/toolhelp/thver.bin
	@mkdir -p $(@D)
	cd $(B)/toolhelp && $(WRC) -q -r -bt=windows -fo=toolhelp.res $(CURDIR)/shell/toolhelp/toolhelp.rc
$(DOS)/TOOLHELP.DLL: $(THOBJS) $(B)/toolhelp/toolhelp.res shell/toolhelp/toolhelp.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/toolhelp/toolhelp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/toolhelp/toolhelp.map option resource=$(B)/toolhelp/toolhelp.res file { $(THOBJS) }
	python3 tools/nemark.py $@ 3.00 8309 08
install-tree: $(W16SYS)/TOOLHELP.DLL
$(W16SYS)/TOOLHELP.DLL: $(DOS)/TOOLHELP.DLL
	@mkdir -p $(@D)
	cp $< $@
# ---- MMSYSTEM.DLL and MMTASK.TSK (shell/mmsystem/; ARCHITECTURE.md 23):
# large model, SS != DS (it runs on its callers' stacks and at interrupt
# time), every entry loads its DS ----
MMCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=MM_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
MMOBJS   := $(B)/mmsystem/mmfix.o $(B)/mmsystem/core.o $(B)/mmsystem/mmio.o $(B)/mmsystem/dev.o \
            $(B)/mmsystem/snd.o $(B)/mmsystem/mci.o $(B)/mmsystem/stubs.o $(B)/gdi/i4.o
mmsystem: $(DOS)/MMSYSTEM.DLL $(DOS)/MMTASK.TSK
dos: $(DOS)/MMSYSTEM.DLL $(DOS)/MMTASK.TSK
.PHONY: mmsystem
$(B)/mmsystem/%.o: shell/mmsystem/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/mmsystem/%.o: shell/mmsystem/%.c shell/mmsystem/mmsys.h
	@mkdir -p $(@D)
	$(WCC) $(MMCFLAGS) -fo=$@ $<
$(B)/mmsystem/mmver.bin: tools/mkver.py
	@mkdir -p $(@D)
	python3 tools/mkver.py $@ --file 3.10.0.103 --type 2 \
	    "CompanyName=w16 project" \
	    "FileDescription=Multimedia system library" \
	    FileVersion=3.1 InternalName=mmsystem.dll \
	    "LegalCopyright=Public domain (CC0 1.0), see the w16 repository" \
	    OriginalFilename=mmsystem.dll ProductName=w16 ProductVersion=3.1
$(B)/mmsystem/mcitab.rc: tools/mkmcitab.py
	@mkdir -p $(@D)
	python3 tools/mkmcitab.py $@
$(B)/mmsystem/mmsystem.res: shell/mmsystem/mmsystem.rc $(B)/mmsystem/mmver.bin $(B)/mmsystem/mcitab.rc
	@mkdir -p $(@D)
	cd $(B)/mmsystem && $(WRC) -q -r -bt=windows -i=. -fo=mmsystem.res $(CURDIR)/shell/mmsystem/mmsystem.rc
$(DOS)/MMSYSTEM.DLL: $(MMOBJS) $(B)/mmsystem/mmsystem.res shell/mmsystem/mmsystem.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/mmsystem/mmsystem.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/mmsystem.map option resource=$(B)/mmsystem/mmsystem.res file { $(MMOBJS) }
	python3 tools/nemark.py $@ 3.10 8309 08
$(DOS)/MMTASK.TSK: $(B)/mmsystem/mmtask.o shell/mmsystem/mmtask.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/mmsystem/mmtask.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/mmtask.map file { $(B)/mmsystem/mmtask.o }
	python3 tools/nemark.py $@ 3.10 030A 08
install-tree: $(W16SYS)/MMSYSTEM.DLL $(W16SYS)/MMTASK.TSK
$(W16SYS)/MMSYSTEM.DLL: $(DOS)/MMSYSTEM.DLL
	@mkdir -p $(@D)
	cp $< $@
$(W16SYS)/MMTASK.TSK: $(DOS)/MMTASK.TSK
	@mkdir -p $(@D)
	cp $< $@
# ---- LZP.EXE, the LZEXPAND probe (tests/lzexpand/; ARCHITECTURE.md,
# LZEXPAND): tests/lzexpand/lzrun.sh, tests/lzexpand/lzscen.sh ----
lzprobes: $(DOS)/LZP.EXE
dos: $(DOS)/LZP.EXE
.PHONY: lzprobes
$(B)/lzexpand/tests/lzp.o: tests/lzexpand/lzp.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/LZP.EXE: $(B)/tests/start.o $(B)/lzexpand/tests/lzp.o tests/apps/app.lnk tests/lzexpand/lzp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/lzexpand/lzp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/lzexpand/tests/lzp.map file { $(B)/tests/start.o $(B)/lzexpand/tests/lzp.o }
# ---- VRP.EXE, the VER.DLL probe (tests/ver/; ARCHITECTURE.md, VER):
# tests/ver/vrun.sh, tests/ver/vscen.sh ----
verprobes: $(DOS)/VRP.EXE
dos: $(DOS)/VRP.EXE
.PHONY: verprobes
$(B)/ver/tests/vrp.o: tests/ver/vrp.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(DOS)/VRP.EXE: $(B)/tests/start.o $(B)/ver/tests/vrp.o tests/apps/app.lnk tests/ver/vrp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/ver/vrp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/ver/tests/vrp.map file { $(B)/tests/start.o $(B)/ver/tests/vrp.o }
# ---- THP.EXE, THC.EXE and THD.DLL: the TOOLHELP.DLL probe, its child and
# its library (tests/toolhelp/; ARCHITECTURE.md 22): tests/toolhelp/thrun.sh
thprobes: $(DOS)/THP.EXE $(DOS)/THC.EXE $(DOS)/THD.DLL
dos: $(DOS)/THP.EXE $(DOS)/THC.EXE $(DOS)/THD.DLL
.PHONY: thprobes
$(B)/toolhelp/tests/%.o: tests/toolhelp/%.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/toolhelp/tests/%.o: tests/toolhelp/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/toolhelp/tests/thp.res: tests/toolhelp/thp.rc
	@mkdir -p $(@D)
	cd $(B)/toolhelp/tests && $(WRC) -q -r -bt=windows -fo=thp.res $(CURDIR)/tests/toolhelp/thp.rc
THPOBJS := $(B)/toolhelp/tests/thstart.o $(B)/toolhelp/tests/thp.o $(B)/toolhelp/tests/thps.o
$(DOS)/THP.EXE: $(THPOBJS) $(B)/toolhelp/tests/thp.res tests/apps/app.lnk tests/toolhelp/thp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/toolhelp/thp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/toolhelp/tests/thp.map option resource=$(B)/toolhelp/tests/thp.res \
	    file { $(THPOBJS) }
THCOBJS := $(B)/toolhelp/tests/thstart.o $(B)/toolhelp/tests/thc.o $(B)/toolhelp/tests/thps.o \
           $(B)/toolhelp/tests/thcs.o
$(DOS)/THC.EXE: $(THCOBJS) tests/apps/app.lnk tests/toolhelp/thc.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/toolhelp/thc.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/toolhelp/tests/thc.map file { $(THCOBJS) }
$(DOS)/THD.DLL: $(B)/tests/dllstart.o $(B)/toolhelp/tests/thd.o tests/apps/dll.lnk tests/toolhelp/thd.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/toolhelp/thd.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/toolhelp/tests/thd.map file { $(B)/tests/dllstart.o $(B)/toolhelp/tests/thd.o }
# ---- DRP.EXE and DRT.DLL, the installable-driver probe and its driver
# (tests/drv/; ARCHITECTURE.md 10.7): tests/drv/drun.sh ----
drvprobes: $(DOS)/DRP.EXE $(DOS)/DRT.DLL
dos: $(DOS)/DRP.EXE $(DOS)/DRT.DLL
.PHONY: drvprobes
$(B)/drv/tests/drp.o: tests/drv/drp.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(B)/drv/tests/drt.o: tests/drv/drt.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(DOS)/DRP.EXE: $(B)/tests/start.o $(B)/drv/tests/drp.o tests/apps/app.lnk tests/drv/drp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/drv/drp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/drv/tests/drp.map file { $(B)/tests/start.o $(B)/drv/tests/drp.o }
$(DOS)/DRT.DLL: $(B)/tests/dllstart.o $(B)/drv/tests/drt.o tests/apps/dll.lnk tests/drv/drt.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/drv/drt.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/drv/tests/drt.map file { $(B)/tests/dllstart.o $(B)/drv/tests/drt.o }
# ---- MMP.EXE and MMC.DLL, the MMSYSTEM probe and its library
# (tests/mmsystem/; ARCHITECTURE.md 23): tests/mmsystem/mrun.sh ----
mmprobes: $(DOS)/MMP.EXE $(DOS)/MMC.DLL
dos: mmprobes
.PHONY: mmprobes
$(B)/mmsystem/tests/%.o: tests/mmsystem/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/mmsystem/tests/mmp.o: tests/mmsystem/mmp.c tests/mmsystem/mmc.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(B)/mmsystem/tests/mmc.o: tests/mmsystem/mmc.c tests/mmsystem/mmc.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/mmsystem/tests/mmio.o: tests/mmsystem/mmio.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(B)/mmsystem/tests/mmdev.o: tests/mmsystem/mmdev.c tests/mmsystem/mmc.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(B)/mmsystem/tests/mmci.o: tests/mmsystem/mmci.c
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
MMPOBJS := $(B)/tests/start.o $(B)/mmsystem/tests/mmp.o $(B)/mmsystem/tests/mmio.o $(B)/mmsystem/tests/mmdev.o \
           $(B)/mmsystem/tests/mmci.o $(B)/gdi/i4.o
$(DOS)/MMP.EXE: $(MMPOBJS) tests/apps/app.lnk tests/mmsystem/mmp.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/mmsystem/mmp.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/tests/mmp.map file { $(MMPOBJS) }
MMCOBJS := $(B)/tests/dllstart.o $(B)/mmsystem/tests/mmc.o $(B)/mmsystem/tests/mmcs.o
$(DOS)/MMC.DLL: $(MMCOBJS) tests/apps/dll.lnk tests/mmsystem/mmc.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/mmsystem/mmc.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/tests/mmc.map file { $(MMCOBJS) }
# MMSYSLOG.DLL and the logging pass-through device drivers MDS*.DRV
# (tests/mmsystem/mdl.c, mds.c): the variants differ in module name and
# exports only (W wave with MIDI, F Creative's FM, M MIDI out, A aux, C MCI,
# X all: SB16.DRV)
MDSVARS := MDSW1 MDSW2 MDSW3 MDSW4 MDSF1 MDSM1 MDSM2 MDSM3 MDSA1 MDSC1 MDSC2 MDSC3 MDSC4 \
           MDSX1 MDSX2 MDSX3
mmprobes: $(DOS)/MMSYSLOG.DLL $(patsubst %,$(DOS)/%.DRV,$(MDSVARS))
$(B)/mmsystem/tests/mdl.o: tests/mmsystem/mdl.c tests/mmsystem/mdl.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/mmsystem/tests/mds.o: tests/mmsystem/mds.c tests/mmsystem/mdl.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
MDLOBJS := $(B)/tests/dllstart.o $(B)/mmsystem/tests/mdl.o $(B)/mmsystem/tests/mdlent.o
$(DOS)/MMSYSLOG.DLL: $(MDLOBJS) tests/apps/dll.lnk tests/mmsystem/mdl.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/mmsystem/mdl.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/tests/mdl.map file { $(MDLOBJS) }
MDSOBJS := $(B)/tests/dllstart.o $(B)/mmsystem/tests/mds.o $(B)/mmsystem/tests/mdsent.o
$(DOS)/MDS%.DRV: $(MDSOBJS) tests/apps/dll.lnk tests/mmsystem/mds.lnk tests/mmsystem/mdsw.lnk \
	    tests/mmsystem/mdsf.lnk tests/mmsystem/mdsm.lnk tests/mmsystem/mdsa.lnk tests/mmsystem/mdsc.lnk \
	    tests/mmsystem/mdsx.lnk
	@mkdir -p $(@D)
	v=$$(echo $* | cut -c1 | tr A-Z a-z); \
	$(WLINK) @tests/mmsystem/mds$$v.lnk option modname=MDS$* name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/tests/mds$*.map file { $(MDSOBJS) }
# MCT.DRV, the MCI probe's test MCI driver (tests/mmsystem/mct.c), with
# its command table (mct.rc)
mmprobes: $(DOS)/MCT.DRV
$(B)/mmsystem/tests/mct.o: tests/mmsystem/mct.c tests/mmsystem/mdl.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(B)/mmsystem/tests/mct.res: tests/mmsystem/mct.rc
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -fo=$@ $<
MCTOBJS := $(B)/tests/dllstart.o $(B)/mmsystem/tests/mct.o $(B)/mmsystem/tests/mdsent.o
$(DOS)/MCT.DRV: $(MCTOBJS) $(B)/mmsystem/tests/mct.res tests/apps/dll.lnk tests/mmsystem/mds.lnk \
	    tests/mmsystem/mct.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/mmsystem/mct.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/mmsystem/tests/mct.map option resource=$(B)/mmsystem/tests/mct.res \
	    file { $(MCTOBJS) }
# ---- the timer probe (tests/timer/; ARCHITECTURE.md 21): TPR.EXE, its
# callback DLL TPC.DLL and the logging pass-through driver TMS.DRV;
# tests/timer/trun.sh, tests/timer/tscen.sh ----
timerprobes: $(DOS)/TPR.EXE $(DOS)/TPC.DLL $(DOS)/TMS.DRV
dos: timerprobes
.PHONY: timerprobes
$(B)/timer/tests/%.o: tests/timer/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/timer/tests/tpr.o: tests/timer/tpr.c tests/timer/tpc.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -fo=$@ $<
$(B)/timer/tests/%.o: tests/timer/%.c tests/timer/tpc.h
	@mkdir -p $(@D)
	$(WCC) $(TCFLAGS) -zu -fo=$@ $<
$(DOS)/TPR.EXE: $(B)/tests/start.o $(B)/timer/tests/tpr.o $(B)/timer/tests/tprs.o tests/apps/app.lnk tests/timer/tpr.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/timer/tpr.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/timer/tests/tpr.map file { $(B)/tests/start.o $(B)/timer/tests/tpr.o $(B)/timer/tests/tprs.o }
$(DOS)/TPC.DLL: $(B)/tests/dllstart.o $(B)/timer/tests/tpc.o $(B)/timer/tests/tpcs.o tests/apps/dll.lnk tests/timer/tpc.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/timer/tpc.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/timer/tests/tpc.map file { $(B)/tests/dllstart.o $(B)/timer/tests/tpc.o $(B)/timer/tests/tpcs.o }
$(DOS)/TMS.DRV: $(B)/tests/dllstart.o $(B)/timer/tests/tms.o $(B)/timer/tests/tmsent.o tests/apps/dll.lnk tests/timer/tms.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/timer/tms.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/timer/tests/tms.map file { $(B)/tests/dllstart.o $(B)/timer/tests/tms.o $(B)/timer/tests/tmsent.o }
# OLESVR.DLL: w16's OLE 1.0 server library (ole/olesvr/); large model,
# SS != DS (it runs on its callers' stacks)
OSVCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -nt=OLESVR_TEXT -i=$(WATCOM)/h -i=$(WATCOM)/h/win
OSVOBJS   := $(B)/olesvr/entry.o $(B)/olesvr/util.o $(B)/olesvr/svr.o $(B)/olesvr/doc.o
olesvr: $(DOS)/OLESVR.DLL
dos: $(DOS)/OLESVR.DLL
.PHONY: olesvr
$(B)/olesvr/%.o: ole/olesvr/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/olesvr/%.o: ole/olesvr/%.c ole/olesvr/olesvr.h
	@mkdir -p $(@D)
	$(WCC) $(OSVCFLAGS) -fo=$@ $<
$(DOS)/OLESVR.DLL: $(OSVOBJS) ole/olesvr/olesvr.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @ole/olesvr/olesvr.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/olesvr/olesvr.map file { $(OSVOBJS) }
	python3 tools/nemark.py $@ 3.10
