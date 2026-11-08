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
NFLAGS  := -f obj -Ikernel/inc/

KOBJS   := $(B)/kernel/kentry.o $(B)/kernel/kmain.o $(B)/kernel/util.o \
           $(B)/kernel/dpmi.o $(B)/kernel/global.o $(B)/kernel/module.o \
           $(B)/kernel/thunk.o $(B)/kernel/task.o
BOBJS   := $(B)/boot/boot0.o $(B)/boot/boot.o
TESTS   := TK1 TK2
# tools that run inside a (reference) Windows: tests/refrun.sh
WINTOOLS := SNAP

.SECONDARY:

.PHONY: all host dos test clean
all: host dos

host: $(B)/host/nedump
dos: $(DOS)/KRNL286.EXE $(TESTS:%=$(DOS)/%.EXE) $(WINTOOLS:%=$(DOS)/%.EXE)

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

$(DOS)/SNAP.EXE: $(B)/tests/start.o $(B)/tests/snap.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/snap.map file { $(B)/tests/start.o $(B)/tests/snap.o }

test: dos
	tests/run-tests.sh

clean:
	rm -rf $(B)
