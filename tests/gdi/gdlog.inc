; gdlog.inc - segments shared by the logging stand-ins (gdlog.asm and the
; stubs tools/mkgdlog.py generates)
segment _NULL  public align=16 class=BEGDATA use16
segment _DATA  public align=2  class=DATA use16
segment CONST  public align=2  class=DATA use16
segment CONST2 public align=2  class=DATA use16
segment _BSS   public align=2  class=BSS  use16
group DGROUP _NULL _DATA CONST CONST2 _BSS
segment _TEXT public align=16 class=CODE use16
%ifndef GDLOG_MAIN
extern gdl_common
%else
extern gdl_realtab
%endif
