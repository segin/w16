; data handles: create, add, get, access, free, owned (a handle freed
; twice crashes 3.1's DDEML: never done here)
mkdata d0 early Item CF_TEXT
lasterr
init 0
mkdata d1 hello Item CF_TEXT
getdata d1
getdata d1 3
getdata d1 100 2
getdata d1 100 6
getdata d1 100 40
lasterr
access d1
unaccess d1
unaccess d1
lasterr
adddata d1 world 6
getdata d1
adddata d1 "far beyond the end of the block" 40
getdata d1 100 40
access d1
unaccess d1
mkdata d3 null Item CF_TEXT
getdata d3 0
mkdata d4 "" 0 CF_TEXT
getdata d4
mkdata d5 owned Item CF_TEXT own
getdata d5
mkdata d6 other Item Private
getdata d6
mkdata d7 bad Item CF_TEXT 2
lasterr
freedata d1
lasterr
freedata d5
lasterr
freedata 0
lasterr
uninit
lasterr
