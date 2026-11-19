; exploration (raw values, not compared): the monitor event blocks, the
; monitor's windows, and which INITIATE sends a monitor sees
start M
M: raw on
start B
start C
M: init 7F000001
M: wins
init 0
B: init 0
C: init 0
hsz MonStr
freehsz MonStr
B: ns Svc reg
conn Svc Topic
info c1
B: info c1
xact c1 request Item CF_TEXT - 2000
xact c1 advstart Item CF_TEXT - 2000
B: postadv Topic Item
xact c1 advstop Item CF_TEXT - 2000
xact c1 request Item CF_TEXT - 2000
xact c1 poke Item2 CF_TEXT text:x 2000
xact c1 exec 0 CF_TEXT text:[mon] 2000
xact c1 request Item2 CF_TEXT - 2000
B: ret CONNECT 0
conn Svc Topic
B: ret CONNECT 1
C: rserve Raw Topic
conn Raw Topic
connlist Svc Topic
keephsz Item
freehsz Item
freehsz Item
disc c1
wait 300
B: uninit
uninit
C: rterm
M: uninit
quit B
quit C
quit M
