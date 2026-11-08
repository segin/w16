# w16 build. Requires OpenWatcom v2 (WATCOM, default /opt/watcom), nasm,
# and a host C compiler. `make test` additionally needs DOSBox and HDPMI16
# (tools/fetch-hdpmi.sh).

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
           $(B)/kernel/thunk.o $(B)/kernel/task.o
BOBJS   := $(B)/boot/boot0.o $(B)/boot/boot.o
TESTS   := TK1 TK2
# tools that run inside a (reference) Windows: tests/refrun.sh
WINTOOLS := SNAP TW2 TEXP3 TEXP2
SHELL_PM := $(B)/shell/start.o $(B)/shell/progman/main.o \
            $(B)/shell/progman/group.o $(B)/shell/progman/dialogs.o \
            $(B)/shell/progman/util.o $(B)/shell/progman/hotkey.o
ICONS    := $(B)/shell/res/progman.ico $(B)/shell/res/group.ico \
            $(B)/shell/res/defitem.ico

.SECONDARY:

.PHONY: all host dos shell shell2 test clean
all: host dos shell

host: $(B)/host/nedump
dos: $(DOS)/KRNL286.EXE $(TESTS:%=$(DOS)/%.EXE) $(WINTOOLS:%=$(DOS)/%.EXE) \
     $(DOS)/KEYFEED.COM $(DOS)/POWEROFF.COM $(B)/hdd/MBR.BIN

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
$(B)/shell/progman/%.o: shell/progman/%.c shell/progman/progman.h
	@mkdir -p $(@D)
	$(WCC) $(SCFLAGS) -fo=$@ $<
$(ICONS) &: tools/mkicons.py
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
$(B)/shell2/progman/%.o: shell/progman/%.c shell/progman/progman.h
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
