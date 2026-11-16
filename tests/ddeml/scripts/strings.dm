; string handles: creation, case, code pages, use counts against the
; global atoms, query, compare, free/keep, before and after init
hsz Early
lasterr
init 0
hsz Alpha
hsz alpha
hsz ALPHA
atomq Alpha
hsz Beta 1004
hsz Gamma 437
hsz Delta 850
hsz Eps 1200
hsz Zeta -1
lasterr
hsz ""
lasterr
hsz "a string with spaces and more than a few characters in it, longer than sixty-four characters for the log table"
lasterr
hszval Alpha
qstr Alpha
qstr Alpha 0
qstr Alpha 1
qstr Alpha 2
qstr Alpha 5
qstr Alpha 6
qstr 0
lasterr
cmp Alpha Beta
cmp Beta Alpha
cmp Alpha alpha
cmp 0 0
cmp 0 Alpha
keephsz Alpha
atomq Alpha
freehsz Alpha
atomq Alpha
freehsz Alpha
atomq Alpha
freehsz Alpha
atomq Alpha
freehsz Alpha
atomq Alpha
lasterr
freehsz 0
lasterr
keephsz 0
lasterr
inst 1
init 0
hsz Beta
atomq Beta
freehsz Beta
atomq Beta
uninit
atomq Beta
inst 0
atomq Beta
hsz Theta
atomq Theta
uninit
atomq Theta
atomq Gamma
hsz Late
lasterr
qstr Gamma
lasterr
