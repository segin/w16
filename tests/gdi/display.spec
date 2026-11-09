# The display driver interface as GDI and USER call it (GD1, the logging
# DISPLAY stand-in; tools/mkgdlog.py). One line per export of VGA.DRV:
#   ordinal  NAME  mode  params
# mode: log  = record the call and its result (params must be complete)
#       raw  = record 8 stack words, then jump (parameters not documented)
#       pass = jump straight through, record nothing (called at interrupt
#              time: the cursor functions)
# params, in declaration order (Pascal: pushed left to right): w = word,
# l = dword, p = far pointer. Source: Microsoft Windows 3.1 Device Driver
# Adaptation Guide (DDK documentation), the reference page of each
# function; ordinals and names as VGA.DRV exports them (NE tables).
# UNVERIFIED: Enable's parameters (the reference page for the display
# Enable was not available; GD1 logs its stack words to check).
1   BITBLT            log  p w w p w w w w l p p
2   COLORINFO         log  p l p
3   CONTROL           log  p w p p
4   DISABLE           log  p
5   ENABLE            log  p w p p p
6   ENUMDFONTS        log  p p p p
7   ENUMOBJ           log  p w p p
8   OUTPUT            log  p w w p p p p p
9   PIXEL             log  p w w l p
10  REALIZEOBJECT     log  p w p p p
11  STRBLT            log  p w w p p w p p p
12  SCANLR            log  p w w l w
13  DEVICEMODE        log  w w p p
14  EXTTEXTOUT        log  p w w p p w p p p p p w
15  GETCHARWIDTH      log  p p w w p p p
16  DEVICEBITMAP      log  p w p p
17  FASTBORDER        log  p w w l p p p p
18  SETATTRIBUTE      log  p w w w
19  DEVICEBITMAPBITS  log  p w w w p p p p
20  CREATEBITMAP      log
21  DIBSCREENBLT      log  p w w w w p p p p p
90  DO_POLYLINES      raw
91  DO_SCANLINES      raw
92  SAVESCREENBITMAP  log  p w
101 INQUIRE           pass
102 SETCURSOR         pass
103 MOVECURSOR        pass
104 CHECKCURSOR       pass
400 PEXTTEXTOUT       raw
401 PSTRBLT           raw
402 REXTTEXTOUT       raw
403 RSTRBLT           raw
500 USERREPAINTDISABLE raw
