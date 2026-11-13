# ---- COMMDLG.DLL: the common dialogs (shell/commdlg; ARCHITECTURE.md 17),
# included by the Makefile. Windows 3.x only (marked 3.0, as the original
# is), installed as SYSTEM\COMMDLG.DLL; and CD1.EXE, the probe
# (tests/apps/cd1.c; tests/commdlgscen.sh).

CDCFLAGS := -q -bt=windows -ml -zu -2 -s -zl -ox -wx -we -zp1 \
            -i=$(WATCOM)/h -i=$(WATCOM)/h/win -ishell/commdlg
CDSRC    := entry util file filelist find color font print
CDOBJS   := $(CDSRC:%=$(B)/commdlg/%.o)
CDHDRS   := shell/commdlg/commdlg.h shell/commdlg/cdlgs.h
CDART    := $(B)/commdlg/art/drives.bmp

.PHONY: commdlg
commdlg: $(DOS)/COMMDLG.DLL
dos: $(DOS)/COMMDLG.DLL

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
