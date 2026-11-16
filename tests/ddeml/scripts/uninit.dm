; DdeUninitialize while conversations, advise loops and transactions
; are open, from either side; the window and string-handle clean-up
start B
start C
init 0
B: init 0
B: hsz Kept
B: ns Svc reg
conn Svc Topic
xact c1 advstart Item CF_TEXT - 2000
xact c1 request Item CF_TEXT - async
B: ret REQUEST block
xact c1 request Item CF_TEXT - async
B: atomq Kept
B: uninit
B: atomq Kept
B: atomq Svc
info c1
lasterr
xact c1 request Item CF_TEXT - 2000
lasterr
C: init 0
C: ns Svc2 reg
conn Svc2 Topic
conn Svc2 Topic2
xact c2 advstart Item CF_TEXT - 2000
uninit
C: wins
C: uninit
C: wins
wins
quit B
quit C
