; a DDEML client driving Program Manager (raw DDE server, PROGMAN.EXE):
; connect, groups, CreateGroup/AddItem/ShowGroup/DeleteItem through
; XTYP_EXECUTE, a request, an advise it refuses, ExitProgman
run PROGMAN.EXE
wait 3000
init 0
conn PROGMAN PROGMAN
info c.
xact c1 request Groups CF_TEXT - 5000
xact c1 exec 0 CF_TEXT text:[CreateGroup(DdeTest)] 5000
xact c1 exec 0 CF_TEXT "text:[AddItem(notepad.exe,DDE Notepad)]" 5000
xact c1 exec 0 CF_TEXT "text:[AddItem(calc.exe,DDE Calc,calc.exe,0)]" 5000
xact c1 exec 0 CF_TEXT text:[ShowGroup(DdeTest,1)] 5000
xact c1 exec 0 CF_TEXT "text:[DeleteItem(DDE Calc)]" 5000
xact c1 exec 0 CF_TEXT text:[Bogus(1)] 5000
lasterr
xact c1 request Groups CF_TEXT - 5000
xact c1 request DdeTest CF_TEXT - 5000
xact c1 advstart Groups CF_TEXT - 5000
lasterr
xact c1 exec 0 CF_TEXT text:[CreateGroup(DdeTest2)][DeleteGroup(DdeTest2)] async
wait 1000
conn 0 PROGMAN
connlist PROGMAN 0
next l1
disclist l1
xact c1 exec 0 CF_TEXT text:[ExitProgman(1)] 5000
lasterr
wait 2000
disc c1
uninit
