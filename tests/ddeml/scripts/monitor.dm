; a DDEML monitor (APPCLASS_MONITOR with every MF_ flag) in its own task
; watching string handles, conversations, transactions, links, errors
start M
start B
M: init 7F000001
init 0
B: init 0
hsz MonStr
freehsz MonStr
B: ns Svc reg
conn Svc Topic
xact c1 request Item CF_TEXT - 2000
xact c1 advstart Item CF_TEXT - 2000
B: postadv Topic Item
xact c1 advstop Item CF_TEXT - 2000
B: ret POKE nack
xact c1 poke Item CF_TEXT text:x 2000
xact c1 exec 0 CF_TEXT text:[mon] 2000
disc c1
conn Nobody Topic
B: ns Svc unreg
B: uninit
uninit
M: uninit
quit B
quit M
