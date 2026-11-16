# CURREC (tests/gdi/currec.asm): a DISPLAY stand-in that only counts.
# Generated from display.spec: every export passed straight through,
# but "count" ones: each call tallied, and the cursor entries (102-104)
# recorded with their time, caller and arguments (tests/apps/cr1.c reads
# them). Interrupt time is fine: nothing but fixed data is touched.
1   BITBLT            count
2   COLORINFO         count
3   CONTROL           count
4   DISABLE           count
5   ENABLE            count
6   ENUMDFONTS        count
7   ENUMOBJ           count
8   OUTPUT            count
9   PIXEL             count
10  REALIZEOBJECT     count
11  STRBLT            count
12  SCANLR            count
13  DEVICEMODE        count
14  EXTTEXTOUT        count
15  GETCHARWIDTH      count
16  DEVICEBITMAP      count
17  FASTBORDER        count
18  SETATTRIBUTE      count
19  DEVICEBITMAPBITS  count
20  CREATEBITMAP      count
21  DIBSCREENBLT      count
90  DO_POLYLINES      pass
91  DO_SCANLINES      pass
92  SAVESCREENBITMAP  count
101 INQUIRE           pass
102 SETCURSOR         count
103 MOVECURSOR        count
104 CHECKCURSOR       count
400 PEXTTEXTOUT       pass
401 PSTRBLT           pass
402 REXTTEXTOUT       pass
403 RSTRBLT           pass
500 USERREPAINTDISABLE pass
