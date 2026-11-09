# ---- COMMDLG.DLL: the common dialogs (shell/commdlg; ARCHITECTURE.md 17),
# included by the Makefile. Two builds from one source tree:
#   $(DOS)/COMMDLG.DLL     Windows 3.x build (marked 3.0, as the original), as SYSTEM\COMMDLG.DLL
#   $(WIN2)/COMMDLG.DLL    Windows 2.x build (W16_WIN2: 8086 code, fixed
#                          segments, its own combo and owner-drawn lists)
# and CD1.EXE, the probe (tests/apps/cd1.c; tests/commdlgscen.sh).

CDCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -zp1 \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win -ishell/commdlg
CDSRC    := entry util file filelist find color font print
CDOBJS   := $(CDSRC:%=$(B)/commdlg/%.o)
CDOBJS2  := $(CDSRC:%=$(B)/commdlg2/%.o)
CDHDRS   := shell/commdlg/commdlg.h shell/commdlg/cdlgs.h
CDART    := $(B)/commdlg/art/drives.bmp

.PHONY: commdlg commdlg2
commdlg: $(DOS)/COMMDLG.DLL
commdlg2: $(WIN2)/COMMDLG.DLL
dos: $(DOS)/COMMDLG.DLL
shell2: $(WIN2)/COMMDLG.DLL

$(CDART) &: tools/mkcdart.py
	@mkdir -p $(@D)
	python3 tools/mkcdart.py $(@D)

$(B)/commdlg/%.o: shell/commdlg/%.c $(CDHDRS)
	@mkdir -p $(@D)
	$(WCC) $(CDCFLAGS) -fo=$@ $<
$(B)/commdlg/%.o: shell/commdlg/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -o $@ $<
$(B)/commdlg/commdlg.res: shell/commdlg/commdlg.rc $(CDHDRS) $(CDART)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -i=$(WATCOM)/h/win -i=shell/commdlg -i=$(B)/commdlg/art \
	    -fo=$@ $<
$(DOS)/COMMDLG.DLL: $(CDOBJS) $(B)/commdlg/commdlg.res shell/commdlg/commdlg.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/commdlg/commdlg.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/commdlg/commdlg.map \
	    option resource=$(B)/commdlg/commdlg.res file { $(CDOBJS) }
	python3 tools/nemark.py $@ 3.0

$(B)/commdlg2/%.o: shell/commdlg/%.c $(CDHDRS)
	@mkdir -p $(@D)
	$(WCC) $(CDCFLAGS) -0 -dW16_WIN2 -fo=$@ $<
$(B)/commdlg2/%.o: shell/commdlg/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NFLAGS) -dW16_WIN2 -o $@ $<
$(B)/commdlg2/commdlg.res: shell/commdlg/commdlg.rc $(CDHDRS) $(CDART)
	@mkdir -p $(@D)
	$(WRC) -q -r -bt=windows -dW16_WIN2 -i=$(WATCOM)/h/win -i=shell/commdlg \
	    -i=$(B)/commdlg/art -fo=$@ $<
$(WIN2)/COMMDLG.DLL: $(CDOBJS2) $(B)/commdlg2/commdlg.res shell/commdlg/commdlg2.lnk \
                     shell/commdlg/commdlg.lnk tools/nemark.py
	@mkdir -p $(@D)
	$(WLINK) @shell/commdlg/commdlg2.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/commdlg2/commdlg.map \
	    option resource=$(B)/commdlg2/commdlg.res file { $(CDOBJS2) }
	python3 tools/nemark.py $@ 2.0

# the install tree: SYSTEM\COMMDLG.DLL
install-tree: $(W16SYS)/COMMDLG.DLL
$(W16SYS)/COMMDLG.DLL: $(DOS)/COMMDLG.DLL
	@mkdir -p $(@D)
	cp $< $@

# CD1.EXE, the probe
dos: $(DOS)/CD1.EXE
$(DOS)/CD1.EXE: $(B)/tests/start.o $(B)/tests/cd1.o tests/apps/app.lnk
	@mkdir -p $(@D)
	$(WLINK) @tests/apps/app.lnk name $@ libpath $(WATCOM)/lib286/win \
	    option map=$(B)/tests/cd1.map file { $(B)/tests/start.o $(B)/tests/cd1.o } \
	    library commdlg.lib
