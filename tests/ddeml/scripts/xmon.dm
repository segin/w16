; exploration (raw values, not compared): the monitor event blocks
start M
M: raw on
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
disc c1
conn Nobody Topic
B: uninit
uninit
M: uninit
quit B
quit M
