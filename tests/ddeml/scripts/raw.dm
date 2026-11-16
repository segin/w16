; DDEML against raw DDE: a raw client talking to a DDEML server and a
; DDEML client talking to a raw server (acks, nacks, busy, no answer,
; advise data, terminate from the raw side)
start B
start R
B: init 0
B: ns Svc reg
R: rinit Svc Topic
R: rreq Item CF_TEXT
R: rreq Item Private
R: rpoke Item CF_TEXT rawpoke
R: rexec [rawcmd]
R: radv Item CF_TEXT 0
R: radv Item2 CF_TEXT 8000
R: radv Item3 CF_TEXT 4000
R: radv Item4 CF_TEXT C000
B: postadv Topic Item
B: postadv Topic Item2
B: postadv Topic Item3
B: postadv Topic Item4
R: rack busy
B: postadv Topic Item2
R: rack pos
R: runadv Item CF_TEXT
R: runadv Never CF_TEXT
B: ret REQUEST 0
R: rreq Item CF_TEXT
B: ret REQUEST item
R: rterm
R: rinit Svc 0
R: rterm
R: rinit 0 0
R: rterm
R: rinit svc topic
R: rterm
B: uninit
R: rserve RawSvc RawTopic
init 0
conn RawSvc RawTopic
info c1
xact c1 request Item CF_TEXT - 2000
xact c1 poke Item CF_TEXT text:topraw 2000
xact c1 exec 0 CF_TEXT text:[toraw] 2000
xact c1 advstart Item CF_TEXT - 2000
xact c1 advstart+ackreq ItemA CF_TEXT - 2000
xact c1 advstart+nodata ItemN CF_TEXT - 2000
R: radvdata Item CF_TEXT advtext
R: radvdata ItemA CF_TEXT advacked
R: radvdata ItemN CF_TEXT nodata
R: rack neg
xact c1 request Item CF_TEXT - 2000
lasterr
xact c1 poke Item CF_TEXT text:x 2000
lasterr
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
xact c1 advstart Item5 CF_TEXT - 2000
lasterr
R: rack busy
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
R: rack none
xact c1 exec 0 CF_TEXT text:[x] 1000
lasterr
xact c1 request Item CF_TEXT - 1000
lasterr
xact c1 advstop Item CF_TEXT - 1000
lasterr
R: rack pos
conn 0 RawTopic
conn RawSvc 0
connlist 0 0
next l1
disclist l1
R: rterm
wait 300
info c1
lasterr
uninit
quit B
quit R
