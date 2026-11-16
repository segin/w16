; connections: initiate filtering, wildcards, lists, reconnect, self,
; context, disconnects from either side
start B
start C
init 0
B: init 0
C: init 0
conn Svc Topic
lasterr
B: ns Svc reg
C: ns Svc2 reg
conn Svc Topic
info c1
conn Other Topic
lasterr
B: ns 0 off
conn Other Topic
lasterr
B: ns 0 on
B: ret CONNECT 0
conn Svc Topic
lasterr
B: ret CONNECT 1
conn Svc 0
lasterr
B: ret WILDCONNECT pairs:Svc/TopA,Svc/TopB
C: ret WILDCONNECT pairs:Svc2/TopC
conn Svc 0
info c.
conn 0 Topic
conn 0 0
connlist 0 0
next l1
info c.
next l1 c.
next l1 c.
next l1 c.
lasterr
connlist 0 0 l1
next l2
lasterr
disclist l1
lasterr
disclist l2
conn Svc Topic cc
info c.
B: info c1
cbdo DISCONNECT reconn c.
B: disc c1
info c1
reconn c1
lasterr
wins
conn Svc Topic
B: wins
disc c1
disc c1
lasterr
ns Self reg
conn Self Topic
info c.
inst 1
init 1000
ns Self reg
inst 0
conn Self Topic
uninit
inst 1
uninit
B: uninit
C: uninit
quit B
quit C
