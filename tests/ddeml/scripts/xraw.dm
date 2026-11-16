; exploration (raw values, not compared): instance-specific names,
; window handles behind conversations, data handle values
raw on
start B
B: raw on
init 0
B: init 0
B: ns Svc reg
B: hszval Svc
wins
B: wins
conn Svc Topic
cwin c1
B: cwin c1
info c1
B: info c1
conn Svc Topic
cwin c3
xact c1 request Item CF_TEXT - async
xact c1 request Item CF_TEXT - async
xact c3 request Item CF_TEXT - async
wait 300
mkdata d1 abc Item CF_TEXT
mkdata d2 abc Item CF_TEXT own
access d2
unaccess d2
B: wins
wins
disc c1
disc c3
B: uninit
uninit
quit B
